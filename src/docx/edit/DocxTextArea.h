#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxTextArea - die selbstgebaute Anzeige-/Eingabefläche des DOCX-Editors
//  (QQuickPaintedItem; bewusst KEIN QML-TextEdit/RichText - Anforderung des
//  Auftrags: eigenes Absatz-/Zeilenmodell in C++; QTextLayout ist als reines
//  Shaping-/Zeilenumbruch-Primitiv UNTER diesem Modell ausdrücklich erlaubt).
//
//  RAM/Architektur:
//   • Das Item ist VIEWPORT-groß (nicht inhaltsgroß!) - QQuickPaintedItem
//     hält eine Textur in Item-Größe; ein 100-Seiten-Dokument als Textur
//     wäre ein VRAM-Desaster. Gescrollt wird über die contentY-Property
//     (QML-Scrollbar/Wheel), paint() zeichnet nur den sichtbaren Streifen.
//   • Layout-Cache je Absatz (QTextLayout + Höhe), Präfix-Offsets mit
//     dirtyFrom-Invalidierung -> Bearbeitungen relayouten nur ab dem
//     betroffenen Block (inkrementell, Regel 17).
//   • Initial-Layout gechunkt über einen 0-ms-Timer (~300 Blöcke je Tick):
//     lange Dokumente blockieren den GUI-Thread nie; noch nicht vermessene
//     Blöcke tragen eine Zeilen-Schätzhöhe, die beim echten Layout ersetzt
//     wird (Scrollbalken bleibt dabei stabil nutzbar).
//
//  Eingabe: Maus (Cursor/Selektion/Doppelklick-Wort), Tastatur (Zeichen,
//  Pfeile/Home/End ± Shift, Enter/Backspace/Entf, Strg+A/C/X/V/Z/Shift+Z,
//  Strg+B/I/U, Strg+S -> saveRequested) sowie Eingabemethoden-Commits
//  (tote Tasten/IME). Alle Mutationen laufen über den DocxEditController.
// ─────────────────────────────────────────────────────────────────────────────

#include <QQuickPaintedItem>
#include <QImage>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTimer>
#include <QVector>
#include <memory>
#include <unordered_map>
#include <vector>

//  Vollständiger Typ nötig: Q_PROPERTY(DocxEditController*) registriert den
//  Metatyp im moc-Code.
#include "docx/edit/DocxEditController.h"

