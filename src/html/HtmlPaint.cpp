#include "html/HtmlPaint.h"

#include "html/HtmlBild.h"

#include <QGlyphRun>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string_view>
#include <unordered_map>

namespace mg::html {

namespace {

float px(int32_t v) { return v / float(kQ); }

uint32_t mitDeckkraft(uint32_t argb, uint8_t alpha) {
    if (alpha == 255) return argb;
    return (((argb >> 24) * alpha / 255) << 24) | (argb & 0xFFFFFFu);
}

bool sichtbar(RahmenStil s) { return s != RahmenStil::None && s != RahmenStil::Hidden; }

// ---- Werte aus Stile::roh: Verlaeufe und transform ----

bool gleich(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if ((a[i] | 0x20) != (b[i] | 0x20)) return false;
    return true;
}

std::string_view getrimmt(std::string_view v) {
    while (!v.empty() && std::isspace(uint8_t(v.front()))) v.remove_prefix(1);
    while (!v.empty() && std::isspace(uint8_t(v.back()))) v.remove_suffix(1);
    return v;
}

//  Teilt an `trenner` ausserhalb von Klammern; mit ' ' auch an jedem anderen Leerraum.
std::vector<std::string_view> teile(std::string_view v, char trenner) {
    std::vector<std::string_view> aus;
    int tiefe = 0;
    size_t a = 0;
    for (size_t i = 0; i <= v.size(); ++i) {
        const char c = i < v.size() ? v[i] : trenner;
        if (c == '(') ++tiefe;
        else if (c == ')') tiefe = std::max(0, tiefe - 1);
        else if (tiefe == 0 && (c == trenner || (trenner == ' ' && std::isspace(uint8_t(c))))) {
            const std::string_view x = getrimmt(v.substr(a, i - a));
            if (!x.empty()) aus.push_back(x);
            a = i + 1;
        }
    }
    return aus;
}

//  Funktion `name(...)`: der Inhalt der Klammer.
bool funktion(std::string_view v, std::string_view& name, std::string_view& arg) {
    const size_t k = v.find('(');
    if (k == std::string_view::npos || v.back() != ')') return false;
    name = v.substr(0, k);
    arg = v.substr(k + 1, v.size() - k - 2);
    return true;
}

bool zahl(std::string_view v, float& z, std::string_view& einheit) {
    if (!v.empty() && v.front() == '+') v.remove_prefix(1);
    const auto [e, fehler] = std::from_chars(v.data(), v.data() + v.size(), z);
    if (fehler != std::errc()) return false;
    einheit = v.substr(size_t(e - v.data()));
    return true;
}

//  Laenge in px; Prozent gegen `bezug`, em gegen `em`.
bool laenge(std::string_view v, float bezug, float em, float& px) {
    float z;
    std::string_view e;
    if (!zahl(v, z, e)) return false;
    if (e.empty() || gleich(e, "px")) px = z;
    else if (e == "%") px = z * bezug / 100;
    else if (gleich(e, "em")) px = z * em;
    else if (gleich(e, "rem")) px = z * 16;
    else if (gleich(e, "pt")) px = z * 4 / 3;
    else return false;
    return e.empty() ? z == 0 : true;
}

bool winkel(std::string_view v, float& grad) {
    float z;
    std::string_view e;
    if (!zahl(v, z, e)) return false;
    if (gleich(e, "deg") || (e.empty() && z == 0)) grad = z;
    else if (gleich(e, "turn")) grad = z * 360;
    else if (gleich(e, "rad")) grad = z * 180 / std::numbers::pi_v<float>;
    else if (gleich(e, "grad")) grad = z * 0.9f;
    else return false;
    return true;
}

//  Lage im Bezug: Schluesselwort oder Laenge; `waag` waehlt die Achse fuer left/right bzw. top/bottom.
bool lageWert(std::string_view v, float bezug, float em, bool waag, float& aus) {
    if (gleich(v, "center")) { aus = bezug / 2; return true; }
    if (gleich(v, waag ? "left" : "top")) { aus = 0; return true; }
    if (gleich(v, waag ? "right" : "bottom")) { aus = bezug; return true; }
    return laenge(v, bezug, em, aus);
}

//  Zwei Werte x y; ein einzelnes Schluesselwort der anderen Achse tauscht die Reihenfolge.
void lagePaar(const std::vector<std::string_view>& w, float b, float h, float em, float& x, float& y) {
    x = b / 2;
    y = h / 2;
    if (w.empty()) return;
    const auto senkrecht = [](std::string_view v) { return gleich(v, "top") || gleich(v, "bottom"); };
    const auto waagrecht = [](std::string_view v) { return gleich(v, "left") || gleich(v, "right"); };
    if (w.size() == 1) {
        if (senkrecht(w[0])) lageWert(w[0], h, em, false, y);
        else lageWert(w[0], b, em, true, x);
        return;
    }
    const bool tauschen = senkrecht(w[0]) || waagrecht(w[1]);
    lageWert(w[tauschen ? 1 : 0], b, em, true, x);
    lageWert(w[tauschen ? 0 : 1], h, em, false, y);
}

//  Farbhalte: fehlende Lagen gleichmaessig verteilt, keine Lage vor der vorigen; Laengen gegen `laengeLinie`.
bool halte(const std::vector<std::string_view>& args, size_t ab, float laengeLinie, float em, uint32_t aktuell,
           std::vector<Halt>& aus) {
    const size_t anfang = aus.size();
    std::vector<uint8_t> fest;
    for (size_t i = ab; i < args.size(); ++i) {
        const std::vector<std::string_view> w = teile(args[i], ' ');
        uint32_t f;
        if (w.empty() || !stil::farbe(w[0], aktuell, f)) {
            float l;
            if (w.size() == 1 && laenge(w[0], laengeLinie, em, l)) continue;    // Mittelpunkt-Hinweis
            return false;
        }
        for (size_t k = 1; k <= std::max<size_t>(1, w.size() - 1); ++k) {
            float l = 0;
            const bool da = k < w.size() && laenge(w[k], laengeLinie, em, l);
            aus.push_back(Halt{ laengeLinie > 0 ? l / laengeLinie : 0, f });
            fest.push_back(da);
        }
    }
    const size_t n = aus.size() - anfang;
    if (n < 1) return false;
    Halt* h = aus.data() + anfang;
    if (!fest[0]) { h[0].lage = 0; fest[0] = 1; }
    if (n > 1 && !fest[n - 1]) { h[n - 1].lage = 1; fest[n - 1] = 1; }
    for (size_t i = 1; i < n; ++i) {
        if (fest[i]) { h[i].lage = std::max(h[i].lage, h[i - 1].lage); continue; }
        size_t j = i;
        while (!fest[j]) ++j;
        const float a = h[i - 1].lage, e = std::max(h[j].lage, a);
        for (size_t k = i; k < j; ++k) h[k].lage = a + (e - a) * float(k - i + 1) / float(j - i + 1);
    }
    //  CSS mischt vormultipliziert: ein durchsichtiger Halt nimmt den Farbton des Nachbarn an.
    for (size_t i = 0; i < n; ++i)
        if ((h[i].farbe >> 24) == 0) {
            const Halt* nb = i + 1 < n && (h[i + 1].farbe >> 24) ? &h[i + 1] : i > 0 ? &h[i - 1] : nullptr;
            if (nb) h[i].farbe = nb->farbe & 0xFFFFFFu;
        }
    return true;
}

//  Ein Verlauf auf einer Kachel b x h; false, wenn die Schicht nicht gezeichnet werden kann (url(), conic ...).
bool verlauf(std::string_view name, std::string_view arg, float b, float h, float em, uint32_t aktuell, Verlauf& v,
             std::vector<Halt>& alle) {
    const std::vector<std::string_view> args = teile(arg, ',');
    if (args.empty()) return false;
    const bool wieder = name.substr(0, std::min<size_t>(10, name.size())) == "repeating-";
    const std::string_view art = wieder ? name.substr(10) : name;
    v.haltAnfang = uint32_t(alle.size());
    if (gleich(art, "linear-gradient")) {
        float grad = 180;
        size_t ab = 0;
        const std::vector<std::string_view> w = teile(args[0], ' ');
        if (!w.empty() && gleich(w[0], "to")) {
            bool l = false, r = false, o = false, u = false;
            for (size_t i = 1; i < w.size(); ++i) {
                l |= gleich(w[i], "left"); r |= gleich(w[i], "right");
                o |= gleich(w[i], "top"); u |= gleich(w[i], "bottom");
            }
            //  Ecken: die Linie steht senkrecht auf der Diagonale zwischen den beiden anderen Ecken.
            const float dx = l ? -h : r ? h : 0, dy = o ? -b : u ? b : 0;
            grad = (l || r) && (o || u) ? std::atan2(dx, -dy) * 180 / std::numbers::pi_v<float>
                 : r ? 90 : l ? 270 : o ? 0 : 180;
            ab = 1;
        } else if (w.size() == 1 && winkel(w[0], grad)) {
            ab = 1;
        }
        const float a = grad * std::numbers::pi_v<float> / 180, sx = std::sin(a), sy = -std::cos(a);
        const float laenge = std::abs(b * sx) + std::abs(h * sy);
        v.x1 = b / 2 - sx * laenge / 2;
        v.y1 = h / 2 - sy * laenge / 2;
        v.x2 = b / 2 + sx * laenge / 2;
        v.y2 = h / 2 + sy * laenge / 2;
        if (!halte(args, ab, laenge, em, aktuell, alle)) return false;
    } else if (gleich(art, "radial-gradient")) {
        v.radial = true;
        size_t ab = 0;
        bool kreis = false;
        float cx = b / 2, cy = h / 2, rx = -1, ry = -1;
        std::vector<std::string_view> w = teile(args[0], ' ');
        uint32_t probe;
        if (!w.empty() && !stil::farbe(w[0], aktuell, probe)) {
            ab = 1;
            const auto bei = std::find_if(w.begin(), w.end(), [](std::string_view x) { return gleich(x, "at"); });
            if (bei != w.end()) lagePaar(std::vector<std::string_view>(bei + 1, w.end()), b, h, em, cx, cy);
            std::vector<float> groessen;
            for (auto it = w.begin(); it != bei; ++it) {
                float l;
                if (gleich(*it, "circle")) kreis = true;
                else if (laenge(*it, b, em, l)) groessen.push_back(l);
            }
            if (groessen.size() == 1) { kreis = true; rx = ry = groessen[0]; }
            else if (groessen.size() >= 2) { rx = groessen[0]; ry = groessen[1]; }
        }
        if (rx < 0) {
            //  farthest-corner; die Ellipse behaelt das Seitenverhaeltnis von farthest-side.
            const float fx = std::max(cx, b - cx), fy = std::max(cy, h - cy);
            if (kreis) rx = ry = std::hypot(fx, fy);
            else { rx = fx * std::numbers::sqrt2_v<float>; ry = fy * std::numbers::sqrt2_v<float>; }
        }
        v.x1 = cx;
        v.y1 = cy;
        v.x2 = std::max(rx, 0.01f);
        v.y2 = std::max(ry, 0.01f);
        if (!halte(args, ab, v.x2, em, aktuell, alle)) return false;
    } else {
        return false;
    }
    v.haltAnzahl = uint32_t(alle.size()) - v.haltAnfang;
    return true;
}

//  transform mit translate, scale, rotate und rotateX/Y (ohne Perspektive: nur der Kosinus bleibt), um die Mitte.
bool transformation(std::string_view wert, const Rechteck& r, float em, QTransform& aus) {
    QTransform m;
    for (std::string_view f : teile(wert, ' ')) {
        std::string_view name, arg;
        if (!funktion(f, name, arg)) return false;
        std::vector<std::string_view> a = teile(arg, ',');
        if (a.size() == 1 && arg.find(',') == std::string_view::npos) a = teile(arg, ' ');
        if (a.empty()) return false;
        float x = 0, y = 0, g = 0;
        if (gleich(name, "translate") || gleich(name, "translateX") || gleich(name, "translateY")) {
            const bool nurY = gleich(name, "translateY");
            if (!laenge(a[0], nurY ? r.h : r.b, em, nurY ? y : x)) return false;
            if (gleich(name, "translate") && a.size() > 1 && !laenge(a[1], r.h, em, y)) return false;
            m.translate(x, y);
        } else if (gleich(name, "scale") || gleich(name, "scaleX") || gleich(name, "scaleY")) {
            std::string_view e;
            if (!zahl(a[0], x, e)) return false;
            y = x;
            if (a.size() > 1 && !zahl(a[1], y, e)) return false;
            if (gleich(name, "scaleX")) y = 1;
            if (gleich(name, "scaleY")) { y = x; x = 1; }
            m.scale(x, y);
        } else if (gleich(name, "rotate") || gleich(name, "rotateZ")) {
            if (!winkel(a[0], g)) return false;
            m.rotate(g);
        } else if (gleich(name, "rotateX") || gleich(name, "rotateY")) {
            if (!winkel(a[0], g)) return false;
            const float c = std::cos(g * std::numbers::pi_v<float> / 180);
            m.scale(gleich(name, "rotateY") ? c : 1, gleich(name, "rotateX") ? c : 1);
        } else {
            return false;
        }
    }
    const float ox = r.x + r.b / 2, oy = r.y + r.h / 2;
    aus = QTransform::fromTranslate(-ox, -oy) * m * QTransform::fromTranslate(ox, oy);
    return true;
}

// ---- Bilder und Inline-SVG ----

//  Kachel nach CSS Backgrounds 3, 3.9: Laengen, contain/cover, auto aus der eigenen Groesse bzw. dem Verhaeltnis.
void bildKachel(const std::vector<std::string_view>& groesse, const Rechteck& feld, float em, const Masse& m,
                float& kb, float& kh) {
    const float v = m.verhaeltnis;
    const auto passend = [&](bool cover) {
        kb = feld.b;
        kh = v > 0 ? feld.b / v : feld.h;
        if (v > 0 && (cover ? kh < feld.h : kh > feld.h)) { kh = feld.h; kb = feld.h * v; }
    };
    if (!groesse.empty() && (gleich(groesse[0], "contain") || gleich(groesse[0], "cover"))) {
        passend(gleich(groesse[0], "cover"));
        return;
    }
    float w = -1, h = -1;
    if (!groesse.empty() && !gleich(groesse[0], "auto") && !laenge(groesse[0], feld.b, em, w)) w = -1;
    if (groesse.size() > 1 && !gleich(groesse[1], "auto") && !laenge(groesse[1], feld.h, em, h)) h = -1;
    if (w >= 0 && h >= 0) { kb = w; kh = h; return; }
    if (w >= 0) { kb = w; kh = v > 0 ? w / v : m.h > 0 ? m.h : feld.h; return; }
    if (h >= 0) { kh = h; kb = v > 0 ? h * v : m.b > 0 ? m.b : feld.b; return; }
    if (m.b > 0 && m.h > 0) { kb = m.b; kh = m.h; return; }
    if (m.b > 0) { kb = m.b; kh = v > 0 ? m.b / v : feld.h; return; }
    if (m.h > 0) { kh = m.h; kb = v > 0 ? m.h * v : feld.b; return; }
    if (v > 0) { passend(false); return; }
    kb = feld.b;
    kh = feld.h;
}

void xmlText(std::string_view t, std::string& aus) {
    for (const char c : t) {
        if (c == '&') aus += "&amp;";
        else if (c == '<') aus += "&lt;";
        else if (c == '>') aus += "&gt;";
        else if (c == '"') aus += "&quot;";
        else aus += c;
    }
}

//  Farbige Eigenschaften mit ihrer Deckkraft: QtSvg liest weder rgba() noch #rrggbbaa (beides wird schwarz).
constexpr std::string_view kSvgFarbe[] = { "fill", "stroke", "stop-color", "flood-color", "color" };
constexpr std::string_view kSvgDeckkraft[] = { "fill-opacity", "stroke-opacity", "stop-opacity", "flood-opacity" };

template <size_t N>
int svgIndex(const std::string_view (&tab)[N], std::string_view n) {
    for (size_t i = 0; i < N; ++i)
        if (gleich(n, tab[i])) return int(i);
    return -1;
}

void attribut(std::string_view an, std::string_view w, std::string& aus) {
    aus += ' ';
    aus += an;
    aus += "=\"";
    xmlText(w, aus);
    aus += '"';
}

//  Der SVG-Teilbaum als XML fuer QtSvg (es kennt kein CSS): currentColor wird je Element seine Farbe, Deckkraft einer Farbe
//  geht in das *-opacity, fill/stroke/stroke-width einer Regel am Element schlagen dessen Attribut (an der Wurzel auch
//  geerbte, darunter erbt QtSvg selbst); `druck` dunkelt helle Farben ab wie die Schrift.
void schreibeSvg(const Dokument& d, const Stile& s, uint32_t el, bool wurzel, bool druck, std::string& aus) {
    const Knoten& k = d.knoten[el];
    const std::string_view name = d.nameVon(k.name);
    aus += '<';
    aus += name;
    const Stil st = el < s.jeKnoten.size() ? s.stile[s.jeKnoten[el]] : Stil{};
    bool ns = false, xlink = false;
    std::string_view farbWert[std::size(kSvgFarbe)] = {}, deck[std::size(kSvgDeckkraft)] = {}, breite;
    bool hatFarbe[std::size(kSvgFarbe)] = {}, hatDeck[std::size(kSvgDeckkraft)] = {}, hatBreite = false;
    std::string stilRest;
    const auto nimm = [&](std::string_view an, std::string_view w) {
        if (const int i = svgIndex(kSvgFarbe, an); i >= 0) { farbWert[i] = w; hatFarbe[i] = true; return true; }
        if (const int i = svgIndex(kSvgDeckkraft, an); i >= 0) { deck[i] = w; hatDeck[i] = true; return true; }
        if (gleich(an, "stroke-width")) { breite = w; hatBreite = true; return true; }
        return false;
    };
    for (uint32_t i = k.anfang; i < k.anfang + k.anzahl && i < d.attribute.size(); ++i) {
        const std::string_view an = d.nameVon(d.attribute[i].name), w = d.wert(d.attribute[i]);
        ns |= an == "xmlns";
        xlink |= an == "xmlns:xlink";
        if (nimm(an, w)) continue;
        if (an != "style") { attribut(an, w, aus); continue; }
        //  Im style-Attribut schlagen sie die Attribute; der Rest bleibt dort.
        for (size_t a = 0; a < w.size();) {
            size_t e = w.find(';', a);
            if (e == std::string_view::npos) e = w.size();
            const std::string_view dekl = w.substr(a, e - a);
            a = e + 1;
            const size_t p = dekl.find(':');
            if (p == std::string_view::npos || nimm(getrimmt(dekl.substr(0, p)), getrimmt(dekl.substr(p + 1)))) continue;
            stilRest += dekl;
            stilRest += ';';
        }
    }
    const auto regel = [&](uint32_t roh, uint32_t bit, bool vorhanden) {
        return roh && roh < s.roh.size() && ((st.svgEigen & bit) || (wurzel && !vorhanden));
    };
    if (regel(st.svgFuellung, 1, hatFarbe[0])) { farbWert[0] = s.roh[st.svgFuellung]; hatFarbe[0] = true; }
    if (regel(st.svgStrich, 2, hatFarbe[1])) { farbWert[1] = s.roh[st.svgStrich]; hatFarbe[1] = true; }
    if (regel(st.svgStrichBreite, 4, hatBreite)) { breite = s.roh[st.svgStrichBreite]; hatBreite = true; }
    if (hatBreite) attribut("stroke-width", breite, aus);
    for (size_t i = 0; i < std::size(kSvgFarbe); ++i) {
        float faktor = 1;
        if (hatFarbe[i]) {
            uint32_t c;
            if (stil::farbe(getrimmt(farbWert[i]), st.farbe, c)) {
                if (druck) c = dunkler(c);
                char hex[8];
                std::snprintf(hex, sizeof hex, "#%06x", c & 0xFFFFFFu);
                attribut(kSvgFarbe[i], hex, aus);
                faktor = float(c >> 24) / 255.f;
            } else {
                attribut(kSvgFarbe[i], farbWert[i], aus);
            }
        }
        if (i >= std::size(kSvgDeckkraft) || (!hatDeck[i] && faktor >= 1)) continue;
        if (faktor >= 1) { attribut(kSvgDeckkraft[i], deck[i], aus); continue; }
        float alt = 1;
        if (hatDeck[i]) {
            const std::string_view v = getrimmt(deck[i]);
            const bool prozent = !v.empty() && v.back() == '%';
            const std::string_view zahl = prozent ? v.substr(0, v.size() - 1) : v;
            if (std::from_chars(zahl.data(), zahl.data() + zahl.size(), alt).ec != std::errc()) alt = 1;
            if (prozent) alt /= 100;
        }
        //  to_chars statt printf: das Gebietsschema der App schriebe ein Komma, QtSvg laese 0.
        char wert[24];
        const auto r = std::to_chars(wert, wert + sizeof wert, std::clamp(alt, 0.f, 1.f) * faktor, std::chars_format::general, 4);
        attribut(kSvgDeckkraft[i], std::string_view(wert, size_t(r.ptr - wert)), aus);
    }
    if (!stilRest.empty()) attribut("style", stilRest, aus);
    if (wurzel && !ns) aus += " xmlns=\"http://www.w3.org/2000/svg\"";
    if (wurzel && !xlink) aus += " xmlns:xlink=\"http://www.w3.org/1999/xlink\"";
    aus += '>';
    for (uint32_t c = k.erstesKind; c != kKein; c = d.knoten[c].naechstes) {
        const Knoten& ck = d.knoten[c];
        if (ck.art == Art::Text) xmlText(d.textVon(ck), aus);
        else if (ck.art == Art::Element && ck.raum == Raum::Svg) schreibeSvg(d, s, c, false, druck, aus);
    }
    aus += "</";
    aus += name;
    aus += '>';
}

//  Wirkende Dekoration: CSS vererbt sie nicht, sie gilt aber fuer den ganzen Inline-Inhalt, je Linie in der Farbe
//  des Kastens, der sie setzt.
struct Deko {
    uint8_t bits = 0;
    uint32_t farbe[3] = {};     // unter, ueber, durch
};

class Baumeister {
public:
    Baumeister(const Dokument& d, const Stile& s, const Kastenbaum& k, const Layout& l, Anzeigeliste& a, bool druck)
        : m_d(d), m_s(s), m_k(k), m_l(l), m_a(a), m_druck(druck) {
        const size_t n = k.kaesten.size();
        m_absatzVon.assign(n, kKein);
        for (uint32_t i = 0; i < l.absaetze.size(); ++i) m_absatzVon[l.absaetze[i].kasten] = i;
        m_markeLauf.assign(n, kKein);
        for (uint32_t i = uint32_t(l.laeufe.size()); i-- > 0 && l.laeufe[i].zeile == kKein;)
            if (l.laeufe[i].kasten < n) m_markeLauf[l.laeufe[i].kasten] = i;
        const auto kastenVon = [&](uint32_t knoten) { return knoten < k.kastenVon.size() ? k.kastenVon[knoten] : kKein; };
        m_html = kastenVon(d.html);
        m_body = kastenVon(d.body);
        m_alpha.assign(n, 255);
        m_deko.assign(n, 0);
        m_verweis.assign(n, 0);
        if (const uint32_t href = d.nameSuchen("href"); href != kKein)
            for (uint32_t knoten = 0; knoten < d.knoten.size(); ++knoten) {
                bool da = false;
                if (d.knoten[knoten].art != Art::Element || kastenVon(knoten) >= n) continue;
                d.attribut(knoten, href, &da);
                if (da && verweisUm(d, knoten) == knoten) m_verweis[kastenVon(knoten)] = 1;
            }
        m_dekos.emplace_back();
        if (n == 0) return;
        //  Leinwand: der Hintergrund von html, ohne ihn der von body; dieser Kasten malt ihn dann nicht selbst.
        for (const uint32_t c : { m_html, m_body })
            if (c != kKein && (stil(c).hintergrund >> 24) != 0) {
                m_a.leinwand = stil(c).hintergrund;
                m_leinwandKasten = c;
                break;
            }
        std::vector<uint32_t> stapel{ 0 };
        while (!stapel.empty()) {
            const uint32_t c = stapel.back();
            stapel.pop_back();
            const Kasten& C = kasten(c);
            const bool eigen = !(C.flags & KAnonym) && (C.art == KArt::Block || C.art == KArt::Inline
                                                        || C.art == KArt::Atom);
            const uint32_t e = C.eltern;
            uint32_t alpha = e != kKein ? m_alpha[e] : 255;
            if (eigen) alpha = alpha * stil(c).deckkraft / 255;
            m_alpha[c] = uint8_t(alpha);
            uint32_t deko = (e == kKein || C.art == KArt::Atom || (C.flags & KAusserFluss)) ? 0 : m_deko[e];
            if (eigen && stil(c).dekoration) {
                Deko neu = m_dekos[deko];
                const uint32_t f = stil(c).farbe;
                if (stil(c).dekoration & DekoUnter) neu.farbe[0] = f;
                if (stil(c).dekoration & DekoUeber) neu.farbe[1] = f;
                if (stil(c).dekoration & DekoDurch) neu.farbe[2] = f;
                neu.bits |= stil(c).dekoration;
                m_dekos.push_back(neu);
                deko = uint32_t(m_dekos.size() - 1);
            }
            m_deko[c] = deko;
            for (uint32_t x = C.erstesKind; x != kKein; x = kasten(x).naechstes) stapel.push_back(x);
        }
    }

