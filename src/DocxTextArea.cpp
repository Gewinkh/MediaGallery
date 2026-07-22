#include "DocxTextArea.h"
#include "DocxEditController.h"

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
constexpr qreal kPadV       = 28.0;           // Innenabstand oben/unten
constexpr qreal kMaxContent = 900.0;          // maximale Zeilenbreite
constexpr qreal kListIndent = 28.0;           // Einzug je Listenebene
constexpr int   kChunk      = 300;            // Blöcke je Initial-Layout-Tick
}

DocxTextArea::DocxTextArea(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(false);
    setFlag(ItemAcceptsInputMethod, true);
    setCursor(QCursor(Qt::IBeamCursor));
    setOpaquePainting(false);

    m_chunkTimer.setInterval(0);
    m_chunkTimer.setSingleShot(false);
    connect(&m_chunkTimer, &QTimer::timeout, this, &DocxTextArea::layoutChunk);

    m_blinkTimer.setInterval(530);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_caretOn = !m_caretOn;
        update();
    });
    m_blinkTimer.start();
}

DocxTextArea::~DocxTextArea() = default;

void DocxTextArea::setCtl(DocxEditController* c) {
    if (m_ctl == c) return;
    if (m_ctl) m_ctl->disconnect(this);
    m_ctl = c;
    if (m_ctl) {
        connect(m_ctl, &DocxEditController::readyChanged, this, [this]() { rebuildAll(); });
        connect(m_ctl, &DocxEditController::blocksReplaced, this,
                [this](int first, int oldCount, int newCount) {
                    invalidateFrom(first, oldCount, newCount);
                });
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
            update();
        });
        //  Format-Änderung OHNE Selektion existiert nur als Pending-Format im
        //  Controller — sie erzeugt weder blocksReplaced noch cursorChanged.
        //  Ohne diese Verbindung blieb der Caret (und die Höhe einer leeren
        //  Zeile) auf der alten Schriftgröße stehen (Nutzerbefund).
        connect(m_ctl, &DocxEditController::formatRevChanged, this, [this]() {
            m_caretOn = true;
            invalidateEmptyBlock(m_ctl->cursor().block);
            updateCursorRect();
            update();
        });
    }
    emit ctlChanged();
    rebuildAll();
}

void DocxTextArea::setContentY(qreal y) {
    y = qMax(0.0, qMin(y, qMax(0.0, m_contentHeight - height())));
    if (qFuzzyCompare(m_contentY + 1.0, y + 1.0)) return;
    m_contentY = y;
    emit contentYChanged();
    update();
}

qreal DocxTextArea::contentLeft() const {
    return qMax(20.0, (width() - contentWidth()) / 2.0);
}
qreal DocxTextArea::contentWidth() const {
    return qMax(120.0, qMin(width() - 40.0, kMaxContent));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout-Verwaltung
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::rebuildAll() {
    m_lay.clear();
    m_offsets.clear();
    m_offsetsValidTo = 0;
    m_layChunkAt = 0;
    m_contentHeight = 0;
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
    updateContentHeight();
    updateCursorRect();
    update();
}

void DocxTextArea::invalidateFrom(int first, int oldCount, int newCount) {
    if (!m_ctl || !m_ctl->ready()) { rebuildAll(); return; }
    if (m_lay.empty() && newCount > 0) { rebuildAll(); return; }
    first = qBound(0, first, int(m_lay.size()));
    for (int i = 0; i < oldCount && first < int(m_lay.size()); ++i)
        m_lay.erase(m_lay.begin() + first);
    for (int i = 0; i < newCount; ++i)
        m_lay.insert(m_lay.begin() + first + i, BlockLayout());
    for (int i = 0; i < newCount; ++i)
        ensureLaid(first + i);
    m_offsetsValidTo = qMin(m_offsetsValidTo, first);
    rebuildMarkers();
    //  Marker können sich hinter der Änderung geändert haben (Listenzähler) —
    //  betroffene Layouts dort NICHT wegwerfen (nur Marker-Strings neu).
    updateContentHeight();
    updateCursorRect();
    update();
}

void DocxTextArea::rebuildMarkers() {
    if (!m_ctl) return;
    const Document& d = m_ctl->doc();
    QHash<int, int> counters;                       // numId → laufende Nummer
    for (int i = 0; i < d.blocks.size() && i < int(m_lay.size()); ++i) {
        const Block& b = d.blocks.at(i);
        QString marker;
        qreal indent = 0;
        if (b.kind == Block::Paragraph) {
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
                    marker.remove(QRegularExpression(QStringLiteral("%\\d")));
                }
                marker += QLatin1Char(' ');
            }
        }
        if (m_lay[i].marker != marker || !qFuzzyCompare(m_lay[i].indentPx + 1, indent + 1)) {
            m_lay[i].marker   = marker;
            m_lay[i].indentPx = indent;
            if (m_lay[i].laid) { m_lay[i].laid = false; m_lay[i].layout.reset(); }
        }
    }
}

