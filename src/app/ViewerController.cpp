#include "app/ViewerController.h"
#include "pdf/PdfMediaHandler.h"
#include "core/PathUtils.h"
#include "core/MemoryUtils.h"   // mg::trimHeap - RSS-Rückgabe nach Annotations-LRU-Eviction
#include "core/TextEncoding.h"
#include "core/TextPdfExporter.h"
#include "datev/DatevCsv.h"

#include <QPdfDocument>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QUrl>
#include <QDesktopServices>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QVariantMap>
#include <QThreadPool>
#include <QRunnable>
#include <QPointer>
#include <utility>

//  Hilfsfunktion: MediaAnnotation-Vektor -> QVariantList (QML-tauglich).
//  Frei (static), damit Worker-Task und synchrone Variante sie teilen.
static QVariantList annotationsToVariant(const QVector<MediaAnnotation>& anns) {
    QVariantList out;
    out.reserve(anns.size());
    for (const MediaAnnotation& a : anns) {
        QVariantMap m;
        m.insert("page",  a.page);
        m.insert("x",     a.rect.x());
        m.insert("y",     a.rect.y());
        m.insert("w",     a.rect.width());
        m.insert("h",     a.rect.height());
        m.insert("type",  static_cast<int>(a.type));
        m.insert("uri",   a.resolvedUri());
        m.insert("label", a.label);
        out.append(m);
    }
    return out;
}

//  Roh-Scan eines PDFs ohne GUI-Thread. Laedt das Dokument LOKAL (lebt nur fuer
//  die Dauer des Scans -> kein RAM-Wachstum), scannt die Annotationen und reicht
//  das Ergebnis per QueuedConnection an den ViewerController zurueck.
namespace {
class PdfScanTask : public QRunnable {
public:
    PdfScanTask(ViewerController* owner, QString path)
        : m_owner(owner), m_path(std::move(path)) { setAutoDelete(true); }