    void baue() {
        if (!m_k.kaesten.empty()) schicht(0);
    }

private:
    const Kasten& kasten(uint32_t k) const { return m_k.kaesten[k]; }
    const Stil& stil(uint32_t k) const { return m_s.stile[m_k.kaesten[k].stil]; }
    bool anonym(uint32_t k) const { return kasten(k).flags & KAnonym; }

    //  Eigene Schicht: positioniert, transformiert oder durchscheinend.
    bool positioniert(uint32_t k) const {
        if (anonym(k)) return false;
        const Stil& s = stil(k);
        return s.position != Lage::Static || s.transformation != 0 || s.deckkraft < 255;
    }

    bool clippt(uint32_t k) const {
        if (anonym(k) || k == m_html || k == m_body) return false;
        const KArt a = kasten(k).art;
        return (a == KArt::Block || a == KArt::Atom)
            && (stil(k).ueberlaufX != Ueberlauf::Visible || stil(k).ueberlaufY != Ueberlauf::Visible);
    }

    void befehl(BArt art, const Rechteck& r, uint32_t farbe = 0, uint32_t daten = kKein) {
        m_a.befehle.push_back(Befehl{ r, farbe, daten, art });
    }

    //  Radien nach CSS: Prozent gegen Breite bzw. Hoehe, ueberlappende Ecken gemeinsam gekuerzt.
    void radien(uint32_t k, const Rechteck& r, Rahmen& ra, bool links = true, bool rechts = true) const {
        const Stil& s = stil(k);
        bool da = false;
        for (int i = 0; i < 4; ++i) {
            const Laenge& l = s.radius[i];
            ra.rx[i] = std::max(0.f, (l.px + l.prozent * r.b / 100.f) / kQ);
            ra.ry[i] = std::max(0.f, (l.px + l.prozent * r.h / 100.f) / kQ);
            da |= ra.rx[i] > 0 && ra.ry[i] > 0;
        }
        if (!links) ra.rx[0] = ra.ry[0] = ra.rx[3] = ra.ry[3] = 0;
        if (!rechts) ra.rx[1] = ra.ry[1] = ra.rx[2] = ra.ry[2] = 0;
        if (!da) return;
        float f = 1;
        const auto passe = [&](float seite, float a, float b) { if (a + b > seite) f = std::min(f, seite / (a + b)); };
        passe(r.b, ra.rx[0], ra.rx[1]);
        passe(r.b, ra.rx[3], ra.rx[2]);
        passe(r.h, ra.ry[0], ra.ry[3]);
        passe(r.h, ra.ry[1], ra.ry[2]);
        for (int i = 0; i < 4; ++i) { ra.rx[i] *= f; ra.ry[i] *= f; }
    }

