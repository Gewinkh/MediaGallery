#include "audio/AudioTags.h"

#include <QFile>

#include <algorithm>
#include <QFileInfo>
#include <QStringDecoder>

namespace {

// ── Schutzgrenzen (Regel 21) ────────────────────────────────────────────────
constexpr qint64 kMaxTagBytes   = 32ll << 20;   // ID3-Block, moov, Ogg-Vorspann
constexpr qint64 kMaxCoverBytes = 16ll << 20;   // ein Bild
constexpr int    kMaxFields     = 4096;         // Frames/Kommentare je Datei
constexpr qint64 kOggScanBytes  = 4ll << 20;    // so weit wird nach dem Kommentar gesucht

// ── Rohlesen mit Grenzen ────────────────────────────────────────────────────
inline bool have(const QByteArray& b, qint64 off, qint64 n) {
    return off >= 0 && n >= 0 && off <= b.size() - n;
}
inline quint32 be16(const uchar* p) { return (quint32(p[0]) << 8) | p[1]; }
inline quint32 be24(const uchar* p) {
    return (quint32(p[0]) << 16) | (quint32(p[1]) << 8) | p[2];
}
inline quint32 be32(const uchar* p) {
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
         | (quint32(p[2]) <<  8) |  quint32(p[3]);
}
inline quint32 le32(const uchar* p) {
    return (quint32(p[3]) << 24) | (quint32(p[2]) << 16)
         | (quint32(p[1]) <<  8) |  quint32(p[0]);
}
inline quint64 be64(const uchar* p) {
    return (quint64(be32(p)) << 32) | be32(p + 4);
}
//  ID3 zählt „synchsafe": je Byte nur 7 Bit, damit im Kopf nie ein
//  Synchronisationsmuster (0xFF 0xEx) entsteht.
inline quint32 syncsafe(const uchar* p) {
    return (quint32(p[0] & 0x7F) << 21) | (quint32(p[1] & 0x7F) << 14)
         | (quint32(p[2] & 0x7F) <<  7) |  quint32(p[3] & 0x7F);
}
const uchar* u(const QByteArray& b) {
    return reinterpret_cast<const uchar*>(b.constData());
}

// ── ID3 ─────────────────────────────────────────────────────────────────────
//  Textkodierung eines ID3-Feldes. 0 = Latin-1, 1 = UTF-16 mit BOM,
//  2 = UTF-16BE, 3 = UTF-8. Alles andere ist ungültig.
QString id3Text(quint8 enc, const QByteArray& data) {
    if (data.isEmpty()) return {};
    QString s;
    switch (enc) {
    case 0: s = QString::fromLatin1(data); break;
    case 1: {
        QStringDecoder dec(QStringDecoder::Utf16);
        s = dec(data);
        if (dec.hasError()) return {};
        break;
    }
    case 2: {
        QStringDecoder dec(QStringDecoder::Utf16BE);
        s = dec(data);
        if (dec.hasError()) return {};
        break;
    }
    case 3: s = QString::fromUtf8(data); break;
    default: return {};
    }
    //  Abschließende Nullen wegnehmen - aber ERST JETZT. Vorher, auf den rohen
    //  Bytes, verstümmelte es UTF-16: dort endet ein gewöhnlicher Buchstabe
    //  („t" = 0x74 0x00) selbst auf einem Nullbyte, und der Text verlor sein
    //  letztes Zeichen (gemessen: „Nachtfahr" statt „Nachtfahrt").
    while (s.endsWith(QChar(u'\0'))) s.chop(1);
    return s;
}

//  Länge eines nullterminierten Textes in der jeweiligen Kodierung (UTF-16
//  endet auf ZWEI Nullbytes an gerader Stelle).
qint64 id3TermLen(quint8 enc, const QByteArray& b, qint64 from) {
    const bool wide = (enc == 1 || enc == 2);
    if (!wide) {
        for (qint64 i = from; i < b.size(); ++i)
            if (b[int(i)] == '\0') return i - from;
        return -1;
    }
    for (qint64 i = from; i + 1 < b.size(); i += 2)
        if (b[int(i)] == '\0' && b[int(i + 1)] == '\0') return i - from;
    return -1;
}

//  Die Rückgängigmachung der „Unsynchronisation": 0xFF 0x00 stand im Tag für
//  ein einfaches 0xFF. Ohne diesen Schritt sind Längen und Texte verschoben.
QByteArray deUnsync(const QByteArray& in) {
    QByteArray out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        out.append(in[i]);
        if (uchar(in[i]) == 0xFF && i + 1 < in.size() && in[i + 1] == '\0') ++i;
    }
    return out;
}

