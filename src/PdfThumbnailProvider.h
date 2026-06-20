#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfThumbnailProvider.h
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Liefert die SEITLICHE Seitenvorschau ("Thumbnail-Leiste") einer GEÖFFNETEN PDF
//  butterweich scrollbar — unabhaengig von der Hauptansicht.
//
//  WARUM EINE EIGENE KLASSE (und KEIN PdfPageImage mehr)?
//   • PdfPageImage band die Leiste an `root.doc` — DASSELBE PdfDocument wie die
//     grosse Hauptansicht. PDFium serialisiert ALLE render()-Aufrufe EINER
//     Dokument-Instanz ueber einen Mutex: jedes Thumbnail-Rendering konkurrierte
//     beim Scrollen direkt mit der sichtbaren Hauptseite → spuerbares Ruckeln.
//   • Mit cache:false rendert jede aus dem schmalen Puffer gescrollte Seite beim
//     Wiedererscheinen KOMPLETT neu.
//
//  LOESUNG (RAM-bewusst, fuer 100–300 MB / 10–200 Seiten optimiert)
//   1) Beim Oeffnen rendert EIN Worker-Task (eigener QThreadPool, EIGENE
//      QPdfDocument-Instanz → Mutex von der Hauptansicht ENTKOPPELT) alle Seiten
//      EINMALIG in Thumbnail-Groesse.
//   2) Jede Vorschau wird sofort als JPEG (~5–10 KB) in einen thread-sicheren
//      RAM-Store komprimiert; die teure QPdfDocument-Instanz wird direkt nach dem
//      Durchlauf wieder GESCHLOSSEN → der grosse RAM-Peak ist nur transient.
//   3) Ein QQuickImageProvider liefert die Vorschauen aus dem Store. Scrollen
//      dekodiert nur winzige JPEGs (asynchron, < 1 ms) — kein PDFium-Render, kein
//      Mutex-Stau. Stationaerer RAM: wenige MB je Dokument.
//
//  Render-Reihenfolge: von der aktuell sichtbaren Seite nach aussen
//  (currentPage, +1, −1, +2, −2, …) → das gerade Sichtbare erscheint zuerst.
//
//  LRU: Die zuletzt geoeffneten PDFs bleiben im RAM-Store (Deckel ueber
//  Dokumentanzahl UND Gesamt-Bytes) → Zurueckblaettern rendert nicht neu. Passt
//  zum bestehenden `pdfPoolSize`-Konzept der PdfSurface.
//
//  Registrierung: qmlRegisterSingletonInstance(…, "PdfThumbs", …) +
//  engine.addImageProvider("pdfthumb", provider->createImageProvider()) in main.cpp.
// ══════════════════════════════════════════════════════════════════════════════

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

// ─────────────────────────────────────────────────────────────────────────────
//  PdfThumbStore — thread-sicherer RAM-Cache der JPEG-komprimierten Vorschauen.
//
//  Wird von BEIDEN Seiten genutzt: vom Render-Task (Pool-Thread, schreibend) und
//  vom QQuickImageProvider (QML-Image-Worker-Thread, lesend). Deshalb intern
//  durchgaengig mutex-geschuetzt und ueber shared_ptr geteilt — so ist die
//  Lebensdauer unabhaengig davon, dass die QML-Engine den ImageProvider besitzt.
//
//  Schluessel ist eine numerische docId (kein Pfad) → keine Sonderzeichen-/
//  Slash-Probleme in den image://-URLs.
// ─────────────────────────────────────────────────────────────────────────────
class PdfThumbStore {
public:
    // Legt/aktualisiert die JPEG-Bytes einer Seite ab.
    void putPage(int docId, int page, const QByteArray& jpeg);

    // Kopiert die JPEG-Bytes einer Seite heraus (leer, wenn nicht vorhanden).
    QByteArray getPage(int docId, int page) const;

    bool containsPage(int docId, int page) const;

    // Verwirft ALLE Seiten eines Dokuments (LRU-Verdraengung).
    void dropDocument(int docId);

    // Summe aller gespeicherten JPEG-Bytes (fuer den Budget-Deckel).
    qint64 totalBytes() const;

private:
    mutable QMutex                       m_mutex;
    QHash<int, QHash<int, QByteArray>>   m_pages;     // docId → (page → jpeg)
    QHash<int, qint64>                   m_docBytes;  // docId → Bytes-Summe
    qint64                               m_total = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  PdfThumbRenderTask — rendert ALLE Seiten EINES PDFs in den Store.
//
//  Oeffnet eine EIGENE QPdfDocument-Instanz (Mutex-Entkopplung), rendert von
//  startPage nach aussen, komprimiert jede Seite zu JPEG, schiebt sie in den
//  Store und meldet pageReady. Die Instanz wird am Ende von run() geschlossen
//  (RAM-Peak transient). Kooperativer Abbruch ueber ein Atomic-Flag.
// ─────────────────────────────────────────────────────────────────────────────
class PdfThumbRenderTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    PdfThumbRenderTask(int docId, QString localPath, int startPage,
                       int targetWidth, int jpegQuality,
                       std::shared_ptr<PdfThumbStore> store, CancelFlag cancel);

