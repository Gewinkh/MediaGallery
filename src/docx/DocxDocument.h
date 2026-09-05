#pragma once
// Verlusterhaltendes Absatz- und Textlauf-Modell einer .docx: `word/document.xml` wird einmal dekodiert, jeder
// Absatz und Run merkt sich seine Herkunft als Span. Unangetastetes geht als Original-Teilstring wieder heraus.

#include <QString>
#include <QStringView>
#include <QColor>
#include <QList>
#include <QHash>

class QIODevice;

namespace Docx {

constexpr QChar kLineBreak(0x2028);
constexpr QChar kPageBreak(0xE000);
// w:br mit clear=all: bricht die Zeile UND setzt das naechste Band unter alle
// Stoerer - der einzige Weg, neben einer Tabelle wieder unter sie zu kommen.
constexpr QChar kClearBreak(0xE001);
constexpr QChar kObjectChar(0xFFFC);

// len == 0 heisst: nicht vorhanden.
struct Span {
    int start = 0;
    int len   = 0;
    bool valid() const { return len > 0; }
};

// set-Maske = explizit gesetzt (direkte Formatierung).
struct RunFmt {
    enum Field { FBold = 1, FItalic = 2, FUnderline = 4,
                 FSize = 8, FFont = 16, FColor = 32 };
    int     set = 0;
    bool    bold = false, italic = false, underline = false;
    qreal   sizePt = 11.0;
    QString font;
    QColor  color;

    bool operator==(const RunFmt& o) const {
        return set == o.set && bold == o.bold && italic == o.italic
               && underline == o.underline && qFuzzyCompare(sizePt + 1, o.sizePt + 1)
               && font == o.font && color == o.color;
    }
};

struct ParFmt {
    enum Field { FAlign = 1, FLine = 2, FBefore = 4, FAfter = 8, FNum = 16 };
    int     set = 0;
    int     align = 0;          // 0 links, 1 zentriert, 2 rechts, 3 Blocksatz
    qreal   lineSpacing = 1.0;  // Vielfaches (w:line/240 bei lineRule auto)
    qreal   beforePt = 0.0;     // w:before Twips/20
    qreal   afterPt  = 0.0;     // w:after  Twips/20
    int     numId = -1;         // Liste (−1 = keine)
    int     ilvl  = 0;
    QString styleId;            // w:pStyle (nur Anzeige-Auflösung)
};

struct Run {
    Span    rawSpan;            // gesamtes <w:r>…</w:r>
    Span    startTagSpan;       // "<w:r …>" (rsid-Attribute erhalten)
    Span    rprSpan;            // "<w:rPr>…</w:rPr>"
    QString rprXml;             // materialisiert, sobald Format geändert wurde
    bool    rprMaterialized = false;

    QString text;               // entkodiert (inkl. Sentinels)
    RunFmt  fmt;                // direkte Formatierung (geparst)
    bool    opaque = false;     // nicht verstandener Run - Raw bleibt verbatim
    bool    dirty  = false;     // Text/Format geändert -> aus Teilen serialisieren

    // Nur Anzeige: eingefuegter Text unterstrichen, geloeschter durchgestrichen.
    // Die Struktur bleibt unangetastet und geht verbatim heraus.
    enum Revision { RevNone = 0, RevInserted, RevDeleted };
    Revision revision = RevNone;
    QString  revAuthor;         // `w:author` (für die Farbe je Autor)

    // Ersatz-Rohform statt Mutation der Dokument-XML: die Spans aller spaeteren
    // Bloecke haengen an deren Offsets. Leer = unveraendert.
    QString rawOverride;

    QString currentRpr(QStringView docXml) const {
        if (rprMaterialized) return rprXml;
        if (rprSpan.valid()) return docXml.mid(rprSpan.start, rprSpan.len).toString();
        return {};
    }
};

struct Block {
    enum Kind {
        Paragraph = 0,
        OpaqueVisible,          // z. B. w:tbl - Platzhalter in der Anzeige
        OpaqueHidden            // z. B. w:sectPr, bookmarkStart - unsichtbar
    };
    Kind    kind = Paragraph;
    Span    rawSpan;            // gesamter Block im Original
    Span    startTagSpan;       // "<w:p …>"
    Span    pprSpan;            // "<w:pPr>…</w:pPr>"
    QString pprXml;             // materialisiert bei Absatzformat-Änderung
    bool    pprMaterialized = false;
    QString opaqueName;         // Elementname opaker Blöcke ("w:tbl", …)
    // Wie Run::rawOverride, aber fuer einen ganzen Block - noetig fuers w:sectPr.
    QString rawOverride;

