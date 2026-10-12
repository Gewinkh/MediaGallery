#include "pdf/PdfThumbnailProvider.h"
#include "pdf/PdfDocShare.h"
#include "core/PathUtils.h"
#include "core/MemoryUtils.h"   // mg::trimHeap - RSS-Rückgabe nach LRU-Verdrängung

#include <QQuickImageProvider>
#include <QPdfDocument>
#include <QImage>
#include <QPainter>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <limits>
#include <QMutexLocker>
#include <utility>

namespace {

// Hochzaehlen, sobald sich das Aussehen der Vorschauen aendert - sonst bleiben alte Bilder stehen.
constexpr int kDiskCacheVersion = 1;
constexpr qint64 kMaxJpegBytes = 4LL * 1024 * 1024;

const QString& diskCacheRoot() {
    static const QString root =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/pdfthumbs");
    return root;
}

QString pageFile(const QString& ablage, int page) {
    return ablage + QLatin1Char('/') + QString::number(page) + QStringLiteral(".jpg");
}

bool writeAtomic(const QString& path, const QByteArray& bytes) {
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

// Leer bei allem, was kein JPEG sein kann; ein kaputter Eintrag wird dann neu gerechnet.
QByteArray readJpeg(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() < 4 || f.size() > kMaxJpegBytes) return {};
    const QByteArray b = f.readAll();
    return b.startsWith("\xFF\xD8") ? b : QByteArray();
}

int readPageCount(const QString& ablage) {
    QFile f(ablage + QStringLiteral("/pages"));
    if (!f.open(QIODevice::ReadWrite)) return 0;
    bool ok = false;
    const int n = f.read(16).trimmed().toInt(&ok);
    if (!ok || n <= 0 || n > 1000000) return 0;
    // Die Aenderungszeit dieser Datei ist die LRU-Marke beim Aufraeumen.
    f.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    return n;
}

void pruneDiskCache(const QString& behalten) {
    QDir root(diskCacheRoot());
    QFileInfoList ordner = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (ordner.size() <= PdfThumbnailProvider::kMaxCachedDocs) return;
    auto marke = [](const QFileInfo& fi) {
        const QFileInfo pages(fi.filePath() + QStringLiteral("/pages"));
        return pages.exists() ? pages.lastModified() : fi.lastModified();
    };
    std::sort(ordner.begin(), ordner.end(),
              [&](const QFileInfo& a, const QFileInfo& b) { return marke(a) < marke(b); });
    for (qsizetype i = 0; i < ordner.size() - PdfThumbnailProvider::kMaxCachedDocs; ++i)
        if (ordner[i].filePath() != behalten)
            QDir(ordner[i].filePath()).removeRecursively();
}

} // namespace

QString PdfThumbnailProvider::diskCacheDir(const QString& localPath) {
    const QFileInfo fi(localPath);
    if (localPath.isEmpty() || !fi.isFile()) return {};
    const QByteArray raw = (fi.absoluteFilePath() + QLatin1Char('|') + QString::number(fi.size())
        + QLatin1Char('|') + QString::number(fi.lastModified().toMSecsSinceEpoch())
        + QLatin1Char('|') + QString::number(kThumbWidthPx) + QLatin1Char('|') + QString::number(kJpegQuality)
        + QLatin1Char('|') + QString::number(kDiskCacheVersion)).toUtf8();
    return diskCacheRoot() + QLatin1Char('/')
           + QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex());
}

void PdfThumbStore::setDiskDir(int docId, const QString& ablage, int pageCount) {
    QMutexLocker lk(&m_mutex);
    m_disk.insert(docId, DiskPages{ablage, std::vector<bool>(static_cast<size_t>(qMax(0, pageCount)), false)});
}

