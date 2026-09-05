#include "core/PdfGlyphRuns.h"

#include "core/ZCodec.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <charconv>
#include <cstring>

// Alles hier arbeitet auf ROHEN Fremddaten: vor jedem Zugriff wird die Grenze geprüft, und jede Unstimmigkeit
// führt zum Abbruch mit unveränderter Eingabe.

namespace {

// Glyphenbreiten eines CID-Zeichensatzes: `/W` führt die Ausnahmen, `/DW` die Vorgabe (bei einer Monospace
// schreibt Qt NUR `/DW`). Der Wert muss exakt der sein, mit dem auch der Betrachter rechnet.
struct Widths {
    QHash<int, double> w;
    double             dw = 1000.0;      // Vorgabe der Spezifikation
    double value(int glyph) const { return w.value(glyph, dw); }
};


inline bool isWs(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0';
}

qsizetype skipWs(const QByteArray& d, qsizetype i) {
    while (i < d.size() && isWs(d.at(i))) ++i;
    return i;
}

//  Trennzeichen der PDF-Syntax - sie beenden ein Wort AUCH ohne Leerraum
//  (`/DescendantFonts [18 0 R]` endet mit „R]", nicht mit „R").
inline bool isDelim(char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '['
        || c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

//  Nächstes Wort nach PDF-Syntax. Leer, wenn nichts mehr kommt.
//  Ein Name (`/Name`), ein Wörterbuch-Rahmen (`<<`/`>>`), eine Hex-Zeichenkette
//  (`<0001>`) und ein einzelnes Trennzeichen sind je EIN Wort.
QByteArray token(const QByteArray& d, qsizetype* i) {
    qsizetype p = skipWs(d, *i);
    if (p >= d.size()) { *i = p; return {}; }
    const qsizetype start = p;
    const char c = d.at(p);
    if (c == '/') {
        ++p;
        while (p < d.size() && !isWs(d.at(p)) && !isDelim(d.at(p))) ++p;
    } else if (c == '<' && p + 1 < d.size() && d.at(p + 1) == '<') {
        p += 2;
    } else if (c == '>' && p + 1 < d.size() && d.at(p + 1) == '>') {
        p += 2;
    } else if (c == '<') {
        ++p;
        while (p < d.size() && d.at(p) != '>') ++p;
        if (p < d.size()) ++p;                     // das '>' gehört dazu
    } else if (isDelim(c)) {
        ++p;
    } else {
        while (p < d.size() && !isWs(d.at(p)) && !isDelim(d.at(p))) ++p;
    }
    *i = p;
    return d.mid(start, p - start);
}

bool toInt(const QByteArray& t, int* out) {
    bool ok = false;
    const int v = t.toInt(&ok);
    if (ok) *out = v;
    return ok;
}

//  `<<` … `>>` ab `from` - klammernzählend, Zeichenketten `( … )` übersprungen.
//  Liefert false, wenn dort kein Wörterbuch beginnt oder es nicht schliesst.
bool dictRange(const QByteArray& d, qsizetype from, qsizetype* start, qsizetype* end) {
    const qsizetype b = skipWs(d, from);
    if (b + 1 >= d.size() || d.at(b) != '<' || d.at(b + 1) != '<') return false;
    int depth = 0;
    qsizetype i = b;
    while (i + 1 < d.size()) {
        const char c = d.at(i);
        if (c == '(') {                            // Zeichenkette überspringen
            int par = 1;
            ++i;
            while (i < d.size() && par > 0) {
                if (d.at(i) == '\\')      i += 2;
                else if (d.at(i) == '(')  { ++par; ++i; }
                else if (d.at(i) == ')')  { --par; ++i; }
                else                       ++i;
            }
            continue;
        }
        if (c == '<' && d.at(i + 1) == '<') { depth += 1; i += 2; continue; }
        if (c == '>' && d.at(i + 1) == '>') {
            depth -= 1;
            i += 2;
            if (depth == 0) { *start = b; *end = i; return true; }
            continue;
        }
        ++i;
    }
    return false;
}

//  `/Key` in einem Wörterbuch finden - nur als eigenständiger Name, damit
//  `/Length1` nicht als `/Length` durchgeht.
qsizetype afterKey(const QByteArray& dict, const char* key) {
    const QByteArray k = QByteArray("/") + key;
    qsizetype at = 0;
    while ((at = dict.indexOf(k, at)) >= 0) {
        const qsizetype next = at + k.size();
        const char c = (next < dict.size()) ? dict.at(next) : ' ';
        //  Ein Name endet an Leerraum oder einem Trennzeichen.
        if (isWs(c) || c == '/' || c == '[' || c == '<' || c == '(' || c == ']'
            || c == '>')
            return next;
        at = next;
    }
    return -1;
}

//  `/Key N 0 R` -> N. −1, wenn dort kein Verweis steht.
int refValue(const QByteArray& dict, const char* key) {
    qsizetype i = afterKey(dict, key);
    if (i < 0) return -1;
    int num = 0, gen = 0;
    if (!toInt(token(dict, &i), &num)) return -1;
    if (!toInt(token(dict, &i), &gen)) return -1;
    if (token(dict, &i) != "R") return -1;
    return num;
}

//  `/Key 123` -> 123. −1, wenn dort keine unmittelbare Zahl steht.
int intValue(const QByteArray& dict, const char* key) {
    qsizetype i = afterKey(dict, key);
    if (i < 0) return -1;
    int v = 0;
    return toInt(token(dict, &i), &v) ? v : -1;
}


struct Objects {
    QHash<int, qsizetype> offset;        // Objektnummer -> Byte-Offset
    QList<int>            byOffset;      // Objektnummern in DATEI-Reihenfolge
    QHash<int, QByteArray> body;         // Objektnummer -> "N 0 obj … endobj"
    QByteArray            header;        // alles vor dem ersten Objekt
    QByteArray            trailer;       // "<< … >>" des Trailers
    int                   size = 0;      // /Size
};

//  Letztes `startxref` -> Offset der Querverweistabelle. −1 = nicht gefunden.
qsizetype startXrefOf(const QByteArray& d) {
    const qsizetype at = d.lastIndexOf("startxref");
    if (at < 0) return -1;
    qsizetype i = at + 9;
    int v = 0;
    if (!toInt(token(d, &i), &v) || v <= 0 || v >= d.size()) return -1;
    return v;
}

//  KLASSISCHE Querverweistabelle lesen (Qt schreibt genau diese). Ein
//  Querverweis-STROM (PDF 1.5) wird abgelehnt - dann bleibt die Datei, wie
//  sie ist.
bool readXref(const QByteArray& d, qsizetype pos, Objects* o) {
    qsizetype i = pos;
    if (token(d, &i) != "xref") return false;
    for (;;) {
        const QByteArray t = token(d, &i);
        if (t == "trailer") break;
        int first = 0, count = 0;
        if (!toInt(t, &first)) return false;
        if (!toInt(token(d, &i), &count)) return false;
        if (count < 0 || count > 1'000'000) return false;
        for (int k = 0; k < count; ++k) {
            int off = 0, gen = 0;
            if (!toInt(token(d, &i), &off)) return false;
            if (!toInt(token(d, &i), &gen)) return false;
            const QByteArray kind = token(d, &i);
            if (kind != "n" && kind != "f") return false;
            if (kind != "n") continue;
            if (off <= 0 || off >= d.size()) return false;
            o->offset.insert(first + k, off);
        }
    }
    qsizetype ds = 0, de = 0;
    if (!dictRange(d, i, &ds, &de)) return false;
    o->trailer = d.mid(ds, de - ds);
    if (o->trailer.contains("/Prev") || o->trailer.contains("/XRefStm"))
        return false;                    // fortgeschriebene Datei - Finger weg
    o->size = intValue(o->trailer, "Size");
    return o->size > 0;
}

//  Objektkörper zuschneiden: von seinem Offset bis zum Beginn des NÄCHSTEN
//  Objekts in der Datei (bzw. bis zur Querverweistabelle). Das kommt ohne
//  Deutung der Strominhalte aus - `endobj` kann in Binärdaten stehen.
bool cutBodies(const QByteArray& d, qsizetype xrefPos, Objects* o) {
    o->byOffset = o->offset.keys();
    std::sort(o->byOffset.begin(), o->byOffset.end(),
              [o](int a, int b) { return o->offset.value(a) < o->offset.value(b); });
    if (o->byOffset.isEmpty()) return false;
    o->header = d.left(o->offset.value(o->byOffset.first()));
    for (int k = 0; k < o->byOffset.size(); ++k) {
        const int num   = o->byOffset.at(k);
        const qsizetype s = o->offset.value(num);
        const qsizetype e = (k + 1 < o->byOffset.size())
                            ? o->offset.value(o->byOffset.at(k + 1)) : xrefPos;
        if (e <= s || e > d.size()) return false;
        const QByteArray b = d.mid(s, e - s);
        //  Der Körper MUSS mit "<num> 0 obj" beginnen - sonst stimmt die
        //  Tabelle nicht mit der Datei überein.
        qsizetype i = 0;
        int n = 0, gen = 0;
        if (!toInt(token(b, &i), &n) || n != num) return false;
        if (!toInt(token(b, &i), &gen)) return false;
        if (token(b, &i) != "obj") return false;
        o->body.insert(num, b);
    }
    return true;
}

//  Das Wörterbuch eines Objekts (leer, wenn es keines ist - Zahl, Feld, …).
QByteArray dictOf(const Objects& o, int num) {
    const QByteArray b = o.body.value(num);
    if (b.isEmpty()) return {};
    qsizetype i = 0;
    token(b, &i); token(b, &i); token(b, &i);      // "N 0 obj"
    qsizetype ds = 0, de = 0;
    if (!dictRange(b, i, &ds, &de)) return {};
    return b.mid(ds, de - ds);
}


struct Stream {
    QByteArray dict;
    QByteArray data;        // roh, wie in der Datei
    bool       flate = false;
};

bool readStream(const Objects& o, int num, Stream* s) {
    const QByteArray b = o.body.value(num);
    if (b.isEmpty()) return false;
    qsizetype i = 0;
    token(b, &i); token(b, &i); token(b, &i);
    qsizetype ds = 0, de = 0;
    if (!dictRange(b, i, &ds, &de)) return false;
    s->dict = b.mid(ds, de - ds);

    const qsizetype kw = b.indexOf("stream", de);
    if (kw < 0) return false;
    qsizetype p = kw + 6;
    if (p < b.size() && b.at(p) == '\r') ++p;
    if (p >= b.size() || b.at(p) != '\n') return false;
    ++p;

    //  ZUERST die indirekte Form prüfen: `/Length 13 0 R` liest sich sonst als
    //  die Zahl 13 (die Objektnummer) - und der Strom wäre 13 Byte lang.
    int len = -1;
    const int lenObj = refValue(s->dict, "Length");
    if (lenObj >= 0) {
        const QByteArray lb = o.body.value(lenObj);
        if (lb.isEmpty()) return false;
        qsizetype q = 0;
        token(lb, &q); token(lb, &q); token(lb, &q);
        if (!toInt(token(lb, &q), &len)) return false;
    } else {
        len = intValue(s->dict, "Length");
    }
    if (len < 0 || p + len > b.size()) return false;
    s->data = b.mid(p, len);

    const qsizetype fk = afterKey(s->dict, "Filter");
    if (fk >= 0) {
        qsizetype q = fk;
        if (token(s->dict, &q) != "/FlateDecode") return false;   // fremd -> Finger weg
        s->flate = true;
    }
    return true;
}

QByteArray buildStreamObject(int num, const QByteArray& data, bool flate) {
    QByteArray out;
    out.reserve(data.size() + 96);
    out += QByteArray::number(num) + " 0 obj\n<<\n/Length "
           + QByteArray::number(data.size()) + "\n";
    if (flate) out += "/Filter /FlateDecode\n";
    out += ">>\nstream\n";
    out += data;
    out += "\nendstream\nendobj\n";
    return out;
}

// Zwei Formen erlaubt die Spezifikation: `c [w w w ...]` und `c_first c_last w`. Qt schreibt die erste; die
// zweite kostet vier Zeilen und macht den Leser vollständig.
bool readWidths(const QByteArray& dict, Widths* out) {
    if (dict.isEmpty()) return false;
    const int dw = intValue(dict, "DW");
    if (dw > 0) out->dw = dw;
    qsizetype i = afterKey(dict, "W");
    //  Kein `/W` ist kein Fehler: dann gilt `/DW` (bzw. die Vorgabe 1000) für
    //  jede Glyphe - genau der Fall einer Monospace-Schrift.
    if (i < 0) return true;
    i = skipWs(dict, i);
    if (i >= dict.size() || dict.at(i) != '[') return false;
    ++i;
    for (;;) {
        i = skipWs(dict, i);
        if (i >= dict.size()) return false;
        if (dict.at(i) == ']') return true;
        int first = 0;
        if (!toInt(token(dict, &i), &first)) return false;
        if (first < 0 || first > 0xFFFF) return false;
        i = skipWs(dict, i);
        if (i >= dict.size()) return false;
        if (dict.at(i) == '[') {
            ++i;
            int k = 0;
            for (;;) {
                i = skipWs(dict, i);
                if (i >= dict.size()) return false;
                if (dict.at(i) == ']') { ++i; break; }
                bool ok = false;
                const double w = token(dict, &i).toDouble(&ok);
                if (!ok) return false;
                if (first + k > 0xFFFF) return false;
                out->w.insert(first + k, w);
                ++k;
            }
        } else {
            int last = 0;
            if (!toInt(token(dict, &i), &last)) return false;
            bool ok = false;
            const double w = token(dict, &i).toDouble(&ok);
            if (!ok || last < first || last > 0xFFFF) return false;
            if (last - first > 65535) return false;
            for (int c = first; c <= last; ++c) out->w.insert(c, w);
        }
    }
}


struct Line { qsizetype start; qsizetype end; };   // ohne '\n', ohne '\r'

//  „x y Td <hhhh> Tj" - der einzige Fall, den Qt für eine Glyphe schreibt.
struct GlyphOp { double dx; double dy; int glyph; };

// Der Inhaltsstrom eines großen Dokuments hat hunderttausende solcher Zeilen: die Erkenner lesen deshalb OHNE
// Zwischenkopie direkt auf den Bytes - mit `token()` kostete allein dieser Lauf mehr als das Packen des Stroms.
inline void skipWsP(const char*& p, const char* e) { while (p < e && isWs(*p)) ++p; }

//  Zahl in der Form, die Qt schreibt: [-+]?Ziffern[.Ziffern] - kein Exponent.
bool readNum(const char*& p, const char* e, double* out) {
    skipWsP(p, e);
    const char* s = p;
    if (p < e && (*p == '-' || *p == '+')) ++p;
    bool digits = false;
    while (p < e && *p >= '0' && *p <= '9') { ++p; digits = true; }
    if (p < e && *p == '.') {
        ++p;
        while (p < e && *p >= '0' && *p <= '9') { ++p; digits = true; }
    }
    if (!digits) return false;
    return std::from_chars(s, p, *out).ec == std::errc();
}

//  Genau dieses Wort erwarten (danach Leerraum oder Zeilenende).
bool expectWord(const char*& p, const char* e, const char* w, int n) {
    skipWsP(p, e);
    if (e - p < n || std::memcmp(p, w, size_t(n)) != 0) return false;
    if (e - p > n && !isWs(p[n])) return false;
    p += n;
    return true;
}

bool parseGlyphOp(const QByteArray& d, const Line& ln, GlyphOp* g) {
    const char* p = d.constData() + ln.start;
    const char* e = d.constData() + ln.end;
    if (!readNum(p, e, &g->dx)) return false;
    if (!readNum(p, e, &g->dy)) return false;
    if (!expectWord(p, e, "Td", 2)) return false;
    skipWsP(p, e);
    if (e - p < 6 || p[0] != '<' || p[5] != '>') return false;
    int v = 0;
    for (int k = 1; k <= 4; ++k) {
        const char c = p[k];
        const int digit = (c >= '0' && c <= '9') ? c - '0'
                        : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                        : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (digit < 0) return false;
        v = v * 16 + digit;
    }
    g->glyph = v;
    p += 6;
    if (!expectWord(p, e, "Tj", 2)) return false;
    skipWsP(p, e);
    return p == e;
}

bool parseFontOp(const QByteArray& d, const Line& ln, QByteArray* name, double* size) {
    const char* p = d.constData() + ln.start;
    const char* e = d.constData() + ln.end;
    skipWsP(p, e);
    if (p >= e || *p != '/') return false;
    const char* ns = ++p;
    while (p < e && !isWs(*p) && !isDelim(*p)) ++p;
    if (p == ns) return false;
    const char* ne = p;
    if (!readNum(p, e, size)) return false;
    if (!expectWord(p, e, "Tf", 2)) return false;
    *name = QByteArray(ns, ne - ns);
    return true;
}

QByteArray transformContent(const QByteArray& in,
                            const QHash<QByteArray, Widths>& fonts,
                            int* mergedOut) {
    //  Zeilen als Bereiche - kein Kopieren des ganzen Stroms (RAM).
    QList<Line> lines;
    for (qsizetype p = 0; p <= in.size();) {
        qsizetype nl = in.indexOf('\n', p);
        if (nl < 0) nl = in.size();
        qsizetype e = nl;
        if (e > p && in.at(e - 1) == '\r') --e;
        lines.append({ p, e });
        if (nl >= in.size()) break;
        p = nl + 1;
    }
    //  „a\nb\n" endet mit einem leeren Reststück - das gäbe eine Zeile zuviel.
    if (!lines.isEmpty() && lines.last().start == lines.last().end
        && !in.isEmpty() && in.endsWith('\n'))
        lines.removeLast();

    QByteArray out;
    out.reserve(in.size());
    const Widths* cur = nullptr;
    double curSize = 0.0;
    int merged = 0;

    auto copyLine = [&](const Line& ln) {
        out.append(in.constData() + ln.start, ln.end - ln.start);
        out += '\n';
    };

    for (int i = 0; i < lines.size();) {
        const Line& ln = lines.at(i);
        QByteArray fname;
        double fsize = 0.0;
        if (parseFontOp(in, ln, &fname, &fsize)) {
            const auto it = fonts.constFind(fname);
            cur     = (it != fonts.constEnd()) ? &it.value() : nullptr;
            curSize = fsize;
            copyLine(ln);
            ++i;
            continue;
        }
        GlyphOp g0{};
        if (!cur || curSize <= 0.0 || !parseGlyphOp(in, ln, &g0)) {
            copyLine(ln);
            ++i;
            continue;
        }
        //  Kette einsammeln: alle FOLGENDEN Glyphen auf derselben Grundlinie.
        QList<GlyphOp> chain{ g0 };
        int j = i + 1;
        while (j < lines.size()) {
            GlyphOp g{};
            if (!parseGlyphOp(in, lines.at(j), &g) || g.dy != 0.0) break;
            chain.append(g);
            ++j;
        }
        // Zusammenfassen NUR, wenn danach `ET` steht: `Td` verschiebt die Zeilen-Matrix, ein `TJ` nicht. Käme danach
        // ein weiteres `Td`, bezöge es sich auf den Ketten-ANFANG - der Rest der Seite verrutschte.
        bool endsAtEt = false;
        if (j < lines.size()) {
            const char* q = in.constData() + lines.at(j).start;
            const char* qe = in.constData() + lines.at(j).end;
            endsAtEt = expectWord(q, qe, "ET", 2) && (skipWsP(q, qe), q == qe);
        }
        if (chain.size() < 2 || !endsAtEt) {
            copyLine(ln);
            ++i;
            continue;
        }

        //  x y Td [ <g0> a0 <g1> a1 … <gn> ] TJ
        //  a_k verschiebt um −a_k/1000 × Schriftgrad; gewünscht ist dx_{k+1}
        //  statt der Glyphenbreite: a_k = Breite(g_k) − dx_{k+1}×1000/Grad.
        QByteArray tj;
        tj.reserve(chain.size() * 14 + 32);
        tj += QByteArray::number(chain.at(0).dx, 'f', 6) + ' '
              + QByteArray::number(chain.at(0).dy, 'f', 6) + " Td [";
        for (int k = 0; k < chain.size(); ++k) {
            if (k > 0) {
                const double adj = cur->value(chain.at(k - 1).glyph)
                                   - chain.at(k).dx * 1000.0 / curSize;
                tj += ' ';
                tj += QByteArray::number(adj, 'f', 4);
                tj += ' ';
            }
            tj += '<';
            const QByteArray hex =
                QByteArray::number(chain.at(k).glyph, 16).rightJustified(4, '0');
            tj += hex.toUpper();
            tj += '>';
        }
        tj += "] TJ";
        out += tj;
        out += '\n';
        ++merged;
        i = j;
    }
    if (mergedOut) *mergedOut += merged;
    return out;
}

// ToUnicode: Ziel U+0009 -> U+0020
//  Ersetzt wird ausschliesslich an ZIEL-Stellen. Ein blindes Suchen nach
//  „<0009>" träfe auch einen QUELL-Code 0x0009 und zerstörte die Tabelle.
QByteArray fixToUnicode(const QByteArray& in, bool* changed) {
    QByteArray out = in;
    *changed = false;

    auto replaceAt = [&](qsizetype pos) {
        //  „<0009>" -> „<0020>" an genau dieser Stelle.
        out.replace(pos + 1, 4, "0020");
        *changed = true;
    };
    //  `<lo> <hi> <dst>` bzw. `<lo> <hi> [ <d> … ]` zwischen begin/endbfrange.
    qsizetype at = 0;
    while ((at = out.indexOf("beginbfrange", at)) >= 0) {
        const qsizetype end = out.indexOf("endbfrange", at);
        if (end < 0) break;
        qsizetype i = at + 12;
        while (i < end) {
            i = skipWs(out, i);
            if (i >= end) break;
            const QByteArray lo = token(out, &i);
            if (lo.size() < 2 || lo.front() != '<') break;
            QByteArray hi = token(out, &i);
            if (hi.size() < 2 || hi.front() != '<') break;
            i = skipWs(out, i);
            if (i >= end) break;
            if (out.at(i) == '[') {
                ++i;
                for (;;) {
                    i = skipWs(out, i);
                    if (i >= end) break;
                    if (out.at(i) == ']') { ++i; break; }
                    const qsizetype dPos = i;
                    const QByteArray dst = token(out, &i);
                    if (dst.size() < 2 || dst.front() != '<') { i = end; break; }
                    if (dst == "<0009>") replaceAt(dPos);
                }
            } else {
                const qsizetype dPos = i;
                const QByteArray dst = token(out, &i);
                if (dst.size() < 2 || dst.front() != '<') break;
                if (dst == "<0009>") replaceAt(dPos);
            }
        }
        at = end + 10;
    }
    //  `<src> <dst>` zwischen begin/endbfchar.
    at = 0;
    while ((at = out.indexOf("beginbfchar", at)) >= 0) {
        const qsizetype end = out.indexOf("endbfchar", at);
        if (end < 0) break;
        qsizetype i = at + 11;
        while (i < end) {
            i = skipWs(out, i);
            if (i >= end) break;
            const QByteArray src = token(out, &i);
            if (src.size() < 2 || src.front() != '<') break;
            i = skipWs(out, i);
            if (i >= end) break;
            const qsizetype dPos = i;
            const QByteArray dst = token(out, &i);
            if (dst.size() < 2 || dst.front() != '<') break;
            if (dst == "<0009>") replaceAt(dPos);
        }
        at = end + 9;
    }
    return out;
}

}  // namespace

namespace mg::pdfglyphs {

QByteArray mergeGlyphRuns(const QByteArray& pdf) {
    if (pdf.size() < 32 || !pdf.startsWith("%PDF-")) return pdf;

    const qsizetype xrefPos = startXrefOf(pdf);
    if (xrefPos < 0) return pdf;

    Objects o;
    if (!readXref(pdf, xrefPos, &o))  return pdf;
    if (!cutBodies(pdf, xrefPos, &o)) return pdf;

    //  Seiten einsammeln: Inhaltsstrom + Zeichensätze je Ressourcenname
    struct PageJob { int content; QHash<QByteArray, Widths> fonts; };
    QList<PageJob> jobs;
    QSet<int>      toUnicodeObjs;

    for (const int num : std::as_const(o.byOffset)) {
        const QByteArray d = dictOf(o, num);
        if (d.isEmpty()) continue;
        {   //  `/Type /Page` - NICHT `/Type /Pages` (der Seitenbaum).
            qsizetype ti = afterKey(d, "Type");
            if (ti < 0 || token(d, &ti) != "/Page") continue;
        }
        const int content = refValue(d, "Contents");
        const int resNum  = refValue(d, "Resources");
        if (content < 0 || resNum < 0) continue;

        const QByteArray res = dictOf(o, resNum);
        qsizetype fk = afterKey(res, "Font");
        PageJob job;
        job.content = content;
        if (fk >= 0) {
            qsizetype fs = 0, fe = 0;
            if (dictRange(res, fk, &fs, &fe)) {
                const QByteArray fonts = res.mid(fs + 2, fe - fs - 4);
                qsizetype i = 0;
                for (;;) {
                    const QByteArray name = token(fonts, &i);
                    if (name.isEmpty() || name.front() != '/') break;
                    int n = 0, gen = 0;
                    if (!toInt(token(fonts, &i), &n)) break;
                    if (!toInt(token(fonts, &i), &gen)) break;
                    if (token(fonts, &i) != "R") break;

                    const QByteArray fd = dictOf(o, n);
                    const int tu = refValue(fd, "ToUnicode");
                    if (tu >= 0) toUnicodeObjs.insert(tu);
                    //  /DescendantFonts [ N 0 R ] -> dort steht /W.
                    qsizetype dk = afterKey(fd, "DescendantFonts");
                    if (dk < 0) continue;
                    dk = skipWs(fd, dk);
                    if (dk >= fd.size() || fd.at(dk) != '[') continue;
                    ++dk;
                    int dn = 0, dg = 0;
                    if (!toInt(token(fd, &dk), &dn)) continue;
                    if (!toInt(token(fd, &dk), &dg)) continue;
                    if (token(fd, &dk) != "R") continue;
                    Widths w;
                    if (readWidths(dictOf(o, dn), &w))
                        job.fonts.insert(name.mid(1), w);
                }
            }
        }
        if (!job.fonts.isEmpty()) jobs.append(job);
    }
    if (jobs.isEmpty() && toUnicodeObjs.isEmpty()) return pdf;

    QHash<int, QByteArray> newBody;
    int merged = 0;
    for (const PageJob& job : std::as_const(jobs)) {
        if (newBody.contains(job.content)) continue;      // Strom mehrfach benutzt
        Stream s;
        if (!readStream(o, job.content, &s)) return pdf;
        QByteArray plain = s.data;
        if (s.flate) {
            bool ok = false;
            plain = mg::zcodec::inflate(s.data, mg::zcodec::Wrap::Zlib, 0, false, &ok);
            if (!ok) return pdf;
        }
        const int before = merged;
        const QByteArray fixed = transformContent(plain, job.fonts, &merged);
        if (merged == before) continue;                   // nichts zusammenzufassen
        QByteArray packed = fixed;
        if (s.flate) {
            bool ok = false;
            packed = mg::zcodec::deflate(fixed, mg::zcodec::Wrap::Zlib, 9, &ok);
            if (!ok) return pdf;
        }
        newBody.insert(job.content, buildStreamObject(job.content, packed, s.flate));
    }

    for (const int num : std::as_const(toUnicodeObjs)) {
        Stream s;
        if (!readStream(o, num, &s)) continue;            // kein Grund abzubrechen
        QByteArray plain = s.data;
        if (s.flate) {
            bool ok = false;
            plain = mg::zcodec::inflate(s.data, mg::zcodec::Wrap::Zlib, 0, false, &ok);
            if (!ok) continue;
        }
        bool changed = false;
        const QByteArray fixed = fixToUnicode(plain, &changed);
        if (!changed) continue;
        QByteArray packed = fixed;
        if (s.flate) {
            bool ok = false;
            packed = mg::zcodec::deflate(fixed, mg::zcodec::Wrap::Zlib, 9, &ok);
            if (!ok) continue;
        }
        newBody.insert(num, buildStreamObject(num, packed, s.flate));
    }

    if (newBody.isEmpty()) return pdf;

    //  Datei neu schreiben: Kopf, Objekte in DATEI-Reihenfolge, frisches
    //     xref. Objektnummern bleiben, also bleiben alle Verweise gültig; ein
    //     nicht mehr benutztes Längen-Objekt bleibt einfach stehen.
    QByteArray out;
    out.reserve(pdf.size() + newBody.size() * 64);
    out += o.header;
    QHash<int, qsizetype> pos;
    for (const int num : std::as_const(o.byOffset)) {
        pos.insert(num, out.size());
        const auto it = newBody.constFind(num);
        out += (it != newBody.constEnd()) ? it.value() : o.body.value(num);
    }
    const qsizetype xrefAt = out.size();
    int maxNum = 0;
    for (const int num : std::as_const(o.byOffset)) maxNum = qMax(maxNum, num);
    //  Der Trailer wird UNVERÄNDERT übernommen - seine `/Size` muss also zu der
    //  Tabelle passen, die hier entsteht. Objektnummern bleiben, das ist der
    //  Normalfall; weichen sie doch ab, wird lieber nichts geändert.
    const int size = maxNum + 1;
    if (size != o.size) return pdf;
    out += "xref\n0 " + QByteArray::number(size) + "\n";
    out += "0000000000 65535 f \n";
    for (int n = 1; n < size; ++n) {
        const auto it = pos.constFind(n);
        if (it == pos.constEnd()) { out += "0000000000 65535 f \n"; continue; }
        out += QByteArray::number(it.value()).rightJustified(10, '0');
        out += " 00000 n \n";
    }
    out += "trailer\n";
    out += o.trailer;
    out += "\nstartxref\n" + QByteArray::number(xrefAt) + "\n%%EOF\n";
    return out;
}

}  // namespace mg::pdfglyphs
