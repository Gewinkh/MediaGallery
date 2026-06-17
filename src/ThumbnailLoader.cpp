#include "ThumbnailLoader.h"
#include "MediaItem.h"

#include <QPdfDocument>
#include <QPainter>
#include <QLinearGradient>
#include <QPixmap>
#include <QImage>
#include <QImageReader>
#include <QFont>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QCryptographicHash>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QThread>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QFile>
#include <QTextStream>
#include <QPainterPath>

// ─── Disk-Cache-Helfer ───────────────────────────────────────────────────────
namespace {

QString cacheDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(base + QStringLiteral("/thumbs"));
    return base + QStringLiteral("/thumbs");
}

QString cacheKeyFor(const QString& path) {
    // mtime einbeziehen → ersetzte/bearbeitete Dateien erhalten frische Thumbnails.
    // v4: feste Generierungsgröße (kThumbDim) statt variabler Kachelgröße.
    static const int kCacheVersion = 4;
    const qint64 mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    const QByteArray raw =
        (path + QChar('|')
         + QString::number(ThumbnailLoader::kThumbDim) + QChar('|')
         + QString::number(mtime) + QChar('|')
         + QString::number(kCacheVersion)).toUtf8();
    return cacheDir() + QStringLiteral("/")
           + QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex())
           + QStringLiteral(".jpg");
}

} // namespace

QString ThumbnailLoader::diskCachePath(const QString& filePath) {
    return cacheKeyFor(filePath);
}

