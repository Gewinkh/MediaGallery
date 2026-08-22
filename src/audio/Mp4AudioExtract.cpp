#include "audio/Mp4AudioExtract.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QVector>

#include <limits>

// ─────────────────────────────────────────────────────────────────────────────
//  Aufbau einer MP4-Datei, soweit hier gebraucht:
//
//      ftyp                    Hülle/Marken
//      moov                    BESCHREIBUNG (klein)
//        mvhd                  Zeitbasis des Films
//        trak                  je Spur eine
//          tkhd
//          edts/elst           Vorlauf/Schnitt (wird 1:1 übernommen)
//          mdia
//            mdhd              Zeitbasis der SPUR
//            hdlr              'soun' = Ton, 'vide' = Bild
//            minf/stbl         die Tabellen: wo liegt welcher Block
//      mdat                    DATEN aller Spuren, verzahnt
//
//  Herauskopieren heißt: aus `stbl` die Bereiche der Tonspur ausrechnen, ihre
//  Bytes hintereinander in ein neues `mdat` schreiben und eine frische
//  Beschreibung dazu bauen. Übernommen wird `stsd` (der Sample-Eintrag mit
//  `esds`/AudioSpecificConfig - ohne ihn wäre der Ton nicht dekodierbar) und
//  `edts`; neu entstehen die Tabellen, weil sich die Lage der Bytes ändert.
//
//  EIN Chunk für alles: `stsc` bekommt einen Eintrag, `stco` einen Offset. Das
//  ist zulässig und die kleinste korrekte Form - die Verzahnung mit einer
//  Bildspur, für die es die Chunks gab, existiert in einer Audiodatei nicht.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// ── Schutzgrenzen (Regel 21: Fremddaten werden nie geglaubt) ─────────────────
constexpr qint64 kMaxMoov     = 64ll << 20;   // 64 MB Beschreibung ist absurd viel
constexpr qint64 kMaxSamples  = 20'000'000;   // ~30 h AAC bei 43 Blöcken/s
constexpr int    kMaxTopBoxes = 4096;         // Endlosschleife bei Müll verhindern
constexpr qint64 kCopyChunk   = 1 << 18;      // 256 kB Kopierpuffer

constexpr quint32 fcc(const char (&s)[5]) {
    return (quint32(quint8(s[0])) << 24) | (quint32(quint8(s[1])) << 16)
         | (quint32(quint8(s[2])) <<  8) |  quint32(quint8(s[3]));
}

constexpr quint32 kFtyp = fcc("ftyp"), kMoov = fcc("moov"), kMdat = fcc("mdat");
constexpr quint32 kMoof = fcc("moof"), kMvex = fcc("mvex"), kMvhd = fcc("mvhd");
constexpr quint32 kTrak = fcc("trak"), kMdia = fcc("mdia"), kHdlr = fcc("hdlr");
constexpr quint32 kMdhd = fcc("mdhd"), kMinf = fcc("minf"), kStbl = fcc("stbl");
constexpr quint32 kStsd = fcc("stsd"), kStts = fcc("stts"), kStsc = fcc("stsc");
constexpr quint32 kStsz = fcc("stsz"), kStz2 = fcc("stz2"), kStco = fcc("stco");
constexpr quint32 kCo64 = fcc("co64"), kEdts = fcc("edts"), kDinf = fcc("dinf");
constexpr quint32 kDref = fcc("dref"), kUrl  = fcc("url "), kSoun = fcc("soun");
//  Fragmentierte Dateien: die Sample-Tabellen stecken je Fragment in `traf`.
constexpr quint32 kTkhd = fcc("tkhd"), kTraf = fcc("traf"), kTfhd = fcc("tfhd");
constexpr quint32 kTrun = fcc("trun"), kTrex = fcc("trex");
constexpr int    kMaxMoofs = 200'000;         // Fragmente je Datei
constexpr qint64 kMaxMoofSize = 8ll << 20;    // ein einzelnes `moof`

inline quint16 be16(const uchar* p) { return quint16((quint32(p[0]) << 8) | p[1]); }
inline quint32 be32(const uchar* p) {
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
         | (quint32(p[2]) <<  8) |  quint32(p[3]);
}
inline quint64 be64(const uchar* p) {
    return (quint64(be32(p)) << 32) | be32(p + 4);
}

// ── Box-Läufer über einen SCHON GELESENEN Puffer (moov) ─────────────────────
struct Box {
    quint32 type = 0;
    qint64  off  = 0;   // Beginn der Box im Puffer
    qint64  hdr  = 0;   // 8 oder 16 (64-Bit-Größe)
    qint64  size = 0;   // Gesamtgröße inkl. Kopf
    qint64  payload() const { return off + hdr; }
    qint64  end()     const { return off + size; }
    qint64  payloadSize() const { return size - hdr; }
};

//  Nächste Box ab `pos` innerhalb von [pos, end). Prüft JEDE Grenze, bevor
//  gelesen wird; `false` heißt „hier steht keine gültige Box mehr".
bool nextBox(const QByteArray& b, qint64 pos, qint64 end, Box* out) {
    if (pos < 0 || end > b.size() || end - pos < 8) return false;
    const uchar* p = reinterpret_cast<const uchar*>(b.constData());
    qint64 size = qint64(be32(p + pos));
    const quint32 type = be32(p + pos + 4);
    qint64 hdr = 8;
    if (size == 1) {
        if (end - pos < 16) return false;
        const quint64 s64 = be64(p + pos + 8);
        if (s64 > quint64(std::numeric_limits<qint64>::max() / 2)) return false;
        size = qint64(s64);
        hdr  = 16;
    } else if (size == 0) {
        size = end - pos;                     // „bis zum Ende des Elternteils"
    }
    if (size < hdr || pos > end - size) return false;
    *out = Box { type, pos, hdr, size };
    return true;
}