void readId3v2(const QByteArray& tag, int major, bool unsyncAll,
               AudioTags::Tags* out, bool withCover) {
    const QByteArray body = unsyncAll ? deUnsync(tag) : tag;
    const uchar* p = u(body);
    qint64 pos = 0;
    const int idLen   = (major == 2) ? 3 : 4;
    const int hdrLen  = (major == 2) ? 6 : 10;
    int fields = 0;

    while (pos + hdrLen <= body.size() && fields++ < kMaxFields) {
        const QByteArray id = body.mid(int(pos), idLen);
        if (id.isEmpty() || id[0] == '\0') break;             // Polsterung
        qint64 size = (major == 2) ? be24(p + pos + idLen)
                    : (major >= 4) ? syncsafe(p + pos + idLen)
                                   : be32(p + pos + idLen);
        //  Manche Programme schreiben in 2.4 GRÖSSEN WIE IN 2.3 (nicht
        //  synchsafe). Ergibt die synchsafe-Lesung Unsinn, wird die andere
        //  probiert - sonst zerfällt der ganze Rest des Tags.
        if (major >= 4 && (size <= 0 || pos + hdrLen + size > body.size())) {
            const qint64 alt = be32(p + pos + idLen);
            if (alt > 0 && pos + hdrLen + alt <= body.size()) size = alt;
        }
        if (size <= 0 || pos + hdrLen + size > body.size()) break;

        const QByteArray payload = body.mid(int(pos + hdrLen), int(size));
        pos += hdrLen + size;

        const bool isText  = id.startsWith('T');
        const bool isPic   = (id == "APIC" || id == "PIC");
        if (isText && payload.size() >= 2) {
            const quint8 enc = quint8(payload[0]);
            const QString val = id3Text(enc, payload.mid(1)).trimmed();
            if (val.isEmpty()) continue;
            if (id == "TIT2" || id == "TT2")      out->title  = val;
            else if (id == "TPE1" || id == "TP1") out->artist = val;
            else if (id == "TALB" || id == "TAL") out->album  = val;
            else if (id == "TRCK" || id == "TRK") out->trackNo = val.split('/').first().toInt();
        } else if (isPic && payload.size() > 4) {
            const quint8 enc = quint8(payload[0]);
            qint64 at = 1;
            QString mime;
            if (id == "PIC") {                    // 2.2: drei Zeichen „JPG"/„PNG"
                const QByteArray fmt = payload.mid(1, 3);
                mime = (fmt.compare("PNG", Qt::CaseInsensitive) == 0)
                       ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
                at = 4;
            } else {
                const qint64 mlen = id3TermLen(0, payload, at);
                if (mlen < 0) continue;
                mime = QString::fromLatin1(payload.mid(int(at), int(mlen)));
                at += mlen + 1;
            }
            if (at + 1 >= payload.size()) continue;
            ++at;                                  // Bildtyp (Titelbild, Rückseite …)
            const qint64 dlen = id3TermLen(enc, payload, at);
            if (dlen < 0) continue;
            at += dlen + ((enc == 1 || enc == 2) ? 2 : 1);
            if (at >= payload.size()) continue;
            out->hasCover = true;
            if (withCover) {
                const qint64 n = payload.size() - at;
                if (n > 0 && n <= kMaxCoverBytes) {
                    out->cover = payload.mid(int(at), int(n));
                    out->coverMime = mime.isEmpty() ? QStringLiteral("image/jpeg") : mime;
                }
            }
        }
    }
}

