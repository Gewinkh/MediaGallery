#include "docx/edit/DocxTextArea.h"

#include "core/PdfGlyphRuns.h"
#include <QFileInfo>
#include <QSaveFile>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QBuffer>
#include "docx/edit/DocxEditController.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QStyleHints>
#include <QRegularExpression>
#include <QCursor>

using namespace Docx;

namespace {
constexpr qreal kPtToPx     = 96.0 / 72.0;    // Punkt -> logische Pixel
constexpr qreal kTwipToPx   = kPtToPx / 20.0; // Twips (1/1440") -> logische Pixel
constexpr qreal kPadV       = 28.0;           // Rand über/unter dem Seitenstapel
constexpr qreal kPageGap    = 20.0;           // Lücke zwischen zwei Seiten
constexpr qreal kSideMargin = 14.0;           // Mindestrand neben der Seite
constexpr qreal kListIndent = 28.0;           // Einzug je Listenebene
constexpr int   kChunk      = 300;            // Blöcke je Initial-Layout-Tick
constexpr qreal kMinTextBesideImage = 60.0;
constexpr qreal kTableWrapGap = 10.0;

// Layout-Fenster: ein vermessener QTextLayout haelt die ganze Glyphenstruktur
// seines Absatzes - bei 10.000 Absaetzen zweistellige Megabyte fuer einen Bildschirm.
constexpr int   kLayoutCap  = 600;
constexpr int   kKeepMargin = 150;
}

DocxTextArea::DocxTextArea(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(false);
    setFlag(ItemAcceptsInputMethod, true);
    setCursor(QCursor(Qt::IBeamCursor));
    setOpaquePainting(false);

    m_chunkTimer.setInterval(0);
    m_chunkTimer.setSingleShot(false);
    connect(&m_chunkTimer, &QTimer::timeout, this, &DocxTextArea::layoutChunk);

    // Der Blinktakt laeuft nur, wenn der Caret sichtbar sein kann. Jedes update() malt
    // den ganzen Viewport neu und laedt die Textur zur GPU.
    m_blinkTimer.setInterval(530);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_caretOn = !m_caretOn;
        update();
    });
    connect(this, &QQuickItem::activeFocusChanged, this, &DocxTextArea::syncCaretBlink);
    syncCaretBlink();
}

void DocxTextArea::syncCaretBlink() {
    const bool want = hasActiveFocus() && m_ctl && m_ctl->ready()
                      && !m_ctl->cursor().hasSelection();
    if (want == m_blinkTimer.isActive())
        return;
    if (want) {
        m_caretOn = true;
        m_blinkTimer.start();
    } else {
        m_blinkTimer.stop();
    }
    update();
}

DocxTextArea::~DocxTextArea() = default;

void DocxTextArea::setCtl(DocxEditController* c) {
    if (m_ctl == c) return;
    if (m_ctl) m_ctl->disconnect(this);
    m_ctl = c;
    if (m_ctl) {
        connect(m_ctl, &DocxEditController::readyChanged, this, [this]() {
            rebuildAll();
            syncCaretBlink();
        });
        connect(m_ctl, &DocxEditController::blocksReplaced, this,
                [this](int first, int oldCount, int newCount) {
                    invalidateFrom(first, oldCount, newCount);
                });
        connect(m_ctl, &DocxEditController::spellRangesChanged, this,
                [this](int) { update(); });
        connect(m_ctl, &DocxEditController::cursorChanged, this, [this]() {
            m_caretOn = true;
            const int now = m_ctl->cursor().block;
            if (m_lastCursorBlock != now) {
                invalidateEmptyBlock(m_lastCursorBlock);
                invalidateEmptyBlock(now);
                m_lastCursorBlock = now;
            }
            updateCursorRect();
            syncCaretBlink();   // Selektion begonnen/aufgehoben -> Blinken an/aus
            update();
        });
        // Format ohne Selektion existiert nur als Pending-Format und erzeugt weder
        // blocksReplaced noch cursorChanged - ohne diese Verbindung blieb der Caret stehen.
        connect(m_ctl, &DocxEditController::formatRevChanged, this, [this]() {
            m_caretOn = true;
            invalidateEmptyBlock(m_ctl->cursor().block);
            updateCursorRect();
            update();
        });
    }
    emit ctlChanged();
    rebuildAll();
    syncCaretBlink();
}

void DocxTextArea::setContentY(qreal y) {
    y = qMax(0.0, qMin(y, qMax(0.0, m_contentHeight - height())));
    if (qFuzzyCompare(m_contentY + 1.0, y + 1.0)) return;
    m_contentY = y;
    emit contentYChanged();
    emit imageSelectionChanged();   // Ziehpunkte scrollen mit
    emit pageGeometryChanged();
    update();
}

// Seitengeometrie in Dokument-Pixeln, bewusst unabhaengig von der Fensterbreite:
// der Umbruch soll dort fallen, wo er auch in Word faellt.
const SectionProps& DocxTextArea::sect() const {
    static const SectionProps fallback;               // A4, falls kein Dokument
    return (m_ctl && m_ctl->ready()) ? m_ctl->section() : fallback;
}
qreal DocxTextArea::pageWpx() const   { return sect().pageW * kTwipToPx; }
qreal DocxTextArea::pageHpx() const   { return sect().pageH * kTwipToPx; }
qreal DocxTextArea::marLpx() const    { return sect().marLeft * kTwipToPx; }
qreal DocxTextArea::marTpx() const    { return sect().marTop * kTwipToPx; }
int   DocxTextArea::colCount() const  { return qMax(1, sect().cols); }
qreal DocxTextArea::colSpacePx() const { return sect().colSpace * kTwipToPx; }

qreal DocxTextArea::contentWidth() const {
    const SectionProps& s = sect();
    const qreal textW = qMax(40.0, (s.pageW - s.marLeft - s.marRight) * kTwipToPx);
    const int n = colCount();
    return qMax(40.0, (textW - (n - 1) * colSpacePx()) / n);
}
qreal DocxTextArea::slotHeight() const {
    const SectionProps& s = sect();
    return qMax(40.0, (s.pageH - s.marTop - s.marBottom) * kTwipToPx);
}
qreal DocxTextArea::slotDocX(int slot) const {
    const int c = ((slot % colCount()) + colCount()) % colCount();
    return marLpx() + c * (contentWidth() + colSpacePx());
}
qreal DocxTextArea::pageDocY(int page) const {
    return kPadV + page * (pageHpx() + kPageGap);
}
qreal DocxTextArea::slotDocY(int slot) const {
    return pageDocY(slot / colCount()) + marTpx();
}
qreal DocxTextArea::docHeight() const {
    return pageDocY(qMax(0, m_pageCount - 1)) + pageHpx() + kPadV;
}

// Zu schmale Kacheln werden verkleinert gezeichnet, nicht neu umbrochen - sonst
// waere die Ansicht nicht mehr seitengenau.
void DocxTextArea::updateScale() {
    const qreal avail = width() - 2 * kSideMargin;
    const qreal s = (avail > 20.0 && pageWpx() > 1.0)
                        ? qMin(1.0, avail / pageWpx()) : 1.0;
    if (qFuzzyCompare(s + 1.0, m_scale + 1.0))
        return;
    m_scale = s;
    updateContentHeight();
    updateCursorRect();
    emit pageGeometryChanged();
    update();
}

bool DocxTextArea::hasText(const BlockLayout& L) const {
    return L.isText;
}
int DocxTextArea::textLength(const BlockLayout& L) const {
    return L.textLen;
}
QString DocxTextArea::blockText(const BlockLayout& L) const {
    if (L.images.empty() && L.pieces.size() == 1 && L.pieces[0].lay
        && L.pieces[0].textStart == 0)
        return L.pieces[0].lay->text();
    QString t(L.textLen, kObjectChar);
    for (const Piece& p : L.pieces) {
        if (!p.lay) continue;
        const QString& s = p.lay->text();
        if (p.textStart >= 0 && p.textStart + s.size() <= t.size())
            t.replace(p.textStart, s.size(), s);
    }
    return t;
}
int DocxTextArea::lineCount(const BlockLayout& L) const {
    return int(L.rows.size());
}
DocxTextArea::LineRef DocxTextArea::lineRef(const BlockLayout& L, int li) const {
    LineRef r;
    if (li < 0 || li >= int(L.rows.size())) return r;
    const RowInfo& R = L.rows[size_t(li)];
    if (R.piece < 0 || R.piece >= int(L.pieces.size())) return r;
    const Piece& p = L.pieces[size_t(R.piece)];
    if (!p.lay || R.line < 0 || R.line >= p.lay->lineCount()) return r;
    r.lay       = p.lay.get();
    r.line      = R.line;
    r.dx        = p.dx;
    r.dy        = p.dy;
    r.textStart = p.textStart;
    return r;
}
qreal DocxTextArea::lineTop(const BlockLayout& L, int li) const {
    if (li < 0 || li >= int(L.rows.size())) return 0.0;
    return L.rows[size_t(li)].y;
}
qreal DocxTextArea::lineTextTop(const BlockLayout& L, int li) const {
    if (li < 0 || li >= int(L.rows.size())) return 0.0;
    return L.rows[size_t(li)].y + L.rows[size_t(li)].textDy;
}
qreal DocxTextArea::lineHeight(const BlockLayout& L, int li) const {
    if (li < 0 || li >= int(L.rows.size())) return 0.0;
    return L.rows[size_t(li)].visH;
}
qreal DocxTextArea::lineAscent(const BlockLayout& L, int li) const {
    if (li < 0 || li >= int(L.rows.size())) return 0.0;
    return L.rows[size_t(li)].ascent;
}
qreal DocxTextArea::linesBottom(const BlockLayout& L) const {
    qreal b = 0.0;
    for (const RowInfo& R : L.rows)
        b = qMax(b, R.y + R.visH);
    return b;
}
int DocxTextArea::lineForPos(const BlockLayout& L, int pos) const {
    const int n = int(L.rows.size());
    if (n == 0) return 0;
    const int p = qBound(0, pos, L.textLen);
    for (int li = 0; li < n; ++li)
        if (p < L.rows[size_t(li)].charEnd)
            return li;
    return n - 1;
}
int DocxTextArea::rowAtX(const BlockLayout& L, int li, qreal x) const {
    if (li < 0 || li >= int(L.rows.size())) return li;
    const qreal y = L.rows[size_t(li)].y;
    int first = li;
    while (first > 0 && qFuzzyIsNull(L.rows[size_t(first - 1)].y - y)) --first;
    int   best = first;
    qreal bestDist = std::numeric_limits<qreal>::max();
    for (int k = first; k < int(L.rows.size()); ++k) {
        if (!qFuzzyIsNull(L.rows[size_t(k)].y - y)) break;   // nächstes Band
        const LineRef r = lineRef(L, k);
        if (!r.valid()) continue;
        const QTextLine ln = r.textLine();
        const qreal a = ln.x() + r.dx;
        const qreal b = a + ln.naturalTextWidth();
        const qreal dist = (x < a) ? a - x : (x > b ? x - b : 0.0);
        if (dist < bestDist) { bestDist = dist; best = k; }
    }
    return best;
}
int DocxTextArea::lineForLocalY(const BlockLayout& L, qreal y) const {
    const int n = int(L.rows.size());
    for (int li = 0; li < n; ++li)
        if (y < L.rows[size_t(li)].y + L.rows[size_t(li)].visH)
            return li;
    return qMax(0, n - 1);
}
void DocxTextArea::lineTextRange(const BlockLayout& L, int li, int* start,
                                 int* len) const {
    const bool ok = li >= 0 && li < int(L.rows.size());
    if (start) *start = ok ? L.rows[size_t(li)].charStart : 0;
    if (len)   *len   = ok ? L.rows[size_t(li)].charEnd
                                 - L.rows[size_t(li)].charStart : 0;
}
qreal DocxTextArea::xForPos(const BlockLayout& L, int li, int pos) const {
    if (li < 0 || li >= int(L.rows.size())) return 0.0;
    const RowInfo& R = L.rows[size_t(li)];
    const int p = qBound(0, pos, L.textLen);
    const LineRef r = lineRef(L, li);
    if (r.valid()) {
        const int n = int(r.lay->text().size());
        if (p >= r.textStart && p <= r.textStart + n)
            return r.dx + r.textLine().cursorToX(p - r.textStart);
    }
    for (int k = R.imgFirst; k < R.imgFirst + R.imgCount
                             && k < int(L.images.size()); ++k) {
        const ImageBox& B = L.images[size_t(k)];
        if (p <= B.pos)     return B.x;              // davor = linke Kante
        if (p <= B.pos + 1) return B.x + B.w;        // dahinter = rechte
    }
    if (r.valid())
        return r.dx + r.textLine().cursorToX(
                   qBound(0, p - r.textStart, int(r.lay->text().size())));
    return 0.0;
}
int DocxTextArea::posForX(const BlockLayout& L, int li, qreal x) const {
    if (li < 0 || li >= int(L.rows.size())) return 0;
    const RowInfo& R = L.rows[size_t(li)];
    const int hit = imageAtX(L, li, x);
    if (hit >= 0) return L.images[size_t(hit)].pos;
    const LineRef r = lineRef(L, li);
    if (r.valid())
        return r.textStart + r.textLine().xToCursor(x - r.dx);
    if (R.imgCount > 0 && R.imgFirst < int(L.images.size())) {
        const ImageBox& B = L.images[size_t(R.imgFirst + R.imgCount - 1)];
        return (x > B.x + B.w / 2.0) ? B.pos + 1 : B.pos;
    }
    return R.charStart;
}
int DocxTextArea::imageAtPos(const BlockLayout& L, int pos) const {
    for (size_t k = 0; k < L.images.size(); ++k)
        if (L.images[k].pos == pos) return int(k);
    return -1;
}
int DocxTextArea::floatingImageAt(const BlockLayout& L, qreal x, qreal y) const {
    for (size_t k = 0; k < L.images.size(); ++k) {
        const ImageBox& B = L.images[k];
        if (!B.floating) continue;
        if (x >= B.x && x < B.x + B.w && y >= B.y && y < B.y + B.h) return int(k);
    }
    return -1;
}

int DocxTextArea::imageAtX(const BlockLayout& L, int li, qreal x) const {
    if (li < 0 || li >= int(L.rows.size())) return -1;
    const RowInfo& R = L.rows[size_t(li)];
    for (int k = R.imgFirst; k < R.imgFirst + R.imgCount
                             && k < int(L.images.size()); ++k) {
        const ImageBox& B = L.images[size_t(k)];
        if (x >= B.x && x < B.x + B.w) return k;
    }
    for (size_t k = 0; k < L.images.size(); ++k) {
        const ImageBox& B = L.images[k];
        if (!B.floating) continue;
        if (x < B.x || x >= B.x + B.w) continue;
        if (R.y >= B.y + B.h || R.y + R.visH <= B.y) continue;
        return int(k);
    }
    return -1;
}
// Baender einzeln zeichnen, fuer Absaetze ueber eine Seitenkante. Die Selektion muss
// hier von Hand hinterlegt werden - QTextLine::draw kennt keine FormatRange.
void DocxTextArea::drawBlockLines(QPainter* p, const BlockLayout& L,
                                  const QPointF& origin, int selStart, int selEnd,
                                  const QColor& selBg, int rowFrom, int rowTo) const {
    p->save();
    for (int li = rowFrom; li < rowTo; ++li) {
        const LineRef r = lineRef(L, li);
        if (!r.valid()) continue;                  // reines Bild-Band
        const QTextLine ln = r.textLine();
        const QPointF at = origin + QPointF(r.dx, r.dy);
        if (selEnd > selStart) {
            const int lineStart = r.textStart + ln.textStart();
            const int lineEnd   = lineStart + ln.textLength();
            const int a = qMax(selStart, lineStart);
            const int b = qMin(selEnd,   lineEnd);
            if (b > a) {
                qreal x1 = ln.cursorToX(a - r.textStart);
                qreal x2 = ln.cursorToX(b - r.textStart);
                if (x1 > x2) std::swap(x1, x2);
                p->fillRect(QRectF(at.x() + x1, at.y() + ln.y(),
                                   x2 - x1, ln.height()), selBg);
            }
        }
        ln.draw(p, at);
    }
    p->restore();
}

void DocxTextArea::drawBlockText(QPainter* p, const BlockLayout& L,
                                 const QPointF& origin, int selStart, int selEnd,
                                 const QColor& selBg, int rowFrom, int rowTo) const {
    const int n  = L.textLen;
    const int s0 = qBound(0, selStart, n);
    const int s1 = qBound(0, selEnd, n);
    const int nRows = int(L.rows.size());
    const int r0 = qBound(0, rowFrom, nRows);
    const int r1 = (rowTo < 0) ? nRows : qBound(r0, rowTo, nRows);
    if (r0 == 0 && r1 >= nRows) {
        for (const Piece& pc : L.pieces) {
            if (!pc.lay) continue;
            QList<QTextLayout::FormatRange> sel;
            const int len = int(pc.lay->text().size());
            const int a = qBound(0, s0 - pc.textStart, len);
            const int b = qBound(0, s1 - pc.textStart, len);
            if (b > a) {
                QTextLayout::FormatRange fr;
                fr.start  = a;
                fr.length = b - a;
                fr.format.setBackground(selBg);
                sel.append(fr);
            }
            pc.lay->draw(p, origin + QPointF(pc.dx, pc.dy), sel);
        }
    } else {
        drawBlockLines(p, L, origin, s0, s1, selBg, r0, r1);
    }
    if (L.images.empty()) return;
    p->save();
    for (const ImageBox& B : L.images) {
        const QRectF box(origin.x() + B.x, origin.y() + B.y, B.w, B.h);
        if (!B.img.isNull()) {
            p->drawImage(box.topLeft(), B.img);
        } else {
            p->setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
            p->setBrush(QColor(0, 0, 0, 8));
            p->drawRect(box);
        }
        if (s1 > s0 && B.pos >= s0 && B.pos < s1)
            p->fillRect(box, selBg);
    }
    p->restore();
}

