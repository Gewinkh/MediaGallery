#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxDocument — verlusterhaltendes Absatz-/Textlauf-Modell einer .docx-Datei.
//
//  VERLUSTERHALTUNGS-PRINZIP (bindend, §0): word/document.xml wird EINMAL
//  dekodiert (m_docXml) und jeder Absatz/Textlauf merkt sich seine Herkunft
//  als SPAN [start,len) in diesem Original. Beim Speichern werden:
//   • unangetastete Blöcke/Runs als ORIGINAL-TEILSTRING re-emittiert
//     (→ byteidentisch nach UTF-8-Re-Encoding),
//   • geänderte Absätze aus ihren Teilen zusammengesetzt, wobei unberührte
//     Fragmente (Start-Tag, pPr, rPr, opake Runs) VERBATIM aus dem Original
//     stammen — nur die tatsächlich betroffenen Knoten ändern sich,
//   • alle übrigen ZIP-Einträge byteidentisch roh kopiert (DocxZip::addRaw).
//  Das Dokument wird NIE komplett aus dem Editier-Modell neu generiert.
//
//  SELBSTPRÜFUNG: Direkt nach dem Parsen wird das Original aus Prefix +
//  Block-Spans + Suffix rekonstruiert und mit m_docXml verglichen. Nur bei
//  exakter Übereinstimmung ist die Datei editierbar — sonst schlägt load()
//  fehl (lieber nicht editieren als still Inhalte verlieren).
//
//  Text-Sentinels im entkodierten Run-Text (Rückabbildung beim Serialisieren):
//   '\t' = <w:tab/> · U+2028 = <w:br/>/<w:cr/> (QTextLayout-Zeilenumbruch) ·
//   U+E000 = <w:br w:type="page"/> (Seitenumbruch-MARKER, Aufgabe 2) ·
//   U+FFFC = atomarer opaker Run (Zeichnung/Feld — Raw bleibt verbatim).
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QStringView>
#include <QColor>
#include <QList>
#include <QHash>

class QIODevice;

namespace Docx {

// Zeichen-Sentinels (s. Kopfkommentar).
constexpr QChar kLineBreak(0x2028);
constexpr QChar kPageBreak(0xE000);
constexpr QChar kObjectChar(0xFFFC);

// Span in m_docXml (QChar-Offsets). len==0 ⇒ nicht vorhanden.
struct Span {
    int start = 0;
    int len   = 0;
    bool valid() const { return len > 0; }
};

// ── Zeichenformat (direkte Formatierung; set-Maske = explizit gesetzt) ───────
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

// ── Absatzformat ─────────────────────────────────────────────────────────────
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

// ── Textlauf ─────────────────────────────────────────────────────────────────
struct Run {
    // Herkunft (leer bei neu erzeugten Runs):
    Span    rawSpan;            // gesamtes <w:r>…</w:r>
    Span    startTagSpan;       // "<w:r …>" (rsid-Attribute erhalten)
    Span    rprSpan;            // "<w:rPr>…</w:rPr>"
    QString rprXml;             // materialisiert, sobald Format geändert wurde
    bool    rprMaterialized = false;

    QString text;               // entkodiert (inkl. Sentinels)
    RunFmt  fmt;                // direkte Formatierung (geparst)
    bool    opaque = false;     // nicht verstandener Run — Raw bleibt verbatim
    bool    dirty  = false;     // Text/Format geändert → aus Teilen serialisieren

    QString currentRpr(QStringView docXml) const {
        if (rprMaterialized) return rprXml;
        if (rprSpan.valid()) return docXml.mid(rprSpan.start, rprSpan.len).toString();
        return {};
    }
};

// ── Block (Absatz oder opaker Fremdblock) ────────────────────────────────────
struct Block {
    enum Kind {
        Paragraph = 0,
        OpaqueVisible,          // z. B. w:tbl — Platzhalter in der Anzeige
        OpaqueHidden            // z. B. w:sectPr, bookmarkStart — unsichtbar
    };
    Kind    kind = Paragraph;
    Span    rawSpan;            // gesamter Block im Original
    Span    startTagSpan;       // "<w:p …>"
    Span    pprSpan;            // "<w:pPr>…</w:pPr>"
    QString pprXml;             // materialisiert bei Absatzformat-Änderung
    bool    pprMaterialized = false;
    QString opaqueName;         // Elementname opaker Blöcke ("w:tbl", …)

    QList<Run> runs;
    ParFmt  pfmt;
    bool    dirty = false;      // Struktur/Text geändert → Absatz serialisieren

