#include "PdfExtractController.h"
#include "PdfPageCopier.h"
#include "PathUtils.h"

#include <QRunnable>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QCollator>
#include <QPdfDocument>
#include <QImage>
#include <QPainter>
#include <QBuffer>
#include <QVariantMap>
#include <utility>

// ══════════════════════════════════════════════════════════════════════════════
//  PdfExtractTask — schreibt die Ziel-PDF im Pool-Thread.
//
//  Je Quelle zuerst der VERLUSTFREIE Weg (PdfAssembler::addSourcePages plant
//  vollständig, bevor es schreibt → ein Fehlschlag hinterlässt keine Fragmente);
//  nur bei Fehlschlag werden die Seiten DIESER Quelle mit einer EIGENEN
//  QPdfDocument-Instanz (Thread-Affinität wie PdfThumbRenderTask) gerastert und
//  als JPEG-Bildseiten in dieselbe Ausgabe gehängt. Kein QObject — der Rückweg
//  läuft wie bei PdfExportTask über QMetaObject::invokeMethod (queued).
// ══════════════════════════════════════════════════════════════════════════════
namespace {

class PdfExtractTask : public QRunnable {
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;
    struct Job {
        QString      path;
        QVector<int> pages;
    };

    PdfExtractTask(PdfExtractController* owner, QVector<Job> jobs,
                   QString targetPath, int generation, CancelFlag cancel)
        : m_owner(owner)
        , m_jobs(std::move(jobs))
        , m_target(std::move(targetPath))
        , m_gen(generation)
        , m_cancel(std::move(cancel)) {
        setAutoDelete(true);
    }

    void run() override {
        QString err;
        const bool ok = writePdf(&err);

        PdfExtractController* owner = m_owner;
        const QString target = m_target;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, ok, target, err, gen]() {
            owner->extractTaskFinished(ok, target, err, gen);
        }, Qt::QueuedConnection);
    }

private:
    bool cancelled() const {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }

    void reportProgress(int done, int total) {
        PdfExtractController* owner = m_owner;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, done, total, gen]() {
            owner->extractTaskProgress(done, total, gen);
        }, Qt::QueuedConnection);
    }

    // Fallback: alle gewählten Seiten EINER Quelle rastern (JPEG → Bildseiten).
    bool rasterizeJob(PdfAssembler* asmb, const Job& job, int* done,
                      int total, QString* err) {
        QPdfDocument doc;   // Affinität: dieser Pool-Thread
        if (doc.load(job.path) != QPdfDocument::Error::None
            || doc.status() != QPdfDocument::Status::Ready) {
            *err = QStringLiteral("load");
            return false;
        }
        const int n = doc.pageCount();
        for (int page : job.pages) {
            if (cancelled()) { *err = QStringLiteral("cancel"); return false; }
            if (page < 0 || page >= n) { *err = QStringLiteral("pageindex"); return false; }

            const QSizeF pts = doc.pagePointSize(page);
            const double wPt = pts.width()  > 1.0 ? pts.width()  : 612.0;
            const double hPt = pts.height() > 1.0 ? pts.height() : 792.0;
            // Punkt → Pixel bei kRasterDpi, mit RAM-Kantenschutz.
            const double scale = PdfExtractController::kRasterDpi / 72.0;
            int w = qBound(1, int(wPt * scale + 0.5), PdfExtractController::kRasterMaxPx);
            int h = qBound(1, int(hPt * scale + 0.5), PdfExtractController::kRasterMaxPx);

            QImage rendered = doc.render(page, QSize(w, h));
            if (rendered.isNull()) { *err = QStringLiteral("render"); return false; }

            // Weiße Komposit-Basis: JPEG hat kein Alpha (wie PdfThumbRenderTask).
            QImage flat(rendered.size(), QImage::Format_RGB32);
            flat.fill(Qt::white);
            {
                QPainter p(&flat);
                p.drawImage(0, 0, rendered);
            }
            QByteArray jpeg;
            {
                QBuffer buf(&jpeg);
                buf.open(QIODevice::WriteOnly);
                flat.save(&buf, "JPG", PdfExtractController::kRasterJpegQuality);
            }
            if (jpeg.isEmpty()) { *err = QStringLiteral("jpeg"); return false; }
            if (!asmb->addRasterPage(jpeg, flat.width(), flat.height(),
                                     QSizeF(wPt, hPt), err))
                return false;
            reportProgress(++(*done), total);
        }
        return true;
    }

    bool writePdf(QString* err) {
        int total = 0;
        for (const Job& j : std::as_const(m_jobs))
            total += j.pages.size();
        if (total <= 0) { *err = QStringLiteral("empty"); return false; }

        QSaveFile out(m_target);
        if (!out.open(QIODevice::WriteOnly)) {
            *err = out.errorString();
            return false;
        }
        PdfAssembler asmb(&out);
        if (!asmb.begin(err)) { out.cancelWriting(); return false; }

        int done = 0;
        for (const Job& job : std::as_const(m_jobs)) {
            if (cancelled()) { out.cancelWriting(); *err = QStringLiteral("cancel"); return false; }

            // 1) Verlustfrei (plant erst vollständig, schreibt dann) …
            QString rawErr;
            if (asmb.addSourcePages(job.path, job.pages, &rawErr)) {
                done += job.pages.size();
                reportProgress(done, total);
                continue;
            }
            // 2) … sonst Raster-Fallback NUR für diese Quelle (Degradations-
            //    kette wie RhiProber: bestmöglich, aber garantiert Ergebnis).
            if (!rasterizeJob(&asmb, job, &done, total, err)) {
                out.cancelWriting();
                return false;
            }
        }
        if (cancelled()) { out.cancelWriting(); *err = QStringLiteral("cancel"); return false; }
        if (!asmb.finish(err)) { out.cancelWriting(); return false; }
        if (!out.commit()) { *err = QStringLiteral("commit"); return false; }
        return true;
    }

    PdfExtractController* m_owner;
    QVector<Job>          m_jobs;
    QString               m_target;
    int                   m_gen;
    CancelFlag            m_cancel;
};