//  ID3v1: die letzten 128 Byte. Nur ein Rückfall - feste Feldlängen, Latin-1.
void readId3v1(QFile& f, AudioTags::Tags* out) {
    if (f.size() < 128) return;
    if (!f.seek(f.size() - 128)) return;
    const QByteArray b = f.read(128);
    if (b.size() != 128 || !b.startsWith("TAG")) return;
    const auto field = [&b](int at, int len) {
        QByteArray v = b.mid(at, len);
        while (!v.isEmpty() && (v.back() == '\0' || v.back() == ' ')) v.chop(1);
        return QString::fromLatin1(v).trimmed();
    };
    if (out->title.isEmpty())  out->title  = field(3, 30);
    if (out->artist.isEmpty()) out->artist = field(33, 30);
    if (out->album.isEmpty())  out->album  = field(63, 30);
    //  ID3v1.1: Kommentar ist auf 28 Byte gekürzt, danach 0 und die Nummer.
    if (out->trackNo == 0 && b[125] == '\0' && quint8(b[126]) > 0)
        out->trackNo = quint8(b[126]);
}

// ── Vorbis-Kommentare (FLAC und Ogg teilen sich das Format) ─────────────────
void readVorbisComments(const QByteArray& b, qint64 at, AudioTags::Tags* out,
                        bool withCover);

//  Ein METADATA_BLOCK_PICTURE, wie es in FLAC steht und in Ogg base64-kodiert
//  in einem Kommentar liegt.
void readFlacPicture(const QByteArray& b, AudioTags::Tags* out, bool withCover) {
    const uchar* p = u(b);
    if (!have(b, 0, 8)) return;
    qint64 at = 4;                                    // Bildtyp
    const qint64 mlen = be32(p + at);
    at += 4;
    if (mlen < 0 || !have(b, at, mlen)) return;
    const QString mime = QString::fromLatin1(b.mid(int(at), int(mlen)));
    at += mlen;
    if (!have(b, at, 4)) return;
    const qint64 dlen = be32(p + at);
    at += 4;
    if (dlen < 0 || !have(b, at, dlen)) return;
    at += dlen;                                       // Beschreibung
    if (!have(b, at, 20)) return;
    at += 16;                                         // Breite/Höhe/Tiefe/Farben
    const qint64 n = be32(p + at);
    at += 4;
    if (n <= 0 || n > kMaxCoverBytes || !have(b, at, n)) return;
    out->hasCover = true;
    if (withCover) {
        out->cover = b.mid(int(at), int(n));
        out->coverMime = mime.isEmpty() ? QStringLiteral("image/jpeg") : mime;
    }
}

void readVorbisComments(const QByteArray& b, qint64 at, AudioTags::Tags* out,
                        bool withCover) {
    const uchar* p = u(b);
    if (!have(b, at, 4)) return;
    const qint64 vendorLen = le32(p + at);
    at += 4;
    if (vendorLen < 0 || !have(b, at, vendorLen)) return;
    at += vendorLen;
    if (!have(b, at, 4)) return;
    qint64 count = le32(p + at);
    at += 4;
    if (count < 0 || count > kMaxFields) count = kMaxFields;

    for (qint64 i = 0; i < count; ++i) {
        if (!have(b, at, 4)) return;
        const qint64 len = le32(p + at);
        at += 4;
        if (len < 0 || !have(b, at, len)) return;
        const QByteArray entry = b.mid(int(at), int(len));
        at += len;

        const int eq = entry.indexOf('=');
        if (eq <= 0) continue;
        const QByteArray key = entry.left(eq).toUpper();
        const QByteArray val = entry.mid(eq + 1);
        if (key == "TITLE")        out->title  = QString::fromUtf8(val).trimmed();
        else if (key == "ARTIST")  out->artist = QString::fromUtf8(val).trimmed();
        else if (key == "ALBUM")   out->album  = QString::fromUtf8(val).trimmed();
        else if (key == "TRACKNUMBER")
            out->trackNo = QString::fromUtf8(val).split('/').first().toInt();
        else if (key == "METADATA_BLOCK_PICTURE") {
            //  In Ogg steht das Bild base64-kodiert IM Kommentar.
            if (val.size() > int(kMaxCoverBytes) * 2) continue;
            const QByteArray raw = QByteArray::fromBase64(val);
            if (!raw.isEmpty()) readFlacPicture(raw, out, withCover);
        }
    }
}

