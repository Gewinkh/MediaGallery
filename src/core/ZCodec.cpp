#include "core/ZCodec.h"

#include <QtEndian>

// CRC-32 für beide Bauarten als Eigenimplementierung: zlibs `crc32()` wäre gleichwertig, aber dann liefen zwei
// Rechenwege durch die Tests statt einem. Die Tabelle kostet 1 KB und ist bitgleich.
namespace {

struct Crc32Table {
    quint32 v[256];
    constexpr Crc32Table() : v() {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            v[i] = c;
        }
    }
};
constexpr Crc32Table kCrc;

}  // namespace

namespace mg::zcodec {

quint32 crc32(const QByteArray& data) {
    quint32 c = 0xFFFFFFFFu;
    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    for (qsizetype i = 0, n = data.size(); i < n; ++i)
        c = kCrc.v[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

}  // namespace mg::zcodec


#ifdef MG_HAVE_ZLIB
//  Mit zlib: direkter Weg. Die Schleifen entsprechen dem, was vorher in
//  PdfObjects/PdfPageCopier/PdfAudioController/DocxZip je einzeln stand.
#include <zlib.h>
#include <cstring>

namespace mg::zcodec {

bool available() { return true; }

namespace {
int windowBitsOf(Wrap w) {
    switch (w) {
        case Wrap::Raw:  return -15;
        case Wrap::Auto: return 15 + 32;
        case Wrap::Zlib: break;
    }
    return 15;
}
}  // namespace

QByteArray inflate(const QByteArray& src, Wrap wrap, qint64 sizeHint,
                   bool tolerant, bool* ok) {
    if (ok) *ok = false;
    if (src.isEmpty() || sizeHint > kMaxOutput) return {};

    z_stream zs; std::memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, windowBitsOf(wrap)) != Z_OK) return {};
    zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(src.constData()));
    zs.avail_in = static_cast<uInt>(qMin<qsizetype>(src.size(), 0x7FFFFFFF));

    QByteArray out;
    if (sizeHint > 0) out.reserve(sizeHint);
    char buf[1 << 15];
    int rc = Z_OK;
    while (rc == Z_OK) {
        zs.next_out  = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        rc = ::inflate(&zs, Z_NO_FLUSH);
        const qint64 produced = qint64(sizeof(buf)) - zs.avail_out;
        if (produced > 0) out.append(buf, produced);
        if (out.size() > kMaxOutput) { rc = Z_MEM_ERROR; break; }
    }
    inflateEnd(&zs);

    const bool good = (rc == Z_STREAM_END)
                   || (tolerant && !out.isEmpty()
                       && (rc == Z_OK || rc == Z_BUF_ERROR));
    if (!good) return {};
    if (ok) *ok = true;
    return out;
}

QByteArray deflate(const QByteArray& src, Wrap wrap, int level, bool* ok) {
    if (ok) *ok = false;
    z_stream zs; std::memset(&zs, 0, sizeof(zs));
    const int wb = (wrap == Wrap::Raw) ? -15 : 15;
    if (deflateInit2(&zs, level, Z_DEFLATED, wb, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(src.constData()));
    zs.avail_in = static_cast<uInt>(qMin<qsizetype>(src.size(), 0x7FFFFFFF));

    QByteArray out;
    char buf[1 << 15];
    int rc = Z_OK;
    do {
        zs.next_out  = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        rc = ::deflate(&zs, Z_FINISH);
        const qint64 produced = qint64(sizeof(buf)) - zs.avail_out;
        if (produced > 0) out.append(buf, produced);
    } while (rc == Z_OK);
    deflateEnd(&zs);

    if (rc != Z_STREAM_END) return {};
    if (ok) *ok = true;
    return out;
}

}  // namespace mg::zcodec

#else
// Ohne zlib: Qt-Codecs. `qCompress`-Ausgabe = [4 B Größe, big-endian][2 B zlib-Kopf][Deflate][4 B Adler-32];
// `qUncompress` erwartet genau dieselbe Form.
namespace mg::zcodec {

bool available() { return false; }

QByteArray inflate(const QByteArray& src, Wrap wrap, qint64 sizeHint,
                   bool /*tolerant*/, bool* ok) {
    if (ok) *ok = false;
    if (src.isEmpty() || sizeHint > kMaxOutput) return {};

    //  Roh-Deflate ist ohne zlib nicht entpackbar: qUncompress prüft am Ende
    //  einen Adler-32 über den entpackten Daten - der lässt sich vorher nicht
    //  bilden. Betrifft ZIP-Einträge (DOCX-Lesen).
    if (wrap == Wrap::Raw) return {};
    //  gzip-Kopf (1F 8B) kann qUncompress ebenfalls nicht; in PDF kommt er
    //  nicht vor (PDF kennt keinen gzip-Filter), deshalb nur abweisen.
    if (wrap == Wrap::Auto && src.size() >= 2
        && quint8(src[0]) == 0x1Fu && quint8(src[1]) == 0x8Bu)
        return {};

    // Die Größenangabe ist für `qUncompress` nur der ERSTE Puffer - zu klein verdoppelt Qt selbst und entpackt
    // erneut. Faktor 6 trifft die üblichen Verhältnisse von PDF-Inhaltsströmen.
    qint64 hint = (sizeHint > 0) ? sizeHint : src.size() * 6;
    hint = qBound<qint64>(1, hint, kMaxOutput);

    QByteArray framed(4, '\0');
    qToBigEndian<quint32>(quint32(hint), framed.data());
    framed += src;

    const QByteArray out = qUncompress(framed);
    //  Leer heißt hier immer Fehler: ein Strom, der nichts erzeugt, ist für
    //  jeden Aufrufer im Projekt genauso wertlos wie ein kaputter.
    if (out.isEmpty() || out.size() > kMaxOutput) return {};
    if (ok) *ok = true;
    return out;
}

QByteArray deflate(const QByteArray& src, Wrap wrap, int level, bool* ok) {
    if (ok) *ok = false;
    const bool raw = (wrap == Wrap::Raw);

    // Leere Eingabe: `qCompress` liefert nur die 4 Byte Größenangabe und gar keinen Deflate-Strom. Beide Rahmen
    // deshalb von Hand - 03 00 ist der leere Endblock, 00 00 00 01 der Adler-32 von nichts.
    if (src.isEmpty()) {
        if (ok) *ok = true;
        return raw ? QByteArray("\x03\x00", 2)
                   : QByteArray("\x78\x01\x03\x00\x00\x00\x00\x01", 8);
    }

    const QByteArray q = qCompress(src, level);
    if (q.size() < 4 + 2 + 4) return {};        // Kopf + Rahmen passen nicht
    if (ok) *ok = true;
    return raw ? q.mid(6, q.size() - 10)        // ohne Größe, zlib-Kopf, Adler
               : q.mid(4);                      // ohne Größe
}

}  // namespace mg::zcodec

#endif  // MG_HAVE_ZLIB
