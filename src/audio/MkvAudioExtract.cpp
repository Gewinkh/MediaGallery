#include "audio/MkvAudioExtract.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QVector>

#include <algorithm>
#include <cstring>
#include <limits>

// Matroska: Segment > Info (Zeitbasis) | Tracks > TrackEntry | Cluster > SimpleBlock.
// Jedes Element ist Kennung (VINT mit Markierung) + Laenge (VINT ohne) + Daten.
// Herauskopieren heisst Pakete der Tonspur einsammeln und in Ogg-Seiten verpacken.

namespace {

constexpr qint64 kMaxHeaderBytes = 16ll << 20;   // Tracks/Info am Stück
constexpr qint64 kMaxPrivate     = 4ll << 20;    // CodecPrivate
constexpr qint64 kMaxPacket      = 16ll << 20;   // ein einzelnes Paket
constexpr qint64 kMaxPackets     = 20'000'000;
constexpr int    kMaxDepth       = 12;
constexpr int    kMaxFrames      = 4096;         // Rahmen je Block (Lacing)

constexpr quint32 kIdEbml        = 0x1A45DFA3;
constexpr quint32 kIdSegment     = 0x18538067;
constexpr quint32 kIdInfo        = 0x1549A966;
constexpr quint32 kIdTimeScale   = 0x2AD7B1;
constexpr quint32 kIdDuration    = 0x4489;
constexpr quint32 kIdTracks      = 0x1654AE6B;
constexpr quint32 kIdTrackEntry  = 0xAE;
constexpr quint32 kIdTrackNumber = 0xD7;
constexpr quint32 kIdTrackType   = 0x83;
constexpr quint32 kIdCodecId     = 0x86;
constexpr quint32 kIdCodecPriv   = 0x63A2;
constexpr quint32 kIdAudio       = 0xE1;
constexpr quint32 kIdSampleFreq  = 0xB5;
constexpr quint32 kIdChannels    = 0x9F;
constexpr quint32 kIdLanguage    = 0x22B59C;   // ISO-639-2, Vorgabe „und"
constexpr quint32 kIdSeekHead    = 0x114D9B74;
constexpr quint32 kIdSeek        = 0x4DBB;
constexpr quint32 kIdSeekId      = 0x53AB;
constexpr quint32 kIdSeekPos     = 0x53AC;
constexpr quint32 kIdCues        = 0x1C53BB6B;
constexpr quint32 kIdCuePoint    = 0xBB;
constexpr quint32 kIdCueTime     = 0xB3;
constexpr quint32 kIdCueTrackPositions = 0xB7;
constexpr quint32 kIdCueClusterPos     = 0xF1;
constexpr quint32 kIdTrackName   = 0x536E;
constexpr quint32 kIdCluster     = 0x1F43B675;
constexpr quint32 kIdTimestamp   = 0xE7;
constexpr quint32 kIdSimpleBlock = 0xA3;
constexpr quint32 kIdBlockGroup  = 0xA0;
constexpr quint32 kIdBlock       = 0xA1;

inline bool have(const QByteArray& b, qint64 off, qint64 n) {
    return off >= 0 && n >= 0 && off <= b.size() - n;
}
const uchar* u(const QByteArray& b) {
    return reinterpret_cast<const uchar*>(b.constData());
}

//  Eine Kennung: das erste Byte sagt über die führenden Nullbits, wie viele
//  Bytes folgen; die Markierung BLEIBT stehen (so werden Kennungen verglichen).
bool readId(const QByteArray& b, qint64 pos, quint32* id, qint64* len) {
    if (!have(b, pos, 1)) return false;
    const uchar first = u(b)[pos];
    int n = 0;
    if      (first & 0x80) n = 1;
    else if (first & 0x40) n = 2;
    else if (first & 0x20) n = 3;
    else if (first & 0x10) n = 4;
    else return false;
    if (!have(b, pos, n)) return false;
    quint32 v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | u(b)[pos + i];
    *id = v;
    *len = n;
    return true;
}

//  Eine Länge: dieselbe Zählweise, aber die Markierung wird ENTFERNT. Alle
//  Nutzbits gesetzt heißt „unbekannte Länge" (kommt bei Segment und Cluster
//  in Datenströmen vor) - dann liefert `unknown` true.
bool readSize(const QByteArray& b, qint64 pos, qint64* size, qint64* len,
              bool* unknown = nullptr) {
    if (!have(b, pos, 1)) return false;
    const uchar first = u(b)[pos];
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        if (first & (0x80 >> i)) { n = i + 1; break; }
    }
    if (n == 0 || !have(b, pos, n)) return false;
    quint64 v = quint64(first & (0xFF >> n));
    bool allOnes = (v == quint64((1u << (7 - (n - 1))) - 1));
    for (int i = 1; i < n; ++i) {
        const uchar c = u(b)[pos + i];
        v = (v << 8) | c;
        if (c != 0xFF) allOnes = false;
    }
    if (unknown) *unknown = allOnes;
    if (v > quint64(std::numeric_limits<qint64>::max() / 2)) return false;
    *size = qint64(v);
    *len  = n;
    return true;
}

quint64 uintOf(const QByteArray& b, qint64 pos, qint64 n) {
    quint64 v = 0;
    for (qint64 i = 0; i < n && i < 8; ++i) v = (v << 8) | u(b)[pos + i];
    return v;
}

double floatOf(const QByteArray& b, qint64 pos, qint64 n) {
    if (n == 4) {
        quint32 bits = quint32(uintOf(b, pos, 4));
        float f;
        std::memcpy(&f, &bits, 4);
        return double(f);
    }
    if (n == 8) {
        quint64 bits = uintOf(b, pos, 8);
        double d;
        std::memcpy(&d, &bits, 8);
        return d;
    }
    return 0.0;
}

// Ogg hat eine EIGENE Prüfsumme: dasselbe Polynom wie CRC-32, aber ohne Spiegelung und ohne Vor- oder
// Nachbehandlung. Mit der gewöhnlichen CRC-32 lehnt jeder Abspieler die Datei ab.
quint32 oggCrc(const QByteArray& page) {
    static quint32 table[256];
    static bool built = false;
    if (!built) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 r = i << 24;
            for (int k = 0; k < 8; ++k)
                r = (r & 0x80000000u) ? ((r << 1) ^ 0x04c11db7u) : (r << 1);
            table[i] = r;
        }
        built = true;
    }
    quint32 crc = 0;
    for (char c : page)
        crc = (crc << 8) ^ table[((crc >> 24) & 0xFF) ^ uchar(c)];
    return crc;
}