bool DocxTextArea::segCountsTableRows(const BlockLayout& L) const {
    return L.isTable && L.table && !L.table->rows.empty();
}
int DocxTextArea::segFirstLine(const BlockLayout& L, const PageSeg& s) const {
    return (L.isToc || segCountsTableRows(L)) ? 0 : s.first;
}
int DocxTextArea::segFirstEntry(const BlockLayout& L, const PageSeg& s) const {
    return L.isToc ? s.first : 0;
}
int DocxTextArea::segFirstRow(const BlockLayout& L, const PageSeg& s) const {
    if (!segCountsTableRows(L)) return 0;
    return qBound(0, s.first, int(L.table->rows.size()) - 1);
}
qreal DocxTextArea::segOriginY(const BlockLayout& L, const PageSeg& s) const {
    if (segCountsTableRows(L)) {
        const int r = segFirstRow(L, s);
        return r > 0 ? L.table->rows[size_t(r)].y : 0.0;
    }
    const int fl = segFirstLine(L, s);
    return fl > 0 ? lineTop(L, fl) : 0.0;
}

void DocxTextArea::tableSegRows(const BlockLayout& A, int segIdx,
                                int* from, int* to) const {
    const int nRows = segCountsTableRows(A) ? int(A.table->rows.size()) : 0;
    *from = 0;
    *to   = nRows;
    if (nRows == 0 || segIdx < 0 || segIdx >= A.segs.size()) return;
    *from = segFirstRow(A, A.segs.at(segIdx));
    if (segIdx + 1 < A.segs.size())
        *to = qBound(*from, segFirstRow(A, A.segs.at(segIdx + 1)), nRows);
}

int DocxTextArea::tableSegOfRow(const BlockLayout& A, int row) const {
    if (!segCountsTableRows(A) || A.segs.isEmpty()) return -1;
    int k = 0;
    for (int j = 1; j < A.segs.size(); ++j) {
        if (segFirstRow(A, A.segs.at(j)) > row) break;
        k = j;
    }
    return k;
}

QPointF DocxTextArea::tableSegOrigin(const BlockLayout& A, int segIdx) const {
    if (segIdx < 0 || segIdx >= A.segs.size()) return QPointF(slotDocX(0), slotDocY(0));
    const PageSeg& s = A.segs.at(segIdx);
    return QPointF(slotDocX(s.slot),
                   slotDocY(s.slot) + s.yInSlot - segOriginY(A, s));
}

const DocxTextArea::PageSeg& DocxTextArea::segAt(int i, int lineIdx) const {
    static const PageSeg zero;
    const BlockLayout& L = m_lay[size_t(i)];
    if (L.segs.isEmpty()) return zero;
    // Verzeichnis und Tabelle sind ueber eine Zeile nicht adressierbar - ihre Lage
    // ist die des ersten Stuecks. Fuer eine Zelle geht es ueber tableSegOfRow.
    if (L.isToc || segCountsTableRows(L)) return L.segs.constFirst();
    int k = 0;
    for (int j = 1; j < L.segs.size(); ++j) {
        if (segFirstLine(L, L.segs.at(j)) > lineIdx) break;
        k = j;
    }
    return L.segs.at(k);
}

// Ohne Zell-Sonderfall: der Zell-Zweig darf nicht docYForLine(anchor) rufen -
// der Anker ist selbst eine Zelle, das ergaebe Endlosrekursion.
qreal DocxTextArea::flowDocYForLine(int i, int lineIdx) {
    if (i < 0 || i >= int(m_lay.size())) return slotDocY(0);
    ensureOffsetsTo(i + 1);
    const BlockLayout& L = m_lay[size_t(i)];
    if (L.segs.isEmpty()) return slotDocY(0);
    const PageSeg& s = segAt(i, lineIdx);
    return slotDocY(s.slot) + s.yInSlot + (lineTop(L, lineIdx) - segOriginY(L, s));
}

qreal DocxTextArea::flowDocXForBlock(int i, int lineIdx) {
    if (i < 0 || i >= int(m_lay.size())) return slotDocX(0);
    ensureOffsetsTo(i + 1);
    return slotDocX(segAt(i, lineIdx).slot) + m_lay[size_t(i)].indentPx;
}

QPointF DocxTextArea::cellOrigin(int cellBlock, int anchor) {
    if (anchor < 0 || anchor >= int(m_lay.size())) return QPointF(slotDocX(0), slotDocY(0));
    ensureOffsetsTo(anchor + 1);
    const BlockLayout& A = m_lay[size_t(anchor)];
    const int seg = tableSegOfRow(A, m_lay[size_t(cellBlock)].cellRow);
    if (seg < 0)
        return QPointF(flowDocXForBlock(anchor, 0), flowDocYForLine(anchor, 0));
    return tableSegOrigin(A, seg);
}

qreal DocxTextArea::docYForLine(int i, int lineIdx) {
    if (i < 0 || i >= int(m_lay.size())) return slotDocY(0);
    const int anchor = tableAnchorOf(i);
    if (anchor >= 0 && m_lay[size_t(i)].isCell) {
        const BlockLayout& C = m_lay[size_t(i)];
        return cellOrigin(i, anchor).y() + C.cellRelY + lineTop(C, lineIdx);
    }
    return flowDocYForLine(i, lineIdx);
}

qreal DocxTextArea::docXForBlock(int i, int lineIdx) {
    if (i < 0 || i >= int(m_lay.size())) return slotDocX(0);
    const int anchor = tableAnchorOf(i);
    if (anchor >= 0 && m_lay[size_t(i)].isCell)
        return cellOrigin(i, anchor).x() + m_lay[size_t(i)].cellRelX;
    return flowDocXForBlock(i, lineIdx);
}

int DocxTextArea::currentPage() const {
    if (!m_ctl || !m_ctl->ready() || m_lay.empty()) return 0;
    const int bi = qBound(0, m_ctl->cursor().block, int(m_lay.size()) - 1);
    const BlockLayout& L = m_lay[size_t(bi)];
    // Ein Zellblock traegt keine eigene Fluss-Lage - seine Seite ist die des
    // Tabellenstuecks, auf dem seine Zeile liegt.
    if (L.isCell) {
        const int anchor = tableAnchorOf(bi);
        if (anchor >= 0 && anchor < int(m_lay.size())) {
            const BlockLayout& A = m_lay[size_t(anchor)];
            const int seg = tableSegOfRow(A, L.cellRow);
            if (seg >= 0) return A.segs.at(seg).slot / colCount();
        }
    }
    if (L.segs.isEmpty()) return 0;
    return segAt(bi, lineForPos(L, m_ctl->cursor().pos)).slot / colCount();
}

qreal DocxTextArea::pageTop(int page) {
    return pageDocY(qBound(0, page, qMax(0, m_pageCount - 1))) * m_scale;
}

qreal DocxTextArea::pageTopItem(int page) {
    return pageTop(page) - m_contentY;
}

qreal DocxTextArea::pageWidthPx()  const { return pageWpx() * m_scale; }
qreal DocxTextArea::pageHeightPx() const { return pageHpx() * m_scale; }

// Bewusst nicht currentPage(): das ist die Seite des Cursors. Gemessen wird ein
// Viertel unterhalb der Fensteroberkante - dort liegt die Seite, die den Blick fuellt.
qreal DocxTextArea::currentPageTopPx() const {
    if (m_pageCount <= 0 || m_scale <= 0.0) return -m_contentY;
    const qreal step = pageHpx() + kPageGap;
    if (step <= 0.0) return -m_contentY;
    const qreal probeDocY = (m_contentY + height() * 0.25) / m_scale;
    int pg = int((probeDocY - kPadV) / step);
    pg = qBound(0, pg, m_pageCount - 1);
    return pageDocY(pg) * m_scale - m_contentY;
}

qreal DocxTextArea::estimateHeight(const Block& b) {
    if (b.kind == Block::OpaqueHidden)  return 0.0;
    if (b.kind == Block::OpaqueVisible) return 34.0;
    return 22.0 * qMax(1, b.textLength() / 90 + 1);
}

void DocxTextArea::rebuildAll() {
    m_lay.clear();
    m_offsets.clear();
    m_offsetsValidTo = 0;
    m_offsetsHighWater = 0;
    m_offsetsDirtyMax = 0;
    m_fmtCache.clear();          // anderes Dokument = andere Formate
    m_layChunkAt = 0;
    m_trimLo = m_trimHi = -1;   // Indizes verschoben -> Layout-Fenster neu bestimmen
    m_contentHeight = 0;
    m_lastCursorBlock = (m_ctl && m_ctl->ready()) ? m_ctl->cursor().block : -1;
    if (m_ctl && m_ctl->ready()) {
        m_lay.resize(size_t(m_ctl->doc().blocks.size()));
        for (int i = 0; i < int(m_lay.size()); ++i)
            m_lay[i].height = estimateHeight(m_ctl->doc().blocks.at(i));
        // Einmal je Dokument pruefen, ob es ueberhaupt Nummerierung gibt - der Lauf
        // darunter geht sonst je Tastendruck ergebnislos ueber alle Bloecke.
        m_anyNumbering = m_ctl->doc().stylesMayNumber();
        if (!m_anyNumbering) {
            for (const Block& b : m_ctl->doc().blocks)
                if (b.pfmt.set & ParFmt::FNum) { m_anyNumbering = true; break; }
        }
        rebuildMarkers();
        startChunkLayout();
    }
    updateScale();
    updateContentHeight();
    updateCursorRect();
    emit documentChanged();
    update();
}

void DocxTextArea::invalidateFrom(int first, int oldCount, int newCount) {
    if (!m_ctl || !m_ctl->ready()) { rebuildAll(); return; }
    if (m_lay.empty() && newCount > 0) { rebuildAll(); return; }
    first = qBound(0, first, int(m_lay.size()));
    // Beim Tippen in einer Zelle aendert sich die Tabellenhoehe; die traegt der Anker,
    // er muss also mit invalidiert werden.
    {
        const int anchor = tableAnchorOf(qMin(first, int(m_lay.size()) - 1));
        if (anchor >= 0 && anchor < int(m_lay.size())) {
            m_lay[size_t(anchor)].laid = false;
            m_lay[size_t(anchor)].table.reset();
            first = qMin(first, anchor);
        }
    }
    // Haeufigster Fall ist 1:1; dann an Ort und Stelle zuruecksetzen. erase samt insert
    // schoebe alle folgenden Eintraege zweimal durch den Speicher (55 % eines
    // Tastendrucks bei 30.451 Bloecken).
    const int common = qMin(oldCount, newCount);
    for (int i = 0; i < common && first + i < int(m_lay.size()); ++i)
        m_lay[size_t(first + i)] = BlockLayout();
    // Entfernen als ein Bereich: einzeln geloescht schob jeder Schritt den Rest des
    // Vektors um eine Stelle - gemessen 5.043 von 5.045 ms bei 30.000 Bloecken.
    if (oldCount > common) {
        const int from = qMin(first + common, int(m_lay.size()));
        const int to   = qMin(first + oldCount, int(m_lay.size()));
        if (to > from)
            m_lay.erase(m_lay.begin() + from, m_lay.begin() + to);
    }
    // Einfuegen darf einzeln bleiben - die Stellen wachsen aufsteigend (2,0 ms).
    for (int i = common; i < newCount; ++i)
        m_lay.insert(m_lay.begin() + first + i, BlockLayout());
    // Die ersten sofort auslegen, den Rest ueber den Chunk-Timer. Alles auf einmal
    // blockierte den GUI-Faden (gemessen 1.088 ms).
    constexpr int kSyncRelayout = 64;
    const int sync = qMin(newCount, kSyncRelayout);
    for (int i = 0; i < sync; ++i)
        ensureLaid(first + i);
    if (newCount > sync) {
        const Document& doc = m_ctl->doc();
        for (int i = sync; i < newCount; ++i) {
            const int at = first + i;
            if (at >= int(m_lay.size()) || at >= doc.blocks.size()) break;
            m_lay[size_t(at)].height = estimateHeight(doc.blocks.at(at));
        }
        m_layChunkAt = qMin(m_layChunkAt, first + sync);
        startChunkLayout();
    }
    if (!m_anyNumbering && m_ctl->ready()) {
        const Document& doc = m_ctl->doc();
        for (int i = 0; i < newCount && first + i < doc.blocks.size(); ++i)
            if (doc.blocks.at(first + i).pfmt.set & ParFmt::FNum) {
                m_anyNumbering = true;
                break;
            }
    }
    m_offsetsValidTo = qMin(m_offsetsValidTo, first);
    m_offsetsDirtyMax = qMax(m_offsetsDirtyMax, first + qMax(0, newCount - 1));
    m_trimLo = m_trimHi = -1;   // Indizes verschoben -> Layout-Fenster neu bestimmen
    // Ein Inhaltsverzeichnis haengt am ganzen Dokument, nicht nur an den Bloecken
    // hinter der Aenderung - also immer verwerfen.
    for (size_t k = 0; k < m_lay.size(); ++k) {
        if (!m_lay[k].isToc) continue;
        m_lay[k].laid = false;
        m_lay[k].tocEntries.clear();
        m_offsetsValidTo = qMin(m_offsetsValidTo, int(k));
        m_offsetsDirtyMax = qMax(m_offsetsDirtyMax, int(k));
    }
    rebuildMarkers();
    updateContentHeight();
    updateCursorRect();
    emit documentChanged();
    update();
}

// Laeuft bei jedem Tastendruck ueber das ganze Dokument - die Listenzaehler sind
// global. Ohne moegliche Nummerierung genuegt ein Bit-Test je Block.
void DocxTextArea::rebuildMarkers() {
    if (!m_ctl) return;
    const Document& d = m_ctl->doc();
    const bool mayNumber = d.stylesMayNumber();
    if (mayNumber) m_anyNumbering = true;
    if (!m_anyNumbering) return;
    QHash<int, int> counters;                       // numId -> laufende Nummer
    for (int i = 0; i < d.blocks.size() && i < int(m_lay.size()); ++i) {
        const Block& b = d.blocks.at(i);
        QString marker;
        qreal indent = 0;
        if (b.kind == Block::Paragraph
            && (mayNumber || (b.pfmt.set & ParFmt::FNum))) {
            const ParFmt pf = d.resolvePar(b);
            if (pf.numId > 0) {
                const NumLevel lv = d.numLevel(pf.numId, pf.ilvl);
                indent = kListIndent * (pf.ilvl + 1);
                if (lv.numFmt == QLatin1String("bullet")) {
                    marker = lv.lvlText.isEmpty() ? QStringLiteral("\u2022")
                                                  : lv.lvlText;
                    if (marker.contains(QLatin1Char('%')))
                        marker = QStringLiteral("\u2022");
                } else {
                    const int n = ++counters[pf.numId];
                    marker = lv.lvlText;
                    if (marker.isEmpty()) marker = QStringLiteral("%1.");
                    marker.replace(QStringLiteral("%1"), QString::number(n));
                    // static: sonst wuerde das Muster je Listenabsatz und Tastendruck neu uebersetzt.
                    static const QRegularExpression reLvlRest(QStringLiteral("%\\d"));
                    marker.remove(reLvlRest);
                }
                marker += QLatin1Char(' ');
            }
        }
        if (m_lay[i].marker != marker || !qFuzzyCompare(m_lay[i].indentPx + 1, indent + 1)) {
            m_lay[i].marker   = marker;
            m_lay[i].indentPx = indent;
            if (m_lay[i].laid) m_lay[i].laid = false;
        }
    }
}

// Grundschrift ist das aufgeloeste Stil-Format, nicht docDefaults - nur so ist eine
// leere Ueberschriftzeile auch so hoch wie eine Ueberschrift.
QFont DocxTextArea::blockBaseFont(const Block& b, int blockIdx) const {
    const Document& d = m_ctl->doc();
    const RunFmt def = d.defaultRun();
    RunFmt bf = d.resolveRun(b, Run());
    if (b.textLength() == 0 && m_ctl->cursor().block == blockIdx)
        bf = m_ctl->caretFormat();
    QFont base;
    base.setFamily(bf.font.isEmpty() ? def.font : bf.font);
    base.setPointSizeF(bf.sizePt > 0 ? bf.sizePt : def.sizePt);
    base.setBold(bf.bold);
    base.setItalic(bf.italic);
    return base;
}

// Der Deckel ist Absicht: bei hunderten Formaten waere die lineare Suche teurer
// als das Bauen.
DocxTextArea::FmtKey DocxTextArea::fmtKeyOf(const RunFmt& rf, const Run& r) const {
    FmtKey key;
    key.rf       = rf;
    key.opaque   = r.opaque;
    key.revision = int(r.revision);
    if (r.revision != Run::RevNone) key.author = r.revAuthor;
    return key;
}

const QTextCharFormat& DocxTextArea::charFormatOf(const RunFmt& rf,
                                                  const RunFmt& def,
                                                  const Run& r) const {
    constexpr size_t kFmtCacheCap = 48;
    const FmtKey key = fmtKeyOf(rf, r);
    for (const std::pair<FmtKey, QTextCharFormat>& e : m_fmtCache)
        if (e.first == key) return e.second;

    QTextCharFormat cf;
    QFont f;
    f.setFamily(rf.font.isEmpty() ? def.font : rf.font);
    f.setPointSizeF(rf.sizePt > 0 ? rf.sizePt : def.sizePt);
    f.setBold(rf.bold);
    f.setItalic(rf.italic);
    f.setUnderline(rf.underline);
    cf.setFont(f);
    cf.setForeground(rf.color.isValid() ? rf.color : QColor(0, 0, 0));
    if (r.opaque && r.revision == Run::RevNone) {
        cf.setBackground(QColor(0, 0, 0, 14));
    }
    if (r.revision != Run::RevNone) {
        const QColor rc = revisionColor(r.revAuthor);
        cf.setForeground(rc);
        if (r.revision == Run::RevInserted) f.setUnderline(true);
        else                                f.setStrikeOut(true);
        cf.setFont(f);
    }
    if (m_fmtCache.size() >= kFmtCacheCap) m_fmtCache.clear();
    m_fmtCache.emplace_back(std::move(key), cf);
    return m_fmtCache.back().second;
}