void PdfThumbStore::putPage(int docId, int page, const QByteArray& jpeg, bool onDisk) {
    QMutexLocker lk(&m_mutex);
    if (onDisk) {
        const auto d = m_disk.find(docId);
        if (d != m_disk.end() && page >= 0 && static_cast<size_t>(page) < d->pages.size())
            d->pages[static_cast<size_t>(page)] = true;
    }
    QHash<int, QByteArray>& pages = m_pages[docId];
    const auto old = pages.constFind(page);
    if (old != pages.constEnd()) {
        const qint64 delta = jpeg.size() - old.value().size();
        m_docBytes[docId] += delta;
        m_total          += delta;
    } else {
        m_docBytes[docId] += jpeg.size();
        m_total          += jpeg.size();
    }
    pages.insert(page, jpeg);
}

QByteArray PdfThumbStore::getPage(int docId, int page) const {
    QString datei;
    {
        QMutexLocker lk(&m_mutex);
        const auto it = m_pages.constFind(docId);
        if (it != m_pages.constEnd()) {
            const auto j = it.value().constFind(page);
            if (j != it.value().constEnd()) return j.value();
        }
        const auto d = m_disk.constFind(docId);
        if (d == m_disk.constEnd() || page < 0 || static_cast<size_t>(page) >= d->pages.size()
            || !d->pages[static_cast<size_t>(page)])
            return {};
        datei = pageFile(d->ablage, page);
    }
    // Ausserhalb der Sperre: der Render-Task soll nicht auf einen Plattenzugriff des Bild-Fadens warten.
    return readJpeg(datei);
}

bool PdfThumbStore::containsPage(int docId, int page) const {
    QMutexLocker lk(&m_mutex);
    const auto it = m_pages.constFind(docId);
    return it != m_pages.constEnd() && it.value().contains(page);
}

bool PdfThumbStore::keepWindow(int docId, int center, int radius) {
    QMutexLocker lk(&m_mutex);
    const auto d = m_disk.constFind(docId);
    const auto it = m_pages.find(docId);
    if (d == m_disk.constEnd() || it == m_pages.end()) return false;
    qint64 frei = 0;
    for (auto j = it->begin(); j != it->end();) {
        const int page = j.key();
        if (qAbs(page - center) > radius && static_cast<size_t>(page) < d->pages.size()
            && d->pages[static_cast<size_t>(page)]) {
            frei += j.value().size();
            j = it->erase(j);
        } else {
            ++j;
        }
    }
    m_docBytes[docId] -= frei;
    m_total -= frei;
    return frei > 0;
}

void PdfThumbStore::dropDocument(int docId) {
    QMutexLocker lk(&m_mutex);
    const qint64 bytes = m_docBytes.value(docId, 0);
    m_total -= bytes;
    if (m_total < 0) m_total = 0;
    m_docBytes.remove(docId);
    m_pages.remove(docId);
    m_disk.remove(docId);
}

qint64 PdfThumbStore::totalBytes() const {
    QMutexLocker lk(&m_mutex);
    return m_total;
}

void PdfThumbStore::setPreview(const QString& key, const QByteArray& jpeg) {
    QMutexLocker lk(&m_mutex);
    m_previewKey  = key;
    m_previewJpeg = jpeg;          // ersetzt den EINEN Slot (RAM-Deckel: 1 JPEG)
}

QString PdfThumbStore::previewKey() const {
    QMutexLocker lk(&m_mutex);
    return m_previewKey;
}

QByteArray PdfThumbStore::previewJpeg() const {
    QMutexLocker lk(&m_mutex);
    return m_previewJpeg;
}

