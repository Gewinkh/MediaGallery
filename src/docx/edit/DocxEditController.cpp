#include "docx/edit/DocxEditController.h"

#include "core/FolderImages.h"
#include "docx/DocxZip.h"
#include "core/AppSettings.h"
#include "core/PathUtils.h"

#include <QThreadPool>
#include <QRunnable>
#include <QSaveFile>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDataStream>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPdfDocument>
#include <QDir>
#include <QMetaObject>
#include <QRegularExpression>
#include <QPointer>

using namespace Docx;

// Kanonische Kind-Reihenfolgen (OOXML-Schema) für upsertProp - nur die im
// Editor relevanten Namen müssen enthalten sein; unbekannte Kinder bleiben
// ohnehin unangetastet an ihrem Platz.
static const QStringList kRPrOrder = {
    QStringLiteral("w:rStyle"), QStringLiteral("w:rFonts"), QStringLiteral("w:b"),
    QStringLiteral("w:bCs"), QStringLiteral("w:i"), QStringLiteral("w:iCs"),
    QStringLiteral("w:caps"), QStringLiteral("w:smallCaps"), QStringLiteral("w:strike"),
    QStringLiteral("w:color"), QStringLiteral("w:spacing"), QStringLiteral("w:kern"),
    QStringLiteral("w:position"), QStringLiteral("w:sz"), QStringLiteral("w:szCs"),
    QStringLiteral("w:highlight"), QStringLiteral("w:u"), QStringLiteral("w:vertAlign"),
    QStringLiteral("w:rtl"), QStringLiteral("w:lang")
};
static const QStringList kPPrOrder = {
    QStringLiteral("w:pStyle"), QStringLiteral("w:keepNext"), QStringLiteral("w:keepLines"),
    QStringLiteral("w:pageBreakBefore"), QStringLiteral("w:widowControl"),
    QStringLiteral("w:numPr"), QStringLiteral("w:pBdr"), QStringLiteral("w:shd"),
    QStringLiteral("w:tabs"), QStringLiteral("w:bidi"), QStringLiteral("w:spacing"),
    QStringLiteral("w:ind"), QStringLiteral("w:contextualSpacing"),
    QStringLiteral("w:jc"), QStringLiteral("w:outlineLvl"), QStringLiteral("w:rPr")
};

DocxEditController::DocxEditController(QObject* parent) : QObject(parent) {
    m_stack.setUndoLimit(200);
    connect(&m_stack, &QUndoStack::canUndoChanged, this, &DocxEditController::undoChanged);
    connect(&m_stack, &QUndoStack::canRedoChanged, this, &DocxEditController::undoChanged);
}

DocxEditController::~DocxEditController() = default;

void DocxEditController::setTranslit(QObject* t) {
    if (m_translit == t) return;
    m_translit = t;
    emit translitChanged();
}

void DocxEditController::bumpFormat() {
    ++m_formatRev;
    emit formatRevChanged();
}