// ── PdfScanTask — PDF-Liste + Seitenzahlen eines Ordners (globaler Dialog) ──
class PdfScanTask : public QRunnable {
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    PdfScanTask(PdfExtractController* owner, QString folder, int generation,
                CancelFlag cancel)
        : m_owner(owner)
        , m_folder(std::move(folder))
        , m_gen(generation)
        , m_cancel(std::move(cancel)) {
        setAutoDelete(true);
    }

    void run() override {
        QVariantList files;
        QDir dir(m_folder);
        QStringList names = dir.entryList({QStringLiteral("*.pdf")},
                                          QDir::Files | QDir::Readable);
        // Natürliche Sortierung wie die Galerie (10 nach 9, nicht nach 1).
        QCollator coll;
        coll.setNumericMode(true);
        coll.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(names.begin(), names.end(),
                  [&coll](const QString& a, const QString& b) {
                      return coll.compare(a, b) < 0;
                  });

        for (const QString& name : std::as_const(names)) {
            if (m_cancel && m_cancel->load(std::memory_order_relaxed)) return;
            const QString path = dir.absoluteFilePath(name);
            // Leichtgewichtiger Struktur-Parse (kein Rendern, mmap) …
            int count = PdfAssembler::probePageCount(path);
            if (count < 0) {
                // … Fallback nur für Sonderfälle (z. B. verschlüsselt): PDFium
                // kann solche Dateien oft trotzdem öffnen und rendern.
                QPdfDocument doc;
                if (doc.load(path) == QPdfDocument::Error::None
                    && doc.status() == QPdfDocument::Status::Ready)
                    count = doc.pageCount();
            }
            if (count <= 0) continue;                 // unlesbar → auslassen
            QVariantMap m;
            m.insert(QStringLiteral("path"), path);
            m.insert(QStringLiteral("name"), name);
            m.insert(QStringLiteral("pageCount"), count);
            files.append(m);
        }

        PdfExtractController* owner = m_owner;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, files, gen]() {
            owner->scanTaskFinished(files, gen);
        }, Qt::QueuedConnection);
    }

private:
    PdfExtractController* m_owner;
    QString               m_folder;
    int                   m_gen;
    CancelFlag            m_cancel;
};

}   // namespace

// ══════════════════════════════════════════════════════════════════════════════
//  PdfExtractController
// ══════════════════════════════════════════════════════════════════════════════
PdfExtractController::PdfExtractController(QObject* parent) : QObject(parent) {
    // EIN Worker: nie zwei Extraktionen/Scans parallel → RAM-Peak gedeckelt,
    // QSaveFile-Ziele kollidieren nicht (Muster wie PdfThumbnailProvider).
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(30000);
}

PdfExtractController::~PdfExtractController() {
    if (m_cancel) m_cancel->store(true, std::memory_order_relaxed);
    m_pool.clear();
    m_pool.waitForDone(3000);
}

