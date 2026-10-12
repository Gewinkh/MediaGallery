#pragma once
// Seitenvorschau-Leiste auf dem Dokument der Ansicht: QtPdf sperrt PDFium global, eine eigene Instanz rendert nicht
// paralleler, haelt aber die Datei ein zweites Mal im RAM. Einmalig beim Öffnen, als JPEG auf Platte und im RAM-Store (LRU).

#include <QColor>
#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <QSet>
#include <QMutex>
#include <QByteArray>
#include <QRunnable>
#include <QThreadPool>
#include <atomic>
#include <memory>
#include <vector>

class QQuickImageProvider;

// Thread-sicherer RAM-Cache der JPEG-Vorschauen; genutzt vom Render-Task (schreibend) und vom ImageProvider
// (lesend), deshalb mutex-geschützt und über `shared_ptr` geteilt. Schlüssel ist eine numerische docId.
// Seiten, deren Datei geschrieben oder gelesen ist (`onDisk`), hält der RAM nur im Fenster um die betrachtete Seite
// (1313 Seiten hielten sonst 29 MB); was NUR im RAM liegt, bleibt.
class PdfThumbStore {
public:
    void setDiskDir(int docId, const QString& ablage, int pageCount);

    void putPage(int docId, int page, const QByteArray& jpeg, bool onDisk = false);

    // Aus dem RAM, sonst von Platte (nur Seiten DIESES Laufs; nach `refreshDocument` liegt dort noch der alte Stand).
    QByteArray getPage(int docId, int page) const;

    bool containsPage(int docId, int page) const;

    // true, wenn etwas frei wurde.
    bool keepWindow(int docId, int center, int radius);

    void dropDocument(int docId);

    qint64 totalBytes() const;

    // Großvorschau: bewusst EIN Slot, der RAM-Deckel ist damit genau ein JPEG. Der Schlüssel (Pfad, Seite, Größe)
    // verhindert, dass eine veraltete Vorschau als aktuelle ausgeliefert wird.
    void       setPreview(const QString& key, const QByteArray& jpeg);
    QString    previewKey()  const;
    QByteArray previewJpeg() const;

private:
    struct DiskPages {
        QString           ablage;
        std::vector<bool> pages;
    };

    mutable QMutex                       m_mutex;
    QHash<int, QHash<int, QByteArray>>   m_pages;     // docId -> (page -> jpeg)
    QHash<int, qint64>                   m_docBytes;  // docId -> Bytes-Summe
    QHash<int, DiskPages>                m_disk;      // docId -> Seiten auf Platte
    qint64                               m_total = 0;
    QString                              m_previewKey;   // Pfad+Seite+Größe
    QByteArray                           m_previewJpeg;  // EIN Slot (s. o.)
};

// Rendert ALLE Seiten EINES PDFs in den Store, jede Seite als JPEG; liegt eine Seite im Plattencache, wird sie nur
// gelesen, und erst die erste fehlende oeffnet das Dokument. `readCache` aus, wenn sich der Inhalt hinter demselben
// Pfad geaendert hat - die Aenderungszeit kann dabei gleich bleiben.
// Die Reihenfolge steht NICHT von vornherein fest: vor jeder Seite wird die noch offene genommen, die der gerade
// betrachteten am naechsten liegt (`m_focus`). Eine feste Spirale ab der Startseite rechnete beim schnellen
// Blaettern minutenlang an Seiten weiter, die laengst aus dem Bild sind.
class PdfThumbRenderTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;
    using FocusPage  = std::shared_ptr<std::atomic<int>>;

    PdfThumbRenderTask(int docId, QString localPath, FocusPage focus,
                       int targetWidth, int jpegQuality,
                       std::shared_ptr<PdfThumbStore> store, CancelFlag cancel,
                       bool readCache = true);

    void run() override;

signals:
    void pageReady(int docId, int page);
    //  Grundton der ersten gerechneten Seite: liegt unter jeder noch rechnenden grossen Seite, auf dunklen
    //  Dokumenten waere ein weisses Blatt beim Rollen das Auffaelligste im Bild.
    void tintReady(int docId, QColor farbe);

private:
    bool cancelled() const {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }

    int       m_docId;
    QString   m_path;
    FocusPage m_focus;
    int       m_targetWidth;
    int     m_quality;
    std::shared_ptr<PdfThumbStore> m_store;
    CancelFlag                     m_cancel;
    bool                           m_readCache;
};

// Rendert EINE Seite als Großvorschau in einem EIGENEN 1-Thread-Pool: hinter dem Thumbnail-Task eingereiht käme
// sie zu spät, der rendert beim ersten Öffnen unter Umständen sekundenlang ein ganzes Dokument.
class PdfPreviewRenderTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    PdfPreviewRenderTask(QString key, QString localPath, int page, int maxPx,
                         int jpegQuality, std::shared_ptr<PdfThumbStore> store,
                         CancelFlag cancel);

    void run() override;