// ─── ThumbnailLoader ─────────────────────────────────────────────────────────
ThumbnailLoader::ThumbnailLoader(QObject* parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
{
    const int threads = qMin(8, qMax(2, QThread::idealThreadCount()));
    m_pool->setMaxThreadCount(threads);
    m_pool->setExpiryTimeout(30000);
}

ThumbnailLoader::~ThumbnailLoader() {
    // Alle laufenden Tasks kooperativ stoppen, dann auf den Pool warten.
    {
        QMutexLocker lk(&m_mutex);
        for (auto& f : m_flags)
            if (f) f->store(true, std::memory_order_relaxed);
    }
    m_pool->clear();
    m_pool->waitForDone(2000);
}

void ThumbnailLoader::requestThumbnail(const QString& filePath) {
    if (filePath.isEmpty()) return;
    {
        QMutexLocker lk(&m_mutex);
        if (m_pending.contains(filePath)) return;   // bereits in Arbeit
    }

    // ── Schneller Pfad: Cache-Datei existiert bereits ────────────────────────
    //  Kein Pool-Dispatch, kein Decode — nur eine Existenzprüfung und ein
    //  queued Signal. Das ist der Normalfall beim Scrollen über bereits
    //  generierte Thumbnails und hält den Pool für echte Misses frei.
    const QString cachePath = cacheKeyFor(filePath);
    if (QFileInfo::exists(cachePath)) {
        const QString url = QUrl::fromLocalFile(cachePath).toString();
        QMetaObject::invokeMethod(this, [this, filePath, url]() {
            emit thumbnailReady(filePath, url);
        }, Qt::QueuedConnection);
        return;
    }

    // ── Miss: Task mit Abbruch-Flag + Priorität einreihen ────────────────────
    const uint64_t gen = m_generation.load(std::memory_order_relaxed);
    auto flag = std::make_shared<std::atomic<bool>>(false);
    auto* task = new ThumbnailTask(filePath, QSize(kThumbDim, kThumbDim), gen, flag);
    task->setAutoDelete(true);

    {
        QMutexLocker lk(&m_mutex);
        m_pending.insert(filePath);
        m_queued.insert(filePath, task);
        m_flags.insert(filePath, flag);
    }

    connect(task, &ThumbnailTask::done, this,
            [this](const QString& path, const QString& thumbPath, bool ok, uint64_t taskGen) {
        bool stale, cancelled;
        {
            QMutexLocker lk(&m_mutex);
            const auto it = m_flags.constFind(path);
            cancelled = (it != m_flags.constEnd()) && it.value()
                        && it.value()->load(std::memory_order_relaxed);
            m_pending.remove(path);
            m_queued.remove(path);
            m_flags.remove(path);
            stale = (taskGen != m_generation.load(std::memory_order_relaxed));
        }
        // Nach cancelAll() (stale) oder gezieltem Abbruch: still verwerfen,
        // damit die Modell-Zeile NICHT als „failed“ markiert wird.
        if (stale || cancelled) return;

        if (ok)
            emit thumbnailReady(path, QUrl::fromLocalFile(thumbPath).toString());
        else
            emit thumbnailFailed(path);
    }, Qt::QueuedConnection);

    // Neuere Anforderung = höhere Priorität → gerade sichtbare Kacheln zuerst.
    m_pool->start(task, ++m_priority);
}

void ThumbnailLoader::cancelThumbnail(const QString& filePath) {
    if (filePath.isEmpty()) return;

    ThumbnailTask* task = nullptr;
    CancelFlag flag;
    {
        QMutexLocker lk(&m_mutex);
        if (const auto fit = m_flags.constFind(filePath); fit != m_flags.constEnd())
            flag = fit.value();
        if (const auto qit = m_queued.constFind(filePath); qit != m_queued.constEnd())
            task = qit.value();
    }

    // Laufende Tasks kooperativ abbrechen.
    if (flag) flag->store(true, std::memory_order_relaxed);

    // Noch nicht gestartete Tasks direkt aus der Queue nehmen → sie laufen nie.
    if (task && m_pool->tryTake(task)) {
        {
            QMutexLocker lk(&m_mutex);
            m_pending.remove(filePath);
            m_queued.remove(filePath);
            m_flags.remove(filePath);
        }
        delete task;   // autoDelete greift nicht, da aus dem Pool entnommen
    }
    // Falls bereits gestartet: Flag sorgt für frühen Abbruch; done() räumt auf.
}

void ThumbnailLoader::cancelAll() {
    // Noch nicht gestartete Tasks entfernen & löschen (autoDelete).
    m_pool->clear();
    // Laufende Ergebnisse über Generationswechsel verwerfen …
    m_generation.fetch_add(1, std::memory_order_relaxed);

    QMutexLocker lock(&m_mutex);
    // … und laufende Tasks zusätzlich kooperativ abbrechen.
    for (auto& f : m_flags)
        if (f) f->store(true, std::memory_order_relaxed);
    m_flags.clear();
    m_queued.clear();   // Zeiger gehören nun dem Pool (gelöscht) bzw. laufen aus
    m_pending.clear();
}

// ─── ThumbnailTask ───────────────────────────────────────────────────────────
ThumbnailTask::ThumbnailTask(const QString& path, const QSize& size, uint64_t generation,
                             std::shared_ptr<std::atomic<bool>> cancel)
    : m_path(path), m_size(size), m_generation(generation), m_cancel(std::move(cancel))
{
    setAutoDelete(true);
}

void ThumbnailTask::run() {
    // Früher Ausstieg, falls bereits vor dem Start abgebrochen.
    if (cancelled()) {
        emit done(m_path, QString(), false, m_generation);
        return;
    }

    const QString cachePath = cacheKeyFor(m_path);

    // Disk-Cache-Treffer: keine Dekodierung nötig (nur Existenzprüfung im Pool).
    if (QFileInfo::exists(cachePath)) {
        emit done(m_path, cachePath, true, m_generation);
        return;
    }

    // Vor dem teuren Decode erneut prüfen (Kachel evtl. schon weggescrollt).
    if (cancelled()) {
        emit done(m_path, QString(), false, m_generation);
        return;
    }

    const MediaType t = MediaItem::detectType(m_path);
    QPixmap pix;
    if      (t == MediaType::Image) pix = generateImageThumbnail(m_path, m_size);
    else if (t == MediaType::Video) pix = generateVideoThumbnail(m_path, m_size);
    else if (t == MediaType::Audio) pix = generateAudioThumbnail(m_path, m_size);
    else if (t == MediaType::Pdf)   pix = generatePdfThumbnail(m_path, m_size);
    else if (t == MediaType::Text)  pix = generateTextThumbnail(m_path, m_size);

    if (pix.isNull()) {
        emit done(m_path, QString(), false, m_generation);
        return;
    }

    // Nach dem Decode, vor dem Speichern: abgebrochene Ergebnisse verwerfen.
    if (cancelled()) {
        emit done(m_path, QString(), false, m_generation);
        return;
    }

    const bool saved = pix.save(cachePath, "JPG", 85);
    emit done(m_path, saved ? cachePath : QString(), saved, m_generation);
}

QPixmap ThumbnailTask::generateAudioThumbnail(const QString& path, const QSize& size) {
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(55, 35, 90));
    grad.setColorAt(1, QColor(25, 15, 45));
    p.fillRect(pix.rect(), grad);

    const int barCount = 12;
    const int barW = qMax(2, size.width() / (barCount * 2));
    const int centerY = size.height() / 2;
    QColor waveColor(180, 140, 255, 160);
    p.setPen(Qt::NoPen);
    p.setBrush(waveColor);
    static const float heights[] = {0.3f,0.6f,0.9f,0.7f,1.0f,0.5f,0.8f,0.4f,0.95f,0.65f,0.75f,0.35f};
    const int totalW = barCount * barW * 2;
    const int startX = (size.width() - totalW) / 2;
    for (int i = 0; i < barCount; ++i) {
        const int h = int(heights[i] * size.height() * 0.35f);
        const int x = startX + i * barW * 2;
        p.drawRoundedRect(x, centerY - h, barW, h * 2, 1, 1);
    }

    const QString fmt = QFileInfo(path).suffix().toUpper();
    QFont fnt("Arial", qMax(10, size.width() / 8), QFont::Bold);
    p.setFont(fnt);
    p.setPen(QColor(220, 190, 255));
    p.drawText(pix.rect().adjusted(0, 0, 0, -size.height()/4), Qt::AlignHCenter | Qt::AlignBottom, fmt);

    p.setPen(QColor(180, 140, 255, 120));
    p.setFont(QFont("Arial", qMax(14, size.width() / 5)));
    p.drawText(pix.rect().adjusted(0, size.height()/10, 0, -size.height()/2), Qt::AlignCenter, QStringLiteral("\u266A"));

    p.end();
    return pix;
}

