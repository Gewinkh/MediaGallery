#include "audio/AudioSeekIndex.h"

#include <QFileInfo>

namespace {

//  Wie viel wird am Stück gelesen? Gross genug, dass ein Rahmen (höchstens
//  ~4 kB) immer ganz darin liegt, klein genug, dass nichts hängt.
constexpr qint64 kChunk    = 256 * 1024;
constexpr qint64 kMaxFrame = 64 * 1024;      // Schutzgrenze je Rahmen

// ── MPEG-1/2/2.5 Audio (MP1/MP2/MP3) ────────────────────────────────────────
//  Kopf: 11 Bit Sync, 2 Bit Version, 2 Bit Layer, 1 Bit CRC, 4 Bit Bitrate,
//  2 Bit Abtastrate, 1 Bit Padding. Länge und Abtastwerte folgen daraus.
struct FrameInfo {
    qint64 length  = 0;      // Bytes dieses Rahmens
    int    samples = 0;      // Abtastwerte darin
    int    rate    = 0;      // Hz
};

bool mpegFrame(const uchar* p, qint64 avail, FrameInfo* out) {
    if (avail < 4) return false;
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return false;

    const int verId = (p[1] >> 3) & 0x03;         // 0=2.5, 2=2, 3=1
    const int layer = (p[1] >> 1) & 0x03;         // 3=I, 2=II, 1=III
    const int brIdx = (p[2] >> 4) & 0x0F;
    const int srIdx = (p[2] >> 2) & 0x03;
    const int pad   = (p[2] >> 1) & 0x01;
    if (verId == 1 || layer == 0 || brIdx == 0 || brIdx == 15 || srIdx == 3) return false;

    //  Bitraten in kbit/s, je Version und Schicht.
    static const int kBitV1L1[15] = { 0,32,64,96,128,160,192,224,256,288,320,352,384,416,448 };
    static const int kBitV1L2[15] = { 0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384 };
    static const int kBitV1L3[15] = { 0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320 };
    static const int kBitV2L1[15] = { 0,32,48,56, 64, 80, 96,112,128,144,160,176,192,224,256 };
    static const int kBitV2L23[15]= { 0, 8,16,24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160 };
    static const int kRateV1[3]   = { 44100, 48000, 32000 };

    const bool v1 = (verId == 3);
    const int* table = v1 ? (layer == 3 ? kBitV1L1 : layer == 2 ? kBitV1L2 : kBitV1L3)
                          : (layer == 3 ? kBitV2L1 : kBitV2L23);
    const int kbit = table[brIdx];
    if (kbit <= 0) return false;

    int rate = kRateV1[srIdx];
    if (verId == 2) rate /= 2;                    // MPEG-2
    else if (verId == 0) rate /= 4;               // MPEG-2.5
    if (rate <= 0) return false;

    //  Abtastwerte je Rahmen: Schicht I 384, Schicht II 1152, Schicht III 1152
    //  (bei MPEG-2/2.5 nur 576).
    const int samples = (layer == 3) ? 384 : (v1 ? 1152 : 576);
    const qint64 len = (layer == 3)
                           ? (qint64(12) * kbit * 1000 / rate + pad) * 4
                           :  qint64(samples / 8) * kbit * 1000 / rate + pad;
    if (len < 8 || len > kMaxFrame) return false;

    *out = FrameInfo { len, samples, rate };
    return true;
}

// ── AC-3 und E-AC-3 ─────────────────────────────────────────────────────────
//  Beide beginnen mit 0x0B77. AC-3 nennt seine Länge über `frmsizecod`,
//  E-AC-3 direkt über `frmsiz` (in 16-Bit-Wörtern).
bool ac3Frame(const uchar* p, qint64 avail, FrameInfo* out) {
    if (avail < 6) return false;
    if (p[0] != 0x0B || p[1] != 0x77) return false;

    static const int kRate[3] = { 48000, 44100, 32000 };
    const int bsid = (p[5] >> 3) & 0x1F;

    if (bsid > 10 && bsid <= 16) {                // E-AC-3
        const int frmsiz = ((p[2] & 0x07) << 8) | p[3];
        const int fscod  = (p[4] >> 6) & 0x03;
        const int numblk = (p[4] >> 4) & 0x03;
        if (fscod == 3) return false;             // „fscod2" - halbe Rate, selten
        static const int kBlocks[4] = { 1, 2, 3, 6 };
        const qint64 len = (qint64(frmsiz) + 1) * 2;
        if (len < 8 || len > kMaxFrame) return false;
        *out = FrameInfo { len, kBlocks[numblk] * 256, kRate[fscod] };
        return true;
    }
    if (bsid <= 10) {                             // AC-3
        const int fscod      = (p[4] >> 6) & 0x03;
        const int frmsizecod = p[4] & 0x3F;
        if (fscod == 3 || frmsizecod >= 38) return false;
        //  Rahmenlänge in 16-Bit-Wörtern, je Abtastrate (Norm A/52, Tabelle 5.18).
        static const quint16 kSize[38][3] = {
            {64,69,96},{64,70,96},{80,87,120},{80,88,120},{96,104,144},{96,105,144},
            {112,121,168},{112,122,168},{128,139,192},{128,140,192},{160,174,240},
            {160,175,240},{192,208,288},{192,209,288},{224,243,336},{224,244,336},
            {256,278,384},{256,279,384},{320,348,480},{320,349,480},{384,417,576},
            {384,418,576},{448,487,672},{448,488,672},{512,557,768},{512,558,768},
            {640,696,960},{640,697,960},{768,835,1152},{768,836,1152},{896,975,1344},
            {896,976,1344},{1024,1114,1536},{1024,1115,1536},{1152,1253,1728},
            {1152,1254,1728},{1280,1393,1920},{1280,1394,1920}
        };
        const qint64 len = qint64(kSize[frmsizecod][fscod]) * 2;
        if (len < 8 || len > kMaxFrame) return false;
        *out = FrameInfo { len, 1536, kRate[fscod] };   // AC-3: immer 6 Blöcke
        return true;
    }
    return false;
}

// ── AAC in ADTS ─────────────────────────────────────────────────────────────
bool adtsFrame(const uchar* p, qint64 avail, FrameInfo* out) {
    if (avail < 7) return false;
    if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0) return false;   // Sync + Layer 00