// Liefert die Vorschauen aus dem Store an QML, im QML-Image-Worker-Thread: dekodiert wird NUR ein winziges JPEG,
// kein PDFium-Render. Fehlt die Seite, kommt ein Platzhalter, und `pageReady` lässt QML erneut anfragen.
namespace {
class PdfThumbImageProvider : public QQuickImageProvider {
public:
    explicit PdfThumbImageProvider(std::shared_ptr<PdfThumbStore> store)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_store(std::move(store)) {}

    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override {
        QString core = id;
        const int q = core.indexOf(QLatin1Char('?'));
        if (q >= 0) core.truncate(q);

        if (core.startsWith(QLatin1String("preview"))) {
            const QByteArray jpeg = m_store->previewJpeg();
            if (!jpeg.isEmpty()) {
                QImage img;
                if (img.loadFromData(jpeg, "JPG") && !img.isNull()) {
                    if (size) *size = img.size();
                    return img;
                }
            }
            QImage placeholder(QSize(2, 2), QImage::Format_ARGB32_Premultiplied);
            placeholder.fill(Qt::transparent);
            if (size) *size = placeholder.size();
            return placeholder;
        }

        const int slash = core.indexOf(QLatin1Char('/'));
        bool okDoc = false, okPage = false;
        const int docId = (slash > 0) ? core.left(slash).toInt(&okDoc) : 0;
        const int page  = (slash >= 0) ? core.mid(slash + 1).toInt(&okPage) : -1;

        if (okDoc && okPage && page >= 0) {
            const QByteArray jpeg = m_store->getPage(docId, page);
            if (!jpeg.isEmpty()) {
                QImage img;
                if (img.loadFromData(jpeg, "JPG") && !img.isNull()) {
                    if (size) *size = img.size();
                    return img;
                }
            }
        }

        // Platzhalter (noch nicht gerendert / ungueltige id): transparent, damit
        // die weisse Seitenkachel der Leiste sauber bleibt.
        QSize ph = requestedSize.isValid() && !requestedSize.isEmpty()
                 ? requestedSize : QSize(2, 2);
        QImage placeholder(ph, QImage::Format_ARGB32_Premultiplied);
        placeholder.fill(Qt::transparent);
        if (size) *size = ph;
        return placeholder;
    }

private:
    std::shared_ptr<PdfThumbStore> m_store;
};
} // namespace

PdfThumbRenderTask::PdfThumbRenderTask(int docId, QString localPath, FocusPage focus,
                                       int targetWidth, int jpegQuality,
                                       std::shared_ptr<PdfThumbStore> store,
                                       CancelFlag cancel, bool readCache)
    : m_docId(docId)
    , m_path(std::move(localPath))
    , m_focus(std::move(focus))
    , m_targetWidth(targetWidth)
    , m_quality(jpegQuality)
    , m_store(std::move(store))
    , m_cancel(std::move(cancel))
    , m_readCache(readCache) {}

