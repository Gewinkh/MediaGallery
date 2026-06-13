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
#include <QFile>
#include <QTextStream>
#include <QPainterPath>

// ---- Disk cache helpers ----
namespace {

QString cacheDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(base + "/thumbs");
    return base + "/thumbs";
}

QString cacheKey(const QString& path, const QSize& size) {
    // Include last-modified timestamp so edited/replaced files get fresh thumbnails.
    // v2: bumped when PDF thumbnail generation changed from letterbox to fill-crop
    // v3: bumped when PDF thumbnail generation changed back to fit-in (no crop) so
    //     the complete first page is always shown inside the tile without clipping.
    static const int kCacheVersion = 3;
    qint64 mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    QByteArray raw = (path + QChar('|') + QString::number(size.width())
                      + QChar('x') + QString::number(size.height())
                      + QChar('|') + QString::number(mtime)
                      + QChar('|') + QString::number(kCacheVersion)).toUtf8();
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
    else if (t == MediaType::Text)
        pix = generateTextThumbnail(m_path, m_size);

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

    QSizeF pageSize = doc.pagePointSize(0);
    if (pageSize.isEmpty())
        return fallbackPdfThumbnail(size);

    // Fit the entire first page inside `size` with the correct aspect ratio.
    // Using qMin (fit-in, not fill-crop) guarantees the complete page is always
    // visible — no content is clipped.  The thumbnail consumer (setThumbnail)
    // also uses KeepAspectRatio so there is no second scaling step that would
    // expand the image beyond the label bounds.
    double scaleByW = size.width()  / pageSize.width();
    double scaleByH = size.height() / pageSize.height();
    double scale    = qMin(scaleByW, scaleByH);
    QSize  renderSize(qMax(1, static_cast<int>(pageSize.width()  * scale)),
                      qMax(1, static_cast<int>(pageSize.height() * scale)));

    QImage img = doc.render(0, renderSize);
    if (img.isNull())
        return fallbackPdfThumbnail(size);

    // Return the rendered page directly — exact pixel size matches renderSize
    // which is <= size on both axes.  setThumbnail will place it inside the
    // label using KeepAspectRatio, centering it.  No background canvas needed:
    // the label's own background color (tileBgColor) fills any remaining area.
    QPixmap page = QPixmap::fromImage(std::move(img));

    // Subtle PDF badge (bottom-left corner of the rendered page)
    {
        QPainter p(&page);
        QFont f = p.font();
        f.setPixelSize(qMax(9, renderSize.height() / 20));
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 50, 50, 200));
        int bh = qMax(14, renderSize.height() / 18);
        int bw = bh * 2;
        QRect badge(3, renderSize.height() - bh - 3, bw, bh);
        p.drawRoundedRect(badge, 3, 3);
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, "PDF");
    }
    return page;
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

// Draw the small extension badge in the top-right corner (same idea as PDF badge).
static void drawExtensionBadge(QPainter& p, const QSize& size, const QString& ext)
{
    if (ext.isEmpty()) return;
    QFont bf("Monospace");
    bf.setStyleHint(QFont::Monospace);
    bf.setBold(true);
    bf.setPixelSize(qMax(9, size.height() / 22));
    p.setFont(bf);

    const QString label = ext.toUpper();
    QFontMetrics fm(bf);
    int textW = fm.horizontalAdvance(label);
    int bh = qMax(14, size.height() / 18);
    int bw = textW + bh;
    int margin = qMax(3, size.width() / 60);
    QRect badge(size.width() - bw - margin, margin, bw, bh);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 150, 136, 210));
    p.drawRoundedRect(badge, 3, 3);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, label);
}

QPixmap ThumbnailTask::generateTextThumbnail(const QString& path, const QSize& size)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fallbackTextThumbnail(path, size);

    // Read only the first few lines — never slurp huge files into memory.
    QTextStream in(&file);
    QStringList lines;
    const int kMaxLines = 5;
    for (int i = 0; i < kMaxLines && !in.atEnd(); ++i)
        lines << in.readLine();
    file.close();

    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Dark background (theme-neutral, slightly bluish like the app chrome)
    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(26, 34, 42));
    grad.setColorAt(1, QColor(16, 22, 28));
    p.fillRect(pix.rect(), grad);

    // Monospace preview text
    QFont mono("Monospace");
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(qMax(8, size.height() / 16));
    p.setFont(mono);
    p.setPen(QColor(180, 205, 200));

    QFontMetrics fm(mono);
    int lineH  = fm.height();
    int margin = qMax(6, size.width() / 18);
    int y      = margin + fm.ascent();
    const int avail = size.width() - 2 * margin;

    for (const QString& raw : std::as_const(lines)) {
        if (y > size.height() - margin) break;
        QString line = raw;
        line.replace('\t', QStringLiteral("    "));
        QString elided = fm.elidedText(line, Qt::ElideRight, avail);
        p.drawText(margin, y, elided);
        y += lineH;
    }

    // Extension badge (top-right)
    drawExtensionBadge(p, size, QFileInfo(path).suffix());
    p.end();
    return pix;
}

QPixmap ThumbnailTask::fallbackTextThumbnail(const QString& path, const QSize& size)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(26, 34, 42));
    grad.setColorAt(1, QColor(16, 22, 28));
    p.fillRect(pix.rect(), grad);

    // Generic document glyph
    int w = size.width(), h = size.height();
    int dw = qMax(24, w / 3);
    int dh = qMax(30, h / 3);
    int fold = dw / 3;
    QRect doc((w - dw) / 2, (h - dh) / 2, dw, dh);

    QPainterPath path2;
    path2.moveTo(doc.left(), doc.top());
    path2.lineTo(doc.right() - fold, doc.top());
    path2.lineTo(doc.right(), doc.top() + fold);
    path2.lineTo(doc.right(), doc.bottom());
    path2.lineTo(doc.left(), doc.bottom());
    path2.closeSubpath();

    p.setPen(QPen(QColor(150, 175, 170), 2));
    p.setBrush(QColor(40, 52, 60));
    p.drawPath(path2);

    p.setPen(QPen(QColor(120, 145, 140), 2));
    int lx0 = doc.left() + dw / 6;
    int lx1 = doc.right() - dw / 6;
    for (int i = 1; i <= 3; ++i) {
        int ly = doc.top() + fold + i * (dh - fold) / 4;
        p.drawLine(lx0, ly, lx1, ly);
    }

    drawExtensionBadge(p, size, QFileInfo(path).suffix());
    p.end();
    return pix;
}