// ── FLAC ────────────────────────────────────────────────────────────────────
void readFlac(QFile& f, AudioTags::Tags* out, bool withCover) {
    qint64 at = 4;                                    // hinter „fLaC"
    for (int block = 0; block < 128; ++block) {
        if (!f.seek(at)) return;
        const QByteArray hdr = f.read(4);
        if (hdr.size() != 4) return;
        const quint8 type = quint8(hdr[0]) & 0x7F;
        const bool   last = (quint8(hdr[0]) & 0x80) != 0;
        const qint64 size = be24(u(hdr) + 1);
        at += 4;
        if (size < 0 || at + size > f.size() || size > kMaxTagBytes) return;

        //  Nur die beiden Blöcke, die uns angehen: Kommentare und Bild.
        if (type == 4 || type == 6) {
            const QByteArray body = f.read(size);
            if (body.size() != size) return;
            if (type == 4) readVorbisComments(body, 0, out, withCover);
            else           readFlacPicture(body, out, withCover);
        }
        at += size;
        if (last) return;
    }
}

// ── Ogg (Vorbis und Opus) ───────────────────────────────────────────────────
//  Gesucht wird das ZWEITE Paket des Datenstroms: bei Vorbis beginnt es mit
//  0x03 "vorbis", bei Opus mit "OpusTags". Pakete dürfen über Seiten hinweg
//  laufen - deshalb werden die Segmente eingesammelt, bis eines kürzer als 255
//  ist (das schließt ein Paket ab).
void readOgg(QFile& f, AudioTags::Tags* out, bool withCover) {
    //  Ohne Bild genügt der Anfang: die Kommentare stehen im zweiten Paket.
    //  Mit Bild darf es mehr sein - dort steckt es base64-kodiert mit drin.
    const qint64 want = withCover ? kOggScanBytes : (256ll << 10);
    const QByteArray all = f.read(std::min<qint64>(f.size(), want));
    const uchar* p = u(all);
    qint64 at = 0;
    QByteArray packet;
    int pages = 0;

    while (have(all, at, 27) && pages++ < 4096) {
        if (all.mid(int(at), 4) != "OggS") return;
        const int segs = int(p[at + 26]);
        if (!have(all, at + 27, segs)) return;
        qint64 dataAt = at + 27 + segs;
        for (int i = 0; i < segs; ++i) {
            const int len = int(p[at + 27 + i]);
            if (!have(all, dataAt, len)) return;
            if (packet.size() + len > kMaxTagBytes) return;
            packet.append(all.constData() + dataAt, len);
            dataAt += len;
            if (len < 255) {                          // Paketende
                if (packet.startsWith(QByteArrayLiteral("\x03vorbis"))) {
                    readVorbisComments(packet, 7, out, withCover);
                    return;
                }
                if (packet.startsWith(QByteArrayLiteral("OpusTags"))) {
                    readVorbisComments(packet, 8, out, withCover);
                    return;
                }
                packet.clear();
            }
        }
        at = dataAt;
    }
}

// ── MP4/M4A: moov/udta/meta/ilst ────────────────────────────────────────────
//  Ein eigener, winziger Box-Läufer: der in `Mp4AudioExtract.cpp` arbeitet auf
//  den Sample-Tabellen und ist dort im anonymen Namensraum richtig aufgehoben -
//  ihn herauszuziehen wäre ein Umbau an laufendem Code (Regel 12) für 30 Zeilen.
struct Box {
    quint32 type;
    qint64  off, hdr, size;
    qint64  end_pos() const { return off + size; }
};

bool nextBox(const QByteArray& b, qint64 pos, qint64 end, Box* out) {
    if (pos < 0 || end > b.size() || end - pos < 8) return false;
    const uchar* p = u(b);
    qint64 size = qint64(be32(p + pos));
    const quint32 type = be32(p + pos + 4);
    qint64 hdr = 8;
    if (size == 1) {
        if (end - pos < 16) return false;
        const quint64 s64 = be64(p + pos + 8);
        if (s64 > quint64(1) << 40) return false;
        size = qint64(s64);
        hdr  = 16;
    } else if (size == 0) {
        size = end - pos;
    }
    if (size < hdr || pos > end - size) return false;
    *out = Box { type, pos, hdr, size };
    return true;
}

