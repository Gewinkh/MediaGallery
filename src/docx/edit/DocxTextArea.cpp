#include "docx/edit/DocxTextArea.h"
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
constexpr qreal kPtToPx     = 96.0 / 72.0;    // Punkt → logische Pixel
constexpr qreal kTwipToPx   = kPtToPx / 20.0; // Twips (1/1440") → logische Pixel
constexpr qreal kPadV       = 28.0;           // Rand über/unter dem Seitenstapel
constexpr qreal kPageGap    = 20.0;           // Lücke zwischen zwei Seiten
constexpr qreal kSideMargin = 14.0;           // Mindestrand neben der Seite
constexpr qreal kListIndent = 28.0;           // Einzug je Listenebene
constexpr int   kChunk      = 300;            // Blöcke je Initial-Layout-Tick
//  Bleibt neben einem Bild weniger Platz als das, fängt der Text darunter an —
//  eine Spalte von zwei Zeichen Breite ist unlesbar.
constexpr qreal kMinTextBesideImage = 60.0;

//  Layout-Fenster (RAM): Ein vermessener QTextLayout haelt die komplette
//  Glyphen-/Zeilenstruktur seines Absatzes — bei einem 400-Seiten-Dokument mit
//  ~10.000 Absaetzen summiert sich das auf zweistellige Megabyte, obwohl nie
//  mehr als ein Bildschirm davon gebraucht wird. Ab kLayoutCap Bloecken werden
//  daher Layouts ausserhalb des Fensters (sichtbarer Bereich ± kKeepMargin)
//  freigegeben; die bereits vermessene HOEHE bleibt erhalten (Bildlaufleiste
//  und Offsets bleiben exakt), ensureLaid() baut das Layout bei Bedarf
//  identisch neu auf.
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

    //  Der Blinktakt laeuft NUR, solange der Caret ueberhaupt sichtbar sein kann
    //  (Fokus, keine Selektion). Jedes update() dieses QQuickPaintedItem malt den
    //  gesamten Viewport samt aller sichtbaren QTextLayouts neu und laedt die
    //  Textur wieder zur GPU — vorher geschah das zweimal je Sekunde dauerhaft,
    //  auch bei fokusloser, unsichtbarer oder gar nicht editierter Ansicht.
    m_blinkTimer.setInterval(530);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_caretOn = !m_caretOn;
        update();
    });
    connect(this, &QQuickItem::activeFocusChanged, this, &DocxTextArea::syncCaretBlink);
    syncCaretBlink();
}