    QList<Run> runs;
    ParFmt  pfmt;
    bool    dirty = false;      // Struktur/Text geändert -> Absatz serialisieren

    // tableId = Index in Document::tables(), -1 = kein Tabellenblock. col ist der
    // laufende Index der Zelle IN IHRER ZEILE, nicht die Gitterspalte.
    int     tableId = -1;
    int     row = -1, col = -1;

    QString plainText() const {
        QString t;
        for (const Run& r : runs) t += r.text;
        return t;
    }
    int textLength() const {
        int n = 0;
        for (const Run& r : runs) n += r.text.size();
        return n;
    }
    QString currentPpr(QStringView docXml) const {
        if (pprMaterialized) return pprXml;
        if (pprSpan.valid()) return docXml.mid(pprSpan.start, pprSpan.len).toString();
        return {};
    }
};

// Alle Laengen in Twips (1/1440 Zoll), genau wie im OOXML.
struct SectionProps {
    int  pageW  = 11906, pageH   = 16838;      // A4 hochkant
    int  marTop = 1417,  marRight = 1417;      // 2,5 cm
    int  marBottom = 1417, marLeft = 1417;
    int  cols      = 1;                        // w:cols/w:num
    int  colSpace  = 708;                      // Spaltenabstand (1,25 cm)
    bool landscape = false;                    // w:pgSz/@w:orient
};

// Nur Anzeige-Daten; die Formatwerte bleiben privat.
struct StyleInfo {
    QString id;                 // w:styleId - steht so im w:pStyle
    QString name;               // w:name (Anzeige; bei Bedarf = id)
    bool    isDefault = false;  // w:default="1" -> pStyle wird ENTFERNT
};

// Nur die XML-Klammern um den Zellinhalt, als Spans ins Original. Auch die
// Schluss-Tags sind Spans und keine Literale, weil zwischen ihnen weiteres
// Markup stehen kann, das sonst verloren ginge.
struct TableDef {
    Span headerSpan;             // "<w:tbl>" + tblPr + tblGrid (bis vor die 1. Zeile)
    Span footerSpan;             // "</w:tbl>"
    QVector<Span> rowSpans;      // je Zeile: "<w:tr …>" + trPr
    QVector<Span> rowEndSpans;   // je Zeile: "</w:tr>"
    QVector<Span> cellSpans;     // je Zelle: "<w:tc>" + tcPr
    QVector<Span> cellEndSpans;  // je Zelle: "</w:tc>"
    QVector<int>  cellRow;       // Zelle -> Zeilenindex
    QVector<int>  rowFirstCell;  // Zeile -> erster Zellindex (Zelle = rowFirstCell+col)
    // Beim Parsen mitgenommen, damit das Layout ohne erneutes XML-Parsen baut.
    QVector<int>  gridTw;        // w:tblGrid/w:gridCol - Spaltenbreiten in Twips
    QVector<int>  cellGridSpan;  // je Zelle: w:gridSpan (1 = keine Spanne)
    QVector<int>  cellWidthTw;   // je Zelle: w:tcW (0 = aus dem Gitter ableiten)
    int parentTableId = -1;      // −1 = oberste Ebene
    int parentCellIndex = -1;    // Zelle des Elternteils, die diese Tabelle enthält

    // Materialisierte Geruest-Teile: sobald das Geruest geaendert wird, emittiert
    // emitBlocks diesen String statt des Spans. Leer = Span nehmen.
    QString headerXml;
    QVector<QString> cellXml;    // je Zelle (Größe == cellSpans oder leer)
    // Zeile eingefuegt/geloescht: der Schnellpfad darf auch ohne dirty-Zelle nicht greifen.
    bool structDirty = false;
    // Stimmt die Zahl nicht mehr, muss der Schnellpfad entfallen - sonst emittiert
    // er den alten Teilstring und neuer Inhalt geht beim Speichern verloren.
    int blockCount = 0;