constexpr quint32 fcc(const char (&s)[5]) {
    return (quint32(quint8(s[0])) << 24) | (quint32(quint8(s[1])) << 16)
         | (quint32(quint8(s[2])) <<  8) |  quint32(quint8(s[3]));
}

bool child(const QByteArray& b, qint64 start, qint64 end, quint32 type, Box* out) {
    Box box;
    qint64 pos = start;
    while (nextBox(b, pos, end, &box)) {
        if (box.type == type) { *out = box; return true; }
        pos = box.end_pos();
        if (box.size <= 0) return false;
    }
    return false;
}

//  Der Inhalt eines `ilst`-Eintrags steckt in einer `data`-Box: 4 Byte
//  Version/Flags, 4 Byte Sprache, dann die Nutzlast. Die Flags sagen, WAS es
//  ist: 1 = UTF-8, 13 = JPEG, 14 = PNG, 21 = ganze Zahl.
void readIlst(const QByteArray& m, const Box& ilst, AudioTags::Tags* out, bool withCover) {
    const uchar* p = u(m);
    Box item;
    qint64 pos = ilst.off + ilst.hdr;
    int fields = 0;
    while (nextBox(m, pos, ilst.off + ilst.size, &item) && fields++ < kMaxFields) {
        pos = item.off + item.size;
        Box data;
        if (!child(m, item.off + item.hdr, item.off + item.size, fcc("data"), &data))
            continue;
        const qint64 at = data.off + data.hdr + 8;
        const qint64 n  = data.off + data.size - at;
        if (n <= 0 || !have(m, at, n)) continue;
        const quint32 flags = be32(p + data.off + data.hdr) & 0x00FFFFFF;

        if (item.type == fcc("covr")) {
            out->hasCover = true;
            if (withCover && n <= kMaxCoverBytes) {
                out->cover = m.mid(int(at), int(n));
                out->coverMime = (flags == 14) ? QStringLiteral("image/png")
                                               : QStringLiteral("image/jpeg");
            }
            continue;
        }
        if (item.type == fcc("trkn")) {
            //  Zwei 16-Bit-Zahlen: Nummer und Gesamtzahl.
            if (n >= 4) out->trackNo = int(be16(p + at + 2));
            continue;
        }
        if (flags != 1 && flags != 0) continue;        // kein Text
        const QString val = QString::fromUtf8(m.mid(int(at), int(n))).trimmed();
        if (val.isEmpty()) continue;
        if (item.type == fcc("\251nam"))      out->title  = val;
        else if (item.type == fcc("\251ART")) out->artist = val;
        else if (item.type == fcc("\251alb")) out->album  = val;
    }
}

void readMp4(QFile& f, AudioTags::Tags* out, bool withCover) {
    //  Oberste Ebene abgehen, bis `moov` gefunden ist - es kann hinter einem
    //  großen `mdat` liegen.
    qint64 pos = 0;
    const qint64 fileSize = f.size();
    int guard = 0;
    while (pos < fileSize && guard++ < 4096) {
        if (!f.seek(pos)) return;
        const QByteArray hdr = f.read(16);
        if (hdr.size() < 8) return;
        const uchar* h = u(hdr);
        qint64 size = qint64(be32(h));
        const quint32 type = be32(h + 4);
        if (size == 1) {
            if (hdr.size() < 16) return;
            const quint64 s64 = be64(h + 8);
            if (s64 > quint64(1) << 40) return;
            size = qint64(s64);
        } else if (size == 0) {
            size = fileSize - pos;
        }
        if (size < 8 || pos > fileSize - size) return;

        if (type == fcc("moov")) {
            if (size > kMaxTagBytes) return;
            if (!f.seek(pos)) return;
            const QByteArray moovBytes = f.read(size);
            if (moovBytes.size() != size) return;
            Box moov;
            if (!nextBox(moovBytes, 0, moovBytes.size(), &moov)) return;
            Box udta, meta, ilst;
            if (!child(moovBytes, moov.off + moov.hdr, moov.off + moov.size,
                       fcc("udta"), &udta)) return;
            if (!child(moovBytes, udta.off + udta.hdr, udta.off + udta.size,
                       fcc("meta"), &meta)) return;
            //  `meta` ist eine FULL box: vor den Kindern stehen 4 Byte
            //  Version/Flags. Wer sie überliest, findet `ilst` nie.
            if (!child(moovBytes, meta.off + meta.hdr + 4, meta.off + meta.size,
                       fcc("ilst"), &ilst)) return;
            readIlst(moovBytes, ilst, out, withCover);
            return;
        }
        pos += size;
    }
}

