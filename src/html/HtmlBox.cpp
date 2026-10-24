#include "html/HtmlBox.h"

#include <charconv>

namespace mg::html {

namespace {

bool leer(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }
bool hex(char c) { return (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f'); }
char klein(char c) { return c >= 'A' && c <= 'Z' ? char(c | 0x20) : c; }

bool gleich(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (klein(a[i]) != b[i]) return false;
    return true;
}

void utf8(std::string& aus, uint32_t cp) {
    if (cp < 0x80) { aus += char(cp); return; }
    if (cp < 0x800) { aus += char(0xC0 | cp >> 6); aus += char(0x80 | (cp & 63)); return; }
    if (cp < 0x10000) { aus += char(0xE0 | cp >> 12); aus += char(0x80 | (cp >> 6 & 63)); aus += char(0x80 | (cp & 63)); return; }
    aus += char(0xF0 | cp >> 18); aus += char(0x80 | (cp >> 12 & 63));
    aus += char(0x80 | (cp >> 6 & 63)); aus += char(0x80 | (cp & 63));
}

int ganzzahl(std::string_view t, int ersatz) {
    while (!t.empty() && leer(t.front())) t.remove_prefix(1);
    int n = ersatz;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), n);
    return r.ec == std::errc() ? n : ersatz;
}

std::string roemisch(int n, bool gross) {
    static const std::pair<int, const char*> stufen[] = { { 1000, "m" }, { 900, "cm" }, { 500, "d" }, { 400, "cd" },
        { 100, "c" }, { 90, "xc" }, { 50, "l" }, { 40, "xl" }, { 10, "x" }, { 9, "ix" }, { 5, "v" }, { 4, "iv" }, { 1, "i" } };
    std::string aus;
    for (const auto& [wert, z] : stufen)
        for (; n >= wert; n -= wert) aus += z;
    if (gross)
        for (char& c : aus) c = char(c - 32);
    return aus;
}

//  Bijektiv wie CSS alphabetic: 26 -> z, 27 -> aa.
std::string alphabetisch(int n, const uint32_t* zeichen, int anzahl) {
    std::vector<uint32_t> z;
    for (; n > 0; n = (n - 1) / anzahl) z.push_back(zeichen[(n - 1) % anzahl]);
    std::string aus;
    for (auto it = z.rbegin(); it != z.rend(); ++it) utf8(aus, *it);
    return aus;
}

//  Symbole und Folgen wie die vordefinierten counter-styles in Chromium, Nachsatz ". " bzw. " ".
std::string markenText(ListenArt a, int n) {
    static const uint32_t klein26[] = { 'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t',
                                        'u','v','w','x','y','z' };
    static const uint32_t gross26[] = { 'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T',
                                        'U','V','W','X','Y','Z' };
    static const uint32_t griechisch[] = { 0x3B1, 0x3B2, 0x3B3, 0x3B4, 0x3B5, 0x3B6, 0x3B7, 0x3B8, 0x3B9, 0x3BA, 0x3BB,
        0x3BC, 0x3BD, 0x3BE, 0x3BF, 0x3C0, 0x3C1, 0x3C3, 0x3C4, 0x3C5, 0x3C6, 0x3C7, 0x3C8, 0x3C9 };
    switch (a) {
    case ListenArt::Disc: return "\xE2\x80\xA2 ";
    case ListenArt::Circle: return "\xE2\x97\xA6 ";
    case ListenArt::Square: return "\xE2\x96\xAA ";
    case ListenArt::DisclosureOpen: return "\xE2\x96\xBE ";
    case ListenArt::DisclosureClosed: return "\xE2\x96\xB8 ";
    case ListenArt::None: return {};
    case ListenArt::LowerAlpha: case ListenArt::UpperAlpha:
        if (n > 0) return alphabetisch(n, a == ListenArt::LowerAlpha ? klein26 : gross26, 26) + ". ";
        break;
    case ListenArt::LowerGreek:
        if (n > 0) return alphabetisch(n, griechisch, 24) + ". ";
        break;
    case ListenArt::LowerRoman: case ListenArt::UpperRoman:
        if (n > 0 && n < 4000) return roemisch(n, a == ListenArt::UpperRoman) + ". ";
        break;
    case ListenArt::DecimalLeadingZero:
        if (n >= 0 && n < 10) return "0" + std::to_string(n) + ". ";
        break;
    case ListenArt::Decimal: break;
    }
    return std::to_string(n) + ". ";
}

KArt artVon(Anzeige a, bool ersetzt) {
    switch (a) {
    case Anzeige::Inline: case Anzeige::Ruby: case Anzeige::RubyText: case Anzeige::Contents:
        return ersetzt ? KArt::Atom : KArt::Inline;
    case Anzeige::InlineBlock: case Anzeige::InlineFlex: case Anzeige::InlineGrid: case Anzeige::InlineTable:
        return KArt::Atom;
    default: return KArt::Block;
    }
}

uint8_t rolleVon(Anzeige a) {
    switch (a) {
    case Anzeige::Table: case Anzeige::InlineTable: return TTabelle;
    case Anzeige::TableHeaderGroup: return TKopf;
    case Anzeige::TableRowGroup: return TRumpf;
    case Anzeige::TableFooterGroup: return TFuss;
    case Anzeige::TableRow: return TZeile;
    case Anzeige::TableCell: return TZelle;
    case Anzeige::TableCaption: return TTitel;
    default: return TKeine;
    }
}

//  Spalten zeichnen nichts; ihre Breiten fuehrt das Layout nicht.
bool spalte(Anzeige a) { return a == Anzeige::TableColumn || a == Anzeige::TableColumnGroup; }

class Bauer {
public:
    Bauer(const Dokument& d, const Stile& s, Kastenbaum& k, std::span<const uint8_t> zustand)
        : m_d(d), m_s(s), m_k(k), m_zustand(zustand) {
        m_open = d.nameSuchen("open");
        m_start = d.nameSuchen("start");
        m_value = d.nameSuchen("value");
        m_dir = d.nameSuchen("dir");
        for (const char* n : { "select", "video", "canvas", "audio" })
            if (const uint32_t id = d.nameSuchen(n); id != kKein) m_ersetzt.push_back(id);
        for (uint32_t id : { NImg, NInput, NTextarea, NIframe, NEmbed, NObject }) m_ersetzt.push_back(id);
        m_bdi = d.nameSuchen("bdi");
    }

