#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfTextController.h
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Liefert die browser-artige TEXTAUSWAHL der PDF-Hauptansicht: Ziehen markiert
//  Text, Strg+C kopiert ihn. Quelle ist die EINGEBETTETE Textebene des PDFs
//  (kein OCR) — fuer digitale PDFs praktisch kostenlos.
//
//  WARUM EINE EIGENE KLASSE (und KEIN QML-PdfSelection auf root.doc)?
//   • Die Hauptansicht skaliert ihre Seiten ueber fitScale*zoom (nicht ueber
//     renderScale). Eine eigene C++-Bruecke gibt VOLLE Kontrolle ueber die
//     Koordinaten-Abbildung (normalisiert [0..1] ↔ PDF-Punkte) und haengt nicht
//     an den internen Koordinaten-Annahmen des QML-PdfSelection.
//   • Die Auswahl laeuft ueber QPdfDocument::getSelection(page, start, end) →
//     QPdfSelection. Deren bounds() sind Rechteck-Polygone mit Ursprung
//     oben-links in PUNKTEN — exakt das, was wir normalisiert an QML zurueck-
//     geben (wie die bestehenden Annotation-Overlays).
//
//  RAM-BEWUSST (Prio 1)
//   • LAZY: Das Auswahl-Dokument wird ERST geladen, wenn der Nutzer tatsaechlich
//     zu markieren beginnt (prepare() beim ersten Press). Reines Ansehen/Scrollen
//     eines PDFs kostet damit KEIN zusaetzliches QPdfDocument.
//   • Es ist immer hoechstens EIN Auswahl-Dokument resident (das aktive). Beim
//     Verlassen/Wechseln gibt PdfSurface es ueber releaseDocument() frei.
//   • Das Dokument haelt nur die Seitenstruktur + bei Bedarf den Text der
//     abgefragten Seite (PDFium-Cache) — KEINE Seitenbitmaps.
//
//  ASYNC-MUSTER (Projektkonvention, wie PdfScanTask/PdfThumbRenderTask)
//   • prepare() stoesst einen QRunnable an (eigener QThreadPool, maxThreadCount=1),
//     der eine EIGENE QPdfDocument-Instanz laedt (Parsen blockiert nie den
//     GUI-Thread). Nach erfolgreichem Laden wird das Dokument auf den GUI-Thread
//     verschoben und per Qt::QueuedConnection uebergeben. Eine Generationszahl
//     verwirft veraltete Ladevorgaenge (schnelles Vor/Zurueck ist sicher).
//
//  Registrierung: qmlRegisterSingletonInstance(…, "PdfText", …) in main.cpp.
// ══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QThreadPool>

class QPdfDocument;
class QPdfSelection;

class PdfTextController : public QObject {
    Q_OBJECT
    // true, sobald fuer den aktiven Pfad ein Auswahl-Dokument geladen ist.
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    // Zuletzt markierter Text (für die Aktivierung der Kopier-Aktion in QML).
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectedTextChanged)
public:
    explicit PdfTextController(QObject* parent = nullptr);
    ~PdfTextController() override;

    bool    isReady() const { return m_doc != nullptr; }
    QString selectedText() const { return m_selText; }

    // Sorgt (lazy, asynchron) dafuer, dass fuer pathOrUrl ein Auswahl-Dokument
    // geladen wird. Idempotent: laeuft bereits ein Laden/ist es aktiv → No-Op.
    // Ein anderer Pfad verwirft das vorherige Dokument.
    Q_INVOKABLE void prepare(const QString& pathOrUrl);

    // Gibt das aktive Auswahl-Dokument frei (RAM) und hebt die Auswahl auf.
    // Verwirft zugleich einen evtl. laufenden Ladevorgang.
    Q_INVOKABLE void releaseDocument();

    // Markiert Text zwischen zwei NORMALISIERTEN Punkten [0..1] (Ursprung
    // oben-links) auf 'page'. Liefert die Highlight-Rechtecke als Liste
    // normalisierter Maps { x, y, w, h } (wie die Annotation-Overlays).
    // Merkt sich zugleich den Text fuer copyToClipboard(). Leer, falls das
    // Dokument noch nicht geladen ist oder kein Text getroffen wurde.
    Q_INVOKABLE QVariantList selectionBetween(int page,
                                              double nx0, double ny0,
                                              double nx1, double ny1);

    // Markiert den GESAMTEN Text einer Seite (Strg+A). Gleiche Rueckgabeform.
    Q_INVOKABLE QVariantList selectAllOnPage(int page);

    // Hebt die aktuelle Auswahl auf (z. B. reiner Klick ohne Ziehen).
    Q_INVOKABLE void clearSelection();

    // Kopiert den zuletzt markierten Text in die System-Zwischenablage.
    Q_INVOKABLE void copyToClipboard();

    // ── Intern (vom Worker-Thread per QueuedConnection aufgerufen) ────────────
    //  Nimmt ein fertig geladenes (oder fehlgeschlagenes = nullptr) Dokument auf
    //  dem GUI-Thread entgegen. NICHT direkt aus QML aufrufen.
    void adoptDocument(QPdfDocument* doc, const QString& localPath, int generation);

signals:
    void readyChanged();
    void selectedTextChanged();

private:
    // Baut aus einer QPdfSelection die normalisierte Rechteckliste und merkt den
    // Text. pageSize ist die Seitengroesse in Punkten (zum Normalisieren).
    QVariantList applySelection(const QPdfSelection& sel, int page,
                                double pageWidthPts, double pageHeightPts);

    QPdfDocument* m_doc = nullptr;   // aktives Auswahl-Dokument (GUI-Thread-Affinitaet)
    QString       m_activePath;      // lokaler Pfad des aktiven Dokuments
    QString       m_pendingPath;     // lokaler Pfad eines gerade ladenden Dokuments
    int           m_generation = 0;  // verwirft veraltete Async-Ladevorgaenge

    // 1 Thread → nie zwei QPdfDocument-Ladevorgaenge gleichzeitig (RAM-Peak gedeckelt).
    QThreadPool   m_pool;

    QString       m_selText;         // zuletzt markierter Text
    int           m_selPage = -1;    // Seite der aktuellen Auswahl
};
