#include "core/SearchPattern.h"

#include <algorithm>

namespace mg::search {
namespace {

//  Alles, was PCRE anders liest als ein Buchstabe. `-` und `#` stehen bewusst
//  NICHT hier: sie kommen in Dateinamen staendig vor und bedeuten als Muster
//  ausserhalb einer Klasse nichts.
constexpr QChar kMeta[] = { u'\\', u'.', u'^', u'$', u'|', u'?', u'*', u'+',
                            u'(',  u')', u'[', u']', u'{', u'}' };

}  // namespace

bool hasMetaCharacters(QStringView s) {
    for (const QChar c : s)
        for (const QChar m : kMeta)
            if (c == m) return true;
    return false;
}

void mergeRanges(QList<Range>& bereiche) {
    if (bereiche.size() < 2) return;
    std::sort(bereiche.begin(), bereiche.end(), [](const Range& a, const Range& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.length > b.length;          // bei gleichem Start der laengere
    });
    QList<Range> raus;
    raus.reserve(bereiche.size());
    int bisherEnde = -1;
    for (const Range& r : std::as_const(bereiche)) {
        if (r.start < bisherEnde) continue;  // ueberlappt einen behaltenen
        raus.append(r);
        bisherEnde = r.start + r.length;
    }
    bereiche = std::move(raus);
}

Pattern::Pattern(const QString& needle, bool caseSensitive, bool wholeWords)
    : m_literal(needle), m_case(caseSensitive), m_whole(wholeWords) {
    if (m_literal.isEmpty()) return;

    const QRegularExpression::PatternOptions opt =
        caseSensitive ? QRegularExpression::NoPatternOption
                      : QRegularExpression::CaseInsensitiveOption;

    //  „Ganze Woerter" braucht auch fuer den WOERTLICHEN Zweig einen Ausdruck -
    //  `QString::indexOf` kennt keine Wortgrenze. Nur dann wird er gebaut.
    if (m_whole) {
        m_literalRe = QRegularExpression(
            QStringLiteral("\\b(?:%1)\\b").arg(QRegularExpression::escape(m_literal)), opt);
        m_literalRe.optimize();
    }

    if (!hasMetaCharacters(m_literal)) return;   // beide Zweige waeren gleich

    QString muster = m_literal;
    if (m_whole) muster = QStringLiteral("\\b(?:%1)\\b").arg(muster);
    QRegularExpression re(muster, opt);
    if (!re.isValid()) return;                   // halbfertige Eingabe: still weg
    re.optimize();
    m_re = re;
    m_reAktiv = true;
}

bool Pattern::contains(QStringView hay) const {
    if (m_literal.isEmpty()) return false;
    if (m_whole) {
        if (m_literalRe.match(hay).hasMatch()) return true;
    } else if (hay.contains(m_literal, caseSensitivity())) {
        return true;
    }
    return m_reAktiv && m_re.match(hay).hasMatch();
}

QList<Range> Pattern::findAll(QStringView hay, int cap) const {
    QList<Range> raus;
    if (m_literal.isEmpty() || hay.isEmpty() || cap <= 0) return raus;

    //  Woertlicher Zweig.
    if (m_whole) {
        auto it = m_literalRe.globalMatch(hay);
        while (it.hasNext() && raus.size() < cap) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedLength() > 0)
                raus.append({ int(m.capturedStart()), int(m.capturedLength()) });
        }
    } else {
        const qsizetype len = m_literal.size();
        for (qsizetype i = hay.indexOf(m_literal, 0, caseSensitivity());
             i >= 0 && raus.size() < cap;
             i = hay.indexOf(m_literal, i + len, caseSensitivity()))
            raus.append({ int(i), int(len) });
    }

    //  Muster-Zweig obendrauf.
    if (m_reAktiv) {
        auto it = m_re.globalMatch(hay);
        while (it.hasNext() && raus.size() < 2 * cap) {
            const QRegularExpressionMatch m = it.next();
            //  Leere Treffer (`a*`) verwerfen - nicht markierbar, nicht
            //  ersetzbar, und `globalMatch` liefert sie an JEDER Position.
            if (m.capturedLength() > 0)
                raus.append({ int(m.capturedStart()), int(m.capturedLength()) });
        }
    }

    mergeRanges(raus);
    if (raus.size() > cap) raus.resize(cap);
    return raus;
}

Range Pattern::firstFrom(QStringView hay, int from) const {
    for (const Range& r : findAll(hay))
        if (r.start >= from) return r;
    return {};
}

Range Pattern::lastEndingAtOrBefore(QStringView hay, int end) const {
    Range best;
    for (const Range& r : findAll(hay)) {
        if (r.start + r.length > end) break;      // findAll liefert sortiert
        best = r;
    }
    return best;
}

bool Pattern::matchesWhole(QStringView hay) const {
    if (hay.isEmpty()) return false;
    const QList<Range> r = findAll(hay, 2);
    return r.size() == 1 && r.first().start == 0
        && r.first().length == int(hay.size());
}

}  // namespace mg::search
