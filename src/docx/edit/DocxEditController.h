#pragma once
// Editor-Kern je DOCX-Kachel (eigene Instanz, Split-View-tauglich): Dokument, Undo,
// Cursor und Selektion als eine Quelle der Wahrheit. Laden asynchron; beim Speichern
// entsteht das XML auf dem GUI-Thread, der Worker macht Deflate und QSaveFile.

#include "core/SpellChecker.h"
#include <QSet>
#include <QThreadPool>
#include <memory>
#include "docx/DocxDocument.h"
#include "docx/edit/DocxEditCommands.h"

#include <QObject>
#include <QUndoStack>
#include <QVariantMap>
#include <QPointer>
#include <functional>

class DocxEditController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)          // Speichern läuft
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY readyChanged)
    //  Format am Cursor/der Selektion (Toolbar) - rev-getrieben wie boxInfo
    //  im PDF-Editor: bei jeder Cursor-/Formatänderung inkrementiert.
    Q_PROPERTY(int formatRev READ formatRev NOTIFY formatRevChanged)
    Q_PROPERTY(int revisionCount READ revisionCount NOTIFY revisionsChanged)
    // Autoren der nachverfolgten Änderungen als fertiger Text. Ohne diese Property las der Streifen
    // `editCtl.revisionAuthors`, das es nie gab - QML meldete undefined und die Zeile blieb ohne Autoren.
    Q_PROPERTY(QString revisionAuthorsText READ revisionAuthorsText NOTIFY revisionsChanged)
    Q_PROPERTY(QObject* translit READ translit WRITE setTranslit NOTIFY translitChanged)
    // Seiteneinrichtung für die Randlineale in MILLIMETERN, weil das Lineal so bedient wird; im Dokument stehen
    // Twips. Alles lesend - geschrieben wird über `setPageMarginsMm`, damit es EIN Rückgängig-Schritt bleibt.
    Q_PROPERTY(qreal pageWidthMm    READ pageWidthMm    NOTIFY sectionChanged)
    Q_PROPERTY(qreal pageHeightMm   READ pageHeightMm   NOTIFY sectionChanged)
    Q_PROPERTY(qreal marginTopMm    READ marginTopMm    NOTIFY sectionChanged)
    Q_PROPERTY(qreal marginRightMm  READ marginRightMm  NOTIFY sectionChanged)
    Q_PROPERTY(qreal marginBottomMm READ marginBottomMm NOTIFY sectionChanged)
    Q_PROPERTY(qreal marginLeftMm   READ marginLeftMm   NOTIFY sectionChanged)
    Q_PROPERTY(bool  marginsChanged READ marginsChanged NOTIFY sectionChanged)
    //  Je LINEAL getrennt: das waagerechte stellt links/rechts, das senkrechte
    //  oben/unten - und jeder Zurücksetzen-Knopf darf nur seine eigenen beiden
    //  Ränder anfassen (beide setzten alles zurück).
    Q_PROPERTY(bool  marginsChangedH READ marginsChangedH NOTIFY sectionChanged)
    Q_PROPERTY(bool  marginsChangedV READ marginsChangedV NOTIFY sectionChanged)

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

    Docx::Document&       doc()       { return m_doc; }
    const Docx::Document& doc() const { return m_doc; }
    const DocxCursor&     cursor() const { return m_cursor; }

    Q_INVOKABLE void setCursor(int block, int pos, bool keepAnchor);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectWordAt(int block, int pos);
    Q_INVOKABLE void clearSelection();

    Q_INVOKABLE void insertText(const QString& text);   // \n = Absatz-Split
    Q_INVOKABLE void deleteBackward();                  // Backspace (Merge am Anfang)
    Q_INVOKABLE void deleteForward();                   // Entf (Merge am Ende)
    Q_INVOKABLE void insertParagraphBreak();            // Enter
    Q_INVOKABLE void insertLineBreak();                 // Shift+Enter (<w:br/>)
    //  "Ab hier unter allem weiter" (Word: Textumbruch mit `w:clear="all"`).
    //  Der Weg, neben einer gleitenden Tabelle bewusst wieder UNTER sie zu
    //  kommen - ohne ihn haengt der Text an ihrer Seite fest.
    Q_INVOKABLE void insertClearBreak();

    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void toggleUnderline();
    Q_INVOKABLE void setFontFamily(const QString& family);
    Q_INVOKABLE void setFontSizePt(qreal pt);
    Q_INVOKABLE void setTextColor(const QColor& c);

    Q_INVOKABLE void setAlignment(int align);            // 0 l · 1 z · 2 r · 3 Block
    Q_INVOKABLE void setLineSpacing(qreal multiple);      // 1.0/1.15/1.5/2.0 …
    Q_INVOKABLE void setSpaceBeforePt(qreal pt);
    Q_INVOKABLE void setSpaceAfterPt(qreal pt);
    Q_INVOKABLE void toggleBullets();
    Q_INVOKABLE void toggleNumbering();

    Q_INVOKABLE QVariantList paragraphStyles() const;
    Q_INVOKABLE void setParagraphStyle(const QString& styleId);

    Q_INVOKABLE void insertTable(int rows, int cols);
    Q_INVOKABLE void insertSignatureImage(const QString& fileUrl);
    Q_INVOKABLE void insertImage(const QString& fileUrl);
    Q_INVOKABLE void insertImageData(const QByteArray& bytes, const QString& ext,
                                     qint64 cxEmu = 0, qint64 cyEmu = 0);
    Q_INVOKABLE QVariantList folderImages() const;

    // Geprüft wird ABSATZWEISE und ASYNCHRON: ein `QRunnable` je Auftrag auf einem Pool mit EINEM Thread - die
    // Reihenfolge bleibt die Tippreihenfolge, und der GUI-Thread sieht nie ein Wörterbuch.
    Q_PROPERTY(bool spellAvailable READ spellAvailable NOTIFY spellChanged)
    Q_PROPERTY(QString spellLanguage READ spellLanguage NOTIFY spellChanged)
    bool    spellAvailable() const { return m_spellReady; }
    QString spellLanguage() const  { return m_spellLang; }
    const QVector<mg::SpellRange>& spellRanges(int block) const;
    int    spellWordAt(int block, int pos, mg::SpellRange* out) const;
    Q_INVOKABLE QStringList spellSuggestions(int block, int pos) const;
    Q_INVOKABLE bool spellHasIssueAt(int block, int pos) const {
        return spellWordAt(block, pos, nullptr) != 0;
    }
    Q_INVOKABLE int cursorBlock() const { return m_cursor.block; }
    Q_INVOKABLE int cursorPos() const   { return m_cursor.pos; }
    Q_INVOKABLE bool spellReplaceAt(int block, int pos, const QString& replacement);
    Q_INVOKABLE void spellIgnoreAt(int block, int pos);

    Q_INVOKABLE int  revisionAt(int block, int pos) const;
    Q_INVOKABLE QString revisionAuthorAt(int block, int pos) const;
    Q_INVOKABLE bool acceptRevisionAt(int block, int pos);
    Q_INVOKABLE bool rejectRevisionAt(int block, int pos);
    Q_INVOKABLE int  acceptAllRevisions();
    Q_INVOKABLE int  rejectAllRevisions();
    int  revisionCount() const { return m_revCount; }
    QString revisionAuthorsText() const { return m_revAuthors.join(QStringLiteral(", ")); }
    Q_INVOKABLE void setSpellCheckEnabled(bool on);
    Q_INVOKABLE void setSpellLanguage(const QString& lang);
    Q_INVOKABLE static QStringList spellLanguages() {
        return mg::SpellChecker::availableLanguages();
    }
    Q_INVOKABLE QString folderPath() const;
    Q_INVOKABLE int pdfPageCount(const QString& fileUrl) const;
    Q_INVOKABLE void insertPdfPage(const QString& fileUrl, int page);
    Q_INVOKABLE void insertTableOfContents();

    Q_INVOKABLE QVariantMap tableInfoAt(int block) const;
    Q_INVOKABLE void tableInsertRow(int tableId, int atRow);
    Q_INVOKABLE void tableDeleteRow(int tableId, int row);
    Q_INVOKABLE void tableInsertColumn(int tableId, int atCol);
    Q_INVOKABLE void tableDeleteColumn(int tableId, int col);
    Q_INVOKABLE void tableSetColumnWidthsMm(int tableId, const QVariantList& mm);
    Q_INVOKABLE void deleteTable(int tableId);
    Q_INVOKABLE void selectTable(int tableId);
    Q_INVOKABLE void scaleTableWidths(int tableId, qreal factor);

    Q_INVOKABLE QVariantMap imageInfoAt(int block) const;
    Q_INVOKABLE bool copyImageAtCursor();
    Q_INVOKABLE bool copyTableAtCursor();
    Q_INVOKABLE bool clipboardHasTable() const;
    Q_INVOKABLE void deleteImageAtCursor();
    Q_INVOKABLE void setImageSizeMm(int block, qreal widthMm, qreal heightMm);
    Q_INVOKABLE void setImageFloating(int block, bool floating);
    Q_INVOKABLE void setImagePositionMm(int block, qreal xMm, qreal yMm);
    Q_INVOKABLE void moveImageToBlock(int srcBlock, int dstBlock,
                                      qreal xMm, qreal yMm);
    Q_INVOKABLE void setImageWrapSide(int block, int side);

    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE QVariantMap findNext(const QString& needle, bool caseSensitive,
                                     bool backward);
    Q_INVOKABLE QVariantMap replaceAndFind(const QString& needle,
                                           const QString& replacement,
                                           bool caseSensitive);
    Q_INVOKABLE int replaceAll(const QString& needle, const QString& replacement,
                               bool caseSensitive);

    Q_INVOKABLE void save();
    Q_INVOKABLE void exportCopy();
    Q_INVOKABLE QString pdfExportTargetPath() const;
    Q_INVOKABLE void release();                          // Kachel wird verlassen

    Q_INVOKABLE QVariantMap currentFormat() const;

    Docx::RunFmt caretFormat() const;

    const Docx::SectionProps& section() const { return m_doc.section(); }

    // Randlineale
    //  Twips -> Millimeter: 1440 Twips = 1 Zoll = 25,4 mm.
    static constexpr qreal kTwipToMm = 25.4 / 1440.0;
    qreal pageWidthMm()    const { return m_doc.section().pageW * kTwipToMm; }
    qreal pageHeightMm()   const { return m_doc.section().pageH * kTwipToMm; }
    qreal marginTopMm()    const { return m_doc.section().marTop * kTwipToMm; }
    qreal marginRightMm()  const { return m_doc.section().marRight * kTwipToMm; }
    qreal marginBottomMm() const { return m_doc.section().marBottom * kTwipToMm; }
    qreal marginLeftMm()   const { return m_doc.section().marLeft * kTwipToMm; }
    bool  marginsChanged() const;
    bool  marginsChangedH() const;
    bool  marginsChangedV() const;

    Q_INVOKABLE bool setPageMarginsMm(qreal top, qreal right,
                                      qreal bottom, qreal left, int axis = 0);
    static constexpr int kStandardMarginTw = 1417;       // 2,5 cm
    Q_INVOKABLE bool resetPageMargins(int axis = 0);
    void applySectionState(const Docx::Document::SectionState& st);

    // Jedes Lineal führt seine EIGENE Undo-Kette, getrennt voneinander und von der des Dokuments. Welche gemeint ist,
    // entscheidet die zuletzt angefasste Stelle - sonst nähme Strg+Z nach einem Zug am Lineal den letzten Tippvorgang zurück.
    Q_PROPERTY(int rulerFocus READ rulerFocus NOTIFY rulerFocusChanged)
    int  rulerFocus() const { return m_rulerFocus; }
    Q_INVOKABLE void setRulerFocus(int axis);
    Q_PROPERTY(bool canUndoHere READ canUndoHere NOTIFY undoChanged)
    Q_PROPERTY(bool canRedoHere READ canRedoHere NOTIFY undoChanged)
    bool canUndoHere() const;
    bool canRedoHere() const;

    void applyBlocks(int first, int oldCount, const QList<Docx::Block>& blocks,
                     const DocxCursor& cur);
    void applyTableDef(int tableId, const Docx::TableDef& def);

