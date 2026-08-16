#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfEditController.h
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  QML-Singleton („PdfEdit") des PDF-Editors: verwaltet den Bearbeitungsmodus,
//  das Overlay-Modell (PdfEditModel), die Auswahl, das delta-basierte Undo/Redo
//  (QUndoStack — seit Qt 6 Teil von QtGui, KEINE Widgets-Abhängigkeit), die
//  Sidecar-Persistenz und den asynchronen PDF-Export.
//
//  OVERLAY-ARCHITEKTUR
//  ───────────────────
//   • Das Original-PDF wird NIE verändert. Alle Bearbeitungen sind
//     PdfEditBox-Objekte über den gerenderten Seiten (Anzeige: QML).
//   • SPEICHERN  → Sidecar „<pdf>.mgedit.json" neben dem Original (QSaveFile,
//     atomar). Beim nächsten Öffnen lädt setDocument() das Sidecar → die
//     Bearbeitung bleibt dauerhaft editierbar (kein Flatten-only-Workflow).
//   • EXPORT     → rendert Original + Overlay in ein NEUES PDF (Worker-Task,
//     eigene QPdfDocument-Instanz → PDFium-Mutex der Anzeige unberührt).
//     Ziel ist IMMER eine Kopie „…_bearbeitet(.n).pdf" — das Original wird
//     nie verändert, die Notizen bleiben über das Sidecar reversibel.
//
//  SESSIONS (RAM-effizientes Undo, KEINE Snapshots)
//  ────────────────────────────────────────────────
//  Kontinuierliche Gesten laufen als Session: begin…() merkt den Altwert,
//  update…() schreibt live ins Modell (Anzeige folgt sofort, KEIN Kommando je
//  Mausbewegung/Taste), end…() erzeugt genau EIN Delta-Kommando alt→neu.
//  Stil-Änderungen (Bold/Farbe/…) sind Einzel-Kommandos; aufeinanderfolgende
//  Änderungen desselben Feldes verschmelzen (PdfEditFieldCommand::mergeWith).
//
//  ASYNC-MUSTER (Projektkonvention): QRunnable + eigener QThreadPool
//  (maxThreadCount=1) + kooperatives Atomic-Cancel + QueuedConnection zurück
//  auf den GUI-Thread; eine Generationszahl verwirft veraltete Ergebnisse.
//
//  Registrierung: qmlRegisterSingletonInstance(…, "PdfEdit", …) in main.cpp.
// ══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QHash>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include <QColor>
#include <QJsonObject>
#include <QUndoStack>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <memory>

#include "pdf/edit/PdfEditModel.h"
#include "pdf/edit/PdfEditTypes.h"
#include "pdf/edit/PdfTextLayout.h"     // mg::PdfGlyph (Caret-Layout der Seite)
#include "pdf/edit/PdfFormFields.h"     // mg::PdfFormField (AcroForm-Felder)
#include "pdf/edit/PdfAnnotations.h"    // mg::PdfAnnotation (übernommene Notizen)
#include "pdf/edit/PdfContentEditor.h"  // mg::PdfTextEdit (Schwärzungen)

class ISettings;

class PdfEditController : public QObject {
    Q_OBJECT
public:
    // Aktives Werkzeug (Panel-Palette). Select = Auswählen/Verschieben/Skalieren;
    // die Werte 0–5 sind identisch zum Bild-Editor (ImageEditController::Tool),
    // ReplaceTool (6, „Text ersetzen") ist PDF-exklusiv.
    enum Tool { Select = 0, TextTool, FreehandTool, ArrowTool, RectTool, EllipseTool,
                ReplaceTool, CaretTool, MarkupTool, RedactTool };
    Q_ENUM(Tool)

private:
    // Bearbeitungsmodus (View ⇄ Edit) — reiner Zustandswechsel, KEIN Reload.
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged)
    Q_PROPERTY(int  trackedCount READ trackedCount NOTIFY trackedChanged)
    // Aktives Werkzeug (0 Auswahl … 7 Text bearbeiten) — s. Tool-Enum.
    Q_PROPERTY(int  tool READ tool WRITE setTool NOTIFY toolChanged)
    // Zähler: bumpt bei jeder Änderung der „Vorlagen"-Defaults (neue Annotation)
    // → Panel/Toolbar lesen defaultInfo() rev-getrieben, wenn nichts ausgewählt.
    Q_PROPERTY(int  defaultRev READ defaultRev NOTIFY defaultRevChanged)
    // Undo/Redo-Verfügbarkeit (Toolbar-Buttons).
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    // Ungesicherte Overlay-Änderungen (relativ zum Sidecar-Stand).
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    // Ausgewählte Box (-1 = keine). QML setzt beim Klick.
    Q_PROPERTY(int selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged)
    // Zähler: erhöht sich bei Auswahl- UND Datenänderung der ausgewählten Box →
    // QML-Bindings der Format-Toolbar/-Panels lesen boxInfo() rev-getrieben neu
    // (gleiches Muster wie _audioRev in PdfSurface).
    Q_PROPERTY(int selectionRev READ selectionRev NOTIFY selectionRevChanged)
    // Export läuft (Buttons sperren).
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // Offene Inline-Textbearbeitung (Session aktiv)? QML nutzt das, um den
    // Entf-Shortcut (Box löschen) zu sperren, solange getippt wird — Entf
    // gehört dann dem TextEdit (Zeichen löschen), nicht der Box.
    Q_PROPERTY(bool textEditing READ textEditing NOTIFY textEditingChanged)
    // Notiz in der (kachel-lokalen) Zwischenablage vorhanden? → Einfügen-Button.
    Q_PROPERTY(bool hasClipboard READ hasClipboard NOTIFY clipboardChanged)
    // Text-Eigenschaften als obere Leiste (Word-Stil) statt rechter Seitenleiste.
    // Persistiert in den App-Einstellungen (ISettings).
    Q_PROPERTY(bool panelOnTop READ panelOnTop WRITE setPanelOnTop NOTIFY panelOnTopChanged)

    // Seiten hinzufügen/entfernen (Aufgabe 3): destruktiv (Original-PDF sofort
    // neu schreiben) vs. nicht-destruktiv (Plan im Sidecar, wirkt beim Export).
    Q_PROPERTY(bool pageEditDestructive READ pageEditDestructive WRITE setPageEditDestructive NOTIFY pageEditDestructiveChanged)

    // Verhalten des EINEN Export-Knopfes (Einstellungen → Editor → PDF-Editor).
    // true (Standard) = verlustfrei bevorzugen (Textebene bearbeiten, Seite
    // bleibt vektoriell; automatischer Rückfall auf Raster, wo nicht sicher);
    // false = immer Raster-Export. Persistiert in ISettings.
    Q_PROPERTY(bool exportLossless READ exportLossless WRITE setExportLossless NOTIFY exportLosslessChanged)
    // Eigene Notizen als ECHTE PDF-Annotationen exportieren (Interchange)
    // statt sie zu malen. Persistiert in ISettings; s. dort zur Begründung des
    // Standards und zu den Notizarten, die immer gemalt werden.
    Q_PROPERTY(bool exportAsAnnotations READ exportAsAnnotations
               WRITE setExportAsAnnotations NOTIFY exportAsAnnotationsChanged)
    // Anzahl Ansichts-Seiten laut Seiten-Plan (inkl. eingefügter Leerseiten,
    // ohne entfernte) — treibt die Seiten-ListView.
    Q_PROPERTY(int  viewPageCount READ viewPageCount NOTIFY planChanged)
    // ── Caret („Text bearbeiten", Werkzeug 7) ────────────────────────────────
    // Seite, auf der das Caret steht (−1 = keines gesetzt).
    Q_PROPERTY(int  caretPage READ caretPage NOTIFY caretChanged)
    // Glyphen-Index VOR dem das Caret steht (0 … Anzahl Glyphen).
    Q_PROPERTY(int  caretIndex READ caretIndex NOTIFY caretChanged)
    // Caret-Rechteck in PDF-Punkten der Seite (leer = nicht sichtbar).
    Q_PROPERTY(QRectF caretRectPt READ caretRectPt NOTIFY caretChanged)
    // Layout der Caret-Seite geladen? (asynchron; solange false ist die Seite
    // nicht zeichenweise bearbeitbar — QML zeigt einen Wartehinweis.)
    Q_PROPERTY(bool caretReady READ caretReady NOTIFY caretReadyChanged)
    // Grund, warum die Seite NICHT zeichenweise bearbeitbar ist (leer = alles
    // in Ordnung) — z. B. fehlende Glyphenbreiten, verschlüsselt.
    Q_PROPERTY(QString caretError READ caretError NOTIFY caretReadyChanged)
    // Eine Textebenen-Änderung wird gerade in die Arbeitsdatei geschrieben.
    Q_PROPERTY(bool textOpsBusy READ textOpsBusy NOTIFY textOpsBusyChanged)
    // Anzahl der Änderungen an der eingebetteten Textebene (Statusanzeige).
    Q_PROPERTY(int  textOpCount READ textOpCount NOTIFY textOpsChanged)

    // ── AcroForm-Formularfelder (PdfFormFields) ───────────────────────────────
    //  Qt PDF zeichnet Widget-Annotationen NICHT (PDFium malt sie nur über
    //  FPDF_FFLDraw, das Qt PDF nicht anbietet) — dieses Overlay ist deshalb die
    //  EINZIGE Darstellung der Felder, nicht bloß eine Eingabehilfe.
    //  Je Widget ein QVariantMap-Eintrag (s. formFields()).
    Q_PROPERTY(QVariantList formFields READ formFields NOTIFY formFieldsChanged)
    // Enthält das Dokument überhaupt ausfüllbare Felder? (Knopf/Leiste zeigen.)
    Q_PROPERTY(bool hasForm READ hasForm NOTIFY formFieldsChanged)
    // Gepufferte Feldwerte, die noch in KEINE PDF geschrieben wurden.
    Q_PROPERTY(bool formDirty READ formDirty NOTIFY formDirtyChanged)
    // Zähler: bumpt bei JEDER Wertänderung. Die Anzeige liest ihren Wert
    // rev-getrieben über formValue(name) (Muster wie selectionRev/boxInfo) —
    // `formFields` bleibt dabei UNVERÄNDERT, damit das Tippen die Delegates des
    // Repeaters nicht neu erzeugt (Fokusverlust nach jedem Zeichen). Nötig ist
    // der Zähler trotzdem: Optionsfelder derselben Gruppe (und dasselbe Feld auf
    // mehreren Seiten) müssen sich gegenseitig sofort nachziehen.
    Q_PROPERTY(int formValueRev READ formValueRev NOTIFY formValueRevChanged)

    // Overlay-Modell für QML-Repeater (je Seite gefiltert über die page-Rolle).
    Q_PROPERTY(QObject* boxModel READ boxModel CONSTANT)
    Q_PROPERTY(int boxCount READ boxCount NOTIFY boxCountChanged)
    // Layout-Konstanten (einzige Quelle — QML-Anzeige und Export nutzen dieselben
    // Werte → WYSIWYG).
    Q_PROPERTY(qreal boxPaddingPt READ boxPaddingPt CONSTANT)
    Q_PROPERTY(qreal minBoxWPt READ minBoxWPt CONSTANT)
    Q_PROPERTY(qreal minBoxHPt READ minBoxHPt CONSTANT)
    // Post-it-Optik: Eselsohr-Größe + Schattenversatz (PDF-Punkte).
    Q_PROPERTY(qreal noteFoldPt READ noteFoldPt CONSTANT)
    Q_PROPERTY(qreal noteShadowDxPt READ noteShadowDxPt CONSTANT)
    Q_PROPERTY(qreal noteShadowDyPt READ noteShadowDyPt CONSTANT)

public:
    // Innenabstand des Textes im Box-Rechteck (PDF-Punkte).
    static constexpr qreal kBoxPaddingPt   = 3.0;
    // Mindest-Boxgröße (PDF-Punkte).
    static constexpr qreal kMinBoxWPt      = 24.0;
    static constexpr qreal kMinBoxHPt      = 14.0;
    // Render-Auflösung der Basisseiten beim Export. 150 dpi balanciert Qualität
    // und Dateigröße/RAM (A4 ≈ 1240×1754 px, transient genau EIN Bild).
    static constexpr qreal kExportRenderDpi = 150.0;
    // Undo-Deckel: Delta-Kommandos sind winzig, der Deckel ist reine Hygiene.
    static constexpr int   kUndoLimit       = 200;
    // Sidecar-Größenschutz beim Laden (wie ViewerController::readTextFile).
    static constexpr qint64 kMaxSidecarBytes = 8LL * 1024 * 1024;
    // Post-it-Optik (PDF-Punkte): Eselsohr unten rechts + weicher Schatten.
    // Anzeige (QML) und Export (QPainter) zeichnen mit exakt diesen Werten.
    static constexpr qreal kNoteFoldPt     = 10.0;
    static constexpr qreal kNoteShadowDxPt = 2.0;
    static constexpr qreal kNoteShadowDyPt = 3.0;
    // Entprellung der Textebenen-Wiedergabe (ms): So lange nach der letzten
    // Taste gewartet wird, bevor die Arbeitsdatei neu geschrieben und die
    // Anzeige neu geladen wird. Kurz genug, um wie eine Live-Vorschau zu
    // wirken, lang genug, damit fließendes Tippen genau EINEN Neubau auslöst.
    static constexpr int   kTextFlushMs    = 350;
    // Mindestgröße von ZEICHNUNGEN (Freihand/Pfeil/Rechteck/Ellipse) beim
    // Skalieren — deutlich kleiner als Textboxen (dünner Pfeil ist legitim).
    static constexpr qreal kMinDrawPt      = 4.0;

    // Default-Konstruktor für die QML-Instanziierung PRO PdfSurface (dezentraler
    // Editor je geöffneter PDF-Kachel): nutzt die zentrale AppSettings-Instanz.
    // Der zusätzlich als Singleton „PdfEdit" registrierte Controller (ISettings&-
    // Ctor) dient dann nur noch der globalen Einstellung panelOnTop.
    explicit PdfEditController(QObject* parent = nullptr);
    explicit PdfEditController(ISettings& settings, QObject* parent = nullptr);
    ~PdfEditController() override;

    //  ── Nachverfolgte Änderungen („Track Changes") ───────────────────────
    //  `recording` schaltet das Mitschreiben ein: Annotationen, die ab dann
    //  entstehen, gelten als offene Änderung; Löschen markiert statt zu
    //  entfernen. Der Schalter lebt IM Sidecar, nicht in den Einstellungen —
    //  er gehört zum Dokument, wie in einer Textverarbeitung.
    bool recording() const { return m_recording; }
    void setRecording(bool on);
    //  Zahl der offenen (noch nicht angenommenen/verworfenen) Änderungen.
    int  trackedCount() const;
    //  Annehmen = die Änderung wird endgültig (neu → bleibt, gelöscht → weg).
    //  Verwerfen = der Zustand vor der Änderung (neu → weg, gelöscht → bleibt).
    //  Jede Entscheidung ist EIN Undo-Schritt; die „alle"-Fassungen sind ein
    //  einziger Schritt für den ganzen Stapel (wie im DOCX-Streifen).
    //  Alle Notizen/Zeichnungen dieser Datei verwerfen. Bewusst NICHT die
    //  Sidecar-Datei löschen: der Editor hält die Boxen im Speicher und
    //  schriebe sie beim nächsten Sichern wieder hin. Stattdessen werden sie
    //  entfernt (EIN Undo-Schritt) und gesichert — ein leeres Overlay räumt
    //  den Sidecar von selbst ab.
    Q_INVOKABLE void discardAllAnnotations();
    Q_INVOKABLE void acceptChange(int id);
    Q_INVOKABLE void rejectChange(int id);
    Q_INVOKABLE void acceptAllChanges();
    Q_INVOKABLE void rejectAllChanges();

    bool editMode() const { return m_editMode; }
    void setEditMode(bool on);
    int  tool() const { return m_tool; }
    void setTool(int t);
    int  defaultRev() const { return m_defaultRev; }
    // Eine schwebende Tipp-Session (Caret) ist noch kein Kommando, aber sehr
    // wohl rücknehmbar und ungesichert — sonst wäre der Undo-Knopf beim
    // direkten Textbearbeiten grau, obwohl es etwas zurückzunehmen gibt.
    bool canUndo() const { return m_stack.canUndo() || m_pendingValid; }
    bool canRedo() const { return m_stack.canRedo(); }
    bool dirty() const { return !m_stack.isClean() || m_pendingValid; }
    int  selectedId() const { return m_selectedId; }
    void setSelectedId(int id);
    int  selectionRev() const { return m_selectionRev; }
    bool busy() const { return m_busy; }
    bool textEditing() const { return m_textEditId >= 0; }
    bool hasClipboard() const { return m_hasClip; }
    bool panelOnTop() const;
    void setPanelOnTop(bool v);

    bool pageEditDestructive() const;
    void setPageEditDestructive(bool v);

    bool exportLossless() const;
    void setExportLossless(bool v);
    bool exportAsAnnotations() const;
    void setExportAsAnnotations(bool v);
    int  caretPage() const { return m_caretPage; }
    int  caretIndex() const { return m_caretIndex; }
    QRectF caretRectPt() const;
    bool caretReady() const { return m_caretReady; }
    QString caretError() const { return m_caretError; }
    bool textOpsBusy() const { return m_textOpsBusy; }
    int  textOpCount() const { return m_textOps.size() + (m_pendingValid ? 1 : 0); }
    int  viewPageCount() const { return m_plan.size(); }
    QVariantList formFields() const;
    bool hasForm() const { return !m_formFields.isEmpty(); }
    bool formDirty() const { return m_formDirty; }
    int  formValueRev() const { return m_formValueRev; }
    QObject* boxModel() { return &m_model; }
    int  boxCount() const { return m_model.count(); }
    qreal boxPaddingPt() const { return kBoxPaddingPt; }
    qreal minBoxWPt() const { return kMinBoxWPt; }
    qreal minBoxHPt() const { return kMinBoxHPt; }
    qreal noteFoldPt() const { return kNoteFoldPt; }
    qreal noteShadowDxPt() const { return kNoteShadowDxPt; }
    qreal noteShadowDyPt() const { return kNoteShadowDyPt; }

    // ── Dokument-Lebenszyklus ─────────────────────────────────────────────────
    //  setDocument: aktiviert das Overlay für pathOrUrl (idempotent). Ein evtl.
    //  ungesicherter Stand des VORHERIGEN Dokuments wird automatisch ins Sidecar
    //  gesichert (nie stiller Verlust). Existiert ein Sidecar, wird es geladen.
    Q_INVOKABLE void setDocument(const QString& pathOrUrl);
    //  releaseDocument: sichert (falls dirty) und leert Modell + Undo-Stack.
    Q_INVOKABLE void releaseDocument();

    // ── Boxen erzeugen / entfernen ────────────────────────────────────────────
    //  addTextBox: freie Box an Klickposition (PDF-Punkte); Standardgröße,
    //  in die Seite geklemmt. Liefert die neue Box-ID (Auswahl folgt).
    Q_INVOKABLE int  addTextBox(int page, qreal xPt, qreal yPt,
                                qreal pageWPt, qreal pageHPt);
    //  addMarkup: EINE Textmarkierung über `quads` (Zeilenrechtecke in
    //  PDF-Punkten der Ansichts-Seite, je Eintrag `{x,y,w,h}` — QML liefert sie
    //  aus dem Zeilenfang). `style`: 0 Markieren, 1 Unterstreichen, 2
    //  Durchstreichen. ALLE Bereiche bilden EIN Objekt (wie `/QuadPoints`) —
    //  eine Markierung über drei Zeilen ist eine Notiz, kein Dreierpack.
    //  Liefert die neue Box-ID (Auswahl folgt) oder −1.
    Q_INVOKABLE int  addMarkup(int page, int style, const QVariantList& quads);
    //  addStamp: Signatur-/Stempelbild an (xPt|yPt) einfügen. Die Größe folgt
    //  dem Seitenverhältnis des Bildes (Breite `wPt`, Höhe daraus); ein nicht
    //  lesbares Bild liefert −1, statt eine leere Box zu hinterlassen.
    Q_INVOKABLE int  addStamp(const QString& pathOrUrl, int page,
                              qreal xPt, qreal yPt, qreal wPt);
    //  Stil der zuletzt benutzten Markierung (Vorlage) — Panel/Toolbar zeigen
    //  ihn an, wenn nichts ausgewählt ist.
    Q_INVOKABLE int  markupStyle() const { return m_markupStyle; }
    Q_INVOKABLE void setMarkupStyle(int style);
    //  addAnchoredTextBox: an eine erkannte PDF-Textzeile gefangene Box
    //  (Rechteck der Zeile in Punkten); Schriftgröße wird aus der Zeilenhöhe
    //  abgeleitet, anchored=true.
    Q_INVOKABLE int  addAnchoredTextBox(int page, qreal xPt, qreal yPt,
                                        qreal wPt, qreal hPt);
    //  Zeichen-Session (Freihand/Pfeil/Rechteck/Ellipse/Text ersetzen — analog
    //  Bild-Editor):
    //   beginDraw() legt die Annotation LIVE auf der Seite an (sichtbare
    //   Vorschau, KEIN Kommando), updateDraw() erweitert/skaliert live,
    //   endDraw() finalisiert → genau EIN Add-Kommando (Undo entfernt die
    //   ganze Zeichnung). kind = PdfAnnKind (1 Freihand … 5 Text ersetzen —
    //   Replace zieht wie Rechteck auf, Live-Vorschau = weiße Deckfläche,
    //   regulärer Abschluss über endReplaceDraw statt endDraw);
    //   Koordinaten in PDF-Punkten der Seite `page`.
    Q_INVOKABLE int  beginDraw(int kind, int page, qreal xPt, qreal yPt);
    Q_INVOKABLE void updateDraw(int id, qreal xPt, qreal yPt);
    Q_INVOKABLE void endDraw(int id);
    //  endReplaceDraw: schließt die „Text ersetzen"-Zeichen-Session ab (statt
    //  endDraw). QML liefert das Ergebnis der Textzeilen-Sonde
    //  (PdfTextController::replaceProbe) gleich mit:
    //   • snapped=true  → Box schnappt auf die Zeilen-Bounds (x/y/w/h in
    //     PDF-Punkten), Schriftgröße aus der Ø-Zeilenhöhe (0.72·lineHPt, wie
    //     addAnchoredTextBox), text = erkannter eingebetteter Text (Vorbefüllung).
    //   • snapped=false → Box bleibt exakt der aufgezogene Bereich, Stil aus
    //     der „Text ersetzen"-Vorlage (Muster „Vorlage erbt letzten Stil");
    //     entartete Klicks ohne Zug werden verworfen (wie endDraw).
    //  Die Deckfläche (highlight) wird IMMER auf deckendes Weiß erzwungen.
    //  Liefert die neue Box-ID (Auswahl folgt) oder −1 (verworfen).
    //  endRedactDraw: schließt die „Text schwärzen"-Geste ab. Wie
    //  `endReplaceDraw`, aber das Ergebnis ist eine DECKENDE Fläche OHNE
    //  eigenen Text: `origText` trägt den erkannten Text, der beim Export aus
    //  dem Content-Stream entfernt wird. Ohne erkannten Text (gescannte Seite)
    //  entsteht trotzdem die Fläche — dann schützt erst der Raster-Export.
    Q_INVOKABLE int  endRedactDraw(int id, bool snapped,
                                   qreal xPt, qreal yPt, qreal wPt, qreal hPt,
                                   const QString& text);
    Q_INVOKABLE int  endReplaceDraw(int id, bool snapped,
                                    qreal xPt, qreal yPt, qreal wPt, qreal hPt,
                                    qreal lineHPt, const QString& text);
    Q_INVOKABLE void removeBox(int id);

    // ── Seiten verwalten (nur PDF, Editmodus) ─────────────────────────────────
    //  SEITENARGUMENTE: Alle Q_INVOKABLE-Funktionen sprechen ANSICHTS-Indizes
    //  (0 … viewPageCount−1) — dieselbe Zählung wie die gerenderte Arbeitsdatei
    //  und die Seiten-ListView. Die Übersetzung auf den stabilen Seiten-Key
    //  (Notizen) bzw. den Quellseiten-Index (Textebene) macht der Controller.
    //
    //  QML meldet die Quell-Seitenzahl, sobald das PdfDocument bereit ist →
    //  initialisiert den Plan (Identität [0..n-1]), falls kein Sidecar-Plan
    //  geladen wurde, und validiert einen geladenen gegen die echte Seitenzahl.
    Q_INVOKABLE void setSourcePageCount(int n);
    //  STABILER Key der Ansichts-Seite (−1 = keine): Notizen (PdfEditBox::page)
    //  adressieren ihre Seite darüber, damit sie beim Umsortieren/Einfügen an
    //  ihrer Seite bleiben. Basis des Overlay-Repeaters je Seite.
    Q_INVOKABLE int  viewPageKey(int viewIndex) const;
    //  Quellseite einer Ansichts-Seite innerhalb IHRER Quelldatei: ≥0 = Index,
    //  −1 = eingefügte Leerseite.
    Q_INVOKABLE int  viewSourcePage(int viewIndex) const;
    //  Zusätzliche Drehung der Ansichts-Seite in Grad (0/90/180/270).
    Q_INVOKABLE int  pageRotation(int viewIndex) const;
    //  Beschreibung einer Ansichts-Seite für die Oberfläche:
    //  { exists, key, src, doc, rot, blank, imported, textEditable }.
    Q_INVOKABLE QVariantMap pageInfo(int viewIndex) const;
    //  Leere A4-Seite NACH viewIndex einfügen (viewIndex = −1 → an den Anfang).
    Q_INVOKABLE void addBlankPageAfter(int viewIndex);
    //  Ansichts-Seite entfernen (die letzte verbleibende Seite bleibt bestehen).
    Q_INVOKABLE void removePage(int viewIndex);
    //  Seite umsortieren: die Seite an `from` landet an Position `to` (beide
    //  Ansichts-Indizes, `to` bezeichnet die Position NACH dem Herausnehmen).
    //  Notizen folgen ihrer Seite (Key-Adressierung), undo-fähig.
    Q_INVOKABLE void movePage(int from, int to);
    //  Seite drehen: `deltaDeg` (±90/180) ZUSÄTZLICH zur bisherigen Drehung.
    //  Die Notizen der Seite werden mitgedreht — Seitenmaße in PDF-Punkten
    //  liefert QML mit (wie bei addTextBox), weil nur die Anzeige sie kennt.
    //  Plan-Änderung + Notiz-Geometrie bilden EINEN Undo-Schritt.
    Q_INVOKABLE void rotatePage(int viewIndex, int deltaDeg,
                                qreal pageWPt, qreal pageHPt);
    //  Seiten aus einer FREMDEN PDF nach `afterViewIndex` einfügen (−1 = an den
    //  Anfang). `pages` = 0-basierte Seitenindizes der Quelle (leer = alle).
    //  Die Seiten werden VERLUSTFREI (Objektebene, PdfAssembler) in die
    //  Begleitdatei „<pdf>.mgpages.pdf" übernommen — der Plan verweist danach
    //  nur noch dorthin, die fremde Datei wird nicht mehr gebraucht und darf
    //  verschwinden. Ergebnis über `pagesInserted`.
    Q_INVOKABLE void insertPagesFrom(const QString& pathOrUrl,
                                     const QVariantList& pages,
                                     int afterViewIndex);
    //  Seitenzahl einer PDF ohne sie zu rendern (−1 = nicht lesbar) — die
    //  Oberfläche braucht sie, um den Auswahldialog zu füllen.
    Q_INVOKABLE int  probePageCount(const QString& pathOrUrl) const;
    //  Renderquelle für die Anzeige: nicht-destruktiv = Originalpfad; destruktiv
    //  = pristine Sicherung (.mgorig), damit die Anzeige stets plan-getrieben
    //  über die UNVERÄNDERTEN Seiten läuft (die .pdf auf Platte trägt derweil
    //  die gebackene Struktur).
    Q_INVOKABLE QString renderSourcePath() const;
    //  Intern (Kommando redo/undo) — NICHT aus QML rufen.
    void applyPlan(const QVector<PdfPlanPage>& plan);

    // ── Copy / Paste (kachel-lokale Zwischenablage; INKL. Text) ───────────────
    //  copySelected sichert die ausgewählte Notiz; paste fügt eine versetzte
    //  Kopie auf DERSELBEN Seite ein (neuer Undo-Schritt) und wählt sie aus.
    Q_INVOKABLE void copySelected();
    Q_INVOKABLE void paste();

    // ── Geometrie-Session (Ziehen/Skalieren → EIN Undo-Schritt) ───────────────
    Q_INVOKABLE void beginGeometryEdit(int id);
    Q_INVOKABLE void updateGeometry(int id, qreal xPt, qreal yPt, qreal wPt, qreal hPt);
    //  updatePlacement: wie updateGeometry, zusätzlich mit ZIELSEITE — Basis des
    //  seitenübergreifenden Verschiebens. y darf während der Session negativ
    //  bzw. größer als die Seite sein (Zwischenzustand über dem Seitenrand);
    //  die finale Klemmung übernimmt die QML-Auflösung beim Loslassen.
    Q_INVOKABLE void updatePlacement(int id, int page, qreal xPt, qreal yPt,
                                     qreal wPt, qreal hPt);
    Q_INVOKABLE void endGeometryEdit(int id);

    // ── Text-Session (Tippen → EIN Undo-Schritt) ──────────────────────────────
    Q_INVOKABLE void beginTextEdit(int id);
    Q_INVOKABLE void updateText(int id, const QString& text);
    Q_INVOKABLE void endTextEdit(int id);

    // ── Reflow: verkettete Textboxen ─────────────────────────────────────────
    //  linkChain(from,to): verkettet die Textbox `from` mit der Folgebox `to`
    //  (Text fließt from → to). Beide müssen textführend (Text/„Text ersetzen")
    //  sein; Selbstbezug/Zyklen werden abgewiesen. Danach reflowt die Kette.
    //  unlinkChain(from): trennt die Kette hinter `from`. Beides undo-fähig.
    Q_INVOKABLE void linkChain(int fromId, int toId);
    Q_INVOKABLE void unlinkChain(int fromId);

    // ── Caret: direktes Bearbeiten der eingebetteten Textebene ────────────────
    //  MODELL: Die Änderungen sind eine geordnete Liste von PdfTextOp — ein
    //  DELTA im Sidecar, KEINE Änderung am Original. Angezeigt und exportiert
    //  wird die Arbeitsdatei „<pdf>.mgtext.pdf" = pristine + Wiedergabe aller
    //  Ops (asynchron erzeugt). Undo entfernt schlicht die letzte Op.
    //  Fortlaufendes Tippen sammelt sich in EINER schwebenden Op (Session-
    //  Muster wie beginTextEdit/updateText/endTextEdit) → EIN Undo-Schritt.

    //  Baut (asynchron) das Zeichen-Layout der Seite. Idempotent; ein Wechsel
    //  der Seite verwirft das vorherige Layout (RAM: höchstens EINE Seite).
    Q_INVOKABLE void prepareCaretPage(int page);
    //  Setzt das Caret an die Zeichenposition unter (xPt|yPt) der Seite.
    //  Beendet eine laufende Tipp-Session (eigener Undo-Schritt).
    Q_INVOKABLE void placeCaret(int page, qreal xPt, qreal yPt);
    //  Caret zeichenweise bewegen (delta = ±1) bzw. an den Zeilenanfang/das
    //  Zeilenende (caretHome/caretEnd) oder eine Zeile hoch/runter (±1).
    Q_INVOKABLE void moveCaret(int delta);
    Q_INVOKABLE void moveCaretLine(int delta);
    Q_INVOKABLE void caretHome();
    Q_INVOKABLE void caretEnd();
    //  Caret aufheben (Werkzeugwechsel/Fokusverlust) — schließt die Session ab.
    Q_INVOKABLE void clearCaret();
    //  Zeichen an der Caret-Position einfügen bzw. löschen
    //  (dir = −1 Rücktaste, +1 Entf).
    Q_INVOKABLE void insertAtCaret(const QString& text);
    Q_INVOKABLE void deleteAtCaret(int dir);
    //  Zeichen der Seite (für QML-Diagnose/Tests): Anzahl der Glyphen.
    Q_INVOKABLE int  caretGlyphCount() const { return m_caretGlyphs.size(); }

    // ── Stil/Format (Einzel-Kommandos, mergefähig) ────────────────────────────
    //  id < 0 → setzt NUR die Vorlage/Default für neue Annotationen (kein
    //  Kommando); id >= 0 → ändert die Annotation UND aktualisiert die Vorlage.
    Q_INVOKABLE void setBoxStroke(int id, const QColor& c);
    Q_INVOKABLE void setBoxLineWidth(int id, qreal wPt);
    Q_INVOKABLE void setBoxFill(int id, const QColor& c);
    Q_INVOKABLE void setBoxFont(int id, const QString& family);
    Q_INVOKABLE void setBoxFontSize(int id, qreal sizePt);
    Q_INVOKABLE void setBoxBold(int id, bool v);
    Q_INVOKABLE void setBoxItalic(int id, bool v);
    Q_INVOKABLE void setBoxUnderline(int id, bool v);
    Q_INVOKABLE void setBoxColor(int id, const QColor& c);
    Q_INVOKABLE void setBoxHighlight(int id, const QColor& c);
    Q_INVOKABLE void setBoxAlignment(int id, int align);
    //  Vertikale Textausrichtung: 0 = oben (Word-Textfeld, Standard), 1 = mittig.
    Q_INVOKABLE void setBoxVAlign(int id, int vAlign);

    // Eigenschaften einer Box für Toolbar/Panel: { exists, page, kind, isText,
    // isReplace, isStroke, isShape, xPt, yPt, wPt, hPt, strokeColor, lineWidth,
    // fillColor, hasFill, text, fontFamily, fontSizePt, bold, italic, underline,
    // textColor, highlightColor, hasHighlight, alignment, vAlign, anchored }.
    Q_INVOKABLE QVariantMap boxInfo(int id) const;
    // Aktuelle Vorlagen-Defaults (wenn nichts ausgewählt ist) — rev-getrieben
    // über defaultRev, wie annInfo/defaultInfo des Bild-Editors.
    Q_INVOKABLE QVariantMap defaultInfo() const;

    // ── Undo/Redo (schließen offene Sessions deterministisch ab) ──────────────
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // ── Persistenz & Export ───────────────────────────────────────────────────
    //  saveOverlay: Sidecar atomar schreiben (leeres Overlay entfernt es).
    Q_INVOKABLE bool saveOverlay();
    //  exportPdf: rendert asynchron Original + Overlay in das Ziel aus
    //  exportTargetPath(). Ergebnis via exportFinished(ok, pfad, fehler).
    Q_INVOKABLE void exportPdf();
    //  exportContentEdited: VERLUSTFREIES Content-Stream-Editing für „Text
    //  ersetzen"-Boxen — schreibt den Ersatztext DIREKT in die eingebettete
    //  Textebene (Datei bleibt vektoriell), statt sie zu überdecken. Nur wenn
    //  ALLE Annotationen stream-editierbare Replace-Boxen sind, der Seiten-Plan
    //  die Identität ist und die Datei nicht zu groß ist; sonst ODER bei einer
    //  nicht sicher editierbaren Seite/Font fällt es automatisch auf den (immer
    //  korrekten) Raster-Export zurück (Signal `contentEditFellBack`). Ergebnis
    //  wie exportPdf über `exportFinished`.
    Q_INVOKABLE void exportContentEdited();
    //  Ziel des nächsten Exports (Anzeige): IMMER ein eindeutiger Kopie-Pfad
    //  „…_bearbeitet(.n).pdf" — das Original wird nie ersetzt (die Bearbeitung
    //  bleibt über das Sidecar dauerhaft reversibel).
    Q_INVOKABLE QString exportTargetPath() const;
    //  Bilder NEBEN dem PDF — dieselbe Abkürzung wie im DOCX-Editor: einen
    //  Stempel aus dem Arbeitsordner wählen, ohne durch den Dateidialog zu
    //  navigieren. `[{name,url}]`, PDFs bleiben draußen (ein Stempel ist ein
    //  Bild). Die Abfrage teilen sich beide Editoren (`core/FolderImages`).
    Q_INVOKABLE QVariantList folderImages() const;

    // ── Formularfelder ────────────────────────────────────────────────────────
    //  setFormValue: neuen Wert eines Feldes PUFFERN (kein Schreiben auf Platte).
    //  `name` ist der vollständige Feldname (s. mg::PdfFormField::name), `value`
    //  der Text bzw. — bei Ankreuz-/Optionsfeldern — der Zustandsname des
    //  gewünschten Knopfes („Off" wählt ab). Schreibgeschützte und unbekannte
    //  Felder werden ignoriert. Der Wert landet mit `saveOverlay()` im Sidecar
    //  (überlebt das Schließen) und mit `saveFormValues()` in einer PDF.
    //
    //  BEWUSST NICHT über PdfEditCommands/QUndoStack: der Stapel gehört den
    //  eigenen Notizen (Sidecar-Overlay). Formularwerte gehören dem DOKUMENT —
    //  ein gemeinsamer Undo-Stapel würde beide Ebenen vermischen; das
    //  „Zurücksetzen" eines Feldes ist schlicht das Wiedereintragen des
    //  Originalwertes (setFormValue mit dem Ursprungswert leert den Puffer).
    Q_INVOKABLE void setFormValue(const QString& name, const QString& value);
    //  Aktueller Wert eines Feldes (gepuffert, sonst der Wert aus der Datei) —
    //  rev-getrieben über formValueRev zu lesen.
    Q_INVOKABLE QString formValue(const QString& name) const;
    //  saveFormValues: schreibt die gepufferten Werte asynchron in eine KOPIE
    //  „…_ausgefuellt(.n).pdf" neben dem Original (inkrementelles Update, das
    //  Original bleibt byteweise unangetastet — dieselbe Zusage wie beim
    //  Export). Ergebnis über `formSaved`.
    Q_INVOKABLE void saveFormValues();
    //  Ziel des nächsten Formular-Speicherns (Anzeige/Toast).
    Q_INVOKABLE QString formTargetPath() const;

    // ── Schriften ─────────────────────────────────────────────────────────────
    //  Die fünf Standard-Schriften des Editors (feste Liste; fehlt eine Familie
    //  im System, substituiert Qt beim Rendern automatisch die nächstliegende).
    Q_INVOKABLE QStringList standardFonts() const;
    //  Erkennung: Familie, die das System für `family` tatsächlich auflöst.
    Q_INVOKABLE QString resolvedFont(const QString& family) const;

    // ── Intern (Worker → GUI via QueuedConnection; NICHT aus QML rufen) ───────
    void exportTaskFinished(bool ok, const QString& target,
                            const QString& error, int generation);
    void exportTaskProgress(int done, int total, int generation);
    //  Ergebnis des Content-Stream-Editing-Workers: bei Erfolg fertig, sonst
    //  wird intern der Raster-Export gestartet (gleiche Generation).
    void contentEditTaskFinished(bool ok, const QString& target, int generation);
    //  Eine Schwärzung ließ sich NICHT aus dem Stream entfernen → der Export
    //  ist auf den Rasterweg gegangen (dort ist der Text ebenfalls weg, die
    //  Seite aber ein Bild). Wird als Hinweis gemeldet, nie verschwiegen.
    void redactionFellBack(int generation);
    //  Fertiges Zeichen-Layout einer Seite übernehmen (Caret-Werkzeug).
    void caretLayoutFinished(int page, const QVector<mg::PdfGlyph>& glyphs,
                             const QString& err, int generation);
    //  Ergebnis der Textebenen-Wiedergabe (Arbeitsdatei geschrieben).
    //  `caretTo` = neue Position der Schreibmarke, falls der anschließende
    //  Absatz-Umbruch Zeichen verschoben hat (−1 = kein Umbruch);
    //  `overflow` = der Absatz ist voll, die letzte Zeile trägt den Rest.
    void textOpsTaskFinished(bool ok, const QString& err, int generation,
                             int caretTo = -1, int caretPage = -1,
                             bool overflow = false);
    //  Gelesene Dokument-Annotationen übernehmen (Worker → GUI).
    void annotReadFinished(const QVector<mg::PdfAnnotation>& annots, int generation);
    //  Gelesene Formularfelder übernehmen (Worker → GUI).
    void formReadFinished(const QVector<mg::PdfFormField>& fields, int generation);
    //  Ergebnis des Formular-Schreibens (Worker → GUI).
    void formSaveFinished(bool ok, const QString& target, const QString& error,
                          int generation, bool applyPlan = false);
    //  Intern (Undo-Kommando) — NICHT aus QML rufen.
    void applyTextOp(const PdfTextOp& op);
    void revokeLastTextOp();

signals:
    void editModeChanged();
    void toolChanged();
    void defaultRevChanged();
    void undoStateChanged();
    void recordingChanged();
    void trackedChanged();
    void dirtyChanged();
    void selectedIdChanged();
    void selectionRevChanged();
    //  Content-Stream-Editing war nicht (vollständig) möglich → Raster-Export
    //  wird genutzt (QML zeigt einen Hinweis-Toast).
    void contentEditFellBack();
    void busyChanged();
    void textEditingChanged();
    void clipboardChanged();
    void panelOnTopChanged();
    void pageEditDestructiveChanged();
    void exportLosslessChanged();
    void exportAsAnnotationsChanged();
    void planChanged();
    //  Ergebnis von insertPagesFrom: Anzahl übernommener Seiten (0 = nichts),
    //  `errorText` leer bei Erfolg (QML zeigt einen Toast).
    void pagesInserted(int count, const QString& errorText);
    // Destruktiver Modus: die .pdf auf Platte wurde neu geschrieben → QML soll
    // ihr PdfDocument neu laden (bzw. renderSourcePath() neu binden).
    void documentRewritten();
    //  Die SEITENSTRUKTUR hat sich geändert (umsortiert/gedreht/eingefügt/
    //  entfernt). Getrennt von documentRewritten, weil daran das Neurendern der
    //  Vorschauleiste hängt: Ein Neubau der Textebene ändert die Struktur NICHT
    //  und darf die Vorschauen deshalb nicht bei jedem Tippen verwerfen.
    void pageStructureChanged();
    void boxCountChanged();
    void caretChanged();
    void caretReadyChanged();
    void textOpsChanged();
    void textOpsBusyChanged();
    //  Eine Änderung an der Textebene ließ sich nicht schreiben (z. B. Zeichen
    //  in der Kodierung der Schrift nicht darstellbar) → sie wurde verworfen.
    void textEditFailed(const QString& reason);
    //  Der Absatz war beim Umbruch voll — der Rest steht in der letzten Zeile
    //  (QML zeigt einen Hinweis-Toast).
    void reflowOverflow();
    void exportFinished(bool ok, const QString& targetPath, const QString& errorText);
    void exportProgress(int done, int total);
    void overlaySaved(bool ok);
    //  Formularfelder gelesen/verändert bzw. Puffer-Zustand geändert.
    void formFieldsChanged();
    void formDirtyChanged();
    void formValueRevChanged();
    //  Ergebnis von saveFormValues (QML zeigt einen Toast).
    //  `flattened` = die Kopie trägt die geänderte Seitenfolge, ist dafür aber
    //  KEIN bedienbares Formular mehr (beim Umsortieren geht `/AcroForm`
    //  verloren; die Werte bleiben als Erscheinungsbild sichtbar). Gemessen,
    //  nicht vermutet — und deshalb gesagt statt verschwiegen.
    void formSaved(bool ok, const QString& targetPath, const QString& errorText,
                   bool flattened = false);

private:
    void pushCommand(QUndoCommand* cmd);
    void bumpSelectionRev();
    void finishGeometrySession();
    void finishTextSession();
    void finishDrawSession();
    void finishOpenSessions() { finishGeometrySession(); finishTextSession(); }
    void setBoxField(int id, PdfEditField f, const QVariant& v);
    //  Neue Notiz erbt den zuletzt benutzten Stil (Schrift/Farben/Deckkraft/
    //  Ausrichtung) — aber OHNE Text.
    PdfEditBox seededBox() const;
    //  Neue Zeichnung erbt die zuletzt benutzten Zeichen-Defaults
    //  (Linienfarbe/-breite/Füllung).
    PdfEditBox seededDraw(PdfAnnKind kind) const;
    //  Neue „Text ersetzen"-Box erbt die zuletzt benutzten Replace-Stilwerte
    //  (eigene Vorlage, getrennt von den Post-its) — aber OHNE Text.
    PdfEditBox seededReplace() const;
    //  Startwerte der „Text ersetzen"-Vorlage: schwarzer Text auf fix weißer,
    //  deckender Fläche (keine Post-it-Optik), oben-links ausgerichtet.
    static PdfEditBox makeReplaceTpl();
    //  Benötigte Boxhöhe (PDF-Punkte) für den Text einer Box bei fester Breite
    //  — dieselbe QTextLayout-Mathematik wie der Export (WYSIWYG-Basis des
    //  automatischen Höhenwachstums der „Text ersetzen"-Boxen).
    static qreal requiredHeightPt(const PdfEditBox& b);
    //  ── Reflow-Helfer ────────────────────────────────────────────────────────
    //  Wie viele führende Zeichen von `text` in die Höhe von `box` passen (feste
    //  Breite, Wortumbruch — dieselbe QTextLayout-Mathematik wie requiredHeightPt).
    //  Die erste Zeile zählt immer (auch wenn zu hoch), damit kein Nullfortschritt.
    static int   fitCharCount(const PdfEditBox& box, const QString& text);
    //  Kopf der Kette, in der `id` liegt (folgt chainNext rückwärts).
    int  chainHead(int id) const;
    //  Geordnete Box-IDs der Kette ab `headId` (folgt chainNext, zyklensicher).
    QVector<int> chainOrder(int headId) const;
    bool isChainMember(int id) const;   // Teil einer Kette (Vorgänger ODER chainNext)?
    //  Text der Kette von `id` neu über die Boxen verteilen (Überlauf → nächste
    //  Box; letzte Box wächst mit dem Rest) — als EIN Undo-Schritt. `editedId`/
    //  `editedOldText`/`editedOldRect` liefern den Session-Start-Zustand der
    //  gerade editierten Box (für ein korrektes Delta), sonst -1/leer.
    void reflowChain(int anyId, int editedId, const QString& editedOldText,
                     const QRectF& editedOldRect);
    //  kind steuert, welche Vorlage nachgezogen wird: Text → m_textTpl,
    //  Replace → m_replaceTpl (ohne Highlight — Deckfläche bleibt fix Weiß),
    //  sonst Zeichen-Defaults (Stroke/LineWidth/Fill).
    void mirrorToTemplate(PdfEditField f, const QVariant& v, PdfAnnKind kind);
    bool loadOverlay(const QString& pdfPath);
    static QString sidecarPath(const QString& pdfPath);
    static QString uniqueCopyPath(const QString& pdfPath);
    //  Freier Kopie-Pfad „<name><suffix>(.n).pdf" neben dem Original.
    static QString uniqueSuffixPath(const QString& pdfPath, const QString& suffix);

    // ── Formularfelder ────────────────────────────────────────────────────────
    //  Felder der PRISTINEN Datei asynchron lesen (Dokumentwechsel). Ein PDF
    //  ohne Formular ist kein Fehler — die Liste bleibt dann leer.
    void startFormRead();
    //  ── Übernommene Annotationen (Interchange) ────────────────────────────────
    //  Annotationen der PRISTINEN Datei asynchron lesen (Dokumentwechsel).
    void startAnnotRead();
    //  Eine gelesene Annotation auf eine Overlay-Box abbilden (kind-Zuordnung
    //  s. .cpp). Liefert false, wenn die Art im Editor keine Entsprechung hat.
    static bool boxFromAnnotation(const mg::PdfAnnotation& a, PdfEditBox* out);
    //  Umkehrung: eine eigene Notiz als PDF-Annotation. Liefert false für
    //  Arten, die sich NICHT verlustfrei abbilden lassen („Text ersetzen"
    //  deckt Inhalt ab, verkettete Textboxen bilden gemeinsam einen Fluss) —
    //  dann bleibt es beim gemalten Weg.
    static bool annotationFromBox(const PdfEditBox& b, mg::PdfAnnotation* out);
    //  Weicht die Box vom Zustand ab, in dem sie aus der Datei kam?
    bool importChanged(const PdfEditBox& b) const;
    //  Originalwert eines Feldes (erstes Widget dieses Namens); `found` meldet,
    //  ob es das Feld überhaupt gibt und ob es beschreibbar ist.
    QString formOriginalValue(const QString& name, bool* found, bool* readOnly) const;
    void setFormDirty(bool v);

    // ── Caret / Textebene (Werkzeug „Text bearbeiten") ────────────────────────
    //  Arbeitsdatei mit ANGEWENDETEN Textebenen-Ops (pristine + Wiedergabe).
    static QString textWorkPath(const QString& pdfPath);
    //  Quelle FÜR den Seiten-Plan/Export: die Textebenen-Arbeitsdatei, sofern
    //  Ops vorliegen und sie geschrieben ist — sonst die pristine Datei.
    QString textSourcePath() const;
    //  Effektive Op-Liste (committete + schwebende Tipp-Session).
    QVector<PdfTextOp> effectiveTextOps() const;
    //  Weicht die effektive Liste von der zuletzt geschriebenen ab?
    bool textRebuildNeeded() const;
    //  Wiedergabe anstoßen (entprellt) bzw. sofort, wenn nichts mehr getippt
    //  wird. Ist die Liste identisch zur zuletzt gebauten, passiert nichts.
    void scheduleTextRebuild();
    void startTextRebuild();
    void afterTextRebuild();
    //  Caret-/Textebenen-Zustand vollständig zurücksetzen (Dokumentwechsel).
    void resetTextState();
    //  Schwebende Tipp-Session als EIN Undo-Kommando festschreiben.
    void commitPendingTextOp();
    //  Export erst nach dem Materialisieren der Textebene starten. Liefert
    //  true = sofort loslegen, false = wird nach dem Neubau fortgesetzt.
    bool flushTextForExport(int kind);
    void resumePendingExport();
    //  Layout der Caret-Seite (neu) anfordern.
    void requestCaretLayout(int page);
    void setCaretIndex(int idx);
    //  Schwebende Tipp-Session setzen/löschen (hält canUndo/dirty aktuell).
    void setPendingValid(bool v);
    //  Glyphen-Index für einen Klickpunkt (Zeichenhälfte entscheidet).
    int  hitIndexAt(const QPointF& ptPt) const;
    //  Stehen beide Glyphen auf derselben Grundlinie?
    static bool sameLine(const mg::PdfGlyph& a, const mg::PdfGlyph& b);
    //  Näherung des Layouts zwischen zwei Neubauten fortschreiben.
    void spliceGlyphsInsert(int index, const QString& text);
    void spliceGlyphsRemove(int index, int count);

    ISettings&   m_settings;
    //  Nachverfolgung läuft (aus dem Sidecar geladen).
    bool         m_recording = false;
    //  Setzt die Marke „neu" auf eine gerade entstehende Box und legt sie ab.
    void         pushAdd(PdfEditBox& b);
    //  Zustand einer Box als Undo-Schritt setzen (nutzt PdfEditField::Track).
    void         setTrack(int id, PdfTrackState st);

    PdfEditModel m_model;
    QUndoStack   m_stack;

    QString m_docPath;                  // lokaler Pfad des aktiven PDFs

    // Seiten-Plan: Ansichts-Reihenfolge (s. PdfPlanPage). Leer, solange keine
    // Seitenzahl bekannt ist.
    QVector<PdfPlanPage> m_plan;
    int          m_srcPageCount = 0;
    // Nächster freier Seiten-Key für NEUE Seiten (leer/importiert). Keys der
    // pristinen Seiten sind per Definition 0…m_srcPageCount−1.
    int          m_nextPageKey = 0;
    bool planIsIdentity() const;        // Plan == pristine Seiten, unverändert?
    //  Notizen für die Ausgabe: `page` von Seiten-KEY auf ANSICHTS-Index
    //  abgebildet (Notizen entfernter Seiten fallen weg).
    QVector<PdfEditBox> exportBoxes() const;
    //  Objektnummern der Annotationen, die aus der Ausgabe VERSCHWINDEN müssen:
    //  im Editor gelöschte oder veränderte Übernahmen (die geänderte Fassung
    //  wird stattdessen gezeichnet).
    QVector<int> importRemovals() const;
    //  Eigene Notizen als Annotationsliste (leer = malen, s. .cpp).
    QVector<mg::PdfAnnotation> exportAnnotations() const;
    //  Die Textstellen, die eine Schwärzung aus dem Dokument entfernen MUSS.
    //  Sie sind kein „nice to have" des verlustfreien Weges: Ohne sie wäre die
    //  Schwärzung nur ein schwarzer Balken über weiterhin lesbarem Text.
    QVector<mg::PdfTextEdit> redactionEdits() const;
    //  Dieselben Schwärzungen als FLÄCHEN (Ansichts-Seite + Rechteck in
    //  PDF-Punkten). Der geometrische Weg braucht KEINEN erkannten Originaltext
    //  — deshalb steht hier jede Schwärzungsbox, auch die ohne `origText`.
    QVector<mg::PdfRedactArea> redactionAreas() const;
    void bakeWorking();                 // Arbeitsdatei aus (pristine + Plan) schreiben
    //  Den Seiten-Plan aus `sourceOverride` (leer = die Plan-Quellen selbst) in
    //  `targetPath` zusammenbauen. Kern von `bakeWorking`, aber ohne dessen
    //  Zielwahl/Signale — auch das ausgefüllte Formular geht diesen Weg, damit
    //  seine Kopie die ANGEZEIGTE Seitenfolge trägt.
    bool assemblePlanTo(const QString& targetPath, const QString& sourceOverride,
                        QString* err);
    QString pristinePath() const;       // Quelle mit den UNVERÄNDERTEN Seiten
    //  Datei einer Plan-Quelle (doc): 0 = pristine (bzw. Textebenen-Arbeitsdatei,
    //  falls `preferTextWork`), 1 = Begleitdatei der importierten Seiten.
    QString planSourceFile(int doc, bool preferTextWork) const;
    //  Key ⇄ Ansichts-Index / Quellseite (−1 = unbekannt).
    int  keyOfView(int viewIndex) const;
    int  srcOfView(int viewIndex) const;
    int  viewOfKey(int key) const;
    //  Wie keyOfView, aber tolerant, solange noch kein Plan steht (dann ist die
    //  Ansichts-Seite selbst der Key) — Eingangsübersetzung der Notiz-API.
    int  pageKeyForView(int viewIndex) const;
    //  Quellseite, auf die sich die Textebenen-Ops des Carets beziehen.
    int  caretSrcPage() const;
    //  Ist die Ansichts-Seite zeichenweise bearbeitbar (pristine + ungedreht)?
    bool pageTextEditable(int viewIndex) const;
    //  Frisch geladenen/erzeugten Plan mit Keys versehen (pristine Seite →
    //  key = src, neue Seiten → laufende Keys) und m_nextPageKey nachziehen.
    void assignPlanKeys();
    static QString backupPath(const QString& pdfPath);   // <pdf>.mgorig (destruktiv)
    static QString previewPath(const QString& pdfPath);  // <pdf>.mgpreview.pdf (nicht-destr.)
    static QString assetPath(const QString& pdfPath);    // <pdf>.mgpages.pdf (Importe)
    bool    m_editMode     = false;
    int     m_tool         = Select;
    int     m_selectedId   = -1;
    int     m_selectionRev = 0;
    int     m_defaultRev   = 0;
    int     m_nextId       = 1;

    // Offene Sessions (genau eine je Art; -1 = keine).
    int     m_geoEditId  = -1;
    int     m_geoOldPage = 0;
    QRectF  m_geoOld;
    QVector<QPointF> m_geoOldPts;       // Striche: Punkte der Session-Basis
    int     m_textEditId = -1;
    QString m_textOld;
    // Rechteck zu Session-Beginn: das automatische Höhenwachstum der „Text
    // ersetzen"-Boxen wird am Session-Ende mit dem Text-Delta zu EINEM
    // Undo-Schritt (Makro) zusammengefasst.
    QRectF  m_textOldRect;
    int     m_drawId     = -1;          // laufende Zeichen-Session
    int     m_drawPage   = 0;           // Seite der Zeichen-Session
    QPointF m_drawStart;                // Startpunkt (Rechteck/Ellipse/Pfeil)

    // Kachel-lokale Zwischenablage (Copy/Paste, inkl. Text) + Stil-Vorlagen
    // für neue Annotationen (Stil-Erben; Zeichen-Defaults separat).
    PdfEditBox m_clip;
    bool       m_hasClip = false;
    PdfEditBox m_textTpl;
    // Eigene Stil-Vorlage der „Text ersetzen"-Boxen (schwarz auf fix Weiß) —
    // getrennt von den Post-its, damit sich die beiden Werkzeuge ihre
    // zuletzt benutzten Stile nicht gegenseitig überschreiben.
    PdfEditBox m_replaceTpl = makeReplaceTpl();
    //  Vorlage der Textmarkierung: zuletzt gewählter Stil + Farbe je Stil
    //  (Markieren durchscheinend gelb, Unterstreichen rot, Durchstreichen
    //  schwarz — die Erwartung aus jedem PDF-Betrachter).
    int    m_markupStyle = 0;
    QColor m_markupColors[3] = { QColor(255, 235, 0, 140), QColor(200, 0, 0),
                                 QColor(20, 20, 20) };
    QColor     m_defStroke    = QColor(230, 44, 44);
    qreal      m_defLineWidth = 2.0;    // PDF-Punkte
    QColor     m_defFill      = QColor(0, 0, 0, 0);

    // ── Caret / Textebene ─────────────────────────────────────────────────────
    //  Zeichen-Layout GENAU EINER Seite (RAM: eine Seite ≈ wenige tausend
    //  Glyphen à 48 Byte). Wird bei Seitenwechsel ersetzt.
    QVector<mg::PdfGlyph> m_caretGlyphs;
    int     m_caretPage    = -1;
    int     m_caretIndex   = -1;
    bool    m_caretReady   = false;
    QString m_caretError;
    int     m_caretGen     = 0;         // verwirft veraltete Layout-Läufe
    QPointF m_caretHitPt;               // Klick, der auf das Layout wartet
    bool    m_caretHitPending = false;
    //  Committete Ops (Sidecar-persistiert) + die schwebende Tipp-Session.
    QVector<PdfTextOp> m_textOps;
    PdfTextOp          m_pending;
    bool               m_pendingValid  = false;
    bool               m_pendingCommit = false;  // wartet auf Bestätigung
    //  Zuletzt in die Arbeitsdatei geschriebene Liste — verhindert Neubauten,
    //  die nichts ändern (z. B. Festschreiben der schwebenden Op).
    QVector<PdfTextOp> m_builtOps;
    QVector<PdfTextOp> m_buildingOps;   // Liste des laufenden Neubaus
    bool               m_textWorkValid = false;
    bool               m_textOpsLoaded = false;  // Ops stammen aus dem Sidecar
    bool               m_textOpsBusy   = false;
    int                m_textOpsGen    = 0;
    int                m_exportPending = 0;      // 0 kein, 1 Raster, 2 verlustfrei
    QTimer             m_textFlush;     // Entprellung des Neubaus

    // ── Formularfelder ────────────────────────────────────────────────────────
    //  Gelesene Widgets der pristinen Datei (RAM: je Feld ~200 Byte, ein
    //  Formular hat Dutzende — kein Deckel nötig). `page` ist der QUELLseiten-
    //  index; die Abbildung auf die Ansichts-Seite macht formFields() über den
    //  Seiten-Key, damit umsortierte/entfernte Seiten stimmen.
    //  Zustand JEDER übernommenen Annotation, wie sie aus der Datei kam
    //  (Objektnummer → `PdfEditBox::toJson`). Grundlage der Änderungserkennung:
    //  Eine unveränderte Übernahme darf beim Export NICHT noch einmal
    //  gezeichnet werden (sie steht schon in der Datei), eine geänderte oder
    //  gelöschte verlangt, dass das ORIGINAL aus `/Annots` gestrichen wird.
    QHash<int, QJsonObject> m_importBaseline;
    int  m_annotReadGen = 0;            // verwirft veraltete Leseläufe

    QVector<mg::PdfFormField> m_formFields;
    //  Gepufferte Werte (Feldname → neuer Wert). Nur ABWEICHUNGEN vom Original;
    //  ein zurückgesetztes Feld verschwindet wieder aus dem Puffer.
    QHash<QString, QString>   m_formEdits;
    bool m_formDirty    = false;
    int  m_formValueRev = 0;            // Wertänderungs-Zähler (s. Q_PROPERTY)
    //  Ziel des Formular-Speicherns, solange noch der Seiten-Plan darauf
    //  angewandt werden muss (leer = kein Zwischenschritt nötig).
    QString m_formPlanTarget;
    int  m_formReadGen = 0;             // verwirft veraltete Leseläufe
    int  m_formSaveGen = 0;             // verwirft veraltete Schreibläufe

    // Export (1 Worker → RAM-Peak gedeckelt; Generationszahl verwirft Veraltetes).
    QThreadPool m_pool;
    bool        m_busy      = false;
    int         m_exportGen = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