void DocxTextArea::buildLayout(int i) {
    const Document& d = m_ctl->doc();
    const Block& b = d.blocks.at(i);
    BlockLayout& L = m_lay[i];
    L.pieces.clear();
    L.images.clear();
    L.rows.clear();
    L.textLen  = 0;
    L.isText   = false;
    L.hasBreak = false;
    L.isImage  = false;
    L.trimmed  = false;
    L.beforePx = 0;

    if (b.kind == Block::OpaqueHidden) {
        L.height = 0; L.laid = true; return;
    }
    if (b.kind == Block::OpaqueVisible) {
        // Tabellen als echtes Gitter auslegen, damit die Hoehe und die Umbrueche danach
        // stimmen; dieser Zweig gilt nur fuer nicht flach zerlegbare Tabellen.
        if (b.opaqueName == QLatin1String("w:tbl")) { buildTableLayout(i); return; }
        L.height = 34; L.laid = true;
        shiftBelowForeignFloats(i);          // auch er weicht nach unten aus
        return;
    }

    // Flach zerlegt: der erste Zellblock ist Anker und traegt Gitter samt Gesamthoehe,
    // alle weiteren sind 0 hoch - so bleibt der Fluss streng monoton.
    if (b.tableId >= 0) {
        const int anchor = d.tableFirstBlock(b.tableId);
        if (anchor == i) { buildFlatTableLayout(i); return; }
        if (anchor >= 0 && anchor < int(m_lay.size()) && !m_lay[size_t(anchor)].table)
            buildFlatTableLayout(anchor);
        L.laid = true;
        return;
    }

    if (buildTocLayout(i))
        return;

    const ParFmt pf = d.resolvePar(b);
    L.beforePx = pf.beforePt * kPtToPx;
    L.textLen  = b.textLength();
    const qreal bottom = buildInlineRows(L, b, blockBaseFont(b, i), pf,
                                         contentWidth() - L.indentPx, i);
    L.height  = bottom + pf.afterPt * kPtToPx;

    L.isImage = (L.images.size() == 1 && L.textLen == 1);
    L.isText  = true;
    L.laid    = true;
}

// Ein w:drawing-Run ist in Word ein Zeichen in der Zeile: zwei Bilder stehen
// nebeneinander, Text dahinter daneben. QTextLayout kann das nicht selbst.
QVector<DocxTextArea::FloatObstacle> DocxTextArea::foreignFloats(int blockIdx) const {
    QVector<FloatObstacle> out;
    if (blockIdx <= 0 || blockIdx > int(m_lay.size())) return out;
    const qreal maxBack = slotHeight();
    qreal between = 0.0;                 // Höhe der Blöcke zwischen p und blockIdx
    for (int p = blockIdx - 1; p >= 0; --p) {
        const BlockLayout& P = m_lay[size_t(p)];
        const qreal off = P.height + between;
        if (P.tableFloating && P.table && P.floatOverhang > 0.0) {
            if (P.floatOverhang - off > 0.0) {
                FloatObstacle o;
                o.x = 0.0;
                o.w = P.table->width;
                o.h = P.floatOverhang;
                o.y = -off;
                o.padR = kTableWrapGap;
                o.wrapSide = Docx::InlineImage::SideRight;   // Text RECHTS davon
                out.append(o);
            }
        }
        if (P.floatOverhang > 0.0) {
            for (const ImageBox& B : P.images) {
                if (!B.floating) continue;
                if (B.y + B.h - off <= 0.0) continue;      // reicht nicht herein
                FloatObstacle o;
                o.x = B.x;  o.w = B.w;  o.h = B.h;
                o.y = B.y - off;                            // darf negativ sein
                o.padL = B.padL;  o.padR = B.padR;
                o.wrapSide = B.wrapSide;
                out.append(o);
            }
        }
        if (off > maxBack) break;
        between = off;
    }
    return out;
}

qreal DocxTextArea::foreignFloatBottom(int blockIdx) const {
    qreal bottom = 0.0;
    const QVector<FloatObstacle> f = foreignFloats(blockIdx);
    for (const FloatObstacle& o : f)
        bottom = qMax(bottom, o.y + o.h);
    return bottom;
}

bool DocxTextArea::canWrapAroundFloats(int blockIdx) const {
    if (!m_ctl || !m_ctl->ready()) return false;
    if (blockIdx < 0 || blockIdx >= m_ctl->doc().blocks.size()) return false;
    // Gelesen wird das Dokument, nicht das Layout: beim Auslegen eines frueheren
    // Blocks stehen die Layout-Flags der spaeteren noch nicht.
    const Docx::Block& b = m_ctl->doc().blocks.at(blockIdx);
    if (b.kind != Docx::Block::Paragraph) return false;   // opak/unsichtbar
    if (b.tableId >= 0) return false;                     // Zelle einer Tabelle
    return !m_ctl->doc().isTocParagraph(b);
}

void DocxTextArea::invalidateFloatFollowers(int i, qreal reach) {
    if (reach <= 0.0) return;
    qreal acc = 0.0;
    for (int k = i + 1; k < int(m_lay.size()) && acc < reach; ++k) {
        BlockLayout& F = m_lay[size_t(k)];
        F.laid = false;
        acc += F.height;
    }
}

qreal DocxTextArea::buildInlineRows(BlockLayout& L, const Block& b,
                                    const QFont& base, const ParFmt& pf,
                                    qreal width, int blockIdx) {
    const Document& d  = m_ctl->doc();
    const RunFmt    def = d.defaultRun();
    // QTextLayout kennt fuer den Clear-Umbruch nur U+2028; gleiche Laenge, alle
    // Positionen bleiben gueltig. clearAt merkt sich die Stellen.
    QString         text = b.plainText();
    QVector<int>    clearAt;
    for (int ci = 0; ci < text.size(); ++ci)
        if (text.at(ci) == Docx::kClearBreak) { clearAt.append(ci); text[ci] = Docx::kLineBreak; }
    const qreal     W = qMax(20.0, width);
    const qreal     spacing = qMax(0.5, pf.lineSpacing);
    L.hasBreak = text.contains(kPageBreak);

    // Diese Funktion baut den Block und muss ihn selbst leeren - sonst blieb ein
    // geloeschtes Bild in images stehen und wurde weitergezeichnet.
    L.pieces.clear();
    L.images.clear();
    L.rows.clear();

    // Angrenzende Runs mit gleichem Aussehen zu einem Bereich verschmelzen: QTextLayout
    // zerlegt sonst je Bereichsgrenze, und der PDF-Export schreibt je Stueck ein
    // eigenes BT..ET mit absolutem Td.
    QList<QTextLayout::FormatRange> fmts;
    int acc = 0;
    FmtKey lastKey;
    bool   haveLast = false;
    for (const Run& r : b.runs) {
        if (r.text.isEmpty()) continue;
        const RunFmt rf  = d.resolveRun(b, r);
        const FmtKey key = fmtKeyOf(rf, r);
        if (haveLast && key == lastKey) {
            fmts.last().length += r.text.size();
        } else {
            QTextLayout::FormatRange fr;
            fr.start  = acc;
            fr.length = r.text.size();
            fr.format = charFormatOf(rf, def, r);
            fmts.append(fr);
            lastKey  = key;
            haveLast = true;
        }
        acc += r.text.size();
    }
    for (int p = text.indexOf(kPageBreak); p >= 0; p = text.indexOf(kPageBreak, p + 1)) {
        QTextLayout::FormatRange fr;
        fr.start = p; fr.length = 1;
        fr.format.setForeground(Qt::transparent);
        fmts.append(fr);
    }

    QTextOption opt;
    switch (pf.align) {
    case 1:  opt.setAlignment(Qt::AlignHCenter); break;
    case 2:  opt.setAlignment(Qt::AlignRight);   break;
    case 3:  opt.setAlignment(Qt::AlignJustify); break;
    default: opt.setAlignment(Qt::AlignLeft);    break;
    }
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setTextDirection(Qt::LayoutDirectionAuto);   // Arabisch: RTL automatisch

    qreal y = L.beforePx;      // Oberkante des laufenden Bandes
    qreal x = 0.0;             // x-Cursor im laufenden Band
    qreal rowAdv = 0.0;        // Vorschub des laufenden Bandes
    qreal rowVis = 0.0;        // sichtbare Höhe des laufenden Bandes
    int   cur = -1;            // laufendes Band (−1 = keins offen)

    auto openRow = [&](int charStart) {
        RowInfo r;
        r.y = y;
        r.charStart = charStart;
        r.charEnd   = charStart;
        r.imgFirst  = int(L.images.size());
        L.rows.push_back(r);
        cur = int(L.rows.size()) - 1;
        x = rowAdv = rowVis = 0.0;
    };
    qreal bandCarryAdv = 0.0, bandCarryVis = 0.0;
    int   leftRowIdx = -1;      // linkes Stück, dessen Höhen noch nachzuziehen sind
    qreal pendingRightX = -1.0; // ≥ 0: nächste Zeile gehört rechts neben das Bild

    auto closeRow = [&](bool advance = true) {
        if (cur < 0) return;
        RowInfo& R = L.rows[size_t(cur)];
        rowAdv = qMax(rowAdv, bandCarryAdv);
        rowVis = qMax(rowVis, bandCarryVis);
        R.h    = rowAdv;
        R.visH = rowVis;
        if (R.imgCount > 0) {
            for (int k = R.imgFirst; k < R.imgFirst + R.imgCount
                                     && k < int(L.images.size()); ++k) {
                ImageBox& B = L.images[size_t(k)];
                B.y = R.y + qMax(0.0, rowVis - B.h);
            }
            if (R.piece >= 0 && R.piece < int(L.pieces.size())
                && L.pieces[size_t(R.piece)].lay) {
                QTextLayout* lp = L.pieces[size_t(R.piece)].lay.get();
                if (R.line >= 0 && R.line < lp->lineCount()) {
                    QTextLine ln = lp->lineAt(R.line);
                    R.textDy = qMax(0.0, rowVis - ln.height());
                    ln.setPosition(QPointF(ln.x(), R.y + R.textDy));
                }
            }
        }
        if (advance) {
            y += rowAdv;
            // Beide Stuecke eines Bandes melden dieselbe Hoehe, sonst trennt die Paginierung sie.
            if (leftRowIdx >= 0 && leftRowIdx < int(L.rows.size())) {
                L.rows[size_t(leftRowIdx)].h    = rowAdv;
                L.rows[size_t(leftRowIdx)].visH = rowVis;
                leftRowIdx = -1;
            }
            bandCarryAdv = bandCarryVis = 0.0;
        } else {
            bandCarryAdv = rowAdv;
            bandCarryVis = rowVis;
        }
        cur = -1;
    };
    auto ensureRow = [&](int charStart) {
        if (cur < 0) openRow(charStart);
    };

    const QVector<InlineImage> allImgs = d.paragraphImages(b);
    QVector<InlineImage> floats;
    for (const InlineImage& ii : allImgs)
        if (ii.anchored && ii.wrap == InlineImage::WrapSquare) floats.append(ii);
    auto isFloating = [](const InlineImage& ii) {
        return ii.anchored && ii.wrap == InlineImage::WrapSquare;
    };
    {
        constexpr qreal kEmuToPx = kPtToPx / 12700.0;
        for (const InlineImage& f : floats) {
            ImageBox box;
            if (!makeImageBox(f, W, &box)) continue;
            box.pos      = f.pos;
            box.run      = f.run;
            box.floating = true;
            box.wrapSide = f.wrapSide;
            box.padL     = qBound(0.0, f.distLEmu * kEmuToPx, 40.0);
            box.padR     = qBound(0.0, f.distREmu * kEmuToPx, 40.0);
            box.x        = qBound(0.0, f.posXEmu * kEmuToPx, qMax(0.0, W - box.w));
            box.y        = L.beforePx + qMax(0.0, f.posYEmu * kEmuToPx);
            L.images.push_back(box);
        }
    }
    const QVector<FloatObstacle> foreign = foreignFloats(blockIdx);

    auto usableSpan = [&](qreal top, qreal h, qreal* left, qreal* right,
                          qreal* pushTo, qreal* gapL, qreal* gapR) {
        *left = 0.0;
        *right = W;
        *pushTo = top;
        *gapL = *gapR = -1.0;
        qreal lowest = top;
        // Das Ob braucht ein eigenes Flag: bothL ist eine Kante und wird negativ, sobald
        // das Bild am linken Rand steht - mit -1 als Kennzeichen fiel der Zweig aus.
        bool  haveBoth = false;
        qreal bothL = 0.0, bothR = 0.0;
        auto consider = [&](qreal by, qreal bh, qreal bx, qreal bw,
                            qreal bpadL, qreal bpadR, int side) {
            if (top >= by + bh || top + h <= by) return;        // kein Überlapp
            const qreal bl = bx - bpadL;
            const qreal br = bx + bw + bpadR;
            if (side == InlineImage::SideLeft)       *right = qMin(*right, bl);
            else if (side == InlineImage::SideRight) *left  = qMax(*left,  br);
            else if (side == InlineImage::SideBoth && !haveBoth) {
                haveBoth = true;
                bothL = bl;                 // Seiten erst unten prüfen (s. u.)
                bothR = br;
            }
            else if (bl <= W - br)                   *left  = qMax(*left,  br);
            else                                     *right = qMin(*right, bl);
            lowest = qMax(lowest, by + bh);
        };
        for (const ImageBox& B : L.images) {
            if (!B.floating) continue;
            consider(B.y, B.h, B.x, B.w, B.padL, B.padR, B.wrapSide);
        }
        for (const FloatObstacle& O : foreign)
            consider(O.y, O.h, O.x, O.w, O.padL, O.padR, O.wrapSide);
        if (haveBoth) {
            if (bothL - *left >= kMinTextBesideImage
                && *right - bothR >= kMinTextBesideImage) {
                *gapL = bothL;
                *gapR = bothR;
            } else if (bothL - *left <= *right - bothR) {
                *left = qMax(*left, bothR);
            } else {
                *right = qMin(*right, bothL);
            }
        }
        // Bleibt weniger als eine lesbare Spalte, faengt der Text unter dem Bild an -
        // sonst bekam neben einem seitenbreiten Bild jedes Wort eine eigene Zeile.
        if (*right - *left < kMinTextBesideImage) {
            *left = 0.0;
            *right = W;
            *pushTo = lowest;      // volle Breite gilt erst UNTER dem Bild
        }
    };

    auto layoutSegment = [&](int from, int to) {
        const QString sub = text.mid(from, to - from);
        if (sub.isEmpty()) return;
        auto lay = std::make_unique<QTextLayout>(sub, base);
        lay->setTextOption(opt);
        lay->setCacheEnabled(true);
        QList<QTextLayout::FormatRange> sf;
        for (const QTextLayout::FormatRange& fr : fmts) {
            const int a = qMax(fr.start, from);
            const int e = qMin(fr.start + fr.length, to);
            if (e <= a) continue;
            QTextLayout::FormatRange c;
            c.start  = a - from;
            c.length = e - a;
            c.format = fr.format;
            sf.append(c);
        }
        lay->setFormats(sf);

        const int pieceIdx = int(L.pieces.size());
        Piece pc;
        pc.textStart = from;
        pc.lay = std::move(lay);
        L.pieces.push_back(std::move(pc));
        QTextLayout* lp = L.pieces[size_t(pieceIdx)].lay.get();

        lp->beginLayout();
        for (int li = 0;; ++li) {
            QTextLine ln = lp->createLine();
            if (!ln.isValid()) break;
            const bool toRight = (pendingRightX >= 0.0);
            if (li > 0 || (cur >= 0 && x > 0.0 && W - x < kMinTextBesideImage)) {
                if (toRight) leftRowIdx = cur;
                closeRow(!toRight);
                if (clearAt.contains(from + ln.textStart() - 1)) {
                    qreal below = y;
                    for (const ImageBox& B : L.images)
                        if (B.floating) below = qMax(below, B.y + B.h);
                    for (const FloatObstacle& O : foreign)
                        below = qMax(below, O.y + O.h);
                    y = below;
                }
                openRow(from + ln.textStart());
            }
            ensureRow(from + ln.textStart());
            // Zwei Durchgaenge: erst volle Breite messen (die Hoehe entscheidet, welche Bilder
            // das Band schneiden), dann auf den nutzbaren Bereich zuruecksetzen.
            ln.setLineWidth(qMax(8.0, W - x));
            qreal availL = 0.0, availR = W, gapL = -1.0, gapR = -1.0;
            // Auch fremde Stoerer loesen den Umfluss aus - ohne sie lief der Text durchs Bild.
            if (!floats.isEmpty() || !foreign.isEmpty()) {
                // Reicht der Platz daneben nicht, rutscht das Band unter das Bild. Nur solange es
                // leer ist, und nie beim rechten Stueck - dessen linkes steht schon.
                const bool bandEmpty = (!toRight && x <= 0.0
                                        && L.rows[size_t(cur)].imgCount == 0);
                for (int guard = 0; guard < 8; ++guard) {
                    qreal push = y;
                    usableSpan(y, ln.height(), &availL, &availR, &push, &gapL, &gapR);
                    if (!bandEmpty || push <= y) break;
                    y = push;
                    L.rows[size_t(cur)].y = y;
                }
                if (toRight) {
                    availL = qMax(availL, pendingRightX);
                    pendingRightX = -1.0;
                    ln.setLineWidth(qMax(8.0, availR - availL));
                } else if (gapR > gapL) {
                    pendingRightX = gapR;
                    ln.setLineWidth(qMax(8.0, gapL - availL - x));
                } else {
                    ln.setLineWidth(qMax(8.0, availR - availL - x));
                }
            }
            ln.setPosition(QPointF(availL + x, y));
            RowInfo& R = L.rows[size_t(cur)];
            R.piece   = pieceIdx;
            R.line    = li;
            R.ascent  = ln.ascent();
            R.charEnd = from + ln.textStart() + ln.textLength();
            rowVis = qMax(rowVis, ln.height());
            rowAdv = qMax(rowAdv, ln.height() * spacing);
            x += ln.naturalTextWidth();
        }
        lp->endLayout();
        pendingRightX = -1.0;
    };

    int at = 0, imgIdx = 0;
    while (at < text.size() || imgIdx < allImgs.size()) {
        const int nextImg = (imgIdx < allImgs.size()) ? allImgs.at(imgIdx).pos
                                                      : int(text.size());
        if (at < nextImg || imgIdx >= allImgs.size()) {
            layoutSegment(at, qMin(nextImg, int(text.size())));
            at = qMin(nextImg, int(text.size()));
            if (imgIdx >= allImgs.size()) break;
            continue;
        }
        const InlineImage& info = allImgs.at(imgIdx);
        ++imgIdx;
        at = qMax(at, info.pos + 1);
        // Ein verankertes Bild steht schon an seiner Stelle; bliebe sein Objekt-Zeichen im
        // Text, malte QTextLayout ein Ersatzkaestchen und belegte dessen Breite.
        if (isFloating(info)) {
            if (cur >= 0)
                L.rows[size_t(cur)].charEnd = qMax(L.rows[size_t(cur)].charEnd,
                                                   info.pos + 1);
            continue;
        }
        ImageBox box;
        if (!makeImageBox(info, W, &box))
            continue;
        box.pos = info.pos;
        box.run = info.run;
        if (cur >= 0 && x > 0.0 && x + box.w > W + 0.5)
            closeRow();                       // passt nicht mehr -> neues Band
        ensureRow(info.pos);
        box.x = x;
        box.y = y;
        L.images.push_back(box);
        RowInfo& R = L.rows[size_t(cur)];
        R.imgCount = int(L.images.size()) - R.imgFirst;
        R.charEnd  = info.pos + 1;
        x += box.w;
        rowVis = qMax(rowVis, box.h);
        rowAdv = qMax(rowAdv, box.h);
    }
    closeRow();

    // Unterkante des Blocks ist die seines Textes. Zog er sie bis unter ein verankertes
    // Bild, begann der Folgeabsatz immer darunter - auch bei halber freier Seite.
    qreal bottom = L.rows.empty()
                       ? L.beforePx + QFontMetricsF(base).height() * spacing
                       : y;
    qreal floatBottom = 0.0;
    for (const ImageBox& B : L.images)
        if (B.floating) floatBottom = qMax(floatBottom, B.y + B.h);
    if (blockIdx < 0) {
        L.floatOverhang = 0.0;
        return qMax(bottom, floatBottom);
    }
    L.floatOverhang = qMax(0.0, floatBottom - bottom);
    return bottom;
}