QPixmap ThumbnailTask::generateImageThumbnail(const QString& path, const QSize& size) {
    QImageReader reader(path);
    reader.setAutoTransform(true);

    const QSize imgSize = reader.size();
    if (imgSize.isValid() && (imgSize.width() > size.width() || imgSize.height() > size.height()))
        reader.setScaledSize(imgSize.scaled(size, Qt::KeepAspectRatio));

    QImage img = reader.read();
    if (img.isNull()) return QPixmap();

    if (img.width() > size.width() || img.height() > size.height())
        img = img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return QPixmap::fromImage(std::move(img));
}

QPixmap ThumbnailTask::generateVideoThumbnail(const QString& path, const QSize& size) {
    // QMediaPlayer benötigt eine Event-Loop und darf NICHT im generischen Pool-
    // Worker laufen → dedizierter QThread, plattformübergreifend korrekt.
    QPixmap result;

    QThread thread;
    QObject context;
    QMediaPlayer* player = nullptr;
    QVideoSink*   sink   = nullptr;
    bool          gotFrame = false;
    QMutex        mutex;
    QWaitCondition cond;

    player = new QMediaPlayer;
    sink   = new QVideoSink;
    player->moveToThread(&thread);
    sink->moveToThread(&thread);

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

    QObject::connect(&thread, &QThread::started, player, [&]() {
        player->setVideoSink(sink);
        player->setSource(QUrl::fromLocalFile(path));
        player->play();
    });

    thread.start();

    {
        QMutexLocker lk(&mutex);
        cond.wait(&mutex, 4000);
    }

    QMetaObject::invokeMethod(player, [&]() {
        player->stop();
        player->deleteLater();
        sink->deleteLater();
        thread.quit();
    }, Qt::QueuedConnection);

    thread.wait(5000);
    return result;
}