class OggWriter {
public:
    OggWriter(QSaveFile* out, quint32 serial) : m_out(out), m_serial(serial) {}

    // `granule` ist der Zeitstand NACH diesem Paket; -1 heißt unbestimmt. `QByteArrayView`, nicht `QByteArray`: das
    // Paket liegt schon im Cluster-Puffer, eine Kopie wäre eine Allokation je Rahmen - im Spielfilm über 300.000.
    bool write(QByteArrayView packet, qint64 granule, bool bos, bool eos) {
        qint64 at = 0;
        bool first = true;
        do {
            const qint64 left = packet.size() - at;
            const int segs = int(std::min<qint64>(255, left / 255 + 1));
            m_table.resize(0);                    // behält seine Kapazität
            qint64 take = 0;
            for (int i = 0; i < segs; ++i) {
                const int len = int(std::min<qint64>(255, left - take));
                m_table.append(char(len));
                take += len;
                if (len < 255) break;
            }
            const bool last = (at + take >= packet.size())
                              && (m_table.isEmpty() || uchar(m_table.back()) < 255);
            if (!writePage(packet.sliced(at, take), m_table,
                           last ? granule : -1,
                           bos && first, eos && last))
                return false;
            at += take;
            first = false;
        } while (at < packet.size());
        return true;
    }

private:
    bool writePage(QByteArrayView data, const QByteArray& table,
                   qint64 granule, bool bos, bool eos) {
        //  Der Seitenpuffer gehört dem Schreiber und wird wiederverwendet:
        //  `resize(0)` behält die Kapazität, `clear()` gäbe sie zurück.
        QByteArray& page = m_page;
        page.resize(0);
        page.reserve(27 + table.size() + data.size());
        page.append("OggS", 4);
        page.append(char(0));                                    // Version
        char type = 0;
        if (m_continued) type |= 0x01;
        if (bos)         type |= 0x02;
        if (eos)         type |= 0x04;
        page.append(type);
        const quint64 g = quint64(granule < 0 ? -1 : granule);
        for (int i = 0; i < 8; ++i) page.append(char((g >> (8 * i)) & 0xFF));
        for (int i = 0; i < 4; ++i) page.append(char((m_serial >> (8 * i)) & 0xFF));
        for (int i = 0; i < 4; ++i) page.append(char((m_seq >> (8 * i)) & 0xFF));
        const int crcAt = page.size();
        page.append(4, '\0');                                    // Platz für CRC
        page.append(char(table.size()));
        page.append(table);
        page.append(data);

        const quint32 crc = oggCrc(page);
        for (int i = 0; i < 4; ++i) page[crcAt + i] = char((crc >> (8 * i)) & 0xFF);

        ++m_seq;
        m_continued = !table.isEmpty() && uchar(table.back()) == 255;
        return m_out->write(page) == page.size();
    }

    QSaveFile* m_out;
    quint32    m_serial;
    quint32    m_seq = 0;
    bool       m_continued = false;
    QByteArray m_page;        // Seitenpuffer, wiederverwendet
    QByteArray m_table;       // Segmenttafel, wiederverwendet
};

qint64 opusPacketSamples(QByteArrayView p) {
    if (p.isEmpty()) return 0;
    const uchar toc = uchar(p[0]);
    const int config = toc >> 3;
    static const int frameSamples[32] = {
        480, 960, 1920, 2880,          // SILK NB   10/20/40/60 ms
        480, 960, 1920, 2880,          // SILK MB
        480, 960, 1920, 2880,          // SILK WB
        480, 960,                      // Hybrid SWB 10/20 ms
        480, 960,                      // Hybrid FB
        120, 240, 480, 960,            // CELT NB   2.5/5/10/20 ms
        120, 240, 480, 960,            // CELT WB
        120, 240, 480, 960,            // CELT SWB
        120, 240, 480, 960,            // CELT FB
    };
    const int per = frameSamples[config & 31];
    const int code = toc & 0x03;
    int frames = 1;
    if (code == 1 || code == 2) frames = 2;
    else if (code == 3) {
        if (p.size() < 2) return per;
        frames = uchar(p[1]) & 0x3F;
        if (frames <= 0) return per;
        if (frames > 48) frames = 48;
    }
    return qint64(per) * frames;
}

struct Track {
    quint64    number = 0;
    QString    codec;
    QByteArray priv;
    QString    language;
    QString    name;
    int        rate = 48000;
    int        channels = 2;
};

struct Elem { quint32 id; qint64 dataAt; qint64 size; qint64 next; bool unknownSize; };

bool nextElem(const QByteArray& b, qint64 pos, qint64 end, Elem* out) {
    quint32 id = 0;
    qint64 idLen = 0, size = 0, sizeLen = 0;
    bool unknown = false;
    if (pos >= end) return false;
    if (!readId(b, pos, &id, &idLen)) return false;
    if (!readSize(b, pos + idLen, &size, &sizeLen, &unknown)) return false;
    const qint64 dataAt = pos + idLen + sizeLen;
    if (unknown) {
        *out = Elem { id, dataAt, end - dataAt, end, true };
        return dataAt <= end;
    }
    if (size < 0 || dataAt > end - size) return false;
    *out = Elem { id, dataAt, size, dataAt + size, false };
    return true;
}

// Das SEGMENT umfasst die ganze Datei und ist damit fast immer größer als das gelesene Kopfstück; `nextElem`
// lehnte es deshalb ab - daran scheiterte jede MKV über 16 MB. Hier wird nur der KOPF im Puffer verlangt.
bool segmentElem(const QByteArray& b, qint64 pos, qint64 bufEnd, qint64 fileEnd,
                 Elem* out) {
    quint32 id = 0;
    qint64 idLen = 0, size = 0, sizeLen = 0;
    bool unknown = false;
    if (pos >= bufEnd) return false;
    if (!readId(b, pos, &id, &idLen)) return false;
    if (!readSize(b, pos + idLen, &size, &sizeLen, &unknown)) return false;
    const qint64 dataAt = pos + idLen + sizeLen;
    if (dataAt > bufEnd) return false;                 // Kopf selbst abgeschnitten
    if (unknown) {
        *out = Elem { id, dataAt, fileEnd - dataAt, fileEnd, true };
        return true;
    }
    if (size < 0) return false;
    const qint64 endAt = (size > fileEnd - dataAt) ? fileEnd : dataAt + size;
    *out = Elem { id, dataAt, endAt - dataAt, endAt, false };
    return true;
}