    //  Rahmen aus dem Tabellen-Layout (Zeilen, Gruppen, Zellen bei border-collapse), sonst nullptr.
    const LRahmen* tabRahmen(uint32_t k) const {
        return m_l.rahmenVon.empty() || m_l.rahmenVon[k] == kKein ? nullptr : &m_l.rahmen[m_l.rahmenVon[k]];
    }

    //  Breite bis zum Innenabstandskasten.
    float innenRand(uint32_t k, int i) const {
        if (const LRahmen* lr = tabRahmen(k)) return lr->halb[i];
        const Stil& s = stil(k);
        return sichtbar(s.rahmenStil[i]) ? px(s.rahmenBreite[i]) : 0.f;
    }

    //  Hintergrund und Rahmen; bei Inline-Fragmenten fehlen die Seiten, an denen die Zeile umbricht.
    void flaeche(uint32_t k, const Rechteck& r, bool links = true, bool rechts = true) {
        if (stil(k).sichtbarkeit != Sichtbarkeit::Visible || r.b <= 0 || r.h <= 0) return;
        const Stil& s = stil(k);
        const uint8_t alpha = m_alpha[k];
        const LRahmen* lr = tabRahmen(k);
        Rahmen ra;
        if (!lr) radien(k, r, ra, links, rechts);
        bool rund = false;
        for (int i = 0; i < 4; ++i) rund |= ra.rx[i] > 0 && ra.ry[i] > 0;
        bool rahmen = false;
        Rechteck rr = r;
        for (int i = 0; i < 4; ++i) {
            if (lr) {
                if (lr->breite[i] <= 0 || !sichtbar(lr->stil[i])) continue;
                ra.breite[i] = lr->breite[i];
                ra.farbe[i] = mitDeckkraft(lr->farbe[i], alpha);
                ra.stil[i] = lr->stil[i];
            } else {
                if ((i == 3 && !links) || (i == 1 && !rechts) || !sichtbar(s.rahmenStil[i])) continue;
                ra.breite[i] = px(s.rahmenBreite[i]);
                ra.farbe[i] = mitDeckkraft(s.rahmenFarbe[i], alpha);
                ra.stil[i] = s.rahmenStil[i];
            }
            rahmen |= ra.breite[i] > 0 && (ra.farbe[i] >> 24) != 0;
        }
        //  border-collapse: die gemalte Linie ragt um ihre aeussere Haelfte ueber den Randkasten hinaus.
        if (lr) {
            const float o = ra.breite[0] - lr->halb[0], re = ra.breite[1] - lr->halb[1];
            const float u = ra.breite[2] - lr->halb[2], li = ra.breite[3] - lr->halb[3];
            rr = Rechteck{ r.x - (ra.breite[3] > 0 ? li : 0), r.y - (ra.breite[0] > 0 ? o : 0), 0, 0 };
            rr.b = r.x + r.b + (ra.breite[1] > 0 ? re : 0) - rr.x;
            rr.h = r.y + r.h + (ra.breite[2] > 0 ? u : 0) - rr.y;
        }
        //  Mit background-clip: text sind die Hintergruende Schrift: der Druckstil behaelt sie.
        const bool maske = s.hintergrundText != 0;
        const bool malen = !maske || textMaske(k, r);
        if (malen && k != m_leinwandKasten && (s.hintergrund >> 24) != 0) {
            uint32_t idx = kKein;
            if (rund) {
                m_a.rahmen.push_back(ra);
                idx = uint32_t(m_a.rahmen.size() - 1);
            }
            befehl(BArt::Flaeche, r, mitDeckkraft(s.hintergrund, alpha), idx);
            m_a.befehle.back().hinten = !maske;
        }
        if (malen && s.hintergrundBild != 0 && s.hintergrundBild < m_s.roh.size()) {
            const size_t vorher = m_a.befehle.size();
            bilder(k, r, ra, rund, alpha);
            for (size_t i = vorher; i < m_a.befehle.size(); ++i) m_a.befehle[i].hinten = !maske;
        }
        if (maske && malen) befehl(BArt::ClipAus, {});
        if (rahmen) {
            m_a.rahmen.push_back(ra);
            befehl(BArt::Rahmen, rr, 0, uint32_t(m_a.rahmen.size() - 1));
        }
    }

    //  Die Laeufe des Kastens samt Nachfahren; ohne sie bleibt der Hintergrund ganz weg wie im Browser.
    bool textMaske(uint32_t k, const Rechteck& r) {
        std::vector<uint8_t> drin(m_k.kaesten.size(), 0);
        std::vector<uint32_t> stapel{ k };
        while (!stapel.empty()) {
            const uint32_t c = stapel.back();
            stapel.pop_back();
            drin[c] = 1;
            for (uint32_t x = kasten(c).erstesKind; x != kKein; x = kasten(x).naechstes) stapel.push_back(x);
        }
        const size_t anfang = m_a.textMasken.size();
        m_a.textMasken.push_back(0);
        for (uint32_t i = 0; i < m_l.laeufe.size(); ++i)
            if (const Lauf& l = m_l.laeufe[i]; l.anzahl > 0 && l.kasten < drin.size() && drin[l.kasten])
                m_a.textMasken.push_back(i);
        m_a.textMasken[anfang] = uint32_t(m_a.textMasken.size() - anfang - 1);
        if (m_a.textMasken[anfang] == 0) {
            m_a.textMasken.resize(anfang);
            return false;
        }
        befehl(BArt::ClipText, r, 0, uint32_t(anfang));
        return true;
    }