    Span rawSpan() const {
        const int end = footerSpan.start + footerSpan.len;
        return { headerSpan.start, end - headerSpan.start };
    }
};

// Reine Darstellungssicht, nur zum Auslegen gebaut - nicht die
// Bearbeitungsdarstellung. Die Tabelle bleibt ein opaker Block.
struct TableCell {
    QList<Block> paragraphs;    // nur pfmt + runs gefüllt (Spans nicht nötig)
    int gridSpan = 1;
    int widthTw  = 0;           // w:tcW, 0 = aus dem Gitter ableiten
};
struct TableRow {
    QList<TableCell> cells;
};
struct TableView {
    QList<int> gridTw;          // w:tblGrid - Spaltenbreiten in Twips
    QList<TableRow> rows;
    bool ok = false;            // false -> Platzhalter zeichnen wie bisher
};

// Die Eintraege stehen nicht in der Datei: das Feld bleibt deklarativ, Word
// rechnet die Zahlen selbst. Unsere Anzeige leitet sie aus der Paginierung ab.
struct TocEntry {
    QString text;
    int     level = 1;          // 1…9 (aus HeadingN)
    int     block = -1;         // Index in Document::blocks
    // Zeichenposition des ZEILENANFANGS. Ein Absatz kann mehrere Ueberschriften
    // tragen (nur durch w:br getrennt) und ueber eine Seitengrenze laufen.
    int     pos   = 0;
};

// Der Run bleibt opak; das hier ist nur, was die Anzeige braucht.
struct InlineImage {
    QString relId;              // r:embed -> Beziehung -> word/media/…
    int cxEmu = 0, cyEmu = 0;   // wp:extent (EMU: 1 Zoll = 914400)
    int run = -1;               // Index des Bild-Runs im Block
    int pos = 0;                // Zeichenposition des U+FFFC im Absatztext
    // Verankert (wp:anchor) statt in der Zeile: der Text fliesst um das Rechteck.
    // Nur Square wird ausgelegt, alles Uebrige verhaelt sich wie None.
    enum Wrap { WrapNone = 0, WrapSquare = 1 };
    // Largest ist Words Vorgabe: nimm die breitere Seite.
    enum WrapSide { SideBoth = 0, SideLeft = 1, SideRight = 2, SideLargest = 3 };
    bool anchored = false;
    int  wrap     = WrapNone;
    int  wrapSide = SideBoth;
    int  posXEmu  = 0;          // wp:positionH/wp:posOffset (Spalte)
    int  posYEmu  = 0;          // wp:positionV/wp:posOffset (Absatz)
    int  distLEmu = 0, distREmu = 0;
};

struct NumLevel {
    QString numFmt;             // "bullet" | "decimal" | …
    QString lvlText;            // z. B. "%1."
};

class Document {
public:
    bool load(const QString& path, QString* err = nullptr);
    // Spans, Selbstpruefung und Emission sind teil-unabhaengig; Beziehungen kommen
    // aus der .rels DIESES Teils.
    bool loadPart(const QString& docxPath, const QString& partPath,
                  QString* err = nullptr);
    QString partPath() const { return m_partPath; }

    // newDocumentXml baut das neue document.xml, replacementParts liefert weitere
    // ZIP-Eintraege, writeTo schreibt den kompletten Container.
    QString newDocumentXml() const;
    QHash<QString, QByteArray> replacementParts() const;
    bool writeTo(QIODevice* target, QString* err = nullptr) const;
    // Zusaetzlich mit fremden Ersatzteilen - so schreibt der Controller Medien mit.
    bool writeTo(QIODevice* target, const QHash<QString, QByteArray>& extraParts,
                 QString* err) const;

    QString path() const { return m_path; }
    QStringView docXml() const { return m_docXml; }

    QList<Block> blocks;        // vom Controller mutiert (Undo über Kopien)

    RunFmt resolveRun(const Block& b, const Run& r) const;
    // Word legt im w:pPr/w:rPr das Format der Absatzmarke ab und wendet es auf ein
    // Feldergebnis an - darueber stellt der Editor die Schrift des Verzeichnisses ein.
    RunFmt paragraphMarkFormat(const Block& b) const;
    ParFmt resolvePar(const Block& b) const;
    const RunFmt& defaultRun() const { return m_defRun; }

    // Ist es false, kann eine Nummerierung nur aus b.pfmt stammen - rebuildMarkers
    // darf die Vorlagenaufloesung dann ganz ueberspringen.
    bool stylesMayNumber() const { return m_stylesMayNumber; }