// AAC ist der einzige Codec, der einen Kopf JE RAHMEN braucht: in Matroska liegen die Rahmen nackt, die
// Beschreibung steht einmal in `CodecPrivate`. Ohne den ADTS-Kopf bleibt die Datei stumm.
struct AdtsConfig {
    int  profile = 1;        // ADTS: 0=Main, 1=LC, 2=SSR, 3=LTP (= AOT - 1)
    int  rateIndex = 4;      // Index in die feste Ratentabelle (4 = 44100)
    int  channelCfg = 2;
    bool ok = false;
};

AdtsConfig parseAsc(const QByteArray& asc) {
    AdtsConfig c;
    if (asc.size() < 2) return c;
    const auto bits = [&](int at, int n) {
        int v = 0;
        for (int i = 0; i < n; ++i) {
            const int b = at + i;
            if (b / 8 >= asc.size()) return -1;
            v = (v << 1) | ((uchar(asc[b / 8]) >> (7 - b % 8)) & 1);
        }
        return v;
    };
    int at = 0;
    int aot = bits(at, 5); at += 5;
    if (aot == 31) {                       // erweiterter Objekttyp
        const int ext = bits(at, 6); at += 6;
        if (ext < 0) return c;
        aot = 32 + ext;
    }
    if (aot < 0) return c;
    int rateIndex = bits(at, 4); at += 4;
    if (rateIndex == 15) at += 24;         // Rate steht ausgeschrieben
    const int chanCfg = bits(at, 4);
    if (rateIndex < 0 || rateIndex > 12 || chanCfg < 0) return c;

    //  HE-AAC (SBR = 5, PS = 29) wird als LC geschrieben: der Kern IST LC, und
    //  jeder Dekoder erkennt die Erweiterung am Strom selbst. Ein ADTS-Kopf
    //  kann sie gar nicht ausdrücken - das Feld hat nur zwei Bit.
    const int base = (aot == 5 || aot == 29) ? 2 : aot;
    if (base < 1 || base > 4) return c;    // Main/LC/SSR/LTP - mehr kennt ADTS nicht

    //  Kanalzahl 0 heißt „steht im Strom" (Program Config Element) - und genau
    //  das darf auch im ADTS-Kopf stehen. Die Zahl der Spur einzusetzen wäre
    //  hier FALSCH: sie würde dem widersprechen, was der Rahmen selbst sagt.
    if (chanCfg > 7) return c;

    c.profile    = base - 1;
    c.rateIndex  = rateIndex;
    c.channelCfg = chanCfg;
    c.ok = true;
    return c;
}

QByteArray adtsHeader(const AdtsConfig& c, qint64 frameLen) {
    const qint64 total = frameLen + 7;
    QByteArray h(7, '\0');
    h[0] = char(0xFF);                                        // Synchronwort
    h[1] = char(0xF1);                                        // MPEG-4, kein CRC
    h[2] = char(((c.profile & 0x3) << 6) | ((c.rateIndex & 0xF) << 2)
                | ((c.channelCfg >> 2) & 0x1));
    h[3] = char(((c.channelCfg & 0x3) << 6) | int((total >> 11) & 0x3));
    h[4] = char((total >> 3) & 0xFF);
    h[5] = char(((total & 0x7) << 5) | 0x1F);                 // Pufferstand: voll
    h[6] = char(0xFC);                                        // ein Rahmen je Kopf
    return h;
}

void readTracks(const QByteArray& b, const Elem& tracks, QVector<Track>* out) {
    Elem e;
    qint64 pos = tracks.dataAt;
    int guard = 0;
    while (nextElem(b, pos, tracks.dataAt + tracks.size, &e) && guard++ < 4096) {
        pos = e.next;
        if (e.id != kIdTrackEntry) continue;

        Track t;
        bool isAudio = false;
        Elem f;
        qint64 p2 = e.dataAt;
        int guard2 = 0;
        while (nextElem(b, p2, e.dataAt + e.size, &f) && guard2++ < 4096) {
            p2 = f.next;
            switch (f.id) {
            case kIdTrackNumber: t.number = uintOf(b, f.dataAt, f.size); break;
            case kIdTrackType:   isAudio = (uintOf(b, f.dataAt, f.size) == 2); break;
            case kIdCodecId:
                t.codec = QString::fromLatin1(b.mid(int(f.dataAt), int(f.size))).trimmed();
                break;
            case kIdLanguage:
                t.language = QString::fromLatin1(b.mid(int(f.dataAt), int(f.size)))
                                 .trimmed();
                break;
            case kIdTrackName:
                t.name = QString::fromUtf8(b.mid(int(f.dataAt), int(f.size))).trimmed();
                break;
            case kIdCodecPriv:
                if (f.size > 0 && f.size <= kMaxPrivate)
                    t.priv = b.mid(int(f.dataAt), int(f.size));
                break;
            case kIdAudio: {
                Elem g;
                qint64 p3 = f.dataAt;
                int guard3 = 0;
                while (nextElem(b, p3, f.dataAt + f.size, &g) && guard3++ < 256) {
                    p3 = g.next;
                    if (g.id == kIdSampleFreq) {
                        const double hz = floatOf(b, g.dataAt, g.size);
                        if (hz > 0.0 && hz < 400000.0) t.rate = int(hz + 0.5);
                    } else if (g.id == kIdChannels) {
                        const quint64 ch = uintOf(b, g.dataAt, g.size);
                        if (ch > 0 && ch < 64) t.channels = int(ch);
                    }
                }
                break;
            }
            default: break;
            }
        }
        if (isAudio && t.number > 0) out->append(t);
    }
}

