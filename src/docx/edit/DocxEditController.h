#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxEditController — DEZENTRALER Editor-Kern des DOCX-Editors: je geöffneter
//  DOCX-Kachel (DocxSurface) erzeugt QML eine EIGENE Instanz (qmlRegisterType,
//  Muster PdfEditController) → getrennter Cursor/Undo/Dirty-Zustand pro Datei,
//  Split-View-tauglich ohne globalen Zustand.
//
//  Besitzt das Docx::Document (Verlusterhaltungs-Modell), den Undo-Stack,
//  Cursor + Selektion (EINE Quelle der Wahrheit — Toolbar und DocxTextArea
//  lesen beide hier) sowie Laden/Speichern:
//   • Laden ASYNC (QRunnable + QThreadPool + Generationszähler, Regel 8/17).
//   • Speichern ASYNC: das neue document.xml + Ersatzteile entstehen auf dem
//     GUI-Thread (reine String-Arbeit am Modell), der Worker übernimmt
//     Deflate + Roh-Kopie + QSaveFile-Commit.
//   • Direkt-Modus: einmalig je Sitzung eine .bak-Sicherung; Export-Modus:
//     <Name>_edited(.n).docx (Vorgabe des Auftrags).
//
//  Alle Text-Operationen laufen über EIN Kommando-Muster (DocxReplaceBlocks-
//  Command): Bereich kopieren → mutieren → Kommando mit Vorher/Nachher pushen.
// ─────────────────────────────────────────────────────────────────────────────

#include "docx/DocxDocument.h"
#include "docx/edit/DocxEditCommands.h"

#include <QObject>
#include <QUndoStack>
#include <QVariantMap>
#include <QPointer>
#include <functional>

class DocxEditController : public QObject {
    Q_OBJECT
    //  ── Bearbeitungs-REGION (Aufgabe 3A) ────────────────────────────────────
    //  Kopf- und Fußzeile sind eigene ZIP-Teile mit eigenem Rumpf. Sie werden
    //  als eigene `Docx::Document`-Instanz geladen; der Controller kennt die
    //  aktive Region und die Fläche bindet immer an die aktive (`doc()`).
    Q_PROPERTY(int activeRegion READ activeRegionInt WRITE setActiveRegionInt
                   NOTIFY activeRegionChanged)
    Q_PROPERTY(bool hasHeader READ hasHeader NOTIFY regionsAvailable)
    Q_PROPERTY(bool hasFooter READ hasFooter NOTIFY regionsAvailable)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)          // Speichern läuft
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY readyChanged)
    //  Format am Cursor/der Selektion (Toolbar) — rev-getrieben wie boxInfo
    //  im PDF-Editor: bei jeder Cursor-/Formatänderung inkrementiert.
    Q_PROPERTY(int formatRev READ formatRev NOTIFY formatRevChanged)
    //  Live-Transliteration: QML reicht das Translit-Singleton herein; der
    //  Controller ruft liveApply() nach jeder Zeichen-Eingabe (Muster
    //  TextSurface, nur controllerseitig).
    Q_PROPERTY(QObject* translit READ translit WRITE setTranslit NOTIFY translitChanged)