    void run() override {
        QVariantList list;
        QStringList  temps;

        QPdfDocument doc;
        if (doc.load(m_path) == QPdfDocument::Error::None
            && doc.status() == QPdfDocument::Status::Ready) {
            PdfMediaHandler handler(&doc);
            handler.scanDocument(m_path);
            list  = annotationsToVariant(handler.allAnnotations());
            temps = handler.tempFiles();
            // Temp-Medien werden bewusst NICHT hier geloescht: sie werden zum
            // Abspielen gebraucht und erst beim App-Ende vom Owner entfernt.
        }

        // Der Owner wird als QPointer gehalten und im GUI-Thread erneut geprüft: ein roher Zeiger wäre beim Zerstören
        // des Owners während des Scans schon für den `invokeMethod`-Aufruf selbst ungültig.
        QPointer<ViewerController> owner = m_owner;
        if (!owner) return;
        const QString path = m_path;
        QMetaObject::invokeMethod(owner, [owner, path, list, temps]() {
            if (owner)
                owner->applyScanResult(path, list, temps);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<ViewerController> m_owner;
    QString                    m_path;
};
} // namespace

ViewerController::ViewerController(QObject* parent) : QObject(parent) {}

ViewerController::~ViewerController() {
    // Extrahierte Temp-Medien dieser Sitzung entfernen.
    for (const QString& p : std::as_const(m_sessionTempFiles))
        QFile::remove(p);
}

QString ViewerController::readTextFile(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    const QByteArray raw = f.read(kMaxTextBytes);
    f.close();

    //  UTF-8 mit Fehlerpruefung, sonst CP1252 - nicht Latin-1: die beiden gehen
    //  bei 0x80-0x9F auseinander, und genau dort liegen Euro-Zeichen und
    //  typografische Anfuehrungszeichen.
    const QString text = mg::decodeUnknownText(raw);

    // BEWUSST kein Hinweistext im Inhalt: der Vermerk "Datei gekürzt" stand früher IM Puffer und wurde beim
    // Speichern mit in die Datei geschrieben. Der Hinweis gehört in die Oberfläche, nicht in die Daten.
    return text;
}

bool ViewerController::isDatevFile(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return mg::datev::looksLikeDatev(f.read(64));
}

bool ViewerController::isTableFile(const QString& filePathOrUrl) const {
    const QString e = QFileInfo(mg::toLocalPath(filePathOrUrl)).suffix().toLower();
    return e == QLatin1String("csv") || e == QLatin1String("tsv");
}

bool ViewerController::textFileTruncated(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    if (path.isEmpty())
        return false;
    const QFileInfo fi(path);
    return fi.exists() && fi.size() > kMaxTextBytes;
}

bool ViewerController::writeTextFile(const QString& filePathOrUrl, const QString& content) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    if (path.isEmpty())
        return false;

    //  Letzte Instanz gegen Datenverlust: von einer Datei jenseits des Deckels
    //  liegt nur der Anfang im Editor. Zurueckschreiben hiesse den Rest loeschen.
    if (QFileInfo(path).size() > kMaxTextBytes)
        return false;

    // Atomar schreiben (QSaveFile: erst Temp-Datei, dann atomarer Rename) - bei
    // einem Fehler bleibt die Originaldatei unangetastet.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QStringEncoder enc(QStringEncoder::Utf8);
    const QByteArray bytes = enc.encode(content);
    if (f.write(bytes) != bytes.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

bool ViewerController::openExternally(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

//  LRU-Pflege (nur GUI-Thread -> keine Synchronisation noetig).
void ViewerController::touchCache(const QString& path) {
    m_cacheOrder.removeAll(path);
    m_cacheOrder.append(path);                 // juengster Eintrag ans Ende
}

void ViewerController::insertIntoCache(const QString& path, const QVariantList& anns) {
    m_annCache.insert(path, anns);
    touchCache(path);
    bool evicted = false;
    while (m_cacheOrder.size() > kMaxCachedPdfs) {
        const QString victim = m_cacheOrder.takeFirst();
        m_annCache.remove(victim);
        evicted = true;
    }
    // Nur bei TATSÄCHLICHER Eviction: Annotationslisten enthalten eingebettete
    // Medien-Payloads (QByteArray, potenziell MB) - Heap ans OS zurückgeben.
    if (evicted)
        mg::trimHeap();
}

//  Asynchrone Anforderung (aus QML). Blockiert nie den GUI-Thread.
void ViewerController::requestPdfAnnotations(const QString& filePathOrUrl) {
    const QString path = mg::toLocalPath(filePathOrUrl);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        // Defensiv: leeres Ergebnis (queued) -> QML kann Badges einheitlich leeren.
        QMetaObject::invokeMethod(this, [this, path]() {
            emit pdfAnnotationsReady(path, QVariantList{});
        }, Qt::QueuedConnection);
        return;
    }

    // Cache-Treffer -> sofort (queued, damit der Aufrufer immer asynchron reagiert).
    if (m_annCache.contains(path)) {
        touchCache(path);
        const QVariantList cached = m_annCache.value(path);
        QMetaObject::invokeMethod(this, [this, path, cached]() {
            emit pdfAnnotationsReady(path, cached);
        }, Qt::QueuedConnection);
        return;
    }

    // Doppelte Scans desselben Pfads vermeiden (z. B. schnelles Vor/Zurueck).
    if (m_inFlight.contains(path))
        return;
    m_inFlight.insert(path);

    QThreadPool::globalInstance()->start(new PdfScanTask(this, path));
}

// Der Text kommt aus dem EDITOR mit; die Quelldatei wird nur für den Zielnamen gebraucht und nicht angefasst.
// Paginieren und Zeichnen laufen im Worker, sonst hielte eine große Datei den UI-Thread an.
void ViewerController::exportTextToPdf(const QString& filePathOrUrl,
                                       const QString& content,
                                       const QColor& textColor,
                                       int tabWidth) {
    const QString src    = mg::toLocalPath(filePathOrUrl);
    const QString target = TextPdf::targetPathFor(src);
    if (target.isEmpty()) {
        // Defensiv: ohne Quelle gibt es keinen Zielnamen - Fehler queued melden,
        // damit QML immer denselben (asynchronen) Weg sieht.
        QMetaObject::invokeMethod(this, [this]() {
            emit textPdfExportFinished(false, QString(),
                                       QStringLiteral("Keine Datei geöffnet."));
        }, Qt::QueuedConnection);
        return;
    }

    class TextPdfTask : public QRunnable {
    public:
        TextPdfTask(ViewerController* owner, QString text, QString target, QColor ink,
                    int tabWidth)
            : m_owner(owner), m_text(std::move(text)), m_target(std::move(target)),
              m_ink(ink), m_tabWidth(tabWidth)
        { setAutoDelete(true); }

        void run() override {
            QString err;
            const bool ok = TextPdf::exportToPdf(m_text, m_target, m_ink, m_tabWidth, &err);
            // Owner als QPointer: er kann waehrend des Exports (App-Ende)
            // verschwinden - wie bei PdfScanTask.
            QPointer<ViewerController> owner = m_owner;
            if (!owner) return;
            const QString tgt = m_target;
            QMetaObject::invokeMethod(owner, [owner, ok, tgt, err]() {
                if (owner)
                    emit owner->textPdfExportFinished(ok, tgt, err);
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<ViewerController> m_owner;
        QString                    m_text;
        QString                    m_target;
        QColor                     m_ink;
        int                        m_tabWidth = 4;
    };

    QThreadPool::globalInstance()->start(
        new TextPdfTask(this, content, target, textColor, tabWidth));
}

//  Ergebnis-Uebernahme auf dem GUI-Thread (vom Worker via QueuedConnection).
void ViewerController::applyScanResult(const QString& path, const QVariantList& anns,
                                       const QStringList& tempFiles) {
    m_inFlight.remove(path);
    for (const QString& t : tempFiles)
        if (!t.isEmpty())
            m_sessionTempFiles.append(t);
    insertIntoCache(path, anns);
    emit pdfAnnotationsReady(path, anns);
}

//  Synchrone Variante (Kompatibilitaet). Nutzt denselben Resultcache.
