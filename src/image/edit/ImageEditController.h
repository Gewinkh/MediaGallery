#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  ImageEditController.h
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  QML-TYP („ImageEditController", via qmlRegisterType je ImageSurface-Kachel
//  instanziiert — dezentral wie der PDF-Editor, s. PdfEditController) des
//  Bild-Editors: verwaltet den Bearbeitungsmodus, das aktive Werkzeug, das
//  Overlay-Modell (ImageEditModel), die Auswahl, das delta-basierte Undo/Redo
//  (QUndoStack — QtGui, KEINE Widgets), Copy/Paste, die Sidecar-Persistenz und
//  den asynchronen Bild-Export.
//
//  OVERLAY-ARCHITEKTUR (analog PDF-Editor)
//  ───────────────────────────────────────
//   • Das Original-BILD wird NIE verändert. Alle Bearbeitungen sind
//     ImageAnnotation-Objekte über dem gerenderten Bild (Anzeige: QML).
//   • SPEICHERN  → Sidecar „<bild>.mgedit.json" (QSaveFile, atomar). Beim
//     nächsten Öffnen lädt setDocument() das Sidecar → dauerhaft editierbar.
//   • EXPORT     → rendert Original + Overlay in eine NEUE Bildkopie
//     (QImage+QPainter, Worker-Task). Ziel ist IMMER eine Kopie
//     „…_bearbeitet(.n).<ext>" im QUELLFORMAT (JPG→JPG, PNG→PNG, sonst PNG).
//
//  KOORDINATEN: Bild-PIXEL (Ursprung oben-links). QML rechnet über `imgScale`
//  (angezeigte Pixel je Bild-Pixel) in Bildschirm-Pixel; der Export zeichnet
//  1:1 in die native Auflösung → WYSIWYG. Font-/Linienbreite ebenfalls Bild-px.
//
//  STIL-ERBEN: Eine neu erzeugte Annotation übernimmt die zuletzt benutzten
//  Einstellungen ihrer Art (Text: Schrift/Farben/Deckkraft/Ausrichtung — OHNE
//  Text; Strich/Form: Linienfarbe/-breite/Füllung). Stiländerungen an einer
//  Auswahl aktualisieren zugleich die Vorlage.
//
//  ASYNC-MUSTER (Projektkonvention): QRunnable + eigener QThreadPool
//  (maxThreadCount=1) + kooperatives Atomic-Cancel + QueuedConnection zurück
//  auf den GUI-Thread; eine Generationszahl verwirft veraltete Ergebnisse.
// ══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include <QColor>
#include <QUndoStack>
#include <QThreadPool>
#include <atomic>
#include <memory>

#include "image/edit/ImageEditModel.h"
#include "image/edit/ImageEditTypes.h"

class ImageEditController : public QObject {
    Q_OBJECT
public:
    // Aktives Werkzeug (Toolbar). Select = Auswählen/Verschieben/Skalieren.
    enum Tool { Select = 0, TextTool, FreehandTool, ArrowTool, RectTool, EllipseTool };
    Q_ENUM(Tool)

private:
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
    //  Nachverfolgte Änderungen — Bedeutung und Bedienung identisch zum
    //  PDF-Editor (s. Structure.md ▸ ## PdfEdit).
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged)
    Q_PROPERTY(int  trackedCount READ trackedCount NOTIFY trackedChanged)
    Q_PROPERTY(int  tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(int  selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged)
    Q_PROPERTY(int  selectionRev READ selectionRev NOTIFY selectionRevChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool textEditing READ textEditing NOTIFY textEditingChanged)
    Q_PROPERTY(bool hasClipboard READ hasClipboard NOTIFY clipboardChanged)
    // Natürliche Bildmaße (Pixel) — Klemm-Grenzen + Platzierung. Aus QImageReader
    // (Header) beim setDocument; QML kann sie via setImageSize() überschreiben,
    // falls QImageReader ein Format nicht messen kann (Fallback).
    Q_PROPERTY(int  imageWidth  READ imageWidth  NOTIFY imageSizeChanged)
    Q_PROPERTY(int  imageHeight READ imageHeight NOTIFY imageSizeChanged)
    // Zähler: bumpt bei jeder Änderung der „Vorlagen"-Defaults (neue Annotation)
    // → Panel/Toolbar lesen defaultInfo() rev-getrieben, wenn nichts ausgewählt.
    Q_PROPERTY(int  defaultRev READ defaultRev NOTIFY defaultRevChanged)
    Q_PROPERTY(QObject* annModel READ annModel CONSTANT)
    Q_PROPERTY(int  annCount READ annCount NOTIFY annCountChanged)
    // Layout-Konstanten (einzige Quelle — Anzeige = Export = WYSIWYG).
    Q_PROPERTY(qreal boxPaddingPx READ boxPaddingPx CONSTANT)
    Q_PROPERTY(qreal minAnnPx READ minAnnPx CONSTANT)
    Q_PROPERTY(qreal noteFoldPx READ noteFoldPx CONSTANT)
    Q_PROPERTY(qreal noteShadowDxPx READ noteShadowDxPx CONSTANT)
    Q_PROPERTY(qreal noteShadowDyPx READ noteShadowDyPx CONSTANT)

public:
    // Innenabstand des Textes im Box-Rechteck (Bild-Pixel).
    static constexpr qreal kBoxPaddingPx   = 6.0;
    // Mindest-Annotationsgröße (Bild-Pixel).
    static constexpr qreal kMinAnnPx       = 8.0;
    // Undo-Deckel (Delta-Kommandos sind winzig — reine Hygiene).
    static constexpr int   kUndoLimit      = 200;
    // Sidecar-Größenschutz beim Laden (wie ViewerController::readTextFile).
    static constexpr qint64 kMaxSidecarBytes = 8LL * 1024 * 1024;
    // Post-it-Optik (Bild-Pixel): Eselsohr unten rechts + weicher Schatten.
    static constexpr qreal kNoteFoldPx     = 14.0;
    static constexpr qreal kNoteShadowDxPx = 3.0;
    static constexpr qreal kNoteShadowDyPx = 4.0;
    // JPEG-Exportqualität (verlustarm).
    static constexpr int   kJpegExportQuality = 92;

    explicit ImageEditController(QObject* parent = nullptr);
    ~ImageEditController() override;

    bool editMode() const { return m_editMode; }
    void setEditMode(bool on);
    int  tool() const { return m_tool; }
    void setTool(int t);
    bool canUndo() const { return m_stack.canUndo(); }
    bool canRedo() const { return m_stack.canRedo(); }
    bool dirty() const { return !m_stack.isClean(); }
    int  selectedId() const { return m_selectedId; }
    void setSelectedId(int id);
    int  selectionRev() const { return m_selectionRev; }
    bool busy() const { return m_busy; }
    bool textEditing() const { return m_textEditId >= 0; }
    bool hasClipboard() const { return m_hasClip; }
    int  imageWidth() const { return m_imgW; }
    int  imageHeight() const { return m_imgH; }
    int  defaultRev() const { return m_defaultRev; }
    QObject* annModel() { return &m_model; }
    int  annCount() const { return m_model.count(); }
    qreal boxPaddingPx() const { return kBoxPaddingPx; }
    qreal minAnnPx() const { return kMinAnnPx; }
    qreal noteFoldPx() const { return kNoteFoldPx; }
    qreal noteShadowDxPx() const { return kNoteShadowDxPx; }
    qreal noteShadowDyPx() const { return kNoteShadowDyPx; }

    // ── Dokument-Lebenszyklus ─────────────────────────────────────────────────
    bool recording() const { return m_recording; }
    void setRecording(bool on);
    int  trackedCount() const;
    //  Alle Notizen/Zeichnungen dieses Bildes verwerfen — wie im PDF-Editor:
    //  NICHT die Sidecar-Datei löschen (der Editor hielte die Annotationen
    //  im Speicher und schriebe sie zurück), sondern entfernen (EIN
    //  Undo-Schritt) und sichern; ein leeres Overlay räumt den Sidecar ab.
    Q_INVOKABLE void discardAllAnnotations();
    Q_INVOKABLE void acceptChange(int id);
    Q_INVOKABLE void rejectChange(int id);
    Q_INVOKABLE void acceptAllChanges();
    Q_INVOKABLE void rejectAllChanges();

    Q_INVOKABLE void setDocument(const QString& pathOrUrl);
    Q_INVOKABLE void releaseDocument();
    //  Fallback, falls QImageReader das Format nicht messen konnte: QML liefert
    //  die natürliche Größe aus der geladenen Image (sourceSize) nach.
    Q_INVOKABLE void setImageSize(int w, int h);

    // ── Annotationen erzeugen / entfernen ─────────────────────────────────────
    //  addText: Textnotiz an Klickposition (Bild-Pixel), Stil aus der Text-
    //  Vorlage (ohne Text). Liefert die neue ID (Auswahl folgt).
    Q_INVOKABLE int  addText(qreal xPx, qreal yPx);
    //  Zeichen-Session (Freihand/Pfeil/Rechteck/Ellipse):
    //   beginDraw() legt die Annotation LIVE an (sichtbare Vorschau, KEIN
    //   Kommando), updateDraw() erweitert/skaliert live, endDraw() finalisiert
    //   → genau EIN Add-Kommando (Undo entfernt die ganze Zeichnung).
    Q_INVOKABLE int  beginDraw(int kind, qreal xPx, qreal yPx);
    Q_INVOKABLE void updateDraw(int id, qreal xPx, qreal yPx);
    Q_INVOKABLE void endDraw(int id);
    Q_INVOKABLE void removeAnn(int id);

    // ── Copy / Paste (Zwischenablage dieser Kachel; INKL. Text) ───────────────
    Q_INVOKABLE void copySelected();
    Q_INVOKABLE void paste();

    // ── Geometrie-Session (Verschieben/Skalieren → EIN Undo-Schritt) ──────────
    //  Für Striche werden die Punkte proportional in das neue Rechteck
    //  transformiert (Verschieben = Translation, Skalieren = Streckung).
    Q_INVOKABLE void beginGeometryEdit(int id);
    Q_INVOKABLE void updateGeometry(int id, qreal xPx, qreal yPx, qreal wPx, qreal hPx);
    Q_INVOKABLE void endGeometryEdit(int id);

    // ── Text-Session (Tippen → EIN Undo-Schritt) ──────────────────────────────
    Q_INVOKABLE void beginTextEdit(int id);
    Q_INVOKABLE void updateText(int id, const QString& text);
    Q_INVOKABLE void endTextEdit(int id);

    // ── Stil/Format (Einzel-Kommandos, mergefähig) ────────────────────────────
    //  id < 0 → setzt NUR die Vorlage/Default für neue Annotationen (kein
    //  Kommando); id >= 0 → ändert die Annotation UND aktualisiert die Vorlage.
    Q_INVOKABLE void setAnnStroke(int id, const QColor& c);
    Q_INVOKABLE void setAnnLineWidth(int id, qreal w);
    Q_INVOKABLE void setAnnFill(int id, const QColor& c);
    Q_INVOKABLE void setAnnFont(int id, const QString& family);
    Q_INVOKABLE void setAnnFontSize(int id, qreal sizePx);
    Q_INVOKABLE void setAnnBold(int id, bool v);
    Q_INVOKABLE void setAnnItalic(int id, bool v);
    Q_INVOKABLE void setAnnUnderline(int id, bool v);
    Q_INVOKABLE void setAnnColor(int id, const QColor& c);
    Q_INVOKABLE void setAnnHighlight(int id, const QColor& c);
    Q_INVOKABLE void setAnnAlignment(int id, int align);
    Q_INVOKABLE void setAnnVAlign(int id, int vAlign);

    // Eigenschaften einer Annotation für Toolbar/Panel.
    Q_INVOKABLE QVariantMap annInfo(int id) const;
    // Aktuelle Vorlagen-Defaults (wenn nichts ausgewählt ist).
    Q_INVOKABLE QVariantMap defaultInfo() const;

    // ── Undo/Redo (schließen offene Sessions deterministisch ab) ──────────────
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // ── Persistenz & Export ───────────────────────────────────────────────────
    Q_INVOKABLE bool saveOverlay();
    Q_INVOKABLE void exportImage();
    Q_INVOKABLE QString exportTargetPath() const;

    // ── Schriften (feste Editor-Palette, wie PDF-Editor) ──────────────────────
    Q_INVOKABLE QStringList standardFonts() const;
    Q_INVOKABLE QString resolvedFont(const QString& family) const;

    // ── Intern (Worker → GUI via QueuedConnection; NICHT aus QML rufen) ───────
    void exportTaskFinished(bool ok, const QString& target,
                            const QString& error, int generation);

signals:
    void editModeChanged();
    void toolChanged();
    void undoStateChanged();
    void recordingChanged();
    void trackedChanged();
    void dirtyChanged();
    void selectedIdChanged();
    void selectionRevChanged();
    void busyChanged();
    void textEditingChanged();
    void clipboardChanged();
    void imageSizeChanged();
    void defaultRevChanged();
    void annCountChanged();
    void exportFinished(bool ok, const QString& targetPath, const QString& errorText);
    void overlaySaved(bool ok);

private:
    void pushCommand(QUndoCommand* cmd);
    void bumpSelectionRev();
    void finishGeometrySession();
    void finishTextSession();
    void finishOpenSessions() { finishGeometrySession(); finishTextSession(); }
    void finishDrawSession();
    void setAnnField(int id, ImageAnnField f, const QVariant& v);
    void mirrorToTemplate(ImageAnnField f, const QVariant& v, bool textKind);
    ImageAnnotation seededText() const;   // Text-Vorlage anwenden (ohne Text)
    ImageAnnotation seededDraw(ImageAnnKind kind) const;
    bool loadOverlay(const QString& imgPath);
    static QString sidecarPath(const QString& imgPath);
    static QString uniqueCopyPath(const QString& imgPath, const QString& ext);

    bool           m_recording = false;
    void           pushAdd(ImageAnnotation& a);
    void           setTrack(int id, ImageTrackState st);

    ImageEditModel m_model;
    QUndoStack     m_stack;

    QString m_docPath;                  // lokaler Pfad des aktiven Bildes
    int     m_imgW = 0;
    int     m_imgH = 0;
    bool    m_editMode  = false;
    int     m_tool      = Select;
    int     m_selectedId = -1;
    int     m_selectionRev = 0;
    int     m_defaultRev = 0;
    int     m_nextId    = 1;

    // Offene Sessions (genau eine je Art; -1 = keine).
    int     m_geoEditId  = -1;
    QRectF  m_geoOldRect;
    QVector<QPointF> m_geoOldPts;
    int     m_textEditId = -1;
    QString m_textOld;
    int     m_drawId     = -1;          // laufende Zeichen-Session
    QPointF m_drawStart;                // Startpunkt (Rechteck/Ellipse/Pfeil)

    // Zwischenablage dieser Kachel (Copy/Paste, inkl. Text).
    ImageAnnotation m_clip;
    bool            m_hasClip = false;

    // „Vorlagen" für neue Annotationen (Stil-Erben).
    ImageAnnotation m_textTpl;          // Text-Vorlage (fontFamily/color/… ; text bleibt leer)
    QColor          m_defStroke    = QColor(230, 44, 44);
    qreal           m_defLineWidth = 4.0;
    QColor          m_defFill      = QColor(0, 0, 0, 0);

    // Export (1 Worker → RAM-Peak gedeckelt; Generationszahl verwirft Veraltetes).
    QThreadPool m_pool;
    bool        m_busy      = false;
    int         m_exportGen = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