    //  Tabellen-Zugehörigkeit (Option A: Zellinhalt liegt FLACH in `blocks`).
    //  tableId = Index in Document::tables(), −1 = kein Tabellenblock.
    //  `col` ist der laufende Index der Zelle IN IHRER ZEILE (dicht, 0-basiert)
    //  — nicht die Gitterspalte; w:gridSpan wird erst im Layout aufgelöst.
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

// ── Seiteneinrichtung (w:sectPr) — Grundlage der Paginierung ─────────────────
//  Alle Längen in TWIPS (1/1440 Zoll), also genau so, wie sie im OOXML stehen;
//  die Umrechnung in Pixel gehört in die Anzeige (DocxTextArea::kPtToPx).
struct SectionProps {
    int  pageW  = 11906, pageH   = 16838;      // A4 hochkant
    int  marTop = 1417,  marRight = 1417;      // 2,5 cm
    int  marBottom = 1417, marLeft = 1417;
    int  cols      = 1;                        // w:cols/w:num
    int  colSpace  = 708;                      // Spaltenabstand (1,25 cm)
    bool landscape = false;                    // w:pgSz/@w:orient
};

// ── Absatzvorlage für die Auswahlliste (Formatvorlagen) ──────────────────────
//  Nur Anzeige-Daten: die Formatwerte selbst bleiben privat (StyleDef) und
//  werden ausschließlich über resolveRun/resolvePar wirksam.
struct StyleInfo {
    QString id;                 // w:styleId — steht so im w:pStyle
    QString name;               // w:name (Anzeige; bei Bedarf = id)
    bool    isDefault = false;  // w:default="1" → pStyle wird ENTFERNT
};

// ── Tabellen-GERÜST (w:tbl) — die Klammern um den Zellinhalt ─────────────────
//  Der Zellinhalt selbst sind reguläre `Paragraph`-Blöcke in `Document::blocks`
//  (Option A). Hier stehen nur die XML-Klammern, die beim Serialisieren wieder
//  darum gelegt werden — als Spans ins Original, also verlusterhaltend.
//
//  WARUM AUCH DIE SCHLUSS-TAGS als Span und nicht als Literal "</w:tc>": zwischen
//  zwei Zellen dürfen Leerraum, Kommentare und Verarbeitungsanweisungen stehen.
//  Ein Literal würde die verlieren und die Selbstprüfung zu Recht scheitern
//  lassen. Ein Span kostet 8 Byte und ist exakt.
//
//  REKURSIV vorbereitet: parentTableId/parentCellIndex zeigen auf die Zelle des
//  Elternteils, in der diese Tabelle steckt (−1 = oberste Ebene). Verschachtelte
//  Tabellen werden derzeit NICHT zerlegt, aber das Feld muss dafür nicht
//  nachträglich eingeführt werden.
struct TableDef {
    Span headerSpan;             // "<w:tbl>" + tblPr + tblGrid (bis vor die 1. Zeile)
    Span footerSpan;             // "</w:tbl>"
    QVector<Span> rowSpans;      // je Zeile: "<w:tr …>" + trPr
    QVector<Span> rowEndSpans;   // je Zeile: "</w:tr>"
    QVector<Span> cellSpans;     // je Zelle: "<w:tc>" + tcPr
    QVector<Span> cellEndSpans;  // je Zelle: "</w:tc>"
    QVector<int>  cellRow;       // Zelle → Zeilenindex
    QVector<int>  rowFirstCell;  // Zeile → erster Zellindex (Zelle = rowFirstCell+col)
    //  Gitter-Maße: beim Parsen mitgenommen, damit das LAYOUT die Tabelle ohne
    //  erneutes XML-Parsen aus den lebenden Zell-Blöcken bauen kann.
    QVector<int>  gridTw;        // w:tblGrid/w:gridCol — Spaltenbreiten in Twips
    QVector<int>  cellGridSpan;  // je Zelle: w:gridSpan (1 = keine Spanne)
    QVector<int>  cellWidthTw;   // je Zelle: w:tcW (0 = aus dem Gitter ableiten)
    int parentTableId = -1;      // −1 = oberste Ebene
    int parentCellIndex = -1;    // Zelle des Elternteils, die diese Tabelle enthält

