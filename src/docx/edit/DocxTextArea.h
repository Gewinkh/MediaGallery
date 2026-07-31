#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxTextArea — die selbstgebaute Anzeige-/Eingabefläche des DOCX-Editors
//  (QQuickPaintedItem; bewusst KEIN QML-TextEdit/RichText — Anforderung des
//  Auftrags: eigenes Absatz-/Zeilenmodell in C++; QTextLayout ist als reines
//  Shaping-/Zeilenumbruch-Primitiv UNTER diesem Modell ausdrücklich erlaubt).
//
//  RAM/Architektur:
//   • Das Item ist VIEWPORT-groß (nicht inhaltsgroß!) — QQuickPaintedItem
//     hält eine Textur in Item-Größe; ein 100-Seiten-Dokument als Textur
//     wäre ein VRAM-Desaster. Gescrollt wird über die contentY-Property
//     (QML-Scrollbar/Wheel), paint() zeichnet nur den sichtbaren Streifen.
//   • Layout-Cache je Absatz (QTextLayout + Höhe), Präfix-Offsets mit
//     dirtyFrom-Invalidierung → Bearbeitungen relayouten nur ab dem
//     betroffenen Block (inkrementell, Regel 17).
//   • Initial-Layout gechunkt über einen 0-ms-Timer (~300 Blöcke je Tick):
//     lange Dokumente blockieren den GUI-Thread nie; noch nicht vermessene
//     Blöcke tragen eine Zeilen-Schätzhöhe, die beim echten Layout ersetzt
//     wird (Scrollbalken bleibt dabei stabil nutzbar).
//
//  Eingabe: Maus (Cursor/Selektion/Doppelklick-Wort), Tastatur (Zeichen,
//  Pfeile/Home/End ± Shift, Enter/Backspace/Entf, Strg+A/C/X/V/Z/Shift+Z,
//  Strg+B/I/U, Strg+S → saveRequested) sowie Eingabemethoden-Commits
//  (tote Tasten/IME). Alle Mutationen laufen über den DocxEditController.
// ─────────────────────────────────────────────────────────────────────────────

#include <QQuickPaintedItem>
#include <QImage>
#include <QTextLayout>
#include <QTimer>
#include <QVector>
#include <memory>
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
    Q_PROPERTY(qreal cursorY READ cursorY NOTIFY cursorRectChanged)
    Q_PROPERTY(qreal cursorH READ cursorH NOTIFY cursorRectChanged)
    //  i18n-Texte kommen aus QML (App.uiText) — kein Strings-Coupling hier.
    Q_PROPERTY(QString tablePlaceholder MEMBER m_tablePlaceholder NOTIFY labelsChanged)
    Q_PROPERTY(QString pageBreakLabel MEMBER m_pageBreakLabel NOTIFY labelsChanged)
    //  Beschriftung, wenn ein Inhaltsverzeichnis (noch) keine Überschriften
    //  findet — sonst wäre der Absatz eine leere Fläche.
    Q_PROPERTY(QString tocEmptyLabel MEMBER m_tocEmptyLabel NOTIFY labelsChanged)
    //  Umgebungsgrund (Theme) um die WEISSE Word-Seite herum — QML bindet
    //  App.themeBase; die Seite selbst bleibt bewusst immer weiß (wie Word).
    Q_PROPERTY(QColor surroundColor MEMBER m_surroundColor NOTIFY labelsChanged)
    //  Paginierung: Seitenzahl des Dokuments und die Seite am Cursor (1-basiert
    //  in der Anzeige, hier 0-basiert) — Grundlage der Miniaturen-Leiste.
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageCountChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    //  AUSGEWÄHLTES BILD: der Cursorblock, falls er ein reiner Bild-Absatz ist
    //  (sonst −1). Es gibt bewusst keinen zweiten Auswahlzustand — ein Klick
    //  aufs Bild setzt den Cursor dorthin, das IST die Auswahl. Das Rechteck
    //  steht in ITEM-Pixeln, damit QML die Ziehpunkte direkt darüberlegen kann.
    Q_PROPERTY(int selImageBlock READ selImageBlock NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageX READ selImageX NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageY READ selImageY NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageW READ selImageW NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selImageH READ selImageH NOTIFY imageSelectionChanged)
    //  AUSGEWÄHLTE TABELLE — dieselbe Regel wie beim Bild: kein zweiter
    //  Auswahlzustand, sondern „der Cursor steht in einer Tabelle". Rechteck
    //  in ITEM-Pixeln, damit QML Rahmen und Ziehpunkte darüberlegen kann.
    Q_PROPERTY(int selTableId READ selTableId NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selTableX READ selTableX NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selTableY READ selTableY NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selTableW READ selTableW NOTIFY imageSelectionChanged)
    Q_PROPERTY(qreal selTableH READ selTableH NOTIFY imageSelectionChanged)

