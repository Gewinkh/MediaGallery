#pragma once
#include <QColor>
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
//  ViewerController - C++->QML-Bridge (Singleton) für die Viewer-Hilfsdienste,
//  die kein reines Property/Model sind:
//
//   • readTextFile(path)  -> Datei-Inhalt (UTF-8, größenbegrenzt) für TextSurface.
//   • openExternally(path)-> Öffnet Medium im System-Standardprogramm (Video-Mode
//                           "external").
//   • requestPdfAnnotations(path) -> ASYNCHRONER Scan der Medien-/Link-Annotationen.
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

    //  Groessendeckel des Text-Editors. Groesseres wird NUR ANGELESEN - der
    //  Editor schaltet die Datei dann auf nur lesen (s. textFileTruncated).
    static constexpr qint64 kMaxTextBytes = 8 * 1024 * 1024;

    // Liest eine Textdatei (max. kMaxTextBytes) als UTF-8 (mit Latin-1-Fallback).
    Q_INVOKABLE QString readTextFile(const QString& filePathOrUrl) const;

    //  Ist die Datei groesser als der Deckel, liegt also nur ihr Anfang vor?
    //  QML fragt das beim Oeffnen und sperrt dann das Schreiben. Kostet einen
    //  stat-Aufruf und haelt keinen Zustand - der Editor kann jederzeit fragen.
    Q_INVOKABLE bool textFileTruncated(const QString& filePathOrUrl) const;

    //  Schreibt den Inhalt (UTF-8) atomar zurueck auf die Datei. true bei Erfolg.
    //  VERWEIGERT die Arbeit, wenn die Zieldatei groesser als kMaxTextBytes ist:
    //  von so einer Datei liegt uns nur der Anfang vor, ein Rueckschreiben wuerde
    //  den Rest unwiederbringlich abschneiden (gemessen: 9,86-MB-Log verlor nach
    //  einem einzigen Tastendruck 1,47 MB). Diese Sperre ist die letzte Instanz -
    //  die Oberflaeche sperrt schon vorher (s. textFileTruncated).
    Q_INVOKABLE bool writeTextFile(const QString& filePathOrUrl, const QString& content) const;

    // Öffnet das Medium im System-Standardprogramm. true bei Erfolg.
    Q_INVOKABLE bool openExternally(const QString& filePathOrUrl) const;

    // Asynchroner Annotation-Scan. Liefert das Ergebnis ueber pdfAnnotationsReady.
    // Bei Cache-Treffer wird das Signal sofort (queued) gefeuert.
    Q_INVOKABLE void requestPdfAnnotations(const QString& filePathOrUrl);

    // Schreibt content als PDF NEBEN die Quelle (<Name>.pdf, bei Kollision
    // "<Name> (2).pdf"); die Quelldatei bleibt unangetastet. Paginiert wird im
    // Worker (Regel 8/17), das Ergebnis kommt ueber textPdfExportFinished.
    //   content KOMMT AUS DEM EDITOR, nicht von Platte: TextSurface ist
    //   editierbar, ein erneutes Einlesen wuerde ungespeicherte Aenderungen
    //   verlieren. Festlegungen des Layouts s. core/TextPdfExporter.h.
    //  textColor: Schriftfarbe des Fließtextes. QML gibt sie mit (Farbe der
    //  Datei bzw. globale Vorgabe, s. AppController::fileTextPdfColor);
    //  ungültig ⇒ Schwarz.
    //  tabWidth: Tabulatorweite in ZEICHEN, aus `Editor.tabWidth`. Damit ist
    //  derselbe Text gedruckt genauso eingerueckt wie am Bildschirm; frueher
    //  stand im Exporter eine feste 8 und eine mit vier eingerueckte Datei sah
    //  im PDF doppelt so tief eingerueckt aus.
    Q_INVOKABLE void exportTextToPdf(const QString& filePathOrUrl, const QString& content,
                                     const QColor& textColor = QColor(Qt::black),
                                     int tabWidth = 4);


    // ── Intern (vom Worker-Thread per QueuedConnection aufgerufen) ────────────
    //  Nimmt das Scan-Ergebnis auf dem GUI-Thread entgegen: Cache pflegen,
    //  Temp-Dateien vormerken, Signal feuern. NICHT direkt aus QML aufrufen.
    void applyScanResult(const QString& path, const QVariantList& anns,
                         const QStringList& tempFiles);

signals:
    // Feuert (auf dem GUI-Thread), sobald die Annotationen eines PDFs vorliegen.
    void pdfAnnotationsReady(const QString& path, const QVariantList& annotations);

    // Ergebnis von exportTextToPdf (GUI-Thread). target = geschriebene Datei.
    void textPdfExportFinished(bool ok, const QString& target, const QString& error);

private:
    // LRU-Pflege fuer den Resultcache.
    void touchCache(const QString& path);
    void insertIntoCache(const QString& path, const QVariantList& anns);

    static constexpr int kMaxCachedPdfs = 16;

    QHash<QString, QVariantList> m_annCache;        // Pfad -> fertige Annotation-Liste
    QStringList                  m_cacheOrder;      // LRU-Reihenfolge (alt -> neu)
    QSet<QString>                m_inFlight;        // laufende Scans (Dedup)
    QStringList                  m_sessionTempFiles;// extrahierte Medien (Cleanup bei Exit)
};