    //  MATERIALISIERTE Gerüst-Teile (Muster Block::pprXml): sobald das Gerüst
    //  selbst geändert wird — w:tblGrid beim Spaltenwechsel, w:tcW je Zelle —
    //  emittiert emitBlocks diesen String statt des Spans. Leer = Span nehmen.
    //  Solange nichts berührt wurde, bleibt der Schnellpfad „ganzer w:tbl als
    //  Original-Teilstring" und damit die Byte-Identität erhalten.
    QString headerXml;
    QVector<QString> cellXml;    // je Zelle (Größe == cellSpans oder leer)
    //  Gerüst geändert → emitBlocks darf den Schnellpfad NICHT nehmen, auch
    //  wenn keine einzige Zelle „dirty" ist (Zeile eingefügt/gelöscht usw.).
    bool structDirty = false;
    //  Zahl der Zellblöcke beim Zerlegen bzw. Bauen. Wird ein Block in eine
    //  Zelle EINGEFÜGT oder daraus ENTFERNT, ohne dass eine Zelle „dirty" wird
    //  (Bild/PDF-Seite/Verzeichnis einfügen, Bild löschen), stimmt die Zahl
    //  nicht mehr — und der Schnellpfad muss entfallen, sonst emittiert er den
    //  alten Original-Teilstring und der neue Inhalt geht beim Speichern
    //  verloren. Zusätzlich prüft emitBlocks die Span-Lage jedes Blocks.
    int blockCount = 0;

    //  Gesamter <w:tbl>…</w:tbl>-Bereich — Schnellpfad beim Speichern.
    Span rawSpan() const {
        const int end = footerSpan.start + footerSpan.len;
        return { headerSpan.start, end - headerSpan.start };
    }
};

// ── Tabelle für die ANZEIGE (w:tbl) ──────────────────────────────────────────
//  Reine Darstellungssicht: die Tabelle bleibt im Blockmodell EIN
//  `OpaqueVisible`-Block und wird beim Speichern byteidentisch aus dem Original
//  übernommen. Diese Struktur wird nur zum Auslegen/Zeichnen gebaut (und mit dem
//  Layout wieder freigegeben) — sie ist NICHT die Bearbeitungsdarstellung.
struct TableCell {
    QList<Block> paragraphs;    // nur pfmt + runs gefüllt (Spans nicht nötig)
    int gridSpan = 1;
    int widthTw  = 0;           // w:tcW, 0 = aus dem Gitter ableiten
};
struct TableRow {
    QList<TableCell> cells;
};
struct TableView {
    QList<int> gridTw;          // w:tblGrid — Spaltenbreiten in Twips
    QList<TableRow> rows;
    bool ok = false;            // false → Platzhalter zeichnen wie bisher
};

// ── Eintrag eines Inhaltsverzeichnisses ──────────────────────────────────────
//  Die Einträge werden NICHT in der Datei gespeichert: das Feld bleibt
//  deklarativ (`w:fldSimple` mit der TOC-Anweisung, ohne eingebackene
//  Seitenzahlen), Word rechnet die Zahlen selbst. Unsere Anzeige leitet sie aus
//  der eigenen Paginierung ab — deshalb reicht hier Text, Ebene und der Block,
//  auf den der Eintrag zeigt.
struct TocEntry {
    QString text;
    int     level = 1;          // 1…9 (aus HeadingN)
    int     block = -1;         // Index in Document::blocks
    //  Zeichenposition des ZEILENANFANGS in `Block::plainText()`. Ein Absatz
    //  kann mehrere Überschriften tragen (nur durch `w:br` getrennt) und über
    //  eine Seitengrenze laufen — dann gilt die Seitenzahl des Blocks nicht mehr
    //  für jeden Eintrag. Verbraucher: `DocxTextArea::pageOfEntry` und der
    //  PDF-Export.
    int     pos   = 0;
};

// ── Eingebettetes Bild eines Absatzes (w:drawing) ────────────────────────────
//  Der Run bleibt ein atomarer opaker Run (Bytes unangetastet); das hier ist
//  nur, was die ANZEIGE braucht.
struct InlineImage {
    QString relId;              // r:embed → Beziehung → word/media/…
    int cxEmu = 0, cyEmu = 0;   // wp:extent (EMU: 1 Zoll = 914400)
    int run = -1;               // Index des Bild-Runs im Block
    int pos = 0;                // Zeichenposition des U+FFFC im Absatztext
    //  ── Verankert statt „in der Zeile" (wp:anchor statt wp:inline) ──────────
    //  Ein verankertes Bild steht NICHT im Zeilenfluss: der Text fließt um sein
    //  Rechteck herum. `wrap` sagt, wie — nur `Square` wird ausgelegt, alles
    //  Übrige (durchlaufend/hinter dem Text) verhält sich wie `None`.
    enum Wrap { WrapNone = 0, WrapSquare = 1 };
    //  Auf WELCHER Seite des Bildes der Text laufen darf (`w:wrapSquare`
    //  ▸ `wrapText`). `Largest` ist Words Vorgabe „nimm die breitere Seite".
    enum WrapSide { SideBoth = 0, SideLeft = 1, SideRight = 2, SideLargest = 3 };
    bool anchored = false;
    int  wrap     = WrapNone;
    int  wrapSide = SideBoth;
    int  posXEmu  = 0;          // wp:positionH/wp:posOffset (Spalte)
    int  posYEmu  = 0;          // wp:positionV/wp:posOffset (Absatz)
    //  Der Text darf rechts bzw. links vom Bild NICHT direkt anstoßen.
    int  distLEmu = 0, distREmu = 0;
};

// ── Kopf-/Fußzeile eines Abschnitts ──────────────────────────────────────────
struct HeaderFooter {
    QList<Block> paragraphs;    // nur pfmt + runs gefüllt (wie Tabellenzellen)
    bool ok = false;
};

// ── Nummerierungs-Definitionen (Anzeige + Erzeugung) ─────────────────────────
struct NumLevel {
    QString numFmt;             // "bullet" | "decimal" | …
    QString lvlText;            // z. B. "%1."
};

// ─────────────────────────────────────────────────────────────────────────────
class Document {
public:
    bool load(const QString& path, QString* err = nullptr);
    //  Denselben Container, aber einen ANDEREN Teil laden (Kopf-/Fußzeile).
    //  Alles außer dem Teilnamen ist identisch: Spans, Selbstprüfung, Emission
    //  und Speichern sind teil-unabhängig. Beziehungen kommen aus der `.rels`
    //  DIESES Teils, Vorlagen und Nummerierung bleiben dokumentweit.
    bool loadPart(const QString& docxPath, const QString& partPath,
                  QString* err = nullptr);
    //  ZIP-Pfad des geladenen Teils ("word/document.xml", "word/header1.xml", …).
    QString partPath() const { return m_partPath; }