//  Blinken exakt dann laufen lassen, wenn paint() den Caret auch zeichnen wuerde
//  (s. Bedingung am Ende von paint()).
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
        //  Ergebnis der Rechtschreibprüfung liegt vor → nur neu ZEICHNEN.
        //  Das Layout ändert sich dadurch nicht (die Wellenlinie liegt unter
        //  dem Text), deshalb kein invalidateFrom.
        connect(m_ctl, &DocxEditController::spellRangesChanged, this,
                [this](int) { update(); });
        connect(m_ctl, &DocxEditController::cursorChanged, this, [this]() {
            m_caretOn = true;
            //  Der zuletzt besuchte LEERE Absatz wurde mit dem Cursor-Format
            //  vermessen — verlässt der Cursor ihn, gilt wieder sein Stil-Format.
            const int now = m_ctl->cursor().block;
            if (m_lastCursorBlock != now) {
                invalidateEmptyBlock(m_lastCursorBlock);
                invalidateEmptyBlock(now);
                m_lastCursorBlock = now;
            }
            updateCursorRect();
            syncCaretBlink();   // Selektion begonnen/aufgehoben → Blinken an/aus
            update();
        });
        //  Format-Änderung OHNE Selektion existiert nur als Pending-Format im
        //  Controller — sie erzeugt weder blocksReplaced noch cursorChanged.
        //  Ohne diese Verbindung blieb der Caret (und die Höhe einer leeren
        //  Zeile) auf der alten Schriftgröße stehen (Nutzerbefund).
        //  Regionswechsel (Körper ↔ Kopf-/Fußzeile) = ANDERES Dokument:
        //  Layout-Cache, Offsets und laufende Teile sind komplett ungültig.
        connect(m_ctl, &DocxEditController::activeRegionChanged, this, [this]() {
            m_header = RunningPart();
            m_footer = RunningPart();
            rebuildAll();
        });
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
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Seitengeometrie — alles in DOKUMENT-Pixeln aus der Seiteneinrichtung des
//  Dokuments (w:sectPr). Bewusst UNABHÄNGIG von der Fensterbreite: die
//  Zeilenbreite ist die echte Textbreite der Seite, damit der Umbruch dort
//  fällt, wo er auch in Word fällt. Zu schmale Kacheln werden beim ZEICHNEN
//  verkleinert (m_scale), nicht neu umbrochen.
// ─────────────────────────────────────────────────────────────────────────────
const SectionProps& DocxTextArea::sect() const {
    static const SectionProps fallback;               // A4, falls kein Dokument
    //  IMMER die Seiteneinrichtung des KÖRPERS — ein Kopfzeilen-Teil hat keine
    //  eigene und die Seite dürfte beim Umschalten nicht die Größe wechseln.
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

//  Maßstab: eine A4-Seite ist ~794 px breit — in einer halben Split-View-Kachel
//  hätte sie keinen Platz. Statt neu umzubrechen (dann wäre die Ansicht nicht
//  mehr seitengenau) wird die Seite VERKLEINERT gezeichnet.
void DocxTextArea::updateScale() {
    const qreal avail = width() - 2 * kSideMargin;
    const qreal s = (avail > 20.0 && pageWpx() > 1.0)
                        ? qMin(1.0, avail / pageWpx()) : 1.0;
    if (qFuzzyCompare(s + 1.0, m_scale + 1.0))
        return;
    m_scale = s;
    updateContentHeight();
    updateCursorRect();
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout-STÜCKE eines Blocks — die einzige Lesesicht auf seine Zeilen.
//
//  Heute hat ein Block genau EIN Stück: sein QTextLayout, über die volle
//  Textbreite, ohne Versatz. Diese Schicht ist trotzdem schon da, weil ein
//  Absatz später mehrere Stücke tragen soll (Text NEBEN einem Bild) — dann
//  ändert sich nur, was hier steht, und nicht die fünf Stellen, die Zeilen
//  lesen. Zeilen zählen block-global, x/y sind block-lokal (Stück-Versatz
//  eingerechnet), Zeichenpositionen block-lokal.
// ─────────────────────────────────────────────────────────────────────────────
bool DocxTextArea::hasText(const BlockLayout& L) const {
    return L.isText;
}
int DocxTextArea::textLength(const BlockLayout& L) const {
    return L.textLen;
}
//  Der Absatztext. Ohne Bild ist er der Text des einzigen Stücks (geteilt, ohne
//  Kopie); mit Bildern wird er aus den Stücken zusammengesetzt, die Bilder
//  tragen ihr Objekt-Zeichen.
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
//  Oberkante der TEXTZEILE eines Bandes (das Band selbst kann höher sein, wenn
//  ein Bild darin steht — der Text sitzt dann an dessen Unterkante).
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
//  Zeichenposition → Zeilenband. Ohne Bänder (leerer Absatz) ist 0 die Antwort.
int DocxTextArea::lineForPos(const BlockLayout& L, int pos) const {
    const int n = int(L.rows.size());
    if (n == 0) return 0;
    const int p = qBound(0, pos, L.textLen);
    for (int li = 0; li < n; ++li)
        if (p < L.rows[size_t(li)].charEnd)
            return li;
    return n - 1;
}
//  Ein Band kann ZWEI Zeilen tragen: Text links UND rechts eines umflossenen
//  Bildes (`wrapText="bothSides"`). Beide liegen auf derselben y — welche
//  gemeint ist, sagt x. Ohne geteiltes Band bleibt es bei `li`.
int DocxTextArea::rowAtX(const BlockLayout& L, int li, qreal x) const {
    if (li < 0 || li >= int(L.rows.size())) return li;
    const qreal y = L.rows[size_t(li)].y;
    //  Auf den ANFANG des Bandes zurückgehen — `li` kann schon das rechte Stück
    //  sein (etwa beim Sprung aus dem Band darunter).
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
//  Block-lokale y → Zeilenband (das erste, dessen Unterkante darunter liegt).
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
    //  Textzeile des Bandes zuerst — eine Stelle IM Text schlägt die Bildkante.
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
    //  Klick AUF ein Bild setzt den Cursor auf sein Objekt-Zeichen — damit ist
    //  es über die Selektion genau eines Zeichens auswählbar.
    const int hit = imageAtX(L, li, x);
    if (hit >= 0) return L.images[size_t(hit)].pos;
    const LineRef r = lineRef(L, li);
    if (r.valid())
        return r.textStart + r.textLine().xToCursor(x - r.dx);
    //  Reines Bild-Band: die nächstgelegene Kante entscheidet.
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
//  Verankertes Bild an einer block-lokalen Stelle (x,y) — die Lage ist seine
//  einzige Bindung, es hängt an keinem Zeilenband.
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
    //  Verankerte Bilder gehören keinem Band — sie decken alle, die ihr
    //  Rechteck schneidet.
    for (size_t k = 0; k < L.images.size(); ++k) {
        const ImageBox& B = L.images[k];
        if (!B.floating) continue;
        if (x < B.x || x >= B.x + B.w) continue;
        if (R.y >= B.y + B.h || R.y + R.visH <= B.y) continue;
        return int(k);
    }
    return -1;
}
void DocxTextArea::drawBlockText(QPainter* p, const BlockLayout& L,
                                 const QPointF& origin, int selStart, int selEnd,
                                 const QColor& selBg) const {
    const int n  = L.textLen;
    const int s0 = qBound(0, selStart, n);
    const int s1 = qBound(0, selEnd, n);
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
    if (L.images.empty()) return;
    p->save();
    for (const ImageBox& B : L.images) {
        const QRectF box(origin.x() + B.x, origin.y() + B.y, B.w, B.h);
        if (!B.img.isNull()) {
            p->drawImage(box.topLeft(), B.img);
        } else {
            //  Beziehung/Format unbekannt → Rahmen in Sollgröße statt Fehlanzeige.
            p->setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
            p->setBrush(QColor(0, 0, 0, 8));
            p->drawRect(box);
        }
        //  Ein Bild IN der Selektion wird wie Text hinterlegt.
        if (s1 > s0 && B.pos >= s0 && B.pos < s1)
            p->fillRect(box, selBg);
    }
    p->restore();
}

//  Eine Tabelle zählt TABELLENZEILEN, ein Verzeichnis EINTRÄGE, alles andere
//  Textzeilen — dieselbe Zahl, drei Bedeutungen (s. PageSeg::first).
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
    //  Dieselbe Rechnung wie in paintSlot — die erste Zeile des Stücks landet
    //  auf seiner Slot-y, alles davor liegt (rechnerisch) darüber.
    return QPointF(slotDocX(s.slot),
                   slotDocY(s.slot) + s.yInSlot - segOriginY(A, s));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Segmente: welche Zeilen eines Absatzes liegen in welchem Slot?
// ─────────────────────────────────────────────────────────────────────────────
const DocxTextArea::PageSeg& DocxTextArea::segAt(int i, int lineIdx) const {
    static const PageSeg zero;
    const BlockLayout& L = m_lay[size_t(i)];
    if (L.segs.isEmpty()) return zero;
    //  Die Stücke eines Verzeichnisses sind über eine ZEILE nicht adressierbar
    //  (sie tragen Eintragsindizes) — seine Lage ist die des ersten Stücks.
    //  Für eine Tabelle gilt dasselbe: ihre Stücke zählen Tabellenzeilen, und
    //  „die Tabelle" liegt dort, wo ihr erstes Stück beginnt. Wer eine ZELLE
    //  meint, geht über tableSegOfRow (s. docYForLine).
    if (L.isToc || segCountsTableRows(L)) return L.segs.constFirst();
    int k = 0;
    for (int j = 1; j < L.segs.size(); ++j) {
        if (segFirstLine(L, L.segs.at(j)) > lineIdx) break;
        k = j;
    }
    return L.segs.at(k);
}

//  Dokument-y AUS DEM FLUSS (Segmente) — ohne Zell-Sonderfall. Für den Anker
//  ist das die Oberkante der TABELLE; jede Zelle rechnet von dort weiter. Der
//  Zell-Zweig darf deshalb NICHT docYForLine(anchor) rufen (der Anker ist selbst
//  eine Zelle → Endlosrekursion).
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

//  Dokument-Lage eines Blocks. Zellblöcke (auch der Anker!) liegen explizit
//  relativ zur Tabellen-Oberkante, alle anderen im Fluss.
//  Ursprung des Tabellenstücks, auf dem ein Zellblock liegt. Eine über Seiten
//  getrennte Tabelle hat je Stück einen eigenen — ohne das rechnete jede Zelle
//  weiter von der Oberkante der Tabelle und läge damit auf der ersten Seite.
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
        //  Zell-Absätze tragen keinen Abstand davor — die Zeile 0 liegt bei 0.
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
    //  Ein Zellblock trägt keine Höhe und damit keine eigene Fluss-Lage — seine
    //  Seite ist die des Tabellenstücks, auf dem SEINE Zeile liegt. Ohne das
    //  meldete der Cursor in einer über Seiten getrennten Tabelle stets die
    //  Seite hinter ihr.
    if (L.isCell) {
        const int anchor = tableAnchorOf(bi);
        if (anchor >= 0 && anchor < int(m_lay.size())) {
            const BlockLayout& A = m_lay[size_t(anchor)];
            const int seg = tableSegOfRow(A, L.cellRow);
            if (seg >= 0) return A.segs.at(seg).slot / colCount();
        }
    }
    if (L.segs.isEmpty()) return 0;
    //  Die ZEILE des Cursors zählt, nicht der Blockanfang: ein Absatz darf über
    //  Seitengrenzen laufen — steht der Cursor in seiner unteren Hälfte, ist er
    //  auf der FOLGESEITE. Mit `segs.constFirst()` meldete die Anzeige dort die
    //  Seite, auf der der Absatz beginnt.
    return segAt(bi, lineForPos(L, m_ctl->cursor().pos)).slot / colCount();
}

qreal DocxTextArea::pageTop(int page) {
    return pageDocY(qBound(0, page, qMax(0, m_pageCount - 1))) * m_scale;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout-Verwaltung
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::rebuildAll() {
    m_lay.clear();
    m_offsets.clear();
    m_offsetsValidTo = 0;
    m_layChunkAt = 0;
    m_trimLo = m_trimHi = -1;   // Indizes verschoben → Layout-Fenster neu bestimmen
    m_contentHeight = 0;
    m_header = RunningPart();
    m_footer = RunningPart();
    m_lastCursorBlock = (m_ctl && m_ctl->ready()) ? m_ctl->cursor().block : -1;
    if (m_ctl && m_ctl->ready()) {
        m_lay.resize(size_t(m_ctl->doc().blocks.size()));
        //  Schätzhöhen (eine Zeile je ~90 Zeichen) — vom Chunk-Layout ersetzt.
        for (int i = 0; i < int(m_lay.size()); ++i) {
            const Block& b = m_ctl->doc().blocks.at(i);
            if (b.kind == Block::OpaqueHidden)      m_lay[i].height = 0;
            else if (b.kind == Block::OpaqueVisible) m_lay[i].height = 34;
            else m_lay[i].height = 22.0 * qMax(1, b.textLength() / 90 + 1);
        }
        rebuildMarkers();
        m_chunkTimer.start();
    }
    //  Seitengröße kommt aus dem Dokument → Einpass-Maßstab neu bestimmen.
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
    //  Wird in einer Zelle getippt, ändert sich Zeilen- und damit Tabellenhöhe.
    //  Die trägt der ANKER — er muss also mit invalidiert werden, sonst bliebe
    //  die Tabelle auf ihrer alten Höhe stehen und alle Umbrüche danach falsch.
    {
        const int anchor = tableAnchorOf(qMin(first, int(m_lay.size()) - 1));
        if (anchor >= 0 && anchor < int(m_lay.size())) {
            m_lay[size_t(anchor)].laid = false;
            m_lay[size_t(anchor)].table.reset();
            first = qMin(first, anchor);
        }
    }
    for (int i = 0; i < oldCount && first < int(m_lay.size()); ++i)
        m_lay.erase(m_lay.begin() + first);
    for (int i = 0; i < newCount; ++i)
        m_lay.insert(m_lay.begin() + first + i, BlockLayout());
    for (int i = 0; i < newCount; ++i)
        ensureLaid(first + i);
    m_offsetsValidTo = qMin(m_offsetsValidTo, first);
    m_trimLo = m_trimHi = -1;   // Indizes verschoben → Layout-Fenster neu bestimmen
    //  Ein Inhaltsverzeichnis hängt am GANZEN Dokument, nicht nur an den
    //  Blöcken hinter der Änderung — es steht ja meist davor. Also immer
    //  verwerfen; der Neuaufbau kostet einen Lauf über die Blöcke, genau wie
    //  rebuildMarkers direkt darunter.
    for (size_t k = 0; k < m_lay.size(); ++k) {
        if (!m_lay[k].isToc) continue;
        m_lay[k].laid = false;
        m_lay[k].tocEntries.clear();
        m_offsetsValidTo = qMin(m_offsetsValidTo, int(k));
    }
    rebuildMarkers();
    //  Marker können sich hinter der Änderung geändert haben (Listenzähler) —
    //  betroffene Layouts dort NICHT wegwerfen (nur Marker-Strings neu).
    updateContentHeight();
    updateCursorRect();
    emit documentChanged();
    update();
}

//  Läuft über das GESAMTE Dokument und wird bei JEDER Blockänderung (also bei
//  jedem Tastendruck) aufgerufen — die Listenzähler sind global. Der teure Teil
//  war bisher resolvePar() je Absatz: Vorlagenkette ablaufen, ParFmt bauen.
//  Kann laut Vorlagen keine Nummerierung entstehen (Normalfall), genügt ein
//  Bit-Test am Absatz selbst und die Schleife wird pro Block O(1) ohne
//  Allokation — bei einem 400-Seiten-Dokument der Unterschied zwischen
//  spürbarem Tipp-Ruckeln und gar nicht messbar.
void DocxTextArea::rebuildMarkers() {
    if (!m_ctl) return;
    const Document& d = m_ctl->doc();
    const bool mayNumber = d.stylesMayNumber();
    QHash<int, int> counters;                       // numId → laufende Nummer
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
                    //  Fremde Mehr-Ebenen-Muster (%2…) pragmatisch kappen.
                    //  static: sonst würde das Muster je Listenabsatz und je
                    //  Tastendruck neu übersetzt.
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

//  Grundschrift eines Absatzes = SEIN aufgelöstes Stil-Format (nicht die
//  docDefaults): nur so ist eine leere Überschriftzeile auch so hoch wie eine
//  Überschrift. Steht der Cursor in dieser LEEREN Zeile, gilt das am Cursor
//  wirksame Format inkl. Pending — sonst hätte eine Schriftgrößen-Änderung in
//  einer leeren Zeile sichtbar keinerlei Wirkung.
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

void DocxTextArea::buildLayout(int i) {
    const Document& d = m_ctl->doc();
    const Block& b = d.blocks.at(i);
    BlockLayout& L = m_lay[i];
    L.pieces.clear();
    L.images.clear();
    L.rows.clear();
    L.textLen  = 0;
    L.isText   = false;
    L.isImage  = false;
    L.trimmed  = false;
    L.beforePx = 0;

    if (b.kind == Block::OpaqueHidden) {
        L.height = 0; L.laid = true; return;
    }
    if (b.kind == Block::OpaqueVisible) {
        //  Tabellen werden als echtes Gitter ausgelegt (richtige HÖHE → richtige
        //  Seitenumbrüche danach); alles andere bleibt eine Platzhalterzeile.
        //  Dieser Zweig gilt nur noch für Tabellen, die NICHT flach zerlegt
        //  werden konnten (verschachtelt, sdt …) — sie bleiben ein opaker Block.
        if (b.opaqueName == QLatin1String("w:tbl")) { buildTableLayout(i); return; }
        L.height = 34; L.laid = true;
        shiftBelowForeignFloats(i);          // auch er weicht nach unten aus
        return;
    }

    //  Flach zerlegte Tabelle: der ERSTE Zellblock ist der Anker und trägt das
    //  Gitter samt Gesamthöhe; alle weiteren Zellblöcke sind 0 hoch. Damit bleibt
    //  der Fluss streng monoton (nebeneinanderliegende Zellen teilen sonst
    //  dasselbe y-Band) — die Auflösung dieser Vereinfachung ist Schritt 1A.2.
    if (b.tableId >= 0) {
        const int anchor = d.tableFirstBlock(b.tableId);
        if (anchor == i) { buildFlatTableLayout(i); return; }
        //  Zellblöcke werden VOM ANKER mitausgelegt (die Zeilenhöhe hängt von
        //  allen Zellen der Zeile ab) — hier nur sicherstellen, dass er es tat.
        if (anchor >= 0 && anchor < int(m_lay.size()) && !m_lay[size_t(anchor)].table)
            buildFlatTableLayout(anchor);
        L.laid = true;
        return;
    }

    //  Inhaltsverzeichnis-Feld → eine Zeile je Überschrift.
    if (buildTocLayout(i))
        return;

    const ParFmt pf = d.resolvePar(b);
    L.beforePx = pf.beforePt * kPtToPx;
    L.textLen  = b.textLength();
    const qreal bottom = buildInlineRows(L, b, blockBaseFont(b, i), pf,
                                         contentWidth() - L.indentPx, i);
    L.height  = bottom + pf.afterPt * kPtToPx;

    //  „Der Absatz IST das Bild" bleibt ein eigener Zustand: daran hängen die
    //  Ziehpunkte in QML und die Bildauswahl ohne Selektion.
    L.isImage = (L.images.size() == 1 && L.textLen == 1);
    L.isText  = true;
    L.laid    = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Absatz in ZEILENBÄNDER auslegen (Bilder im Fließtext)
//
//  Ein `w:drawing`-Run ist in Word ein Zeichen IN der Zeile (`wp:inline`):
//  zwei Bilder hintereinander stehen NEBENEINANDER, Text dahinter steht daneben,
//  und erst wenn die Breite nicht mehr reicht, beginnt ein neues Band. Genau das
//  bildet diese Funktion nach — QTextLayout kann Inline-Objekte ohne
//  QTextDocument nicht vermessen, also wird der Absatz an jedem Objekt-Zeichen
//  in Stücke zerlegt und der Platz für das Bild ausgespart.
// ─────────────────────────────────────────────────────────────────────────────
//  Störer aus den Blöcken DAVOR, umgerechnet in die block-lokalen Koordinaten
//  von `blockIdx`. Gelesen werden ausschließlich `height` und die Bildkästen der
//  Vorgänger — KEINE Offsets und keine Paginierung, sonst liefe das Auslegen in
//  eine Rekursion (ensureOffsetsTo → paginateBlock → ensureLaid → buildLayout).
//  Weiter zurück als eine Slot-Höhe wird nicht gesucht: was so weit reicht,
//  liegt ohnehin auf einer anderen Seite.
QVector<DocxTextArea::FloatObstacle> DocxTextArea::foreignFloats(int blockIdx) const {
    QVector<FloatObstacle> out;
    if (blockIdx <= 0 || blockIdx > int(m_lay.size())) return out;
    const qreal maxBack = slotHeight();
    qreal between = 0.0;                 // Höhe der Blöcke zwischen p und blockIdx
    for (int p = blockIdx - 1; p >= 0; --p) {
        const BlockLayout& P = m_lay[size_t(p)];
        //  Oberkante von `blockIdx`, ausgedrückt in den Koordinaten von p.
        const qreal off = P.height + between;
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
    //  Gelesen wird das DOKUMENT, nicht das Layout: gefragt wird beim Auslegen
    //  eines FRÜHEREN Blocks, und die Layout-Flags der späteren (`isCell`,
    //  `isToc`) stehen dann noch gar nicht — ein Zellblock sah damit wie ein
    //  gewöhnlicher Absatz aus und der Deckel griff nie.
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
        //  Nur das Auslegen verwerfen — Höhe/Offsets bleiben gültig, bis
        //  ensureLaid sie neu vermisst (dasselbe Muster wie trimLayouts).
        F.laid = false;
        acc += F.height;
    }
}

qreal DocxTextArea::buildInlineRows(BlockLayout& L, const Block& b,
                                    const QFont& base, const ParFmt& pf,
                                    qreal width, int blockIdx) {
    const Document& d  = m_ctl->doc();
    const RunFmt    def = d.defaultRun();
    const QString   text = b.plainText();
    const qreal     W = qMax(20.0, width);
    const qreal     spacing = qMax(0.5, pf.lineSpacing);

    //  Diese Funktion BAUT den Block — sie muss ihn deshalb selbst leeren.
    //  Der Zellpfad (buildFlatTableLayout) ruft sie direkt auf; ohne das blieb
    //  ein gelöschtes Bild in `images` stehen und wurde weitergezeichnet,
    //  obwohl es im Dokument nicht mehr existierte (Nutzerbefund: „Geisterbild"
    //  nach Backspace in einer Tabellenzelle).
    L.pieces.clear();
    L.images.clear();
    L.rows.clear();

    //  Format-Bereiche des GANZEN Absatzes (block-lokal); je Stück geklippt.
    QList<QTextLayout::FormatRange> fmts;
    int acc = 0;
    for (const Run& r : b.runs) {
        if (r.text.isEmpty()) continue;
        const RunFmt rf = d.resolveRun(b, r);
        QTextCharFormat cf;
        QFont f;
        f.setFamily(rf.font.isEmpty() ? def.font : rf.font);
        f.setPointSizeF(rf.sizePt > 0 ? rf.sizePt : def.sizePt);
        f.setBold(rf.bold);
        f.setItalic(rf.italic);
        f.setUnderline(rf.underline);
        cf.setFont(f);
        cf.setForeground(rf.color.isValid() ? rf.color : QColor(0, 0, 0));
        //  ── Fußnoten-VERWEIS: hochgestellte Zahl ─────────────────────────────
        //  Word setzt ihn hochgestellt und kleiner; ohne eigenes Format stünde
        //  eine gewöhnliche Ziffer im Text, die von einer echten Zahl nicht zu
        //  unterscheiden wäre.
        if (r.footnoteId > 0) {
            f.setPointSizeF(qMax(5.0, (rf.sizePt > 0 ? rf.sizePt : def.sizePt) * 0.72));
            cf.setFont(f);
            cf.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
            cf.setForeground(QColor(30, 90, 170));
        } else if (r.opaque && r.revision == Run::RevNone) {
            //  Atomare Fremdinhalte dezent hinterlegen (Hyperlink-Blau wäre
            //  irreführend — es sind auch Felder/Zeichnungen).
            cf.setBackground(QColor(0, 0, 0, 14));
        }
        //  ── Änderungsverfolgung als DEKORATOR ────────────────────────────────
        //  Eingefügt = unterstrichen, gelöscht = durchgestrichen, beides in der
        //  Farbe des Autors — wie in Word. Das Dokument selbst bleibt
        //  unangetastet; die Markierung ist reine Darstellung.
        if (r.revision != Run::RevNone) {
            const QColor rc = revisionColor(r.revAuthor);
            cf.setForeground(rc);
            if (r.revision == Run::RevInserted) f.setUnderline(true);
            else                                f.setStrikeOut(true);
            cf.setFont(f);
        }
        QTextLayout::FormatRange fr;
        fr.start  = acc;
        fr.length = r.text.size();
        fr.format = cf;
        fmts.append(fr);
        acc += r.text.size();
    }
    //  Seitenumbruch-Sentinel unsichtbar machen (der Marker wird gemalt).
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
    //  ── Geteiltes Band (`wrapText="bothSides"`) ──────────────────────────────
    //  Ein Bild mit Umfluss auf BEIDEN Seiten zerlegt eine Zeile in ein Stück
    //  links und eines rechts davon. Beide sind eigene Bänder mit DERSELBEN `y`;
    //  vorgerückt wird erst nach dem rechten Stück, und beide bekommen die Höhe
    //  des ganzen Bandes (die Paginierung liest sie zeilenweise).
    qreal bandCarryAdv = 0.0, bandCarryVis = 0.0;
    int   leftRowIdx = -1;      // linkes Stück, dessen Höhen noch nachzuziehen sind
    qreal pendingRightX = -1.0; // ≥ 0: nächste Zeile gehört rechts neben das Bild

    //  Band abschließen: Höhen festschreiben und — falls ein Bild darin steht —
    //  Bild und Textzeile auf die UNTERKANTE ausrichten (Word setzt den Text
    //  auf die Grundlinie unter das Bild). `advance == false` schließt nur das
    //  linke Stück eines geteilten Bandes: die y bleibt stehen.
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
                    //  Versatz MERKEN — Caret und Listenmarker folgen ihm.
                    R.textDy = qMax(0.0, rowVis - ln.height());
                    ln.setPosition(QPointF(ln.x(), R.y + R.textDy));
                }
            }
        }
        if (advance) {
            y += rowAdv;
            //  Höhen des linken Stücks nachziehen — beide Stücke eines Bandes
            //  melden dieselbe Höhe, sonst trennte die Paginierung sie.
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

    //  Bilder des Absatzes: im Zeilenfluss (`wp:inline`) oder VERANKERT mit
    //  Textumfluss (`wp:anchor` + `w:wrapSquare`). Verankerte belegen keinen
    //  Platz in der Zeile — der Text weicht ihrem Rechteck aus.
    const QVector<InlineImage> allImgs = d.paragraphImages(b);
    QVector<InlineImage> floats;
    for (const InlineImage& ii : allImgs)
        if (ii.anchored && ii.wrap == InlineImage::WrapSquare) floats.append(ii);
    auto isFloating = [](const InlineImage& ii) {
        return ii.anchored && ii.wrap == InlineImage::WrapSquare;
    };
    //  Verankerte ZUERST setzen: die Zeilen fragen sie beim Auslegen ab. Ihre
    //  Lage steht im Dokument (relativ zu Spalte und Absatz), nicht im Fluss.
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
    //  Bilder, die aus einem VORHERIGEN Absatz hereinragen (Word lässt den
    //  Umfluss nicht am Absatz enden). Einmal geholt, dann wie eigene behandelt.
    const QVector<FloatObstacle> foreign = foreignFloats(blockIdx);

    //  Waagerecht nutzbarer Bereich einer Zeile: verankerte Bilder, die ihr
    //  Höhenband schneiden, drängen sie zur Seite. WOHIN, sagt die Umbruchseite
    //  des Dokuments (`w:wrapSquare` ▸ `wrapText`); nur `largest`/`bothSides`
    //  dürfen selbst wählen — dann gewinnt die breitere Seite.
    //  `pushTo` meldet, bis wohin die Zeile ausweichen MUSS, wenn daneben keine
    //  lesbare Spalte übrig bleibt (= Unterkante des breitesten Störers).
    //  `gapL`/`gapR` melden zusätzlich die LÜCKE eines `bothSides`-Bildes: dort
    //  läuft der Text links UND rechts daneben, das Band wird geteilt. Ohne
    //  Lücke bleibt `gapR <= gapL`.
    auto usableSpan = [&](qreal top, qreal h, qreal* left, qreal* right,
                          qreal* pushTo, qreal* gapL, qreal* gapR) {
        *left = 0.0;
        *right = W;
        *pushTo = top;
        *gapL = *gapR = -1.0;
        qreal lowest = top;
        //  Erstes Bild mit Umfluss BEIDSEITIG. Das „ob" braucht ein eigenes
        //  Flag: `bothL` ist eine KANTE und wird negativ, sobald das Bild am
        //  linken Rand steht und einen Abstand hat (x=0, padL>0). Mit −1 als
        //  Kennzeichen fiel der ganze Zweig dort lautlos aus.
        bool  haveBoth = false;
        qreal bothL = 0.0, bothR = 0.0;
        //  EIN Durchlauf über eigene UND fremde Störer: für die Zeile macht es
        //  keinen Unterschied, ob das Bild in diesem Absatz verankert ist oder
        //  im vorherigen — sie muss ihm so oder so ausweichen.
        auto consider = [&](qreal by, qreal bh, qreal bx, qreal bw,
                            qreal bpadL, qreal bpadR, int side) {
            if (top >= by + bh || top + h <= by) return;        // kein Überlapp
            const qreal bl = bx - bpadL;
            const qreal br = bx + bw + bpadR;
            //  `wrapText="left"` heißt: der Text läuft LINKS vom Bild.
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
        //  Die Lücke gilt nur, wenn auf BEIDEN Seiten eine lesbare Spalte übrig
        //  bleibt — sonst verhält sich das Bild wie `largest` (breitere Seite).
        //  Erst hier zu prüfen ist nötig: andere Bilder desselben Bandes können
        //  die Ränder vorher noch verschoben haben.
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
        //  Bleibt weniger als eine lesbare Spalte, fängt der Text UNTER dem Bild
        //  an, statt sich Wort für Wort durch einen Streifen zu quetschen (am
        //  Beleg: ein seitenbreites Bild ließ 50 px stehen, in denen jedes Wort
        //  eine eigene Zeile bekam). Dieselbe Schwelle wie bei Zeilen-Bildern.
        if (*right - *left < kMinTextBesideImage) {
            *left = 0.0;
            *right = W;
            *pushTo = lowest;      // volle Breite gilt erst UNTER dem Bild
        }
    };

    //  Einen Textabschnitt [from,to) als EIN Stück auslegen. Die erste Zeile
    //  schließt an das laufende Band an (Text NEBEN dem Bild), jede weitere
    //  beginnt ein neues Band über die volle Breite.
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
            //  Umbruch innerhalb des Stücks ⇒ neues Band. Und wenn neben einem
            //  Bild kaum Platz bleibt, fängt der Text lieber darunter an.
            //  Gehört die Zeile ins RECHTE Stück eines geteilten Bandes, bleibt
            //  die y stehen: sie sitzt neben dem linken Stück, nicht darunter.
            const bool toRight = (pendingRightX >= 0.0);
            if (li > 0 || (cur >= 0 && x > 0.0 && W - x < kMinTextBesideImage)) {
                if (toRight) leftRowIdx = cur;
                closeRow(!toRight);
                openRow(from + ln.textStart());
            }
            ensureRow(from + ln.textStart());
            //  Zwei Durchgänge: erst mit voller Breite messen (die Zeilenhöhe
            //  entscheidet, welche verankerten Bilder das Band schneiden), dann
            //  auf den tatsächlich nutzbaren Bereich zurücksetzen.
            ln.setLineWidth(qMax(8.0, W - x));
            qreal availL = 0.0, availR = W, gapL = -1.0, gapR = -1.0;
            //  Auch FREMDE Störer (Bild aus einem vorherigen Absatz, das
            //  hereinragt) lösen den Umfluss aus — ohne `|| !foreign.isEmpty()`
            //  lief der Text quer durch das Bild, weil `usableSpan` gar nicht
            //  erst gefragt wurde.
            if (!floats.isEmpty() || !foreign.isEmpty()) {
                //  Reicht der Platz daneben nicht, rutscht das BAND unter das
                //  Bild — volle Breite an derselben y wäre Text ÜBER dem Bild.
                //  Nur solange das Band noch leer ist; sonst müssten die schon
                //  gesetzten Zeilen-Bilder mitwandern. Das rechte Stück eines
                //  geteilten Bandes darf nie schieben — sein linkes Stück steht
                //  schon.
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
                    //  Rechtes Stück: von der Bildkante bis zum rechten Rand.
                    availL = qMax(availL, pendingRightX);
                    pendingRightX = -1.0;
                    ln.setLineWidth(qMax(8.0, availR - availL));
                } else if (gapR > gapL) {
                    //  Linkes Stück; was nicht hineinpasst, läuft rechts weiter.
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
        //  Endet ein Stück mitten in einem geteilten Band (danach kommt ein Bild
        //  im Zeilenfluss), gilt die offene rechte Hälfte nicht weiter.
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
        //  Bild einsetzen.
        const InlineImage& info = allImgs.at(imgIdx);
        ++imgIdx;
        at = qMax(at, info.pos + 1);
        //  Ein VERANKERTES Bild steht schon an seiner Stelle — hier ist nur
        //  sein Objekt-Zeichen auszusparen. Bliebe es im Text, malte
        //  QTextLayout dafür ein Ersatzkästchen mitten in die Zeile und
        //  belegte dessen Breite (am Beleg neben dem umfließenden Bild zu
        //  sehen). Das Band schluckt die Stelle, damit sie adressierbar bleibt.
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
            closeRow();                       // passt nicht mehr → neues Band
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

    //  Unterkante des BLOCKS = Unterkante seines Textes. Ein verankertes Bild
    //  darf darüber hinausragen; der Überstand wird gemerkt, und die folgenden
    //  Absätze fließen darum herum (s. foreignFloats). Zog der Block früher
    //  seine Unterkante bis unter das Bild, begann der Folgeabsatz IMMER
    //  darunter — auch wenn daneben eine halbe Seite frei war.
    qreal bottom = L.rows.empty()
                       ? L.beforePx + QFontMetricsF(base).height() * spacing
                       : y;
    qreal floatBottom = 0.0;
    for (const ImageBox& B : L.images)
        if (B.floating) floatBottom = qMax(floatBottom, B.y + B.h);
    //  Ein Zell-Absatz (blockIdx < 0) behält das alte Verhalten: eine Zelle
    //  wächst mit ihrem Bild, der Überstand hätte dort keinen Empfänger.
    if (blockIdx < 0) {
        L.floatOverhang = 0.0;
        return qMax(bottom, floatBottom);
    }
    L.floatOverhang = qMax(0.0, floatBottom - bottom);
    return bottom;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tabellen-Anzeige (read-only): Gitter auslegen und zeichnen
// ─────────────────────────────────────────────────────────────────────────────
//  Einen Absatz vermessen, ohne die Block-Layout-Verwaltung zu berühren — wird
//  für ZELL-Absätze gebraucht (dieselben Schrift-/Ausrichtungsregeln wie
//  buildLayout, nur ohne Listenmarker und Absatzabstände).
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
    for (const Run& r : b.runs) {
        if (r.text.isEmpty()) continue;
        const RunFmt rf = d.resolveRun(b, r);
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

//  Bild eines Runs auf die verfügbare Breite einpassen. Gehalten wird NUR das
//  eingepasste QImage, nicht das Original (RAM = Priorität 1); freigegeben wird
//  es mit dem Layout-Fenster (trimLayouts), die Kastenmaße bleiben stehen.
bool DocxTextArea::makeImageBox(const InlineImage& info, qreal avail,
                                ImageBox* out) const {
    if (!out || !m_ctl || !m_ctl->ready()) return false;
    //  Sollmaß aus wp:extent (EMU → Punkt → Pixel).
    constexpr qreal kEmuToPx = kPtToPx / 12700.0;     // 1 pt = 12700 EMU
    const qreal av = avail > 0.0 ? avail : contentWidth();
    qreal w = info.cxEmu > 0 ? info.cxEmu * kEmuToPx : 0.0;
    qreal h = info.cyEmu > 0 ? info.cyEmu * kEmuToPx : 0.0;

    const QByteArray bytes = m_ctl->doc().imageBytes(info.relId);
    QImage src;
    if (!bytes.isEmpty()) src.loadFromData(bytes);

    if (src.isNull()) {
        //  Beziehung/Format unbekannt → Rahmen in Sollgröße statt Fehlanzeige.
        if (w <= 0.0) w = qMin(av, 160.0);
        if (h <= 0.0) h = 90.0;
    } else if (w <= 0.0 || h <= 0.0) {               // kein extent → native Größe
        w = src.width()  * (96.0 / qMax(1, src.dotsPerMeterX() > 0
                                            ? qRound(src.dotsPerMeterX() * 0.0254) : 96));
        h = src.height() * (96.0 / qMax(1, src.dotsPerMeterY() > 0
                                            ? qRound(src.dotsPerMeterY() * 0.0254) : 96));
        if (w <= 0.0 || h <= 0.0) { w = src.width(); h = src.height(); }
    }
    //  Auf die verfügbare Breite einpassen (Seitenverhältnis halten).
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

// ─────────────────────────────────────────────────────────────────────────────
//  Inhaltsverzeichnis
//
//  Das FELD in der Datei bleibt deklarativ (keine eingebackenen Seitenzahlen).
//  Hier wird je Überschrift eine Zeile reserviert; die SEITENZAHL trägt erst
//  `paintToc` ein. Das geht auf, weil die Höhe nur an der ANZAHL der Einträge
//  hängt, nicht an den Zahlen — es braucht also keinen zweiten Layout-Pass.
// ─────────────────────────────────────────────────────────────────────────────
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

    //  Schriftart/-größe des Verzeichnisses: aufgelöstes Format des Absatzes
    //  (der Nutzer setzt es über die Leiste, es landet in w:pPr/w:rPr) — mehr
    //  ist am Verzeichnis bewusst nicht einstellbar.
    const RunFmt def = d.defaultRun();
    const RunFmt tf  = d.paragraphMarkFormat(b);
    L.tocFont = QFont();
    L.tocFont.setFamily(tf.font.isEmpty() ? def.font : tf.font);
    L.tocFont.setPointSizeF(tf.sizePt > 0 ? tf.sizePt : 11.0);
    L.tocFont.setBold(tf.bold);
    L.tocFont.setItalic(tf.italic);
    const QFontMetricsF fm(L.tocFont);
    L.tocLineH = fm.height() + 4.0;

    //  Das Verzeichnis bekommt EIGENE Seiten: es beginnt oben auf einer Seite
    //  und der Text danach fängt auf der nächsten an (s. paginateBlock). Passt
    //  es nicht auf eine Seite, läuft es seitenweise weiter — deshalb die Zahl
    //  der Einträge je Seite.
    const qreal usable = qMax(L.tocLineH, slotHeight() - 8.0);
    L.tocPerPage = qMax(1, int(usable / L.tocLineH));

    //  Auch ein LEERES Verzeichnis braucht Höhe, sonst wäre der Absatz
    //  unsichtbar und nicht mehr anwählbar (zum Löschen).
    const int n = qMax(1, int(L.tocEntries.size()));
    L.height   = n * L.tocLineH + 8.0;
    L.beforePx = 0.0;
    L.indentPx = 0.0;
    L.marker.clear();
    L.laid     = true;
    //  Auch das Verzeichnis legt sich nicht um ein Bild herum. In der Praxis
    //  beginnt es auf einer eigenen Seite, aber verlassen wird sich darauf
    //  nicht — steht ein Bild darüber, das hereinragt, rückt es nach unten.
    shiftBelowForeignFloats(i);
    return true;
}

//  1-basierte Seitenzahl eines Blocks (Verzeichnis-Einträge).
int DocxTextArea::pageOfBlock(int i) {
    return pageOfEntry(i, 0);
}

//  Dasselbe für EINE STELLE im Block: Zeichenposition → Zeilenband → Stück →
//  Slot → Seite. Nötig, weil ein Überschrift-Absatz mehrere Verzeichnis-Einträge
//  tragen kann (nur durch `w:br` getrennt) und dabei über eine Seitengrenze
//  laufen darf — die Zeilen liegen dann in verschiedenen Stücken.
int DocxTextArea::pageOfEntry(int i, int pos) {
    if (i < 0 || i >= int(m_lay.size())) return 1;
    ensureOffsetsTo(i + 1);
    const BlockLayout& L = m_lay[size_t(i)];
    if (L.segs.isEmpty()) return 1;
    return segAt(i, lineForPos(L, pos)).slot / qMax(1, colCount()) + 1;
}

//  Ein abgelegtes Bild an den Absatz hängen, über dessen TEXT seine Oberkante
//  liegt. Gemessen wird am Text, nicht an der Blockhöhe: ein verankertes Bild
//  bläht seinen eigenen Absatz auf (der Block reicht bis zu seiner Unterkante),
//  sonst läge es immer über „seinem" Absatz und wechselte nie.
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
    //  Texthöhe eines Absatzes OHNE seine verankerten Bilder.
    auto textHeight = [&](int i) {
        ensureLaid(i);
        const BlockLayout& L = m_lay[size_t(i)];
        return qMax(L.beforePx + 1.0, linesBottom(L));
    };

    const BlockLayout& L0 = m_lay[size_t(src)];
    qreal top = L0.beforePx + yMm * kMmToPx;     // Oberkante im Quellabsatz
    int dst = src;

    if (top > textHeight(src)) {
        //  Nach UNTEN: den überstehenden Rest durch die folgenden Absätze
        //  reichen. Reicht er über den letzten hinaus, gilt der letzte — sonst
        //  bliebe ein Ablegen unterhalb des Textes wirkungslos.
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
        //  Nach OBEN: dasselbe rückwärts, mit demselben Anschlag am ersten Absatz.
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

    //  ── O7b: auf das SEGMENT des Ablegepunkts klemmen ───────────────────────
    //  Die Bildlage ist ABSATZrelativ, die Seitenaufteilung ZEILENrelativ.
    //  Läuft der Zielabsatz über eine Seitenkante, steht sein Anfang oben auf
    //  der vorherigen Seite — ein Versatz, der auf der Folgeseite abgelegt
    //  wurde, schob das Bild dann unter den Textbereich, wo der Seiten-Clip es
    //  abschnitt (sichtbar erst beim erneuten Ziehen). Also die Ziel-y so
    //  klemmen, dass das Bild im Stück seines Ablegepunkts bleibt.
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
        //  Auch ohne Absatzwechsel gilt die Klemmung — sonst rutschte das Bild
        //  im eigenen, über die Seitenkante laufenden Absatz aus dem Textbereich.
        const qreal yClamped = (top - m_lay[size_t(src)].beforePx) / kMmToPx;
        m_ctl->setImagePositionMm(block, xMm, qMax(0.0, yClamped));
        return;
    }
    //  `yMm` zählt ab der Oberkante des ZIELabsatzes (ohne dessen Abstand davor,
    //  genau wie `posYEmu` im Dokument).
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

    //  Nur die Einträge DIESER Seite (das Verzeichnis kann über mehrere
    //  laufen — s. paginateBlock).
    const int from = qBound(0, firstEntry, int(L.tocEntries.size()));
    const int to   = qMin(int(L.tocEntries.size()),
                          from + qMax(1, L.tocPerPage));
    qreal ly = y + 4.0;
    for (int ei = from; ei < to; ++ei) {
        const Docx::TocEntry& e = L.tocEntries.at(ei);
        const qreal indent = (e.level - 1) * 18.0;
        const QString page = QString::number(pageOfEntry(e.block, e.pos));
        const qreal pw = fm.horizontalAdvance(page);
        //  Text links (bei Bedarf gekürzt), Seitenzahl rechtsbündig, dazwischen
        //  eine Punktreihe — wie in Word.
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

//  Kopf-/Fußzeile EINMAL auslegen (auf allen Seiten dieselbe) und für jede Seite
//  wiederverwenden — sie hängt nicht am Fluss und darf ihn nicht verschieben.
void DocxTextArea::ensureHeaderFooter() {
    if (!m_ctl || !m_ctl->ready()) return;
    //  Wird die Kopf-/Fußzeile SELBST bearbeitet, steht sie schon als Inhalt
    //  auf der Seite — sie dann zusätzlich in den Rand zu malen wäre doppelt.
    if (m_ctl->activeRegionInt() != DocxEditController::Body) return;
    const Document& d = m_ctl->doc();
    const qreal w = contentWidth() * colCount()
                    + colSpacePx() * (colCount() - 1);
    for (int pass = 0; pass < 2; ++pass) {
        RunningPart& part = (pass == 0) ? m_header : m_footer;
        if (part.built) continue;
        part.built = true;
        const HeaderFooter hf = d.headerFooter(pass == 1, false);
        if (!hf.ok) continue;
        qreal total = 0.0;
        for (const Block& p : hf.paragraphs) {
            qreal ph = 0.0;
            part.paras.push_back(layoutParagraph(p, w, &ph));
            total += ph;
        }
        part.height = total;
    }
}

void DocxTextArea::paintHeaderFooter(QPainter* p, int page) {
    ensureHeaderFooter();
    const qreal x = marLpx();
    //  Kopfzeile SITZT IM oberen Rand (über dem Textbereich), Fußzeile im
    //  unteren — genau dafür ist der Rand da. Läuft der Teil höher als der
    //  Rand, wird er oben/unten abgeschnitten statt in den Text zu laufen.
    if (m_header.height > 0.0) {
        p->save();
        p->setClipRect(QRectF(x, pageDocY(page), pageWpx() - marLpx(),
                              qMax(0.0, marTpx() - 4.0)), Qt::IntersectClip);
        qreal y = pageDocY(page) + qMax(4.0, marTpx() - m_header.height - 8.0);
        for (const auto& lay : m_header.paras) {
            if (!lay) continue;
            lay->draw(p, QPointF(x, y));
            qreal h = 0.0;
            for (int ln = 0; ln < lay->lineCount(); ++ln)
                h = qMax(h, lay->lineAt(ln).y() + lay->lineAt(ln).height());
            y += h;
        }
        p->restore();
    }
    if (m_footer.height > 0.0) {
        const qreal top = pageDocY(page) + pageHpx() - sect().marBottom * kTwipToPx + 4.0;
        p->save();
        p->setClipRect(QRectF(x, top, pageWpx() - marLpx(),
                              qMax(0.0, sect().marBottom * kTwipToPx - 4.0)),
                       Qt::IntersectClip);
        qreal y = top;
        for (const auto& lay : m_footer.paras) {
            if (!lay) continue;
            lay->draw(p, QPointF(x, y));
            qreal h = 0.0;
            for (int ln = 0; ln < lay->lineCount(); ++ln)
                h = qMax(h, lay->lineAt(ln).y() + lay->lineAt(ln).height());
            y += h;
        }
        p->restore();
    }
}

int DocxTextArea::tableAnchorOf(int i) const {
    if (!m_ctl || !m_ctl->ready() || i < 0 || i >= m_ctl->doc().blocks.size())
        return -1;
    const Block& b = m_ctl->doc().blocks.at(i);
    if (b.tableId < 0) return -1;
    return m_ctl->doc().tableFirstBlock(b.tableId);
}

//  Gitter einer FLACH zerlegten Tabelle aus ihren lebenden Zellblöcken bauen.
//  Der Anker trägt die Gesamthöhe (Fluss bleibt monoton, die Tabelle ist damit
//  für die Paginierung ein Stück); jeder Zellblock bekommt seine Lage RELATIV
//  zum Anker. Bearbeiten einer Zelle ändert deren Höhe → Zeilenhöhe →
//  Tabellenhöhe; deshalb wird bei jeder Änderung der ANKER neu ausgelegt
//  (s. invalidateFrom).
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

    //  Spaltenbreiten wie im Anzeige-Pfad: Gitter, sonst gleichmäßig; zu breite
    //  Tabellen werden proportional in die Textspalte eingepasst.
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

            //  Absätze DIESER Zelle sind die nächsten Blöcke mit (row,col).
            const int colOfCell = c - firstCell;
            qreal yInCell = pad;
            while (blockAt < d.blocks.size()
                   && d.blocks.at(blockAt).tableId == tid
                   && d.blocks.at(blockAt).row == r
                   && d.blocks.at(blockAt).col == colOfCell) {
                BlockLayout& CB = m_lay[size_t(blockAt)];
                qreal ph = 0.0;
                //  Zell-Absätze laufen über DENSELBEN Weg wie der Fließtext:
                //  auf die Zellbreite eingepasste Zeilenbänder samt Bildern —
                //  sonst stünde dort das nackte Objekt-Zeichen und die Zelle
                //  hätte die falsche Höhe.
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

    //  Der Anker hat ZWEI Rollen: er trägt das Gitter (isTable) UND ist der
    //  erste Absatz der ersten Zelle (isCell) — die Zellschleife oben hat seine
    //  Flags dabei überschrieben, deshalb hier wiederherstellen. Genau das war
    //  der Grund, warum jeder Klick in der Tabelle im Anker landete.
    A.isTable = true;
    A.height  = yRow + 6.0;
    A.table   = std::move(tl);
    A.laid    = true;
    shiftBelowForeignFloats(anchor);
}

//  Ein Block, der NICHT umfließen kann, weicht einem hereinragenden Bild nach
//  UNTEN aus (Word schiebt eine Tabelle unter das Bild, statt sie darum
//  herumzulegen). Gerechnet wird erst HIER, beim Auslegen des Blocks selbst:
//  dann stehen die Höhen aller Vorgänger fest — ein Deckeln beim Bild-Absatz
//  hätte mit deren SCHÄTZhöhen gerechnet und die Folgeabsätze zu weit
//  nach unten geschoben (gemessen).
void DocxTextArea::shiftBelowForeignFloats(int i) {
    if (i < 0 || i >= int(m_lay.size())) return;
    BlockLayout& L = m_lay[size_t(i)];
    const qreal pad = foreignFloatBottom(i);
    L.topPad = qMax(0.0, pad);
    if (L.topPad <= 0.0) return;
    L.height += L.topPad;
    //  Gitter und Zelltext tragen ihre Lage explizit — beide mitschieben,
    //  dann folgen Zeichnen, Treffersuche und `docYForLine` von allein.
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
    //  Gezeichnet wird immer aus dem ROH-Bereich der Tabelle — egal ob sie flach
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
        //  Nicht deutbar → wie bisher eine Platzhalterzeile.
        L.isTable = false;
        L.height  = 34;
        L.laid    = true;
        return;
    }
    L.isTable = true;

    //  Spaltenbreiten: Gitter aus w:tblGrid, sonst gleichmäßig. Ist die Tabelle
    //  breiter als die Textspalte, wird sie proportional eingepasst — eine
    //  Tabelle, die über den Seitenrand hinausläuft, wäre schlimmer als eine
    //  leicht gestauchte.
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
                    //  Verschachtelte Tabelle: Platzhalterzeile.
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
        //  Zeilenhöhe = höchste Zelle (mindestens eine leere Textzeile).
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
}

void DocxTextArea::paintTable(QPainter* p, const BlockLayout& L, qreal left, qreal y,
                              int rowFrom, int rowTo) {
    if (!L.table) return;
    const int nRows = int(L.table->rows.size());
    const int r0 = qBound(0, rowFrom, nRows);
    const int r1 = (rowTo < 0) ? nRows : qBound(r0, rowTo, nRows);
    const qreal pad = 108.0 * kTwipToPx;
    p->save();
    //  Nur die Zeilen DIESES Stücks. Getrennt wird an Zeilengrenzen, deshalb
    //  schließt das Gitter oben und unten von allein: jede gezeichnete Zeile
    //  malt ihr eigenes Rechteck.
    for (int ri = r0; ri < r1; ++ri) {
        const RowLayout& row = L.table->rows[size_t(ri)];
        for (const CellLayout& cell : row.cells) {
            const QRectF cr(left + cell.x, y + row.y, cell.w, row.h);
            //  Gitterlinien (Word-Standardtabelle). Eigene Rahmen-/Schattierungs-
            //  Angaben aus w:tcBorders/w:shd werden bewusst nicht gedeutet.
            p->setPen(QPen(QColor(140, 140, 148), 0.8));
            p->setBrush(Qt::NoBrush);
            p->drawRect(cr);

            qreal py = cr.y() + pad;
            //  Bei flach zerlegten Tabellen ist `paras` leer — der Zelltext kommt
            //  dann aus den echten Blöcken (s. paintSlot).
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
    //  ausgelegt. Der Anker baut ALLE Zellen — einzeln ginge es nicht, weil die
    //  Zeilenhöhe von den Nachbarzellen abhängt.
    {
        const int anchor = tableAnchorOf(i);
        if (anchor >= 0 && anchor != i) {
            //  Ein BILD-Absatz in der Zelle hat bewusst kein QTextLayout — ohne
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
        if (!qFuzzyCompare(oldH + 1, m_lay[i].height + 1))
            m_offsetsValidTo = qMin(m_offsetsValidTo, i);
        //  Der Überstand eines verankerten Bildes bestimmt das Layout der
        //  FOLGENDEN Blöcke mit — ändert er sich, sind deren Zeilen veraltet.
        //  Nur vorwärts markieren (kein Zyklus), das Neuauslegen bleibt lazy.
        if (!qFuzzyCompare(oldOver + 1, m_lay[i].floatOverhang + 1)) {
            invalidateFloatFollowers(i, qMax(oldOver, m_lay[i].floatOverhang));
            m_offsetsValidTo = qMin(m_offsetsValidTo, i);
        }
    }
}

//  Verteilt EINEN Block auf Spalten-Slots. Rückgabe: Fluss-y direkt nach ihm.
//  Ein Absatz, der die Slot-Grenze überschreitet, wird ZEILENWEISE getrennt
//  (mehrere Segmente) — genau das macht die Ansicht seitengenau. Ist er nicht
//  trennbar (Platzhalter, noch nicht vermessen, einzeilig), wandert er
//  vollständig in den nächsten Slot; ist er höher als ein Slot, läuft er über
//  (der Fluss bleibt dabei monoton, s. Koordinaten-Kommentar im Header).
// ─────────────────────────────────────────────────────────────────────────────
//  Fußnoten am Seitenfuß
// ─────────────────────────────────────────────────────────────────────────────
//  Abstände des Bereichs: Luft über der Trennlinie, die Linie selbst, Luft
//  darunter. Bewusst klein — der Bereich soll den Textbereich nicht auffressen.
namespace { constexpr qreal kFootGapTop = 8.0, kFootGapBottom = 4.0;
            constexpr int   kFootPasses = 3; }

qreal DocxTextArea::footHeightOf(int slot) const {
    return m_footHeight.value(slot, 0.0);
}

//  Was in einen Slot PASST. Die Slot-Höhe selbst bleibt uniform (Invariante),
//  nur die Kapazität schrumpft um den Fußnotenbereich. Mindestens eine
//  Textzeile bleibt stehen: eine Seite ohne jeden Platz für Text wäre eine
//  Endlosschleife.
qreal DocxTextArea::slotCapacity(int slot) const {
    const qreal h = slotHeight();
    const qreal f = footHeightOf(slot);
    if (f <= 0.0) return h;
    return qMax(h * 0.25, h - f);
}

const DocxTextArea::FootLayout& DocxTextArea::footLayout(int fnIndex) {
    const auto it = m_footLay.find(fnIndex);
    if (it != m_footLay.end()) return it->second;
    FootLayout fl;
    if (m_ctl && m_ctl->ready()) {
        const QVector<Docx::Footnote>& fns = m_ctl->doc().footnotes();
        if (fnIndex >= 0 && fnIndex < fns.size()) {
            const qreal w = qMax(40.0, contentWidth());
            for (const Docx::Block& b : fns.at(fnIndex).paragraphs) {
                qreal ph = 0.0;
                //  Derselbe Weg wie Tabellenzellen: ein Absatz OHNE die
                //  Block-Layout-Verwaltung, nur vermessen und gezeichnet.
                auto lay = layoutParagraph(b, w, &ph);
                if (!lay) continue;
                fl.height += ph;
                fl.paras.push_back(std::move(lay));
            }
        }
    }
    return m_footLay.emplace(fnIndex, std::move(fl)).first->second;
}

//  Welche Fußnote steht in welchem Slot? Gelesen wird die Lage des VERWEISES
//  (der Run mit `footnoteId`), also erst nach der Paginierung sinnvoll.
bool DocxTextArea::recomputeFootnoteSlots() {
    QHash<int, QVector<int>> next;
    if (m_ctl && m_ctl->ready()) {
        const Document& d = m_ctl->doc();
        const QVector<Docx::Footnote>& fns = d.footnotes();
        //  `w:id` → Index in der Liste (die Reihenfolge der Datei).
        QHash<int, int> byId;
        for (int i = 0; i < fns.size(); ++i) byId.insert(fns.at(i).id, i);

        for (int bi = 0; bi < int(m_lay.size()) && bi < d.blocks.size(); ++bi) {
            const Docx::Block& b = d.blocks.at(bi);
            int pos = 0;
            for (const Docx::Run& r : b.runs) {
                if (r.footnoteId > 0) {
                    const auto it = byId.constFind(r.footnoteId);
                    if (it != byId.cend()) {
                        const BlockLayout& L = m_lay[size_t(bi)];
                        //  Zeile des Verweises → Segment → Slot.
                        const int li = hasText(L) ? lineForPos(L, pos) : 0;
                        const int slot = segAt(bi, li).slot;
                        next[slot].append(it.value());
                    }
                }
                pos += r.text.size();
            }
        }
    }
    if (next == m_footOfSlot) return false;
    m_footOfSlot = next;
    //  Höhen neu bestimmen.
    m_footHeight.clear();
    for (auto it = m_footOfSlot.cbegin(); it != m_footOfSlot.cend(); ++it) {
        qreal h = kFootGapTop + 1.0 + kFootGapBottom;
        for (int idx : it.value()) h += footLayout(idx).height;
        m_footHeight.insert(it.key(), h);
    }
    return true;
}

//  Mehrpass mit Stabilitätstest: die Höhe des Bereichs hängt davon ab, WELCHE
//  Verweise auf der Seite landen — und das hängt an der Paginierung, die die
//  Höhe braucht. Zwei Läufe mit gleicher Zuordnung heißen stabil; nach
//  `kFootPasses` wird abgebrochen und der letzte Stand genommen (eine
//  schwingende Zuordnung darf die Anzeige nicht aufhängen).
void DocxTextArea::settleFootnotes() {
    if (m_inFootSettle) return;
    if (!m_ctl || !m_ctl->ready()) return;
    if (m_ctl->doc().footnotes().isEmpty()) return;      // Normalfall: nichts zu tun
    m_inFootSettle = true;
    for (int pass = 0; pass < kFootPasses; ++pass) {
        ensureOffsetsTo(int(m_lay.size()));
        if (!recomputeFootnoteSlots()) break;            // stabil
        m_offsetsValidTo = 0;                            // Kapazität geändert
    }
    m_inFootSettle = false;
}

qreal DocxTextArea::paginateBlock(int idx, qreal flowStart, qreal slotH) {
    //  Ein Block, der die Slot-Grenze überschreitet, muss zeilenweise getrennt
    //  werden KÖNNEN — dafür braucht er sein Layout. `trimLayouts` gibt Layouts
    //  außerhalb des Fensters frei; hinge die Trennung daran, ob eines gerade
    //  da ist, wäre die Seitenzahl von Lauf zu Lauf eine andere (im Prüfstand
    //  beobachtet: 18 vs. 19 Seiten fürs selbe Dokument). Deshalb wird hier
    //  notfalls nachvermessen — das trifft ~einen Block je Seitengrenze, nicht
    //  das Dokument.
    {
        const BlockLayout& L0 = m_lay[size_t(idx)];
        const int slot0 = int(flowStart / slotH);
        const qreal yInSlot0 = flowStart - slot0 * slotH;
        //  Maßgeblich ist, ob die ZEILEN vorliegen — nicht, ob der Block je
        //  vermessen wurde. `trimLayouts` leert `rows` und lässt `isText`/`laid`
        //  stehen (Höhe und Offsets sollen ja gültig bleiben); ein so
        //  freigegebener Absatz galt hier deshalb als untrennbar und wanderte
        //  ganz in den nächsten Slot. Genau die Abhängigkeit vom Layout-Fenster,
        //  die dieser Zweig verhindern soll.
        //  Für eine TABELLE gilt dasselbe eine Ebene höher: `trimLayouts` gibt
        //  auch das Gitter frei, und ohne Gitter kennt die Trennung unten keine
        //  Zeilengrenzen — die Tabelle wanderte wieder als Ganzes weiter.
        const bool needsGrid = L0.isTable && !L0.table;
        const bool needsLines = (!hasText(L0) || L0.rows.empty())
                                && m_ctl && m_ctl->ready()
                                && m_ctl->doc().blocks.at(idx).kind == Block::Paragraph;
        if (L0.height > 0.0 && (needsGrid || needsLines)
            && yInSlot0 + L0.height > slotCapacity(slot0)
            && m_ctl && m_ctl->ready())
            ensureLaid(idx);
    }
    BlockLayout& L = m_lay[size_t(idx)];
    L.segs.clear();
    const int nCols = colCount();
    int   slot = int(flowStart / slotH);
    qreal y    = flowStart - slot * slotH;
    const qreal h = L.height;

    //  Unsichtbare Blöcke (w:sectPr, Kommentare) belegen keinen Platz.
    if (h <= 0.0) {
        L.segs.append({ slot, 0, y });
        return flowStart;
    }

    //  ── Inhaltsverzeichnis: EIGENE Seiten ───────────────────────────────────
    //  Es beginnt oben auf einer frischen Seite, belegt sie allein und schiebt
    //  den Text danach auf die nächste. Passt es nicht auf eine Seite, läuft es
    //  seitenweise weiter — jede dieser Seiten trägt nur Verzeichnis.
    if (L.isToc) {
        const int pageSlots = qMax(1, nCols);
        //  An den Seitenanfang, falls wir nicht ohnehin dort stehen — aber NUR,
        //  wenn auf dieser Seite schon etwas Sichtbares steht. Ein leerer
        //  Absatz davor (der Normalfall: das Verzeichnis wird ganz oben
        //  eingefügt, über ihm bleibt die alte Leerzeile) hat zwar Höhe, aber
        //  keine Tinte — ohne diese Prüfung schöbe er das Verzeichnis auf
        //  Seite 2 und ließe Seite 1 leer (Nutzerbefund).
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
        //  Der nächste Block fängt auf der Seite DANACH an.
        return qreal(slot + usedPages * pageSlots) * slotH;
    }

    const int nLines = lineCount(L);
    const QString text = blockText(L);
    const bool hasExplicitBreak = text.contains(kPageBreak);

    //  Einfachster und häufigster Fall: passt komplett, kein erzwungener Umbruch.
    //  Gemessen wird gegen die KAPAZITÄT des Slots (Slot-Höhe minus
    //  Fußnotenbereich) — die Slot-HÖHE selbst bleibt uniform, sonst bräche die
    //  Fluss-Invariante `Fluss-y = slot · slotHeight() + y`.
    if (y + h <= slotCapacity(slot) && !hasExplicitBreak) {
        L.segs.append({ slot, 0, y });
        return slot * slotH + y + h;
    }

    //  ── Tabelle: an ZEILENgrenzen trennen ───────────────────────────────────
    //  Der Anker trägt die ganze Tabelle in EINEM Block; für die Zeilen-Logik
    //  unten ist er einzeilig und wäre damit „nicht trennbar". Ohne diesen
    //  Zweig reserviert der Fluss den Platz, gezeichnet würde aber nur das
    //  erste Stück — dahinter blieben Seiten vollständig leer.
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
            //  Steht das laufende Stück noch bei seiner ersten Zeile, wird es
            //  ganz verschoben statt ein leeres zurückzulassen — wie beim Text.
            if (rBot > slotCapacity(slot) && !(first && segTop <= 0.0)) {
                ++slot;
                segTop = 0.0;
                segFirstY = rows[size_t(r)].y;
                if (first) L.segs.last() = { slot, r, segTop };
                else       L.segs.append({ slot, r, segTop });
            }
        }
        const PageSeg& lastSeg = L.segs.constLast();
        const qreal tableBottom = rows.back().y + rows.back().h;
        //  Der Abstand NACH der Tabelle steckt in L.height, nicht im Gitter.
        const qreal trailing = qMax(0.0, h - tableBottom);
        return lastSeg.slot * slotH + lastSeg.yInSlot
               + (tableBottom - segOriginY(L, lastSeg)) + trailing;
    }

    //  Nicht trennbar → ganz in den nächsten Slot.
    if (nLines <= 1) {
        if (y > 0.0) { ++slot; y = 0.0; }
        L.segs.append({ slot, 0, y });
        return slot * slotH + y + h;
    }

    //  Zeilenweise verteilen. `segFirstY` ist die Layout-y der ersten Zeile des
    //  laufenden Segments — daraus ergibt sich die Slot-y jeder Folgezeile.
    L.segs.append({ slot, 0, y });
    qreal segTop = y;
    qreal segFirstY = 0.0;                 // Zeile 0 liegt bei y = beforePx
    for (int li = 0; li < nLines; ++li) {
        const qreal lnY   = lineTop(L, li);
        const qreal lnTop = segTop + (lnY - segFirstY);
        const qreal lnBot = lnTop + lineHeight(L, li);
        const bool first  = (li == segFirstLine(L, L.segs.constLast()));
        if (lnBot > slotCapacity(slot) && !(first && segTop <= 0.0)) {
            //  Zeile passt nicht mehr: neues Segment im nächsten Slot. Steht das
            //  laufende Segment noch bei seiner ersten Zeile, wird es komplett
            //  verschoben statt ein leeres zurückzulassen.
            ++slot;
            segTop = 0.0;
            segFirstY = lnY;
            if (first) L.segs.last() = { slot, li, segTop };
            else       L.segs.append({ slot, li, segTop });
        }
        //  Erzwungener Seitenumbruch: alles NACH dieser Zeile beginnt auf der
        //  nächsten SEITE (nicht nur in der nächsten Spalte).
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

    //  Fluss-y hinter dem letzten Segment: verbrauchte Layout-Höhe plus der
    //  Abstand DANACH (der steckt in L.height, nicht in den Zeilen).
    const qreal layoutBottom = linesBottom(L);
    const qreal trailing = qMax(0.0, h - layoutBottom);
    const PageSeg& last = L.segs.constLast();
    return last.slot * slotH + last.yInSlot
           + (layoutBottom - segOriginY(L, last)) + trailing;
}

//  Gefragt ist TINTE, nicht Höhe: ein leerer Absatz belegt Platz, ist aber
//  nichts, wofür sich eine eigene Seite lohnt. Gelesen wird deshalb das
//  DOKUMENT (Textlänge, Art), nicht das Layout — die Blöcke davor sind zwar
//  paginiert, aber nicht zwingend schon vermessen.
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
        //  Platzhalterzeile, Tabelle, Text oder Bild (dessen U+FFFC zählt mit).
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
    }
    if (m_offsets.isEmpty()) return;
    const qreal slotH = slotHeight();
    if (m_offsetsValidTo == 0) {
        m_offsets[0] = 0.0;
        if (!m_lay.empty()) m_offsets[1] = paginateBlock(0, 0.0, slotH);
        m_offsetsValidTo = qMin(1, int(m_lay.size()));
    }
    for (int k = m_offsetsValidTo + 1; k <= i; ++k)
        m_offsets[k] = paginateBlock(k - 1, m_offsets[k - 1], slotH);
    m_offsetsValidTo = qMax(m_offsetsValidTo, i);

    //  Seitenzahl steht erst fest, wenn der ganze Fluss paginiert ist.
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
    //  Binäre Suche über die Präfix-Offsets.
    int lo = 0, hi = int(m_lay.size()) - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (m_offsets[mid] <= y) lo = mid; else hi = mid - 1;
    }
    return lo;
}

void DocxTextArea::layoutChunk() {
    if (!m_ctl || !m_ctl->ready()) { m_chunkTimer.stop(); return; }
    int done = 0;
    while (m_layChunkAt < int(m_lay.size()) && done < kChunk) {
        //  ÜBER ensureLaid, nicht direkt über buildLayout: nur ensureLaid
        //  vergleicht die Schätzhöhe mit der vermessenen und macht die
        //  Präfix-Offsets ab dieser Stelle ungültig. Ohne das blieb der ganze
        //  Fluss auf den Schätzwerten stehen (rebuildAll paginiert das
        //  Dokument einmal mit ihnen), das Dokument lag beim ersten Öffnen
        //  zusammengeschoben da und rückte erst durch die erste Änderung an
        //  seinen Platz — die invalidiert die Offsets ohnehin.
        if (!m_lay[m_layChunkAt].laid) { ensureLaid(m_layChunkAt); ++done; }
        ++m_layChunkAt;
    }
    updateContentHeight();
    if (m_layChunkAt >= int(m_lay.size())) {
        m_chunkTimer.stop();
        updateCursorRect();
    }
    //  Schon WAEHREND des Initial-Layouts trimmen: sonst laegen bei einem
    //  400-Seiten-Dokument kurzzeitig alle Layouts gleichzeitig im Speicher
    //  (genau der Spitzenwert, den das Fenster vermeiden soll). m_trimLo wird
    //  zurueckgesetzt, damit die gerade neu gebauten Layouts erfasst werden.
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
        return;                       // Fenster unveraendert → nichts zu tun
    m_trimLo = lo;
    m_trimHi = hi;

    //  Cursor-Block ausnehmen: updateCursorRect/moveCursorVertical brauchen sein
    //  Layout bei jedem Tastendruck — ihn wegzuwerfen kostete mehr, als er belegt.
    const int cursorBlock = (m_ctl && m_ctl->ready()) ? m_ctl->cursor().block : -1;
    for (int i = 0; i < n; ++i) {
        if ((i >= lo && i <= hi) || i == cursorBlock)
            continue;
        //  NUR das Layout freigeben — `laid`/`height` bleiben gueltig, damit
        //  weder Praefix-Offsets noch die Inhaltshoehe neu berechnet werden.
        //  Fuer Tabellen gilt dasselbe: das Gitter-Layout haelt je Zelle
        //  QTextLayouts und ist damit der groesste Einzelposten im Fenster.
        m_lay[i].pieces.clear();
        m_lay[i].rows.clear();
        m_lay[i].table.reset();
        //  Das eingepasste Bild ist der groesste Einzelposten je Block. Die
        //  KASTENMASSE bleiben stehen, die Hoehe aendert sich also nicht.
        for (ImageBox& B : m_lay[i].images) B.img = QImage();
        m_lay[i].trimmed = true;
    }
}

//  Höhe EINER Zeile der Grundschrift, in ITEM-Pixeln. Grundlage der
//  Rad-Schrittweite: ein Textdokument scrollt in ZEILEN. Ein Anteil der
//  Fensterhöhe war viermal so weit wie in jedem Texteditor (Nutzerbefund
//  „Scrollen ist zu schnell") und hing außerdem an der Fenstergröße — auf einem
//  großen Schirm raste dasselbe Rad schneller.
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
    //  Fußnoten setzen die Kapazität der Seiten herab — das ändert die
    //  Aufteilung und damit die Seitenzahl. Der Mehrpass läuft hier, weil hier
    //  ohnehin über das ganze Dokument paginiert wird; ohne Fußnoten ist er ein
    //  No-op und kostet einen Vergleich.
    settleFootnotes();
    //  Scrollbare Höhe = SEITENSTAPEL in Item-Pixeln (nicht die Fluss-Höhe:
    //  die kennt weder Seitenränder noch die Lücken zwischen den Seiten).
    const qreal h = docHeight() * m_scale;
    if (!qFuzzyCompare(m_contentHeight + 1, h + 1)) {
        m_contentHeight = h;
        emit contentHeightChanged();
        //  Klemmen, falls der Inhalt geschrumpft ist.
        setContentY(m_contentY);
    }
}

//  Ein leerer Absatz wird mit dem am Cursor geltenden Format vermessen
//  (s. buildLayout) — ändert sich dieser Bezug, muss sein Layout neu entstehen.
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

    //  Höhe/Grundlinie aus dem am Cursor WIRKSAMEN Zeichenformat (inkl.
    //  Pending) statt aus der Zeilenhöhe: Die Zeile ist so hoch wie ihr
    //  größtes Zeichen — der Caret muss aber die Größe zeigen, in der das
    //  NÄCHSTE Zeichen erscheint. Genau das fehlte (Nutzerbefund: „Caret wird
    //  bei Schriftgrößen-Wechsel nicht aktualisiert").
    const RunFmt cf = m_ctl->caretFormat();
    QFont f;
    f.setFamily(cf.font.isEmpty() ? m_ctl->doc().defaultRun().font : cf.font);
    f.setPointSizeF(cf.sizePt > 0 ? cf.sizePt : m_ctl->doc().defaultRun().sizePt);
    f.setBold(cf.bold);
    f.setItalic(cf.italic);
    const QFontMetricsF fm(f);

    //  Caret in DOKUMENT-Koordinaten aufbauen (Seite + Spalte des Cursors, s.
    //  Segmente) und erst am Ende in Item-Pixel umrechnen — die Property wird
    //  von QML zum Mitscrollen benutzt und muss dieselbe Einheit wie contentY
    //  haben.
    const int li = lineForPos(L, c.pos);
    qreal x = docXForBlock(bi, li);
    qreal y = docYForLine(bi, 0) + L.beforePx;
    qreal h = fm.height();
    if (lineCount(L) > 0) {
        x += xForPos(L, li, c.pos);
        //  An der GRUNDLINIE der Zeile ausrichten (nicht an der Bandoberkante)
        //  — sonst „schwebt" ein kleiner Caret in einer hohen Mischzeile, und
        //  neben einem Bild stünde er oben, während der Text unten erscheint.
        y = docYForLine(bi, li) + (lineTextTop(L, li) - lineTop(L, li))
            + lineAscent(L, li) - fm.ascent();
    }
    //  Gespeichert in DOKUMENT-Pixeln (paint zeichnet darin); die Properties
    //  cursorY/cursorH rechnen für QML in Item-Pixel um.
    m_cursorRect = QRectF(x, y, 1.6, h);
    emit cursorRectChanged();
    updateImageSelection();
    if (currentPage() != m_lastPage) {
        m_lastPage = currentPage();
        emit currentPageChanged();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bild-Auswahl: ausgewählt ist ein Bild, wenn der Absatz NUR aus ihm besteht
//  (Cursor darin) oder wenn die Selektion genau sein Objekt-Zeichen deckt — so
//  wählt ein Klick auf ein Bild im Fließtext genau dieses aus. Das Rechteck
//  wird in DOKUMENT-Pixeln gemerkt und erst in den Gettern auf Item-Pixel
//  umgerechnet — wie beim Caret, damit Scrollen und Maßstabswechsel keinen
//  Neuaufbau brauchen.
// ─────────────────────────────────────────────────────────────────────────────
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
    //  Tabelle: Rechteck des ANKERS (er trägt Gitter und Gesamthöhe).
    int tid = -1;
    QRectF tr;
    if (m_ctl && m_ctl->ready() && !m_lay.empty()) {
        const int bi = qBound(0, m_ctl->cursor().block, int(m_lay.size()) - 1);
        const int anchor = tableAnchorOf(bi);
        if (anchor >= 0 && anchor < int(m_lay.size())) {
            ensureLaid(anchor);
            const BlockLayout& A = m_lay[size_t(anchor)];
            if (A.isTable && A.table && A.table->width > 0.0) {
                tid = m_ctl->doc().blocks.at(anchor).tableId;
                tr = QRectF(flowDocXForBlock(anchor, 0), flowDocYForLine(anchor, 0),
                            A.table->width, qMax(1.0, A.height - 6.0));
            }
        }
    }
    if (b == m_imgSelBlock && r == m_imgSelDoc && tid == m_tblSelId && tr == m_tblSelDoc)
        return;
    m_imgSelBlock = b;
    m_imgSelDoc   = r;
    m_tblSelId    = tid;
    m_tblSelDoc   = tr;
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

//  Waagerechter Versatz der Seite im Item — identisch zu paint().
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

// ─────────────────────────────────────────────────────────────────────────────
//  Zeichnen
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::paint(QPainter* p) {
    if (!m_ctl || !m_ctl->ready())
        return;
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    p->fillRect(QRectF(0, 0, width(), height()), m_surroundColor);

    ensureOffsetsTo(int(m_lay.size()));

    //  Item-Pixel → Dokument-Pixel: waagerecht zentrieren, senkrecht scrollen,
    //  dann auf den Einpass-Maßstab stellen. Alles Weitere rechnet in
    //  Dokument-Pixeln (Seitengeometrie).
    const qreal s = m_scale;
    const qreal offX = qMax(kSideMargin * s, (width() - pageWpx() * s) / 2.0);
    p->save();
    p->translate(offX, -m_contentY);
    p->scale(s, s);

    //  Sichtbarer Bereich in Dokument-y.
    const qreal viewTop = m_contentY / s;
    const qreal viewBot = (m_contentY + height()) / s;

    const int firstPage = qBound(0, int((viewTop - kPadV) / (pageHpx() + kPageGap)),
                                 qMax(0, m_pageCount - 1));
    const int nCols = colCount();
    for (int page = firstPage; page < m_pageCount; ++page) {
        const qreal pTop = pageDocY(page);
        if (pTop > viewBot) break;
        if (pTop + pageHpx() < viewTop) continue;

        //  Papier mit Schattenkante — jetzt als EINZELNE Seite.
        const QRectF paper(0, pTop, pageWpx(), pageHpx());
        p->fillRect(paper.translated(3.0 / s, 3.0 / s), QColor(0, 0, 0, 45));
        p->fillRect(paper, QColor(255, 255, 255));
        p->setPen(QColor(0, 0, 0, 45));
        p->drawRect(paper);

        paintHeaderFooter(p, page);
        for (int c = 0; c < nCols; ++c) {
            paintSlot(p, page * nCols + c, true);
            paintFootnotes(p, page * nCols + c);
        }
    }
    p->restore();

    //  Layouts weit ausserhalb des Viewports freigeben (nur grosse Dokumente).
    const int fv = blockAtY(qMax(0.0, viewTop - pageDocY(firstPage)) + firstPage * nCols * slotHeight());
    trimLayouts(qMax(0, fv - nCols * 2), qMin(int(m_lay.size()) - 1, fv + nCols * 8));
}

//  Zeichnet die Blöcke EINES Spalten-Slots. Der Painter steht auf
//  Dokument-Pixeln; geclippt wird auf den Textbereich des Slots, damit ein
//  Absatz, der auf der nächsten Seite weiterläuft, hier nicht überstehen kann.
//  Rote Wellenlinie unter den falsch geschriebenen Stellen eines Absatzes.
//  Gezeichnet wird je ZEILE, damit ein über mehrere Zeilen gebrochenes Wort
//  auch mehrfach unterstrichen wird — und in Bild-Absätzen gar nicht.
//  Farbe je Autor — dieselbe Idee wie in Word: stabil über die Sitzung, aus dem
//  Namen abgeleitet, damit zwei Bearbeiter nicht dieselbe bekommen. Bewusst
//  gedeckte Töne (die Markierung soll den Text lesbar lassen).
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
        //  Die Stelle kann über mehrere Zeilen laufen — je Zeile ein Stück.
        int li = lineForPos(L, from);
        while (li >= 0 && li < lineCount(L)) {
            int ls = 0, ll = 0;
            lineTextRange(L, li, &ls, &ll);
            const int a = qMax(from, ls);
            const int b = qMin(to, ls + ll);
            if (b > a) {
                const qreal x0 = origin.x() + xForPos(L, li, a);
                const qreal x1 = origin.x() + xForPos(L, li, b);
                //  Knapp unter der Grundlinie, wie in jedem Textprogramm.
                const qreal yy = origin.y() + lineTextTop(L, li)
                                 + lineAscent(L, li) + 2.0;
                //  Wellenlinie aus kurzen Strichen — eine gepunktete Linie wäre
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

//  Fußnotenbereich EINES Slots: Trennlinie und die Absätze darunter, am
//  unteren Rand des Textbereichs. Gezeichnet wird in Dokument-Pixeln, wie alles
//  andere auch.
void DocxTextArea::paintFootnotes(QPainter* p, int slot) {
    const auto it = m_footOfSlot.constFind(slot);
    if (it == m_footOfSlot.cend() || it.value().isEmpty()) return;
    const qreal h = footHeightOf(slot);
    if (h <= 0.0) return;

    const qreal x = slotDocX(slot);
    const qreal w = contentWidth();
    //  Oberkante des Bereichs: unten im Slot, um seine Höhe nach oben.
    qreal y = slotDocY(slot) + slotHeight() - h + kFootGapTop;

    p->save();
    //  Trennlinie wie in Word: kurz, nicht über die ganze Breite.
    p->setPen(QPen(QColor(120, 120, 128), 0.8));
    p->drawLine(QPointF(x, y), QPointF(x + qMin(w, w * 0.33), y));
    y += 1.0 + kFootGapBottom;

    p->setPen(QColor(30, 30, 30));
    for (int idx : it.value()) {
        const FootLayout& fl = footLayout(idx);
        for (const std::unique_ptr<QTextLayout>& lay : fl.paras) {
            if (!lay) continue;
            lay->draw(p, QPointF(x, y));
            qreal ph = 0.0;
            for (int i = 0; i < lay->lineCount(); ++i)
                ph = qMax(ph, lay->lineAt(i).y() + lay->lineAt(i).height());
            y += ph;
        }
    }
    p->restore();
}

void DocxTextArea::paintSlot(QPainter* p, int slot, bool withCaret) {
    const Document& d = m_ctl->doc();
    const qreal slotH = slotHeight();
    const qreal sx = slotDocX(slot);
    const qreal sy = slotDocY(slot);
    const qreal cw = contentWidth();

    p->save();
    p->setClipRect(QRectF(sx - marLpx(), sy, pageWpx(), slotH), Qt::IntersectClip);

    //  Blöcke dieses Slots über den FLUSS finden (dort ist die Reihenfolge
    //  monoton, s. Koordinaten-Kommentar im Header).
    const qreal flowLo = slot * slotH;
    const qreal flowHi = flowLo + slotH;
    int i = blockAtY(flowLo);
    if (i < 0) { p->restore(); return; }
    //  Zellblöcke tragen keine Höhe: fällt die Slot-Oberkante genau auf das
    //  Ende einer Tabelle, liefert die Fluss-Suche einen von ihnen — der Anker
    //  läge dann VOR dem Startindex und die Tabelle bliebe auf dieser Seite
    //  ungezeichnet. Also auf den Anker zurückgehen.
    {
        const int a = tableAnchorOf(i);
        if (a >= 0 && a < i) i = a;
    }

    int b1, p1, b2, p2;
    const DocxCursor& cur = m_ctl->cursor();
    b1 = cur.aBlock; p1 = cur.aPos; b2 = cur.block; p2 = cur.pos;
    if (b1 > b2 || (b1 == b2 && p1 > p2)) { std::swap(b1, b2); std::swap(p1, p2); }
    const bool hasSel = cur.hasSelection();
    const QColor selBg(38, 118, 216, 110);

    for (; i < int(m_lay.size()); ++i) {
        ensureLaid(i);
        const qreal flowTop = blockTop(i);
        if (flowTop >= flowHi) break;
        const BlockLayout& L = m_lay[i];
        if (L.height <= 0)
            continue;
        //  Ob ein Block hierher gehört, entscheiden ALLEIN seine Segmente —
        //  nicht seine Fluss-Lage. Ein Inhaltsverzeichnis springt auf den
        //  nächsten Seitenanfang, sein Fluss-y bleibt aber davor: eine Abkürzung
        //  über „liegt komplett vor diesem Slot" hat es deshalb verschluckt und
        //  die Seite blieb leer (Nutzerbefund an tests/ER.docx).
        //  Hat der Block in diesem Slot überhaupt ein Stück?
        int segIdx = -1;
        for (int k = 0; k < L.segs.size(); ++k)
            if (L.segs.at(k).slot == slot) { segIdx = k; break; }
        if (segIdx < 0)
            continue;
        const PageSeg& seg = L.segs.at(segIdx);
        const Block& b = d.blocks.at(i);
        //  Ursprung so setzen, dass die erste Zeile des Stücks an seiner
        //  Slot-y landet; ältere/spätere Zeilen fallen ins Clipping.
        const qreal y = sy + seg.yInSlot - segOriginY(L, seg);
        const qreal left = sx;

        //  Reine Zellblöcke zeichnet der Anker mit (sie haben keine Fluss-Lage);
        //  der Anker selbst ist AUCH ein Zellblock und muss hier durch.
        if (L.isCell && !L.isTable)
            continue;
        //  Tabellen-Anker zeichnet Gitter UND — bei flach zerlegten Tabellen —
        //  den Text seiner Zellblöcke an deren expliziten Lagen.
        if (L.isTable && L.table) {
            //  Nur das Stück, das auf diesem Slot liegt (Zeilenbereich).
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
                    //  Zelltext folgt dem Gitter: nur die Zeilen dieses Stücks.
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
            //  Platzhalter (nicht deutbare Fremdblöcke) — Inhalt bleibt intakt.
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
        //  Inhaltsverzeichnis: Einträge mit AKTUELLER Seitenzahl.
        if (L.isToc) {
            paintToc(p, L, left, y, cw, segFirstEntry(L, seg));
            continue;
        }
        if (!hasText(L))
            continue;

        //  Listenmarker.
        if (!L.marker.isEmpty() && lineCount(L) > 0) {
            const RunFmt def = d.defaultRun();
            QFont mf; mf.setFamily(def.font); mf.setPointSizeF(def.sizePt);
            p->setFont(mf);
            p->setPen(QColor(30, 30, 30));
            p->drawText(QPointF(left + L.indentPx - QFontMetricsF(mf).horizontalAdvance(L.marker),
                                y + lineTextTop(L, 0) + lineAscent(L, 0)),
                        L.marker);
        }

        //  Rechtschreibung: rote Wellenlinie UNTER den beanstandeten Stellen.
        //  Gezeichnet wird nur, was ohnehin sichtbar ist — die Fundstellen
        //  liegen im Controller und kosten hier nichts als ein Nachschlagen.
        paintSpell(p, L, i, QPointF(left + L.indentPx, y));

        //  Text samt Selektion (block-lokale Positionen).
        int s0 = -1, s1 = -1;
        if (hasSel && i >= b1 && i <= b2) {
            s0 = (i == b1) ? p1 : 0;
            s1 = (i == b2) ? p2 : textLength(L);
        }
        drawBlockText(p, L, QPointF(left + L.indentPx, y), s0, s1, selBg);

        //  Seitenumbruch-Marker: gestrichelte Linie in der Zeile des Sentinels.
        const QString t = blockText(L);
        for (int pb = t.indexOf(kPageBreak); pb >= 0; pb = t.indexOf(kPageBreak, pb + 1)) {
            const int lb = lineForPos(L, pb);   // Text da ⇒ Zeilen da
            const qreal ly = y + lineTop(L, lb) + lineHeight(L, lb) + 2;
            p->setPen(QPen(QColor(140, 140, 150), 1, Qt::DashLine));
            p->drawLine(QPointF(left, ly), QPointF(left + cw, ly));
            QFont f; f.setPointSizeF(8.0);
            p->setFont(f);
            p->setPen(QColor(130, 130, 140));
            p->drawText(QPointF(left + cw / 2 - 30, ly - 3), m_pageBreakLabel);
        }
    }

    //  Caret — nur in dem Slot, in dem der Cursor auch steht (m_cursorRect
    //  liegt in Dokument-Pixeln, der Painter ebenfalls).
    if (withCaret && m_caretOn && hasActiveFocus() && !hasSel
        && !m_cursorRect.isNull()
        && m_cursorRect.y() >= sy - 1.0 && m_cursorRect.y() < sy + slotH
        && m_cursorRect.x() >= sx - marLpx()) {
        p->fillRect(m_cursorRect, QColor(20, 20, 20));
    }
    p->restore();
}

//  Eine EINZELNE Seite in ein Zielrechteck malen — der Weg der Miniaturen.
//  Bewusst derselbe Zeichencode wie paint() (keine zweite Darstellung, kein
//  Bild-Cache): das Zielrechteck bestimmt nur den Maßstab.
void DocxTextArea::paintPageInto(QPainter* p, int page, const QRectF& target) {
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
    p->fillRect(paper, QColor(255, 255, 255));
    p->setPen(QColor(0, 0, 0, 60));
    p->drawRect(paper);

    paintHeaderFooter(p, page);
    const int nCols = colCount();
    for (int c = 0; c < nCols; ++c) {
        paintSlot(p, page * nCols + c, false);   // Miniatur ohne Caret
        paintFootnotes(p, page * nCols + c);
    }
    p->restore();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Maus
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::hitTest(const QPointF& itemPos, int* block, int* pos) {
    *block = -1; *pos = 0;
    if (!m_ctl || m_lay.empty()) return;
    ensureOffsetsTo(int(m_lay.size()));

    //  Item-Pixel → Dokument-Pixel (Umkehrung der Transformation in paint()).
    const qreal s = qMax(0.05, m_scale);
    const qreal offX = qMax(kSideMargin * s, (width() - pageWpx() * s) / 2.0);
    const qreal docX = (itemPos.x() - offX) / s;
    const qreal docY = (itemPos.y() + m_contentY) / s;

    //  Seite und Spalte aus der Dokument-Position ableiten, daraus den Slot —
    //  erst der Slot macht aus dem Klick eine Stelle im FLUSS.
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

    //  ── ZWEITE Verfeinerungsstufe: liegt der Klick IN einer Tabelle? ─────────
    //  Zellblöcke haben keine eigene Fluss-Lage (die Tabelle hängt am Anker), ihre
    //  Dokument-Lage steht aber explizit. Es genügt also, die Zellen der
    //  sichtbaren Tabelle direkt zu prüfen: Zeile über y, Zelle über x — und
    //  zwar BEVOR die Fluss-Suche greift, die hier nur den Anker fände.
    for (int t = 0; t < m_ctl->doc().tables().size(); ++t) {
        const int anchor = m_ctl->doc().tableFirstBlock(t);
        if (anchor < 0 || anchor >= int(m_lay.size())) continue;
        const BlockLayout& A = m_lay[size_t(anchor)];
        if (!A.isTable || !A.table) continue;
        //  Eine getrennte Tabelle hat je Seite ein eigenes Stück mit eigenem
        //  Ursprung. Geprüft wird deshalb NUR das Stück im geklickten Slot —
        //  ohne das rechnete die Suche von der Oberkante der Tabelle weiter und
        //  fände auf Seite 3 die Zeilen von Seite 1.
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
        //  Nächstgelegener Zellblock dieses Stücks (rechteckige Trefferprüfung,
        //  sonst der mit dem kleinsten Abstand — ein Klick in den Zellrand soll
        //  nicht ins Leere gehen).
        int best = -1;
        qreal bestDist = 0.0;
        const int tid = t;
        for (int k = anchor; k < int(m_lay.size())
                             && k < m_ctl->doc().blocks.size()
                             && m_ctl->doc().blocks.at(k).tableId == tid; ++k) {
            const BlockLayout& CB = m_lay[size_t(k)];
            if (!CB.isCell || !hasText(CB)) continue;
            if (CB.cellRow < rowFrom || CB.cellRow >= rowTo) continue;
            //  Bänder decken Text UND Bilder ab (linesBottom kennt beides).
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
    //  y im Textbereich des Slots, geklemmt: ein Klick in den Seitenrand
    //  landet auf der nächstgelegenen Zeile statt ins Leere.
    const qreal yInSlot = qBound(0.0, docY - slotDocY(slot), slotH);
    const qreal cy = slot * slotH + yInSlot;
    int bi = blockAtY(cy);
    //  Nur editierbare Absätze sind Cursor-Ziele; zum nächsten suchen.
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
    //  Dokument-y → Layout-y des Blocks: über das Segment, das an dieser Stelle
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
    //  liegen zwei Zeilen auf derselben y — x entscheidet.
    const int li = rowAtX(L, lineForLocalY(L, localY), localX);
    //  Ein VERANKERTES Bild gehört keinem Zeilenband — getroffen wird es über
    //  seine LAGE. `imageAtX` findet es nur, solange ein Band sein Rechteck
    //  schneidet; läuft der Text vollständig darunter (breites Bild), war es
    //  sonst nicht mehr anklickbar und damit auch nicht mehr auswählbar.
    const int fk = floatingImageAt(L, localX, localY);
    if (fk >= 0) { *pos = L.images[size_t(fk)].pos; return; }
    *pos = posForX(L, li, localX);
}

void DocxTextArea::mousePressEvent(QMouseEvent* e) {
    forceActiveFocus();
    int b, p;
    hitTest(e->position(), &b, &p);

    //  Traf der Klick ein BILD, wird genau sein Objekt-Zeichen ausgewählt —
    //  das ist die Bild-Auswahl (Ziehpunkte, Kopieren, Löschen). Ohne das
    //  bliebe ein Bild im Fließtext unauswählbar.
    const bool onImage = b >= 0 && b < int(m_lay.size())
                         && imageAtPos(m_lay[size_t(b)], p) >= 0;

    if (e->button() == Qt::RightButton) {
        //  Wie in Word: der Rechtsklick verschiebt den Cursor NUR, wenn er
        //  außerhalb der bestehenden Selektion liegt — sonst bliebe das Menü
        //  ohne die Auswahl, auf die es sich beziehen soll.
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

// ─────────────────────────────────────────────────────────────────────────────
//  Tastatur
// ─────────────────────────────────────────────────────────────────────────────
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
    //  ↑/↓ springen ein BAND, nicht ein Stück: bei einem geteilten Band (Text
    //  links und rechts eines Bildes) liegen zwei Zeilen auf derselben y, und
    //  „eine Zeile tiefer" wäre sonst die Hälfte daneben. Innerhalb des Zielbandes
    //  entscheidet `m_goalX`, welches Stück gemeint ist.
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
            //  Letzte Zeile des letzten Absatzes: ↓ springt ans ZEILENENDE
            //  (statt wirkungslos zu bleiben) — einheitlich mit den übrigen
            //  Editoren der App.
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

    //  Esc verlässt die Kopf-/Fußzeilen-Bearbeitung (wie in Word). Im Körper
    //  wird die Taste NICHT verbraucht — dort schließt sie den Viewer.
    if (e->key() == Qt::Key_Escape
        && m_ctl->activeRegionInt() != DocxEditController::Body) {
        m_ctl->setRegion(DocxEditController::Body);
        e->accept();
        return;
    }

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
    //  Minimal: Commit-Strings (tote Tasten/IME) einfügen; Preedit wird nicht
    //  gesondert dargestellt (Grundgerüst).
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

//  Solange diese Fläche den Fokus hat, gehört JEDE Taste, die Text erzeugt,
//  dem Editor — auch ohne Modifikator. Ohne das öffnete ein Tippen von „d" den
//  Datum-Editor des Viewers, statt ein „d" zu schreiben (Nutzerbefund).
//  Modifizierte Kürzel (Strg/Alt/Meta) bleiben unangetastet, damit Strg+S,
//  Alt+Q & Co. weiterhin die App erreichen.
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
        //  Die Zeilenbreite ist jetzt die TEXTBREITE DER SEITE und damit von der
        //  Kachelbreite unabhängig — ein Breitenwechsel ändert also nur noch den
        //  Einpass-Maßstab, KEIN Umbruch und kein Neu-Vermessen (früher wurde
        //  hier das komplette Dokument neu ausgelegt).
        updateScale();
    }
    emit imageSelectionChanged();     // Seite wandert waagerecht/skaliert
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DocxPageThumb
// ─────────────────────────────────────────────────────────────────────────────
DocxPageThumb::DocxPageThumb(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(false);
}

void DocxPageThumb::setArea(DocxTextArea* a) {
    if (m_area == a) return;
    if (m_area) disconnect(m_area, nullptr, this, nullptr);
    m_area = a;
    if (m_area) {
        //  Inhalt/Seitenzahl geändert → Miniatur neu zeichnen.
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