public:
    explicit DocxEditController(QObject* parent = nullptr);
    ~DocxEditController() override;

    QString source() const { return m_source; }
    void    setSource(const QString& s);
    bool    ready() const { return m_ready; }
    bool    busy() const { return m_busy; }
    bool    modified() const { return m_modified; }
    bool    canUndo() const { return m_stack.canUndo(); }
    bool    canRedo() const { return m_stack.canRedo(); }
    QString loadError() const { return m_loadError; }
    int     formatRev() const { return m_formatRev; }
    QObject* translit() const { return m_translit; }
    void     setTranslit(QObject* t);

    //  Zugriff der Anzeige (DocxTextArea, gleicher Prozess/Thread).
    Docx::Document&       doc()       { return m_doc; }
    const Docx::Document& doc() const { return m_doc; }
    const DocxCursor&     cursor() const { return m_cursor; }

    // ── Cursor & Selektion ───────────────────────────────────────────────────
    Q_INVOKABLE void setCursor(int block, int pos, bool keepAnchor);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectWordAt(int block, int pos);
    Q_INVOKABLE void clearSelection();

    // ── Text-Operationen (alle Undo-fähig; ersetzen ggf. die Selektion) ──────
    Q_INVOKABLE void insertText(const QString& text);   // \n = Absatz-Split
    Q_INVOKABLE void deleteBackward();                  // Backspace (Merge am Anfang)
    Q_INVOKABLE void deleteForward();                   // Entf (Merge am Ende)
    Q_INVOKABLE void insertParagraphBreak();            // Enter
    Q_INVOKABLE void insertLineBreak();                 // Shift+Enter (<w:br/>)

    // ── Zeichenformat (Selektion; ohne Selektion: Format fürs nächste Tippen) ─
    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void toggleUnderline();
    Q_INVOKABLE void setFontFamily(const QString& family);
    Q_INVOKABLE void setFontSizePt(qreal pt);
    Q_INVOKABLE void setTextColor(const QColor& c);

    // ── Absatzformat (alle Absätze der Selektion) ────────────────────────────
    Q_INVOKABLE void setAlignment(int align);            // 0 l · 1 z · 2 r · 3 Block
    Q_INVOKABLE void setLineSpacing(qreal multiple);      // 1.0/1.15/1.5/2.0 …
    Q_INVOKABLE void setSpaceBeforePt(qreal pt);
    Q_INVOKABLE void setSpaceAfterPt(qreal pt);
    Q_INVOKABLE void toggleBullets();
    Q_INVOKABLE void toggleNumbering();

    // ── Formatvorlagen (Absatzvorlagen des Dokuments) ────────────────────────
    //  Liste für die Auswahl: [{ id, name, isDefault }] in Katalog-Reihenfolge
    //  (Standardvorlage zuerst). Leer, wenn das Dokument keine styles.xml hat.
    Q_INVOKABLE QVariantList paragraphStyles() const;
    //  Vorlage auf alle Absätze der Selektion anwenden. Die Standardvorlage
    //  (oder eine leere id) ENTFERNT das w:pStyle — direkte Formatierung des
    //  Absatzes bleibt in beiden Fällen erhalten.
    Q_INVOKABLE void setParagraphStyle(const QString& styleId);

    // ── Einfügen ─────────────────────────────────────────────────────────────
    //  Leere Tabelle NACH dem aktuellen Absatz einfügen (undo-fähig). Steht der
    //  Cursor in einer Zelle, landet sie hinter der GANZEN Tabelle — verschachtelte
    //  Tabellen werden bewusst nicht erzeugt (der Parser deutet sie auch nicht).
    Q_INVOKABLE void insertTable(int rows, int cols);
    //  Bild als eigenen Absatz einfügen (undo-fähig). `fileUrl` darf eine
    //  file://-URL oder ein Pfad sein. Fehler landen im Status über
    //  imageInsertFailed.
    Q_INVOKABLE void insertImage(const QString& fileUrl);
    //  Bild aus BYTES einfügen (Zwischenablage): `ext` ist die Zielendung
    //  ("png"/"jpg"/…) und bestimmt Content-Type und Teilname im Container.
    Q_INVOKABLE void insertImageData(const QByteArray& bytes, const QString& ext);
    //  Bilder im ORDNER der geöffneten Datei — als [{name, url}] für das
    //  Auswahl-Popup. Filter ist QImageReader::supportedImageFormats(), also
    //  jedes Format, das Qt lesen kann (keine feste Endungsliste).
    Q_INVOKABLE QVariantList folderImages() const;
    //  Seitenzahl einer PDF (für den Seitenwähler); 0 = keine lesbare PDF.
    Q_INVOKABLE int pdfPageCount(const QString& fileUrl) const;
    //  EINE Seite einer PDF als Bild einfügen. Gerendert wird bei 150 dpi —
    //  fein genug zum Drucken, ohne den Container zu sprengen.
    Q_INVOKABLE void insertPdfPage(const QString& fileUrl, int page);
    //  Inhaltsverzeichnis NACH dem aktuellen Absatz einfügen (undo-fähig).
    //  Das Feld bleibt deklarativ — die Einträge zeigt die Fläche aus der
    //  eigenen Paginierung, Word rechnet sie beim Öffnen selbst.
    Q_INVOKABLE void insertTableOfContents();

    // ── Tabellen-Struktur (Kontextmenü) ──────────────────────────────────────
    //  Lage-/Zustandsauskunft für den Block `block` (−1 = Cursorblock):
    //  { table (bool), tableId, row, col, rows, cols, editable, widths[] }.
    //  `editable` false ⇒ verbundene Zellen/ungleiche Zeilen: Struktur wird
    //  nicht angefasst, das Menü bietet dann nur „Tabelle löschen".
    Q_INVOKABLE QVariantMap tableInfoAt(int block) const;
    Q_INVOKABLE void tableInsertRow(int tableId, int atRow);
    Q_INVOKABLE void tableDeleteRow(int tableId, int row);
    Q_INVOKABLE void tableInsertColumn(int tableId, int atCol);
    Q_INVOKABLE void tableDeleteColumn(int tableId, int col);
    //  Spaltenbreiten in MILLIMETERN (die Anzeige rechnet nicht in Twips).
    Q_INVOKABLE void tableSetColumnWidthsMm(int tableId, const QVariantList& mm);
    Q_INVOKABLE void deleteTable(int tableId);
    //  ALLE Spalten mit demselben Faktor skalieren (Ziehen an Rahmen/Ecke) —
    //  die Zellen behalten dadurch ihr Größenverhältnis zueinander.
    Q_INVOKABLE void scaleTableWidths(int tableId, qreal factor);

    // ── Bildgröße ────────────────────────────────────────────────────────────
    //  { image (bool), block, widthMm, heightMm } für den Block `block`
    //  (−1 = Cursorblock). image == false ⇒ kein reiner Bild-Absatz.
    Q_INVOKABLE QVariantMap imageInfoAt(int block) const;
    //  Ausgewähltes Bild (= Cursor steht in einem reinen Bild-Absatz) in die
    //  Zwischenablage legen bzw. entfernen. `copy()`/`cut()` rufen das selbst
    //  auf, wenn keine TEXT-Selektion besteht — damit wirken Strg+C/Strg+X
    //  auch auf ein Bild.
    Q_INVOKABLE bool copyImageAtCursor();
    Q_INVOKABLE void deleteImageAtCursor();
    //  Neue Größe in Millimetern (undo-fähig; Seitenverhältnis hält der
    //  Aufrufer, damit Ziehen an einer Kante bewusst verzerren darf).
    Q_INVOKABLE void setImageSizeMm(int block, qreal widthMm, qreal heightMm);

    // ── Zwischenablage / Undo ────────────────────────────────────────────────
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // ── Suchen & Ersetzen ────────────────────────────────────────────────────
    //  Sucht `needle` ab der Cursorstelle (bzw. Selektionsgrenze), wählt den
    //  Treffer aus (scrollt hin) und liefert { found, block, pos, wrapped }.
    //  Gesucht wird nur in editierbaren Absätzen (Tabellen/opake Blöcke werden
    //  übersprungen); ein Treffer überschreitet keine Absatzgrenze. Die Suche
    //  läuft umlaufend (wrap-around) EINMAL durch das Dokument.
    Q_INVOKABLE QVariantMap findNext(const QString& needle, bool caseSensitive,
                                     bool backward);
    //  Ersetzt die AKTUELLE Selektion, wenn sie exakt `needle` ist, durch
    //  `replacement` (undo-fähig) und sucht danach den nächsten Treffer.
    //  Liefert das Ergebnis von findNext plus { replaced }.
    Q_INVOKABLE QVariantMap replaceAndFind(const QString& needle,
                                           const QString& replacement,
                                           bool caseSensitive);
    //  Ersetzt ALLE Vorkommen im ganzen Dokument als EINEN Undo-Schritt;
    //  liefert die Anzahl der Ersetzungen.
    Q_INVOKABLE int replaceAll(const QString& needle, const QString& replacement,
                               bool caseSensitive);

    // ── Speichern ────────────────────────────────────────────────────────────
    //  save(): Direkt-Modus (auf die Quelldatei, .bak einmalig je Sitzung).
    //  exportCopy(): immer <Name>_edited(.n).docx daneben.
    Q_INVOKABLE void save();
    Q_INVOKABLE void exportCopy();
    //  DOCX → PDF exportieren (Aufgabe 2): schreibt <Name>.pdf NEBEN der
    //  Quelle (Original bleibt erhalten), async im Worker (Muster wie oben).
    //  tablePlaceholder/pageBreakLabel = i18n-Texte aus QML (wie die Anzeige).
    Q_INVOKABLE void exportToPdf(const QString& tablePlaceholder,
                                 const QString& pageBreakLabel);
    Q_INVOKABLE void release();                          // Kachel wird verlassen

    //  Format am Cursor (Toolbar): { bold, italic, underline, font, sizePt,
    //  color, align, lineSpacing, beforePt, afterPt, list (0/1/2) }.
    Q_INVOKABLE QVariantMap currentFormat() const;

    //  Aufgelöstes ZEICHENformat an der Cursorstelle (inkl. Pending-Overlay) —
    //  EINE Quelle für die Toolbar (currentFormat) UND die Caret-Geometrie der
    //  DocxTextArea. Ohne diese gemeinsame Auflösung zeigte der Caret stets die
    //  Zeilenhöhe des Layouts und reagierte gar nicht auf Schriftgrößen-
    //  Änderungen ohne Selektion (die nur als Pending-Format existieren).
    Docx::RunFmt caretFormat() const;

    //  Seiteneinrichtung — IMMER die des Körpers, auch wenn gerade die
    //  Kopfzeile bearbeitet wird: sie hat keine eigene und würde sonst auf A4
    //  zurückfallen, die Seite also unter dem Cursor die Größe wechseln.
    const Docx::SectionProps& section() const { return bodyDoc().section(); }

    // ── Regionen (Körper / Kopfzeile / Fußzeile) ─────────────────────────────
    enum Region { Body = 0, Header = 1, Footer = 2 };
    Q_ENUM(Region)

    int  activeRegionInt() const { return int(m_region); }
    void setActiveRegionInt(int r);
    bool hasHeader() const { return m_slots[Header].available; }
    bool hasFooter() const { return m_slots[Footer].available; }
    //  Region wechseln; lädt den Teil beim ersten Mal nach. Liefert false, wenn
    //  es den Teil nicht gibt oder er nicht editierbar ist.
    Q_INVOKABLE bool setRegion(int r);

    //  Interner Anwender der Kommandos (public für DocxReplaceBlocksCommand).
    void applyBlocks(int first, int oldCount, const QList<Docx::Block>& blocks,
                     const DocxCursor& cur);
    //  Undo/Redo eines Kommandos einer ANDEREN Region: erst umschalten (die
    //  Ansicht folgt), dann anwenden — genau das macht Undo Word-ähnlich.
    void activateRegionForCommand(int r);
    //  Gerüst einer Tabelle zurücksetzen (Undo/Redo von Struktur-Änderungen).
    void applyTableDef(int tableId, const Docx::TableDef& def);

