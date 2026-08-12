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
    //  Format am Cursor/der Selektion (Toolbar) — rev-getrieben wie boxInfo
    //  im PDF-Editor: bei jeder Cursor-/Formatänderung inkrementiert.
    Q_PROPERTY(int formatRev READ formatRev NOTIFY formatRevChanged)
    //  Änderungsverfolgung: Anzahl und Autoren der nachverfolgten Änderungen.
    Q_PROPERTY(int revisionCount READ revisionCount NOTIFY revisionsChanged)
    //  Autoren der nachverfolgten Änderungen als fertiger Text („A, B“).
    //  Ohne diese Property las der Streifen `editCtl.revisionAuthors` — die
    //  es nie gab: QML meldete „Cannot read property 'length' of undefined“
    //  und die Zeile blieb ohne Autoren stehen.
    Q_PROPERTY(QString revisionAuthorsText READ revisionAuthorsText NOTIFY revisionsChanged)
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
    //  "Ab hier unter allem weiter" (Word: Textumbruch mit `w:clear="all"`).
    //  Der Weg, neben einer gleitenden Tabelle bewusst wieder UNTER sie zu
    //  kommen — ohne ihn haengt der Text an ihrer Seite fest.
    Q_INVOKABLE void insertClearBreak();

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
    //  UNTERSCHRIFT/STEMPEL: dasselbe wie `insertImage`, aber das Bild wird
    //  sofort als VERANKERTES, umflossenes Bild eingesetzt (Word:
    //  `wp:anchor` + `w:wrapSquare`) und ausgewaehlt — es laesst sich also
    //  gleich frei auf der Seite ziehen. EIN Undo-Schritt fuer beides.
    Q_INVOKABLE void insertSignatureImage(const QString& fileUrl);
    Q_INVOKABLE void insertImage(const QString& fileUrl);
    //  Bild aus BYTES einfügen (Zwischenablage): `ext` ist die Zielendung
    //  ("png"/"jpg"/…) und bestimmt Content-Type und Teilname im Container.
    //  `cxEmu`/`cyEmu` > 0 = Anzeigegröße vorgeben (Einfügen eines im Editor
    //  kopierten Bildes); 0 = aus den nativen Pixeln rechnen.
    Q_INVOKABLE void insertImageData(const QByteArray& bytes, const QString& ext,
                                     qint64 cxEmu = 0, qint64 cyEmu = 0);
    //  Bilder im ORDNER der geöffneten Datei — als [{name, url}] für das
    //  Auswahl-Popup. Filter ist QImageReader::supportedImageFormats(), also
    //  jedes Format, das Qt lesen kann (keine feste Endungsliste).
    Q_INVOKABLE QVariantList folderImages() const;

    //  ── Rechtschreib-PRÜFUNG (unterkringeln + Vorschläge) ────────────────────
    //  Geprüft wird ABSATZWEISE und ASYNCHRON (Regel 8): ein `QRunnable` je
    //  Auftrag auf einem eigenen Pool mit EINEM Thread — die Reihenfolge bleibt
    //  damit die Tippreihenfolge, und der GUI-Thread sieht nie ein Wörterbuch.
    //  Der Text wird NIE von selbst geändert; „Korrektur" ist ein Menüpunkt.
    Q_PROPERTY(bool spellAvailable READ spellAvailable NOTIFY spellChanged)
    Q_PROPERTY(QString spellLanguage READ spellLanguage NOTIFY spellChanged)
    bool    spellAvailable() const { return m_spellReady; }
    QString spellLanguage() const  { return m_spellLang; }
    //  Falsch geschriebene Stellen eines Absatzes (leer, solange der Auftrag
    //  läuft oder die Prüfung aus ist). Leser ist `DocxTextArea` beim Zeichnen.
    const QVector<mg::SpellRange>& spellRanges(int block) const;
    int    spellWordAt(int block, int pos, mg::SpellRange* out) const;
    //  Vorschläge für das Wort an einer Stelle (Kontextmenü). Leer, wenn dort
    //  kein beanstandetes Wort steht.
    Q_INVOKABLE QStringList spellSuggestions(int block, int pos) const;
    //  Steht an dieser Stelle überhaupt ein beanstandetes Wort? (Das Menü
    //  zeigt „Ignorieren" auch dann, wenn es keine Vorschläge gibt.)
    Q_INVOKABLE bool spellHasIssueAt(int block, int pos) const {
        return spellWordAt(block, pos, nullptr) != 0;
    }
    //  Cursorstelle für QML — das Kontextmenü fragt genau dort nach.
    Q_INVOKABLE int cursorBlock() const { return m_cursor.block; }
    Q_INVOKABLE int cursorPos() const   { return m_cursor.pos; }
    //  Das beanstandete Wort an einer Stelle durch `replacement` ersetzen —
    //  EIN Undo-Schritt, wie jede andere Textänderung.
    Q_INVOKABLE bool spellReplaceAt(int block, int pos, const QString& replacement);
    //  Für diese Sitzung durchgehen lassen (alle Absätze werden neu geprüft).
    Q_INVOKABLE void spellIgnoreAt(int block, int pos);

    //  ── Änderungsverfolgung annehmen / verwerfen ─────────────────────────────
    //  An der Cursorstelle: `revisionAt` sagt, was dort steht (0 = nichts,
    //  1 = Einfügung, 2 = Löschung) — das Kontextmenü zeigt die Einträge nur
    //  dann. `acceptRevisionAt`/`rejectRevisionAt` sind EIN Undo-Schritt.
    Q_INVOKABLE int  revisionAt(int block, int pos) const;
    Q_INVOKABLE QString revisionAuthorAt(int block, int pos) const;
    Q_INVOKABLE bool acceptRevisionAt(int block, int pos);
    Q_INVOKABLE bool rejectRevisionAt(int block, int pos);
    //  ALLE Änderungen des Dokuments in EINEM Undo-Schritt; Rückgabe = Anzahl.
    Q_INVOKABLE int  acceptAllRevisions();
    Q_INVOKABLE int  rejectAllRevisions();
    //  Zusammenfassung für den Hinweisstreifen: wie viele Änderungen stehen im
    //  Dokument (aufeinanderfolgende Runs derselben Art und desselben Autors
    //  zählen als EINE) und von wem. Ohne diese Anzeige sah ein Dokument mit
    //  Änderungen aus wie eines ohne — der Nutzer wusste nicht, was die
    //  Markierungen bedeuten (Befund N5).
    int  revisionCount() const { return m_revCount; }
    QString revisionAuthorsText() const { return m_revAuthors.join(QStringLiteral(", ")); }
    //  Prüfung an/aus (folgt der Einstellung, QML schaltet sie um).
    Q_INVOKABLE void setSpellCheckEnabled(bool on);
    //  Wörterbuch-Kürzel („de_DE"); leer = die erste gefundene Sprache.
    Q_INVOKABLE void setSpellLanguage(const QString& lang);
    //  Welche Wörterbücher liegen auf diesem Rechner? (Einstellungsdialog.)
    Q_INVOKABLE static QStringList spellLanguages() {
        return mg::SpellChecker::availableLanguages();
    }
    //  Ordner der geöffneten Datei (Pfad, leer wenn nichts geladen) — der
    //  PDF-Seitenwähler scannt damit denselben Ordner wie folderImages().
    Q_INVOKABLE QString folderPath() const;
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
    //  Ganze Tabelle AUSWÄHLEN (erster bis letzter Zellblock). Danach löscht
    //  Entf/Rücktaste sie — der Tastaturweg zum Entfernen einer Tabelle.
    Q_INVOKABLE void selectTable(int tableId);
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
    //  Ganze Tabelle am Cursor in die Zwischenablage (eigener Typ + Klartext);
    //  false, wenn der Cursor in keiner Tabelle steht oder die Tabelle
    //  verbundene Zellen hat. Wird von copy()/cut() ohne Textauswahl benutzt.
    Q_INVOKABLE bool copyTableAtCursor();
    //  Liegt eine mit copyTableAtCursor() abgelegte Tabelle in der
    //  Zwischenablage? (Kontextmenü: „Tabelle einfügen" nur dann zeigen.)
    Q_INVOKABLE bool clipboardHasTable() const;
    Q_INVOKABLE void deleteImageAtCursor();
    //  Neue Größe in Millimetern (undo-fähig; Seitenverhältnis hält der
    //  Aufrufer, damit Ziehen an einer Kante bewusst verzerren darf).
    Q_INVOKABLE void setImageSizeMm(int block, qreal widthMm, qreal heightMm);
    //  Umbruchart des ausgewählten Bildes: false = mit dem Text in der Zeile
    //  (`wp:inline`), true = umfließend (`wp:anchor` + `w:wrapSquare`).
    Q_INVOKABLE void setImageFloating(int block, bool floating);
    //  LAGE eines umfließenden Bildes in Millimetern, relativ zur linken
    //  Textkante und zur Oberkante seines Absatzes — das schreibt das Ziehen.
    Q_INVOKABLE void setImagePositionMm(int block, qreal xMm, qreal yMm);
    //  Dasselbe, aber der Anker wechselt den ABSATZ (Word hängt ein abgelegtes
    //  Bild an den Absatz, über dem es liegt — erst dadurch umfließt dessen
    //  Text es). `yMm` zählt ab der Oberkante des ZIELabsatzes. Umhängen und
    //  neue Lage sind EIN Undo-Schritt. Den Zielabsatz bestimmt die Anzeige
    //  (`DocxTextArea::dropSelectedImage`), sie kennt die Geometrie.
    Q_INVOKABLE void moveImageToBlock(int srcBlock, int dstBlock,
                                      qreal xMm, qreal yMm);
    //  Umbruchseite: 0 = beide, 1 = nur links, 2 = nur rechts, 3 = breitere
    //  Seite (`Docx::InlineImage::WrapSide`).
    Q_INVOKABLE void setImageWrapSide(int block, int side);

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
    //  Zielpfad des PDF-Exports: <Name>.pdf neben der Quelle, bei Kollision
    //  „<Name> (2).pdf". MUSS öffentlich stehen — `Q_INVOKABLE` allein genügt
    //  NICHT: aus dem privaten Teil heraus meldet QML „is not a function", und
    //  der Aufruf scheitert STILL (der Knopf tat dann gar nichts).
    Q_INVOKABLE QString pdfExportTargetPath() const;
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

    //  Seiteneinrichtung des Dokuments.
    const Docx::SectionProps& section() const { return m_doc.section(); }

    //  Interner Anwender der Kommandos (public für DocxReplaceBlocksCommand).
    void applyBlocks(int first, int oldCount, const QList<Docx::Block>& blocks,
                     const DocxCursor& cur);
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
    //  Die geprüften Stellen eines Absatzes liegen vor (oder die Prüfung wurde
    //  an-/abgeschaltet: `block < 0` = alles neu zeichnen).
    void spellRangesChanged(int block);
    void spellChanged();
    void cursorChanged();
    void saveFinished(bool ok, const QString& target, const QString& error);
    //  Ergebnis des DOCX→PDF-Exports (Aufgabe 2).
    void pdfExportFinished(bool ok, const QString& target, const QString& error);
    void imageInsertFailed(const QString& error);
    //  Zahl/Autoren der nachverfolgten Änderungen haben sich geändert.
    void revisionsChanged();