    //  Schichten von background-image, die letzte zuunterst; Kachel und Lage gegen den Innenabstandskasten.
    void bilder(uint32_t k, const Rechteck& r, const Rahmen& ra, bool rund, uint8_t alpha) {
        const Stil& s = stil(k);
        const float em = px(s.schriftGroesse);
        const auto b = [&](int i) { return innenRand(k, i); };
        const Rechteck innen{ r.x + b(3), r.y + b(0), std::max(0.f, r.b - b(1) - b(3)), std::max(0.f, r.h - b(0) - b(2)) };
        uint32_t radien = kKein;
        const std::vector<std::string_view> schichten = teile(m_s.roh[s.hintergrundBild], ',');
        const auto lang = [&](uint32_t feld) {
            return feld != 0 && feld < m_s.roh.size() ? teile(m_s.roh[feld], ',') : std::vector<std::string_view>();
        };
        const std::vector<std::string_view> langGroesse = lang(s.hintergrundGroesse), langLage = lang(s.hintergrundLage),
                                            langWdh = lang(s.hintergrundWdh), langFest = lang(s.hintergrundFest);
        //  fixed liegt am Anfangsblock statt am Kasten; gemalt wird weiter nur im Kasten.
        const Rechteck fenster{ 0, 0, m_l.breite, m_l.fensterHoehe };
        for (size_t i = schichten.size(); i-- > 0;) {
            std::vector<std::string_view> w;
            for (std::string_view x : teile(schichten[i], ' ')) {
                int tiefe = 0;
                size_t a = 0;
                for (size_t j = 0; j <= x.size(); ++j) {
                    const char c = j < x.size() ? x[j] : '/';
                    if (c == '(') ++tiefe;
                    else if (c == ')') --tiefe;
                    else if (c == '/' && tiefe == 0) {
                        if (j > a) w.push_back(x.substr(a, j - a));
                        if (j < x.size()) w.push_back("/");
                        a = j + 1;
                    }
                }
            }
            std::string_view name, arg;
            std::vector<std::string_view> lage, groesse;
            bool nachStrich = false, wx = true, wy = true, bild = false, fest = false;
            for (std::string_view x : w) {
                if (x == "/") nachStrich = true;
                else if (funktion(x, name, arg)) bild = true;
                else if (gleich(x, "fixed")) fest = true;
                else if (gleich(x, "scroll") || gleich(x, "local")) {}
                else if (gleich(x, "no-repeat")) wx = wy = false;
                else if (gleich(x, "repeat-x")) wy = false;
                else if (gleich(x, "repeat-y")) wx = false;
                else if (gleich(x, "repeat") || gleich(x, "space") || gleich(x, "round")) {}
                else (nachStrich ? groesse : lage).push_back(x);
            }
            if (!bild) continue;
            //  Fehlen einer Langform Schichten, wiederholt sie ihre Liste wie im Browser.
            if (!langGroesse.empty()) groesse = teile(langGroesse[i % langGroesse.size()], ' ');
            if (!langLage.empty()) lage = teile(langLage[i % langLage.size()], ' ');
            if (!langFest.empty()) fest = gleich(getrimmt(langFest[i % langFest.size()]), "fixed");
            const Rechteck& feld = fest ? fenster : innen;
            if (!langWdh.empty()) {
                const std::vector<std::string_view> r = teile(langWdh[i % langWdh.size()], ' ');
                const bool zwei = r.size() > 1;
                wx = !r.empty() && !gleich(r[0], "no-repeat") && !gleich(r[0], "repeat-y");
                wy = !r.empty() && !gleich(r[zwei ? 1 : 0], "no-repeat") && (zwei || !gleich(r[0], "repeat-x"));
            }
            if (gleich(name, "url")) {
                const Geladen& g = laden(arg);
                if (!g.svg && g.raster.isNull()) continue;
                float kb, kh, kx, ky;
                bildKachel(groesse, feld, em, g.masse, kb, kh);
                if (!(kb > 0 && kh > 0)) continue;
                lagePaar(lage, feld.b - kb, feld.h - kh, em, kx, ky);
                if (lage.empty()) kx = ky = 0;
                if (rund && radien == kKein) {
                    m_a.rahmen.push_back(ra);
                    radien = uint32_t(m_a.rahmen.size() - 1);
                }
                m_a.bilder.push_back(Bild{ g.svg, g.raster, Rechteck{ feld.x + kx, feld.y + ky, kb, kh }, radien, alpha, wx, wy,
                                           fest });
                befehl(BArt::Bild, r, 0, uint32_t(m_a.bilder.size() - 1));
                continue;
            }
            float kb = feld.b, kh = feld.h;
            if (!groesse.empty() && !gleich(groesse[0], "auto") && !gleich(groesse[0], "cover")
                && !gleich(groesse[0], "contain")) {
                laenge(groesse[0], feld.b, em, kb);
                if (groesse.size() > 1 && !gleich(groesse[1], "auto")) laenge(groesse[1], feld.h, em, kh);
            }
            if (kb <= 0 || kh <= 0) continue;
            float kx, ky;
            lagePaar(lage, feld.b - kb, feld.h - kh, em, kx, ky);
            if (lage.empty()) kx = ky = 0;
            Verlauf v;
            v.kachel = Rechteck{ feld.x + kx, feld.y + ky, kb, kh };
            v.wiederholenX = wx;
            v.wiederholenY = wy;
            v.fest = fest;
            const size_t halteVorher = m_a.halte.size();
            if (!verlauf(name, arg, kb, kh, em, s.farbe, v, m_a.halte)) { m_a.halte.resize(halteVorher); continue; }
            for (uint32_t h = v.haltAnfang; h < v.haltAnfang + v.haltAnzahl; ++h)
                m_a.halte[h].farbe = mitDeckkraft(m_a.halte[h].farbe, alpha);
            if (rund && radien == kKein) {
                m_a.rahmen.push_back(ra);
                radien = uint32_t(m_a.rahmen.size() - 1);
            }
            v.rahmen = radien;
            m_a.verlaeufe.push_back(v);
            befehl(BArt::Verlauf, r, 0, uint32_t(m_a.verlaeufe.size() - 1));
        }
    }

    //  Die Tabelle eines Teils und ob sie getrennte Rahmen mit Abstand hat (dort scheint sie zwischen den Zellen durch).
    uint32_t tabelleVon(uint32_t k) const {
        for (uint32_t t = kasten(k).eltern; t < m_k.kaesten.size(); t = kasten(t).eltern)
            if (kasten(t).rolle == TTabelle) return t;
        return kKein;
    }
    bool getrennt(uint32_t t) const {
        return t != kKein && !stil(t).rahmenZusammen && (stil(t).rahmenAbstand[0] > 0 || stil(t).rahmenAbstand[1] > 0);
    }
    //  empty-cells hide: eine leere Zelle malt weder Hintergrund noch Rahmen; den ihrer Zeile malt sie wie Chromium.
    bool versteckt(uint32_t z, uint32_t t) const {
        return t != kKein && !stil(t).rahmenZusammen && stil(z).leereZellen && (kasten(z).flags & KLeer);
    }

    //  Zeilen und Gruppen malen ihren Hintergrund nur unter ihren Zellen, gelegt gegen den eigenen Kasten (CSS 2.1, 17.5.1).
    void unterZellen(uint32_t k) {
        std::vector<uint32_t> stapel{ k };
        while (!stapel.empty()) {
            const uint32_t c = stapel.back();
            stapel.pop_back();
            for (uint32_t x = kasten(c).erstesKind; x != kKein; x = kasten(x).naechstes) {
                if (kasten(x).rolle == TZeile) { stapel.push_back(x); continue; }
                if (kasten(x).rolle != TZelle) continue;
                befehl(BArt::ClipAn, m_l.lage[x]);
                flaeche(k, m_l.lage[k]);
                befehl(BArt::ClipAus, {});
            }
        }
    }

    void hintergrund(uint32_t k) {
        const KArt a = kasten(k).art;
        if (anonym(k) || (a != KArt::Block && a != KArt::Atom)) return;
        const uint8_t rolle = kasten(k).rolle;
        const bool teil = rolle == TZeile || istGruppe(rolle);
        const uint32_t t = teil || rolle == TZelle ? tabelleVon(k) : kKein;
        if (rolle == TZelle && versteckt(k, t)) return;
        if (teil && getrennt(t) && ((stil(k).hintergrund >> 24) != 0 || stil(k).hintergrundBild != 0)) {
            unterZellen(k);
            return;
        }
        flaeche(k, m_l.lage[k]);
        verweis(k, m_l.lage[k]);
        if (kasten(k).flags & KErsetzt) {
            svg(k);
            bild(k);
        }
    }

    void verweis(uint32_t k, const Rechteck& r) {
        if (m_verweis[k] && stil(k).sichtbarkeit == Sichtbarkeit::Visible && m_alpha[k]) befehl(BArt::Verweis, r, 0, k);
    }

    //  Inline-SVG in den Inhaltskasten, Seitenverhaeltnis wie preserveAspectRatio xMidYMid meet.
    void svg(uint32_t k) {
        const uint32_t n = kasten(k).knoten;
        if (n >= m_d.knoten.size() || m_d.knoten[n].raum != Raum::Svg || m_d.knoten[n].name != NSvg
            || stil(k).sichtbarkeit != Sichtbarkeit::Visible)
            return;
        std::string xml;
        schreibeSvg(m_d, m_s, n, true, m_druck, xml);
        auto r = std::make_shared<QSvgRenderer>(QByteArray::fromStdString(xml));
        if (!r->isValid()) return;
        if (!gleich(wurzelAttribut(xml, "preserveAspectRatio"), "none")) r->setAspectRatioMode(Qt::KeepAspectRatio);
        const Stil& s = stil(k);
        const auto b = [&](int i) { return innenRand(k, i) + px(s.innen[i].px); };
        const Rechteck& g = m_l.lage[k];
        const Rechteck inhalt{ g.x + b(3), g.y + b(0), g.b - b(1) - b(3), g.h - b(0) - b(2) };
        if (!(inhalt.b > 0 && inhalt.h > 0)) return;
        m_a.bilder.push_back(Bild{ std::move(r), {}, inhalt, kKein, m_alpha[k], false, false });
        befehl(BArt::Bild, inhalt, 0, uint32_t(m_a.bilder.size() - 1));
    }

    //  Je URL einmal zerlegt: der Arabisch-Zettel nutzt dieselben Sterne in vielen Regeln.
    //  Hintergruende: die Kachelgroesse steht erst nach dem Laden fest, darum nur eine feste Obergrenze.
    const Geladen& laden(std::string_view url) {
        const auto [it, neu] = m_geladen.try_emplace(url);
        if (neu) it->second = ladeBild(url, m_d.basis, 4096);
        return it->second;
    }