    //  Speichern über gezieltes XML-Splicing + ZIP-Roh-Kopie: newDocumentXml()
    //  baut das neue document.xml, replacementParts() liefert zusätzlich zu
    //  ersetzende/neue ZIP-Einträge (numbering.xml, [Content_Types].xml,
    //  word/_rels/document.xml.rels — nur wenn Listen-Infrastruktur nötig
    //  wurde). writeTo() schreibt den kompletten Container auf ein QIODevice.
    QString newDocumentXml() const;
    QHash<QString, QByteArray> replacementParts() const;
    bool writeTo(QIODevice* target, QString* err = nullptr) const;
    //  Wie oben, zusätzlich mit FREMDEN Ersatzteilen — so schreibt der
    //  Controller die Kopf-/Fußzeilen-Teile mit, die eigene Document-Instanzen
    //  erzeugt haben (s. DocxEditController::Region).
    bool writeTo(QIODevice* target, const QHash<QString, QByteArray>& extraParts,
                 QString* err) const;

    QString path() const { return m_path; }
    QStringView docXml() const { return m_docXml; }

    QList<Block> blocks;        // vom Controller mutiert (Undo über Kopien)

    // ── Anzeige-Auflösung (docDefaults + pStyle-Kette + direkte Formate) ─────
    RunFmt resolveRun(const Block& b, const Run& r) const;
    //  Zeichenformat der ABSATZMARKE (`w:pPr/w:rPr`), über das aufgelöste
    //  Absatzformat gelegt. Word legt dort das Format der Marke ab und
    //  wendet es auf ein Feldergebnis an — der Editor stellt darüber
    //  Schriftart und -größe des Inhaltsverzeichnisses ein.
    RunFmt paragraphMarkFormat(const Block& b) const;
    ParFmt resolvePar(const Block& b) const;
    const RunFmt& defaultRun() const { return m_defRun; }

    //  true, wenn IRGENDEINE Absatzvorlage (oder docDefaults) eine Nummerierung
    //  mitbringt. Ist es false, kann resolvePar(b).numId ausschliesslich aus
    //  b.pfmt stammen — Aufrufer, die nur an der Nummerierung interessiert
    //  sind (DocxTextArea::rebuildMarkers, laeuft ueber ALLE Bloecke bei jedem
    //  Tastendruck), duerfen die Vorlagenaufloesung dann komplett ueberspringen.
    bool stylesMayNumber() const { return m_stylesMayNumber; }