bool readBlockFrames(const QByteArray& b, qint64 at, qint64 size,
                     quint64 wantTrack, qint16* relTime,
                     QVector<QPair<qint64, qint64>>* frames) {
    qint64 track = 0, vlen = 0;
    if (!readSize(b, at, &track, &vlen)) return false;
    qint64 p = at + vlen;
    if (!have(b, p, 3)) return false;
    *relTime = qint16((uchar(b[int(p)]) << 8) | uchar(b[int(p + 1)]));
    const uchar flags = uchar(b[int(p + 2)]);
    p += 3;
    if (quint64(track) != wantTrack) return true;          // andere Spur

    const qint64 end = at + size;
    const int lacing = (flags >> 1) & 0x03;
    if (lacing == 0) {                                     // ein Rahmen
        if (end < p) return false;
        frames->append({ p, end - p });
        return true;
    }
    if (!have(b, p, 1)) return false;
    const int count = int(uchar(b[int(p)])) + 1;
    ++p;
    if (count <= 0 || count > kMaxFrames) return false;

    QVector<qint64> sizes;
    if (lacing == 2) {                                     // feste Größe
        const qint64 rest = end - p;
        if (rest <= 0 || rest % count != 0) return false;
        sizes.fill(rest / count, count);
    } else if (lacing == 1) {                              // Xiph
        for (int i = 0; i < count - 1; ++i) {
            qint64 n = 0;
            while (true) {
                if (!have(b, p, 1)) return false;
                const uchar v = uchar(b[int(p++)]);
                n += v;
                if (v != 255) break;
            }
            sizes.append(n);
        }
    } else {                                               // EBML
        qint64 first = 0, len = 0;
        if (!readSize(b, p, &first, &len)) return false;
        p += len;
        sizes.append(first);
        qint64 prev = first;
        for (int i = 1; i < count - 1; ++i) {
            qint64 raw = 0, l2 = 0;
            if (!readSize(b, p, &raw, &l2)) return false;
            const qint64 bias = (qint64(1) << (7 * l2 - 1)) - 1;
            prev += raw - bias;
            if (prev < 0) return false;
            sizes.append(prev);
            p += l2;
        }
    }
    qint64 used = 0;
    for (qint64 n : sizes) {
        if (n < 0 || !have(b, p + used, n)) return false;
        frames->append({ p + used, n });
        used += n;
    }
    if (lacing != 2) {                                     // letzter Rahmen: Rest
        const qint64 rest = end - (p + used);
        if (rest < 0) return false;
        frames->append({ p + used, rest });
    }
    return true;
}

// Ogg (Opus, Vorbis) braucht Seiten, Segmenttafel und Zeitstand - die Pakete tragen
// ihre Laenge nicht selbst. Roh (AC-3, MPEG-Audio) ist selbstrahmend und geht
// hintereinander weg. Alles andere bleibt UnsupportedCodec.
enum class Wrap { Ogg, Raw, Adts };

struct CodecEntry {
    const char* prefix;
    const char* ext;
    Wrap        wrap;
};

constexpr CodecEntry kCodecs[] = {
    { "A_OPUS",     "opus", Wrap::Ogg },
    { "A_VORBIS",   "ogg",  Wrap::Ogg },
    { "A_EAC3",     "eac3", Wrap::Raw },
    { "A_AC3",      "ac3",  Wrap::Raw },   // auch A_AC3/BSID9, /BSID10
    { "A_MPEG/L3",  "mp3",  Wrap::Raw },
    { "A_MPEG/L2",  "mp2",  Wrap::Raw },
    { "A_AAC",      "aac",  Wrap::Adts },  // auch A_AAC/MPEG4/LC, /SBR …
};

const CodecEntry* codecEntry(const QString& codec) {
    for (const CodecEntry& e : kCodecs)
        if (codec.startsWith(QLatin1String(e.prefix))) return &e;
    return nullptr;
}

QString extensionFor(const QString& codec) {
    const CodecEntry* e = codecEntry(codec);
    return e ? QString::fromLatin1(e->ext) : QStringLiteral("ogg");
}