    static const int kRate[13] = { 96000, 88200, 64000, 48000, 44100, 32000,
                                   24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    const int rateIdx = (p[2] >> 2) & 0x0F;
    if (rateIdx > 12) return false;
    const qint64 len = (qint64(p[3] & 0x03) << 11) | (qint64(p[4]) << 3) | (p[5] >> 5);
    if (len < 7 || len > kMaxFrame) return false;
    //  Ein ADTS-Rahmen kann mehrere „raw data blocks" tragen; je Block 1024.
    const int blocks = (p[6] & 0x03) + 1;
    *out = FrameInfo { len, 1024 * blocks, kRate[rateIdx] };
    return true;
}

//  Welcher Leser passt zur Endung? Ein falsch geratener Leser findet keinen
//  gültigen Rahmen und meldet das sauber - geraten wird trotzdem nicht.
using FrameFn = bool (*)(const uchar*, qint64, FrameInfo*);

FrameFn readerFor(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("mp3") || ext == QLatin1String("mp2")
        || ext == QLatin1String("mp1") || ext == QLatin1String("mpga"))
        return &mpegFrame;
    if (ext == QLatin1String("ac3") || ext == QLatin1String("eac3")
        || ext == QLatin1String("ec3"))
        return &ac3Frame;
    if (ext == QLatin1String("aac") || ext == QLatin1String("adts"))
        return &adtsFrame;
    return nullptr;
}

//  ID3v2 am Anfang überspringen (MP3-Dateien tragen es fast immer).
qint64 skipId3(QFile& f) {
    char hdr[10];
    if (f.read(hdr, 10) != 10) return 0;
    if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') return 0;
    const uchar* p = reinterpret_cast<const uchar*>(hdr);
    const qint64 size = (qint64(p[6] & 0x7F) << 21) | (qint64(p[7] & 0x7F) << 14)
                      | (qint64(p[8] & 0x7F) << 7)  |  qint64(p[9] & 0x7F);
    return 10 + size;
}

}  // namespace