    // Gefiltert auf die zumutbaren Vorlagen; leer ohne styles.xml.
    const QList<StyleInfo>& paragraphStyles() const { return m_parStyles; }
    QString defaultParagraphStyleId() const { return m_defaultParStyle; }
    // w:pStyle auf eine undefinierte id loest wie eine fehlende Vorlage auf.
    bool hasStyle(const QString& id) const { return m_styles.contains(id); }
    // Die meisten .docx bringen keine Ueberschriftvorlage mit - ohne das liesse
    // sich darin keine Ueberschrift schreiben. Ein Undo laesst sie inert stehen.
    QString ensureHeadingStyle(int level);
    static constexpr int kMaxHeadingLevel = 3;

    // Neues XML wird an m_docXml ANGEHAENGT, die neuen Spans zeigen dorthin:
    // bestehende Offsets bleiben gueltig. Der Anhang wird nie als Ganzes emittiert.
    int appendPool(const QString& xml);          // -> Start-Offset im Pool

    // Liefert den Index des ersten neuen Zellblocks, -1 bei unbrauchbaren Massen.
    int insertTable(int beforeBlock, int rows, int cols);

    bool isTocParagraph(const Block& b) const;
    // Erkannt wird die styleId HeadingN - so heisst sie in jeder Sprachfassung
    // von Word, nur der Anzeigename ist uebersetzt. Zellbloecke bleiben aussen vor.
    QList<TocEntry> tocEntries(int maxLevel = 3) const;
    // Als w:fldSimple ohne eingebackene Seitenzahlen - die Datei bleibt deklarativ.
    int insertToc(int beforeBlock, int maxLevel = 3);

    // Die Datei wird als neuer ZIP-Teil vorgemerkt, die Zeichnung kommt in den Pool.
    int insertImage(int beforeBlock, const QString& localPath, QString* err);
    // cxEmu/cyEmu > 0 geben die Anzeigegroesse vor, 0 rechnet aus den nativen
    // Pixeln. Auf die Textbreite gedeckelt wird in beiden Faellen.
    int insertImageData(int beforeBlock, const QByteArray& bytes,
                        const QString& ext, QString* err,
                        qint64 cxEmu = 0, qint64 cyEmu = 0);
    // Als w:drawing-Run vor runIdx - so entstehen zwei Bilder nebeneinander mit
    // Text daneben, in der Datei ganz normales wp:inline.
    int insertImageRunAt(int blockIdx, int runIdx, const QByteArray& bytes,
                         const QString& ext, QString* err,
                         qint64 cxEmu = 0, qint64 cyEmu = 0);
    // Der Bild-Run bleibt opak; die neue Zeichnung kommt in den Anhang-Pool.
    bool setImageSizeEmu(int blockIdx, qint64 cxEmu, qint64 cyEmu);
    bool setImageSizeEmu(int blockIdx, int runIdx, qint64 cxEmu, qint64 cyEmu);
    // wp:inline <-> wp:anchor + w:wrapSquare. Alles Uebrige am Bild (Zuschnitt,
    // Effekte, Alternativtext) bleibt unangetastet.
    bool setImageWrap(int blockIdx, int runIdx, bool floating);
    // Der Run wandert mit seinem Roh-Span, bleibt also byteweise erhalten; nur
    // seine Lage ist danach relativ zum neuen Absatz.
    int moveImageRun(int srcBlock, int runIdx, int dstBlock);
    // wp:posOffset in EMU, relativ zu Textspalte und Absatz - was Ziehen schreibt.
    bool setImageAnchorEmu(int blockIdx, int runIdx, int posXEmu, int posYEmu);
    bool setImageWrapSide(int blockIdx, int runIdx, int side);
    bool imageOfRun(int blockIdx, int runIdx, InlineImage* out) const;

    const QVector<TableDef>& tables() const { return m_tables; }
    int tableFirstBlock(int tableId) const;
    int tableLastBlock(int tableId) const;

    // Der Aufrufer klammert sie in einen EditScope samt TableDef-Schnappschuss.
    int  tableRowCount(int tableId) const;
    int  tableColumnCount(int tableId) const;   // Zellen der ersten Zeile
    // Verbundene Zellen oder ungleiche Zellzahl werden nicht angefasst - lieber
    // ablehnen als das Gitter zerreissen.
    bool tableStructEditable(int tableId) const;
    // In Twips; die Groesse muss der Spaltenzahl entsprechen.
    QVector<int> tableColumnWidths(int tableId) const;

