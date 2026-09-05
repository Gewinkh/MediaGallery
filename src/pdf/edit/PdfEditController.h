#pragma once
// PDF-Editor: Bearbeitungsmodus, Overlay-Modell, Auswahl, Undo/Redo, Sidecar
// und Export. Je geoeffneter PDF-Kachel eine Instanz; der Singleton PdfEdit
// traegt nur die globalen Einstellungen.

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
    // Werte 0-5 sind identisch zu ImageEditController::Tool; 6 ist PDF-exklusiv.
    enum Tool { Select = 0, TextTool, FreehandTool, ArrowTool, RectTool, EllipseTool,
                ReplaceTool, CaretTool, MarkupTool, RedactTool };
    Q_ENUM(Tool)

private:
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged)
    Q_PROPERTY(int  trackedCount READ trackedCount NOTIFY trackedChanged)
    Q_PROPERTY(int  tool READ tool WRITE setTool NOTIFY toolChanged)
    // Rev-getrieben: Panel liest defaultInfo() neu, wenn nichts ausgewaehlt ist.
    Q_PROPERTY(int  defaultRev READ defaultRev NOTIFY defaultRevChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(int selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged)
    // Steigt bei Auswahl- UND Datenaenderung; Toolbar liest boxInfo() rev-getrieben.
    Q_PROPERTY(int selectionRev READ selectionRev NOTIFY selectionRevChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // QML sperrt damit Entf - beim Tippen gehoert die Taste dem TextEdit.
    Q_PROPERTY(bool textEditing READ textEditing NOTIFY textEditingChanged)
    Q_PROPERTY(bool hasClipboard READ hasClipboard NOTIFY clipboardChanged)
    Q_PROPERTY(bool panelOnTop READ panelOnTop WRITE setPanelOnTop NOTIFY panelOnTopChanged)

    // Destruktiv = Original sofort neu schreiben, sonst Plan im Sidecar.


    // true = verlustfrei bevorzugen, mit automatischem Rueckfall auf Raster.
    Q_PROPERTY(bool exportLossless READ exportLossless WRITE setExportLossless NOTIFY exportLosslessChanged)
    // Notizen als echte PDF-Annotationen statt gemalt.
    Q_PROPERTY(bool exportAsAnnotations READ exportAsAnnotations
               WRITE setExportAsAnnotations NOTIFY exportAsAnnotationsChanged)
    // Inklusive eingefuegter Leerseiten, ohne entfernte.
    Q_PROPERTY(int  viewPageCount READ viewPageCount NOTIFY planChanged)
    Q_PROPERTY(int  caretPage READ caretPage NOTIFY caretChanged)
    // Glyphen-Index VOR dem Caret.
    Q_PROPERTY(int  caretIndex READ caretIndex NOTIFY caretChanged)
    Q_PROPERTY(QRectF caretRectPt READ caretRectPt NOTIFY caretChanged)
    // Asynchron - solange false, ist die Seite nicht zeichenweise bearbeitbar.
    Q_PROPERTY(bool caretReady READ caretReady NOTIFY caretReadyChanged)
    Q_PROPERTY(QString caretError READ caretError NOTIFY caretReadyChanged)
    Q_PROPERTY(bool textOpsBusy READ textOpsBusy NOTIFY textOpsBusyChanged)
    Q_PROPERTY(int  textOpCount READ textOpCount NOTIFY textOpsChanged)

    // Qt PDF zeichnet Widget-Annotationen nicht (nur PDFium ueber FPDF_FFLDraw) -
    // dieses Overlay ist die einzige Darstellung der Felder.
    Q_PROPERTY(QVariantList formFields READ formFields NOTIFY formFieldsChanged)
    Q_PROPERTY(bool hasForm READ hasForm NOTIFY formFieldsChanged)
    Q_PROPERTY(bool formDirty READ formDirty NOTIFY formDirtyChanged)
    // formFields bleibt beim Tippen unveraendert, sonst erzeugt der Repeater seine
    // Delegates neu und der Fokus geht je Zeichen verloren; der Zaehler zieht
    // Optionsfelder derselben Gruppe trotzdem nach.
    Q_PROPERTY(int formValueRev READ formValueRev NOTIFY formValueRevChanged)

    Q_PROPERTY(QObject* boxModel READ boxModel CONSTANT)
    Q_PROPERTY(int boxCount READ boxCount NOTIFY boxCountChanged)
    // Einzige Quelle - Anzeige und Export rechnen mit denselben Werten (WYSIWYG).
    Q_PROPERTY(qreal boxPaddingPt READ boxPaddingPt CONSTANT)
    Q_PROPERTY(qreal minBoxWPt READ minBoxWPt CONSTANT)
    Q_PROPERTY(qreal minBoxHPt READ minBoxHPt CONSTANT)
    Q_PROPERTY(qreal noteFoldPt READ noteFoldPt CONSTANT)
    Q_PROPERTY(qreal noteShadowDxPt READ noteShadowDxPt CONSTANT)
    Q_PROPERTY(qreal noteShadowDyPt READ noteShadowDyPt CONSTANT)

public:
    static constexpr qreal kBoxPaddingPt   = 3.0;
    static constexpr qreal kMinBoxWPt      = 24.0;
    static constexpr qreal kMinBoxHPt      = 14.0;
    // 150 dpi: A4 ~ 1240x1754 px, transient genau ein Bild.
    static constexpr qreal kExportRenderDpi = 150.0;
    static constexpr int   kUndoLimit       = 200;
    static constexpr qint64 kMaxSidecarBytes = 8LL * 1024 * 1024;
    // Anzeige (QML) und Export (QPainter) zeichnen mit exakt diesen Werten.
    static constexpr qreal kNoteFoldPt     = 10.0;
    static constexpr qreal kNoteShadowDxPt = 2.0;
    static constexpr qreal kNoteShadowDyPt = 3.0;
    // Lang genug, dass fliessendes Tippen genau einen Neubau ausloest.
    static constexpr int   kTextFlushMs    = 350;
    // Kleiner als bei Textboxen - ein duenner Pfeil ist legitim.
    static constexpr qreal kMinDrawPt      = 4.0;

    // Fuer die QML-Instanziierung je PdfSurface; nutzt die zentrale AppSettings.
    explicit PdfEditController(QObject* parent = nullptr);
    explicit PdfEditController(ISettings& settings, QObject* parent = nullptr);
    ~PdfEditController() override;

    // Der Schalter lebt im Sidecar, nicht in den Einstellungen - er gehoert zum
    // Dokument, wie in einer Textverarbeitung.
    bool recording() const { return m_recording; }
    void setRecording(bool on);
    int  trackedCount() const;
    // Annehmen macht die Aenderung endgueltig, Verwerfen stellt den Stand davor her.
    // Die alle-Fassungen sind ein einziger Undo-Schritt.
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
    // Eine schwebende Tipp-Session ist kein Kommando, aber ruecknehmbar - sonst
    // waere der Undo-Knopf beim direkten Textbearbeiten grau.
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

    // Ungesicherter Stand des vorherigen Dokuments wird automatisch ins Sidecar
    // gesichert - nie stiller Verlust.
    Q_INVOKABLE void setDocument(const QString& pathOrUrl);
    Q_INVOKABLE void releaseDocument();

    Q_INVOKABLE int  addTextBox(int page, qreal xPt, qreal yPt,
                                qreal pageWPt, qreal pageHPt);
    // quads = Zeilenrechtecke in PDF-Punkten. Alle Bereiche bilden EIN Objekt
    // (wie /QuadPoints): eine Markierung ueber drei Zeilen ist eine Notiz.
    Q_INVOKABLE int  addMarkup(int page, int style, const QVariantList& quads);
    // Hoehe folgt dem Seitenverhaeltnis; ein unlesbares Bild liefert -1.
    Q_INVOKABLE int  addStamp(const QString& pathOrUrl, int page,
                              qreal xPt, qreal yPt, qreal wPt);
    Q_INVOKABLE int  markupStyle() const { return m_markupStyle; }
    Q_INVOKABLE void setMarkupStyle(int style);
    // An eine erkannte Textzeile gefangen; Schriftgroesse aus der Zeilenhoehe.
    Q_INVOKABLE int  addAnchoredTextBox(int page, qreal xPt, qreal yPt,
                                        qreal wPt, qreal hPt);
    // beginDraw legt die Annotation live an (kein Kommando), endDraw macht daraus
    // genau ein Add-Kommando.
    Q_INVOKABLE int  beginDraw(int kind, int page, qreal xPt, qreal yPt);
    Q_INVOKABLE void updateDraw(int id, qreal xPt, qreal yPt);
    Q_INVOKABLE void endDraw(int id);
    // Schliesst die Zeichen-Session ab; snapped=true laesst die Box auf die
    // Zeilen-Bounds schnappen, Schriftgroesse aus der mittleren Zeilenhoehe.
    Q_INVOKABLE int  endRedactDraw(int id, bool snapped,
                                   qreal xPt, qreal yPt, qreal wPt, qreal hPt,
                                   const QString& text);
    Q_INVOKABLE int  endReplaceDraw(int id, bool snapped,
                                    qreal xPt, qreal yPt, qreal wPt, qreal hPt,
                                    qreal lineHPt, const QString& text);
    Q_INVOKABLE void removeBox(int id);

    // Alle Q_INVOKABLE sprechen ANSICHTS-Indizes (0 .. viewPageCount-1). Die
    // Uebersetzung auf Seiten-Key bzw. Quellseite macht der Controller.
    Q_INVOKABLE void setSourcePageCount(int n);
    // Notizen adressieren ihre Seite darueber, damit sie beim Umsortieren bleiben.
    Q_INVOKABLE int  viewPageKey(int viewIndex) const;
    // >=0 = Index in der Quelldatei, -1 = eingefuegte Leerseite.
    Q_INVOKABLE int  viewSourcePage(int viewIndex) const;
    Q_INVOKABLE int  pageRotation(int viewIndex) const;
    // { exists, key, src, doc, rot, blank, imported, textEditable }
    Q_INVOKABLE QVariantMap pageInfo(int viewIndex) const;
    // viewIndex -1 fuegt an den Anfang ein.
    Q_INVOKABLE void addBlankPageAfter(int viewIndex);
    // Die letzte verbleibende Seite bleibt bestehen.
    Q_INVOKABLE void removePage(int viewIndex);
    // to bezeichnet die Position NACH dem Herausnehmen.
    Q_INVOKABLE void movePage(int from, int to);
    // deltaDeg zusaetzlich zur bisherigen Drehung; Notizen drehen mit.
    Q_INVOKABLE void rotatePage(int viewIndex, int deltaDeg,
                                qreal pageWPt, qreal pageHPt);
    // Verlustfrei in die Begleitdatei <pdf>.mgpages.pdf uebernommen - die fremde
    // Datei wird danach nicht mehr gebraucht.
    Q_INVOKABLE void insertPagesFrom(const QString& pathOrUrl,
                                     const QVariantList& pages,
                                     int afterViewIndex);
    Q_INVOKABLE int  probePageCount(const QString& pathOrUrl) const;
    // Destruktiv liefert die pristine Sicherung (.mgorig), damit die Anzeige
    // plan-getrieben ueber unveraenderte Seiten laeuft.
    Q_INVOKABLE QString renderSourcePath() const;
    // Zielseite nach dem naechsten Neuladen; wird genau einmal gelesen und danach
    // wieder -1. Ohne das sprang die Ansicht an den Dokumentanfang.
    Q_INVOKABLE int  takeStructureFocus();

    // Schreibt die erkannten Woerter als unsichtbare Textebene dauerhaft in die
    // Datei. Ohne Tesseract ein No-op.
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable CONSTANT)
    Q_PROPERTY(bool searchableBusy READ searchableBusy NOTIFY searchableBusyChanged)
    Q_PROPERTY(bool alreadySearchable READ alreadySearchable NOTIFY documentRewritten)

    bool ocrAvailable() const;
    bool searchableBusy() const { return m_searchableBusy; }
    bool alreadySearchable() const;
    // Ein zweiter Aufruf waehrend des Laufs tut nichts.
    Q_INVOKABLE void makeSearchable();
    Q_INVOKABLE void cancelSearchable();
    // Intern (Kommando redo/undo) - nicht aus QML rufen.
    void applyPlan(const QVector<PdfPlanPage>& plan);

    // paste fuegt eine versetzte Kopie auf derselben Seite ein.
    Q_INVOKABLE void copySelected();
    Q_INVOKABLE void paste();

    Q_INVOKABLE void beginGeometryEdit(int id);
    Q_INVOKABLE void updateGeometry(int id, qreal xPt, qreal yPt, qreal wPt, qreal hPt);
    // y darf waehrend der Session ueber den Seitenrand hinausgehen; die finale
    // Klemmung macht QML beim Loslassen.
    Q_INVOKABLE void updatePlacement(int id, int page, qreal xPt, qreal yPt,
                                     qreal wPt, qreal hPt);
    Q_INVOKABLE void endGeometryEdit(int id);

    Q_INVOKABLE void beginTextEdit(int id);
    Q_INVOKABLE void updateText(int id, const QString& text);
    Q_INVOKABLE void endTextEdit(int id);

    // Selbstbezug und Zyklen werden abgewiesen; danach reflowt die Kette.
    Q_INVOKABLE void linkChain(int fromId, int toId);
    Q_INVOKABLE void unlinkChain(int fromId);

    // Die Aenderungen sind ein Delta im Sidecar, keine Aenderung am Original.
    // Angezeigt wird die Arbeitsdatei <pdf>.mgtext.pdf = pristine + alle Ops.

    // Ein Seitenwechsel verwirft das vorherige Layout - hoechstens eine Seite im RAM.
    Q_INVOKABLE void prepareCaretPage(int page);
    Q_INVOKABLE void placeCaret(int page, qreal xPt, qreal yPt);
    Q_INVOKABLE void moveCaret(int delta);
    Q_INVOKABLE void moveCaretLine(int delta);
    Q_INVOKABLE void caretHome();
    Q_INVOKABLE void caretEnd();
    Q_INVOKABLE void clearCaret();
    // dir: -1 Ruecktaste, +1 Entf.
    Q_INVOKABLE void insertAtCaret(const QString& text);
    Q_INVOKABLE void deleteAtCaret(int dir);
    Q_INVOKABLE int  caretGlyphCount() const { return m_caretGlyphs.size(); }

    // id < 0 setzt nur die Vorlage fuer neue Annotationen, id >= 0 aendert die
    // Annotation und zieht die Vorlage nach.
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
    // 0 = oben (Standard), 1 = mittig.
    Q_INVOKABLE void setBoxVAlign(int id, int vAlign);

    Q_INVOKABLE QVariantMap boxInfo(int id) const;
    Q_INVOKABLE QVariantMap defaultInfo() const;

    // Schliessen offene Sessions deterministisch ab.
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Sidecar atomar schreiben; ein leeres Overlay entfernt es.
    Q_INVOKABLE bool saveOverlay();
    Q_INVOKABLE void exportPdf();
    // Schreibt den Ersatztext direkt in die eingebettete Textebene, die Datei
    // bleibt vektoriell. Nur wenn alle Annotationen stream-editierbar sind und
    // der Plan die Identitaet ist; sonst Rueckfall auf Raster.
    Q_INVOKABLE void exportContentEdited();
    // Immer ein eindeutiger Kopie-Pfad - das Original wird nie ersetzt.
    Q_INVOKABLE QString exportTargetPath() const;
    // Stempel aus dem Arbeitsordner waehlen; PDFs bleiben draussen.
    Q_INVOKABLE QVariantList folderImages() const;

    // Puffert nur - value ist bei Ankreuz-/Optionsfeldern der Zustandsname des
    // gewuenschten Knopfes, "Off" waehlt ab.
    Q_INVOKABLE void setFormValue(const QString& name, const QString& value);
    // Gepuffert, sonst der Wert aus der Datei; rev-getrieben ueber formValueRev.
    Q_INVOKABLE QString formValue(const QString& name) const;
    // Schreibt in eine Kopie - das Original bleibt byteweise unangetastet.
    Q_INVOKABLE void saveFormValues();
    Q_INVOKABLE QString formTargetPath() const;

    // Feste Liste; fehlt eine Familie, substituiert Qt beim Rendern.
    Q_INVOKABLE QStringList standardFonts() const;
    Q_INVOKABLE QString resolvedFont(const QString& family) const;

    // Intern (Worker -> GUI ueber QueuedConnection) - nicht aus QML rufen.
    void exportTaskFinished(bool ok, const QString& target,
                            const QString& error, int generation);
    void exportTaskProgress(int done, int total, int generation);
    void searchableTaskProgress(int done, int total, int generation);
    void searchableTaskFinished(bool ok, int pages, int words, int skipped,
                                const QString& error, int generation);
    // Bei Misserfolg startet intern der Raster-Export mit gleicher Generation.
    void contentEditTaskFinished(bool ok, const QString& target, int generation);
    // Schwaerzung liess sich nicht aus dem Stream entfernen - der Export ging den
    // Rasterweg. Wird gemeldet, nie verschwiegen.
    void redactionFellBack(int generation);
    void caretLayoutFinished(int page, const QVector<mg::PdfGlyph>& glyphs,
                             const QString& err, int generation);
    // caretTo = neue Position, falls der Absatz-Umbruch Zeichen verschoben hat
    // (-1 = kein Umbruch); overflow = der Absatz ist voll.
    void textOpsTaskFinished(bool ok, const QString& err, int generation,
                             int caretTo = -1, int caretPage = -1,
                             bool overflow = false);
    void annotReadFinished(const QVector<mg::PdfAnnotation>& annots, int generation);
    void formReadFinished(const QVector<mg::PdfFormField>& fields, int generation);
    void formSaveFinished(bool ok, const QString& target, const QString& error,
                          int generation, bool applyPlan = false);
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
    void contentEditFellBack();
    void busyChanged();
    void textEditingChanged();
    void clipboardChanged();
    void panelOnTopChanged();
    void exportLosslessChanged();
    void exportAsAnnotationsChanged();
    void planChanged();
    // count 0 = nichts uebernommen; errorText leer bei Erfolg.
    void pagesInserted(int count, const QString& errorText);
    // Die .pdf auf Platte wurde neu geschrieben - QML soll neu laden.
    void documentRewritten();
    // Getrennt von documentRewritten: ein Neubau der Textebene aendert die Struktur
    // nicht und darf die Vorschauen nicht bei jedem Tippen verwerfen.
    void pageStructureChanged();
    void boxCountChanged();
    void caretChanged();
    void caretReadyChanged();
    void textOpsChanged();
    void textOpsBusyChanged();
    // Zeichen in der Kodierung der Schrift nicht darstellbar - Aenderung verworfen.
    void textEditFailed(const QString& reason);
    void reflowOverflow();
    void exportFinished(bool ok, const QString& targetPath, const QString& errorText);
    void exportProgress(int done, int total);
    // pages = Seiten mit neuer Textebene, skipped = an der Kodierung gescheitert.
    void searchableProgress(int done, int total);
    void searchableFinished(bool ok, int pages, int words, int skipped,
                            const QString& errorText);
    void searchableBusyChanged();
    void overlaySaved(bool ok);
    void formFieldsChanged();
    void formDirtyChanged();
    void formValueRevChanged();
    // flattened = die Kopie traegt die geaenderte Seitenfolge, ist dafuer kein
    // bedienbares Formular mehr (/AcroForm geht beim Umsortieren verloren).
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
    // Erbt den zuletzt benutzten Stil - aber ohne Text.
    PdfEditBox seededBox() const;
    PdfEditBox seededDraw(PdfAnnKind kind) const;
    PdfEditBox seededReplace() const;
    // Schwarzer Text auf fix weisser Flaeche, keine Post-it-Optik.
    static PdfEditBox makeReplaceTpl();
    // Dieselbe QTextLayout-Mathematik wie der Export - Basis des Hoehenwachstums.
    static qreal requiredHeightPt(const PdfEditBox& b);
    // Die erste Zeile zaehlt immer, damit kein Nullfortschritt entsteht.
    static int   fitCharCount(const PdfEditBox& box, const QString& text);
    int  chainHead(int id) const;
    QVector<int> chainOrder(int headId) const;
    bool isChainMember(int id) const;   // Teil einer Kette (Vorgänger ODER chainNext)?
    // editedId/editedOldText liefern den Session-Start-Zustand fuer ein korrektes
    // Delta; das Ganze wird ein Undo-Schritt.
    void reflowChain(int anyId, int editedId, const QString& editedOldText,
                     const QRectF& editedOldRect);
    // Text -> m_textTpl, Replace -> m_replaceTpl (ohne Highlight), sonst Zeichen-Defaults.
    void mirrorToTemplate(PdfEditField f, const QVariant& v, PdfAnnKind kind);
    bool loadOverlay(const QString& pdfPath);
    static QString sidecarPath(const QString& pdfPath);
    static QString uniqueCopyPath(const QString& pdfPath);
    static QString uniqueSuffixPath(const QString& pdfPath, const QString& suffix);

    void startFormRead();
    void startAnnotRead();
    // Liefert false, wenn die Art im Editor keine Entsprechung hat.
    static bool boxFromAnnotation(const mg::PdfAnnotation& a, PdfEditBox* out);
    // false fuer Arten, die sich nicht verlustfrei abbilden lassen - dann wird gemalt.
    static bool annotationFromBox(const PdfEditBox& b, mg::PdfAnnotation* out);
    bool importChanged(const PdfEditBox& b) const;
    QString formOriginalValue(const QString& name, bool* found, bool* readOnly) const;
    void setFormDirty(bool v);

    // Arbeitsdatei mit angewendeten Textebenen-Ops (pristine + Wiedergabe).
    static QString textWorkPath(const QString& pdfPath);
    // Die Arbeitsdatei, sofern Ops vorliegen - sonst die pristine Datei.
    QString textSourcePath() const;
    QVector<PdfTextOp> effectiveTextOps() const;
    bool textRebuildNeeded() const;
    // Entprellt; ist die Liste identisch zur zuletzt gebauten, passiert nichts.
    void scheduleTextRebuild();
    void startTextRebuild();
    void afterTextRebuild();
    void resetTextState();
    void commitPendingTextOp();
    // true = sofort loslegen, false = wird nach dem Neubau fortgesetzt.
    bool flushTextForExport(int kind);
    void resumePendingExport();
    void requestCaretLayout(int page);
    void setCaretIndex(int idx);
    void setPendingValid(bool v);
    // Die Zeichenhaelfte entscheidet, vor oder hinter welchem Glyph.
    int  hitIndexAt(const QPointF& ptPt) const;
    static bool sameLine(const mg::PdfGlyph& a, const mg::PdfGlyph& b);
    // Naehert das Layout zwischen zwei Neubauten an.
    void spliceGlyphsInsert(int index, const QString& text);
    void spliceGlyphsRemove(int index, int count);

    ISettings&   m_settings;
    bool         m_recording = false;
    void         pushAdd(PdfEditBox& b);
    void         setTrack(int id, PdfTrackState st);

    PdfEditModel m_model;
    QUndoStack   m_stack;

    QString m_docPath;                  // lokaler Pfad des aktiven PDFs

    // Leer, solange keine Seitenzahl bekannt ist.
    QVector<PdfPlanPage> m_plan;
    int          m_srcPageCount = 0;
    // Keys der pristinen Seiten sind per Definition 0 .. m_srcPageCount-1.
    int          m_nextPageKey = 0;
    bool planIsIdentity() const;        // Plan == pristine Seiten, unverändert?
    // page von Seiten-Key auf Ansichts-Index abgebildet; Notizen entfernter Seiten fallen weg.
    QVector<PdfEditBox> exportBoxes() const;
    // Objektnummern der Annotationen, die aus der Ausgabe verschwinden muessen.
    QVector<int> importRemovals() const;
    QVector<mg::PdfAnnotation> exportAnnotations() const;
    // Ohne sie waere die Schwaerzung nur ein Balken ueber weiterhin lesbarem Text.
    QVector<mg::PdfTextEdit> redactionEdits() const;
    // Der geometrische Weg braucht keinen erkannten Originaltext.
    QVector<mg::PdfRedactArea> redactionAreas() const;
    void bakeWorking();                 // Arbeitsdatei aus (pristine + Plan) schreiben
    // Kern von bakeWorking ohne dessen Zielwahl - auch das ausgefuellte Formular
    // geht diesen Weg, damit die Kopie die angezeigte Seitenfolge traegt.
    bool assemblePlanTo(const QString& targetPath, const QString& sourceOverride,
                        QString* err);
    QString pristinePath() const;       // Quelle mit den UNVERÄNDERTEN Seiten
    // doc: 0 = pristine bzw. Textebenen-Arbeitsdatei, 1 = importierte Seiten.
    QString planSourceFile(int doc, bool preferTextWork) const;
    int  keyOfView(int viewIndex) const;
    int  srcOfView(int viewIndex) const;
    int  viewOfKey(int key) const;
    // Tolerant, solange noch kein Plan steht - dann ist die Ansichts-Seite der Key.
    int  pageKeyForView(int viewIndex) const;
    int  caretSrcPage() const;
    bool pageTextEditable(int viewIndex) const;
    // pristine Seite -> key = src, neue Seiten -> laufende Keys.
    void assignPlanKeys();

    bool m_searchableBusy = false;
    int  m_searchableGen  = 0;
    std::shared_ptr<std::atomic<bool>> m_searchableCancel;

    // Beim Schliessen: die Operationen stehen schon in der PDF, der Plan wird
    // verbraucht. Danach kann beim naechsten Oeffnen kein Plan zweimal wirken.
    void consumePlan();
    static QString backupPath(const QString& pdfPath);   // <pdf>.mgorig (destruktiv)
    static QString previewPath(const QString& pdfPath);  // <pdf>.mgpreview.pdf (nicht-destr.)
    static QString assetPath(const QString& pdfPath);    // <pdf>.mgpages.pdf (Importe)
    bool    m_editMode     = false;
    int     m_structureFocus = -1;   // s. takeStructureFocus()
    int     m_tool         = Select;
    int     m_selectedId   = -1;
    int     m_selectionRev = 0;
    int     m_defaultRev   = 0;
    int     m_nextId       = 1;

    // Offene Sessions, genau eine je Art (-1 = keine).
    int     m_geoEditId  = -1;
    int     m_geoOldPage = 0;
    QRectF  m_geoOld;
    QVector<QPointF> m_geoOldPts;       // Striche: Punkte der Session-Basis
    int     m_textEditId = -1;
    QString m_textOld;
    // Session-Start: Hoehenwachstum und Text-Delta werden am Ende ein Undo-Makro.
    QRectF  m_textOldRect;
    int     m_drawId     = -1;          // laufende Zeichen-Session
    int     m_drawPage   = 0;           // Seite der Zeichen-Session
    QPointF m_drawStart;                // Startpunkt (Rechteck/Ellipse/Pfeil)

    PdfEditBox m_clip;
    bool       m_hasClip = false;
    PdfEditBox m_textTpl;
    // Getrennt von den Post-its, damit sich beide Werkzeuge ihre Stile nicht
    // gegenseitig ueberschreiben.
    PdfEditBox m_replaceTpl = makeReplaceTpl();
    // Markieren gelb, Unterstreichen rot, Durchstreichen schwarz.
    int    m_markupStyle = 0;
    QColor m_markupColors[3] = { QColor(255, 235, 0, 140), QColor(200, 0, 0),
                                 QColor(20, 20, 20) };
    QColor     m_defStroke    = QColor(230, 44, 44);
    qreal      m_defLineWidth = 2.0;    // PDF-Punkte
    QColor     m_defFill      = QColor(0, 0, 0, 0);

    // Genau eine Seite (wenige tausend Glyphen a 48 Byte), bei Seitenwechsel ersetzt.
    QVector<mg::PdfGlyph> m_caretGlyphs;
    int     m_caretPage    = -1;
    int     m_caretIndex   = -1;
    bool    m_caretReady   = false;
    QString m_caretError;
    int     m_caretGen     = 0;         // verwirft veraltete Layout-Läufe
    QPointF m_caretHitPt;               // Klick, der auf das Layout wartet
    bool    m_caretHitPending = false;
    QVector<PdfTextOp> m_textOps;
    PdfTextOp          m_pending;
    bool               m_pendingValid  = false;
    bool               m_pendingCommit = false;  // wartet auf Bestätigung
    // Verhindert Neubauten, die nichts aendern.
    QVector<PdfTextOp> m_builtOps;
    QVector<PdfTextOp> m_buildingOps;   // Liste des laufenden Neubaus
    bool               m_textWorkValid = false;
    bool               m_textOpsLoaded = false;  // Ops stammen aus dem Sidecar
    bool               m_textOpsBusy   = false;
    int                m_textOpsGen    = 0;
    int                m_exportPending = 0;      // 0 kein, 1 Raster, 2 verlustfrei
    QTimer             m_textFlush;     // Entprellung des Neubaus

    // page ist der QUELLseitenindex; formFields() bildet ueber den Seiten-Key auf
    // die Ansichts-Seite ab, damit umsortierte Seiten stimmen.
    QHash<int, QJsonObject> m_importBaseline;
    int  m_annotReadGen = 0;            // verwirft veraltete Leseläufe

    QVector<mg::PdfFormField> m_formFields;
    // Nur Abweichungen vom Original; ein zurueckgesetztes Feld verschwindet wieder.
    QHash<QString, QString>   m_formEdits;
    bool m_formDirty    = false;
    int  m_formValueRev = 0;            // Wertänderungs-Zähler (s. Q_PROPERTY)
    // Leer = kein Zwischenschritt noetig.
    QString m_formPlanTarget;
    int  m_formReadGen = 0;             // verwirft veraltete Leseläufe
    int  m_formSaveGen = 0;             // verwirft veraltete Schreibläufe

    // 1 Worker deckelt den RAM-Peak; die Generationszahl verwirft Veraltetes.
    QThreadPool m_pool;
    bool        m_busy      = false;
    int         m_exportGen = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