//  Erstes Kind `type` in [start,end). Nicht rekursiv - der Weg wird bewusst
//  Schritt für Schritt gegangen, damit ein Treffer in der falschen Ebene
//  (z. B. `stsd` einer Bildspur) nicht versehentlich mitgenommen wird.
bool child(const QByteArray& b, qint64 start, qint64 end, quint32 type, Box* out) {
    Box box;
    qint64 pos = start;
    while (nextBox(b, pos, end, &box)) {
        if (box.type == type) { *out = box; return true; }
        pos = box.end();
        if (box.size <= 0) return false;
    }
    return false;
}

bool childOfBox(const QByteArray& b, const Box& parent, quint32 type, Box* out) {
    return child(b, parent.payload(), parent.end(), type, out);
}

//  Volles Feld einer Tabelle lesen: `n` Werte à 4 Byte ab `off`, mit Prüfung.
bool haveBytes(const QByteArray& b, qint64 off, qint64 n) {
    return off >= 0 && n >= 0 && off <= b.size() - n;
}

// ── Was aus der Quelle gebraucht wird ───────────────────────────────────────
struct SttsEntry { quint32 count; quint32 delta; };

struct Track {
    quint32 movieTimescale = 1000;   // Zeitbasis des FILMS (edts rechnet darin)
    quint32 mediaTimescale = 0;
    quint64 mediaDuration  = 0;
    QByteArray stsd;                 // GANZE Box, wird 1:1 übernommen
    QByteArray edts;                 // GANZE Box (oder leer)
    QVector<SttsEntry> stts;
    QVector<qint64>    offsets;      // je Sample: Lage in der QUELLE
    QVector<quint32>   sizes;        // je Sample: Länge
    quint32 codec   = 0;
    int     rate    = 0;
    int     channels = 0;
    qint64  totalBytes = 0;
};

// ── Die oberste Ebene der DATEI abgehen, ohne sie zu laden ──────────────────
//  Nur Kopf-Bytes werden gelesen; `moov` kann hinter einem 12-MB-`mdat` liegen.
bool scanTopLevel(QFile& f, qint64 fileSize, Box* moov, bool* fragmented,
                  bool* sawFtyp, QVector<Box>* moofs = nullptr) {
    qint64 pos = 0;
    int guard = 0;
    *fragmented = false;
    *sawFtyp = false;
    bool found = false;
    while (pos < fileSize && guard++ < kMaxTopBoxes) {
        if (fileSize - pos < 8) break;
        if (!f.seek(pos)) return false;
        char hdrBuf[16];
        if (f.read(hdrBuf, 8) != 8) return false;
        const uchar* p = reinterpret_cast<const uchar*>(hdrBuf);
        qint64 size = qint64(be32(p));
        const quint32 type = be32(p + 4);
        qint64 hdr = 8;
        if (size == 1) {
            if (fileSize - pos < 16) return false;
            if (f.read(hdrBuf + 8, 8) != 8) return false;
            const quint64 s64 = be64(reinterpret_cast<const uchar*>(hdrBuf) + 8);
            if (s64 > quint64(std::numeric_limits<qint64>::max() / 2)) return false;
            size = qint64(s64);
            hdr  = 16;
        } else if (size == 0) {
            size = fileSize - pos;
        }
        if (size < hdr || pos > fileSize - size) return false;

        if (type == kFtyp) *sawFtyp = true;
        if (type == kMoof) {
            *fragmented = true;
            //  Die Lage jedes Fragments merken - die Sample-Offsets rechnen
            //  gleich VON HIER aus (`default-base-is-moof`).
            if (moofs && moofs->size() < kMaxMoofs)
                moofs->append(Box { type, pos, hdr, size });
        }
        if (type == kMoov && !found) {
            *moov = Box { type, pos, hdr, size };
            found = true;
        }
        pos += size;
    }
    return found;
}

