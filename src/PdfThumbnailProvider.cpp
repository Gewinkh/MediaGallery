#include "PdfThumbnailProvider.h"
#include "PathUtils.h"

#include <QQuickImageProvider>
#include <QPdfDocument>
#include <QImage>
#include <QPainter>
#include <QBuffer>
#include <QThread>
#include <QMutexLocker>
#include <QMetaObject>
#include <utility>

// ══════════════════════════════════════════════════════════════════════════════
//  PdfThumbStore
// ══════════════════════════════════════════════════════════════════════════════
void PdfThumbStore::putPage(int docId, int page, const QByteArray& jpeg) {
    QMutexLocker lk(&m_mutex);
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
    QMutexLocker lk(&m_mutex);
    const auto it = m_pages.constFind(docId);
    if (it == m_pages.constEnd()) return {};
    return it.value().value(page);   // leerer QByteArray, wenn Seite fehlt
}

bool PdfThumbStore::containsPage(int docId, int page) const {
    QMutexLocker lk(&m_mutex);
    const auto it = m_pages.constFind(docId);
    return it != m_pages.constEnd() && it.value().contains(page);
}

void PdfThumbStore::dropDocument(int docId) {
    QMutexLocker lk(&m_mutex);
    const qint64 bytes = m_docBytes.value(docId, 0);
    m_total -= bytes;
    if (m_total < 0) m_total = 0;
    m_docBytes.remove(docId);
    m_pages.remove(docId);
}

qint64 PdfThumbStore::totalBytes() const {
    QMutexLocker lk(&m_mutex);
    return m_total;
}

