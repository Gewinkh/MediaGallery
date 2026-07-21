#include "DocxEditController.h"
#include "DocxZip.h"
#include "DocxPdfExporter.h"
#include "AppSettings.h"
#include "PathUtils.h"

#include <QThreadPool>
#include <QRunnable>
#include <QSaveFile>
#include <QFileInfo>
#include <QFile>
#include <QGuiApplication>
#include <QClipboard>
#include <QMetaObject>
#include <QPointer>

using namespace Docx;

// Kanonische Kind-Reihenfolgen (OOXML-Schema) für upsertProp — nur die im
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
//  Laden — asynchron (QRunnable + Generationszähler; Muster PdfTextController)
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
    return i >= 0 && i < m_doc.blocks.size()
           && m_doc.blocks.at(i).kind == Block::Paragraph;
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
            //  auf dasselbe Original-Fragment — verbatim geteilt ist ok).
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
    //  Runs [i1, i2') entfernen — i2 nach dem zweiten Split neu bestimmen.
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
    emit cursorChanged();
    bumpFormat();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Edit-Scope: Bereich kopieren → mutieren → Kommando mit Vorher/Nachher.
// ─────────────────────────────────────────────────────────────────────────────
struct DocxEditController::EditScope {
    DocxEditController* c;
    int first;
    int oldCount;
    QList<Block> before;
    DocxCursor   curBefore;
    int          mergeKind;

