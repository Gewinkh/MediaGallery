#include "ThumbnailLoader.h"
#include <QPdfDocument>
#include <QPainter>
#include <QLinearGradient>
#include "MediaItem.h"
#include <QImageReader>
#include <QFont>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QThread>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSink>

// ---- Disk cache helpers ----
namespace {

QString cacheDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(base + "/thumbs");
    return base + "/thumbs";
}

QString cacheKey(const QString& path, const QSize& size) {
    // Include last-modified timestamp so edited/replaced files get fresh thumbnails
    qint64 mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    QByteArray raw = (path + QChar('|') + QString::number(size.width())
                      + QChar('x') + QString::number(size.height())
                      + QChar('|') + QString::number(mtime)).toUtf8();
    return cacheDir() + "/" +
           QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex() + ".jpg";
}

} // namespace

// ---- ThumbnailLoader ----

ThumbnailLoader::ThumbnailLoader(QObject* parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
{
    // Use all available cores but cap at 8 to avoid thrashing on large folders
    int threads = qMin(8, qMax(2, QThread::idealThreadCount()));
    m_pool->setMaxThreadCount(threads);
    m_pool->setExpiryTimeout(30000);
}

ThumbnailLoader::~ThumbnailLoader() {
    m_pool->waitForDone(2000);
}

void ThumbnailLoader::requestThumbnail(const QString& filePath, const QSize& size, int index) {
    // ── In-memory cache: instant delivery for recently viewed thumbnails ──────
    {
        QMutexLocker lk(&m_mutex);
        auto it = m_memCache.find(filePath);
        if (it != m_memCache.end()) {
            // Move to back of LRU list (most recently used)
            m_cacheOrder.removeOne(filePath);
            m_cacheOrder.append(filePath);
            emit thumbnailReady(index, filePath, it.value());
            return;
        }
        // Already queued – don't double-submit
        if (m_pending.contains(filePath)) return;
        m_pending.insert(filePath);
    }

    // Snapshot the current generation under the lock so the task carries it
    uint64_t gen = m_generation.load(std::memory_order_relaxed);

    // Disk cache check + actual decode happen on the thread pool (non-blocking UI)
    auto* task = new ThumbnailTask(filePath, size, index, gen);
    task->setAutoDelete(true);
    connect(task, &ThumbnailTask::done, this,
            [this](int idx, const QString& path, const QPixmap& pix, uint64_t taskGen) {
        // Discard result if cancelAll() was called after this task was queued
        if (taskGen != m_generation.load(std::memory_order_relaxed)) return;
        {
            QMutexLocker lk(&m_mutex);
            m_pending.remove(path);
            if (!pix.isNull()) {
                // Real LRU eviction: remove the least-recently-used entry
                if (m_memCache.size() >= 200) {
                    const QString lruKey = m_cacheOrder.takeFirst();
                    m_memCache.remove(lruKey);
                }
                m_cacheOrder.append(path);
                m_memCache.insert(path, pix);
            }
        }
        if (pix.isNull())
            emit thumbnailFailed(idx, path);
        else
            emit thumbnailReady(idx, path, pix);
    }, Qt::QueuedConnection);
    m_pool->start(task);
}

void ThumbnailLoader::cancelAll() {
    m_pool->clear();
    // Increment generation so any in-flight task results are silently discarded
    m_generation.fetch_add(1, std::memory_order_relaxed);
    QMutexLocker lock(&m_mutex);
    m_pending.clear();
    // Keep mem cache — still valid for re-display after filter changes
}

// ---- ThumbnailTask ----

ThumbnailTask::ThumbnailTask(const QString& path, const QSize& size, int index, uint64_t generation)
    : m_path(path), m_size(size), m_index(index), m_generation(generation)
{
    setAutoDelete(true);
}

void ThumbnailTask::run() {
    // ── Disk cache hit: skip decode entirely ─────────────────────────────────
    QString cachePath = cacheKey(m_path, m_size);
    if (QFileInfo::exists(cachePath)) {
        QPixmap cached(cachePath);
        if (!cached.isNull()) {
            emit done(m_index, m_path, cached, m_generation);
            return;
        }
    }

    MediaType t = MediaItem::detectType(m_path);
    QPixmap pix;

    if (t == MediaType::Image)
        pix = generateImageThumbnail(m_path, m_size);
    else if (t == MediaType::Video)
        pix = generateVideoThumbnail(m_path, m_size);
    else if (t == MediaType::Audio)
        pix = generateAudioThumbnail(m_path, m_size);
    else if (t == MediaType::Pdf)
        pix = generatePdfThumbnail(m_path, m_size);

    if (!pix.isNull())
        pix.save(cachePath, "JPG", 85);

    emit done(m_index, m_path, pix, m_generation);
}

QPixmap ThumbnailTask::generateAudioThumbnail(const QString& path, const QSize& size) {
    // Create a stylized audio placeholder showing the format label
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Background gradient
    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(55, 35, 90));
    grad.setColorAt(1, QColor(25, 15, 45));
    p.fillRect(pix.rect(), grad);

    // Waveform decoration (simple bars)
    int barCount = 12;
    int barW = qMax(2, size.width() / (barCount * 2));
    int centerY = size.height() / 2;
    QColor waveColor(180, 140, 255, 160);
    p.setPen(Qt::NoPen);
    p.setBrush(waveColor);
    static const float heights[] = {0.3f,0.6f,0.9f,0.7f,1.0f,0.5f,0.8f,0.4f,0.95f,0.65f,0.75f,0.35f};
    int totalW = barCount * barW * 2;
    int startX = (size.width() - totalW) / 2;
    for (int i = 0; i < barCount; ++i) {
        int h = int(heights[i] * size.height() * 0.35f);
        int x = startX + i * barW * 2;
        p.drawRoundedRect(x, centerY - h, barW, h * 2, 1, 1);
    }

    // Format label (e.g. "MP3")
    QString fmt = QFileInfo(path).suffix().toUpper();
    QFont fnt("Arial", qMax(10, size.width() / 8), QFont::Bold);
    p.setFont(fnt);
    p.setPen(QColor(220, 190, 255));
    p.drawText(pix.rect().adjusted(0, 0, 0, -size.height()/4), Qt::AlignHCenter | Qt::AlignBottom, fmt);

    // Music note icon at top
    p.setPen(QColor(180, 140, 255, 120));
    p.setFont(QFont("Arial", qMax(14, size.width() / 5)));
    p.drawText(pix.rect().adjusted(0, size.height()/10, 0, -size.height()/2), Qt::AlignCenter, "♪");

    p.end();
    return pix;
}

