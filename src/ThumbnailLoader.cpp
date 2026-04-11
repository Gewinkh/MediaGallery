#include "ThumbnailLoader.h"
#include "MediaItem.h"
#include <QImageReader>
#include <QPainter>
#include <QLinearGradient>
#include <QFont>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QCryptographicHash>
#include <QMutexLocker>
#include <QThread>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QEventLoop>
#include <QTimer>

// ---- Disk cache helpers ----
static QString cacheDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(base + "/thumbs");
    return base + "/thumbs";
}

static QString cacheKey(const QString& path, const QSize& size) {
    QByteArray raw = (path + QChar('|') + QString::number(size.width())
                      + QChar('x') + QString::number(size.height())).toUtf8();
    return cacheDir() + "/" +
           QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex() + ".jpg";
}

// ---- ThumbnailLoader ----

ThumbnailLoader::ThumbnailLoader(QObject* parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
{
    int threads = qMax(4, QThread::idealThreadCount());
    m_pool->setMaxThreadCount(threads);
    m_pool->setExpiryTimeout(30000);
}

ThumbnailLoader::~ThumbnailLoader() {
    m_pool->waitForDone(2000);
}

void ThumbnailLoader::requestThumbnail(const QString& filePath, const QSize& size, int index) {
    // Check disk cache synchronously (just a stat + pixmap load - very fast)
    QString cached = cacheKey(filePath, size);
    if (QFileInfo::exists(cached)) {
        QPixmap pix(cached);
        if (!pix.isNull()) {
            emit thumbnailReady(index, filePath, pix);
            return;
        }
    }

    QMutexLocker lock(&m_mutex);
    if (m_pending.contains(filePath)) return;
    m_pending.insert(filePath);
    lock.unlock();

    auto* task = new ThumbnailTask(filePath, size, index);
    task->setAutoDelete(true);
    connect(task, &ThumbnailTask::done, this, [this](int idx, const QString& path, const QPixmap& pix) {
        QMutexLocker lk(&m_mutex);
        m_pending.remove(path);
        lk.unlock();
        if (pix.isNull())
            emit thumbnailFailed(idx, path);
        else
            emit thumbnailReady(idx, path, pix);
    }, Qt::QueuedConnection);
    m_pool->start(task);
}

void ThumbnailLoader::cancelAll() {
    m_pool->clear();
    QMutexLocker lock(&m_mutex);
    m_pending.clear();
}

// ---- ThumbnailTask ----

ThumbnailTask::ThumbnailTask(const QString& path, const QSize& size, int index)
    : m_path(path), m_size(size), m_index(index)
{
    setAutoDelete(false);
}

void ThumbnailTask::run() {
    MediaType t = MediaItem::detectType(m_path);
    QPixmap pix;

    if (t == MediaType::Image)
        pix = generateImageThumbnail(m_path, m_size);
    else if (t == MediaType::Video)
        pix = generateVideoThumbnail(m_path, m_size);
    else if (t == MediaType::Audio)
        pix = generateAudioThumbnail(m_path, m_size);

    if (!pix.isNull()) {
        pix.save(cacheKey(m_path, m_size), "JPG", 85);
    }

    emit done(m_index, m_path, pix);  // null pixmap signals failure
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
    QMediaPlayer player;
    QVideoSink sink;
    player.setVideoSink(&sink);
    player.setSource(QUrl::fromLocalFile(path));

    QPixmap result;
    QEventLoop loop;
    bool gotFrame = false;

    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop,
                     [&](const QVideoFrame& frame) {
        if (gotFrame || !frame.isValid()) return;
        gotFrame = true;
        QImage img = frame.toImage();
        if (!img.isNull())
            result = QPixmap::fromImage(
                img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        loop.quit();
    });

    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    player.play();
    loop.exec();
    player.stop();

    return result;
}
