#include "html/HtmlStyle.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace mg::html {

namespace stil {

class RohPool {
public:
    explicit RohPool(std::vector<std::string>& roh) : m_roh(roh) { if (m_roh.empty()) m_roh.emplace_back(); }
    uint32_t ablegen(std::string_view s) {
        if (s.empty()) return 0;
        if (auto it = m_index.find(s); it != m_index.end()) return it->second;
        const auto id = uint32_t(m_roh.size());
        m_roh.emplace_back(s);
        m_index.emplace(std::string(s), id);
        return id;
    }
    std::string_view text(uint32_t id) const { return id < m_roh.size() ? std::string_view(m_roh[id]) : std::string_view(); }

private:
    struct Hash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    std::vector<std::string>& m_roh;
    std::unordered_map<std::string, uint32_t, Hash, std::equal_to<>> m_index;
};

}   // namespace stil

namespace {

using stil::RohPool;
using stil::Umfeld;

const Stil kStandard{};

bool leer(char c) { return c == ' ' || c == '\t' || c == '\n'; }
char klein(char c) { return c >= 'A' && c <= 'Z' ? char(c | 0x20) : c; }
bool ziffer(char c) { return c >= '0' && c <= '9'; }

bool gleich(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (klein(a[i]) != klein(b[i])) return false;
    return true;
}

bool beginntMit(std::string_view a, std::string_view b) { return a.size() >= b.size() && gleich(a.substr(0, b.size()), b); }

int32_t q6(double v) { return int32_t(std::lround(std::clamp(v * kQ, -2147483520.0, 2147483520.0))); }
int32_t qZahl(double v) { return int32_t(std::lround(std::clamp(v * kQZahl, -2147483520.0, 2147483520.0))); }

//  Woerter auf oberster Ebene; -1, wenn es mehr als `max` sind.
int woerter(std::string_view v, std::string_view* aus, int max, std::string_view trenner = " \t\n") {
    int n = 0;
    size_t i = 0;
    const bool leerraum = trenner == " \t\n";
    while (i <= v.size()) {
        if (leerraum) while (i < v.size() && leer(v[i])) ++i;
        if (leerraum && i >= v.size()) break;
        const size_t j = bisAussen(v, i, v.size(), trenner);
        if (n == max) return -1;
        aus[n++] = leerraum ? v.substr(i, j - i) : ohneRand(v.substr(i, j - i));
        i = j + 1;
        if (!leerraum && j >= v.size()) break;
    }
    return n;
}

template <class E, size_t N>
bool wort(std::string_view t, const std::pair<std::string_view, E> (&tab)[N], E& aus) {
    for (const auto& [name, wert] : tab)
        if (gleich(t, name)) { aus = wert; return true; }
    return false;
}

bool leseZahl(std::string_view t, double& v, std::string_view& rest) {
    size_t i = !t.empty() && t[0] == '+' ? 1 : 0;
    if (i >= t.size()) return false;
    const size_t z = t[i] == '-' ? i + 1 : i;
    if (z >= t.size() || !(ziffer(t[z]) || (t[z] == '.' && z + 1 < t.size() && ziffer(t[z + 1])))) return false;
    const auto [p, fehler] = std::from_chars(t.data() + i, t.data() + t.size(), v);
    if (fehler != std::errc() || !std::isfinite(v)) return false;
    rest = t.substr(size_t(p - t.data()));
    return true;
}

bool reineZahl(std::string_view t, double& v) {
    std::string_view rest;
    return leseZahl(ohneRand(t), v, rest) && rest.empty();
}

struct Basis {
    double em = 16, rem = 16, vw = 10.24, vh = 7.68;
    const Stil* schrift = nullptr;      // ch/ex messen an deren Schrift, erst wenn ein Wert sie braucht
    const RohPool* pool = nullptr;
    SchriftMesser* messer = nullptr;
    const Medium* medium = nullptr;
};

double jeEm(const Basis& b, bool ch) {
    double c = 0.5, x = 0.5;
    if (b.messer && b.schrift && b.pool)
        b.messer->jeEm(b.pool->text(b.schrift->schrift), b.schrift->gewicht, b.schrift->schriftStil != SchriftStil::Normal, c, x);
    return ch ? c : x;
}

//  Linearer Wert eines calc(): px + Prozent oder eine reine Zahl.
struct Lin { double px = 0, pz = 0, zahl = 0; bool laenge = false, mitProzent = false; };

bool einheit(double v, std::string_view e, const Basis& b, Lin& aus) {
    aus = Lin{};
    aus.laenge = true;
    if (e == "%") { aus.pz = v; aus.mitProzent = true; return true; }
    double f;
    if (gleich(e, "px")) f = 1;
    else if (gleich(e, "em")) f = b.em;
    else if (gleich(e, "rem")) f = b.rem;
    else if (gleich(e, "vw")) f = b.vw;
    else if (gleich(e, "vh") || gleich(e, "vmin") || gleich(e, "vmax")) {
        if (b.medium) b.medium->nutztHoehe();
        f = gleich(e, "vh") ? b.vh : gleich(e, "vmin") ? std::min(b.vw, b.vh) : std::max(b.vw, b.vh);
    }
    else if (gleich(e, "ch") || gleich(e, "ex")) f = b.em * jeEm(b, gleich(e, "ch"));
    else if (gleich(e, "pt")) f = 96.0 / 72;
    else if (gleich(e, "pc")) f = 16;
    else if (gleich(e, "in")) f = 96;
    else if (gleich(e, "cm")) f = 96 / 2.54;
    else if (gleich(e, "mm")) f = 96 / 25.4;
    else if (gleich(e, "q")) f = 96 / 101.6;
    else return false;
    aus.px = v * f;
    return true;
}

//  calc(), min(), max(), clamp() mit + - * /. Prozente lassen sich ohne Layout nicht vergleichen: dort gilt bei
//  clamp() der mittlere, bei min()/max() der erste Wert.
class Rechner {
public:
    Rechner(std::string_view s, const Basis& b) : m_s(s), m_b(b) {}
    bool funktion(std::string_view name, std::string_view arg, Lin& aus, int tiefe) {
        if (tiefe > 32) return false;
        if (gleich(name, "calc")) return ausdruck(arg, aus, tiefe);
        std::string_view teile[8];
        const int n = woerter(arg, teile, 8, ",");
        if (n <= 0) return false;
        Lin w[8];
        for (int i = 0; i < n; ++i)
            if (!ausdruck(teile[i], w[i], tiefe) || w[i].laenge != w[0].laenge) return false;
        bool prozent = false;
        for (int i = 0; i < n; ++i) prozent |= w[i].mitProzent;
        auto wert = [](const Lin& l) { return l.laenge ? l.px : l.zahl; };
        if (gleich(name, "clamp")) {
            if (n != 3) return false;
            if (prozent) { aus = w[1]; return true; }
            aus = w[1];
            const double v = std::max(wert(w[0]), std::min(wert(w[1]), wert(w[2])));
            (aus.laenge ? aus.px : aus.zahl) = v;
            return true;
        }
        const bool klein_ = gleich(name, "min");
        if (!klein_ && !gleich(name, "max")) return false;
        aus = w[0];
        if (prozent) return true;
        for (int i = 1; i < n; ++i)
            if (klein_ ? wert(w[i]) < wert(aus) : wert(w[i]) > wert(aus)) aus = w[i];
        return true;
    }

private:
    bool ausdruck(std::string_view s, Lin& aus, int tiefe) {
        Rechner r(s, m_b);
        if (!r.summe(aus, tiefe + 1)) return false;
        r.leerraum();
        return r.m_i == s.size();
    }
    void leerraum() { while (m_i < m_s.size() && leer(m_s[m_i])) ++m_i; }
    bool summe(Lin& aus, int tiefe) {
        if (!produkt(aus, tiefe)) return false;
        while (true) {
            const size_t vor = m_i;
            leerraum();
            if (m_i >= m_s.size() || (m_s[m_i] != '+' && m_s[m_i] != '-')) { m_i = vor; return true; }
            //  + und - brauchen Leerraum auf beiden Seiten, sonst gehoeren sie zur Zahl.
            if (m_i == vor || m_i + 1 >= m_s.size() || !leer(m_s[m_i + 1])) return false;
            const bool minus = m_s[m_i++] == '-';
            Lin r;
            if (!produkt(r, tiefe) || r.laenge != aus.laenge) return false;
            const double f = minus ? -1 : 1;
            aus.px += f * r.px; aus.pz += f * r.pz; aus.zahl += f * r.zahl;
            aus.mitProzent |= r.mitProzent;
        }
    }
    bool produkt(Lin& aus, int tiefe) {
        if (!glied(aus, tiefe)) return false;
        while (true) {
            const size_t vor = m_i;
            leerraum();
            if (m_i >= m_s.size() || (m_s[m_i] != '*' && m_s[m_i] != '/')) { m_i = vor; return true; }
            const bool teilen = m_s[m_i++] == '/';
            Lin r;
            if (!glied(r, tiefe)) return false;
            if (teilen) {
                if (r.laenge || r.zahl == 0) return false;
                aus.px /= r.zahl; aus.pz /= r.zahl; aus.zahl /= r.zahl;
            } else if (!r.laenge) {
                aus.px *= r.zahl; aus.pz *= r.zahl; aus.zahl *= r.zahl;
            } else if (!aus.laenge) {
                const double f = aus.zahl;
                aus = r;
                aus.px *= f; aus.pz *= f;
                aus.mitProzent = r.mitProzent;
            } else {
                return false;
            }
        }
    }
    bool glied(Lin& aus, int tiefe) {
        leerraum();
        if (m_i >= m_s.size()) return false;
        if (m_s[m_i] == '(') {
            const size_t e = bisAussen(m_s, m_i + 1, m_s.size(), ")");
            if (e >= m_s.size() || !ausdruck(m_s.substr(m_i + 1, e - m_i - 1), aus, tiefe)) return false;
            m_i = e + 1;
            return true;
        }
        size_t k = m_i;
        while (k < m_s.size() && ((klein(m_s[k]) >= 'a' && klein(m_s[k]) <= 'z') || m_s[k] == '-')) ++k;
        if (k > m_i && k < m_s.size() && m_s[k] == '(') {
            const size_t e = bisAussen(m_s, k + 1, m_s.size(), ")");
            if (e >= m_s.size() || !funktion(m_s.substr(m_i, k - m_i), m_s.substr(k + 1, e - k - 1), aus, tiefe))
                return false;
            m_i = e + 1;
            return true;
        }
        double v;
        std::string_view rest;
        if (!leseZahl(m_s.substr(m_i), v, rest)) return false;
        size_t e = 0;
        while (e < rest.size() && ((klein(rest[e]) >= 'a' && klein(rest[e]) <= 'z') || rest[e] == '%')) ++e;
        m_i = size_t(rest.data() - m_s.data()) + e;
        if (e == 0) { aus = Lin{}; aus.zahl = v; return true; }
        return einheit(v, rest.substr(0, e), m_b, aus);
    }