std::unique_ptr<QTextLayout> DocxTextArea::layoutParagraph(const Block& b, qreal width,
                                                           qreal* heightOut) const {
    const Document& d = m_ctl->doc();
    const RunFmt def = d.defaultRun();
    const RunFmt bf  = d.resolveRun(b, Run());
    const ParFmt pf  = d.resolvePar(b);

    QFont base;
    base.setFamily(bf.font.isEmpty() ? def.font : bf.font);
    base.setPointSizeF(bf.sizePt > 0 ? bf.sizePt : def.sizePt);
    base.setBold(bf.bold);
    base.setItalic(bf.italic);

    auto lay = std::make_unique<QTextLayout>(b.plainText(), base);
    QTextOption opt;
    switch (pf.align) {
    case 1:  opt.setAlignment(Qt::AlignHCenter); break;
    case 2:  opt.setAlignment(Qt::AlignRight);   break;
    case 3:  opt.setAlignment(Qt::AlignJustify); break;
    default: opt.setAlignment(Qt::AlignLeft);    break;
    }
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setTextDirection(Qt::LayoutDirectionAuto);
    lay->setTextOption(opt);

    QList<QTextLayout::FormatRange> fmts;
    int acc = 0;
    RunFmt lastRf;
    bool   haveLast = false;
    for (const Run& r : b.runs) {
        if (r.text.isEmpty()) continue;
        const RunFmt rf = d.resolveRun(b, r);
        if (haveLast && rf == lastRf) {
            fmts.last().length += r.text.size();
            acc += r.text.size();
            continue;
        }
        QTextCharFormat cf;
        QFont f;
        f.setFamily(rf.font.isEmpty() ? def.font : rf.font);
        f.setPointSizeF(rf.sizePt > 0 ? rf.sizePt : def.sizePt);
        f.setBold(rf.bold);
        f.setItalic(rf.italic);
        f.setUnderline(rf.underline);
        cf.setFont(f);
        cf.setForeground(rf.color.isValid() ? rf.color : QColor(0, 0, 0));
        QTextLayout::FormatRange fr;
        fr.start = acc; fr.length = r.text.size(); fr.format = cf;
        fmts.append(fr);
        lastRf   = rf;
        haveLast = true;
        acc += r.text.size();
    }
    lay->setFormats(fmts);

    const qreal spacing = qMax(0.5, pf.lineSpacing);
    qreal y = 0.0;
    lay->beginLayout();
    for (;;) {
        QTextLine line = lay->createLine();
        if (!line.isValid()) break;
        line.setLineWidth(qMax(8.0, width));
        line.setPosition(QPointF(0, y));
        y += line.height() * spacing;
    }
    lay->endLayout();
    if (lay->lineCount() == 0)
        y += QFontMetricsF(base).height() * spacing;
    if (heightOut) *heightOut = y;
    return lay;
}

bool DocxTextArea::makeImageBox(const InlineImage& info, qreal avail,
                                ImageBox* out) const {
    if (!out || !m_ctl || !m_ctl->ready()) return false;
    constexpr qreal kEmuToPx = kPtToPx / 12700.0;     // 1 pt = 12700 EMU
    const qreal av = avail > 0.0 ? avail : contentWidth();
    qreal w = info.cxEmu > 0 ? info.cxEmu * kEmuToPx : 0.0;
    qreal h = info.cyEmu > 0 ? info.cyEmu * kEmuToPx : 0.0;

    const QByteArray bytes = m_ctl->doc().imageBytes(info.relId);
    QImage src;
    if (!bytes.isEmpty()) src.loadFromData(bytes);

    if (src.isNull()) {
        if (w <= 0.0) w = qMin(av, 160.0);
        if (h <= 0.0) h = 90.0;
    } else if (w <= 0.0 || h <= 0.0) {               // kein extent -> native Größe
        w = src.width()  * (96.0 / qMax(1, src.dotsPerMeterX() > 0
                                            ? qRound(src.dotsPerMeterX() * 0.0254) : 96));
        h = src.height() * (96.0 / qMax(1, src.dotsPerMeterY() > 0
                                            ? qRound(src.dotsPerMeterY() * 0.0254) : 96));
        if (w <= 0.0 || h <= 0.0) { w = src.width(); h = src.height(); }
    }
    if (w > av && w > 0.0) { h *= av / w; w = av; }
    w = qBound(8.0, w, qMax(8.0, av));
    h = qBound(8.0, h, 4000.0);

    out->relId  = info.relId;
    out->w      = w;
    out->h      = h;
    out->broken = src.isNull();
    if (!src.isNull()) {
        const QSize target(qMax(1, qRound(w)), qMax(1, qRound(h)));
        out->img = src.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        out->img = QImage();
    }
    return true;
}

bool DocxTextArea::buildTocLayout(int i) {
    BlockLayout& L = m_lay[size_t(i)];
    const Document& d = m_ctl->doc();
    const Block& b = d.blocks.at(i);

    if (!d.isTocParagraph(b)) {
        L.isToc = false;
        L.tocEntries.clear();
        return false;
    }
    L.isToc = true;
    L.tocEntries = d.tocEntries();
    L.pieces.clear();
    L.rows.clear();
    L.images.clear();

    const RunFmt def = d.defaultRun();
    const RunFmt tf  = d.paragraphMarkFormat(b);
    L.tocFont = QFont();
    L.tocFont.setFamily(tf.font.isEmpty() ? def.font : tf.font);
    L.tocFont.setPointSizeF(tf.sizePt > 0 ? tf.sizePt : 11.0);
    L.tocFont.setBold(tf.bold);
    L.tocFont.setItalic(tf.italic);
    const QFontMetricsF fm(L.tocFont);
    L.tocLineH = fm.height() + 4.0;

    const qreal usable = qMax(L.tocLineH, slotHeight() - 8.0);
    L.tocPerPage = qMax(1, int(usable / L.tocLineH));

    const int n = qMax(1, int(L.tocEntries.size()));
    L.height   = n * L.tocLineH + 8.0;
    L.beforePx = 0.0;
    L.indentPx = 0.0;
    L.marker.clear();
    L.laid     = true;
    shiftBelowForeignFloats(i);
    return true;
}

int DocxTextArea::pageOfBlock(int i) {
    return pageOfEntry(i, 0);
}

int DocxTextArea::pageOfEntry(int i, int pos) {
    if (i < 0 || i >= int(m_lay.size())) return 1;
    ensureOffsetsTo(i + 1);
    const BlockLayout& L = m_lay[size_t(i)];
    if (L.segs.isEmpty()) return 1;
    return segAt(i, lineForPos(L, pos)).slot / qMax(1, colCount()) + 1;
}

void DocxTextArea::dropSelectedImage(int block, qreal xMm, qreal yMm) {
    if (!m_ctl || !m_ctl->ready()) return;
    const int n = int(m_lay.size());
    const int src = qBound(0, block < 0 ? m_ctl->cursor().block : block, qMax(0, n - 1));
    if (src >= n) return;
    ensureLaid(src);

    constexpr qreal kMmToPx = kPtToPx / 12700.0 * 36000.0;   // 1 mm = 36000 EMU
    const Document& d = m_ctl->doc();
    auto editable = [&](int i) {
        return i >= 0 && i < d.blocks.size()
               && d.blocks.at(i).kind == Block::Paragraph && d.blocks.at(i).tableId < 0
               && !m_lay[size_t(i)].isToc;
    };
    auto textHeight = [&](int i) {
        ensureLaid(i);
        const BlockLayout& L = m_lay[size_t(i)];
        return qMax(L.beforePx + 1.0, linesBottom(L));
    };

    const BlockLayout& L0 = m_lay[size_t(src)];
    qreal top = L0.beforePx + yMm * kMmToPx;     // Oberkante im Quellabsatz
    int dst = src;

    if (top > textHeight(src)) {
        qreal rest = top - textHeight(src);
        for (int i = src + 1; i < n; ++i) {
            if (!editable(i)) continue;
            const qreal h = textHeight(i);
            dst = i;
            top = qMin(rest, h);
            if (rest <= h) break;
            rest -= h;
        }
    } else if (top < m_lay[size_t(src)].beforePx) {
        qreal rest = top - m_lay[size_t(src)].beforePx;   // ≤ 0
        for (int i = src - 1; i >= 0; --i) {
            if (!editable(i)) continue;
            const qreal h = textHeight(i);
            dst = i;
            top = qMax(0.0, rest + h);
            if (rest + h >= 0.0) break;
            rest += h;
        }
    }

    // Die Bildlage ist absatzrelativ, die Seitenaufteilung zeilenrelativ. Laeuft der
    // Zielabsatz ueber eine Seitenkante, schob ein Versatz das Bild aus dem Textbereich.
    {
        ensureOffsetsTo(dst + 1);
        const BlockLayout& LD = m_lay[size_t(dst)];
        const qreal imgH = m_imgSelDoc.height();
        if (!LD.segs.isEmpty() && imgH > 0.0) {
            int si = 0;
            for (int k = 0; k < LD.segs.size(); ++k) {
                if (segOriginY(LD, LD.segs.at(k)) <= top + 0.01) si = k;
                else break;
            }
            const PageSeg& sg = LD.segs.at(si);
            const qreal so    = segOriginY(LD, sg);
            const qreal avail = qMax(0.0, slotHeight() - sg.yInSlot);
            if ((top - so) + imgH > avail)
                top = so + qMax(0.0, avail - imgH);
        }
    }

    if (dst == src) {
        const qreal yClamped = (top - m_lay[size_t(src)].beforePx) / kMmToPx;
        m_ctl->setImagePositionMm(block, xMm, qMax(0.0, yClamped));
        return;
    }
    const qreal yInDst = (top - m_lay[size_t(dst)].beforePx) / kMmToPx;
    m_ctl->moveImageToBlock(src, dst, xMm, yInDst);
}

void DocxTextArea::paintToc(QPainter* p, const BlockLayout& L, qreal left,
                            qreal y, qreal width, int firstEntry) {
    y += L.topPad;                     // s. shiftBelowForeignFloats
    const QFont f = L.tocFont;
    p->setFont(f);
    const QFontMetricsF fm(f);

    if (L.tocEntries.isEmpty()) {
        p->setPen(QColor(140, 140, 140));
        QFont it = f; it.setItalic(true);
        p->setFont(it);
        p->drawText(QRectF(left, y + 4, width, L.tocLineH),
                    Qt::AlignLeft | Qt::AlignVCenter, m_tocEmptyLabel);
        return;
    }

    const int from = qBound(0, firstEntry, int(L.tocEntries.size()));
    const int to   = qMin(int(L.tocEntries.size()),
                          from + qMax(1, L.tocPerPage));
    qreal ly = y + 4.0;
    for (int ei = from; ei < to; ++ei) {
        const Docx::TocEntry& e = L.tocEntries.at(ei);
        const qreal indent = (e.level - 1) * 18.0;
        const QString page = QString::number(pageOfEntry(e.block, e.pos));
        const qreal pw = fm.horizontalAdvance(page);
        const qreal availText = qMax(20.0, width - indent - pw - 24.0);
        const QString text = fm.elidedText(e.text, Qt::ElideRight, availText);
        const qreal tw = fm.horizontalAdvance(text);
        p->setPen(QColor(30, 30, 30));
        p->drawText(QPointF(left + indent, ly + fm.ascent()), text);
        p->drawText(QPointF(left + width - pw, ly + fm.ascent()), page);

        const qreal dotFrom = left + indent + tw + 4.0;
        const qreal dotTo   = left + width - pw - 4.0;
        if (dotTo > dotFrom) {
            p->setPen(QPen(QColor(150, 150, 150), 1, Qt::DotLine));
            const qreal by = ly + fm.ascent() - fm.xHeight() / 2.0;
            p->drawLine(QPointF(dotFrom, by), QPointF(dotTo, by));
        }
        ly += L.tocLineH;
    }
}

int DocxTextArea::tableAnchorOf(int i) const {
    if (!m_ctl || !m_ctl->ready() || i < 0 || i >= m_ctl->doc().blocks.size())
        return -1;
    const Block& b = m_ctl->doc().blocks.at(i);
    if (b.tableId < 0) return -1;
    return m_ctl->doc().tableFirstBlock(b.tableId);
}

void DocxTextArea::buildFlatTableLayout(int anchor) {
    const Document& d = m_ctl->doc();
    BlockLayout& A = m_lay[size_t(anchor)];
    const int tid = d.blocks.at(anchor).tableId;
    const TableDef& def = d.tables().at(tid);

    A.isTable = true;
    A.table.reset();
    A.pieces.clear();
    A.rows.clear();
    A.images.clear();
    A.isCell = false;

    const qreal avail = contentWidth();
    int maxCells = 1;
    for (int r = 0; r < def.rowSpans.size(); ++r) {
        const int firstCell = def.rowFirstCell.at(r);
        const int lastCell = (r + 1 < def.rowFirstCell.size())
                                 ? def.rowFirstCell.at(r + 1) - 1
                                 : def.cellSpans.size() - 1;
        int n = 0;
        for (int c = firstCell; c <= lastCell; ++c)
            n += qMax(1, def.cellGridSpan.value(c, 1));
        maxCells = qMax(maxCells, n);
    }
    QVector<qreal> colW;
    qreal sum = 0;
    for (int w : def.gridTw) { colW.append(w * kTwipToPx); sum += w * kTwipToPx; }
    if (sum > avail && sum > 0) {
        const qreal f = avail / sum;
        for (qreal& w : colW) w *= f;
    }
    while (colW.size() < maxCells)
        colW.append(avail / maxCells);

    auto tl = std::make_unique<TableLayout>();
    const qreal pad = 108.0 * kTwipToPx;
    qreal yRow = 0.0;
    int blockAt = anchor;

    for (int r = 0; r < def.rowSpans.size(); ++r) {
        RowLayout rl;
        rl.y = yRow;
        const int firstCell = def.rowFirstCell.at(r);
        const int lastCell = (r + 1 < def.rowFirstCell.size())
                                 ? def.rowFirstCell.at(r + 1) - 1
                                 : def.cellSpans.size() - 1;
        int colIdx = 0;
        qreal x = 0.0;
        for (int c = firstCell; c <= lastCell; ++c) {
            const int span = qBound(1, def.cellGridSpan.value(c, 1),
                                    qMax(1, colW.size() - colIdx));
            qreal w = 0.0;
            for (int k = 0; k < span && colIdx + k < colW.size(); ++k)
                w += colW.at(colIdx + k);
            if (w <= 0.0) w = avail / maxCells;

            CellLayout cl;
            cl.x = x;
            cl.w = w;

            const int colOfCell = c - firstCell;
            qreal yInCell = pad;
            while (blockAt < d.blocks.size()
                   && d.blocks.at(blockAt).tableId == tid
                   && d.blocks.at(blockAt).row == r
                   && d.blocks.at(blockAt).col == colOfCell) {
                BlockLayout& CB = m_lay[size_t(blockAt)];
                qreal ph = 0.0;
                {
                    const Block& cb = d.blocks.at(blockAt);
                    CB.beforePx = 0.0;
                    CB.indentPx = 0.0;
                    CB.marker.clear();
                    CB.textLen  = cb.textLength();
                    ph = buildInlineRows(CB, cb, blockBaseFont(cb, blockAt),
                                         d.resolvePar(cb), w - 2 * pad);
                    CB.isImage = (CB.images.size() == 1 && CB.textLen == 1);
                    CB.isText  = true;
                }
                CB.isCell   = true;
                CB.isTable  = false;
                CB.cellRow  = r;           // entscheidet über Seite und Stück
                CB.cellRelX = x + pad;
                CB.cellRelY = yRow + yInCell;
                CB.cellW    = w - 2 * pad;
                CB.height   = 0.0;         // Höhe trägt der Anker
                CB.beforePx = 0.0;
                CB.indentPx = 0.0;
                CB.laid     = true;
                yInCell += ph;
                ++blockAt;
            }
            cl.h = yInCell + pad;
            rl.cells.push_back(std::move(cl));
            x += w;
            colIdx += span;
        }
        qreal rowH = 20.0;
        for (const CellLayout& c2 : rl.cells) rowH = qMax(rowH, c2.h);
        rl.h = rowH;
        tl->width = qMax(tl->width, x);
        yRow += rowH;
        tl->rows.push_back(std::move(rl));
    }

    // Der Anker hat zwei Rollen: Gitter (isTable) und erster Absatz der ersten Zelle
    // (isCell). Die Zellschleife ueberschreibt seine Flags - daher hier wiederherstellen.
    A.isTable = true;
    A.height  = yRow + 6.0;
    A.table   = std::move(tl);
    A.laid    = true;
    shiftBelowForeignFloats(anchor);
    maybeFloatTable(anchor);
}