void DocxTextArea::buildLayout(int i) {
    const Document& d = m_ctl->doc();
    const Block& b = d.blocks.at(i);
    BlockLayout& L = m_lay[i];
    L.layout.reset();
    L.beforePx = 0;

    if (b.kind == Block::OpaqueHidden) {
        L.height = 0; L.laid = true; return;
    }
    if (b.kind == Block::OpaqueVisible) {
        L.height = 34; L.laid = true; return;
    }

    const ParFmt pf = d.resolvePar(b);
    const QString text = b.plainText();

    //  Grundschrift des Absatzes = SEIN aufgelöstes Stil-Format (nicht die
    //  docDefaults): nur so ist eine leere Überschriftzeile auch so hoch wie
    //  eine Überschrift. Steht der Cursor in dieser (leeren) Zeile, gilt das am
    //  Cursor wirksame Format inkl. Pending — sonst hätte eine Schriftgrößen-
    //  Änderung in einer leeren Zeile sichtbar keinerlei Wirkung.
    const RunFmt def = d.defaultRun();
    RunFmt bf = d.resolveRun(b, Run());
    if (text.isEmpty() && m_ctl->cursor().block == i)
        bf = m_ctl->caretFormat();
    QFont base;
    base.setFamily(bf.font.isEmpty() ? def.font : bf.font);
    base.setPointSizeF(bf.sizePt > 0 ? bf.sizePt : def.sizePt);
    base.setBold(bf.bold);
    base.setItalic(bf.italic);

    auto* lay = new QTextLayout(text, base);
    QTextOption opt;
    switch (pf.align) {
    case 1:  opt.setAlignment(Qt::AlignHCenter); break;
    case 2:  opt.setAlignment(Qt::AlignRight);   break;
    case 3:  opt.setAlignment(Qt::AlignJustify); break;
    default: opt.setAlignment(Qt::AlignLeft);    break;
    }
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setTextDirection(Qt::LayoutDirectionAuto);   // Arabisch: RTL automatisch
    lay->setTextOption(opt);
    lay->setCacheEnabled(true);

    //  Format-Bereiche aus den Runs (aufgelöst über docDefaults + Vorlagen).
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
        if (r.opaque) {
            //  Atomare Fremdinhalte dezent hinterlegen (Hyperlink-Blau wäre
            //  irreführend — es sind auch Felder/Zeichnungen).
            cf.setBackground(QColor(0, 0, 0, 14));
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
    lay->setFormats(fmts);

    //  Zeilen manuell positionieren (Zeilenabstand = Vielfaches der Zeilenhöhe).
    const qreal w = contentWidth() - L.indentPx;
    const qreal spacing = qMax(0.5, pf.lineSpacing);
    L.beforePx = pf.beforePt * kPtToPx;
    qreal y = L.beforePx;
    lay->beginLayout();
    for (;;) {
        QTextLine line = lay->createLine();
        if (!line.isValid()) break;
        line.setLineWidth(w);
        line.setPosition(QPointF(0, y));
        y += line.height() * spacing;
    }
    lay->endLayout();
    if (lay->lineCount() == 0)                       // leerer Absatz: eine Zeile hoch
        y += QFontMetricsF(base).height() * spacing;
    L.height = y + pf.afterPt * kPtToPx;
    L.layout.reset(lay);
    L.laid = true;
}

void DocxTextArea::ensureLaid(int i) {
    if (!m_ctl || i < 0 || i >= int(m_lay.size())) return;
    if (!m_lay[i].laid || (!m_lay[i].layout && m_ctl->doc().blocks.at(i).kind == Block::Paragraph)) {
        const qreal oldH = m_lay[i].height;
        buildLayout(i);
        if (!qFuzzyCompare(oldH + 1, m_lay[i].height + 1))
            m_offsetsValidTo = qMin(m_offsetsValidTo, i);
    }
}

void DocxTextArea::ensureOffsetsTo(int i) {
    i = qMin(i, int(m_lay.size()));
    if (m_offsets.size() != int(m_lay.size()) + 1) {
        m_offsets.resize(int(m_lay.size()) + 1);
        m_offsetsValidTo = 0;
    }
    if (m_offsets.isEmpty()) return;
    if (m_offsetsValidTo == 0) m_offsets[0] = kPadV;
    for (int k = qMax(1, m_offsetsValidTo + 1); k <= i; ++k)
        m_offsets[k] = m_offsets[k - 1] + m_lay[k - 1].height;
    m_offsetsValidTo = qMax(m_offsetsValidTo, i);
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
        if (!m_lay[m_layChunkAt].laid) { buildLayout(m_layChunkAt); ++done; }
        ++m_layChunkAt;
    }
    updateContentHeight();
    if (m_layChunkAt >= int(m_lay.size())) {
        m_chunkTimer.stop();
        updateCursorRect();
    }
    update();
}