    // ── Absatzvorlagen (Formatvorlagen) ──────────────────────────────────────
    //  Die im Dokument definierten ABSATZ-Vorlagen in Reihenfolge der
    //  styles.xml, gefiltert auf die dem Nutzer zumutbaren (s. .cpp). Leer,
    //  wenn die Datei keine styles.xml mitbringt → die Auswahlliste entfällt.
    const QList<StyleInfo>& paragraphStyles() const { return m_parStyles; }
    //  w:styleId der Standard-Absatzvorlage (w:default="1"), sonst leer.
    QString defaultParagraphStyleId() const { return m_defaultParStyle; }
    //  Kennt das Dokument diese Vorlage überhaupt? (w:pStyle auf eine
    //  undefinierte id löst wie eine fehlende Vorlage auf — also gar nicht.)
    bool hasStyle(const QString& id) const { return m_styles.contains(id); }
    //  Überschriftvorlage `Heading<level>` SICHERSTELLEN (level 1…9) und ihre
    //  styleId liefern; leer bei unsinnigem Level. Die meisten .docx bringen
    //  keine mit — ohne das ließe sich in ihnen keine Überschrift schreiben.
    //  Neu angelegte Vorlagen gehen beim Speichern nach `word/styles.xml`
    //  (s. stylesParts()); ein Undo lässt sie stehen, sie sind dann inert.
    QString ensureHeadingStyle(int level);
    //  Höchster Überschrift-Level, den die Auswahlliste anbieten soll.
    static constexpr int kMaxHeadingLevel = 3;

    // ── NEUE Knoten anlegen (Einfügen) ───────────────────────────────────────
    //  Neu erzeugtes XML hat keine Herkunft im Original. Damit das Span-Modell
    //  trotzdem trägt, wird es an `m_docXml` ANGEHÄNGT und die neuen Spans
    //  zeigen dorthin: bestehende Offsets bleiben gültig (es wird nur
    //  angehängt), Emission und Undo brauchen keinen Sonderfall. Der Anhang
    //  wird NIE als Ganzes emittiert — er ist reiner Textspeicher.
    int appendPool(const QString& xml);          // → Start-Offset im Pool

    //  Leere Tabelle VOR `beforeBlock` einfügen (rows×cols, gleichmäßiges
    //  Gitter über die Textbreite). Liefert den Index des ersten neuen
    //  Zellblocks, −1 bei unbrauchbaren Maßen.
    int insertTable(int beforeBlock, int rows, int cols);

    // ── Inhaltsverzeichnis ───────────────────────────────────────────────────
    //  Ist dieser Block das TOC-FELD? (`w:fldSimple` mit einer `TOC`-Anweisung)
    bool isTocParagraph(const Block& b) const;
    //  Überschriften des Dokuments als Verzeichnis-Einträge, in Lesereihenfolge.
    //  Erkannt wird die Absatzvorlage `HeadingN` (so heißt sie in JEDER
    //  Sprachfassung von Word — der Anzeigename ist übersetzt, die styleId
    //  nicht). Blöcke in Tabellenzellen bleiben außen vor.
    QList<TocEntry> tocEntries(int maxLevel = 3) const;
    //  Inhaltsverzeichnis VOR `beforeBlock` einfügen — als `w:fldSimple` OHNE
    //  eingebackene Seitenzahlen: die Datei bleibt deklarativ und Word rechnet
    //  die Zahlen selbst; unsere Anzeige füllt sie aus der eigenen Paginierung.
    //  Liefert den Blockindex, −1 bei unbrauchbaren Maßen.
    int insertToc(int beforeBlock, int maxLevel = 3);