signals:
    void sourceChanged();
    void readyChanged();
    void busyChanged();
    void modifiedChanged();
    void undoChanged();
    void formatRevChanged();
    void translitChanged();
    //  Anzeige-Invalidierung: Blöcke [first, first+oldCount) wurden durch
    //  newCount Blöcke ersetzt (Layout ab first neu).
    void blocksReplaced(int first, int oldCount, int newCount);
    void cursorChanged();
    void saveFinished(bool ok, const QString& target, const QString& error);
    //  Ergebnis des DOCX→PDF-Exports (Aufgabe 2).
    void pdfExportFinished(bool ok, const QString& target, const QString& error);
    void imageInsertFailed(const QString& error);
    //  Aktive Region gewechselt — die Fläche legt alles neu aus (anderes
    //  Dokument), die Leiste zeigt die Umschaltung an.
    void activeRegionChanged();
    void regionsAvailable();

private:
    struct EditScope;                                    // s. cpp

    void bumpFormat();
    void setModified(bool m);
    //  Gemeinsamer Rahmen der Tabellen-Struktur-Operationen: EditScope über die
    //  ganze Tabelle + Gerüst-Schnappschuss, dann `op`. Liefert false, wenn die
    //  Operation abgelehnt wurde (dann entsteht auch kein Undo-Schritt).
    bool tableStructOp(int tableId, const std::function<bool()>& op);
    QString blockText(int i) const;
    //  Klartext der aktuellen Selektion (mehrblockig mit „\n" verbunden).
    QString selectionPlainText() const;
    //  Treffer [start, start+len) in Block `bi` auswählen (Anker → Ende).
    void selectRange(int bi, int start, int len);
    int  blockLen(int i) const;
    bool isEditableParagraph(int i) const;
    //  Tabellen-Grenzregeln (Option A): Struktur-Änderungen dürfen nie über
    //  eine Zellgrenze laufen, sonst verschwände eine Zelle.
    bool sameCell(int i, int j) const;
    void clampRangeToCell(int b1, int& b2) const;
    //  Ordnet (block,pos) so, dass (b1,p1) ≤ (b2,p2).
    void orderedSelection(int& b1, int& p1, int& b2, int& p2) const;
    //  Run-Index + Offset im Run zu einer Absatzposition.
    void runAt(const Docx::Block& b, int pos, int* runIdx, int* runOfs) const;
    //  Stellt eine Run-Grenze bei pos her (teilt bei Bedarf); liefert den
    //  Index des Runs, der BEI pos beginnt.
    int  ensureRunBoundary(Docx::Block& b, int pos) const;
    //  Löscht [p1,p2) innerhalb EINES Blocks (Run-bewusst, opake atomar).
    void removeRangeInBlock(Docx::Block& b, int p1, int p2) const;
    //  Wendet das Pending-Format auf einen frisch getippten Run an.
    void applyPendingTo(Docx::Run& r) const;
    //  Pending-Format verwerfen (Cursor verlässt die Stelle / Merge in einen
    //  anderen Absatz). setCursor() macht das implizit — Pfade, die m_cursor
    //  DIREKT setzen (Löschen/Verschmelzen), brauchen den expliziten Aufruf.
    void clearPending();
    //  Aufgelöstes Zeichenformat der Stelle (block,pos) — Zeichen LINKS vom
    //  Cursor; im leeren Absatz das Stil-Format des Absatzes selbst.
    Docx::RunFmt resolvedFormatAt(int block, int pos) const;
    //  Word-Verhalten nach dem Löschen des LETZTEN Zeichens einer Zeile: Die
    //  Zeile behält ihre eigene Formatierung, statt sofort auf das Absatz-/
    //  Stil-Format (z. B. der Überschrift, von der sie das pPr geerbt hat)
    //  zurückzufallen. `had` ist das VOR dem Löschen aufgelöste Format; gesetzt
    //  werden nur die Felder, die sich vom Stil-Format des leeren Absatzes
    //  unterscheiden (minimales rPr beim nächsten Tippen).
    void keepFormatOnEmptiedBlock(int bi, const Docx::RunFmt& had);
    //  Fügt fertige Runs (Zwischenablage) absatzweise an der Cursorstelle ein —
    //  Gegenstück zu insertText, nur mit MITGEBRACHTEM Zeichenformat.
    void insertRunParagraphs(const QList<QList<Docx::Run>>& paras);
    //  Interne Zwischenablage: Selektion → Blob / Blob → Absätze mit Runs.
    QByteArray serializeSelection() const;
    static bool deserializeRuns(const QByteArray& blob,
                                QList<QList<Docx::Run>>* out);
    //  Selektion als HTML-Fragment (Zwischenablage für Word/LibreOffice & Co.).
    QString selectionAsHtml() const;
    //  Zeichenformat-Feld auf die Selektion anwenden (oder Pending setzen).
    void applyCharFormat(int field, const QVariant& value);
    //  Absatzformat via upsertProp auf alle selektierten Absätze.
    void applyParProp(const QString& propName, const QString& newXml,
                      const std::function<void(Docx::ParFmt&)>& mut);
    void toggleList(bool bullet);
    //  Nach Zeichen-Eingabe: Live-Transliteration am Cursor-Block.
    void runTranslit();
    QString exportTargetPath() const;
    QString pdfExportTargetPath() const;                 // <Name>.pdf, Kollision → „ (n)"
    void startSaveWorker(const QString& targetPath, bool direct);

    //  ── Regionen ─────────────────────────────────────────────────────────────
    //  Die AKTIVE Region liegt in `m_doc`/`m_cursor` — dadurch bleibt der
    //  gesamte übrige Editor-Code unverändert und arbeitet immer auf ihr. Beim
    //  Umschalten wird der Zustand in den Slot GEPARKT und der andere geholt
    //  (move, also O(1) je Member).
    //
    //  EIN Undo-Stack für ALLE Regionen — bewusst statt je Region einer plus
    //  Koordinator: jedes Kommando merkt sich seine Region und schaltet beim
    //  Undo/Redo selbst um. Zwei Stapel könnten auseinanderlaufen (eine
    //  verworfene Redo-Historie in Region A ist aus Region B heraus nicht mehr
    //  aus einem QUndoStack zu entfernen); mit einem Stapel ist die Reihenfolge
    //  per Konstruktion global richtig.
    struct RegionSlot {
        Docx::Document doc;
        DocxCursor     cursor;
        QString        partPath;          // "word/header1.xml", … ("" = keiner)
        bool           available = false; // Teil im Container vorhanden
        bool           loaded    = false; // schon geparst
    };
    RegionSlot m_slots[3];
    Region     m_region = Body;
    //  Teil laden, falls nötig; false = nicht vorhanden/nicht editierbar.
    bool ensureRegionLoaded(Region r);
    //  Das KÖRPER-Dokument, egal welche Region gerade aktiv ist (PDF-Export,
    //  Seitengeometrie, Kopfzeilen-Anzeige hängen daran).
    const Docx::Document& bodyDoc() const {
        return (m_region == Body) ? m_doc : m_slots[Body].doc;
    }
    //  Alle Regionen für das Speichern zusammentragen (Teil-XML + Ersatzteile).
    QHash<QString, QByteArray> allRegionParts() const;

    Docx::Document m_doc;
    QString    m_source;
    QString    m_loadError;
    bool       m_ready = false;
    bool       m_busy = false;
    bool       m_modified = false;
    bool       m_bakDone = false;        // .bak einmalig je Sitzung
    int        m_formatRev = 0;
    int        m_loadGen = 0;            // Generationszähler async Laden
    DocxCursor m_cursor;
    QUndoStack m_stack;
    QObject*   m_translit = nullptr;
    //  „Format fürs nächste Tippen" (Word-Muster: Fett klicken ohne Selektion
    //  wirkt auf die nächste Eingabe an dieser Stelle).
    Docx::RunFmt m_pending;
    int        m_pendingBlock = -1;
    int        m_pendingPos   = -1;
};