    //  <img>: in den Inhaltskasten gestreckt; dekodiert hoechstens doppelt so gross wie der Kasten (HiDPI, Zoom).
    void bild(uint32_t k) {
        const uint32_t n = kasten(k).knoten;
        if (n >= m_d.knoten.size() || m_d.knoten[n].raum != Raum::Html || m_d.knoten[n].name != NImg
            || stil(k).sichtbarkeit != Sichtbarkeit::Visible)
            return;
        const Stil& s = stil(k);
        const auto b = [&](int i) { return innenRand(k, i) + px(s.innen[i].px); };
        const Rechteck& g = m_l.lage[k];
        const Rechteck inhalt{ g.x + b(3), g.y + b(0), g.b - b(1) - b(3), g.h - b(0) - b(2) };
        bool da = false;
        const std::string_view src = m_d.attribut(n, m_d.nameSuchen("src"), &da);
        if (!da || !(inhalt.b > 0 && inhalt.h > 0)) return;
        Geladen geladen = ladeBild(src, m_d.basis, int(std::ceil(2 * std::max(inhalt.b, inhalt.h))));
        if (!geladen.svg && geladen.raster.isNull()) return;
        m_a.bilder.push_back(Bild{ std::move(geladen.svg), std::move(geladen.raster), inhalt, kKein, m_alpha[k], false,
                                   false });
        befehl(BArt::Bild, inhalt, 0, uint32_t(m_a.bilder.size() - 1));
    }

    //  Rund wie der Innenabstandskasten (Radius minus Rahmen): eckige Kinder ragten sonst aus runden Karten.
    void clipAn(uint32_t k) {
        const Rechteck& r = m_l.lage[k];
        const auto b = [&](int i) { return innenRand(k, i); };
        uint32_t idx = kKein;
        if (!tabRahmen(k)) {
            Rahmen ra;
            radien(k, r, ra);
            const int sx[4] = { 3, 1, 1, 3 }, sy[4] = { 0, 0, 2, 2 };
            bool rund = false;
            for (int i = 0; i < 4; ++i) {
                ra.rx[i] = std::max(0.f, ra.rx[i] - b(sx[i]));
                ra.ry[i] = std::max(0.f, ra.ry[i] - b(sy[i]));
                rund |= ra.rx[i] > 0 && ra.ry[i] > 0;
            }
            if (rund) {
                m_a.rahmen.push_back(ra);
                idx = uint32_t(m_a.rahmen.size() - 1);
            }
        }
        befehl(BArt::ClipAn, Rechteck{ r.x + b(3), r.y + b(0), std::max(0.f, r.b - b(1) - b(3)),
                                       std::max(0.f, r.h - b(0) - b(2)) }, 0, idx);
    }

    void sammlePositionierte(uint32_t k, std::vector<uint32_t>& pos) const {
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes) {
            if (positioniert(c)) { pos.push_back(c); continue; }
            if (kasten(c).art == KArt::Atom) continue;
            sammlePositionierte(c, pos);
        }
    }

    //  Kinder im Fluss: erst alle Block-Hintergruende, dann der Inline-Inhalt (CSS 2.1, Anhang E, ohne Floats).
    void bloecke(uint32_t k) {
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes) {
            const KArt a = kasten(c).art;
            if (positioniert(c) || (a != KArt::Block && a != KArt::Inline)) continue;
            if (a == KArt::Block) hintergrund(c);
            const bool clip = clippt(c);
            if (clip) clipAn(c);
            bloecke(c);
            if (clip) befehl(BArt::ClipAus, {});
        }
    }

    void inhalt(uint32_t k) {
        marken(k);
        if (m_absatzVon[k] != kKein) absatz(m_l.absaetze[m_absatzVon[k]]);
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes) {
            if (positioniert(c) || kasten(c).art != KArt::Block) continue;
            const bool clip = clippt(c);
            if (clip) clipAn(c);
            inhalt(c);
            if (clip) befehl(BArt::ClipAus, {});
        }
    }

    void linien(const Lauf& l, uint8_t welche) {
        const Deko& d = m_dekos[m_deko[l.kasten]];
        if (!(d.bits & welche) || l.x2 <= l.x1) return;
        const SchriftMass& m = m_l.masse[l.schrift];
        const uint8_t alpha = m_alpha[l.kasten];
        if (welche & DekoUnter & d.bits)
            befehl(BArt::Linie, Rechteck{ l.x1, l.grundlinie + m.unterLage, l.x2 - l.x1, std::max(1.f, m.unterDicke) },
                   mitDeckkraft(d.farbe[0], alpha));
        if (welche & DekoUeber & d.bits)
            befehl(BArt::Linie, Rechteck{ l.x1, l.grundlinie - m.oben, l.x2 - l.x1, std::max(1.f, m.unterDicke) },
                   mitDeckkraft(d.farbe[1], alpha));
        if (welche & DekoDurch & d.bits)
            befehl(BArt::Linie, Rechteck{ l.x1, l.grundlinie + m.durchLage, l.x2 - l.x1, std::max(1.f, m.durchDicke) },
                   mitDeckkraft(d.farbe[2], alpha));
    }

    void text(uint32_t i) {
        const Lauf& l = m_l.laeufe[i];
        if (l.anzahl == 0 || stil(l.kasten).sichtbarkeit != Sichtbarkeit::Visible) return;
        const SchriftMass& m = m_l.masse[l.schrift];
        //  Glyphen ragen ueber den Vorschub hinaus (Kursive, Harakat): der Rand nur zum Aussortieren.
        const float rand = m.oben;
        befehl(BArt::Text, Rechteck{ l.x1 - rand, l.grundlinie - 2 * m.oben, l.x2 - l.x1 + 2 * rand, 3 * m.oben },
               mitDeckkraft(stil(l.kasten).farbe, m_alpha[l.kasten]), i);
    }

    void absatz(const Absatz& a) {
        for (uint32_t i = a.fragmentAnfang; i < a.fragmentAnfang + a.fragmentAnzahl; ++i) {
            const Fragment& f = m_l.fragmente[i];
            flaeche(f.kasten, f.r, f.links, f.rechts);
            verweis(f.kasten, f.r);
        }
        const uint32_t ende = a.laufAnfang + a.laufAnzahl;
        for (uint32_t i = a.laufAnfang; i < ende; ++i)
            if (stil(m_l.laeufe[i].kasten).sichtbarkeit == Sichtbarkeit::Visible)
                linien(m_l.laeufe[i], DekoUnter | DekoUeber);
        for (uint32_t i = a.laufAnfang; i < ende; ++i) text(i);
        for (const Stueck& st : a.stuecke)
            if (kasten(st.kasten).art == KArt::Atom && !positioniert(st.kasten)) schicht(st.kasten);
        for (uint32_t i = a.laufAnfang; i < ende; ++i)
            if (stil(m_l.laeufe[i].kasten).sichtbarkeit == Sichtbarkeit::Visible) linien(m_l.laeufe[i], DekoDurch);
    }

    //  Ein Kasten mit allem, was er umschliesst: positionierte Nachfahren nach z-index darueber, negative darunter.
    void schicht(uint32_t k) {
        //  Nur die aeusserste: eine fixed-Schicht in einer transformierten oder festen liegt an deren Kasten.
        const bool fest = m_festTiefe == 0 && !anonym(k) && stil(k).position == Lage::Fixed;
        const Klebend* kl = m_festTiefe == 0 && !fest ? klebend(k) : nullptr;
        //  Eigene Schicht der Ansicht nur ueber dem Fluss: die Textur liegt ueber allen Kacheln, ein fixed-Hintergrund
        //  mit negativem z-index deckte sonst die Seite zu.
        const bool eigen = (fest || kl) && (stil(k).zIndex == kZAuto || stil(k).zIndex >= 0);
        const size_t schichtIndex = m_a.schichten.size();
        if (fest || eigen) {
            if (fest) m_a.fest.push_back(uint32_t(m_a.transformationen.size()));
            m_a.transformationen.emplace_back();
            if (eigen) m_a.schichten.emplace_back().anfang = uint32_t(m_a.befehle.size());
            befehl(BArt::TransformAn, {}, 0, uint32_t(m_a.transformationen.size() - 1));
        }
        QTransform tr;
        const bool transformiert = !anonym(k) && stil(k).transformation != 0
            && stil(k).transformation < m_s.roh.size()
            && transformation(m_s.roh[stil(k).transformation], m_l.lage[k], px(stil(k).schriftGroesse), tr);
        if (transformiert) {
            m_a.transformationen.push_back(tr);
            befehl(BArt::TransformAn, {}, 0, uint32_t(m_a.transformationen.size() - 1));
        }
        m_festTiefe += fest || eigen || transformiert;
        std::vector<uint32_t> pos;
        sammlePositionierte(k, pos);
        const auto z = [&](uint32_t c) { const int32_t v = stil(c).zIndex; return v == kZAuto ? 0 : v; };
        std::stable_sort(pos.begin(), pos.end(), [&](uint32_t a, uint32_t b) { return z(a) < z(b); });
        hintergrund(k);
        size_t i = 0;
        for (; i < pos.size() && z(pos[i]) < 0; ++i) schicht(pos[i]);
        const bool clip = clippt(k);
        if (clip) clipAn(k);
        bloecke(k);
        inhalt(k);
        if (clip) befehl(BArt::ClipAus, {});
        for (; i < pos.size(); ++i) schicht(pos[i]);
        m_festTiefe -= fest || eigen || transformiert;
        if (transformiert) befehl(BArt::TransformAus, {});
        if (fest || eigen) befehl(BArt::TransformAus, {});
        if (eigen) schliesse(m_a.schichten[schichtIndex], k, kl);
    }

    //  Sticky klebt nur am Fenster: unter einem clippenden Vorfahren rollte es im Browser mit dessen Inhalt.
    const Klebend* klebend(uint32_t k) const {
        if (anonym(k) || stil(k).position != Lage::Sticky) return nullptr;
        const auto it = std::lower_bound(m_l.klebend.begin(), m_l.klebend.end(), k,
                                         [](const Klebend& e, uint32_t x) { return e.kasten < x; });
        if (it == m_l.klebend.end() || it->kasten != k) return nullptr;
        for (uint32_t a = kasten(k).eltern; a != kKein; a = kasten(a).eltern)
            if (clippt(a)) return nullptr;
        return &*it;
    }

    void schliesse(Schicht& s, uint32_t k, const Klebend* kl) {
        s.ende = uint32_t(m_a.befehle.size());
        s.knoten = kasten(k).knoten;
        s.kasten = m_l.lage[k];
        //  Befehle unter einem transform stehen in dessen Koordinaten: eine nach rechts hinausgeschobene Leiste ist unsichtbar.
        std::vector<QTransform> tr{ QTransform() };
        float x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
        bool malt = false;
        for (uint32_t n = s.anfang + 1; n + 1 < s.ende; ++n) {
            const Befehl& b = m_a.befehle[n];
            if (b.art == BArt::TransformAn) { tr.push_back(m_a.transformationen[b.daten] * tr.back()); continue; }
            if (b.art == BArt::TransformAus) { if (tr.size() > 1) tr.pop_back(); continue; }
            if (b.art == BArt::ClipAn || b.art == BArt::ClipAus || b.art == BArt::Verweis || b.r.b <= 0 || b.r.h <= 0) continue;
            malt = true;
            const QRectF q = tr.back().mapRect(QRectF(b.r.x, b.r.y, b.r.b, b.r.h));
            x0 = std::min(x0, float(q.left()));
            y0 = std::min(y0, float(q.top()));
            x1 = std::max(x1, float(q.right()));
            y1 = std::max(y1, float(q.bottom()));
        }
        s.r = Rechteck{ x0, y0, x1 - x0, y1 - y0 };
        //  Unsichtbar (zu, ausgeblendet, ganz neben der Seite) braucht keine Textur. Eine sehr grosse ist zu teuer: sie
        //  bleibt in den Kacheln und rollt mit. Feste Grenze, die Fensterhoehe des Legens ist beim Oeffnen oft noch 0.
        if (!malt || s.r.x >= m_l.breite || s.r.x + s.r.b <= 0 || s.r.h > kSchichtMax || s.r.b > 2 * std::max(m_l.breite, 1.f)) {
            m_a.schichten.pop_back();
            return;
        }
        s.fest = !kl;
        if (kl) {
            s.oben = kl->oben;
            s.unten = kl->unten;
            s.grenzeOben = kl->grenzeOben;
            s.grenzeUnten = kl->grenzeUnten;
        }
    }

    //  Listenmarken liegen ausserhalb der Zeilen; ihre Laeufe stehen je Marke zusammen am Ende von Layout::laeufe.
    void marken(uint32_t k) {
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes)
            if (kasten(c).art == KArt::Marke)
                for (uint32_t i = m_markeLauf[c]; i < m_l.laeufe.size() && m_l.laeufe[i].kasten == c
                     && m_l.laeufe[i].zeile == kKein; ++i)
                    text(i);
    }

    const Dokument& m_d;
    const Stile& m_s;
    const Kastenbaum& m_k;
    const Layout& m_l;
    Anzeigeliste& m_a;
    std::vector<uint32_t> m_absatzVon, m_deko, m_markeLauf;
    std::vector<uint8_t> m_alpha, m_verweis;
    std::vector<Deko> m_dekos;
    uint32_t m_html = kKein, m_body = kKein, m_leinwandKasten = kKein;
    int m_festTiefe = 0;
    bool m_druck = false;
    std::unordered_map<std::string_view, Geladen> m_geladen;
};