    //  Bild VOR `beforeBlock` als eigenen Absatz einfügen: die Datei wird als
    //  neuer ZIP-Teil vorgemerkt (word/media/…) samt Beziehung und
    //  Content-Type; die Zeichnung selbst kommt in den Anhang-Pool.
    //  Liefert den Blockindex, −1 bei Fehler (Text in `err`).
    int insertImage(int beforeBlock, const QString& localPath, QString* err);
    //  Dasselbe aus BYTES (Zwischenablage): `ext` bestimmt Teilname und
    //  Content-Type; unbekannte Endungen werden zu "png".
    //  `cxEmu`/`cyEmu` > 0 geben die ANZEIGEGRÖSSE vor (Kopieren innerhalb des
    //  Editors reicht die Größe der Quelle durch); 0 = aus den nativen Pixeln
    //  rechnen. Auf die Textbreite gedeckelt wird in beiden Fällen.
    int insertImageData(int beforeBlock, const QByteArray& bytes,
                        const QString& ext, QString* err,
                        qint64 cxEmu = 0, qint64 cyEmu = 0);
    //  Bild MITTEN in einen Absatz setzen: als `w:drawing`-Run VOR `runIdx`
    //  (der Aufrufer hat dort bereits eine Run-Grenze erzeugt). Das ist der
    //  Weg, auf dem zwei Bilder nebeneinander und Text daneben entstehen —
    //  in der Datei ganz normales `wp:inline`, wie Word es schreibt.
    //  Liefert den Run-Index, −1 bei Fehler (Text in `err`).
    int insertImageRunAt(int blockIdx, int runIdx, const QByteArray& bytes,
                         const QString& ext, QString* err,
                         qint64 cxEmu = 0, qint64 cyEmu = 0);
    //  Größe eines EINGEBETTETEN Bildes ändern (wp:extent + a:ext in pic:spPr).
    //  Der Bild-Run bleibt opak; geschrieben wird eine NEUE Zeichnung in den
    //  Anhang-Pool, auf die der Run zeigt — das Original bleibt unangetastet.
    //  false, wenn der Block kein reiner Bild-Absatz ist.
    bool setImageSizeEmu(int blockIdx, qint64 cxEmu, qint64 cyEmu);
    //  Dasselbe für EIN Bild eines Absatzes, der auch Text tragen kann.
    bool setImageSizeEmu(int blockIdx, int runIdx, qint64 cxEmu, qint64 cyEmu);
    //  UMBRUCHART eines Bildes: `wp:inline` (in der Zeile) ⇄ `wp:anchor` +
    //  `w:wrapSquare` (Text fließt daneben). Umgeschrieben wird wie bei der
    //  Größe: der Roh-Span des Bild-Runs wird materialisiert, die neue
    //  Zeichnung kommt in den Anhang-Pool, der Run zeigt dorthin — alles
    //  Übrige am Bild (Zuschnitt, Effekte, Alternativtext) bleibt unangetastet.
    //  false, wenn der Run kein deutbares `w:drawing` trägt.
    bool setImageWrap(int blockIdx, int runIdx, bool floating);
    //  Bild-RUN in einen ANDEREN Absatz umhängen (Anker wechselt den Absatz,
    //  wie in Word beim Ablegen über einem anderen Absatz). Der Run wandert
    //  unverändert mit seinem Roh-Span — Zeichnung, Zuschnitt und Größe bleiben
    //  also byteweise erhalten; nur seine LAGE ist danach relativ zum neuen
    //  Absatz und wird vom Aufrufer über `setImageAnchorEmu` nachgezogen.
    //  Liefert den Run-Index im Zielabsatz, −1 wenn nicht möglich.
    int moveImageRun(int srcBlock, int runIdx, int dstBlock);
    //  LAGE eines verankerten Bildes (`wp:positionH/V` ▸ `wp:posOffset`, EMU,
    //  relativ zu Textspalte und Absatz) — das, was Ziehen mit der Maus
    //  schreibt. false, wenn das Bild in der Zeile steht oder sich nichts ändert.
    bool setImageAnchorEmu(int blockIdx, int runIdx, int posXEmu, int posYEmu);
    //  UMBRUCHSEITE eines verankerten Bildes (`InlineImage::WrapSide`).
    bool setImageWrapSide(int blockIdx, int runIdx, int side);
    //  Das Bild EINES Runs (Lage/Umbruchseite/Maße) — false, wenn dieser Run
    //  keine deutbare Zeichnung trägt.
    bool imageOfRun(int blockIdx, int runIdx, InlineImage* out) const;

    // ── Tabellen-Gerüst ──────────────────────────────────────────────────────
    const QVector<TableDef>& tables() const { return m_tables; }
    //  Erster/letzter Block einer Tabelle in `blocks` (−1, wenn sie leer ist).
    int tableFirstBlock(int tableId) const;
    int tableLastBlock(int tableId) const;

    // ── Tabellen-STRUKTUR bearbeiten (Zeilen/Spalten/Breiten) ────────────────
    //  Alle Operationen mutieren TableDef UND die Zellblöcke; der Aufrufer
    //  (DocxEditController) klammert sie in einen EditScope samt TableDef-
    //  Schnappschuss, damit Undo beides zurücknimmt.
    int  tableRowCount(int tableId) const;
    int  tableColumnCount(int tableId) const;   // Zellen der ersten Zeile
    //  Nur gleichmäßige Gitter sind strukturell änderbar: verbundene Zellen
    //  (w:gridSpan/w:vMerge/w:hMerge) oder Zeilen mit ungleicher Zellzahl
    //  werden NICHT angefasst — lieber ablehnen als das Gitter zerreißen.
    bool tableStructEditable(int tableId) const;
    //  Spaltenbreiten in Twips (Gitter + je Zelle w:tcW). Größe muss der
    //  Spaltenzahl entsprechen.
    QVector<int> tableColumnWidths(int tableId) const;

    bool tableInsertRow(int tableId, int atRow);
    bool tableDeleteRow(int tableId, int row);
    bool tableInsertColumn(int tableId, int atCol);
    bool tableDeleteColumn(int tableId, int col);
    bool tableSetColumnWidths(int tableId, const QVector<int>& widthsTw);

    //  Schnappschuss/Wiederherstellung des Gerüsts für Undo.
    TableDef tableDef(int tableId) const;
    void     setTableDef(int tableId, const TableDef& def);