// ── Sample-Tabellen einer Spur auflösen ─────────────────────────────────────
//  Aus `stsc` (wie viele Samples je Chunk), `stco`/`co64` (wo der Chunk liegt)
//  und `stsz`/`stz2` (wie lang jedes Sample ist) entsteht die flache Liste
//  „Sample N liegt bei Offset X und ist Y Byte lang".
bool buildSampleList(const QByteArray& m, const Box& stbl, qint64 fileSize,
                     Track* t, Mp4Audio::Result* err) {
    const uchar* p = reinterpret_cast<const uchar*>(m.constData());

    // ── Sample-Größen ───────────────────────────────────────────────────────
    QVector<quint32> sizes;
    Box b;
    if (childOfBox(m, stbl, kStsz, &b)) {
        if (b.payloadSize() < 12) { *err = Mp4Audio::Result::Damaged; return false; }
        const quint32 uniform = be32(p + b.payload() + 4);
        const quint32 count   = be32(p + b.payload() + 8);
        if (count > kMaxSamples) { *err = Mp4Audio::Result::TooLarge; return false; }
        sizes.resize(int(count));
        if (uniform != 0) {
            sizes.fill(uniform);
        } else {
            if (!haveBytes(m, b.payload() + 12, qint64(count) * 4)
                || b.payloadSize() < 12 + qint64(count) * 4) {
                *err = Mp4Audio::Result::Damaged; return false;
            }
            for (quint32 i = 0; i < count; ++i)
                sizes[int(i)] = be32(p + b.payload() + 12 + qint64(i) * 4);
        }
    } else if (childOfBox(m, stbl, kStz2, &b)) {
        //  Kompaktform: 4/8/16 Bit je Größe. Selten, aber zulässig - und wer
        //  sie nicht kennt, liest die Tabelle als Müll.
        if (b.payloadSize() < 12) { *err = Mp4Audio::Result::Damaged; return false; }
        const quint32 fieldSize = p[b.payload() + 7];
        const quint32 count     = be32(p + b.payload() + 8);
        if (count > kMaxSamples) { *err = Mp4Audio::Result::TooLarge; return false; }
        if (fieldSize != 4 && fieldSize != 8 && fieldSize != 16) {
            *err = Mp4Audio::Result::Damaged; return false;
        }
        const qint64 bits  = qint64(count) * fieldSize;
        const qint64 bytes = (bits + 7) / 8;
        if (!haveBytes(m, b.payload() + 12, bytes)
            || b.payloadSize() < 12 + bytes) {
            *err = Mp4Audio::Result::Damaged; return false;
        }
        sizes.resize(int(count));
        for (quint32 i = 0; i < count; ++i) {
            const qint64 base = b.payload() + 12;
            if (fieldSize == 16)      sizes[int(i)] = be16(p + base + qint64(i) * 2);
            else if (fieldSize == 8)  sizes[int(i)] = p[base + i];
            else {
                const quint8 byte = p[base + i / 2];
                sizes[int(i)] = (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
            }
        }
    } else {
        *err = Mp4Audio::Result::Damaged;
        return false;
    }

    // ── Chunk-Offsets ───────────────────────────────────────────────────────
    QVector<qint64> chunkOffsets;
    if (childOfBox(m, stbl, kStco, &b)) {
        if (b.payloadSize() < 8) { *err = Mp4Audio::Result::Damaged; return false; }
        const quint32 n = be32(p + b.payload() + 4);
        if (n > kMaxSamples) { *err = Mp4Audio::Result::TooLarge; return false; }
        if (b.payloadSize() < 8 + qint64(n) * 4) { *err = Mp4Audio::Result::Damaged; return false; }
        chunkOffsets.resize(int(n));
        for (quint32 i = 0; i < n; ++i)
            chunkOffsets[int(i)] = qint64(be32(p + b.payload() + 8 + qint64(i) * 4));
    } else if (childOfBox(m, stbl, kCo64, &b)) {
        if (b.payloadSize() < 8) { *err = Mp4Audio::Result::Damaged; return false; }
        const quint32 n = be32(p + b.payload() + 4);
        if (n > kMaxSamples) { *err = Mp4Audio::Result::TooLarge; return false; }
        if (b.payloadSize() < 8 + qint64(n) * 8) { *err = Mp4Audio::Result::Damaged; return false; }
        chunkOffsets.resize(int(n));
        for (quint32 i = 0; i < n; ++i) {
            const quint64 v = be64(p + b.payload() + 8 + qint64(i) * 8);
            if (v > quint64(std::numeric_limits<qint64>::max())) {
                *err = Mp4Audio::Result::Damaged; return false;
            }
            chunkOffsets[int(i)] = qint64(v);
        }
    } else {
        *err = Mp4Audio::Result::Damaged;
        return false;
    }

    // ── Sample-zu-Chunk ─────────────────────────────────────────────────────
    struct ScEntry { quint32 firstChunk; quint32 perChunk; };
    QVector<ScEntry> sc;
    if (!childOfBox(m, stbl, kStsc, &b) || b.payloadSize() < 8) {
        *err = Mp4Audio::Result::Damaged; return false;
    }
    {
        const quint32 n = be32(p + b.payload() + 4);
        if (n > kMaxSamples) { *err = Mp4Audio::Result::TooLarge; return false; }
        if (b.payloadSize() < 8 + qint64(n) * 12) { *err = Mp4Audio::Result::Damaged; return false; }
        sc.resize(int(n));
        for (quint32 i = 0; i < n; ++i) {
            const qint64 o = b.payload() + 8 + qint64(i) * 12;
            sc[int(i)] = ScEntry { be32(p + o), be32(p + o + 4) };
            //  Die Tabelle MUSS aufsteigen und bei 1 beginnen; sonst liefe die
            //  Auflösung unten in eine falsche Richtung.
            if (sc[int(i)].firstChunk == 0
                || (i > 0 && sc[int(i)].firstChunk < sc[int(i) - 1].firstChunk)) {
                *err = Mp4Audio::Result::Damaged; return false;
            }
        }
    }
    if (sc.isEmpty()) { *err = Mp4Audio::Result::Damaged; return false; }

    // ── Auflösen: je Chunk die Samples hintereinander ───────────────────────
    const int sampleCount = sizes.size();
    t->offsets.reserve(sampleCount);
    t->sizes.reserve(sampleCount);
    int sample = 0;
    for (int c = 0; c < chunkOffsets.size() && sample < sampleCount; ++c) {
        //  Wie viele Samples in diesem Chunk? Der letzte Eintrag, dessen
        //  `firstChunk` (1-basiert) noch <= c+1 ist, gilt.
        quint32 perChunk = 0;
        for (int e = sc.size() - 1; e >= 0; --e) {
            if (sc[e].firstChunk <= quint32(c) + 1) { perChunk = sc[e].perChunk; break; }
        }
        if (perChunk == 0) continue;
        if (perChunk > quint32(kMaxSamples)) { *err = Mp4Audio::Result::TooLarge; return false; }

        qint64 off = chunkOffsets[c];
        for (quint32 s = 0; s < perChunk && sample < sampleCount; ++s, ++sample) {
            const qint64 len = qint64(sizes[sample]);
            //  DER entscheidende Bounds-Check: der Bereich muss ganz in der
            //  Datei liegen. Eine manipulierte Tabelle zeigt sonst irgendwohin.
            if (off < 0 || len < 0 || off > fileSize - len) {
                *err = Mp4Audio::Result::Damaged; return false;
            }
            t->offsets.append(off);
            t->sizes.append(quint32(len));
            t->totalBytes += len;
            off += len;
        }
    }
    if (t->offsets.isEmpty()) { *err = Mp4Audio::Result::Damaged; return false; }

    // ── Zeit-zu-Sample ──────────────────────────────────────────────────────
    if (!childOfBox(m, stbl, kStts, &b) || b.payloadSize() < 8) {
        *err = Mp4Audio::Result::Damaged; return false;
    }
    {
        const quint32 n = be32(p + b.payload() + 4);
        if (n > kMaxSamples) { *err = Mp4Audio::Result::TooLarge; return false; }
        if (b.payloadSize() < 8 + qint64(n) * 8) { *err = Mp4Audio::Result::Damaged; return false; }
        qint64 covered = 0;
        for (quint32 i = 0; i < n; ++i) {
            const qint64 o = b.payload() + 8 + qint64(i) * 8;
            const SttsEntry e { be32(p + o), be32(p + o + 4) };
            t->stts.append(e);
            covered += qint64(e.count);
        }
        //  Die Tabelle beschreibt womöglich MEHR Samples, als wirklich da sind
        //  (abgeschnittene Datei). Dann wird sie auf die vorhandenen gekürzt -
        //  sonst behauptete die Zieldatei eine Dauer, die sie nicht hat.
        if (covered > t->offsets.size()) {
            qint64 left = t->offsets.size();
            QVector<SttsEntry> trimmed;
            for (const SttsEntry& e : std::as_const(t->stts)) {
                if (left <= 0) break;
                const quint32 take = quint32(qMin<qint64>(left, e.count));
                trimmed.append(SttsEntry { take, e.delta });
                left -= take;
            }
            t->stts = trimmed;
        }
    }

    t->mediaDuration = 0;
    for (const SttsEntry& e : std::as_const(t->stts))
        t->mediaDuration += quint64(e.count) * e.delta;
    return true;
}

//  `dref` prüfen: liegen die Daten in DIESER Datei? Ein 'url '-Eintrag mit
//  gesetztem Flag 1 heißt „selbsttragend"; alles andere zeigt nach außen.
bool selfContained(const QByteArray& m, const Box& minf) {
    Box dinf, dref;
    if (!childOfBox(m, minf, kDinf, &dinf)) return true;    // fehlt -> Standard
    if (!childOfBox(m, dinf, kDref, &dref)) return true;
    const uchar* p = reinterpret_cast<const uchar*>(m.constData());
    if (dref.payloadSize() < 8) return false;
    Box e;
    if (!child(m, dref.payload() + 8, dref.end(), kUrl, &e)) return false;
    if (e.payloadSize() < 4) return false;
    return (be32(p + e.payload()) & 0x00FFFFFF) == 1;       // flags == self
}

// ── Schreiben ───────────────────────────────────────────────────────────────
struct Writer {
    QByteArray b;

    void u8 (quint8  v) { b.append(char(v)); }
    void u16(quint16 v) { u8(quint8(v >> 8)); u8(quint8(v)); }
    void u32(quint32 v) { u16(quint16(v >> 16)); u16(quint16(v)); }
    void u64(quint64 v) { u32(quint32(v >> 32)); u32(quint32(v)); }
    void raw(const QByteArray& d) { b.append(d); }
    void zeros(int n) { b.append(QByteArray(n, '\0')); }

    //  Box öffnen: Platzhalter für die Größe, dann der Typ. `close` trägt die
    //  wirkliche Größe nach - so muss sie nirgends im Voraus gerechnet werden.
    qint64 open(quint32 type) {
        const qint64 pos = b.size();
        u32(0);
        u32(type);
        return pos;
    }
    void close(qint64 pos) {
        const quint32 size = quint32(b.size() - pos);
        uchar* p = reinterpret_cast<uchar*>(b.data()) + pos;
        p[0] = uchar(size >> 24); p[1] = uchar(size >> 16);
        p[2] = uchar(size >>  8); p[3] = uchar(size);
    }
    void fullBoxHeader() { u32(0); }        // version 0 + flags 0
};

void writeMatrix(Writer& w) {
    w.u32(0x00010000); w.u32(0); w.u32(0);
    w.u32(0); w.u32(0x00010000); w.u32(0);
    w.u32(0); w.u32(0); w.u32(0x40000000);
}

//  Baut die ganze Beschreibung. `stcoValuePos` bekommt die Stelle, an der der
//  Chunk-Offset steht: er ist erst bekannt, wenn die Größe von moov feststeht.
QByteArray buildMoov(const Track& t, quint32 movieTimescale, qint64* stcoValuePos) {
    Writer w;
    const quint64 movieDuration = t.mediaTimescale > 0
        ? (t.mediaDuration * movieTimescale) / t.mediaTimescale
        : 0;

    const qint64 moov = w.open(kMoov);

    const qint64 mvhd = w.open(kMvhd);
    w.fullBoxHeader();
    w.u32(0); w.u32(0);                       // Erstellt/Geändert: 0 = unbekannt
    w.u32(movieTimescale);
    w.u32(quint32(qMin<quint64>(movieDuration, 0xFFFFFFFFull)));
    w.u32(0x00010000);                        // Rate 1.0
    w.u16(0x0100);                            // Lautstärke 1.0
    w.u16(0); w.u32(0); w.u32(0);             // reserviert
    writeMatrix(w);
    w.zeros(24);                              // pre_defined
    w.u32(2);                                 // next_track_ID
    w.close(mvhd);

    const qint64 trak = w.open(kTrak);

    const qint64 tkhd = w.open(fcc("tkhd"));
    w.u32(0x00000007);                        // version 0, aktiviert/im Film
    w.u32(0); w.u32(0);
    w.u32(1);                                 // track_ID
    w.u32(0);
    w.u32(quint32(qMin<quint64>(movieDuration, 0xFFFFFFFFull)));
    w.u32(0); w.u32(0);
    w.u16(0);                                 // layer
    w.u16(0);                                 // alternate_group
    w.u16(0x0100);                            // Lautstärke 1.0 (Tonspur!)
    w.u16(0);
    writeMatrix(w);
    w.u32(0); w.u32(0);                       // Breite/Höhe = 0
    w.close(tkhd);

    //  Der Vorlauf der Quelle wird 1:1 übernommen - er trimmt bei AAC die
    //  Kodier-Verzögerung. Ohne ihn begänne der Ton hörbar zu früh.
    if (!t.edts.isEmpty()) w.raw(t.edts);

    const qint64 mdia = w.open(kMdia);

    const qint64 mdhd = w.open(kMdhd);
    w.fullBoxHeader();
    w.u32(0); w.u32(0);
    w.u32(t.mediaTimescale);
    w.u32(quint32(qMin<quint64>(t.mediaDuration, 0xFFFFFFFFull)));
    w.u16(0x55C4);                            // Sprache „und" (undetermined)
    w.u16(0);
    w.close(mdhd);

    const qint64 hdlr = w.open(kHdlr);
    w.fullBoxHeader();
    w.u32(0);
    w.u32(kSoun);
    w.zeros(12);
    w.raw(QByteArrayLiteral("SoundHandler"));
    w.u8(0);
    w.close(hdlr);

    const qint64 minf = w.open(kMinf);

    const qint64 smhd = w.open(fcc("smhd"));
    w.fullBoxHeader();
    w.u16(0); w.u16(0);                       // Balance, reserviert
    w.close(smhd);

    const qint64 dinf = w.open(kDinf);
    const qint64 dref = w.open(kDref);
    w.fullBoxHeader();
    w.u32(1);                                 // ein Eintrag
    const qint64 url = w.open(kUrl);
    w.u32(0x00000001);                        // Flag 1 = Daten in dieser Datei
    w.close(url);
    w.close(dref);
    w.close(dinf);

    const qint64 stbl = w.open(kStbl);

    //  Der Sample-Eintrag wird BYTEWEISE übernommen: darin steckt bei AAC das
    //  `esds` mit dem AudioSpecificConfig. Ihn nachzubauen hieße, den Codec zu
    //  interpretieren - genau das soll hier nicht passieren.
    w.raw(t.stsd);

    const qint64 stts = w.open(kStts);
    w.fullBoxHeader();
    w.u32(quint32(t.stts.size()));
    for (const SttsEntry& e : t.stts) { w.u32(e.count); w.u32(e.delta); }
    w.close(stts);

    const qint64 stsc = w.open(kStsc);
    w.fullBoxHeader();
    w.u32(1);
    w.u32(1);                                 // erster Chunk
    w.u32(quint32(t.sizes.size()));           // alle Samples darin
    w.u32(1);                                 // Sample-Eintrag 1
    w.close(stsc);

    const qint64 stsz = w.open(kStsz);
    w.fullBoxHeader();
    w.u32(0);                                 // keine Einheitsgröße
    w.u32(quint32(t.sizes.size()));
    for (quint32 s : t.sizes) w.u32(s);
    w.close(stsz);

    const qint64 stco = w.open(kStco);
    w.fullBoxHeader();
    w.u32(1);
    *stcoValuePos = w.b.size();               // hier kommt der Offset hinein
    w.u32(0);                                 // Platzhalter
    w.close(stco);

    w.close(stbl);
    w.close(minf);
    w.close(mdia);
    w.close(trak);
    w.close(moov);
    return w.b;
}

QByteArray buildFtyp() {
    Writer w;
    const qint64 ftyp = w.open(kFtyp);
    w.u32(fcc("M4A "));
    w.u32(0x00000200);                        // Nebenversion, wie üblich
    w.u32(fcc("M4A "));
    w.u32(fcc("mp42"));
    w.u32(fcc("isom"));
    w.close(ftyp);
    return w.b;
}

// ── Fragmentierte Dateien: die Tabellen stehen in den Fragmenten ────────────
//  In einer fragmentierten Datei ist `stbl` LEER - jedes `moof` bringt seine
//  eigene kleine Tabelle mit (`traf` -> `tfhd` + `trun`), und die Daten liegen
//  im `mdat` direkt dahinter. Zusammengesetzt ergibt das dieselbe flache Liste
//  „Sample N liegt bei Offset X und ist Y Byte lang", die auch `buildSampleList`
//  liefert - der Schreiber dahinter merkt keinen Unterschied.
//
//  Woher die Zahlen kommen, wenn ein Feld fehlt: `tfhd` kann Vorgaben je
//  Fragment setzen, `trex` (in `mvex`) welche für die ganze Datei. Fehlt beides
//  UND das Feld im `trun`, ist die Datei nicht auflösbar - dann wird abgelehnt,
//  nicht geraten (Regel 19).
struct TrexDefaults {
    quint32 duration = 0;
    quint32 size     = 0;
};

bool buildFragmentedSampleList(QFile& f, qint64 fileSize, const QVector<Box>& moofs,
                               quint32 trackId, const TrexDefaults& trex,
                               Track* t, Mp4Audio::Result* err) {
    *err = Mp4Audio::Result::Damaged;
    qint64 totalDuration = 0;

    for (const Box& moofBox : moofs) {
        if (moofBox.size > kMaxMoofSize) { *err = Mp4Audio::Result::TooLarge; return false; }
        if (!f.seek(moofBox.off)) return false;
        const QByteArray mf = f.read(moofBox.size);
        if (mf.size() != moofBox.size) return false;
        const uchar* p = reinterpret_cast<const uchar*>(mf.constData());

        Box moof;
        if (!nextBox(mf, 0, mf.size(), &moof) || moof.type != kMoof) return false;

        Box traf;
        qint64 tp = moof.payload();
        while (nextBox(mf, tp, moof.end(), &traf)) {
            tp = traf.end();
            if (traf.type != kTraf) continue;

            // ── tfhd: für WELCHE Spur, und welche Vorgaben gelten ───────────
            Box tfhd;
            if (!childOfBox(mf, traf, kTfhd, &tfhd) || tfhd.payloadSize() < 8) continue;
            qint64 at = tfhd.payload();
            const quint32 tfFlags = be32(p + at) & 0x00FFFFFF;
            at += 4;
            if (be32(p + at) != trackId) continue;          // andere Spur
            at += 4;

            //  Ohne ausdrückliche Angabe zählt der Anfang des Fragments - so
            //  schreibt es `default-base-is-moof`, und so tun es alle Muxer,
            //  die überhaupt fragmentieren.
            qint64 base = moofBox.off;
            if (tfFlags & 0x000001) {                       // base-data-offset
                if (!haveBytes(mf, at, 8)) return false;
                const quint64 v = be64(p + at);
                if (v > quint64(fileSize)) return false;
                base = qint64(v);
                at += 8;
            }
            if (tfFlags & 0x000002) at += 4;                // sample-description-index
            quint32 defDuration = trex.duration, defSize = trex.size;
            if (tfFlags & 0x000008) {
                if (!haveBytes(mf, at, 4)) return false;
                defDuration = be32(p + at); at += 4;
            }
            if (tfFlags & 0x000010) {
                if (!haveBytes(mf, at, 4)) return false;
                defSize = be32(p + at); at += 4;
            }
            if (tfFlags & 0x000020) at += 4;                // default-sample-flags

            // ── trun: die Samples dieses Fragments ─────────────────────────
            //  Mehrere `trun` je `traf` sind erlaubt; ohne eigenen `data_offset`
            //  schließt der nächste unmittelbar an den vorigen an.
            qint64 cursor = base;
            Box trun;
            qint64 rp = traf.payload();
            while (nextBox(mf, rp, traf.end(), &trun)) {
                rp = trun.end();
                if (trun.type != kTrun) continue;
                if (trun.payloadSize() < 8) return false;
                qint64 q = trun.payload();
                const quint32 raw = be32(p + q);
                const quint32 version = raw >> 24;
                const quint32 flags = raw & 0x00FFFFFF;
                q += 4;
                const quint32 count = be32(p + q); q += 4;
                if (count > kMaxSamples
                    || qint64(t->sizes.size()) + qint64(count) > kMaxSamples) {
                    *err = Mp4Audio::Result::TooLarge;
                    return false;
                }
                if (flags & 0x000001) {                     // data-offset
                    if (!haveBytes(mf, q, 4)) return false;
                    cursor = base + qint32(be32(p + q));
                    q += 4;
                }
                if (flags & 0x000004) q += 4;               // first-sample-flags

                //  Wie viele Bytes je Sample im `trun` stehen - daraus ergibt
                //  sich, ob die Tabelle überhaupt vollständig da ist.
                int per = 0;
                if (flags & 0x000100) per += 4;             // Dauer
                if (flags & 0x000200) per += 4;             // Größe
                if (flags & 0x000400) per += 4;             // Flags
                if (flags & 0x000800) per += 4;             // Zeitversatz
                if (per > 0 && !haveBytes(mf, q, qint64(count) * per)) return false;
                if (!(flags & 0x000200) && defSize == 0) return false;   // Größe unbekannt
                if (!(flags & 0x000100) && defDuration == 0 && count > 0) {
                    //  Ohne Dauer ist die Zeitachse unbekannt; für das reine
                    //  Herauskopieren reicht 0, die Länge steht dann in `mdhd`.
                    defDuration = 0;
                }
                (void)version;

                for (quint32 i = 0; i < count; ++i) {
                    quint32 dur = defDuration, sz = defSize;
                    if (flags & 0x000100) { dur = be32(p + q); q += 4; }
                    if (flags & 0x000200) { sz  = be32(p + q); q += 4; }
                    if (flags & 0x000400) q += 4;
                    if (flags & 0x000800) q += 4;

                    if (sz == 0) return false;
                    if (cursor < 0 || sz > quint32(fileSize) || cursor > fileSize - qint64(sz))
                        return false;
                    t->offsets.append(cursor);
                    t->sizes.append(sz);
                    t->totalBytes += sz;
                    cursor += sz;
                    totalDuration += dur;

                    //  `stts` als Lauflänge - genau wie bei einer gewöhnlichen
                    //  Datei, damit der Schreiber unverändert bleibt.
                    if (!t->stts.isEmpty() && t->stts.back().delta == dur)
                        ++t->stts.back().count;
                    else
                        t->stts.append(SttsEntry { 1, dur });
                }
            }
        }
    }

    if (t->sizes.isEmpty()) return false;
    if (t->mediaDuration == 0) t->mediaDuration = quint64(totalDuration);
    return true;
}

// ── Die Quelle einlesen: moov holen, Tonspur heraussuchen ───────────────────
Mp4Audio::Result readSource(const QString& path, QFile* f, Track* t, Mp4Audio::Info* info,
                            int trackIndex = 0) {
    f->setFileName(path);
    if (!f->open(QIODevice::ReadOnly)) return Mp4Audio::Result::NotOpenable;
    const qint64 fileSize = f->size();
    if (fileSize < 16) return Mp4Audio::Result::NotMp4;

    Box moovBox;
    bool fragmented = false, sawFtyp = false;
    QVector<Box> moofs;
    if (!scanTopLevel(*f, fileSize, &moovBox, &fragmented, &sawFtyp, &moofs)) {
        //  Kein `moov` gefunden: entweder eine ganz andere Hülle oder Bruch.
        return sawFtyp ? Mp4Audio::Result::Damaged : Mp4Audio::Result::NotMp4;
    }
    if (moovBox.size > kMaxMoov) return Mp4Audio::Result::TooLarge;

    QByteArray m;
    if (!f->seek(moovBox.off)) return Mp4Audio::Result::Damaged;
    m = f->read(moovBox.size);
    if (m.size() != moovBox.size) return Mp4Audio::Result::Damaged;
    //  Ab hier sind die Offsets in `m` relativ zum Beginn der moov-Box.
    Box moov;
    if (!nextBox(m, 0, m.size(), &moov) || moov.type != kMoov)
        return Mp4Audio::Result::Damaged;

    //  Fragmentiert? Dann stehen die Sample-Tabellen in den `moof`-Boxen, nicht
    //  in `stbl` - der Weg dorthin ist ein anderer, das Ergebnis dasselbe.
    Box mvex;
    const bool hasMvex = childOfBox(m, moov, kMvex, &mvex);
    const bool fragmentedFile = fragmented || hasMvex;
    //  Vorgaben für die ganze Datei (`trex`); je Fragment darf `tfhd` sie
    //  überschreiben. Gesucht wird erst, wenn die Spur feststeht.
    TrexDefaults trex;

    quint32 movieTimescale = 1000;
    Box mvhd;
    if (childOfBox(m, moov, kMvhd, &mvhd) && mvhd.payloadSize() >= 20) {
        const uchar* p = reinterpret_cast<const uchar*>(m.constData());
        const quint8 version = p[mvhd.payload()];
        const qint64 tsOff = mvhd.payload() + (version == 1 ? 20 : 12);
        if (haveBytes(m, tsOff, 4)) {
            const quint32 ts = be32(p + tsOff);
            if (ts > 0) movieTimescale = ts;
        }
    }
    info->audioTracks = 0;

    //  Alle Spuren durchgehen; genommen wird die ERSTE Tonspur. Mehrere
    //  Tonspuren sind selten (Mehrsprachigkeit) - die Zahl steht in `Info`,
    //  damit die Oberfläche es sagen kann.
    bool haveTrack = false;
    Box trak;
    qint64 pos = moov.payload();
    while (nextBox(m, pos, moov.end(), &trak)) {
        pos = trak.end();
        if (trak.type != kTrak) continue;

        Box mdia, hdlr;
        if (!childOfBox(m, trak, kMdia, &mdia)) continue;
        if (!childOfBox(m, mdia, kHdlr, &hdlr) || hdlr.payloadSize() < 12) continue;
        const uchar* p = reinterpret_cast<const uchar*>(m.constData());
        if (be32(p + hdlr.payload() + 8) != kSoun) continue;

        ++info->audioTracks;

        //  JEDE Tonspur beschreiben - die Oberfläche kann nur wählen lassen,
        //  was sie kennt. Codec, Rate und Kanäle stehen im Sample-Eintrag,
        //  die Sprache in `mdhd` (fünf Bit je Buchstabe, „und" = unbestimmt).
        {
            Mp4Audio::TrackDesc d;
            Box mdhdB, minfB, stblB, stsdB;
            if (childOfBox(m, mdia, kMdhd, &mdhdB) && mdhdB.payloadSize() >= 20) {
                const quint8 ver = p[mdhdB.payload()];
                const qint64 base = mdhdB.payload() + (ver == 1 ? 28 : 16);
                if (haveBytes(m, base, 2)) {
                    const quint16 packed = be16(p + base);
                    QString lang;
                    for (int i = 2; i >= 0; --i)
                        lang.append(QChar(ushort(((packed >> (i * 5)) & 0x1F) + 0x60)));
                    if (lang != QStringLiteral("und")) d.language = lang;
                }
                d.rate = int(be32(p + mdhdB.payload() + (ver == 1 ? 20 : 12)));
            }
            if (childOfBox(m, mdia, kMinf, &minfB)
                && childOfBox(m, minfB, kStbl, &stblB)
                && childOfBox(m, stblB, kStsd, &stsdB) && stsdB.payloadSize() >= 16) {
                const qint64 e0 = stsdB.payload() + 8;
                if (haveBytes(m, e0, 36)) {
                    const char cc4[5] = { char(be32(p + e0 + 4) >> 24),
                                          char(be32(p + e0 + 4) >> 16),
                                          char(be32(p + e0 + 4) >> 8),
                                          char(be32(p + e0 + 4)), 0 };
                    d.codec    = QString::fromLatin1(cc4).trimmed();
                    d.channels = int(be16(p + e0 + 24));
                    if (d.rate == 0) d.rate = int(be32(p + e0 + 32) >> 16);
                }
            }
            info->tracks.append(d);
        }

        //  Genommen wird die GEWÄHLTE Spur; die übrigen werden nur beschrieben.
        if (haveTrack || info->audioTracks - 1 != trackIndex) continue;

        Box mdhd, minf, stbl, stsd, edts;
        if (!childOfBox(m, mdia, kMdhd, &mdhd) || mdhd.payloadSize() < 20) continue;
        const quint8 version = p[mdhd.payload()];
        if (version == 1) {
            if (mdhd.payloadSize() < 32) continue;
            t->mediaTimescale = be32(p + mdhd.payload() + 20);
        } else {
            t->mediaTimescale = be32(p + mdhd.payload() + 12);
        }
        if (t->mediaTimescale == 0) continue;

        if (!childOfBox(m, mdia, kMinf, &minf)) continue;
        if (!selfContained(m, minf)) return Mp4Audio::Result::ExternalMedia;
        if (!childOfBox(m, minf, kStbl, &stbl)) continue;
        if (!childOfBox(m, stbl, kStsd, &stsd) || stsd.payloadSize() < 16) continue;

        //  Sample-Eintrag lesen (Codec, Kanäle, Abtastrate) und prüfen, dass er
        //  auf die eigene `dref` zeigt.
        const qint64 entry = stsd.payload() + 8;
        if (!haveBytes(m, entry, 36)) continue;
        const quint32 entrySize = be32(p + entry);
        if (entrySize < 36 || entry + entrySize > stsd.end()) continue;
        t->codec    = be32(p + entry + 4);
        if (be16(p + entry + 14) != 1) return Mp4Audio::Result::ExternalMedia;
        t->channels = int(be16(p + entry + 24));
        t->rate     = int(be32(p + entry + 32) >> 16);

        t->stsd = m.mid(stsd.off, stsd.size);
        if (childOfBox(m, trak, kEdts, &edts))
            t->edts = m.mid(edts.off, edts.size);

        Mp4Audio::Result err = Mp4Audio::Result::Damaged;
        if (fragmentedFile) {
            //  WELCHE Spur ist es? In den Fragmenten steht nur ihre Nummer.
            Box tkhd;
            if (!childOfBox(m, trak, kTkhd, &tkhd) || tkhd.payloadSize() < 20)
                return Mp4Audio::Result::Damaged;
            const quint8 tkVersion = p[tkhd.payload()];
            const qint64 idOff = tkhd.payload() + (tkVersion == 1 ? 20 : 12);
            if (!haveBytes(m, idOff, 4)) return Mp4Audio::Result::Damaged;
            const quint32 trackId = be32(p + idOff);

            //  `trex` liefert die Vorgaben für genau diese Spur.
            if (hasMvex) {
                Box trex_;
                qint64 xp = mvex.payload();
                while (nextBox(m, xp, mvex.end(), &trex_)) {
                    xp = trex_.end();
                    if (trex_.type != kTrex || trex_.payloadSize() < 24) continue;
                    if (be32(p + trex_.payload() + 4) != trackId) continue;
                    trex.duration = be32(p + trex_.payload() + 12);
                    trex.size     = be32(p + trex_.payload() + 16);
                    break;
                }
            }
            //  Steht `mvex` da, aber kein einziges Fragment: dann ist die Datei
            //  leer oder ein Anfangsstück - das ehrliche Ergebnis ist der alte
            //  Wert, nicht eine leere Tondatei.
            if (moofs.isEmpty()) return Mp4Audio::Result::Fragmented;
            if (!buildFragmentedSampleList(*f, fileSize, moofs, trackId, trex, t, &err))
                return err;
        } else {
            if (!buildSampleList(m, stbl, fileSize, t, &err)) return err;
        }
        haveTrack = true;
    }
    //  Ein Index außerhalb ist kein Grund zu scheitern - dann gilt die erste
    //  Spur. Der zweite Lauf beschreibt die Spuren neu, deshalb vorher leeren.
    if (!haveTrack && info->audioTracks > 0 && trackIndex != 0) {
        info->tracks.clear();
        info->audioTracks = 0;
        return readSource(path, f, t, info, 0);
    }
    if (!haveTrack) return Mp4Audio::Result::NoAudioTrack;

    const char cc[5] = { char(t->codec >> 24), char(t->codec >> 16),
                         char(t->codec >> 8),  char(t->codec), 0 };
    t->movieTimescale = movieTimescale;
    t->offsets.squeeze();
    t->sizes.squeeze();
    t->stts.squeeze();

    info->codec       = QString::fromLatin1(cc).trimmed();
    info->sampleRate  = t->rate;
    info->channels    = t->channels;
    info->sampleCount = t->sizes.size();
    info->audioBytes  = t->totalBytes;
    info->durationMs  = qint64((t->mediaDuration * 1000ull) / t->mediaTimescale);
    info->result      = Mp4Audio::Result::Ok;
    return Mp4Audio::Result::Ok;
}

} // namespace