MkvAudio::Result run(const QString& path, MkvAudio::Info* info,
                     QSaveFile* out, const std::atomic<bool>* cancel,
                     int trackIndex = 0) {
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return MkvAudio::Result::NotOpenable;
    if (f.size() < 32) return MkvAudio::Result::NotMatroska;

    //  Matroska liest sich am Stück am einfachsten; die Datei kann aber groß
    //  sein. Deshalb: Kopf lesen, und die Cluster in Stücken durchgehen.
    const QByteArray head = f.read(std::min<qint64>(f.size(), kMaxHeaderBytes));
    if (head.size() < 32) return MkvAudio::Result::NotMatroska;
    Elem ebml;
    if (!nextElem(head, 0, head.size(), &ebml) || ebml.id != kIdEbml)
        return MkvAudio::Result::NotMatroska;

    Elem seg;
    if (!segmentElem(head, ebml.next, head.size(), f.size(), &seg)
        || seg.id != kIdSegment)
        return MkvAudio::Result::NotMatroska;

    quint64 timeScale = 1000000;         // Vorgabe: 1 ms
    double  durationTicks = 0.0;
    QVector<Track> tracks;
    {
        Elem e;
        qint64 pos = seg.dataAt;
        const qint64 end = std::min<qint64>(head.size(), seg.dataAt + seg.size);
        int guard = 0;
        while (nextElem(head, pos, end, &e) && guard++ < 4096) {
            if (e.id == kIdCluster) break;               // ab hier kommen Daten
            if (e.id == kIdInfo) {
                Elem g;
                qint64 p2 = e.dataAt;
                int guard2 = 0;
                while (nextElem(head, p2, e.dataAt + e.size, &g) && guard2++ < 256) {
                    p2 = g.next;
                    if (g.id == kIdTimeScale) {
                        const quint64 v = uintOf(head, g.dataAt, g.size);
                        if (v > 0) timeScale = v;
                    } else if (g.id == kIdDuration) {
                        durationTicks = floatOf(head, g.dataAt, g.size);
                    }
                }
            } else if (e.id == kIdTracks) {
                readTracks(head, e, &tracks);
            }
            pos = e.next;
        }
    }

    info->audioTracks = int(tracks.size());
    if (tracks.isEmpty()) return MkvAudio::Result::NoAudioTrack;

    info->tracks.clear();
    info->tracks.reserve(tracks.size());
    for (const Track& tr : tracks) {
        MkvAudio::TrackDesc d;
        d.codec     = tr.codec;
        d.language  = (tr.language == QStringLiteral("und")) ? QString() : tr.language;
        d.name      = tr.name;
        d.channels  = tr.channels;
        d.rate      = tr.rate;
        d.supported = (codecEntry(tr.codec) != nullptr);
        info->tracks.append(d);
    }

    if (trackIndex < 0 || trackIndex >= tracks.size()) trackIndex = 0;
    const Track& t = tracks.at(trackIndex);
    info->codec      = t.codec;
    info->sampleRate = t.rate;
    info->channels   = t.channels;
    info->durationMs = durationTicks > 0.0
                       ? qint64(durationTicks * double(timeScale) / 1000000.0) : 0;

    const CodecEntry* ce = codecEntry(t.codec);
    if (!ce) return MkvAudio::Result::UnsupportedCodec;
    const bool raw      = (ce->wrap == Wrap::Raw);
    const bool adts     = (ce->wrap == Wrap::Adts);
    const bool isOpus   = t.codec.startsWith(QStringLiteral("A_OPUS"));
    //  Nur die Ogg-Familie braucht `CodecPrivate`: dort steckt der Kopf, ohne
    //  den der Ton nicht dekodierbar wäre. AC-3 und MPEG-Audio haben gar keinen
    //  - ihr Kopf steht in jedem Rahmen.
    if (!raw && !adts && t.priv.isEmpty()) return MkvAudio::Result::Damaged;

    //  AAC: die Beschreibung muss LESBAR sein, sonst wird jeder Rahmen mit
    //  einem falschen Kopf versehen - eine Datei, die scheinbar geht und in
    //  Wahrheit rauscht. Lieber sauber ablehnen.
    AdtsConfig aac;
    if (adts) {
        aac = parseAsc(t.priv);
        if (!aac.ok) return MkvAudio::Result::UnsupportedCodec;
    }

    if (!out) {                                   // `probe`: hier ist Schluss
        info->result = MkvAudio::Result::Ok;
        return MkvAudio::Result::Ok;
    }

    OggWriter ogg(out, 0x4D470001u);              // fester Serienwert: „MG"

    if (raw || adts) {
    } else if (isOpus) {
        //  Bei Opus IST `CodecPrivate` der OpusHead. Die Kommentare erzeugen
        //  wir - Matroska führt sie an anderer Stelle, und ein Ogg-Opus ohne
        //  OpusTags ist ungültig.
        if (!ogg.write(t.priv, 0, /*bos=*/true, false)) return MkvAudio::Result::WriteFailed;
        QByteArray tags = QByteArrayLiteral("OpusTags");
        const QByteArray vendor = QByteArrayLiteral("MediaGallery");
        for (int i = 0; i < 4; ++i) tags.append(char((vendor.size() >> (8 * i)) & 0xFF));
        tags.append(vendor);
        tags.append(4, '\0');                     // keine Kommentare
        if (!ogg.write(tags, 0, false, false)) return MkvAudio::Result::WriteFailed;
    } else {
        const QByteArray& p = t.priv;
        if (p.size() < 3 || uchar(p[0]) != 2) return MkvAudio::Result::Damaged;
        qint64 at = 1;
        qint64 len[2] = { 0, 0 };
        for (int i = 0; i < 2; ++i) {
            while (true) {
                if (!have(p, at, 1)) return MkvAudio::Result::Damaged;
                const uchar v = uchar(p[int(at++)]);
                len[i] += v;
                if (v != 255) break;
            }
        }
        const qint64 third = p.size() - at - len[0] - len[1];
        if (len[0] <= 0 || len[1] <= 0 || third <= 0) return MkvAudio::Result::Damaged;
        const QByteArray h1 = p.mid(int(at), int(len[0]));
        const QByteArray h2 = p.mid(int(at + len[0]), int(len[1]));
        const QByteArray h3 = p.mid(int(at + len[0] + len[1]), int(third));
        if (!ogg.write(h1, 0, /*bos=*/true, false)) return MkvAudio::Result::WriteFailed;
        if (!ogg.write(h2, 0, false, false))        return MkvAudio::Result::WriteFailed;
        if (!ogg.write(h3, 0, false, false))        return MkvAudio::Result::WriteFailed;
    }

    // Die Cluster durchgehen
    //  Gelesen wird in Stücken: eine MKV kann Gigabyte groß sein, ein Cluster
    //  ist es nie.
    qint64 granule = 0;
    qint64 packets = 0;
    qint64 audioBytes = 0;
    QByteArrayView lastPacket;      // Sicht in `lastCluster`
    QByteArray     lastCluster;     // hält den Puffer der Sicht am Leben
    bool           havePending = false;
    qint64 lastGranule = 0;
    //  Nur für den rohen Weg: die Zeit des zuletzt gesehenen Blocks. Sie dient
    //  als Rückfall für die Dauer, wenn die Datei kein `Duration` führt.
    qint64 lastBlockMs = 0;
    QVector<QPair<qint64, qint64>> frames;

    qint64 pos = seg.dataAt;
    const qint64 segEnd = seg.unknownSize ? f.size()
                                          : std::min<qint64>(f.size(), seg.dataAt + seg.size);
    while (pos < segEnd) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return MkvAudio::Result::NotOpenable;
        if (!f.seek(pos)) return MkvAudio::Result::Damaged;
        const QByteArray hdr = f.read(12);
        if (hdr.size() < 2) break;
        quint32 id = 0;
        qint64 idLen = 0, size = 0, sizeLen = 0;
        bool unknown = false;
        if (!readId(hdr, 0, &id, &idLen)) return MkvAudio::Result::Damaged;
        if (!readSize(hdr, idLen, &size, &sizeLen, &unknown)) return MkvAudio::Result::Damaged;
        const qint64 dataAt = pos + idLen + sizeLen;
        if (unknown) size = segEnd - dataAt;
        if (size < 0 || dataAt > segEnd - size) return MkvAudio::Result::Damaged;

        if (id != kIdCluster) { pos = dataAt + size; continue; }
        if (size > kMaxHeaderBytes * 4) return MkvAudio::Result::TooLarge;

        if (!f.seek(dataAt)) return MkvAudio::Result::Damaged;
        const QByteArray cluster = f.read(size);
        if (cluster.size() != size) return MkvAudio::Result::Damaged;
        pos = dataAt + size;

        qint64 clusterTime = 0;
        Elem e;
        qint64 cp = 0;
        int guard = 0;
        while (nextElem(cluster, cp, cluster.size(), &e) && guard++ < 100000) {
            const qint64 nextPos = e.next;
            if (e.id == kIdTimestamp) {
                clusterTime = qint64(uintOf(cluster, e.dataAt, e.size));
            } else if (e.id == kIdSimpleBlock || e.id == kIdBlockGroup) {
                qint64 blockAt = e.dataAt, blockSize = e.size;
                if (e.id == kIdBlockGroup) {
                    Elem inner;
                    bool found = false;
                    qint64 bp = e.dataAt;
                    int g2 = 0;
                    while (nextElem(cluster, bp, e.dataAt + e.size, &inner) && g2++ < 256) {
                        bp = inner.next;
                        if (inner.id == kIdBlock) {
                            blockAt = inner.dataAt;
                            blockSize = inner.size;
                            found = true;
                            break;
                        }
                    }
                    if (!found) { cp = nextPos; continue; }
                }
                qint16 rel = 0;
                frames.resize(0);
                if (!readBlockFrames(cluster, blockAt, blockSize, t.number, &rel, &frames))
                    return MkvAudio::Result::Damaged;

                for (const auto& fr : frames) {
                    if (fr.second <= 0 || fr.second > kMaxPacket)
                        return MkvAudio::Result::Damaged;
                    if (++packets > kMaxPackets) return MkvAudio::Result::TooLarge;
                    const QByteArrayView packet(cluster.constData() + fr.first,
                                                fr.second);
                    audioBytes += packet.size();

                    // Roh: der Rahmen geht direkt in die Datei - keine Seite, keine Tafel, kein Zurückhalten, es gibt keine
                    // Ende-Markierung. AAC bekommt seinen 7-Byte-Kopf davor.
                    if (raw || adts) {
                        if (adts) {
                            const QByteArray h = adtsHeader(aac, packet.size());
                            if (out->write(h) != h.size())
                                return MkvAudio::Result::WriteFailed;
                        }
                        if (out->write(packet.data(), packet.size()) != packet.size())
                            return MkvAudio::Result::WriteFailed;
                        lastBlockMs = std::max<qint64>(
                            lastBlockMs,
                            qint64((clusterTime + rel) * double(timeScale) / 1000000.0));
                        continue;
                    }

                    //  Immer EIN Paket zurückhalten: das letzte trägt die
                    //  Ende-Markierung, und die kennt man erst, wenn keines
                    //  mehr kommt.
                    if (havePending) {
                        if (!ogg.write(lastPacket, lastGranule, false, false))
                            return MkvAudio::Result::WriteFailed;
                    }
                    if (isOpus) {
                        granule += opusPacketSamples(packet);
                    } else {
                        const qint64 ms = qint64((clusterTime + rel)
                                                 * double(timeScale) / 1000000.0);
                        granule = std::max<qint64>(granule,
                                                   ms * t.rate / 1000);
                    }
                    // Das zurückgehaltene Paket ist eine SICHT in den Cluster-Puffer; `lastCluster` hält den alten Puffer am Leben,
                    // damit sie beim nächsten Cluster gültig bleibt - eine QByteArray-Kopie ist nur ein Zähler, keine Daten.
                    lastPacket  = packet;
                    lastCluster = cluster;
                    havePending = true;
                    lastGranule = granule;
                }
            }
            cp = nextPos;
        }
    }

    if (packets == 0) return MkvAudio::Result::NoAudioTrack;
    if (!raw && !adts && !ogg.write(lastPacket, lastGranule, false, /*eos=*/true))
        return MkvAudio::Result::WriteFailed;

    info->packets    = int(std::min<qint64>(packets, INT32_MAX));
    info->audioBytes = audioBytes;
    if (info->durationMs == 0 && (raw || adts))
        info->durationMs = lastBlockMs;          // Zeit des letzten Rahmens
    else if (info->durationMs == 0 && t.rate > 0)
        info->durationMs = granule * 1000 / (isOpus ? 48000 : t.rate);
    info->result = MkvAudio::Result::Ok;
    return MkvAudio::Result::Ok;
}


