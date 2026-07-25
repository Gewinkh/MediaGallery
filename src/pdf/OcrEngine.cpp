#include "pdf/OcrEngine.h"

#ifdef MG_HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <tesseract/publictypes.h>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QSet>
#include <mutex>

namespace {
//  Gewählte tessdata-Umgebung: Verzeichnis (für Init-datapath) + Sprache.
//  RAUSCHFREI per DATEIPROBE ermittelt (KEINE Init-Fehlversuche, die Tesseract
//  sonst als „Failed loading language" auf stderr ausgibt). Einmalig.
struct TessPick { QByteArray dir; QByteArray lang; };

const TessPick& pick() {
    static TessPick cached;
    static std::once_flag once;
    std::call_once(once, [] {
        QStringList dirs;
        const QByteArray pref = qgetenv("TESSDATA_PREFIX");
        if (!pref.isEmpty()) {
            dirs << QString::fromLocal8Bit(pref);
            dirs << QString::fromLocal8Bit(pref) + QStringLiteral("/tessdata");
        }
        dirs << QStringLiteral("/usr/share/tessdata")
             << QStringLiteral("/usr/share/tesseract-ocr/5/tessdata")
             << QStringLiteral("/usr/share/tesseract-ocr/4.00/tessdata")
             << QStringLiteral("/usr/local/share/tessdata");

        QSet<QString> avail;
        QString foundDir;
        for (const QString& d : std::as_const(dirs)) {
            QDir dir(d);
            if (!dir.exists()) continue;
            const QStringList files = dir.entryList({ QStringLiteral("*.traineddata") }, QDir::Files);
            for (const QString& f : files)
                avail.insert(f.left(f.size() - 12));   // ".traineddata" = 12 Zeichen
            if (!files.isEmpty() && foundDir.isEmpty())
                foundDir = d;
        }
        if (avail.isEmpty())
            return;
        //  Präferenz: sinnvolle Latein-Modelle vor „osd" (nur Orientierung).
        const char* prefs[] = { "eng", "deu", "fra", "spa", "ita", "por", "nld", nullptr };
        for (int i = 0; prefs[i]; ++i) {
            if (avail.contains(QString::fromLatin1(prefs[i]))) {
                cached.lang = prefs[i];
                cached.dir  = foundDir.toLocal8Bit();
                return;
            }
        }
        //  Sonst irgendeine echte Sprache (osd ist nur Skript-/Orientierung).
        avail.remove(QStringLiteral("osd"));
        if (!avail.isEmpty()) {
            cached.lang = avail.values().first().toLatin1();
            cached.dir  = foundDir.toLocal8Bit();
        }
    });
    return cached;
}

QByteArray pickLanguage() { return pick().lang; }
}  // namespace

namespace mg::ocr {

bool available() { return !pickLanguage().isEmpty(); }

QString language() { return QString::fromLatin1(pickLanguage()); }

QList<OcrLine> recognize(const QImage& img, double dpi) {
    QList<OcrLine> out;
    const QByteArray lang = pickLanguage();
    if (lang.isEmpty() || img.isNull() || dpi <= 0.0)
        return out;

    tesseract::TessBaseAPI api;
    const QByteArray dir = pick().dir;
    if (api.Init(dir.isEmpty() ? nullptr : dir.constData(), lang.constData()) != 0)
        return out;

    //  Graustufen-8 = ein Byte je Pixel → direkt an Tesseract übergebbar.
    const QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull()) { api.End(); return out; }
    api.SetImage(gray.constBits(), gray.width(), gray.height(), 1,
                 static_cast<int>(gray.bytesPerLine()));
    api.SetSourceResolution(qMax(70, static_cast<int>(dpi + 0.5)));
    if (api.Recognize(nullptr) != 0) { api.End(); return out; }

    const double toPts = 72.0 / dpi;
    tesseract::ResultIterator* it = api.GetIterator();
    const tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;
    if (it) {
        do {
            int x1, y1, x2, y2;
            if (!it->BoundingBox(level, &x1, &y1, &x2, &y2))
                continue;
            char* c = it->GetUTF8Text(level);
            const QString t = c ? QString::fromUtf8(c).trimmed() : QString();
            delete[] c;
            if (t.isEmpty())
                continue;
            out.append({ QRectF(x1 * toPts, y1 * toPts,
                                (x2 - x1) * toPts, (y2 - y1) * toPts), t });
        } while (it->Next(level));
    }
    api.End();
    return out;
}

}  // namespace mg::ocr

#else  // ── ohne Tesseract: neutrale No-op-Implementierung ───────────────────

namespace mg::ocr {
bool    available() { return false; }
QString language()  { return {}; }
QList<OcrLine> recognize(const QImage&, double) { return {}; }
}  // namespace mg::ocr

#endif
