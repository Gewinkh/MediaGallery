#include "pdf/edit/PdfImageEmbed.h"
#include "pdf/edit/PdfObjects.h"

#include <QImage>
#include <QImageReader>

using namespace mg::pdfobj;

// Die Erläuterungen zum Verfahren stehen im Header.
namespace mg {

bool PdfImageEmbed::encode(const QImage& img, PdfImageData* out,
                           int maxEdgePx, QString* err) {
    auto fail = [&](const char* m) {
        if (err) *err = QString::fromLatin1(m);
        return false;
    };
    if (!out)
        return false;
    *out = PdfImageData();
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
        return fail("leeres Bild");

    //  Verkleinern, BEVOR die Pixel angefasst werden - sonst liegt kurzzeitig
    //  das volle Bild zusätzlich als RGB-Puffer im Speicher.
    QImage src = img;
    const int edge = qMax(src.width(), src.height());
    if (maxEdgePx > 0 && edge > maxEdgePx)
        src = src.scaled(src.size() * (double(maxEdgePx) / edge),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (src.isNull())
        return fail("Skalieren fehlgeschlagen");

    const bool alpha = src.hasAlphaChannel();
    src = src.convertToFormat(alpha ? QImage::Format_RGBA8888
                                    : QImage::Format_RGB888);
    if (src.isNull())
        return fail("Format nicht wandelbar");

    const int w = src.width(), h = src.height();
    QByteArray rgb;
    rgb.reserve(qsizetype(w) * h * 3);
    QByteArray a8;
    if (alpha)
        a8.reserve(qsizetype(w) * h);

    for (int y = 0; y < h; ++y) {
        const uchar* line = src.constScanLine(y);
        if (alpha) {
            for (int x = 0; x < w; ++x) {
                const uchar* px = line + qsizetype(x) * 4;
                rgb.append(char(px[0]));
                rgb.append(char(px[1]));
                rgb.append(char(px[2]));
                a8.append(char(px[3]));
            }
        } else {
            //  RGB888-Zeilen sind auf 4 Byte ausgerichtet - nur die echten
            //  Bilddaten übernehmen, nicht die Auffüllbytes.
            rgb.append(reinterpret_cast<const char*>(line), qsizetype(w) * 3);
        }
    }

    //  Vollständig deckend? Dann spart die Maske nur Platz und Verwirrung.
    if (alpha) {
        bool anyTransparent = false;
        for (char c : a8)
            if (static_cast<unsigned char>(c) != 0xFF) { anyTransparent = true; break; }
        if (!anyTransparent)
            a8.clear();
    }

    out->width  = w;
    out->height = h;
    out->rgb    = zDeflate(rgb);
    out->alpha  = a8.isEmpty() ? QByteArray() : zDeflate(a8);
    if (out->rgb.isEmpty())
        return fail("Packen fehlgeschlagen");
    return true;
}

bool PdfImageEmbed::encodeFile(const QString& path, PdfImageData* out,
                               int maxEdgePx, QString* err) {
    QImageReader reader(path);
    reader.setAutoTransform(true);          // EXIF-Drehung berücksichtigen
    const QImage img = reader.read();
    if (img.isNull()) {
        if (err) *err = QStringLiteral("Bild nicht lesbar: %1").arg(reader.errorString());
        return false;
    }
    return encode(img, out, maxEdgePx, err);
}

QByteArray PdfImageEmbed::imageDict(const PdfImageData& d, int smaskObj) {
    QByteArray o = "/Type /XObject /Subtype /Image"
                   " /Width "  + QByteArray::number(d.width) +
                   " /Height " + QByteArray::number(d.height) +
                   " /ColorSpace /DeviceRGB /BitsPerComponent 8"
                   " /Filter /FlateDecode";
    if (smaskObj >= 0)
        o += " /SMask " + QByteArray::number(smaskObj) + " 0 R";
    return o;
}

QByteArray PdfImageEmbed::smaskDict(const PdfImageData& d) {
    return "/Type /XObject /Subtype /Image"
           " /Width "  + QByteArray::number(d.width) +
           " /Height " + QByteArray::number(d.height) +
           " /ColorSpace /DeviceGray /BitsPerComponent 8"
           " /Filter /FlateDecode";
}

} // namespace mg