    void run() override;

signals:
    // Feuert (queued → GUI-Thread) je fertig gerenderter Seite.
    void pageReady(int docId, int page);
    // Feuert genau einmal, sobald das Dokument vollstaendig im Store liegt.
    void documentReady(int docId, int pageCount);
    // Laden/Parsen fehlgeschlagen (z. B. defekte Datei).
    void documentFailed(int docId);

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

// ─────────────────────────────────────────────────────────────────────────────
//  PdfThumbnailProvider — QML-Singleton ("PdfThumbs") + Steuerzentrale.
//
//  • ensureDocument(path, startPage) → vergibt/holt die docId und stoesst (nur
//    beim ersten Mal) den Render-Task an. Idempotent; bei LRU-Treffer wird nichts
//    neu gerendert. Gibt die docId fuer den image://-URL-Aufbau in QML zurueck.
//  • pageReady(docId, page) → QML rebindet die betroffene Image-source.
//  • createImageProvider() → liefert den QQuickImageProvider (gleicher Store) zur
//    Registrierung in main.cpp. Darf nur EINMAL aufgerufen werden.
//
//  Der QThreadPool hat maxThreadCount=1: nie sind zwei der grossen
//  QPdfDocument-Instanzen gleichzeitig offen → der RAM-Peak bleibt gedeckelt.
// ─────────────────────────────────────────────────────────────────────────────
class PdfThumbnailProvider : public QObject {
    Q_OBJECT
public:
    explicit PdfThumbnailProvider(QObject* parent = nullptr);
    ~PdfThumbnailProvider() override;

    // Render-Zielbreite der Vorschauen in Pixeln (Hoehe proportional). 320 px ist
    // auf dem 152-px-Panel auch bei HiDPI scharf und bleibt RAM-guenstig.
    static constexpr int kThumbWidthPx = 320;
    static constexpr int kJpegQuality  = 85;

    // LRU-Deckel: so viele PDFs bleiben im RAM-Store warm …
    static constexpr int    kMaxDocs   = 6;
    // … bzw. so viele JPEG-Bytes insgesamt (greift der striktere von beiden).
    static constexpr qint64 kMaxBytes  = 48LL * 1024 * 1024;

    // Stellt sicher, dass fuer `pathOrUrl` Vorschauen erzeugt werden (oder schon
    // vorliegen). startPage priorisiert die zuerst sichtbare Seite. Liefert die
    // docId fuer den URL-Aufbau: "image://pdfthumb/<docId>/<page>".
    Q_INVOKABLE int ensureDocument(const QString& pathOrUrl, int startPage = 0);

    // Erzeugt den zum Store gehoerenden ImageProvider. NUR EINMAL aufrufen
    // (in main.cpp, vor engine.load). Eigentum geht an die QML-Engine ueber.
    QQuickImageProvider* createImageProvider();

signals:
    // Eine Seite liegt jetzt im Store → QML soll die Image-source neu anfordern.
    void pageReady(int docId, int page);
    // Alle Seiten eines Dokuments liegen vor.
    void documentReady(int docId, int pageCount);
    // Vorschau-Erzeugung fuer ein Dokument fehlgeschlagen.
    void documentFailed(int docId);

private:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    static QString toLocalPath(const QString& s);

    // LRU-Pflege: docId ans Ende (juengster) ruecken.
    void touchLru(int docId);
    // Aelteste Dokumente verdraengen, bis kMaxDocs UND kMaxBytes eingehalten sind.
    void enforceBudget();

    std::shared_ptr<PdfThumbStore> m_store;
    QThreadPool                    m_pool;

    QHash<QString, int>     m_pathToId;   // lokaler Pfad → docId
    QHash<int, QString>     m_idToPath;    // docId → lokaler Pfad
    QSet<int>               m_prepared;    // docIds, fuer die bereits ein Task lief
    QHash<int, CancelFlag>  m_flags;       // docId → kooperatives Abbruch-Flag
    QList<int>              m_lruOrder;    // LRU-Reihenfolge der docIds (alt → neu)
    int                     m_nextId = 1;  // 0 bleibt frei (= "ungueltig" in QML)
};
