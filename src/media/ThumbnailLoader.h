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
//  Thumbnail-Kantenlänge in STUFEN (512/1024/2048/4096, s. setTargetDim):
//  innerhalb einer Stufe invalidiert Ctrl+Mausrad-Zoom den Cache NICHT; erst
//  ein Stufenwechsel (deutlich größere/kleinere Kacheln) erzeugt neue
//  Cache-Dateien in passender Auflösung — große Kacheln bleiben dadurch
//  scharf.  QML skaliert die Cache-Datei per `sourceSize` auf die exakte
//  Kachelgröße — das Original wird nie in Vollauflösung dekodiert.
// ─────────────────────────────────────────────────────────────────────────────
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject* parent = nullptr);
    ~ThumbnailLoader();

    // Basis-Generierungsgröße (längste Kante). QML dekodiert daraus per sourceSize.
    static constexpr int kThumbDim = 512;

    // Ziel-Kantenlänge an die Kachelgröße anpassen (quantisierte Stufen
    // 512/1024/2048/4096, Minimum kThumbDim). Die Stufen halten den Disk-Cache
    // über kleine Zoomschritte hinweg gültig; erst ein Stufenwechsel erzeugt
    // neue Cache-Dateien. Liefert true, wenn sich die Stufe geändert hat —
    // der Aufrufer fordert dann sichtbare Thumbnails neu an. Ohne diese
    // Anpassung wurden Kacheln > 512 px aus der 512er-Cache-Datei
    // hochskaliert (sichtbar unscharf, „nicht originalgetreu").
    bool setTargetDim(int needPx);
    int  targetDim() const { return m_targetDim; }

    // Sorgt dafür, dass für filePath eine Cache-Datei existiert. Bei Treffer wird
    // thumbnailReady sofort (queued) emittiert; sonst nach asynchroner Erzeugung.
    void requestThumbnail(const QString& filePath);

    // Bricht eine konkrete (noch ausstehende) Anforderung ab. Noch nicht
    // gestartete Tasks werden aus der Pool-Queue genommen; laufende kooperativ
    // abgebrochen. Bereits gelieferte Ergebnisse bleiben unberührt.
    void cancelThumbnail(const QString& filePath);

    // Verwirft alle in-flight-Ergebnisse (Ordnerwechsel). Cache auf Platte bleibt.
    void cancelAll();

signals:
    // thumbUrl ist eine fertige "file:///..."-URL für Image.source in QML.
    void thumbnailReady(const QString& filePath, const QString& thumbUrl);
    void thumbnailFailed(const QString& filePath);

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    static int quantizeDim(int needPx);

    QThreadPool*                   m_pool;
    int                            m_targetDim = kThumbDim;  // nur GUI-Thread
    QMutex                         m_mutex;
    QSet<QString>                  m_pending;
    //  Pfade, die WAEHREND eines laufenden Abbruchs erneut angefordert wurden.
    //  `done` reiht sie danach neu ein — sonst ginge die Anforderung verloren.
    QSet<QString>  m_rearm;   // verhindert Doppel-Submits
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

    //  Alle Erzeuger arbeiten auf QImage, NICHT auf QPixmap: QPixmap ist an den
    //  GUI-Thread gebunden (Qt-Dokumentation: „QPixmap … outside the GUI thread"
    //  ist nicht unterstuetzt), diese Funktionen laufen aber ausnahmslos in
    //  Pool-Workern. QImage ist dort explizit erlaubt und spart zusaetzlich die
    //  QPixmap::fromImage-Konvertierung samt zweitem Vollbild-Puffer je Thumbnail.
    static QImage generateVideoThumbnail(const QString& path, const QSize& size);
    static QImage generateImageThumbnail(const QString& path, const QSize& size);
    static QImage generateAudioThumbnail(const QString& path, const QSize& size);
    static QImage generatePdfThumbnail(const QString& path, const QSize& size);
    static QImage generateTextThumbnail(const QString& path, const QSize& size);
    //  DOCX-Karte (erste Absätze via Docx::Document::plainTextPreview).
    static QImage generateDocxThumbnail(const QString& path, const QSize& size);
    static QImage generateHtmlCardThumbnail(const QString& path, const QSize& size);
    static QImage fallbackPdfThumbnail(const QSize& size);
    static QImage fallbackTextThumbnail(const QString& path, const QSize& size);
};