signals:
    void previewReady(const QString& path, int page);

private:
    bool cancelled() const {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }

    QString m_key;
    QString m_path;
    int     m_page;
    int     m_maxPx;
    int     m_quality;
    std::shared_ptr<PdfThumbStore> m_store;
    CancelFlag                     m_cancel;
};

// QML-Singleton "PdfThumbs" und Steuerzentrale: `ensureDocument` vergibt die docId und stößt nur beim ersten Mal
// den Render-Task an, `createImageProvider()` darf nur EINMAL gerufen werden. `maxThreadCount=1` deckelt den RAM.
class PdfThumbnailProvider : public QObject {
    Q_OBJECT
public:
    explicit PdfThumbnailProvider(QObject* parent = nullptr);
    ~PdfThumbnailProvider() override;

    // 320 px: auf dem 152-px-Panel auch bei HiDPI scharf, Hoehe proportional.
    static constexpr int kThumbWidthPx = 320;
    static constexpr int kJpegQuality  = 85;

    static constexpr int    kMaxDocs   = 6;
    static constexpr qint64 kMaxBytes  = 48LL * 1024 * 1024;
    // Halbe Breite des RAM-Fensters in Seiten: deckt Leiste samt Vorhalt und die Hauptansicht daneben (~1,4 MB).
    static constexpr int    kRamWindow = 32;
    static constexpr int    kMaxCachedDocs = 32;   // Ordner im Plattencache

    // Plattencache-Ordner fuer den JETZIGEN Stand der Datei (Pfad, Groesse, Aenderungszeit); leer, wenn sie fehlt.
    static QString diskCacheDir(const QString& localPath);

    // Stellt sicher, dass fuer `pathOrUrl` Vorschauen erzeugt werden (oder schon
    // vorliegen). startPage priorisiert die zuerst sichtbare Seite. Liefert die
    // docId fuer den URL-Aufbau: "image://pdfthumb/<docId>/<page>".
    Q_INVOKABLE int ensureDocument(const QString& pathOrUrl, int startPage = 0);

    // Ein atomarer Schreibzugriff - darf bei jedem Rollschritt kommen.
    Q_INVOKABLE void setFocusPage(int docId, int page);
    //  Nur zum Nachsehen (Pruefstand): welche Seite zuletzt gemeldet wurde.
    Q_INVOKABLE int  focusPage(int docId) const;

    //  Grundton der ersten gerechneten Seite; ungueltig, solange keine da ist.
    Q_INVOKABLE QColor documentTint(int docId) const;

    // Vorschauen verwerfen und neu erzeugen, wenn sich der Inhalt hinter demselben Pfad geändert hat. Liefert eine
    // NEUE docId, damit die QML-Quellen neu anfragen statt die alten Kacheln weiterzuzeigen.
    Q_INVOKABLE int refreshDocument(const QString& pathOrUrl, int startPage = 0);

    // Großvorschau EINER Seite anfordern; `maxPx` begrenzt die längere Kante. Meldet `largePreviewReady`, sobald
    // sie abrufbar ist - liegt der identische Schlüssel schon im Slot, feuert das Signal sofort.
    Q_INVOKABLE void requestLargePreview(const QString& pathOrUrl, int page,
                                         int maxPx);

    // Nur einmal, vor engine.load; Eigentum geht an die QML-Engine.
    QQuickImageProvider* createImageProvider();

signals:
    void pageReady(int docId, int page);
    void largePreviewReady(const QString& path, int page);
    void documentTintChanged(int docId);

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    void touchLru(int docId);
    void enforceBudget();
    int  startDocument(const QString& pathOrUrl, int startPage, bool readCache);

    std::shared_ptr<PdfThumbStore> m_store;
    QThreadPool                    m_pool;
    QThreadPool                    m_previewPool;    // eigener 1-Thread-Pool (s. Task)
    CancelFlag                     m_previewCancel;  // Abbruch des letzten Preview-Tasks

    QHash<QString, int>     m_pathToId;   // lokaler Pfad -> docId
    QHash<int, QString>     m_idToPath;    // docId -> lokaler Pfad
    QSet<int>               m_prepared;    // docIds, fuer die bereits ein Task lief
    QHash<int, CancelFlag>  m_flags;       // docId -> kooperatives Abbruch-Flag
    QHash<int, std::shared_ptr<std::atomic<int>>> m_focus;   // docId -> betrachtete Seite
    QHash<int, QColor>      m_tint;        // docId -> Grundton der ersten Seite
    QList<int>              m_lruOrder;    // LRU-Reihenfolge der docIds (alt -> neu)
    int                     m_nextId = 1;  // 0 bleibt frei (= "ungueltig" in QML)
};
