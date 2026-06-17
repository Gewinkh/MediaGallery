#include "ViewerController.h"
#include "PdfMediaHandler.h"

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
#include <utility>

// ─────────────────────────────────────────────────────────────────────────────
//  Hilfsfunktion: MediaAnnotation-Vektor → QVariantList (QML-tauglich).
//  Frei (static), damit Worker-Task und synchrone Variante sie teilen.
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
//  Roh-Scan eines PDFs ohne GUI-Thread. Laedt das Dokument LOKAL (lebt nur fuer
//  die Dauer des Scans → kein RAM-Wachstum), scannt die Annotationen und reicht
//  das Ergebnis per QueuedConnection an den ViewerController zurueck.
// ─────────────────────────────────────────────────────────────────────────────
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
            // Bewusst KEIN handler.cleanup(): extrahierte Temp-Medien werden zum
            // Abspielen gebraucht und erst beim App-Ende vom Owner entfernt.
        }

        // Ergebnis auf den GUI-Thread marshallen. Die QPointer-/Context-Form von
        // invokeMethod verwirft den Aufruf automatisch, falls der Owner zwischen-
        // zeitlich zerstoert wurde (App-Shutdown) → kein Dangling-Zugriff.
        ViewerController* owner = m_owner;
        const QString path = m_path;
        QMetaObject::invokeMethod(owner, [owner, path, list, temps]() {
            owner->applyScanResult(path, list, temps);
        }, Qt::QueuedConnection);
    }

private:
    ViewerController* m_owner;
    QString           m_path;
};
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
ViewerController::ViewerController(QObject* parent) : QObject(parent) {}

ViewerController::~ViewerController() {
    // Extrahierte Temp-Medien dieser Sitzung entfernen.
    for (const QString& p : std::as_const(m_sessionTempFiles))
        QFile::remove(p);
}

QString ViewerController::toLocalPath(const QString& s) {
    if (s.startsWith(QLatin1String("file:")))
        return QUrl(s).toLocalFile();
    return s;
}

QString ViewerController::readTextFile(const QString& filePathOrUrl) const {
    const QString path = toLocalPath(filePathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    constexpr qint64 kMaxBytes = 8 * 1024 * 1024;   // 8 MB Schutzgrenze
    const QByteArray raw = f.read(kMaxBytes);
    f.close();

    // UTF-8 mit Fehlerprüfung; bei ungültigen Sequenzen Latin-1-Fallback.
    QStringDecoder dec(QStringDecoder::Utf8);
    QString text = dec.decode(raw);
    if (dec.hasError())
        text = QString::fromLatin1(raw);

    if (f.size() > kMaxBytes)
        text += QStringLiteral("\n\n… [Datei gekürzt: > 8 MB]");
    return text;
}

bool ViewerController::writeTextFile(const QString& filePathOrUrl, const QString& content) const {
    const QString path = toLocalPath(filePathOrUrl);
    if (path.isEmpty())
        return false;

    // Atomar schreiben (QSaveFile: erst Temp-Datei, dann atomarer Rename) — bei
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
    const QString path = toLocalPath(filePathOrUrl);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ─────────────────────────────────────────────────────────────────────────────
//  LRU-Pflege (nur GUI-Thread → keine Synchronisation noetig).
// ─────────────────────────────────────────────────────────────────────────────
void ViewerController::touchCache(const QString& path) {
    m_cacheOrder.removeAll(path);
    m_cacheOrder.append(path);                 // juengster Eintrag ans Ende
}

void ViewerController::insertIntoCache(const QString& path, const QVariantList& anns) {
    m_annCache.insert(path, anns);
    touchCache(path);
    while (m_cacheOrder.size() > kMaxCachedPdfs) {
        const QString victim = m_cacheOrder.takeFirst();
        m_annCache.remove(victim);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Asynchrone Anforderung (aus QML). Blockiert nie den GUI-Thread.
// ─────────────────────────────────────────────────────────────────────────────
void ViewerController::requestPdfAnnotations(const QString& filePathOrUrl) {
    const QString path = toLocalPath(filePathOrUrl);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        // Defensiv: leeres Ergebnis (queued) → QML kann Badges einheitlich leeren.
        QMetaObject::invokeMethod(this, [this, path]() {
            emit pdfAnnotationsReady(path, QVariantList{});
        }, Qt::QueuedConnection);
        return;
    }

    // Cache-Treffer → sofort (queued, damit der Aufrufer immer asynchron reagiert).
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

// ─────────────────────────────────────────────────────────────────────────────
//  Ergebnis-Uebernahme auf dem GUI-Thread (vom Worker via QueuedConnection).
// ─────────────────────────────────────────────────────────────────────────────
void ViewerController::applyScanResult(const QString& path, const QVariantList& anns,
                                       const QStringList& tempFiles) {
    m_inFlight.remove(path);
    for (const QString& t : tempFiles)
        if (!t.isEmpty())
            m_sessionTempFiles.append(t);
    insertIntoCache(path, anns);
    emit pdfAnnotationsReady(path, anns);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Synchrone Variante (Kompatibilitaet). Nutzt denselben Resultcache.
// ─────────────────────────────────────────────────────────────────────────────
QVariantList ViewerController::pdfAnnotations(const QString& filePathOrUrl) {
    const QString path = toLocalPath(filePathOrUrl);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};

    if (m_annCache.contains(path)) {
        touchCache(path);
        return m_annCache.value(path);
    }

    QPdfDocument doc;
    if (doc.load(path) != QPdfDocument::Error::None
        || doc.status() != QPdfDocument::Status::Ready)
        return {};

    PdfMediaHandler handler(&doc);
    handler.scanDocument(path);
    const QVariantList out = annotationsToVariant(handler.allAnnotations());
    for (const QString& t : handler.tempFiles())
        if (!t.isEmpty())
            m_sessionTempFiles.append(t);
    insertIntoCache(path, out);
    return out;
}