    std::string_view m_s;
    const Basis& m_b;
    size_t m_i = 0;
};

enum LErlaubt : int { LProzent = 1, LNegativ = 2, LAuto = 4, LKeine = 8, LNormal = 16, LZahl = 32, LInhalt = 64 };

bool laenge(std::string_view t, const Basis& b, int erlaubt, Laenge& aus) {
    t = ohneRand(t);
    if (t.empty()) return false;
    Laenge l;
    if ((erlaubt & LAuto) && gleich(t, "auto")) { l.art = LArt::Auto; aus = l; return true; }
    if ((erlaubt & LKeine) && gleich(t, "none")) { l.art = LArt::Keine; aus = l; return true; }
    if ((erlaubt & LNormal) && gleich(t, "normal")) { l.art = LArt::Normal; aus = l; return true; }
    if (erlaubt & LInhalt) {
        if (gleich(t, "min-content")) { l.art = LArt::MinInhalt; aus = l; return true; }
        if (gleich(t, "max-content")) { l.art = LArt::MaxInhalt; aus = l; return true; }
        if (gleich(t, "fit-content")) { l.art = LArt::Passend; aus = l; return true; }
    }
    Lin w;
    const size_t klammer = t.find('(');
    if (klammer != std::string_view::npos) {
        if (t.back() != ')') return false;
        Rechner r(t, b);
        if (!r.funktion(t.substr(0, klammer), t.substr(klammer + 1, t.size() - klammer - 2), w, 0)) return false;
        //  Ein Rechenergebnis ausserhalb des Bereichs wird beschnitten, nicht verworfen; mit Prozent erst im Layout.
        if (!(erlaubt & LNegativ) && !w.mitProzent) {
            w.px = std::max(w.px, 0.0);
            w.zahl = std::max(w.zahl, 0.0);
        }
        if (!(erlaubt & LNegativ) && w.mitProzent) erlaubt |= LNegativ;
    } else {
        double v;
        std::string_view rest;
        if (!leseZahl(t, v, rest)) return false;
        if (rest.empty()) {
            w.zahl = v;
        } else if (!einheit(v, rest, b, w)) {
            return false;
        }
    }
    if (!w.laenge) {
        if (erlaubt & LZahl) {
            if (w.zahl < 0 && !(erlaubt & LNegativ)) return false;
            l.art = LArt::Zahl;
            l.px = qZahl(w.zahl);
            aus = l;
            return true;
        }
        if (w.zahl != 0) return false;
    }
    if (w.mitProzent && !(erlaubt & LProzent)) return false;
    if (!(erlaubt & LNegativ) && (w.px < 0 || w.pz < 0)) return false;
    l.px = q6(w.px);
    l.prozent = q6(w.pz);
    l.mitProzent = w.mitProzent;
    aus = l;
    return true;
}

struct Farbname { std::string_view name; uint32_t rgb; };
constexpr Farbname kFarben[] = {
    { "aliceblue", 0xf0f8ff }, { "antiquewhite", 0xfaebd7 }, { "aqua", 0x00ffff }, { "aquamarine", 0x7fffd4 },
    { "azure", 0xf0ffff }, { "beige", 0xf5f5dc }, { "bisque", 0xffe4c4 }, { "black", 0x000000 },
    { "blanchedalmond", 0xffebcd }, { "blue", 0x0000ff }, { "blueviolet", 0x8a2be2 }, { "brown", 0xa52a2a },
    { "burlywood", 0xdeb887 }, { "cadetblue", 0x5f9ea0 }, { "chartreuse", 0x7fff00 }, { "chocolate", 0xd2691e },
    { "coral", 0xff7f50 }, { "cornflowerblue", 0x6495ed }, { "cornsilk", 0xfff8dc }, { "crimson", 0xdc143c },
    { "cyan", 0x00ffff }, { "darkblue", 0x00008b }, { "darkcyan", 0x008b8b }, { "darkgoldenrod", 0xb8860b },
    { "darkgray", 0xa9a9a9 }, { "darkgreen", 0x006400 }, { "darkgrey", 0xa9a9a9 }, { "darkkhaki", 0xbdb76b },
    { "darkmagenta", 0x8b008b }, { "darkolivegreen", 0x556b2f }, { "darkorange", 0xff8c00 },
    { "darkorchid", 0x9932cc }, { "darkred", 0x8b0000 }, { "darksalmon", 0xe9967a }, { "darkseagreen", 0x8fbc8f },
    { "darkslateblue", 0x483d8b }, { "darkslategray", 0x2f4f4f }, { "darkslategrey", 0x2f4f4f },
    { "darkturquoise", 0x00ced1 }, { "darkviolet", 0x9400d3 }, { "deeppink", 0xff1493 },
    { "deepskyblue", 0x00bfff }, { "dimgray", 0x696969 }, { "dimgrey", 0x696969 }, { "dodgerblue", 0x1e90ff },
    { "firebrick", 0xb22222 }, { "floralwhite", 0xfffaf0 }, { "forestgreen", 0x228b22 }, { "fuchsia", 0xff00ff },
    { "gainsboro", 0xdcdcdc }, { "ghostwhite", 0xf8f8ff }, { "gold", 0xffd700 }, { "goldenrod", 0xdaa520 },
    { "gray", 0x808080 }, { "green", 0x008000 }, { "greenyellow", 0xadff2f }, { "grey", 0x808080 },
    { "honeydew", 0xf0fff0 }, { "hotpink", 0xff69b4 }, { "indianred", 0xcd5c5c }, { "indigo", 0x4b0082 },
    { "ivory", 0xfffff0 }, { "khaki", 0xf0e68c }, { "lavender", 0xe6e6fa }, { "lavenderblush", 0xfff0f5 },
    { "lawngreen", 0x7cfc00 }, { "lemonchiffon", 0xfffacd }, { "lightblue", 0xadd8e6 }, { "lightcoral", 0xf08080 },
    { "lightcyan", 0xe0ffff }, { "lightgoldenrodyellow", 0xfafad2 }, { "lightgray", 0xd3d3d3 },
    { "lightgreen", 0x90ee90 }, { "lightgrey", 0xd3d3d3 }, { "lightpink", 0xffb6c1 }, { "lightsalmon", 0xffa07a },
    { "lightseagreen", 0x20b2aa }, { "lightskyblue", 0x87cefa }, { "lightslategray", 0x778899 },
    { "lightslategrey", 0x778899 }, { "lightsteelblue", 0xb0c4de }, { "lightyellow", 0xffffe0 },
    { "lime", 0x00ff00 }, { "limegreen", 0x32cd32 }, { "linen", 0xfaf0e6 }, { "magenta", 0xff00ff },
    { "maroon", 0x800000 }, { "mediumaquamarine", 0x66cdaa }, { "mediumblue", 0x0000cd },
    { "mediumorchid", 0xba55d3 }, { "mediumpurple", 0x9370db }, { "mediumseagreen", 0x3cb371 },
    { "mediumslateblue", 0x7b68ee }, { "mediumspringgreen", 0x00fa9a }, { "mediumturquoise", 0x48d1cc },
    { "mediumvioletred", 0xc71585 }, { "midnightblue", 0x191970 }, { "mintcream", 0xf5fffa },
    { "mistyrose", 0xffe4e1 }, { "moccasin", 0xffe4b5 }, { "navajowhite", 0xffdead }, { "navy", 0x000080 },
    { "oldlace", 0xfdf5e6 }, { "olive", 0x808000 }, { "olivedrab", 0x6b8e23 }, { "orange", 0xffa500 },
    { "orangered", 0xff4500 }, { "orchid", 0xda70d6 }, { "palegoldenrod", 0xeee8aa }, { "palegreen", 0x98fb98 },
    { "paleturquoise", 0xafeeee }, { "palevioletred", 0xdb7093 }, { "papayawhip", 0xffefd5 },
    { "peachpuff", 0xffdab9 }, { "peru", 0xcd853f }, { "pink", 0xffc0cb }, { "plum", 0xdda0dd },
    { "powderblue", 0xb0e0e6 }, { "purple", 0x800080 }, { "rebeccapurple", 0x663399 }, { "red", 0xff0000 },
    { "rosybrown", 0xbc8f8f }, { "royalblue", 0x4169e1 }, { "saddlebrown", 0x8b4513 }, { "salmon", 0xfa8072 },
    { "sandybrown", 0xf4a460 }, { "seagreen", 0x2e8b57 }, { "seashell", 0xfff5ee }, { "sienna", 0xa0522d },
    { "silver", 0xc0c0c0 }, { "skyblue", 0x87ceeb }, { "slateblue", 0x6a5acd }, { "slategray", 0x708090 },
    { "slategrey", 0x708090 }, { "snow", 0xfffafa }, { "springgreen", 0x00ff7f }, { "steelblue", 0x4682b4 },
    { "tan", 0xd2b48c }, { "teal", 0x008080 }, { "thistle", 0xd8bfd8 }, { "tomato", 0xff6347 },
    { "turquoise", 0x40e0d0 }, { "violet", 0xee82ee }, { "wheat", 0xf5deb3 }, { "white", 0xffffff },
    { "whitesmoke", 0xf5f5f5 }, { "yellow", 0xffff00 }, { "yellowgreen", 0x9acd32 },
};

uint32_t argb(double r, double g, double b, double a) {
    auto k = [](double v) { return uint32_t(std::lround(std::clamp(v, 0.0, 255.0))); };
    return k(a * 255) << 24 | k(r) << 16 | k(g) << 8 | k(b);
}

//  Bestandteile von rgb()/hsl(): Kommas oder Leerraum, Deckkraft hinter `/` bzw. als viertes.
int farbteile(std::string_view arg, std::string_view* aus) {
    if (arg.find(',') != std::string_view::npos) return woerter(arg, aus, 4, ",");
    const size_t strich = bisAussen(arg, 0, arg.size(), "/");
    const int n = woerter(arg.substr(0, strich), aus, 3);
    if (n != 3 || strich >= arg.size()) return n;
    aus[3] = ohneRand(arg.substr(strich + 1));
    return aus[3].empty() ? -1 : 4;
}

bool anteil(std::string_view t, double skala, double& v) {
    std::string_view rest;
    if (gleich(t, "none")) { v = 0; return true; }
    if (!leseZahl(t, v, rest)) return false;
    if (rest == "%") v = v / 100 * skala;
    else if (!rest.empty()) return false;
    return true;
}

double hsl(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 0.5) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

//  font-family als Liste ohne Anfuehrungszeichen, durch Kommas getrennt.
bool familien(std::string_view t, std::string& aus) {
    std::string_view teile[32];
    const int n = woerter(t, teile, 32, ",");
    if (n <= 0) return false;
    aus.clear();
    for (int i = 0; i < n; ++i) {
        std::string_view f = teile[i];
        if (f.empty()) return false;
        if (i) aus += ',';
        if (f.front() == '"' || f.front() == '\'') {
            if (f.size() < 2 || f.back() != f.front()) return false;
            for (size_t k = 1; k + 1 < f.size(); ++k)
                if (f[k] != '\\') aus += f[k];
            continue;
        }
        std::string_view w[16];
        const int m = woerter(f, w, 16);
        if (m <= 0) return false;
        for (int k = 0; k < m; ++k) {
            if (k) aus += ' ';
            for (char c : w[k]) aus += m == 1 ? klein(c) : c;
        }
    }
    return true;
}

//  Wort-Index fuer Seite oder Ecke `i` bei 1-4 Werten (oben, rechts, unten, links).
int seite(int n, int i) {
    if (n == 1) return 0;
    if (n == 2) return i % 2;
    if (n == 3) return i == 0 ? 0 : i == 2 ? 2 : 1;
    return i;
}

const std::pair<std::string_view, RahmenStil> kRahmen[] = {
    { "none", RahmenStil::None }, { "hidden", RahmenStil::Hidden }, { "solid", RahmenStil::Solid },
    { "dashed", RahmenStil::Dashed }, { "dotted", RahmenStil::Dotted }, { "double", RahmenStil::Double },
    { "groove", RahmenStil::Groove }, { "ridge", RahmenStil::Ridge }, { "inset", RahmenStil::Inset },
    { "outset", RahmenStil::Outset },
};
const std::pair<std::string_view, Anzeige> kAnzeige[] = {
    { "inline", Anzeige::Inline }, { "block", Anzeige::Block }, { "inline-block", Anzeige::InlineBlock },
    { "list-item", Anzeige::ListItem }, { "flex", Anzeige::Flex }, { "inline-flex", Anzeige::InlineFlex },
    { "grid", Anzeige::Grid }, { "inline-grid", Anzeige::InlineGrid }, { "table", Anzeige::Table },
    { "inline-table", Anzeige::InlineTable }, { "table-row-group", Anzeige::TableRowGroup },
    { "table-header-group", Anzeige::TableHeaderGroup }, { "table-footer-group", Anzeige::TableFooterGroup },
    { "table-row", Anzeige::TableRow }, { "table-cell", Anzeige::TableCell },
    { "table-column-group", Anzeige::TableColumnGroup }, { "table-column", Anzeige::TableColumn },
    { "table-caption", Anzeige::TableCaption }, { "flow-root", Anzeige::FlowRoot }, { "contents", Anzeige::Contents },
    { "ruby", Anzeige::Ruby }, { "ruby-text", Anzeige::RubyText }, { "none", Anzeige::None },
};
const std::pair<std::string_view, Lage> kLage[] = {
    { "static", Lage::Static }, { "relative", Lage::Relative }, { "absolute", Lage::Absolute },
    { "fixed", Lage::Fixed }, { "sticky", Lage::Sticky }, { "-webkit-sticky", Lage::Sticky },
};
const std::pair<std::string_view, Ueberlauf> kUeberlauf[] = {
    { "visible", Ueberlauf::Visible }, { "hidden", Ueberlauf::Hidden }, { "clip", Ueberlauf::Clip },
    { "scroll", Ueberlauf::Scroll }, { "auto", Ueberlauf::Auto }, { "overlay", Ueberlauf::Auto },
};
const std::pair<std::string_view, FlexRichtung> kFlexRichtung[] = {
    { "row", FlexRichtung::Row }, { "row-reverse", FlexRichtung::RowReverse }, { "column", FlexRichtung::Column },
    { "column-reverse", FlexRichtung::ColumnReverse },
};
const std::pair<std::string_view, FlexUmbruch> kFlexUmbruch[] = {
    { "nowrap", FlexUmbruch::NoWrap }, { "wrap", FlexUmbruch::Wrap }, { "wrap-reverse", FlexUmbruch::WrapReverse },
};
const std::pair<std::string_view, Ausrichtung> kAusrichtung[] = {
    { "normal", Ausrichtung::Normal }, { "stretch", Ausrichtung::Stretch }, { "flex-start", Ausrichtung::FlexStart },
    { "flex-end", Ausrichtung::FlexEnd }, { "start", Ausrichtung::Start }, { "end", Ausrichtung::End },
    { "self-start", Ausrichtung::Start }, { "self-end", Ausrichtung::End }, { "center", Ausrichtung::Center },
    { "baseline", Ausrichtung::Baseline }, { "space-between", Ausrichtung::SpaceBetween },
    { "space-around", Ausrichtung::SpaceAround }, { "space-evenly", Ausrichtung::SpaceEvenly },
    { "left", Ausrichtung::Left }, { "right", Ausrichtung::Right }, { "auto", Ausrichtung::Auto },
};
const std::pair<std::string_view, Vertikal> kVertikal[] = {
    { "baseline", Vertikal::Baseline }, { "sub", Vertikal::Sub }, { "super", Vertikal::Super },
    { "top", Vertikal::Top }, { "text-top", Vertikal::TextTop }, { "middle", Vertikal::Middle },
    { "bottom", Vertikal::Bottom }, { "text-bottom", Vertikal::TextBottom },
};
const std::pair<std::string_view, TextAusrichtung> kTextAusrichtung[] = {
    { "start", TextAusrichtung::Start }, { "end", TextAusrichtung::End }, { "left", TextAusrichtung::Left },
    { "right", TextAusrichtung::Right }, { "center", TextAusrichtung::Center },
    { "justify", TextAusrichtung::Justify }, { "-webkit-left", TextAusrichtung::Left },
    { "-webkit-right", TextAusrichtung::Right }, { "-webkit-center", TextAusrichtung::Center },
};
const std::pair<std::string_view, Leerraum> kLeerraum[] = {
    { "normal", Leerraum::Normal }, { "pre", Leerraum::Pre }, { "nowrap", Leerraum::NoWrap },
    { "pre-wrap", Leerraum::PreWrap }, { "pre-line", Leerraum::PreLine }, { "break-spaces", Leerraum::BreakSpaces },
};
const std::pair<std::string_view, ListenArt> kListenArt[] = {
    { "disc", ListenArt::Disc }, { "circle", ListenArt::Circle }, { "square", ListenArt::Square },
    { "decimal", ListenArt::Decimal }, { "decimal-leading-zero", ListenArt::DecimalLeadingZero },
    { "lower-alpha", ListenArt::LowerAlpha }, { "lower-latin", ListenArt::LowerAlpha },
    { "upper-alpha", ListenArt::UpperAlpha }, { "upper-latin", ListenArt::UpperAlpha },
    { "lower-roman", ListenArt::LowerRoman }, { "upper-roman", ListenArt::UpperRoman },
    { "lower-greek", ListenArt::LowerGreek }, { "disclosure-open", ListenArt::DisclosureOpen },
    { "disclosure-closed", ListenArt::DisclosureClosed }, { "none", ListenArt::None },
};

bool rahmenBreite(std::string_view t, const Basis& b, int32_t& aus) {
    if (gleich(t, "thin"))   { aus = kQ; return true; }
    if (gleich(t, "medium")) { aus = 3 * kQ; return true; }
    if (gleich(t, "thick"))  { aus = 5 * kQ; return true; }
    Laenge l;
    if (!laenge(t, b, 0, l)) return false;
    aus = l.px;
    return true;
}

//  Eine Rasterlinie: auto, eine Zahl (nicht 0) mit oder ohne Namen, ein Name, span mit positiver Zahl und/oder Namen.
bool gridName(std::string_view w) {
    if (w.empty() || gleich(w, "auto") || gleich(w, "span")) return false;
    const auto zeichen = [](char x) { const auto u = uint8_t(x); return std::isalnum(u) || x == '_' || x == '-' || u >= 0x80; };
    return !std::isdigit(uint8_t(w.front())) && std::all_of(w.begin(), w.end(), zeichen);
}

bool gridLinie(std::string_view t) {
    std::string_view w[4];
    const int n = woerter(t, w, 4);
    if (n == 1 && gleich(w[0], "auto")) return true;
    if (n < 1 || n > 3) return false;
    bool span = false, name = false;
    int zahl = 0;
    for (int i = 0; i < n; ++i) {
        int v = 0;
        const auto [p, ec] = std::from_chars(w[i].data(), w[i].data() + w[i].size(), v);
        if (gleich(w[i], "span") && !span) span = true;
        else if (ec == std::errc() && p == w[i].data() + w[i].size() && v != 0 && !zahl) zahl = v;
        else if (gridName(w[i]) && !name) name = true;
        else return false;
    }
    return (zahl != 0 || name) && !(span && zahl < 0);
}

bool roh(std::string_view t, uint32_t& feld, RohPool* pool, std::string_view keiner, std::string_view keiner2 = {}) {
    if (gleich(t, keiner) || (!keiner2.empty() && gleich(t, keiner2))) { feld = 0; return true; }
    feld = pool ? pool->ablegen(t) : 1;
    return true;
}

bool istBild(std::string_view w) {
    return beginntMit(w, "url(") || beginntMit(w, "image-set(") || beginntMit(w, "-webkit-image-set(")
        || (w.find("gradient(") != std::string_view::npos && w.back() == ')');
}

}   // namespace