    bool tableInsertRow(int tableId, int atRow);
    bool tableDeleteRow(int tableId, int row);
    bool tableInsertColumn(int tableId, int atCol);
    bool tableDeleteColumn(int tableId, int col);
    bool tableSetColumnWidths(int tableId, const QVector<int>& widthsTw);

    TableDef tableDef(int tableId) const;
    void     setTableDef(int tableId, const TableDef& def);

    // Tolerant: bei beschaedigtem Fragment kommt ok == false zurueck. Eine
    // verschachtelte Tabelle wird als Platzhalter gefuehrt, nicht rekursiv zerlegt.
    TableView parseTableForDisplay(const Block& b) const;

    // Nur dieser Fall wird dargestellt - ein Bild mitten im Text braeuchte einen
    // Inline-Objekt-Handler, den QTextLayout ohne QTextDocument nicht kennt.
    bool paragraphImage(const Block& b, InlineImage* out) const;
    // Alle Bilder in Textreihenfolge, mit Run-Index und Stelle des Objekt-Zeichens.
    QVector<InlineImage> paragraphImages(const Block& b) const;
    QString relTarget(const QString& relId) const;
    // Oeffnet das ZIP erneut - die Bytes werden bewusst nicht gehalten.
    QByteArray partBytes(const QString& zipPath) const;
    QByteArray imageBytes(const QString& relId) const;

    // accept = die Aenderung gilt, !accept = zuruecknehmen. Gearbeitet wird ueber
    // rawOverride und Entfernen von Runs, nicht ueber eine XML-Mutation.
    bool applyRevision(int blockIdx, int runIdx, bool accept);

    // Die Werte des LETZTEN w:sectPr im Koerper. Abschnittswechsel mitten im
    // Dokument werden bewusst nicht abgebildet - eine Geometrie je Dokument.
    const SectionProps& section() const { return m_section; }

    // Twips wie in w:pgMar, geklemmt wie beim Einlesen. Fehlt w:sectPr, entsteht
    // eines mit der bis dahin geltenden Seitengroesse.
    bool setPageMargins(int top, int right, int bottom, int left);
    // Werte und umgeschriebenes w:sectPr gehoeren zusammen - die Werte treiben die
    // Auslegung, das XML das Speichern.
    struct SectionState {
        SectionProps props;
        QString      sectPrXml;      // leer = kein eigener Roh-Bereich gesetzt
    };
    SectionState sectionState() const;
    void         setSectionState(const SectionState& st);

    NumLevel numLevel(int numId, int ilvl) const;
    // Legt lazy eigene abstractNum/num-Definitionen an, die beim Speichern in
    // word/numbering.xml gespliced werden.
    int newListNum(bool bullet);

    static QByteArray emptyDocxBytes(const QString& title);   // leeres A4-Dokument
    static QString    plainTextPreview(const QString& path, int maxLines);
    static QString    xmlEscape(const QString& s);
    static QString    serializeRunsText(const QString& text); // Text -> <w:t>/…
    // Oeffentlich: der Controller schreibt damit das Zeichenformat des
    // Verzeichnis-Absatzes; der Feld-Run selbst bleibt opak.
    QString buildRPrXml(const RunFmt& f) const;

    // Kanonisch geordnet; alle uebrigen Kinder bleiben verbatim erhalten.
    static QString upsertProp(const QString& prXml, const QString& wrapTag,
                              const QString& propName, const QString& newXml,
                              const QStringList& order);

    // Oeffentlich, weil die Tabellen-Anzeigesicht dieselben Regeln braucht.
    static void parseRunProps(QStringView xml, RunFmt* out);
    static void parseParProps(QStringView xml, ParFmt* out);

private:
    struct StyleDef {
        QString basedOn;
        RunFmt  rf;
        ParFmt  pf;
    };

    // Der eine Schreibweg fuer den Rahmen: die Kinder werden uebernommen.
    bool rewriteDrawingFrame(int blockIdx, int runIdx, bool floating,
                             int posXEmu, int posYEmu, int wrapSide,
                             bool requireModeChange);

    bool parseDocumentXml(QString* err);
    void parseSectPr(QStringView xml);       // w:pgSz/w:pgMar/w:cols -> m_section
    // Dieselben Regeln wie beim Einlesen - eine Stelle fuer beide Wege.
    static void clampSection(SectionProps* s);
    // Legt eines am Ende des Koerpers an; -1 nur bei leerem Dokument.
    int  ensureSectPrBlock();
    // w:header/w:footer/w:gutter werden uebernommen - sie gehoeren nicht uns.
    bool rewriteSectPr();
    int  m_sectPrBlock = -1;
    bool parseStylesXml(const QByteArray& xml);
    bool parseNumberingXml(const QByteArray& xml);