public:
    explicit DocxTextArea(QQuickItem* parent = nullptr);
    ~DocxTextArea() override;

    DocxEditController* ctl() const { return m_ctl; }
    void setCtl(DocxEditController* c);

    qreal contentY() const { return m_contentY; }
    void  setContentY(qreal y);
    qreal contentHeight() const { return m_contentHeight; }
    //  m_cursorRect liegt in Dokument-Pixeln; QML rechnet mit Item-Pixeln.
    qreal cursorY() const { return m_cursorRect.y() * m_scale; }
    qreal cursorH() const { return m_cursorRect.height() * m_scale; }

    int   pageCount() const { return m_pageCount; }
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
    //  Oberkante einer Seite in ITEM-Pixeln — QML setzt damit contentY, wenn
    //  eine Miniatur angeklickt wird.
    Q_INVOKABLE qreal pageTop(int page);

    //  EINE Seite in ein Zielrechteck malen (Miniaturen, s. DocxPageThumb).
    //  Nutzt denselben Zeichenweg wie paint() — es gibt keine zweite Darstellung
    //  und keinen Bild-Cache (RAM = Priorität 1).
    void paintPageInto(QPainter* p, int page, const QRectF& target);

    void paint(QPainter* p) override;

signals:
    void ctlChanged();
    void contentYChanged();
    void contentHeightChanged();
    void cursorRectChanged();
    void labelsChanged();
    void saveRequested();
    void pageCountChanged();
    void currentPageChanged();
    //  Inhalt hat sich geändert (Laden/Bearbeiten) — die Miniaturen zeichnen neu.
    void documentChanged();
    //  Auswahl/Lage des Bildes hat sich geändert (Cursor, Bearbeitung, Scrollen,
    //  Maßstab) — QML richtet die Ziehpunkte neu aus.
    void imageSelectionChanged();
    //  Rechtsklick in der Fläche: Position in ITEM-Pixeln + getroffener Block.
    //  Das Menü selbst baut QML (gethemt) — hier gibt es keine UI-Texte.
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
    //  DOKUMENT-y (Seitenstapel): was man SIEHT — Seiten mit Rand und Lücke
    //    dazwischen. Ein Absatz kann über mehrere Slots laufen; welche Zeilen
    //    wo liegen, steht in seinen Segmenten.
    //  ITEM-Pixel (contentY/contentHeight/cursorY): Dokument-y × m_scale. Der
    //    Maßstab passt eine zu breite Seite in eine schmale Kachel ein und ist
    //    ein reiner Zeichen-Faktor — das Layout selbst bleibt seitengenau.
    struct PageSeg {
        int   slot      = 0;                   // Spalten-Slot dieses Stücks
        int   firstLine = 0;                   // erste Zeile des Stücks
        qreal yInSlot   = 0.0;                 // Oberkante im Slot
    };
    //  ── Tabellen-ANZEIGE (read-only) ─────────────────────────────────────────
    //  Eine Tabelle bleibt EIN Block (beim Speichern byteidentisch). Sie wird
    //  aber als echtes Gitter ausgelegt, damit ihre HÖHE stimmt — mit dem
    //  früheren festen Platzhalter von 34 px war jeder Seitenumbruch NACH einer
    //  Tabelle falsch. Bearbeiten von Zellen ist (noch) nicht möglich.
    struct CellLayout {
        //  Nur noch für NICHT flach zerlegte Tabellen (opaker Block) — dort
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

    struct BlockLayout {
        std::unique_ptr<QTextLayout> layout;   // nullptr = noch nicht vermessen
        std::unique_ptr<TableLayout> table;    // nur bei w:tbl (s. isTable)
        bool    isTable = false;               // Block ist eine deutbare Tabelle
        //  Absatz besteht NUR aus einem Bild: das eingepasste QImage wird im
        //  Layout-Fenster gehalten (nicht die Originalbytes) und mit dem Layout
        //  wieder freigegeben. `imageBoxH` bleibt gültig, damit die Höhe steht.
        bool    isImage = false;
        QImage  image;
        //  ── Inhaltsverzeichnis ───────────────────────────────────────────────
        //  Die EINTRÄGE stehen nicht in der Datei (das Feld bleibt deklarativ),
        //  sie werden hier zum Auslegen gehalten. Die SEITENZAHL steht bewusst
        //  NICHT dabei: sie ist erst nach der Paginierung bekannt, hängt aber
        //  nicht an der Höhe (die Zahl der Einträge steht fest) — deshalb füllt
        //  sie erst `paint` ein und das Verzeichnis braucht keinen zweiten Pass.
        bool    isToc = false;
        QList<Docx::TocEntry> tocEntries;
        qreal   tocLineH = 0.0;
        qreal   imageBoxW = 0.0, imageBoxH = 0.0;
        //  ── Zellblock einer flach zerlegten Tabelle ──────────────────────────
        //  Der FLUSS bleibt monoton, weil die ganze Tabelle am Anker hängt (nur
        //  er trägt Höhe). Die Lage im Dokument steht deshalb hier EXPLIZIT,
        //  relativ zur Oberkante/linken Textkante des Ankers — genau so, wie
        //  Spalten ihre eigene x-Lage haben, ohne den Fluss zu verbiegen.
        bool    isCell = false;
        qreal   cellRelX = 0.0, cellRelY = 0.0, cellW = 0.0;
        qreal   height = 0.0;                  // inkl. Abstand davor/danach
        qreal   beforePx = 0.0;                // Versatz bis zur ersten Zeile
        qreal   indentPx = 0.0;                // Listen-Einzug
        QString marker;                        // "• " / "3. " (Listen)
        bool    laid = false;                  // height ist ECHT (nicht Schätzung)
        //  Leer = ein Stück, ganz im Slot von blockSlot(i) (Normalfall). Mehr
        //  als ein Eintrag nur für Absätze, die eine Seiten-/Spaltengrenze
        //  überschreiten.
        QVector<PageSeg> segs;
    };

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
    void  paintSlot(QPainter* p, int slot, bool withCaret);

    void  rebuildAll();                        // Dokument (neu) geladen
    void  invalidateFrom(int first, int oldCount, int newCount);
    void  ensureLaid(int i);                   // Layout eines Blocks erzwingen
    void  ensureOffsetsTo(int i);              // Präfix-Offsets bis i gültig
    //  Einen Block auf Slots verteilen (füllt seine Segmente) und das Fluss-y
    //  NACH ihm zurückgeben. Kern der Paginierung.
    qreal paginateBlock(int idx, qreal flowStart, qreal slotH);
    qreal blockTop(int i);                     // Inhalts-y des Blocks
    int   blockAtY(qreal y);                   // Block unter Inhalts-y
    void  layoutChunk();                       // Timer-Tick des Initial-Layouts
    void  updateContentHeight();
    void  rebuildMarkers();                    // Listen-Zähler (ganzes Dokument)
    void  buildLayout(int i);                  // QTextLayout eines Absatzes
    void  buildTableLayout(int i);             // Gitter-Layout eines w:tbl-Blocks
    void  buildFlatTableLayout(int anchor);    // Gitter aus den LEBENDEN Zellblöcken
    int   tableAnchorOf(int i) const;          // Zellblock → Anker (−1 = keiner)
    //  Bild-Absatz auslegen (true = war einer). `availWidth` ≤ 0 bedeutet die
    //  Textbreite der Spalte; in einer Tabellenzelle wird deren Breite übergeben.
    bool  buildImageLayout(int i, qreal availWidth = -1.0);
    //  Inhaltsverzeichnis-Feld auslegen (true = war eines). Reserviert je
    //  Überschrift eine Zeile; gezeichnet wird in `paintToc`.
    bool  buildTocLayout(int i);
    void  paintToc(QPainter* p, const BlockLayout& L, qreal left, qreal y,
                   qreal width);
    //  1-basierte Seitenzahl eines Blocks (für die Verzeichnis-Einträge).
    int   pageOfBlock(int i);
    void  ensureHeaderFooter();                // Kopf-/Fußzeile lazy auslegen
    void  paintHeaderFooter(QPainter* p, int page);
    void  paintTable(QPainter* p, const BlockLayout& L, qreal left, qreal y);
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
    //  (inkl. Pending) — beim Wechsel des Cursor-Blocks bzw. bei einer
    //  Format-Änderung ohne Selektion muss ihr Layout daher verfallen.
    void  invalidateEmptyBlock(int i);
    //  Maus (Item-Koordinaten) → (Block, Zeichenposition).
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

    std::vector<BlockLayout> m_lay;   // move-only (unique_ptr) → std::vector
    QVector<qreal>       m_offsets;            // Präfix-Summen der Höhen
    int    m_offsetsValidTo = 0;               // Offsets [0..N] gültig
    int    m_layChunkAt = 0;                   // Fortschritt Initial-Layout
    int    m_trimLo = -1, m_trimHi = -1;       // zuletzt getrimmtes Layout-Fenster
    //  Kopf-/Fußzeile des Hauptabschnitts, EINMAL ausgelegt und für jede
    //  Seite wiederverwendet (sie ist auf allen Seiten dieselbe).
    struct RunningPart {
        std::vector<std::unique_ptr<QTextLayout>> paras;
        qreal height = 0.0;
        bool  built = false;
    };
    RunningPart m_header, m_footer;
    int    m_pageCount = 1;                    // s. pageCount()
    int    m_lastPage = -1;                    // letzter gemeldeter currentPage
    qreal  m_scale = 1.0;                      // Dokument-Pixel → Item-Pixel
    QTimer m_chunkTimer;
    QTimer m_blinkTimer;
    bool   m_caretOn = true;
    bool   m_selecting = false;
    qreal  m_goalX = -1.0;                     // Wunsch-x für ↑/↓
    int    m_lastCursorBlock = -1;             // s. invalidateEmptyBlock()
    int    m_tblSelId = -1;                    // s. selTableId()
    QRectF m_tblSelDoc;                        // Tabellenrechteck in Dokument-Px
    int    m_imgSelBlock = -1;                 // s. selImageBlock()
    QRectF m_imgSelDoc;                        // Bildrechteck in Dokument-Pixeln
};

// ─────────────────────────────────────────────────────────────────────────────
//  DocxPageThumb — Miniatur EINER Seite. Absichtlich winzig und ohne eigenen
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