    // ── Tabellen-ANZEIGE ─────────────────────────────────────────────────────
    //  Zerlegt den Roh-Span eines `w:tbl`-Blocks in Zeilen/Zellen/Absätze für
    //  die Darstellung. Tolerant: fehlt das Gitter oder ist das Fragment
    //  beschädigt, kommt `ok == false` zurück und der Aufrufer zeichnet den
    //  bisherigen Platzhalter. Eine VERSCHACHTELTE Tabelle wird in ihrer Zelle
    //  als `OpaqueVisible`-Absatz geführt (Platzhalter), nicht rekursiv zerlegt.
    TableView parseTableForDisplay(const Block& b) const;

    // ── Bilder & weitere ZIP-Teile (ANZEIGE) ─────────────────────────────────
    //  Enthält der Absatz GENAU ein eingebettetes Bild (und sonst keinen
    //  sichtbaren Text)? Nur dieser Fall wird dargestellt — ein Bild MITTEN im
    //  Text bräuchte einen Inline-Objekt-Handler, den QTextLayout ohne
    //  QTextDocument nicht kennt; es bleibt dann der graue Platzhalter.
    bool paragraphImage(const Block& b, InlineImage* out) const;
    //  ALLE Bilder eines Absatzes in Textreihenfolge (Run-Index + Zeichenstelle
    //  des Objekt-Zeichens). Grundlage der Anzeige „Bild im Fließtext".
    QVector<InlineImage> paragraphImages(const Block& b) const;
    //  Beziehungsziel (`rId…` → z. B. "media/bild1.png"), leer wenn unbekannt.
    QString relTarget(const QString& relId) const;
    //  EINEN Eintrag aus dem Container nachladen (öffnet das ZIP erneut — die
    //  Bytes werden bewusst NICHT gehalten, s. RAM-Priorität).
    QByteArray partBytes(const QString& zipPath) const;
    //  Bilddaten hinter einer Beziehung; leer bei unbekannter/fremder Zielart.
    QByteArray imageBytes(const QString& relId) const;

    // ── Kopf-/Fußzeile des Hauptabschnitts (ANZEIGE) ─────────────────────────
    //  Aus w:headerReference/w:footerReference des letzten w:sectPr. `first`
    //  liefert die Variante für die erste Seite, sonst die Standardvariante.
    HeaderFooter headerFooter(bool footer, bool first) const;
    //  ZIP-Pfad des zugehörigen Teils ("word/header1.xml"), leer wenn keiner —
    //  der Editor lädt ihn als eigene Document-Instanz (Region), s. loadPart.
    QString headerFooterPart(bool footer, bool first) const;

    // ── Seiteneinrichtung ────────────────────────────────────────────────────
    //  Die Werte des LETZTEN w:sectPr im Körper (das für den Hauptteil gilt).
    //  Abschnittswechsel mitten im Dokument werden bewusst nicht abgebildet —
    //  eine Seitengeometrie je Dokument; ohne w:sectPr bleibt es bei A4.
    const SectionProps& section() const { return m_section; }

    // ── Nummerierung ─────────────────────────────────────────────────────────
    NumLevel numLevel(int numId, int ilvl) const;
    //  Liefert eine numId für neue Listen; legt (lazy) eigene abstractNum/num-
    //  Definitionen an, die beim Speichern in word/numbering.xml gespliced
    //  werden (bzw. die Datei + ContentType-Override + Relationship anlegen).
    int newListNum(bool bullet);

    // ── Fabriken/Utilities ───────────────────────────────────────────────────
    static QByteArray emptyDocxBytes(const QString& title);   // leeres A4-Dokument
    static QString    plainTextPreview(const QString& path, int maxLines);
    static QString    xmlEscape(const QString& s);
    static QString    serializeRunsText(const QString& text); // Text → <w:t>/<w:tab/>…
    //  <w:rPr>-Fragment aus einem RunFmt (öffentlich: der Controller schreibt
    //  damit das Zeichenformat des Inhaltsverzeichnis-Absatzes in dessen
    //  w:pPr/w:rPr — der Feld-Run selbst bleibt opak und unangetastet).
    QString buildRPrXml(const RunFmt& f) const;

    //  Kanonisch geordnetes Einfügen/Ersetzen EINES Property-Elements in einem
    //  bestehenden <w:rPr>/<w:pPr>-Fragment — alle übrigen Kinder bleiben
    //  verbatim erhalten (öffentlich für gezielte Tests).
    static QString upsertProp(const QString& prXml, const QString& wrapTag,
                              const QString& propName, const QString& newXml,
                              const QStringList& order);

