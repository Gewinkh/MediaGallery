#pragma once
#include <QObject>
#include <QRunnable>
#include <QThreadPool>
#include <QString>
#include <QSet>
#include <QMutex>
#include <QSize>
#include <atomic>

// ─────────────────────────────────────────────────────────────────────────────
//  ThumbnailLoader — Phase 2: reiner ASYNC-DISK-CACHE.
//
//  Erzeugt Thumbnails on disk (QThreadPool, UI-Thread blockiert nie) und liefert
//  den PFAD/URL der gecachten Datei zurück.  KEIN QPixmap-RAM-Cache mehr (RAM-Prio):
//  die dekodierten Bilder hält ausschließlich die QML-Scene (Image{cache:true}),
//  begrenzt durch GridView-Recycling.  KEIN QQuickImageProvider.
//
//  Feste Thumbnail-Kantenlänge (kThumbDim): entkoppelt Plattengröße von der
//  Kachelgröße, damit Ctrl+Mausrad-Zoom NICHT den Cache invalidiert.  QML skaliert
//  die kleine Cache-Datei per `sourceSize` auf die Kachelgröße herunter — das
//  Original wird nie in Vollauflösung dekodiert.
// ─────────────────────────────────────────────────────────────────────────────
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject* parent = nullptr);
    ~ThumbnailLoader();

    // Feste Generierungsgröße (längste Kante). QML dekodiert daraus per sourceSize.
    static constexpr int kThumbDim = 512;

    // Sorgt dafür, dass für filePath eine Cache-Datei existiert. Bei Treffer wird
    // thumbnailReady sofort (queued) emittiert; sonst nach asynchroner Erzeugung.
    void requestThumbnail(const QString& filePath);

    // Verwirft alle in-flight-Ergebnisse (Ordnerwechsel). Cache auf Platte bleibt.
    void cancelAll();

    // Deterministischer Cache-Pfad (.jpg) für filePath bei kThumbDim.
    static QString diskCachePath(const QString& filePath);

signals:
    // thumbUrl ist eine fertige "file:///..."-URL für Image.source in QML.
    void thumbnailReady(const QString& filePath, const QString& thumbUrl);
    void thumbnailFailed(const QString& filePath);

private:
    QThreadPool*          m_pool;
    QMutex                m_mutex;
    QSet<QString>         m_pending;     // verhindert Doppel-Submits
    std::atomic<uint64_t> m_generation{0};
};

// ─────────────────────────────────────────────────────────────────────────────
//  ThumbnailTask — erzeugt EINE Cache-Datei im Pool-Thread.
// ─────────────────────────────────────────────────────────────────────────────
class ThumbnailTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    ThumbnailTask(const QString& path, const QSize& size, uint64_t generation);
    void run() override;

signals:
    // success==false → Erzeugung fehlgeschlagen (thumbPath ungültig).
    void done(const QString& path, const QString& thumbPath, bool success, uint64_t generation);

private:
    QString  m_path;
    QSize    m_size;
    uint64_t m_generation;

    static QPixmap generateVideoThumbnail(const QString& path, const QSize& size);
    static QPixmap generateImageThumbnail(const QString& path, const QSize& size);
    static QPixmap generateAudioThumbnail(const QString& path, const QSize& size);
    static QPixmap generatePdfThumbnail(const QString& path, const QSize& size);
    static QPixmap generateTextThumbnail(const QString& path, const QSize& size);
    static QPixmap fallbackPdfThumbnail(const QSize& size);
    static QPixmap fallbackTextThumbnail(const QString& path, const QSize& size);
};