void DocxEditController::setModified(bool m) {
    if (m_modified == m) return;
    m_modified = m;
    emit modifiedChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Laden - asynchron (QRunnable + Generationszähler; Muster PdfTextController)
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::setSource(const QString& s) {
    const QString path = mg::toLocalPath(s);
    if (m_source == path)
        return;
    m_source = path;
    emit sourceChanged();

    m_ready = false;
    m_loadError.clear();
    m_stack.clear();
    m_bakDone  = false;
    m_modified = false;
    m_cursor   = DocxCursor();
    emit readyChanged();
    emit modifiedChanged();
    if (path.isEmpty())
        return;

    const int gen = ++m_loadGen;
    QPointer<DocxEditController> self(this);

    class LoadTask : public QRunnable {
    public:
        LoadTask(QPointer<DocxEditController> c, QString p, int g)
            : m_c(c), m_path(std::move(p)), m_gen(g) { setAutoDelete(true); }
        void run() override {
            auto* doc = new Document();
            QString err;
            const bool ok = doc->load(m_path, &err);
            auto c = m_c;
            const int gen = m_gen;
            //  Übergabe an den GUI-Thread (QueuedConnection-Äquivalent).
            QMetaObject::invokeMethod(qApp, [c, gen, doc, ok, err]() {
                if (!c || gen != c->m_loadGen) { delete doc; return; }
                if (ok) {
                    c->m_doc = std::move(*doc);
                    c->m_ready = true;
                    c->refreshRevisions();    // Hinweisstreifen (Änderungen)
                    c->spellStart();          // Wörterbuch + erste Prüfung
                } else {
                    c->m_loadError = err;
                }
                delete doc;
                emit c->blocksReplaced(0, 0, c->m_doc.blocks.size());
                emit c->readyChanged();
                emit c->cursorChanged();
                c->bumpFormat();
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<DocxEditController> m_c;
        QString m_path;
        int     m_gen;
    };
    QThreadPool::globalInstance()->start(new LoadTask(self, path, gen));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Positions-Helfer
// ─────────────────────────────────────────────────────────────────────────────
QString DocxEditController::blockText(int i) const {
    if (i < 0 || i >= m_doc.blocks.size()) return {};
    return m_doc.blocks.at(i).plainText();
}
int DocxEditController::blockLen(int i) const {
    if (i < 0 || i >= m_doc.blocks.size()) return 0;
    return m_doc.blocks.at(i).textLength();
}
bool DocxEditController::isEditableParagraph(int i) const {
    if (i < 0 || i >= m_doc.blocks.size())
        return false;
    const Block& b = m_doc.blocks.at(i);
    if (b.kind != Block::Paragraph)
        return false;
    return true;
}

//  Gehören zwei Blöcke in DIESELBE Zelle (bzw. beide in den Rumpf)? Das ist die
//  Grenze, über die keine STRUKTUR-Änderung laufen darf: ein Absatz-Merge über
//  eine Zellgrenze würde eine Zelle auflösen und damit die Tabelle zerstören -
//  die Datei bliebe zwar wohlgeformt, aber der Inhalt wäre verschoben.
bool DocxEditController::sameCell(int i, int j) const {
    if (i == j) return true;
    if (i < 0 || j < 0 || i >= m_doc.blocks.size() || j >= m_doc.blocks.size())
        return false;
    const Block& a = m_doc.blocks.at(i);
    const Block& b = m_doc.blocks.at(j);
    if (a.tableId != b.tableId) return false;
    if (a.tableId < 0) return true;                  // beide im Rumpf
    return a.row == b.row && a.col == b.col;
}

//  Mehrblock-Operationen auf die Zelle des ANKERS begrenzen. Eine Selektion darf
//  quer über Zellen laufen (Kopieren ist harmlos); LÖSCHEN darf es nicht, sonst
//  verschwänden Zellgrenzen. Statt die Aktion zu verweigern, wird der Bereich
//  gekürzt - so bleibt Strg+A + Entf im Rumpf benutzbar.
void DocxEditController::clampRangeToCell(int b1, int& b2) const {
    while (b2 > b1 && !sameCell(b1, b2)) --b2;
}

void DocxEditController::orderedSelection(int& b1, int& p1, int& b2, int& p2) const {
    b1 = m_cursor.aBlock; p1 = m_cursor.aPos;
    b2 = m_cursor.block;  p2 = m_cursor.pos;
    if (b1 > b2 || (b1 == b2 && p1 > p2)) {
        std::swap(b1, b2);
        std::swap(p1, p2);
    }
}

void DocxEditController::runAt(const Block& b, int pos, int* runIdx, int* runOfs) const {
    int acc = 0;
    for (int i = 0; i < b.runs.size(); ++i) {
        const int len = b.runs.at(i).text.size();
        if (pos < acc + len || i == b.runs.size() - 1) {
            *runIdx = i;
            *runOfs = qBound(0, pos - acc, len);
            return;
        }
        acc += len;
    }
    *runIdx = -1;
    *runOfs = 0;
}

int DocxEditController::ensureRunBoundary(Block& b, int pos) const {
    int acc = 0;
    for (int i = 0; i < b.runs.size(); ++i) {
        Run& r = b.runs[i];
        const int len = r.text.size();
        if (pos == acc)
            return i;                                   // Grenze existiert
        if (pos < acc + len) {
            //  Run teilen: beide Hälften erben rPr (Original-Span bzw. schon
            //  materialisiertes Fragment) und werden dirty (Neuaufbau nötig).
            Run right = r;
            right.text = r.text.mid(pos - acc);
            right.dirty = true;
            r.text.truncate(pos - acc);
            r.dirty = true;
            //  rPr der rechten Hälfte materialisieren (Span zeigt weiterhin
            //  auf dasselbe Original-Fragment - verbatim geteilt ist ok).
            b.runs.insert(i + 1, right);
            return i + 1;
        }
        acc += len;
    }
    return b.runs.size();
}

void DocxEditController::removeRangeInBlock(Block& b, int p1, int p2) const {
    if (p1 >= p2) return;
    const int i2 = ensureRunBoundary(b, p2);
    const int i1 = ensureRunBoundary(b, p1);
    //  Runs [i1, i2') entfernen - i2 nach dem zweiten Split neu bestimmen.
    int acc = 0, end = b.runs.size();
    for (int i = 0; i < b.runs.size(); ++i) {
        if (acc == p2 && i >= i1) { end = i; break; }
        acc += b.runs.at(i).text.size();
    }
    if (acc == p2) end = qMin(end, b.runs.size());
    Q_UNUSED(i2)
    for (int i = end - 1; i >= i1; --i)
        b.runs.removeAt(i);
    b.dirty = true;
}

//  Absatz an `pos` teilen. Der neue Absatz erbt Zelle, pPr und ParFmt - er ist
//  derselbe Absatz, nur ab dieser Stelle. `dropBreakAtPos` schluckt ein dort
//  stehendes `w:br`-Zeichen: aus dem Zeilenumbruch wird die Absatzgrenze,
//  sonst bliebe er als leere erste Zeile im neuen Absatz stehen.
int DocxEditController::splitParagraphAt(int blockIdx, int pos, bool dropBreakAtPos) {
    if (blockIdx < 0 || blockIdx >= m_doc.blocks.size()) return blockIdx;
    Block& blk = m_doc.blocks[blockIdx];
    if (dropBreakAtPos && pos < blk.textLength()
        && blk.plainText().at(pos) == Docx::kLineBreak)
        removeRangeInBlock(blk, pos, pos + 1);

    const int k = ensureRunBoundary(blk, pos);
    Block nb;
    nb.kind    = Block::Paragraph;
    //  In DERSELBEN Zelle bleiben - sonst stünde der neue Absatz nach dem
    //  Speichern ausserhalb der Tabelle (wie in `insertText`).
    nb.tableId = blk.tableId;
    nb.row     = blk.row;
    nb.col     = blk.col;
    const QString ppr = blk.currentPpr(m_doc.docXml());
    if (!ppr.isEmpty()) { nb.pprXml = ppr; nb.pprMaterialized = true; }
    nb.pfmt = blk.pfmt;
    while (blk.runs.size() > k)
        nb.runs.append(blk.runs.takeAt(k));
    blk.dirty = true;
    nb.dirty  = true;
    m_doc.blocks.insert(blockIdx + 1, nb);
    return blockIdx + 1;
}

//  Die Zeilen [from,to) eines Absatzes, dessen Zeilen nur durch `w:br` getrennt
//  sind, zu einem EIGENEN Absatz machen. Nötig, weil eine Absatzvorlage sonst
//  zwangsläufig ALLE Zeilen trifft - der Nutzer markiert aber eine Zeile.
//  Erst HINTEN teilen: eine Teilung vorne verschöbe sonst die hintere Stelle.
int DocxEditController::splitOffLines(int blockIdx, int from, int to) {
    if (blockIdx < 0 || blockIdx >= m_doc.blocks.size()) return -1;
    const Block& blk = m_doc.blocks.at(blockIdx);
    const QString t = blk.plainText();
    if (!t.contains(Docx::kLineBreak)) return -1;          // nichts zu teilen
    if (from <= 0 && to >= t.size()) return -1;            // schon der ganze Absatz

    //  Hinten: der Umbruch steht AUF `to` (er trennt die letzte gewählte Zeile
    //  von der nächsten). Vorne steht er auf `from - 1` - dort wird geteilt,
    //  sonst bliebe er als leere Schlusszeile im ersten Absatz stehen.
    if (to < t.size())  splitParagraphAt(blockIdx, to, /*dropBreakAtPos=*/true);
    if (from > 0)       return splitParagraphAt(blockIdx, from - 1, /*dropBreakAtPos=*/true);
    return blockIdx;
}

void DocxEditController::applyPendingTo(Run& r) const {
    QString rpr = r.currentRpr(m_doc.docXml());
    auto upsert = [&](const QString& name, const QString& xml) {
        rpr = Document::upsertProp(rpr, QStringLiteral("w:rPr"), name, xml, kRPrOrder);
    };
    if (m_pending.set & RunFmt::FBold) {
        r.fmt.bold = m_pending.bold; r.fmt.set |= RunFmt::FBold;
        upsert(QStringLiteral("w:b"),
               m_pending.bold ? QStringLiteral("<w:b/>") : QStringLiteral("<w:b w:val=\"0\"/>"));
    }
    if (m_pending.set & RunFmt::FItalic) {
        r.fmt.italic = m_pending.italic; r.fmt.set |= RunFmt::FItalic;
        upsert(QStringLiteral("w:i"),
               m_pending.italic ? QStringLiteral("<w:i/>") : QStringLiteral("<w:i w:val=\"0\"/>"));
    }
    if (m_pending.set & RunFmt::FUnderline) {
        r.fmt.underline = m_pending.underline; r.fmt.set |= RunFmt::FUnderline;
        upsert(QStringLiteral("w:u"), m_pending.underline
                   ? QStringLiteral("<w:u w:val=\"single\"/>")
                   : QStringLiteral("<w:u w:val=\"none\"/>"));
    }
    if (m_pending.set & RunFmt::FFont) {
        r.fmt.font = m_pending.font; r.fmt.set |= RunFmt::FFont;
        upsert(QStringLiteral("w:rFonts"),
               QStringLiteral("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\" w:cs=\"%1\"/>")
                   .arg(Document::xmlEscape(m_pending.font)));
    }
    if (m_pending.set & RunFmt::FSize) {
        r.fmt.sizePt = m_pending.sizePt; r.fmt.set |= RunFmt::FSize;
        const int hp = qRound(m_pending.sizePt * 2.0);
        upsert(QStringLiteral("w:sz"),   QStringLiteral("<w:sz w:val=\"%1\"/>").arg(hp));
        upsert(QStringLiteral("w:szCs"), QStringLiteral("<w:szCs w:val=\"%1\"/>").arg(hp));
    }
    if (m_pending.set & RunFmt::FColor) {
        r.fmt.color = m_pending.color; r.fmt.set |= RunFmt::FColor;
        upsert(QStringLiteral("w:color"),
               QStringLiteral("<w:color w:val=\"%1\"/>")
                   .arg(m_pending.color.name(QColor::HexRgb).mid(1).toUpper()));
    }
    r.rprXml = rpr;
    r.rprMaterialized = true;
    r.dirty = true;
}

void DocxEditController::clearPending() {
    m_pending      = RunFmt();
    m_pendingBlock = -1;
    m_pendingPos   = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Word-Verhalten: Wird das LETZTE Zeichen einer Zeile gelöscht, behält die
//  Zeile ihr eigenes Zeichenformat, statt auf ihr Absatz-/Stil-Format
//  zurückzufallen (Nutzerbefund: eine per Enter aus einer 20-pt-Zeile
//  entstandene Zeile sprang nach dem Leerlöschen wieder auf 20 pt).
//
//  WO das Format lebt - der TRÄGER-RUN:
//  Ein Enter am Zeilenende legt im neuen Absatz einen LEEREN Run an, der das
//  rPr der alten Zeile erbt. Genau dieser Run ist das Gegenstück zur
//  Absatzmarke in Word: Er trägt kein Zeichen, bestimmt aber, wie der Absatz
//  aussieht und womit das nächste Tippen fortsetzt. Das ist gewollt - ohne ihn
//  würde ein Enter die Formatierung der Vorzeile NICHT fortführen.
//  Der Fehler war, dass dieser Träger nach dem Leerlöschen weiter das ALTE
//  Format (20 pt) trug: Solange das Pending-Format lebte, überdeckte es das
//  zwar - verließ der Cursor die Zeile aber einmal, kam wieder 20 pt zurück.
//  Deshalb wird der Träger jetzt UMGESCHRIEBEN statt nur überdeckt; das Format
//  überlebt damit Cursor-Wechsel, Speichern und Neuladen.
//
//  Gesetzt werden NUR die Felder, die vom Stil-Format des leeren Absatzes
//  abweichen: minimales rPr (Verlusterhaltungs-/RAM-Prinzip), gleiches Bild.
//  Gibt es keinen Träger (der Absatz hatte nie einen), wird einer angelegt -
//  ein leerer `<w:r>` mit rPr ist gültiges OOXML und genau das, was Word für
//  eine formatierte, leere Zeile schreibt.
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::keepFormatOnEmptiedBlock(int bi, const RunFmt& had) {
    if (!isEditableParagraph(bi))
        return;
    Block& blk = m_doc.blocks[bi];
    if (blk.textLength() != 0)
        return;                                   // Zeile ist nicht leer geworden
    const RunFmt base = m_doc.resolveRun(blk, Run());
    RunFmt p;
    if (had.bold      != base.bold)      { p.bold      = had.bold;      p.set |= RunFmt::FBold; }
    if (had.italic    != base.italic)    { p.italic    = had.italic;    p.set |= RunFmt::FItalic; }
    if (had.underline != base.underline) { p.underline = had.underline; p.set |= RunFmt::FUnderline; }
    if (!qFuzzyCompare(had.sizePt + 1, base.sizePt + 1)) { p.sizePt = had.sizePt; p.set |= RunFmt::FSize; }
    if (had.font != base.font && !had.font.isEmpty())    { p.font   = had.font;   p.set |= RunFmt::FFont; }
    if (had.color.isValid() && had.color != base.color)  { p.color  = had.color;  p.set |= RunFmt::FColor; }
    clearPending();

    //  Träger suchen (erster nicht-opaker Run des leeren Absatzes).
    int carrier = -1;
    for (int i = 0; i < blk.runs.size(); ++i) {
        if (!blk.runs.at(i).opaque) { carrier = i; break; }
    }

    if (p.set == 0) {
        //  Nichts weicht vom Stil ab: einen vorhandenen Träger von seinem
        //  ALTEN (geerbten) rPr befreien, damit er nicht weiter ein fremdes
        //  Format festhält. Ohne Träger ist ohnehin nichts zu tun.
        if (carrier >= 0) {
            Run& r = blk.runs[carrier];
            r.rprXml.clear();
            r.rprMaterialized = true;
            r.fmt = RunFmt();
            r.dirty = true;
            blk.dirty = true;
        }
        return;
    }

    if (carrier < 0) {
        //  Kein Träger vorhanden -> einen leeren anlegen (Word-Äquivalent der
        //  formatierten Absatzmarke).
        Run r;
        r.rprMaterialized = true;                 // startet ohne rPr
        r.dirty = true;
        blk.runs.append(r);
        carrier = blk.runs.size() - 1;
    } else {
        //  Vorhandenen Träger auf ein FRISCHES rPr setzen: sonst bliebe das
        //  geerbte Format (20 pt) unter den neu gesetzten Feldern erhalten.
        Run& r = blk.runs[carrier];
        r.rprXml.clear();
        r.rprMaterialized = true;
        r.fmt = RunFmt();
    }

    //  Felder über denselben Weg schreiben wie das Pending-Format beim Tippen
    //  (applyPendingTo pflegt rPr-XML UND das geparste RunFmt konsistent).
    const RunFmt save = m_pending;
    m_pending = p;
    applyPendingTo(blk.runs[carrier]);
    m_pending = save;
    blk.dirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Cursor & Selektion
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::setCursor(int block, int pos, bool keepAnchor) {
    if (m_doc.blocks.isEmpty()) return;
    block = qBound(0, block, int(m_doc.blocks.size()) - 1);
    pos   = qBound(0, pos, blockLen(block));
    if (m_cursor.block == block && m_cursor.pos == pos
        && (keepAnchor || !m_cursor.hasSelection()))
        return;
    //  Jede Cursor-Bewegung hebt die Objekt-Auswahl der Tabelle wieder auf.
    m_tableObjectSel = -1;
    m_cursor.block = block;
    m_cursor.pos   = pos;
    if (!keepAnchor)
        m_cursor.collapse();
    //  Pending-Format erlischt, sobald der Cursor die Stelle verlässt.
    if (m_pendingBlock != block || m_pendingPos != pos) {
        m_pending = RunFmt();
        m_pendingBlock = -1;
    }
    emit cursorChanged();
    bumpFormat();
}

void DocxEditController::selectAll() {
    if (m_doc.blocks.isEmpty()) return;
    m_cursor.aBlock = 0;
    m_cursor.aPos   = 0;
    m_cursor.block  = m_doc.blocks.size() - 1;
    m_cursor.pos    = blockLen(m_cursor.block);
    emit cursorChanged();
    bumpFormat();
}

void DocxEditController::selectWordAt(int block, int pos) {
    if (!isEditableParagraph(block)) return;
    const QString t = blockText(block);
    pos = qBound(0, pos, t.size());
    int a = pos, b = pos;
    auto wordChar = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };
    while (a > 0 && wordChar(t.at(a - 1))) --a;
    while (b < t.size() && wordChar(t.at(b))) ++b;
    if (a == b && pos < t.size()) b = pos + 1;           // Einzelzeichen
    m_cursor = { block, b, block, a };
    emit cursorChanged();
    bumpFormat();
}

void DocxEditController::clearSelection() {
    if (!m_cursor.hasSelection()) return;
    m_cursor.collapse();
    emit cursorChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Kommando-Anwendung (Undo/Redo-Pfad)
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::applyBlocks(int first, int oldCount,
                                     const QList<Block>& blocks,
                                     const DocxCursor& cur) {
    for (int i = 0; i < oldCount; ++i)
        m_doc.blocks.removeAt(first);
    for (int i = 0; i < blocks.size(); ++i)
        m_doc.blocks.insert(first + i, blocks.at(i));
    m_cursor = cur;
    setModified(true);
    emit blocksReplaced(first, oldCount, blocks.size());
    //  Geänderte Absätze neu prüfen. Der Deckel auf die eingefügten Blöcke
    //  hält den Aufwand am Tippen klein: ein Tastendruck betrifft EINEN Absatz.
    spellInvalidate(first, qMax(1, blocks.size()));
    //  Undo/Redo kann eine Änderung zurückholen oder entfernen - der
    //  Hinweisstreifen muss folgen. Bewusst NICHT im EditScope: dort liefe der
    //  Lauf über alle Blöcke bei JEDEM Tastendruck.
    refreshRevisions();
    emit cursorChanged();
    bumpFormat();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Edit-Scope: Bereich kopieren -> mutieren -> Kommando mit Vorher/Nachher.
// ─────────────────────────────────────────────────────────────────────────────
struct DocxEditController::EditScope {
    DocxEditController* c;
    int first;
    int oldCount;
    QList<Block> before;
    DocxCursor   curBefore;
    int          mergeKind;
    //  Struktur-Änderungen an einer Tabelle betreffen NEBEN den Blöcken auch
    //  das Gerüst - beides muss zusammen zurückgenommen werden.
    int          tableId = -1;
    Docx::TableDef tblBefore;

    EditScope(DocxEditController* ctl, int firstIdx, int count, int merge = -1)
        : c(ctl), first(firstIdx), oldCount(count), mergeKind(merge) {
        curBefore = c->m_cursor;
        before.reserve(count);
        for (int i = 0; i < count; ++i)
            before.append(c->m_doc.blocks.at(first + i));
    }
    //  VOR der Mutation aufrufen.
    void watchTable(int tid) {
        tableId = tid;
        tblBefore = c->m_doc.tableDef(tid);
    }
    //  newCount = Blockzahl des Bereichs NACH der Mutation; Cursor ist vom
    //  Aufrufer bereits gesetzt.
    void commit(int newCount) {
        QList<Block> after;
        after.reserve(newCount);
        for (int i = 0; i < newCount; ++i)
            after.append(c->m_doc.blocks.at(first + i));
        c->setModified(true);
        emit c->blocksReplaced(first, oldCount, newCount);
        emit c->cursorChanged();
        c->bumpFormat();
        auto* cmd = new DocxReplaceBlocksCommand(
            c, first, std::move(before), std::move(after),
            curBefore, c->m_cursor, mergeKind);
        if (tableId >= 0)
            cmd->snapshotTable(tableId, tblBefore, c->m_doc.tableDef(tableId));
        c->m_stack.push(cmd);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Text-Operationen
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::insertText(const QString& raw) {
    if (!m_ready || raw.isEmpty()) return;
    //  Ins Inhaltsverzeichnis wird nicht geschrieben - sein Inhalt kommt aus
    //  den Überschriften. Löschen bleibt möglich (der Feld-Run ist atomar).
    if (cursorInToc()) return;
    QString text = raw;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    if (!isEditableParagraph(b1) || !isEditableParagraph(b2))
        return;
    //  Selektion über Zellgrenzen: nur den Teil in der Zelle des Ankers ersetzen.
    if (b2 != b1) {
        const int wanted = b2;
        clampRangeToCell(b1, b2);
        if (b2 != wanted) p2 = blockLen(b2);
    }

    const bool oneChar = !m_cursor.hasSelection() && text.size() == 1
                         && !text.contains(QLatin1Char('\n'));
    EditScope scope(this, b1, b2 - b1 + 1, oneChar ? 0 : -1);

    //  1) Selektion entfernen: Bereich zu EINEM Block verschmelzen.
    Block& head = m_doc.blocks[b1];
    if (m_cursor.hasSelection()) {
        if (b1 == b2) {
            removeRangeInBlock(head, p1, p2);
        } else {
            removeRangeInBlock(head, p1, blockLen(b1));
            Block& tail = m_doc.blocks[b2];
            removeRangeInBlock(tail, 0, p2);
            const int k = ensureRunBoundary(head, p1);
            Q_UNUSED(k)
            head.runs += tail.runs;
            head.dirty = true;
            for (int i = b2; i > b1; --i)
                m_doc.blocks.removeAt(i);
        }
    }

    //  2) Text einfügen (\n teilt Absätze).
    const QStringList parts = text.split(QLatin1Char('\n'));
    const bool pendingHere = (m_pendingBlock == b1 && m_pendingPos == p1
                              && m_pending.set != 0);
    int curBlock = b1, curPos = p1;
    {
        Block& blk = m_doc.blocks[curBlock];
        const int at = ensureRunBoundary(blk, curPos);
        Run nr;
        //  Format erben: linker Nachbar-Run (nicht-opak), sonst rechter.
        int inherit = -1;
        for (int i = at - 1; i >= 0; --i)
            if (!blk.runs.at(i).opaque) { inherit = i; break; }
        if (inherit < 0)
            for (int i = at; i < blk.runs.size(); ++i)
                if (!blk.runs.at(i).opaque) { inherit = i; break; }
        if (inherit >= 0) {
            const Run& src = blk.runs.at(inherit);
            nr.rprSpan = src.rprSpan;
            nr.rprXml = src.rprXml;
            nr.rprMaterialized = src.rprMaterialized;
            nr.fmt = src.fmt;
        }
        nr.text  = parts.at(0);
        nr.dirty = true;
        if (pendingHere)
            applyPendingTo(nr);
        blk.runs.insert(at, nr);
        blk.dirty = true;
        curPos += parts.at(0).size();
    }
    for (int pi = 1; pi < parts.size(); ++pi) {
        //  Absatz-Split an curPos: Rest wandert in einen NEUEN Absatz.
        Block& blk = m_doc.blocks[curBlock];
        const int k = ensureRunBoundary(blk, curPos);
        Block nb;
        nb.kind = Block::Paragraph;
        //  Ein neuer Absatz bleibt in DERSELBEN Zelle - sonst stünde er nach dem
        //  Speichern außerhalb der Tabelle (die Gruppen-Emission ordnet Blöcke
        //  über tableId/row/col ihren Zellen zu).
        nb.tableId = blk.tableId;
        nb.row = blk.row;
        nb.col = blk.col;
        const QString ppr = blk.currentPpr(m_doc.docXml());
        if (!ppr.isEmpty()) { nb.pprXml = ppr; nb.pprMaterialized = true; }
        nb.pfmt = blk.pfmt;
        while (blk.runs.size() > k)
            nb.runs.append(blk.runs.takeAt(k));
        blk.dirty = true;
        nb.dirty  = true;
        if (!parts.at(pi).isEmpty()) {
            Run nr;
            if (!blk.runs.isEmpty()) {
                const Run& src = blk.runs.constLast();
                nr.rprSpan = src.rprSpan; nr.rprXml = src.rprXml;
                nr.rprMaterialized = src.rprMaterialized; nr.fmt = src.fmt;
            }
            nr.text = parts.at(pi);
            nr.dirty = true;
            if (pendingHere) applyPendingTo(nr);
            nb.runs.prepend(nr);
        }
        ++curBlock;
        m_doc.blocks.insert(curBlock, nb);
        curPos = parts.at(pi).size();
    }

    m_cursor = { curBlock, curPos, curBlock, curPos };
    if (pendingHere) { m_pendingBlock = curBlock; m_pendingPos = curPos; }
    scope.commit(curBlock - b1 + 1);
    if (text.size() == 1)
        runTranslit();
}

void DocxEditController::insertParagraphBreak() {
    //  Word-Verhalten: Enter in einem LEEREN Listenabsatz beendet die Liste,
    //  statt einen weiteren leeren Punkt anzulegen - der Cursor bleibt in
    //  DERSELBEN Zeile, nur ohne Aufzählung/Nummerierung. (Der erste Enter
    //  nach einem befüllten Punkt führt die Liste normal fort: der neue
    //  Absatz erbt das pPr inkl. w:numPr.)
    if (m_ready && !m_cursor.hasSelection() && isEditableParagraph(m_cursor.block)) {
        const Block& b = m_doc.blocks.at(m_cursor.block);
        if (b.textLength() == 0 && m_doc.resolvePar(b).numId > 0) {
            applyParProp(QStringLiteral("w:numPr"), QString(),
                         [](ParFmt& p) { p.numId = -1; p.ilvl = 0;
                                         p.set &= ~ParFmt::FNum; });
            return;
        }
    }
    insertText(QStringLiteral("\n"));
}
void DocxEditController::insertLineBreak()      { insertText(QString(kLineBreak)); }
void DocxEditController::insertClearBreak()     { insertText(QString(Docx::kClearBreak)); }

//  Deckt die Selektion eine GANZE Tabelle ab, löschen Entf/Rücktaste die
//  Tabelle statt nur den Zellinhalt der ersten Zelle (`clampRangeToCell` kürzt
//  eine zellübergreifende Selektion sonst auf eine Zelle). Das ist der Weg,
//  über den sich eine Tabelle mit der Tastatur entfernen lässt: Rahmen
//  anklicken (-> `selectTable`) oder über alle Zellen ziehen, dann Entf.
bool DocxEditController::deleteSelectedTable() {
    if (!m_ready || m_tableObjectSel < 0 || m_doc.blocks.isEmpty())
        return false;
    const int tid = m_tableObjectSel;
    m_tableObjectSel = -1;
    //  Defensiv: die Auswahl muss die Tabelle noch decken (zwischenzeitliche
    //  Änderungen könnten sie verschoben haben).
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    Q_UNUSED(p1) Q_UNUSED(p2)
    if (b1 != m_doc.tableFirstBlock(tid) || b2 != m_doc.tableLastBlock(tid))
        return false;
    deleteTable(tid);
    return true;
}

//  Ganze Tabelle als OBJEKT auswählen (Klick auf ihren Rahmen). Nur dieser
//  ausdrückliche Zustand lässt Entf/Rücktaste die Tabelle löschen - eine mit
//  der Maus über alle Zellen gezogene Selektion bleibt wie bisher auf ihre
//  Zelle geklemmt (`clampRangeToCell`), damit ein Ziehen keine Tabelle
//  wegreißt.
void DocxEditController::selectTable(int tableId) {
    if (!m_ready) return;
    const int first = m_doc.tableFirstBlock(tableId);
    const int last  = m_doc.tableLastBlock(tableId);
    if (first < 0 || last < first || last >= m_doc.blocks.size()) return;
    m_cursor.aBlock = first;
    m_cursor.aPos   = 0;
    m_cursor.block  = last;
    m_cursor.pos    = blockLen(last);
    m_tableObjectSel = tableId;
    clearPending();
    emit cursorChanged();
}

void DocxEditController::deleteBackward() {
    if (!m_ready) return;
    if (deleteSelectedTable()) return;
    if (m_cursor.hasSelection()) {
        //  Selektion löschen = leeren Text „einfügen" über dieselbe Mechanik.
        int b1, p1, b2, p2;
        orderedSelection(b1, p1, b2, p2);
        if (b2 != b1) {
            const int wanted = b2;
            clampRangeToCell(b1, b2);
            if (b2 != wanted) p2 = blockLen(b2);
        }
        //  Format der gelöschten Stelle merken (s. keepFormatOnEmptiedBlock).
        const RunFmt had = resolvedFormatAt(b1, p1);
        EditScope scope(this, b1, b2 - b1 + 1);
        Block& head = m_doc.blocks[b1];
        if (b1 == b2) {
            removeRangeInBlock(head, p1, p2);
        } else {
            removeRangeInBlock(head, p1, blockLen(b1));
            Block& tail = m_doc.blocks[b2];
            removeRangeInBlock(tail, 0, p2);
            head.runs += tail.runs;
            head.dirty = true;
            for (int i = b2; i > b1; --i)
                m_doc.blocks.removeAt(i);
        }
        m_cursor = { b1, p1, b1, p1 };
        clearPending();
        keepFormatOnEmptiedBlock(b1, had);
        scope.commit(1);
        return;
    }
    const int bi = m_cursor.block;
    if (!isEditableParagraph(bi)) return;
    if (m_cursor.pos > 0) {
        //  Ein Zeichen rückwärts (Surrogat-Paare als Einheit; endet das
        //  Zeichen einen opaken atomaren Run, wird der GANZE Run entfernt).
        const QString t = blockText(bi);
        int p1 = m_cursor.pos - 1;
        if (p1 > 0 && t.at(p1).isLowSurrogate() && t.at(p1 - 1).isHighSurrogate())
            --p1;
        int ri, ro;
        runAt(m_doc.blocks[bi], p1, &ri, &ro);
        //  Format des gelöschten Zeichens merken (s. keepFormatOnEmptiedBlock).
        const RunFmt had = resolvedFormatAt(bi, p1);
        EditScope scope(this, bi, 1, 1);
        Block& blk = m_doc.blocks[bi];
        if (ri >= 0 && blk.runs.at(ri).opaque && !blk.runs.at(ri).text.isEmpty()) {
            //  Atomar: kompletter opaker Run fällt (Hyperlink/Zeichnung).
            int acc = 0;
            for (int i = 0; i < ri; ++i) acc += blk.runs.at(i).text.size();
            p1 = acc;
            const int p2 = acc + blk.runs.at(ri).text.size();
            removeRangeInBlock(blk, p1, p2);
        } else {
            removeRangeInBlock(blk, p1, m_cursor.pos);
        }
        m_cursor = { bi, p1, bi, p1 };
        clearPending();
        keepFormatOnEmptiedBlock(bi, had);
        scope.commit(1);
        return;
    }
    //  Am Absatzanfang: mit dem VORHERIGEN Absatz verschmelzen.
    int prev = bi - 1;
    if (prev < 0) return;                           // nichts zu tun -> Pending hält
    //  ZELLGRENZE: der vorherige Absatz steht in einer anderen Zelle (oder im
    //  Rumpf) -> NICHT verschmelzen. Sonst fiele eine Zellgrenze weg und die
    //  Tabelle verlöre eine Zelle. Der Cursor bleibt einfach stehen.
    if (!sameCell(bi, prev))
        return;
    //  VERZEICHNIS: nie hineinverschmelzen. Sonst zieht ein Rücktaste-Druck am
    //  Absatzanfang den ganzen Absatz in den Feld-Absatz - er gilt dann als
    //  Verzeichnis, sein Text ist unsichtbar und nicht mehr bearbeitbar
    //  (genau so ist tests/ER.docx entstanden).
    if (m_doc.isTocParagraph(m_doc.blocks.at(prev))) {
        setCursor(prev, 0, false);
        return;
    }
    if (!isEditableParagraph(prev)) {
        //  Opaker Block davor (Tabelle/sectPr): nichts löschen, nur Cursor
        //  vor den Block bewegen (Schutz vor versehentlichem Strukturverlust).
        for (int i = prev; i >= 0; --i) {
            if (isEditableParagraph(i)) { setCursor(i, blockLen(i), false); return; }
        }
        return;
    }
    //  Der Cursor wechselt jetzt die Zeile -> das für die geleerte Zeile
    //  gemerkte Format erlischt (genau das vom Nutzer gewünschte Verhalten:
    //  „erst beim nächsten Backspace gilt die Überschriftformatierung").
    clearPending();
    EditScope scope(this, prev, 2);
    Block& a = m_doc.blocks[prev];
    Block& b = m_doc.blocks[bi];
    const int joinPos = a.textLength();
    a.runs += b.runs;
    a.dirty = true;
    m_doc.blocks.removeAt(bi);
    m_cursor = { prev, joinPos, prev, joinPos };
    scope.commit(1);
}

void DocxEditController::deleteForward() {
    if (!m_ready) return;
    if (deleteSelectedTable()) return;
    if (m_cursor.hasSelection()) { deleteBackward(); return; }
    const int bi = m_cursor.block;
    if (!isEditableParagraph(bi)) return;
    const int len = blockLen(bi);
    if (m_cursor.pos < len) {
        const QString t = blockText(bi);
        int p2 = m_cursor.pos + 1;
        if (p2 < len && t.at(m_cursor.pos).isHighSurrogate() && t.at(p2).isLowSurrogate())
            ++p2;
        int ri, ro;
        runAt(m_doc.blocks[bi], m_cursor.pos, &ri, &ro);
        //  Format des gelöschten Zeichens merken (s. keepFormatOnEmptiedBlock);
        //  bei Entf steht es RECHTS vom Cursor.
        const RunFmt had = resolvedFormatAt(bi, m_cursor.pos);
        EditScope scope(this, bi, 1);
        Block& blk = m_doc.blocks[bi];
        if (ri >= 0 && blk.runs.at(ri).opaque && !blk.runs.at(ri).text.isEmpty()) {
            int acc = 0;
            for (int i = 0; i < ri; ++i) acc += blk.runs.at(i).text.size();
            removeRangeInBlock(blk, acc, acc + blk.runs.at(ri).text.size());
            m_cursor = { bi, acc, bi, acc };
        } else {
            removeRangeInBlock(blk, m_cursor.pos, p2);
        }
        clearPending();
        keepFormatOnEmptiedBlock(bi, had);
        scope.commit(1);
        return;
    }
    //  Am Absatzende: mit dem NÄCHSTEN Absatz verschmelzen.
    const int nxt = bi + 1;
    //  ZELLGRENZE (s. deleteBackward): kein Verschmelzen aus einer anderen Zelle.
    if (nxt < m_doc.blocks.size() && !sameCell(bi, nxt))
        return;
    if (nxt >= m_doc.blocks.size() || !isEditableParagraph(nxt))
        return;
    //  Und ein Verzeichnis wird auch nicht in den laufenden Absatz gezogen
    //  (Gegenstück zur Sperre in deleteBackward).
    if (m_doc.isTocParagraph(m_doc.blocks.at(nxt))
        || m_doc.isTocParagraph(m_doc.blocks.at(bi)))
        return;
    EditScope scope(this, bi, 2);
    Block& a = m_doc.blocks[bi];
    Block& b = m_doc.blocks[nxt];
    a.runs += b.runs;
    a.dirty = true;
    m_doc.blocks.removeAt(nxt);
    scope.commit(1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Zeichenformat
// ─────────────────────────────────────────────────────────────────────────────
//  Steht der Cursor in einem Inhaltsverzeichnis? Dort ist NUR Schriftart und
//  -größe einstellbar; getippt wird nicht (der Inhalt entsteht aus den
//  Überschriften des Dokuments).
bool DocxEditController::cursorInToc() const {
    if (!m_ready || m_doc.blocks.isEmpty()) return false;
    const int bi = qBound(0, m_cursor.block, m_doc.blocks.size() - 1);
    return m_doc.isTocParagraph(m_doc.blocks.at(bi));
}

//  Schriftart/-größe des Verzeichnis-Absatzes: geschrieben wird in sein
//  `w:pPr/w:rPr` - die OOXML-Stelle für das Format der Absatzmarke, die Word
//  auch auf das Feldergebnis anwendet. Der Feld-Run selbst bleibt unangetastet
//  (er ist opak und geht verbatim heraus).
void DocxEditController::applyTocCharFormat(int field, const QVariant& value) {
    if (field != RunFmt::FFont && field != RunFmt::FSize)
        return;                      // fett/kursiv/Farbe: bewusst nicht
    const int bi = qBound(0, m_cursor.block, m_doc.blocks.size() - 1);
    //  Vom AKTUELL aufgelösten Format ausgehen, damit die jeweils andere
    //  Angabe nicht verlorengeht.
    const RunFmt cur = m_doc.paragraphMarkFormat(m_doc.blocks.at(bi));
    RunFmt f;
    f.font   = (field == RunFmt::FFont) ? value.toString() : cur.font;
    f.sizePt = (field == RunFmt::FSize) ? value.toReal()   : cur.sizePt;
    f.set    = RunFmt::FFont | RunFmt::FSize;

    EditScope scope(this, bi, 1);
    Block& blk = m_doc.blocks[bi];
    blk.pprXml = Document::upsertProp(blk.currentPpr(m_doc.docXml()),
                                      QStringLiteral("w:pPr"),
                                      QStringLiteral("w:rPr"),
                                      m_doc.buildRPrXml(f), kPPrOrder);
    blk.pprMaterialized = true;
    scope.commit(1);
}

void DocxEditController::applyCharFormat(int field, const QVariant& value) {
    if (!m_ready) return;
    if (cursorInToc()) { applyTocCharFormat(field, value); return; }
    if (!m_cursor.hasSelection()) {
        //  Pending-Format (fürs nächste Tippen an genau dieser Stelle).
        switch (field) {
        case RunFmt::FBold:      m_pending.bold      = value.toBool(); break;
        case RunFmt::FItalic:    m_pending.italic    = value.toBool(); break;
        case RunFmt::FUnderline: m_pending.underline = value.toBool(); break;
        case RunFmt::FFont:      m_pending.font      = value.toString(); break;
        case RunFmt::FSize:      m_pending.sizePt    = value.toReal(); break;
        case RunFmt::FColor:     m_pending.color     = value.value<QColor>(); break;
        }
        m_pending.set |= field;
        m_pendingBlock = m_cursor.block;
        m_pendingPos   = m_cursor.pos;
        bumpFormat();
        return;
    }
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    EditScope scope(this, b1, b2 - b1 + 1);
    const RunFmt save = m_pending;
    m_pending = RunFmt();
    m_pending.set = field;
    switch (field) {
    case RunFmt::FBold:      m_pending.bold      = value.toBool(); break;
    case RunFmt::FItalic:    m_pending.italic    = value.toBool(); break;
    case RunFmt::FUnderline: m_pending.underline = value.toBool(); break;
    case RunFmt::FFont:      m_pending.font      = value.toString(); break;
    case RunFmt::FSize:      m_pending.sizePt    = value.toReal(); break;
    case RunFmt::FColor:     m_pending.color     = value.value<QColor>(); break;
    }
    for (int i = b1; i <= b2; ++i) {
        if (!isEditableParagraph(i)) continue;
        Block& blk = m_doc.blocks[i];
        const int from = (i == b1) ? p1 : 0;
        const int to   = (i == b2) ? p2 : blockLen(i);
        if (from >= to) continue;
        ensureRunBoundary(blk, to);
        const int k1 = ensureRunBoundary(blk, from);
        int acc = 0;
        for (int r = 0; r < k1; ++r) acc += blk.runs.at(r).text.size();
        for (int r = k1; r < blk.runs.size() && acc < to; ++r) {
            Run& rn = blk.runs[r];
            if (!rn.opaque && !rn.text.isEmpty())
                applyPendingTo(rn);                       // nutzt m_pending
            acc += rn.text.size();
        }
        blk.dirty = true;
    }
    m_pending = save;
    scope.commit(b2 - b1 + 1);
}

//  Toggle-Zustand: „alles in der Selektion bereits X?" -> aus, sonst an.
static bool allRunsHave(const Document& d, const DocxCursor& cur,
                        const std::function<bool(const RunFmt&)>& pred,
                        int b1, int p1, int b2, int p2) {
    bool any = false;
    for (int i = b1; i <= b2; ++i) {
        if (i < 0 || i >= d.blocks.size()) continue;
        const Block& blk = d.blocks.at(i);
        if (blk.kind != Block::Paragraph) continue;
        const int from = (i == b1) ? p1 : 0;
        const int to   = (i == b2) ? p2 : blk.textLength();
        int acc = 0;
        for (const Run& r : blk.runs) {
            const int rs = acc, re = acc + r.text.size();
            acc = re;
            if (r.opaque || r.text.isEmpty()) continue;
            if (re <= from || rs >= to) continue;
            any = true;
            if (!pred(d.resolveRun(blk, r)))
                return false;
        }
    }
    Q_UNUSED(cur)
    return any;
}

void DocxEditController::toggleBold() {
    int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
    const bool on = m_cursor.hasSelection()
        ? !allRunsHave(m_doc, m_cursor, [](const RunFmt& f){ return f.bold; }, b1, p1, b2, p2)
        : !currentFormat().value(QStringLiteral("bold")).toBool();
    applyCharFormat(RunFmt::FBold, on);
}
void DocxEditController::toggleItalic() {
    int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
    const bool on = m_cursor.hasSelection()
        ? !allRunsHave(m_doc, m_cursor, [](const RunFmt& f){ return f.italic; }, b1, p1, b2, p2)
        : !currentFormat().value(QStringLiteral("italic")).toBool();
    applyCharFormat(RunFmt::FItalic, on);
}
void DocxEditController::toggleUnderline() {
    int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
    const bool on = m_cursor.hasSelection()
        ? !allRunsHave(m_doc, m_cursor, [](const RunFmt& f){ return f.underline; }, b1, p1, b2, p2)
        : !currentFormat().value(QStringLiteral("underline")).toBool();
    applyCharFormat(RunFmt::FUnderline, on);
}
void DocxEditController::setFontFamily(const QString& f) { applyCharFormat(RunFmt::FFont, f); }
void DocxEditController::setFontSizePt(qreal pt) {
    applyCharFormat(RunFmt::FSize, qBound<qreal>(6.0, pt, 96.0));
}
void DocxEditController::setTextColor(const QColor& c) { applyCharFormat(RunFmt::FColor, c); }

// ─────────────────────────────────────────────────────────────────────────────
//  Absatzformat
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::applyParProp(const QString& propName, const QString& newXml,
                                      const std::function<void(ParFmt&)>& mut) {
    if (!m_ready) return;
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    Q_UNUSED(p1) Q_UNUSED(p2)
    EditScope scope(this, b1, b2 - b1 + 1);
    for (int i = b1; i <= b2; ++i) {
        if (!isEditableParagraph(i)) continue;
        Block& blk = m_doc.blocks[i];
        //  Für w:spacing hängt das fertige Element vom (mutierten) ParFmt des
        //  Blocks ab -> mut() zuerst, dann Fragment bauen (s. Aufrufer).
        mut(blk.pfmt);
        QString xml = newXml;
        if (propName == QLatin1String("w:spacing")) {
            //  Nur DIREKT gesetzte Werte schreiben (Stil-Werte bleiben Stil).
            QString attrs;
            if (blk.pfmt.set & ParFmt::FBefore)
                attrs += QStringLiteral(" w:before=\"%1\"").arg(qRound(blk.pfmt.beforePt * 20));
            if (blk.pfmt.set & ParFmt::FAfter)
                attrs += QStringLiteral(" w:after=\"%1\"").arg(qRound(blk.pfmt.afterPt * 20));
            if (blk.pfmt.set & ParFmt::FLine)
                attrs += QStringLiteral(" w:line=\"%1\" w:lineRule=\"auto\"")
                             .arg(qRound(blk.pfmt.lineSpacing * 240));
            xml = attrs.isEmpty() ? QString()
                                  : QStringLiteral("<w:spacing%1/>").arg(attrs);
        }
        blk.pprXml = Document::upsertProp(blk.currentPpr(m_doc.docXml()),
                                          QStringLiteral("w:pPr"), propName, xml, kPPrOrder);
        blk.pprMaterialized = true;
    }
    scope.commit(b2 - b1 + 1);
}

void DocxEditController::setAlignment(int align) {
    align = qBound(0, align, 3);
    static const char* vals[] = { "left", "center", "right", "both" };
    applyParProp(QStringLiteral("w:jc"),
                 QStringLiteral("<w:jc w:val=\"%1\"/>").arg(QLatin1String(vals[align])),
                 [align](ParFmt& p) { p.align = align; p.set |= ParFmt::FAlign; });
}
void DocxEditController::setLineSpacing(qreal m) {
    m = qBound<qreal>(0.5, m, 4.0);
    applyParProp(QStringLiteral("w:spacing"), QString(),
                 [m](ParFmt& p) { p.lineSpacing = m; p.set |= ParFmt::FLine; });
}
void DocxEditController::setSpaceBeforePt(qreal pt) {
    pt = qBound<qreal>(0.0, pt, 200.0);
    applyParProp(QStringLiteral("w:spacing"), QString(),
                 [pt](ParFmt& p) { p.beforePt = pt; p.set |= ParFmt::FBefore; });
}
void DocxEditController::setSpaceAfterPt(qreal pt) {
    pt = qBound<qreal>(0.0, pt, 200.0);
    applyParProp(QStringLiteral("w:spacing"), QString(),
                 [pt](ParFmt& p) { p.afterPt = pt; p.set |= ParFmt::FAfter; });
}

QVariantList DocxEditController::paragraphStyles() const {
    QVariantList out;
    if (!m_ready) return out;
    const QList<Docx::StyleInfo>& styles = m_doc.paragraphStyles();
    out.reserve(styles.size());
    for (const Docx::StyleInfo& s : styles) {
        QVariantMap m;
        m.insert(QStringLiteral("id"),        s.id);
        m.insert(QStringLiteral("name"),      s.name);
        m.insert(QStringLiteral("isDefault"), s.isDefault);
        out.append(m);
    }
    //  Überschriften IMMER anbieten, auch wenn das Dokument keine mitbringt
    //  (der Normalfall - dann ließe sich sonst keine schreiben). Angelegt wird
    //  die Vorlage erst beim Anwenden, s. setParagraphStyle.
    for (int lv = 1; lv <= Docx::Document::kMaxHeadingLevel; ++lv) {
        const QString id = QStringLiteral("Heading%1").arg(lv);
        if (m_doc.hasStyle(id)) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"),        id);
        m.insert(QStringLiteral("name"),      QStringLiteral("heading %1").arg(lv));
        m.insert(QStringLiteral("isDefault"), false);
        out.append(m);
    }
    return out;
}

void DocxEditController::setParagraphStyle(const QString& styleId) {
    if (!m_ready) return;
    //  Standardvorlage = KEIN w:pStyle: Word schreibt sie nie in den Absatz,
    //  und ein explizites w:pStyle="Standard" würde beim Vorlagenwechsel im
    //  Zieldokument anders auflösen als der Absatz ohne Angabe.
    const bool toDefault = styleId.isEmpty()
                           || styleId == m_doc.defaultParagraphStyleId();
    QString id = toDefault ? QString() : styleId;
    //  Überschriftvorlage, die es im Dokument noch nicht gibt, jetzt anlegen -
    //  ein w:pStyle auf eine undefinierte id bliebe wirkungslos.
    if (!id.isEmpty() && !m_doc.hasStyle(id)) {
        static const QRegularExpression kHeading(QStringLiteral("^Heading([1-9])$"));
        const auto m = kHeading.match(id);
        if (m.hasMatch())
            id = m_doc.ensureHeadingStyle(m.captured(1).toInt());
        if (id.isEmpty()) return;
    }
    const QString styleXml = id.isEmpty()
                                 ? QString()
                                 : QStringLiteral("<w:pStyle w:val=\"%1\"/>")
                                       .arg(Document::xmlEscape(id));

    //  ── Nur die MARKIERTEN Zeilen, nicht der ganze Absatz ───────────────────
    //  Viele Dokumente trennen mehrere Überschriften nur durch `w:br` (Beleg:
    //  `tests/ER.docx`, ein Absatz mit sechs Zeilen). Eine Absatzvorlage trifft
    //  zwangsläufig den GANZEN Absatz - wer eine Zeile markiert, formatierte so
    //  auch alles darunter (Nutzerbefund). Deshalb wird der Absatz vorher an
    //  seinen Zeilenumbrüchen geteilt; Teilen und Vorlage sind EIN Undo-Schritt.
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    if (b1 == b2 && isEditableParagraph(b1)) {
        const QString t = m_doc.blocks.at(b1).plainText();
        if (t.contains(Docx::kLineBreak)) {
            //  Markierung auf GANZE Zeilen aufziehen.
            int from = qBound(0, p1, int(t.size()));
            int to   = qBound(0, p2, int(t.size()));
            while (from > 0 && t.at(from - 1) != Docx::kLineBreak) --from;
            while (to < t.size() && t.at(to) != Docx::kLineBreak) ++to;
            if (!(from == 0 && to >= t.size())) {
                EditScope scope(this, b1, 1);
                const int before = int(m_doc.blocks.size());
                const int mid = splitOffLines(b1, from, to);
                if (mid < 0) return;                         // nichts zu teilen
                Block& blk = m_doc.blocks[mid];
                blk.pprXml = Document::upsertProp(blk.currentPpr(m_doc.docXml()),
                                                  QStringLiteral("w:pPr"),
                                                  QStringLiteral("w:pStyle"),
                                                  styleXml, kPPrOrder);
                blk.pprMaterialized = true;
                blk.pfmt.styleId = id;
                //  Auswahl auf den neuen Absatz nachziehen.
                m_cursor.block = m_cursor.aBlock = mid;
                m_cursor.aPos  = 0;
                m_cursor.pos   = blk.textLength();
                clearPending();
                scope.commit(1 + int(m_doc.blocks.size()) - before);
                return;
            }
        }
    }

    applyParProp(QStringLiteral("w:pStyle"), styleXml,
                 [id](ParFmt& p) { p.styleId = id; });
}

void DocxEditController::insertTable(int rows, int cols) {
    if (!m_ready) return;
    rows = qBound(1, rows, 100);
    cols = qBound(1, cols, 32);

    //  Einfügestelle: hinter dem Cursor-Absatz. Steht er in einer Zelle, hinter
    //  der ganzen Tabelle (keine verschachtelten Tabellen).
    int at = qBound(0, m_cursor.block, m_doc.blocks.size() - 1) + 1;
    if (!m_doc.blocks.isEmpty()) {
        const Block& cur = m_doc.blocks.at(qBound(0, m_cursor.block,
                                                  m_doc.blocks.size() - 1));
        if (cur.tableId >= 0) {
            const int last = m_doc.tableLastBlock(cur.tableId);
            if (last >= 0) at = last + 1;
        }
    }
    at = qBound(0, at, m_doc.blocks.size());

    //  Der Undo-Schnappschuss deckt einen LEEREN Bereich ab (reines Einfügen):
    //  vorher 0 Blöcke, nachher rows*cols. Das Kommando kann das bereits.
    //  Die TableDef bleibt beim Rückgängig in m_tables stehen - sie ist ohne
    //  zugehörige Blöcke inert (die Emission läuft über die Blöcke) und wird
    //  beim Wiederherstellen mit derselben tableId wieder benutzt.
    EditScope scope(this, at, 0);
    const int before = int(m_doc.blocks.size());
    const int first = m_doc.insertTable(at, rows, cols);
    if (first < 0) return;
    //  Die GEMESSENE Zahl neuer Blöcke, nicht rows*cols: das Modell hängt hinter
    //  die Tabelle noch einen leeren Absatz, falls dort keiner steht (Word macht
    //  es genauso - ohne ihn käme der Cursor an einer Tabelle am Dokumentende
    //  nie wieder heraus). Mit der alten festen Zahl bliebe dieser Absatz beim
    //  Rückgängigmachen stehen.
    const int added = int(m_doc.blocks.size()) - before;
    m_cursor = { first, 0, first, 0 };
    clearPending();
    scope.commit(qMax(0, added));
}

void DocxEditController::insertTableOfContents() {
    if (!m_ready) return;
    //  Wie beim Einfügen einer Tabelle: hinter dem Cursor-Absatz, bei einer
    //  Tabelle hinter der GANZEN Tabelle.
    int at = qBound(0, m_cursor.block, qMax(0, m_doc.blocks.size() - 1)) + 1;
    if (!m_doc.blocks.isEmpty()) {
        const Block& cur = m_doc.blocks.at(qBound(0, m_cursor.block,
                                                  m_doc.blocks.size() - 1));
        if (cur.tableId >= 0) {
            const int last = m_doc.tableLastBlock(cur.tableId);
            if (last >= 0) at = last + 1;
        }
    }
    at = qBound(0, at, m_doc.blocks.size());

    EditScope scope(this, at, 0);
    const int idx = m_doc.insertToc(at);
    if (idx < 0) return;
    m_cursor = { idx, 0, idx, 0 };
    clearPending();
    scope.commit(1);
}

//  Unterschrift/Stempel: Bild einsetzen UND sofort verankern. Beides gehört
//  für den Nutzer zusammen (er will es frei hinschieben), deshalb EIN
//  Undo-Schritt über ein Makro. Danach ist das Bild ausgewählt - die
//  Ziehpunkte der Kachel hängen an der Auswahl.
void DocxEditController::insertSignatureImage(const QString& fileUrl) {
    if (!m_ready) return;
    const QString path = mg::toLocalPath(fileUrl);
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit imageInsertFailed(QStringLiteral("Bilddatei nicht lesbar."));
        return;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    m_stack.beginMacro(QStringLiteral("Unterschrift"));
    insertImageBytes(bytes, QFileInfo(path).suffix(), 0, 0);
    const int bi  = qBound(0, m_cursor.block, qMax(0, m_doc.blocks.size() - 1));
    const int pos = qMax(0, m_cursor.pos - 1);      // Objekt-Zeichen des Bildes

    //  ZUERST auswählen, dann verankern: `setImageFloating` findet das Bild
    //  über den Cursor (`imageAtCursor`) - und `paragraphImage` als Rückfall
    //  greift nur bei einem Absatz, der NUR aus dem Bild besteht. Eine
    //  Unterschrift steht aber meist hinter Text.
    setCursor(bi, pos, false);
    setCursor(bi, pos + 1, true);

    bool ok = false;
    {
        const QVector<Docx::InlineImage> imgs =
            m_doc.paragraphImages(m_doc.blocks.at(bi));
        for (const Docx::InlineImage& ii : imgs)
            if (ii.pos == pos) ok = true;
    }
    if (ok) setImageFloating(bi, true);
    m_stack.endMacro();
    if (!ok) { m_stack.undo(); return; }

    //  Die Auswahl auf das Objekt-Zeichen wiederherstellen (das Verankern hat
    //  den Absatz neu gesetzt und den Cursor mitgeführt).
    setCursor(bi, pos, false);
    setCursor(bi, pos + 1, true);
}

void DocxEditController::insertImage(const QString& fileUrl) {
    if (!m_ready) return;
    const QString path = mg::toLocalPath(fileUrl);
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit imageInsertFailed(QStringLiteral("Bilddatei nicht lesbar."));
        return;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    insertImageBytes(bytes, QFileInfo(path).suffix(), 0, 0);
}

void DocxEditController::insertImageData(const QByteArray& bytes,
                                         const QString& ext,
                                         qint64 cxEmu, qint64 cyEmu) {
    insertImageBytes(bytes, ext, cxEmu, cyEmu);
}

//  Das Bild kommt AN DIE CURSOR-STELLE in den laufenden Absatz - genau wie in
//  Word. Steht der Cursor in einem leeren Absatz, sieht das aus wie früher
//  („Bild allein in seiner Zeile"); mitten im Text steht es IM Text, und zwei
//  Bilder hintereinander stehen nebeneinander (s. DocxTextArea, Zeilenbänder).
void DocxEditController::insertImageBytes(const QByteArray& bytes,
                                          const QString& ext,
                                          qint64 cxEmu, qint64 cyEmu) {
    if (!m_ready || bytes.isEmpty() || m_doc.blocks.isEmpty()) return;
    const int bi = qBound(0, m_cursor.block, m_doc.blocks.size() - 1);
    //  Ins Inhaltsverzeichnis wird nicht geschrieben (sein Inhalt kommt aus den
    //  Überschriften); opake Blöcke sind ohnehin keine Absätze.
    if (!isEditableParagraph(bi) || cursorInToc()) {
        emit imageInsertFailed(QStringLiteral("Hier kann kein Bild stehen."));
        return;
    }
    const int pos = qBound(0, m_cursor.pos, blockLen(bi));

    EditScope scope(this, bi, 1);
    const int at = ensureRunBoundary(m_doc.blocks[bi], pos);
    QString err;
    if (m_doc.insertImageRunAt(bi, at, bytes, ext, &err, cxEmu, cyEmu) < 0) {
        emit imageInsertFailed(err);
        return;                                   // Scope ohne commit = kein Kommando
    }
    m_cursor = { bi, pos + 1, bi, pos + 1 };
    clearPending();
    scope.commit(1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Rechtschreib-PRÜFUNG - absatzweise, asynchron, ohne den Text anzufassen
// ─────────────────────────────────────────────────────────────────────────────
//  Ein Auftrag je Absatz auf einem Pool mit EINEM Thread: die Reihenfolge
//  bleibt die Tippreihenfolge, und `m_spell` (das Wörterbuch) gehört genau
//  diesem Thread - es wird dort erzeugt und nie vom GUI-Thread berührt.
namespace {
class SpellTask : public QRunnable {
public:
    SpellTask(QPointer<DocxEditController> owner, std::shared_ptr<mg::SpellChecker> sc,
              int block, QString text, QStringList ignored, int gen)
        : m_owner(std::move(owner)), m_sc(std::move(sc)), m_block(block),
          m_text(std::move(text)), m_ignored(std::move(ignored)), m_gen(gen) {
        setAutoDelete(true);
    }
    void run() override {
        if (!m_sc) return;
        for (const QString& w : std::as_const(m_ignored)) m_sc->ignoreWord(w);
        const std::vector<mg::SpellRange> bad = m_sc->checkText(m_text);
        QVector<mg::SpellRange> out;
        out.reserve(int(bad.size()));
        for (const mg::SpellRange& r : bad) out.append(r);
        const int block = m_block, gen = m_gen;
        QPointer<DocxEditController> owner = m_owner;
        QMetaObject::invokeMethod(owner ? owner.data() : nullptr,
                                  [owner, block, gen, out]() {
            if (owner) owner->spellResult(block, gen, out);
        }, Qt::QueuedConnection);
    }
private:
    QPointer<DocxEditController> m_owner;
    std::shared_ptr<mg::SpellChecker> m_sc;
    int m_block;
    QString m_text;
    QStringList m_ignored;
    int m_gen;
};
} // namespace

void DocxEditController::spellResult(int block, int gen,
                                     const QVector<mg::SpellRange>& bad) {
    if (gen != m_spellGen) return;                  // Ergebnis eines alten Laufs
    m_spellPending.remove(block);
    if (bad.isEmpty()) m_spellBad.remove(block);
    else               m_spellBad.insert(block, bad);
    emit spellRangesChanged(block);
}

void DocxEditController::spellStart() {
    ++m_spellGen;
    m_spellBad.clear();
    m_spellPending.clear();
    m_spellReady = false;
    m_spellLang.clear();
    m_spell.reset();
    if (!m_spellPool) {
        m_spellPool = new QThreadPool(this);
        m_spellPool->setMaxThreadCount(1);
    }
    if (!m_spellOn || !mg::SpellChecker::compiledIn()) {
        emit spellChanged();
        emit spellRangesChanged(-1);
        return;
    }
    //  Sprache: die vom Aufrufer gesetzte, sonst die erste gefundene. Die
    //  EINSTELLUNG liest QML und reicht sie herein - der Controller gehört zur
    //  Kachel und kennt die globalen Einstellungen bewusst nicht.
    QString lang = m_spellWanted;
    if (lang.isEmpty()) {
        const QStringList have = mg::SpellChecker::availableLanguages();
        if (!have.isEmpty()) lang = have.first();
    }
    auto sc = std::make_shared<mg::SpellChecker>();
    //  Das Öffnen liest zwei Dateien - im Vergleich zum Prüfen selbst
    //  vernachlässigbar, aber es gehört trotzdem nicht in den GUI-Thread.
    //  Deshalb: hier nur anlegen, geöffnet wird beim ersten Auftrag.
    m_spellReady = !lang.isEmpty() && sc->open(lang);
    m_spellLang  = m_spellReady ? sc->language() : QString();
    m_spell      = m_spellReady ? sc : nullptr;
    emit spellChanged();
    if (m_spellReady) spellInvalidate(0, m_doc.blocks.size());
    else              emit spellRangesChanged(-1);
}

void DocxEditController::setSpellCheckEnabled(bool on) {
    if (m_spellOn == on) return;
    m_spellOn = on;
    spellStart();
}

void DocxEditController::setSpellLanguage(const QString& lang) {
    if (m_spellWanted == lang) return;
    m_spellWanted = lang;
    if (m_spellOn) spellStart();
}

void DocxEditController::spellInvalidate(int first, int count) {
    if (!m_spellReady) return;
    const int n = m_doc.blocks.size();
    for (int i = qMax(0, first); i < qMin(n, first + qMax(1, count)); ++i) {
        m_spellBad.remove(i);
        spellRequest(i);
    }
    emit spellRangesChanged(-1);
}

void DocxEditController::spellRequest(int block) {
    if (!m_spellReady || !m_spell || !m_spellPool) return;
    if (block < 0 || block >= m_doc.blocks.size()) return;
    const Docx::Block& b = m_doc.blocks.at(block);
    if (b.kind != Docx::Block::Paragraph) return;
    m_spellPending.insert(block);
    m_spellPool->start(new SpellTask(QPointer<DocxEditController>(this), m_spell,
                                     block, b.plainText(), m_spellIgnored,
                                     m_spellGen));
}

const QVector<mg::SpellRange>& DocxEditController::spellRanges(int block) const {
    static const QVector<mg::SpellRange> empty;
    const auto it = m_spellBad.constFind(block);
    return it == m_spellBad.cend() ? empty : it.value();
}

//  Das beanstandete Wort an einer Stelle finden (Kontextmenü).
int DocxEditController::spellWordAt(int block, int pos, mg::SpellRange* out) const {
    const QVector<mg::SpellRange>& r = spellRanges(block);
    for (const mg::SpellRange& s : r) {
        if (pos >= s.start && pos <= s.start + s.length) {
            if (out) *out = s;
            return 1;
        }
    }
    return 0;
}

QStringList DocxEditController::spellSuggestions(int block, int pos) const {
    mg::SpellRange r;
    if (!m_spellReady || !m_spell || !spellWordAt(block, pos, &r)) return {};
    if (block < 0 || block >= m_doc.blocks.size()) return {};
    const QString word = m_doc.blocks.at(block).plainText().mid(r.start, r.length);
    //  Gelesen wird hier im GUI-Thread - Vorschläge sind eine EINZELNE Abfrage
    //  auf Tastendruck (Rechtsklick), keine Schleife über das Dokument.
    return m_spell->suggest(word);
}

bool DocxEditController::spellReplaceAt(int block, int pos,
                                        const QString& replacement) {
    mg::SpellRange r;
    if (replacement.isEmpty() || !spellWordAt(block, pos, &r)) return false;
    //  Über den gewöhnlichen Weg: auswählen, ersetzen - EIN Undo-Schritt, und
    //  alle Beobachter (Anzeige, Modified-Flag) erfahren es wie immer.
    setCursor(block, r.start, false);
    setCursor(block, r.start + r.length, true);
    insertText(replacement);
    return true;
}

void DocxEditController::spellIgnoreAt(int block, int pos) {
    mg::SpellRange r;
    if (!spellWordAt(block, pos, &r)) return;
    if (block < 0 || block >= m_doc.blocks.size()) return;
    const QString word = m_doc.blocks.at(block).plainText().mid(r.start, r.length);
    if (word.isEmpty() || m_spellIgnored.contains(word)) return;
    m_spellIgnored << word;
    //  Das Wort kann überall stehen - also alles neu prüfen.
    spellInvalidate(0, m_doc.blocks.size());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Änderungsverfolgung annehmen / verwerfen
// ─────────────────────────────────────────────────────────────────────────────
//  Der Run an einer Stelle: die Runs eines Absatzes liegen hintereinander, die
//  Zeichenposition zählt über sie hinweg.
static int runIndexAt(const Docx::Block& b, int pos) {
    int acc = 0;
    for (int i = 0; i < b.runs.size(); ++i) {
        const int len = int(b.runs.at(i).text.size());
        if (pos >= acc && pos < acc + len) return i;
        acc += len;
    }
    return -1;
}

int DocxEditController::revisionAt(int block, int pos) const {
    if (!m_ready || block < 0 || block >= m_doc.blocks.size()) return 0;
    const Docx::Block& b = m_doc.blocks.at(block);
    const int ri = runIndexAt(b, pos);
    if (ri < 0) return 0;
    switch (b.runs.at(ri).revision) {
    case Docx::Run::RevInserted: return 1;
    case Docx::Run::RevDeleted:  return 2;
    default:                     return 0;
    }
}

QString DocxEditController::revisionAuthorAt(int block, int pos) const {
    if (!m_ready || block < 0 || block >= m_doc.blocks.size()) return {};
    const Docx::Block& b = m_doc.blocks.at(block);
    const int ri = runIndexAt(b, pos);
    return ri < 0 ? QString() : b.runs.at(ri).revAuthor;
}

//  EIN Undo-Schritt über dasselbe Kommando wie jede andere Textänderung: der
//  Scope schnappt den Absatz vorher und nachher.
bool DocxEditController::applyRevisionAt(int block, int pos, bool accept) {
    if (!m_ready || block < 0 || block >= m_doc.blocks.size()) return false;
    const int ri = runIndexAt(m_doc.blocks.at(block), pos);
    if (ri < 0) return false;
    EditScope scope(this, block, 1);
    if (!m_doc.applyRevision(block, ri, accept)) return false;
    //  OHNE commit gäbe es weder Undo-Schritt noch `blocksReplaced`, und das
    //  Dokument gälte als unverändert - die Annahme wäre beim Verlassen der
    //  Kachel verloren gewesen.
    scope.commit(1);
    refreshRevisions();
    return true;
}

bool DocxEditController::acceptRevisionAt(int block, int pos) {
    return applyRevisionAt(block, pos, true);
}

bool DocxEditController::rejectRevisionAt(int block, int pos) {
    return applyRevisionAt(block, pos, false);
}

//  ── ALLE Änderungen auf einmal ──────────────────────────────────────────────
//  EIN Undo-Schritt (Makro) über alle betroffenen Absätze. Gearbeitet wird von
//  HINTEN nach vorn: `applyRevision` kann Runs entfernen, spätere Indizes im
//  selben Absatz verschöben sich sonst.
int DocxEditController::applyAllRevisions(bool accept) {
    if (!m_ready) return 0;
    int done = 0;
    m_stack.beginMacro(accept ? QStringLiteral("Alle Änderungen annehmen")
                              : QStringLiteral("Alle Änderungen verwerfen"));
    for (int bi = 0; bi < m_doc.blocks.size(); ++bi) {
        //  Trägt der Absatz überhaupt eine Änderung? Sonst kein Kommando.
        bool any = false;
        for (const Docx::Run& r : m_doc.blocks.at(bi).runs)
            if (r.revision != Docx::Run::RevNone) { any = true; break; }
        if (!any) continue;

        EditScope scope(this, bi, 1);
        int n = 0;
        for (int ri = m_doc.blocks.at(bi).runs.size() - 1; ri >= 0; --ri) {
            if (ri >= m_doc.blocks.at(bi).runs.size()) continue;
            if (m_doc.blocks.at(bi).runs.at(ri).revision == Docx::Run::RevNone) continue;
            if (m_doc.applyRevision(bi, ri, accept)) ++n;
        }
        if (n == 0) continue;                 // Scope ohne commit = kein Kommando
        scope.commit(1);
        done += n;
    }
    m_stack.endMacro();
    if (done == 0) m_stack.undo();            // leeres Makro nicht stehen lassen
    refreshRevisions();
    return done;
}

int DocxEditController::acceptAllRevisions() { return applyAllRevisions(true); }
int DocxEditController::rejectAllRevisions() { return applyAllRevisions(false); }

//  Wie viele nachverfolgte Änderungen stehen im Dokument, und von wem?
//  Gezählt werden GRUPPEN: aufeinanderfolgende Runs derselben Art und desselben
//  Autors sind EINE Änderung - Word zählt genauso, und ein Wort in drei Runs
//  wäre sonst „drei Änderungen".
void DocxEditController::refreshRevisions() {
    int count = 0;
    QStringList authors;
    for (const Docx::Block& b : m_doc.blocks) {
        int lastRev = int(Docx::Run::RevNone);
        QString lastAuthor;
        for (const Docx::Run& r : b.runs) {
            if (r.revision == Docx::Run::RevNone) {
                lastRev = int(Docx::Run::RevNone);
                lastAuthor.clear();
                continue;
            }
            if (int(r.revision) != lastRev || r.revAuthor != lastAuthor) ++count;
            lastRev    = int(r.revision);
            lastAuthor = r.revAuthor;
            if (!r.revAuthor.isEmpty() && !authors.contains(r.revAuthor))
                authors.append(r.revAuthor);
        }
    }
    if (count == m_revCount && authors == m_revAuthors) return;
    m_revCount   = count;
    m_revAuthors = authors;
    emit revisionsChanged();
}

QVariantList DocxEditController::folderImages() const {
    //  PDFs kommen MIT: ein Tippen darauf öffnet die Seitenauswahl (s.
    //  DocxSurface). Die Abfrage selbst teilt sich der Editor mit dem
    //  PDF-Editor - sie steht in `core/FolderImages`.
    return mg::folderImages(m_source, 300, true);
}

QString DocxEditController::folderPath() const {
    if (m_source.isEmpty()) return QString();
    return QFileInfo(m_source).absolutePath();
}

int DocxEditController::pdfPageCount(const QString& fileUrl) const {
    const QString path = mg::toLocalPath(fileUrl);
    if (path.isEmpty()) return 0;
    QPdfDocument d;
    if (d.load(path) != QPdfDocument::Error::None) return 0;
    return qMax(0, d.pageCount());
}

void DocxEditController::insertPdfPage(const QString& fileUrl, int page) {
    if (!m_ready) return;
    const QString path = mg::toLocalPath(fileUrl);
    if (path.isEmpty()) return;
    QPdfDocument d;
    if (d.load(path) != QPdfDocument::Error::None || d.pageCount() <= 0) {
        emit imageInsertFailed(QStringLiteral("PDF nicht lesbar."));
        return;
    }
    page = qBound(0, page, d.pageCount() - 1);

    //  150 dpi: Druckqualität, ohne den Container zu sprengen. Die Punktgröße
    //  der Seite ist 1/72 Zoll - daraus folgt die Pixelgröße direkt.
    constexpr qreal kDpi = 150.0;
    const QSizeF ptSize = d.pagePointSize(page);
    const QSize px(qMax(1, qRound(ptSize.width()  / 72.0 * kDpi)),
                   qMax(1, qRound(ptSize.height() / 72.0 * kDpi)));
    const QImage raw = d.render(page, px);
    if (raw.isNull()) {
        emit imageInsertFailed(QStringLiteral("Seite konnte nicht gerendert werden."));
        return;
    }
    //  Auf WEISS komponieren - der Renderer liefert einen transparenten Grund,
    //  im Dokument sähe die Seite sonst je nach Betrachter unterschiedlich aus.
    QImage flat(raw.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    { QPainter p(&flat); p.drawImage(0, 0, raw); }

    QByteArray bytes;
    {
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        if (!flat.save(&buf, "PNG")) {
            emit imageInsertFailed(QStringLiteral("Seite konnte nicht gewandelt werden."));
            return;
        }
    }
    insertImageData(bytes, QStringLiteral("png"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tabellen-Struktur (Kontextmenü)
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::applyTableDef(int tableId, const Docx::TableDef& def) {
    m_doc.setTableDef(tableId, def);
}

QVariantMap DocxEditController::tableInfoAt(int block) const {
    QVariantMap m;
    m.insert(QStringLiteral("table"), false);
    if (!m_ready || m_doc.blocks.isEmpty()) return m;
    const int bi = qBound(0, block < 0 ? m_cursor.block : block,
                          m_doc.blocks.size() - 1);
    const Block& b = m_doc.blocks.at(bi);
    if (b.tableId < 0) return m;

    const int tid = b.tableId;
    const bool editable = m_doc.tableStructEditable(tid);
    m.insert(QStringLiteral("table"), true);
    m.insert(QStringLiteral("tableId"), tid);
    m.insert(QStringLiteral("row"), b.row);
    m.insert(QStringLiteral("col"), b.col);
    m.insert(QStringLiteral("rows"), m_doc.tableRowCount(tid));
    m.insert(QStringLiteral("cols"), m_doc.tableColumnCount(tid));
    m.insert(QStringLiteral("editable"), editable);
    //  Breiten für den Dialog in MILLIMETERN (Twips sind keine Nutzergröße).
    QVariantList mm;
    if (editable)
        for (int tw : m_doc.tableColumnWidths(tid))
            mm.append(qRound(tw / 56.6929 * 10.0) / 10.0);
    m.insert(QStringLiteral("widths"), mm);
    return m;
}

//  Gemeinsamer Rahmen aller Struktur-Operationen: der Undo-Bereich ist die
//  GANZE Tabelle (Blöcke) plus ihr Gerüst-Schnappschuss.
bool DocxEditController::tableStructOp(int tableId,
                                       const std::function<bool()>& op) {
    if (!m_ready) return false;
    const int first = m_doc.tableFirstBlock(tableId);
    const int last  = m_doc.tableLastBlock(tableId);
    if (first < 0 || last < first) return false;

    EditScope scope(this, first, last - first + 1);
    scope.watchTable(tableId);
    if (!op()) return false;                       // ohne commit = kein Kommando

    const int newLast = m_doc.tableLastBlock(tableId);
    const int newCount = (newLast < first) ? 0 : newLast - first + 1;
    //  Der Cursor kann in einem entfallenen Block gestanden haben.
    const int nb = m_doc.blocks.size();
    m_cursor.block = qBound(0, m_cursor.block, qMax(0, nb - 1));
    m_cursor.pos   = qBound(0, m_cursor.pos, blockLen(m_cursor.block));
    m_cursor.collapse();
    clearPending();
    scope.commit(newCount);
    return true;
}

void DocxEditController::tableInsertRow(int tableId, int atRow) {
    tableStructOp(tableId, [&] { return m_doc.tableInsertRow(tableId, atRow); });
}
void DocxEditController::tableDeleteRow(int tableId, int row) {
    tableStructOp(tableId, [&] { return m_doc.tableDeleteRow(tableId, row); });
}
void DocxEditController::tableInsertColumn(int tableId, int atCol) {
    tableStructOp(tableId, [&] { return m_doc.tableInsertColumn(tableId, atCol); });
}
void DocxEditController::tableDeleteColumn(int tableId, int col) {
    tableStructOp(tableId, [&] { return m_doc.tableDeleteColumn(tableId, col); });
}

void DocxEditController::tableSetColumnWidthsMm(int tableId,
                                                const QVariantList& mm) {
    QVector<int> tw;
    for (const QVariant& v : mm)
        tw.append(qRound(v.toDouble() * 56.6929));      // mm -> Twips
    tableStructOp(tableId, [&] { return m_doc.tableSetColumnWidths(tableId, tw); });
}

void DocxEditController::scaleTableWidths(int tableId, qreal factor) {
    if (!m_ready) return;
    factor = qBound(0.1, factor, 10.0);
    QVector<int> tw = m_doc.tableColumnWidths(tableId);
    if (tw.isEmpty()) return;
    for (int& w : tw) w = qBound(200, int(qRound(w * factor)), 100000);
    tableStructOp(tableId, [&] { return m_doc.tableSetColumnWidths(tableId, tw); });
}

void DocxEditController::deleteTable(int tableId) {
    if (!m_ready) return;
    const int first = m_doc.tableFirstBlock(tableId);
    const int last  = m_doc.tableLastBlock(tableId);
    if (first < 0 || last < first) return;

    //  Die TableDef bleibt stehen - ohne Blöcke ist sie inert (die Emission
    //  läuft über die Blöcke), und ein Undo benutzt sie unverändert wieder.
    EditScope scope(this, first, last - first + 1);
    for (int i = last; i >= first; --i)
        m_doc.blocks.removeAt(i);
    const int nb = m_doc.blocks.size();
    m_cursor.block = qBound(0, first, qMax(0, nb - 1));
    m_cursor.pos   = 0;
    m_cursor.collapse();
    clearPending();
    scope.commit(0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bildgröße (A2)
// ─────────────────────────────────────────────────────────────────────────────
//  Dieselbe Regel wie in der Anzeige (DocxTextArea::updateImageSelection):
//  entweder der Absatz IST das Bild, oder die Selektion deckt genau sein
//  Objekt-Zeichen. Beide Seiten müssen sich einig sein - sonst zeigte die
//  Fläche Ziehpunkte an einem Bild, das der Controller nicht kennt.
bool DocxEditController::imageAtCursor(int* block, int* run,
                                       Docx::InlineImage* info) const {
    if (!m_ready || m_doc.blocks.isEmpty()) return false;
    const int bi = qBound(0, m_cursor.block, m_doc.blocks.size() - 1);
    const Docx::Block& b = m_doc.blocks.at(bi);
    const QVector<Docx::InlineImage> imgs = m_doc.paragraphImages(b);
    if (imgs.isEmpty()) return false;

    int k = -1;
    if (b.textLength() == 1 && imgs.size() == 1) {
        k = 0;                                    // der Absatz IST das Bild
    } else if (m_cursor.aBlock == m_cursor.block
               && qAbs(m_cursor.pos - m_cursor.aPos) == 1) {
        const int p = qMin(m_cursor.pos, m_cursor.aPos);
        for (int i = 0; i < imgs.size(); ++i)
            if (imgs.at(i).pos == p) { k = i; break; }
    }
    if (k < 0) return false;
    if (block) *block = bi;
    if (run)   *run   = imgs.at(k).run;
    if (info)  *info  = imgs.at(k);
    return true;
}

QVariantMap DocxEditController::imageInfoAt(int block) const {
    QVariantMap m;
    m.insert(QStringLiteral("image"), false);
    if (!m_ready || m_doc.blocks.isEmpty()) return m;
    const int bi = qBound(0, block < 0 ? m_cursor.block : block,
                          m_doc.blocks.size() - 1);
    Docx::InlineImage info;
    int cb = -1, run = -1;
    //  Am Cursor gilt die Auswahl-Regel (auch für ein Bild IM Text); ein
    //  fremder Block kann nur als reiner Bild-Absatz gemeint sein.
    if (!(imageAtCursor(&cb, &run, &info) && cb == bi)
        && !m_doc.paragraphImage(m_doc.blocks.at(bi), &info))
        return m;
    m.insert(QStringLiteral("image"), true);
    m.insert(QStringLiteral("block"), bi);
    m.insert(QStringLiteral("run"), info.run);
    //  Umbruchart - daran hängt der Eintrag im Kontextmenü.
    m.insert(QStringLiteral("floating"),
             info.anchored && info.wrap == Docx::InlineImage::WrapSquare);
    //  Umbruchseite und Lage: nur ein verankertes Bild hat beides.
    m.insert(QStringLiteral("wrapSide"), info.wrapSide);
    m.insert(QStringLiteral("xMm"), qRound(info.posXEmu / 36000.0 * 10.0) / 10.0);
    m.insert(QStringLiteral("yMm"), qRound(info.posYEmu / 36000.0 * 10.0) / 10.0);
    //  Größe in Millimetern (1 mm = 36000 EMU).
    m.insert(QStringLiteral("widthMm"),  qRound(info.cxEmu / 36000.0 * 10.0) / 10.0);
    m.insert(QStringLiteral("heightMm"), qRound(info.cyEmu / 36000.0 * 10.0) / 10.0);
    return m;
}

void DocxEditController::setImageSizeMm(int block, qreal widthMm, qreal heightMm) {
    if (!m_ready || m_doc.blocks.isEmpty()) return;
    const int bi = qBound(0, block < 0 ? m_cursor.block : block,
                          m_doc.blocks.size() - 1);
    const qint64 cx = qint64(qRound(qBound(1.0, widthMm,  5000.0) * 36000.0));
    const qint64 cy = qint64(qRound(qBound(1.0, heightMm, 5000.0) * 36000.0));

    Docx::InlineImage info;
    int cb = -1, run = -1;
    if (!(imageAtCursor(&cb, &run, &info) && cb == bi)) {
        if (!m_doc.paragraphImage(m_doc.blocks.at(bi), &info)) return;
        run = info.run;
    }

    EditScope scope(this, bi, 1);
    if (!m_doc.setImageSizeEmu(bi, run, cx, cy)) return;
    clearPending();
    scope.commit(1);
}

//  Umbruchart umstellen - dieselbe Auswahl-Regel wie Größe/Kopieren/Löschen.
void DocxEditController::setImageFloating(int block, bool floating) {
    if (!m_ready || m_doc.blocks.isEmpty()) return;
    const int bi = qBound(0, block < 0 ? m_cursor.block : block,
                          m_doc.blocks.size() - 1);
    Docx::InlineImage info;
    int cb = -1, run = -1;
    if (!(imageAtCursor(&cb, &run, &info) && cb == bi)) {
        if (!m_doc.paragraphImage(m_doc.blocks.at(bi), &info)) return;
        run = info.run;
    }

    EditScope scope(this, bi, 1);
    if (!m_doc.setImageWrap(bi, run, floating)) return;   // kein Kommando
    clearPending();
    scope.commit(1);
}

//  Lage eines VERANKERTEN Bildes (Ziehen mit der Maus). Millimeter, weil die
//  Anzeige nicht in OOXML-Einheiten rechnet - dieselbe Konvention wie
//  `setImageSizeMm`. Ein Bild in der Zeile hat keine Lage und lehnt ab.
void DocxEditController::setImagePositionMm(int block, qreal xMm, qreal yMm) {
    int bi = -1, run = -1;
    if (!selectedImage(block, &bi, &run)) return;
    const int px = int(qRound(qBound(-5000.0, xMm, 5000.0) * 36000.0));
    const int py = int(qRound(qBound(-5000.0, yMm, 5000.0) * 36000.0));

    EditScope scope(this, bi, 1);
    if (!m_doc.setImageAnchorEmu(bi, run, px, py)) return;   // kein Kommando
    clearPending();
    scope.commit(1);
}

//  Bild an einen ANDEREN Absatz hängen und dort ablegen - EIN Undo-Schritt über
//  beide Absätze (der Quellabsatz verliert den Run, der Zielabsatz bekommt ihn).
void DocxEditController::moveImageToBlock(int srcBlock, int dstBlock,
                                          qreal xMm, qreal yMm) {
    int bi = -1, run = -1;
    if (!selectedImage(srcBlock, &bi, &run)) return;
    if (dstBlock < 0 || dstBlock >= m_doc.blocks.size() || dstBlock == bi) {
        setImagePositionMm(srcBlock, xMm, yMm);      // kein Wechsel nötig
        return;
    }
    const int px = int(qRound(qBound(-5000.0, xMm, 5000.0) * 36000.0));
    const int py = int(qRound(qBound(-5000.0, yMm, 5000.0) * 36000.0));

    const int first = qMin(bi, dstBlock);
    const int count = qAbs(dstBlock - bi) + 1;
    EditScope scope(this, first, count);
    const int newRun = m_doc.moveImageRun(bi, run, dstBlock);
    if (newRun < 0) return;                          // kein Kommando
    m_doc.setImageAnchorEmu(dstBlock, newRun, px, py);
    //  Auswahl dem Bild nachziehen: sie hängt am Objekt-Zeichen, das jetzt im
    //  Zielabsatz steht - sonst zeigten die Ziehpunkte auf die alte Stelle.
    Docx::InlineImage moved;
    if (m_doc.imageOfRun(dstBlock, newRun, &moved)) {
        m_cursor.block = m_cursor.aBlock = dstBlock;
        m_cursor.pos   = moved.pos;
        m_cursor.aPos  = moved.pos + 1;
    }
    clearPending();
    scope.commit(count);
}

//  Umbruchseite eines verankerten Bildes (`Docx::InlineImage::WrapSide`).
void DocxEditController::setImageWrapSide(int block, int side) {
    int bi = -1, run = -1;
    if (!selectedImage(block, &bi, &run)) return;

    EditScope scope(this, bi, 1);
    if (!m_doc.setImageWrapSide(bi, run, side)) return;      // kein Kommando
    clearPending();
    scope.commit(1);
}

//  Die EINE Auswahl-Regel der Bild-Kommandos: erst der Cursor (auch für ein
//  Bild IM Text), sonst „der Absatz IST das Bild".
bool DocxEditController::selectedImage(int block, int* blockOut, int* runOut) const {
    if (!m_ready || m_doc.blocks.isEmpty()) return false;
    const int bi = qBound(0, block < 0 ? m_cursor.block : block,
                          m_doc.blocks.size() - 1);
    Docx::InlineImage info;
    int cb = -1, run = -1;
    if (!(imageAtCursor(&cb, &run, &info) && cb == bi)) {
        if (!m_doc.paragraphImage(m_doc.blocks.at(bi), &info)) return false;
        run = info.run;
    }
    *blockOut = bi;
    *runOut   = run;
    return true;
}

void DocxEditController::toggleList(bool bullet) {
    if (!m_ready) return;
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    Q_UNUSED(p1) Q_UNUSED(p2)
    //  Zustand: sind ALLE selektierten Absätze bereits eine Liste dieses Typs?
    bool all = true, any = false;
    for (int i = b1; i <= b2; ++i) {
        if (!isEditableParagraph(i)) continue;
        any = true;
        const ParFmt pf = m_doc.resolvePar(m_doc.blocks.at(i));
        const bool isType = pf.numId > 0
            && (m_doc.numLevel(pf.numId, pf.ilvl).numFmt == QLatin1String("bullet")) == bullet;
        all = all && isType;
    }
    if (!any) return;
    if (all) {
        //  Ausschalten: numPr entfernen.
        applyParProp(QStringLiteral("w:numPr"), QString(),
                     [](ParFmt& p) { p.numId = -1; p.set &= ~ParFmt::FNum; });
        return;
    }
    //  Einschalten: numId des Vorgänger-Absatzes fortführen (gleicher Typ),
    //  sonst neue Listen-Instanz anlegen (Word-Verhalten).
    int numId = -1;
    if (b1 > 0 && isEditableParagraph(b1 - 1)) {
        const ParFmt prev = m_doc.resolvePar(m_doc.blocks.at(b1 - 1));
        if (prev.numId > 0
            && (m_doc.numLevel(prev.numId, 0).numFmt == QLatin1String("bullet")) == bullet)
            numId = prev.numId;
    }
    if (numId < 0)
        numId = m_doc.newListNum(bullet);
    applyParProp(QStringLiteral("w:numPr"),
                 QStringLiteral("<w:numPr><w:ilvl w:val=\"0\"/>"
                                "<w:numId w:val=\"%1\"/></w:numPr>").arg(numId),
                 [numId](ParFmt& p) { p.numId = numId; p.ilvl = 0; p.set |= ParFmt::FNum; });
}
void DocxEditController::toggleBullets()   { toggleList(true); }
void DocxEditController::toggleNumbering() { toggleList(false); }

// ─────────────────────────────────────────────────────────────────────────────
//  Zwischenablage / Undo
// ─────────────────────────────────────────────────────────────────────────────
//  Eigener Zwischenablage-Typ: trägt die Runs MIT ihrem rPr-Fragment, damit
//  Kopieren/Einfügen innerhalb der App (und zwischen zwei DOCX-Kacheln)
//  Schriftgröße/-art/Stil/Farbe verlustfrei erhält. Reiner Text (setText)
//  konnte das nicht - daher der Nutzerbefund „Kopieren verliert Formatierung".
static const char* const kDocxMime = "application/x-mediagallery-docx-runs";
static constexpr quint32 kClipMagic   = 0x4D474458u;   // "MGDX"
static constexpr quint16 kClipVersion = 1;

//  Eigener Typ fürs BILD: die Pixel allein genügen nicht - aus ihnen ließe sich
//  beim Einfügen nur die native Auflösung zurückrechnen, das Bild käme also in
//  voller Größe statt in der, die es im Dokument hatte. Hier stehen zusätzlich
//  die EMU-Maße der Quelle. Das Bild liegt PARALLEL weiter als Pixel in der
//  Zwischenablage, damit fremde Programme es nach wie vor annehmen.
static const char* const kDocxImgMime = "application/x-mediagallery-docx-image";
static constexpr quint32 kImgMagic    = 0x4D474449u;   // "MGDI"
static constexpr quint16 kImgVersion  = 1;

//  Eigener Typ für die TABELLE: sie ist Blockgruppe UND `TableDef`. Übertragen
//  wird deshalb kein XML, sondern die REINE FORM (Zeilen, Spalten, Breiten,
//  Zellinhalt) - in einem fremden Dokument gelten die Spans der Quelle nicht,
//  das Gerüst muss dort ohnehin neu gebaut werden (über insertTable).
static const char* const kDocxTblMime = "application/x-mediagallery-docx-table";
static constexpr quint32 kTblMagic    = 0x4D474454u;   // "MGDT"
static constexpr quint16 kTblVersion  = 1;

//  Run-Kodierung an EINER Stelle: Auswahl und Tabelle schreiben dieselben
//  Felder in derselben Reihenfolge, sonst liefen die beiden Formate auseinander.
static void writeRuns(QDataStream& ds, const QList<Run>& runs) {
    ds << quint32(runs.size());
    for (const Run& r : runs) {
        ds << r.rprXml << r.text << qint32(r.fmt.set)
           << r.fmt.bold << r.fmt.italic << r.fmt.underline
           << double(r.fmt.sizePt) << r.fmt.font << r.fmt.color;
    }
}
static bool readRuns(QDataStream& ds, QList<Run>* out) {
    quint32 runCount = 0;
    ds >> runCount;
    if (ds.status() != QDataStream::Ok || runCount > 1000000u)
        return false;
    out->clear();
    out->reserve(int(runCount));
    for (quint32 k = 0; k < runCount; ++k) {
        Run r;
        qint32 set = 0; double sz = 11.0;
        ds >> r.rprXml >> r.text >> set
           >> r.fmt.bold >> r.fmt.italic >> r.fmt.underline
           >> sz >> r.fmt.font >> r.fmt.color;
        if (ds.status() != QDataStream::Ok)
            return false;
        r.fmt.set    = int(set);
        r.fmt.sizePt = sz;
        //  Frische Runs OHNE Herkunfts-Spans: das rPr-Fragment ist bereits
        //  materialisiert, der Text wird neu serialisiert.
        r.rprMaterialized = true;
        r.dirty = true;
        out->append(r);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Selektion -> Blob. Kopiert werden AUSSCHLIESSLICH normale Runs; opake
//  (Zeichnung/Feld/Hyperlink-Konstrukt) hängen an ihrem Original-XML im
//  Quelldokument und dürfen nicht in ein anderes verpflanzt werden - sie
//  entfallen wie bisher schon in der Klartext-Fassung. Die Sentinels
//  U+FFFC/U+E000 werden mitentfernt (sonst wanderten Platzhalter mit).
// ─────────────────────────────────────────────────────────────────────────────
QByteArray DocxEditController::serializeSelection() const {
    QByteArray blob;
    QDataStream ds(&blob, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_6_0);
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);

    QList<QList<Run>> paras;
    for (int i = b1; i <= b2; ++i) {
        QList<Run> out;
        if (isEditableParagraph(i)) {
            const Block& blk = m_doc.blocks.at(i);
            const int from = (i == b1) ? p1 : 0;
            const int to   = (i == b2) ? p2 : blk.textLength();
            int acc = 0;
            for (const Run& r : blk.runs) {
                const int rs = acc, re = acc + r.text.size();
                acc = re;
                if (r.opaque || r.text.isEmpty()) continue;
                if (re <= from || rs >= to) continue;
                Run c;
                c.text = r.text.mid(qMax(0, from - rs),
                                    qMin(re, to) - qMax(rs, from));
                c.text.remove(kObjectChar);
                c.text.remove(kPageBreak);
                if (c.text.isEmpty()) continue;
                c.rprXml = r.currentRpr(m_doc.docXml());
                c.fmt    = r.fmt;
                out.append(c);
            }
        }
        paras.append(out);
    }

    ds << kClipMagic << kClipVersion << quint32(paras.size());
    for (const QList<Run>& p : paras)
        writeRuns(ds, p);
    return blob;
}

bool DocxEditController::deserializeRuns(const QByteArray& blob,
                                         QList<QList<Run>>* out) {
    if (!out) return false;
    QDataStream ds(blob);
    ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0; quint16 ver = 0; quint32 paraCount = 0;
    ds >> magic >> ver >> paraCount;
    if (ds.status() != QDataStream::Ok || magic != kClipMagic || ver != kClipVersion)
        return false;
    if (paraCount == 0 || paraCount > 100000u)
        return false;
    out->clear();
    out->reserve(int(paraCount));
    for (quint32 i = 0; i < paraCount; ++i) {
        QList<Run> runs;
        if (!readRuns(ds, &runs))
            return false;
        out->append(runs);
    }
    return !out->isEmpty();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Selektion als HTML-Fragment - damit auch Word/LibreOffice/Browser die
//  Formatierung übernehmen (die interne MIME-Form kennen sie nicht).
// ─────────────────────────────────────────────────────────────────────────────
QString DocxEditController::selectionAsHtml() const {
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    static const char* const kAlign[] = { "left", "center", "right", "justify" };
    QString html = QStringLiteral("<!--StartFragment--><div>");
    for (int i = b1; i <= b2; ++i) {
        if (!isEditableParagraph(i)) continue;
        const Block& blk = m_doc.blocks.at(i);
        const ParFmt pf  = m_doc.resolvePar(blk);
        html += QStringLiteral("<p style=\"margin:0;text-align:%1\">")
                    .arg(QLatin1String(kAlign[qBound(0, pf.align, 3)]));
        const int from = (i == b1) ? p1 : 0;
        const int to   = (i == b2) ? p2 : blk.textLength();
        int acc = 0;
        bool any = false;
        for (const Run& r : blk.runs) {
            const int rs = acc, re = acc + r.text.size();
            acc = re;
            if (r.opaque || r.text.isEmpty()) continue;
            if (re <= from || rs >= to) continue;
            QString t = r.text.mid(qMax(0, from - rs), qMin(re, to) - qMax(rs, from));
            t.remove(kObjectChar);
            t.remove(kPageBreak);
            if (t.isEmpty()) continue;
            const RunFmt f = m_doc.resolveRun(blk, r);
            QString style = QStringLiteral("font-family:'%1';font-size:%2pt;")
                                .arg(Document::xmlEscape(f.font))
                                .arg(f.sizePt, 0, 'f', 1);
            if (f.bold)      style += QStringLiteral("font-weight:bold;");
            if (f.italic)    style += QStringLiteral("font-style:italic;");
            if (f.underline) style += QStringLiteral("text-decoration:underline;");
            if (f.color.isValid())
                style += QStringLiteral("color:%1;").arg(f.color.name(QColor::HexRgb));
            html += QStringLiteral("<span style=\"%1\">%2</span>")
                        .arg(style,
                             Document::xmlEscape(t)
                                 .replace(QLatin1Char('\t'), QLatin1String("&#9;"))
                                 .replace(kLineBreak, QLatin1String("<br/>")));
            any = true;
        }
        if (!any) html += QStringLiteral("<br/>");
        html += QStringLiteral("</p>");
    }
    html += QStringLiteral("</div><!--EndFragment-->");
    return html;
}

//  Bild in die Zwischenablage: als BILD, nicht als Modell-Ausschnitt. Damit
//  landet es auch in anderen Programmen, und `paste()` fügt es über die
//  bestehende Bild-Kette wieder ein - die Beziehung (rId) wandert also nicht
//  mit, was sie in einem FREMDEN Dokument ohnehin nicht dürfte. Preis: die
//  Pixel werden neu als PNG kodiert, das Original bleibt aber unangetastet
//  (kopiert wird ja nur).
bool DocxEditController::copyImageAtCursor() {
    Docx::InlineImage info;
    int bi = -1, run = -1;
    if (!imageAtCursor(&bi, &run, &info)) return false;
    const QByteArray bytes = m_doc.imageBytes(info.relId);
    QImage img;
    if (bytes.isEmpty() || !img.loadFromData(bytes) || img.isNull()) return false;

    //  Beide Darstellungen in EINEM QMimeData: eigener Typ mit ORIGINALBYTES
    //  und Anzeigegröße (verlustfrei innerhalb der App), Pixel für alles andere.
    QByteArray blob;
    {
        QDataStream ds(&blob, QIODevice::WriteOnly);
        ds.setVersion(QDataStream::Qt_6_0);
        ds << kImgMagic << kImgVersion
           << QFileInfo(m_doc.relTarget(info.relId)).suffix().toLower()
           << qint64(info.cxEmu) << qint64(info.cyEmu) << bytes;
    }
    auto* md = new QMimeData;
    md->setData(QLatin1String(kDocxImgMime), blob);
    md->setImageData(img);
    QGuiApplication::clipboard()->setMimeData(md);
    return true;
}

void DocxEditController::deleteImageAtCursor() {
    Docx::InlineImage info;
    int bi = -1, run = -1;
    if (!imageAtCursor(&bi, &run, &info)) return;

    //  Bild IM Fließtext: nur sein Run geht weg, der Absatz bleibt stehen.
    if (m_doc.blocks.at(bi).textLength() > 1) {
        EditScope scope(this, bi, 1);
        Docx::Block& b = m_doc.blocks[bi];
        if (run < 0 || run >= b.runs.size()) return;
        b.runs.removeAt(run);
        b.dirty = true;
        m_cursor = { bi, info.pos, bi, info.pos };
        clearPending();
        scope.commit(1);
        return;
    }
    //  Absatz IST das Bild -> der Absatz geht mit.
    EditScope scope(this, bi, 1);
    m_doc.blocks.removeAt(bi);
    const int nb = m_doc.blocks.size();
    m_cursor.block = qBound(0, bi > 0 ? bi - 1 : 0, qMax(0, nb - 1));
    m_cursor.pos   = 0;
    m_cursor.collapse();
    clearPending();
    scope.commit(0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tabelle in die Zwischenablage (R6). Kopiert wird die FORM, nicht das XML:
//  Zeilen/Spalten/Breiten + je Zelle ihre Absätze. Im Zieldokument baut
//  `insertTable` daraus ein frisches Gerüst - die Spans der Quelle gelten dort
//  ohnehin nicht, und ein fremdes `w:tbl` verbatim einzusetzen hieße, Verweise
//  auf Vorlagen und Nummerierungen des Quelldokuments mitzuschleppen.
// ─────────────────────────────────────────────────────────────────────────────
bool DocxEditController::copyTableAtCursor() {
    if (!m_ready || m_doc.blocks.isEmpty()) return false;
    const int bi  = qBound(0, m_cursor.block, m_doc.blocks.size() - 1);
    const int tid = m_doc.blocks.at(bi).tableId;
    if (tid < 0) return false;
    //  Verbundene Zellen/ungleiche Zeilen: kein Teilverständnis (wie
    //  tableStructEditable es für jede Struktur-Operation verlangt).
    if (!m_doc.tableStructEditable(tid)) return false;

    const int rows = m_doc.tableRowCount(tid);
    const int cols = m_doc.tableColumnCount(tid);
    if (rows <= 0 || cols <= 0) return false;
    const QVector<int> widths = m_doc.tableColumnWidths(tid);

    //  Zellinhalt in Lesereihenfolge einsammeln; eine Zelle darf mehrere
    //  Absätze tragen.
    QList<QList<QList<Run>>> cells;
    cells.resize(rows * cols);
    QStringList plainRows;
    QStringList plainCells;
    const int first = m_doc.tableFirstBlock(tid);
    const int last  = m_doc.tableLastBlock(tid);
    int lastRow = -1;
    for (int i = first; i >= 0 && i <= last && i < m_doc.blocks.size(); ++i) {
        const Block& blk = m_doc.blocks.at(i);
        if (blk.tableId != tid || blk.kind != Block::Paragraph) continue;
        if (blk.row < 0 || blk.row >= rows || blk.col < 0 || blk.col >= cols) continue;
        QList<Run> runs;
        for (const Run& r : blk.runs) {
            if (r.opaque || r.text.isEmpty()) continue;
            Run c;
            c.text = r.text;
            c.text.remove(kObjectChar);
            c.text.remove(kPageBreak);
            if (c.text.isEmpty()) continue;
            c.rprXml = r.currentRpr(m_doc.docXml());
            c.fmt    = r.fmt;
            runs.append(c);
        }
        cells[blk.row * cols + blk.col].append(runs);

        //  Klartext-Fassung für fremde Programme: Zellen mit Tab, Zeilen mit \n.
        if (blk.row != lastRow) {
            if (lastRow >= 0) plainRows << plainCells.join(QLatin1Char('\t'));
            plainCells.clear();
            lastRow = blk.row;
        }
        QString t = blk.plainText();
        t.remove(kObjectChar);
        t.replace(kPageBreak, QLatin1Char(' '));
        t.replace(kLineBreak, QLatin1Char(' '));
        if (plainCells.size() == blk.col) plainCells << t;
        else if (blk.col < plainCells.size()) plainCells[blk.col] += QLatin1Char(' ') + t;
    }
    if (lastRow >= 0) plainRows << plainCells.join(QLatin1Char('\t'));

    QByteArray blob;
    {
        QDataStream ds(&blob, QIODevice::WriteOnly);
        ds.setVersion(QDataStream::Qt_6_0);
        ds << kTblMagic << kTblVersion << qint32(rows) << qint32(cols);
        ds << quint32(widths.size());
        for (int w : widths) ds << qint32(w);
        for (const QList<QList<Run>>& cell : cells) {
            ds << quint32(cell.size());
            for (const QList<Run>& para : cell)
                writeRuns(ds, para);
        }
    }

    auto* md = new QMimeData;
    md->setData(QLatin1String(kDocxTblMime), blob);
    md->setText(plainRows.join(QLatin1Char('\n')));
    QGuiApplication::clipboard()->setMimeData(md);
    return true;
}

bool DocxEditController::clipboardHasTable() const {
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    return md && md->hasFormat(QLatin1String(kDocxTblMime));
}

//  Tabelle aus dem eigenen Zwischenablage-Typ einfügen - Gerüst über
//  insertTable(), danach die Zellen füllen. EIN Undo-Schritt.
bool DocxEditController::pasteTableBlob(const QByteArray& blob) {
    if (!m_ready) return false;
    QDataStream ds(blob);
    ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0; quint16 ver = 0;
    qint32 rows = 0, cols = 0;
    ds >> magic >> ver >> rows >> cols;
    if (ds.status() != QDataStream::Ok || magic != kTblMagic || ver != kTblVersion)
        return false;
    if (rows < 1 || rows > 100 || cols < 1 || cols > 32)
        return false;

    quint32 widthCount = 0;
    ds >> widthCount;
    if (ds.status() != QDataStream::Ok || widthCount > 32u) return false;
    QVector<int> widths;
    widths.reserve(int(widthCount));
    for (quint32 i = 0; i < widthCount; ++i) {
        qint32 w = 0;
        ds >> w;
        if (ds.status() != QDataStream::Ok) return false;
        widths.append(qBound(200, int(w), 100000));
    }

    QList<QList<QList<Run>>> cells;
    cells.reserve(rows * cols);
    for (int i = 0; i < rows * cols; ++i) {
        quint32 paraCount = 0;
        ds >> paraCount;
        if (ds.status() != QDataStream::Ok || paraCount > 10000u) return false;
        QList<QList<Run>> cell;
        cell.reserve(int(paraCount));
        for (quint32 p = 0; p < paraCount; ++p) {
            QList<Run> runs;
            if (!readRuns(ds, &runs)) return false;
            cell.append(runs);
        }
        cells.append(cell);
    }

    //  Einfügestelle wie bei insertTable(): hinter dem Cursor-Absatz, bzw.
    //  hinter der GANZEN Tabelle, wenn der Cursor in einer Zelle steht.
    int at = qBound(0, m_cursor.block, qMax(0, m_doc.blocks.size() - 1)) + 1;
    if (!m_doc.blocks.isEmpty()) {
        const Block& cur = m_doc.blocks.at(qBound(0, m_cursor.block,
                                                  m_doc.blocks.size() - 1));
        if (cur.tableId >= 0) {
            const int lastB = m_doc.tableLastBlock(cur.tableId);
            if (lastB >= 0) at = lastB + 1;
        }
    }
    at = qBound(0, at, m_doc.blocks.size());

    EditScope scope(this, at, 0);
    const int first = m_doc.insertTable(at, rows, cols);
    if (first < 0) return false;
    const int tid = m_doc.blocks.at(first).tableId;
    if (widths.size() == cols)
        m_doc.tableSetColumnWidths(tid, widths);

    //  Zellen füllen. Der erste Absatz einer Zelle ist schon da (leer); jeder
    //  weitere kommt als zusätzlicher Block MIT denselben Zellkoordinaten
    //  dahinter - emitBlocks() legt alle Blöcke einer Zelle in dasselbe w:tc.
    int inserted = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const QList<QList<Run>>& cell = cells.at(r * cols + c);
            if (cell.isEmpty()) continue;
            const int base = first + inserted + r * cols + c;
            if (base < 0 || base >= m_doc.blocks.size()) continue;
            Block& target = m_doc.blocks[base];
            target.runs  = cell.constFirst();
            target.dirty = true;
            for (int p = 1; p < cell.size(); ++p) {
                Block extra;
                extra.kind    = Block::Paragraph;
                extra.tableId = target.tableId;
                extra.row     = target.row;
                extra.col     = target.col;
                extra.runs    = cell.at(p);
                extra.dirty   = true;      // ohne Span -> buildParagraphXml
                m_doc.blocks.insert(base + p, extra);
                ++inserted;
            }
        }
    }

    m_cursor = { first, 0, first, 0 };
    clearPending();
    scope.commit(rows * cols + inserted);
    return true;
}

void DocxEditController::copy() {
    //  Ein ausgewähltes Bild kopiert seine ORIGINALBYTES samt Anzeigegröße
    //  (eigener Ablage-Typ) - auch wenn die Auswahl technisch genau sein
    //  Objekt-Zeichen ist, wie bei einem Bild im Fließtext.
    if (copyImageAtCursor()) return;
    //  Kein Text markiert und der Cursor steht in einer Tabelle -> die GANZE
    //  Tabelle kopieren. Dieselbe Konvention wie beim Bild: der Cursor IST die
    //  Auswahl (s. DocxTextArea::updateImageSelection, selTableId).
    if (!m_cursor.hasSelection() && copyTableAtCursor()) return;
    if (!m_cursor.hasSelection()) return;
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    QString out;
    for (int i = b1; i <= b2; ++i) {
        const QString t = blockText(i);
        const int from = (i == b1) ? p1 : 0;
        const int to   = (i == b2) ? p2 : t.size();
        out += t.mid(from, to - from);
        if (i < b2) out += QLatin1Char('\n');
    }
    out.remove(kObjectChar);
    out.replace(kPageBreak, QLatin1Char('\n'));
    //  DREI Darstellungen in EINEM QMimeData: intern verlustfrei (eigener Typ),
    //  HTML für Fremdprogramme, Klartext als kleinster gemeinsamer Nenner.
    //  Das QMimeData geht in den Besitz der Zwischenablage über.
    auto* md = new QMimeData;
    md->setText(out);
    md->setHtml(selectionAsHtml());
    md->setData(QLatin1String(kDocxMime), serializeSelection());
    QGuiApplication::clipboard()->setMimeData(md);
}
void DocxEditController::cut() {
    if (copyImageAtCursor()) { deleteImageAtCursor(); return; }
    if (!m_cursor.hasSelection()) {
        //  Tabelle ausschneiden = kopieren + löschen. Zusammen mit dem
        //  Einfügen ist das zugleich das VERSCHIEBEN einer Tabelle.
        if (m_doc.blocks.isEmpty()) return;
        const int tid = m_doc.blocks.at(qBound(0, m_cursor.block,
                                               m_doc.blocks.size() - 1)).tableId;
        if (tid >= 0 && copyTableAtCursor()) deleteTable(tid);
        return;
    }
    copy();
    deleteBackward();
}
void DocxEditController::paste() {
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    if (!md) return;
    //  Ganze TABELLE zuerst - sie trägt zusätzlich einen Klartext-Fallback,
    //  der sonst gewönne und die Tabelle als Tabulatorzeilen einfügte.
    if (md->hasFormat(QLatin1String(kDocxTblMime))
        && pasteTableBlob(md->data(QLatin1String(kDocxTblMime))))
        return;
    //  Eigener Typ zuerst: Runs samt rPr -> Formatierung bleibt erhalten.
    if (md->hasFormat(QLatin1String(kDocxMime))) {
        QList<QList<Run>> paras;
        if (deserializeRuns(md->data(QLatin1String(kDocxMime)), &paras)) {
            insertRunParagraphs(paras);
            return;
        }
    }
    //  Eigener BILD-Typ: Originalbytes samt Anzeigegröße der Quelle - das Bild
    //  kommt in der Größe zurück, die es im Dokument hatte, und ohne
    //  Umkodierung über PNG.
    if (md->hasFormat(QLatin1String(kDocxImgMime))) {
        QDataStream ds(md->data(QLatin1String(kDocxImgMime)));
        ds.setVersion(QDataStream::Qt_6_0);
        quint32 magic = 0; quint16 ver = 0;
        QString ext; qint64 cx = 0, cy = 0; QByteArray bytes;
        ds >> magic >> ver;
        if (magic == kImgMagic && ver == kImgVersion) {
            ds >> ext >> cx >> cy >> bytes;
            if (ds.status() == QDataStream::Ok && !bytes.isEmpty()) {
                insertImageData(bytes, ext, cx, cy);
                return;
            }
        }
    }
    //  BILD aus der Zwischenablage (Bildschirmfoto, Browser, Bildbetrachter):
    //  vor dem Klartext prüfen - viele Quellen legen zusätzlich einen
    //  Dateinamen als Text ab, der sonst gewönne. PNG behält Transparenz.
    if (md->hasImage()) {
        const QImage img = qvariant_cast<QImage>(md->imageData());
        if (!img.isNull()) {
            QByteArray bytes;
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            if (img.save(&buf, "PNG")) {
                buf.close();
                insertImageData(bytes, QStringLiteral("png"));
                return;
            }
        }
    }
    //  Fremdinhalt: wie bisher als Klartext (HTML-Import ist bewusst NICHT
    //  Teil dieses Editors - s. README „Planned").
    const QString t = md->text();
    if (!t.isEmpty()) insertText(t);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Einfügen fertiger Runs (Zwischenablage) - Ablauf identisch zu insertText,
//  nur dass die Runs ihr Format MITBRINGEN (kein Erben vom Nachbarn, kein
//  Pending). Absatz-Eigenschaften (pPr) übernimmt der Zielabsatz - genau wie
//  beim Tippen von Enter; damit können keine Listen-/Stil-Verweise (numId,
//  pStyle) aus einem fremden Dokument ins Ziel lecken.
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::insertRunParagraphs(const QList<QList<Run>>& paras) {
    if (!m_ready || paras.isEmpty()) return;
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    if (!isEditableParagraph(b1) || !isEditableParagraph(b2))
        return;

    EditScope scope(this, b1, b2 - b1 + 1);

    //  1) Selektion entfernen: Bereich zu EINEM Block verschmelzen.
    Block& head = m_doc.blocks[b1];
    if (m_cursor.hasSelection()) {
        if (b1 == b2) {
            removeRangeInBlock(head, p1, p2);
        } else {
            removeRangeInBlock(head, p1, blockLen(b1));
            Block& tail = m_doc.blocks[b2];
            removeRangeInBlock(tail, 0, p2);
            const int k = ensureRunBoundary(head, p1);
            Q_UNUSED(k)
            head.runs += tail.runs;
            head.dirty = true;
            for (int i = b2; i > b1; --i)
                m_doc.blocks.removeAt(i);
        }
    }

    //  2) Absätze einfügen.
    int curBlock = b1, curPos = p1;
    {
        Block& blk = m_doc.blocks[curBlock];
        int at = ensureRunBoundary(blk, curPos);
        for (const Run& r : paras.at(0)) {
            blk.runs.insert(at++, r);
            curPos += r.text.size();
        }
        blk.dirty = true;
    }
    for (int pi = 1; pi < paras.size(); ++pi) {
        //  Absatz-Split an curPos: Rest wandert in einen NEUEN Absatz.
        Block& blk = m_doc.blocks[curBlock];
        const int k = ensureRunBoundary(blk, curPos);
        Block nb;
        nb.kind = Block::Paragraph;
        const QString ppr = blk.currentPpr(m_doc.docXml());
        if (!ppr.isEmpty()) { nb.pprXml = ppr; nb.pprMaterialized = true; }
        nb.pfmt = blk.pfmt;
        while (blk.runs.size() > k)
            nb.runs.append(blk.runs.takeAt(k));
        blk.dirty = true;
        nb.dirty  = true;
        int at = 0;
        curPos = 0;
        for (const Run& r : paras.at(pi)) {
            nb.runs.insert(at++, r);
            curPos += r.text.size();
        }
        ++curBlock;
        m_doc.blocks.insert(curBlock, nb);
    }

    m_cursor = { curBlock, curPos, curBlock, curPos };
    clearPending();
    scope.commit(curBlock - b1 + 1);
}
void DocxEditController::undo() { if (m_stack.canUndo()) m_stack.undo(); }
void DocxEditController::redo() { if (m_stack.canRedo()) m_stack.redo(); }

// ─────────────────────────────────────────────────────────────────────────────
//  Suchen & Ersetzen
// ─────────────────────────────────────────────────────────────────────────────
QString DocxEditController::selectionPlainText() const {
    if (!m_cursor.hasSelection()) return {};
    int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
    if (b1 == b2)
        return blockText(b1).mid(p1, p2 - p1);
    QString out;
    for (int i = b1; i <= b2; ++i) {
        const QString t = blockText(i);
        const int s = (i == b1) ? p1 : 0;
        const int e = (i == b2) ? p2 : t.size();
        out += t.mid(s, e - s);
        if (i < b2) out += QLatin1Char('\n');
    }
    return out;
}

void DocxEditController::selectRange(int bi, int start, int len) {
    setCursor(bi, start, false);          // Anker + Position auf den Anfang
    setCursor(bi, start + len, true);     // Position ans Ende ziehen (Selektion)
}

QVariantMap DocxEditController::findNext(const QString& needle, bool caseSensitive,
                                         bool backward) {
    QVariantMap r;
    r.insert(QStringLiteral("found"), false);
    const int n = m_doc.blocks.size();
    if (!m_ready || needle.isEmpty() || n == 0)
        return r;
    const auto sens = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    //  Startgrenze: vorwärts ab Selektionsende, rückwärts ab Selektionsanfang.
    int sb, sp;
    { int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
      if (backward) { sb = b1; sp = p1; } else { sb = b2; sp = p2; } }

    //  EINMAL umlaufend durch alle Blöcke; der Wrap-Durchgang (k==n) landet
    //  wieder im Startblock und akzeptiert nur Treffer VOR der Startgrenze
    //  (die dahinter wurden bei k==0 bereits abgedeckt).
    for (int k = 0; k <= n; ++k) {
        const int bi = backward ? ((sb - k) % n + n) % n
                                : (sb + k) % n;
        if (!isEditableParagraph(bi))
            continue;
        const QString t = blockText(bi);
        int idx;
        if (!backward) {
            const int from = (k == 0) ? sp : 0;
            idx = t.indexOf(needle, from, sens);
            if (k == n) {                         // Wrap zurück in den Startblock
                if (idx >= 0 && idx < sp) { selectRange(bi, idx, needle.size());
                    r.insert(QStringLiteral("found"), true);
                    r.insert(QStringLiteral("wrapped"), true);
                    r.insert(QStringLiteral("block"), bi);
                    r.insert(QStringLiteral("pos"), idx); return r; }
                continue;
            }
            if (idx >= 0) { selectRange(bi, idx, needle.size());
                r.insert(QStringLiteral("found"), true);
                r.insert(QStringLiteral("wrapped"), false);
                r.insert(QStringLiteral("block"), bi);
                r.insert(QStringLiteral("pos"), idx); return r; }
        } else {
            const int fromEnd = (k == 0) ? sp - needle.size() : t.size();
            idx = t.lastIndexOf(needle, fromEnd, sens);
            if (k == n) {                         // Wrap zurück in den Startblock
                if (idx >= 0 && idx >= sp) { selectRange(bi, idx, needle.size());
                    r.insert(QStringLiteral("found"), true);
                    r.insert(QStringLiteral("wrapped"), true);
                    r.insert(QStringLiteral("block"), bi);
                    r.insert(QStringLiteral("pos"), idx); return r; }
                continue;
            }
            if (idx >= 0) { selectRange(bi, idx, needle.size());
                r.insert(QStringLiteral("found"), true);
                r.insert(QStringLiteral("wrapped"), false);
                r.insert(QStringLiteral("block"), bi);
                r.insert(QStringLiteral("pos"), idx); return r; }
        }
    }
    return r;
}

QVariantMap DocxEditController::replaceAndFind(const QString& needle,
                                               const QString& replacement,
                                               bool caseSensitive) {
    //  Suchen&Ersetzen ist absatzintern -> Ersatztext ohne Umbrüche (sonst
    //  spaltete insertText den Absatz und verschöbe die Trefferpositionen).
    QString rep = replacement; rep.remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
    if (m_ready && !needle.isEmpty() && m_cursor.hasSelection()) {
        const auto sens = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (selectionPlainText().compare(needle, sens) == 0) {
            //  Aktuelle Selektion IST der Treffer -> ersetzen (undo-fähig).
            if (rep.isEmpty()) deleteBackward();   // löscht die Selektion
            else               insertText(rep);
        }
    }
    return findNext(needle, caseSensitive, false);
}

int DocxEditController::replaceAll(const QString& needle, const QString& replacement,
                                   bool caseSensitive) {
    if (!m_ready || needle.isEmpty())
        return 0;
    //  Absatzintern -> Ersatztext ohne Umbrüche (s. replaceAndFind).
    QString rep = replacement; rep.remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
    const auto sens = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int count = 0;
    m_stack.beginMacro(QStringLiteral("Alle ersetzen"));
    //  Deterministisch von vorne: je Block alle Vorkommen, danach der nächste.
    //  Beim Ersetzen ändert sich nur der eigene Block (Absatzgrenzen bleiben,
    //  da needle/replacement absatzintern sind), daher Suche im selben Block
    //  ab dem Ende des Ersatzes fortsetzen - keine Endlosschleife (Deckel).
    const int hardCap = 5'000'000;
    for (int bi = 0; bi < m_doc.blocks.size(); ++bi) {
        if (!isEditableParagraph(bi))
            continue;
        int from = 0;
        while (true) {
            const QString t = blockText(bi);
            const int idx = t.indexOf(needle, from, sens);
            if (idx < 0) break;
            selectRange(bi, idx, needle.size());
            if (rep.isEmpty()) deleteBackward();
            else               insertText(rep);
            from = idx + rep.size();               // hinter dem Ersatz weitersuchen
            if (++count >= hardCap) break;
        }
        if (count >= hardCap) break;
    }
    m_stack.endMacro();
    if (count > 0)
        setModified(true);
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Live-Transliteration (Latein -> Arabisch/Kana) am Cursor-Absatz - Muster
//  TextSurface._applyTranslit, nur controllerseitig: liveApply liefert
//  {changed,start,end,replacement,cursor}; die Ersetzung koalesziert (merge-
//  Kind 0) mit dem auslösenden Tastendruck zu EINEM Undo-Schritt.
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::runTranslit() {
    if (!m_translit || m_cursor.hasSelection()) return;
    const int bi = m_cursor.block;
    if (!isEditableParagraph(bi)) return;
    QVariantMap res;
    const bool ok = QMetaObject::invokeMethod(
        m_translit, "liveApply", Qt::DirectConnection,
        Q_RETURN_ARG(QVariantMap, res),
        Q_ARG(QString, blockText(bi)), Q_ARG(int, m_cursor.pos));
    if (!ok || !res.value(QStringLiteral("changed")).toBool())
        return;
    const int start = res.value(QStringLiteral("start")).toInt();
    const int end   = res.value(QStringLiteral("end")).toInt();
    const QString repl = res.value(QStringLiteral("replacement")).toString();
    const int newCur   = res.value(QStringLiteral("cursor")).toInt();
    if (start < 0 || end < start || end > blockLen(bi))
        return;
    EditScope scope(this, bi, 1, 0);                    // koalesziert mit Tippen
    Block& blk = m_doc.blocks[bi];
    removeRangeInBlock(blk, start, end);
    if (!repl.isEmpty()) {
        const int at = ensureRunBoundary(blk, start);
        Run nr;
        int inherit = -1;
        for (int i = at - 1; i >= 0; --i)
            if (!blk.runs.at(i).opaque) { inherit = i; break; }
        if (inherit < 0)
            for (int i = at; i < blk.runs.size(); ++i)
                if (!blk.runs.at(i).opaque) { inherit = i; break; }
        if (inherit >= 0) {
            const Run& src = blk.runs.at(inherit);
            nr.rprSpan = src.rprSpan; nr.rprXml = src.rprXml;
            nr.rprMaterialized = src.rprMaterialized; nr.fmt = src.fmt;
        }
        nr.text = repl;
        nr.dirty = true;
        blk.runs.insert(at, nr);
        blk.dirty = true;
    }
    m_cursor = { bi, qBound(0, newCur, blockLen(bi)), bi, 0 };
    m_cursor.collapse();
    scope.commit(1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Toolbar-Format
// ─────────────────────────────────────────────────────────────────────────────
//  Aufgelöstes Zeichenformat EINER Stelle. Leerer Absatz (alle Runs gelöscht):
//  NICHT die docDefaults, sondern das Stil-Format des Absatzes selbst
//  (resolveRun mit leerem Run läuft die pStyle-Kette ab) - sonst zeigte die
//  Leiste in einer leergelöschten Überschrift-Zeile ein drittes, mit nichts
//  übereinstimmendes Format (docDefaults) an.
RunFmt DocxEditController::resolvedFormatAt(int block, int pos) const {
    if (!isEditableParagraph(block))
        return m_doc.defaultRun();
    const Block& blk = m_doc.blocks.at(block);
    int ri = -1, ro = 0;
    runAt(blk, pos, &ri, &ro);
    if (ri >= 0 && ri < blk.runs.size())
        return m_doc.resolveRun(blk, blk.runs.at(ri));
    return m_doc.resolveRun(blk, Run());
}

RunFmt DocxEditController::caretFormat() const {
    if (!m_ready || m_doc.blocks.isEmpty())
        return m_doc.defaultRun();
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    //  Format des Zeichens LINKS vom Cursor (Editor-Konvention).
    RunFmt rf = resolvedFormatAt(b1, m_cursor.hasSelection() ? p1 : qMax(0, p1 - 1));
    //  Pending-Format überlagert an der Pending-Stelle.
    if (m_pendingBlock == m_cursor.block && m_pendingPos == m_cursor.pos) {
        if (m_pending.set & RunFmt::FBold)      rf.bold      = m_pending.bold;
        if (m_pending.set & RunFmt::FItalic)    rf.italic    = m_pending.italic;
        if (m_pending.set & RunFmt::FUnderline) rf.underline = m_pending.underline;
        if (m_pending.set & RunFmt::FFont)      rf.font      = m_pending.font;
        if (m_pending.set & RunFmt::FSize)      rf.sizePt    = m_pending.sizePt;
        if (m_pending.set & RunFmt::FColor)     rf.color     = m_pending.color;
    }
    return rf;
}

QVariantMap DocxEditController::currentFormat() const {
    QVariantMap m;
    if (m_doc.blocks.isEmpty() || !m_ready) {
        m.insert(QStringLiteral("ready"), false);
        return m;
    }
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    const int bi = b1;
    const RunFmt rf = caretFormat();
    ParFmt pf;
    if (isEditableParagraph(bi))
        pf = m_doc.resolvePar(m_doc.blocks.at(bi));
    int list = 0;
    if (pf.numId > 0)
        list = (m_doc.numLevel(pf.numId, pf.ilvl).numFmt == QLatin1String("bullet")) ? 1 : 2;
    m.insert(QStringLiteral("ready"),       true);
    m.insert(QStringLiteral("bold"),        rf.bold);
    m.insert(QStringLiteral("italic"),      rf.italic);
    m.insert(QStringLiteral("underline"),   rf.underline);
    m.insert(QStringLiteral("font"),        rf.font);
    m.insert(QStringLiteral("sizePt"),      rf.sizePt);
    m.insert(QStringLiteral("color"),       rf.color);
    m.insert(QStringLiteral("align"),       pf.align);
    m.insert(QStringLiteral("lineSpacing"), pf.lineSpacing);
    m.insert(QStringLiteral("beforePt"),    pf.beforePt);
    m.insert(QStringLiteral("afterPt"),     pf.afterPt);
    m.insert(QStringLiteral("list"),        list);
    //  Absatzvorlage am Cursor. Leer = Standardvorlage (kein w:pStyle); die
    //  Auswahlliste zeigt dafür ihren ersten Eintrag.
    m.insert(QStringLiteral("styleId"),     pf.styleId);
    m.insert(QStringLiteral("hasSelection"), m_cursor.hasSelection());
    return m;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Speichern (asynchron; GUI baut die Ersatzteile, der Worker schreibt)
// ─────────────────────────────────────────────────────────────────────────────
QString DocxEditController::exportTargetPath() const {
    const QFileInfo fi(m_source);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName() + QStringLiteral("_edited");
    QString candidate = dir + QLatin1Char('/') + base + QStringLiteral(".docx");
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).docx").arg(n);
        ++n;
    }
    return candidate;
}

void DocxEditController::save() {
    //  Primäre Speicheraktion (Button/Strg+S) - folgt der globalen
    //  Einstellung: Direkt speichern ODER Kopie exportieren.
    if (AppSettings::instance().docxSaveDirect()) {
        if (!m_ready || m_busy || m_source.isEmpty() || !m_modified)
            return;
        //  Einmalig je Sitzung: .bak-Sicherung NEBEN dem Original.
        if (!m_bakDone) {
            const QString bak = m_source + QStringLiteral(".bak");
            QFile::remove(bak);
            QFile::copy(m_source, bak);
            m_bakDone = true;
        }
        startSaveWorker(m_source, true);
    } else {
        exportCopy();
    }
}

void DocxEditController::exportCopy() {
    if (!m_ready || m_busy || m_source.isEmpty())
        return;
    startSaveWorker(exportTargetPath(), false);
}

QString DocxEditController::pdfExportTargetPath() const {
    const QFileInfo fi(m_source);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName();
    QString candidate = dir + QLatin1Char('/') + base + QStringLiteral(".pdf");
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).pdf").arg(n);
        ++n;
    }
    return candidate;
}

void DocxEditController::startSaveWorker(const QString& targetPath, bool direct) {
    m_busy = true;
    emit busyChanged();

    //  Schnappschuss der zu schreibenden Teile auf dem GUI-Thread (reine
    //  Strings): das neue document.xml plus die Ersatzteile (Nummerierung,
    //  Medien, Vorlagen). Der Worker fasst das Modell danach nicht mehr an.
    QHash<QString, QByteArray> parts = m_doc.replacementParts();
    parts.insert(m_doc.partPath(), m_doc.newDocumentXml().toUtf8());
    const QString srcPath = m_source;
    QPointer<DocxEditController> self(this);

    class SaveTask : public QRunnable {
    public:
        SaveTask(QPointer<DocxEditController> c, QString src, QString tgt,
                 QHash<QString, QByteArray> parts, bool direct)
            : m_c(c), m_src(std::move(src)), m_tgt(std::move(tgt)),
              m_parts(std::move(parts)), m_direct(direct) { setAutoDelete(true); }
        void run() override {
            QString err;
            bool ok = false;
            //  Quelle KOMPLETT in den Speicher lesen und schließen - im
            //  Direkt-Modus ersetzt QSaveFile::commit die Quelldatei (unter
            //  Windows scheitert das Umbenennen sonst am offenen Handle).
            QList<QPair<DocxZip::Entry, QByteArray>> raws;
            {
                DocxZip::Reader zip;
                if (zip.open(m_src, &err)) {
                    ok = true;
                    raws.reserve(zip.entries().size());
                    for (int i = 0; i < zip.entries().size(); ++i) {
                        bool rok = false;
                        raws.append({ zip.entries().at(i), zip.rawData(i, &rok) });
                        if (!rok) { ok = false; err = QStringLiteral("Eintrag nicht lesbar."); break; }
                    }
                }
            }
            if (ok) {
                QSaveFile out(m_tgt);
                ok = out.open(QIODevice::WriteOnly);
                if (!ok) err = QStringLiteral("Ziel nicht beschreibbar.");
                if (ok) {
                    DocxZip::Writer w(&out);
                    auto parts = m_parts;
                    for (const auto& pr : std::as_const(raws)) {
                        auto it = parts.find(pr.first.name);
                        if (it != parts.end()) {
                            ok = w.addFile(pr.first.name, it.value(), &pr.first, &err);
                            parts.erase(it);
                        } else {
                            ok = w.addRaw(pr.first, pr.second, &err);
                        }
                        if (!ok) break;
                    }
                    if (ok) {
                        for (auto it = parts.constBegin(); ok && it != parts.constEnd(); ++it)
                            ok = w.addFile(it.key(), it.value(), nullptr, &err);
                    }
                    ok = ok && w.finish(&err) && out.commit();
                    if (!ok && err.isEmpty())
                        err = QStringLiteral("Schreiben fehlgeschlagen.");
                    if (!ok) out.cancelWriting();
                }
            }
            auto c = m_c;
            const QString tgt = m_tgt;
            const bool direct = m_direct;
            QMetaObject::invokeMethod(qApp, [c, ok, tgt, err, direct]() {
                if (!c) return;
                c->m_busy = false;
                emit c->busyChanged();
                if (ok && direct)
                    c->setModified(false);
                emit c->saveFinished(ok, tgt, err);
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<DocxEditController> m_c;
        QString m_src, m_tgt;
        QHash<QString, QByteArray> m_parts;
        bool m_direct;
    };
    QThreadPool::globalInstance()->start(new SaveTask(self, srcPath, targetPath, parts, direct));
}

void DocxEditController::release() {
    //  Beim Verlassen der Kachel automatisch sichern (Muster TextSurface -
    //  kein Datenverlust); der Speicherweg folgt der globalen Einstellung.
    if (m_ready && m_modified && !m_busy)
        save();
}