//  Steht in einem Feld NICHTS Sichtbares, gilt es als nicht gesetzt. Manche
//  Werkzeuge „leeren" den Titel, indem sie ein unsichtbares Zeichen
//  hineinschreiben statt das Feld wegzulassen (gemessen: ein Titel aus genau
//  einem U+2800 BRAILLE PATTERN BLANK). Ohne diese Prüfung stünde in der
//  Anzeige eine leere Zeile, statt auf den Dateinamen zurückzufallen.
bool invisibleField(const QString& s) {
    for (const QChar c : s) {
        if (c.isSpace()) continue;
        const QChar::Category cat = c.category();
        if (cat == QChar::Other_Format || cat == QChar::Other_Control) continue;
        if (c.unicode() == 0x2800) continue;
        return false;
    }
    return true;
}

//  Ein Tag lesen - der gemeinsame Weg für beide öffentlichen Funktionen.
AudioTags::Tags readInto(const QString& path, bool withCover) {
    AudioTags::Tags t;
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return t;
    if (f.size() < 16) return t;

    const QByteArray head = f.read(16);
    if (head.size() < 12) return t;
    const uchar* h = u(head);

    if (head.startsWith("ID3")) {
        const int major = int(h[3]);
        const quint8 flags = h[5];
        const qint64 size = syncsafe(h + 6);
        if (size > 0 && size <= kMaxTagBytes && size <= f.size()) {
            if (f.seek(10)) {
                QByteArray tag = f.read(size);
                if (tag.size() == size) {
                    //  Erweiterter Kopf (Flag 0x40) wird übersprungen: seine
                    //  Länge steht in seinen ersten vier Bytes.
                    qint64 skip = 0;
                    if ((flags & 0x40) && tag.size() > 4) {
                        const qint64 ext = (major >= 4) ? syncsafe(u(tag)) : be32(u(tag)) + 4;
                        if (ext > 0 && ext < tag.size()) skip = ext;
                    }
                    readId3v2(tag.mid(int(skip)), major, (flags & 0x80) != 0, &t, withCover);
                }
            }
        }
        readId3v1(f, &t);
    } else if (head.startsWith("fLaC")) {
        readFlac(f, &t, withCover);
    } else if (head.startsWith("OggS")) {
        if (f.seek(0)) readOgg(f, &t, withCover);
    } else if (head.mid(4, 4) == "ftyp" || head.mid(4, 4) == "moov") {
        readMp4(f, &t, withCover);
    } else {
        //  Keine bekannte Hülle vorn - vielleicht liegt hinten ein ID3v1.
        readId3v1(f, &t);
    }

    if (invisibleField(t.title))  t.title.clear();
    if (invisibleField(t.artist)) t.artist.clear();
    if (invisibleField(t.album))  t.album.clear();

    t.ok = !t.title.isEmpty() || !t.artist.isEmpty() || !t.album.isEmpty()
           || t.hasCover;
    return t;
}

} // namespace

namespace AudioTags {

QString Tags::displayTitle(const QString& path) const {
    if (!title.isEmpty()) return title;
    return QFileInfo(path).completeBaseName();
}

QString Tags::subtitle() const {
    if (!artist.isEmpty() && !album.isEmpty())
        return artist + QStringLiteral(" - ") + album;
    return artist.isEmpty() ? album : artist;
}

Tags read(const QString& path, bool withCover) {
    return readInto(path, withCover);
}

QByteArray readCover(const QString& path, QString* mime) {
    const Tags t = readInto(path, /*withCover=*/true);
    if (mime) *mime = t.coverMime;
    return t.cover;
}

} // namespace AudioTags
