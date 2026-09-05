#pragma once
#include "editor/SyntaxPalette.h"

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

// Wie eine TEXT-Kachel aussieht: `zeigeInhalt` an = die ersten Zeilen mit Syntaxfärbung, aus = nur der Dateityp.
// `tag` geht in den Cache-Schlüssel ein - sonst behielten bereits erzeugte Kacheln ihr altes Aussehen.
struct TextPreviewStyle {
    bool                     zeigeInhalt = true;
    mg::editor::SyntaxPalette palette{};
    QString                  tag;          // Fingerabdruck fuer den Cache
};

// Reiner Disk-Cache: die dekodierten Bilder hält allein die QML-Szene; existiert die Cache-Datei, wird sofort
// gemeldet - ohne Pool. Kantenlänge in Stufen, erst ein Stufenwechsel erneuert den Cache.
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject* parent = nullptr);
    ~ThumbnailLoader();

    static constexpr int kThumbDim = 512;

    // Quantisierte Stufen (512/1024/2048/4096) halten den Disk-Cache über kleine Zoomschritte gültig; erst ein
    // Stufenwechsel liefert true. Ohne die Anpassung wurden Kacheln über 512 px aus der 512er-Datei hochskaliert.
    bool setTargetDim(int needPx);
    int  targetDim() const { return m_targetDim; }

    // Aussehen der TEXT-Kacheln setzen; liefert true bei einer Änderung, dann lässt der Aufrufer die sichtbaren
    // Kacheln neu erzeugen. Wird IMMER vom GUI-Faden gerufen, jeder Task bekommt eine Kopie mit.
    bool setTextPreviewStyle(bool zeigeInhalt, const mg::editor::SyntaxPalette& p);
    const TextPreviewStyle& textPreviewStyle() const { return m_textStil; }

    void requestThumbnail(const QString& filePath);

    void cancelThumbnail(const QString& filePath);

    void cancelAll();

signals:
    void thumbnailReady(const QString& filePath, const QString& thumbUrl);
    void thumbnailFailed(const QString& filePath);

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    static int quantizeDim(int needPx);

    QThreadPool*                   m_pool;
    int                            m_targetDim = kThumbDim;  // nur GUI-Thread
    TextPreviewStyle               m_textStil;               // nur GUI-Thread
    QMutex                         m_mutex;
    QSet<QString>                  m_pending;
    //  Pfade, die WAEHREND eines laufenden Abbruchs erneut angefordert wurden.
    //  `done` reiht sie danach neu ein - sonst ginge die Anforderung verloren.
    QSet<QString>  m_rearm;   // verhindert Doppel-Submits
    QHash<QString, ThumbnailTask*> m_queued;    // path -> noch nicht beendeter Task
    QHash<QString, CancelFlag>     m_flags;     // path -> kooperatives Abbruch-Flag
    std::atomic<uint64_t>          m_generation{0};
    int                            m_priority = 0;  // monoton steigend (neueste zuerst)
};

//  ThumbnailTask - erzeugt EINE Cache-Datei im Pool-Thread.
//  Prüft an mehreren Stellen ein kooperatives Abbruch-Flag, damit weggescrollte
//  Kacheln keinen teuren Decode mehr auslösen.
class ThumbnailTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    ThumbnailTask(const QString& path, const QSize& size, uint64_t generation,
                  std::shared_ptr<std::atomic<bool>> cancel,
                  const TextPreviewStyle& stil);
    void run() override;

signals:
    void done(const QString& path, const QString& thumbPath, bool success, uint64_t generation);

private:
    QString  m_path;
    QSize    m_size;
    uint64_t m_generation;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    //  KOPIE, nicht Zeiger: der Task laeuft im Pool, die Palette gehoert dem
    //  GUI-Faden. Sie ist klein (gut zwanzig Farben) und aendert sich selten -
    //  eine Kopie je Kachel ist billiger als jede Absicherung.
    TextPreviewStyle m_stil;

    bool cancelled() const {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }

    // Alle Erzeuger arbeiten auf QImage, NICHT auf QPixmap: QPixmap ist an den GUI-Thread gebunden, diese
    // Funktionen laufen aber ausnahmslos in Pool-Workern. QImage spart zusätzlich eine Vollbild-Konvertierung.
    static QImage generateVideoThumbnail(const QString& path, const QSize& size);
    static QImage generateImageThumbnail(const QString& path, const QSize& size);
    static QImage generateAudioThumbnail(const QString& path, const QSize& size);
    static QImage generatePdfThumbnail(const QString& path, const QSize& size);
    static QImage generateTextThumbnail(const QString& path, const QSize& size,
                                        const TextPreviewStyle& stil);
    static QImage generateTypeCardThumbnail(const QString& path, const QSize& size,
                                            const TextPreviewStyle& stil);
    static QImage generateDocxThumbnail(const QString& path, const QSize& size);
    static QImage generateHtmlCardThumbnail(const QString& path, const QSize& size);
    static QImage fallbackPdfThumbnail(const QSize& size);
    static QImage fallbackTextThumbnail(const QString& path, const QSize& size);
};