bool readElemAt(QFile& f, qint64 pos, quint32* id, qint64* dataAt, qint64* size) {
    if (!f.seek(pos)) return false;
    const QByteArray hdr = f.read(12);
    if (hdr.size() < 2) return false;
    qint64 idLen = 0, szLen = 0;
    bool unknown = false;
    if (!readId(hdr, 0, id, &idLen)) return false;
    if (!readSize(hdr, idLen, size, &szLen, &unknown)) return false;
    *dataAt = pos + idLen + szLen;
    if (unknown) *size = f.size() - *dataAt;
    return *size >= 0;
}

bool clusterTimeAt(QFile& f, qint64 dataAt, qint64 clusterEnd, qint64* ticks) {
    quint32 id = 0;
    qint64 at = dataAt, elemAt = 0, size = 0;
    int guard = 0;
    while (at < clusterEnd && guard++ < 8) {
        if (!readElemAt(f, at, &id, &elemAt, &size)) return false;
        if (id == kIdTimestamp) {
            if (!f.seek(elemAt)) return false;
            const QByteArray v = f.read(std::min<qint64>(size, 8));
            quint64 t = 0;
            for (char c : v) t = (t << 8) | uchar(c);
            *ticks = qint64(t);
            return true;
        }
        at = elemAt + size;
    }
    return false;
}

// Die Zielstelle finden
//  Erst über das Inhaltsverzeichnis (`Cues`, per `SeekHead` gefunden), sonst
//  durch Abschreiten der Cluster-Köpfe. Beides liest NUR Köpfe.
struct StreamStart {
    bool   ok = false;
    qint64 clusterPos = 0;      // Dateiposition des Clusters
    qint64 clusterMs  = 0;      // seine Zeit
};

qint64 cuesPositionOf(QFile& f, qint64 segDataStart, qint64 segEnd) {
    quint32 id = 0;
    qint64 at = segDataStart, dataAt = 0, size = 0;
    int guard = 0;
    while (at < segEnd && guard++ < 32) {
        if (!readElemAt(f, at, &id, &dataAt, &size)) return -1;
        if (id == kIdCluster) return -1;                    // ab hier kommen Daten
        if (id == kIdSeekHead) {
            const qint64 end = dataAt + size;
            qint64 p = dataAt;
            int g2 = 0;
            while (p < end && g2++ < 64) {
                quint32 sid = 0;
                qint64 sAt = 0, sSize = 0;
                if (!readElemAt(f, p, &sid, &sAt, &sSize)) break;
                if (sid == kIdSeek) {
                    qint64 q = sAt;
                    quint64 what = 0, where = 0;
                    int g3 = 0;
                    while (q < sAt + sSize && g3++ < 16) {
                        quint32 iid = 0;
                        qint64 iAt = 0, iSize = 0;
                        if (!readElemAt(f, q, &iid, &iAt, &iSize)) break;
                        if (!f.seek(iAt)) break;
                        const QByteArray v = f.read(std::min<qint64>(iSize, 8));
                        quint64 val = 0;
                        for (char c : v) val = (val << 8) | uchar(c);
                        if (iid == kIdSeekId) what = val;
                        else if (iid == kIdSeekPos) where = val;
                        q = iAt + iSize;
                    }
                    if (what == kIdCues) return segDataStart + qint64(where);
                }
                p = sAt + sSize;
            }
        }
        at = dataAt + size;
    }
    return -1;
}