QColor farbe(uint32_t argb) { return QColor::fromRgba(argb); }

QPainterPath rundesRechteck(const QRectF& r, const float rx[4], const float ry[4]) {
    QPainterPath p;
    const qreal l = r.left(), t = r.top(), rr = r.right(), b = r.bottom();
    p.moveTo(l + rx[0], t);
    p.lineTo(rr - rx[1], t);
    if (rx[1] > 0 && ry[1] > 0) p.arcTo(rr - 2 * rx[1], t, 2 * rx[1], 2 * ry[1], 90, -90);
    p.lineTo(rr, b - ry[2]);
    if (rx[2] > 0 && ry[2] > 0) p.arcTo(rr - 2 * rx[2], b - 2 * ry[2], 2 * rx[2], 2 * ry[2], 0, -90);
    p.lineTo(l + rx[3], b);
    if (rx[3] > 0 && ry[3] > 0) p.arcTo(l, b - 2 * ry[3], 2 * rx[3], 2 * ry[3], 270, -90);
    p.lineTo(l, t + ry[0]);
    if (rx[0] > 0 && ry[0] > 0) p.arcTo(l, t, 2 * rx[0], 2 * ry[0], 180, -90);
    p.closeSubpath();
    return p;
}

//  Kanten auf Geraetepixel: mit Kantenglaettung lag ein 1-px-Rahmen auf x,5 halb in zwei Spalten, doppelt so breit
//  und blass, und welche Kante es traf, wechselte mit jeder Fensterbreite. Gedreht oder geschert bleibt alles, wie es ist.
QRectF einrasten(const QTransform& t, const QRectF& r) {
    if (t.type() > QTransform::TxScale || r.isEmpty()) return r;
    const QRectF g = t.mapRect(r);
    const qreal l = std::round(g.left()), o = std::round(g.top());
    const qreal re = std::max(l + 1, std::round(g.right())), u = std::max(o + 1, std::round(g.bottom()));
    return t.inverted().mapRect(QRectF(QPointF(l, o), QPointF(re, u)));
}

//  Wie CSS: Rahmenbreiten auf ganze Geraetepixel abgerundet, ein duennerer Rahmen bleibt ein Pixel.
void rasteBreiten(const QTransform& t, Rahmen& ra) {
    if (t.type() > QTransform::TxScale) return;
    const qreal sx = std::abs(t.m11()), sy = std::abs(t.m22());
    for (int i = 0; i < 4; ++i) {
        const qreal s = i % 2 ? sx : sy;
        if (ra.breite[i] > 0) ra.breite[i] = float(std::max(1.0, std::floor(ra.breite[i] * s + 1e-3)) / s);
    }
}

