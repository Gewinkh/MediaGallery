#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QHash>
#include <QSet>

class QPdfDocument;
class PdfMediaHandler;
struct MediaAnnotation;

// ─────────────────────────────────────────────────────────────────────────────
//  ViewerController — C++→QML-Bridge (Singleton) für die Viewer-Hilfsdienste,
//  die kein reines Property/Model sind:
//
//   • readTextFile(path)  → Datei-Inhalt (UTF-8, größenbegrenzt) für TextSurface.
//   • openExternally(path)→ Öffnet Medium im System-Standardprogramm (Video-Mode
//                           "external").
//   • requestPdfAnnotations(path) → ASYNCHRONER Scan der Medien-/Link-Annotationen.
//                           Der teure Roh-Scan (komplette Datei + viele indexOf)
//                           laeuft in einem Worker-Thread (QThreadPool); das
//                           Ergebnis kommt per pdfAnnotationsReady(path, list)
//                           zurueck auf den GUI-Thread. Dadurch blockiert das
//                           Umschalten zwischen PDFs NICHT mehr.
//
//  RAM: Es werden keine QPdfDocument-Objekte resident gehalten. Nur die fertige
//  Annotation-Liste je Pfad landet in einem kleinen LRU (kMaxCachedPdfs). Das zum
//  Scan lokal geladene Dokument lebt nur fuer die Dauer des Scans.
//
//  Registrierung via qmlRegisterSingletonInstance(…,"Viewer",…) in main.cpp.
// ─────────────────────────────────────────────────────────────────────────────
class ViewerController : public QObject {
    Q_OBJECT
public:
    explicit ViewerController(QObject* parent = nullptr);
    ~ViewerController() override;

    // Liest eine Textdatei (max. ~8 MB) als UTF-8 (mit Latin-1-Fallback).
    Q_INVOKABLE QString readTextFile(const QString& filePathOrUrl) const;

    // Schreibt den Inhalt (UTF-8) atomar zurueck auf die Datei. true bei Erfolg.
    Q_INVOKABLE bool writeTextFile(const QString& filePathOrUrl, const QString& content) const;

    // Öffnet das Medium im System-Standardprogramm. true bei Erfolg.
    Q_INVOKABLE bool openExternally(const QString& filePathOrUrl) const;

    // Asynchroner Annotation-Scan. Liefert das Ergebnis ueber pdfAnnotationsReady.
    // Bei Cache-Treffer wird das Signal sofort (queued) gefeuert.
    Q_INVOKABLE void requestPdfAnnotations(const QString& filePathOrUrl);


    // ── Intern (vom Worker-Thread per QueuedConnection aufgerufen) ────────────
    //  Nimmt das Scan-Ergebnis auf dem GUI-Thread entgegen: Cache pflegen,
    //  Temp-Dateien vormerken, Signal feuern. NICHT direkt aus QML aufrufen.
    void applyScanResult(const QString& path, const QVariantList& anns,
                         const QStringList& tempFiles);

signals:
    // Feuert (auf dem GUI-Thread), sobald die Annotationen eines PDFs vorliegen.
    void pdfAnnotationsReady(const QString& path, const QVariantList& annotations);

private:
    // LRU-Pflege fuer den Resultcache.
    void touchCache(const QString& path);
    void insertIntoCache(const QString& path, const QVariantList& anns);

    static constexpr int kMaxCachedPdfs = 16;

    QHash<QString, QVariantList> m_annCache;        // Pfad → fertige Annotation-Liste
    QStringList                  m_cacheOrder;      // LRU-Reihenfolge (alt → neu)
    QSet<QString>                m_inFlight;        // laufende Scans (Dedup)
    QStringList                  m_sessionTempFiles;// extrahierte Medien (Cleanup bei Exit)
};