void DocxTextArea::shiftBelowForeignFloats(int i) {
    if (i < 0 || i >= int(m_lay.size())) return;
    BlockLayout& L = m_lay[size_t(i)];
    const qreal pad = foreignFloatBottom(i);
    L.topPad = qMax(0.0, pad);
    if (L.topPad <= 0.0) return;
    L.height += L.topPad;
    if (L.table)
        for (RowLayout& r : L.table->rows) r.y += L.topPad;
    if (!m_ctl || !m_ctl->ready()) return;
    const Document& d = m_ctl->doc();
    if (i >= d.blocks.size()) return;
    const int tid = d.blocks.at(i).tableId;
    if (tid < 0) return;
    for (int k = i; k < int(m_lay.size()) && k < d.blocks.size()
                    && d.blocks.at(k).tableId == tid; ++k)
        m_lay[size_t(k)].cellRelY += L.topPad;
}

void DocxTextArea::buildTableLayout(int i) {
    BlockLayout& L = m_lay[size_t(i)];
    L.table.reset();
    const Block& b = m_ctl->doc().blocks.at(i);
    //  Gezeichnet wird immer aus dem ROH-Bereich der Tabelle - egal ob sie flach
    //  zerlegt wurde (dann liefert das Gerüst den Bereich) oder als opaker Block
    //  vorliegt. So gibt es genau EINEN Tabellen-Renderer.
    Block src = b;
    if (b.tableId >= 0 && b.tableId < m_ctl->doc().tables().size()) {
        src = Block();
        src.kind = Block::OpaqueVisible;
        src.opaqueName = QStringLiteral("w:tbl");
        src.rawSpan = m_ctl->doc().tables().at(b.tableId).rawSpan();
    }
    const TableView tv = m_ctl->doc().parseTableForDisplay(src);
    if (!tv.ok) {
        L.isTable = false;
        L.height  = 34;
        L.laid    = true;
        return;
    }
    L.isTable = true;

    const qreal avail = contentWidth();
    int maxCells = 1;
    for (const TableRow& r : tv.rows) {
        int n = 0;
        for (const TableCell& c : r.cells) n += qMax(1, c.gridSpan);
        maxCells = qMax(maxCells, n);
    }
    QVector<qreal> colW;
    qreal totalTw = 0;
    for (int w : tv.gridTw) totalTw += w;
    if (!tv.gridTw.isEmpty() && totalTw > 0) {
        qreal sum = 0;
        for (int w : tv.gridTw) { colW.append(w * kTwipToPx); sum += w * kTwipToPx; }
        if (sum > avail && sum > 0) {
            const qreal f = avail / sum;
            for (qreal& w : colW) w *= f;
        }
    }
    while (colW.size() < maxCells)
        colW.append(avail / maxCells);

    auto tl = std::make_unique<TableLayout>();
    const qreal pad = 108.0 * kTwipToPx;          // Word-Standard-Zellrand
    qreal yRow = 0.0;
    for (const TableRow& row : tv.rows) {
        RowLayout rl;
        rl.y = yRow;
        int colIdx = 0;
        qreal x = 0.0;
        for (const TableCell& cell : row.cells) {
            const int span = qBound(1, cell.gridSpan, qMax(1, colW.size() - colIdx));
            qreal w = 0.0;
            for (int k = 0; k < span && colIdx + k < colW.size(); ++k)
                w += colW.at(colIdx + k);
            if (w <= 0.0) w = avail / maxCells;

            CellLayout cl;
            cl.x = x;
            cl.w = w;
            qreal h = pad;
            for (const Block& para : cell.paragraphs) {
                if (para.kind == Block::OpaqueVisible) {
                    cl.paras.push_back(nullptr);
                    cl.paraIsPlaceholder.push_back(true);
                    h += 22.0;
                    continue;
                }
                qreal ph = 0.0;
                cl.paras.push_back(layoutParagraph(para, w - 2 * pad, &ph));
                cl.paraIsPlaceholder.push_back(false);
                h += ph;
            }
            cl.h = h + pad;
            rl.cells.push_back(std::move(cl));
            x += w;
            colIdx += span;
        }
        qreal rowH = 20.0;
        for (const CellLayout& c : rl.cells) rowH = qMax(rowH, c.h);
        rl.h = rowH;
        tl->width = qMax(tl->width, x);
        yRow += rowH;
        tl->rows.push_back(std::move(rl));
    }

    L.height = yRow + 6.0;          // kleiner Abstand nach der Tabelle
    L.table  = std::move(tl);
    L.laid   = true;
    shiftBelowForeignFloats(i);
    maybeFloatTable(i);
}

// Bleibt neben der Tabelle eine lesbare Spalte und passt sie auf eine Seite, gibt sie ihre Flusshöhe ab und
// tritt als Störer auf. NICHT gleiten darf eine über Seiten getrennte Tabelle: ihre Stücke hängen an der Blockhöhe.
void DocxTextArea::maybeFloatTable(int i) {
    if (i < 0 || i >= int(m_lay.size())) return;
    BlockLayout& L = m_lay[size_t(i)];
    L.tableFloating = false;
    if (!L.isTable || !L.table || L.table->rows.empty()) return;
    if (L.table->width <= 0.0) return;

    const RowLayout& last = L.table->rows.back();
    const qreal gridH = last.y + last.h;                 // inkl. topPad
    const qreal strip = contentWidth() - L.table->width - kTableWrapGap;
    if (strip < kMinTextBesideImage) return;             // kein lesbarer Streifen
    if (gridH > slotHeight() * 0.9) return;              // passt nicht auf eine Seite

    L.tableFloating = true;
    L.floatOverhang = gridH;
    L.height = 6.0;
}

void DocxTextArea::paintTable(QPainter* p, const BlockLayout& L, qreal left, qreal y,
                              int rowFrom, int rowTo) {
    if (!L.table) return;
    const int nRows = int(L.table->rows.size());
    const int r0 = qBound(0, rowFrom, nRows);
    const int r1 = (rowTo < 0) ? nRows : qBound(r0, rowTo, nRows);
    const qreal pad = 108.0 * kTwipToPx;
    p->save();
    for (int ri = r0; ri < r1; ++ri) {
        const RowLayout& row = L.table->rows[size_t(ri)];
        for (const CellLayout& cell : row.cells) {
            const QRectF cr(left + cell.x, y + row.y, cell.w, row.h);
            p->setPen(QPen(QColor(140, 140, 148), 0.8));
            p->setBrush(Qt::NoBrush);
            p->drawRect(cr);

            qreal py = cr.y() + pad;
            for (size_t k = 0; k < cell.paras.size(); ++k) {
                if (k < cell.paraIsPlaceholder.size() && cell.paraIsPlaceholder[k]) {
                    p->setPen(QColor(120, 120, 128));
                    QFont f; f.setPointSizeF(8.5); f.setItalic(true);
                    p->setFont(f);
                    p->drawText(QPointF(cr.x() + pad, py + 14.0), m_tablePlaceholder);
                    py += 22.0;
                    continue;
                }
                const QTextLayout* lay = cell.paras[k].get();
                if (!lay) continue;
                lay->draw(p, QPointF(cr.x() + pad, py));
                qreal h = 0.0;
                for (int ln = 0; ln < lay->lineCount(); ++ln)
                    h = qMax(h, lay->lineAt(ln).y() + lay->lineAt(ln).height());
                py += h;
            }
        }
    }
    p->restore();
}

void DocxTextArea::ensureLaid(int i) {
    if (!m_ctl || i < 0 || i >= int(m_lay.size())) return;
    //  Ein Zellblock ohne Layout heißt: der Anker seiner Tabelle ist nicht (mehr)
    //  ausgelegt. Der Anker baut ALLE Zellen - einzeln ginge es nicht, weil die
    //  Zeilenhöhe von den Nachbarzellen abhängt.
    {
        const int anchor = tableAnchorOf(i);
        if (anchor >= 0 && anchor != i) {
            //  Ein BILD-Absatz in der Zelle hat bewusst kein QTextLayout - ohne
            //  diese Ausnahme würde hier bei jedem Aufruf die ganze Tabelle neu
            //  ausgelegt (paint ruft ensureLaid je Block).
            if (!m_lay[size_t(anchor)].table || m_lay[size_t(i)].trimmed)
                ensureLaid(anchor);
            return;
        }
    }
    if (!m_lay[i].laid || m_lay[i].trimmed
        || (m_lay[i].isTable && !m_lay[i].table)) {
        const qreal oldH    = m_lay[i].height;
        const qreal oldOver = m_lay[i].floatOverhang;
        buildLayout(i);
        if (!qFuzzyCompare(oldH + 1, m_lay[i].height + 1)) {
            m_offsetsValidTo = qMin(m_offsetsValidTo, i);
            m_offsetsDirtyMax = qMax(m_offsetsDirtyMax, i);
        }
        //  Der Überstand eines verankerten Bildes bestimmt das Layout der
        //  FOLGENDEN Blöcke mit - ändert er sich, sind deren Zeilen veraltet.
        //  Nur vorwärts markieren (kein Zyklus), das Neuauslegen bleibt lazy.
        if (!qFuzzyCompare(oldOver + 1, m_lay[i].floatOverhang + 1)) {
            invalidateFloatFollowers(i, qMax(oldOver, m_lay[i].floatOverhang));
            m_offsetsValidTo = qMin(m_offsetsValidTo, i);
            m_offsetsDirtyMax = qMax(m_offsetsDirtyMax, i);
        }
    }
}

// Verteilt EINEN Block auf Spalten-Slots, Rückgabe ist das Fluss-y danach. Ein Absatz über der Slot-Grenze wird
// ZEILENWEISE getrennt - das macht die Ansicht seitengenau; ein untrennbarer wandert ganz in den nächsten Slot.
qreal DocxTextArea::paginateBlock(int idx, qreal flowStart, qreal slotH) {
    // Ein Block über der Slot-Grenze muss zeilenweise getrennt werden KÖNNEN - dafür braucht er sein Layout. Hinge
    // das daran, ob `trimLayouts` es gerade freigab, wäre die Seitenzahl je Lauf eine andere (18 gegen 19 Seiten).
    {
        const BlockLayout& L0 = m_lay[size_t(idx)];
        const int slot0 = int(flowStart / slotH);
        const qreal yInSlot0 = flowStart - slot0 * slotH;
        // Maßgeblich ist, ob die ZEILEN vorliegen - nicht, ob der Block je vermessen wurde: `trimLayouts` leert `rows`,
        // lässt `laid` aber stehen. Für eine Tabelle gilt dasselbe eine Ebene höher, ohne Gitter kennt sie keine Zeilengrenzen.
        const bool needsGrid = L0.isTable && !L0.table;
        const bool needsLines = (!hasText(L0) || L0.rows.empty())
                                && m_ctl && m_ctl->ready()
                                && m_ctl->doc().blocks.at(idx).kind == Block::Paragraph;
        if (L0.height > 0.0 && (needsGrid || needsLines)
            && yInSlot0 + L0.height > slotHeight()
            && m_ctl && m_ctl->ready())
            ensureLaid(idx);
    }
    BlockLayout& L = m_lay[size_t(idx)];
    L.segs.clear();
    const int nCols = colCount();
    int   slot = int(flowStart / slotH);
    qreal y    = flowStart - slot * slotH;
    const qreal h = L.height;

    if (h <= 0.0) {
        L.segs.append({ slot, 0, y });
        return flowStart;
    }

    // Eine gleitende Tabelle gibt ihre Flusshöhe ab, MUSS aber als Ganzes auf die Seite passen - sonst wüchse sie
    // über den Papierrand. Passt sie nicht, rückt sie ganz auf die nächste Seite; getrennt wird sie nie.
    if (L.tableFloating) {
        if (y > 0.0 && y + L.floatOverhang > slotHeight()) {
            ++slot;
            y = 0.0;
        }
        L.segs.append({ slot, 0, y });
        return slot * slotH + y + h;
    }

    // Das Inhaltsverzeichnis beginnt oben auf einer frischen Seite, belegt sie allein und schiebt den Text danach
    // weiter. Passt es nicht auf eine Seite, läuft es seitenweise weiter.
    if (L.isToc) {
        const int pageSlots = qMax(1, nCols);
        // An den Seitenanfang nur, wenn auf dieser Seite schon etwas Sichtbares steht: ein leerer Absatz davor hat Höhe,
        // aber keine Tinte - ohne die Prüfung schöbe er das Verzeichnis auf Seite 2 und ließe Seite 1 leer.
        if (!(slot % pageSlots == 0 && qFuzzyIsNull(y))
            && pageHasInkBefore(idx, slot / pageSlots)) {
            slot = ((slot / pageSlots) + 1) * pageSlots;
            y = 0.0;
        }
        const int entries = qMax(1, int(L.tocEntries.size()));
        const int perPage = qMax(1, L.tocPerPage);
        int firstEntry = 0;
        int usedPages  = 0;
        while (firstEntry < entries) {
            L.segs.append({ slot + usedPages * pageSlots, firstEntry, 0.0 });
            firstEntry += perPage;
            ++usedPages;
        }
        return qreal(slot + usedPages * pageSlots) * slotH;
    }

    const int nLines = lineCount(L);
    const bool hasExplicitBreak = L.hasBreak;
    //  Der Absatztext wird nur noch dort gebraucht, wo wirklich ein
    //  erzwungener Umbruch drinsteht - er kostet sonst je Block und je
    //  Tastendruck eine Zeichenkette samt Suchlauf.
    QString text;
    if (hasExplicitBreak) text = blockText(L);

    // Gemessen wird gegen die KAPAZITÄT des Slots (Höhe minus Fußnotenbereich) - die Slot-HÖHE selbst bleibt
    // uniform, sonst bräche die Fluss-Invariante `Fluss-y = slot * slotHeight() + y`.
    if (y + h <= slotHeight() && !hasExplicitBreak) {
        L.segs.append({ slot, 0, y });
        return slot * slotH + y + h;
    }

    // Tabelle an ZEILENgrenzen trennen: der Anker trägt sie in EINEM Block und wäre für die Zeilen-Logik einzeilig,
    // also "nicht trennbar". Ohne diesen Zweig reserviert der Fluss den Platz, gezeichnet würde nur das erste Stück.
    if (segCountsTableRows(L)) {
        const std::vector<RowLayout>& rows = L.table->rows;
        const int nRows = int(rows.size());
        L.segs.append({ slot, 0, y });
        qreal segTop = y;
        qreal segFirstY = 0.0;              // Zeile 0 liegt bei y = 0
        for (int r = 0; r < nRows; ++r) {
            const qreal rTop = segTop + (rows[size_t(r)].y - segFirstY);
            const qreal rBot = rTop + rows[size_t(r)].h;
            const bool first = (r == segFirstRow(L, L.segs.constLast()));
            if (rBot > slotHeight() && !(first && segTop <= 0.0)) {
                ++slot;
                segTop = 0.0;
                segFirstY = rows[size_t(r)].y;
                if (first) L.segs.last() = { slot, r, segTop };
                else       L.segs.append({ slot, r, segTop });
            }
        }
        const PageSeg& lastSeg = L.segs.constLast();
        const qreal tableBottom = rows.back().y + rows.back().h;
        const qreal trailing = qMax(0.0, h - tableBottom);
        return lastSeg.slot * slotH + lastSeg.yInSlot
               + (tableBottom - segOriginY(L, lastSeg)) + trailing;
    }

    if (nLines <= 1) {
        if (y > 0.0) { ++slot; y = 0.0; }
        L.segs.append({ slot, 0, y });
        return slot * slotH + y + h;
    }

    L.segs.append({ slot, 0, y });
    qreal segTop = y;
    qreal segFirstY = 0.0;                 // Zeile 0 liegt bei y = beforePx
    for (int li = 0; li < nLines; ++li) {
        const qreal lnY   = lineTop(L, li);
        const qreal lnTop = segTop + (lnY - segFirstY);
        const qreal lnBot = lnTop + lineHeight(L, li);
        const bool first  = (li == segFirstLine(L, L.segs.constLast()));
        if (lnBot > slotHeight() && !(first && segTop <= 0.0)) {
            ++slot;
            segTop = 0.0;
            segFirstY = lnY;
            if (first) L.segs.last() = { slot, li, segTop };
            else       L.segs.append({ slot, li, segTop });
        }
        if (hasExplicitBreak && li + 1 < nLines) {
            int st = 0, len = 0;
            lineTextRange(L, li, &st, &len);
            const int at = text.indexOf(kPageBreak, st);
            if (at >= 0 && at < st + len) {
                slot = (slot / nCols + 1) * nCols;
                segTop = 0.0;
                segFirstY = lineTop(L, li + 1);
                L.segs.append({ slot, li + 1, segTop });
            }
        }
    }

    const qreal layoutBottom = linesBottom(L);
    const qreal trailing = qMax(0.0, h - layoutBottom);
    const PageSeg& last = L.segs.constLast();
    return last.slot * slotH + last.yInSlot
           + (layoutBottom - segOriginY(L, last)) + trailing;
}

