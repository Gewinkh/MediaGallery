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

// Viewer-Hilfsdienste, die kein Property oder Modell sind: Textdatei lesen, extern
// oeffnen, PDF-Annotationen asynchron scannen. Kein QPdfDocument bleibt resident -
// nur die fertige Annotationsliste je Pfad landet in einem kleinen LRU.
class ViewerController : public QObject {
    Q_OBJECT
public:
    explicit ViewerController(QObject* parent = nullptr);
    ~ViewerController() override;

    //  Groessendeckel des Text-Editors. Groesseres wird NUR ANGELESEN - der
    //  Editor schaltet die Datei dann auf nur lesen (s. textFileTruncated).
    static constexpr qint64 kMaxTextBytes = 8 * 1024 * 1024;

    Q_INVOKABLE QString readTextFile(const QString& filePathOrUrl) const;

    //  Ist die Datei groesser als der Deckel, liegt also nur ihr Anfang vor?
    //  QML fragt das beim Oeffnen und sperrt dann das Schreiben. Kostet einen
    //  stat-Aufruf und haelt keinen Zustand - der Editor kann jederzeit fragen.
    Q_INVOKABLE bool textFileTruncated(const QString& filePathOrUrl) const;

    // VERWEIGERT die Arbeit, wenn die Zieldatei größer als `kMaxTextBytes` ist: von so einer Datei liegt uns nur der
    // Anfang vor, ein Rückschreiben schnitte den Rest ab (gemessen: 9,86-MB-Log verlor 1,47 MB nach einem Tastendruck).
    //  Sieht die Datei nach einem DATEV-Buchungsstapel aus? Entschieden wird am
    //  INHALT: dieselbe Datei wird als .csv UND als .txt ausgeliefert.
    Q_INVOKABLE bool isDatevFile(const QString& filePathOrUrl) const;

    //  Gewoehnliche Tabellendatei? Entschieden wird an der ENDUNG (.csv/.tsv):
    //  eine `.txt` bleibt Text, sonst wuerde ein Logfile mit Semikolons
    //  ploetzlich als Tabelle aufgehen.
    Q_INVOKABLE bool isTableFile(const QString& filePathOrUrl) const;

    Q_INVOKABLE bool writeTextFile(const QString& filePathOrUrl, const QString& content) const;

    Q_INVOKABLE bool openExternally(const QString& filePathOrUrl) const;

    Q_INVOKABLE void requestPdfAnnotations(const QString& filePathOrUrl);

    // Schreibt content als PDF neben die Quelle; die Quelldatei bleibt unangetastet.
    // content kommt AUS DEM EDITOR, nicht von Platte - erneutes Einlesen verloere
    // ungespeicherte Aenderungen. tabWidth in Zeichen, damit Druck und Schirm gleich sind.
    Q_INVOKABLE void exportTextToPdf(const QString& filePathOrUrl, const QString& content,
                                     const QColor& textColor = QColor(Qt::black),
                                     int tabWidth = 4);


    // Intern (vom Worker-Thread per QueuedConnection aufgerufen)
    //  Nimmt das Scan-Ergebnis auf dem GUI-Thread entgegen: Cache pflegen,
    //  Temp-Dateien vormerken, Signal feuern. NICHT direkt aus QML aufrufen.
    void applyScanResult(const QString& path, const QVariantList& anns,
                         const QStringList& tempFiles);

signals:
    void pdfAnnotationsReady(const QString& path, const QVariantList& annotations);

    void textPdfExportFinished(bool ok, const QString& target, const QString& error);

private:
    void touchCache(const QString& path);
    void insertIntoCache(const QString& path, const QVariantList& anns);

    static constexpr int kMaxCachedPdfs = 16;

    QHash<QString, QVariantList> m_annCache;        // Pfad -> fertige Annotation-Liste
    QStringList                  m_cacheOrder;      // LRU-Reihenfolge (alt -> neu)
    QSet<QString>                m_inFlight;        // laufende Scans (Dedup)
    QStringList                  m_sessionTempFiles;// extrahierte Medien (Cleanup bei Exit)
};
