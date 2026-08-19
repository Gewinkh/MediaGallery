#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfExtractController.h - QML-Singleton „PdfExtract"
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Extrahiert ausgewählte PDF-Seiten in eine NEUE PDF-Datei - für beide Wege:
//   • aus der GEÖFFNETEN PDF (Kontextmenü der PdfSurface: eine oder mehrere
//     Seiten), Ziel = Ordner der Quelldatei;
//   • GLOBAL aus allen PDFs des aktuellen Ordners (FilterBar -> Auswahldialog),
//     Seiten mehrerer Quellen in EINER Ausgabedatei.
//
//  VERFAHREN (Entscheidung nach §0-Prioritätenliste)
//  ─────────────────────────────────────────────────
//  Primär VERLUSTFREI über den PdfPageCopier (Roh-Kopie auf Objektebene:
//  Textebene/Vektoren/Fonts/Annotationen bleiben erhalten). Scheitert eine
//  Quelle (verschlüsselt, exotisch, defekt), rastert der Worker NUR DEREN
//  Seiten mit kRasterDpi als JPEG-Bildseiten in dieselbe Ausgabe - Degradations-
//  kette wie beim RHI-Backend (RhiProber): bestmöglich, aber garantiert ein
//  Ergebnis. Geschrieben wird atomar über QSaveFile.
//
//  NAMEN & KOLLISIONEN (Anforderung)
//  ─────────────────────────────────
//   • Einzelseite ohne Eingabe:  „<Quellname> - Page <N>.pdf"
//   • Mehrfachauswahl in-PDF:    „<Quellname>-Selected.pdf"
//   • Global: Name ist PFLICHT (QML erzwingt nichtleer).
//  Existiert der Zielname, wird „ (1)", „ (2)" … angehängt. Die Endung „.pdf"
//  wird IMMER automatisch ergänzt (kein Eingabeschritt). Die Namens-Säuberung
//  folgt AppController::createEmptyFile (Pfadtrenner raus, führende Punkte weg).
//
//  ASYNC-MUSTER (Projektkonvention wie PdfEditController/ThumbnailLoader):
//  QRunnable im EIGENEN QThreadPool (maxThreadCount=1), kooperativer Abbruch
//  über std::atomic<bool>, Generationszähler gegen veraltete Ergebnisse,
//  Rückmeldungen via QMetaObject::invokeMethod(…, Qt::QueuedConnection).
//
//  scanFolder() liefert die PDF-Liste des Ordners für den globalen Dialog
//  asynchron ([{path,name,pageCount}]) - die Seitenzahl kommt aus dem leicht-
//  gewichtigen Struktur-Parse (PdfAssembler::probePageCount, kein Rendern);
//  nur wenn der scheitert (z. B. verschlüsselt), lädt der Worker QPdfDocument.
//
//  Registrierung: qmlRegisterSingletonInstance(…, "PdfExtract", …) in main.cpp.
// ══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <QThreadPool>
#include <atomic>
#include <memory>

class PdfExtractController : public QObject {
    Q_OBJECT
    // Läuft gerade eine Extraktion/ein Ordner-Scan? (Buttons in QML sperren.)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit PdfExtractController(QObject* parent = nullptr);
    ~PdfExtractController() override;

    // Raster-Fallback: Parität zum Editor-Export (PdfEditController 150 dpi).
    static constexpr qreal kRasterDpi         = 150.0;
    static constexpr int   kRasterJpegQuality = 85;
    // Kantenschutz beim Fallback-Rendern extrem großer Seiten (RAM-Deckel).
    static constexpr int   kRasterMaxPx       = 8000;

    bool busy() const { return m_busy; }

    // ── Namens-Vorschläge (Platzhalter der Namensdialoge, OHNE „.pdf") ───────
    Q_INVOKABLE QString defaultSingleName(const QString& pathOrUrl,
                                          int pageIndex) const;
    Q_INVOKABLE QString defaultMultiName(const QString& pathOrUrl) const;

    // ── Extraktion (baseName leer -> Default; Ziel = Ordner der Quelle) ───────
    Q_INVOKABLE void extractSingle(const QString& pathOrUrl, int page,
                                   const QString& baseName);
    Q_INVOKABLE void extractSelection(const QString& pathOrUrl,
                                      const QVariantList& pages,
                                      const QString& baseName);
    // Global: jobs = [{path, pages:[…]}, …] in Ordnerreihenfolge; Ziel-Ordner
    // explizit; baseName ist Pflicht (QML erzwingt, hier defensiv geprüft).
    Q_INVOKABLE void extractGlobal(const QVariantList& jobs,
                                   const QString& folderOrUrl,
                                   const QString& baseName);
    // Geordnete Extraktion (Werkbank-Auswahlleiste): items = [{path, page}, …]
    // in EXAKT dieser Reihenfolge = Ausgabereihenfolge. Aufeinanderfolgende
    // Seiten derselben Quelle werden zu einem Job zusammengefasst (weniger
    // Struktur-Parses), die Reihenfolge bleibt erhalten; (path,page)-Duplikate
    // werden verworfen (erstes Vorkommen gewinnt). baseName leer -> Default aus
    // der ersten Quelle („<Name>-Selected"). Ziel-Ordner explizit.
    Q_INVOKABLE void extractOrdered(const QVariantList& items,
                                    const QString& folderOrUrl,
                                    const QString& baseName);

    // ── Ordner-Scan für den globalen Dialog ──────────────────────────────────
    Q_INVOKABLE void scanFolder(const QString& folderOrUrl);

    // Nur für die Worker-Tasks (queued Rückweg) - nicht aus QML aufrufen.
    void extractTaskFinished(bool ok, const QString& target,
                             const QString& error, int generation);
    void extractTaskProgress(int done, int total, int generation);
    void scanTaskFinished(const QVariantList& files, int generation);

signals:
    void busyChanged();
    // ok=true: targetPath = erzeugte Datei. QML aktualisiert danach die Galerie
    // (App.refreshCurrentFolder) und zeigt die Statusmeldung.
    void extractFinished(bool ok, const QString& targetPath,
                         const QString& errorText);
    void extractProgress(int done, int total);
    // Ergebnis von scanFolder: [{path, name, pageCount}] (Name aufsteigend).
    void folderPdfsReady(const QVariantList& files);

private:
    struct Job {
        QString      path;
        QVector<int> pages;   // 0-basiert, aufsteigend (Originalreihenfolge)
    };

    void    startExtract(QVector<Job> jobs, const QString& targetPath);
    void    setBusy(bool b);
    // Säubern + Kollisionsauflösung „ (1)", „ (2)" … -> absoluter Zielpfad.
    static QString makeTargetPath(const QString& folder, QString base,
                                  const QString& fallbackBase);
    // QML-Seitenliste -> sortierte, deduplizierte 0-basierte Indizes.
    static QVector<int> normalizePages(const QVariantList& pages);

    QThreadPool m_pool;                                   // 1 Worker (RAM-Deckel)
    std::shared_ptr<std::atomic<bool>> m_cancel;          // kooperativer Abbruch
    int  m_generation = 0;                                // veraltete Tasks filtern
    bool m_busy       = false;
};