namespace AudioSeek {

bool isSelfFraming(const QString& path) {
    return readerFor(path) != nullptr;
}

Position findFrame(const QString& path, qint64 targetMs,
                   const std::atomic<bool>* cancel) {
    Position pos;
    const FrameFn frame = readerFor(path);
    if (!frame) return pos;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return pos;

    qint64 at = skipId3(f);
    if (!f.seek(at)) return pos;

    qint64 samples = 0;
    int    rate    = 0;
    QByteArray buf;
    //  Der zuletzt gesehene Rahmen, der NICHT hinter der Zielstelle beginnt.
    //  Genau der wird gesucht: der Aufrufer setzt dort auf und überspringt den
    //  kleinen Rest bis zur genauen Stelle. Ein Rahmen DAHINTER wäre falsch -
    //  der Ton begänne nach der Stelle, und der Aufrufer könnte nichts mehr
    //  überspringen (gefunden am 2026-08-21: genau daran fiel jeder Sprung
    //  zurück auf den langsamen Weg, wenn die Zielzeit nicht zufällig auf eine
    //  Rahmengrenze fiel).
    qint64 lastAt = -1, lastSamples = 0;

    while (true) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return pos;

        //  Nachladen, wenn weniger als ein größtmöglicher Rahmen im Puffer ist.
        if (buf.size() < kMaxFrame) {
            const QByteArray more = f.read(kChunk);
            if (more.isEmpty() && buf.isEmpty()) break;
            buf.append(more);
            if (more.isEmpty() && buf.size() < 4) break;
        }

        const uchar* p = reinterpret_cast<const uchar*>(buf.constData());
        FrameInfo fi;
        if (!frame(p, buf.size(), &fi)) {
            //  Kein gültiger Kopf: ein Byte weiter suchen. So werden auch
            //  Datenreste zwischen den Rahmen übergangen (Tags am Ende, Müll).
            buf.remove(0, 1);
            ++at;
            if (buf.isEmpty() && f.atEnd()) break;
            continue;
        }
        if (rate == 0) rate = fi.rate;

        //  Beginnt dieser Rahmen HINTER der Stelle, war der vorige der
        //  gesuchte (bei `targetMs` 0 ist es der erste).
        if (rate > 0 && samples * 1000 / rate > targetMs) {
            pos.ok = true;
            pos.byteOffset = lastAt >= 0 ? lastAt : at;
            pos.sampleAt   = lastAt >= 0 ? lastSamples : samples;
            pos.sampleRate = rate;
            return pos;
        }
        lastAt      = at;
        lastSamples = samples;

        if (fi.length > buf.size()) {
            //  Rahmen reicht über den Puffer hinaus: nachladen statt raten.
            const QByteArray more = f.read(kChunk);
            if (more.isEmpty()) break;
            buf.append(more);
            continue;
        }
        buf.remove(0, int(fi.length));
        at      += fi.length;
        samples += fi.samples;
    }

    //  Über das Ende hinaus gesucht: dann gilt der letzte gesehene Rahmen.
    if (rate > 0) {
        pos.ok = true;
        pos.byteOffset = lastAt >= 0 ? lastAt : at;
        pos.sampleAt   = lastAt >= 0 ? lastSamples : samples;
        pos.sampleRate = rate;
    }
    return pos;
}

// ── TailDevice ──────────────────────────────────────────────────────────────
TailDevice::TailDevice(const QString& path, qint64 from, QObject* parent)
    : QIODevice(parent), m_file(path), m_from(std::max<qint64>(0, from)) {}

bool TailDevice::open(OpenMode mode) {
    if (!m_file.open(QIODevice::ReadOnly)) return false;
    if (m_from > m_file.size()) m_from = m_file.size();
    if (!m_file.seek(m_from)) { m_file.close(); return false; }
    return QIODevice::open((mode & ~QIODevice::WriteOnly) | QIODevice::ReadOnly);
}

void TailDevice::close() {
    m_file.close();
    QIODevice::close();
}

qint64 TailDevice::size() const {
    return std::max<qint64>(0, m_file.size() - m_from);
}

bool TailDevice::seek(qint64 pos) {
    if (pos < 0 || pos > size()) return false;
    if (!QIODevice::seek(pos)) return false;
    return m_file.seek(m_from + pos);
}

qint64 TailDevice::readData(char* data, qint64 maxlen) {
    return m_file.read(data, maxlen);
}

}  // namespace AudioSeek