    QString buildParagraphXml(const Block& b) const;
    // rawOnly = nur Original-Spans, damit die Selbstpruefung genau den Speicherweg prueft.
    QString emitBlocks(bool rawOnly) const;
public:
    // Oeffentlich, damit der Fussnoten-Teil einzeln geprueft werden kann.
    QString buildPartXml() const {
        QString out;
        out.reserve(m_docXml.size() + 512);
        out += QStringView(m_docXml).mid(m_bodyPrefix.start, m_bodyPrefix.len);
        out += emitBlocks(false);
        out += QStringView(m_docXml).mid(m_bodySuffix.start, m_bodySuffix.len);
        return out;
    }
private:
    QHash<QString, QByteArray> numberingParts() const;   // Listen-Infrastruktur
    QHash<QString, QByteArray> mediaParts() const;       // eingefügte Bilder
    QHash<QString, QByteArray> stylesParts(
        const QHash<QString, QByteArray>& base) const;   // neu angelegte Vorlagen
    QString buildRunXml(const Run& r) const;
    // Gemeinsamer Kern von insertImageData und insertImageRunAt.
    QString buildImageRunXml(const QByteArray& bytes, const QString& ext,
                             QString* err, qint64 cxEmu, qint64 cyEmu);

    QString tableHeaderText(const TableDef& d) const;
    QString tableCellText(const TableDef& d, int cellIdx) const;
    void    materializeGrid(TableDef& d, const QVector<int>& widthsTw);
    // last = -1, wenn die Zeile leer ist.
    static void rowCellRange(const TableDef& d, int row, int* first, int* last);

    QString m_path;
    QString m_partPath = QStringLiteral("word/document.xml");   // s. partPath()
    QString m_docXml;            // dekodierter Teil (EINZIGE Kopie)
    Span    m_bodyPrefix;        // alles vor dem ersten Block (inkl. <w:body>)
    Span    m_bodySuffix;        // alles nach dem letzten Block (</w:body>…)

    RunFmt  m_defRun;            // docDefaults (vollständig belegt)
    ParFmt  m_defPar;
    QHash<QString, StyleDef> m_styles;
    bool    m_stylesMayNumber = false;   // s. stylesMayNumber()
    QList<StyleInfo> m_parStyles;        // s. paragraphStyles()
    QString m_defaultParStyle;           // s. defaultParagraphStyleId()
    SectionProps m_section;              // s. section()
    QVector<TableDef> m_tables;          // s. tables()
    // Bytes bleiben bis zum Speichern im Zugriff, damit die Anzeige das Bild sofort
    // zeigen kann - im Container liegt es ja noch nicht.
    struct PendingMedia {
        QString    zipName;              // "word/media/mg1.png"
        QString    relId;                // "rIdMGimg1"
        QString    ext;                  // "png"
        QByteArray bytes;
    };
    QList<PendingMedia> m_pendingMedia;
    int m_nextMediaId = 1;
    QHash<QString, QString> m_rels;      // rId -> Ziel (relativ zu word/)
    // Art -> rId (w:headerReference/w:footerReference des Hauptabschnitts).
    QHash<QString, QString> m_hdrRefs;   // "default"/"first"/"even"
    QHash<QString, QString> m_ftrRefs;

    QHash<int, QHash<int, NumLevel>> m_numLevels;    // numId -> ilvl -> Level
    QHash<int, int> m_numToAbstract;
    mutable int m_ownAbstractBullet = -1;
    mutable int m_ownAbstractDecimal = -1;
    QList<QPair<int, bool>> m_pendingNums;           // (numId, bullet)
    int  m_nextNumId = 1;
    int  m_nextAbstractId = 0;
    bool m_hadNumberingPart = false;
    QString m_numberingXml;      // dekodierter Bestand (für Splice), sonst leer

    bool m_hadStylesPart = false;
    QString m_stylesXml;         // dekodierter Bestand (für Splice), sonst leer
    // In Anlegereihenfolge; beim Speichern vor </w:styles> gespliced.
    QStringList m_pendingStyles;

};

} // namespace Docx