bool DocxTextArea::pageHasInkBefore(int idx, int page) const {
    if (!m_ctl || !m_ctl->ready()) return true;   // im Zweifel wie bisher
    const Document& d = m_ctl->doc();
    const int pageSlots = qMax(1, colCount());
    for (int k = idx - 1; k >= 0 && k < int(m_lay.size()); --k) {
        const BlockLayout& P = m_lay[size_t(k)];
        if (P.segs.isEmpty()) continue;
        if (P.segs.constLast().slot / pageSlots < page) break;   // frühere Seite
        if (k >= int(d.blocks.size())) continue;
        const Block& b = d.blocks.at(k);
        if (b.kind == Block::OpaqueHidden) continue;
        if (b.kind != Block::Paragraph || b.tableId >= 0 || b.textLength() > 0)
            return true;
    }
    return false;
}

void DocxTextArea::ensureOffsetsTo(int i) {
    i = qMin(i, int(m_lay.size()));
    if (m_offsets.size() != int(m_lay.size()) + 1) {
        m_offsets.resize(int(m_lay.size()) + 1);
        m_offsetsValidTo = 0;
        m_offsetsHighWater = 0;      // Indizes verschoben - nichts ist mehr vergleichbar
        m_offsetsDirtyMax = 0;
    }
    if (m_offsets.isEmpty()) return;
    const qreal slotH = slotHeight();
    if (m_offsetsValidTo == 0) {
        m_offsets[0] = 0.0;
        if (!m_lay.empty()) m_offsets[1] = paginateBlock(0, 0.0, slotH);
        m_offsetsValidTo = qMin(1, int(m_lay.size()));
    }
    // Liefert ein Block wieder exakt denselben Fluss-Ausgang wie zuvor, sind alle
    // folgenden Ergebnisse bitgleich - dann wird bis zum Hochwasserstand uebersprungen.
    // Verglichen wird exakt, kein qFuzzyCompare: ein Bruchteil Pixel ist eine Verschiebung.
    int k = m_offsetsValidTo + 1;
    while (k <= i) {
        const qreal before = m_offsets[k];
        m_offsets[k] = paginateBlock(k - 1, m_offsets[k - 1], slotH);
        if (k < m_offsetsHighWater && k - 1 >= m_offsetsDirtyMax
            && m_offsets[k] == before) {
            m_offsetsValidTo = m_offsetsHighWater;
            m_offsetsDirtyMax = 0;
            k = m_offsetsHighWater + 1;
            continue;
        }
        ++k;
    }
    m_offsetsValidTo = qMax(m_offsetsValidTo, i);
    m_offsetsHighWater = qMax(m_offsetsHighWater, m_offsetsValidTo);
    if (m_offsetsValidTo >= int(m_lay.size()))
        m_offsetsDirtyMax = 0;        // der Lauf ist durch - nichts steht mehr aus

    if (m_offsetsValidTo >= int(m_lay.size()) && !m_lay.empty()) {
        const PageSeg last = m_lay.back().segs.isEmpty()
                                 ? PageSeg() : m_lay.back().segs.constLast();
        const int pages = qMax(1, last.slot / colCount() + 1);
        if (pages != m_pageCount) {
            m_pageCount = pages;
            emit pageCountChanged();
        }
    }
}

qreal DocxTextArea::blockTop(int i) {
    ensureOffsetsTo(i);
    return (i >= 0 && i < m_offsets.size()) ? m_offsets[i] : kPadV;
}

int DocxTextArea::blockAtY(qreal y) {
    if (m_lay.empty()) return -1;
    ensureOffsetsTo(int(m_lay.size()));
    int lo = 0, hi = int(m_lay.size()) - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (m_offsets[mid] <= y) lo = mid; else hi = mid - 1;
    }
    return lo;
}

void DocxTextArea::startChunkLayout() {
    if (m_chunkTimer.isActive()) return;
    m_chunkTimer.start();
    emit layoutBusyChanged();
}

void DocxTextArea::stopChunkLayout() {
    if (!m_chunkTimer.isActive()) return;
    m_chunkTimer.stop();
    emit layoutBusyChanged();
}

void DocxTextArea::layoutChunk() {
    if (!m_ctl || !m_ctl->ready()) { stopChunkLayout(); return; }
    int done = 0;
    while (m_layChunkAt < int(m_lay.size()) && done < kChunk) {
        // Über `ensureLaid`, nicht direkt über `buildLayout`: nur `ensureLaid` vergleicht die Schätzhöhe mit der
        // vermessenen und macht die Präfix-Offsets ungültig. Ohne das lag das Dokument beim ersten Öffnen zusammengeschoben da.
        if (!m_lay[m_layChunkAt].laid) { ensureLaid(m_layChunkAt); ++done; }
        ++m_layChunkAt;
    }
    updateContentHeight();
    if (m_layChunkAt >= int(m_lay.size())) {
        stopChunkLayout();
        updateCursorRect();
    }
    // Schon WÄHREND des Initial-Layouts trimmen: sonst lägen bei einem 400-Seiten-Dokument kurzzeitig alle Layouts
    // gleichzeitig im Speicher. `m_trimLo` wird zurückgesetzt, damit die neu gebauten Layouts erfasst werden.
    m_trimLo = -1;
    trimLayouts(blockAtY(m_contentY), blockAtY(m_contentY + height()));
    update();
}

void DocxTextArea::trimLayouts(int firstVisible, int lastVisible) {
    const int n = int(m_lay.size());
    if (n <= kLayoutCap)
        return;                       // kleine Dokumente zahlen hier gar nichts

    const int lo = qMax(0, firstVisible - kKeepMargin);
    const int hi = qMin(n - 1, lastVisible + kKeepMargin);
    if (lo == m_trimLo && hi == m_trimHi)
        return;                       // Fenster unveraendert -> nichts zu tun
    m_trimLo = lo;
    m_trimHi = hi;

    //  Cursor-Block ausnehmen: updateCursorRect/moveCursorVertical brauchen sein
    //  Layout bei jedem Tastendruck - ihn wegzuwerfen kostete mehr, als er belegt.
    const int cursorBlock = (m_ctl && m_ctl->ready()) ? m_ctl->cursor().block : -1;
    for (int i = 0; i < n; ++i) {
        if ((i >= lo && i <= hi) || i == cursorBlock)
            continue;
        // Nur das Layout freigeben - laid/height bleiben gueltig. Die Zeilenbaender bleiben
        // stehen (64 Byte je Zeile): ohne sie musste paginateBlock an jeder Seitengrenze neu
        // shapen, gemessen 8 ms bei 218 und 26 ms bei 648 Seiten je Umbruch-Verschiebung.
        m_lay[i].pieces.clear();
        m_lay[i].table.reset();
        //  Das eingepasste Bild ist der groesste Einzelposten je Block. Die
        //  KASTENMASSE bleiben stehen, die Hoehe aendert sich also nicht.
        for (ImageBox& B : m_lay[i].images) B.img = QImage();
        m_lay[i].trimmed = true;
    }
}

// Höhe EINER Zeile der Grundschrift - ein Textdokument scrollt in ZEILEN. Ein Anteil der Fensterhöhe war
// viermal so weit wie in jedem Texteditor und hing zudem an der Fenstergröße.
qreal DocxTextArea::lineStep() const {
    qreal h = 0.0;
    if (m_ctl && m_ctl->ready()) {
        const Docx::RunFmt def = m_ctl->doc().defaultRun();
        QFont f;
        if (!def.font.isEmpty()) f.setFamily(def.font);
        if (def.sizePt > 0) f.setPointSizeF(def.sizePt);
        h = QFontMetricsF(f).height();
    }
    if (h <= 1.0) h = 20.0;                    // vor dem Laden: Notwert
    return h * m_scale;
}

void DocxTextArea::updateContentHeight() {
    ensureOffsetsTo(int(m_lay.size()));
    // Fußnoten setzen die Kapazität der Seiten herab und ändern damit die Seitenzahl; der Mehrpass läuft hier, weil
    // hier ohnehin ganz paginiert wird. Scrollbare Höhe = SEITENSTAPEL, nicht Fluss-Höhe: die kennt keine Ränder.
    const qreal h = docHeight() * m_scale;
    if (!qFuzzyCompare(m_contentHeight + 1, h + 1)) {
        m_contentHeight = h;
        emit contentHeightChanged();
        setContentY(m_contentY);
    }
}

void DocxTextArea::invalidateEmptyBlock(int i) {
    if (!m_ctl || !m_ctl->ready() || i < 0 || i >= int(m_lay.size()))
        return;
    const Block& b = m_ctl->doc().blocks.at(i);
    if (b.kind != Block::Paragraph || b.textLength() != 0)
        return;
    m_lay[i].laid = false;
    ensureLaid(i);
    updateContentHeight();
}

void DocxTextArea::updateCursorRect() {
    if (!m_ctl || !m_ctl->ready() || m_lay.empty()) {
        m_cursorRect = QRectF();
        emit cursorRectChanged();
        return;
    }
    const DocxCursor& c = m_ctl->cursor();
    const int bi = qBound(0, c.block, int(m_lay.size()) - 1);
    ensureLaid(bi);
    const BlockLayout& L = m_lay[bi];

    // Höhe und Grundlinie aus dem am Cursor WIRKSAMEN Zeichenformat statt aus der Zeilenhöhe: die Zeile ist so hoch
    // wie ihr größtes Zeichen, der Caret muss aber die Größe des NÄCHSTEN zeigen.
    const RunFmt cf = m_ctl->caretFormat();
    QFont f;
    f.setFamily(cf.font.isEmpty() ? m_ctl->doc().defaultRun().font : cf.font);
    f.setPointSizeF(cf.sizePt > 0 ? cf.sizePt : m_ctl->doc().defaultRun().sizePt);
    f.setBold(cf.bold);
    f.setItalic(cf.italic);
    const QFontMetricsF fm(f);

    const int li = lineForPos(L, c.pos);
    qreal x = docXForBlock(bi, li);
    qreal y = docYForLine(bi, 0) + L.beforePx;
    qreal h = fm.height();
    if (lineCount(L) == 0) {
        // Ein LEERER Absatz neben einem Störer hat keine Zeile - `xForPos` greift nicht, und der Caret stünde an der
        // linken Slotkante, also mitten über dem Störer. Gefragt wird dieselbe Quelle, aus der das Layout seine Bänder baut.
        const QVector<FloatObstacle> obs = foreignFloats(bi);
        const qreal W = contentWidth();
        qreal left = 0.0;
        for (const FloatObstacle& o : obs) {
            if (0.0 >= o.y + o.h || h <= o.y) continue;
            const qreal bl = o.x - o.padL;              // linke Kante inkl. Luft
            const qreal br = o.x + o.w + o.padR;        // rechte Kante inkl. Luft
            const int side = o.wrapSide;
            if (side == Docx::InlineImage::SideLeft)
                continue;                               // Text läuft LINKS davon
            if (side == Docx::InlineImage::SideRight) {
                left = qMax(left, br);                  // Text läuft RECHTS davon
                continue;
            }
            if (side == Docx::InlineImage::SideBoth)
                continue;                               // beidseitig: linkes Band
            if (bl <= W - br)
                left = qMax(left, br);
        }
        x += left;
    }
    if (lineCount(L) > 0) {
        x += xForPos(L, li, c.pos);
        //  An der GRUNDLINIE der Zeile ausrichten (nicht an der Bandoberkante)
        //  - sonst „schwebt" ein kleiner Caret in einer hohen Mischzeile, und
        //  neben einem Bild stünde er oben, während der Text unten erscheint.
        y = docYForLine(bi, li) + (lineTextTop(L, li) - lineTop(L, li))
            + lineAscent(L, li) - fm.ascent();
    }
    m_cursorRect = QRectF(x, y, 1.6, h);
    emit cursorRectChanged();
    updateImageSelection();
    if (currentPage() != m_lastPage) {
        m_lastPage = currentPage();
        emit currentPageChanged();
        emit pageGeometryChanged();
    }
}

// Ausgewählt ist ein Bild, wenn der Absatz nur aus ihm besteht oder die Selektion genau sein Objekt-Zeichen
// deckt. Das Rechteck steht in DOKUMENT-Pixeln und wird erst in den Gettern umgerechnet - wie beim Caret.
void DocxTextArea::updateImageSelection() {
    int b = -1;
    QRectF r;
    if (m_ctl && m_ctl->ready() && !m_lay.empty()) {
        const DocxCursor& c = m_ctl->cursor();
        const int bi = qBound(0, c.block, int(m_lay.size()) - 1);
        ensureLaid(bi);
        const BlockLayout& L = m_lay[size_t(bi)];
        int k = -1;
        if (L.isImage && !L.images.empty()) {
            k = 0;                                   // der Absatz IST das Bild
        } else if (c.aBlock == c.block && qAbs(c.pos - c.aPos) == 1) {
            k = imageAtPos(L, qMin(c.pos, c.aPos));  // genau ein Bild markiert
        }
        if (k >= 0 && k < int(L.images.size())) {
            const ImageBox& B = L.images[size_t(k)];
            const int row = lineForPos(L, B.pos);
            b = bi;
            r = QRectF(docXForBlock(bi, row) + B.x,
                       docYForLine(bi, row) + (B.y - lineTop(L, row)),
                       B.w, B.h);
        }
    }
    // Tabelle: JE SEITENSTÜCK ein Rechteck. Eines aus "Lage des ersten Stücks + Gesamthöhe" säße bei einer
    // getrennten Tabelle in der Luft. Gerechnet wird aus den ZEILEN des Stücks, wie `paintSlot` zeichnet.
    int tid = -1;
    QRectF tr;
    QVector<QRectF> tsegs;
    if (m_ctl && m_ctl->ready() && !m_lay.empty()) {
        const int bi = qBound(0, m_ctl->cursor().block, int(m_lay.size()) - 1);
        const int anchor = tableAnchorOf(bi);
        if (anchor >= 0 && anchor < int(m_lay.size())) {
            ensureLaid(anchor);
            const BlockLayout& A = m_lay[size_t(anchor)];
            if (A.isTable && A.table && A.table->width > 0.0) {
                tid = m_ctl->doc().blocks.at(anchor).tableId;
                int primary = 0;
                {
                    const BlockLayout& C = m_lay[size_t(bi)];
                    const int s = C.isCell ? tableSegOfRow(A, C.cellRow) : -1;
                    if (s >= 0) primary = s;
                }
                for (int s = 0; s < A.segs.size(); ++s) {
                    int rowFrom = 0, rowTo = 0;
                    tableSegRows(A, s, &rowFrom, &rowTo);
                    if (rowTo <= rowFrom) continue;
                    const QPointF org = tableSegOrigin(A, s);
                    const RowLayout& rTop = A.table->rows[size_t(rowFrom)];
                    const RowLayout& rBot = A.table->rows[size_t(rowTo - 1)];
                    const QRectF sr(org.x(), org.y() + rTop.y, A.table->width,
                                    qMax(1.0, rBot.y + rBot.h - rTop.y));
                    tsegs.append(sr);
                    if (s == primary) tr = sr;
                }
                if (tsegs.isEmpty()) {
                    tr = QRectF(flowDocXForBlock(anchor, 0), flowDocYForLine(anchor, 0),
                                A.table->width, qMax(1.0, A.height - A.topPad - 6.0));
                    tsegs.append(tr);
                } else if (tr.isNull()) {
                    tr = tsegs.constFirst();
                }
            }
        }
    }
    if (b == m_imgSelBlock && r == m_imgSelDoc && tid == m_tblSelId && tr == m_tblSelDoc
        && tsegs == m_tblSelSegs)
        return;
    m_imgSelBlock = b;
    m_imgSelDoc   = r;
    m_tblSelId    = tid;
    m_tblSelDoc   = tr;
    m_tblSelSegs  = std::move(tsegs);
    emit imageSelectionChanged();
}

int   DocxTextArea::selTableId() const { return m_tblSelId; }
qreal DocxTextArea::selTableX() const {
    return itemOffsetX() + m_tblSelDoc.x() * m_scale;
}
qreal DocxTextArea::selTableY() const {
    return m_tblSelDoc.y() * m_scale - m_contentY;
}
qreal DocxTextArea::selTableW() const { return m_tblSelDoc.width() * m_scale; }
qreal DocxTextArea::selTableH() const { return m_tblSelDoc.height() * m_scale; }

// Gebaut wird erst beim Lesen: die Liste hängt an Maßstab und contentY und wird beim Scrollen neu ausgewertet -
// gehalten werden nur die Dokument-Rechtecke.
QVariantList DocxTextArea::selTableRects() const {
    QVariantList out;
    out.reserve(m_tblSelSegs.size());
    for (const QRectF& s : m_tblSelSegs) {
        QVariantMap m;
        m.insert(QStringLiteral("x"), itemOffsetX() + s.x() * m_scale);
        m.insert(QStringLiteral("y"), s.y() * m_scale - m_contentY);
        m.insert(QStringLiteral("w"), s.width() * m_scale);
        m.insert(QStringLiteral("h"), s.height() * m_scale);
        out.append(m);
    }
    return out;
}

qreal DocxTextArea::itemOffsetX() const {
    return qMax(kSideMargin * m_scale, (width() - pageWpx() * m_scale) / 2.0);
}