void maleRahmen(QPainter& p, const QRectF& r, const Rahmen& ra) {
    const float* w = ra.breite;
    bool gleich = true, rund = false;
    for (int i = 0; i < 4; ++i) {
        rund |= ra.rx[i] > 0 && ra.ry[i] > 0;
        if (w[i] > 0 && (ra.farbe[i] != ra.farbe[0] || ra.stil[i] != RahmenStil::Solid || w[0] <= 0)) gleich = false;
    }
    //  Ohne Radien je Seite ein Rechteck: Aussen- minus Innenform liess an Seiten ohne Breite zwei Kanten aufeinander,
    //  PDF-Betrachter zeichnen dort eine Haarlinie (neben runden Karten wie eine eckige Kante).
    if (gleich && !rund) {
        const QColor c = farbe(ra.farbe[0]);
        const qreal t = r.top() + w[0], h = r.height() - w[0] - w[2];
        p.fillRect(QRectF(r.left(), r.top(), r.width(), w[0]), c);
        if (w[2] > 0) p.fillRect(QRectF(r.left(), r.bottom() - w[2], r.width(), w[2]), c);
        if (h > 0 && w[3] > 0) p.fillRect(QRectF(r.left(), t, w[3], h), c);
        if (h > 0 && w[1] > 0) p.fillRect(QRectF(r.right() - w[1], t, w[1], h), c);
        return;
    }
    //  Gleichfarbig und durchgezogen: Aussen- minus Innenform, mit Radien die einzige saubere Form.
    if (gleich) {
        const QRectF innen = r.adjusted(w[3], w[0], -w[1], -w[2]);
        float ix[4], iy[4];
        const int sx[4] = { 3, 1, 1, 3 }, sy[4] = { 0, 0, 2, 2 };
        for (int i = 0; i < 4; ++i) {
            ix[i] = std::max(0.f, ra.rx[i] - w[sx[i]]);
            iy[i] = std::max(0.f, ra.ry[i] - w[sy[i]]);
        }
        QPainterPath pfad = rundesRechteck(r, ra.rx, ra.ry);
        if (innen.width() > 0 && innen.height() > 0) {
            //  Seite ohne Breite: dieselbe Haarlinie wie oben, das Abziehen verschmilzt die Kanten.
            if (w[1] <= 0 || w[2] <= 0 || w[3] <= 0) pfad = pfad.subtracted(rundesRechteck(innen, ix, iy));
            else pfad.addPath(rundesRechteck(innen, ix, iy));
        }
        pfad.setFillRule(Qt::OddEvenFill);
        p.fillPath(pfad, farbe(ra.farbe[0]));
        return;
    }
    const qreal l = r.left(), t = r.top(), rr = r.right(), b = r.bottom();
    const QPointF ecken[4][4] = {
        { { l, t }, { rr, t }, { rr - w[1], t + w[0] }, { l + w[3], t + w[0] } },
        { { rr, t }, { rr, b }, { rr - w[1], b - w[2] }, { rr - w[1], t + w[0] } },
        { { rr, b }, { l, b }, { l + w[3], b - w[2] }, { rr - w[1], b - w[2] } },
        { { l, b }, { l, t }, { l + w[3], t + w[0] }, { l + w[3], b - w[2] } },
    };
    for (int i = 0; i < 4; ++i) {
        if (w[i] <= 0 || (ra.farbe[i] >> 24) == 0) continue;
        const QColor c = farbe(ra.farbe[i]);
        if (ra.stil[i] == RahmenStil::Dashed || ra.stil[i] == RahmenStil::Dotted) {
            const QPointF a = (ecken[i][0] + ecken[i][3]) / 2, e = (ecken[i][1] + ecken[i][2]) / 2;
            QPen stift(c, w[i], ra.stil[i] == RahmenStil::Dotted ? Qt::DotLine : Qt::DashLine,
                       ra.stil[i] == RahmenStil::Dotted ? Qt::RoundCap : Qt::FlatCap);
            p.setPen(stift);
            p.setBrush(Qt::NoBrush);
            p.drawLine(a, e);
            continue;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        if (ra.stil[i] == RahmenStil::Double && w[i] >= 3) {
            //  Zwei Striche zu je einem Drittel: der aeussere und der innere Rand des Trapezes.
            const auto mitte = [&](const QPointF& a, const QPointF& b, qreal f) { return a + (b - a) * f; };
            const QPointF au[4] = { ecken[i][0], ecken[i][1], mitte(ecken[i][1], ecken[i][2], 1 / 3.),
                                    mitte(ecken[i][0], ecken[i][3], 1 / 3.) };
            const QPointF in[4] = { mitte(ecken[i][3], ecken[i][0], 1 / 3.), mitte(ecken[i][2], ecken[i][1], 1 / 3.),
                                    ecken[i][2], ecken[i][3] };
            p.drawPolygon(au, 4);
            p.drawPolygon(in, 4);
            continue;
        }
        p.drawPolygon(ecken[i], 4);
    }
}

void maleVerlauf(QPainter& p, const QRectF& r, const Anzeigeliste& a, const Verlauf& v, const Rechteck& w,
                 QImage* ganzeKachel) {
    QGradientStops halte;
    halte.reserve(qsizetype(v.haltAnzahl));
    for (uint32_t i = v.haltAnfang; i < v.haltAnfang + v.haltAnzahl; ++i) {
        //  QGradient verwirft gleiche Lagen; ein harter Uebergang braucht einen winzigen Abstand.
        qreal l = std::clamp(qreal(a.halte[i].lage), 0.0, 1.0);
        if (!halte.isEmpty() && l <= halte.last().first) l = std::min(1.0, halte.last().first + 1e-6);
        halte.append({ l, farbe(a.halte[i].farbe) });
    }
    if (halte.isEmpty()) return;
    //  Die Kachel rastet mit ein: ein Bruchteil-Rest der naechsten Kachel deckte sonst halb und liess eine helle Fuge.
    const QRectF k = einrasten(p.worldTransform(), QRectF(v.kachel.x, v.kachel.y, v.kachel.b, v.kachel.h));
    QBrush pinsel;
    if (v.radial) {
        QRadialGradient g(QPointF(v.x1, v.y1), v.x2);
        g.setStops(halte);
        pinsel = QBrush(g);
        pinsel.setTransform(QTransform::fromTranslate(-v.x1, -v.y1) * QTransform::fromScale(1, v.y2 / v.x2)
                            * QTransform::fromTranslate(v.x1 + k.x(), v.y1 + k.y()));
    } else {
        QLinearGradient g(QPointF(v.x1, v.y1), QPointF(v.x2, v.y2));
        g.setStops(halte);
        pinsel = QBrush(g);
        pinsel.setTransform(QTransform::fromTranslate(k.x(), k.y()));
    }
    p.save();
    if (v.rahmen != kKein) p.setClipPath(rundesRechteck(r, a.rahmen[v.rahmen].rx, a.rahmen[v.rahmen].ry), Qt::IntersectClip);
    //  Kacheln ueber die ganze Flaeche; ohne Wiederholung nur die eine.
    const QRectF ziel = r.intersected(QRectF(w.x, w.y, w.b, w.h));
    const auto spannen = [](qreal start, qreal groesse, qreal von, qreal bis, bool wieder, int& a, int& e) {
        if (!wieder) { a = 0; e = 1; return; }
        a = int(std::floor((von - start) / groesse));
        e = int(std::ceil((bis - start) / groesse));
    };
    int ax, ex, ay, ey;
    spannen(k.x(), k.width(), ziel.left(), ziel.right(), v.wiederholenX, ax, ex);
    spannen(k.y(), k.height(), ziel.top(), ziel.bottom(), v.wiederholenY, ay, ey);
    if (qint64(ex - ax) * (ey - ay) > 4096) { ex = ax + 1; ey = ay + 1; }
    //  PDFium zeigt die Deckkraft-Maske, die Qts PDF-Schreiber neben einen Verlauf legt, deckend (Alpha 4 wird weiss):
    //  durchscheinend geht er dort als Bild mit Alphakanal hinaus.
    const bool alsBild = ganzeKachel
        && std::any_of(halte.cbegin(), halte.cend(), [](const QGradientStop& h) { return h.second.alpha() < 255; });
    if (alsBild) p.setClipRect(ziel, Qt::IntersectClip);
    //  Weiche Verlaeufe brauchen kaum Bildpunkte: nur so fein, dass Nachbarn hoechstens eine Stufe trennt (vormultipliziert,
    //  samt Deckkraft) - ein Seitenhintergrund wog doppelt aufgeloest 400 KB. Harte Uebergaenge bleiben bei 2.
    qreal fein = 0.25;
    if (alsBild) {
        const qreal laenge = v.radial ? std::min(v.x2, v.y2) : std::hypot(v.x2 - v.x1, v.y2 - v.y1);
        for (qsizetype i = 1; i < halte.size(); ++i) {
            const QColor &c0 = halte[i - 1].second, &c1 = halte[i].second;
            const auto vm = [](const QColor& c, int k) {
                return c.alpha() * (k == 0 ? c.red() : k == 1 ? c.green() : c.blue()) / 255.0;
            };
            qreal stufen = 0;
            for (int k = 0; k < 3; ++k) stufen = std::max(stufen, std::abs(vm(c1, k) - vm(c0, k)));
            stufen += std::abs(c1.alpha() - c0.alpha());
            fein = std::max(fein, stufen / std::max(1e-3, (halte[i].first - halte[i - 1].first) * laenge));
        }
    }
    for (int j = ay; j < ey; ++j)
        for (int i = ax; i < ex; ++i) {
            const QPointF d(i * k.width(), j * k.height());
            QBrush b = pinsel;
            b.setTransform(pinsel.transform() * QTransform::fromTranslate(d.x(), d.y()));
            const QRectF t = k.translated(d);
            if (!alsBild) { p.fillRect(t.intersected(r), b); continue; }
            //  Ab dem ersten Halt der durchsichtigen Schlussfolge ist alles leer: das Bild bleibt auf diese Ellipse
            //  beschraenkt (der Trainer legt eine 1200x600 ueber das ganze, 31.000 px hohe Dokument, ein Punktraster
            //  „farbe 1px, transparent 1px" ohne Kachelgroesse sonst ein ganzseitiges Bild).
            QRectF e = t;
            if (v.radial && halte.last().second.alpha() == 0) {
                qreal f = 1;
                for (qsizetype h = halte.size(); h-- > 0 && halte[h].second.alpha() == 0;) f = halte[h].first;
                e &= QRectF(t.x() + v.x1 - f * v.x2, t.y() + v.y1 - f * v.y2, 2.0 * f * v.x2, 2.0 * f * v.y2);
            }
            if (!e.intersects(ziel)) continue;
            //  Bis 4 Mio. Pixel die ganze Kachel, beschnitten auf das Ziel: so teilen auch die Seiten einer fixed-Schicht
            //  ein Bild. Groessere (ein Verlauf ueber das ganze Dokument) nur im Stueck der Seite.
            const bool ganz = e.width() * e.height() <= 4e6;
            if (ganz && !ganzeKachel->isNull()) { p.drawImage(e, *ganzeKachel); continue; }
            const QRectF q = ganz ? e : e.intersected(ziel);
            const qreal s = std::min({ 2.0, fein, std::sqrt(4e6 / std::max(1.0, q.width() * q.height())) });
            QImage bild(QSize(int(std::ceil(q.width() * s)), int(std::ceil(q.height() * s))), QImage::Format_ARGB32_Premultiplied);
            bild.fill(Qt::transparent);
            QPainter m(&bild);
            //  Je Achse genau auf ganze Bildpunkte: eine 1 px hohe Linie deckte bei s = 0,3 nur ein Drittel und verblasste.
            m.scale(bild.width() / q.width(), bild.height() / q.height());
            m.translate(-q.x(), -q.y());
            m.fillRect(q, b);
            m.end();
            p.drawImage(q, bild);
            if (ganz) *ganzeKachel = std::move(bild);
        }
    p.restore();
}

//  Eine Kachel direkt; viele einmal in Geraetepixeln gerastert und als Muster - der Arabisch-Zettel legt Sterne
//  von 64 px ueber die ganze Seite, je Kachel neu gezeichnet waeren das ueber 80 SVG-Laeufe je Bildschirmstreifen.
//  Im PDF bleibt die gerasterte Kachel ueber die Seiten: dasselbe QImage ist dort ein Objekt statt eines je Seite.
void maleBild(QPainter& p, const QRectF& r, const Anzeigeliste& a, const Bild& b, const Rechteck& w, QImage* kachelBild,
              qint64 merkGrenze) {
    const QRectF k(b.kachel.x, b.kachel.y, b.kachel.b, b.kachel.h);
    const QRectF ziel = r.intersected(QRectF(w.x, w.y, w.b, w.h));
    if (k.isEmpty() || ziel.isEmpty()) return;
    const auto zeichne = [&](QPainter& q, const QRectF& in) {
        if (b.svg) {
            b.svg->render(&q, in);
        } else {
            q.setRenderHint(QPainter::SmoothPixmapTransform);
            q.drawImage(in, b.raster);
        }
    };
    p.save();
    p.setOpacity(p.opacity() * b.deckkraft / 255.0);
    if (b.rahmen != kKein) p.setClipPath(rundesRechteck(r, a.rahmen[b.rahmen].rx, a.rahmen[b.rahmen].ry), Qt::IntersectClip);
    p.setClipRect(r, Qt::IntersectClip);
    QRectF flaeche = ziel;
    if (!b.wiederholenX) flaeche &= QRectF(k.x(), flaeche.y(), k.width(), flaeche.height());
    if (!b.wiederholenY) flaeche &= QRectF(flaeche.x(), k.y(), flaeche.width(), k.height());
    const qreal s = std::sqrt(std::abs(p.worldTransform().determinant()));
    const QSize px(std::max(1, int(std::lround(k.width() * s))), std::max(1, int(std::lround(k.height() * s))));
    if (!b.wiederholenX && !b.wiederholenY) zeichne(p, k);
    else if (!flaeche.isEmpty() && qint64(px.width()) * px.height() <= (qint64(4096) << 10)) {
        QImage bild = kachelBild && kachelBild->size() == px ? *kachelBild : QImage();
        if (bild.isNull()) {
            bild = QImage(px, QImage::Format_ARGB32_Premultiplied);
            bild.fill(Qt::transparent);
            {
                QPainter q(&bild);
                q.setRenderHint(QPainter::SmoothPixmapTransform);
                zeichne(q, QRectF(0, 0, px.width(), px.height()));
            }
            if (kachelBild && qint64(px.width()) * px.height() <= merkGrenze) *kachelBild = bild;
        }
        QBrush muster(bild);
        muster.setTransform(QTransform::fromScale(k.width() / px.width(), k.height() / px.height())
                            * QTransform::fromTranslate(k.x(), k.y()));
        p.fillRect(flaeche, muster);
    }
    p.restore();
}

void maleBefehl(QPainter& p, const Befehl& b, const Anzeigeliste& a, const Layout& l, const Rechteck& w, PdfMalen* pdf,
                std::vector<QImage>* kacheln, QList<QPointF>& lagen, QGlyphRun& g) {
    const QRectF r(b.r.x, b.r.y, b.r.b, b.r.h);
    switch (b.art) {
    case BArt::Flaeche: {
        const QRectF e = einrasten(p.worldTransform(), r);
        if (b.daten == kKein) p.fillRect(e, farbe(b.farbe));
        else p.fillPath(rundesRechteck(e, a.rahmen[b.daten].rx, a.rahmen[b.daten].ry), farbe(b.farbe));
        break;
    }
    case BArt::Linie:
        p.fillRect(einrasten(p.worldTransform(), r), farbe(b.farbe));
        break;
    case BArt::Rahmen: {
        Rahmen ra = a.rahmen[b.daten];
        rasteBreiten(p.worldTransform(), ra);
        maleRahmen(p, einrasten(p.worldTransform(), r), ra);
        break;
    }
    case BArt::Verlauf: {
        //  Eingerastet wie Flaechen: nebeneinander liegende Verlaeufe (Tabellenkopf) liessen sonst helle Fugen.
        //  fixed setzt auf Papier je Seite neu an wie im Browser; die Kachel selbst bleibt dieselbe.
        Verlauf v = a.verlaeufe[b.daten];
        if (pdf && v.fest) v.kachel.y += pdf->blatt.y;
        maleVerlauf(p, einrasten(p.worldTransform(), r), a, v, w, pdf ? &pdf->verlaufBilder[b.daten] : nullptr);
        break;
    }
    case BArt::Bild:
        if (pdf && a.bilder[b.daten].fest) {
            Bild bild = a.bilder[b.daten];
            bild.kachel.y += pdf->blatt.y;
            maleBild(p, r, a, bild, w, &pdf->bildKacheln[b.daten], INT64_MAX);
        } else {
            //  Die Ansicht merkt nur kleine Kacheln (Muster); eine grosse kostete mehr RAM, als ihr Neuzeichnen Zeit.
            maleBild(p, r, a, a.bilder[b.daten], w, kacheln ? &(*kacheln)[b.daten] : nullptr,
                     pdf ? INT64_MAX : qint64(512) * 512);
        }
        break;
    case BArt::Text: {
        const Lauf& la = l.laeufe[b.daten];
        if (pdf && (la.grundlinie < w.y || la.grundlinie >= w.y + w.h)) break;
        lagen.resize(qsizetype(la.anzahl));
        for (uint32_t i = 0; i < la.anzahl; ++i)
            lagen[qsizetype(i)] = QPointF(l.glyphLagen[2 * (la.anfang + i)], l.glyphLagen[2 * (la.anfang + i) + 1]);
        g.setRawFont(l.schriften[la.schrift]);
        g.setRawData(l.glyphen.data() + la.anfang, lagen.constData(), int(la.anzahl));
        p.setPen(farbe(b.farbe));
        p.drawGlyphRun(QPointF(0, 0), g);
        break;
    }
    case BArt::Verweis:
        if (pdf) {
            const QRectF q = r.intersected(QRectF(w.x, w.y, w.b, w.h));
            if (!q.isEmpty()) pdf->verweise.push_back({ b.daten, p.worldTransform().mapRect(q) });
        }
        break;
    default: break;
    }
}

QPainterPath textPfad(const Anzeigeliste& a, const Layout& l, uint32_t anfang) {
    QPainterPath pfad;
    pfad.setFillRule(Qt::WindingFill);
    for (uint32_t j = anfang + 1; j <= anfang + a.textMasken[anfang]; ++j) {
        const Lauf& la = l.laeufe[a.textMasken[j]];
        const QRawFont& f = l.schriften[la.schrift];
        for (uint32_t i = la.anfang; i < la.anfang + la.anzahl; ++i)
            pfad.addPath(f.pathForGlyph(l.glyphen[i]).translated(l.glyphLagen[2 * i], l.glyphLagen[2 * i + 1]));
    }
    return pfad;
}

//  Am Bildschirm eine eigene Ebene statt des Clips: der Raster-Clip eines Pfads glaettet keine Kanten, die Schrift waere gezackt.
void maleMaske(QPainter& p, const Anzeigeliste& a, const Layout& l, const Befehl& m, size_t von, size_t bis,
               const Rechteck& w, QList<QPointF>& lagen, QGlyphRun& g) {
    const float x = std::max(w.x, m.r.x), y = std::max(w.y, m.r.y);
    const Rechteck f{ x, y, std::min(w.x + w.b, m.r.x + m.r.b) - x, std::min(w.y + w.h, m.r.y + m.r.h) - y };
    const QRect ziel = p.worldTransform().mapRect(QRectF(f.x, f.y, f.b, f.h)).toAlignedRect();
    if (f.b <= 0 || f.h <= 0 || ziel.isEmpty()) return;
    QImage ebene(ziel.size(), QImage::Format_ARGB32_Premultiplied);
    ebene.fill(Qt::transparent);
    QPainter q(&ebene);
    q.setRenderHint(QPainter::Antialiasing);
    q.setTransform(p.worldTransform() * QTransform::fromTranslate(-ziel.x(), -ziel.y()));
    for (size_t i = von; i < bis; ++i) maleBefehl(q, a.befehle[i], a, l, f, nullptr, nullptr, lagen, g);
    //  Als ganzes Bild: DestinationIn wirkt nur, wo gemalt wird - ein gefuellter Pfad liesse alles ausserhalb stehen.
    QImage maske(ziel.size(), QImage::Format_Alpha8);
    maske.fill(0);
    {
        QPainter mp(&maske);
        mp.setRenderHint(QPainter::Antialiasing);
        mp.setTransform(q.worldTransform());
        mp.fillPath(textPfad(a, l, m.daten), Qt::black);
    }
    q.resetTransform();
    q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    q.drawImage(0, 0, maske);
    q.end();
    p.save();
    p.resetTransform();
    p.drawImage(ziel.topLeft(), ebene);
    p.restore();
}

}   // namespace

