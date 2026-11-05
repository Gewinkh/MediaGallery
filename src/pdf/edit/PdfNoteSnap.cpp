#include "pdf/edit/PdfNoteSnap.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace mg {

namespace {

bool isBlankChar(QChar c) {
    return c.isSpace() || c == QChar(0x00A0);
}

qreal median(QVector<qreal> v) {
    if (v.isEmpty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v.at(v.size() / 2);
}

} // namespace

void PdfNoteSnap::build(const PdfPageText& page) {
    m_runs.clear();
    m_blanks.clear();
    m_h.clear();
    m_v.clear();
    m_pageH = page.pageHeightPt;
    buildRuns(page.glyphs);
    buildRules(page.rules, page.ruleWidths);
    buildMetrics();
    buildBlanks(page.glyphs);
}

void PdfNoteSnap::buildRuns(const QVector<PdfGlyph>& glyphs) {
    QVector<const PdfGlyph*> gs;
    gs.reserve(glyphs.size());
    for (const PdfGlyph& g : glyphs)
        if (!isBlankChar(g.ch) && g.box.height() > 0.5)
            gs.push_back(&g);
    std::sort(gs.begin(), gs.end(), [](const PdfGlyph* a, const PdfGlyph* b) {
        return a->box.bottom() < b->box.bottom();
    });

    // Hoch-/Tiefstellungen liegen um ein Drittel der Größe versetzt - sie bilden eigene Läufe.
    int i = 0;
    while (i < gs.size()) {
        const qreal base0 = gs[i]->box.bottom();
        const qreal tol   = qMax(0.5, gs[i]->box.height() * 0.2);
        int j = i + 1;
        while (j < gs.size() && gs[j]->box.bottom() - base0 <= tol) ++j;
        std::sort(gs.begin() + i, gs.begin() + j, [](const PdfGlyph* a, const PdfGlyph* b) {
            return a->box.left() < b->box.left();
        });
        PdfSnapRun run;
        bool open = false;
        for (int k = i; k < j; ++k) {
            const PdfGlyph* g = gs[k];
            const qreal fs = qMax(1.0, g->box.height());
            if (open && g->box.left() - run.box.right() > 1.5 * qMax(fs, run.fontSizePt)) {
                m_runs.push_back(run);
                open = false;
            }
            if (!open) {
                run = PdfSnapRun{ g->box, g->box.bottom(), fs };
                open = true;
            } else {
                run.box = run.box.united(g->box);
                run.baseline   = qMax(run.baseline, g->box.bottom());
                run.fontSizePt = qMax(run.fontSizePt, fs);
            }
        }
        if (open) m_runs.push_back(run);
        i = j;
    }
}

void PdfNoteSnap::buildRules(const QVector<QLineF>& lines, const QVector<qreal>& widths) {
    QVector<Rule> h, v;
    for (int i = 0; i < lines.size(); ++i) {
        const QLineF& l = lines.at(i);
        const qreal half = i < widths.size() ? qBound(0.0, widths.at(i) / 2.0, 10.0) : 0.0;
        if (std::abs(l.dy()) < std::abs(l.dx()))
            h.push_back({ (l.y1() + l.y2()) / 2.0, qMin(l.x1(), l.x2()), qMax(l.x1(), l.x2()), half });
        else
            v.push_back({ (l.x1() + l.x2()) / 2.0, qMin(l.y1(), l.y2()), qMax(l.y1(), l.y2()), half });
    }
    // Zeilenweise gezeichnete Gitter bestehen aus Stücken mit Haarfugen; zusammengefasst wird eine Linie daraus.
    auto merge = [](QVector<Rule>& in, QVector<Rule>& out) {
        std::sort(in.begin(), in.end(), [](const Rule& a, const Rule& b) { return a.at < b.at; });
        int i = 0;
        while (i < in.size()) {
            int j = i + 1;
            while (j < in.size() && in[j].at - in[i].at <= 0.75) ++j;
            std::sort(in.begin() + i, in.begin() + j,
                      [](const Rule& a, const Rule& b) { return a.from < b.from; });
            Rule cur = in[i];
            for (int k = i + 1; k < j; ++k) {
                if (in[k].from <= cur.to + 1.5) {
                    cur.to = qMax(cur.to, in[k].to);
                    cur.half = qMax(cur.half, in[k].half);
                } else {
                    out.push_back(cur);
                    cur = in[k];
                }
            }
            out.push_back(cur);
            i = j;
        }
    };
    merge(h, m_h);
    merge(v, m_v);
}

void PdfNoteSnap::buildMetrics() {
    if (m_runs.isEmpty()) return;
    QHash<int, qreal> weight;
    for (const PdfSnapRun& r : std::as_const(m_runs))
        weight[qRound(r.fontSizePt * 2.0)] += r.box.width();
    int best = 0;
    qreal bestW = -1.0;
    for (auto it = weight.cbegin(); it != weight.cend(); ++it)
        if (it.value() > bestW) { bestW = it.value(); best = it.key(); }
    m_bodyFs = qMax(1.0, best / 2.0);

    QVector<qreal> bases;
    m_colLeft = 1e9;
    m_colRight = -1e9;
    for (const PdfSnapRun& r : std::as_const(m_runs)) {
        if (std::abs(r.fontSizePt - m_bodyFs) > 0.6) continue;
        bases.push_back(r.baseline);
        m_colLeft  = qMin(m_colLeft, r.box.left());
        m_colRight = qMax(m_colRight, r.box.right());
    }
    if (m_colRight < m_colLeft) { m_colLeft = 0; m_colRight = 0; }
    std::sort(bases.begin(), bases.end());
    QVector<qreal> diffs;
    for (int i = 1; i < bases.size(); ++i) {
        const qreal d = bases[i] - bases[i - 1];
        if (d > 0.9 * m_bodyFs && d < 2.5 * m_bodyFs) diffs.push_back(d);
    }
    m_pitch = diffs.isEmpty() ? 1.2 * m_bodyFs : median(diffs);
}

bool PdfNoteSnap::touchesVertical(const Rule& h) const {
    for (const Rule& v : m_v) {
        if (h.at < v.from - 2.0 || h.at > v.to + 2.0) continue;
        if (std::abs(v.at - h.from) <= 2.0 || std::abs(v.at - h.to) <= 2.0) return true;
    }
    return false;
}

void PdfNoteSnap::buildBlanks(const QVector<PdfGlyph>& glyphs) {
    // Unterstriche in Folge, wie sie im Strom stehen; ein einzelner `_` gehört zu einem Bezeichner.
    int i = 0;
    while (i < glyphs.size()) {
        if (glyphs[i].ch != QLatin1Char('_')) { ++i; continue; }
        int j = i + 1;
        while (j < glyphs.size() && glyphs[j].ch == QLatin1Char('_')
               && std::abs(glyphs[j].box.bottom() - glyphs[j - 1].box.bottom()) < 0.5
               && glyphs[j].box.left() - glyphs[j - 1].box.right() <= 1.0)
            ++j;
        if (j - i >= 3) {
            const QRectF& a = glyphs[i].box;
            const QRectF& z = glyphs[j - 1].box;
            m_blanks.push_back({ a.left(), z.right(), a.top(), a.bottom(), qMax(1.0, a.height()) });
        }
        i = j;
    }

    // Kurze Linie auf der Grundlinie neben Text (`Name: ______` als Strich gezeichnet). Unter Text ist es eine
    // Unterstreichung, an Senkrechten ein Gitter, über die ganze Spalte ein Trenner.
    const qreal colW = m_colRight - m_colLeft;
    for (const Rule& h : std::as_const(m_h)) {
        const qreal len = h.to - h.from;
        if (len < 15.0 || (colW > 0 && len > 0.5 * colW) || touchesVertical(h)) continue;
        const PdfSnapRun* host = nullptr;
        bool underText = false;
        for (const PdfSnapRun& r : std::as_const(m_runs)) {
            if (h.at < r.baseline - 1.5 || h.at > r.baseline + 4.5) continue;
            const qreal over = qMin(h.to, r.box.right()) - qMax(h.from, r.box.left());
            if (over > 0.25 * len) { underText = true; break; }
            if (r.box.right() < h.from - 3.0 * r.fontSizePt || r.box.left() > h.to + 3.0 * r.fontSizePt)
                continue;
            if (!host || std::abs(r.baseline - h.at) < std::abs(host->baseline - h.at)) host = &r;
        }
        if (underText || !host) continue;
        m_blanks.push_back({ h.from, h.to, host->baseline - host->fontSizePt, host->baseline,
                             host->fontSizePt });
    }
}

QRectF PdfNoteSnap::cellAt(const QPointF& p, QRectF* inner) const {
    const Rule* top = nullptr;
    const Rule* bottom = nullptr;
    for (const Rule& h : m_h) {
        if (p.x() < h.from - 1.0 || p.x() > h.to + 1.0) continue;
        if (h.at <= p.y() && (!top || h.at > top->at)) top = &h;
        if (h.at >= p.y() && (!bottom || h.at < bottom->at)) bottom = &h;
    }
    if (!top || !bottom || bottom->at - top->at < 4.0) return {};
    const Rule* left = nullptr;
    const Rule* right = nullptr;
    for (const Rule& v : m_v) {
        if (p.y() < v.from - 1.0 || p.y() > v.to + 1.0) continue;
        if (v.at <= p.x() && (!left || v.at > left->at)) left = &v;
        if (v.at >= p.x() && (!right || v.at < right->at)) right = &v;
    }
    if (!left || !right || right->at - left->at < 8.0) return {};

    // Die vier Linien müssen sich an den Ecken treffen. Sonst bildeten ein Seitenrahmen und zwei Trennstriche
    // eine Scheinzelle über die ganze Breite.
    auto reaches = [](const Rule& r, qreal a, qreal b) { return r.from <= a + 3.0 && r.to >= b - 3.0; };
    if (!reaches(*left, top->at, bottom->at) || !reaches(*right, top->at, bottom->at)
        || !reaches(*top, left->at, right->at) || !reaches(*bottom, left->at, right->at))
        return {};
    // Ein Viertelpunkt Luft: die Kantenglaettung der Linie bleibt sichtbar, auch unter deckendem Weiss.
    if (inner) {
        constexpr qreal kAir = 0.25;
        *inner = QRectF(QPointF(left->at + left->half + kAir, top->at + top->half + kAir),
                        QPointF(right->at - right->half - kAir, bottom->at - bottom->half - kAir));
    }
    return QRectF(QPointF(left->at, top->at), QPointF(right->at, bottom->at));
}

qreal PdfNoteSnap::columnInset(const QRectF& cell) const {
    // Abstand Linie -> Text, wie ihn die Tabelle selbst in derselben Spalte hält (LaTeX: \tabcolsep).
    const Rule* edge = nullptr;
    for (const Rule& v : m_v)
        if (std::abs(v.at - cell.left()) < 0.5 && v.from <= cell.center().y() && v.to >= cell.center().y())
            edge = &v;
    qreal best = 1e9;
    for (const PdfSnapRun& r : m_runs) {
        const qreal d = r.box.left() - cell.left();
        if (d <= 0.5 || d > 20.0) continue;
        if (edge && (r.box.center().y() < edge->from || r.box.center().y() > edge->to)) continue;
        if (!edge && !cell.contains(r.box.center())) continue;
        best = qMin(best, d);
    }
    return best < 1e9 ? best : 4.0;
}

qreal PdfNoteSnap::fontNear(qreal y) const {
    const PdfSnapRun* best = nullptr;
    for (const PdfSnapRun& r : m_runs)
        if (!best || std::abs(r.baseline - y) < std::abs(best->baseline - y)) best = &r;
    return best ? best->fontSizePt : m_bodyFs;
}

PdfNoteSlot PdfNoteSnap::slotAt(const QPointF& p, qreal pageWPt) const {
    PdfNoteSlot s;

    // (1) Lücke: ein Klick in ihre Zeile in der Nähe genügt.
    const PdfSnapBlank* blank = nullptr;
    qreal blankD = 1e9;
    for (const PdfSnapBlank& b : m_blanks) {
        if (p.y() < b.top - 0.4 * b.fontSizePt || p.y() > b.baseline + 0.6 * b.fontSizePt) continue;
        if (p.x() < b.left - 4.0 || p.x() > b.right + 4.0) continue;
        const qreal d = p.x() < b.left ? b.left - p.x() : (p.x() > b.right ? p.x() - b.right : 0.0);
        if (d < blankD) { blankD = d; blank = &b; }
    }
    if (blank) {
        s.kind = PdfNoteSlot::Blank;
        s.textLeft = blank->left;
        s.right = blank->right;
        s.baseline = blank->baseline;
        s.bottom = blank->baseline + 0.3 * blank->fontSizePt;
        s.fontSizePt = blank->fontSizePt;
        return s;
    }

    // (2) Zelle oder Rahmen. Ein Seitenrahmen ist keine Zelle.
    QRectF inner;
    const QRectF cell = cellAt(p, &inner);
    if (!cell.isEmpty() && (pageWPt <= 0 || cell.width() * cell.height() <= 0.5 * pageWPt * m_pageH)) {
        const qreal inset = columnInset(cell);
        s.kind = PdfNoteSlot::Cell;
        s.frame = inner;
        s.textLeft = cell.left() + inset;
        s.right = cell.right() - 0.5;
        s.bottom = cell.bottom() - 0.5;

        const PdfSnapRun* hit = nullptr;
        for (const PdfSnapRun& r : m_runs) {
            if (!cell.contains(r.box.center())) continue;
            if (p.y() < r.box.top() - 0.25 * r.fontSizePt || p.y() > r.baseline + 0.35 * r.fontSizePt) continue;
            if (!hit || std::abs(r.baseline - p.y()) < std::abs(hit->baseline - p.y())) hit = &r;
        }
        const bool rowLike = cell.height() <= 2.6 * m_pitch;
        const PdfSnapRun* row = nullptr;
        if (!hit && rowLike)
            for (const PdfSnapRun& r : m_runs) {
                if (r.baseline <= cell.top() + 0.5 || r.baseline >= cell.bottom() || r.box.top() < cell.top() - 1.0)
                    continue;
                if (!row || std::abs(r.baseline - p.y()) < std::abs(row->baseline - p.y())) row = &r;
            }
        if (hit) {
            // Beschriftete Zelle: hinter den Text schreiben, solange Platz bleibt.
            s.baseline = hit->baseline;
            s.fontSizePt = hit->fontSizePt;
            const qreal after = hit->box.right() + 0.33 * hit->fontSizePt;
            if (after + 12.0 <= s.right) s.textLeft = qMax(s.textLeft, after);
        } else if (row) {
            s.baseline = row->baseline;
            s.fontSizePt = row->fontSizePt;
        } else if (rowLike) {
            s.fontSizePt = fontNear(cell.center().y());
            s.baseline = cell.center().y() + 0.35 * s.fontSizePt;
        } else {
            // Leerer Rahmen: das Zeilenraster der Seite, oben mit demselben Abstand wie seitlich.
            s.fontSizePt = fontNear(cell.top());
            const qreal pitch = m_pitch * s.fontSizePt / m_bodyFs;
            const qreal first = cell.top() + qBound(3.0, inset, 8.0) + 0.75 * s.fontSizePt;
            int k = qMax(0, int(std::floor((p.y() - (first - 0.75 * s.fontSizePt)) / pitch)));
            while (k > 0 && first + k * pitch + 0.25 * s.fontSizePt > cell.bottom() - 1.0) --k;
            s.baseline = first + k * pitch;
        }
        return s;
    }

    // (3) Auf einer Textzeile: hinter ihr Ende oder an ihren Anfang.
    const PdfSnapRun* line = nullptr;
    qreal lineD = 1e9;
    for (const PdfSnapRun& r : m_runs) {
        if (p.y() < r.box.top() - 0.35 * r.fontSizePt || p.y() > r.baseline + 0.45 * r.fontSizePt) continue;
        const qreal d = p.x() < r.box.left() ? (r.box.left() - p.x()) * 2.0
                      : (p.x() > r.box.right() ? p.x() - r.box.right() : 0.0);
        if (d < lineD) { lineD = d; line = &r; }
    }
    if (line) {
        const qreal fs = line->fontSizePt;
        qreal right = qMax(line->box.right(), m_colRight);
        for (const PdfSnapRun& r : m_runs)
            if (&r != line && std::abs(r.baseline - line->baseline) < 0.3 * fs
                && r.box.left() > line->box.right() && r.box.left() - 0.33 * fs < right)
                right = r.box.left() - 0.33 * fs;
        s.baseline = line->baseline;
        s.fontSizePt = fs;
        s.bottom = line->baseline + 0.3 * fs;
        s.right = right;
        if (p.x() > line->box.right() + 0.5 * fs) {
            s.kind = PdfNoteSlot::LineEnd;
            s.textLeft = line->box.right() + 0.33 * fs;
        } else {
            s.kind = PdfNoteSlot::Line;
            s.textLeft = line->box.left();
        }
        return s;
    }

    // (4) Leerraum: das Raster der Zeile darüber fortsetzen.
    const PdfSnapRun* above = nullptr;
    for (const PdfSnapRun& r : m_runs) {
        if (r.baseline >= p.y()) continue;
        if (p.x() < r.box.left() - 3.0 * r.fontSizePt
            || p.x() > qMax(r.box.right(), m_colRight) + 3.0 * r.fontSizePt) continue;
        if (!above || r.baseline > above->baseline) above = &r;
    }
    if (!above) return s;
    const qreal fs = above->fontSizePt;
    const qreal pitch = m_pitch * fs / m_bodyFs;
    qreal base = above->baseline;
    qreal floorY = above->baseline;
    // Eine Linie dazwischen (Abschnittsstrich, Rahmenunterkante) beginnt das Raster neu.
    for (const Rule& h : m_h)
        if (h.at > above->baseline && h.at < p.y() && p.x() >= h.from - 1.0 && p.x() <= h.to + 1.0) {
            base = qMax(base, h.at + 4.0 + 0.75 * fs - pitch);
            floorY = qMax(floorY, h.at);
        }
    qreal limit = 1e9;
    for (const PdfSnapRun& r : m_runs)
        if (r.box.top() > floorY && p.x() >= r.box.left() - 3.0 * r.fontSizePt
            && p.x() <= qMax(r.box.right(), m_colRight) + 3.0 * r.fontSizePt)
            limit = qMin(limit, r.box.top());
    for (const Rule& h : m_h)
        if (h.at > floorY + 0.5 && p.x() >= h.from - 1.0 && p.x() <= h.to + 1.0)
            limit = qMin(limit, h.at);
    int k = qMax(1, int(std::lround((p.y() + 0.35 * fs - base) / pitch)));
    while (k > 1 && base + k * pitch + 0.3 * fs > limit - 0.5) --k;
    const qreal baseline = base + k * pitch;
    if (baseline + 0.3 * fs > limit - 0.5 || baseline > m_pageH) return s;
    s.kind = PdfNoteSlot::Gap;
    s.textLeft = above->box.left();
    s.right = qMax(m_colRight, above->box.right());
    s.baseline = baseline;
    s.bottom = baseline + 0.3 * fs;
    s.fontSizePt = fs;
    return s;
}

} // namespace mg