private:
    struct EditScope;                                    // s. cpp

    void bumpFormat();
    void setModified(bool m);
    //  Gemeinsamer Rahmen der Tabellen-Struktur-Operationen: EditScope über die
    //  ganze Tabelle + Gerüst-Schnappschuss, dann `op`. Liefert false, wenn die
    //  Operation abgelehnt wurde (dann entsteht auch kein Undo-Schritt).
    bool tableStructOp(int tableId, const std::function<bool()>& op);
    //  Gemeinsamer Kern von accept/rejectRevisionAt bzw. …AllRevisions.
    bool applyRevisionAt(int block, int pos, bool accept);
    int  applyAllRevisions(bool accept);
    //  Zahl/Autoren neu bestimmen (nach Laden und nach jeder Änderung).
    void refreshRevisions();
    int         m_revCount = 0;
    QStringList m_revAuthors;
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
    //  Deckt die Selektion GENAU eine ganze Tabelle ab? Dann löschen
    //  Entf/Rücktaste sie als Ganzes (statt nur die erste Zelle zu leeren).
    bool deleteSelectedTable();
    //  Cursor in einem Inhaltsverzeichnis? Dort ist nur Schriftart und
    //  -größe einstellbar, getippt wird nicht.
    bool cursorInToc() const;
    void applyTocCharFormat(int field, const QVariant& value);
    //  Tabelle, die per selectTable() als OBJEKT ausgewählt ist (−1 = keine).
    //  Jede Cursor-Bewegung löscht den Zustand wieder.
    int  m_tableObjectSel = -1;
    //  Ordnet (block,pos) so, dass (b1,p1) ≤ (b2,p2).
    void orderedSelection(int& b1, int& p1, int& b2, int& p2) const;
    //  Run-Index + Offset im Run zu einer Absatzposition.
    void runAt(const Docx::Block& b, int pos, int* runIdx, int* runOfs) const;
    //  Bild AM CURSOR — dieselbe Regel wie in der Anzeige: entweder der Absatz
    //  besteht nur aus diesem Bild, oder die Selektion deckt genau sein
    //  Objekt-Zeichen (so wählt ein Klick ein Bild im Fließtext aus).
    bool imageAtCursor(int* block, int* run, Docx::InlineImage* info) const;
    //  Block+Run des gemeinten Bildes nach derselben Regel — Lage/Umbruchseite
    //  brauchen die Auskunft ohne die `InlineImage` selbst.
    bool selectedImage(int block, int* blockOut, int* runOut) const;
    //  Gemeinsamer Weg von insertImage/insertImageData/paste: das Bild kommt
    //  AN DIE CURSOR-STELLE in den laufenden Absatz (wie in Word) — nur so
    //  können zwei Bilder nebeneinander und Text daneben stehen.
    void insertImageBytes(const QByteArray& bytes, const QString& ext,
                          qint64 cxEmu, qint64 cyEmu);
    //  Stellt eine Run-Grenze bei pos her (teilt bei Bedarf); liefert den
    //  Index des Runs, der BEI pos beginnt.
    int  ensureRunBoundary(Docx::Block& b, int pos) const;
    //  Löscht [p1,p2) innerhalb EINES Blocks (Run-bewusst, opake atomar).
    void removeRangeInBlock(Docx::Block& b, int p1, int p2) const;
    //  Absatz an `pos` teilen: alles ab dort wandert in einen NEUEN Absatz
    //  DAHINTER (gleiche Zelle, gleiches pPr/pfmt). Steht an `pos` ein
    //  `w:br`-Zeichen, wird es dabei geschluckt — aus dem Zeilenumbruch wird
    //  die Absatzgrenze. Liefert den Index des neuen Absatzes.
    int  splitParagraphAt(int blockIdx, int pos, bool dropBreakAtPos);
    //  Zeilenbereich [from,to) eines `w:br`-Absatzes als EIGENEN Absatz
    //  herauslösen (0…2 Teilungen). Liefert dessen Index, −1 wenn nichts zu tun
    //  ist (kein Zeilenumbruch, oder der Bereich ist schon der ganze Absatz).
    int  splitOffLines(int blockIdx, int from, int to);
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
    //  Tabelle aus dem eigenen Zwischenablage-Typ einsetzen (EIN Undo-Schritt);
    //  false bei fremdem/defektem Blob — dann greifen die übrigen Formate.
    bool pasteTableBlob(const QByteArray& blob);
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
    void startSaveWorker(const QString& targetPath, bool direct);

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

    //  ── Rechtschreibprüfung ──────────────────────────────────────────────────
    //  Eigener Pool mit EINEM Thread (Regel 8): die Aufträge laufen in der
    //  Reihenfolge, in der getippt wurde, und der Cache bleibt konsistent.
    //  Der `SpellChecker` gehört DIESEM Pool-Thread — er wird dort erzeugt.
    void   spellStart();                 // Wörterbuch (neu) öffnen
    void   spellInvalidate(int first, int count);   // Absätze neu prüfen
    void   spellRequest(int block);      // einen Absatz einreihen
public:
    //  Rückmeldung des Prüf-Auftrags (aus dem Pool via QueuedConnection).
    void   spellResult(int block, int gen, const QVector<mg::SpellRange>& bad);
private:
    QThreadPool* m_spellPool = nullptr;
    std::shared_ptr<mg::SpellChecker> m_spell;      // nur im Pool-Thread benutzt
    QHash<int, QVector<mg::SpellRange>> m_spellBad; // Absatz → Fundstellen
    QSet<int>  m_spellPending;           // eingereiht, Ergebnis steht aus
    QStringList m_spellIgnored;          // Sitzungswörter (GUI-Thread hält sie)
    bool       m_spellOn = false;
    bool       m_spellReady = false;
    QString    m_spellLang;
    QString    m_spellWanted;      // gewünschte Sprache (aus den Einstellungen)
    int        m_spellGen = 0;           // verwirft Ergebnisse alter Dokumente
};