namespace Mp4Audio {

bool isCandidate(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("mp4") || ext == QLatin1String("m4v")
        || ext == QLatin1String("mov") || ext == QLatin1String("qt");
}

QString targetPathFor(const QString& srcPath) {
    const QFileInfo fi(srcPath);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName();
    QString candidate = dir + QLatin1Char('/') + base + QStringLiteral(".m4a");
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).m4a").arg(n);
        ++n;
    }
    return candidate;
}

Info probe(const QString& path) {
    Info info;
    QFile f;
    Track t;
    const Result r = readSource(path, &f, &t, &info);
    info.result = r;
    return info;
}

Info probeTrack(const QString& path, int trackIndex) {
    Info info;
    QFile f;
    Track t;
    const Result r = readSource(path, &f, &t, &info, trackIndex);
    info.result = r;
    return info;
}

Result extract(const QString& srcPath, const QString& targetPath, Info* infoOut,
               const std::atomic<bool>* cancel, int trackIndex) {
    Info info;
    QFile src;
    Track t;
    const Result r = readSource(srcPath, &src, &t, &info, trackIndex);
    info.result = r;
    if (infoOut) *infoOut = info;
    if (r != Result::Ok) return r;

    qint64 stcoValuePos = 0;
    QByteArray ftyp = buildFtyp();
    QByteArray moov = buildMoov(t, t.movieTimescale, &stcoValuePos);

    //  Der Chunk-Offset ist die Stelle, an der die TÖNE beginnen - also hinter
    //  ftyp, moov und dem Kopf von mdat. Erst jetzt steht er fest.
    const bool big = (t.totalBytes + 16) > 0xFFFFFFFFll;
    const qint64 mdatHdr = big ? 16 : 8;
    const qint64 dataOff = ftyp.size() + moov.size() + mdatHdr;
    if (dataOff > 0xFFFFFFFFll) return Result::TooLarge;   // stco ist 32-bittig
    {
        uchar* p = reinterpret_cast<uchar*>(moov.data()) + stcoValuePos;
        const quint32 v = quint32(dataOff);
        p[0] = uchar(v >> 24); p[1] = uchar(v >> 16);
        p[2] = uchar(v >> 8);  p[3] = uchar(v);
    }

    QSaveFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly)) return Result::WriteFailed;
    if (out.write(ftyp) != ftyp.size()) return Result::WriteFailed;
    if (out.write(moov) != moov.size()) return Result::WriteFailed;

    Writer hdr;
    if (big) {
        hdr.u32(1);
        hdr.u32(kMdat);
        hdr.u64(quint64(t.totalBytes + 16));
    } else {
        hdr.u32(quint32(t.totalBytes + 8));
        hdr.u32(kMdat);
    }
    if (out.write(hdr.b) != hdr.b.size()) return Result::WriteFailed;

    //  Die Samples liegen in der Quelle meist schon hintereinander (ein Chunk
    //  = mehrere Samples). Zusammenhängende Bereiche werden deshalb zu EINEM
    //  Lesevorgang zusammengefasst - sonst wären es bei einer Stunde Ton über
    //  150 000 Einzel-Lesevorgänge.
    QByteArray buf;
    buf.resize(int(kCopyChunk));
    qint64 i = 0;
    const int n = t.offsets.size();
    while (i < n) {
        //  Kooperativer Abbruch (Regel 8): geprüft je zusammenhängendem Stück,
        //  nicht je Byte - dazwischen liegen höchstens ein paar hundert kB.
        if (cancel && cancel->load(std::memory_order_relaxed))
            return Result::NotOpenable;   // ohne commit() bleibt kein Bruchstück
        qint64 runStart = t.offsets[int(i)];
        qint64 runLen   = t.sizes[int(i)];
        qint64 j = i + 1;
        while (j < n && t.offsets[int(j)] == runStart + runLen) {
            runLen += t.sizes[int(j)];
            ++j;
        }
        if (!src.seek(runStart)) return Result::Damaged;
        qint64 left = runLen;
        while (left > 0) {
            const qint64 want = qMin<qint64>(left, kCopyChunk);
            const qint64 got  = src.read(buf.data(), want);
            if (got <= 0) return Result::Damaged;
            if (out.write(buf.constData(), got) != got) return Result::WriteFailed;
            left -= got;
        }
        i = j;
    }

    if (!out.commit()) return Result::WriteFailed;
    return Result::Ok;
}

} // namespace Mp4Audio