int   DocxTextArea::selImageBlock() const { return m_imgSelBlock; }
qreal DocxTextArea::selImageX() const {
    return itemOffsetX() + m_imgSelDoc.x() * m_scale;
}
qreal DocxTextArea::selImageY() const {
    return m_imgSelDoc.y() * m_scale - m_contentY;
}
qreal DocxTextArea::selImageW() const { return m_imgSelDoc.width() * m_scale; }
qreal DocxTextArea::selImageH() const { return m_imgSelDoc.height() * m_scale; }

void DocxTextArea::setPageNumberPos(int pos) {
    pos = qBound(0, pos, 3);
    if (m_pageNumberPos == pos) return;
    m_pageNumberPos = pos;
    emit pageNumberChanged();
    emit documentChanged();      // die Miniaturen tragen sie auch
    update();
}

void DocxTextArea::setPageNumberStyle(int style) {
    style = qBound(0, style, 1);
    if (m_pageNumberStyle == style) return;
    m_pageNumberStyle = style;
    emit pageNumberChanged();
    emit documentChanged();
    update();
}

// Die Seitenzahl sitzt in festem Abstand über der Blattkante (~9 mm auf A4), nicht relativ zum Satzspiegel -
// sonst fiele sie auf die Kante. Alle Maße sind Anteile des BLATTS: Bildschirm, Miniatur und PDF sehen gleich aus.
void DocxTextArea::paintPageNumber(QPainter* p, const QRectF& sheet, int page,
                                   int total, int pos, int style) const {
    if (pos <= 0 || sheet.height() <= 0.0) return;
    const QString text = (style == 0) ? QString::number(page + 1)
                                      : QStringLiteral("%1 / %2").arg(page + 1)
                                            .arg(qMax(1, total));
    const qreal numY   = sheet.bottom() - sheet.height() * 0.030;
    const qreal numPad = sheet.width() * 0.06;
    QFont numFont;
    //  9 pt bei 300 dpi = 0,0107 der A4-Höhe; als ANTEIL gerechnet ist die Zahl
    //  in jeder Auflösung gleich groß (`setPointSizeF` wäre an die dpi des
    //  Malgeräts gebunden und in der Miniatur riesig).
    numFont.setPixelSize(qMax(1, qRound(sheet.height() * 0.0107)));
    p->save();
    p->setFont(numFont);
    p->setPen(QColor(120, 120, 120));
    const QRectF line(sheet.left() + numPad, numY - sheet.height() * 0.03,
                      sheet.width() - 2 * numPad, sheet.height() * 0.06);
    const Qt::Alignment h = (pos == 1) ? Qt::AlignLeft
                          : (pos == 3) ? Qt::AlignRight
                                       : Qt::AlignHCenter;
    p->drawText(line, int(h | Qt::AlignVCenter), text);
    p->restore();
}

void DocxTextArea::paint(QPainter* p) {
    if (!m_ctl || !m_ctl->ready())
        return;
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    p->fillRect(QRectF(0, 0, width(), height()), m_surroundColor);

    ensureOffsetsTo(int(m_lay.size()));

    const qreal s = m_scale;
    const qreal offX = qMax(kSideMargin * s, (width() - pageWpx() * s) / 2.0);
    p->save();
    p->translate(offX, -m_contentY);
    p->scale(s, s);

    const qreal viewTop = m_contentY / s;
    const qreal viewBot = (m_contentY + height()) / s;

    const int firstPage = qBound(0, int((viewTop - kPadV) / (pageHpx() + kPageGap)),
                                 qMax(0, m_pageCount - 1));
    const int nCols = colCount();
    for (int page = firstPage; page < m_pageCount; ++page) {
        const qreal pTop = pageDocY(page);
        if (pTop > viewBot) break;
        if (pTop + pageHpx() < viewTop) continue;

        const QRectF paper(0, pTop, pageWpx(), pageHpx());
        p->fillRect(paper.translated(3.0 / s, 3.0 / s), QColor(0, 0, 0, 45));
        p->fillRect(paper, QColor(255, 255, 255));
        p->setPen(QColor(0, 0, 0, 45));
        p->drawRect(paper);

        for (int c = 0; c < nCols; ++c) {
            paintSlot(p, page * nCols + c, true);
        }
        paintPageNumber(p, paper, page, m_pageCount, m_pageNumberPos,
                        m_pageNumberStyle);
    }
    p->restore();

    const int fv = blockAtY(qMax(0.0, viewTop - pageDocY(firstPage)) + firstPage * nCols * slotHeight());
    trimLayouts(qMax(0, fv - nCols * 2), qMin(int(m_lay.size()) - 1, fv + nCols * 8));
}

// Zeichnet die Blöcke EINES Spalten-Slots, geclippt auf dessen Textbereich. Die Wellenlinie läuft je ZEILE,
// damit ein gebrochenes Wort mehrfach unterstrichen wird; die Autorenfarbe ist aus dem Namen abgeleitet und gedeckt.
QColor DocxTextArea::revisionColor(const QString& author) {
    static const QColor kPalette[] = {
        QColor(180,  40,  40), QColor( 30,  90, 170), QColor( 20, 120,  70),
        QColor(140,  70, 160), QColor(180, 100,  20), QColor( 60, 110, 130),
    };
    constexpr int n = int(sizeof(kPalette) / sizeof(kPalette[0]));
    if (author.isEmpty()) return kPalette[0];
    return kPalette[qAbs(qHash(author)) % n];
}

void DocxTextArea::paintSpell(QPainter* p, const BlockLayout& L, int blockIdx,
                              const QPointF& origin) {
    if (!m_ctl || !m_ctl->spellAvailable() || !hasText(L)) return;
    const QVector<mg::SpellRange>& bad = m_ctl->spellRanges(blockIdx);
    if (bad.isEmpty()) return;

    p->save();
    QPen pen(QColor(210, 40, 40));
    pen.setWidthF(1.0);
    p->setPen(pen);
    for (const mg::SpellRange& r : bad) {
        const int from = r.start;
        const int to   = r.start + r.length;
        int li = lineForPos(L, from);
        while (li >= 0 && li < lineCount(L)) {
            int ls = 0, ll = 0;
            lineTextRange(L, li, &ls, &ll);
            const int a = qMax(from, ls);
            const int b = qMin(to, ls + ll);
            if (b > a) {
                const qreal x0 = origin.x() + xForPos(L, li, a);
                const qreal x1 = origin.x() + xForPos(L, li, b);
                const qreal yy = origin.y() + lineTextTop(L, li)
                                 + lineAscent(L, li) + 2.0;
                //  Wellenlinie aus kurzen Strichen - eine gepunktete Linie wäre
                //  von einer Unterstreichung nicht zu unterscheiden.
                const qreal step = 2.0;
                bool up = true;
                QPolygonF wave;
                for (qreal x = x0; x <= x1; x += step, up = !up)
                    wave << QPointF(x, up ? yy : yy + 1.6);
                if (wave.size() >= 2) p->drawPolyline(wave);
            }
            if (ls + ll >= to) break;
            ++li;
        }
    }
    p->restore();
}

void DocxTextArea::paintSlot(QPainter* p, int slot, bool withCaret,
                             bool withSelection, bool withSpell) {
    const Document& d = m_ctl->doc();
    const qreal slotH = slotHeight();
    const qreal sx = slotDocX(slot);
    const qreal sy = slotDocY(slot);
    const qreal cw = contentWidth();

    p->save();
    p->setClipRect(QRectF(sx - marLpx(), sy, pageWpx(), slotH), Qt::IntersectClip);

    const qreal flowLo = slot * slotH;
    const qreal flowHi = flowLo + slotH;
    int i = blockAtY(flowLo);
    if (i < 0) { p->restore(); return; }
    // Zellblöcke tragen keine Höhe: fällt die Slot-Oberkante genau auf das Ende einer Tabelle, liefert die
    // Fluss-Suche einen von ihnen - der Anker läge vor dem Startindex und die Tabelle bliebe ungezeichnet.
    {
        const int a = tableAnchorOf(i);
        if (a >= 0 && a < i) i = a;
    }

    int b1, p1, b2, p2;
    const DocxCursor& cur = m_ctl->cursor();
    b1 = cur.aBlock; p1 = cur.aPos; b2 = cur.block; p2 = cur.pos;
    if (b1 > b2 || (b1 == b2 && p1 > p2)) { std::swap(b1, b2); std::swap(p1, p2); }
    const bool hasSel = cur.hasSelection() && withSelection;
    const QColor selBg(38, 118, 216, 110);

    for (; i < int(m_lay.size()); ++i) {
        ensureLaid(i);
        const qreal flowTop = blockTop(i);
        if (flowTop >= flowHi) break;
        const BlockLayout& L = m_lay[i];
        if (L.height <= 0)
            continue;
        // Ob ein Block hierher gehört, entscheiden ALLEIN seine Segmente, nicht seine Fluss-Lage: ein Inhaltsverzeichnis
        // springt auf den nächsten Seitenanfang, sein Fluss-y bleibt davor - eine Abkürzung darüber verschluckte es.
        int segIdx = -1;
        for (int k = 0; k < L.segs.size(); ++k)
            if (L.segs.at(k).slot == slot) { segIdx = k; break; }
        if (segIdx < 0)
            continue;
        const PageSeg& seg = L.segs.at(segIdx);
        const Block& b = d.blocks.at(i);
        const qreal y = sy + seg.yInSlot - segOriginY(L, seg);
        const qreal left = sx;

        if (L.isCell && !L.isTable)
            continue;
        if (L.isTable && L.table) {
            int rowFrom = 0, rowTo = -1;
            tableSegRows(L, segIdx, &rowFrom, &rowTo);
            paintTable(p, L, left, y, rowFrom, rowTo);
            const Block& ab = d.blocks.at(i);
            if (ab.tableId >= 0) {
                const int tid = ab.tableId;
                for (int k = i; k < int(m_lay.size())
                                && k < d.blocks.size()
                                && d.blocks.at(k).tableId == tid; ++k) {
                    const BlockLayout& CB = m_lay[size_t(k)];
                    if (!CB.isCell || !hasText(CB)) continue;
                    if (CB.cellRow < rowFrom || CB.cellRow >= rowTo) continue;
                    int cs0 = -1, cs1 = -1;
                    if (hasSel && k >= b1 && k <= b2) {
                        cs0 = (k == b1) ? p1 : 0;
                        cs1 = (k == b2) ? p2 : textLength(CB);
                    }
                    drawBlockText(p, CB, QPointF(left + CB.cellRelX, y + CB.cellRelY),
                                  cs0, cs1, selBg);
                }
            }
            continue;
        }
        if (b.kind == Block::OpaqueVisible) {
            const QRectF r(left, y + L.topPad + 5, cw, 24);
            p->setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
            p->setBrush(QColor(0, 0, 0, 8));
            p->drawRoundedRect(r, 5, 5);
            p->setPen(QColor(110, 110, 110));
            QFont f; f.setPointSizeF(9.5); f.setItalic(true);
            p->setFont(f);
            p->drawText(r, Qt::AlignCenter, m_tablePlaceholder);
            continue;
        }
        if (L.isToc) {
            paintToc(p, L, left, y, cw, segFirstEntry(L, seg));
            continue;
        }
        if (!hasText(L))
            continue;

        const int rowFrom = segFirstLine(L, seg);
        const int rowTo   = (segIdx + 1 < L.segs.size())
                                ? segFirstLine(L, L.segs.at(segIdx + 1))
                                : -1;

        if (!L.marker.isEmpty() && lineCount(L) > 0 && rowFrom <= 0) {
            const RunFmt def = d.defaultRun();
            QFont mf; mf.setFamily(def.font); mf.setPointSizeF(def.sizePt);
            p->setFont(mf);
            p->setPen(QColor(30, 30, 30));
            p->drawText(QPointF(left + L.indentPx - QFontMetricsF(mf).horizontalAdvance(L.marker),
                                y + lineTextTop(L, 0) + lineAscent(L, 0)),
                        L.marker);
        }

        if (withSpell)
            paintSpell(p, L, i, QPointF(left + L.indentPx, y));

        int s0 = -1, s1 = -1;
        if (hasSel && i >= b1 && i <= b2) {
            s0 = (i == b1) ? p1 : 0;
            s1 = (i == b2) ? p2 : textLength(L);
        }
        drawBlockText(p, L, QPointF(left + L.indentPx, y), s0, s1, selBg,
                      rowFrom, rowTo);

        const QString t = blockText(L);
        for (int pb = t.indexOf(kPageBreak); pb >= 0; pb = t.indexOf(kPageBreak, pb + 1)) {
            const int lb = lineForPos(L, pb);   // Text da ⇒ Zeilen da
            if (lb < rowFrom || (rowTo >= 0 && lb >= rowTo)) continue;
            const qreal ly = y + lineTop(L, lb) + lineHeight(L, lb) + 2;
            p->setPen(QPen(QColor(140, 140, 150), 1, Qt::DashLine));
            p->drawLine(QPointF(left, ly), QPointF(left + cw, ly));
            QFont f; f.setPointSizeF(8.0);
            p->setFont(f);
            p->setPen(QColor(130, 130, 140));
            p->drawText(QPointF(left + cw / 2 - 30, ly - 3), m_pageBreakLabel);
        }
    }

    if (withCaret && m_caretOn && hasActiveFocus() && !hasSel
        && !m_cursorRect.isNull()
        && m_cursorRect.y() >= sy - 1.0 && m_cursorRect.y() < sy + slotH
        && m_cursorRect.x() >= sx - marLpx()) {
        p->fillRect(m_cursorRect, QColor(20, 20, 20));
    }
    p->restore();
}

// Gemalt wird aus DIESER Auslegung, Seite fuer Seite mit paintSlot. Ein eigenes
// QTextDocument kam auf ein anderes Layout - an tests/ER.docx 4 Anzeige- gegen
// 3 Export-Seiten. Raender am Writer sind 0, sie stecken schon im Layout.
QString DocxTextArea::exportPagesToPdf(const QString& targetPath,
                                       int pageNumberPos, int pageNumberStyle) {
    if (!m_ctl || !m_ctl->ready())
        return QStringLiteral("Kein Dokument geladen.");
    if (targetPath.isEmpty())
        return QStringLiteral("Kein Zielpfad.");

    ensureOffsetsTo(int(m_lay.size()));
    const int pages = m_pageCount;
    if (pages <= 0 || pageWpx() <= 0.0 || pageHpx() <= 0.0)
        return QStringLiteral("Seiten noch nicht ausgelegt.");

    QSaveFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly))
        return QStringLiteral("Ziel nicht beschreibbar.");

    // Der Writer schreibt ZUERST in den Speicher: danach fasst `mg::pdfglyphs::mergeGlyphRuns` die von Qt je Glyphe
    // geschriebenen Textobjekte zusammen, sonst liest PDFium "H allo". Kostet eine Kopie der fertigen PDF im RAM.
    QByteArray pdfBytes;
    {
        QBuffer sink(&pdfBytes);
        sink.open(QIODevice::WriteOnly);
        constexpr qreal kTwipToMm = 25.4 / 1440.0;
        const Docx::SectionProps& sp = m_ctl->section();
        QPdfWriter writer(&sink);
        writer.setPageSize(QPageSize(QSizeF(sp.pageW * kTwipToMm, sp.pageH * kTwipToMm),
                                     QPageSize::Millimeter, QString(),
                                     QPageSize::FuzzyMatch));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
        writer.setResolution(300);
        writer.setTitle(QFileInfo(targetPath).completeBaseName());

        // Das BLATT ist das Ziel - der Schreibbereich steckt in den Seitenrändern des Dokuments und damit schon in der
        // Auslegung. Ein zusätzlicher Rand verkleinerte den Inhalt maßstäblich und addierte sich mit den Randlinealen.
        const QRectF sheet = writer.pageLayout().paintRectPixels(writer.resolution());
        const QRectF target = sheet;

        QPainter p(&writer);
        for (int pg = 0; pg < pages; ++pg) {
            if (pg > 0) writer.newPage();
            paintPageInto(&p, pg, target, false, false);

            paintPageNumber(&p, sheet, pg, pages, pageNumberPos, pageNumberStyle);
            // Das Layout-Fenster MITZIEHEN: sonst hält der Export die Auslegung jeder Seite im Speicher - gemessen +145 MB
            // bei 648 Seiten. Nur jede achte Seite: je Seite kostete 19 % Exportzeit (1034 -> 1230 ms), jede achte nur 6 %.
            if ((pg & 7) == 0) {
                const int nCols  = colCount();
                const qreal slotH = slotHeight();
                const qreal top   = qreal(pg * nCols) * slotH;
                trimLayouts(blockAtY(top), blockAtY(top + nCols * slotH));
            }
        }
        p.end();
    }   // Writer zerstört -> PDF finalisiert

    const QByteArray fixed = mg::pdfglyphs::mergeGlyphRuns(pdfBytes);
    pdfBytes.clear();                       // die Kopie sofort wieder freigeben
    if (out.write(fixed) != fixed.size())
        return QStringLiteral("Schreiben fehlgeschlagen.");
    if (!out.commit())
        return QStringLiteral("Schreiben fehlgeschlagen.");
    return QString();
}

void DocxTextArea::paintPageInto(QPainter* p, int page, const QRectF& target,
                                 bool withPaperFrame, bool withSelection) {
    if (!m_ctl || !m_ctl->ready() || pageWpx() <= 0.0 || pageHpx() <= 0.0)
        return;
    ensureOffsetsTo(int(m_lay.size()));
    if (page < 0 || page >= m_pageCount)
        return;

    const qreal s = qMin(target.width() / pageWpx(), target.height() / pageHpx());
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    p->translate(target.x(), target.y());
    p->scale(s, s);
    p->translate(0, -pageDocY(page));

    const QRectF paper(0, pageDocY(page), pageWpx(), pageHpx());
    if (withPaperFrame) {
        p->fillRect(paper, QColor(255, 255, 255));
        p->setPen(QColor(0, 0, 0, 60));
        p->drawRect(paper);
    }

    const int nCols = colCount();
    for (int c = 0; c < nCols; ++c) {
        paintSlot(p, page * nCols + c, false, withSelection, /*withSpell=*/false);
    }
    if (withPaperFrame)
        paintPageNumber(p, paper, page, m_pageCount, m_pageNumberPos,
                        m_pageNumberStyle);
    p->restore();
}