// ══════════════════════════════════════════════════════════════════════════════
//  PdfThumbImageProvider — liefert die Vorschauen aus dem Store an QML.
//
//  Laeuft (bei Image{asynchronous:true}) im QML-Image-Worker-Thread. Es wird NUR
//  ein winziges JPEG aus dem Store dekodiert — KEIN PDFium-Render. Fehlt die
//  Seite noch, liefert ein transparenter Platzhalter; sobald der Task die Seite
//  nachreicht, fordert QML das Bild ueber pageReady erneut an.
// ══════════════════════════════════════════════════════════════════════════════
namespace {
class PdfThumbImageProvider : public QQuickImageProvider {
public:
    explicit PdfThumbImageProvider(std::shared_ptr<PdfThumbStore> store)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_store(std::move(store)) {}

    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override {
        // id-Format: "<docId>/<page>" (der ?r=…-Cache-Buster wurde von der Engine
        // bereits abgetrennt). Defensive Behandlung etwaiger Restparameter.
        QString core = id;
        const int q = core.indexOf(QLatin1Char('?'));
        if (q >= 0) core.truncate(q);

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

// ══════════════════════════════════════════════════════════════════════════════
//  PdfThumbRenderTask
// ══════════════════════════════════════════════════════════════════════════════
PdfThumbRenderTask::PdfThumbRenderTask(int docId, QString localPath, int startPage,
                                       int targetWidth, int jpegQuality,
                                       std::shared_ptr<PdfThumbStore> store,
                                       CancelFlag cancel)
    : m_docId(docId)
    , m_path(std::move(localPath))
    , m_startPage(startPage)
    , m_targetWidth(targetWidth)
    , m_quality(jpegQuality)
    , m_store(std::move(store))
    , m_cancel(std::move(cancel)) {}

void PdfThumbRenderTask::run() {
    if (cancelled()) return;

    // EIGENE Instanz → eigener PDFium-Render-Mutex (entkoppelt von der Hauptansicht).
    QPdfDocument doc;
    if (doc.load(m_path) != QPdfDocument::Error::None) {
        emit documentFailed(m_docId);
        return;
    }
    const int n = doc.pageCount();
    if (n <= 0) {
        emit documentFailed(m_docId);
        return;
    }

    // Render-Reihenfolge: von der sichtbaren Seite nach aussen wachsend.
    QList<int> order;
    order.reserve(n);
    const int s = qBound(0, m_startPage, n - 1);
    order.append(s);
    for (int d = 1; d < n; ++d) {
        if (s - d >= 0) order.append(s - d);
        if (s + d <  n) order.append(s + d);
    }

    const int maxH = m_targetWidth * 4;   // sehr lange Seiten deckeln (RAM-Schutz)

    for (int page : std::as_const(order)) {
        // Bei Abbruch (z. B. LRU-Verdraengung dieses Dokuments) die bereits
        // geschriebenen Seiten wieder freigeben → kein verwaister Store-Eintrag.
        if (cancelled()) { m_store->dropDocument(m_docId); return; }

        const QSizeF pts = doc.pagePointSize(page);
        if (pts.isEmpty() || pts.width() <= 0.0) continue;

        const double scale = static_cast<double>(m_targetWidth) / pts.width();
        const int w = m_targetWidth;
        const int h = qBound(1, static_cast<int>(pts.height() * scale), maxH);

        QImage rendered = doc.render(page, QSize(w, h));
        if (rendered.isNull()) continue;
        if (cancelled()) { m_store->dropDocument(m_docId); return; }

        // Auf weissen Hintergrund komponieren → JPEG hat kein Alpha, keine
        // schwarzen Raender bei transparenten Seitenbereichen.
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
        if (jpeg.isEmpty()) continue;
        if (cancelled()) { m_store->dropDocument(m_docId); return; }

        m_store->putPage(m_docId, page, jpeg);
        emit pageReady(m_docId, page);
    }

    // Abschliessender Abbruch-Check: wurde das Dokument waehrend der letzten Seite
    // verdraengt, dessen Seiten ebenfalls freigeben statt documentReady zu melden.
    if (cancelled()) { m_store->dropDocument(m_docId); return; }

    emit documentReady(m_docId, n);
    // doc geht hier out of scope → die grosse Instanz wird sofort geschlossen.
}

// ══════════════════════════════════════════════════════════════════════════════
//  PdfThumbnailProvider
// ══════════════════════════════════════════════════════════════════════════════
PdfThumbnailProvider::PdfThumbnailProvider(QObject* parent)
    : QObject(parent)
    , m_store(std::make_shared<PdfThumbStore>())
{
    // EIN Worker: nie zwei der grossen QPdfDocument-Instanzen gleichzeitig offen.
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(30000);
}

PdfThumbnailProvider::~PdfThumbnailProvider() {
    // Laufende/anstehende Tasks kooperativ stoppen, dann auf den Pool warten.
    for (auto& f : m_flags)
        if (f) f->store(true, std::memory_order_relaxed);
    m_pool.clear();
    m_pool.waitForDone(3000);
}

QQuickImageProvider* PdfThumbnailProvider::createImageProvider() {
    return new PdfThumbImageProvider(m_store);
}

void PdfThumbnailProvider::touchLru(int docId) {
    m_lruOrder.removeAll(docId);
    m_lruOrder.append(docId);
}

void PdfThumbnailProvider::enforceBudget() {
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
    }
}

int PdfThumbnailProvider::ensureDocument(const QString& pathOrUrl, int startPage) {
    const QString key = mg::toLocalPath(pathOrUrl);
    if (key.isEmpty()) return 0;

    int docId = m_pathToId.value(key, 0);
    if (docId == 0) {
        docId = m_nextId++;
        m_pathToId.insert(key, docId);
        m_idToPath.insert(docId, key);
    }
    touchLru(docId);

    // Nur beim ERSTEN Mal je Dokument einen Render-Task einreihen. Beim
    // Zurueckblaettern (LRU-Treffer) liegen die Seiten bereits im Store.
    if (!m_prepared.contains(docId)) {
        m_prepared.insert(docId);

        auto flag = std::make_shared<std::atomic<bool>>(false);
        m_flags.insert(docId, flag);

        auto* task = new PdfThumbRenderTask(docId, key, startPage,
                                            kThumbWidthPx, kJpegQuality,
                                            m_store, flag);
        task->setAutoDelete(true);

        // Queued auf den GUI-Thread (Task lebt im Pool-Thread). Re-Emit an QML.
        connect(task, &PdfThumbRenderTask::pageReady,
                this, &PdfThumbnailProvider::pageReady, Qt::QueuedConnection);
        connect(task, &PdfThumbRenderTask::documentReady,
                this, &PdfThumbnailProvider::documentReady, Qt::QueuedConnection);
        connect(task, &PdfThumbRenderTask::documentFailed,
                this, &PdfThumbnailProvider::documentFailed, Qt::QueuedConnection);

        m_pool.start(task);
    }

    enforceBudget();
    return docId;
}