QPixmap ThumbnailTask::generatePdfThumbnail(const QString& path, const QSize& size) {
    QPdfDocument doc;
    if (doc.load(path) != QPdfDocument::Error::None)
        return fallbackPdfThumbnail(size);

    const QSizeF pageSize = doc.pagePointSize(0);
    if (pageSize.isEmpty())
        return fallbackPdfThumbnail(size);

    const double scale = qMin(size.width() / pageSize.width(),
                              size.height() / pageSize.height());
    const QSize renderSize(qMax(1, static_cast<int>(pageSize.width()  * scale)),
                           qMax(1, static_cast<int>(pageSize.height() * scale)));

    QImage img = doc.render(0, renderSize);
    if (img.isNull())
        return fallbackPdfThumbnail(size);

    QPixmap page = QPixmap::fromImage(std::move(img));
    {
        QPainter p(&page);
        QFont f = p.font();
        f.setPixelSize(qMax(9, renderSize.height() / 20));
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 50, 50, 200));
        const int bh = qMax(14, renderSize.height() / 18);
        const int bw = bh * 2;
        const QRect badge(3, renderSize.height() - bh - 3, bw, bh);
        p.drawRoundedRect(badge, 3, 3);
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, QStringLiteral("PDF"));
    }
    return page;
}

QPixmap ThumbnailTask::fallbackPdfThumbnail(const QSize& size) {
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
    p.drawText(pix.rect(), Qt::AlignCenter, QStringLiteral("PDF"));
    p.end();
    return pix;
}

static void drawExtensionBadge(QPainter& p, const QSize& size, const QString& ext) {
    if (ext.isEmpty()) return;
    QFont bf("Monospace");
    bf.setStyleHint(QFont::Monospace);
    bf.setBold(true);
    bf.setPixelSize(qMax(9, size.height() / 22));
    p.setFont(bf);

    const QString label = ext.toUpper();
    QFontMetrics fm(bf);
    const int textW = fm.horizontalAdvance(label);
    const int bh = qMax(14, size.height() / 18);
    const int bw = textW + bh;
    const int margin = qMax(3, size.width() / 60);
    const QRect badge(size.width() - bw - margin, margin, bw, bh);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 150, 136, 210));
    p.drawRoundedRect(badge, 3, 3);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, label);
}

QPixmap ThumbnailTask::generateTextThumbnail(const QString& path, const QSize& size) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fallbackTextThumbnail(path, size);

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

    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(26, 34, 42));
    grad.setColorAt(1, QColor(16, 22, 28));
    p.fillRect(pix.rect(), grad);

    QFont mono("Monospace");
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(qMax(8, size.height() / 16));
    p.setFont(mono);
    p.setPen(QColor(180, 205, 200));

    QFontMetrics fm(mono);
    const int lineH  = fm.height();
    const int margin = qMax(6, size.width() / 18);
    int y            = margin + fm.ascent();
    const int avail  = size.width() - 2 * margin;

    for (const QString& raw : std::as_const(lines)) {
        if (y > size.height() - margin) break;
        QString line = raw;
        line.replace('\t', QStringLiteral("    "));
        p.drawText(margin, y, fm.elidedText(line, Qt::ElideRight, avail));
        y += lineH;
    }

    drawExtensionBadge(p, size, QFileInfo(path).suffix());
    p.end();
    return pix;
}

QPixmap ThumbnailTask::fallbackTextThumbnail(const QString& path, const QSize& size) {
    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(26, 34, 42));
    grad.setColorAt(1, QColor(16, 22, 28));
    p.fillRect(pix.rect(), grad);

    const int w = size.width(), h = size.height();
    const int dw = qMax(24, w / 3);
    const int dh = qMax(30, h / 3);
    const int fold = dw / 3;
    const QRect doc((w - dw) / 2, (h - dh) / 2, dw, dh);

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
    const int lx0 = doc.left() + dw / 6;
    const int lx1 = doc.right() - dw / 6;
    for (int i = 1; i <= 3; ++i) {
        const int ly = doc.top() + fold + i * (dh - fold) / 4;
        p.drawLine(lx0, ly, lx1, ly);
    }

    drawExtensionBadge(p, size, QFileInfo(path).suffix());
    p.end();
    return pix;
}