    void baue() {
        m_k.kastenVon.assign(m_d.knoten.size(), kKein);
        if (m_d.html != kKein && m_d.html < m_d.knoten.size() && m_s.jeKnoten.size() == m_d.knoten.size())
            element(m_d.html, kKein, 0);
        for (Kasten& z : m_k.kaesten) {
            if (z.rolle != TZelle) continue;
            bool inhalt = false;
            for (uint32_t c = z.erstesKind; c != kKein && !inhalt; c = m_k.kaesten[c].naechstes)
                inhalt = !neutral(m_k.kaesten[c]) && !nurLeerraum(m_k.kaesten[c]);
            if (!inhalt) z.flags |= KLeer;
        }
    }

private:
    uint32_t neu(KArt art, uint32_t knoten, uint32_t stil, uint8_t flags = 0, uint8_t pseudo = PKein) {
        Kasten k;
        k.art = art;
        k.knoten = knoten;
        k.stil = stil;
        k.flags = flags;
        k.pseudo = pseudo;
        m_k.kaesten.push_back(k);
        return uint32_t(m_k.kaesten.size() - 1);
    }

    void haenge(uint32_t eltern, uint32_t kind) {
        if (eltern == kKein) return;
        Kasten& k = m_k.kaesten[kind];
        k.eltern = eltern;
        k.naechstes = kKein;
        Kasten& e = m_k.kaesten[eltern];
        if (e.letztesKind == kKein) e.erstesKind = kind;
        else m_k.kaesten[e.letztesKind].naechstes = kind;
        e.letztesKind = kind;
    }

