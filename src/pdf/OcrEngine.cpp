#include "pdf/OcrEngine.h"

#ifdef MG_HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <tesseract/publictypes.h>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QSet>
#include <memory>
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

// Eine Tesseract-Instanz JE FADEN, einmal aufgesetzt: `Init` lädt das Sprachmodell und kostet gemessen ~118 ms.
// TessBaseAPI ist nicht fadensicher; `thread_local` gibt jedem Faden seine eigene, ohne Sperre.
struct TessSession {
    tesseract::TessBaseAPI api;
    bool ready = false;

    TessSession() {
        const TessPick& p = pick();
        if (p.lang.isEmpty()) return;
        ready = api.Init(p.dir.isEmpty() ? nullptr : p.dir.constData(),
                         p.lang.constData()) == 0;
    }
    ~TessSession() { if (ready) api.End(); }
};

//  Wird beim Beenden des Fadens abgeräumt.
TessSession* session() {
    static thread_local TessSession s;
    return s.ready ? &s : nullptr;
}
}  // namespace

namespace mg::ocr {

bool available() { return !pickLanguage().isEmpty(); }

QString language() { return QString::fromLatin1(pickLanguage()); }

QList<OcrLine> recognize(const QImage& img, double dpi, OcrLevel level) {
    QList<OcrLine> out;
    if (img.isNull() || dpi <= 0.0)
        return out;

    TessSession* s = session();
    if (!s) return out;
    tesseract::TessBaseAPI& api = s->api;

    //  Graustufen-8 = ein Byte je Pixel -> direkt an Tesseract übergebbar.
    const QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull()) return out;
    api.SetImage(gray.constBits(), gray.width(), gray.height(), 1,
                 static_cast<int>(gray.bytesPerLine()));
    api.SetSourceResolution(qMax(70, static_cast<int>(dpi + 0.5)));
    if (api.Recognize(nullptr) != 0) { api.Clear(); return out; }

    const double toPts = 72.0 / dpi;
    //  `GetIterator` gibt einen NEUEN Iterator heraus, den der Aufrufer
    //  freigeben muss - `End()`/`Clear()` tun das nicht.
    const std::unique_ptr<tesseract::ResultIterator> it(api.GetIterator());
    const tesseract::PageIteratorLevel lvl =
        (level == OcrLevel::Word) ? tesseract::RIL_WORD : tesseract::RIL_TEXTLINE;
    if (it) {
        do {
            int x1, y1, x2, y2;
            if (!it->BoundingBox(lvl, &x1, &y1, &x2, &y2))
                continue;
            char* c = it->GetUTF8Text(lvl);
            const QString t = c ? QString::fromUtf8(c).trimmed() : QString();
            delete[] c;
            if (t.isEmpty())
                continue;
            out.append({ QRectF(x1 * toPts, y1 * toPts,
                                (x2 - x1) * toPts, (y2 - y1) * toPts), t });
        } while (it->Next(lvl));
    }
    //  `Clear` gibt Bild und Zwischenergebnisse frei, lässt das geladene
    //  Sprachmodell aber stehen - sonst zeigte die Instanz auf den Puffer des
    //  `gray`-Bildes, das gleich verschwindet.
    api.Clear();
    return out;
}

}  // namespace mg::ocr

#else  // ── ohne Tesseract: neutrale No-op-Implementierung ───────────────────

namespace mg::ocr {
bool    available() { return false; }
QString language()  { return {}; }
QList<OcrLine> recognize(const QImage&, double, OcrLevel) { return {}; }
}  // namespace mg::ocr

#endif
