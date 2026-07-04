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
#include <QRectF>
#include <QColor>
#include <QUndoStack>
#include <QThreadPool>
#include <atomic>
#include <memory>

#include "PdfEditModel.h"
#include "PdfEditTypes.h"

class ISettings;

class PdfEditController : public QObject {
    Q_OBJECT
    // Bearbeitungsmodus (View ⇄ Edit) — reiner Zustandswechsel, KEIN Reload.
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
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
    // Text-Eigenschaften als obere Leiste (Word-Stil) statt rechter Seitenleiste.
    // Persistiert in den App-Einstellungen (ISettings).
    Q_PROPERTY(bool panelOnTop READ panelOnTop WRITE setPanelOnTop NOTIFY panelOnTopChanged)
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

    explicit PdfEditController(ISettings& settings, QObject* parent = nullptr);
    ~PdfEditController() override;

    bool editMode() const { return m_editMode; }
    void setEditMode(bool on);
    bool canUndo() const { return m_stack.canUndo(); }
    bool canRedo() const { return m_stack.canRedo(); }
    bool dirty() const { return !m_stack.isClean(); }
    int  selectedId() const { return m_selectedId; }
    void setSelectedId(int id);
    int  selectionRev() const { return m_selectionRev; }
    bool busy() const { return m_busy; }
    bool textEditing() const { return m_textEditId >= 0; }
    bool panelOnTop() const;
    void setPanelOnTop(bool v);
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
    //  addAnchoredTextBox: an eine erkannte PDF-Textzeile gefangene Box
    //  (Rechteck der Zeile in Punkten); Schriftgröße wird aus der Zeilenhöhe
    //  abgeleitet, anchored=true.
    Q_INVOKABLE int  addAnchoredTextBox(int page, qreal xPt, qreal yPt,
                                        qreal wPt, qreal hPt);
    Q_INVOKABLE void removeBox(int id);

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

    // ── Stil/Format (Einzel-Kommandos, mergefähig) ────────────────────────────
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

    // Eigenschaften einer Box für Toolbar/Panel: { exists, page, xPt, yPt, wPt,
    // hPt, text, fontFamily, fontSizePt, bold, italic, underline, textColor,
    // highlightColor, hasHighlight, alignment, vAlign, anchored }.
    Q_INVOKABLE QVariantMap boxInfo(int id) const;

    // ── Undo/Redo (schließen offene Sessions deterministisch ab) ──────────────
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // ── Persistenz & Export ───────────────────────────────────────────────────
    //  saveOverlay: Sidecar atomar schreiben (leeres Overlay entfernt es).
    Q_INVOKABLE bool saveOverlay();
    //  exportPdf: rendert asynchron Original + Overlay in das Ziel aus
    //  exportTargetPath(). Ergebnis via exportFinished(ok, pfad, fehler).
    Q_INVOKABLE void exportPdf();
    //  Ziel des nächsten Exports (Anzeige): IMMER ein eindeutiger Kopie-Pfad
    //  „…_bearbeitet(.n).pdf" — das Original wird nie ersetzt (die Bearbeitung
    //  bleibt über das Sidecar dauerhaft reversibel).
    Q_INVOKABLE QString exportTargetPath() const;

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

signals:
    void editModeChanged();
    void undoStateChanged();
    void dirtyChanged();
    void selectedIdChanged();
    void selectionRevChanged();
    void busyChanged();
    void textEditingChanged();
    void panelOnTopChanged();
    void boxCountChanged();
    void exportFinished(bool ok, const QString& targetPath, const QString& errorText);
    void exportProgress(int done, int total);
    void overlaySaved(bool ok);

private:
    void pushCommand(QUndoCommand* cmd);
    void bumpSelectionRev();
    void finishGeometrySession();
    void finishTextSession();
    void finishOpenSessions() { finishGeometrySession(); finishTextSession(); }
    void setBoxField(int id, PdfEditField f, const QVariant& v);
    bool loadOverlay(const QString& pdfPath);
    static QString sidecarPath(const QString& pdfPath);
    static QString uniqueCopyPath(const QString& pdfPath);

    ISettings&   m_settings;
    PdfEditModel m_model;
    QUndoStack   m_stack;

    QString m_docPath;                  // lokaler Pfad des aktiven PDFs
    bool    m_editMode     = false;
    int     m_selectedId   = -1;
    int     m_selectionRev = 0;
    int     m_nextId       = 1;

    // Offene Sessions (genau eine je Art; -1 = keine).
    int     m_geoEditId  = -1;
    int     m_geoOldPage = 0;
    QRectF  m_geoOld;
    int     m_textEditId = -1;
    QString m_textOld;

    // Export (1 Worker → RAM-Peak gedeckelt; Generationszahl verwirft Veraltetes).
    QThreadPool m_pool;
    bool        m_busy      = false;
    int         m_exportGen = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