void PdfThumbRenderTask::run() {
    if (cancelled()) return;

    // Nur mit der Breite der Leiste: der Schluessel des Ordners kennt keine andere.
    const QString ablage = (m_targetWidth == PdfThumbnailProvider::kThumbWidthPx
                            && m_quality == PdfThumbnailProvider::kJpegQuality)
                               ? PdfThumbnailProvider::diskCacheDir(m_path) : QString();
    PdfDocShare::Lease lease;
    QPdfDocument* doc = nullptr;
    auto oeffne = [&] {
        if (!doc) {
            lease = PdfDocShare::open(m_path);
            doc = lease.doc();
        }
        return doc != nullptr;
    };

    int n = (m_readCache && !ablage.isEmpty()) ? readPageCount(ablage) : 0;
    if (n <= 0) {
        if (!oeffne()) return;
        n = doc->pageCount();
        if (n <= 0) return;
        if (!ablage.isEmpty() && QDir().mkpath(ablage)
            && writeAtomic(ablage + QStringLiteral("/pages"), QByteArray::number(n)))
            pruneDiskCache(ablage);
    }
    if (!ablage.isEmpty()) m_store->setDiskDir(m_docId, ablage, n);

    const int maxH = m_targetWidth * 4;   // sehr lange Seiten deckeln (RAM-Schutz)

    //  Offene Seiten; die naechste wird JE DURCHGANG neu gewaehlt - die am
    //  naechsten an der gerade betrachteten. Der Scan ueber die offenen Seiten
    //  kostet nichts gegen das Rendern einer einzigen (Mikrosekunden gegen
    //  Millisekunden).
    QList<int> offen;
    offen.reserve(n);
    for (int i = 0; i < n; ++i) offen.append(i);
    bool tonGemeldet = false;
    bool verworfen = false;

    while (!offen.isEmpty()) {
        const int mitte = m_focus ? qBound(0, m_focus->load(std::memory_order_relaxed), n - 1) : 0;
        int besterIdx = 0;
        int besterAbstand = std::numeric_limits<int>::max();
        for (int i = 0; i < offen.size(); ++i) {
            const int d = qAbs(offen.at(i) - mitte);
            if (d < besterAbstand) { besterAbstand = d; besterIdx = i; }
        }
        const int page = offen.takeAt(besterIdx);
        // Bei Abbruch (z. B. LRU-Verdraengung dieses Dokuments) die bereits
        // geschriebenen Seiten wieder freigeben -> kein verwaister Store-Eintrag.
        if (cancelled()) { m_store->dropDocument(m_docId); return; }

        QImage flat;
        QByteArray jpeg = (m_readCache && !ablage.isEmpty()) ? readJpeg(pageFile(ablage, page)) : QByteArray();
        bool aufPlatte = !jpeg.isEmpty();
        if (jpeg.isEmpty()) {
            if (!oeffne()) { m_store->dropDocument(m_docId); return; }
            if (page >= doc->pageCount()) continue;
            const QSizeF pts = doc->pagePointSize(page);
            if (pts.isEmpty() || pts.width() <= 0.0) continue;

            const double scale = static_cast<double>(m_targetWidth) / pts.width();
            const int w = m_targetWidth;
            const int h = qBound(1, static_cast<int>(pts.height() * scale), maxH);

            QImage rendered = doc->render(page, QSize(w, h));
            if (rendered.isNull()) continue;
            if (cancelled()) { m_store->dropDocument(m_docId); return; }

            // Auf weissen Hintergrund komponieren -> JPEG hat kein Alpha, keine
            // schwarzen Raender bei transparenten Seitenbereichen.
            flat = QImage(rendered.size(), QImage::Format_RGB32);
            flat.fill(Qt::white);
            {
                QPainter p(&flat);
                p.drawImage(0, 0, rendered);
            }

            {
                QBuffer buf(&jpeg);
                buf.open(QIODevice::WriteOnly);
                flat.save(&buf, "JPG", m_quality);
            }
            if (jpeg.isEmpty()) continue;
            aufPlatte = !ablage.isEmpty() && QDir().mkpath(ablage) && writeAtomic(pageFile(ablage, page), jpeg);
        }
        if (cancelled()) { m_store->dropDocument(m_docId); return; }

        m_store->putPage(m_docId, page, jpeg, aufPlatte);
        if (aufPlatte && m_store->keepWindow(m_docId, mitte, PdfThumbnailProvider::kRamWindow))
            verworfen = true;
        if (!tonGemeldet) {
            if (flat.isNull()) flat = QImage::fromData(jpeg, "JPG");
            //  Auf EIN Pixel herunterrechnen ist der billigste Mittelwert, den
            //  Qt hat; einmal je Dokument faellt er nicht ins Gewicht.
            const QImage winzig = flat.scaled(1, 1, Qt::IgnoreAspectRatio,
                                              Qt::SmoothTransformation);
            if (!winzig.isNull()) {
                emit tintReady(m_docId, QColor(winzig.pixel(0, 0)));
                tonGemeldet = true;
            }
        }
        emit pageReady(m_docId, page);
    }

    // Abschliessender Abbruch-Check: wurde das Dokument waehrend der letzten Seite
    // verdraengt, dessen Seiten ebenfalls freigeben.
    if (cancelled()) { m_store->dropDocument(m_docId); return; }
    // Die verworfenen Seiten sind kleine Bloecke mitten im Heap; glibc gibt sie sonst nicht zurueck.
    if (verworfen) mg::trimHeap();
}

PdfPreviewRenderTask::PdfPreviewRenderTask(QString key, QString localPath,
                                           int page, int maxPx, int jpegQuality,
                                           std::shared_ptr<PdfThumbStore> store,
                                           CancelFlag cancel)
    : m_key(std::move(key))
    , m_path(std::move(localPath))
    , m_page(page)
    , m_maxPx(maxPx)
    , m_quality(jpegQuality)
    , m_store(std::move(store))
    , m_cancel(std::move(cancel)) {}