    uint32_t eigenerText(uint32_t knoten, uint32_t stil, std::string_view t, uint8_t pseudo, KArt art = KArt::Text) {
        const uint32_t x = neu(art, knoten, stil, KEigenText, pseudo);
        m_k.kaesten[x].anfang = uint32_t(m_k.text.size());
        m_k.kaesten[x].laenge = uint32_t(t.size());
        m_k.text += t;
        return x;
    }

    bool istErsetzt(uint32_t el) const {
        const Knoten& k = m_d.knoten[el];
        if (k.raum == Raum::Svg) return k.name == NSvg;
        if (k.raum != Raum::Html) return false;
        for (uint32_t id : m_ersetzt)
            if (k.name == id) return true;
        return false;
    }

    void element(uint32_t el, uint32_t eltern, int zahl) {
        const uint32_t si = m_s.jeKnoten[el];
        const Stil& st = m_s.stile[si];
        if (st.anzeige == Anzeige::None || spalte(st.anzeige)) return;
        if (st.anzeige == Anzeige::Contents && eltern != kKein) { kinder(el, eltern, zahl); return; }
        const Knoten& kn = m_d.knoten[el];
        if (kn.name == NBr && kn.raum == Raum::Html) {
            const uint32_t b = neu(KArt::Umbruch, el, si);
            m_k.kastenVon[el] = b;
            haenge(eltern, b);
            return;
        }
        const bool ersetzt = istErsetzt(el);
        uint8_t f = ersetzt ? KErsetzt : 0;
        if (st.position == Lage::Absolute || st.position == Lage::Fixed) f |= KAusserFluss;
        const KArt art = eltern == kKein ? KArt::Block : artVon(st.anzeige, ersetzt);
        if (art == KArt::Inline && kn.raum == Raum::Html
            && ((m_dir != kKein && hatAttribut(el, m_dir)) || (m_bdi != kKein && kn.name == m_bdi)))
            f |= KIsoliert;
        const uint32_t b = neu(art, el, si, f);
        if (art != KArt::Inline) m_k.kaesten[b].rolle = rolleVon(st.anzeige);
        m_k.kastenVon[el] = b;
        haenge(eltern, b);
        if (!ersetzt) kinder(el, b, zahl);
        nachbereiten(b);
    }

    bool hatAttribut(uint32_t el, uint32_t name) const {
        bool da = false;
        m_d.attribut(el, name, &da);
        return da;
    }

    void kinder(uint32_t el, uint32_t b, int zahl) {
        const Stil& st = m_s.stile[m_s.jeKnoten[el]];
        const Knoten& kn = m_d.knoten[el];
        if (st.anzeige == Anzeige::ListItem && st.listenArt != ListenArt::None) marke(el, b, zahl);
        if (m_s.vor[el] != kKein) pseudo(el, m_s.vor[el], b, PVor);
        //  Geschlossenes details zeigt nur seine erste summary.
        const bool zu = kn.raum == Raum::Html && kn.name == NDetails
            && !(el < m_zustand.size() ? (m_zustand[el] & kOffen) : m_open != kKein && hatAttribut(el, m_open));
        bool summary = false;
        int z = 0;
        if (kn.raum == Raum::Html && kn.name == NOl && m_start != kKein) z = ganzzahl(m_d.attribut(el, m_start), 1) - 1;
        for (uint32_t c = kn.erstesKind; c != kKein; c = m_d.knoten[c].naechstes) {
            const Knoten& ck = m_d.knoten[c];
            if (ck.art == Art::Text) {
                if (zu || ck.anzahl == 0) continue;
                const uint32_t t = neu(KArt::Text, c, m_s.jeKnoten[c]);
                m_k.kaesten[t].anfang = ck.anfang;
                m_k.kaesten[t].laenge = ck.anzahl;
                m_k.kastenVon[c] = t;
                haenge(b, t);
                continue;
            }
            if (ck.art != Art::Element) continue;
            if (zu) {
                if (summary || ck.name != NSummary || ck.raum != Raum::Html) continue;
                summary = true;
            }
            if (m_s.stile[m_s.jeKnoten[c]].anzeige == Anzeige::ListItem) {
                bool da = false;
                const std::string_view v = m_value != kKein ? m_d.attribut(c, m_value, &da) : std::string_view();
                z = da ? ganzzahl(v, z + 1) : z + 1;
            }
            element(c, b, z);
        }
        if (m_s.nach[el] != kKein) pseudo(el, m_s.nach[el], b, PNach);
    }

