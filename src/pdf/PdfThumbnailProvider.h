#pragma once
// Seitenvorschau-Leiste mit EIGENER QPdfDocument-Instanz: PDFium serialisiert alle render()-Aufrufe
// einer Instanz über einen Mutex, geteilt mit der Hauptansicht ruckelte das Scrollen sichtbar.
// Gerendert wird einmalig beim Öffnen, gehalten als JPEG im RAM-Store (LRU), Dokument danach zu.

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

class QQuickImageProvider;

// Thread-sicherer RAM-Cache der JPEG-Vorschauen; genutzt vom Render-Task (schreibend) und vom ImageProvider
// (lesend), deshalb mutex-geschützt und über `shared_ptr` geteilt. Schlüssel ist eine numerische docId.
class PdfThumbStore {
public:
    void putPage(int docId, int page, const QByteArray& jpeg);

    QByteArray getPage(int docId, int page) const;

    bool containsPage(int docId, int page) const;

    void dropDocument(int docId);

    qint64 totalBytes() const;

    // Großvorschau: bewusst EIN Slot, der RAM-Deckel ist damit genau ein JPEG. Der Schlüssel (Pfad, Seite, Größe)
    // verhindert, dass eine veraltete Vorschau als aktuelle ausgeliefert wird.
    void       setPreview(const QString& key, const QByteArray& jpeg);
    QString    previewKey()  const;
    QByteArray previewJpeg() const;

private:
    mutable QMutex                       m_mutex;
    QHash<int, QHash<int, QByteArray>>   m_pages;     // docId -> (page -> jpeg)
    QHash<int, qint64>                   m_docBytes;  // docId -> Bytes-Summe
    qint64                               m_total = 0;
    QString                              m_previewKey;   // Pfad+Seite+Größe
    QByteArray                           m_previewJpeg;  // EIN Slot (s. o.)
};

// Rendert ALLE Seiten EINES PDFs in den Store: eigene QPdfDocument-Instanz (Mutex-Entkopplung), von `startPage`
// nach außen, jede Seite als JPEG. Die Instanz wird am Ende von `run()` geschlossen - der RAM-Peak bleibt transient.
class PdfThumbRenderTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    PdfThumbRenderTask(int docId, QString localPath, int startPage,
                       int targetWidth, int jpegQuality,
                       std::shared_ptr<PdfThumbStore> store, CancelFlag cancel);

    void run() override;

signals:
    void pageReady(int docId, int page);

private:
    bool cancelled() const {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }

    int     m_docId;
    QString m_path;
    int     m_startPage;
    int     m_targetWidth;
    int     m_quality;
    std::shared_ptr<PdfThumbStore> m_store;
    CancelFlag                     m_cancel;
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

    // Render-Zielbreite der Vorschauen in Pixeln (Hoehe proportional). 320 px ist
    // auf dem 152-px-Panel auch bei HiDPI scharf und bleibt RAM-guenstig.
    static constexpr int kThumbWidthPx = 320;
    static constexpr int kJpegQuality  = 85;

    static constexpr int    kMaxDocs   = 6;
    static constexpr qint64 kMaxBytes  = 48LL * 1024 * 1024;

    // Stellt sicher, dass fuer `pathOrUrl` Vorschauen erzeugt werden (oder schon
    // vorliegen). startPage priorisiert die zuerst sichtbare Seite. Liefert die
    // docId fuer den URL-Aufbau: "image://pdfthumb/<docId>/<page>".
    Q_INVOKABLE int ensureDocument(const QString& pathOrUrl, int startPage = 0);

    // Vorschauen verwerfen und neu erzeugen, wenn sich der Inhalt hinter demselben Pfad geändert hat. Liefert eine
    // NEUE docId, damit die QML-Quellen neu anfragen statt die alten Kacheln weiterzuzeigen.
    Q_INVOKABLE int refreshDocument(const QString& pathOrUrl, int startPage = 0);

    // Großvorschau EINER Seite anfordern; `maxPx` begrenzt die längere Kante. Meldet `largePreviewReady`, sobald
    // sie abrufbar ist - liegt der identische Schlüssel schon im Slot, feuert das Signal sofort.
    Q_INVOKABLE void requestLargePreview(const QString& pathOrUrl, int page,
                                         int maxPx);

    // Erzeugt den zum Store gehoerenden ImageProvider. NUR EINMAL aufrufen
    // (in main.cpp, vor engine.load). Eigentum geht an die QML-Engine ueber.
    QQuickImageProvider* createImageProvider();

signals:
    void pageReady(int docId, int page);
    void largePreviewReady(const QString& path, int page);

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    void touchLru(int docId);
    void enforceBudget();

    std::shared_ptr<PdfThumbStore> m_store;
    QThreadPool                    m_pool;
    QThreadPool                    m_previewPool;    // eigener 1-Thread-Pool (s. Task)
    CancelFlag                     m_previewCancel;  // Abbruch des letzten Preview-Tasks

    QHash<QString, int>     m_pathToId;   // lokaler Pfad -> docId
    QHash<int, QString>     m_idToPath;    // docId -> lokaler Pfad
    QSet<int>               m_prepared;    // docIds, fuer die bereits ein Task lief
    QHash<int, CancelFlag>  m_flags;       // docId -> kooperatives Abbruch-Flag
    QList<int>              m_lruOrder;    // LRU-Reihenfolge der docIds (alt -> neu)
    int                     m_nextId = 1;  // 0 bleibt frei (= "ungueltig" in QML)
};
