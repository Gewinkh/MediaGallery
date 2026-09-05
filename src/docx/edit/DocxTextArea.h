#pragma once
// DocxTextArea - Anzeige- und Eingabeflaeche des DOCX-Editors als
// QQuickPaintedItem mit eigenem Absatz-/Zeilenmodell. QTextLayout dient nur
// als Shaping- und Umbruch-Primitiv darunter.

#include <QQuickPaintedItem>
#include <QImage>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTimer>
#include <QVector>
#include <memory>
#include <unordered_map>
#include <vector>

// Vollstaendiger Typ noetig: Q_PROPERTY registriert den Metatyp im moc-Code.
#include "docx/edit/DocxEditController.h"

class DocxTextArea : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(DocxEditController* ctl READ ctl WRITE setCtl NOTIFY ctlChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
    // Caret-Lage in Item-Pixeln; QML scrollt das Rechteck sichtbar.
    Q_PROPERTY(qreal cursorX READ cursorX NOTIFY cursorRectChanged)
    Q_PROPERTY(qreal cursorY READ cursorY NOTIFY cursorRectChanged)
    Q_PROPERTY(qreal cursorH READ cursorH NOTIFY cursorRectChanged)
    // Das Mausrad rechnet seine Schrittweite daraus - ein Textdokument scrollt in
    // Zeilen, nicht in Bruchteilen der Fensterhoehe.
    Q_PROPERTY(qreal lineStep READ lineStep NOTIFY contentHeightChanged)
    Q_PROPERTY(QString tablePlaceholder MEMBER m_tablePlaceholder NOTIFY labelsChanged)
    Q_PROPERTY(QString pageBreakLabel MEMBER m_pageBreakLabel NOTIFY labelsChanged)
    Q_PROPERTY(QString tocEmptyLabel MEMBER m_tocEmptyLabel NOTIFY labelsChanged)
    // Grund um die Seite herum; die Seite selbst bleibt immer weiss, wie in Word.
    Q_PROPERTY(QColor surroundColor MEMBER m_surroundColor NOTIFY labelsChanged)
    // Position: 0 aus, 1 links, 2 mittig, 3 rechts. Stil: 0 = "3", 1 = "3 / 12".
    // Dieselbe Angabe bekommt der PDF-Export.
    Q_PROPERTY(int pageNumberPos READ pageNumberPos WRITE setPageNumberPos
               NOTIFY pageNumberChanged)
    Q_PROPERTY(int pageNumberStyle READ pageNumberStyle WRITE setPageNumberStyle
               NOTIFY pageNumberChanged)
    // Die Lineale fragen, wo die Seite steht, statt die Massstabsrechnung
    // nachzubauen - sonst liefen beide auseinander.
    Q_PROPERTY(qreal pageOffsetX  READ pageOffsetX  NOTIFY pageGeometryChanged)
    Q_PROPERTY(qreal pageWidthPx  READ pageWidthPx  NOTIFY pageGeometryChanged)
    Q_PROPERTY(qreal pageHeightPx READ pageHeightPx NOTIFY pageGeometryChanged)
    // Das senkrechte Lineal meint immer die Seite, die man vor sich hat.
    Q_PROPERTY(qreal currentPageTopPx READ currentPageTopPx NOTIFY pageGeometryChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageCountChanged)
    // Fuer Pruefstaende: auf eine stabile Seitenzahl zu warten lief bei kurzen
    // Dokumenten ins Zeitlimit, weil sie nie von 1 abweicht.
    Q_PROPERTY(bool layoutBusy READ layoutBusy NOTIFY layoutBusyChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    // Kein zweiter Auswahlzustand - ein Klick aufs Bild setzt den Cursor dorthin,
    // das IST die Auswahl. Rechteck in Item-Pixeln fuer die Ziehpunkte.
    Q_PROPERTY(int selImageBlock READ selImageBlock NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageX READ selImageX NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageY READ selImageY NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageW READ selImageW NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageH READ selImageH NOTIFY imageSelectionChanged)
    // selTableRects traegt eines je Seitenstueck: eine getrennte Tabelle liegt auf
    // mehreren Seiten und laesst sich nie durch ein Rechteck fassen.
    Q_PROPERTY(int selTableId READ selTableId NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selTableX READ selTableX NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selTableY READ selTableY NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selTableW READ selTableW NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selTableH READ selTableH NOTIFY imageSelectionChanged)
    Q_PROPERTY(QVariantList selTableRects READ selTableRects NOTIFY imageSelectionChanged)

public:
    explicit DocxTextArea(QQuickItem* parent = nullptr);
    ~DocxTextArea() override;

    DocxEditController* ctl() const { return m_ctl; }
    void setCtl(DocxEditController* c);

    qreal contentY() const { return m_contentY; }
    void  setContentY(qreal y);
    qreal contentHeight() const { return m_contentHeight; }
    // m_cursorRect liegt in Dokument-Pixeln, QML rechnet in Item-Pixeln.
    qreal cursorX() const { return m_cursorRect.x() * m_scale; }
    qreal cursorY() const { return m_cursorRect.y() * m_scale; }
    qreal cursorH() const { return m_cursorRect.height() * m_scale; }
    qreal lineStep() const;

    int   pageNumberPos() const { return m_pageNumberPos; }
    void  setPageNumberPos(int p);
    int   pageNumberStyle() const { return m_pageNumberStyle; }
    void  setPageNumberStyle(int s);
    int   pageCount() const { return m_pageCount; }
    bool  layoutBusy() const { return m_chunkTimer.isActive(); }
    int   currentPage() const;

    int   selImageBlock() const;
    qreal selImageX() const;
    qreal selImageY() const;
    qreal selImageW() const;
    qreal selImageH() const;
    int   selTableId() const;
    qreal selTableX() const;
    qreal selTableY() const;
    qreal selTableW() const;
    qreal selTableH() const;
    // [{x,y,w,h}] in Item-Pixeln, ein Eintrag je Seitenstueck.
    QVariantList selTableRects() const;
    Q_INVOKABLE qreal pageTop(int page);
    // In Item-Koordinaten, also abzueglich contentY - daran haengt das Lineal.
    Q_INVOKABLE qreal pageTopItem(int page);
    qreal pageOffsetX()  const { return itemOffsetX(); }
    qreal pageWidthPx()  const;
    qreal pageHeightPx() const;
    qreal currentPageTopPx() const;
    Q_INVOKABLE int pageOfBlock(int i);
    // Ein Ueberschrift-Absatz kann mehrere Eintraege tragen und ueber eine
    // Seitengrenze laufen; pageOfBlock waere dann fuer alle ausser dem ersten falsch.
    Q_INVOKABLE int pageOfEntry(int i, int pos);
    // Liegt die Oberkante nicht mehr ueber dem Text seines Absatzes, wandert der
    // Anker in den Absatz darunter - erst dadurch umfliesst dessen Text es.
    Q_INVOKABLE void dropSelectedImage(int block, qreal xMm, qreal yMm);

    // Nutzt denselben Zeichenweg wie paint, es gibt keine zweite Darstellung.
    // withPaperFrame beim PDF-Export AUS - dort IST die Seite das Papier.
    void paintPageInto(QPainter* p, int page, const QRectF& target,
                       bool withPaperFrame = true, bool withSelection = true);

    // Schreibt jede Seite so, wie sie am Bildschirm steht: derselbe Zeichenweg,
    // dieselben Seitengrenzen. Damit ist das PDF seitengleich per Konstruktion -
    // kein zweites Layout, das auseinanderlaufen koennte.
    Q_INVOKABLE QString exportPagesToPdf(const QString& targetPath,
                                         int pageNumberPos = 0,
                                         int pageNumberStyle = 1);

    void paint(QPainter* p) override;

signals:
    void ctlChanged();
    void contentYChanged();
    void contentHeightChanged();
    void cursorRectChanged();
    void labelsChanged();
    void saveRequested();
    void pageCountChanged();
    void layoutBusyChanged();
    void pageGeometryChanged();
    void pageNumberChanged();
    void currentPageChanged();
    void documentChanged();
    void imageSelectionChanged();
    // Position in Item-Pixeln; das Menue baut QML, hier gibt es keine UI-Texte.
    void contextMenuRequested(qreal x, qreal y, int block);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    // Einzeltasten-Kuerzel der App duerfen die Texteingabe nicht kapern - diese
    // selbstgebaute Flaeche muss ShortcutOverride annehmen.
    bool event(QEvent* e) override;
    void inputMethodEvent(QInputMethodEvent* e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    void geometryChange(const QRectF& n, const QRectF& o) override;

private:
    // Fluss-y ist fortlaufend und monoton, unabhaengig vom Fenster. Ein Slot ist
    // der Textbereich einer Spalte einer Seite; Fluss-y = slot * slotHeight() + y
    // im Slot. So bleibt die binaere Suche ueber die Praefix-Offsets gueltig.
    struct PageSeg {
        int   slot    = 0;                     // Spalten-Slot dieses Stücks
        // Drei Bedeutungen je nach Block: erste Zeile, erster Verzeichnis-Eintrag oder
        // erste Tabellenzeile. Nie direkt lesen - dafuer gibt es segFirst*().
        int   first   = 0;
        qreal yInSlot = 0.0;                   // Oberkante im Slot
    };
    // Eine Tabelle bleibt ein Block und wird byteidentisch gespeichert, aber als
    // echtes Gitter ausgelegt: mit festem Platzhalter war jeder Umbruch danach falsch.
    struct CellLayout {
        // Nur fuer nicht flach zerlegte Tabellen; sonst kommt der Text aus den Bloecken.
        std::vector<std::unique_ptr<QTextLayout>> paras;
        std::vector<bool> paraIsPlaceholder;   // verschachtelte Tabelle in der Zelle
        qreal x = 0.0, w = 0.0, h = 0.0;
    };
    struct RowLayout {
        std::vector<CellLayout> cells;
        qreal y = 0.0, h = 0.0;
    };
    struct TableLayout {
        std::vector<RowLayout> rows;
        qreal width = 0.0;
    };

    // Ein Absatz ohne Bild hat genau ein Stueck, mit Bildern eines je Abschnitt
    // dazwischen. Das Objekt-Zeichen U+FFFC gehoert keinem Stueck - QTextLayout
    // malte daraus sonst ein Kaestchen.
    struct Piece {
        std::unique_ptr<QTextLayout> lay;
        qreal dx = 0.0, dy = 0.0;
        int   textStart = 0;                   // Zeichenposition im Absatz
    };
    // Gehalten wird nur das eingepasste QImage; die Kastenmasse ueberleben das Trimmen.
    struct ImageBox {
        QImage  img;
        QString relId;
        qreal   x = 0.0, y = 0.0, w = 0.0, h = 0.0;   // block-lokal
        int     pos = 0;                       // Zeichenindex des U+FFFC
        int     run = -1;                      // Run-Index im Block
        bool    broken = false;                // Bytes/Beziehung unbrauchbar
        // Verankert: das Bild steht nicht im Zeilenfluss, der Text weicht ihm aus.
        // padL/padR sind die Abstaende, die das Dokument verlangt (distL/distR).
        bool    floating = false;
        qreal   padL = 0.0, padR = 0.0;
        int     wrapSide = 0;
    };
    // Ein Zeilenband ist die Einheit von Paginierung, Caret und Suche: hoechstens
    // eine Textzeile plus die Bilder daneben.
    struct RowInfo {
        qreal y = 0.0;                         // Oberkante (block-lokal)
        qreal h = 0.0;                         // Vorschub bis zum nächsten Band
        qreal visH = 0.0;                      // sichtbare Höhe (ohne Durchschuss)
        // Neben einem Bild sitzt die Textzeile an dessen Unterkante, das Band beginnt
        // aber oben - Caret und Listenmarker folgen der ZEILE, nicht dem Band.
        qreal textDy = 0.0;
        qreal ascent = 0.0;
        int   charStart = 0, charEnd = 0;      // Zeichenspanne im Absatz
        int   piece = -1, line = 0;            // Textzeile (piece < 0 = nur Bilder)
        int   imgFirst = 0, imgCount = 0;      // Bilder dieses Bandes
    };

    struct BlockLayout {
        // Alles ueber die Facade lesen, nie direkt.
        std::vector<Piece>    pieces;
        std::vector<ImageBox> images;
        std::vector<RowInfo>  rows;
        int     textLen = 0;                   // Zeichen des Absatzes
        bool    isText  = false;               // Block ist ein ausgelegter Absatz
        // Einmal beim Auslegen bestimmt; frueher lief dafuer je Block und Tastendruck
        // ein voller Suchlauf ueber den zusammengesetzten Absatztext.
        bool    hasBreak = false;
        bool    trimmed = false;
        std::unique_ptr<TableLayout> table;    // nur bei w:tbl (s. isTable)
        bool    isTable = false;               // Block ist eine deutbare Tabelle
        bool    isImage = false;
        // Die Eintraege stehen nicht in der Datei - das Feld bleibt deklarativ. Die
        // Seitenzahl fuellt erst paint ein, deshalb braucht es keinen zweiten Pass.
        bool    isToc = false;
        QList<Docx::TocEntry> tocEntries;
        qreal   tocLineH = 0.0;
        QFont   tocFont;
        int     tocPerPage = 0;
        // Die ganze Tabelle haengt am Anker, nur er traegt Hoehe - so bleibt der Fluss
        // monoton. Die Lage steht deshalb explizit relativ zum Anker.
        bool    isCell = false;
        qreal   cellRelX = 0.0, cellRelY = 0.0, cellW = 0.0;
        // Entscheidet, auf welchem Stueck der Tabelle und damit auf welcher Seite er liegt.
        int     cellRow = 0;
        qreal   height = 0.0;                  // inkl. Abstand davor/danach
        // Wie weit verankerte Bilder unter die Unterkante ragen - darum fliessen die
        // FOLGENDEN Absaetze herum; in Word endet der Umfluss nicht am Absatz.
        qreal   floatOverhang = 0.0;
        // Gleitende Tabelle: gibt ihre Flusshoehe ab und wird fuer die folgenden
        // Absaetze zum Stoerer. Nur wenn ein lesbarer Streifen bleibt und sie auf
        // eine Seite passt.
        bool    tableFloating = false;
        // Versatz fuer Bloecke, die nicht umfliessen koennen (in height enthalten).
        qreal   topPad = 0.0;
        qreal   beforePx = 0.0;                // Versatz bis zur ersten Zeile
        qreal   indentPx = 0.0;                // Listen-Einzug
        QString marker;                        // "• " / "3. " (Listen)
        bool    laid = false;                  // height ist ECHT (nicht Schätzung)
        // Leer = ein Stueck im Slot von blockSlot(i); mehr nur bei Seitengrenzen.
        QVector<PageSeg> segs;
    };

    // Die einzige Lesesicht auf die Zeilen eines Blocks - nie direkt an
    // pieces/images. Zeile heisst hier Zeilenband (RowInfo).
    struct LineRef {
        const QTextLayout* lay = nullptr;      // Stück, in dem die Zeile liegt
        int   line = -1;                       // Zeilenindex IN diesem Stück
        qreal dx = 0.0, dy = 0.0;              // Lage des Stücks im Block
        int   textStart = 0;                   // Zeichenversatz des Stücks
        bool  valid() const { return lay && line >= 0; }
        QTextLine textLine() const { return lay->lineAt(line); }
    };
    bool    hasText(const BlockLayout& L) const;    // Block ist ein Absatz
    int     textLength(const BlockLayout& L) const; // Zeichen des Blocks
    QString blockText(const BlockLayout& L) const;
    int     lineCount(const BlockLayout& L) const;  // Zeilenbänder
    LineRef lineRef(const BlockLayout& L, int li) const;
    qreal   lineTop(const BlockLayout& L, int li) const;
    qreal   lineTextTop(const BlockLayout& L, int li) const;   // inkl. textDy
    qreal   lineHeight(const BlockLayout& L, int li) const;
    qreal   lineAscent(const BlockLayout& L, int li) const;
    qreal   linesBottom(const BlockLayout& L) const;  // Unterkante des Inhalts
    int     lineForPos(const BlockLayout& L, int pos) const;
    // Bei geteiltem Band das Stueck zu dieser x, sonst li unveraendert.
    int     rowAtX(const BlockLayout& L, int li, qreal x) const;
    int     lineForLocalY(const BlockLayout& L, qreal y) const;
    void    lineTextRange(const BlockLayout& L, int li, int* start, int* len) const;
    qreal   xForPos(const BlockLayout& L, int li, int pos) const;
    int     posForX(const BlockLayout& L, int li, qreal x) const;
    int     imageAtPos(const BlockLayout& L, int pos) const;
    int     imageAtX(const BlockLayout& L, int li, qreal x) const;
    int     floatingImageAt(const BlockLayout& L, qreal x, qreal y) const;
    // rowFrom/rowTo grenzen die Zeilenbaender ein: laeuft ein Absatz ueber eine
    // Seitenkante, malt jedes Segment nur seine eigenen Zeilen.
    void    drawBlockText(QPainter* p, const BlockLayout& L, const QPointF& origin,
                          int selStart, int selEnd, const QColor& selBg,
                          int rowFrom = 0, int rowTo = -1) const;
    void    drawBlockLines(QPainter* p, const BlockLayout& L, const QPointF& origin,
                           int selStart, int selEnd, const QColor& selBg,
                           int rowFrom, int rowTo) const;
    int     segFirstLine(const BlockLayout& L, const PageSeg& s) const;
    int     segFirstEntry(const BlockLayout& L, const PageSeg& s) const;
    int     segFirstRow(const BlockLayout& L, const PageSeg& s) const;
    bool    segCountsTableRows(const BlockLayout& L) const;
    // Beim ersten Segment 0 - der Abstand davor gehoert zum Block, nicht zur Zeile.
    qreal   segOriginY(const BlockLayout& L, const PageSeg& s) const;
    // to ist die erste Zeile des naechsten Stuecks bzw. die Zeilenzahl beim letzten.
    void    tableSegRows(const BlockLayout& A, int segIdx, int* from, int* to) const;
    int     tableSegOfRow(const BlockLayout& A, int row) const;
    // Dieselbe Rechnung wie paintSlot, damit Gezeichnetes und Getroffenes zusammenfallen.
    QPointF tableSegOrigin(const BlockLayout& A, int segIdx) const;
    QPointF cellOrigin(int cellBlock, int anchor);

    // Seitengeometrie in Dokument-Pixeln, fensterunabhaengig.
    const Docx::SectionProps& sect() const;
    qreal pageWpx() const;
    qreal pageHpx() const;
    qreal marLpx() const;
    qreal marTpx() const;
    int   colCount() const;
    qreal colSpacePx() const;
    qreal contentWidth() const;            // Textbreite EINER Spalte
    qreal slotHeight() const;              // Texthöhe einer Spalte
    qreal slotDocX(int slot) const;        // linker Textrand des Slots
    qreal slotDocY(int slot) const;        // Oberkante des Textbereichs
    qreal pageDocY(int page) const;        // Oberkante des Papiers
    qreal docHeight() const;               // gesamter Seitenstapel
    void  updateScale();                   // Einpassen in die Kachelbreite

    const PageSeg& segAt(int i, int lineIdx) const;   // Segment einer Zeile
    qreal flowDocYForLine(int i, int lineIdx);        // aus dem FLUSS (ohne Zelle)
    qreal flowDocXForBlock(int i, int lineIdx);
    qreal docYForLine(int i, int lineIdx);            // Dokument-y einer Zeile
    qreal docXForBlock(int i, int lineIdx);           // Dokument-x einer Zeile

    // Gemeinsamer Weg von paint() und paintPageInto(). withSpell aus: die roten
    // Wellenlinien sind Anzeigehilfe und gehoeren nicht in Export oder Miniatur.
    void  paintSlot(QPainter* p, int slot, bool withCaret, bool withSelection = true,
                    bool withSpell = true);
    // Ein Weg fuer Anzeige, Miniatur und PDF-Export, damit die drei nicht auseinanderlaufen.
    void  paintPageNumber(QPainter* p, const QRectF& sheet, int page, int total,
                          int pos, int style) const;
    // Farbe der Aenderungsmarkierung je Autor, stabil aus dem Namen abgeleitet.
    static QColor revisionColor(const QString& author);
    void  paintSpell(QPainter* p, const BlockLayout& L, int blockIdx,
                     const QPointF& origin);

    void  rebuildAll();                        // Dokument (neu) geladen
    static qreal estimateHeight(const Docx::Block& b);
    void  invalidateFrom(int first, int oldCount, int newCount);
    void  ensureLaid(int i);                   // Layout eines Blocks erzwingen
    void  ensureOffsetsTo(int i);              // Präfix-Offsets bis i gültig
    // Fuellt die Segmente und liefert das Fluss-y danach - Kern der Paginierung.
    qreal paginateBlock(int idx, qreal flowStart, qreal slotH);
    // Entscheidet, ob ein Inhaltsverzeichnis auf die naechste Seite springt.
    bool  pageHasInkBefore(int idx, int page) const;
    qreal blockTop(int i);                     // Inhalts-y des Blocks
    int   blockAtY(qreal y);                   // Block unter Inhalts-y
    void  layoutChunk();                       // Timer-Tick des Initial-Layouts
    void  startChunkLayout();                  // schaltet m_chunkTimer + layoutBusy
    void  stopChunkLayout();
    void  updateContentHeight();
    void  rebuildMarkers();                    // Listen-Zähler (ganzes Dokument)
    void  buildLayout(int i);                  // Stücke/Bänder eines Absatzes
    void  buildTableLayout(int i);             // Gitter-Layout eines w:tbl-Blocks
    void  buildFlatTableLayout(int anchor);    // Gitter aus den LEBENDEN Zellblöcken
    int   tableAnchorOf(int i) const;          // Zellblock -> Anker (−1 = keiner)
    // Bilder nebeneinander, Text daneben - wie wp:inline in Word.
    qreal buildInlineRows(BlockLayout& L, const Docx::Block& b, const QFont& base,
                          const Docx::ParFmt& pf, qreal width, int blockIdx = -1);
    // Ein verankertes Bild laesst auch die FOLGENDEN Absaetze um sich laufen,
    // solange es in sie hineinragt.
    struct FloatObstacle {
        qreal x = 0.0, y = 0.0, w = 0.0, h = 0.0, padL = 0.0, padR = 0.0;
        int   wrapSide = 0;
    };
    QVector<FloatObstacle> foreignFloats(int blockIdx) const;
    // <= 0 = keiner ragt herein.
    qreal foreignFloatBottom(int blockIdx) const;
    // Nur gewoehnliche Absaetze; Tabellen, Verzeichnis und opake Bloecke nicht.
    bool  canWrapAroundFloats(int blockIdx) const;
    // Setzt topPad, erhoeht die Hoehe und verschiebt Gitter und Zell-Lagen.
    void  shiftBelowForeignFloats(int blockIdx);
    void  maybeFloatTable(int blockIdx);
    // Nur vorwaerts - kein Zyklus.
    void  invalidateFloatFollowers(int i, qreal reach);
    QFont blockBaseFont(const Docx::Block& b, int blockIdx) const;
    // QTextCharFormat::setFont legt jede Eigenschaft einzeln als QVariant ab
    // (gemessen 16 % eines Tastendrucks); ein Dokument kennt aber nur wenige Formate.
    struct FmtKey {
        Docx::RunFmt rf;
        bool         opaque = false;
        int          revision = 0;
        QString      author;
        bool operator==(const FmtKey& o) const {
            return opaque == o.opaque && revision == o.revision
                   && author == o.author && rf == o.rf;
        }
    };
    const QTextCharFormat& charFormatOf(const Docx::RunFmt& rf,
                                        const Docx::RunFmt& def,
                                        const Docx::Run& r) const;
    // Zwei Runs mit gleichem Schluessel sehen identisch aus und duerfen zu einem
    // Formatbereich verschmelzen.
    FmtKey fmtKeyOf(const Docx::RunFmt& rf, const Docx::Run& r) const;
    bool  makeImageBox(const Docx::InlineImage& info, qreal avail,
                       ImageBox* out) const;
    // Reserviert je Ueberschrift eine Zeile; gezeichnet wird in paintToc.
    bool  buildTocLayout(int i);
    // firstEntry = erster Eintrag DIESER Seite - das Verzeichnis kann sich strecken.
    void  paintToc(QPainter* p, const BlockLayout& L, qreal left, qreal y,
                   qreal width, int firstEntry);
    // rowTo < 0 heisst alle Zeilen.
    void  paintTable(QPainter* p, const BlockLayout& L, qreal left, qreal y,
                     int rowFrom = 0, int rowTo = -1);
    std::unique_ptr<QTextLayout> layoutParagraph(const Docx::Block& b, qreal width,
                                                 qreal* heightOut) const;
    void  updateCursorRect();
    void  updateImageSelection();
    qreal itemOffsetX() const;             // wie in paint(): Seite waagerecht
    void  syncCaretBlink();
    // QTextLayouts weit ausserhalb des Viewports freigeben; Hoehen bleiben gueltig.
    void  trimLayouts(int firstVisible, int lastVisible);
    // Leere Absaetze werden mit dem am Cursor geltenden Format vermessen - beim
    // Blockwechsel muss ihr Layout daher verfallen.
    void  invalidateEmptyBlock(int i);
    void  hitTest(const QPointF& itemPos, int* block, int* pos);
    void  moveCursorVertical(int dir, bool keepAnchor);

    DocxEditController* m_ctl = nullptr;
    qreal   m_contentY = 0.0;
    qreal   m_contentHeight = 0.0;
    QRectF  m_cursorRect;                      // Inhalts-Koordinaten
    QString m_tablePlaceholder;
    QString m_pageBreakLabel;
    QString m_tocEmptyLabel;
    QColor  m_surroundColor = QColor(58, 62, 70);

    std::vector<BlockLayout> m_lay;   // move-only (unique_ptr) -> std::vector
    mutable std::vector<std::pair<FmtKey, QTextCharFormat>> m_fmtCache;  // s. charFormatOf
    QVector<qreal>       m_offsets;            // Präfix-Summen der Höhen
    int    m_offsetsValidTo = 0;               // Offsets [0..N] gültig
    // Nur bis hierhin darf ensureOffsetsTo abbrechen, wenn ein Block denselben
    // Fluss-Ausgang liefert wie zuvor.
    int    m_offsetsHighWater = 0;
    // Der Abbruch darf erst hinter dem hoechsten geaenderten Block greifen - sonst
    // sind die Offsets dahinter veraltet (gemessen: Seitenzahl 218 statt 207).
    int    m_offsetsDirtyMax = 0;
    int    m_layChunkAt = 0;                   // Fortschritt Initial-Layout
    int    m_trimLo = -1, m_trimHi = -1;       // zuletzt getrimmtes Layout-Fenster
    int    m_pageCount = 1;                    // s. pageCount()
    // Kann in diesem Dokument ueberhaupt Nummerierung entstehen? Wenn nein,
    // entfaellt rebuildMarkers (gemessen 33 % eines Tastendrucks bei 30.451 Bloecken).
    // Wird nur gesetzt, nie geloescht - entfernte Nummerierung muss aufraeumbar bleiben.
    bool   m_anyNumbering = false;
    int    m_pageNumberPos = 0;                // s. pageNumberPos()
    int    m_pageNumberStyle = 1;              // s. pageNumberStyle()
    int    m_lastPage = -1;                    // letzter gemeldeter currentPage
    qreal  m_scale = 1.0;                      // Dokument-Pixel -> Item-Pixel
    QTimer m_chunkTimer;
    QTimer m_blinkTimer;
    bool   m_caretOn = true;
    bool   m_selecting = false;
    qreal  m_goalX = -1.0;                     // Wunsch-x für ↑/↓
    int    m_lastCursorBlock = -1;             // s. invalidateEmptyBlock()
    int    m_tblSelId = -1;                    // s. selTableId()
    QRectF m_tblSelDoc;                        // Stück am Cursor, Dokument-Px
    QVector<QRectF> m_tblSelSegs;              // alle Stücke, Dokument-Px
    int    m_imgSelBlock = -1;                 // s. selImageBlock()
    QRectF m_imgSelDoc;                        // Bildrechteck in Dokument-Pixeln
    // Ausgelegte Fussnoten, lazy. Kein QHash: der Eintrag haelt einen unique_ptr.

};

// Miniatur einer Seite - ruft DocxTextArea::paintPageInto, es gibt also nur
// einen Zeichenweg und keinen Bild-Cache.
class DocxPageThumb : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(DocxTextArea* area READ area WRITE setArea NOTIFY areaChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)

public:
    explicit DocxPageThumb(QQuickItem* parent = nullptr);

    DocxTextArea* area() const { return m_area; }
    void setArea(DocxTextArea* a);
    int  page() const { return m_page; }
    void setPage(int p);

    void paint(QPainter* p) override;

signals:
    void areaChanged();
    void pageChanged();

private:
    DocxTextArea* m_area = nullptr;
    int m_page = 0;

};