namespace stil {

int global(std::string_view w) {
    w = ohneRand(w);
    if (gleich(w, "inherit")) return 1;
    if (gleich(w, "initial")) return 2;
    if (gleich(w, "unset") || gleich(w, "revert") || gleich(w, "revert-layer")) return 3;
    return 0;
}

bool farbe(std::string_view t, uint32_t aktuell, uint32_t& aus) {
    t = ohneRand(t);
    if (t.empty()) return false;
    if (t[0] == '#') {
        const std::string_view h = t.substr(1);
        uint32_t v[8];
        for (size_t i = 0; i < h.size() && i < 8; ++i) {
            const char c = klein(h[i]);
            if (ziffer(c)) v[i] = uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v[i] = uint32_t(c - 'a' + 10);
            else return false;
        }
        switch (h.size()) {
        case 3: case 4: {
            const uint32_t a = h.size() == 4 ? v[3] * 17 : 255;
            aus = a << 24 | (v[0] * 17) << 16 | (v[1] * 17) << 8 | v[2] * 17;
            return true;
        }
        case 6: case 8: {
            const uint32_t a = h.size() == 8 ? v[6] << 4 | v[7] : 255;
            aus = a << 24 | (v[0] << 4 | v[1]) << 16 | (v[2] << 4 | v[3]) << 8 | (v[4] << 4 | v[5]);
            return true;
        }
        default:
            return false;
        }
    }
    const size_t k = t.find('(');
    if (k != std::string_view::npos) {
        if (t.back() != ')') return false;
        const std::string_view name = t.substr(0, k), arg = t.substr(k + 1, t.size() - k - 2);
        std::string_view teile[4];
        const int n = farbteile(arg, teile);
        if (n != 3 && n != 4) return false;
        double a = 1;
        if (n == 4 && !anteil(teile[3], 1, a)) return false;
        if (gleich(name, "rgb") || gleich(name, "rgba")) {
            double r, g, b;
            if (!anteil(teile[0], 255, r) || !anteil(teile[1], 255, g) || !anteil(teile[2], 255, b)) return false;
            aus = argb(r, g, b, std::clamp(a, 0.0, 1.0));
            return true;
        }
        if (gleich(name, "hsl") || gleich(name, "hsla")) {
            double h, sat, l;
            std::string_view rest;
            if (!leseZahl(teile[0], h, rest)) return false;
            if (gleich(rest, "turn")) h *= 360;
            else if (gleich(rest, "rad")) h *= 180 / 3.14159265358979323846;
            else if (gleich(rest, "grad")) h *= 0.9;
            else if (!rest.empty() && !gleich(rest, "deg")) return false;
            if (!anteil(teile[1], 1, sat) || !anteil(teile[2], 1, l)) return false;
            if (teile[1].back() != '%') sat /= 100;
            if (teile[2].back() != '%') l /= 100;
            sat = std::clamp(sat, 0.0, 1.0);
            l = std::clamp(l, 0.0, 1.0);
            h = std::fmod(std::fmod(h, 360) + 360, 360) / 360;
            const double q = l < 0.5 ? l * (1 + sat) : l + sat - l * sat, p = 2 * l - q;
            aus = argb(hsl(p, q, h + 1.0 / 3) * 255, hsl(p, q, h) * 255, hsl(p, q, h - 1.0 / 3) * 255,
                       std::clamp(a, 0.0, 1.0));
            return true;
        }
        return false;
    }
    if (gleich(t, "transparent")) { aus = 0; return true; }
    if (gleich(t, "currentcolor")) { aus = aktuell; return true; }
    if (t.size() > 20) return false;
    char n[21];
    for (size_t i = 0; i < t.size(); ++i) n[i] = klein(t[i]);
    const std::string_view name(n, t.size());
    const auto it = std::lower_bound(std::begin(kFarben), std::end(kFarben), name,
                                     [](const Farbname& f, std::string_view x) { return f.name < x; });
    if (it == std::end(kFarben) || it->name != name) return false;
    aus = 0xFF000000u | it->rgb;
    return true;
}

void uebernimm(uint16_t e, const Stil& v, Stil& n) {
    switch (e) {
    case EColor: n.farbe = v.farbe; break;
    case EFontFamily: n.schrift = v.schrift; break;
    case EFontSize: n.schriftGroesse = v.schriftGroesse; break;
    case EFontWeight: n.gewicht = v.gewicht; break;
    case EFontStyle: n.schriftStil = v.schriftStil; break;
    case ELineHeight: n.zeilenHoehe = v.zeilenHoehe; break;
    case ELetterSpacing: n.buchstabenAbstand = v.buchstabenAbstand; break;
    case ETextAlign: n.textAusrichtung = v.textAusrichtung; break;
    case EDirection: n.richtung = v.richtung; break;
    case EWhiteSpace: n.leerraum = v.leerraum; break;
    case ETextTransform: n.textTransform = v.textTransform; break;
    case EVisibility: n.sichtbarkeit = v.sichtbarkeit; break;
    case EListStyleType: n.listenArt = v.listenArt; break;
    case EListStylePosition: n.listenInnen = v.listenInnen; break;
    case EWordBreak: n.wortUmbruch = v.wortUmbruch; break;
    case EBorderCollapse: n.rahmenZusammen = v.rahmenZusammen; break;
    case EBorderSpacing: n.rahmenAbstand[0] = v.rahmenAbstand[0]; n.rahmenAbstand[1] = v.rahmenAbstand[1]; break;
    case ECaptionSide: n.titelUnten = v.titelUnten; break;
    case EFill: n.svgFuellung = v.svgFuellung; break;
    case EStroke: n.svgStrich = v.svgStrich; break;
    case EStrokeWidth: n.svgStrichBreite = v.svgStrichBreite; break;
    case EDisplay: n.anzeige = v.anzeige; break;
    case EPosition: n.position = v.position; break;
    case ETop: case ERight: case EBottom: case ELeft: n.versatz[e - ETop] = v.versatz[e - ETop]; break;
    case EZIndex: n.zIndex = v.zIndex; break;
    case EWidth: n.breite = v.breite; break;
    case EHeight: n.hoehe = v.hoehe; break;
    case EMinWidth: n.minBreite = v.minBreite; break;
    case EMinHeight: n.minHoehe = v.minHoehe; break;
    case EMaxWidth: n.maxBreite = v.maxBreite; break;
    case EMaxHeight: n.maxHoehe = v.maxHoehe; break;
    case EBoxSizing: n.randBox = v.randBox; break;
    case EMarginTop: case EMarginRight: case EMarginBottom: case EMarginLeft:
        n.aussen[e - EMarginTop] = v.aussen[e - EMarginTop]; break;
    case EPaddingTop: case EPaddingRight: case EPaddingBottom: case EPaddingLeft:
        n.innen[e - EPaddingTop] = v.innen[e - EPaddingTop]; break;
    case EBorderTopWidth: case EBorderRightWidth: case EBorderBottomWidth: case EBorderLeftWidth:
        n.rahmenBreite[e - EBorderTopWidth] = v.rahmenBreite[e - EBorderTopWidth]; break;
    case EBorderTopStyle: case EBorderRightStyle: case EBorderBottomStyle: case EBorderLeftStyle:
        n.rahmenStil[e - EBorderTopStyle] = v.rahmenStil[e - EBorderTopStyle]; break;
    case EBorderTopColor: case EBorderRightColor: case EBorderBottomColor: case EBorderLeftColor:
        n.rahmenFarbe[e - EBorderTopColor] = v.rahmenFarbe[e - EBorderTopColor]; break;
    case EBorderTopLeftRadius: case EBorderTopRightRadius: case EBorderBottomRightRadius: case EBorderBottomLeftRadius:
        n.radius[e - EBorderTopLeftRadius] = v.radius[e - EBorderTopLeftRadius]; break;
    case EBackgroundColor: n.hintergrund = v.hintergrund; break;
    case EBackgroundImage: n.hintergrundBild = v.hintergrundBild; break;
    case EBackgroundSize: n.hintergrundGroesse = v.hintergrundGroesse; break;
    case EBackgroundPosition: n.hintergrundLage = v.hintergrundLage; break;
    case EBackgroundRepeat: n.hintergrundWdh = v.hintergrundWdh; break;
    case EBackgroundAttachment: n.hintergrundFest = v.hintergrundFest; break;
    case EOpacity: n.deckkraft = v.deckkraft; break;
    case EOverflowX: n.ueberlaufX = v.ueberlaufX; break;
    case EOverflowY: n.ueberlaufY = v.ueberlaufY; break;
    case EFlexDirection: n.flexRichtung = v.flexRichtung; break;
    case EFlexWrap: n.flexUmbruch = v.flexUmbruch; break;
    case EFlexGrow: n.flexWachsen = v.flexWachsen; break;
    case EFlexShrink: n.flexSchrumpfen = v.flexSchrumpfen; break;
    case EFlexBasis: n.flexBasis = v.flexBasis; break;
    case EJustifyContent: n.inhaltAusrichtung = v.inhaltAusrichtung; break;
    case EAlignItems: n.elementAusrichtung = v.elementAusrichtung; break;
    case EAlignSelf: n.selbstAusrichtung = v.selbstAusrichtung; break;
    case EJustifyItems: n.justierung = v.justierung; break;
    case EJustifySelf: n.selbstJustierung = v.selbstJustierung; break;
    case ERowGap: n.zeilenAbstand = v.zeilenAbstand; break;
    case EColumnGap: n.spaltenAbstand = v.spaltenAbstand; break;
    case EGridTemplateColumns: n.gridSpalten = v.gridSpalten; break;
    case EGridColumnStart: n.gridSpalteA = v.gridSpalteA; break;
    case EGridColumnEnd: n.gridSpalteE = v.gridSpalteE; break;
    case EGridRowStart: n.gridZeileA = v.gridZeileA; break;
    case EGridRowEnd: n.gridZeileE = v.gridZeileE; break;
    case EGridTemplateRows: n.gridZeilen = v.gridZeilen; break;
    case EGridTemplateAreas: n.gridFlaechen = v.gridFlaechen; break;
    case EGridAutoRows: n.gridAutoZeilen = v.gridAutoZeilen; break;
    case EGridAutoColumns: n.gridAutoSpalten = v.gridAutoSpalten; break;
    case EGridAutoFlow: n.gridFluss = v.gridFluss; break;
    case EOrder: n.reihenfolge = v.reihenfolge; break;
    case EAlignContent: n.zeilenVerteilung = v.zeilenVerteilung; break;
    case ETableLayout: n.tabelleFest = v.tabelleFest; break;
    case EEmptyCells: n.leereZellen = v.leereZellen; break;
    case ETextDecorationLine: n.dekoration = v.dekoration; break;
    case EVerticalAlign: n.vertikal = v.vertikal; n.vertikalWert = v.vertikalWert; break;
    case EBoxShadow: n.schatten = v.schatten; break;
    case ETransform: n.transformation = v.transformation; break;
    case EContent: n.inhalt = v.inhalt; break;
    case EBreakInside: n.umbruchInnen = v.umbruchInnen; break;
    case EBreakBefore: n.umbruchVor = v.umbruchVor; break;
    case EBreakAfter: n.umbruchNach = v.umbruchNach; break;
    case EBackgroundClip: n.hintergrundText = v.hintergrundText; break;
    case EScrollMarginTop: n.rollRand = v.rollRand; break;
    default: break;
    }
}

bool setze(uint16_t e, std::string_view wert, Stil& s, const Umfeld& u, RohPool* pool) {
    const Stil& el = u.eltern ? *u.eltern : kStandard;
    const Medium standard;
    const Medium& m = u.medium ? *u.medium : standard;
    const Basis b{ s.schriftGroesse / double(kQ), u.wurzelSchrift / double(kQ), m.breite / 100.0, m.hoehe / 100.0, &s, pool,
                   m.messer, &m };
    const std::string_view t = ohneRand(wert);
    std::string_view w[16];
    Laenge l;
    switch (e) {
    case EColor: return farbe(t, el.farbe, s.farbe);
    case EFontFamily: {
        std::string f;
        if (!familien(t, f)) return false;
        s.schrift = pool ? pool->ablegen(f) : 0;
        return true;
    }
    case EFontSize: {
        const double vorher = el.schriftGroesse / double(kQ);
        static const std::pair<std::string_view, double> stufen[] = {
            { "xx-small", 9 }, { "x-small", 10 }, { "small", 13 }, { "medium", 16 }, { "large", 18 },
            { "x-large", 24 }, { "xx-large", 32 }, { "xxx-large", 48 }, { "smaller", -1 }, { "larger", -2 },
        };
        double px;
        if (wort(t, stufen, px)) {
            if (px == -1) px = vorher / 1.2;
            else if (px == -2) px = vorher * 1.2;
        } else {
            Basis fb = b;
            fb.em = vorher;
            fb.schrift = &el;
            if (!laenge(t, fb, LProzent, l)) return false;
            px = l.px / double(kQ) + l.prozent / double(kQ) / 100 * vorher;
        }
        s.schriftGroesse = q6(px);
        return true;
    }
    case EFontWeight: {
        if (gleich(t, "normal")) { s.gewicht = 400; return true; }
        if (gleich(t, "bold")) { s.gewicht = 700; return true; }
        const int g = el.gewicht;
        if (gleich(t, "bolder")) { s.gewicht = uint16_t(g < 350 ? 400 : g < 550 ? 700 : std::max(g, 900)); return true; }
        if (gleich(t, "lighter")) { s.gewicht = uint16_t(g < 100 ? g : g < 550 ? 100 : g < 750 ? 400 : 700); return true; }
        double v;
        if (!reineZahl(t, v) || v < 1 || v > 1000) return false;
        s.gewicht = uint16_t(std::lround(v));
        return true;
    }
    case EFontStyle: {
        const int n = woerter(t, w, 2);
        if (n < 1) return false;
        if (n == 1 && gleich(w[0], "normal")) s.schriftStil = SchriftStil::Normal;
        else if (n == 1 && gleich(w[0], "italic")) s.schriftStil = SchriftStil::Italic;
        else if (gleich(w[0], "oblique")) s.schriftStil = SchriftStil::Oblique;
        else return false;
        return true;
    }
    case ELineHeight:
        if (!laenge(t, b, LProzent | LNormal | LZahl, l)) return false;
        if (l.art == LArt::Wert) l = Laenge{ q6(l.px / double(kQ) + l.prozent / double(kQ) / 100 * b.em), 0, LArt::Wert };
        s.zeilenHoehe = l;
        return true;
    case ELetterSpacing:
        if (!laenge(t, b, LNegativ | LNormal, l)) return false;
        s.buchstabenAbstand = l.art == LArt::Normal ? 0 : l.px;
        return true;
    case ETextAlign:
        //  match-parent: start/end des Elternteils nach seiner Richtung als left/right.
        if (gleich(t, "match-parent")) {
            TextAusrichtung a = el.textAusrichtung;
            const bool rtl = el.richtung == Richtung::Rtl;
            if (a == TextAusrichtung::Start) a = rtl ? TextAusrichtung::Right : TextAusrichtung::Left;
            else if (a == TextAusrichtung::End) a = rtl ? TextAusrichtung::Left : TextAusrichtung::Right;
            s.textAusrichtung = a;
            return true;
        }
        //  -internal-center (th): mittig, ausser der Elternteil richtet selbst aus.
        if (gleich(t, "-internal-center")) {
            s.textAusrichtung = el.textAusrichtung == TextAusrichtung::Start ? TextAusrichtung::Center : el.textAusrichtung;
            return true;
        }
        return wort(t, kTextAusrichtung, s.textAusrichtung);
    case EDirection: {
        static const std::pair<std::string_view, Richtung> tab[] = { { "ltr", Richtung::Ltr }, { "rtl", Richtung::Rtl } };
        return wort(t, tab, s.richtung);
    }
    case EWhiteSpace: return wort(t, kLeerraum, s.leerraum);
    case ETextTransform: {
        static const std::pair<std::string_view, TextTransform> tab[] = {
            { "none", TextTransform::None }, { "uppercase", TextTransform::Uppercase },
            { "lowercase", TextTransform::Lowercase }, { "capitalize", TextTransform::Capitalize },
        };
        return wort(t, tab, s.textTransform);
    }
    case EVisibility: {
        static const std::pair<std::string_view, Sichtbarkeit> tab[] = {
            { "visible", Sichtbarkeit::Visible }, { "hidden", Sichtbarkeit::Hidden },
            { "collapse", Sichtbarkeit::Collapse },
        };
        return wort(t, tab, s.sichtbarkeit);
    }
    case EListStyleType: return wort(t, kListenArt, s.listenArt);
    case EListStylePosition: {
        static const std::pair<std::string_view, uint8_t> tab[] = { { "outside", 0 }, { "inside", 1 } };
        return wort(t, tab, s.listenInnen);
    }
    case EWordBreak: {
        static const std::pair<std::string_view, uint8_t> tab[] = {
            { "normal", uint8_t(WortUmbruch::Normal) }, { "break-all", uint8_t(WortUmbruch::BreakAll) },
            { "keep-all", uint8_t(WortUmbruch::KeepAll) }, { "break-word", uint8_t(WortUmbruch::BreakWord) },
        };
        return wort(t, tab, s.wortUmbruch);
    }
    case EBorderCollapse: {
        static const std::pair<std::string_view, uint8_t> tab[] = { { "separate", 0 }, { "collapse", 1 } };
        return wort(t, tab, s.rahmenZusammen);
    }
    case EBorderSpacing: {
        const int n = woerter(t, w, 3);
        Laenge senkrecht;
        if (n < 1 || n > 2 || !laenge(w[0], b, 0, l) || !laenge(w[n - 1], b, 0, senkrecht)) return false;
        s.rahmenAbstand[0] = l.px;
        s.rahmenAbstand[1] = senkrecht.px;
        return true;
    }
    case ECaptionSide: {
        static const std::pair<std::string_view, uint8_t> tab[] = { { "top", 0 }, { "bottom", 1 } };
        return wort(t, tab, s.titelUnten);
    }
    case EFill: case EStroke: {
        //  Als Text: currentColor gilt je SVG-Element, url(#...) zeigt in dessen Baum; beides loest erst schreibeSvg.
        uint32_t c;
        if (!gleich(t, "none") && !beginntMit(t, "url(") && !farbe(t, s.farbe, c)) return false;
        (e == EFill ? s.svgFuellung : s.svgStrich) = pool ? pool->ablegen(t) : 1;
        return true;
    }
    case EStrokeWidth: {
        double zahl = 0;
        const auto [ende, fehler] = std::from_chars(t.data(), t.data() + t.size(), zahl);
        if (!(fehler == std::errc() && ende == t.data() + t.size() && zahl >= 0) && !laenge(t, b, LProzent, l)) return false;
        s.svgStrichBreite = pool ? pool->ablegen(t) : 1;
        return true;
    }
    case EDisplay: return wort(t, kAnzeige, s.anzeige);
    case EPosition: return wort(t, kLage, s.position);
    case ETop: case ERight: case EBottom: case ELeft:
        if (!laenge(t, b, LProzent | LNegativ | LAuto, l)) return false;
        s.versatz[e - ETop] = l;
        return true;
    case EZIndex: {
        if (gleich(t, "auto")) { s.zIndex = kZAuto; return true; }
        double v;
        if (!reineZahl(t, v) || v != std::floor(v)) return false;
        s.zIndex = int32_t(std::clamp(v, -2147483647.0, 2147483647.0));
        return true;
    }
    case EWidth: case EHeight:
        if (!laenge(t, b, LProzent | LAuto | LInhalt, l)) return false;
        (e == EWidth ? s.breite : s.hoehe) = l;
        return true;
    case EMinWidth: case EMinHeight:
        if (!laenge(t, b, LProzent | LAuto | LInhalt, l)) return false;
        (e == EMinWidth ? s.minBreite : s.minHoehe) = l;
        return true;
    case EMaxWidth: case EMaxHeight:
        if (!laenge(t, b, LProzent | LKeine | LInhalt, l)) return false;
        (e == EMaxWidth ? s.maxBreite : s.maxHoehe) = l;
        return true;
    case EBoxSizing: {
        static const std::pair<std::string_view, uint8_t> tab[] = { { "content-box", 0 }, { "border-box", 1 } };
        return wort(t, tab, s.randBox);
    }
    case EMarginTop: case EMarginRight: case EMarginBottom: case EMarginLeft:
        if (!laenge(t, b, LProzent | LNegativ | LAuto, l)) return false;
        s.aussen[e - EMarginTop] = l;
        return true;
    case EPaddingTop: case EPaddingRight: case EPaddingBottom: case EPaddingLeft:
        if (!laenge(t, b, LProzent, l)) return false;
        s.innen[e - EPaddingTop] = l;
        return true;
    case EBorderTopWidth: case EBorderRightWidth: case EBorderBottomWidth: case EBorderLeftWidth:
        return rahmenBreite(t, b, s.rahmenBreite[e - EBorderTopWidth]);
    case EBorderTopStyle: case EBorderRightStyle: case EBorderBottomStyle: case EBorderLeftStyle:
        return wort(t, kRahmen, s.rahmenStil[e - EBorderTopStyle]);
    case EBorderTopColor: case EBorderRightColor: case EBorderBottomColor: case EBorderLeftColor:
        return farbe(t, s.farbe, s.rahmenFarbe[e - EBorderTopColor]);
    case EBorderTopLeftRadius: case EBorderTopRightRadius: case EBorderBottomRightRadius: case EBorderBottomLeftRadius: {
        const int n = woerter(t, w, 2);
        if (n < 1 || !laenge(w[0], b, LProzent, l)) return false;
        Laenge zweite;
        if (n == 2 && !laenge(w[1], b, LProzent, zweite)) return false;
        s.radius[e - EBorderTopLeftRadius] = l;
        return true;
    }
    case EBackgroundColor: return farbe(t, s.farbe, s.hintergrund);
    case EBackgroundImage: return roh(t, s.hintergrundBild, pool, "none");
    case EBackgroundSize: return roh(t, s.hintergrundGroesse, pool, "auto", "auto auto");
    case EBackgroundPosition: return roh(t, s.hintergrundLage, pool, "0% 0%", "0 0");
    case EBackgroundRepeat: return roh(t, s.hintergrundWdh, pool, "repeat", "repeat repeat");
    case EBackgroundAttachment: {
        //  local rollt hier wie scroll: Kaesten mit eigener Rollleiste rollen ihren Inhalt nicht.
        for (size_t a = 0; a <= t.size();) {
            size_t e = t.find(',', a);
            if (e == std::string_view::npos) e = t.size();
            const std::string_view x = ohneRand(t.substr(a, e - a));
            if (!gleich(x, "scroll") && !gleich(x, "fixed") && !gleich(x, "local")) return false;
            if (gleich(x, "fixed")) m.nutztHoehe();
            a = e + 1;
        }
        return roh(t, s.hintergrundFest, pool, "scroll");
    }
    case EOpacity: {
        double v;
        std::string_view rest;
        if (!leseZahl(t, v, rest)) return false;
        if (rest == "%") v /= 100;
        else if (!rest.empty()) return false;
        s.deckkraft = uint8_t(std::lround(std::clamp(v, 0.0, 1.0) * 255));
        return true;
    }
    case EOverflowX: return wort(t, kUeberlauf, s.ueberlaufX);
    case EOverflowY: return wort(t, kUeberlauf, s.ueberlaufY);
    case EFlexDirection: return wort(t, kFlexRichtung, s.flexRichtung);
    case EFlexWrap: return wort(t, kFlexUmbruch, s.flexUmbruch);
    case EFlexGrow: case EFlexShrink: {
        double v;
        if (!reineZahl(t, v) || v < 0) return false;
        (e == EFlexGrow ? s.flexWachsen : s.flexSchrumpfen) = qZahl(v);
        return true;
    }
    case EFlexBasis:
        if (gleich(t, "content")) { s.flexBasis = Laenge{ 0, 0, LArt::Auto }; return true; }
        if (!laenge(t, b, LProzent | LAuto | LInhalt, l)) return false;
        s.flexBasis = l;
        return true;
    case EJustifyContent: case EAlignItems: case EAlignSelf: case EJustifyItems: case EJustifySelf: {
        //  legacy gilt nur fuer Bloecke mit align-Attribut; in flex und grid wirkt es wie normal.
        if (e == EJustifyItems && gleich(t, "legacy")) { s.justierung = Ausrichtung::Normal; return true; }
        const int n = woerter(t, w, 2);
        if (n < 1) return false;
        //  `safe center`, `first baseline`: das letzte Wort traegt die Ausrichtung.
        if (n == 2 && !gleich(w[0], "safe") && !gleich(w[0], "unsafe") && !gleich(w[0], "first") && !gleich(w[0], "last"))
            return false;
        Ausrichtung a;
        if (!wort(w[n - 1], kAusrichtung, a)) return false;
        if (a == Ausrichtung::Auto && e != EAlignSelf && e != EJustifySelf) return false;
        (e == EJustifyContent ? s.inhaltAusrichtung : e == EAlignItems ? s.elementAusrichtung : e == EAlignSelf
            ? s.selbstAusrichtung : e == EJustifyItems ? s.justierung : s.selbstJustierung) = a;
        return true;
    }
    case ERowGap: case EColumnGap:
        if (!laenge(t, b, LProzent | LNormal, l)) return false;
        (e == ERowGap ? s.zeilenAbstand : s.spaltenAbstand) = l;
        return true;
    case EGridTemplateColumns: return roh(t, s.gridSpalten, pool, "none");
    case EGridColumnStart: case EGridColumnEnd: case EGridRowStart: case EGridRowEnd: {
        if (!gridLinie(t)) return false;
        uint32_t& f = e == EGridColumnStart ? s.gridSpalteA : e == EGridColumnEnd ? s.gridSpalteE
                    : e == EGridRowStart ? s.gridZeileA : s.gridZeileE;
        return roh(t, f, pool, "auto");
    }
    case EGridTemplateRows: return roh(t, s.gridZeilen, pool, "none");
    case EGridAutoRows: return roh(t, s.gridAutoZeilen, pool, "auto");
    case EGridAutoColumns: return roh(t, s.gridAutoSpalten, pool, "auto");
    case EGridTemplateAreas: {
        //  Je Zeile eine Zeichenkette; Namen, Punkte als leere Zellen. Die Form prueft erst das Layout.
        if (gleich(t, "none")) { s.gridFlaechen = 0; return true; }
        if (t.empty() || (t.front() != '"' && t.front() != '\'')) return false;
        return roh(t, s.gridFlaechen, pool, "none");
    }
    case EGridAutoFlow: {
        const int n = woerter(t, w, 3);
        if (n < 1 || n > 2) return false;
        uint8_t f = 0;
        bool richtung = false, dicht = false;
        for (int i = 0; i < n; ++i) {
            if (gleich(w[i], "row") && !richtung) richtung = true;
            else if (gleich(w[i], "column") && !richtung) { richtung = true; f |= 1; }
            else if (gleich(w[i], "dense") && !dicht) { dicht = true; f |= 2; }
            else return false;
        }
        s.gridFluss = f;
        return true;
    }
    case EOrder: {
        long long v = 0;
        const auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
        if (ec != std::errc() || p != t.data() + t.size()) return false;
        s.reihenfolge = int32_t(std::clamp(v, -2147483648LL, 2147483647LL));
        return true;
    }
    case EAlignContent: return wort(t, kAusrichtung, s.zeilenVerteilung);
    case ETableLayout: {
        static const std::pair<std::string_view, uint8_t> tab[] = { { "auto", 0 }, { "fixed", 1 } };
        return wort(t, tab, s.tabelleFest);
    }
    case EEmptyCells: {
        static const std::pair<std::string_view, uint8_t> tab[] = { { "show", 0 }, { "hide", 1 } };
        return wort(t, tab, s.leereZellen);
    }
    case EGridColumn: case EGridRow: case EGridArea: {
        //  Fehlende Teile: ein Name wiederholt sich als Ende (bzw. Spalte), sonst auto.
        std::string_view teil[5];
        const int n = woerter(t, teil, 5, "/");
        if (n < 1 || n > (e == EGridArea ? 4 : 2)) return false;
        for (int i = 0; i < n; ++i) {
            teil[i] = ohneRand(teil[i]);
            if (!gridLinie(teil[i])) return false;
        }
        //  Der Reihe nach ergaenzt: das Spaltenende erbt vom (vielleicht selbst ergaenzten) Spaltenanfang.
        for (int i = n; i < 4; ++i) {
            const int von = i == 3 ? 1 : 0;
            teil[i] = gridName(teil[von]) ? teil[von] : std::string_view("auto");
        }
        Stil neu = s;
        const auto lege = [&](uint32_t& f, std::string_view v) { roh(v, f, pool, "auto"); };
        if (e == EGridColumn) { lege(neu.gridSpalteA, teil[0]); lege(neu.gridSpalteE, teil[1]); }
        else if (e == EGridRow) { lege(neu.gridZeileA, teil[0]); lege(neu.gridZeileE, teil[1]); }
        else {
            lege(neu.gridZeileA, teil[0]);
            lege(neu.gridSpalteA, teil[1]);
            lege(neu.gridZeileE, teil[2]);
            lege(neu.gridSpalteE, teil[3]);
        }
        s = neu;
        return true;
    }
    case EBoxShadow: return roh(t, s.schatten, pool, "none");
    case ETransform: return roh(t, s.transformation, pool, "none");
    case EContent: return roh(t, s.inhalt, pool, "normal", "none");
    case EBreakInside: {
        static const std::pair<std::string_view, uint8_t> tab[] = {
            { "auto", 0 }, { "avoid", 1 }, { "avoid-page", 1 }, { "avoid-column", 0 }, { "avoid-region", 0 },
        };
        return wort(t, tab, s.umbruchInnen);
    }
    case EBackgroundClip: {
        //  padding-box und content-box malen wie border-box.
        static const std::pair<std::string_view, uint8_t> tab[] = {
            { "border-box", 0 }, { "padding-box", 0 }, { "content-box", 0 }, { "text", 1 },
        };
        return wort(t, tab, s.hintergrundText);
    }
    case EScrollMarginTop:
        if (!laenge(t, b, LNegativ, l)) return false;
        s.rollRand = l.px;
        return true;
    case EScrollMargin: {
        //  Nur die obere Seite wird gebraucht; die uebrigen muessen trotzdem gueltig sein.
        const int n = woerter(t, w, 4);
        if (n < 1) return false;
        Laenge r[4];
        for (int i = 0; i < n; ++i)
            if (!laenge(w[i], b, LNegativ, r[i])) return false;
        s.rollRand = r[0].px;
        return true;
    }
    case EBreakBefore: case EBreakAfter: {
        //  Spalten und Regionen gibt es hier nicht: sie wirken wie auto.
        static const std::pair<std::string_view, uint8_t> tab[] = {
            { "auto", 0 }, { "avoid", 1 }, { "avoid-page", 1 }, { "page", 2 }, { "always", 2 }, { "left", 2 },
            { "right", 2 }, { "recto", 2 }, { "verso", 2 }, { "column", 0 }, { "avoid-column", 0 }, { "region", 0 },
            { "avoid-region", 0 },
        };
        return wort(t, tab, e == EBreakBefore ? s.umbruchVor : s.umbruchNach);
    }
    case ETextDecorationLine: case ETextDecoration: {
        const int n = woerter(t, w, 8);
        if (n < 1) return false;
        uint8_t linie = 0;
        bool keine = false;
        for (int i = 0; i < n; ++i) {
            uint32_t c;
            Laenge dicke;
            if (gleich(w[i], "none")) keine = true;
            else if (gleich(w[i], "underline")) linie |= DekoUnter;
            else if (gleich(w[i], "overline")) linie |= DekoUeber;
            else if (gleich(w[i], "line-through")) linie |= DekoDurch;
            else if (gleich(w[i], "blink")) {}
            else if (e == ETextDecoration && (gleich(w[i], "solid") || gleich(w[i], "double") || gleich(w[i], "dotted")
                                              || gleich(w[i], "dashed") || gleich(w[i], "wavy") || gleich(w[i], "from-font")
                                              || farbe(w[i], s.farbe, c) || laenge(w[i], b, LProzent | LAuto, dicke))) {}
            else return false;
        }
        if (keine && linie) return false;
        s.dekoration = linie;
        return true;
    }
    case EVerticalAlign:
        if (wort(t, kVertikal, s.vertikal)) { s.vertikalWert = Laenge{}; return true; }
        if (!laenge(t, b, LProzent | LNegativ, l)) return false;
        s.vertikal = Vertikal::Wert;
        s.vertikalWert = l;
        return true;

    case EMargin: case EPadding: case EInset: case EBorderWidth: case EBorderStyle: case EBorderColor: {
        const int n = woerter(t, w, 4);
        if (n < 1) return false;
        Stil neu = s;
        for (int i = 0; i < 4; ++i) {
            const std::string_view x = w[seite(n, i)];
            bool ok;
            switch (e) {
            case EMargin:      ok = laenge(x, b, LProzent | LNegativ | LAuto, neu.aussen[i]); break;
            case EPadding:     ok = laenge(x, b, LProzent, neu.innen[i]); break;
            case EInset:       ok = laenge(x, b, LProzent | LNegativ | LAuto, neu.versatz[i]); break;
            case EBorderWidth: ok = rahmenBreite(x, b, neu.rahmenBreite[i]); break;
            case EBorderStyle: ok = wort(x, kRahmen, neu.rahmenStil[i]); break;
            default:           ok = farbe(x, s.farbe, neu.rahmenFarbe[i]); break;
            }
            if (!ok) return false;
        }
        s = neu;
        return true;
    }
    case EBorderRadius: {
        const size_t strich = bisAussen(t, 0, t.size(), "/");
        const int n = woerter(t.substr(0, strich), w, 4);
        if (n < 1) return false;
        Laenge r[4];
        for (int i = 0; i < 4; ++i)
            if (!laenge(w[seite(n, i)], b, LProzent, r[i])) return false;
        if (strich < t.size()) {
            std::string_view w2[4];
            const int m = woerter(t.substr(strich + 1), w2, 4);
            if (m < 1) return false;
            for (int i = 0; i < m; ++i)
                if (!laenge(w2[i], b, LProzent, l)) return false;
        }
        std::copy(r, r + 4, s.radius);
        return true;
    }
    case EBorder: case EBorderTop: case EBorderRight: case EBorderBottom: case EBorderLeft: {
        const int n = woerter(t, w, 3);
        if (n < 1) return false;
        int32_t breite = 3 * kQ;
        RahmenStil stilArt = RahmenStil::None;
        uint32_t c = s.farbe;
        bool hatBreite = false, hatStil = false, hatFarbe = false;
        for (int i = 0; i < n; ++i) {
            if (!hatStil && wort(w[i], kRahmen, stilArt)) hatStil = true;
            else if (!hatBreite && rahmenBreite(w[i], b, breite)) hatBreite = true;
            else if (!hatFarbe && farbe(w[i], s.farbe, c)) hatFarbe = true;
            else return false;
        }
        const int von = e == EBorder ? 0 : e - EBorderTop, bis = e == EBorder ? 4 : von + 1;
        for (int i = von; i < bis; ++i) {
            s.rahmenBreite[i] = breite;
            s.rahmenStil[i] = stilArt;
            s.rahmenFarbe[i] = c;
        }
        return true;
    }
    case EBackground: {
        std::string_view schichten[16];
        const int n = woerter(t, schichten, 16, ",");
        if (n < 1) return false;
        uint32_t c = 0;
        std::string bild;
        bool mitBild = false;
        for (int i = 0; i < n; ++i) {
            std::string_view x[32];
            const int m = woerter(schichten[i], x, 32);
            if (m < 1) return false;
            if (i) bild += ", ";
            bool erstes = true;
            for (int k = 0; k < m; ++k) {
                uint32_t f;
                if (!istBild(x[k]) && farbe(x[k], s.farbe, f)) {
                    if (i != n - 1) return false;
                    c = f;
                    continue;
                }
                if (istBild(x[k])) mitBild = true;
                if (!erstes) bild += ' ';
                bild.append(x[k]);
                erstes = false;
            }
            if (erstes) bild += "none";
        }
        s.hintergrund = c;
        s.hintergrundBild = mitBild ? (pool ? pool->ablegen(bild) : 1) : 0;
        s.hintergrundGroesse = s.hintergrundLage = s.hintergrundWdh = s.hintergrundFest = 0;
        s.hintergrundText = 0;
        return true;
    }
    case EGap: {
        const int n = woerter(t, w, 2);
        Laenge spalte;
        if (n < 1 || !laenge(w[0], b, LProzent | LNormal, l) || !laenge(w[n - 1], b, LProzent | LNormal, spalte))
            return false;
        s.zeilenAbstand = l;
        s.spaltenAbstand = spalte;
        return true;
    }
    case EPlaceItems: {
        const int n = woerter(t, w, 2);
        if (n < 1) return false;
        Ausrichtung a, j;
        if (!wort(w[0], kAusrichtung, a) || a == Ausrichtung::Auto) return false;
        if (n == 1) j = a;
        else if (!wort(w[1], kAusrichtung, j) || j == Ausrichtung::Auto) return false;
        s.elementAusrichtung = a;
        s.justierung = j;
        return true;
    }
    case EFlex: {
        int32_t wachsen = 0, schrumpfen = kQZahl;
        Laenge basis{ 0, 0, LArt::Auto };
        if (gleich(t, "none")) schrumpfen = 0;
        else if (gleich(t, "auto")) wachsen = kQZahl;
        else {
            //  [ <grow> <shrink>? || <basis> ]: die beiden Zahlen stehen beieinander, die Basis davor oder danach.
            const int n = woerter(t, w, 3);
            if (n < 1) return false;
            auto istBasis = [&](std::string_view x) {
                if (gleich(x, "content")) { basis = Laenge{ 0, 0, LArt::Auto }; return true; }
                return laenge(x, b, LProzent | LAuto | LInhalt, basis);
            };
            double v;
            int i = 0;
            bool hatBasis = false, hatWachsen = false;
            if (!reineZahl(w[0], v)) {
                if (!istBasis(w[0])) return false;
                hatBasis = true;
                i = 1;
            }
            if (i < n && reineZahl(w[i], v)) {
                if (v < 0) return false;
                wachsen = qZahl(v);
                hatWachsen = true;
                if (++i < n && reineZahl(w[i], v)) {
                    if (v < 0) return false;
                    schrumpfen = qZahl(v);
                    ++i;
                }
            }
            if (i < n && !hatBasis) {
                if (!istBasis(w[i])) return false;
                hatBasis = true;
                ++i;
            }
            if (i != n) return false;
            if (!hatWachsen) wachsen = kQZahl;
            else if (!hatBasis) basis = Laenge{ 0, 0, LArt::Wert, 1 };
        }
        s.flexWachsen = wachsen;
        s.flexSchrumpfen = schrumpfen;
        s.flexBasis = basis;
        return true;
    }
    case EFlexFlow: {
        const int n = woerter(t, w, 2);
        if (n < 1) return false;
        FlexRichtung r = FlexRichtung::Row;
        FlexUmbruch u_ = FlexUmbruch::NoWrap;
        bool hatR = false, hatU = false;
        for (int i = 0; i < n; ++i) {
            if (!hatR && wort(w[i], kFlexRichtung, r)) hatR = true;
            else if (!hatU && wort(w[i], kFlexUmbruch, u_)) hatU = true;
            else return false;
        }
        s.flexRichtung = r;
        s.flexUmbruch = u_;
        return true;
    }
    case EListStyle: {
        const int n = woerter(t, w, 3);
        if (n < 1) return false;
        ListenArt art = ListenArt::Disc;
        uint8_t innen = 0;
        bool hatArt = false, hatLage = false, hatBild = false;
        for (int i = 0; i < n; ++i) {
            if (!hatLage && (gleich(w[i], "inside") || gleich(w[i], "outside"))) { innen = gleich(w[i], "inside"); hatLage = true; }
            else if (!hatBild && beginntMit(w[i], "url(")) hatBild = true;
            else if (!hatArt && wort(w[i], kListenArt, art)) hatArt = true;
            else if (!hatBild && gleich(w[i], "none")) hatBild = true;
            else return false;
        }
        s.listenArt = art;
        s.listenInnen = innen;
        return true;
    }
    case EOverflow: {
        const int n = woerter(t, w, 2);
        Ueberlauf x, y;
        if (n < 1 || !wort(w[0], kUeberlauf, x) || !wort(w[n - 1], kUeberlauf, y)) return false;
        s.ueberlaufX = x;
        s.ueberlaufY = y;
        return true;
    }
    case EFont: {
        //  [style || variant || weight || stretch] size[/line-height] family; Systemschriften (caption ...) scheitern an der
        //  Groesse. Die Zeilenhoehe kommt nach der Groesse, damit em und % die neue Schrift meinen.
        Stil f = s;
        f.schriftStil = SchriftStil::Normal;
        f.gewicht = 400;
        f.zeilenHoehe = Laenge{ 0, 0, LArt::Normal };
        const auto endet = [](std::string_view x, std::string_view e) {
            return x.size() >= e.size() && gleich(x.substr(x.size() - e.size()), e);
        };
        size_t i = 0;
        for (int vorne = 0;; ++vorne) {
            while (i < t.size() && leer(t[i])) ++i;
            size_t j = i;
            while (j < t.size() && !leer(t[j]) && t[j] != '/') ++j;
            const std::string_view x = t.substr(i, j - i);
            if (x.empty() || vorne > 4) return false;
            i = j;
            if (gleich(x, "normal") || gleich(x, "small-caps") || endet(x, "condensed") || endet(x, "expanded")) continue;
            if (setze(EFontStyle, x, f, u, pool) || setze(EFontWeight, x, f, u, pool)) continue;
            if (!setze(EFontSize, x, f, u, pool)) return false;
            break;
        }
        while (i < t.size() && leer(t[i])) ++i;
        if (i < t.size() && t[i] == '/') {
            ++i;
            while (i < t.size() && leer(t[i])) ++i;
            size_t j = i;
            while (j < t.size() && !leer(t[j])) ++j;
            if (!setze(ELineHeight, t.substr(i, j - i), f, u, pool)) return false;
            i = j;
        }
        if (!setze(EFontFamily, t.substr(i), f, u, pool)) return false;
        s = f;
        return true;
    }
    default:
        return false;
    }
}

}   // namespace stil