QPixmap ThumbnailTask::generateImageThumbnail(const QString& path, const QSize& size) {
    QImageReader reader(path);
    reader.setAutoTransform(true);

    QSize imgSize = reader.size();
    if (imgSize.isValid() && (imgSize.width() > size.width() || imgSize.height() > size.height())) {
        reader.setScaledSize(imgSize.scaled(size, Qt::KeepAspectRatio));
    }

    QImage img = reader.read();
    if (img.isNull()) return QPixmap();

    if (img.width() > size.width() || img.height() > size.height())
        img = img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return QPixmap::fromImage(std::move(img));
}

QPixmap ThumbnailTask::generateVideoThumbnail(const QString& path, const QSize& size) {
    // QMediaPlayer requires its own event loop and must NOT run in a generic
    // QThreadPool worker (which has no event loop). We spin up a dedicated
    // QThread so the player's internal machinery works correctly on all platforms.
    QPixmap result;

    QThread thread;
    QObject context; // lives on this (pool) thread – used only for the connection
    QMediaPlayer* player = nullptr;
    QVideoSink*   sink   = nullptr;
    bool          gotFrame = false;
    QMutex        mutex;
    QWaitCondition cond;

    // Move the media objects into the worker thread
    player = new QMediaPlayer;
    sink   = new QVideoSink;
    player->moveToThread(&thread);
    sink->moveToThread(&thread);

    // Frame capture: runs in the dedicated thread
    QObject::connect(sink, &QVideoSink::videoFrameChanged,
                     &context, [&](const QVideoFrame& frame) {
        QMutexLocker lk(&mutex);
        if (gotFrame || !frame.isValid()) return;
        gotFrame = true;
        QImage img = frame.toImage();
        if (!img.isNull())
            result = QPixmap::fromImage(
                img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        cond.wakeAll();
    }, Qt::DirectConnection);

    // Start player once the thread is running
    QObject::connect(&thread, &QThread::started, player, [&]() {
        player->setVideoSink(sink);
        player->setSource(QUrl::fromLocalFile(path));
        player->play();
    });

    thread.start();

    // Wait for a frame or timeout (4 s)
    {
        QMutexLocker lk(&mutex);
        cond.wait(&mutex, 4000);
    }

    // Teardown in the thread so Qt's ownership rules are respected
    QMetaObject::invokeMethod(player, [&]() {
        player->stop();
        player->deleteLater();
        sink->deleteLater();
        thread.quit();
    }, Qt::QueuedConnection);

    thread.wait(5000);

    return result;
}

QPixmap ThumbnailTask::generatePdfThumbnail(const QString& path, const QSize& size)
{
    QPdfDocument doc;
    if (doc.load(path) != QPdfDocument::Error::None)
        return fallbackPdfThumbnail(size);

    // Render first page at thumbnail resolution
    QSizeF pageSize = doc.pagePointSize(0);
    if (pageSize.isEmpty())
        return fallbackPdfThumbnail(size);

    // Scale to fit thumbnail keeping aspect ratio
    double scaleX = size.width()  / pageSize.width();
    double scaleY = size.height() / pageSize.height();
    double scale  = qMin(scaleX, scaleY);
    QSize  renderSize(static_cast<int>(pageSize.width()  * scale),
                      static_cast<int>(pageSize.height() * scale));

    QImage img = doc.render(0, renderSize);
    if (img.isNull())
        return fallbackPdfThumbnail(size);

    // Center on a background canvas
    QPixmap canvas(size);
    canvas.fill(QColor(35, 45, 50));
    QPainter p(&canvas);
    QPixmap page = QPixmap::fromImage(std::move(img));
    int ox = (size.width()  - page.width())  / 2;
    int oy = (size.height() - page.height()) / 2;
    p.drawPixmap(ox, oy, page);

    // Subtle PDF badge
    QFont f = p.font();
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(200, 50, 50, 200));
    QRect badge(4, size.height() - 22, 36, 18);
    p.drawRoundedRect(badge, 4, 4);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, "PDF");
    p.end();
    return canvas;
}

QPixmap ThumbnailTask::fallbackPdfThumbnail(const QSize& size)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(90, 20, 20));
    grad.setColorAt(1, QColor(40,  8,  8));
    p.fillRect(pix.rect(), grad);

    QFont f = p.font();
    f.setPixelSize(qMax(18, size.width() / 4));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(255, 180, 160, 200));
    p.drawText(pix.rect(), Qt::AlignCenter, "PDF");
    p.end();
    return pix;
}
