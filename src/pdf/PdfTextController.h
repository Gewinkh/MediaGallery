#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfTextController.h
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Liefert die browser-artige TEXTAUSWAHL der PDF-Hauptansicht: Ziehen markiert
//  Text, Strg+C kopiert ihn. Quelle ist die EINGEBETTETE Textebene des PDFs
//  (kein OCR) - fuer digitale PDFs praktisch kostenlos.
//
//  WARUM EINE EIGENE KLASSE (und KEIN QML-PdfSelection auf root.doc)?
//   • Die Hauptansicht skaliert ihre Seiten ueber fitScale*zoom (nicht ueber
//     renderScale). Eine eigene C++-Bruecke gibt VOLLE Kontrolle ueber die
//     Koordinaten-Abbildung (normalisiert [0..1] ↔ PDF-Punkte) und haengt nicht
//     an den internen Koordinaten-Annahmen des QML-PdfSelection.
//   • Die Auswahl laeuft ueber QPdfDocument::getSelection(page, start, end) ->
//     QPdfSelection. Deren bounds() sind Rechteck-Polygone mit Ursprung
//     oben-links in PUNKTEN - exakt das, was wir normalisiert an QML zurueck-
//     geben (wie die bestehenden Annotation-Overlays).
//
//  RAM-BEWUSST (Prio 1)
//   • LAZY: Das Auswahl-Dokument wird ERST geladen, wenn der Nutzer tatsaechlich
//     zu markieren beginnt (prepare() beim ersten Press). Reines Ansehen/Scrollen
//     eines PDFs kostet damit KEIN zusaetzliches QPdfDocument.
//   • Es ist immer hoechstens EIN Auswahl-Dokument resident (das aktive). Beim
//     Verlassen/Wechseln gibt PdfSurface es ueber releaseDocument() frei.
//   • Das Dokument haelt nur die Seitenstruktur + bei Bedarf den Text der
//     abgefragten Seite (PDFium-Cache) - KEINE Seitenbitmaps.
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
#include <QVariantMap>
#include <QThreadPool>
#include <QTimer>
#include <QRectF>
#include <QList>
#include <QHash>

#include "pdf/OcrEngine.h"      // mg::OcrLine (für OCR-Cache/Signaturen)

class QPdfDocument;
class QPdfSearchModel;
class QPdfSelection;

class PdfTextController : public QObject {
    Q_OBJECT
    // true, sobald fuer den aktiven Pfad ein Auswahl-Dokument geladen ist.
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    // Zuletzt markierter Text (für die Aktivierung der Kopier-Aktion in QML).
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectedTextChanged)
    // OCR verfügbar (Tesseract einkompiliert + Sprachdatei vorhanden)?
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable CONSTANT)
    // Läuft gerade eine (asynchrone) OCR-Erkennung?
    Q_PROPERTY(bool ocrBusy READ ocrBusy NOTIFY ocrBusyChanged)
    // ── Suche im Dokument ─────────────────────────────────────────────────────
    //  Anzahl der Treffer der laufenden/letzten Suche.
    Q_PROPERTY(int  searchCount READ searchCount NOTIFY searchChanged)
    //  Läuft noch (die Seiten werden STÜCKWEISE durchsucht, s. .cpp).
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    //  Der aktuelle Suchbegriff (leer = keine Suche).
    Q_PROPERTY(QString searchTerm READ searchTerm NOTIFY searchChanged)