void DocxTextArea::updateContentHeight() {
    ensureOffsetsTo(int(m_lay.size()));
    const qreal h = (m_offsets.isEmpty() ? kPadV : m_offsets.last()) + kPadV;
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
    m_lay[i].layout.reset();
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
    const qreal top = blockTop(bi);

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

    qreal x = contentLeft() + L.indentPx;
    qreal y = top + L.beforePx;
    qreal h = fm.height();
    if (L.layout && L.layout->lineCount() > 0) {
        QTextLine line = L.layout->lineForTextPosition(
            qBound(0, c.pos, L.layout->text().size()));
        if (!line.isValid()) line = L.layout->lineAt(L.layout->lineCount() - 1);
        x += line.cursorToX(qBound(0, c.pos, L.layout->text().size()));
        //  An der GRUNDLINIE der Zeile ausrichten (nicht an der Zeilenoberkante)
        //  — sonst „schwebt" ein kleiner Caret in einer hohen Mischzeile.
        y = top + line.y() + line.ascent() - fm.ascent();
    }
    m_cursorRect = QRectF(x, y, 1.6, h);
    emit cursorRectChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Zeichnen
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::paint(QPainter* p) {
    if (!m_ctl || !m_ctl->ready())
        return;
    const Document& d = m_ctl->doc();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);

    //  Theme-Umgebung + WEISSE „Word-Seite" (durchlaufende Papierbahn ohne
    //  Seitenschnitte — echte Paginierung folgt in Aufgabe 3): dezenter Rand
    //  und Schattenkante heben die Seite vom Umgebungsgrund ab.
    const qreal left = contentLeft();
    const qreal cw   = contentWidth();
    p->fillRect(QRectF(0, 0, width(), height()), m_surroundColor);
    const QRectF paper(left - 18, -2, cw + 36, height() + 4);
    p->fillRect(paper.translated(2.0, 0), QColor(0, 0, 0, 45));   // Schattenkante
    p->fillRect(paper, QColor(255, 255, 255));
    p->setPen(QColor(0, 0, 0, 40));
    p->drawLine(paper.topLeft(), paper.bottomLeft());
    p->drawLine(paper.topRight(), paper.bottomRight());

    int b1, p1, b2, p2;
    const DocxCursor& cur = m_ctl->cursor();
    b1 = cur.aBlock; p1 = cur.aPos; b2 = cur.block; p2 = cur.pos;
    if (b1 > b2 || (b1 == b2 && p1 > p2)) { std::swap(b1, b2); std::swap(p1, p2); }
    const bool hasSel = cur.hasSelection();
    const QColor selBg(38, 118, 216, 110);

    const qreal viewTop = m_contentY, viewBot = m_contentY + height();
    int i = blockAtY(viewTop);
    if (i < 0) return;
    for (; i < int(m_lay.size()); ++i) {
        ensureLaid(i);
        const qreal top = blockTop(i);
        if (top > viewBot) break;
        const BlockLayout& L = m_lay[i];
        if (top + L.height < viewTop || L.height <= 0)
            continue;
        const Block& b = d.blocks.at(i);
        const qreal y = top - m_contentY;

        if (b.kind == Block::OpaqueVisible) {
            //  Platzhalter (Tabelle & Co.) — Inhalt bleibt in der Datei intakt.
            const QRectF r(left, y + 5, cw, 24);
            p->setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
            p->setBrush(QColor(0, 0, 0, 8));
            p->drawRoundedRect(r, 5, 5);
            p->setPen(QColor(110, 110, 110));
            QFont f; f.setPointSizeF(9.5); f.setItalic(true);
            p->setFont(f);
            p->drawText(r, Qt::AlignCenter, m_tablePlaceholder);
            continue;
        }
        if (!L.layout)
            continue;

        //  Listenmarker.
        if (!L.marker.isEmpty() && L.layout->lineCount() > 0) {
            const RunFmt def = d.defaultRun();
            QFont mf; mf.setFamily(def.font); mf.setPointSizeF(def.sizePt);
            p->setFont(mf);
            p->setPen(QColor(30, 30, 30));
            const QTextLine l0 = L.layout->lineAt(0);
            p->drawText(QPointF(left + L.indentPx - QFontMetricsF(mf).horizontalAdvance(L.marker),
                                y + l0.y() + l0.ascent()),
                        L.marker);
        }

        //  Selektion des Blocks als FormatRange.
        QList<QTextLayout::FormatRange> sel;
        if (hasSel && i >= b1 && i <= b2) {
            QTextLayout::FormatRange fr;
            fr.start  = (i == b1) ? p1 : 0;
            const int end = (i == b2) ? p2 : L.layout->text().size();
            fr.length = qMax(0, end - fr.start);
            fr.format.setBackground(selBg);
            if (fr.length > 0) sel.append(fr);
        }
        L.layout->draw(p, QPointF(left + L.indentPx, y), sel);

        //  Seitenumbruch-Marker: gestrichelte Linie in der Zeile des Sentinels.
        const QString t = L.layout->text();
        for (int pb = t.indexOf(kPageBreak); pb >= 0; pb = t.indexOf(kPageBreak, pb + 1)) {
            const QTextLine line = L.layout->lineForTextPosition(pb);
            if (!line.isValid()) continue;
            const qreal ly = y + line.y() + line.height() + 2;
            p->setPen(QPen(QColor(140, 140, 150), 1, Qt::DashLine));
            p->drawLine(QPointF(left, ly), QPointF(left + cw, ly));
            QFont f; f.setPointSizeF(8.0);
            p->setFont(f);
            p->setPen(QColor(130, 130, 140));
            p->drawText(QPointF(left + cw / 2 - 30, ly - 3), m_pageBreakLabel);
        }
    }

    //  Caret.
    if (m_caretOn && hasActiveFocus() && !hasSel && !m_cursorRect.isNull()) {
        p->fillRect(m_cursorRect.translated(0, -m_contentY), QColor(20, 20, 20));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Maus
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::hitTest(const QPointF& itemPos, int* block, int* pos) {
    *block = -1; *pos = 0;
    if (!m_ctl || m_lay.empty()) return;
    const qreal cy = itemPos.y() + m_contentY;
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
    if (!L.layout || L.layout->lineCount() == 0) { *pos = 0; return; }
    const qreal localY = cy - blockTop(bi);
    QTextLine line = L.layout->lineAt(0);
    for (int li = 0; li < L.layout->lineCount(); ++li) {
        const QTextLine l = L.layout->lineAt(li);
        line = l;
        if (localY < l.y() + l.height())
            break;
    }
    *pos = line.xToCursor(itemPos.x() - contentLeft() - L.indentPx);
}

void DocxTextArea::mousePressEvent(QMouseEvent* e) {
    forceActiveFocus();
    int b, p;
    hitTest(e->position(), &b, &p);
    if (b >= 0) {
        m_ctl->setCursor(b, p, e->modifiers() & Qt::ShiftModifier);
        m_selecting = true;
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
    if (!L.layout) return;
    QTextLine line = L.layout->lineForTextPosition(c.pos);
    if (!line.isValid() && L.layout->lineCount() > 0)
        line = L.layout->lineAt(0);
    if (m_goalX < 0 && line.isValid())
        m_goalX = line.cursorToX(c.pos);
    const int li = line.isValid() ? line.lineNumber() : 0;
    const Document& d = m_ctl->doc();
    auto editable = [&](int i) {
        return i >= 0 && i < d.blocks.size()
               && d.blocks.at(i).kind == Block::Paragraph;
    };
    if (dir < 0) {
        if (li > 0) {
            const QTextLine up = L.layout->lineAt(li - 1);
            m_ctl->setCursor(bi, up.xToCursor(m_goalX), keepAnchor);
            return;
        }
        int pb = bi - 1;
        while (pb >= 0 && !editable(pb)) --pb;
        if (pb < 0) return;
        ensureLaid(pb);
        const BlockLayout& P = m_lay[pb];
        int pos = m_ctl->doc().blocks.at(pb).textLength();
        if (P.layout && P.layout->lineCount() > 0)
            pos = P.layout->lineAt(P.layout->lineCount() - 1).xToCursor(m_goalX);
        m_ctl->setCursor(pb, pos, keepAnchor);
    } else {
        if (L.layout && li < L.layout->lineCount() - 1) {
            const QTextLine dn = L.layout->lineAt(li + 1);
            m_ctl->setCursor(bi, dn.xToCursor(m_goalX), keepAnchor);
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
        if (N.layout && N.layout->lineCount() > 0)
            pos = N.layout->lineAt(0).xToCursor(m_goalX);
        m_ctl->setCursor(nb, pos, keepAnchor);
    }
}

void DocxTextArea::keyPressEvent(QKeyEvent* e) {
    if (!m_ctl || !m_ctl->ready()) { e->ignore(); return; }
    const bool ctrl  = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const DocxCursor& c = m_ctl->cursor();

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

void DocxTextArea::geometryChange(const QRectF& n, const QRectF& o) {
    QQuickPaintedItem::geometryChange(n, o);
    if (!qFuzzyCompare(n.width() + 1, o.width() + 1)) {
        //  Breite ändert den Umbruch → alles neu vermessen (gechunkt).
        for (auto& L : m_lay) { L.laid = false; L.layout.reset(); }
        m_offsetsValidTo = 0;
        m_layChunkAt = 0;
        if (m_ctl && m_ctl->ready())
            m_chunkTimer.start();
    }
    update();
}
