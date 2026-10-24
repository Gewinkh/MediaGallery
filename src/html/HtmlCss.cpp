#include "html/HtmlCss.h"

#include "html/HtmlStyle.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace mg::html {

namespace {

constexpr std::array<std::string_view, EVariable> kEigNamen = {
    "color", "font-family", "font-size", "font-weight", "font-style", "line-height", "letter-spacing", "text-align",
    "direction", "white-space", "text-transform", "visibility", "list-style-type", "list-style-position",
    "word-break", "border-collapse", "border-spacing", "caption-side", "fill", "stroke", "stroke-width", "empty-cells",
    "display", "position", "top", "right", "bottom", "left", "z-index", "width", "height", "min-width", "min-height",
    "max-width", "max-height", "box-sizing",
    "margin-top", "margin-right", "margin-bottom", "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
    "border-top-style", "border-right-style", "border-bottom-style", "border-left-style",
    "border-top-color", "border-right-color", "border-bottom-color", "border-left-color",
    "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius", "border-bottom-left-radius",
    "background-color", "background-image", "background-size", "background-position", "background-repeat", "opacity", "overflow-x", "overflow-y",
    "flex-direction", "flex-wrap", "flex-grow", "flex-shrink", "flex-basis", "justify-content", "align-items",
    "align-self", "justify-items", "justify-self", "row-gap", "column-gap", "grid-template-columns", "grid-column-start", "grid-column-end",
    "grid-row-start", "grid-row-end", "grid-template-rows", "grid-template-areas", "grid-auto-rows", "grid-auto-columns",
    "grid-auto-flow", "order", "align-content", "table-layout", "text-decoration-line",
    "vertical-align", "box-shadow", "transform", "content", "break-inside",
    "break-before", "break-after", "background-clip", "scroll-margin-top", "background-attachment",
    "margin", "padding", "border", "border-top", "border-right", "border-bottom", "border-left", "border-width",
    "border-style", "border-color", "border-radius", "background", "inset", "gap", "place-items", "flex",
    "flex-flow", "list-style", "overflow", "text-decoration", "font", "scroll-margin", "grid-column", "grid-row", "grid-area",
};

constexpr int kTiefe = 32;              // @media und :not/:is ineinander
constexpr size_t kStapel = 1024;        // offene Klammern beim Ueberspringen

bool leer(char c) { return c == ' ' || c == '\t' || c == '\n'; }
char klein(char c) { return c >= 'A' && c <= 'Z' ? char(c | 0x20) : c; }
bool nameAnfang(unsigned char c) { return (c | 0x20) >= 'a' && (c | 0x20) <= 'z' ? true : c == '_' || c >= 0x80; }
bool nameZeichen(unsigned char c) { return nameAnfang(c) || (c >= '0' && c <= '9') || c == '-'; }
bool hex(char c) { return (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f'); }

std::string_view trimme(std::string_view s) {
    while (!s.empty() && leer(s.front())) s.remove_prefix(1);
    while (!s.empty() && leer(s.back())) s.remove_suffix(1);
    return s;
}

bool gleichOhneFall(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (klein(a[i]) != klein(b[i])) return false;
    return true;
}

std::string kleinVon(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = klein(c);
    return r;
}

void utf8(std::string& t, uint32_t z) {
    if (z == 0 || z > 0x10FFFF || (z >= 0xD800 && z <= 0xDFFF)) z = 0xFFFD;
    if (z < 0x80) t += char(z);
    else if (z < 0x800) { t += char(0xC0 | (z >> 6)); t += char(0x80 | (z & 0x3F)); }
    else if (z < 0x10000) { t += char(0xE0 | (z >> 12)); t += char(0x80 | ((z >> 6) & 0x3F)); t += char(0x80 | (z & 0x3F)); }
    else {
        t += char(0xF0 | (z >> 18)); t += char(0x80 | ((z >> 12) & 0x3F));
        t += char(0x80 | ((z >> 6) & 0x3F)); t += char(0x80 | (z & 0x3F));
    }
}

//  Escape ab `i` (hinter dem Backslash) aufloesen.
size_t escape(std::string_view s, size_t i, std::string& aus) {
    if (i >= s.size()) { utf8(aus, 0xFFFD); return i; }
    if (!hex(s[i])) { aus += s[i]; return i + 1; }
    uint32_t z = 0;
    size_t n = 0;
    while (i < s.size() && n < 6 && hex(s[i])) {
        const char c = klein(s[i++]);
        z = z * 16 + uint32_t(c <= '9' ? c - '0' : c - 'a' + 10);
        ++n;
    }
    if (i < s.size() && leer(s[i])) ++i;
    utf8(aus, z);
    return i;
}

//  CSS-Bezeichner ab `i`; false, wenn dort keiner beginnt.
bool bezeichner(std::string_view s, size_t& i, std::string& aus) {
    aus.clear();
    size_t p = i;
    auto beginnt = [&](size_t q) {
        return q < s.size() && (nameAnfang((unsigned char)s[q]) || (s[q] == '\\' && q + 1 < s.size() && s[q + 1] != '\n'));
    };
    if (p < s.size() && s[p] == '-') {
        if (!(p + 1 < s.size() && s[p + 1] == '-') && !beginnt(p + 1)) return false;
    } else if (!beginnt(p)) {
        return false;
    }
    while (p < s.size()) {
        const char c = s[p];
        if (c == '\\') {
            if (p + 1 < s.size() && s[p + 1] == '\n') break;
            p = escape(s, p + 1, aus);
        } else if (nameZeichen((unsigned char)c)) {
            aus += c;
            ++p;
        } else {
            break;
        }
    }
    i = p;
    return true;
}

size_t hinterString(std::string_view s, size_t i, size_t bis) {
    const char q = s[i++];
    while (i < bis) {
        const char c = s[i];
        if (c == q) return i + 1;
        if (c == '\n') return i;
        i += c == '\\' ? 2 : 1;
    }
    return bis;
}

//  Erstes Zeichen aus `stopp` ausserhalb von Klammern und Strings, sonst `bis`. Schliessende Klammern, die nicht
//  zur offenen passen, zaehlen nicht.
size_t scanne(std::string_view s, size_t i, size_t bis, std::string_view stopp) {
    char stapel[kStapel];
    size_t tief = 0;
    //  Hoechstens drei Stoppzeichen; je Zeichen ein memchr kostete ein Zehntel der Kaskade.
    const char s0 = stopp.empty() ? '\0' : stopp[0];
    const char s1 = stopp.size() > 1 ? stopp[1] : s0, s2 = stopp.size() > 2 ? stopp[2] : s0;
    while (i < bis) {
        const char c = s[i];
        if (tief == 0 && (c == s0 || c == s1 || c == s2) && !stopp.empty()) return i;
        if (c == '"' || c == '\'') { i = hinterString(s, i, bis); continue; }
        if (c == '\\') { i += 2; continue; }
        if (c == '(' || c == '[' || c == '{') {
            if (tief < kStapel) stapel[tief++] = c == '(' ? ')' : c == '[' ? ']' : '}';
        } else if (tief > 0 && c == stapel[tief - 1]) {
            --tief;
        }
        ++i;
    }
    return bis;
}

void ohneKommentar(std::string& t, std::string_view css) {
    t.reserve(t.size() + css.size());
    size_t i = 0;
    while (i < css.size()) {
        const char c = css[i];
        if (c == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            const size_t e = css.find("*/", i + 2);
            if (e == std::string_view::npos) break;
            //  Ein Kommentar trennt wie Leerraum: 1px/**/solid sind zwei Werte.
            t += ' ';
            i = e + 2;
            continue;
        }
        if (c == '\r') {
            t += '\n';
            i += i + 1 < css.size() && css[i + 1] == '\n' ? 2 : 1;
            continue;
        }
        if (c == '\f') { t += '\n'; ++i; continue; }
        if (c == '\0') { t += "\xEF\xBF\xBD"; ++i; continue; }
        if (c == '"' || c == '\'') {
            const size_t e = hinterString(css, i, css.size());
            for (size_t k = i; k < e; ++k) t += css[k] == '\0' ? '?' : css[k] == '\r' ? '\n' : css[k];
            i = e;
            continue;
        }
        if (c == '\\' && i + 1 < css.size()) {
            t += c;
            t += css[i + 1] == '\0' ? '?' : css[i + 1];
            i += 2;
            continue;
        }
        t += c;
        ++i;
    }
}

bool mitVar(std::string_view w) {
    for (size_t i = 0; i + 4 <= w.size(); ++i)
        if (klein(w[i]) == 'v' && klein(w[i + 1]) == 'a' && klein(w[i + 2]) == 'r' && w[i + 3] == '(') return true;
    return false;
}

//  an+b aus dem Argument von :nth-child; `of S` wird nicht gefuehrt.
bool anb(std::string_view s, int32_t& a, int32_t& b) {
    std::string t;
    for (char c : trimme(s)) {
        if (c == ' ' || c == '\t' || c == '\n') {
            if (t.size() >= 2 && t.compare(t.size() - 2, 2, "of") == 0) return false;
            continue;
        }
        t += klein(c);
    }
    if (t == "odd") { a = 2; b = 1; return true; }
    if (t == "even") { a = 2; b = 0; return true; }
    auto zahl = [](std::string_view z, int32_t& aus) {
        if (z.empty()) return false;
        int64_t v = 0;
        for (char c : z) {
            if (c < '0' || c > '9') return false;
            v = std::min<int64_t>(v * 10 + (c - '0'), INT32_MAX);
        }
        aus = int32_t(v);
        return true;
    };
    const size_t n = t.find('n');
    if (n == std::string::npos) {
        const bool minus = !t.empty() && t[0] == '-';
        const std::string_view z = std::string_view(t).substr(!t.empty() && (t[0] == '+' || t[0] == '-') ? 1 : 0);
        if (!zahl(z, b)) return false;
        if (minus) b = -b;
        a = 0;
        return true;
    }
    std::string_view vorn = std::string_view(t).substr(0, n), hinten = std::string_view(t).substr(n + 1);
    if (vorn.empty() || vorn == "+") a = 1;
    else if (vorn == "-") a = -1;
    else {
        const bool minus = vorn[0] == '-';
        if (vorn[0] == '+' || vorn[0] == '-') vorn.remove_prefix(1);
        if (!zahl(vorn, a)) return false;
        if (minus) a = -a;
    }
    if (hinten.empty()) { b = 0; return true; }
    if (hinten[0] != '+' && hinten[0] != '-') return false;
    if (!zahl(hinten.substr(1), b)) return false;
    if (hinten[0] == '-') b = -b;
    return true;
}

struct PseudoName { std::string_view name; Pseudo p; };
constexpr PseudoName kPseudo[] = {
    { "root", Pseudo::Root }, { "first-child", Pseudo::FirstChild }, { "last-child", Pseudo::LastChild },
    { "only-child", Pseudo::OnlyChild }, { "first-of-type", Pseudo::FirstOfType },
    { "last-of-type", Pseudo::LastOfType }, { "only-of-type", Pseudo::OnlyOfType }, { "checked", Pseudo::Checked },
    { "disabled", Pseudo::Disabled }, { "enabled", Pseudo::Enabled }, { "empty", Pseudo::Empty },
    { "link", Pseudo::Link }, { "any-link", Pseudo::Link },
    { "hover", Pseudo::Nie }, { "active", Pseudo::Nie }, { "focus", Pseudo::Nie }, { "focus-visible", Pseudo::Nie },
    { "focus-within", Pseudo::Nie }, { "visited", Pseudo::Nie }, { "target", Pseudo::Nie },
    { "indeterminate", Pseudo::Nie }, { "placeholder-shown", Pseudo::Nie }, { "autofill", Pseudo::Nie },
};

//  Pseudo-Elemente, die gueltig sind, aber nicht gezeichnet werden.
constexpr std::string_view kNieElemente[] = {
    "marker", "placeholder", "selection", "backdrop", "first-line", "first-letter", "file-selector-button",
    "cue", "slotted", "part",
};

int langformenVon(uint16_t eig, uint16_t* aus) {
    auto vier = [&](uint16_t erste) { for (int i = 0; i < 4; ++i) aus[i] = uint16_t(erste + i); return 4; };
    switch (eig) {
    case EMargin:       return vier(EMarginTop);
    case EPadding:      return vier(EPaddingTop);
    case EBorderWidth:  return vier(EBorderTopWidth);
    case EBorderStyle:  return vier(EBorderTopStyle);
    case EBorderColor:  return vier(EBorderTopColor);
    case EBorderRadius: return vier(EBorderTopLeftRadius);
    case EInset:        return vier(ETop);
    case EBorder:
        for (int i = 0; i < 12; ++i) aus[i] = uint16_t(EBorderTopWidth + i);
        return 12;
    case EBorderTop: case EBorderRight: case EBorderBottom: case EBorderLeft: {
        const int seite = eig - EBorderTop;
        aus[0] = uint16_t(EBorderTopWidth + seite);
        aus[1] = uint16_t(EBorderTopStyle + seite);
        aus[2] = uint16_t(EBorderTopColor + seite);
        return 3;
    }
    case EBackground:
        aus[0] = EBackgroundColor; aus[1] = EBackgroundImage; aus[2] = EBackgroundSize; aus[3] = EBackgroundPosition;
        aus[4] = EBackgroundRepeat; aus[5] = EBackgroundClip; aus[6] = EBackgroundAttachment; return 7;
    case EGap:            aus[0] = ERowGap; aus[1] = EColumnGap; return 2;
    case EPlaceItems:     aus[0] = EAlignItems; aus[1] = EJustifyItems; return 2;
    case EFlex:           aus[0] = EFlexGrow; aus[1] = EFlexShrink; aus[2] = EFlexBasis; return 3;
    case EFlexFlow:       aus[0] = EFlexDirection; aus[1] = EFlexWrap; return 2;
    case EListStyle:      aus[0] = EListStyleType; aus[1] = EListStylePosition; return 2;
    case EOverflow:       aus[0] = EOverflowX; aus[1] = EOverflowY; return 2;
    case ETextDecoration: aus[0] = ETextDecorationLine; return 1;
    case EScrollMargin:   aus[0] = EScrollMarginTop; return 1;
    case EGridColumn:     aus[0] = EGridColumnStart; aus[1] = EGridColumnEnd; return 2;
    case EGridRow:        aus[0] = EGridRowStart; aus[1] = EGridRowEnd; return 2;
    case EGridArea:
        aus[0] = EGridRowStart; aus[1] = EGridColumnStart; aus[2] = EGridRowEnd; aus[3] = EGridColumnEnd; return 4;
    case EFont:
        aus[0] = EFontStyle; aus[1] = EFontWeight; aus[2] = EFontSize; aus[3] = ELineHeight; aus[4] = EFontFamily;
        return 5;
    default:              return 0;
    }
}

enum MArt : uint8_t { MBreite, MHoehe, MQuer, MWenigBewegung, MDunkel, MHover, MFein, MImmer };
enum MVergleich : uint8_t { MMin, MMax, MGleich };
enum MTyp : uint8_t { MAlle, MBildschirm, MDruck, MNie };

}   // namespace

size_t bisAussen(std::string_view s, size_t i, size_t bis, std::string_view stopp) { return scanne(s, i, bis, stopp); }
std::string_view ohneRand(std::string_view s) { return trimme(s); }

uint16_t eigenschaftVon(std::string_view name) {
    static const std::unordered_map<std::string_view, uint16_t> index = [] {
        std::unordered_map<std::string_view, uint16_t> m;
        for (uint16_t i = 0; i < EVariable; ++i) m.emplace(kEigNamen[i], i);
        //  Alte Schreibweisen; ihre Werte (always, avoid, left, right) kennt die neue Tabelle mit.
        m.emplace("page-break-inside", EBreakInside);
        m.emplace("page-break-before", EBreakBefore);
        m.emplace("page-break-after", EBreakAfter);
        m.emplace("-webkit-background-clip", EBackgroundClip);
        return m;
    }();
    const auto it = index.find(name);
    return it == index.end() ? uint16_t(kKein & 0xFFFF) : it->second;
}

std::string_view eigenschaftName(uint16_t eig) { return eig < EVariable ? kEigNamen[eig] : std::string_view(); }

int langformen(uint16_t eig, uint16_t* aus) { return langformenVon(eig, aus); }

uint32_t Stilblatt::atom(std::string_view s) {
    if (auto it = m_index.find(s); it != m_index.end()) return it->second;
    const auto id = uint32_t(m_atome.size());
    m_atome.emplace_back(s);
    m_index.emplace(std::string(s), id);
    return id;
}

uint32_t Stilblatt::atomSuchen(std::string_view s) const {
    const auto it = m_index.find(s);
    return it == m_index.end() ? kKein : it->second;
}

void Stilblatt::zerlege(std::string_view css, uint32_t medienIndex) {
    const size_t start = text.size();
    ohneKommentar(text, css);
    block(start, text.size(), medienIndex, 0);
}

void Stilblatt::zerlegeAttribut(uint32_t element, std::string_view dekl, bool hinweis) {
    const size_t start = text.size();
    ohneKommentar(text, dekl);
    const auto erste = uint32_t(deklarationen.size());
    deklarationsblock(start, text.size());
    if (deklarationen.size() > erste)
        regeln.push_back({ erste, uint32_t(deklarationen.size()) - erste, kKein, element, hinweis });
}

namespace {

//  HTML-Laenge wie im Browser: fuehrende Ziffern (mit Bruch), dahinter % oder nichts; 0 oder Muell gilt nicht.
std::string htmlLaenge(std::string_view v, bool nullErlaubt = false) {
    v = trimme(v);
    size_t i = 0;
    while (i < v.size() && ((v[i] >= '0' && v[i] <= '9') || v[i] == '.')) ++i;
    if (i == 0) return {};
    double zahl = 0;
    if (std::from_chars(v.data(), v.data() + i, zahl).ec != std::errc() || (zahl <= 0 && !nullErlaubt)) return {};
    char b[32];
    const auto r = std::to_chars(b, b + sizeof b, zahl);
    return std::string(b, r.ptr) + (i < v.size() && v[i] == '%' ? "%" : "px");
}

//  Darstellende Hinweise der HTML-Attribute (HTML, Rendering 15.3) fuer Tabellen, ihre Teile, col und body.
std::string hinweise(const Dokument& d, uint32_t i) {
    const Knoten& k = d.knoten[i];
    if (k.raum != Raum::Html) return {};
    const auto attr = [&](std::string_view n, bool* da = nullptr) {
        const uint32_t id = d.nameSuchen(n);
        if (id == kKein) { if (da) *da = false; return std::string_view(); }
        return d.attribut(i, id, da);
    };
    std::string aus;
    const auto setze = [&](std::string_view eig, const std::string& wert) {
        if (wert.empty()) return;
        aus += eig;
        aus += ':';
        aus += wert;
        aus += ';';
    };
    const bool tabelle = k.name == NTable, zelle = k.name == NTd || k.name == NTh;
    const bool teil = k.name == NTr || k.name == NTbody || k.name == NThead || k.name == NTfoot;
    const bool spalte = k.name == NCol || k.name == NColgroup;
    if (!tabelle && !zelle && !teil && !spalte && k.name != NBody) return {};
    if (tabelle || zelle || spalte) setze("width", htmlLaenge(attr("width")));
    if (tabelle || zelle || k.name == NTr) setze("height", htmlLaenge(attr("height")));
    if (tabelle || zelle || teil || k.name == NBody) {
        const std::string_view farbe = trimme(attr("bgcolor"));
        if (!farbe.empty()) setze("background-color", std::string(farbe));
    }
    if (zelle || teil) {
        const std::string_view a = trimme(attr("align")), v = trimme(attr("valign"));
        for (const char* x : { "left", "right", "center", "justify" })
            if (gleichOhneFall(a, x)) setze("text-align", x);
        for (const char* x : { "top", "middle", "bottom", "baseline" })
            if (gleichOhneFall(v, x)) setze("vertical-align", x);
    }
    if (zelle) {
        bool da = false;
        attr("nowrap", &da);
        if (da) setze("white-space", "nowrap");
    }
    //  border, cellspacing und cellpadding der Tabelle gelten auch fuer ihre Zellen.
    uint32_t t = i;
    if (zelle)
        for (t = k.eltern; t < d.knoten.size() && !(d.knoten[t].name == NTable && d.knoten[t].raum == Raum::Html);)
            t = d.knoten[t].eltern;
    if (t >= d.knoten.size()) return aus;
    const auto tabAttr = [&](std::string_view n, bool* da = nullptr) {
        const uint32_t id = d.nameSuchen(n);
        if (id == kKein) { if (da) *da = false; return std::string_view(); }
        return d.attribut(t, id, da);
    };
    bool hatRand = false;
    const std::string_view rand = tabAttr("border", &hatRand);
    std::string randPx = hatRand ? htmlLaenge(rand, true) : std::string();
    if (hatRand && (randPx.empty() || randPx.back() == '%')) randPx = trimme(rand).empty() ? "1px" : std::string();
    if (tabelle) {
        if (!randPx.empty() && randPx != "0px") setze("border", randPx + " outset rgb(128,128,128)");
        std::string abstand = htmlLaenge(attr("cellspacing"), true);
        if (!abstand.empty() && abstand.back() != '%') setze("border-spacing", abstand);
        if (gleichOhneFall(trimme(attr("align")), "center")) setze("margin-left", "auto"), setze("margin-right", "auto");
    } else if (zelle) {
        if (!randPx.empty() && randPx != "0px") setze("border", "1px inset rgb(128,128,128)");
        std::string polster = htmlLaenge(tabAttr("cellpadding"), true);
        if (!polster.empty() && polster.back() != '%') setze("padding", polster);
    }
    return aus;
}

}   // namespace

void Stilblatt::block(size_t von, size_t bis, uint32_t medienIndex, int tiefe) {
    const std::string_view s = text;
    size_t i = von;
    std::vector<uint32_t> neu;
    std::string name;
    while (i < bis) {
        while (i < bis && leer(s[i])) ++i;
        if (i >= bis) break;
        if (s.compare(i, 4, "<!--") == 0) { i += 4; continue; }
        if (s.compare(i, 3, "-->") == 0) { i += 3; continue; }
        if (s[i] == '}' || s[i] == ';') { ++i; continue; }
        if (s[i] == '@') {
            size_t p = i + 1;
            if (!bezeichner(s, p, name)) name.clear();
            const size_t j = scanne(s, p, bis, "{;");
            if (j >= bis) break;
            if (s[j] == ';') { i = j + 1; continue; }
            const size_t k = scanne(s, j + 1, bis, "}");
            if (gleichOhneFall(name, "media") && tiefe < kTiefe)
                block(j + 1, k, medienliste(s.substr(p, j - p), medienIndex), tiefe + 1);
            else if (gleichOhneFall(name, "page"))
                seitenblock(trimme(s.substr(p, j - p)), s.substr(j + 1, k - j - 1), medienIndex);
            i = k + 1;
            continue;
        }
        const size_t j = scanne(s, i, bis, "{");
        if (j >= bis) break;
        const size_t k = scanne(s, j + 1, bis, "}");
        const auto regel = uint32_t(regeln.size());
        const auto dekl = uint32_t(deklarationen.size());
        const size_t teileVorher = teile.size(), selVorher = selektoren.size(), argVorher = argumente.size(),
                     zeichenVorher = zeichen.size();
        deklarationsblock(j + 1, k);
        neu.clear();
        if (deklarationen.size() > dekl && selektorliste(trimme(s.substr(i, j - i)), regel, &neu, false, 0)) {
            regeln.push_back({ dekl, uint32_t(deklarationen.size()) - dekl, medienIndex, kKein });
            for (uint32_t sel : neu) einsortieren(sel);
        } else {
            deklarationen.resize(dekl);
            teile.resize(teileVorher);
            selektoren.resize(selVorher);
            argumente.resize(argVorher);
            zeichen.resize(zeichenVorher);
        }
        i = k + 1;
    }
}

//  :first, :left, :right und benannte Seiten gelten nur fuer einzelne Seiten: weggelassen. Randfelder (@top-center ...)
//  ebenso; die Seitenzahl steht immer im unteren Rand.
void Stilblatt::seitenblock(std::string_view auswahl, std::string_view inhalt, uint32_t medienIndex) {
    if (!auswahl.empty()) return;
    SeitenRegel r;
    r.medien = medienIndex;
    std::string name;
    for (size_t i = 0; i < inhalt.size();) {
        const size_t j = scanne(inhalt, i, inhalt.size(), ";{");
        if (j < inhalt.size() && inhalt[j] == '{') {
            i = scanne(inhalt, j + 1, inhalt.size(), "}") + 1;
            continue;
        }
        const std::string_view dekl = inhalt.substr(i, j - i);
        i = j + 1;
        const size_t doppel = dekl.find(':');
        if (doppel == std::string_view::npos) continue;
        size_t p = 0;
        while (p < doppel && leer(dekl[p])) ++p;
        if (!bezeichner(dekl, p, name)) continue;
        for (char& c : name) c = klein(c);
        const std::string wert(trimme(dekl.substr(doppel + 1)));
        static constexpr std::string_view seite[] = { "margin-top", "margin-right", "margin-bottom", "margin-left" };
        if (name == "size") r.groesse = wert;
        else if (name == "margin") {
            //  Ein bis vier Werte wie bei margin: oben, rechts, unten, links mit Wiederholung.
            std::string_view w[4];
            int n = 0;
            for (size_t a = 0; a < wert.size() && n < 4;) {
                while (a < wert.size() && leer(wert[a])) ++a;
                size_t e = a;
                while (e < wert.size() && !leer(wert[e])) ++e;
                if (e > a) w[n++] = std::string_view(wert).substr(a, e - a);
                a = e;
            }
            static constexpr int quelle[4][4] = { { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 } };
            if (n > 0) for (int s = 0; s < 4; ++s) r.rand[s] = std::string(w[quelle[n - 1][s]]);
        }
        else for (int s = 0; s < 4; ++s) if (name == seite[s]) r.rand[s] = wert;
    }
    if (!r.groesse.empty() || !r.rand[0].empty() || !r.rand[1].empty() || !r.rand[2].empty() || !r.rand[3].empty())
        seiten.push_back(std::move(r));
}

void Stilblatt::deklarationsblock(size_t von, size_t bis) {
    const std::string_view s = text;
    size_t i = von;
    std::string name;
    Stil probe;
    const stil::Umfeld umfeld;
    while (i < bis) {
        while (i < bis && (leer(s[i]) || s[i] == ';')) ++i;
        if (i >= bis) break;
        const size_t j = scanne(s, i, bis, ";");
        const size_t doppel = s.find(':', i);
        size_t p = i;
        if (doppel < j && bezeichner(s, p, name)) {
            while (p < doppel && leer(s[p])) ++p;
            if (p == doppel) {
                std::string_view wert = trimme(s.substr(doppel + 1, j - doppel - 1));
                bool wichtig = false;
                if (wert.size() >= 9 && gleichOhneFall(wert.substr(wert.size() - 9), "important")) {
                    const std::string_view vor = trimme(wert.substr(0, wert.size() - 9));
                    if (!vor.empty() && vor.back() == '!') {
                        wichtig = true;
                        wert = trimme(vor.substr(0, vor.size() - 1));
                    }
                }
                Deklaration d;
                d.anfang = uint32_t(wert.data() - s.data());
                d.laenge = uint32_t(wert.size());
                d.wichtig = wichtig;
                d.mitVar = mitVar(wert);
                bool gut = false;
                if (name.size() > 2 && name[0] == '-' && name[1] == '-') {
                    d.eig = EVariable;
                    d.variable = atom(name);
                    gut = true;
                } else if (const uint16_t e = eigenschaftVon(kleinVon(name)); e < EVariable && !wert.empty()) {
                    d.eig = e;
                    //  Ungueltiges faellt hier weg, sonst verdraengte es einen frueheren gueltigen Wert derselben Regel.
                    gut = d.mitVar || stil::global(wert) || stil::setze(e, wert, probe, umfeld, nullptr);
                }
                if (gut) deklarationen.push_back(d);
            }
        }
        i = j + 1;
    }
}

bool Stilblatt::selektorliste(std::string_view s, uint32_t regel, std::vector<uint32_t>* aus, bool nachsichtig,
                              int tiefe) {
    size_t i = 0;
    while (i <= s.size()) {
        const size_t j = scanne(s, i, s.size(), ",");
        const std::string_view teil = trimme(s.substr(i, j - i));
        Selektor sel;
        sel.regel = regel;
        if (!teil.empty() && komplex(teil, sel, tiefe)) {
            aus->push_back(uint32_t(selektoren.size()));
            selektoren.push_back(sel);
        } else if (!nachsichtig) {
            //  Ein ungueltiger Selektor verwirft die ganze Liste; :is/:where ueberspringen ihn nur.
            return false;
        }
        i = j + 1;
    }
    return true;
}

bool Stilblatt::komplex(std::string_view s, Selektor& sel, int tiefe) {
    std::vector<Teil> lokal;
    int ids = 0, klassen = 0, typen = 0;
    std::string name;
    size_t i = 0;
    auto offenerKombinator = [&] { return lokal.empty() || lokal.back().art == Teilart::Kombinator; };
    while (true) {
        const size_t vorLeer = i;
        while (i < s.size() && leer(s[i])) ++i;
        if (i >= s.size()) break;
        const char c = s[i];
        if (c == '>' || c == '+' || c == '~') {
            if (offenerKombinator()) return false;
            Teil k;
            k.art = Teilart::Kombinator;
            k.op = uint8_t(c == '>' ? Kombinator::Kind : c == '+' ? Kombinator::Nachbar : Kombinator::Geschwister);
            lokal.push_back(k);
            ++i;
            continue;
        }
        if (sel.pseudo != PseudoElement::Kein) return false;
        if (!offenerKombinator()) {
            if (i == vorLeer) return false;
            Teil k;
            k.art = Teilart::Kombinator;
            k.op = uint8_t(Kombinator::Nachfahr);
            lokal.push_back(k);
        }
        const size_t vorher = lokal.size();
        bool erstes = true;
        while (i < s.size()) {
            const char z = s[i];
            Teil t;
            if (z == '*' && erstes) {
                if (i + 1 < s.size() && s[i + 1] == '|') return false;
                t.art = Teilart::Typ;
                ++i;
            } else if (erstes && bezeichner(s, i, name)) {
                if (i < s.size() && s[i] == '|') return false;
                t.art = Teilart::Typ;
                t.atom = atom(kleinVon(name));
                ++typen;
            } else if (z == '#' || z == '.') {
                size_t p = i + 1;
                if (!bezeichner(s, p, name)) return false;
                t.art = z == '#' ? Teilart::Id : Teilart::Klasse;
                t.atom = atom(name);
                ++(z == '#' ? ids : klassen);
                i = p;
            } else if (z == '[') {
                const size_t e = scanne(s, i + 1, s.size(), "]");
                if (e >= s.size()) return false;
                const std::string_view inhalt = s.substr(i + 1, e - i - 1);
                size_t p = 0;
                while (p < inhalt.size() && leer(inhalt[p])) ++p;
                if (!bezeichner(inhalt, p, name)) return false;
                t.art = Teilart::Attribut;
                t.atom = atom(kleinVon(name));
                while (p < inhalt.size() && leer(inhalt[p])) ++p;
                t.op = uint8_t(AttrOp::Da);
                if (p < inhalt.size()) {
                    const char o = inhalt[p];
                    if (o == '=') { t.op = uint8_t(AttrOp::Gleich); ++p; }
                    else if (p + 1 < inhalt.size() && inhalt[p + 1] == '=') {
                        if (o == '~') t.op = uint8_t(AttrOp::Wort);
                        else if (o == '|') t.op = uint8_t(AttrOp::Strich);
                        else if (o == '^') t.op = uint8_t(AttrOp::Anfang);
                        else if (o == '$') t.op = uint8_t(AttrOp::Ende);
                        else if (o == '*') t.op = uint8_t(AttrOp::Enthaelt);
                        else return false;
                        p += 2;
                    } else {
                        return false;
                    }
                    while (p < inhalt.size() && leer(inhalt[p])) ++p;
                    std::string wert;
                    if (p < inhalt.size() && (inhalt[p] == '"' || inhalt[p] == '\'')) {
                        const char q = inhalt[p++];
                        while (p < inhalt.size() && inhalt[p] != q) {
                            if (inhalt[p] == '\\') p = escape(inhalt, p + 1, wert);
                            else wert += inhalt[p++];
                        }
                        if (p >= inhalt.size()) return false;
                        ++p;
                    } else if (!bezeichner(inhalt, p, wert)) {
                        return false;
                    }
                    t.a = int32_t(zeichen.size());
                    t.b = int32_t(wert.size());
                    zeichen += wert;
                    while (p < inhalt.size() && leer(inhalt[p])) ++p;
                    if (p < inhalt.size() && (klein(inhalt[p]) == 'i' || klein(inhalt[p]) == 's')) {
                        t.ohneFall = klein(inhalt[p]) == 'i';
                        ++p;
                    }
                    while (p < inhalt.size() && leer(inhalt[p])) ++p;
                    if (p != inhalt.size()) return false;
                }
                ++klassen;
                i = e + 1;
            } else if (z == ':') {
                const bool element = i + 1 < s.size() && s[i + 1] == ':';
                size_t p = i + (element ? 2 : 1);
                if (!bezeichner(s, p, name)) return false;
                const std::string n = kleinVon(name);
                if (element || n == "before" || n == "after" || n == "first-line" || n == "first-letter") {
                    if (n == "before") sel.pseudo = PseudoElement::Vor;
                    else if (n == "after") sel.pseudo = PseudoElement::Nach;
                    else if (n.starts_with("-webkit-") || n.starts_with("-moz-")
                             || std::find(std::begin(kNieElemente), std::end(kNieElemente), n) != std::end(kNieElemente))
                        sel.pseudo = PseudoElement::Nie;
                    else return false;
                    if (p < s.size() && s[p] == '(') {
                        const size_t e = scanne(s, p + 1, s.size(), ")");
                        if (e >= s.size()) return false;
                        p = e + 1;
                    }
                    ++typen;
                    i = p;
                    erstes = false;
                    if (i < s.size() && !leer(s[i])) return false;
                    continue;
                }
                t.art = Teilart::Pseudo;
                if (p < s.size() && s[p] == '(') {
                    const size_t e = scanne(s, p + 1, s.size(), ")");
                    if (e >= s.size()) return false;
                    const std::string_view arg = s.substr(p + 1, e - p - 1);
                    i = e + 1;
                    if (n == "nth-child" || n == "nth-last-child" || n == "nth-of-type" || n == "nth-last-of-type") {
                        if (!anb(arg, t.a, t.b)) return false;
                        t.op = uint8_t(n == "nth-child" ? Pseudo::NthChild : n == "nth-last-child" ? Pseudo::NthLastChild
                                       : n == "nth-of-type" ? Pseudo::NthOfType : Pseudo::NthLastOfType);
                        ++klassen;
                    } else if (n == "not" || n == "is" || n == "where" || n == "matches" || n == "-webkit-any") {
                        if (tiefe >= kTiefe) return false;
                        const bool where = n == "where";
                        std::vector<uint32_t> innen;
                        if (!selektorliste(arg, kKein, &innen, n != "not", tiefe + 1)) return false;
                        if (n == "not" && innen.empty()) return false;
                        uint32_t hoechste = 0;
                        for (uint32_t k : innen) {
                            if (selektoren[k].pseudo != PseudoElement::Kein) return false;
                            hoechste = std::max(hoechste, selektoren[k].spezifitaet);
                        }
                        t.op = uint8_t(n == "not" ? Pseudo::Not : where ? Pseudo::Where : Pseudo::Is);
                        t.a = int32_t(argumente.size());
                        t.b = int32_t(innen.size());
                        argumente.insert(argumente.end(), innen.begin(), innen.end());
                        if (!where) {
                            ids += int(hoechste >> 20);
                            klassen += int((hoechste >> 10) & 1023);
                            typen += int(hoechste & 1023);
                        }
                    } else {
                        return false;
                    }
                } else {
                    const auto it = std::find_if(std::begin(kPseudo), std::end(kPseudo),
                                                 [&](const PseudoName& x) { return x.name == n; });
                    if (it == std::end(kPseudo)) return false;
                    t.op = uint8_t(it->p);
                    ++klassen;
                    i = p;
                }
            } else {
                break;
            }
            lokal.push_back(t);
            erstes = false;
        }
        if (lokal.size() == vorher) {
            if (sel.pseudo == PseudoElement::Kein) return false;
            lokal.push_back(Teil{});
        }
        if (i < s.size() && !leer(s[i]) && s[i] != '>' && s[i] != '+' && s[i] != '~') return false;
    }
    if (offenerKombinator()) return false;
    auto satt = [](int v) { return uint32_t(std::min(v, 1023)); };
    sel.spezifitaet = satt(ids) << 20 | satt(klassen) << 10 | satt(typen);
    sel.anfang = uint32_t(teile.size());
    sel.anzahl = uint32_t(lokal.size());
    teile.insert(teile.end(), lokal.begin(), lokal.end());
    return true;
}

void Stilblatt::einsortieren(uint32_t sel) {
    const Selektor& s = selektoren[sel];
    if (s.pseudo == PseudoElement::Nie) return;
    if (nachId.size() < m_atome.size()) {
        nachId.resize(m_atome.size());
        nachKlasse.resize(m_atome.size());
        nachTyp.resize(m_atome.size());
    }
    uint32_t start = s.anfang + s.anzahl;
    while (start > s.anfang && teile[start - 1].art != Teilart::Kombinator) --start;
    uint32_t id = kKein, klasse = kKein, typ = kKein;
    for (uint32_t i = start; i < s.anfang + s.anzahl; ++i) {
        const Teil& t = teile[i];
        if (t.art == Teilart::Id && id == kKein) id = t.atom;
        else if (t.art == Teilart::Klasse && klasse == kKein) klasse = t.atom;
        else if (t.art == Teilart::Typ && t.atom != kKein) typ = t.atom;
    }
    for (uint32_t i = s.anfang; i < s.anfang + s.anzahl; ++i)
        if (teile[i].art == Teilart::Id || teile[i].art == Teilart::Klasse) mitKlassen = true;
    //  Id vor Klasse vor Typ: das seltenste Merkmal haelt den Eimer klein.
    if (id != kKein)          nachId[id].push_back(sel);
    else if (klasse != kKein) nachKlasse[klasse].push_back(sel);
    else if (typ != kKein)    nachTyp[typ].push_back(sel);
    else                      uebrige.push_back(sel);
}

uint32_t Stilblatt::medienliste(std::string_view text_, uint32_t eltern) {
    const std::string_view s = trimme(text_);
    if (s.empty()) return eltern;
    Medienliste liste;
    liste.anfang = uint32_t(anfragen.size());
    liste.eltern = eltern;
    std::string wort;
    size_t i = 0;
    while (i <= s.size()) {
        const size_t j = scanne(s, i, s.size(), ",");
        const std::string_view q = trimme(s.substr(i, j - i));
        MedienAnfrage a;
        a.anfang = uint32_t(merkmale.size());
        a.typ = MAlle;
        bool kaputt = q.empty(), typGesehen = false, erwarteUnd = false, offenesUnd = false;
        size_t p = 0;
        while (!kaputt) {
            while (p < q.size() && leer(q[p])) ++p;
            if (p >= q.size()) break;
            if (q[p] == '(') {
                if (erwarteUnd) { kaputt = true; break; }
                const size_t e = scanne(q, p + 1, q.size(), ")");
                if (e >= q.size()) { kaputt = true; break; }
                const std::string_view inhalt = trimme(q.substr(p + 1, e - p - 1));
                p = e + 1;
                erwarteUnd = true;
                offenesUnd = false;
                const size_t dp = inhalt.find(':');
                const std::string n = kleinVon(trimme(inhalt.substr(0, dp)));
                const std::string w = dp == std::string_view::npos ? std::string() : kleinVon(trimme(inhalt.substr(dp + 1)));
                MedienMerkmal m;
                m.vergleich = MGleich;
                auto laenge = [&](const std::string& v) {
                    double zahl = 0;
                    const auto [ende, fehler] = std::from_chars(v.data(), v.data() + v.size(), zahl);
                    if (fehler != std::errc() || zahl < 0) return false;
                    const std::string_view einheit = std::string_view(v).substr(size_t(ende - v.data()));
                    if (einheit == "px" || (einheit.empty() && zahl == 0)) {}
                    else if (einheit == "em" || einheit == "rem") zahl *= 16;
                    else return false;
                    m.wert = int32_t(std::min(zahl * kQ, double(INT32_MAX)));
                    return true;
                };
                const std::string_view basis = std::string_view(n).substr(n.starts_with("min-") || n.starts_with("max-") ? 4 : 0);
                if (n.starts_with("min-")) m.vergleich = MMin;
                else if (n.starts_with("max-")) m.vergleich = MMax;
                if ((basis == "width" || basis == "height") && laenge(w)) {
                    m.art = basis == "width" ? MBreite : MHoehe;
                } else if (n == "orientation" && (w == "landscape" || w == "portrait")) {
                    m.art = MQuer; m.wert = w == "landscape";
                } else if (n == "prefers-reduced-motion" && (w == "reduce" || w == "no-preference" || w.empty())) {
                    m.art = MWenigBewegung; m.wert = w != "no-preference";
                } else if (n == "prefers-color-scheme" && (w == "dark" || w == "light")) {
                    m.art = MDunkel; m.wert = w == "dark";
                } else if ((n == "hover" || n == "any-hover") && (w == "hover" || w == "none" || w.empty())) {
                    m.art = MHover; m.wert = w != "none";
                } else if ((n == "pointer" || n == "any-pointer") && (w == "fine" || w == "coarse" || w == "none" || w.empty())) {
                    m.art = MFein; m.wert = w == "fine" || w.empty();
                } else if ((n == "color" || n == "grid" || n == "monochrome") && w.empty()) {
                    m.art = MImmer; m.wert = n == "color";
                } else {
                    kaputt = true;
                    break;
                }
                merkmale.push_back(m);
                continue;
            }
            if (!bezeichner(q, p, wort)) { kaputt = true; break; }
            const std::string w = kleinVon(wort);
            if (erwarteUnd) {
                if (w != "and") kaputt = true;
                erwarteUnd = false;
                offenesUnd = true;
                continue;
            }
            if (w == "not" && !typGesehen && a.anzahl == 0 && merkmale.size() == a.anfang) { a.nicht = 1; continue; }
            if (w == "only" && !typGesehen) continue;
            if (typGesehen) { kaputt = true; break; }
            typGesehen = true;
            erwarteUnd = true;
            a.typ = w == "all" ? MAlle : w == "screen" ? MBildschirm : w == "print" ? MDruck : MNie;
        }
        if (kaputt || offenesUnd) {
            merkmale.resize(a.anfang);
            a.typ = MNie;
            a.nicht = 0;
        }
        a.anzahl = uint32_t(merkmale.size()) - a.anfang;
        anfragen.push_back(a);
        i = j + 1;
    }
    liste.anzahl = uint32_t(anfragen.size()) - liste.anfang;
    medien.push_back(liste);
    return uint32_t(medien.size() - 1);
}

bool Stilblatt::gilt(uint32_t index, const Medium& m) const {
    for (int tiefe = 0; index != kKein && index < medien.size() && tiefe <= kTiefe; ++tiefe) {
        const Medienliste& l = medien[index];
        bool eine = false;
        for (uint32_t i = l.anfang; i < l.anfang + l.anzahl && !eine; ++i) {
            const MedienAnfrage& a = anfragen[i];
            bool ja = a.typ == MAlle || (a.typ == MBildschirm && !m.druck) || (a.typ == MDruck && m.druck);
            for (uint32_t k = a.anfang; k < a.anfang + a.anzahl && ja; ++k) {
                const MedienMerkmal& f = merkmale[k];
                const int64_t ist = f.art == MBreite ? int64_t(m.breite) * kQ : int64_t(m.hoehe) * kQ;
                switch (f.art) {
                case MBreite: case MHoehe:
                    ja = f.vergleich == MMin ? ist >= f.wert : f.vergleich == MMax ? ist <= f.wert : ist == f.wert;
                    break;
                case MQuer:  ja = (m.breite >= m.hoehe) == bool(f.wert); break;
                //  Angezeigt wird ohne Bewegung, hell und mit Maus.
                case MWenigBewegung: ja = f.wert; break;
                case MDunkel: ja = !f.wert; break;
                case MHover: case MFein: case MImmer: ja = f.wert; break;
                default: ja = false;
                }
            }
            if (a.typ != MNie && a.nicht) ja = !ja;
            eine = ja;
        }
        if (!eine) return false;
        index = l.eltern;
    }
    return true;
}

size_t Stilblatt::bytes() const {
    size_t n = text.capacity() + zeichen.capacity() + regeln.capacity() * sizeof(Regel)
             + deklarationen.capacity() * sizeof(Deklaration) + selektoren.capacity() * sizeof(Selektor)
             + teile.capacity() * sizeof(Teil) + medien.capacity() * sizeof(Medienliste)
             + anfragen.capacity() * sizeof(MedienAnfrage) + merkmale.capacity() * sizeof(MedienMerkmal)
             + uebrige.capacity() * sizeof(uint32_t) + argumente.capacity() * sizeof(uint32_t)
             + seiten.capacity() * sizeof(SeitenRegel);
    for (const auto* eimer : { &nachId, &nachKlasse, &nachTyp }) {
        n += eimer->capacity() * sizeof(std::vector<uint32_t>);
        for (const auto& v : *eimer) n += v.capacity() * sizeof(uint32_t);
    }
    for (const std::string& a : m_atome) n += sizeof(std::string) * 2 + 2 * sizeof(void*) + (a.size() > 15 ? 2 * (a.size() + 1) : 0);
    return n + m_index.bucket_count() * sizeof(void*);
}

Stilblatt stilblattVon(const Dokument& d) {
    Stilblatt b;
    const uint32_t nStil = d.nameSuchen("style"), nTyp = d.nameSuchen("type"), nMedia = d.nameSuchen("media");
    for (uint32_t i = 0; i < d.knoten.size(); ++i) {
        const Knoten& k = d.knoten[i];
        if (k.art != Art::Element) continue;
        if (k.name == NStyle && k.raum != Raum::MathMl) {
            bool da = false;
            const std::string_view typ = trimme(d.attribut(i, nTyp, &da));
            if (!da || typ.empty() || gleichOhneFall(typ, "text/css")) {
                const uint32_t m = b.medienliste(d.attribut(i, nMedia));
                for (uint32_t c = k.erstesKind; c != kKein; c = d.knoten[c].naechstes)
                    if (d.knoten[c].art == Art::Text) b.zerlege(d.textVon(d.knoten[c]), m);
            }
        }
        if (const std::string h = hinweise(d, i); !h.empty()) b.zerlegeAttribut(i, h, true);
        if (nStil != kKein) {
            bool da = false;
            const std::string_view s = d.attribut(i, nStil, &da);
            if (da && !s.empty()) b.zerlegeAttribut(i, s);
        }
    }
    return b;
}

}   // namespace mg::html