public:
    explicit PdfTextController(QObject* parent = nullptr);
    ~PdfTextController() override;

    bool    isReady() const { return m_doc != nullptr; }
    QString selectedText() const { return m_selText; }
    bool    ocrAvailable() const;
    bool    ocrBusy() const { return m_ocrBusy; }
    int     searchCount() const { return m_hits.size(); }
    bool    searching() const { return m_searchPage >= 0; }
    QString searchTerm() const { return m_searchTerm; }

    // Sorgt (lazy, asynchron) dafuer, dass fuer pathOrUrl ein Auswahl-Dokument
    // geladen wird. Idempotent: laeuft bereits ein Laden/ist es aktiv -> No-Op.
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

    // ── PDF-Editor: Zeilenfang (Snapping) ─────────────────────────────────────
    //  Liefert die erkannten TEXTZEILEN einer Seite als normalisierte Rechtecke
    //  { x, y, w, h } (Ursprung oben-links). Quelle ist getAllText() - die
    //  Fragment-Polygone werden nach vertikaler Mitte gruppiert und je Zeile
    //  vereinigt. Leer, wenn das Auswahl-Dokument (lazy) noch nicht geladen ist
    //  oder die Seite keine Textebene hat -> der Editor fällt dann auf freie
    //  Platzierung zurück.
    Q_INVOKABLE QVariantList textLineRects(int page);

    // ── PDF-Editor: „Text ersetzen" (Vorbefüllungs-Sonde) ─────────────────────
    //  Prüft den NORMALISIERT [0..1] aufgezogenen Bereich gegen die erkannten
    //  Textzeilen der Seite. Getroffene Zeilen (vertikale Überlappung ≥ 35 %
    //  der Zeilenhöhe bzw. ≥ 80 % der Aufzieh-Höhe, horizontale Überlappung
    //  > 0) werden VEREINIGT - die Box schnappt exakt auf die Zeilen-Bounds.
    //  Rückgabe: { found, x, y, w, h (normalisiert, Union), lineH (normalisierte
    //  Ø-Zeilenhöhe -> Schriftgröße), text (eingebetteter Text unter der
    //  Fläche) }. found=false ohne Textebene/Treffer -> der Editor fällt STILL
    //  auf die unbefüllte Box zurück (Anforderung: kein Hinweis-Dialog).
    //  BEWUSST seiteneffektfrei: verändert weder die sichtbare Auswahl noch
    //  selectedText (Strg+C des Nutzers bleibt unberührt).
    Q_INVOKABLE QVariantMap replaceProbe(int page, double nx0, double ny0,
                                         double nx1, double ny1);

    // ── OCR (gescannte PDFs) ──────────────────────────────────────────────────
    //  Erkennt die Textzeilen der Seite ASYNCHRON (eigener 1-Thread-Pool,
    //  transiente QPdfDocument-Instanz zum Rendern -> GUI-Thread bleibt frei).
    //  Nach Erfolg liegen die Zeilen im Cache und `textLineRects`/`replaceProbe`/
    //  die Textauswahl nutzen sie automatisch, als hätte die Seite eine
    //  eingebettete Textebene. Signal `ocrReady(page,lineCount)`. No-op ohne
    //  Tesseract oder wenn die Seite bereits eine eingebettete Textebene hat.
    Q_INVOKABLE void ocrPage(int page);
    //  true, wenn für die Seite bereits OCR-Zeilen im Cache liegen.
    Q_INVOKABLE bool hasOcr(int page) const;

    // Kopiert den zuletzt markierten Text in die System-Zwischenablage.
    Q_INVOKABLE void copyToClipboard();

    // ── Suche ─────────────────────────────────────────────────────────────────
    //  search: startet eine neue Suche (leerer Begriff hebt sie auf). Die Seiten
    //  werden STÜCKWEISE durchsucht, damit die Oberfläche nicht stehenbleibt -
    //  Fortschritt/Ende meldet `searchChanged`.
    Q_INVOKABLE void search(const QString& needle);
    Q_INVOKABLE void clearSearch();
    //  Treffer-Rechtecke EINER Seite, normalisiert [0..1] wie `selectionBetween`
    //  (die Anzeige zeichnet sie wie eine Auswahl).
    Q_INVOKABLE QVariantList searchHitsOnPage(int page) const;
    //  EIN Treffer für die Ergebnisliste/Navigation:
    //  { page, x, y, w, h, before, after, ocr }.
    Q_INVOKABLE QVariantMap searchHit(int index) const;

    // ── Intern (vom Worker-Thread per QueuedConnection aufgerufen) ────────────
    //  Nimmt ein fertig geladenes (oder fehlgeschlagenes = nullptr) Dokument auf
    //  dem GUI-Thread entgegen. NICHT direkt aus QML aufrufen.
    void adoptDocument(QPdfDocument* doc, const QString& localPath, int generation);

    //  OCR-Ergebnis auf dem GUI-Thread übernehmen (vom Worker). NICHT aus QML.
    void adoptOcr(int page, const QList<mg::OcrLine>& lines, int generation);

signals:
    void readyChanged();
    void selectedTextChanged();
    void ocrBusyChanged();
    void ocrReady(int page, int lineCount);
    //  Suchzustand geändert (neue Treffer, Fortschritt, Ende).
    void searchChanged();

