#include "editor/MarkdownParser.h"

#include <QTextDocumentFragment>
#include <array>

using namespace Qt::StringLiterals;

namespace mg::editor::md {
namespace {

// Zitate, Listen und Details rufen sich selbst auf; `>>>>…` in zehntausend Stufen liefe sonst den Stapel voll.
constexpr int kMaxDepth = 32;
// Suchweite fuer die schliessende Klammer eines Verweises - tausende offene `[` waeren sonst quadratisch.
constexpr int kMaxLinkScan = 2000;
constexpr int kMaxInlineDepth = 6;

bool isSpace(QChar c) { return c == u' ' || c == u'\t'; }

bool isBlank(QStringView l) {
    for (QChar c : l)
        if (!isSpace(c)) return false;
    return true;
}

int indentOf(QStringView l) {
    int n = 0;
    while (n < l.size() && l[n] == u' ') ++n;
    return n;
}

QStringView stripUpTo(QStringView l, int n) {
    int k = 0;
    while (k < n && k < l.size() && l[k] == u' ') ++k;
    return l.mid(k);
}

QStringView leftTrimmed(QStringView l) {
    int k = 0;
    while (k < l.size() && isSpace(l[k])) ++k;
    return l.mid(k);
}

bool isAsciiPunct(QChar c) {
    const ushort u = c.unicode();
    return (u >= 33 && u <= 47) || (u >= 58 && u <= 64) || (u >= 91 && u <= 96) || (u >= 123 && u <= 126);
}

bool isPunct(QChar c) { return isAsciiPunct(c) || c.isPunct() || c.isSymbol(); }

bool isAsciiDigit(QChar c) { return c >= u'0' && c <= u'9'; }

bool isAsciiAlpha(QChar c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }

// ---------- Blockerkennung ----------

struct Fence {
    QChar ch;
    int len = 0;
    int indent = 0;
    QString info;
};

bool fenceOpen(QStringView l, Fence& f) {
    const int ind = indentOf(l);
    if (ind > 3 || ind >= l.size()) return false;
    const QStringView r = l.mid(ind);
    const QChar c = r[0];
    if (c != u'`' && c != u'~') return false;
    int n = 0;
    while (n < r.size() && r[n] == c) ++n;
    if (n < 3) return false;
    const QStringView info = r.mid(n).trimmed();
    if (c == u'`' && info.contains(u'`')) return false;
    f.ch = c;
    f.len = n;
    f.indent = ind;
    f.info = info.toString();
    return true;
}

bool fenceClose(QStringView l, const Fence& f) {
    const int ind = indentOf(l);
    if (ind > 3) return false;
    const QStringView r = l.mid(ind);
    int n = 0;
    while (n < r.size() && r[n] == f.ch) ++n;
    return n >= f.len && isBlank(r.mid(n));
}

bool atxHeading(QStringView l, int& level, QString& text) {
    const int ind = indentOf(l);
    if (ind > 3) return false;
    const QStringView r = l.mid(ind);
    int n = 0;
    while (n < r.size() && r[n] == u'#') ++n;
    if (n < 1 || n > 6) return false;
    if (n < r.size() && !isSpace(r[n])) return false;
    QStringView t = r.mid(n).trimmed();
    int k = t.size();
    while (k > 0 && t[k - 1] == u'#') --k;
    if (k < t.size() && (k == 0 || isSpace(t[k - 1]))) t = t.left(k).trimmed();
    level = n;
    text = t.toString();
    return true;
}

bool isRule(QStringView l) {
    const int ind = indentOf(l);
    if (ind > 3) return false;
    QChar ch;
    int cnt = 0;
    for (QChar x : l.mid(ind)) {
        if (isSpace(x)) continue;
        if (x != u'-' && x != u'*' && x != u'_') return false;
        if (ch.isNull()) ch = x;
        else if (x != ch) return false;
        ++cnt;
    }
    return cnt >= 3;
}

int setextLevel(QStringView l) {
    if (indentOf(l) > 3) return 0;
    const QStringView t = l.trimmed();
    if (t.isEmpty() || (t[0] != u'=' && t[0] != u'-')) return 0;
    for (QChar x : t)
        if (x != t[0]) return 0;
    return t[0] == u'=' ? 1 : 2;
}

struct Marker {
    bool ordered = false;
    QChar ch;
    int number = 1;
    int width = 0;          // Spalte, ab der der Inhalt des Eintrags steht
    bool empty = false;
};

bool listMarker(QStringView l, Marker& m) {
    const int ind = indentOf(l);
    if (ind > 3 || ind >= l.size()) return false;
    const QStringView r = l.mid(ind);
    int mlen = 0;
    if (r[0] == u'-' || r[0] == u'+' || r[0] == u'*') {
        m.ordered = false;
        m.ch = r[0];
        mlen = 1;
    } else {
        int d = 0;
        while (d < r.size() && d < 9 && isAsciiDigit(r[d])) ++d;
        if (d == 0 || d >= r.size() || (r[d] != u'.' && r[d] != u')')) return false;
        m.ordered = true;
        m.ch = r[d];
        m.number = r.left(d).toInt();
        mlen = d + 1;
    }
    if (mlen < r.size() && !isSpace(r[mlen])) return false;
    const QStringView rest = r.mid(mlen);
    int sp = 0;
    while (sp < rest.size() && rest[sp] == u' ') ++sp;
    m.empty = isBlank(rest);
    // Mehr als vier Leerzeichen: der Inhalt ist ein eingerueckter Codeblock, der Eintrag beginnt nach einem.
    if (m.empty || sp == 0 || sp > 4) sp = 1;
    m.width = ind + mlen + sp;
    return true;
}

bool isQuoteLine(QStringView l) {
    const int ind = indentOf(l);
    return ind <= 3 && ind < l.size() && l[ind] == u'>';
}

QString stripQuote(QStringView l) {
    QStringView r = l.mid(indentOf(l) + 1);
    if (!r.isEmpty() && isSpace(r[0])) r = r.mid(1);
    return r.toString();
}

bool hasUnescapedPipe(QStringView l) {
    for (int i = 0; i < l.size(); ++i) {
        if (l[i] == u'\\') { ++i; continue; }
        if (l[i] == u'|') return true;
    }
    return false;
}

QStringList splitRow(QStringView l) {
    QStringView t = l.trimmed();
    if (t.startsWith(u'|')) t = t.mid(1);
    if (t.endsWith(u'|') && !(t.size() >= 2 && t[t.size() - 2] == u'\\')) t.chop(1);
    QStringList cells;
    QString cur;
    for (int i = 0; i < t.size(); ++i) {
        const QChar c = t[i];
        if (c == u'\\' && i + 1 < t.size()) {
            cur += c;
            cur += t[++i];
            continue;
        }
        if (c == u'|') {
            cells << cur.trimmed();
            cur.clear();
            continue;
        }
        cur += c;
    }
    cells << cur.trimmed();
    return cells;
}

bool delimiterRow(QStringView l, QList<Align>& al) {
    if (indentOf(l) > 3 || !hasUnescapedPipe(l)) return false;
    const QStringList cells = splitRow(l);
    al.clear();
    for (const QString& c : cells) {
        const QStringView x = QStringView(c).trimmed();
        if (x.isEmpty()) return false;
        const bool links = x.startsWith(u':');
        const bool rechts = x.size() > 1 && x.endsWith(u':');
        QStringView kern = x.mid(links ? 1 : 0);
        if (rechts) kern.chop(1);
        if (kern.isEmpty()) return false;
        for (QChar ch : kern)
            if (ch != u'-') return false;
        al << (links && rechts ? Align::Center : rechts ? Align::Right : links ? Align::Left : Align::None);
    }
    return !al.isEmpty();
}

bool tableStart(const QStringList& lines, int i, QList<Align>& al) {
    return i + 1 < lines.size() && indentOf(lines[i]) <= 3 && hasUnescapedPipe(lines[i])
        && delimiterRow(lines[i + 1], al) && splitRow(lines[i]).size() == al.size();
}

bool detailsOpen(QStringView l, bool& open, QString& after) {
    const QStringView t = l.trimmed();
    if (!t.startsWith(u"<details", Qt::CaseInsensitive)) return false;
    if (t.size() > 8 && t[8] != u'>' && !isSpace(t[8])) return false;
    const int gt = t.indexOf(u'>');
    if (gt < 0) return false;
    open = false;
    const QString attrs = t.mid(8, gt - 8).toString().toLower();
    for (const QString& w : attrs.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (w == u"open" || w.startsWith(u"open=")) open = true;
    after = t.mid(gt + 1).toString();
    return true;
}

bool isDetailsClose(QStringView l) {
    return l.trimmed().startsWith(u"</details", Qt::CaseInsensitive);
}

bool isHtmlCommentStart(QStringView l) {
    return indentOf(l) <= 3 && leftTrimmed(l).startsWith(u"<!--");
}

// Zeilen, die NUR aus Blockrahmen-Tags bestehen (`<div align="center">`, `</p>`, `<a name="x"></a>`): sie tragen
// keinen Inhalt und fallen weg; der Text dazwischen wird gewoehnlich zerlegt.
bool isBlockTagLine(QStringView l) {
    static const QStringList tags = {
        u"div"_s, u"p"_s, u"center"_s, u"section"_s, u"article"_s, u"aside"_s, u"header"_s,
        u"footer"_s, u"nav"_s, u"figure"_s, u"figcaption"_s, u"main"_s, u"a"_s, u"br"_s, u"hr"_s,
        u"picture"_s, u"source"_s };
    QStringView t = l.trimmed();
    if (!t.startsWith(u'<') || indentOf(l) > 3) return false;
    bool eins = false;
    while (!t.isEmpty()) {
        if (t[0] != u'<') return false;
        int k = 1;
        if (k < t.size() && t[k] == u'/') ++k;
        const int nameStart = k;
        while (k < t.size() && (isAsciiAlpha(t[k]) || isAsciiDigit(t[k]))) ++k;
        if (!tags.contains(t.mid(nameStart, k - nameStart).toString().toLower())) return false;
        const int gt = t.indexOf(u'>', k);
        if (gt < 0) return false;
        t = t.mid(gt + 1).trimmed();
        eins = true;
    }
    return eins;
}

QString firstWord(const QString& info) {
    const QStringView t = QStringView(info).trimmed();
    int k = 0;
    while (k < t.size() && !isSpace(t[k]) && t[k] != u'{' && t[k] != u',') ++k;
    return t.left(k).toString();
}

// Definition `[label]: ziel "titel"` - nur einzeilig.
bool linkRefDef(QStringView l, QString& label, LinkRef& ref) {
    if (indentOf(l) > 3) return false;
    const QStringView r = leftTrimmed(l);
    if (!r.startsWith(u'[') || r.startsWith(u"[^")) return false;
    int k = 1;
    while (k < r.size() && r[k] != u']') {
        if (r[k] == u'\\') ++k;
        else if (r[k] == u'[') return false;
        ++k;
    }
    if (k >= r.size() || k == 1 || k > 1000 || k + 1 >= r.size() || r[k + 1] != u':') return false;
    label = r.mid(1, k - 1).toString();
    QStringView rest = leftTrimmed(r.mid(k + 2));
    if (rest.isEmpty()) return false;
    QString url;
    if (rest[0] == u'<') {
        const int gt = rest.indexOf(u'>');
        if (gt < 0) return false;
        url = rest.mid(1, gt - 1).toString();
        rest = rest.mid(gt + 1);
    } else {
        int e = 0;
        while (e < rest.size() && !isSpace(rest[e])) ++e;
        url = rest.left(e).toString();
        rest = rest.mid(e);
    }
    QString title;
    const QStringView t = rest.trimmed();
    if (!t.isEmpty()) {
        const QChar o = t[0];
        const QChar c = o == u'(' ? u')' : o;
        if ((o != u'"' && o != u'\'' && o != u'(') || t.size() < 2 || t.back() != c) return false;
        if (rest.size() && !isSpace(rest[0])) return false;
        title = t.mid(1, t.size() - 2).toString();
    }
    ref.url = url;
    ref.title = title;
    return true;
}

bool footnoteDef(QStringView l, QString& label, QString& text) {
    if (indentOf(l) > 3) return false;
    const QStringView r = leftTrimmed(l);
    if (!r.startsWith(u"[^")) return false;
    const int k = r.indexOf(u"]:");
    if (k < 3) return false;
    label = r.mid(2, k - 2).toString();
    if (label.contains(u' ') || label.contains(u']')) return false;
    text = r.mid(k + 2).trimmed().toString();
    return true;
}

// ---------- Blockzerleger ----------

class BlockParser {
public:
    explicit BlockParser(Document& doc) : m_doc(doc) {}
    QList<Block> parse(const QStringList& lines, int depth, bool* blankBetween);

private:
    Document& m_doc;

    bool startsBlock(const QStringList& lines, int i) const;
    bool interruptsParagraph(const QStringList& lines, int i) const;
    static bool lazyAllowed(const QStringList& collected);
};

bool BlockParser::startsBlock(const QStringList& lines, int i) const {
    const QStringView l = lines[i];
    Fence f;
    int lvl;
    QString s;
    bool o;
    Marker m;
    QList<Align> al;
    return fenceOpen(l, f) || atxHeading(l, lvl, s) || isRule(l) || isQuoteLine(l)
        || listMarker(l, m) || detailsOpen(l, o, s) || isDetailsClose(l) || isHtmlCommentStart(l)
        || tableStart(lines, i, al);
}

bool BlockParser::interruptsParagraph(const QStringList& lines, int i) const {
    const QStringView l = lines[i];
    Marker m;
    if (listMarker(l, m)) return !m.empty && (!m.ordered || m.number == 1) && !isRule(l);
    QList<Align> al;
    Fence f;
    int lvl;
    QString s;
    bool o;
    return fenceOpen(l, f) || atxHeading(l, lvl, s) || isRule(l) || isQuoteLine(l)
        || detailsOpen(l, o, s) || isDetailsClose(l) || isHtmlCommentStart(l) || isBlockTagLine(l)
        || tableStart(lines, i, al);
}

// Eine faule Fortsetzungszeile (ohne `>` bzw. ohne Einrueckung) gehoert nur dann dazu, wenn davor ein Absatz steht -
// nie in einem offenen Code-Zaun, nie hinter einer Ueberschrift.
bool BlockParser::lazyAllowed(const QStringList& collected) {
    if (collected.isEmpty() || isBlank(collected.last())) return false;
    Fence f;
    bool inFence = false;
    for (const QString& l : collected) {
        if (inFence) {
            if (fenceClose(l, f)) inFence = false;
        } else if (fenceOpen(l, f)) {
            inFence = true;
        }
    }
    if (inFence) return false;
    const QString& last = collected.last();
    int lvl;
    QString s;
    Fence g;
    return indentOf(last) < 4 && !atxHeading(last, lvl, s) && !isRule(last) && !fenceClose(last, g)
        && !hasUnescapedPipe(last);
}

QList<Block> BlockParser::parse(const QStringList& lines, int depth, bool* blankBetween) {
    QList<Block> out;
    const int n = lines.size();
    const bool nest = depth < kMaxDepth;
    bool pendingBlank = false;
    auto push = [&](Block&& b) {
        if (pendingBlank && !out.isEmpty() && blankBetween) *blankBetween = true;
        pendingBlank = false;
        out.append(std::move(b));
    };

    int i = 0;
    while (i < n) {
        const QStringView l = lines[i];
        if (isBlank(l)) {
            pendingBlank = true;
            ++i;
            continue;
        }

        Fence f;
        if (fenceOpen(l, f)) {
            Block b;
            b.kind = Block::Code;
            b.info = firstWord(f.info);
            QStringList body;
            ++i;
            while (i < n && !fenceClose(lines[i], f)) body << stripUpTo(lines[i++], f.indent).toString();
            if (i < n) ++i;
            b.text = body.join(QLatin1Char('\n'));
            push(std::move(b));
            continue;
        }

        int lvl = 0;
        QString txt;
        if (atxHeading(l, lvl, txt)) {
            Block b;
            b.kind = Block::Heading;
            b.level = lvl;
            b.text = txt;
            push(std::move(b));
            ++i;
            continue;
        }

        if (isRule(l)) {
            Block b;
            b.kind = Block::Rule;
            push(std::move(b));
            ++i;
            continue;
        }

        if (indentOf(l) >= 4) {
            QStringList body;
            while (i < n && (isBlank(lines[i]) || indentOf(lines[i]) >= 4))
                body << stripUpTo(lines[i++], 4).toString();
            while (!body.isEmpty() && isBlank(body.last())) body.removeLast();
            Block b;
            b.kind = Block::Code;
            b.text = body.join(QLatin1Char('\n'));
            push(std::move(b));
            continue;
        }

        if (nest && isQuoteLine(l)) {
            QStringList q;
            while (i < n) {
                const QStringView x = lines[i];
                if (isQuoteLine(x)) {
                    q << stripQuote(x);
                    ++i;
                } else if (!isBlank(x) && lazyAllowed(q) && !startsBlock(lines, i)) {
                    q << leftTrimmed(x).toString();
                    ++i;
                } else {
                    break;
                }
            }
            Block b;
            b.kind = Block::Quote;
            b.children = parse(q, depth + 1, nullptr);
            push(std::move(b));
            continue;
        }

        Marker m;
        if (nest && listMarker(l, m)) {
            Block list;
            list.kind = Block::List;
            list.ordered = m.ordered;
            list.start = m.number;
            const QChar ch = m.ch;
            bool loose = false;
            int trailing = 0;
            for (;;) {
                QStringList itemLines;
                itemLines << (m.empty ? QString() : lines[i].mid(m.width));
                ++i;
                bool sawBlank = false;
                while (i < n) {
                    const QStringView x = lines[i];
                    if (isBlank(x)) {
                        // Ein leer begonnener Eintrag endet an der ersten Leerzeile.
                        if (m.empty && itemLines.size() == 1) break;
                        itemLines << QString();
                        sawBlank = true;
                        ++i;
                        continue;
                    }
                    if (indentOf(x) >= m.width) {
                        itemLines << x.mid(m.width).toString();
                        sawBlank = false;
                        ++i;
                        continue;
                    }
                    if (!sawBlank && lazyAllowed(itemLines) && !startsBlock(lines, i)) {
                        itemLines << leftTrimmed(x).toString();
                        ++i;
                        continue;
                    }
                    break;
                }
                trailing = 0;
                while (!itemLines.isEmpty() && isBlank(itemLines.last())) {
                    itemLines.removeLast();
                    ++trailing;
                }
                bool innerBlank = false;
                Block item;
                item.kind = Block::Item;
                item.children = parse(itemLines, depth + 1, &innerBlank);
                if (innerBlank) loose = true;
                if (!item.children.isEmpty() && item.children.first().kind == Block::Paragraph) {
                    QString& t = item.children.first().text;
                    if (t.size() >= 3 && t[0] == u'[' && t[2] == u']'
                        && (t[1] == u' ' || t[1] == u'x' || t[1] == u'X')
                        && (t.size() == 3 || isSpace(t[3]) || t[3] == u'\n')) {
                        item.task = t[1] == u' ' ? 0 : 1;
                        t = t.mid(4);
                    }
                }
                list.children << item;

                Marker nm;
                if (i < n && listMarker(lines[i], nm) && nm.ordered == m.ordered && nm.ch == ch
                    && !isRule(lines[i])) {
                    if (trailing > 0) loose = true;
                    m = nm;
                    continue;
                }
                break;
            }
            list.tight = !loose;
            push(std::move(list));
            pendingBlank = trailing > 0;
            continue;
        }

        bool open = false;
        QString after;
        if (nest && detailsOpen(l, open, after)) {
            Block b;
            b.kind = Block::Details;
            b.open = open;
            b.id = m_doc.detailsCount++;
            ++i;
            // Die Summary steht auf derselben Zeile oder auf den naechsten; was hinter `</summary>` folgt, ist Inhalt.
            QString buf = after;
            int probe = i;
            while (!buf.contains(u"<summary", Qt::CaseInsensitive) && probe < n && isBlank(lines[probe]))
                ++probe;
            if (!buf.contains(u"<summary", Qt::CaseInsensitive) && probe < n
                && leftTrimmed(lines[probe]).startsWith(u"<summary", Qt::CaseInsensitive)) {
                buf += lines[probe];
                i = probe + 1;
            }
            QStringList content;
            const int so = buf.indexOf(u"<summary", 0, Qt::CaseInsensitive);
            if (so >= 0) {
                int guard = 0;
                while (!buf.contains(u"</summary>", Qt::CaseInsensitive) && i < n && guard++ < 20)
                    buf += QLatin1Char('\n') + lines[i++];
                const int gt = buf.indexOf(u'>', so);
                const int sc = buf.indexOf(u"</summary>", 0, Qt::CaseInsensitive);
                if (gt >= 0 && sc > gt) {
                    b.text = buf.mid(gt + 1, sc - gt - 1).trimmed();
                    const QString rest = buf.mid(sc + 10);
                    if (!isBlank(rest)) content << rest.trimmed();
                } else {
                    b.text = buf.mid(so).trimmed();
                }
            } else if (!isBlank(buf)) {
                content << buf.trimmed();
            }
            int tiefe = 1;
            Fence g;
            bool inFence = false;
            while (i < n) {
                const QString& x = lines[i];
                if (inFence) {
                    if (fenceClose(x, g)) inFence = false;
                } else if (fenceOpen(x, g)) {
                    inFence = true;
                } else {
                    bool o2;
                    QString a2;
                    if (detailsOpen(x, o2, a2)) {
                        ++tiefe;
                    } else if (isDetailsClose(x)) {
                        if (--tiefe == 0) { ++i; break; }
                    } else if (x.trimmed().endsWith(u"</details>", Qt::CaseInsensitive)) {
                        if (--tiefe == 0) {
                            const QString t = x.trimmed();
                            content << t.left(t.size() - 10);
                            ++i;
                            break;
                        }
                    }
                }
                content << x;
                ++i;
            }
            if (content.size() && content.first().isEmpty()) content.removeFirst();
            b.children = parse(content, depth + 1, nullptr);
            push(std::move(b));
            continue;
        }

        if (isDetailsClose(l) || isBlockTagLine(l)) {
            ++i;
            continue;
        }

        if (isHtmlCommentStart(l)) {
            while (i < n && !lines[i].contains(u"-->")) ++i;
            ++i;
            continue;
        }

        QList<Align> al;
        if (tableStart(lines, i, al)) {
            Block b;
            b.kind = Block::Table;
            b.aligns = al;
            const int spalten = al.size();
            auto zeile = [spalten](QStringView x) {
                QStringList c = splitRow(x);
                while (c.size() < spalten) c << QString();
                while (c.size() > spalten) c.removeLast();
                return c;
            };
            b.rows << zeile(lines[i]);
            i += 2;
            while (i < n && !isBlank(lines[i]) && !startsBlock(lines, i)) b.rows << zeile(lines[i++]);
            push(std::move(b));
            continue;
        }

        QString label;
        LinkRef ref;
        if (linkRefDef(l, label, ref)) {
            const QString key = normalizeLabel(label);
            if (!m_doc.refs.contains(key)) m_doc.refs.insert(key, ref);
            ++i;
            continue;
        }
        QString fnText;
        if (footnoteDef(l, label, fnText)) {
            ++i;
            while (i < n && !isBlank(lines[i]) && (indentOf(lines[i]) >= 4 || !startsBlock(lines, i)))
                fnText += QLatin1Char(' ') + lines[i++].trimmed();
            m_doc.footnotes << Footnote{label, fnText};
            continue;
        }

        QStringList para;
        para << leftTrimmed(l).toString();
        ++i;
        int setext = 0;
        while (i < n) {
            const QStringView x = lines[i];
            if (isBlank(x)) break;
            setext = setextLevel(x);
            if (setext) { ++i; break; }
            if (interruptsParagraph(lines, i)) break;
            para << leftTrimmed(x).toString();
            ++i;
        }
        Block b;
        b.kind = setext ? Block::Heading : Block::Paragraph;
        b.level = setext;
        b.text = para.join(QLatin1Char('\n'));
        if (setext) b.text = b.text.trimmed();
        push(std::move(b));
    }
    return out;
}

QList<QPair<QString, QString>> frontMatterPairs(const QStringList& lines) {
    QList<QPair<QString, QString>> out;
    for (const QString& l : lines) {
        if (isBlank(l) || l.trimmed().startsWith(u'#')) continue;
        const bool fortsetzung = (isSpace(l[0]) || l[0] == u'-') && !out.isEmpty();
        const int colon = l.indexOf(u':');
        if (fortsetzung || colon < 0) {
            if (out.isEmpty()) out.append({l.trimmed(), QString()});
            else {
                QString& v = out.last().second;
                if (!v.isEmpty()) v += QLatin1Char(' ');
                v += l.trimmed();
            }
            continue;
        }
        QString v = l.mid(colon + 1).trimmed();
        if (v.size() >= 2 && (v.front() == u'"' || v.front() == u'\'') && v.back() == v.front())
            v = v.mid(1, v.size() - 2);
        out.append({l.left(colon).trimmed(), v});
    }
    return out;
}

// ---------- Inline ----------

// Benannte Entitaeten loest Qts HTML-Leser auf, eine eigene Tabelle gibt es nicht. Geprueft wird vorher, dass der
// Name nur aus Buchstaben und Ziffern besteht - sonst liefe Markup mit durch (`amp;<b>x</b>` wurde zu `&x`).
QString namedEntity(QStringView name) {
    if (name.isEmpty() || name.size() > 32) return {};
    for (QChar c : name)
        if (!isAsciiAlpha(c) && !isAsciiDigit(c)) return {};
    thread_local QHash<QString, QString> cache;
    const QString key = name.toString();
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return *it;
    // Mit Text drumherum: ein Fragment nur aus Leerraum verwirft Qt ganz. Unbekannte Namen kommen woertlich zurueck.
    const QString roh = QTextDocumentFragment::fromHtml(u"x&"_s + key + u";x"_s).toRawText();
    QString wert;
    if (roh.size() >= 3 && roh.front() == u'x' && roh.back() == u'x') wert = roh.mid(1, roh.size() - 2);
    if (wert == u'&' + key + u';') wert.clear();
    if (cache.size() > 512) cache.clear();
    cache.insert(key, wert);
    return wert;
}

// `&name;`, `&#123;`, `&#x1F;` ab `p`. Liefert die Laenge, 0 wenn keine Entitaet.
int decodeEntity(QStringView s, int p, QString& out) {
    const int semi = s.indexOf(u';', p + 1);
    if (semi < 0 || semi - p > 33) return 0;
    const QStringView body = s.mid(p + 1, semi - p - 1);
    if (body.isEmpty()) return 0;
    if (body[0] != u'#') {
        const QString wert = namedEntity(body);
        if (wert.isEmpty()) return 0;
        out += wert;
        return semi - p + 1;
    }
    bool ok = false;
    const bool hex = body.size() > 1 && (body[1] == u'x' || body[1] == u'X');
    const QStringView digits = body.mid(hex ? 2 : 1);
    if (digits.isEmpty() || digits.size() > (hex ? 6 : 7)) return 0;
    const uint v = digits.toUInt(&ok, hex ? 16 : 10);
    if (!ok) return 0;
    const char32_t cp = (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) ? 0xFFFD : v;
    out += QString::fromUcs4(&cp, 1);
    return semi - p + 1;
}

quint16 htmlFlag(const QString& name) {
    if (name == u"b" || name == u"strong") return Bold;
    if (name == u"i" || name == u"em") return Italic;
    if (name == u"s" || name == u"del" || name == u"strike") return Strike;
    if (name == u"u" || name == u"ins") return Underline;
    if (name == u"mark") return Mark;
    if (name == u"sub") return Sub;
    if (name == u"sup") return Sup;
    if (name == u"kbd") return Kbd;
    return 0;
}

QString attrValue(QStringView tag, QStringView name) {
    int k = 0;
    while ((k = tag.indexOf(name, k, Qt::CaseInsensitive)) >= 0) {
        const bool grenze = k > 0 && isSpace(tag[k - 1]);
        int e = k + name.size();
        k = e;
        if (!grenze) continue;
        while (e < tag.size() && isSpace(tag[e])) ++e;
        if (e >= tag.size() || tag[e] != u'=') continue;
        ++e;
        while (e < tag.size() && isSpace(tag[e])) ++e;
        if (e >= tag.size()) return {};
        if (tag[e] == u'"' || tag[e] == u'\'') {
            const int c = tag.indexOf(tag[e], e + 1);
            return c < 0 ? QString() : tag.mid(e + 1, c - e - 1).toString();
        }
        int c = e;
        while (c < tag.size() && !isSpace(tag[c]) && tag[c] != u'>') ++c;
        return tag.mid(e, c - e).toString();
    }
    return {};
}

struct Tok {
    enum K : quint8 { Text, Delim, Code, Break, Link, Image, Open, Close };
    K k = Text;
    QString text;
    QChar ch;
    int count = 0;
    int orig = 0;
    bool canOpen = false;
    bool canClose = false;
    bool active = false;
    QList<quint16> opens;
    QList<quint16> closes;
    QString href;
    QString src;
    QList<Run> inner;
    quint16 flag = 0;
};

class InlineParser {
public:
    InlineParser(QStringView s, const QHash<QString, LinkRef>& refs, const QList<Footnote>& fns,
                 int depth, bool noLinks)
        : m_s(s), m_refs(refs), m_fns(fns), m_depth(depth), m_noLinks(noLinks) {
        for (int i = 0; i < s.size();) {
            if (s[i] != u'`') { ++i; continue; }
            int r = 0;
            while (i + r < s.size() && s[i + r] == u'`') ++r;
            m_ticks[r].append(i);
            i += r;
        }
    }

    QList<Run> run();

private:
    QStringView m_s;
    const QHash<QString, LinkRef>& m_refs;
    const QList<Footnote>& m_fns;
    int m_depth;
    bool m_noLinks;
    QHash<int, QList<int>> m_ticks;
    QList<Tok> m_toks;
    QString m_text;

    void flush();
    int codeSpanEnd(int p, int r) const;
    bool tryLink(int& p, bool image);
    bool tryAngle(int& p);
    bool tryAutolink(int& p);
    bool parseDest(int& q, QString& url);
    void processEmphasis();
    QList<Run> toRuns();
};

void InlineParser::flush() {
    if (m_text.isEmpty()) return;
    Tok t;
    t.k = Tok::Text;
    t.text = std::move(m_text);
    m_toks.append(std::move(t));
    m_text.clear();
}

// Ende (Position NACH dem schliessenden Lauf) eines Code-Spans, der bei `p` mit `r` Backticks beginnt; -1 wenn
// keiner schliesst. Die Laeufe je Laenge liegen sortiert vor - keine erneute Suche je Backtick.
int InlineParser::codeSpanEnd(int p, int r) const {
    const auto it = m_ticks.constFind(r);
    if (it == m_ticks.constEnd()) return -1;
    const QList<int>& pos = *it;
    const auto next = std::upper_bound(pos.begin(), pos.end(), p);
    return next == pos.end() ? -1 : *next + r;
}

bool InlineParser::parseDest(int& q, QString& url) {
    const int n = m_s.size();
    while (q < n && m_s[q].isSpace()) ++q;
    if (q < n && m_s[q] == u'<') {
        int e = q + 1;
        while (e < n && m_s[e] != u'>' && m_s[e] != u'\n' && m_s[e] != u'<') ++e;
        if (e >= n || m_s[e] != u'>') return false;
        url = m_s.mid(q + 1, e - q - 1).toString();
        q = e + 1;
        return true;
    }
    int par = 0;
    const int start = q;
    QString u;
    while (q < n) {
        const QChar x = m_s[q];
        if (x == u'\\' && q + 1 < n && isAsciiPunct(m_s[q + 1])) {
            u += m_s[q + 1];
            q += 2;
            continue;
        }
        if (x == u'&') {
            const int len = decodeEntity(m_s, q, u);
            if (len) { q += len; continue; }
        }
        if (x.isSpace() || x.category() == QChar::Other_Control) break;
        if (x == u'(') ++par;
        else if (x == u')') {
            if (par == 0) break;
            --par;
        }
        u += x;
        ++q;
    }
    if (par != 0 || (q == start && (q >= n || m_s[q] != u')'))) return false;
    url = u;
    return true;
}

bool InlineParser::tryLink(int& p, bool image) {
    const int n = m_s.size();
    const int open = p + (image ? 2 : 1);
    if (m_depth >= kMaxInlineDepth) return false;
    int tiefe = 1;
    int q = open;
    const int grenze = qMin(n, open + kMaxLinkScan);
    while (q < grenze) {
        const QChar x = m_s[q];
        if (x == u'\\' && q + 1 < n) { q += 2; continue; }
        if (x == u'`') {
            int r = 0;
            while (q + r < n && m_s[q + r] == u'`') ++r;
            const int e = codeSpanEnd(q, r);
            q = e > 0 ? e : q + r;
            continue;
        }
        if (x == u'[') ++tiefe;
        else if (x == u']' && --tiefe == 0) break;
        ++q;
    }
    if (q >= grenze || tiefe != 0) return false;
    const QStringView label = m_s.mid(open, q - open);
    int after = q + 1;

    if (!image && label.startsWith(u'^')) {
        const QString key = label.mid(1).toString();
        for (int f = 0; f < m_fns.size(); ++f) {
            if (m_fns[f].label != key) continue;
            flush();
            Tok t;
            t.k = Tok::Link;
            t.href = QStringLiteral("#fn-") + key;
            t.inner = {Run{Run::Text, QString::number(f + 1), Sup, {}, {}}};
            m_toks.append(std::move(t));
            p = after;
            return true;
        }
        return false;
    }
    if (!image && m_noLinks) return false;

    QString url;
    bool ok = false;
    if (after < n && m_s[after] == u'(') {
        int k = after + 1;
        if (parseDest(k, url)) {
            while (k < n && m_s[k].isSpace()) ++k;
            if (k < n && (m_s[k] == u'"' || m_s[k] == u'\'' || m_s[k] == u'(')) {
                const QChar c = m_s[k] == u'(' ? u')' : m_s[k];
                int e = k + 1;
                while (e < n && m_s[e] != c) e += (m_s[e] == u'\\') ? 2 : 1;
                k = e + 1;
                while (k < n && m_s[k].isSpace()) ++k;
            }
            if (k < n && m_s[k] == u')') {
                ok = true;
                after = k + 1;
            }
        }
    }
    if (!ok && after < n && m_s[after] == u'[') {
        const int e = m_s.indexOf(u']', after + 1);
        if (e > 0 && e - after < 1000) {
            QStringView ref = m_s.mid(after + 1, e - after - 1);
            if (ref.isEmpty()) ref = label;
            const auto it = m_refs.constFind(normalizeLabel(ref));
            if (it != m_refs.constEnd()) {
                url = it->url;
                ok = true;
                after = e + 1;
            }
        }
    }
    if (!ok) {
        const auto it = m_refs.constFind(normalizeLabel(label));
        if (it != m_refs.constEnd()) {
            url = it->url;
            ok = true;
            if (after + 1 < n && m_s[after] == u'[' && m_s[after + 1] == u']') after += 2;
        }
    }
    if (!ok) return false;

    flush();
    Tok t;
    const QList<Run> inner = InlineParser(label, m_refs, m_fns, m_depth + 1, true).run();
    if (image) {
        t.k = Tok::Image;
        t.text = plainText(inner);
        t.src = url;
    } else {
        t.k = Tok::Link;
        t.href = url;
        t.inner = inner;
    }
    m_toks.append(std::move(t));
    p = after;
    return true;
}

bool InlineParser::tryAngle(int& p) {
    if (m_s.mid(p).startsWith(u"<!--")) {
        const int e = m_s.indexOf(u"-->", p + 4);
        if (e < 0) return false;
        p = e + 3;
        return true;
    }
    const int gt = m_s.indexOf(u'>', p + 1);
    if (gt < 0) return false;
    const QStringView inhalt = m_s.mid(p + 1, gt - p - 1);

    // Autolink `<https://…>` oder `<name@host>`
    if (!inhalt.isEmpty() && !inhalt.contains(u' ') && !inhalt.contains(u'<') && !inhalt.contains(u'\n')) {
        int k = 0;
        while (k < inhalt.size() && (isAsciiAlpha(inhalt[k]) || isAsciiDigit(inhalt[k])
                                     || inhalt[k] == u'+' || inhalt[k] == u'.' || inhalt[k] == u'-'))
            ++k;
        if (k >= 2 && k <= 32 && k < inhalt.size() && inhalt[k] == u':' && isAsciiAlpha(inhalt[0])
            && !m_noLinks) {
            flush();
            Tok t;
            t.k = Tok::Link;
            t.href = inhalt.toString();
            t.inner = {Run{Run::Text, t.href, 0, {}, {}}};
            m_toks.append(std::move(t));
            p = gt + 1;
            return true;
        }
        const int at = inhalt.indexOf(u'@');
        if (at > 0 && at < inhalt.size() - 1 && inhalt.indexOf(u'.', at) > at + 1 && !m_noLinks) {
            flush();
            Tok t;
            t.k = Tok::Link;
            t.href = QStringLiteral("mailto:") + inhalt.toString();
            t.inner = {Run{Run::Text, inhalt.toString(), 0, {}, {}}};
            m_toks.append(std::move(t));
            p = gt + 1;
            return true;
        }
    }

    // Formatierende HTML-Tags; alles andere bleibt woertlich stehen (`<dir>` in Fliesstext ist Text).
    int k = 0;
    const bool schliesst = !inhalt.isEmpty() && inhalt[0] == u'/';
    if (schliesst) k = 1;
    const int nameStart = k;
    while (k < inhalt.size() && (isAsciiAlpha(inhalt[k]) || isAsciiDigit(inhalt[k]))) ++k;
    const QString name = inhalt.mid(nameStart, k - nameStart).toString().toLower();
    if (name.isEmpty() || (k < inhalt.size() && !isSpace(inhalt[k]) && inhalt[k] != u'/')) return false;

    if (name == u"br") {
        flush();
        Tok t;
        t.k = Tok::Break;
        m_toks.append(std::move(t));
        p = gt + 1;
        return true;
    }
    if (!schliesst && name == u"code") {
        const int e = m_s.indexOf(u"</code>", gt + 1, Qt::CaseInsensitive);
        if (e < 0) return false;
        flush();
        Tok t;
        t.k = Tok::Code;
        t.text = m_s.mid(gt + 1, e - gt - 1).toString();
        m_toks.append(std::move(t));
        p = e + 7;
        return true;
    }
    if (!schliesst && name == u"img") {
        const QString src = attrValue(inhalt, u"src");
        if (src.isEmpty()) return false;
        flush();
        Tok t;
        t.k = Tok::Image;
        t.src = src;
        t.text = attrValue(inhalt, u"alt");
        m_toks.append(std::move(t));
        p = gt + 1;
        return true;
    }
    if (!schliesst && name == u"a" && !m_noLinks) {
        const QString href = attrValue(inhalt, u"href");
        const int e = m_s.indexOf(u"</a>", gt + 1, Qt::CaseInsensitive);
        if (e < 0) return false;
        flush();
        if (href.isEmpty()) {
            p = gt + 1;
            return true;
        }
        Tok t;
        t.k = Tok::Link;
        t.href = href;
        t.inner = InlineParser(m_s.mid(gt + 1, e - gt - 1), m_refs, m_fns, m_depth + 1, true).run();
        m_toks.append(std::move(t));
        p = e + 4;
        return true;
    }
    if (schliesst && name == u"a") {
        p = gt + 1;
        return true;
    }
    const quint16 fl = htmlFlag(name);
    if (!fl) return false;
    flush();
    Tok t;
    t.k = schliesst ? Tok::Close : Tok::Open;
    t.flag = fl;
    m_toks.append(std::move(t));
    p = gt + 1;
    return true;
}

// GitHub-Erweiterung: `https://…`, `http://…`, `www.…` ohne spitze Klammern.
bool InlineParser::tryAutolink(int& p) {
    if (m_noLinks) return false;
    const QStringView r = m_s.mid(p);
    int praefix = 0;
    if (r.startsWith(u"https://", Qt::CaseInsensitive)) praefix = 8;
    else if (r.startsWith(u"http://", Qt::CaseInsensitive)) praefix = 7;
    else if (r.startsWith(u"www.", Qt::CaseInsensitive)) praefix = 4;
    else return false;
    if (p > 0) {
        const QChar v = m_s[p - 1];
        if (!v.isSpace() && v != u'*' && v != u'_' && v != u'~' && v != u'(') return false;
    }
    int e = praefix;
    while (e < r.size() && !r[e].isSpace() && r[e] != u'<') ++e;
    while (e > praefix) {
        const QChar c = r[e - 1];
        if (QStringView(u"?!.,:*_~'\"").contains(c)) { --e; continue; }
        if (c == u')') {
            const QStringView t = r.left(e);
            if (t.count(u'(') < t.count(u')')) { --e; continue; }
        }
        break;
    }
    if (e <= praefix) return false;
    flush();
    Tok t;
    t.k = Tok::Link;
    const QString text = r.left(e).toString();
    t.href = praefix == 4 ? QStringLiteral("http://") + text : text;
    t.inner = {Run{Run::Text, text, 0, {}, {}}};
    m_toks.append(std::move(t));
    p += e;
    return true;
}

QList<Run> InlineParser::run() {
    const int n = m_s.size();
    int p = 0;
    while (p < n) {
        const QChar c = m_s[p];
        switch (c.unicode()) {
        case u'\\':
            if (p + 1 < n && isAsciiPunct(m_s[p + 1])) {
                m_text += m_s[p + 1];
                p += 2;
                continue;
            }
            if (p + 1 < n && m_s[p + 1] == u'\n') {
                flush();
                Tok t;
                t.k = Tok::Break;
                m_toks.append(std::move(t));
                p += 2;
                while (p < n && isSpace(m_s[p])) ++p;
                continue;
            }
            m_text += c;
            ++p;
            continue;
        case u'`': {
            int r = 0;
            while (p + r < n && m_s[p + r] == u'`') ++r;
            const int e = codeSpanEnd(p, r);
            if (e < 0) {
                m_text += m_s.mid(p, r);
                p += r;
                continue;
            }
            QString inhalt = m_s.mid(p + r, e - r - p - r).toString();
            inhalt.replace(QLatin1Char('\n'), QLatin1Char(' '));
            if (inhalt.size() >= 2 && inhalt.front() == u' ' && inhalt.back() == u' '
                && !inhalt.trimmed().isEmpty())
                inhalt = inhalt.mid(1, inhalt.size() - 2);
            flush();
            Tok t;
            t.k = Tok::Code;
            t.text = inhalt;
            m_toks.append(std::move(t));
            p = e;
            continue;
        }
        case u'*':
        case u'_':
        case u'~': {
            int r = 0;
            while (p + r < n && m_s[p + r] == c) ++r;
            if (c == u'~' && r > 2) {
                m_text += m_s.mid(p, r);
                p += r;
                continue;
            }
            const QChar vor = p > 0 ? m_s[p - 1] : QChar(u' ');
            const QChar nach = p + r < n ? m_s[p + r] : QChar(u' ');
            const bool vWs = vor.isSpace(), nWs = nach.isSpace();
            const bool vP = isPunct(vor), nP = isPunct(nach);
            const bool left = !nWs && (!nP || vWs || vP);
            const bool right = !vWs && (!vP || nWs || nP);
            flush();
            Tok t;
            t.k = Tok::Delim;
            t.ch = c;
            t.count = t.orig = r;
            if (c == u'_') {
                t.canOpen = left && (!right || vP);
                t.canClose = right && (!left || nP);
            } else {
                t.canOpen = left;
                t.canClose = right;
            }
            t.active = t.canOpen || t.canClose;
            m_toks.append(std::move(t));
            p += r;
            continue;
        }
        case u'!':
            if (p + 1 < n && m_s[p + 1] == u'[' && tryLink(p, true)) continue;
            m_text += c;
            ++p;
            continue;
        case u'[':
            if (tryLink(p, false)) continue;
            m_text += c;
            ++p;
            continue;
        case u'<':
            if (tryAngle(p)) continue;
            m_text += c;
            ++p;
            continue;
        case u'&': {
            const int len = decodeEntity(m_s, p, m_text);
            if (!len) m_text += c;
            p += len ? len : 1;
            continue;
        }
        case u'\n': {
            int sp = 0;
            while (sp < m_text.size() && m_text[m_text.size() - 1 - sp] == u' ') ++sp;
            m_text.chop(sp);
            if (sp >= 2) {
                flush();
                Tok t;
                t.k = Tok::Break;
                m_toks.append(std::move(t));
            } else {
                m_text += QLatin1Char(' ');
            }
            ++p;
            while (p < n && isSpace(m_s[p])) ++p;
            continue;
        }
        case u'h':
        case u'H':
        case u'w':
        case u'W':
            if (tryAutolink(p)) continue;
            m_text += c;
            ++p;
            continue;
        default:
            m_text += c;
            ++p;
        }
    }
    while (!m_text.isEmpty() && m_text.back() == u' ') m_text.chop(1);
    flush();
    processEmphasis();
    return toRuns();
}

// CommonMark "process emphasis": von links nach rechts jeden Schliesser mit dem naechsten passenden Oeffner paaren.
// `bottom` merkt sich je Art, bis wohin erfolglos gesucht wurde - ohne das waere die Suche quadratisch.
void InlineParser::processEmphasis() {
    QList<int> stack;
    for (int i = 0; i < m_toks.size(); ++i)
        if (m_toks[i].k == Tok::Delim && m_toks[i].active) stack.append(i);

    std::array<int, 18> bottom;
    bottom.fill(-1);
    auto slot = [](const Tok& d) {
        const int art = d.ch == u'*' ? 0 : d.ch == u'_' ? 1 : 2;
        const int rest = d.ch == u'~' ? (d.count & 1) : d.orig % 3;
        return art * 6 + (d.canOpen ? 3 : 0) + rest;
    };

    int cur = 0;
    while (cur < stack.size()) {
        Tok& d = m_toks[stack[cur]];
        if (!d.active || !d.canClose || d.count == 0) {
            ++cur;
            continue;
        }
        const int b = slot(d);
        int found = -1;
        for (int k = cur - 1; k > bottom[b]; --k) {
            const Tok& o = m_toks[stack[k]];
            if (!o.active || o.ch != d.ch || !o.canOpen || o.count == 0) continue;
            if (d.ch == u'~') {
                if (o.count != d.count) continue;
            } else if ((o.canClose || d.canOpen) && (o.orig + d.orig) % 3 == 0
                       && !(o.orig % 3 == 0 && d.orig % 3 == 0)) {
                continue;
            }
            found = k;
            break;
        }
        if (found < 0) {
            bottom[b] = cur - 1;
            if (!d.canOpen) d.active = false;
            ++cur;
            continue;
        }
        Tok& o = m_toks[stack[found]];
        const int use = d.ch == u'~' ? d.count : (d.count >= 2 && o.count >= 2 ? 2 : 1);
        const quint16 fl = d.ch == u'~' ? Strike : (use == 2 ? Bold : Italic);
        o.opens.append(fl);
        o.count -= use;
        d.closes.append(fl);
        d.count -= use;
        for (int k = found + 1; k < cur; ++k) m_toks[stack[k]].active = false;
        if (o.count == 0) o.active = false;
        if (d.count == 0) {
            d.active = false;
            ++cur;
        }
    }
}

QList<Run> InlineParser::toRuns() {
    QList<Run> out;
    int cnt[16] = {};
    auto flags = [&cnt] {
        quint16 f = 0;
        for (int b = 0; b < 16; ++b)
            if (cnt[b] > 0) f |= quint16(1u << b);
        return f;
    };
    auto bit = [](quint16 f) {
        int b = 0;
        while (f > 1) { f >>= 1; ++b; }
        return b;
    };
    auto add = [&out](Run r) {
        if (r.kind == Run::Text && r.text.isEmpty()) return;
        if (r.kind == Run::Text && !out.isEmpty()) {
            Run& l = out.last();
            if (l.kind == Run::Text && l.flags == r.flags && l.href == r.href) {
                l.text += r.text;
                return;
            }
        }
        out.append(std::move(r));
    };

    for (const Tok& t : std::as_const(m_toks)) {
        switch (t.k) {
        case Tok::Text:
            add(Run{Run::Text, t.text, flags(), {}, {}});
            break;
        case Tok::Code:
            add(Run{Run::Text, t.text, quint16(flags() | Code), {}, {}});
            break;
        case Tok::Break:
            out.append(Run{Run::Break, {}, flags(), {}, {}});
            break;
        case Tok::Delim:
            for (quint16 f : t.closes) cnt[bit(f)] = qMax(0, cnt[bit(f)] - 1);
            if (t.count > 0) add(Run{Run::Text, QString(t.count, t.ch), flags(), {}, {}});
            for (auto it = t.opens.crbegin(); it != t.opens.crend(); ++it) ++cnt[bit(*it)];
            break;
        case Tok::Open:
            ++cnt[bit(t.flag)];
            break;
        case Tok::Close:
            cnt[bit(t.flag)] = qMax(0, cnt[bit(t.flag)] - 1);
            break;
        case Tok::Link: {
            const quint16 f = flags();
            for (Run r : t.inner) {
                r.flags |= f;
                if (r.href.isEmpty()) r.href = t.href;
                if (r.kind == Run::Text) add(std::move(r));
                else out.append(std::move(r));
            }
            break;
        }
        case Tok::Image:
            out.append(Run{Run::Image, t.text, flags(), {}, t.src});
            break;
        }
    }
    return out;
}

}  // namespace

QString normalizeLabel(QStringView label) {
    return label.toString().simplified().toCaseFolded();
}

QString slugify(QStringView text) {
    QString out;
    out.reserve(text.size());
    for (QChar c : text) {
        if (c.isLetterOrNumber() || c == u'_' || c == u'-') out += c.toLower();
        else if (c.isSpace()) out += QLatin1Char('-');
    }
    return out;
}

QString plainText(const QList<Run>& runs) {
    QString out;
    for (const Run& r : runs) out += r.kind == Run::Break ? QStringLiteral(" ") : r.text;
    return out;
}

QList<Run> parseInlines(QStringView text, const QHash<QString, LinkRef>& refs,
                        const QList<Footnote>& footnotes) {
    return InlineParser(text, refs, footnotes, 0, false).run();
}

Document parse(QStringView source) {
    Document doc;
    QStringList lines;
    lines.reserve(source.count(u'\n') + 1);
    for (QStringView l : source.split(u'\n')) {
        if (l.endsWith(u'\r')) l.chop(1);
        // Tabulatoren im Einzug als Viertelschritte - daran haengen Listen, Zitate und Codebloecke.
        int k = 0;
        while (k < l.size() && isSpace(l[k])) ++k;
        if (!l.left(k).contains(u'\t')) {
            lines << l.toString();
            continue;
        }
        QString z;
        for (int i = 0; i < k; ++i) {
            if (l[i] == u'\t') z += QString(4 - z.size() % 4, u' ');
            else z += u' ';
        }
        lines << z + l.mid(k);
    }
    if (!lines.isEmpty() && lines.first().startsWith(QChar(0xFEFF))) lines.first().remove(0, 1);
    // Der Umbruch am Dateiende beendet die letzte Zeile, er beginnt keine neue.
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();

    if (!lines.isEmpty() && lines.first().trimmed() == u"---") {
        for (int j = 1; j < lines.size(); ++j) {
            const QString t = lines[j].trimmed();
            if (t != u"---" && t != u"...") continue;
            Block fm;
            fm.kind = Block::FrontMatter;
            fm.pairs = frontMatterPairs(lines.mid(1, j - 1));
            if (!fm.pairs.isEmpty()) doc.blocks << fm;
            lines = lines.mid(j + 1);
            break;
        }
    }

    BlockParser bp(doc);
    doc.blocks += bp.parse(lines, 0, nullptr);
    return doc;
}

}  // namespace mg::editor::md
