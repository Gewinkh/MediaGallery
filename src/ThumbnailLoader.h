#pragma once
#include <QObject>
#include <QRunnable>
#include <QThreadPool>
#include <QString>
#include <QSet>
#include <QHash>
#include <QMutex>
#include <QSize>
#include <atomic>
#include <memory>

class ThumbnailTask;

// ─────────────────────────────────────────────────────────────────────────────
//  ThumbnailLoader — Phase 2/3: reiner ASYNC-DISK-CACHE mit Priorisierung &
//  Abbruch.
//
//  Erzeugt Thumbnails on disk (QThreadPool, UI-Thread blockiert nie) und liefert
//  den PFAD/URL der gecachten Datei zurück.  KEIN QPixmap-RAM-Cache (RAM-Prio):
//  die dekodierten Bilder hält ausschließlich die QML-Scene (Image{cache:true}),
//  begrenzt durch GridView-Recycling.  KEIN QQuickImageProvider.
//
//  Performance (Scrollen):
//   • SCHNELLER PFAD: existiert die Cache-Datei bereits, wird thumbnailReady
//     sofort (queued) emittiert — OHNE Pool-Dispatch.  Das ist der Normalfall
//     nach dem ersten Laden und hält schnelles Scrollen frei von Pool-Churn.
//   • PRIORISIERUNG: jede Anforderung wird mit steigender Priorität eingereiht
//     (neueste zuerst) → gerade sichtbar gewordene Kacheln laufen vor älteren.
//   • ABBRUCH: cancelThumbnail() entfernt noch nicht gestartete Tasks via
//     QThreadPool::tryTake() aus der Queue und bricht laufende Tasks kooperativ
//     über ein Atomic-Flag ab → kein verschwendeter Decode für weggescrollte
//     Kacheln.
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

    // Bricht eine konkrete (noch ausstehende) Anforderung ab. Noch nicht
    // gestartete Tasks werden aus der Pool-Queue genommen; laufende kooperativ
    // abgebrochen. Bereits gelieferte Ergebnisse bleiben unberührt.
    void cancelThumbnail(const QString& filePath);

    // Verwirft alle in-flight-Ergebnisse (Ordnerwechsel). Cache auf Platte bleibt.
    void cancelAll();

    // Deterministischer Cache-Pfad (.jpg) für filePath bei kThumbDim.
    static QString diskCachePath(const QString& filePath);

signals:
    // thumbUrl ist eine fertige "file:///..."-URL für Image.source in QML.
    void thumbnailReady(const QString& filePath, const QString& thumbUrl);
    void thumbnailFailed(const QString& filePath);

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    QThreadPool*                   m_pool;
    QMutex                         m_mutex;
    QSet<QString>                  m_pending;   // verhindert Doppel-Submits
    QHash<QString, ThumbnailTask*> m_queued;    // path → noch nicht beendeter Task
    QHash<QString, CancelFlag>     m_flags;     // path → kooperatives Abbruch-Flag
    std::atomic<uint64_t>          m_generation{0};
    int                            m_priority = 0;  // monoton steigend (neueste zuerst)
};

// ─────────────────────────────────────────────────────────────────────────────
//  ThumbnailTask — erzeugt EINE Cache-Datei im Pool-Thread.
//
//  Prüft an mehreren Stellen ein kooperatives Abbruch-Flag, damit weggescrollte
//  Kacheln keinen teuren Decode mehr auslösen.
// ─────────────────────────────────────────────────────────────────────────────
class ThumbnailTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    ThumbnailTask(const QString& path, const QSize& size, uint64_t generation,
                  std::shared_ptr<std::atomic<bool>> cancel);
    void run() override;

signals:
    // success==false → Erzeugung fehlgeschlagen ODER abgebrochen (thumbPath leer).
    void done(const QString& path, const QString& thumbPath, bool success, uint64_t generation);

private:
    QString  m_path;
    QSize    m_size;
    uint64_t m_generation;
    std::shared_ptr<std::atomic<bool>> m_cancel;

    bool cancelled() const {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }

    static QPixmap generateVideoThumbnail(const QString& path, const QSize& size);
    static QPixmap generateImageThumbnail(const QString& path, const QSize& size);
    static QPixmap generateAudioThumbnail(const QString& path, const QSize& size);
    static QPixmap generatePdfThumbnail(const QString& path, const QSize& size);
    static QPixmap generateTextThumbnail(const QString& path, const QSize& size);
    static QPixmap fallbackPdfThumbnail(const QSize& size);
    static QPixmap fallbackTextThumbnail(const QString& path, const QSize& size);
};