private:
    // Baut aus einer QPdfSelection die normalisierte Rechteckliste und merkt den
    // Text. pageSize ist die Seitengroesse in Punkten (zum Normalisieren).
    QVariantList applySelection(const QPdfSelection& sel, int page,
                                double pageWidthPts, double pageHeightPts);

    // OCR-Textauswahl (gescannte Seiten): liefert die Highlight-Rechtecke der
    // OCR-Zeilen, die `dragPts` (in Punkten) schneiden - bzw. ALLE bei
    // selectAll - und merkt deren Text. Zeilengranular (OCR liefert Zeilen).
    QVariantList ocrSelection(int page, const QRectF& dragPts,
                              double pageWidthPts, double pageHeightPts, bool selectAll);

    // Erkannte Textzeilen einer Seite in PDF-PUNKTEN (gemeinsamer Kern von
    // textLineRects und replaceProbe): Fragment-Rechtecke aus getAllText()
    // nach vertikaler Mitte gruppiert und je Zeile vereinigt.
    QList<QRectF> lineRectsPts(int page) const;

    QPdfDocument* m_doc = nullptr;   // aktives Auswahl-Dokument (GUI-Thread-Affinitaet)
    QString       m_activePath;      // lokaler Pfad des aktiven Dokuments
    QString       m_pendingPath;     // lokaler Pfad eines gerade ladenden Dokuments
    int           m_generation = 0;  // verwirft veraltete Async-Ladevorgaenge

    // 1 Thread -> nie zwei QPdfDocument-Ladevorgaenge gleichzeitig (RAM-Peak gedeckelt).
    QThreadPool   m_pool;

    QString       m_selText;         // zuletzt markierter Text

    // ── OCR ───────────────────────────────────────────────────────────────────
    //  Eigener 1-Thread-Pool (blockiert nicht die Dokumentladung); Cache je Seite
    //  (erkannte Zeilen in PDF-Punkten + Text); Generationszahl verwirft veraltete
    //  Läufe (Pfadwechsel/Freigabe).
    QThreadPool   m_ocrPool;
    QHash<int, QList<mg::OcrLine>> m_ocrCache;

    // ── Suche ────────────────────────────────────────────────────────────────
    //  EIN Treffer. `rect` steht in PDF-Punkten mit Ursprung oben-links -
    //  genau so liefert QPdfSearchModel sie (gemessen), also dieselbe
    //  Konvention wie im ganzen Editor.
    //  EIN Treffer = EINE Fundstelle, auch wenn sie über mehrere Rechtecke
    //  gezeichnet wird. PDFium liefert je Fundstelle so viele Rechtecke, wie sie
    //  Zeige-Operatoren berührt - und manche Erzeuger (dieses DOCX->PDF etwa)
    //  setzen JEDES Zeichen einzeln. Ein Rechteck = ein Treffer zu zählen ergab
    //  dort „7 Treffer" für das eine Wort „Stellen" (Nutzerbefund).
    struct SearchHit {
        int             page = 0;
        QList<QRectF>   rects;      // alle Teilstücke DIESER Fundstelle
        QString         before;
        QString         after;
        bool            ocr = false;  // aus der OCR-Zeile, nicht aus der Textebene

        //  Umschließendes Rechteck - für das Anspringen (▲/▼).
        QRectF bounds() const {
            QRectF b;
            for (const QRectF& r : rects) b = b.isNull() ? r : b.united(r);
            return b;
        }
    };
    QVector<SearchHit> m_hits;
    QString            m_searchTerm;
    //  Nächste zu durchsuchende Seite (−1 = keine Suche läuft). QPdfSearchModel
    //  arbeitet LAZY je Seite (gemessen: erst `resultsOnPage(p)` durchsucht sie)
    //  - der Timer holt sie stückweise, damit eine 500-Seiten-Datei die
    //  Oberfläche nicht einfriert.
    int                m_searchPage = -1;
    QPdfSearchModel*   m_searchModel = nullptr;
    //  Dokument, zu dem die Treffer gehören. Ohne diesen Vergleich hätte der
    //  Kurzschluss „derselbe Begriff -> nichts tun" eine Suche verschluckt, die
    //  VOR dem Öffnen eingetippt wurde (sie lief nie, der Begriff stand aber
    //  schon da).
    QPdfDocument*      m_searchedDoc = nullptr;
    QTimer             m_searchTimer;
    void stepSearch();
    //  Treffer der OCR-Zeilen einer Seite (nur wo es OCR gibt): die ZEILE ist
    //  der Treffer - feiner geht es nicht, OCR liefert keine Zeichenlagen.
    void appendOcrHits(int page);
    int           m_ocrGen  = 0;
    bool          m_ocrBusy = false;
};
