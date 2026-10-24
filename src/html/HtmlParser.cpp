#include "html/HtmlParser.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>

namespace mg::html {

namespace {

//  Tiefer wird nicht verschachtelt, weitere Elemente werden Geschwister: haelt jede Stapelsuche und das Layout begrenzt.
constexpr size_t kTiefeMax = 512;

enum Merkmal : uint16_t { Leer = 1, SchliesstP = 2, Speziell = 4, Kopf = 8, Rand = 16, Ausbruch = 32 };

constexpr std::array<uint16_t, NFesteNamen> baueMerkmale() {
    std::array<uint16_t, NFesteNamen> m{};
    const auto setze = [&m](uint16_t merkmal, std::initializer_list<Name> namen) {
        for (Name n : namen) m[n] |= merkmal;
    };
    setze(Leer, { NArea, NBase, NBr, NCol, NEmbed, NHr, NImg, NInput, NLink, NMeta, NSource, NTrack, NWbr, NParam });
    setze(SchliesstP, { NAddress, NArticle, NAside, NBlockquote, NCenter, NDetails, NDialog, NDiv, NDl, NFieldset,
                        NFigcaption, NFigure, NFooter, NHeader, NHgroup, NMain, NMenu, NNav, NOl, NP, NSection, NSummary,
                        NUl, NH1, NH2, NH3, NH4, NH5, NH6, NPre, NListing, NForm, NLi, NDd, NDt, NPlaintext, NTable,
                        NHr, NXmp });
    setze(Speziell, { NAddress, NApplet, NArea, NArticle, NAside, NBase, NBlockquote, NBody, NBr, NButton, NCaption,
                      NCenter, NCol, NColgroup, NDd, NDetails, NDialog, NDiv, NDl, NDt, NEmbed, NFieldset, NFigcaption,
                      NFigure, NFooter, NForm, NH1, NH2, NH3, NH4, NH5, NH6, NHead, NHeader, NHgroup, NHr, NHtml,
                      NIframe, NImg, NInput, NLi, NLink, NListing, NMain, NMarquee, NMenu, NMeta, NNav, NNoembed,
                      NNoframes, NNoscript, NObject, NOl, NP, NParam, NPlaintext, NPre, NScript, NSection, NSource,
                      NStyle, NSummary, NTable, NTbody, NTd, NTemplate, NTextarea, NTfoot, NTh, NThead, NTitle, NTr,
                      NTrack, NUl, NWbr, NXmp });
    setze(Kopf, { NTitle, NMeta, NLink, NStyle, NScript, NBase });
    setze(Rand, { NApplet, NCaption, NHtml, NTable, NTd, NTh, NMarquee, NObject, NTemplate });
    setze(Ausbruch, { NB, NBig, NBlockquote, NBody, NBr, NCenter, NCode, NDd, NDiv, NDl, NDt, NEm, NEmbed, NH1, NH2,
                      NH3, NH4, NH5, NH6, NHead, NHr, NI, NImg, NLi, NListing, NMenu, NMeta, NNobr, NOl, NP, NPre,
                      NRuby, NS, NSmall, NSpan, NStrong, NStrike, NSub, NSup, NTable, NTt, NU, NUl, NVar });
    return m;
}
constexpr auto kMerkmale = baueMerkmale();

bool hat(uint32_t name, uint16_t merkmal) { return name < NFesteNamen && (kMerkmale[name] & merkmal); }

struct Entitaet { std::string_view name; uint32_t zeichen; };

//  Benannte Zeichen fuer deutsche, westeuropaeische und typografische Texte; die volle Liste hat 2231. Sortiert.
constexpr Entitaet kEntitaeten[] = {
    { "AElig", 0xC6 }, { "Aacute", 0xC1 }, { "Acirc", 0xC2 }, { "Agrave", 0xC0 }, { "Aring", 0xC5 },
    { "Atilde", 0xC3 }, { "Auml", 0xC4 }, { "Ccedil", 0xC7 }, { "Dagger", 0x2021 }, { "Eacute", 0xC9 },
    { "Ecirc", 0xCA }, { "Egrave", 0xC8 }, { "Euml", 0xCB }, { "Iacute", 0xCD }, { "Icirc", 0xCE },
    { "Igrave", 0xCC }, { "Iuml", 0xCF }, { "Ntilde", 0xD1 }, { "Oacute", 0xD3 }, { "Ocirc", 0xD4 },
    { "Ograve", 0xD2 }, { "Omega", 0x3A9 }, { "Oslash", 0xD8 }, { "Otilde", 0xD5 }, { "Ouml", 0xD6 },
    { "Uacute", 0xDA }, { "Ucirc", 0xDB }, { "Ugrave", 0xD9 }, { "Uuml", 0xDC }, { "Yacute", 0xDD },
    { "aacute", 0xE1 }, { "acirc", 0xE2 }, { "aelig", 0xE6 }, { "agrave", 0xE0 }, { "alpha", 0x3B1 },
    { "amp", 0x26 }, { "apos", 0x27 }, { "aring", 0xE5 }, { "atilde", 0xE3 }, { "auml", 0xE4 },
    { "bdquo", 0x201E }, { "beta", 0x3B2 }, { "brvbar", 0xA6 }, { "bull", 0x2022 }, { "ccedil", 0xE7 },
    { "cent", 0xA2 }, { "copy", 0xA9 }, { "curren", 0xA4 }, { "dagger", 0x2020 }, { "darr", 0x2193 },
    { "deg", 0xB0 }, { "delta", 0x3B4 }, { "divide", 0xF7 }, { "eacute", 0xE9 }, { "ecirc", 0xEA },
    { "egrave", 0xE8 }, { "emsp", 0x2003 }, { "ensp", 0x2002 }, { "euml", 0xEB }, { "euro", 0x20AC },
    { "frac12", 0xBD }, { "frac14", 0xBC }, { "frac34", 0xBE }, { "gamma", 0x3B3 }, { "ge", 0x2265 },
    { "gt", 0x3E }, { "harr", 0x2194 }, { "hellip", 0x2026 }, { "iacute", 0xED }, { "icirc", 0xEE },
    { "iexcl", 0xA1 }, { "igrave", 0xEC }, { "infin", 0x221E }, { "iquest", 0xBF }, { "iuml", 0xEF },
    { "lambda", 0x3BB }, { "laquo", 0xAB }, { "larr", 0x2190 }, { "ldquo", 0x201C }, { "le", 0x2264 },
    { "lrm", 0x200E }, { "lsaquo", 0x2039 }, { "lsquo", 0x2018 }, { "lt", 0x3C }, { "macr", 0xAF },
    { "mdash", 0x2014 }, { "micro", 0xB5 }, { "middot", 0xB7 }, { "minus", 0x2212 }, { "mu", 0x3BC },
    { "nbsp", 0xA0 }, { "ndash", 0x2013 }, { "ne", 0x2260 }, { "not", 0xAC }, { "ntilde", 0xF1 },
    { "oacute", 0xF3 }, { "ocirc", 0xF4 }, { "ograve", 0xF2 }, { "omega", 0x3C9 }, { "ordf", 0xAA },
    { "ordm", 0xBA }, { "oslash", 0xF8 }, { "otilde", 0xF5 }, { "ouml", 0xF6 }, { "para", 0xB6 },
    { "pi", 0x3C0 }, { "plusmn", 0xB1 }, { "pound", 0xA3 }, { "quot", 0x22 }, { "raquo", 0xBB },
    { "rarr", 0x2192 }, { "rdquo", 0x201D }, { "reg", 0xAE }, { "rlm", 0x200F }, { "rsaquo", 0x203A },
    { "rsquo", 0x2019 }, { "sbquo", 0x201A }, { "sect", 0xA7 }, { "shy", 0xAD }, { "sigma", 0x3C3 },
    { "sup1", 0xB9 }, { "sup2", 0xB2 }, { "sup3", 0xB3 }, { "szlig", 0xDF }, { "theta", 0x3B8 },
    { "thinsp", 0x2009 }, { "times", 0xD7 }, { "trade", 0x2122 }, { "uacute", 0xFA }, { "uarr", 0x2191 },
    { "ucirc", 0xFB }, { "ugrave", 0xF9 }, { "uml", 0xA8 }, { "uuml", 0xFC }, { "yacute", 0xFD },
    { "yen", 0xA5 }, { "yuml", 0xFF }, { "zwj", 0x200D }, { "zwnj", 0x200C },
};
static_assert(std::is_sorted(std::begin(kEntitaeten), std::end(kEntitaeten),
                             [](const Entitaet& a, const Entitaet& b) { return a.name < b.name; }));

uint32_t sucheEntitaet(std::string_view name) {
    const auto it = std::lower_bound(std::begin(kEntitaeten), std::end(kEntitaeten), name,
                                     [](const Entitaet& e, std::string_view n) { return e.name < n; });
    return it != std::end(kEntitaeten) && it->name == name ? it->zeichen : 0;
}

//  Ohne Semikolon gelten nur die alten Namen; sonst blieben Adressen wie `?a=1&copy=2` nicht heil.
bool altName(std::string_view n) {
    return n == "amp" || n == "lt" || n == "gt" || n == "quot" || n == "nbsp" || n == "copy" || n == "reg";
}

bool istLeer(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }
bool istBuchstabe(char c) { return (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; }
bool istAlnum(char c) { return istBuchstabe(c) || (c >= '0' && c <= '9'); }
char klein(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
bool beginntMarkup(char c) { return istBuchstabe(c) || c == '/' || c == '!' || c == '?'; }

bool gleichOhneFall(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (klein(a[i]) != klein(b[i])) return false;
    return true;
}

void utf8(std::string& t, uint32_t z) {
    if (z < 0x80) {
        t += char(z);
    } else if (z < 0x800) {
        t += char(0xC0 | (z >> 6));
        t += char(0x80 | (z & 0x3F));
    } else if (z < 0x10000) {
        t += char(0xE0 | (z >> 12));
        t += char(0x80 | ((z >> 6) & 0x3F));
        t += char(0x80 | (z & 0x3F));
    } else {
        t += char(0xF0 | (z >> 18));
        t += char(0x80 | ((z >> 12) & 0x3F));
        t += char(0x80 | ((z >> 6) & 0x3F));
        t += char(0x80 | (z & 0x3F));
    }
}

//  `p` steht hinter dem `&`. Haengt das Zeichen an - oder ein woertliches `&`, wenn nichts passt - und liefert,
//  wo es weitergeht.
const char* entitaet(const char* p, const char* bis, std::string& t, bool imAttribut) {
    if (p < bis && *p == '#') {
        const char* q = p + 1;
        const bool hex = q < bis && (*q | 0x20) == 'x';
        if (hex) ++q;
        const char* ziffern = q;
        uint32_t z = 0;
        for (; q < bis; ++q) {
            const char c = *q;
            uint32_t w;
            if (c >= '0' && c <= '9')                                  w = uint32_t(c - '0');
            else if (hex && (c | 0x20) >= 'a' && (c | 0x20) <= 'f')    w = uint32_t((c | 0x20) - 'a' + 10);
            else break;
            if (z <= 0x10FFFF) z = z * (hex ? 16 : 10) + w;            // bleibt so unter 2^32
        }
        if (q == ziffern) { t += '&'; return p; }
        if (q < bis && *q == ';') ++q;
        if (z == 0 || z > 0x10FFFF || (z >= 0xD800 && z <= 0xDFFF)) z = 0xFFFD;
        utf8(t, z);
        return q;
    }
    const char* q = p;
    while (q < bis && q - p < 8 && istAlnum(*q)) ++q;
    const std::string_view name(p, size_t(q - p));
    if (q < bis && *q == ';') {
        if (const uint32_t z = sucheEntitaet(name)) { utf8(t, z); return q + 1; }
    } else if (!(q < bis && (istAlnum(*q) || (imAttribut && *q == '='))) && altName(name)) {
        utf8(t, sucheEntitaet(name));
        return q;
    }
    t += '&';
    return p;
}

constexpr std::array<bool, 256> kBesonders = [] {
    std::array<bool, 256> b{};
    b[uint8_t('&')] = b[uint8_t('\r')] = b[uint8_t('\0')] = true;
    return b;
}();

enum class Ziel : uint8_t { Text, Attribut, Roh };
enum class Bereich : uint8_t { Normal, Knopf, Liste, Tabelle };

class Zerleger {
public:
    explicit Zerleger(std::string_view q) : m_p(q.data()), m_ende(q.data() + q.size()) {}

    Dokument lauf(bool abgeschnitten) {
        d.abgeschnitten = abgeschnitten;
        const size_t n = size_t(m_ende - m_p);
        if (n >= 3 && std::memcmp(m_p, "\xEF\xBB\xBF", 3) == 0) m_p += 3;
        //  Geschaetzt am Pruefbestand (ein Knoten je ~18 Byte), damit das Feld nicht oft umzieht.
        d.knoten.reserve(n / 16 + 4);
        d.attribute.reserve(n / 64 + 4);
        d.text.reserve(n / 2);
        Knoten wurzel;
        wurzel.art = Art::Dokument;
        d.knoten.push_back(wurzel);
        m_stapel.push_back(0);
        while (m_p < m_ende) {
            if (*m_p == '<' && m_ende - m_p > 1 && beginntMarkup(m_p[1])) tag();
            else text();
        }
        sicherBody();
        d.knoten.shrink_to_fit();
        d.attribute.shrink_to_fit();
        d.text.shrink_to_fit();
        return std::move(d);
    }

private:
    struct RohAttribut { std::string_view name; uint32_t anfang, laenge; };

    Dokument d;
    const char* m_p;
    const char* m_ende;
    std::vector<uint32_t> m_stapel;     // offene Elemente, [0] ist die Wurzel
    std::vector<RohAttribut> m_attr;
    std::string m_klein;
    bool m_lfWeg = false;               // ein Zeilenumbruch direkt nach <pre>/<listing> zaehlt nicht

    uint32_t oben() const { return m_stapel.back(); }

    std::string_view kleinVon(std::string_view s) {
        m_klein.assign(s);
        for (char& c : m_klein) c = klein(c);
        return m_klein;
    }

    void schiebe(uint32_t el) { if (m_stapel.size() < kTiefeMax) m_stapel.push_back(el); }

    void anhaengen(uint32_t eltern, uint32_t kind) {
        Knoten& e = d.knoten[eltern];
        if (e.letztesKind == kKein) e.erstesKind = kind;
        else d.knoten[e.letztesKind].naechstes = kind;
        e.letztesKind = kind;
        d.knoten[kind].eltern = eltern;
    }

    uint32_t element(uint32_t eltern, uint32_t name, Raum raum, bool mitAttributen) {
        Knoten n;
        n.name = name;
        n.raum = raum;
        n.anfang = uint32_t(d.attribute.size());
        if (mitAttributen) attributeUebernehmen(raum);
        n.anzahl = uint32_t(d.attribute.size()) - n.anfang;
        const auto i = uint32_t(d.knoten.size());
        d.knoten.push_back(n);
        anhaengen(eltern, i);
        return i;
    }

    //  In SVG/MathML bleibt die Schreibweise (`viewBox`); das erste von doppelten Attributen gilt.
    void attributeUebernehmen(Raum raum) {
        const size_t erstes = d.attribute.size();
        for (const RohAttribut& r : m_attr) {
            const uint32_t name = d.nameEintragen(raum == Raum::Html ? kleinVon(r.name) : r.name);
            bool doppelt = false;
            for (size_t i = erstes; i < d.attribute.size() && !doppelt; ++i) doppelt = d.attribute[i].name == name;
            if (!doppelt) d.attribute.push_back({ name, r.anfang, r.laenge });
        }
    }

    //  Ein zweites <html>/<body> ergaenzt fehlende Attribute; die Liste zieht dafuer ans Ende, weil sie am Stueck liegt.
    void attributeErgaenzen(uint32_t el) {
        if (m_attr.empty()) return;
        const uint32_t alt = d.knoten[el].anfang, altAnzahl = d.knoten[el].anzahl;
        const auto neu = uint32_t(d.attribute.size());
        for (uint32_t i = 0; i < altAnzahl; ++i) {
            const Attribut a = d.attribute[alt + i];
            d.attribute.push_back(a);
        }
        d.knoten[el].anfang = neu;
        const size_t vorher = d.attribute.size();
        attributeUebernehmen(Raum::Html);
        //  attributeUebernehmen prueft nur gegen die neuen - hier zaehlen auch die alten.
        size_t schreib = vorher;
        for (size_t i = vorher; i < d.attribute.size(); ++i) {
            bool schonDa = false;
            for (size_t j = neu; j < vorher && !schonDa; ++j) schonDa = d.attribute[j].name == d.attribute[i].name;
            if (!schonDa) d.attribute[schreib++] = d.attribute[i];
        }
        d.attribute.resize(schreib);
        d.knoten[el].anzahl = uint32_t(d.attribute.size()) - neu;
    }

    void sicherHtml() {
        if (d.html != kKein) return;
        d.html = element(0, NHtml, Raum::Html, false);
        m_stapel.push_back(d.html);
    }
    void sicherHead() {
        sicherHtml();
        if (d.head == kKein) d.head = element(d.html, NHead, Raum::Html, false);
    }
    void sicherBody() {
        sicherHead();
        if (d.body != kKein) return;
        d.body = element(d.html, NBody, Raum::Html, false);
        m_stapel.push_back(d.body);
    }

    bool integrationspunkt(const Knoten& n) const {
        if (n.raum == Raum::Svg)
            return n.name == NDesc || n.name == NTitle || gleichOhneFall(d.nameVon(n.name), "foreignObject");
        if (n.raum != Raum::MathMl) return false;
        const std::string_view s = d.nameVon(n.name);
        return s == "mi" || s == "mo" || s == "mn" || s == "ms" || s == "mtext" || s == "annotation-xml";
    }
    bool speziell(const Knoten& n) const { return n.raum == Raum::Html ? hat(n.name, Speziell) : integrationspunkt(n); }

    bool istRand(const Knoten& n, Bereich b) const {
        if (n.raum != Raum::Html) return integrationspunkt(n);
        if (b == Bereich::Tabelle) return n.name == NHtml || n.name == NTable || n.name == NTemplate;
        return hat(n.name, Rand) || (b == Bereich::Knopf && n.name == NButton)
            || (b == Bereich::Liste && (n.name == NUl || n.name == NOl));
    }

    //  Stapelplatz des obersten offenen HTML-Elements `name`, solange keine Grenze dazwischen liegt; sonst 0.
    size_t imBereich(uint32_t name, Bereich b) const {
        for (size_t i = m_stapel.size(); i-- > 1;) {
            const Knoten& n = d.knoten[m_stapel[i]];
            if (n.raum == Raum::Html && n.name == name) return i;
            if (istRand(n, b)) return 0;
        }
        return 0;
    }

    void schliesseAb(size_t platz) { if (platz > 0) m_stapel.resize(platz); }

    //  <li> schliesst ein offenes <li>, <dd>/<dt> einander - aber nicht ueber ein Blockelement hinweg.
    void schliesseGleiches(uint32_t a, uint32_t b) {
        for (size_t i = m_stapel.size(); i-- > 1;) {
            const Knoten& n = d.knoten[m_stapel[i]];
            if (n.raum == Raum::Html && (n.name == a || n.name == b)) { m_stapel.resize(i); return; }
            if (speziell(n) && !(n.raum == Raum::Html && (n.name == NAddress || n.name == NDiv || n.name == NP)))
                return;
        }
    }

    // ---- Text ----

    //  NUL faellt im Fliesstext weg wie im Browser; in Werten und Rohtext wird es U+FFFD.
    void dekodiere(const char* p, const char* bis, Ziel z) {
        std::string& t = d.text;
        while (p < bis) {
            const char* r = p;
            while (r < bis && !kBesonders[uint8_t(*r)]) ++r;
            t.append(p, size_t(r - p));
            if (r >= bis) break;
            p = r;
            if (*p == '\r') {
                t += '\n';
                if (++p < bis && *p == '\n') ++p;
            } else if (*p == '\0') {
                if (z != Ziel::Text) utf8(t, 0xFFFD);
                ++p;
            } else if (z == Ziel::Roh) {
                t += '&';
                ++p;
            } else {
                p = entitaet(p + 1, bis, t, z == Ziel::Attribut);
            }
        }
    }

    void textAn(uint32_t eltern, size_t start) {
        const uint32_t letztes = d.knoten[eltern].letztesKind;
        if (letztes != kKein) {
            Knoten& l = d.knoten[letztes];
            if (l.art == Art::Text && size_t(l.anfang) + l.anzahl == start) {
                l.anzahl = uint32_t(d.text.size() - l.anfang);
                return;
            }
            //  Liegen Attributwerte dazwischen (zweites <body class=..>), zieht der alte Text hinter sie.
            if (l.art == Art::Text) {
                const std::string zusammen = d.text.substr(l.anfang, l.anzahl) + d.text.substr(start);
                d.text.resize(start);
                d.text += zusammen;
                l.anfang = uint32_t(start);
                l.anzahl = uint32_t(d.text.size() - start);
                return;
            }
        }
        Knoten n;
        n.art = Art::Text;
        n.anfang = uint32_t(start);
        n.anzahl = uint32_t(d.text.size() - start);
        const auto i = uint32_t(d.knoten.size());
        d.knoten.push_back(n);
        anhaengen(eltern, i);
    }

    void textEinfuegen(size_t start) {
        std::string& t = d.text;
        if (m_lfWeg) {
            m_lfWeg = false;
            if (t.size() > start && t[start] == '\n') t.erase(start, 1);
        }
        if (t.size() == start) return;
        //  Leerraum vor dem body gehoert nirgends hin; erst sichtbarer Text eroeffnet ihn.
        if (d.body == kKein) {
            if (std::all_of(t.begin() + std::ptrdiff_t(start), t.end(), istLeer)) { t.resize(start); return; }
            sicherBody();
        }
        //  Text direkt in <table> bleibt an seiner Stelle; der Browser zoege ihn vor die Tabelle.
        textAn(oben(), start);
    }

    void text() {
        const char* q = m_p;
        for (;;) {
            q = static_cast<const char*>(std::memchr(q, '<', size_t(m_ende - q)));
            if (!q) { q = m_ende; break; }
            if (m_ende - q > 1 && beginntMarkup(q[1])) break;
            ++q;
        }
        const size_t start = d.text.size();
        dekodiere(m_p, q, Ziel::Text);
        m_p = q;
        textEinfuegen(start);
    }

    //  Anfang von `</name` mit Leerraum, `/` oder `>` dahinter - erst das beendet Skript und Stil.
    const char* sucheEnde(std::string_view name) const {
        for (const char* q = m_p; q < m_ende; ++q) {
            q = static_cast<const char*>(std::memchr(q, '<', size_t(m_ende - q)));
            if (!q) break;
            if (m_ende - q < 3 || q[1] != '/' || size_t(m_ende - q - 2) <= name.size()) continue;
            if (!gleichOhneFall(std::string_view(q + 2, name.size()), name)) continue;
            const char c = q[2 + name.size()];
            if (istLeer(c) || c == '/' || c == '>') return q;
        }
        return m_ende;
    }

    //  Inhalt von Skript, Stil, Titel & Co. bis zum passenden End-Tag; `behalten` legt ihn als Text unter `el` ab.
    void rohtext(uint32_t el, Ziel z, bool behalten) {
        const char* schluss = sucheEnde(d.nameVon(d.knoten[el].name));
        if (behalten) {
            const size_t start = d.text.size();
            dekodiere(m_p, schluss, z);
            if (d.knoten[el].name == NTextarea && d.text.size() > start && d.text[start] == '\n')
                d.text.erase(start, 1);
            if (d.text.size() > start) textAn(el, start);
        }
        m_p = schluss;
    }

    // ---- Tags ----

    std::string_view leseName() {
        const char* a = m_p;
        while (m_p < m_ende && !istLeer(*m_p) && *m_p != '/' && *m_p != '>') ++m_p;
        return { a, size_t(m_p - a) };
    }

    //  false = Dateiende mitten im Tag; das Tag gilt dann als nie geschrieben.
    bool leseAttribute(bool& selbst) {
        m_attr.clear();
        selbst = false;
        for (;;) {
            //  `/` zaehlt nur direkt vor `>`; in HTML schliesst es ohnehin nur leere und fremde Elemente.
            while (m_p < m_ende && (istLeer(*m_p) || *m_p == '/')) {
                selbst = *m_p == '/' && m_ende - m_p > 1 && m_p[1] == '>';
                ++m_p;
            }
            if (m_p >= m_ende) return false;
            if (*m_p == '>') { ++m_p; return true; }
            const char* n = m_p++;
            while (m_p < m_ende && !istLeer(*m_p) && *m_p != '/' && *m_p != '>' && *m_p != '=') ++m_p;
            const std::string_view name(n, size_t(m_p - n));
            while (m_p < m_ende && istLeer(*m_p)) ++m_p;
            const auto anfang = uint32_t(d.text.size());
            if (m_p < m_ende && *m_p == '=') {
                ++m_p;
                while (m_p < m_ende && istLeer(*m_p)) ++m_p;
                if (m_p >= m_ende) return false;
                if (*m_p == '"' || *m_p == '\'') {
                    const char* zu = static_cast<const char*>(std::memchr(m_p + 1, *m_p, size_t(m_ende - m_p - 1)));
                    if (!zu) return false;
                    dekodiere(m_p + 1, zu, Ziel::Attribut);
                    m_p = zu + 1;
                } else {
                    const char* w = m_p;
                    while (m_p < m_ende && !istLeer(*m_p) && *m_p != '>') ++m_p;
                    if (m_p >= m_ende) return false;
                    dekodiere(w, m_p, Ziel::Attribut);
                }
            }
            m_attr.push_back({ name, anfang, uint32_t(d.text.size()) - anfang });
        }
    }

    void bisGroesser(const char* q) {
        const char* g = static_cast<const char*>(std::memchr(q, '>', size_t(m_ende - q)));
        m_p = g ? g + 1 : m_ende;
    }

    void tag() {
        m_lfWeg = false;
        const char c = m_p[1];
        if (c == '!') { deklaration(); return; }
        if (c == '?') { bisGroesser(m_p + 2); return; }
        const size_t merk = d.text.size();
        bool selbst = false;
        if (c == '/') {
            const char* n = m_p + 2;
            if (n >= m_ende) {                  // `</` am Dateiende ist Text
                d.text += "</";
                m_p = m_ende;
                textEinfuegen(merk);
                return;
            }
            if (*n == '>') { m_p = n + 1; return; }
            if (!istBuchstabe(*n)) { bisGroesser(n); return; }
            m_p = n;
            const std::string_view name = leseName();
            const bool ganz = leseAttribute(selbst);
            d.text.resize(merk);
            if (ganz) endTag(name);
            else m_p = m_ende;
            return;
        }
        ++m_p;
        const std::string_view name = leseName();
        if (!leseAttribute(selbst)) { d.text.resize(merk); m_p = m_ende; return; }
        const size_t knoten = d.knoten.size(), attribute = d.attribute.size();
        startTag(name, selbst);
        //  Ein verworfenes Tag laesst seine Werte nicht im Puffer stehen - sonst risse es zwei Texte auseinander.
        if (d.knoten.size() == knoten && d.attribute.size() == attribute) d.text.resize(merk);
    }

    void deklaration() {
        const char* q = m_p + 2;
        if (m_ende - q >= 2 && q[0] == '-' && q[1] == '-') {
            q += 2;
            if (q < m_ende && *q == '>') { m_p = q + 1; return; }                           // <!-->
            if (m_ende - q >= 2 && q[0] == '-' && q[1] == '>') { m_p = q + 2; return; }     // <!--->
            for (;; ++q) {
                q = static_cast<const char*>(std::memchr(q, '-', size_t(m_ende - q)));
                if (!q) { m_p = m_ende; return; }
                if (m_ende - q >= 3 && q[1] == '-' && q[2] == '>') { m_p = q + 3; return; }
                if (m_ende - q >= 4 && q[1] == '-' && q[2] == '!' && q[3] == '>') { m_p = q + 4; return; }
            }
        }
        //  CDATA ist nur in SVG/MathML Text, in HTML ein Kommentar.
        if (m_ende - q >= 7 && std::memcmp(q, "[CDATA[", 7) == 0 && d.knoten[oben()].raum != Raum::Html) {
            const char* a = q + 7;
            const char* e = a;
            while (e < m_ende && !(m_ende - e >= 3 && e[0] == ']' && e[1] == ']' && e[2] == '>')) ++e;
            const size_t start = d.text.size();
            dekodiere(a, e, Ziel::Roh);
            m_p = e < m_ende ? e + 3 : m_ende;
            textEinfuegen(start);
            return;
        }
        bisGroesser(q);
    }

    //  Gewoehnliche HTML-Elemente bleiben auch mit `/>` offen: <div/> umschliesst, was folgt.
    void einfuegen(uint32_t eltern, uint32_t id) {
        const uint32_t el = element(eltern, id, Raum::Html, true);
        if (hat(id, Leer)) return;
        schiebe(el);
        switch (id) {
        //  Skript laeuft nie; sein Inhalt waere toter Speicher.
        case NScript:   d.hatSkript = true; rohtext(el, Ziel::Roh, false); break;
        case NStyle:
        case NXmp:      rohtext(el, Ziel::Roh, true); break;
        //  Ersatzinhalt fuer Browser ohne die Funktion - wird nicht gezeigt.
        case NIframe:
        case NNoembed:
        case NNoframes: rohtext(el, Ziel::Roh, false); break;
        case NTitle:
        case NTextarea: rohtext(el, Ziel::Text, true); break;
        case NPlaintext: {
            const size_t start = d.text.size();
            dekodiere(m_p, m_ende, Ziel::Roh);
            m_p = m_ende;
            if (d.text.size() > start) textAn(el, start);
            break;
        }
        case NPre:
        case NListing:  m_lfWeg = true; break;
        default:        break;
        }
    }

    //  Zeilen und Zellen ohne Huelle bekommen die fehlende <tbody>/<tr>, wie im Browser. Ohne offene Tabelle: verworfen.
    void tabellenteil(uint32_t id) {
        const size_t tabelle = imBereich(NTable, Bereich::Tabelle);
        if (tabelle == 0) return;
        const auto bis = [&](std::initializer_list<uint32_t> ziel) {
            while (m_stapel.size() > tabelle + 1) {
                const Knoten& n = d.knoten[oben()];
                if (n.raum == Raum::Html && std::find(ziel.begin(), ziel.end(), n.name) != ziel.end()) break;
                m_stapel.pop_back();
            }
        };
        const auto huelle = [&](uint32_t name) { schiebe(element(oben(), name, Raum::Html, false)); };
        switch (id) {
        case NCol:
            //  Ein col ohne colgroup bekommt eine wie im Browser; folgende col landen in derselben.
            if (d.knoten[oben()].name != NColgroup) {
                bis({});
                huelle(NColgroup);
            }
            element(oben(), NCol, Raum::Html, true);
            return;
        case NTr:
            bis({ NTbody, NThead, NTfoot });
            if (d.knoten[oben()].name == NTable) huelle(NTbody);
            break;
        case NTd:
        case NTh:
            bis({ NTr, NTbody, NThead, NTfoot });
            if (d.knoten[oben()].name == NTable) huelle(NTbody);
            if (d.knoten[oben()].name != NTr) huelle(NTr);
            break;
        default:
            bis({});
            break;
        }
        schiebe(element(oben(), id, Raum::Html, true));
    }

    void startTag(std::string_view roh, bool selbst) {
        //  In SVG/MathML bleiben Kinder fremd, bis ein typisches HTML-Element den Bereich verlaesst.
        const Knoten& a = d.knoten[oben()];
        if (a.art == Art::Element && a.raum != Raum::Html && !integrationspunkt(a)) {
            if (!hat(d.nameSuchen(kleinVon(roh)), Ausbruch)) {
                const Raum raum = a.raum;
                const uint32_t el = element(oben(), d.nameEintragen(roh), raum, true);
                if (!selbst) schiebe(el);
                return;
            }
            while (m_stapel.size() > 1) {
                const Knoten& n = d.knoten[oben()];
                if (n.raum == Raum::Html || integrationspunkt(n)) break;
                m_stapel.pop_back();
            }
        }
        const uint32_t id = d.nameEintragen(kleinVon(roh));
        switch (id) {
        case NHtml:
            if (d.html == kKein) { d.html = element(0, NHtml, Raum::Html, true); m_stapel.push_back(d.html); }
            else attributeErgaenzen(d.html);
            return;
        case NHead:     // ein zweites oder spaetes <head> faellt weg
            if (d.body == kKein && d.head == kKein) { sicherHtml(); d.head = element(d.html, NHead, Raum::Html, true); }
            return;
        case NBody:
            if (d.body == kKein) {
                sicherHead();
                d.body = element(d.html, NBody, Raum::Html, true);
                m_stapel.push_back(d.body);
            } else {
                attributeErgaenzen(d.body);
            }
            return;
        default:
            break;
        }
        if (d.body == kKein && hat(id, Kopf)) {
            sicherHead();
            einfuegen(d.head, id);
            return;
        }
        sicherBody();
        if (id == NSvg || id == NMath) {
            const uint32_t el = element(oben(), id, id == NSvg ? Raum::Svg : Raum::MathMl, true);
            if (!selbst) schiebe(el);
            return;
        }
        switch (id) {
        case NCaption: case NColgroup: case NCol: case NThead: case NTbody: case NTfoot: case NTr: case NTd: case NTh:
            tabellenteil(id);
            return;
        case NLi: schliesseGleiches(NLi, NLi); break;
        case NDd:
        case NDt: schliesseGleiches(NDd, NDt); break;
        case NButton: schliesseAb(imBereich(NButton, Bereich::Normal)); break;
        //  Ein neues <a> schliesst das offene; Browser loesen falsch verschachtelte Formatierung aufwendiger auf.
        case NA: schliesseAb(imBereich(NA, Bereich::Normal)); break;
        case NOption:
        case NOptgroup:
            if (d.knoten[oben()].name == NOption) m_stapel.pop_back();
            break;
        default: break;
        }
        if (hat(id, SchliesstP)) schliesseAb(imBereich(NP, Bereich::Knopf));
        if (id >= NH1 && id <= NH6) {
            const uint32_t o = d.knoten[oben()].name;
            if (d.knoten[oben()].raum == Raum::Html && o >= NH1 && o <= NH6) m_stapel.pop_back();
        }
        einfuegen(oben(), id);
    }

    void endTag(std::string_view roh) {
        //  In SVG/MathML findet ein End-Tag sein Element ohne Ruecksicht auf die Schreibweise.
        if (d.knoten[oben()].raum != Raum::Html) {
            for (size_t i = m_stapel.size(); i-- > 1;) {
                const Knoten& n = d.knoten[m_stapel[i]];
                if (n.raum == Raum::Html) break;
                if (gleichOhneFall(d.nameVon(n.name), roh)) { m_stapel.resize(i); return; }
            }
        }
        const uint32_t id = d.nameSuchen(kleinVon(roh));
        if (id == kKein) return;                    // nie geoeffnet
        switch (id) {
        case NHtml: case NHead: case NBody:
            return;                                 // was danach kommt, gehoert weiter in den body
        case NBr:
            sicherBody();
            m_attr.clear();
            element(oben(), NBr, Raum::Html, false);
            return;
        case NH1: case NH2: case NH3: case NH4: case NH5: case NH6:
            for (size_t i = m_stapel.size(); i-- > 1;) {
                const Knoten& n = d.knoten[m_stapel[i]];
                if (n.raum == Raum::Html && n.name >= NH1 && n.name <= NH6) { m_stapel.resize(i); return; }
                if (istRand(n, Bereich::Normal)) return;
            }
            return;
        case NLi: schliesseAb(imBereich(NLi, Bereich::Liste)); return;
        //  `</p>` ohne offenes p ergaebe im Browser ein leeres p - das zeichnet nichts und entfaellt.
        case NP:  schliesseAb(imBereich(NP, Bereich::Knopf)); return;
        case NTable: case NTbody: case NThead: case NTfoot: case NTr: case NTd: case NTh: case NCaption: case NColgroup:
            schliesseAb(imBereich(id, Bereich::Tabelle));
            return;
        default:
            break;
        }
        if (hat(id, Speziell)) { schliesseAb(imBereich(id, Bereich::Normal)); return; }
        //  Inline-Elemente schliessen nie ueber einen Block hinweg: </span> in <span><div> bleibt wirkungslos.
        for (size_t i = m_stapel.size(); i-- > 1;) {
            const Knoten& n = d.knoten[m_stapel[i]];
            if (n.raum == Raum::Html && n.name == id) { m_stapel.resize(i); return; }
            if (speziell(n)) return;
        }
    }
};

}   // namespace

Dokument zerlege(std::string_view quelle) {
    const bool zuGross = quelle.size() > kGroesstesDokument;
    if (zuGross) quelle = quelle.substr(0, kGroesstesDokument);
    return Zerleger(quelle).lauf(zuGross);
}

}   // namespace mg::html