StreamStart findStartViaCues(QFile& f, qint64 segDataStart, qint64 segEnd,
                             quint64 timeScale, qint64 targetMs) {
    StreamStart out;
    const qint64 cuesAt = cuesPositionOf(f, segDataStart, segEnd);
    if (cuesAt < 0) return out;

    quint32 id = 0;
    qint64 dataAt = 0, size = 0;
    if (!readElemAt(f, cuesAt, &id, &dataAt, &size) || id != kIdCues) return out;
    if (size > kMaxHeaderBytes) return out;                 // 16 MB Deckel

    const qint64 end = dataAt + size;
    qint64 p = dataAt;
    int guard = 0;
    while (p < end && guard++ < 200000) {
        quint32 pid = 0;
        qint64 pAt = 0, pSize = 0;
        if (!readElemAt(f, p, &pid, &pAt, &pSize)) break;
        if (pid == kIdCuePoint) {
            qint64 q = pAt;
            quint64 cueTime = 0, cuePos = 0;
            int g2 = 0;
            while (q < pAt + pSize && g2++ < 64) {
                quint32 iid = 0;
                qint64 iAt = 0, iSize = 0;
                if (!readElemAt(f, q, &iid, &iAt, &iSize)) break;
                if (iid == kIdCueTime) {
                    if (!f.seek(iAt)) break;
                    const QByteArray v = f.read(std::min<qint64>(iSize, 8));
                    cueTime = 0;
                    for (char c : v) cueTime = (cueTime << 8) | uchar(c);
                } else if (iid == kIdCueTrackPositions) {
                    qint64 r = iAt;
                    int g3 = 0;
                    while (r < iAt + iSize && g3++ < 32) {
                        quint32 jid = 0;
                        qint64 jAt = 0, jSize = 0;
                        if (!readElemAt(f, r, &jid, &jAt, &jSize)) break;
                        if (jid == kIdCueClusterPos) {
                            if (!f.seek(jAt)) break;
                            const QByteArray v = f.read(std::min<qint64>(jSize, 8));
                            cuePos = 0;
                            for (char c : v) cuePos = (cuePos << 8) | uchar(c);
                        }
                        r = jAt + jSize;
                    }
                }
                q = iAt + iSize;
            }
            const qint64 ms = qint64(double(cueTime) * double(timeScale) / 1000000.0);
            if (cuePos > 0 && ms <= targetMs) {
                out.ok = true;
                out.clusterPos = segDataStart + qint64(cuePos);
                out.clusterMs  = ms;
            } else if (ms > targetMs) {
                break;                                      // Cues sind sortiert
            }
        }
        p = pAt + pSize;
    }
    return out;
}

StreamStart findStartByWalking(QFile& f, qint64 segDataStart, qint64 segEnd,
                               quint64 timeScale, qint64 targetMs) {
    StreamStart out;
    qint64 at = segDataStart;
    int guard = 0;
    while (at < segEnd && guard++ < 2000000) {
        quint32 id = 0;
        qint64 dataAt = 0, size = 0;
        if (!readElemAt(f, at, &id, &dataAt, &size)) break;
        if (id == kIdCluster) {
            qint64 ticks = 0;
            if (clusterTimeAt(f, dataAt, dataAt + size, &ticks)) {
                const qint64 ms = qint64(double(ticks) * double(timeScale) / 1000000.0);
                if (ms > targetMs) return out.ok ? out : StreamStart{ true, at, ms };
                out = StreamStart{ true, at, ms };
            }
        }
        at = dataAt + size;
    }
    return out;
}

class RawStreamDevice : public QIODevice {
public:
    RawStreamDevice(const QString& path, quint64 trackNumber, qint64 firstCluster,
                    qint64 segEnd, bool adts, const AdtsConfig& aac, QObject* parent)
        : QIODevice(parent), m_file(path), m_track(trackNumber)
        , m_at(firstCluster), m_segEnd(segEnd), m_adts(adts), m_aac(aac) {}

    bool open(OpenMode mode) override {
        if (!m_file.open(QIODevice::ReadOnly)) return false;
        if (!QIODevice::open((mode & ~QIODevice::WriteOnly) | QIODevice::ReadOnly))
            return false;
        //  VORFÜLLEN ist Pflicht, nicht Beschleunigung: ein sequenzielles Gerät
        //  ohne verfügbare Bytes gilt als am ENDE (`QIODevice::atEnd`), und der
        //  Dekoder dekodiert dann gar nichts (gemessen: kein einziger Rahmen).
        while (m_buf.size() < (256 << 10) && fillOneCluster()) { }
        return true;
    }
    void close() override { m_file.close(); QIODevice::close(); }
    //  BEWUSST sequenziell: der Dekoder soll nicht zurückspulen können - er
    //  bekommt einen Strom, keine Datei.
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override {
        return m_buf.size() - m_out + QIODevice::bytesAvailable();
    }
    //  Am Ende ist der Strom erst, wenn KEIN Cluster mehr kommt - nicht schon
    //  dann, wenn der Puffer gerade leer ist.
    bool atEnd() const override {
        return m_finished && (m_buf.size() - m_out) <= 0;
    }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        while (m_buf.size() - m_out < maxlen && fillOneCluster()) { }
        const qint64 have = std::min<qint64>(maxlen, m_buf.size() - m_out);
        if (have <= 0) return 0;
        memcpy(data, m_buf.constData() + m_out, size_t(have));
        m_out += have;
        if (m_out > (4 << 20)) { m_buf.remove(0, int(m_out)); m_out = 0; }
        return have;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    bool fillOneCluster() {
        while (m_at < m_segEnd) {
            quint32 id = 0;
            qint64 dataAt = 0, size = 0;
            if (!readElemAt(m_file, m_at, &id, &dataAt, &size)) return false;
            if (id != kIdCluster) { m_at = dataAt + size; continue; }
            if (size > kMaxHeaderBytes * 4) return false;
            if (!m_file.seek(dataAt)) return false;
            const QByteArray cluster = m_file.read(size);
            m_at = dataAt + size;
            if (cluster.size() != size) return false;

            Elem e;
            qint64 cp = 0;
            int guard = 0;
            while (nextElem(cluster, cp, cluster.size(), &e) && guard++ < 100000) {
                const qint64 nextPos = e.next;
                if (e.id == kIdSimpleBlock || e.id == kIdBlockGroup) {
                    qint64 blockAt = e.dataAt, blockSize = e.size;
                    if (e.id == kIdBlockGroup) {
                        Elem inner;
                        bool found = false;
                        qint64 bp = e.dataAt;
                        int g2 = 0;
                        while (nextElem(cluster, bp, e.dataAt + e.size, &inner) && g2++ < 256) {
                            bp = inner.next;
                            if (inner.id == kIdBlock) {
                                blockAt = inner.dataAt; blockSize = inner.size; found = true;
                                break;
                            }
                        }
                        if (!found) { cp = nextPos; continue; }
                    }
                    qint16 rel = 0;
                    m_frames.resize(0);
                    if (readBlockFrames(cluster, blockAt, blockSize, m_track, &rel, &m_frames)) {
                        for (const auto& fr : m_frames) {
                            if (fr.second <= 0 || fr.second > kMaxPacket) continue;
                            if (m_adts)
                                m_buf.append(adtsHeader(m_aac, fr.second));
                            m_buf.append(cluster.constData() + fr.first, int(fr.second));
                        }
                    }
                }
                cp = nextPos;
            }
            return true;
        }
        m_finished = true;
        return false;
    }