void PdfExtractController::setBusy(bool b) {
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

// ── Namens-Vorschläge ────────────────────────────────────────────────────────
QString PdfExtractController::defaultSingleName(const QString& pathOrUrl,
                                                int pageIndex) const {
    const QFileInfo fi(mg::toLocalPath(pathOrUrl));
    return QStringLiteral("%1 - Page %2").arg(fi.completeBaseName())
                                         .arg(pageIndex + 1);
}

QString PdfExtractController::defaultMultiName(const QString& pathOrUrl) const {
    const QFileInfo fi(mg::toLocalPath(pathOrUrl));
    return QStringLiteral("%1-Selected").arg(fi.completeBaseName());
}

// ── Hilfen ───────────────────────────────────────────────────────────────────
QString PdfExtractController::makeTargetPath(const QString& folder, QString base,
                                             const QString& fallbackBase) {
    // Säuberung wie AppController::createEmptyFile: Pfadtrenner raus, führende
    // Punkte weg (keine versteckten Dateien aus Versehen).
    base = base.trimmed();
    base.remove(QLatin1Char('/'));
    base.remove(QLatin1Char('\\'));
    while (base.startsWith(QLatin1Char('.')))
        base.remove(0, 1);
    if (base.isEmpty())
        base = fallbackBase;

    // Kollisionen laut Anforderung ab „ (1)" aufwärts auflösen.
    QString path = folder + QLatin1Char('/') + base + QStringLiteral(".pdf");
    int n = 1;
    while (QFileInfo::exists(path))
        path = folder + QLatin1Char('/') + base
               + QStringLiteral(" (%1).pdf").arg(n++);
    return path;
}

QVector<int> PdfExtractController::normalizePages(const QVariantList& pages) {
    QVector<int> out;
    out.reserve(pages.size());
    for (const QVariant& v : pages) {
        bool ok = false;
        const int p = v.toInt(&ok);
        if (ok && p >= 0 && !out.contains(p))
            out.append(p);
    }
    // Anforderung: die erzeugte PDF folgt IMMER der Originalreihenfolge —
    // unabhängig von der Klick-Reihenfolge der Auswahl.
    std::sort(out.begin(), out.end());
    return out;
}

// ── Extraktion ───────────────────────────────────────────────────────────────
void PdfExtractController::startExtract(QVector<Job> jobs,
                                        const QString& targetPath) {
    // Laufenden Task kooperativ stoppen, neue Generation beginnen.
    if (m_cancel) m_cancel->store(true, std::memory_order_relaxed);
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const int gen = ++m_generation;

    QVector<PdfExtractTask::Job> taskJobs;
    taskJobs.reserve(jobs.size());
    for (Job& j : jobs)
        taskJobs.append({std::move(j.path), std::move(j.pages)});

    setBusy(true);
    m_pool.start(new PdfExtractTask(this, std::move(taskJobs), targetPath,
                                    gen, m_cancel));
}

void PdfExtractController::extractSingle(const QString& pathOrUrl, int page,
                                         const QString& baseName) {
    extractSelection(pathOrUrl, QVariantList{page}, baseName.isEmpty()
                         ? defaultSingleName(pathOrUrl, page) : baseName);
}

void PdfExtractController::extractSelection(const QString& pathOrUrl,
                                            const QVariantList& pages,
                                            const QString& baseName) {
    const QString src = mg::toLocalPath(pathOrUrl);
    const QVector<int> idx = normalizePages(pages);
    if (src.isEmpty() || idx.isEmpty() || !QFileInfo::exists(src)) {
        emit extractFinished(false, QString(), QStringLiteral("input"));
        return;
    }
    const QString fallback = defaultMultiName(src);
    const QString target = makeTargetPath(QFileInfo(src).absolutePath(),
                                          baseName, fallback);
    startExtract({{src, idx}}, target);
}

void PdfExtractController::extractGlobal(const QVariantList& jobs,
                                         const QString& folderOrUrl,
                                         const QString& baseName) {
    const QString folder = mg::toLocalPath(folderOrUrl);
    QVector<Job> parsed;
    for (const QVariant& jv : jobs) {
        const QVariantMap m = jv.toMap();
        const QString path = mg::toLocalPath(m.value(QStringLiteral("path")).toString());
        const QVector<int> idx = normalizePages(m.value(QStringLiteral("pages")).toList());
        if (path.isEmpty() || idx.isEmpty() || !QFileInfo::exists(path))
            continue;
        parsed.append({path, idx});
    }
    // Name ist bei der globalen Extraktion PFLICHT (QML erzwingt; defensiv).
    if (parsed.isEmpty() || folder.isEmpty()
        || baseName.trimmed().isEmpty() || !QFileInfo(folder).isDir()) {
        emit extractFinished(false, QString(), QStringLiteral("input"));
        return;
    }
    const QString target = makeTargetPath(folder, baseName,
                                          QStringLiteral("Selected"));
    startExtract(std::move(parsed), target);
}

// ── Ordner-Scan ──────────────────────────────────────────────────────────────
void PdfExtractController::scanFolder(const QString& folderOrUrl) {
    const QString folder = mg::toLocalPath(folderOrUrl);
    if (m_cancel) m_cancel->store(true, std::memory_order_relaxed);
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const int gen = ++m_generation;

    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        emit folderPdfsReady({});
        return;
    }
    setBusy(true);
    m_pool.start(new PdfScanTask(this, folder, gen, m_cancel));
}

// ── Task-Rückwege (GUI-Thread, queued) ───────────────────────────────────────
void PdfExtractController::extractTaskFinished(bool ok, const QString& target,
                                               const QString& error,
                                               int generation) {
    if (generation != m_generation) return;   // veraltet (neuer Auftrag läuft)
    setBusy(false);
    emit extractFinished(ok, ok ? target : QString(), ok ? QString() : error);
}

void PdfExtractController::extractTaskProgress(int done, int total,
                                               int generation) {
    if (generation != m_generation) return;
    emit extractProgress(done, total);
}

void PdfExtractController::scanTaskFinished(const QVariantList& files,
                                            int generation) {
    if (generation != m_generation) return;
    setBusy(false);
    emit folderPdfsReady(files);
}