void PdfPreviewRenderTask::run() {
    if (cancelled()) return;

    const PdfDocShare::Lease lease = PdfDocShare::open(m_path);
    QPdfDocument* const doc = lease.doc();
    if (!doc || m_page < 0 || m_page >= doc->pageCount()) return;

    const QSizeF pts = doc->pagePointSize(m_page);
    if (pts.isEmpty() || pts.width() <= 0.0 || pts.height() <= 0.0) return;

    const double scale = m_maxPx / qMax(pts.width(), pts.height());
    const int w = qBound(1, static_cast<int>(pts.width()  * scale), m_maxPx);
    const int h = qBound(1, static_cast<int>(pts.height() * scale), m_maxPx);
    if (cancelled()) return;

    QImage rendered = doc->render(m_page, QSize(w, h));
    if (rendered.isNull() || cancelled()) return;

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
        flat.save(&buf, "JPG", m_quality);
    }
    if (jpeg.isEmpty() || cancelled()) return;

    m_store->setPreview(m_key, jpeg);
    emit previewReady(m_path, m_page);
}

PdfThumbnailProvider::PdfThumbnailProvider(QObject* parent)
    : QObject(parent)
    , m_store(std::make_shared<PdfThumbStore>())
{
    // EIN Worker: nie zwei der grossen QPdfDocument-Instanzen gleichzeitig offen.
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(30000);
    // Großvorschau-Pool (Strg+Hover): ebenfalls 1 Worker, aber GETRENNT vom
    // Thumb-Pool - Hover-Vorschauen dürfen nicht hinter dem Volldokument-
    // Rendern eines frisch geöffneten PDFs hängen (Details s. Task-Kommentar).
    m_previewPool.setMaxThreadCount(1);
    m_previewPool.setExpiryTimeout(30000);
}

PdfThumbnailProvider::~PdfThumbnailProvider() {
    for (auto& f : m_flags)
        if (f) f->store(true, std::memory_order_relaxed);
    if (m_previewCancel)
        m_previewCancel->store(true, std::memory_order_relaxed);
    m_pool.clear();
    m_previewPool.clear();
    m_pool.waitForDone(3000);
    m_previewPool.waitForDone(3000);
}

void PdfThumbnailProvider::requestLargePreview(const QString& pathOrUrl,
                                               int page, int maxPx) {
    const QString path = mg::toLocalPath(pathOrUrl);
    if (path.isEmpty() || page < 0) return;
    const int px = qBound(64, maxPx, 4096);

    const QString key = path + QLatin1Char('\n') + QString::number(page)
                      + QLatin1Char('\n') + QString::number(px);
    if (m_store->previewKey() == key) {
        emit largePreviewReady(path, page);       // Slot-Treffer: sofort melden
        return;
    }
    if (m_previewCancel)
        m_previewCancel->store(true, std::memory_order_relaxed);
    m_previewCancel = std::make_shared<std::atomic<bool>>(false);

    auto* task = new PdfPreviewRenderTask(key, path, page, px, kJpegQuality,
                                          m_store, m_previewCancel);
    task->setAutoDelete(true);
    connect(task, &PdfPreviewRenderTask::previewReady,
            this, &PdfThumbnailProvider::largePreviewReady, Qt::QueuedConnection);
    m_previewPool.start(task);
}

QQuickImageProvider* PdfThumbnailProvider::createImageProvider() {
    return new PdfThumbImageProvider(m_store);
}

void PdfThumbnailProvider::touchLru(int docId) {
    m_lruOrder.removeAll(docId);
    m_lruOrder.append(docId);
}

