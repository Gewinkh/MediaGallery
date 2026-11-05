#pragma once
// Wohin gehört eine neue Notiz? Aus Glyphen und Linien einer Seite: Lücken (`____`), Tabellenzellen und
// Antwortrahmen, Zeilenenden und das Zeilenraster im Leerraum. Reines C++, ohne Qt-GUI - der Controller
// rechnet daraus mit der Schriftmetrik das Rechteck.
#include "pdf/edit/PdfTextLayout.h"

#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QVector>

namespace mg {

// Ein Textlauf: Glyphen einer Grundlinie ohne große Lücke dazwischen.
struct PdfSnapRun {
    QRectF box;              // ohne Leerzeichen
    qreal  baseline   = 0;
    qreal  fontSizePt = 0;
};

// Eine Lücke zum Ausfüllen: Unterstriche im Text oder eine kurze Linie auf der Grundlinie.
struct PdfSnapBlank {
    qreal left = 0, right = 0;
    qreal top = 0, baseline = 0;
    qreal fontSizePt = 0;
};

struct PdfNoteSlot {
    enum Kind { None = 0, Blank, Cell, LineEnd, Line, Gap };
    Kind  kind       = None;
    qreal textLeft   = 0;    // hier beginnt der Text
    qreal right      = 0;    // rechte Kante der Fläche
    qreal baseline   = 0;    // Grundlinie der ersten Textzeile
    qreal bottom     = 0;    // Unterkante der Fläche
    qreal fontSizePt = 0;
    QRectF frame;            // Zelle: Innenkanten der umschliessenden Linien, sonst leer
};

class PdfNoteSnap {
public:
    void build(const PdfPageText& page);
    PdfNoteSlot slotAt(const QPointF& pt, qreal pageWPt) const;

    bool  isEmpty() const { return m_runs.isEmpty() && m_h.isEmpty(); }
    qreal pageHeightPt() const { return m_pageH; }
    qreal bodyFontPt() const { return m_bodyFs; }
    qreal pitchPt() const { return m_pitch; }
    const QVector<PdfSnapRun>&   runs()   const { return m_runs; }
    const QVector<PdfSnapBlank>& blanks() const { return m_blanks; }
    // Von vier Linien umschlossene Fläche um den Punkt (Linienmitten), sonst leer; `inner` ohne die Striche.
    QRectF cellAt(const QPointF& pt, QRectF* inner = nullptr) const;

private:
    // Zusammengefasste Linien: waagerecht (y, x0..x1) bzw. senkrecht (x, y0..y1).
    struct Rule { qreal at, from, to, half; };

    void buildRuns(const QVector<PdfGlyph>& glyphs);
    void buildRules(const QVector<QLineF>& lines, const QVector<qreal>& widths);
    void buildBlanks(const QVector<PdfGlyph>& glyphs);
    void buildMetrics();
    qreal columnInset(const QRectF& cell) const;
    qreal fontNear(qreal y) const;
    bool  touchesVertical(const Rule& h) const;

    QVector<PdfSnapRun>   m_runs;
    QVector<PdfSnapBlank> m_blanks;
    QVector<Rule>         m_h, m_v;
    qreal m_pageH = 0, m_bodyFs = 10, m_pitch = 12;
    qreal m_colLeft = 0, m_colRight = 0;
};

} // namespace mg