    void marke(uint32_t el, uint32_t b, int zahl) {
        const uint32_t si = m_s.jeKnoten[el];
        const Stil& st = m_s.stile[si];
        const std::string t = markenText(st.listenArt, zahl);
        haenge(b, eigenerText(el, si, t, PMarke, st.listenInnen ? KArt::Text : KArt::Marke));
    }

    void pseudo(uint32_t el, uint32_t si, uint32_t b, uint8_t welche) {
        const Stil& ps = m_s.stile[si];
        if (ps.anzeige == Anzeige::None || spalte(ps.anzeige)) return;
        uint8_t f = 0;
        if (ps.position == Lage::Absolute || ps.position == Lage::Fixed) f |= KAusserFluss;
        const uint32_t p = neu(artVon(ps.anzeige, false), el, si, f, welche);
        if (m_k.kaesten[p].art != KArt::Inline) m_k.kaesten[p].rolle = rolleVon(ps.anzeige);
        haenge(b, p);
        const std::string t = ps.inhalt < m_s.roh.size() ? inhaltText(m_s.roh[ps.inhalt], el) : std::string();
        if (!t.empty()) haenge(p, eigenerText(el, si, t, welche));
        nachbereiten(p);
    }

    //  content: Zeichenketten mit CSS-Escapes, attr(), Anfuehrungszeichen; counter() und url() bleiben leer.
    std::string inhaltText(std::string_view r, uint32_t el) const {
        std::string aus;
        size_t i = 0;
        while (i < r.size()) {
            const char c = r[i];
            if (leer(c)) { ++i; continue; }
            if (c == '/') break;    // Ersatztext fuer Vorleser
            if (c == '"' || c == '\'') {
                for (++i; i < r.size() && r[i] != c;) {
                    if (r[i] != '\\' || i + 1 >= r.size()) { aus += r[i++]; continue; }
                    ++i;
                    if (!hex(r[i])) {
                        if (r[i] != '\n') aus += r[i];
                        ++i;
                        continue;
                    }
                    uint32_t cp = 0;
                    for (int n = 0; n < 6 && i < r.size() && hex(r[i]); ++n, ++i)
                        cp = cp * 16 + uint32_t(r[i] <= '9' ? r[i] - '0' : (r[i] | 0x20) - 'a' + 10);
                    if (i < r.size() && leer(r[i])) ++i;
                    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                    utf8(aus, cp);
                }
                ++i;
                continue;
            }
            const size_t a = i;
            while (i < r.size() && !leer(r[i]) && r[i] != '(') ++i;
            const std::string_view wort = r.substr(a, i - a);
            if (i < r.size() && r[i] == '(') {
                const size_t zu = r.find(')', i);
                std::string_view arg = r.substr(i + 1, (zu == std::string_view::npos ? r.size() : zu) - i - 1);
                i = zu == std::string_view::npos ? r.size() : zu + 1;
                if (!gleich(wort, "attr")) continue;
                while (!arg.empty() && leer(arg.front())) arg.remove_prefix(1);
                size_t n = 0;
                while (n < arg.size() && !leer(arg[n]) && arg[n] != ',') ++n;
                std::string name(arg.substr(0, n));
                for (char& x : name) x = klein(x);
                if (const uint32_t id = m_d.nameSuchen(name); id != kKein) aus += m_d.attribut(el, id);
                continue;
            }
            if (gleich(wort, "open-quote")) aus += "\xE2\x80\x9C";
            else if (gleich(wort, "close-quote")) aus += "\xE2\x80\x9D";
        }
        return aus;
    }