namespace {

//  Vorgaben des Browsers nach dem HTML-Standard; Formularfelder und Tabellen so, wie Chromium sie berechnet.
constexpr std::string_view kVorgaben = R"css(
html,address,blockquote,body,center,dialog,div,figure,figcaption,footer,form,header,hr,legend,listing,main,p,
plaintext,pre,search,xmp,article,aside,h1,h2,h3,h4,h5,h6,hgroup,nav,section,dir,dd,dl,dt,menu,ol,ul,details,summary,
fieldset,optgroup{display:block}
head,link,meta,script,style,title,base,template,area,datalist,param,rp,noembed,noframes,[hidden],
input[type=hidden i],dialog:not([open]){display:none}
li{display:list-item;text-align:match-parent}
table{display:table;border-collapse:separate;border-spacing:2px;box-sizing:border-box;border-color:gray}
thead,tbody,tfoot,tr{border-color:inherit}
caption{display:table-caption;text-align:center}
colgroup{display:table-column-group}
col{display:table-column}
thead{display:table-header-group;vertical-align:middle}
tbody{display:table-row-group;vertical-align:middle}
tfoot{display:table-footer-group;vertical-align:middle}
tr{display:table-row;vertical-align:inherit}
td,th{display:table-cell;padding:1px;vertical-align:inherit}
th{font-weight:bold;text-align:-internal-center}
ruby{display:ruby}
rt{display:ruby-text;font-size:50%}
body{margin:8px}
p,dl,menu,ol,ul,pre,listing,xmp,plaintext{margin-top:1em;margin-bottom:1em}
blockquote,figure{margin:1em 40px}
dd{margin-left:40px}
:is(dir,menu,ol,ul) :is(dir,menu,ol,ul){margin-top:0;margin-bottom:0}
dir,menu,ol,ul{padding-left:40px}
dir,menu,ul{list-style-type:disc}
ol{list-style-type:decimal}
:is(dir,menu,ol,ul) :is(dir,menu,ul){list-style-type:circle}
:is(dir,menu,ol,ul) :is(dir,menu,ol,ul) :is(dir,menu,ul){list-style-type:square}
h1{font-size:2em;margin-top:.67em;margin-bottom:.67em}
h2{font-size:1.5em;margin-top:.83em;margin-bottom:.83em}
h3{font-size:1.17em;margin-top:1em;margin-bottom:1em}
h4{margin-top:1.33em;margin-bottom:1.33em}
h5{font-size:.83em;margin-top:1.67em;margin-bottom:1.67em}
h6{font-size:.67em;margin-top:2.33em;margin-bottom:2.33em}
h1,h2,h3,h4,h5,h6{font-weight:bold}
b,strong{font-weight:bolder}
i,em,cite,var,dfn,address{font-style:italic}
pre,listing,xmp,plaintext{white-space:pre}
pre,listing,xmp,plaintext,code,kbd,samp,tt{font-family:monospace}
textarea{white-space:pre-wrap}
small{font-size:smaller}
big{font-size:larger}
sub{vertical-align:sub;font-size:smaller}
sup{vertical-align:super;font-size:smaller}
u,ins{text-decoration:underline}
s,strike,del{text-decoration:line-through}
a:link{color:#0000ee;text-decoration:underline}
mark{background-color:yellow;color:black}
center{text-align:center}
nobr{white-space:nowrap}
hr{color:gray;border-style:inset;border-width:1px;margin:.5em auto;overflow:hidden}
button,input,select,textarea{display:inline-block;font-size:13.333px;font-weight:normal;font-style:normal;
line-height:normal;color:black;letter-spacing:normal;text-transform:none;text-align:start;font-family:Arial}
input{background-color:white;border:2px inset #767676;padding:1px 2px}
input:not([type=image i],[type=range i],[type=checkbox i],[type=radio i]){overflow:clip!important}
input[type=search i]{box-sizing:border-box}
input[type=range i]{color:#9d968e;border:none;padding:0;margin:2px}
input[type=radio i],input[type=checkbox i]{background-color:initial;border:none;padding:0;box-sizing:border-box;
margin:3px 3px 3px 4px}
input[type=radio i]{margin:3px 3px 0 5px}
input[type=image i],input[type=file i]{background-color:initial;border:none;padding:0}
input[type=file i]{color:inherit}
button,input[type=button i],input[type=submit i],input[type=reset i]{background-color:#efefef;border:2px outset black;
padding:1px 6px;text-align:center;box-sizing:border-box}
input[type=button i],input[type=submit i],input[type=reset i],select{white-space:pre}
input[type=color i]{background-color:#efefef;border:1px solid black;box-sizing:border-box}
select{background-color:#efefef;border:1px solid #767676;padding:0;box-sizing:border-box}
textarea{background-color:white;border:1px solid #767676;padding:2px;font-family:monospace;overflow:auto}
svg:not(:root){overflow:hidden}
[dir=rtl i]{direction:rtl}
[dir=ltr i]{direction:ltr}
details>summary:first-of-type{display:list-item;list-style-type:disclosure-closed;list-style-position:inside}
details[open]>summary:first-of-type{list-style-type:disclosure-open}
)css";

const Stilblatt& vorgaben() {
    static const Stilblatt b = [] {
        Stilblatt v;
        v.zerlege(kVorgaben);
        return v;
    }();
    return b;
}

//  NieMehr: kein Vorfahr erfuellt den Rest - ein weiter oben gewaehlter Startpunkt hat nur weniger Vorfahren.
enum class Treffer : uint8_t { Ja, Nein, NieMehr };

//  Ein Stilblatt an die Namen des Dokuments gebunden: Atome je Element, Medien ausgewertet.
struct Bindung {
    const Stilblatt* b = nullptr;
    std::vector<uint32_t> atomVonName, nameVonAtom;
    uint32_t atomOpen = kKein;
    std::vector<uint32_t> idAtom, klAnfang, klassen;
    std::vector<char> medien;

    void binde(const Stilblatt& blatt, const Dokument& d, const Medium& m) {
        b = &blatt;
        atomVonName.assign(d.namenAnzahl(), kKein);
        nameVonAtom.assign(blatt.atomAnzahl(), kKein);
        for (uint32_t a = 0; a < blatt.atomAnzahl(); ++a) {
            const uint32_t n = d.nameSuchen(blatt.atomVon(a));
            nameVonAtom[a] = n;
            if (n != kKein) atomVonName[n] = a;
            if (blatt.atomVon(a) == "open") atomOpen = a;
        }
        medien.resize(blatt.medien.size());
        for (uint32_t i = 0; i < medien.size(); ++i) medien[i] = blatt.gilt(i, m);
        const size_t n = d.knoten.size();
        idAtom.assign(n, kKein);
        klAnfang.assign(n + 1, 0);
        klassen.clear();
        if (!blatt.mitKlassen) return;
        const uint32_t nId = d.nameSuchen("id"), nKlasse = d.nameSuchen("class");
        for (uint32_t i = 0; i < n; ++i) {
            klAnfang[i] = uint32_t(klassen.size());
            if (d.knoten[i].art != Art::Element) continue;
            if (nId != kKein) {
                bool da = false;
                const std::string_view id = d.attribut(i, nId, &da);
                if (da && !id.empty()) idAtom[i] = blatt.atomSuchen(id);
            }
            if (nKlasse == kKein) continue;
            const std::string_view k = d.attribut(i, nKlasse);
            size_t p = 0;
            while (p < k.size()) {
                while (p < k.size() && (leer(k[p]) || k[p] == '\r' || k[p] == '\f')) ++p;
                size_t e = p;
                while (e < k.size() && !(leer(k[e]) || k[e] == '\r' || k[e] == '\f')) ++e;
                if (e > p) {
                    //  Klassen, die kein Selektor nennt, fallen gleich weg; verglichen wird nur noch per Atom.
                    const uint32_t a = blatt.atomSuchen(k.substr(p, e - p));
                    if (a != kKein) klassen.push_back(a);
                }
                p = e;
            }
        }
        klAnfang[n] = uint32_t(klassen.size());
    }
    bool hatKlasse(uint32_t el, uint32_t atom) const {
        if (klassen.empty()) return false;
        for (uint32_t i = klAnfang[el]; i < klAnfang[el + 1]; ++i)
            if (klassen[i] == atom) return true;
        return false;
    }
};

//  Baumbeziehungen nur ueber Elemente: Eltern, vorheriges Geschwister, Platz von vorn und hinten.
struct Baum {
    std::vector<uint32_t> eltern, vorher, nr, nrHinten;
    void baue(const Dokument& d) {
        const size_t n = d.knoten.size();
        eltern.assign(n, kKein);
        vorher.assign(n, kKein);
        nr.assign(n, 0);
        nrHinten.assign(n, 0);
        for (uint32_t i = 0; i < n; ++i) {
            const Knoten& k = d.knoten[i];
            uint32_t letztes = kKein, zahl = 0;
            for (uint32_t c = k.erstesKind; c != kKein; c = d.knoten[c].naechstes) {
                if (d.knoten[c].art != Art::Element) continue;
                if (k.art == Art::Element) eltern[c] = i;
                vorher[c] = letztes;
                nr[c] = ++zahl;
                letztes = c;
            }
            for (uint32_t c = k.erstesKind; c != kKein; c = d.knoten[c].naechstes)
                if (d.knoten[c].art == Art::Element) nrHinten[c] = zahl - nr[c] + 1;
        }
    }
};

bool nth(int32_t a, int32_t b, int64_t pos) {
    if (a == 0) return pos == b;
    const int64_t k = pos - b;
    return k % a == 0 && k / a >= 0;
}

class Pruefer {
public:
    Pruefer(const Dokument& d, const Baum& baum, const Bindung& bd, std::span<const uint8_t> zustand, uint32_t namen[8])
        : m_d(d), m_baum(baum), m_bd(bd), m_s(*bd.b), m_zustand(zustand) { std::copy(namen, namen + 8, m_n); }

    bool passt(uint32_t sel, uint32_t el) {
        m_schritte = 0;
        const Selektor& s = m_s.selektoren[sel];
        return ab(s, s.anfang + s.anzahl, el) == Treffer::Ja;
    }

private:
    enum { ZChecked, ZType, ZSelected, ZDisabled, ZHref, ZSelect };

    Treffer ab(const Selektor& s, uint32_t ende, uint32_t el) {
        //  Begrenzt den Aufwand verschachtelter Kombinatoren auf boesartigen Eingaben.
        if (++m_schritte > 20000) return Treffer::NieMehr;
        uint32_t start = ende;
        while (start > s.anfang && m_s.teile[start - 1].art != Teilart::Kombinator) --start;
        for (uint32_t i = start; i < ende; ++i)
            if (!einfach(m_s.teile[i], el)) return Treffer::Nein;
        if (start == s.anfang) return Treffer::Ja;
        switch (Kombinator(m_s.teile[start - 1].op)) {
        case Kombinator::Kind: {
            const uint32_t p = m_baum.eltern[el];
            if (p == kKein) return Treffer::NieMehr;
            return ab(s, start - 1, p);
        }
        case Kombinator::Nachfahr:
            for (uint32_t p = m_baum.eltern[el]; p != kKein; p = m_baum.eltern[p]) {
                const Treffer t = ab(s, start - 1, p);
                if (t != Treffer::Nein) return t;
            }
            return Treffer::NieMehr;
        //  Ueber Geschwister hinweg nur Nein: die Abkuerzung ist nur fuer Vorfahrenketten belegt.
        case Kombinator::Nachbar: {
            const uint32_t v = m_baum.vorher[el];
            if (v == kKein) return Treffer::Nein;
            const Treffer t = ab(s, start - 1, v);
            return t == Treffer::Ja ? t : Treffer::Nein;
        }
        case Kombinator::Geschwister:
            for (uint32_t v = m_baum.vorher[el]; v != kKein; v = m_baum.vorher[v]) {
                const Treffer t = ab(s, start - 1, v);
                if (t == Treffer::Ja) return t;
                if (m_schritte > 20000) return Treffer::NieMehr;
            }
            return Treffer::Nein;
        }
        return Treffer::Nein;
    }

    bool eines(const Teil& t, uint32_t el) {
        for (int32_t i = 0; i < t.b; ++i) {
            const Selektor& s = m_s.selektoren[m_s.argumente[size_t(t.a + i)]];
            if (ab(s, s.anfang + s.anzahl, el) == Treffer::Ja) return true;
        }
        return false;
    }

    bool formular(uint32_t name) const {
        return name == NButton || name == NInput || name == m_n[ZSelect] || name == NTextarea || name == NOptgroup
            || name == NOption || name == NFieldset;
    }

    bool einfach(const Teil& t, uint32_t el) {
        const Knoten& k = m_d.knoten[el];
        switch (t.art) {
        case Teilart::Typ: return t.atom == kKein || (k.name < m_bd.atomVonName.size() && m_bd.atomVonName[k.name] == t.atom);
        case Teilart::Klasse: return m_bd.hatKlasse(el, t.atom);
        case Teilart::Id: return m_bd.idAtom[el] == t.atom;
        case Teilart::Attribut: {
            const uint32_t name = m_bd.nameVonAtom[t.atom];
            bool da = false;
            std::string_view w;
            //  [open] folgt dem Zustand: aufgeklappt per Klick, ohne das Dokument anzufassen. Vor der Namenspruefung,
            //  denn ohne ein details mit open in der Quelle kennt das Dokument den Namen nicht.
            if (t.atom == m_bd.atomOpen && k.name == NDetails && k.raum == Raum::Html && el < m_zustand.size())
                da = m_zustand[el] & kOffen;
            else if (name != kKein)
                w = m_d.attribut(el, name, &da);
            if (!da) return false;
            const std::string_view soll = std::string_view(m_s.zeichen).substr(size_t(t.a), size_t(t.b));
            auto eq = [&](std::string_view x, std::string_view y) { return t.ohneFall ? gleich(x, y) : x == y; };
            switch (AttrOp(t.op)) {
            case AttrOp::Da: return true;
            case AttrOp::Gleich: return eq(w, soll);
            case AttrOp::Strich: return eq(w, soll) || (w.size() > soll.size() && w[soll.size()] == '-' && eq(w.substr(0, soll.size()), soll));
            case AttrOp::Anfang: return !soll.empty() && w.size() >= soll.size() && eq(w.substr(0, soll.size()), soll);
            case AttrOp::Ende: return !soll.empty() && w.size() >= soll.size() && eq(w.substr(w.size() - soll.size()), soll);
            case AttrOp::Enthaelt: {
                if (soll.empty() || w.size() < soll.size()) return false;
                for (size_t i = 0; i + soll.size() <= w.size(); ++i)
                    if (eq(w.substr(i, soll.size()), soll)) return true;
                return false;
            }
            case AttrOp::Wort: {
                if (soll.empty() || soll.find_first_of(" \t\n") != std::string_view::npos) return false;
                size_t p = 0;
                while (p < w.size()) {
                    while (p < w.size() && leer(w[p])) ++p;
                    size_t e = p;
                    while (e < w.size() && !leer(w[e])) ++e;
                    if (e > p && eq(w.substr(p, e - p), soll)) return true;
                    p = e;
                }
                return false;
            }
            }
            return false;
        }
        case Teilart::Pseudo:
            switch (Pseudo(t.op)) {
            case Pseudo::Root: return el == m_d.html;
            case Pseudo::FirstChild: return m_baum.nr[el] == 1;
            case Pseudo::LastChild: return m_baum.nrHinten[el] == 1;
            case Pseudo::OnlyChild: return m_baum.nr[el] == 1 && m_baum.nrHinten[el] == 1;
            case Pseudo::NthChild: return nth(t.a, t.b, m_baum.nr[el]);
            case Pseudo::NthLastChild: return nth(t.a, t.b, m_baum.nrHinten[el]);
            case Pseudo::FirstOfType: return typNr(el, false) == 1;
            case Pseudo::LastOfType: return typNr(el, true) == 1;
            case Pseudo::OnlyOfType: return typNr(el, false) == 1 && typNr(el, true) == 1;
            case Pseudo::NthOfType: return nth(t.a, t.b, typNr(el, false));
            case Pseudo::NthLastOfType: return nth(t.a, t.b, typNr(el, true));
            case Pseudo::Not: return !eines(t, el);
            case Pseudo::Is: case Pseudo::Where: return eines(t, el);
            case Pseudo::Checked: {
                if (el < m_zustand.size()) return m_zustand[el] & kAngehakt;
                bool da = false;
                if (k.name == NOption) { m_d.attribut(el, m_n[ZSelected], &da); return da; }
                if (k.name != NInput) return false;
                const std::string_view typ = m_d.attribut(el, m_n[ZType]);
                if (!gleich(typ, "checkbox") && !gleich(typ, "radio")) return false;
                m_d.attribut(el, m_n[ZChecked], &da);
                return da;
            }
            case Pseudo::Disabled: case Pseudo::Enabled: {
                if (!formular(k.name)) return false;
                bool da = false;
                m_d.attribut(el, m_n[ZDisabled], &da);
                return (Pseudo(t.op) == Pseudo::Disabled) == da;
            }
            case Pseudo::Empty: return k.erstesKind == kKein;
            case Pseudo::Link: {
                if (k.name != NA && k.name != NArea && k.name != NLink) return false;
                bool da = false;
                m_d.attribut(el, m_n[ZHref], &da);
                return da;
            }
            case Pseudo::Nie: return false;
            }
            return false;
        case Teilart::Kombinator: return false;
        }
        return false;
    }

    int64_t typNr(uint32_t el, bool hinten) const {
        const uint32_t name = m_d.knoten[el].name;
        int64_t n = 1;
        if (!hinten) {
            for (uint32_t v = m_baum.vorher[el]; v != kKein; v = m_baum.vorher[v]) n += m_d.knoten[v].name == name;
        } else {
            for (uint32_t c = m_d.knoten[el].naechstes; c != kKein; c = m_d.knoten[c].naechstes)
                n += m_d.knoten[c].art == Art::Element && m_d.knoten[c].name == name;
        }
        return n;
    }

    const Dokument& m_d;
    const Baum& m_baum;
    const Bindung& m_bd;
    const Stilblatt& m_s;
    std::span<const uint8_t> m_zustand;
    uint32_t m_n[8] = {};
    uint32_t m_schritte = 0;
};

struct Quelle {
    const Stilblatt* b = nullptr;
    uint32_t dekl = kKein;
    bool operator==(const Quelle&) const = default;
};

//  Eigene Variablen eines Elements, aufgeloest; Kette ueber `eltern` zu den geerbten.
struct VarTabelle { uint32_t eltern = kKein, anfang = 0, anzahl = 0; };
struct VarEintrag { uint32_t atom = kKein, anfang = 0, laenge = 0; uint8_t zustand = 0; Quelle q; };
enum : uint8_t { VOffen, VLaeuft, VGut, VUngueltig };

//  var()-Lawine: 40 Stufen Verdopplung waeren Terabytes; ein laengerer Wert ist ungueltig.
constexpr size_t kVarGrenze = size_t(1) << 20;

class Kaskade {
public:
    Kaskade(const Dokument& d, const Stilblatt& autor, const Medium& m, std::span<const uint8_t> zustand, Stile& aus)
        : m_d(d), m_m(m), m_zustand(zustand), m_aus(aus), m_pool(aus.roh) {
        m_baum.baue(d);
        m_ua.binde(vorgaben(), d, m);
        m_autor.binde(autor, d, m);
        m_n[0] = d.nameSuchen("checked");
        m_n[1] = d.nameSuchen("type");
        m_n[2] = d.nameSuchen("selected");
        m_n[3] = d.nameSuchen("disabled");
        m_n[4] = d.nameSuchen("href");
        m_n[5] = d.nameSuchen("select");
        m_inline.assign(d.knoten.size(), kKein);
        m_hinweis.assign(d.knoten.size(), kKein);
        for (uint32_t r = 0; r < autor.regeln.size(); ++r)
            if (autor.regeln[r].element < d.knoten.size())
                (autor.regeln[r].hinweis ? m_hinweis : m_inline)[autor.regeln[r].element] = r;
    }

    void lauf() {
        const size_t n = m_d.knoten.size();
        m_aus.stile.clear();
        merke(kStandard);
        m_aus.jeKnoten.assign(n, 0);
        m_aus.vor.assign(n, kKein);
        m_aus.nach.assign(n, kKein);
        m_varTab.assign(n, kKein);
        Pruefer pu(m_d, m_baum, m_ua, m_zustand, m_n);
        Pruefer pa(m_d, m_baum, m_autor, m_zustand, m_n);
        for (uint32_t i = 0; i < n; ++i) {
            const Knoten& k = m_d.knoten[i];
            const uint32_t p = k.eltern;
            if (k.art != Art::Element) {
                m_aus.jeKnoten[i] = p < i ? m_aus.jeKnoten[p] : 0;
                continue;
            }
            const bool elternGut = p < i && m_d.knoten[p].art == Art::Element;
            const uint32_t elternStil = elternGut ? m_aus.jeKnoten[p] : 0;
            const uint32_t elternVars = elternGut ? m_varTab[p] : kKein;
            sammle(pu, m_ua, i, m_uaTreffer);
            sammle(pa, m_autor, i, m_autorTreffer);
            //  Gleiche Treffer unter gleichem Elternstil ergeben denselben Stil: Werte nur einmal lesen.
            const bool wurzel = i == m_d.html;
            const uint64_t h = schluessel(elternStil, elternVars, m_inline[i], m_hinweis[i]);
            if (!wurzel && nachschlagen(h, elternStil, elternVars, i)) continue;
            const Stil eigen = berechne(i, m_aus.stile[elternStil], elternVars, PseudoElement::Kein);
            m_aus.jeKnoten[i] = merke(eigen);
            m_varTab[i] = m_letzteVars;
            if (m_wurzel && wurzel) m_wurzelSchrift = eigen.schriftGroesse;
            for (const PseudoElement pe : { PseudoElement::Vor, PseudoElement::Nach }) {
                if (!hatPseudo(pe)) continue;
                const Stil ps = berechne(i, m_aus.stile[m_aus.jeKnoten[i]], m_varTab[i], pe);
                if (ps.inhalt != 0 && ps.anzeige != Anzeige::None)
                    (pe == PseudoElement::Vor ? m_aus.vor : m_aus.nach)[i] = merke(ps);
            }
            if (!wurzel) ablegen(h, elternStil, elternVars, i);
        }
    }

private:
    struct Gemerkt {
        uint32_t elternStil, elternVars, inlineRegel, hinweisRegel, anfang, ua, autor;
        uint32_t stil, vars, vor, nach, naechstes;
    };

    uint64_t schluessel(uint32_t elternStil, uint32_t elternVars, uint32_t inlineRegel, uint32_t hinweisRegel) const {
        uint64_t h = 0x9E3779B97F4A7C15ull;
        auto mische = [&](uint64_t v) { h = (h ^ v) * 0x100000001B3ull; h ^= h >> 29; };
        mische(elternStil);
        mische(elternVars);
        mische(inlineRegel);
        mische(hinweisRegel);
        for (uint64_t x : m_uaTreffer) mische(x);
        mische(~0ull);
        for (uint64_t x : m_autorTreffer) mische(x);
        return h;
    }

    bool nachschlagen(uint64_t h, uint32_t elternStil, uint32_t elternVars, uint32_t el) {
        const auto it = m_gemerktIndex.find(h);
        if (it == m_gemerktIndex.end()) return false;
        for (uint32_t g = it->second; g != kKein; g = m_gemerkt[g].naechstes) {
            const Gemerkt& x = m_gemerkt[g];
            if (x.elternStil != elternStil || x.elternVars != elternVars || x.inlineRegel != m_inline[el]
                || x.hinweisRegel != m_hinweis[el]
                || x.ua != m_uaTreffer.size() || x.autor != m_autorTreffer.size()
                || !std::equal(m_uaTreffer.begin(), m_uaTreffer.end(), m_gemerktTreffer.begin() + x.anfang)
                || !std::equal(m_autorTreffer.begin(), m_autorTreffer.end(), m_gemerktTreffer.begin() + x.anfang + x.ua))
                continue;
            m_aus.jeKnoten[el] = x.stil;
            m_varTab[el] = x.vars;
            m_aus.vor[el] = x.vor;
            m_aus.nach[el] = x.nach;
            return true;
        }
        return false;
    }

    void ablegen(uint64_t h, uint32_t elternStil, uint32_t elternVars, uint32_t el) {
        auto [it, neu] = m_gemerktIndex.try_emplace(h, kKein);
        Gemerkt g{ elternStil, elternVars, m_inline[el], m_hinweis[el], uint32_t(m_gemerktTreffer.size()), uint32_t(m_uaTreffer.size()),
                   uint32_t(m_autorTreffer.size()), m_aus.jeKnoten[el], m_varTab[el], m_aus.vor[el], m_aus.nach[el],
                   it->second };
        m_gemerktTreffer.insert(m_gemerktTreffer.end(), m_uaTreffer.begin(), m_uaTreffer.end());
        m_gemerktTreffer.insert(m_gemerktTreffer.end(), m_autorTreffer.begin(), m_autorTreffer.end());
        it->second = uint32_t(m_gemerkt.size());
        m_gemerkt.push_back(g);
    }

    void sammle(Pruefer& p, const Bindung& bd, uint32_t el, std::vector<uint64_t>& aus) {
        aus.clear();
        const Stilblatt& s = *bd.b;
        auto pruefe = [&](const std::vector<uint32_t>& eimer) {
            for (uint32_t sel : eimer) {
                const Selektor& x = s.selektoren[sel];
                const uint32_t medien = s.regeln[x.regel].medien;
                if (medien != kKein && !bd.medien[medien]) continue;
                if (p.passt(sel, el)) aus.push_back(uint64_t(x.spezifitaet) << 32 | sel);
            }
        };
        if (bd.idAtom[el] != kKein && bd.idAtom[el] < s.nachId.size()) pruefe(s.nachId[bd.idAtom[el]]);
        if (!bd.klassen.empty())
            for (uint32_t i = bd.klAnfang[el]; i < bd.klAnfang[el + 1]; ++i)
                if (bd.klassen[i] < s.nachKlasse.size()) pruefe(s.nachKlasse[bd.klassen[i]]);
        const uint32_t name = m_d.knoten[el].name;
        if (name < bd.atomVonName.size() && bd.atomVonName[name] != kKein && bd.atomVonName[name] < s.nachTyp.size())
            pruefe(s.nachTyp[bd.atomVonName[name]]);
        pruefe(s.uebrige);
        std::sort(aus.begin(), aus.end());
    }

    bool hatPseudo(PseudoElement pe) const {
        for (const auto* t : { &m_uaTreffer, &m_autorTreffer })
            for (uint64_t x : *t)
                if ((t == &m_uaTreffer ? m_ua : m_autor).b->selektoren[uint32_t(x)].pseudo == pe) return true;
        return false;
    }

    void wende(const Bindung& bd, const std::vector<uint64_t>& treffer, PseudoElement pe, bool wichtig) {
        const Stilblatt& s = *bd.b;
        for (uint64_t x : treffer) {
            const Selektor& sel = s.selektoren[uint32_t(x)];
            if (sel.pseudo != pe) continue;
            const Regel& r = s.regeln[sel.regel];
            wendeRegel(s, r, wichtig);
        }
    }

    void wendeRegel(const Stilblatt& s, const Regel& r, bool wichtig) {
        uint16_t lang[12];
        for (uint32_t i = r.dekl; i < r.dekl + r.anzahl; ++i) {
            const Deklaration& d = s.deklarationen[i];
            if (bool(d.wichtig) != wichtig) continue;
            const Quelle q{ &s, i };
            if (d.eig == EVariable) {
                auto it = std::find_if(m_eigeneVars.begin(), m_eigeneVars.end(),
                                       [&](const VarEintrag& v) { return v.atom == d.variable; });
                if (it == m_eigeneVars.end()) m_eigeneVars.push_back({ d.variable, 0, 0, VOffen, q });
                else it->q = q;
            } else if (d.eig < ELanghand) {
                m_gewinner[d.eig] = q;
            } else {
                const int n = langformen(d.eig, lang);
                for (int k = 0; k < n; ++k) m_gewinner[lang[k]] = q;
            }
        }
    }

    bool geerbteVariable(uint32_t atom, std::string& aus, uint32_t tabelle) const {
        for (uint32_t t = tabelle; t != kKein; t = m_tabellen[t].eltern) {
            const VarTabelle& tab = m_tabellen[t];
            for (uint32_t i = tab.anfang + tab.anzahl; i-- > tab.anfang;) {
                if (m_eintraege[i].atom != atom) continue;
                if (m_eintraege[i].zustand != VGut) return false;
                aus.append(m_varText, m_eintraege[i].anfang, m_eintraege[i].laenge);
                return aus.size() <= kVarGrenze;
            }
        }
        return false;
    }

    //  Text der Variable `name`, sichtbar am Element; false, wenn sie fehlt oder ungueltig ist.
    bool variable(std::string_view name, std::string& aus, uint32_t geerbt, int tiefe) {
        const uint32_t atom = m_autor.b->atomSuchen(name);
        if (atom == kKein) return false;
        for (size_t i = 0; i < m_eigeneVars.size(); ++i) {
            if (m_eigeneVars[i].atom != atom) continue;
            loese(i, geerbt, tiefe + 1);
            const VarEintrag& v = m_eigeneVars[i];
            if (v.zustand != VGut) return false;
            aus.append(m_varText, v.anfang, v.laenge);
            return aus.size() <= kVarGrenze;
        }
        return geerbteVariable(atom, aus, geerbt);
    }

    //  var() im Wert ersetzen; false, wenn eine Variable fehlt und kein Ersatz dasteht.
    bool ersetze(std::string_view w, std::string& aus, uint32_t geerbt, int tiefe) {
        if (tiefe > 32) return false;
        size_t i = 0;
        while (i < w.size()) {
            const char c = w[i];
            if (c == '"' || c == '\'') {
                size_t e = i + 1;
                while (e < w.size() && w[e] != c) e += w[e] == '\\' ? 2 : 1;
                e = std::min(e + 1, w.size());
                aus.append(w, i, e - i);
                i = e;
                continue;
            }
            const char v = i ? klein(w[i - 1]) : ' ';
            const bool grenze = !((v >= 'a' && v <= 'z') || ziffer(v) || v == '-' || v == '_');
            if (grenze && i + 4 <= w.size() && gleich(w.substr(i, 4), "var(")) {
                const size_t e = bisAussen(w, i + 4, w.size(), ")");
                if (e >= w.size()) return false;
                const std::string_view arg = w.substr(i + 4, e - i - 4);
                const size_t komma = bisAussen(arg, 0, arg.size(), ",");
                const std::string_view name = ohneRand(arg.substr(0, komma));
                if (!variable(name, aus, geerbt, tiefe)) {
                    if (komma >= arg.size() || !ersetze(ohneRand(arg.substr(komma + 1)), aus, geerbt, tiefe + 1))
                        return false;
                }
                if (aus.size() > kVarGrenze) return false;
                i = e + 1;
                continue;
            }
            aus += c;
            ++i;
        }
        return true;
    }

    //  Eigene Variablen loesen sich gegenseitig auf; ein Zyklus macht die Beteiligten ungueltig.
    void loese(size_t i, uint32_t geerbt, int tiefe) {
        VarEintrag& v = m_eigeneVars[i];
        if (v.zustand == VGut || v.zustand == VUngueltig) return;
        if (v.zustand == VLaeuft || tiefe > 32) { v.zustand = VUngueltig; return; }
        const Deklaration& d = v.q.b->deklarationen[v.q.dekl];
        const std::string_view w = ohneRand(v.q.b->wert(d));
        const int g = stil::global(w);
        std::string text;
        bool gut = true;
        if (g == 1 || g == 3) gut = geerbteVariable(v.atom, text, geerbt);
        else if (g == 2) gut = false;
        else if (d.mitVar) {
            m_eigeneVars[i].zustand = VLaeuft;
            gut = ersetze(w, text, geerbt, tiefe);
        } else {
            text = w;
        }
        VarEintrag& fertig = m_eigeneVars[i];
        if (!gut || m_varText.size() + text.size() > 64 * kVarGrenze) { fertig.zustand = VUngueltig; return; }
        fertig.anfang = uint32_t(m_varText.size());
        fertig.laenge = uint32_t(text.size());
        m_varText += text;
        fertig.zustand = VGut;
    }

    Stil berechne(uint32_t el, const Stil& eltern, uint32_t elternVars, PseudoElement pe) {
        std::fill(std::begin(m_gewinner), std::end(m_gewinner), Quelle{});
        m_eigeneVars.clear();
        //  Herkunft vor Spezifitaet: Vorgaben < Autor < style-Attribut; !important kehrt die Folge um.
        wende(m_ua, m_uaTreffer, pe, false);
        if (pe == PseudoElement::Kein && m_hinweis[el] != kKein) wendeRegel(*m_autor.b, m_autor.b->regeln[m_hinweis[el]], false);
        wende(m_autor, m_autorTreffer, pe, false);
        if (pe == PseudoElement::Kein && m_inline[el] != kKein) wendeRegel(*m_autor.b, m_autor.b->regeln[m_inline[el]], false);
        wende(m_autor, m_autorTreffer, pe, true);
        if (pe == PseudoElement::Kein && m_inline[el] != kKein) wendeRegel(*m_autor.b, m_autor.b->regeln[m_inline[el]], true);
        wende(m_ua, m_uaTreffer, pe, true);

        m_letzteVars = elternVars;
        if (!m_eigeneVars.empty()) {
            for (size_t i = 0; i < m_eigeneVars.size(); ++i) loese(i, elternVars, 0);
            VarTabelle t{ elternVars, uint32_t(m_eintraege.size()), uint32_t(m_eigeneVars.size()) };
            m_eintraege.insert(m_eintraege.end(), m_eigeneVars.begin(), m_eigeneVars.end());
            m_tabellen.push_back(t);
            m_letzteVars = uint32_t(m_tabellen.size() - 1);
        }

        Stil s = kStandard;
        for (uint16_t e = 0; e < EGeerbt; ++e) stil::uebernimm(e, eltern, s);
        const stil::Umfeld u{ &eltern, m_wurzel ? m_wurzelSchrift : 16 * kQ, &m_m };
        //  em haengt an der eigenen Schriftgroesse, ch/ex an der Schrift, currentColor an der Farbe - alle zuerst.
        //  Kurzformen des Vorgaengers sind mit dessen Elternwerten gelesen: vor allem anderen leeren.
        m_kurzGetan.clear();
        static constexpr uint16_t zuerst[] = { EFontSize, EFontFamily, EFontWeight, EFontStyle, EColor };
        for (uint16_t e : zuerst) anwenden(e, s, eltern, u);
        for (int i = 0; i < 4; ++i) s.rahmenFarbe[i] = s.farbe;
        for (uint16_t e = 0; e < ELanghand; ++e)
            if (std::find(std::begin(zuerst), std::end(zuerst), e) == std::end(zuerst) && m_gewinner[e].b)
                anwenden(e, s, eltern, u);

        s.svgEigen = uint32_t((m_gewinner[EFill].b ? 1 : 0) | (m_gewinner[EStroke].b ? 2 : 0)
                             | (m_gewinner[EStrokeWidth].b ? 4 : 0));
        //  Rahmen ohne Stil haben keine Breite; sonst rastet die Breite auf Geraetepixel, duenne auf eines.
        const double f = m_m.pixelFaktor > 0 ? m_m.pixelFaktor : 1;
        for (int i = 0; i < 4; ++i) {
            if (s.rahmenStil[i] == RahmenStil::None || s.rahmenStil[i] == RahmenStil::Hidden) s.rahmenBreite[i] = 0;
            else if (s.rahmenBreite[i] > 0) {
                const double g = s.rahmenBreite[i] / double(kQ) * f;
                s.rahmenBreite[i] = q6((g < 1 ? 1 : std::floor(g)) / f);
            }
        }
        //  Scrollt eine Achse, kann die andere nicht ueberlaufen: visible wird auto, clip wird hidden.
        auto rollt = [](Ueberlauf u) { return u != Ueberlauf::Visible && u != Ueberlauf::Clip; };
        if (rollt(s.ueberlaufX) != rollt(s.ueberlaufY)) {
            Ueberlauf& u = rollt(s.ueberlaufX) ? s.ueberlaufY : s.ueberlaufX;
            u = u == Ueberlauf::Visible ? Ueberlauf::Auto : Ueberlauf::Hidden;
        }
        const bool flexEltern = eltern.anzeige == Anzeige::Flex || eltern.anzeige == Anzeige::InlineFlex
                             || eltern.anzeige == Anzeige::Grid || eltern.anzeige == Anzeige::InlineGrid;
        if ((pe == PseudoElement::Kein && el == m_d.html) || s.position == Lage::Absolute || s.position == Lage::Fixed
            || flexEltern)
            blockifiziere(s);
        if (pe == PseudoElement::Kein && el == m_d.html) m_wurzel = true;
        return s;
    }

    //  Wurzel, absolut gesetzte Elemente und Kinder von flex/grid sind schon im berechneten Wert Bloecke.
    static void blockifiziere(Stil& s) {
        switch (s.anzeige) {
        case Anzeige::InlineFlex: s.anzeige = Anzeige::Flex; break;
        case Anzeige::InlineGrid: s.anzeige = Anzeige::Grid; break;
        case Anzeige::InlineTable: s.anzeige = Anzeige::Table; break;
        case Anzeige::Inline: case Anzeige::InlineBlock: case Anzeige::Ruby: case Anzeige::RubyText:
        case Anzeige::TableRowGroup: case Anzeige::TableHeaderGroup: case Anzeige::TableFooterGroup:
        case Anzeige::TableRow: case Anzeige::TableCell: case Anzeige::TableColumnGroup: case Anzeige::TableColumn:
        case Anzeige::TableCaption:
            s.anzeige = Anzeige::Block;
            break;
        default: break;
        }
    }

    void zuruecksetzen(uint16_t e, int g, Stil& s, const Stil& eltern) {
        const bool erbt = e < EGeerbt;
        if (g == 1 || (g == 3 && erbt)) stil::uebernimm(e, eltern, s);
        else stil::uebernimm(e, kStandard, s);
        if (!(g == 1 || (g == 3 && erbt)) && e >= EBorderTopColor && e <= EBorderLeftColor)
            s.rahmenFarbe[e - EBorderTopColor] = s.farbe;
    }

    void anwenden(uint16_t e, Stil& s, const Stil& eltern, const stil::Umfeld& u) {
        const Quelle q = m_gewinner[e];
        if (!q.b) return;
        const Deklaration& d = q.b->deklarationen[q.dekl];
        //  Eine Kurzform wird je Deklaration einmal gelesen und gibt nur die Langformen ab, die sie gewonnen hat.
        if (d.eig != e) {
            auto it = std::find_if(m_kurzGetan.begin(), m_kurzGetan.end(), [&](const auto& k) { return k.first == q; });
            if (it == m_kurzGetan.end()) {
                Stil probe = s;
                std::string_view w = q.b->wert(d);
                int g = 0;
                bool gut = true;
                if (d.mitVar) {
                    m_text.clear();
                    gut = ersetze(w, m_text, m_letzteVars, 0);
                    w = m_text;
                }
                if (gut) g = stil::global(w);
                if (gut && !g) gut = stil::setze(d.eig, w, probe, u, &m_pool);
                m_kurzGetan.push_back({ q, { probe, gut ? g : 3 } });
                it = m_kurzGetan.end() - 1;
            }
            const auto& [probe, g] = it->second;
            if (g) zuruecksetzen(e, g, s, eltern);
            else stil::uebernimm(e, probe, s);
            return;
        }
        std::string_view w = q.b->wert(d);
        if (d.mitVar) {
            m_text.clear();
            if (!ersetze(w, m_text, m_letzteVars, 0)) { zuruecksetzen(e, 3, s, eltern); return; }
            w = m_text;
        }
        if (const int g = stil::global(w)) { zuruecksetzen(e, g, s, eltern); return; }
        if (!stil::setze(e, w, s, u, &m_pool)) zuruecksetzen(e, 3, s, eltern);
    }

    uint32_t merke(const Stil& s) {
        const size_t h = std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(&s), sizeof(Stil)));
        auto it = m_einmal.try_emplace(h, kKein).first;
        for (uint32_t i = it->second; i != kKein; i = m_kette[i])
            if (std::memcmp(&m_aus.stile[i], &s, sizeof(Stil)) == 0) return i;
        const auto id = uint32_t(m_aus.stile.size());
        m_aus.stile.push_back(s);
        m_kette.push_back(it->second);
        it->second = id;
        return id;
    }

    const Dokument& m_d;
    const Medium& m_m;
    std::span<const uint8_t> m_zustand;
    Stile& m_aus;
    RohPool m_pool;
    Baum m_baum;
    Bindung m_ua, m_autor;
    uint32_t m_n[8] = {};
    std::vector<uint32_t> m_inline, m_hinweis;
    std::vector<uint64_t> m_uaTreffer, m_autorTreffer;
    Quelle m_gewinner[ELanghand];
    std::vector<VarEintrag> m_eigeneVars, m_eintraege;
    std::vector<VarTabelle> m_tabellen;
    std::vector<uint32_t> m_varTab;
    std::string m_varText, m_text;
    uint32_t m_letzteVars = kKein;
    std::vector<std::pair<Quelle, std::pair<Stil, int>>> m_kurzGetan;
    std::unordered_map<size_t, uint32_t> m_einmal;
    std::vector<uint32_t> m_kette;
    std::unordered_map<uint64_t, uint32_t> m_gemerktIndex;
    std::vector<Gemerkt> m_gemerkt;
    std::vector<uint64_t> m_gemerktTreffer;
    int32_t m_wurzelSchrift = 16 * kQ;
    bool m_wurzel = false;
};

}   // namespace

size_t Stile::bytes() const {
    size_t n = stile.capacity() * sizeof(Stil) + (jeKnoten.capacity() + vor.capacity() + nach.capacity()) * sizeof(uint32_t)
             + roh.capacity() * sizeof(std::string);
    for (const std::string& r : roh) n += r.size() > 15 ? r.capacity() + 1 : 0;
    return n;
}

Stile berechne(const Dokument& d, const Stilblatt& blatt, const Medium& m, std::span<const uint8_t> zustand) {
    Stile aus;
    Kaskade(d, blatt, m, zustand, aus).lauf();
    return aus;
}

namespace {

struct Formular {
    const Dokument& d;
    uint32_t type, checked, selected, open, fuer, name, disabled, label, form;

    explicit Formular(const Dokument& dok)
        : d(dok), type(d.nameSuchen("type")), checked(d.nameSuchen("checked")), selected(d.nameSuchen("selected")),
          open(d.nameSuchen("open")), fuer(d.nameSuchen("for")), name(d.nameSuchen("name")),
          disabled(d.nameSuchen("disabled")), label(d.nameSuchen("label")), form(NForm) {}

    bool ist(uint32_t n, uint32_t nm) const {
        return nm != kKein && n < d.knoten.size() && d.knoten[n].art == Art::Element && d.knoten[n].raum == Raum::Html
            && d.knoten[n].name == nm;
    }
    bool hat(uint32_t n, uint32_t attr) const {
        bool da = false;
        if (attr != kKein) d.attribut(n, attr, &da);
        return da;
    }
    //  1 Haken, 2 Radioknopf, sonst 0.
    int schalter(uint32_t n) const {
        if (!ist(n, NInput) || type == kKein) return 0;
        const std::string_view t = d.attribut(n, type);
        return gleich(t, "checkbox") ? 1 : gleich(t, "radio") ? 2 : 0;
    }
    uint32_t formularVon(uint32_t n) const {
        while (n < d.knoten.size() && !ist(n, form)) n = d.knoten[n].eltern;
        return n;
    }
    //  Ziel eines label: das Element mit der id aus for, sonst der erste Schalter darin.
    uint32_t ziel(uint32_t l) const {
        bool da = false;
        const std::string_view id = fuer == kKein ? std::string_view() : d.attribut(l, fuer, &da);
        if (da) {
            for (uint32_t n = 0; n < d.knoten.size(); ++n)
                if (d.knoten[n].art == Art::Element && hat(n, NId) && d.attribut(n, NId) == id) return n;
            return kKein;
        }
        //  Eltern stehen vor ihren Kindern: ein Nachfahre hat l unter seinen Vorfahren, keiner davon liegt vor l.
        for (uint32_t n = l + 1; n < d.knoten.size(); ++n) {
            uint32_t v = d.knoten[n].eltern;
            while (v != kKein && v > l) v = d.knoten[v].eltern;
            if (v == l && schalter(n)) return n;
        }
        return kKein;
    }
};

}   // namespace

std::vector<uint8_t> anfangsZustand(const Dokument& d) {
    const Formular f(d);
    std::vector<uint8_t> z(d.knoten.size(), 0);
    for (uint32_t n = 0; n < d.knoten.size(); ++n) {
        if ((f.schalter(n) && f.hat(n, f.checked)) || (f.ist(n, NOption) && f.hat(n, f.selected))) z[n] |= kAngehakt;
        if (f.ist(n, NDetails) && f.hat(n, f.open)) z[n] |= kOffen;
    }
    return z;
}

bool aktiviere(const Dokument& d, std::vector<uint8_t>& z, uint32_t element) {
    if (z.size() != d.knoten.size()) z = anfangsZustand(d);
    const Formular f(d);
    const auto schalte = [&](uint32_t n) {
        const int art = f.schalter(n);
        if (!art || f.hat(n, f.disabled)) return false;
        if (art == 1) { z[n] ^= kAngehakt; return true; }
        if (z[n] & kAngehakt) return false;
        z[n] |= kAngehakt;
        const std::string_view gruppe = f.name == kKein ? std::string_view() : d.attribut(n, f.name);
        if (gruppe.empty()) return true;
        const uint32_t form = f.formularVon(n);
        for (uint32_t m = 0; m < d.knoten.size(); ++m)
            if (m != n && f.schalter(m) == 2 && d.attribut(m, f.name) == gruppe && f.formularVon(m) == form)
                z[m] &= uint8_t(~kAngehakt);
        return true;
    };
    for (uint32_t e = element; e < d.knoten.size(); e = d.knoten[e].eltern) {
        const uint32_t p = d.knoten[e].eltern;
        if (f.ist(e, NSummary) && f.ist(p, NDetails)) {
            uint32_t erste = d.knoten[p].erstesKind;
            while (erste != kKein && !f.ist(erste, NSummary)) erste = d.knoten[erste].naechstes;
            if (erste == e) { z[p] ^= kOffen; return true; }
        }
        if (f.schalter(e)) return schalte(e);
        if (f.ist(e, f.label)) { const uint32_t t = f.ziel(e); return t != kKein && schalte(t); }
    }
    return false;
}

bool oeffneUm(const Dokument& d, std::vector<uint8_t>& z, uint32_t element) {
    if (z.size() != d.knoten.size()) z = anfangsZustand(d);
    const Formular f(d);
    bool neu = false;
    for (uint32_t e = element; e < d.knoten.size(); e = d.knoten[e].eltern) {
        const uint32_t p = d.knoten[e].eltern;
        if (!f.ist(p, NDetails) || (z[p] & kOffen)) continue;
        uint32_t erste = d.knoten[p].erstesKind;
        while (erste != kKein && !f.ist(erste, NSummary)) erste = d.knoten[erste].naechstes;
        if (erste == e) continue;
        z[p] |= kOffen;
        neu = true;
    }
    return neu;
}

uint32_t verweisUm(const Dokument& d, uint32_t element) {
    const Formular f(d);
    const uint32_t href = d.nameSuchen("href");
    if (href == kKein) return kKein;
    for (uint32_t e = element; e < d.knoten.size(); e = d.knoten[e].eltern) {
        if (f.schalter(e)) return kKein;
        if (f.ist(e, NA) && f.hat(e, href)) return e;
    }
    return kKein;
}

uint32_t sprungZiel(const Dokument& d, std::string_view fragment) {
    if (fragment.empty()) return 0;
    const Formular f(d);
    for (uint32_t n = 0; n < d.knoten.size(); ++n)
        if (d.knoten[n].art == Art::Element && f.hat(n, NId) && d.attribut(n, NId) == fragment) return n;
    if (f.name != kKein)
        for (uint32_t n = 0; n < d.knoten.size(); ++n)
            if (f.ist(n, NA) && f.hat(n, f.name) && d.attribut(n, f.name) == fragment) return n;
    return gleich(fragment, "top") ? 0 : kKein;
}

}   // namespace mg::html