class DocxTextArea : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(DocxEditController* ctl READ ctl WRITE setCtl NOTIFY ctlChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
    //  Cursor-Rechteck in INHALTS-Koordinaten (QML scrollt es sichtbar).
    //  Caret-Lage in ITEM-Pixeln. `cursorX` gab es lange nicht (QML scrollt nur
    //  senkrecht) - ohne sie ließ sich die WAAGERECHTE Caret-Lage im Treiber
    //  gar nicht prüfen, und genau dort saß N19 (Caret an der Slotkante statt
    //  neben der gleitenden Tabelle).
    Q_PROPERTY(qreal cursorX READ cursorX NOTIFY cursorRectChanged)
    Q_PROPERTY(qreal cursorY READ cursorY NOTIFY cursorRectChanged)
    Q_PROPERTY(qreal cursorH READ cursorH NOTIFY cursorRectChanged)
    //  Höhe EINER Textzeile in ITEM-Pixeln (mit Maßstab). Das Mausrad rechnet
    //  seine Schrittweite daraus - ein Textdokument scrollt in Zeilen, nicht in
    //  Bruchteilen der Fenster­höhe.
    Q_PROPERTY(qreal lineStep READ lineStep NOTIFY contentHeightChanged)
    //  i18n-Texte kommen aus QML (App.uiText) - kein Strings-Coupling hier.
    Q_PROPERTY(QString tablePlaceholder MEMBER m_tablePlaceholder NOTIFY labelsChanged)
    Q_PROPERTY(QString pageBreakLabel MEMBER m_pageBreakLabel NOTIFY labelsChanged)
    //  Beschriftung, wenn ein Inhaltsverzeichnis (noch) keine Überschriften
    //  findet - sonst wäre der Absatz eine leere Fläche.
    Q_PROPERTY(QString tocEmptyLabel MEMBER m_tocEmptyLabel NOTIFY labelsChanged)
    //  Umgebungsgrund (Theme) um die WEISSE Word-Seite herum - QML bindet
    //  App.themeBase; die Seite selbst bleibt bewusst immer weiß (wie Word).
    Q_PROPERTY(QColor surroundColor MEMBER m_surroundColor NOTIFY labelsChanged)
    //  Paginierung: Seitenzahl des Dokuments und die Seite am Cursor (1-basiert
    //  in der Anzeige, hier 0-basiert) - Grundlage der Miniaturen-Leiste.
    //  SEITENZAHL: dieselbe Angabe, die der PDF-Export bekommt (Dokument-Menü).
    //  Sie gehört auf die Seite, nicht erst ins ausgegebene PDF - sonst sieht
    //  man erst nach dem Umwandeln, was man eingestellt hat.
    //  0 aus · 1 links · 2 mittig · 3 rechts; Stil 0 = „3", 1 = „3 / 12".
    Q_PROPERTY(int pageNumberPos READ pageNumberPos WRITE setPageNumberPos
               NOTIFY pageNumberChanged)
    Q_PROPERTY(int pageNumberStyle READ pageNumberStyle WRITE setPageNumberStyle
               NOTIFY pageNumberChanged)
    //  ── Wo liegt die SEITE im Item? (Randlineale) ────────────────────────
    //  Die Lineale sollen die Maßstabsrechnung dieser Fläche nicht nachbauen -
    //  dann liefen sie über kurz oder lang auseinander. Sie fragen stattdessen,
    //  wo die Seite gerade steht: Versatz von links, Breite und Höhe in
    //  ITEM-Pixeln, also einschließlich des Einpass-Maßstabs.
    Q_PROPERTY(qreal pageOffsetX  READ pageOffsetX  NOTIFY pageGeometryChanged)
    Q_PROPERTY(qreal pageWidthPx  READ pageWidthPx  NOTIFY pageGeometryChanged)
    Q_PROPERTY(qreal pageHeightPx READ pageHeightPx NOTIFY pageGeometryChanged)
    //  Oberkante der AKTUELLEN Seite in Item-Koordinaten. Das senkrechte Lineal
    //  meint immer die Seite, die man gerade vor sich hat - wie in Word.
    Q_PROPERTY(qreal currentPageTopPx READ currentPageTopPx NOTIFY pageGeometryChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageCountChanged)
    //  Laeuft das gechunkte Initial-Layout noch? Fuer Pruefstaende und Tests:
    //  die warteten bisher darauf, dass sich die SEITENZAHL nicht mehr aendert
    //  - bei einem Dokument mit zwei Absaetzen bleibt sie bei 1, und der Lauf
    //  lief in sein Zeitlimit statt in ein Ergebnis (gemessen: 60010 ms).
    Q_PROPERTY(bool layoutBusy READ layoutBusy NOTIFY layoutBusyChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    //  AUSGEWÄHLTES BILD: der Cursorblock, falls er ein reiner Bild-Absatz ist
    //  (sonst −1). Es gibt bewusst keinen zweiten Auswahlzustand - ein Klick
    //  aufs Bild setzt den Cursor dorthin, das IST die Auswahl. Das Rechteck
    //  steht in ITEM-Pixeln, damit QML die Ziehpunkte direkt darüberlegen kann.
    Q_PROPERTY(int selImageBlock READ selImageBlock NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageX READ selImageX NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageY READ selImageY NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageW READ selImageW NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageH READ selImageH NOTIFY imageSelectionChanged)
    //  AUSGEWÄHLTE TABELLE - dieselbe Regel wie beim Bild: kein zweiter
    //  Auswahlzustand, sondern „der Cursor steht in einer Tabelle". Rechtecke
    //  in ITEM-Pixeln, damit QML Rahmen und Ziehpunkte darüberlegen kann.
    //  `selTableRects` trägt JE SEITENSTÜCK eines - eine getrennte Tabelle
    //  liegt auf mehreren Seiten und lässt sich nie durch EIN Rechteck fassen;
    //  `selTableX/Y/W/H` ist davon das Stück am Cursor (dort die Ziehpunkte).
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
    //  m_cursorRect liegt in Dokument-Pixeln; QML rechnet mit Item-Pixeln.
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
    //  [{x,y,w,h}] in Item-Pixeln - ein Eintrag je Seitenstück der Tabelle.
    QVariantList selTableRects() const;
    //  Oberkante einer Seite in ITEM-Pixeln - QML setzt damit contentY, wenn
    //  eine Miniatur angeklickt wird.
    Q_INVOKABLE qreal pageTop(int page);
    //  Oberkante einer Seite in ITEM-Koordinaten (also abzüglich `contentY`) -
    //  daran hängt das senkrechte Lineal, das immer die Seite meint, die man
    //  gerade vor sich hat.
    Q_INVOKABLE qreal pageTopItem(int page);
    qreal pageOffsetX()  const { return itemOffsetX(); }
    qreal pageWidthPx()  const;
    qreal pageHeightPx() const;
    qreal currentPageTopPx() const;
    //  1-basierte Seitenzahl eines Blocks - Grundlage der
    //  Verzeichnis-Einträge und im Testtreiber die Probe darauf, dass
    //  das Verzeichnis wirklich allein auf seinen Seiten steht.
    Q_INVOKABLE int pageOfBlock(int i);
    //  1-basierte Seitenzahl EINER STELLE im Block. Ein Überschrift-Absatz kann
    //  mehrere Einträge tragen und über eine Seitengrenze laufen - dann liegen
    //  seine Zeilen auf verschiedenen Seiten und `pageOfBlock` wäre für alle
    //  ausser der ersten falsch.
    Q_INVOKABLE int pageOfEntry(int i, int pos);
    //  Ein umfließendes Bild ABLEGEN (Ende der Ziehgeste): liegt seine
    //  Oberkante nicht mehr über dem Text seines Absatzes, wandert der Anker in
    //  den Absatz, über dem es jetzt steht - erst dadurch umfließt DESSEN Text
    //  es (Word macht es genauso). `xMm`/`yMm` sind die gezogene Lage relativ
    //  zum bisherigen Absatz; der Rest ist Geometrie und deshalb Sache der
    //  Anzeige. Ohne Absatzwechsel identisch zu `setImagePositionMm`.
    Q_INVOKABLE void dropSelectedImage(int block, qreal xMm, qreal yMm);

    //  EINE Seite in ein Zielrechteck malen (Miniaturen, s. DocxPageThumb).
    //  Nutzt denselben Zeichenweg wie paint() - es gibt keine zweite Darstellung
    //  und keinen Bild-Cache (RAM = Priorität 1).
    //  `withPaperFrame` = Papierfläche + grauer Rand (Anzeige/Miniatur). Für
    //  den PDF-Export AUS: dort IST die Seite das Papier, ein gezeichneter
    //  Rahmen wäre ein Strich auf jedem Blatt.
    //  `withSelection` = die blaue Auswahl-Hinterlegung mitmalen. Für den
    //  PDF-Export AUS: sonst brennt eine beim Drücken bestehende Markierung als
    //  blauer Balken in die Datei - der Caret wird schon immer unterdrückt, die
    //  Auswahl war es nicht.
    void paintPageInto(QPainter* p, int page, const QRectF& target,
                       bool withPaperFrame = true, bool withSelection = true);

    //  ── DOCX -> PDF, gemalt aus DIESER Auslegung (N6/N9) ─────────────────
    //  Schreibt jede Seite so, wie sie am Bildschirm steht: derselbe
    //  Zeichenweg (`paintSlot`), dieselben Seitengrenzen. Das PDF ist damit
    //  seitengleich PER KONSTRUKTION - kein zweites Layout, das auseinander
    //  laufen könnte (der frühere `QTextDocument`-Weg tat genau das:
    //  gemessen 4 Anzeige-Seiten gegen 3 Export-Seiten).
    //  Läuft auf dem GUI-Thread, weil die Auslegung einem QQuickItem gehört -
    //  sie ist aber bereits fertig (das Dokument steht auf dem Schirm), es
    //  wird nur noch gezeichnet. Leerer Rückgabewert = Erfolg, sonst der
    //  Fehlertext.
    //  Den SCHREIBBEREICH bestimmen die Seitenränder des Dokuments
    //  (`w:sectPr/w:pgMar`, im Editor an den Randlinealen einstellbar) - der
    //  Export malt die Bildschirm-Auslegung und braucht dafür keinen eigenen
    //  Rand mehr. Die frühere Einstellung „zusätzlicher Rand beim PDF-Export"
    //  war der Behelf dafür und ist entfallen.
    //  `pageNumberPos`: 0 aus · 1 links · 2 mittig · 3 rechts;
    //  `pageNumberStyle`: 0 = „3" · 1 = „3 / 12".
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
    //  Lage/Maßstab der Seite im Item hat sich geändert (Breite, Einpassung,
    //  neue Seiteneinrichtung) - die Randlineale richten sich danach.
    void pageGeometryChanged();
    void pageNumberChanged();
    void currentPageChanged();
    //  Inhalt hat sich geändert (Laden/Bearbeiten) - die Miniaturen zeichnen neu.
    void documentChanged();
    //  Auswahl/Lage des Bildes hat sich geändert (Cursor, Bearbeitung, Scrollen,
    //  Maßstab) - QML richtet die Ziehpunkte neu aus.
    void imageSelectionChanged();
    //  Rechtsklick in der Fläche: Position in ITEM-Pixeln + getroffener Block.
    //  Das Menü selbst baut QML (gethemt) - hier gibt es keine UI-Texte.
    void contextMenuRequested(qreal x, qreal y, int block);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    //  Einzeltasten-Kürzel der App (z. B. „D" = Datum-Editor im Viewer)
    //  dürfen die Texteingabe NICHT kapern. QML-Textfelder machen das von
    //  selbst; diese selbstgebaute Fläche muss ShortcutOverride annehmen.
    bool event(QEvent* e) override;
    void inputMethodEvent(QInputMethodEvent* e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    void geometryChange(const QRectF& n, const QRectF& o) override;

private:
    //  ── Koordinatensysteme (Paginierung) ─────────────────────────────────────
    //  FLUSS-y (m_offsets): fortlaufend in LESERICHTUNG, monoton, unabhängig
    //    vom Fenster. Ein „Slot" ist der Textbereich EINER Spalte EINER Seite;
    //    Fluss-y = slot * slotHeight() + y innerhalb des Slots. Damit bleibt die
    //    binäre Suche über die Präfix-Offsets gültig, auch wenn zwei Spalten
    //    dieselbe Bildschirmhöhe teilen.
    //  DOKUMENT-y (Seitenstapel): was man SIEHT - Seiten mit Rand und Lücke
    //    dazwischen. Ein Absatz kann über mehrere Slots laufen; welche Zeilen
    //    wo liegen, steht in seinen Segmenten.
    //  ITEM-Pixel (contentY/contentHeight/cursorY): Dokument-y × m_scale. Der
    //    Maßstab passt eine zu breite Seite in eine schmale Kachel ein und ist
    //    ein reiner Zeichen-Faktor - das Layout selbst bleibt seitengenau.
    struct PageSeg {
        int   slot    = 0;                     // Spalten-Slot dieses Stücks
        //  DREI Bedeutungen, je nach Block: bei einem Absatz die erste ZEILE
        //  des Stücks, bei einem Inhaltsverzeichnis der erste EINTRAG dieser
        //  Seite, bei einer Tabelle die erste TABELLENZEILE. Nie direkt lesen -
        //  segFirstLine()/segFirstEntry()/segFirstRow() trennen das.
        int   first   = 0;
        qreal yInSlot = 0.0;                   // Oberkante im Slot
    };
    //  ── Tabellen-ANZEIGE (read-only) ─────────────────────────────────────────
    //  Eine Tabelle bleibt EIN Block (beim Speichern byteidentisch). Sie wird
    //  aber als echtes Gitter ausgelegt, damit ihre HÖHE stimmt - mit dem
    //  früheren festen Platzhalter von 34 px war jeder Seitenumbruch NACH einer
    //  Tabelle falsch. Bearbeiten von Zellen ist (noch) nicht möglich.
    struct CellLayout {
        //  Nur noch für NICHT flach zerlegte Tabellen (opaker Block) - dort
        //  liefert parseTableForDisplay den Inhalt. Bei flach zerlegten Tabellen
        //  kommt der Text aus den echten Blöcken, dann bleibt das hier leer.
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

    //  ── Ein STÜCK eines Absatzes ─────────────────────────────────────────────
    //  Ein Absatz ohne Bild hat genau EIN Stück über seinen ganzen Text; ein
    //  Absatz mit Bildern eines je Textabschnitt ZWISCHEN den Bildern (das
    //  Objekt-Zeichen U+FFFC gehört keinem Stück - QTextLayout würde daraus ein
    //  Kästchen malen). Die Zeilen eines Stücks tragen BLOCK-lokale
    //  Koordinaten, `dx`/`dy` bleiben deshalb 0, solange ein Stück nicht als
    //  Ganzes versetzt wird.
    struct Piece {
        std::unique_ptr<QTextLayout> lay;
        qreal dx = 0.0, dy = 0.0;
        int   textStart = 0;                   // Zeichenposition im Absatz
    };
    //  ── Ein BILD im Absatz (`w:drawing`-Run) ─────────────────────────────────
    //  Gehalten wird nur das eingepasste `QImage` (nicht die Originalbytes);
    //  das Layout-Fenster gibt es wieder frei, die Kastenmaße bleiben stehen,
    //  damit die Höhe des Absatzes exakt bleibt.
    struct ImageBox {
        QImage  img;
        QString relId;
        qreal   x = 0.0, y = 0.0, w = 0.0, h = 0.0;   // block-lokal
        int     pos = 0;                       // Zeichenindex des U+FFFC
        int     run = -1;                      // Run-Index im Block
        bool    broken = false;                // Bytes/Beziehung unbrauchbar
        //  VERANKERT (`wp:anchor` + `w:wrapSquare`): das Bild steht NICHT im
        //  Zeilenfluss, sondern an seiner eigenen Stelle - der Text weicht ihm
        //  über so viele Zeilen aus, wie es hoch ist. `padL/padR` sind die
        //  Abstände, die das Dokument dafür verlangt (`distL`/`distR`).
        bool    floating = false;
        qreal   padL = 0.0, padR = 0.0;
        //  Auf welcher Seite der Text laufen darf (`InlineImage::WrapSide`).
        int     wrapSide = 0;
    };
    //  ── Ein ZEILENBAND = eine sichtbare Zeile ────────────────────────────────
    //  Höchstens EINE Textzeile, dazu die Bilder, die daneben stehen. Das Band
    //  ist die Einheit von Paginierung, Caret und Treffersuche - deshalb spricht
    //  die Facade von „Zeilen": ohne Bild ist ein Band genau eine `QTextLine`.
    struct RowInfo {
        qreal y = 0.0;                         // Oberkante (block-lokal)
        qreal h = 0.0;                         // Vorschub bis zum nächsten Band
        qreal visH = 0.0;                      // sichtbare Höhe (ohne Durchschuss)
        //  Versatz der TEXTZEILE im Band: neben einem Bild sitzt sie an dessen
        //  Unterkante (Grundlinie, wie in Word), das Band beginnt aber oben.
        //  Caret und Listenmarker müssen der ZEILE folgen, nicht dem Band -
        //  sonst stünde die Schreibmarke oben und der Text erschiene unten.
        qreal textDy = 0.0;
        qreal ascent = 0.0;
        int   charStart = 0, charEnd = 0;      // Zeichenspanne im Absatz
        int   piece = -1, line = 0;            // Textzeile (piece < 0 = nur Bilder)
        int   imgFirst = 0, imgCount = 0;      // Bilder dieses Bandes
    };

    struct BlockLayout {
        //  Textstücke + Bilder + Zeilenbänder - ALLES über die Facade lesen.
        std::vector<Piece>    pieces;
        std::vector<ImageBox> images;
        std::vector<RowInfo>  rows;
        int     textLen = 0;                   // Zeichen des Absatzes
        bool    isText  = false;               // Block ist ein ausgelegter Absatz
        //  Steht ein erzwungener Seitenumbruch (U+E000) IM Absatz? Beim
        //  Auslegen einmal bestimmt. Die Paginierung fragte das früher über
        //  `blockText(L)` ab - ein zusammengesetzter Absatztext plus voller
        //  Suchlauf JE BLOCK und JE Tastendruck, obwohl fast kein Absatz einen
        //  Umbruch trägt. Das Flag überlebt `trimLayouts` (wie `height`).
        bool    hasBreak = false;
        //  Layout-Fenster hat Stücke/Bilder freigegeben (Höhen bleiben gültig).
        bool    trimmed = false;
        std::unique_ptr<TableLayout> table;    // nur bei w:tbl (s. isTable)
        bool    isTable = false;               // Block ist eine deutbare Tabelle
        //  Absatz besteht NUR aus einem Bild (Sonderfall für Auswahl/Zellen).
        bool    isImage = false;
        //  ── Inhaltsverzeichnis ───────────────────────────────────────────────
        //  Die EINTRÄGE stehen nicht in der Datei (das Feld bleibt deklarativ),
        //  sie werden hier zum Auslegen gehalten. Die SEITENZAHL steht bewusst
        //  NICHT dabei: sie ist erst nach der Paginierung bekannt, hängt aber
        //  nicht an der Höhe (die Zahl der Einträge steht fest) - deshalb füllt
        //  sie erst `paint` ein und das Verzeichnis braucht keinen zweiten Pass.
        bool    isToc = false;
        QList<Docx::TocEntry> tocEntries;
        qreal   tocLineH = 0.0;
        //  Zeichenformat des Verzeichnisses (aus w:pPr/w:rPr des Absatzes) -
        //  Schriftart und -größe sind das Einzige, was daran einstellbar ist.
        QFont   tocFont;
        //  Einträge, die auf EINE Seite passen (Aufteilung über mehrere Seiten).
        int     tocPerPage = 0;
        //  ── Zellblock einer flach zerlegten Tabelle ──────────────────────────
        //  Der FLUSS bleibt monoton, weil die ganze Tabelle am Anker hängt (nur
        //  er trägt Höhe). Die Lage im Dokument steht deshalb hier EXPLIZIT,
        //  relativ zur Oberkante/linken Textkante des Ankers - genau so, wie
        //  Spalten ihre eigene x-Lage haben, ohne den Fluss zu verbiegen.
        bool    isCell = false;
        qreal   cellRelX = 0.0, cellRelY = 0.0, cellW = 0.0;
        //  Tabellenzeile dieses Zellblocks - sie entscheidet, auf welchem Stück
        //  der Tabelle (und damit auf welcher Seite) er liegt.
        int     cellRow = 0;
        qreal   height = 0.0;                  // inkl. Abstand davor/danach
        //  Wie weit ragen verankerte Bilder dieses Blocks UNTER seine eigene
        //  Unterkante? Genau darum fließen die FOLGENDEN Absätze herum - in
        //  Word endet der Umfluss nicht am Absatz. 0 = kein Überstand.
        qreal   floatOverhang = 0.0;
        //  GLEITENDE Tabelle: sie gibt ihre Flusshöhe ab und wird für die
        //  folgenden Absätze zum Störer - daneben lässt sich schreiben, genau
        //  wie neben einem verankerten Bild. Nur wenn ein lesbarer Streifen
        //  bleibt UND die Tabelle auf eine Seite passt (s. maybeFloatTable);
        //  eine über Seiten getrennte Tabelle bleibt im Fluss, ihre Stücke
        //  hängen an der Blockhöhe.
        bool    tableFloating = false;
        //  Blöcke, die NICHT umfließen können (Tabelle, Verzeichnis), weichen
        //  einem hereinragenden Bild nach UNTEN aus: `topPad` ist der Versatz,
        //  um den ihr Inhalt dafür nach unten rückt (in `height` enthalten).
        qreal   topPad = 0.0;
        qreal   beforePx = 0.0;                // Versatz bis zur ersten Zeile
        qreal   indentPx = 0.0;                // Listen-Einzug
        QString marker;                        // "• " / "3. " (Listen)
        bool    laid = false;                  // height ist ECHT (nicht Schätzung)
        //  Leer = ein Stück, ganz im Slot von blockSlot(i) (Normalfall). Mehr
        //  als ein Eintrag nur für Absätze, die eine Seiten-/Spaltengrenze
        //  überschreiten.
        QVector<PageSeg> segs;
    };

    //  ── Layout-STÜCKE eines Blocks (die einzige Lesesicht auf seine Zeilen) ──
    //  Jeder Leser geht über diese Schicht, NIE direkt an `pieces`/`images`.
    //  „Zeile" heißt hier ZEILENBAND (`RowInfo`): ohne Bild ist das genau eine
    //  `QTextLine`, mit Bildern das Band aus Bild(ern) und der Textzeile
    //  daneben. Aufrufer: `updateCursorRect` · `hitTest` · `moveCursorVertical`
    //  · Selektions-Hinterlegung · `paginateBlock`/`PageSeg`.
    //  Einheiten: Bänder werden BLOCK-global gezählt, x/y sind BLOCK-lokal,
    //  Zeichenpositionen sind BLOCK-lokal.
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
    //  Bei einem geteilten Band (Text links UND rechts eines Bildes) das Stück,
    //  das zu dieser x gehört - sonst `li` unverändert.
    int     rowAtX(const BlockLayout& L, int li, qreal x) const;
    int     lineForLocalY(const BlockLayout& L, qreal y) const;
    void    lineTextRange(const BlockLayout& L, int li, int* start, int* len) const;
    qreal   xForPos(const BlockLayout& L, int li, int pos) const;
    int     posForX(const BlockLayout& L, int li, qreal x) const;
    //  Bild an einer Zeichenposition (−1 = dort steht keines) bzw. unter einem
    //  Punkt eines Bandes - die Treffersuche macht daraus die Cursor-Stelle.
    int     imageAtPos(const BlockLayout& L, int pos) const;
    int     imageAtX(const BlockLayout& L, int li, qreal x) const;
    //  Verankertes Bild an einer block-lokalen Stelle - es hängt an keinem
    //  Zeilenband, sondern nur an seiner Lage.
    int     floatingImageAt(const BlockLayout& L, qreal x, qreal y) const;
    //  Inhalt eines Blocks samt Selektion zeichnen (block-lokale Positionen;
    //  selStart ≥ selEnd = keine Selektion). Malt Textstücke UND Bilder.
    //  `rowFrom`/`rowTo` grenzen die ZEILENBÄNDER ein (rowTo < 0 = bis zum
    //  Ende): läuft ein Absatz über eine Seitenkante, malt jedes Segment nur
    //  seine EIGENEN Zeilen. Das Clipping schneidet die fremden zwar optisch
    //  weg, im PDF blieben sie aber im Textstrom stehen - jede Zeile stünde dann
    //  im Inhalt BEIDER Seiten (s. drawBlockLines).
    void    drawBlockText(QPainter* p, const BlockLayout& L, const QPointF& origin,
                          int selStart, int selEnd, const QColor& selBg,
                          int rowFrom = 0, int rowTo = -1) const;
    //  Zeichnet die Bänder [rowFrom, rowTo) EINZELN (QTextLine statt QTextLayout).
    void    drawBlockLines(QPainter* p, const BlockLayout& L, const QPointF& origin,
                           int selStart, int selEnd, const QColor& selBg,
                           int rowFrom, int rowTo) const;
    //  Deutung eines Segments (s. PageSeg::first).
    int     segFirstLine(const BlockLayout& L, const PageSeg& s) const;
    int     segFirstEntry(const BlockLayout& L, const PageSeg& s) const;
    int     segFirstRow(const BlockLayout& L, const PageSeg& s) const;
    //  Trägt dieser Block ein Gitter, dessen Segmente Tabellenzeilen zählen?
    bool    segCountsTableRows(const BlockLayout& L) const;
    //  Layout-y, die auf der OBERKANTE eines Segments liegt: beim ERSTEN
    //  Segment 0 (der Abstand davor gehört zum Block, nicht zur Zeile), bei
    //  jedem weiteren die y seiner ersten Zeile bzw. Tabellenzeile.
    qreal   segOriginY(const BlockLayout& L, const PageSeg& s) const;
    //  ── Eine Tabelle über mehrere Seiten ─────────────────────────────────────
    //  Zeilenbereich [from, to) eines Tabellen-Segments; `to` ist die erste
    //  Zeile des NÄCHSTEN Stücks bzw. die Zeilenzahl beim letzten.
    void    tableSegRows(const BlockLayout& A, int segIdx, int* from, int* to) const;
    //  Segment des Ankers, in dem eine Tabellenzeile liegt (−1 = keines).
    int     tableSegOfRow(const BlockLayout& A, int row) const;
    //  Dokument-Ursprung EINES Tabellenstücks - dieselbe Rechnung, die
    //  `paintSlot` zum Zeichnen benutzt. Zellblöcke hängen daran mit ihrem
    //  `cellRelX`/`cellRelY`, damit Gezeichnetes und Getroffenes zusammenfallen.
    QPointF tableSegOrigin(const BlockLayout& A, int segIdx) const;
    //  Ursprung des Stücks, auf dem ein Zellblock liegt (Zeile ⇒ Segment).
    QPointF cellOrigin(int cellBlock, int anchor);

    //  ── Seitengeometrie (Dokument-Pixel, fensterunabhängig) ──────────────────
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

    //  Segment-Zugriff (Fluss ↔ Sichtbares).
    const PageSeg& segAt(int i, int lineIdx) const;   // Segment einer Zeile
    qreal flowDocYForLine(int i, int lineIdx);        // aus dem FLUSS (ohne Zelle)
    qreal flowDocXForBlock(int i, int lineIdx);
    qreal docYForLine(int i, int lineIdx);            // Dokument-y einer Zeile
    qreal docXForBlock(int i, int lineIdx);           // Dokument-x einer Zeile

    //  Blöcke eines Slots zeichnen (gemeinsamer Weg von paint() und
    //  paintPageInto(); `p` ist bereits auf Dokument-Pixel gestellt).
    //  `withSpell`: die roten Wellenlinien sind eine ANZEIGEHILFE. Sie gehören
    //  weder in den PDF-Export noch in die Seitenminiaturen - beide gehen über
    //  `paintPageInto`, das sie deshalb abschaltet.
    void  paintSlot(QPainter* p, int slot, bool withCaret, bool withSelection = true,
                    bool withSpell = true);
    //  Die Seitenzahl auf EIN Blatt malen - EIN Weg für Anzeige, Miniatur und
    //  PDF-Export, damit die drei nicht auseinanderlaufen. `sheet` ist das
    //  Blatt in den Koordinaten des Malers; Lage und Größe werden daraus
    //  relativ gerechnet, also unabhängig von der Auflösung.
    void  paintPageNumber(QPainter* p, const QRectF& sheet, int page, int total,
                          int pos, int style) const;
    //  Rote Wellenlinie unter den beanstandeten Stellen (Rechtschreibprüfung).
    //  Farbe der Änderungsmarkierung je Autor (stabil, aus dem Namen).
    static QColor revisionColor(const QString& author);
    void  paintSpell(QPainter* p, const BlockLayout& L, int blockIdx,
                     const QPointF& origin);

    void  rebuildAll();                        // Dokument (neu) geladen
    //  Grobe Höhe eines noch nicht vermessenen Blocks (Fluss/Bildlaufleiste
    //  bleiben plausibel, bis `layoutChunk` ihn vermisst).
    static qreal estimateHeight(const Docx::Block& b);
    void  invalidateFrom(int first, int oldCount, int newCount);
    void  ensureLaid(int i);                   // Layout eines Blocks erzwingen
    void  ensureOffsetsTo(int i);              // Präfix-Offsets bis i gültig
    //  Einen Block auf Slots verteilen (füllt seine Segmente) und das Fluss-y
    //  NACH ihm zurückgeben. Kern der Paginierung.
    qreal paginateBlock(int idx, qreal flowStart, qreal slotH);
    //  Steht auf `page` VOR Block `idx` schon etwas Sichtbares? Entscheidet, ob
    //  ein Inhaltsverzeichnis auf die nächste Seite springt.
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
    //  Absatz mit Bildern in ZEILENBÄNDER auslegen (Bilder nebeneinander, Text
    //  daneben - genau wie `wp:inline` in Word). Rückgabe: Unterkante des
    //  Inhalts (block-lokal, ohne Abstand danach).
    qreal buildInlineRows(BlockLayout& L, const Docx::Block& b, const QFont& base,
                          const Docx::ParFmt& pf, qreal width, int blockIdx = -1);
    //  ── Umfluss ÜBER die Absatzgrenze (Word-Verhalten) ───────────────────────
    //  Ein verankertes Bild lässt auch die FOLGENDEN Absätze um sich herum
    //  laufen, solange es in sie hineinragt. `foreignFloats` sammelt die Störer
    //  der Blöcke VOR `blockIdx` in dessen block-lokalen Koordinaten;
    //  `blockIdx < 0` (Zell-Absätze) liefert nichts.
    struct FloatObstacle {
        qreal x = 0.0, y = 0.0, w = 0.0, h = 0.0, padL = 0.0, padR = 0.0;
        int   wrapSide = 0;
    };
    QVector<FloatObstacle> foreignFloats(int blockIdx) const;
    //  Unterkante des tiefsten fremden Störers in Block-Koordinaten (≤ 0 = keiner
    //  ragt herein). Blöcke ohne Umfluss schieben sich um diesen Wert nach unten.
    qreal foreignFloatBottom(int blockIdx) const;
    //  Kann dieser Block einem hereinragenden Bild AUSWEICHEN? Nur gewöhnliche
    //  Absätze legen sich über `buildInlineRows` aus und fragen dabei
    //  `usableSpan`; Tabellen, Inhaltsverzeichnis und opake Blöcke nicht.
    bool  canWrapAroundFloats(int blockIdx) const;
    //  Einen nicht umfließenden Block unter die hereinragenden Bilder schieben
    //  (setzt `topPad`, erhöht die Höhe, verschiebt Gitter und Zell-Lagen).
    void  shiftBelowForeignFloats(int blockIdx);
    //  Entscheidet nach dem Auslegen des Gitters, ob die Tabelle GLEITET
    //  (s. BlockLayout::tableFloating). Setzt Höhe und Überstand entsprechend.
    void  maybeFloatTable(int blockIdx);
    //  Nach einer Höhen-/Überstands-Änderung die Blöcke im Reichweitenband des
    //  Überstands zum Neuauslegen markieren (nur vorwärts - kein Zyklus).
    void  invalidateFloatFollowers(int i, qreal reach);
    //  Grundschrift eines Absatzes (mit Cursor-Sonderfall für leere Zeilen).
    QFont blockBaseFont(const Docx::Block& b, int blockIdx) const;
    //  ── Zeichenformat eines Runs, GEMERKT ────────────────────────────────────
    //  Aus dem aufgelösten `RunFmt` ein `QTextCharFormat` zu bauen ist teuer:
    //  `QTextCharFormat::setFont` legt jede Schrifteigenschaft einzeln als
    //  `QVariant` ab (gemessen 16 % eines Tastendrucks). Ein Dokument kennt
    //  aber nur eine Handvoll verschiedener Formate, und dasselbe Format
    //  wiederholt sich in jedem Absatz. Deshalb ein kleiner Merkspeicher mit
    //  linearer Suche - bei dieser Größe schneller als eine Hash-Tabelle.
    //  Geleert wird er beim Laden eines anderen Dokuments (`rebuildAll`).
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
    //  Schluessel EINES Runs - dieselbe Kennung, nach der `charFormatOf`
    //  zwischenspeichert. Zwei Runs mit gleichem Schluessel sehen identisch
    //  aus und duerfen deshalb zu EINEM Formatbereich verschmelzen.
    FmtKey fmtKeyOf(const Docx::RunFmt& rf, const Docx::Run& r) const;
    //  Bild eines Runs auf `avail` einpassen (Seitenverhältnis bleibt).
    bool  makeImageBox(const Docx::InlineImage& info, qreal avail,
                       ImageBox* out) const;
    //  Inhaltsverzeichnis-Feld auslegen (true = war eines). Reserviert je
    //  Überschrift eine Zeile; gezeichnet wird in `paintToc`.
    bool  buildTocLayout(int i);
    //  `firstEntry` = erster Eintrag DIESER Seite; das Verzeichnis kann sich
    //  über mehrere Seiten erstrecken (je Seite ein PageSeg).
    void  paintToc(QPainter* p, const BlockLayout& L, qreal left, qreal y,
                   qreal width, int firstEntry);
    //  1-basierte Seitenzahl eines Blocks (für die Verzeichnis-Einträge).
    //  Gitter und (bei opaken Tabellen) Zelltext EINES Stücks zeichnen -
    //  `rowTo < 0` heißt „alle Zeilen".
    void  paintTable(QPainter* p, const BlockLayout& L, qreal left, qreal y,
                     int rowFrom = 0, int rowTo = -1);
    //  Einen Absatz (auch einen Zell-Absatz) vermessen; Rückgabe = Höhe.
    std::unique_ptr<QTextLayout> layoutParagraph(const Docx::Block& b, qreal width,
                                                 qreal* heightOut) const;
    void  updateCursorRect();
    //  Bild-Auswahl/Lage neu bestimmen (s. selImage*-Properties).
    void  updateImageSelection();
    qreal itemOffsetX() const;             // wie in paint(): Seite waagerecht
    //  Blinktakt an-/abschalten (nur bei Fokus + ohne Selektion, s. paint()).
    void  syncCaretBlink();
    //  QTextLayouts weit ausserhalb des Viewports freigeben (Hoehen bleiben).
    void  trimLayouts(int firstVisible, int lastVisible);
    //  Leere Absätze werden mit dem am Cursor GELTENDEN Format vermessen
    //  (inkl. Pending) - beim Wechsel des Cursor-Blocks bzw. bei einer
    //  Format-Änderung ohne Selektion muss ihr Layout daher verfallen.
    void  invalidateEmptyBlock(int i);
    //  Maus (Item-Koordinaten) -> (Block, Zeichenposition).
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
    //  Wie weit war der Fluss schon EINMAL vollständig paginiert? Nur bis
    //  hierhin darf `ensureOffsetsTo` abbrechen, sobald ein Block wieder
    //  denselben Fluss-Ausgang liefert wie zuvor (s. dort). Zurückgesetzt wird
    //  der Stand nur, wenn sich die Blockzahl ändert oder alles neu entsteht.
    int    m_offsetsHighWater = 0;
    //  Höchster Block, dessen HÖHE sich seit dem letzten vollständigen Lauf
    //  geändert hat. Der Abbruch oben darf erst GREIFEN, wenn der Lauf an ihm
    //  vorbei ist - sonst überspränge er Blöcke, die sich sehr wohl verschoben
    //  haben, und die gespeicherten Offsets dahinter wären veraltet (gemessen:
    //  Seitenzahl 218 statt 207, `docx.pdfexport` rot).
    int    m_offsetsDirtyMax = 0;
    int    m_layChunkAt = 0;                   // Fortschritt Initial-Layout
    int    m_trimLo = -1, m_trimHi = -1;       // zuletzt getrimmtes Layout-Fenster
    int    m_pageCount = 1;                    // s. pageCount()
    //  Kann in DIESEM Dokument überhaupt eine Nummerierung entstehen? Ist die
    //  Antwort nein, hat `rebuildMarkers` nichts zu tun - und der Lauf über
    //  ALLE Blöcke je Tastendruck entfällt (gemessen: 33 % eines Tastendrucks
    //  in einem Dokument mit 30.451 Blöcken). Wird nur GESETZT, nie gelöscht:
    //  eine entfernte Nummerierung muss der Lauf noch aufräumen dürfen.
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
    //  ── Fußnoten (Anzeige am Seitenfuß) ──────────────────────────────────────
    //  Slot -> Höhe des Bereichs (Trennlinie + Absätze + Abstände).
    //  Ausgelegte Fußnoten (lazy; klein, deshalb gehalten). KEIN QHash: der
    //  Eintrag hält `unique_ptr` und ist damit nicht kopierbar.

};

// ─────────────────────────────────────────────────────────────────────────────
//  DocxPageThumb - Miniatur EINER Seite. Absichtlich winzig und ohne eigenen
//  Zustand: sie ruft DocxTextArea::paintPageInto() auf, es gibt also nur EINEN
//  Zeichenweg für Ansicht und Miniatur und KEINEN Bild-Cache (RAM = Priorität
//  1). Gezeichnet wird nur, was die QML-ListView gerade als Delegate hält.
// ─────────────────────────────────────────────────────────────────────────────
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