    //  Öffentlich, weil die Tabellen-Anzeigesicht (parseTableForDisplay) die
    //  Formate ihrer Zell-Absätze mit denselben Regeln liest wie der Hauptparser.
    static void parseRunProps(QStringView xml, RunFmt* out);
    static void parseParProps(QStringView xml, ParFmt* out);

private:
    struct StyleDef {
        QString basedOn;
        RunFmt  rf;
        ParFmt  pf;
    };

    //  Der EINE Schreibweg für den Rahmen einer Zeichnung: Umbruchart, Lage und
    //  Umbruchseite. Die Kinder werden übernommen, nur der Rahmen entsteht neu.
    bool rewriteDrawingFrame(int blockIdx, int runIdx, bool floating,
                             int posXEmu, int posYEmu, int wrapSide,
                             bool requireModeChange);

    bool parseDocumentXml(QString* err);
    void parseSectPr(QStringView xml);       // w:pgSz/w:pgMar/w:cols → m_section
    bool parseStylesXml(const QByteArray& xml);
    bool parseNumberingXml(const QByteArray& xml);

    QString buildParagraphXml(const Block& b) const;
    //  Gemeinsamer Lauf über die Blöcke: Tabellen werden als GRUPPE
    //  emittiert (Gerüst + Zellinhalt). `rawOnly` = nur Original-Spans —
    //  damit prüft die Selbstprüfung genau den Weg, den auch das
    //  Speichern unangetasteter Teile nimmt.
    QString emitBlocks(bool rawOnly) const;
    QHash<QString, QByteArray> numberingParts() const;   // Listen-Infrastruktur
    QHash<QString, QByteArray> mediaParts() const;       // eingefügte Bilder
    QHash<QString, QByteArray> stylesParts(
        const QHash<QString, QByteArray>& base) const;   // neu angelegte Vorlagen
    QString buildRunXml(const Run& r) const;
    //  Bild als ZIP-Teil vormerken und den `w:drawing`-Run bauen — gemeinsamer
    //  Kern von `insertImageData` (eigener Absatz) und `insertImageRunAt`
    //  (Bild im Fließtext). Leerer Rückgabewert = Fehler (Text in `err`).
    QString buildImageRunXml(const QByteArray& bytes, const QString& ext,
                             QString* err, qint64 cxEmu, qint64 cyEmu);

    //  Gerüst-Text EINER Stelle: materialisiert, sonst Original-Span.
    QString tableHeaderText(const TableDef& d) const;
    QString tableCellText(const TableDef& d, int cellIdx) const;
    //  Gitter/Zellbreiten in die materialisierten Fragmente schreiben.
    void    materializeGrid(TableDef& d, const QVector<int>& widthsTw);
    //  Zellindex-Bereich einer Zeile (last = −1, wenn die Zeile leer ist).
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
    //  Noch nicht geschriebene Medien-Teile (Einfügen). Die Bytes bleiben bis
    //  zum Speichern im Zugriff, damit die ANZEIGE das Bild sofort zeigen kann —
    //  im Container liegt es ja noch nicht.
    struct PendingMedia {
        QString    zipName;              // "word/media/mg1.png"
        QString    relId;                // "rIdMGimg1"
        QString    ext;                  // "png"
        QByteArray bytes;
    };
    QList<PendingMedia> m_pendingMedia;
    int m_nextMediaId = 1;
    QHash<QString, QString> m_rels;      // rId → Ziel (relativ zu word/)
    //  w:headerReference/w:footerReference des Hauptabschnitts: Art → rId.
    QHash<QString, QString> m_hdrRefs;   // "default"/"first"/"even"
    QHash<QString, QString> m_ftrRefs;

    QHash<int, QHash<int, NumLevel>> m_numLevels;    // numId → ilvl → Level
    QHash<int, int> m_numToAbstract;
    //  Lazy erzeugte eigene Listen-Definitionen (beim Speichern gespliced):
    mutable int m_ownAbstractBullet = -1;
    mutable int m_ownAbstractDecimal = -1;
    QList<QPair<int, bool>> m_pendingNums;           // (numId, bullet)
    int  m_nextNumId = 1;
    int  m_nextAbstractId = 0;
    bool m_hadNumberingPart = false;
    QString m_numberingXml;      // dekodierter Bestand (für Splice), sonst leer

    bool m_hadStylesPart = false;
    QString m_stylesXml;         // dekodierter Bestand (für Splice), sonst leer
    //  XML der per ensureHeadingStyle() neu angelegten Vorlagen, in
    //  Anlegereihenfolge — beim Speichern vor </w:styles> gespliced.
    QStringList m_pendingStyles;
};

} // namespace Docx