size_t Anzeigeliste::bytes() const {
    return befehle.capacity() * sizeof(Befehl) + rahmen.capacity() * sizeof(Rahmen)
         + verlaeufe.capacity() * sizeof(Verlauf) + halte.capacity() * sizeof(Halt)
         + bilder.capacity() * sizeof(Bild) + transformationen.capacity() * sizeof(QTransform)
         + fest.capacity() * sizeof(uint32_t) + textMasken.capacity() * sizeof(uint32_t)
         + schichten.capacity() * sizeof(Schicht);
}

//  Wie im Browser: oben gewinnt vor unten, und der Kasten verlaesst den Inhaltskasten seines Bezugs nie.
float Schicht::versatz(float y0, float h) const {
    if (fest) return y0;
    const float hoch = kasten.h;
    if (!std::isnan(oben) && y0 + oben > kasten.y)
        return std::max(0.f, std::min(y0 + oben, grenzeUnten - hoch) - kasten.y);
    if (!std::isnan(unten) && y0 + h - unten < kasten.y + hoch)
        return std::min(0.f, std::max(y0 + h - unten - hoch, grenzeOben) - kasten.y);
    return 0;
}

Anzeigeliste baueAnzeige(const Dokument& d, const Stile& s, const Kastenbaum& k, const Layout& l, bool druck) {
    Anzeigeliste a;
    a.breite = l.breite;
    a.hoehe = l.hoehe;
    Baumeister(d, s, k, l, a, druck).baue();
    return a;
}

uint32_t dunkler(uint32_t argb) {
    const float r = float(argb >> 16 & 255), g = float(argb >> 8 & 255), b = float(argb & 255);
    const float y = (0.299f * r + 0.587f * g + 0.114f * b) / 255.f;
    if (y <= 0.5f) return argb;
    const float f = 0.4f / y;
    return (argb & 0xFF000000u) | uint32_t(std::lround(r * f)) << 16 | uint32_t(std::lround(g * f)) << 8
         | uint32_t(std::lround(b * f));
}

void male(QPainter& p, const Anzeigeliste& a, const Layout& l, const Rechteck& f, PdfMalen* pdf,
          std::vector<QImage>* kacheln, int32_t schicht) {
    //  Je Clip- und Transform-Ebene das Fenster in den Koordinaten der Befehle; leer, wenn nicht umkehrbar.
    std::vector<Rechteck> fenster{ f };
    const auto schneidet = [&](const Rechteck& r) {
        const Rechteck& w = fenster.back();
        return r.x < w.x + w.b && r.x + r.b > w.x && r.y < w.y + w.h && r.y + r.h > w.y;
    };
    const bool einzeln = schicht >= 0 && size_t(schicht) < a.schichten.size();
    if (!einzeln) {
        p.fillRect(QRectF(f.x, f.y, f.b, f.h), Qt::white);
        if (a.leinwand != 0xFFFFFFFFu) p.fillRect(QRectF(f.x, f.y, f.b, f.h), farbe(a.leinwand));
    }
    p.setRenderHint(QPainter::Antialiasing);
    if (pdf) {
        pdf->verlaufBilder.resize(a.verlaeufe.size());
        pdf->bildKacheln.resize(a.bilder.size());
        kacheln = &pdf->bildKacheln;
    }
    if (kacheln) kacheln->resize(a.bilder.size());
    QList<QPointF> lagen;
    QGlyphRun g;
    int ueber = 0;      // Tiefe in einem Clip ausserhalb des Fensters
    size_t naechste = 0;    // naechste Schicht, die die Kacheln auslassen
    const size_t von = einzeln ? a.schichten[size_t(schicht)].anfang : 0;
    const size_t bis = einzeln ? a.schichten[size_t(schicht)].ende : a.befehle.size();
    for (size_t n = von; n < bis; ++n) {
        if (schicht == kOhneSchichten) {
            while (naechste < a.schichten.size() && a.schichten[naechste].anfang < n) ++naechste;
            if (naechste < a.schichten.size() && a.schichten[naechste].anfang == n) {
                n = a.schichten[naechste++].ende - 1;
                continue;
            }
        }
        const Befehl& b = a.befehle[n];
        if (ueber > 0) {
            if (b.art == BArt::ClipAn || b.art == BArt::ClipText || b.art == BArt::TransformAn) ++ueber;
            else if (b.art == BArt::ClipAus || b.art == BArt::TransformAus) --ueber;
            continue;
        }
        if (b.art == BArt::ClipText && !pdf) {
            //  Darin stehen nur Hintergruende, ohne eigene Clips.
            size_t ende = n + 1;
            while (ende < a.befehle.size() && a.befehle[ende].art != BArt::ClipAus) ++ende;
            if (schneidet(b.r)) maleMaske(p, a, l, b, n + 1, ende, fenster.back(), lagen, g);
            n = ende;
            continue;
        }
        if (b.art == BArt::ClipAn || b.art == BArt::ClipText) {
            //  Nichts darin kann sichtbar werden; das PDF schrieb sonst je Seite die Clips des ganzen Dokuments.
            if (!schneidet(b.r)) { ueber = 1; continue; }
            p.save();
            const QRectF c(b.r.x, b.r.y, b.r.b, b.r.h);
            if (b.art == BArt::ClipText) p.setClipPath(textPfad(a, l, b.daten), Qt::IntersectClip);
            else if (b.daten == kKein) p.setClipRect(c, Qt::IntersectClip);
            else p.setClipPath(rundesRechteck(c, a.rahmen[b.daten].rx, a.rahmen[b.daten].ry), Qt::IntersectClip);
            const Rechteck& w = fenster.back();
            const float x = std::max(w.x, b.r.x), y = std::max(w.y, b.r.y);
            fenster.push_back(Rechteck{ x, y, std::min(w.x + w.b, b.r.x + b.r.b) - x, std::min(w.y + w.h, b.r.y + b.r.h) - y });
            continue;
        }
        if (b.art == BArt::TransformAn) {
            const QTransform& t = a.transformationen[b.daten];
            p.save();
            //  Auf Papier steht fixed auf jeder ganzen Seite: der Schnitt der Seite, oft ueber dem Blattende, beschnitt sonst
            //  eine Fussleiste. Fixed entkommt auch im Browser jedem Clip seiner Vorfahren.
            const bool blatt = pdf && pdf->blatt.b > 0 && std::find(a.fest.begin(), a.fest.end(), b.daten) != a.fest.end();
            if (blatt) p.setClipRect(QRectF(pdf->blatt.x, pdf->blatt.y, pdf->blatt.b, pdf->blatt.h), Qt::ReplaceClip);
            p.setTransform(t, true);
            bool umkehrbar = false;
            const QTransform inv = t.inverted(&umkehrbar);
            const Rechteck& w = blatt ? pdf->blatt : fenster.back();
            const QRectF q = umkehrbar ? inv.mapRect(QRectF(w.x, w.y, w.b, w.h)) : QRectF();
            fenster.push_back(umkehrbar ? Rechteck{ float(q.x()), float(q.y()), float(q.width()), float(q.height()) }
                                        : Rechteck{ 0, 0, -1, -1 });
            continue;
        }
        if (b.art == BArt::ClipAus || b.art == BArt::TransformAus) {
            p.restore();
            if (fenster.size() > 1) fenster.pop_back();
            continue;
        }
        if (schneidet(b.r)) maleBefehl(p, b, a, l, fenster.back(), pdf, kacheln, lagen, g);
    }
}

}   // namespace mg::html