    bool nurLeerraum(const Kasten& k) const {
        if (k.art != KArt::Text || (k.flags & KEigenText)) return false;
        const Leerraum l = m_s.stile[k.stil].leerraum;
        if (l != Leerraum::Normal && l != Leerraum::NoWrap) return false;
        for (uint32_t i = 0; i < k.laenge; ++i)
            if (!leer(m_d.text[k.anfang + i])) return false;
        return true;
    }

    bool neutral(const Kasten& k) const { return (k.flags & KAusserFluss) || k.art == KArt::Marke; }

    void nachbereiten(uint32_t b) {
        Kasten& k = m_k.kaesten[b];
        if (k.art == KArt::Inline) {
            //  Block in einem Inline-Element: das Element wird selbst ein Block ohne eigene Raender.
            for (uint32_t c = k.erstesKind; c != kKein; c = m_k.kaesten[c].naechstes)
                if (m_k.kaesten[c].art == KArt::Block && !neutral(m_k.kaesten[c])) {
                    k.art = KArt::Block;
                    k.flags |= KAnonym;
                    break;
                }
            if (k.art == KArt::Inline) return;
        }
        if ((k.art != KArt::Block && k.art != KArt::Atom) || (k.flags & KErsetzt)) return;
        if (k.rolle == TTabelle || k.rolle == TZeile || istGruppe(k.rolle)) { ergaenze(b); return; }
        waisen(b);
        ordne(b);
    }

    uint32_t anonym(uint32_t eltern, uint8_t rolle) {
        const uint32_t a = neu(KArt::Block, kKein, m_k.kaesten[eltern].stil, KAnonym);
        m_k.kaesten[a].rolle = rolle;
        haenge(eltern, a);
        return a;
    }

    std::vector<uint32_t> loese(uint32_t b) {
        std::vector<uint32_t> alt;
        for (uint32_t c = m_k.kaesten[b].erstesKind; c != kKein; c = m_k.kaesten[c].naechstes) alt.push_back(c);
        m_k.kaesten[b].erstesKind = m_k.kaesten[b].letztesKind = kKein;
        return alt;
    }

    //  Tabelle, Gruppe, Zeile: was nicht hineinpasst, kommt in anonyme Zeilen bzw. Zellen; reiner Leerraum faellt weg.
    void ergaenze(uint32_t b) {
        const uint8_t r = m_k.kaesten[b].rolle;
        const auto passt = [r](uint8_t c) {
            if (r == TZeile) return c == TZelle;
            return c == TZeile || (r == TTabelle && (c == TTitel || istGruppe(c)));
        };
        std::vector<uint32_t> folge;
        const auto schliesse = [&] {
            bool inhalt = false;
            for (uint32_t x : folge) inhalt |= !nurLeerraum(m_k.kaesten[x]);
            if (inhalt) {
                const uint32_t a = anonym(b, r == TZeile ? TZelle : TZeile);
                for (uint32_t x : folge) haenge(a, x);
                if (r == TZeile) { waisen(a); ordne(a); }
                else ergaenze(a);
            }
            folge.clear();
        };
        for (uint32_t c : loese(b)) {
            const Kasten& k = m_k.kaesten[c];
            if (neutral(k)) haenge(b, c);
            else if (passt(k.rolle)) { schliesse(); haenge(b, c); }
            else folge.push_back(c);
        }
        schliesse();
    }

    //  Tabellenteile ohne passenden Rahmen: Zellen in anonyme Zeilen, dann Zeilen, Gruppen, Titel in eine anonyme
    //  Tabelle. Leerraum zwischen zwei Gliedern einer Folge faellt weg.
    void waisen(uint32_t b) {
        bool da = false;
        for (uint32_t c = m_k.kaesten[b].erstesKind; c != kKein && !da; c = m_k.kaesten[c].naechstes)
            da = m_k.kaesten[c].rolle > TTabelle && !neutral(m_k.kaesten[c]);
        if (!da) return;
        gruppiere(b, [](uint8_t r) { return r == TZelle; }, TZeile);
        gruppiere(b, [](uint8_t r) { return r == TZeile || r == TTitel || istGruppe(r); }, TTabelle);
    }