    QFile      m_file;
    quint64    m_track;
    qint64     m_at;
    qint64     m_segEnd;
    bool       m_adts;
    AdtsConfig m_aac;
    QByteArray m_buf;
    qint64     m_out = 0;
    bool       m_finished = false;
    QVector<QPair<qint64, qint64>> m_frames;
};

} // namespace

namespace MkvAudio {

bool isCandidate(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("mkv") || ext == QLatin1String("webm")
        || ext == QLatin1String("mka");
}

Info probe(const QString& path) {
    Info info;
    const Result r = run(path, &info, nullptr, nullptr);
    info.result = r;
    return info;
}

Info probeTrack(const QString& path, int trackIndex) {
    Info info;
    const Result r = run(path, &info, nullptr, nullptr, trackIndex);
    info.result = r;
    return info;
}

QString targetPathFor(const QString& srcPath, int trackIndex) {
    const Info info = probeTrack(srcPath, trackIndex);
    const QString ext = info.ok() ? extensionFor(info.codec) : QStringLiteral("ogg");
    const QFileInfo fi(srcPath);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName();
    QString candidate = dir + QLatin1Char('/') + base + QLatin1Char('.') + ext;
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).").arg(n) + ext;
        ++n;
    }
    return candidate;
}

QIODevice* openRawStream(const QString& srcPath, int trackIndex, qint64 startMs,
                         qint64* actualMs, QObject* parent) {
    if (actualMs) *actualMs = 0;

    QFile f(srcPath);
    if (!f.open(QIODevice::ReadOnly) || f.size() < 32) return nullptr;
    const QByteArray head = f.read(std::min<qint64>(f.size(), kMaxHeaderBytes));
    if (head.size() < 32) return nullptr;

    Elem ebml, seg;
    if (!nextElem(head, 0, head.size(), &ebml) || ebml.id != kIdEbml) return nullptr;
    if (!segmentElem(head, ebml.next, head.size(), f.size(), &seg)
        || seg.id != kIdSegment) return nullptr;

    quint64 timeScale = 1000000;
    QVector<Track> tracks;
    {
        Elem e;
        qint64 pos = seg.dataAt;
        const qint64 end = std::min<qint64>(head.size(), seg.dataAt + seg.size);
        int guard = 0;
        while (nextElem(head, pos, end, &e) && guard++ < 4096) {
            if (e.id == kIdCluster) break;
            if (e.id == kIdInfo) {
                Elem g;
                qint64 p2 = e.dataAt;
                int g2 = 0;
                while (nextElem(head, p2, e.dataAt + e.size, &g) && g2++ < 256) {
                    p2 = g.next;
                    if (g.id == kIdTimeScale) {
                        const quint64 v = uintOf(head, g.dataAt, g.size);
                        if (v > 0) timeScale = v;
                    }
                }
            } else if (e.id == kIdTracks) {
                readTracks(head, e, &tracks);
            }
            pos = e.next;
        }
    }
    if (tracks.isEmpty()) return nullptr;
    if (trackIndex < 0 || trackIndex >= tracks.size()) trackIndex = 0;
    const Track& t = tracks.at(trackIndex);

    //  2) Taugt der Codec für einen ROHEN Strom? Opus und Vorbis brauchen eine
    //     Ogg-Hülle mit Kopfpaketen - das kann dieser Weg nicht.
    const CodecEntry* ce = codecEntry(t.codec);
    if (!ce || ce->wrap == Wrap::Ogg) return nullptr;
    AdtsConfig aac;
    const bool adts = (ce->wrap == Wrap::Adts);
    if (adts) {
        aac = parseAsc(t.priv);
        if (!aac.ok) return nullptr;
    }

    const qint64 segEnd = seg.unknownSize ? f.size()
                                          : std::min<qint64>(f.size(), seg.dataAt + seg.size);
    StreamStart start = findStartViaCues(f, seg.dataAt, segEnd, timeScale, startMs);
    if (!start.ok)
        start = findStartByWalking(f, seg.dataAt, segEnd, timeScale, startMs);
    if (!start.ok) return nullptr;

    if (actualMs) *actualMs = start.clusterMs;
    auto* dev = new RawStreamDevice(srcPath, t.number, start.clusterPos, segEnd,
                                    adts, aac, parent);
    if (!dev->open(QIODevice::ReadOnly)) { delete dev; return nullptr; }
    return dev;
}

Result extract(const QString& srcPath, const QString& targetPath, Info* infoOut,
               const std::atomic<bool>* cancel, int trackIndex) {
    Info info;
    QSaveFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly)) {
        info.result = Result::WriteFailed;
        if (infoOut) *infoOut = info;
        return Result::WriteFailed;
    }
    const Result r = run(srcPath, &info, &out, cancel, trackIndex);
    info.result = r;
    if (infoOut) *infoOut = info;
    if (r != Result::Ok) return r;               // ohne commit: keine halbe Datei
    return out.commit() ? Result::Ok : Result::WriteFailed;
}

} // namespace MkvAudio