signals:
    void sourceChanged();
    void readyChanged();
    void busyChanged();
    void modifiedChanged();
    void undoChanged();
    void formatRevChanged();
    void translitChanged();
    void blocksReplaced(int first, int oldCount, int newCount);
    void spellRangesChanged(int block);
    void spellChanged();
    void cursorChanged();
    void saveFinished(bool ok, const QString& target, const QString& error);
    void imageInsertFailed(const QString& error);
    void revisionsChanged();
    void sectionChanged();
    void rulerFocusChanged();

private:
    struct EditScope;                                    // s. cpp

    void bumpFormat();
    void setModified(bool m);
    bool tableStructOp(int tableId, const std::function<bool()>& op);
    bool applyRevisionAt(int block, int pos, bool accept);
    int  applyAllRevisions(bool accept);
    void refreshRevisions();
    int         m_revCount = 0;
    QStringList m_revAuthors;
    QString blockText(int i) const;
    QString selectionPlainText() const;
    void selectRange(int bi, int start, int len);
    int  blockLen(int i) const;
    bool isEditableParagraph(int i) const;
    bool sameCell(int i, int j) const;
    void clampRangeToCell(int b1, int& b2) const;
    bool deleteSelectedTable();
    bool cursorInToc() const;
    void applyTocCharFormat(int field, const QVariant& value);
    int  m_tableObjectSel = -1;
    void orderedSelection(int& b1, int& p1, int& b2, int& p2) const;
    void runAt(const Docx::Block& b, int pos, int* runIdx, int* runOfs) const;
    bool imageAtCursor(int* block, int* run, Docx::InlineImage* info) const;
    bool selectedImage(int block, int* blockOut, int* runOut) const;
    void insertImageBytes(const QByteArray& bytes, const QString& ext,
                          qint64 cxEmu, qint64 cyEmu);
    int  ensureRunBoundary(Docx::Block& b, int pos) const;
    void removeRangeInBlock(Docx::Block& b, int p1, int p2) const;
    // Angrenzende Runs mit identischem rPr wieder zu EINEM zusammenfassen: `ensureRunBoundary` teilt bei jedem
    // Eingriff, ohne diesen Lauf stünde in der Datei ein `<w:r>` je getipptem Zeichen. Nur auf DIRTY Blöcken.
    int  coalesceRuns(Docx::Block& b) const;
    int  splitParagraphAt(int blockIdx, int pos, bool dropBreakAtPos);
    int  splitOffLines(int blockIdx, int from, int to);
    void applyPendingTo(Docx::Run& r) const;
    void clearPending();
    Docx::RunFmt resolvedFormatAt(int block, int pos) const;
    // Word-Verhalten nach dem Löschen des letzten Zeichens: die Zeile behält ihre eigene Formatierung, statt auf das
    // Stil-Format zurückzufallen. Gesetzt wird nur, was sich vom Stil-Format des leeren Absatzes unterscheidet.
    void keepFormatOnEmptiedBlock(int bi, const Docx::RunFmt& had);
    void insertRunParagraphs(const QList<QList<Docx::Run>>& paras);
    QByteArray serializeSelection() const;
    bool pasteTableBlob(const QByteArray& blob);
    static bool deserializeRuns(const QByteArray& blob,
                                QList<QList<Docx::Run>>* out);
    QString selectionAsHtml() const;
    void applyCharFormat(int field, const QVariant& value);
    void applyParProp(const QString& propName, const QString& newXml,
                      const std::function<void(Docx::ParFmt&)>& mut);
    void toggleList(bool bullet);
    void runTranslit();
    QString exportTargetPath() const;
    void startSaveWorker(const QString& targetPath, bool direct);

    Docx::Document m_doc;
    QString    m_source;
    QString    m_loadError;
    bool       m_ready = false;
    bool       m_busy = false;
    bool       m_modified = false;
    // Bewusst EIGENE Stapel neben `m_stack`: ein Randwechsel gehört nicht in die Historie des TEXTES, und die beiden
    // Lineale sollen einander nicht ins Handwerk pfuschen.
    QUndoStack&       activeStack();
    const QUndoStack& activeStack() const;
    QUndoStack m_hMarginStack;
    QUndoStack m_vMarginStack;
    int        m_rulerFocus = 0;         // 0 Text · 1 waagerecht · 2 senkrecht
    bool       m_bakDone = false;        // .bak einmalig je Sitzung
    // Die `.bak` hält nur den Stand VOR den Änderungen DIESER Sitzung und wird beim Verlassen entfernt; bei
    // nachverfolgten Änderungen bleibt sie. Das Speichern läuft asynchron - deshalb ein Merker statt Löschen.
    bool       m_dropBakOnFinish = false;
    //  Gilt NUR fuer das Speichern beim Verlassen: der Schreib-Worker raeumt
    //  die `.bak` danach selbst weg. Ein Rueckruf hierher genuegt nicht - die
    //  Kachel ist dann meist schon abgebaut (s. `release`).
    bool       m_dropBakAfterSave = false;
    void       dropBackupIfIdle();
    int        m_formatRev = 0;
    int        m_loadGen = 0;            // Generationszähler async Laden
    DocxCursor m_cursor;
    QUndoStack m_stack;
    QObject*   m_translit = nullptr;
    Docx::RunFmt m_pending;
    int        m_pendingBlock = -1;
    int        m_pendingPos   = -1;

    // Eigener Pool mit EINEM Thread: die Aufträge laufen in der Reihenfolge, in der getippt wurde, und der Cache
    // bleibt konsistent. Der `SpellChecker` gehört diesem Pool-Thread und wird dort erzeugt.
    void   spellStart();                 // Wörterbuch (neu) öffnen
    void   spellInvalidate(int first, int count);   // Absätze neu prüfen
    void   spellRequest(int block);      // einen Absatz einreihen
public:
    void   spellResult(int block, int gen, const QVector<mg::SpellRange>& bad);
private:
    QThreadPool* m_spellPool = nullptr;
    std::shared_ptr<mg::SpellChecker> m_spell;      // nur im Pool-Thread benutzt
    QHash<int, QVector<mg::SpellRange>> m_spellBad; // Absatz -> Fundstellen
    QSet<int>  m_spellPending;           // eingereiht, Ergebnis steht aus
    QSet<int>  m_spellStale;             // waehrenddessen erneut geaendert
    QStringList m_spellIgnored;          // Sitzungswörter (GUI-Thread hält sie)
    bool       m_spellOn = false;
    bool       m_spellReady = false;
    QString    m_spellLang;
    QString    m_spellWanted;      // gewünschte Sprache (aus den Einstellungen)
    int        m_spellGen = 0;           // verwirft Ergebnisse alter Dokumente
};