    EditScope(DocxEditController* ctl, int firstIdx, int count, int merge = -1)
        : c(ctl), first(firstIdx), oldCount(count), mergeKind(merge) {
        curBefore = c->m_cursor;
        before.reserve(count);
        for (int i = 0; i < count; ++i)
            before.append(c->m_doc.blocks.at(first + i));
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
        c->m_stack.push(new DocxReplaceBlocksCommand(
            c, first, std::move(before), std::move(after),
            curBefore, c->m_cursor, mergeKind));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Text-Operationen
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::insertText(const QString& raw) {
    if (!m_ready || raw.isEmpty()) return;
    QString text = raw;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    if (!isEditableParagraph(b1) || !isEditableParagraph(b2))
        return;

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
    //  statt einen weiteren leeren Punkt anzulegen — der Cursor bleibt in
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

void DocxEditController::deleteBackward() {
    if (!m_ready) return;
    if (m_cursor.hasSelection()) {
        //  Selektion löschen = leeren Text „einfügen" über dieselbe Mechanik.
        int b1, p1, b2, p2;
        orderedSelection(b1, p1, b2, p2);
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
        scope.commit(1);
        return;
    }
    //  Am Absatzanfang: mit dem VORHERIGEN Absatz verschmelzen.
    int prev = bi - 1;
    if (prev < 0) return;
    if (!isEditableParagraph(prev)) {
        //  Opaker Block davor (Tabelle/sectPr): nichts löschen, nur Cursor
        //  vor den Block bewegen (Schutz vor versehentlichem Strukturverlust).
        for (int i = prev; i >= 0; --i) {
            if (isEditableParagraph(i)) { setCursor(i, blockLen(i), false); return; }
        }
        return;
    }
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
        scope.commit(1);
        return;
    }
    //  Am Absatzende: mit dem NÄCHSTEN Absatz verschmelzen.
    const int nxt = bi + 1;
    if (nxt >= m_doc.blocks.size() || !isEditableParagraph(nxt))
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
void DocxEditController::applyCharFormat(int field, const QVariant& value) {
    if (!m_ready) return;
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

//  Toggle-Zustand: „alles in der Selektion bereits X?" → aus, sonst an.
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
        //  Blocks ab → mut() zuerst, dann Fragment bauen (s. Aufrufer).
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
void DocxEditController::copy() {
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
    QGuiApplication::clipboard()->setText(out);
}
void DocxEditController::cut() {
    if (!m_cursor.hasSelection()) return;
    copy();
    deleteBackward();
}
void DocxEditController::paste() {
    const QString t = QGuiApplication::clipboard()->text();
    if (!t.isEmpty()) insertText(t);
}
void DocxEditController::undo() { if (m_stack.canUndo()) m_stack.undo(); }
void DocxEditController::redo() { if (m_stack.canRedo()) m_stack.redo(); }

// ─────────────────────────────────────────────────────────────────────────────
//  Live-Transliteration (Latein → Arabisch/Kana) am Cursor-Absatz — Muster
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
QVariantMap DocxEditController::currentFormat() const {
    QVariantMap m;
    if (m_doc.blocks.isEmpty() || !m_ready) {
        m.insert(QStringLiteral("ready"), false);
        return m;
    }
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    const int bi = b1;
    RunFmt rf = m_doc.defaultRun();
    if (isEditableParagraph(bi)) {
        const Block& blk = m_doc.blocks.at(bi);
        //  Format des Zeichens LINKS vom Cursor (Editor-Konvention).
        int pos = m_cursor.hasSelection() ? p1 : qMax(0, p1 - 1);
        int ri, ro;
        runAt(blk, pos, &ri, &ro);
        if (ri >= 0 && ri < blk.runs.size())
            rf = m_doc.resolveRun(blk, blk.runs.at(ri));
    }
    //  Pending-Format überlagert die Anzeige an der Pending-Stelle.
    if (m_pendingBlock == m_cursor.block && m_pendingPos == m_cursor.pos) {
        if (m_pending.set & RunFmt::FBold)      rf.bold      = m_pending.bold;
        if (m_pending.set & RunFmt::FItalic)    rf.italic    = m_pending.italic;
        if (m_pending.set & RunFmt::FUnderline) rf.underline = m_pending.underline;
        if (m_pending.set & RunFmt::FFont)      rf.font      = m_pending.font;
        if (m_pending.set & RunFmt::FSize)      rf.sizePt    = m_pending.sizePt;
        if (m_pending.set & RunFmt::FColor)     rf.color     = m_pending.color;
    }
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
    //  Primäre Speicheraktion (Button/Strg+S) — folgt der globalen
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

void DocxEditController::exportToPdf(const QString& tablePlaceholder,
                                     const QString& pageBreakLabel) {
    //  Aufgabe 2 „→ PDF": Original-.docx bleibt unangetastet, es entsteht
    //  <Name>.pdf daneben. Der Aufbau des QTextDocument + das Paginieren nach
    //  A4 laufen im Worker (potenziell länger → UI-Thread bleibt responsiv,
    //  Regel 17). Der Worker arbeitet auf einer COW-Kopie des Modells.
    if (!m_ready || m_busy || m_source.isEmpty())
        return;
    m_busy = true;
    emit busyChanged();

    const QString target = pdfExportTargetPath();
    Docx::Document docCopy = m_doc;   // implizit geteilte Kopie (GUI-Thread)
    QPointer<DocxEditController> self(this);

    class PdfTask : public QRunnable {
    public:
        PdfTask(QPointer<DocxEditController> c, Docx::Document d, QString tgt,
                QString tbl, QString pb)
            : m_c(c), m_doc(std::move(d)), m_tgt(std::move(tgt)),
              m_tbl(std::move(tbl)), m_pb(std::move(pb)) { setAutoDelete(true); }
        void run() override {
            QString err;
            const bool ok = DocxPdf::exportToPdf(m_doc, m_tgt, m_tbl, m_pb, &err);
            auto c = m_c;
            const QString tgt = m_tgt;
            QMetaObject::invokeMethod(qApp, [c, ok, tgt, err]() {
                if (!c) return;
                c->m_busy = false;
                emit c->busyChanged();
                emit c->pdfExportFinished(ok, tgt, err);
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<DocxEditController> m_c;
        Docx::Document m_doc;
        QString m_tgt, m_tbl, m_pb;
    };
    QThreadPool::globalInstance()->start(
        new PdfTask(self, std::move(docCopy), target, tablePlaceholder, pageBreakLabel));
}

void DocxEditController::startSaveWorker(const QString& targetPath, bool direct) {
    m_busy = true;
    emit busyChanged();

    //  Schnappschuss der Ersatzteile auf dem GUI-Thread (reine Strings).
    QHash<QString, QByteArray> parts = m_doc.replacementParts();
    parts.insert(QStringLiteral("word/document.xml"), m_doc.newDocumentXml().toUtf8());
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
            //  Quelle KOMPLETT in den Speicher lesen und schließen — im
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
    //  Beim Verlassen der Kachel automatisch sichern (Muster TextSurface —
    //  kein Datenverlust); der Speicherweg folgt der globalen Einstellung.
    if (m_ready && m_modified && !m_busy)
        save();
}