void PdfThumbnailProvider::enforceBudget() {
    bool evicted = false;
    while (m_lruOrder.size() > kMaxDocs ||
           (m_store->totalBytes() > kMaxBytes && m_lruOrder.size() > 1)) {
        const int victim = m_lruOrder.takeFirst();
        if (auto f = m_flags.value(victim))
            f->store(true, std::memory_order_relaxed);   // evtl. laufenden Task stoppen
        m_store->dropDocument(victim);
        const QString path = m_idToPath.take(victim);
        if (!path.isEmpty()) m_pathToId.remove(path);
        m_prepared.remove(victim);
        m_flags.remove(victim);
        m_focus.remove(victim);
        m_tint.remove(victim);
        evicted = true;
    }
    // Nur bei TATSÄCHLICHER Verdrängung: dropDocument gibt ganze JPEG-Seiten-
    // sätze (MB-Bereich) frei - glibc behält den Heap sonst im RSS.
    if (evicted)
        mg::trimHeap();
}

int PdfThumbnailProvider::refreshDocument(const QString& pathOrUrl, int startPage) {
    const QString key = mg::toLocalPath(pathOrUrl);
    if (key.isEmpty()) return 0;

    const int old = m_pathToId.value(key, 0);
    if (old != 0) {
        // Laufenden Render-Lauf kooperativ abbrechen (er malte den ALTEN Stand)
        // und die Kacheln aus dem Store werfen.
        if (const CancelFlag f = m_flags.value(old))
            f->store(true, std::memory_order_relaxed);
        m_store->dropDocument(old);
        m_prepared.remove(old);
        m_flags.remove(old);
        m_focus.remove(old);
        m_tint.remove(old);
        m_pathToId.remove(key);
        m_idToPath.remove(old);
        m_lruOrder.removeAll(old);
    }
    return startDocument(key, startPage, false);   // frische docId + neuer Lauf, Plattencache nur schreiben
}

//  Kostet einen atomaren Schreibzugriff - die Ansicht darf das bei jedem
//  Rollschritt melden, auch mitten in einem schnellen Zug.
void PdfThumbnailProvider::setFocusPage(int docId, int page) {
    if (const auto f = m_focus.value(docId))
        f->store(page, std::memory_order_relaxed);
}

QColor PdfThumbnailProvider::documentTint(int docId) const {
    return m_tint.value(docId, QColor());
}

int PdfThumbnailProvider::focusPage(int docId) const {
    const auto f = m_focus.value(docId);
    return f ? f->load(std::memory_order_relaxed) : -1;
}

int PdfThumbnailProvider::ensureDocument(const QString& pathOrUrl, int startPage) {
    return startDocument(pathOrUrl, startPage, true);
}

int PdfThumbnailProvider::startDocument(const QString& pathOrUrl, int startPage, bool readCache) {
    const QString key = mg::toLocalPath(pathOrUrl);
    if (key.isEmpty()) return 0;

    int docId = m_pathToId.value(key, 0);
    if (docId == 0) {
        docId = m_nextId++;
        m_pathToId.insert(key, docId);
        m_idToPath.insert(docId, key);
    }
    touchLru(docId);
    //  Auch beim Wiedersehen gilt die zuletzt genannte Seite.
    if (const auto f = m_focus.value(docId))
        f->store(startPage, std::memory_order_relaxed);

    // Nur beim ERSTEN Mal je Dokument einen Render-Task einreihen. Beim
    // Zurueckblaettern (LRU-Treffer) liegen die Seiten bereits im Store.
    if (!m_prepared.contains(docId)) {
        m_prepared.insert(docId);

        auto flag = std::make_shared<std::atomic<bool>>(false);
        m_flags.insert(docId, flag);

        auto fokus = std::make_shared<std::atomic<int>>(startPage);
        m_focus.insert(docId, fokus);

        auto* task = new PdfThumbRenderTask(docId, key, fokus,
                                            kThumbWidthPx, kJpegQuality,
                                            m_store, flag, readCache);
        task->setAutoDelete(true);

        connect(task, &PdfThumbRenderTask::pageReady,
                this, &PdfThumbnailProvider::pageReady, Qt::QueuedConnection);
        connect(task, &PdfThumbRenderTask::tintReady, this,
                [this](int id, const QColor& farbe) {
                    m_tint.insert(id, farbe);
                    emit documentTintChanged(id);
                }, Qt::QueuedConnection);

        m_pool.start(task);
    }

    enforceBudget();
    return docId;
}