    void gruppiere(uint32_t b, bool (*glied)(uint8_t), uint8_t rolle) {
        uint32_t a = kKein;
        std::vector<uint32_t> leer;
        for (uint32_t c : loese(b)) {
            const Kasten& k = m_k.kaesten[c];
            if (glied(k.rolle) && !neutral(k)) {
                if (a == kKein) a = anonym(b, rolle);
                leer.clear();
                haenge(a, c);
            } else if (a != kKein && nurLeerraum(k)) {
                leer.push_back(c);
            } else {
                for (uint32_t x : leer) haenge(b, x);
                leer.clear();
                a = kKein;
                haenge(b, c);
            }
        }
        for (uint32_t x : leer) haenge(b, x);
    }

    //  Gemischte Kinder: Inline-Folgen kommen in anonyme Bloecke, Folgen aus reinem Leerraum fallen weg. In flex und
    //  grid wird jede Folge ein eigenes Element, auch ohne Block daneben.
    void ordne(uint32_t b) {
        bool block = false, zeile = false;
        for (uint32_t c = m_k.kaesten[b].erstesKind; c != kKein; c = m_k.kaesten[c].naechstes) {
            const Kasten& k = m_k.kaesten[c];
            if (neutral(k)) continue;
            (k.art == KArt::Block ? block : zeile) = true;
        }
        const Anzeige a = m_s.stile[m_k.kaesten[b].stil].anzeige;
        const bool felder = !(m_k.kaesten[b].flags & KAnonym)
                         && (a == Anzeige::Flex || a == Anzeige::InlineFlex || a == Anzeige::Grid || a == Anzeige::InlineGrid);
        if (!block && !felder) {
            if (zeile) m_k.kaesten[b].flags |= KZeilen;
            return;
        }
        if (!zeile) return;
        std::vector<uint32_t> alt;
        for (uint32_t c = m_k.kaesten[b].erstesKind; c != kKein; c = m_k.kaesten[c].naechstes) alt.push_back(c);
        m_k.kaesten[b].erstesKind = m_k.kaesten[b].letztesKind = kKein;
        std::vector<uint32_t> folge;
        const auto schliesse = [&] {
            bool inhalt = false;
            for (uint32_t x : folge) inhalt |= !neutral(m_k.kaesten[x]) && !nurLeerraum(m_k.kaesten[x]);
            if (!inhalt) {
                for (uint32_t x : folge)
                    if (neutral(m_k.kaesten[x])) haenge(b, x);
            } else {
                const uint32_t a = neu(KArt::Block, kKein, m_k.kaesten[b].stil, KAnonym | KZeilen);
                haenge(b, a);
                for (uint32_t x : folge) haenge(a, x);
            }
            folge.clear();
        };
        for (uint32_t c : alt) {
            const Kasten& k = m_k.kaesten[c];
            if (k.art == KArt::Block && !neutral(k)) {
                schliesse();
                haenge(b, c);
            } else {
                folge.push_back(c);
            }
        }
        schliesse();
    }

    const Dokument& m_d;
    const Stile& m_s;
    Kastenbaum& m_k;
    std::span<const uint8_t> m_zustand;
    uint32_t m_open = kKein, m_start = kKein, m_value = kKein, m_dir = kKein, m_bdi = kKein;
    std::vector<uint32_t> m_ersetzt;
};

}   // namespace

size_t Kastenbaum::bytes() const {
    return kaesten.capacity() * sizeof(Kasten) + kastenVon.capacity() * sizeof(uint32_t) + text.capacity();
}

Kastenbaum baueKaesten(const Dokument& d, const Stile& s, std::span<const uint8_t> zustand) {
    Kastenbaum k;
    k.kaesten.reserve(d.knoten.size() + d.knoten.size() / 8);
    Bauer(d, s, k, zustand).baue();
    return k;
}

}   // namespace mg::html