void DocxTextArea::hitTest(const QPointF& itemPos, int* block, int* pos) {
    *block = -1; *pos = 0;
    if (!m_ctl || m_lay.empty()) return;
    ensureOffsetsTo(int(m_lay.size()));

    const qreal s = qMax(0.05, m_scale);
    const qreal offX = qMax(kSideMargin * s, (width() - pageWpx() * s) / 2.0);
    const qreal docX = (itemPos.x() - offX) / s;
    const qreal docY = (itemPos.y() + m_contentY) / s;

    const qreal pageStride = pageHpx() + kPageGap;
    const int page = qBound(0, int((docY - kPadV) / pageStride), qMax(0, m_pageCount - 1));
    const int nCols = colCount();
    int col = 0;
    if (nCols > 1) {
        const qreal colStride = contentWidth() + colSpacePx();
        col = qBound(0, int((docX - marLpx() + colSpacePx() / 2.0) / colStride), nCols - 1);
    }
    const int slot = page * nCols + col;
    const qreal slotH = slotHeight();

    // Zweite Verfeinerung: liegt der Klick IN einer Tabelle? Zellblöcke haben keine eigene Fluss-Lage, ihre
    // Dokument-Lage aber schon - geprüft wird deshalb direkt, und BEVOR die Fluss-Suche greift, die nur den Anker fände.
    for (int t = 0; t < m_ctl->doc().tables().size(); ++t) {
        const int anchor = m_ctl->doc().tableFirstBlock(t);
        if (anchor < 0 || anchor >= int(m_lay.size())) continue;
        const BlockLayout& A = m_lay[size_t(anchor)];
        if (!A.isTable || !A.table) continue;
        // Eine getrennte Tabelle hat je Seite ein eigenes Stück mit eigenem Ursprung; geprüft wird deshalb NUR das
        // Stück im geklickten Slot - sonst fände die Suche auf Seite 3 die Zeilen von Seite 1.
        int segIdx = -1;
        for (int s = 0; s < A.segs.size(); ++s)
            if (A.segs.at(s).slot == slot) { segIdx = s; break; }
        if (segIdx < 0) continue;
        int rowFrom = 0, rowTo = 0;
        tableSegRows(A, segIdx, &rowFrom, &rowTo);
        if (rowTo <= rowFrom) continue;
        const QPointF org = tableSegOrigin(A, segIdx);
        const qreal ax = org.x();
        const qreal ay = org.y();
        const RowLayout& rTop = A.table->rows[size_t(rowFrom)];
        const RowLayout& rBot = A.table->rows[size_t(rowTo - 1)];
        if (docY < ay + rTop.y || docY > ay + rBot.y + rBot.h) continue;
        // ... UND waagerecht innerhalb der Tabelle plus Umfluss-Lücke. Ohne diese Zeile entschied allein das y-Band, und
        // seit Text daneben umfließt, landete ein Klick in diesen Streifen IN der Tabelle.
        if (docX < ax - kTableWrapGap
            || docX > ax + A.table->width + kTableWrapGap) continue;
        int best = -1;
        qreal bestDist = 0.0;
        const int tid = t;
        for (int k = anchor; k < int(m_lay.size())
                             && k < m_ctl->doc().blocks.size()
                             && m_ctl->doc().blocks.at(k).tableId == tid; ++k) {
            const BlockLayout& CB = m_lay[size_t(k)];
            if (!CB.isCell || !hasText(CB)) continue;
            if (CB.cellRow < rowFrom || CB.cellRow >= rowTo) continue;
            const qreal h = linesBottom(CB);
            const QRectF r(ax + CB.cellRelX, ay + CB.cellRelY,
                           qMax(4.0, CB.cellW), qMax(4.0, h));
            const qreal dx = qMax(qMax(r.left() - docX, docX - r.right()), 0.0);
            const qreal dy = qMax(qMax(r.top() - docY, docY - r.bottom()), 0.0);
            const qreal dist = dx * dx + dy * dy;
            if (best < 0 || dist < bestDist) { best = k; bestDist = dist; }
        }
        if (best >= 0) {
            const BlockLayout& CB = m_lay[size_t(best)];
            *block = best;
            *pos = 0;
            {
                const qreal cx = docX - (ax + CB.cellRelX);
                const qreal cyLocal = docY - (ay + CB.cellRelY);
                const int fk = floatingImageAt(CB, cx, cyLocal);
                if (fk >= 0) *pos = CB.images[size_t(fk)].pos;
                else if (lineCount(CB) > 0)
                    *pos = posForX(CB, lineForLocalY(CB, cyLocal), cx);
            }
            return;
        }
    }
    const qreal yInSlot = qBound(0.0, docY - slotDocY(slot), slotH);
    const qreal cy = slot * slotH + yInSlot;
    int bi = blockAtY(cy);
    const Document& d = m_ctl->doc();
    auto editable = [&](int i) {
        return i >= 0 && i < d.blocks.size()
               && d.blocks.at(i).kind == Block::Paragraph;
    };
    if (!editable(bi)) {
        int up = bi, down = bi;
        while (up >= 0 && !editable(up)) --up;
        while (down < d.blocks.size() && !editable(down)) ++down;
        bi = editable(down) ? down : up;
        if (!editable(bi)) return;
    }
    ensureLaid(bi);
    const BlockLayout& L = m_lay[bi];
    *block = bi;
    if (lineCount(L) == 0) { *pos = 0; return; }
    //  Dokument-y -> Layout-y des Blocks: über das Segment, das an dieser Stelle
    //  liegt (bei einem über die Seitengrenze getrennten Absatz gehört dieselbe
    //  Layout-Zeile zu einer anderen Slot-y).
    int segIdx = 0;
    for (int k = 0; k < L.segs.size(); ++k)
        if (L.segs.at(k).slot <= slot) segIdx = k;
    const PageSeg seg = L.segs.isEmpty() ? PageSeg() : L.segs.at(segIdx);
    const qreal localY = (docY - (slotDocY(seg.slot) + seg.yInSlot))
                         + segOriginY(L, seg);
    const qreal localX = docX - slotDocX(seg.slot) - L.indentPx;
    //  `rowAtX`: bei einem geteilten Band (Text links UND rechts eines Bildes)
    //  liegen zwei Zeilen auf derselben y - x entscheidet.
    const int li = rowAtX(L, lineForLocalY(L, localY), localX);
    // Ein VERANKERTES Bild gehört keinem Zeilenband, getroffen wird es über seine LAGE: `imageAtX` findet es nur,
    // solange ein Band sein Rechteck schneidet - bei einem breiten Bild war es sonst nicht mehr anklickbar.
    const int fk = floatingImageAt(L, localX, localY);
    if (fk >= 0) { *pos = L.images[size_t(fk)].pos; return; }
    *pos = posForX(L, li, localX);
}

void DocxTextArea::mousePressEvent(QMouseEvent* e) {
    forceActiveFocus();
    if (m_ctl) m_ctl->setRulerFocus(0);
    int b, p;
    hitTest(e->position(), &b, &p);

    const bool onImage = b >= 0 && b < int(m_lay.size())
                         && imageAtPos(m_lay[size_t(b)], p) >= 0;

    if (e->button() == Qt::RightButton) {
        if (b >= 0) {
            const DocxCursor& c = m_ctl->cursor();
            int b1 = c.aBlock, b2 = c.block;
            if (b1 > b2) std::swap(b1, b2);
            if (onImage) {
                m_ctl->setCursor(b, p, false);
                m_ctl->setCursor(b, p + 1, true);
            } else if (!c.hasSelection() || b < b1 || b > b2) {
                m_ctl->setCursor(b, p, false);
            }
        }
        emit contextMenuRequested(e->position().x(), e->position().y(), b);
        e->accept();
        return;
    }

    if (b >= 0) {
        const bool shift = e->modifiers() & Qt::ShiftModifier;
        if (onImage && !shift) {
            m_ctl->setCursor(b, p, false);
            m_ctl->setCursor(b, p + 1, true);
            m_selecting = false;          // Ziehen darf die Auswahl nicht lösen
        } else {
            m_ctl->setCursor(b, p, shift);
            m_selecting = true;
        }
        m_goalX = -1;
    }
    e->accept();
}
void DocxTextArea::mouseMoveEvent(QMouseEvent* e) {
    if (!m_selecting) return;
    int b, p;
    hitTest(e->position(), &b, &p);
    if (b >= 0)
        m_ctl->setCursor(b, p, true);
    e->accept();
}
void DocxTextArea::mouseReleaseEvent(QMouseEvent* e) {
    m_selecting = false;
    e->accept();
}
void DocxTextArea::mouseDoubleClickEvent(QMouseEvent* e) {
    int b, p;
    hitTest(e->position(), &b, &p);
    if (b >= 0)
        m_ctl->selectWordAt(b, p);
    e->accept();
}

void DocxTextArea::moveCursorVertical(int dir, bool keepAnchor) {
    const DocxCursor& c = m_ctl->cursor();
    const int bi = c.block;
    ensureLaid(bi);
    const BlockLayout& L = m_lay[bi];
    if (!hasText(L)) return;
    const int nLines = lineCount(L);
    const int li = lineForPos(L, c.pos);
    if (m_goalX < 0 && nLines > 0)
        m_goalX = xForPos(L, li, c.pos);
    const Document& d = m_ctl->doc();
    auto editable = [&](int i) {
        return i >= 0 && i < d.blocks.size()
               && d.blocks.at(i).kind == Block::Paragraph;
    };
    // Hoch/Runter springen ein BAND, nicht ein Stück: bei einem geteilten Band liegen zwei Zeilen auf derselben y,
    // und "eine Zeile tiefer" wäre sonst die Hälfte daneben. Im Zielband entscheidet `m_goalX`.
    auto bandStep = [&](int from, int step) {
        const qreal y0 = lineTop(L, from);
        int k = from;
        while (k + step >= 0 && k + step < nLines
               && qFuzzyIsNull(lineTop(L, k + step) - y0))
            k += step;
        k += step;
        return (k < 0 || k >= nLines) ? -1 : rowAtX(L, k, m_goalX);
    };
    if (dir < 0) {
        const int up = bandStep(li, -1);
        if (up >= 0) {
            m_ctl->setCursor(bi, posForX(L, up, m_goalX), keepAnchor);
            return;
        }
        int pb = bi - 1;
        while (pb >= 0 && !editable(pb)) --pb;
        if (pb < 0) return;
        ensureLaid(pb);
        const BlockLayout& P = m_lay[pb];
        int pos = m_ctl->doc().blocks.at(pb).textLength();
        if (lineCount(P) > 0)
            pos = posForX(P, lineCount(P) - 1, m_goalX);
        m_ctl->setCursor(pb, pos, keepAnchor);
    } else {
        const int down = bandStep(li, 1);
        if (down >= 0) {
            m_ctl->setCursor(bi, posForX(L, down, m_goalX), keepAnchor);
            return;
        }
        int nb = bi + 1;
        while (nb < d.blocks.size() && !editable(nb)) ++nb;
        if (nb >= d.blocks.size()) {
            m_ctl->setCursor(bi, INT_MAX, keepAnchor);
            return;
        }
        ensureLaid(nb);
        const BlockLayout& N = m_lay[nb];
        int pos = 0;
        if (lineCount(N) > 0)
            pos = posForX(N, 0, m_goalX);
        m_ctl->setCursor(nb, pos, keepAnchor);
    }
}

void DocxTextArea::keyPressEvent(QKeyEvent* e) {
    if (!m_ctl || !m_ctl->ready()) { e->ignore(); return; }
    const bool ctrl  = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const DocxCursor& c = m_ctl->cursor();

    // Tippen gibt Strg+Z wieder dem Dokument statt dem zuletzt gezogenen Lineal.
    // Ausgenommen sind Strg+Z/Y selbst und die MODIFIKATORtasten: Qt schickt fuer Strg
    // ein eigenes Ereignis vor dem Z, das sonst den Fokus schon zuruecksetzte.
    const int key = e->key();
    const bool modifierKey = (key == Qt::Key_Control || key == Qt::Key_Shift
                              || key == Qt::Key_Alt  || key == Qt::Key_Meta
                              || key == Qt::Key_AltGr);
    const bool undoRedo    = ctrl && (key == Qt::Key_Z || key == Qt::Key_Y);
    if (!modifierKey && !undoRedo)
        m_ctl->setRulerFocus(0);
    if (ctrl) {
        switch (e->key()) {
        case Qt::Key_S: emit saveRequested();          e->accept(); return;
        case Qt::Key_A: m_ctl->selectAll();            e->accept(); return;
        case Qt::Key_C: m_ctl->copy();                 e->accept(); return;
        case Qt::Key_X: m_ctl->cut();                  e->accept(); return;
        case Qt::Key_V: m_ctl->paste();                e->accept(); return;
        case Qt::Key_Z: shift ? m_ctl->redo() : m_ctl->undo(); e->accept(); return;
        case Qt::Key_Y: m_ctl->redo();                 e->accept(); return;
        case Qt::Key_B: m_ctl->toggleBold();           e->accept(); return;
        case Qt::Key_I: m_ctl->toggleItalic();         e->accept(); return;
        case Qt::Key_U: m_ctl->toggleUnderline();      e->accept(); return;
        default: break;
        }
    }
    m_goalX = (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) ? m_goalX : -1;
    switch (e->key()) {
    case Qt::Key_Left:
        if (c.pos > 0) m_ctl->setCursor(c.block, c.pos - 1, shift);
        else if (c.block > 0) m_ctl->setCursor(c.block - 1, INT_MAX, shift);
        e->accept(); return;
    case Qt::Key_Right: {
        const int len = m_ctl->doc().blocks.at(c.block).textLength();
        if (c.pos < len) m_ctl->setCursor(c.block, c.pos + 1, shift);
        else if (c.block < m_ctl->doc().blocks.size() - 1)
            m_ctl->setCursor(c.block + 1, 0, shift);
        e->accept(); return;
    }
    case Qt::Key_Up:   moveCursorVertical(-1, shift); e->accept(); return;
    case Qt::Key_Down: moveCursorVertical(+1, shift); e->accept(); return;
    case Qt::Key_Home: m_ctl->setCursor(c.block, 0, shift);       e->accept(); return;
    case Qt::Key_End:  m_ctl->setCursor(c.block, INT_MAX, shift); e->accept(); return;
    case Qt::Key_Backspace: m_ctl->deleteBackward();   e->accept(); return;
    case Qt::Key_Delete:    m_ctl->deleteForward();    e->accept(); return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        shift ? m_ctl->insertLineBreak() : m_ctl->insertParagraphBreak();
        e->accept(); return;
    case Qt::Key_Tab:
        m_ctl->insertText(QStringLiteral("\t"));
        e->accept(); return;
    default: break;
    }
    const QString t = e->text();
    if (!t.isEmpty() && t.at(0).isPrint()) {
        m_ctl->insertText(t);
        e->accept();
        return;
    }
    e->ignore();
}

void DocxTextArea::inputMethodEvent(QInputMethodEvent* e) {
    if (!e->commitString().isEmpty() && m_ctl && m_ctl->ready())
        m_ctl->insertText(e->commitString());
    e->accept();
}

QVariant DocxTextArea::inputMethodQuery(Qt::InputMethodQuery q) const {
    switch (q) {
    case Qt::ImEnabled:        return true;
    case Qt::ImCursorRectangle:
        return m_cursorRect.translated(0, -m_contentY);
    case Qt::ImHints:          return int(Qt::ImhMultiLine);
    default:                   return QQuickPaintedItem::inputMethodQuery(q);
    }
}

// Solange diese Fläche den Fokus hat, gehört JEDE Taste, die Text erzeugt, dem Editor - sonst öffnete ein
// getipptes "d" den Datum-Editor des Viewers. Modifizierte Kürzel bleiben unangetastet.
bool DocxTextArea::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride && hasActiveFocus()) {
        auto* ke = static_cast<QKeyEvent*>(e);
        const bool modified = ke->modifiers()
                              & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (!modified && !ke->text().isEmpty() && ke->text().at(0).isPrint()) {
            e->accept();
            return true;
        }
    }
    return QQuickPaintedItem::event(e);
}

void DocxTextArea::geometryChange(const QRectF& n, const QRectF& o) {
    QQuickPaintedItem::geometryChange(n, o);
    if (!qFuzzyCompare(n.width() + 1, o.width() + 1)) {
        // Die Zeilenbreite ist die TEXTBREITE DER SEITE und damit von der Kachelbreite unabhängig - ein Breitenwechsel
        // ändert nur noch den Einpass-Maßstab, keinen Umbruch und kein Neu-Vermessen.
        updateScale();
    }
    emit imageSelectionChanged();     // Seite wandert waagerecht/skaliert
    emit pageGeometryChanged();
    update();
}

DocxPageThumb::DocxPageThumb(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(false);
}

void DocxPageThumb::setArea(DocxTextArea* a) {
    if (m_area == a) return;
    if (m_area) disconnect(m_area, nullptr, this, nullptr);
    m_area = a;
    if (m_area) {
        connect(m_area, &DocxTextArea::documentChanged, this, [this]() { update(); });
        connect(m_area, &DocxTextArea::pageCountChanged, this, [this]() { update(); });
    }
    emit areaChanged();
    update();
}

void DocxPageThumb::setPage(int p) {
    if (m_page == p) return;
    m_page = p;
    emit pageChanged();
    update();
}

void DocxPageThumb::paint(QPainter* p) {
    if (!m_area || width() <= 1.0 || height() <= 1.0)
        return;
    m_area->paintPageInto(p, m_page, QRectF(0, 0, width(), height()));
}
