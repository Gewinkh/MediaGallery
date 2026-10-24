#include "html/HtmlLayout.h"

#include "html/HtmlBild.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QRawFont>
#include <QTextLayout>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>

namespace mg::html {

namespace {

//  Kanten eines Inline-Kastens und Atome stehen als WORD JOINER im Absatztext, die Breite traegt ein absoluter
//  Buchstabenabstand: kein Umbruch, arabische Verbindungen bleiben ganz. Um Atome stehen ZWSP als Umbruchstellen.
constexpr char16_t kKante = 0x2060;
constexpr char16_t kTrenner = 0x200B;
constexpr char16_t kNbsp = 0x00A0;
constexpr uint32_t kMaxSpalten = 10000;     // Haertung: colspan-Lawinen

bool sichtbarAscii(char16_t z) { return z > 0x20 && z < 0x7F; }
bool asciiAlnum(char16_t z) { return (z >= u'0' && z <= u'9') || ((z | 0x20) >= u'a' && (z | 0x20) <= u'z'); }

//  Zwischen zwei sichtbaren ASCII-Zeichen bricht Chromium nach eigener Tabelle statt nach UAX #14: nur nach '-' und '?'
//  sowie vor oeffnender Klammer hinter Satzzeichen. Die Fuge zwingt Qt dorthin, nur wo es anders entscheidet (gemessen).
char16_t asciiFuge(char16_t vv, char16_t v, char16_t c) {
    const auto in = [](char16_t z, const char* m) { return std::strchr(m, char(z)) != nullptr; };
    const bool ziffer = c >= u'0' && c <= u'9';
    bool bricht;
    if (v == u'-') bricht = ziffer ? asciiAlnum(vv) : !in(c, "!$),./:;?]}");
    else if (v == u'?') bricht = !in(c, "!\"'),./:;?]}");
    else bricht = in(c, "(<[{") && in(v, "!\"#%&)*+,.:;=>\\]|}~");
    if (bricht)
        return in(c, "(<[{") || (v == u'-' && (ziffer || in(c, "\"'-|"))) || (v == u'?' && in(c, "-|")) ? kTrenner : 0;
    return in(v, "!/|}$") || (in(c, "$%+\\") && !asciiAlnum(v)) ? kKante : 0;
}

float px(int32_t v) { return v / float(kQ); }

bool bestimmt(const Laenge& l, float bezug) {
    return l.art == LArt::Wert && ((!l.mitProzent && l.prozent == 0) || bezug >= 0);
}
float aufloesen(const Laenge& l, float bezug) {
    return (l.px + (bezug > 0 ? l.prozent * bezug / 100.f : 0.f)) / kQ;
}

struct Strut {
    float pos = 0, neg = 0;
    void add(float m) { if (m > 0) pos = std::max(pos, m); else neg = std::min(neg, m); }
    void add(const Strut& o) { pos = std::max(pos, o.pos); neg = std::min(neg, o.neg); }
    float wert() const { return pos + neg; }
};

//  oben, rechts, unten, links
struct Kanten {
    float a[4] = {}, r[4] = {}, i[4] = {};
    bool autoL = false, autoR = false;
    float horizontal() const { return r[1] + r[3] + i[1] + i[3]; }
    float vertikal() const { return r[0] + r[2] + i[0] + i[2]; }
};

//  Masse ungerundet in CSS-px; `skaliert`/`format` gelten fuer die Skala des zuletzt gelegten Absatzes.
struct Schrift {
    QFont font;                         // ohne Groesse
    float groesse = 16, abstand = 0, eigeneSkala = 1;
    float oben = 0, unten = 0, luecke = 0, xHoehe = 0, mittel = 0, breitest = 0, leer = 0;
    mutable float skala = 0;
    mutable QFont skaliert;
    mutable QTextCharFormat format;
    float normal() const { return oben + unten + luecke; }
};

//  Qt rundet Schriftgroessen auf ganze Pixel (14,5 px misst wie 15). Gelegt wird deshalb in s-facher Groesse, mit s so,
//  dass alle Groessen des Absatzes moeglichst ganz werden. Ueber 256 px wird Qts Schrift viermal langsamer.
float besteSkala(const std::vector<float>& groessen) {
    float gross = 0;
    for (float g : groessen) gross = std::max(gross, g);
    if (gross <= 0) return 1;
    const int obergrenze = std::clamp(int(256 / gross), 1, 64);
    int beste = 1;
    float fehler = 2;
    for (int s = obergrenze; s >= 1; --s) {
        float f = 0;
        for (float g : groessen)
            if (g > 0) f = std::max(f, std::abs(g * s - std::round(g * s)) / (g * s));
        if (f < fehler - 1e-7f) { fehler = f; beste = s; }
    }
    return float(beste);
}

struct Intr { float min = 0, max = 0; };

enum class Modus : uint8_t { Legen, Min, Max };

//  Senkrechter Beitrag eines Inline-Kastens oder Atoms zu den Zeilen, die [anfang, ende) beruehren.
struct Beitrag {
    int32_t anfang = 0, ende = 0;
    float oben = 0, unten = 0;
};

struct Rand {
    uint32_t kasten;
    int32_t anfang, ende;
    float verschiebung, oben, unten;    // Inhaltsbereich ueber und unter der Grundlinie samt Innenabstand und Rahmen
    float links, rechts;                // Aussenabstand an Anfang und Ende
};

struct AtomLage {
    uint32_t kasten;
    int32_t pos;
    float links, oben;                  // Aussenabstand links, Randkasten-Oberkante ab der Grundlinie
    uint32_t absatzVon, absatzBis;
};

struct Sammlung {
    QString text;
    QList<QTextLayout::FormatRange> formate;
    std::vector<Stueck> stuecke;
    std::vector<Beitrag> beitraege;
    std::vector<Rand> raender;
    std::vector<AtomLage> atome;
    std::vector<std::pair<uint32_t, int32_t>> ausserFluss, umbrueche;
    bool inhalt = false, leerZuletzt = true, umbricht = true;
    int32_t leerPos = -1;
    float s = 1;                        // Skala aller Laengen im QTextLayout
    const void* letzteHerkunft = nullptr;
};

class Leger {
public:
    Leger(const Dokument& d, const Stile& s, const Kastenbaum& k, const Medium& m, float breite, Layout& aus)
        : m_d(d), m_s(s), m_k(k), m_m(m), m_breite(breite), m_aus(aus) {
        //  QFont nimmt Bruchteile nur als Punkt an; die Punkte rechnet Qt mit seiner Standard-DPI in Pixel um.
        //  Ein eigenes Zeichengeraet fuer 72 dpi ginge auch, dann verwirft QTextLayout aber jeden Buchstabenabstand.
        QFont probe;
        probe.setPointSizeF(72);
        m_ptJePx = 72.f / float(std::max(1, QFontInfo(probe).pixelSize()));
        m_schriftJeStil.assign(s.stile.size(), kKein);
        m_intr.assign(k.kaesten.size(), Intr{ -1, -1 });
        m_bezug.assign(k.kaesten.size(), kKein);
        m_attrBreite = d.nameSuchen("width");
        m_attrSrc = d.nameSuchen("src");
        m_attrHoehe = d.nameSuchen("height");
        m_attrViewBox = d.nameSuchen("viewbox");
        m_attrTyp = d.nameSuchen("type");
        m_attrWert = d.nameSuchen("value");
        m_attrSize = d.nameSuchen("size");
        m_attrCols = d.nameSuchen("cols");
        m_attrRows = d.nameSuchen("rows");
        m_attrColspan = d.nameSuchen("colspan");
        m_attrRowspan = d.nameSuchen("rowspan");
    }

    void lege() {
        m_aus.lage.assign(m_k.kaesten.size(), Rechteck{});
        m_aus.breite = m_breite;
        m_aus.hoehe = m_aus.fensterHoehe = float(m_m.hoehe);
        if (m_k.kaesten.empty()) return;
        const Erg r = block(0, m_breite, float(m_m.hoehe), -1, -1);
        m_aus.lage[0].x = r.links;
        m_aus.lage[0].y = r.oben.wert() + r.versatz;
        absolute(0, kKein);
        absolutieren();
        marken();
        klebe();
    }

private:
    struct Erg {
        float hoehe = 0, links = 0, grundlinie = -1, erste = -1;
        float natur = 0;                // Hoehe allein aus dem Inhalt, ohne height (Mindestmass in flex-Spalten)
        float versatz = 0;              // Tabellentitel ueber dem Randkasten: um so viel tiefer setzen
        Strut oben, unten;
        bool leer = false;
    };
    struct Absolut {
        uint32_t kasten, bezug, rahmen;
        float x, y;
    };

    const Kasten& kasten(uint32_t k) const { return m_k.kaesten[k]; }
    const Stil& stil(uint32_t k) const { return m_s.stile[m_k.kaesten[k].stil]; }
    bool anonym(uint32_t k) const { return m_k.kaesten[k].flags & KAnonym; }

    Kanten kanten(uint32_t k, float cbB) const {
        Kanten e;
        const LRahmen* lr = !m_aus.rahmenVon.empty() && m_aus.rahmenVon[k] != kKein ? &m_aus.rahmen[m_aus.rahmenVon[k]]
                                                                                   : nullptr;
        if (anonym(k)) {
            if (lr) std::copy(lr->halb, lr->halb + 4, e.r);
            return e;
        }
        const Stil& s = stil(k);
        for (int i = 0; i < 4; ++i) {
            if (s.aussen[i].art == LArt::Auto) (i == 3 ? e.autoL : e.autoR) |= (i == 1 || i == 3);
            else e.a[i] = aufloesen(s.aussen[i], cbB);
            e.r[i] = lr ? lr->halb[i] : px(s.rahmenBreite[i]);
            e.i[i] = lr && lr->ohneInnen ? 0 : aufloesen(s.innen[i], cbB);
        }
        //  Zeilen, Gruppen und Zellen haben keinen Aussenabstand.
        const uint8_t r = kasten(k).rolle;
        if (r == TZeile || r == TZelle || istGruppe(r)) std::fill(e.a, e.a + 4, 0.f);
        return e;
    }

    bool istBfc(uint32_t k) const {
        const Kasten& K = kasten(k);
        if (k == 0 || (K.flags & (KAusserFluss | KErsetzt)) || K.art == KArt::Atom || K.rolle != TKeine) return true;
        if (K.flags & KAnonym) return false;
        const Stil& s = stil(k);
        if (s.ueberlaufX != Ueberlauf::Visible || s.ueberlaufY != Ueberlauf::Visible) return true;
        if (s.anzeige != Anzeige::Block && s.anzeige != Anzeige::ListItem) return true;
        if (K.eltern == kKein) return false;
        const Anzeige e = stil(K.eltern).anzeige;
        return e == Anzeige::Flex || e == Anzeige::InlineFlex || e == Anzeige::Grid || e == Anzeige::InlineGrid;
    }

    // ---- Schriften ----

    const Schrift& schrift(uint32_t stilIndex) {
        uint32_t& idx = m_schriftJeStil[stilIndex];
        if (idx != kKein) return m_schriften[idx];
        const Stil& s = m_s.stile[stilIndex];
        const auto schluessel = std::array<int32_t, 6>{ int32_t(s.schrift), s.schriftGroesse, s.gewicht,
            int32_t(s.schriftStil), s.buchstabenAbstand, int32_t(s.textTransform) };
        std::string sk(reinterpret_cast<const char*>(schluessel.data()), sizeof(schluessel));
        if (const auto it = m_schriftIndex.find(sk); it != m_schriftIndex.end()) return m_schriften[idx = it->second];
        Schrift f;
        const QStringList fam = m_wahl.familien(s.schrift < m_s.roh.size() ? m_s.roh[s.schrift] : std::string());
        const bool kursiv = s.schriftStil != SchriftStil::Normal;
        f.font.setFamilies(fam);
        f.font.setWeight(QFont::Weight(std::clamp(m_wahl.gewicht(fam.value(0), s.gewicht, kursiv), 1, 1000)));
        f.font.setStyle(s.schriftStil == SchriftStil::Italic ? QFont::StyleItalic
                        : s.schriftStil == SchriftStil::Oblique ? QFont::StyleOblique : QFont::StyleNormal);
        f.font.setHintingPreference(QFont::PreferNoHinting);
        static const QFont::Capitalization gross[] = { QFont::MixedCase, QFont::AllUppercase, QFont::AllLowercase,
                                                       QFont::Capitalize };
        f.font.setCapitalization(gross[int(s.textTransform)]);
        f.groesse = px(s.schriftGroesse);
        f.abstand = px(s.buchstabenAbstand);
        f.eigeneSkala = besteSkala({ f.groesse });
        const float k = f.eigeneSkala;
        const QFontMetricsF fm(skaliert(f, k));
        //  Wie Chromium: Ober- und Unterlaenge und Zeilenluecke einzeln gerundet.
        f.oben = std::round(float(fm.ascent()) / k);
        f.unten = std::round(float(fm.descent()) / k);
        f.luecke = std::round(float(fm.leading()) / k);
        f.xHoehe = float(fm.xHeight()) / k;
        f.mittel = float(fm.averageCharWidth()) / k;
        f.breitest = float(fm.maxWidth()) / k;
        //  Chromium: OS/2 xAvgCharWidth und die Breite des head-Rahmens, beide gerundet; Qt nimmt den groessten Vorschub.
        const QRawFont roh = QRawFont::fromFont(skaliert(f, k));
        const QByteArray os2 = roh.fontTable("OS/2"), head = roh.fontTable("head");
        const auto be16 = [](const QByteArray& b, int i) { return int16_t(uint8_t(b[i]) << 8 | uint8_t(b[i + 1])); };
        if (head.size() >= 44) {
            const float em = float(uint16_t(be16(head, 18)));
            if (em > 0) {
                f.breitest = std::round(float(be16(head, 40) - be16(head, 36)) * f.groesse / em);
                if (os2.size() >= 4 && be16(os2, 2) > 0) f.mittel = std::round(float(be16(os2, 2)) * f.groesse / em);
            }
        }
        f.leer = float(fm.horizontalAdvance(QLatin1Char(' '))) / k;
        m_schriften.push_back(std::move(f));
        m_schriftIndex.emplace(std::move(sk), uint32_t(m_schriften.size() - 1));
        return m_schriften[idx = uint32_t(m_schriften.size() - 1)];
    }

    const QFont& skaliert(const Schrift& f, float s) const {
        if (f.skala != s) {
            f.skala = s;
            f.skaliert = f.font;
            f.skaliert.setPointSizeF(std::max(0.01f, f.groesse * s * m_ptJePx));
            if (f.abstand != 0) f.skaliert.setLetterSpacing(QFont::AbsoluteSpacing, f.abstand * s);
            f.format = QTextCharFormat();
            f.format.setFont(f.skaliert, QTextCharFormat::FontPropertiesAll);
        }
        return f.skaliert;
    }
    const QTextCharFormat& format(const Schrift& f, float s) const {
        skaliert(f, s);
        return f.format;
    }

    float textBreite(const Schrift& f, const QString& t) const {
        return float(QFontMetricsF(skaliert(f, f.eigeneSkala)).horizontalAdvance(t)) / f.eigeneSkala;
    }

    //  Skala eines Absatzes aus den Schriftgroessen seines Blocks und seiner Inline-Kaesten.
    float skalaFuer(uint32_t k) {
        std::vector<float>& g = m_groessen;
        g.clear();
        g.push_back(schrift(kasten(k).stil).groesse);
        std::vector<uint32_t>& stapel = m_stapel;
        stapel.assign(1, k);
        while (!stapel.empty()) {
            const uint32_t x = stapel.back();
            stapel.pop_back();
            for (uint32_t c = kasten(x).erstesKind; c != kKein; c = kasten(c).naechstes)
                if (kasten(c).art == KArt::Inline && !(kasten(c).flags & KAusserFluss)) {
                    const float v = schrift(kasten(c).stil).groesse;
                    if (std::find(g.begin(), g.end(), v) == g.end()) g.push_back(v);
                    stapel.push_back(c);
                }
        }
        return besteSkala(g);
    }

    float zeilenHoehe(const Stil& s, const Schrift& f) const {
        if (s.zeilenHoehe.art == LArt::Zahl) return s.zeilenHoehe.px / float(kQZahl) * px(s.schriftGroesse);
        if (s.zeilenHoehe.art == LArt::Wert) return px(s.zeilenHoehe.px);
        return f.normal();
    }

    // ---- ersetzte Elemente ----

    float attrZahl(uint32_t el, uint32_t name, float bezug, bool& da) const {
        da = false;
        if (name == kKein) return 0;
        bool steht = false;
        std::string_view v = m_d.attribut(el, name, &steht);
        while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
        float z = 0;
        const auto r = std::from_chars(v.data(), v.data() + v.size(), z);
        if (!steht || r.ec != std::errc() || z < 0) return 0;
        da = true;
        if (r.ptr < v.data() + v.size() && *r.ptr == '%') {
            if (bezug < 0) { da = false; return 0; }
            return z * bezug / 100;
        }
        return z;
    }

    //  Inhaltsmasse eines ersetzten Elements; CSS-Breite und -Hoehe gehen vor, das Seitenverhaeltnis bleibt.
    void ersetzt(uint32_t k, float cbB, float cbH, float& w, float& h) {
        const Kasten& K = kasten(k);
        const Stil& s = stil(k);
        const Knoten& kn = m_d.knoten[K.knoten];
        const std::string_view name = m_d.nameVon(kn.name);
        float iw = 300, ih = 150;
        bool verhaeltnis = false;
        float bildVerhaeltnis = 0;
        const Schrift& f = schrift(K.stil);
        const float zeile = zeilenHoehe(s, f);
        if (kn.name == NInput && kn.raum == Raum::Html) {
            std::string typ(m_attrTyp != kKein ? m_d.attribut(K.knoten, m_attrTyp) : std::string_view());
            for (char& c : typ) c = char(std::tolower(static_cast<unsigned char>(c)));
            bool da = false;
            const float groesse = attrZahl(K.knoten, m_attrSize, -1, da);
            if (typ == "checkbox" || typ == "radio") { iw = ih = 13; }
            else if (typ == "range") { iw = 129; ih = 16; }
            else if (typ == "color") { iw = 44; ih = 23; }
            else if (typ == "button" || typ == "submit" || typ == "reset") {
                const QString t = QString::fromUtf8(m_attrWert != kKein ? m_d.attribut(K.knoten, m_attrWert) : std::string_view());
                iw = textBreite(f, t);
                ih = zeile;
            } else {
                //  Wie Chromium: size Zeichen mittlerer Breite, dazu der Ueberhang des breitesten Zeichens.
                iw = (da && groesse > 0 ? groesse : 20) * f.mittel + std::max(0.f, f.breitest - f.mittel);
                ih = zeile;
            }
        } else if (kn.name == NTextarea && kn.raum == Raum::Html) {
            bool da = false;
            const float spalten = attrZahl(K.knoten, m_attrCols, -1, da);
            iw = (da && spalten > 0 ? spalten : 20) * f.mittel;
            const float zeilen = attrZahl(K.knoten, m_attrRows, -1, da);
            ih = (da && zeilen > 0 ? zeilen : 2) * zeile;
        } else if (name == "select") {
            iw = 4 * f.mittel + 20;
            ih = zeile;
        } else if (kn.name == NImg) {
            //  Eigene Groesse des Bilds; fehlt eine Seite (SVG nur mit viewBox), folgt sie dem Verhaeltnis.
            bool da = false;
            const std::string_view src = m_attrSrc != kKein ? m_d.attribut(K.knoten, m_attrSrc, &da) : std::string_view();
            const Masse m = da ? bildMasse(src, m_d.basis) : Masse{};
            bildVerhaeltnis = m.verhaeltnis;
            iw = m.b > 0 ? m.b : m.h > 0 && m.verhaeltnis > 0 ? m.h * m.verhaeltnis : 0;
            ih = m.h > 0 ? m.h : m.b > 0 && m.verhaeltnis > 0 ? m.b / m.verhaeltnis : 0;
        }
        if (kn.name == NSvg || kn.name == NImg || name == "canvas" || name == "video" || kn.name == NIframe) {
            bool daB = false, daH = false;
            const float aw = attrZahl(K.knoten, m_attrBreite, cbB, daB);
            const float ah = attrZahl(K.knoten, m_attrHoehe, cbH, daH);
            float vb[4] = {};
            int n = 0;
            if (kn.name == NSvg && m_attrViewBox != kKein) {
                std::string_view v = m_d.attribut(K.knoten, m_attrViewBox);
                while (n < 4 && !v.empty()) {
                    while (!v.empty() && (v.front() == ' ' || v.front() == ',')) v.remove_prefix(1);
                    const auto r = std::from_chars(v.data(), v.data() + v.size(), vb[n]);
                    if (r.ec != std::errc()) break;
                    v.remove_prefix(size_t(r.ptr - v.data()));
                    ++n;
                }
            }
            if (bildVerhaeltnis > 0) { vb[2] = bildVerhaeltnis; vb[3] = 1; n = 4; }
            const bool box = n == 4 && vb[2] > 0 && vb[3] > 0;
            //  Ein <img> ohne eigenes Verhaeltnis behaelt bei nur einer Angabe seine eigene andere Seite.
            const bool img = kn.name == NImg;
            if (daB && daH) { iw = aw; ih = ah; verhaeltnis = iw > 0 && ih > 0; }
            else if (daB) { ih = box ? aw * vb[3] / vb[2] : img ? ih : 150; iw = aw; verhaeltnis = box; }
            else if (daH) { iw = box ? ah * vb[2] / vb[3] : img ? iw : 300; ih = ah; verhaeltnis = box; }
            else if (box) verhaeltnis = true;
        }
        const Kanten e = kanten(k, cbB);
        const bool bw = bestimmt(s.breite, cbB), bh = bestimmt(s.hoehe, cbH);
        w = bw ? aufloesen(s.breite, cbB) - (s.randBox ? e.horizontal() : 0) : -1;
        h = bh ? aufloesen(s.hoehe, cbH) - (s.randBox ? e.vertikal() : 0) : -1;
        if (w < 0 && h < 0) { w = iw; h = ih; }
        else if (w < 0) w = verhaeltnis && ih > 0 ? h * iw / ih : iw;
        else if (h < 0) h = verhaeltnis && iw > 0 ? w * ih / iw : ih;
        w = std::max(0.f, w);
        h = std::max(0.f, h);
    }

    //  Textfelder und Auswahllisten tragen die Grundlinie ihres Texts, mittig im Inhalt; alle anderen ersetzten
    //  Elemente stehen mit der Unterkante auf der Grundlinie.
    float textGrundlinie(uint32_t k, float inhaltH) {
        const Kasten& K = kasten(k);
        const Knoten& kn = m_d.knoten[K.knoten];
        if (kn.raum != Raum::Html) return -1;
        if (kn.name == NInput) {
            std::string typ(m_attrTyp != kKein ? m_d.attribut(K.knoten, m_attrTyp) : std::string_view());
            for (char& c : typ) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (typ == "checkbox" || typ == "radio" || typ == "range" || typ == "color" || typ == "image") return -1;
        } else if (m_d.nameVon(kn.name) != "select") {
            return -1;
        }
        const Schrift& f = schrift(K.stil);
        const Stil& s = stil(k);
        return px(s.rahmenBreite[0]) + aufloesen(s.innen[0], -1) + (inhaltH - (f.oben + f.unten)) / 2 + f.oben;
    }

    // ---- Breiten ----

    //  Randkasten-Breite aus width/min-width/max-width; `fuellen` gilt fuer auto.
    float randBreite(uint32_t k, const Kanten& e, float cbB, float fuellen) {
        if (anonym(k)) return fuellen;
        const Stil& s = stil(k);
        const float h = e.horizontal();
        const auto zuRand = [&](float v) { return s.randBox ? std::max(v, h) : v + h; };
        float w = fuellen;
        switch (s.breite.art) {
        case LArt::Wert: if (bestimmt(s.breite, cbB)) w = zuRand(aufloesen(s.breite, cbB)); break;
        case LArt::MinInhalt: w = intrinsisch(k).min; break;
        case LArt::MaxInhalt: w = intrinsisch(k).max; break;
        case LArt::Passend: { const Intr i = intrinsisch(k); w = std::clamp(fuellen, i.min, std::max(i.min, i.max)); break; }
        default:
            if (kasten(k).flags & KErsetzt) {
                float iw, ih;
                ersetzt(k, cbB, -1, iw, ih);
                w = iw + h;
            }
            break;
        }
        if (s.maxBreite.art == LArt::Wert && bestimmt(s.maxBreite, cbB)) w = std::min(w, zuRand(aufloesen(s.maxBreite, cbB)));
        if (s.minBreite.art == LArt::Wert && bestimmt(s.minBreite, cbB)) w = std::max(w, zuRand(aufloesen(s.minBreite, cbB)));
        if (kasten(k).rolle == TTabelle) w = std::max(w, intrinsisch(k).min);
        return std::max(w, h);
    }

    Intr intrinsisch(uint32_t k) {
        if (m_intr[k].min >= 0) return m_intr[k];
        return m_intr[k] = inhaltsBreiten(k, false);
    }

    //  `ohneBreite`: was der Inhalt braucht, auch wenn width fest ist (automatisches Minimum in flex).
    Intr inhaltsBreiten(uint32_t k, bool ohneBreite) {
        const Kasten& K = kasten(k);
        const Stil& s = stil(k);
        if (K.rolle == TTabelle) return tabellenBreiten(k);
        const Kanten e = kanten(k, -1);
        Intr r;
        const float h = e.horizontal();
        if (K.flags & KErsetzt) {
            float iw, ih;
            ersetzt(k, -1, -1, iw, ih);
            r.min = r.max = iw + h;
        } else if (!ohneBreite && !anonym(k) && K.rolle != TZelle && s.breite.art == LArt::Wert && bestimmt(s.breite, -1)) {
            r.min = r.max = s.randBox ? std::max(px(s.breite.px), h) : px(s.breite.px) + h;
        } else {
            if (K.flags & KZeilen) {
                Sammlung sm;
                sammle(k, -1, Modus::Max, sm);
                r.max = absatzBreite(k, sm, 1e7f);
                if (sm.umbricht) {
                    Sammlung mn;
                    sammle(k, -1, Modus::Min, mn);
                    r.min = absatzBreite(k, mn, 0);
                } else {
                    r.min = r.max;
                }
            } else if (istGrid(k)) {
                r = gridBreiten(k);
            } else {
                //  Flex in Zeilen: Summe der Beitraege, mit Umbruch min-content des breitesten.
                const bool reihe = istFlex(k) && zeilenFlex(k);
                float summeMin = 0, summeMax = 0;
                uint32_t n = 0;
                for (uint32_t c = K.erstesKind; c != kKein; c = kasten(c).naechstes) {
                    const Kasten& C = kasten(c);
                    if ((C.flags & KAusserFluss) || C.art == KArt::Marke) continue;
                    const Intr ci = intrinsisch(c);
                    const Kanten ec = kanten(c, -1);
                    const float m = ec.a[1] + ec.a[3];
                    r.min = std::max(r.min, ci.min + m);
                    r.max = std::max(r.max, ci.max + m);
                    summeMin += ci.min + m;
                    summeMax += ci.max + m;
                    ++n;
                }
                if (reihe) {
                    const float l = n > 1 ? luecke(s.spaltenAbstand, -1) * float(n - 1) : 0;
                    r.max = summeMax + l;
                    if (s.flexUmbruch == FlexUmbruch::NoWrap) r.min = summeMin + l;
                }
            }
            r.min += h;
            r.max += h;
            if (!anonym(k)) {
                const auto zuRand = [&](float v) { return s.randBox ? std::max(v, h) : v + h; };
                if (s.maxBreite.art == LArt::Wert && bestimmt(s.maxBreite, -1) && K.rolle != TZelle) {
                    const float m = zuRand(px(s.maxBreite.px));
                    r.min = std::min(r.min, m);
                    r.max = std::min(r.max, m);
                }
                if (s.minBreite.art == LArt::Wert && bestimmt(s.minBreite, -1)) {
                    const float m = zuRand(px(s.minBreite.px));
                    r.min = std::max(r.min, m);
                    r.max = std::max(r.max, m);
                }
            }
        }
        r.max = std::max(r.max, r.min);
        return r;
    }

    // ---- Blockfluss ----

    Erg block(uint32_t k, float cbB, float cbH, float vorgabeB, float vorgabeH) {
        ++m_bloecke;
        const Kasten& K = kasten(k);
        const Stil& s = stil(k);
        const bool istTabelle = K.rolle == TTabelle;
        if (istTabelle) struktur(k);
        const Kanten e = kanten(k, cbB);
        const bool ist = K.flags & KErsetzt;
        Erg r;
        float ml = e.autoL ? 0 : e.a[3], mr = e.autoR ? 0 : e.a[1];
        float bb;
        if (vorgabeB >= 0) {
            bb = vorgabeB;
        } else {
            float fuellen = std::max(0.f, cbB - ml - mr);
            if (istTabelle) {
                const Intr in = intrinsisch(k);
                fuellen = std::min(std::max(in.min, fuellen), in.max);
            }
            bb = randBreite(k, e, cbB, fuellen);
            const float rest = cbB - bb - ml - mr;
            if (e.autoL && e.autoR) ml = mr = std::max(0.f, rest) / 2;
            else if (e.autoL) ml = std::max(0.f, rest);
            else if (!e.autoR && K.eltern != kKein && stil(K.eltern).richtung == Richtung::Rtl) ml += rest;
        }
        r.links = ml;
        const float cw = std::max(0.f, bb - e.horizontal());
        float ch = -1;
        if (vorgabeH >= 0) ch = std::max(0.f, vorgabeH - e.vertikal());
        else if (!anonym(k) && bestimmt(s.hoehe, cbH))
            ch = std::max(0.f, aufloesen(s.hoehe, cbH) - (s.randBox ? e.vertikal() : 0));
        //  In Tabellen ist height eine Untergrenze: Zellen und Tabelle wachsen mit dem Inhalt.
        float mindestH = -1;
        if (istTabelle || K.rolle == TZelle) std::swap(ch, mindestH);
        const float x0 = e.r[3] + e.i[3], y0 = e.r[0] + e.i[0];
        const size_t absAnfang = m_absolut.size();
        const bool bfc = istBfc(k);
        const bool obenZu = bfc || y0 > 0;
        const bool untenZu = bfc || e.r[2] + e.i[2] > 0 || ch >= 0;
        Strut oben, unten;
        oben.add(e.a[0]);
        float inhaltH = 0;
        bool inhalt = false;
        float grund = -1, erste = -1, titelOben = 0;
        if (ist) {
            float iw, ih;
            ersetzt(k, cbB, cbH, iw, ih);
            if (ch < 0) ch = ih;
            inhalt = true;
            grund = textGrundlinie(k, ch);
            erste = grund;
        } else if (istTabelle) {
            inhaltH = legeTabelle(k, cw, mindestH, x0, y0, bb, erste, titelOben);
            grund = erste;
            inhalt = true;
        } else if (istFlex(k) || istGrid(k)) {
            float mnH, mxH;
            hoehenGrenzen(k, e, cbH, mnH, mxH);
            inhaltH = istFlex(k) ? legeFlex(k, cw, ch, mnH, mxH, x0, y0, erste) : legeGrid(k, cw, ch, mnH, mxH, x0, y0, erste);
            grund = erste;
            inhalt = true;
        } else if (K.flags & KZeilen) {
            const size_t z0 = m_aus.zeilen.size();
            inhalt = zeilen(k, cw, x0, y0, inhaltH, grund);
            if (m_aus.zeilen.size() > z0) erste = m_aus.zeilen[z0].y + m_aus.zeilen[z0].grundlinie;
        } else {
            float y = y0;
            Strut lauf;
            for (uint32_t c = K.erstesKind; c != kKein; c = kasten(c).naechstes) {
                const Kasten& C = kasten(c);
                if (C.art == KArt::Marke) { m_bezug[c] = k; continue; }
                if (C.flags & KAusserFluss) {
                    merke(c, k, x0, (inhalt || obenZu) ? y + lauf.wert() : y);
                    continue;
                }
                const Erg rc = block(c, cw, ch, -1, -1);
                Strut sg = lauf;
                sg.add(rc.oben);
                float cy;
                if (rc.leer) {
                    sg.add(rc.unten);
                    if (!inhalt && !obenZu) { oben.add(sg); lauf = Strut{}; cy = y; }
                    else { lauf = sg; cy = y + sg.wert(); }
                } else {
                    if (!inhalt && !obenZu) { oben.add(sg); cy = y; }
                    else cy = y + sg.wert();
                    y = cy + rc.hoehe;
                    lauf = rc.unten;
                    inhalt = true;
                    if (rc.grundlinie >= 0) grund = cy + rc.grundlinie;
                    if (erste < 0 && rc.erste >= 0) erste = cy + rc.erste;
                }
                setze(c, k, x0 + rc.links, cy + rc.versatz);
                versetze(c, cw, ch);
            }
            if (!untenZu) { unten = lauf; inhaltH = y - y0; }
            else inhaltH = y + lauf.wert() - y0;
        }
        float h = ch >= 0 ? ch : std::max(std::max(0.f, inhaltH), mindestH);
        if (!anonym(k)) {
            const float v = s.randBox ? e.vertikal() : 0;
            if (s.maxHoehe.art == LArt::Wert && bestimmt(s.maxHoehe, cbH))
                h = std::min(h, std::max(0.f, aufloesen(s.maxHoehe, cbH) - v));
            if (s.minHoehe.art == LArt::Wert && bestimmt(s.minHoehe, cbH))
                h = std::max(h, std::max(0.f, aufloesen(s.minHoehe, cbH) - v));
        }
        unten.add(e.a[2]);
        r.leer = !inhalt && !bfc && h == 0 && y0 == 0 && e.r[2] + e.i[2] == 0;
        r.oben = oben;
        r.unten = unten;
        r.hoehe = h + e.vertikal();
        r.natur = (ist ? h : std::max(0.f, inhaltH)) + e.vertikal();
        r.grundlinie = grund;
        r.erste = erste;
        m_aus.lage[k].b = bb;
        m_aus.lage[k].h = r.hoehe;
        if (istTabelle) {
            r.hoehe += titelOben + titelUnten(k, r.hoehe);
            r.versatz = titelOben;
            if (r.grundlinie >= 0) r.grundlinie += titelOben;
            if (r.erste >= 0) r.erste += titelOben;
        }
        if (istBezug(k)) absolute(absAnfang, k);
        return r;
    }

    // ---- Flex und Grid ----

    bool istFlex(uint32_t k) const {
        if (anonym(k) || (kasten(k).flags & KErsetzt)) return false;
        const Anzeige a = stil(k).anzeige;
        return a == Anzeige::Flex || a == Anzeige::InlineFlex;
    }
    bool istGrid(uint32_t k) const {
        if (anonym(k) || (kasten(k).flags & KErsetzt)) return false;
        const Anzeige a = stil(k).anzeige;
        return a == Anzeige::Grid || a == Anzeige::InlineGrid;
    }
    bool zeilenFlex(uint32_t k) const {
        const FlexRichtung r = stil(k).flexRichtung;
        return r == FlexRichtung::Row || r == FlexRichtung::RowReverse;
    }

    static float luecke(const Laenge& l, float bezug) {
        return l.art == LArt::Wert && bestimmt(l, bezug) ? std::max(0.f, aufloesen(l, bezug)) : 0.f;
    }

    //  min-/max-height als Inhaltshoehe; ohne bestimmte Bezugshoehe gelten nur feste Werte.
    void hoehenGrenzen(uint32_t k, const Kanten& e, float cbH, float& mn, float& mx) const {
        mn = 0;
        mx = INFINITY;
        if (anonym(k)) return;
        const Stil& s = stil(k);
        const float v = s.randBox ? e.vertikal() : 0;
        if (s.minHoehe.art == LArt::Wert && bestimmt(s.minHoehe, cbH)) mn = std::max(0.f, aufloesen(s.minHoehe, cbH) - v);
        if (s.maxHoehe.art == LArt::Wert && bestimmt(s.maxHoehe, cbH)) mx = std::max(mn, aufloesen(s.maxHoehe, cbH) - v);
    }
    //  Dasselbe als Randkasten.
    float randHoehe(uint32_t k, const Kanten& e, float cbH, float h) const {
        float mn, mx;
        hoehenGrenzen(k, e, cbH, mn, mx);
        return std::clamp(h, mn + e.vertikal(), mx + e.vertikal());
    }

    Ausrichtung querVon(uint32_t c, uint32_t k) const {
        const Ausrichtung a = anonym(c) ? Ausrichtung::Auto : stil(c).selbstAusrichtung;
        return a == Ausrichtung::Auto ? stil(k).elementAusrichtung : a;
    }
    static bool dehnt(Ausrichtung a) { return a == Ausrichtung::Normal || a == Ausrichtung::Stretch; }
    static bool amEnde(Ausrichtung a) {
        return a == Ausrichtung::End || a == Ausrichtung::FlexEnd;
    }

    //  Die Ausgabe bis hierher. Haengt der Inhalt eines Elements von der Groesse ab, die es erst beim Ausrichten
    //  bekommt, wird alles ab dem Stand verworfen und mit den endgueltigen Groessen neu gelegt.
    struct Stand {
        size_t zeilen, absaetze, laeufe, glyphen, fragmente, absolut;
    };
    Stand stand() const {
        return { m_aus.zeilen.size(), m_aus.absaetze.size(), m_aus.laeufe.size(), m_aus.glyphen.size(),
                 m_aus.fragmente.size(), m_absolut.size() };
    }
    void zurueck(const Stand& st) {
        m_aus.zeilen.resize(st.zeilen);
        m_aus.absaetze.resize(st.absaetze);
        while (!m_aus.einbettungen.empty() && m_aus.einbettungen.back().wirt >= st.absaetze) m_aus.einbettungen.pop_back();
        m_aus.laeufe.resize(st.laeufe);
        m_aus.glyphen.resize(st.glyphen);
        m_aus.glyphLagen.resize(2 * st.glyphen);
        m_aus.glyphZeichen.resize(st.glyphen);
        m_aus.fragmente.resize(st.fragmente);
        m_absolut.resize(st.absolut);
    }
    //  Nachgelegt wird nur, solange die Aufrufe von block() unter dem Vierfachen der Kaesten bleiben: ineinander
    //  gedehnte Behaelter legten sonst je Ebene doppelt (200 Ebenen grid: kein Ende).
    bool hoeheWirkt(uint32_t c) const {
        return m_bloecke < 4 * m_k.kaesten.size() + 4096 && (istFlex(c) || istGrid(c) || istBezug(c));
    }

    //  Flex-Element; Haupt- und Querachse logisch: 0 liegt am Anfang der Achse, auch bei reverse und RTL.
    struct FEl {
        uint32_t c = kKein;
        Kanten e;
        float basis = 0, innen = 0, hypo = 0, ziel = 0, mn = 0, mx = INFINITY, verl = 0;
        float a0 = 0, a1 = 0, q0 = 0, q1 = 0;   // Aussenabstand Haupt- bzw. Querachse, Anfang und Ende
        bool auto0 = false, auto1 = false, autoQ0 = false, autoQ1 = false;
        float wachsen = 0, schrumpfen = 1;
        float quer = 0, grund = 0, vorgabeH = -1;
        float lage = 0, querLage = 0;
        Ausrichtung ausr = Ausrichtung::Normal;
        bool fest = false, dehnen = false;
        Erg r;
    };

    FEl flexElement(uint32_t c, uint32_t k, float cw, bool zeile, bool spiegelH, bool spiegelQ, bool umbruch) {
        FEl f;
        f.c = c;
        f.e = kanten(c, cw);
        const Kanten& e = f.e;
        const bool an = anonym(c);
        const Stil& cs = stil(c);
        const bool autoO = !an && cs.aussen[0].art == LArt::Auto, autoU = !an && cs.aussen[2].art == LArt::Auto;
        //  physisch: Haupt links/oben, rechts/unten; Quer oben/links, unten/rechts
        const float h0 = zeile ? e.a[3] : e.a[0], h1 = zeile ? e.a[1] : e.a[2];
        const bool ah0 = zeile ? e.autoL : autoO, ah1 = zeile ? e.autoR : autoU;
        const float g0 = zeile ? e.a[0] : e.a[3], g1 = zeile ? e.a[2] : e.a[1];
        const bool ag0 = zeile ? autoO : e.autoL, ag1 = zeile ? autoU : e.autoR;
        f.a0 = spiegelH ? h1 : h0;
        f.a1 = spiegelH ? h0 : h1;
        f.auto0 = spiegelH ? ah1 : ah0;
        f.auto1 = spiegelH ? ah0 : ah1;
        f.q0 = spiegelQ ? g1 : g0;
        f.q1 = spiegelQ ? g0 : g1;
        f.autoQ0 = spiegelQ ? ag1 : ag0;
        f.autoQ1 = spiegelQ ? ag0 : ag1;
        f.wachsen = an ? 0 : float(cs.flexWachsen) / kQZahl;
        f.schrumpfen = an ? 1 : float(cs.flexSchrumpfen) / kQZahl;
        f.ausr = querVon(c, k);
        const LArt querArt = an ? LArt::Auto : (zeile ? cs.hoehe : cs.breite).art;
        f.dehnen = dehnt(f.ausr) && querArt == LArt::Auto && !f.autoQ0 && !f.autoQ1;
        if (zeile) {
            const float hz = e.horizontal();
            const auto zuRand = [&](float v) { return !an && cs.randBox ? std::max(v, hz) : v + hz; };
            const bool breiteFest = !an && cs.breite.art == LArt::Wert && bestimmt(cs.breite, cw);
            if (!an && cs.flexBasis.art == LArt::Wert && bestimmt(cs.flexBasis, cw)) f.basis = zuRand(aufloesen(cs.flexBasis, cw));
            else if (breiteFest) f.basis = zuRand(aufloesen(cs.breite, cw));
            else f.basis = intrinsisch(c).max;
            if (!an && cs.maxBreite.art == LArt::Wert && bestimmt(cs.maxBreite, cw)) f.mx = zuRand(aufloesen(cs.maxBreite, cw));
            if (!an && cs.minBreite.art == LArt::Wert && bestimmt(cs.minBreite, cw)) {
                f.mn = zuRand(aufloesen(cs.minBreite, cw));
            } else if (an || (kasten(c).flags & KErsetzt)
                       || (cs.ueberlaufX == Ueberlauf::Visible && cs.ueberlaufY == Ueberlauf::Visible)) {
                //  Automatisches Minimum: nie schmaler als das laengste Wort, ausser width ist kleiner. overflow macht ein
                //  ersetztes Element nicht zum Rollbereich (svg hat hidden als Vorgabe; Symbole in Kaestchen fielen auf 0).
                float m = breiteFest ? std::min(inhaltsBreiten(c, true).min, zuRand(aufloesen(cs.breite, cw)))
                                     : intrinsisch(c).min;
                f.mn = std::min(m, f.mx);
            }
            f.mn = std::max(f.mn, hz);
            f.innen = std::max(0.f, f.basis - hz);
            f.hypo = std::clamp(f.basis, f.mn, std::max(f.mn, f.mx));
        } else {
            const float frei = std::max(0.f, cw - e.a[1] - e.a[3]);
            float fuellen = frei;
            if (!f.dehnen || umbruch) {
                const Intr in = intrinsisch(c);
                fuellen = std::min(std::max(in.min, frei), in.max);
            }
            f.quer = randBreite(c, e, cw, fuellen);
        }
        return f;
    }

    //  Hauptgroessen einer Linie nach CSS Flexbox 9.7: wachsen oder schrumpfen, Verletzungen von min/max einfrieren.
    static void biege(std::vector<FEl>& el, size_t a, size_t b, float raum, float luecke) {
        float aussen = luecke * float(b - a - 1), summe = aussen;
        for (size_t i = a; i < b; ++i) summe += el[i].hypo + el[i].a0 + el[i].a1;
        const bool wachsen = summe < raum;
        float anfang = raum - aussen;
        for (size_t i = a; i < b; ++i) {
            FEl& f = el[i];
            f.ziel = f.hypo;
            f.fest = (wachsen ? f.wachsen : f.schrumpfen) == 0 || (wachsen && f.basis > f.hypo)
                  || (!wachsen && f.basis < f.hypo);
            anfang -= (f.fest ? f.hypo : f.basis) + f.a0 + f.a1;
        }
        for (size_t runde = a; runde <= b; ++runde) {
            float frei = raum - aussen, faktoren = 0, skaliert = 0;
            for (size_t i = a; i < b; ++i) {
                const FEl& f = el[i];
                frei -= f.a0 + f.a1 + (f.fest ? f.ziel : f.basis);
                if (f.fest) continue;
                faktoren += wachsen ? f.wachsen : f.schrumpfen;
                skaliert += f.schrumpfen * f.innen;
            }
            if (faktoren == 0) break;
            if (faktoren < 1 && std::abs(anfang * faktoren) < std::abs(frei)) frei = anfang * faktoren;
            float verletzung = 0;
            for (size_t i = a; i < b; ++i) {
                FEl& f = el[i];
                if (f.fest) continue;
                float z = f.basis;
                if (wachsen) z += frei * f.wachsen / faktoren;
                else if (skaliert > 0) z += frei * f.schrumpfen * f.innen / skaliert;
                const float k = std::clamp(z, f.mn, std::max(f.mn, f.mx));
                f.verl = k - z;
                verletzung += f.verl;
                f.ziel = k;
            }
            for (size_t i = a; i < b; ++i) {
                FEl& f = el[i];
                if (f.fest) continue;
                if (std::abs(verletzung) < 1e-4f || (verletzung > 0 && f.verl > 0) || (verletzung < 0 && f.verl < 0))
                    f.fest = true;
            }
        }
    }

    //  Inhalt eines Flex-Behaelters; gibt die Inhaltshoehe zurueck. `erste`: Grundlinie, an der er in Zeilen steht.
    float legeFlex(uint32_t k, float cw, float ch, float mnH, float mxH, float x0, float y0, float& erste) {
        const Stil& s = stil(k);
        const bool zeile = zeilenFlex(k);
        const bool umkehr = s.flexRichtung == FlexRichtung::RowReverse || s.flexRichtung == FlexRichtung::ColumnReverse;
        const bool rtl = s.richtung == Richtung::Rtl;
        const bool umbruch = s.flexUmbruch != FlexUmbruch::NoWrap;
        const bool spiegelH = zeile ? rtl != umkehr : umkehr;
        const bool spiegelQ = (s.flexUmbruch == FlexUmbruch::WrapReverse) != (!zeile && rtl);
        const float lueckeH = zeile ? luecke(s.spaltenAbstand, cw) : luecke(s.zeilenAbstand, ch);
        const float lueckeQ = zeile ? luecke(s.zeilenAbstand, ch) : luecke(s.spaltenAbstand, cw);
        std::vector<FEl> el;
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes) {
            const Kasten& C = kasten(c);
            if (C.art == KArt::Marke) { m_bezug[c] = k; continue; }
            if (C.flags & KAusserFluss) { merke(c, k, x0, y0); continue; }
            el.push_back(flexElement(c, k, cw, zeile, spiegelH, spiegelQ, umbruch));
        }
        const auto ordnung = [&](uint32_t c) { return anonym(c) ? 0 : int(stil(c).reihenfolge); };
        std::stable_sort(el.begin(), el.end(), [&](const FEl& a, const FEl& b) { return ordnung(a.c) < ordnung(b.c); });
        erste = -1;
        if (el.empty()) return 0;
        const Stand st0 = stand();
        if (!zeile) {
            //  Spalten: die Hoehe kennt erst das Legen in der Querbreite.
            for (FEl& f : el) {
                f.r = block(f.c, cw, ch, f.quer, -1);
                const bool an = anonym(f.c);
                const Stil& cs = stil(f.c);
                const float vz = f.e.vertikal();
                const auto zuRand = [&](float v) { return !an && cs.randBox ? std::max(v, vz) : v + vz; };
                if (!an && cs.flexBasis.art == LArt::Wert && bestimmt(cs.flexBasis, ch)) f.basis = zuRand(aufloesen(cs.flexBasis, ch));
                else f.basis = f.r.hoehe;
                float mn, mx;
                hoehenGrenzen(f.c, f.e, ch, mn, mx);
                f.mx = mx + vz;
                const bool sichtbar = an || (kasten(f.c).flags & KErsetzt)
                                   || (cs.ueberlaufX == Ueberlauf::Visible && cs.ueberlaufY == Ueberlauf::Visible);
                //  Inhalt oder feste Hoehe, das Kleinere (CSS Flexbox 4.5).
                f.mn = !an && cs.minHoehe.art == LArt::Wert ? mn + vz : sichtbar ? std::min({ f.r.hoehe, f.r.natur, f.mx }) : vz;
                f.innen = std::max(0.f, f.basis - vz);
                f.hypo = std::clamp(f.basis, f.mn, std::max(f.mn, f.mx));
            }
        }
        //  Linien
        const float raum = zeile ? cw : ch >= 0 ? ch : mxH;
        std::vector<std::pair<size_t, size_t>> linien;
        float laengste = 0;
        {
            size_t a = 0;
            float summe = 0;
            for (size_t i = 0; i < el.size(); ++i) {
                const float aussen = el[i].hypo + el[i].a0 + el[i].a1;
                if (umbruch && i > a && summe + lueckeH + aussen > raum) {
                    linien.push_back({ a, i });
                    laengste = std::max(laengste, summe);
                    a = i;
                    summe = 0;
                }
                summe += (i > a ? lueckeH : 0) + aussen;
            }
            linien.push_back({ a, el.size() });
            laengste = std::max(laengste, summe);
        }
        const float haupt = zeile ? cw : ch >= 0 ? ch : std::clamp(laengste, mnH, std::max(mnH, mxH));
        for (const auto& [a, b] : linien) biege(el, a, b, haupt, lueckeH);
        //  Zeilen: jetzt in der Zielbreite legen; einzeilig mit fester Hoehe gleich gedehnt.
        bool neu = false;
        if (zeile) {
            const bool festQ = !umbruch && ch >= 0;
            for (FEl& f : el) {
                float vh = -1;
                if (festQ && f.dehnen) vh = randHoehe(f.c, f.e, ch, ch - f.q0 - f.q1);
                f.r = block(f.c, cw, ch, f.ziel, vh);
                f.quer = f.r.hoehe;
                f.grund = f.r.erste >= 0 ? f.r.erste : f.r.hoehe;
            }
        } else {
            for (FEl& f : el) {
                f.grund = f.r.erste >= 0 ? f.r.erste : f.r.hoehe;
                m_aus.lage[f.c].h = f.ziel;
                if (f.ziel != f.r.hoehe && hoeheWirkt(f.c)) { f.vorgabeH = f.ziel; neu = true; }
            }
        }
        //  Querachse: Linienhoehen, einzeilig die ganze Innenhoehe, sonst nach align-content (normal dehnt gleich).
        const size_t nl = linien.size();
        std::vector<float> lq(nl, 0), loben(nl, 0);
        float summeQ = lueckeQ * float(nl - 1);
        for (size_t l = 0; l < nl; ++l) {
            float gross = 0, oben = 0, unten = 0;
            for (size_t i = linien[l].first; i < linien[l].second; ++i) {
                const FEl& f = el[i];
                const float aussen = f.quer + f.q0 + f.q1;
                if (zeile && !spiegelQ && f.ausr == Ausrichtung::Baseline && !f.autoQ0 && !f.autoQ1) {
                    oben = std::max(oben, f.q0 + f.grund);
                    unten = std::max(unten, aussen - f.q0 - f.grund);
                } else {
                    gross = std::max(gross, aussen);
                }
            }
            lq[l] = std::max(gross, oben + unten);
            loben[l] = oben;
            summeQ += lq[l];
        }
        const float querInnen = !zeile ? cw : ch >= 0 ? ch : std::clamp(summeQ, mnH, std::max(mnH, mxH));
        float qAnfang = 0, qZwischen = 0;
        if (!umbruch) lq[0] = querInnen;
        else if (s.zeilenVerteilung != Ausrichtung::Normal && s.zeilenVerteilung != Ausrichtung::Stretch)
            verteile(s.zeilenVerteilung, querInnen - summeQ, nl, false, qAnfang, qZwischen);
        else if (querInnen > summeQ)
            for (float& q : lq) q += (querInnen - summeQ) / float(nl);
        //  Dehnen
        for (size_t l = 0; l < nl; ++l)
            for (size_t i = linien[l].first; i < linien[l].second; ++i) {
                FEl& f = el[i];
                if (!f.dehnen) continue;
                if (zeile) {
                    const float z = randHoehe(f.c, f.e, ch, lq[l] - f.q0 - f.q1);
                    if (z == f.quer) continue;
                    f.quer = z;
                    m_aus.lage[f.c].h = z;
                    if (hoeheWirkt(f.c)) { f.vorgabeH = z; neu = true; }
                } else {
                    const float z = randBreite(f.c, f.e, cw, std::max(0.f, lq[l] - f.q0 - f.q1));
                    if (z == f.quer) continue;
                    f.quer = z;
                    neu = true;
                }
            }
        if (neu) {
            zurueck(st0);
            for (FEl& f : el) {
                f.r = block(f.c, cw, ch, zeile ? f.ziel : f.quer, f.vorgabeH);
                f.grund = f.r.erste >= 0 ? f.r.erste : f.r.hoehe;
                m_aus.lage[f.c].h = zeile ? f.quer : f.ziel;
            }
        }
        //  Lage je Linie
        const Ausrichtung j = s.inhaltAusrichtung;
        float querLauf = qAnfang;
        for (size_t l = 0; l < nl; ++l) {
            const auto [a, b] = linien[l];
            const size_t n = b - a;
            float belegt = lueckeH * float(n - 1);
            int autos = 0;
            for (size_t i = a; i < b; ++i) {
                belegt += el[i].ziel + el[i].a0 + el[i].a1;
                autos += el[i].auto0 + el[i].auto1;
            }
            const float frei = haupt - belegt;
            float start = 0, zwischen = 0, autoTeil = 0;
            if (autos > 0 && frei > 0) {
                autoTeil = frei / float(autos);
            } else {
                Ausrichtung w = j;
                if (w == Ausrichtung::Start) w = umkehr ? Ausrichtung::FlexEnd : Ausrichtung::FlexStart;
                else if (w == Ausrichtung::End) w = umkehr ? Ausrichtung::FlexStart : Ausrichtung::FlexEnd;
                else if (w == Ausrichtung::Left || w == Ausrichtung::Right)
                    w = !zeile || (w == Ausrichtung::Left) != spiegelH ? Ausrichtung::FlexStart : Ausrichtung::FlexEnd;
                switch (w) {
                case Ausrichtung::FlexEnd: start = frei; break;
                case Ausrichtung::Center: start = frei / 2; break;
                case Ausrichtung::SpaceBetween: if (n > 1 && frei > 0) zwischen = frei / float(n - 1); break;
                case Ausrichtung::SpaceAround:
                    if (frei > 0) { zwischen = frei / float(n); start = zwischen / 2; } else start = frei / 2;
                    break;
                case Ausrichtung::SpaceEvenly:
                    if (frei > 0) { zwischen = frei / float(n + 1); start = zwischen; } else start = frei / 2;
                    break;
                default: break;
                }
            }
            float p = start;
            for (size_t i = a; i < b; ++i) {
                FEl& f = el[i];
                p += (f.auto0 ? autoTeil : 0) + f.a0;
                f.lage = p;
                p += f.ziel + f.a1 + (f.auto1 ? autoTeil : 0) + lueckeH + zwischen;
                const float aussen = f.quer + f.q0 + f.q1;
                float q;
                if (f.autoQ0 && f.autoQ1) q = (lq[l] - f.quer) / 2;
                else if (f.autoQ0) q = lq[l] - f.quer - f.q1;
                else if (f.autoQ1) q = f.q0;
                else if (f.ausr == Ausrichtung::Center) q = (lq[l] - aussen) / 2 + f.q0;
                else if (amEnde(f.ausr)) q = lq[l] - f.quer - f.q1;
                else if (zeile && !spiegelQ && f.ausr == Ausrichtung::Baseline) q = loben[l] - f.grund;
                else q = f.q0;
                f.querLage = querLauf + q;
            }
            querLauf += lq[l] + lueckeQ + qZwischen;
        }
        for (FEl& f : el) {
            const float h = spiegelH ? haupt - f.lage - f.ziel : f.lage;
            const float q = spiegelQ ? querInnen - f.querLage - f.quer : f.querLage;
            if (zeile) setze(f.c, k, x0 + h, y0 + q);
            else setze(f.c, k, x0 + q, y0 + h);
            versetze(f.c, cw, ch);
        }
        //  Grundlinie: das erste grundlinien-ausgerichtete Element der ersten Linie, sonst ihr erstes.
        const FEl* g = &el[linien[0].first];
        for (size_t i = linien[0].first; i < linien[0].second; ++i)
            if (zeile && el[i].ausr == Ausrichtung::Baseline) { g = &el[i]; break; }
        erste = m_aus.lage[g->c].y + g->grund;
        return zeile ? querInnen : haupt;
    }

    //  Raster-Spur: Art und Wert der Unter- und Obergrenze; bei Fr ist maxW der Faktor.
    enum class SArt : uint8_t { Fest, Auto, Min, Max, Fr };
    struct Spur {
        SArt minArt = SArt::Auto, maxArt = SArt::Auto;
        float minW = 0, maxW = 0;
    };
    struct GEl {
        uint32_t c, spalte, spalten, zeile, zeilen;
    };
    struct GPlan {
        std::vector<Spur> spuren, zeilenSpuren;
        std::vector<GEl> el;
        uint32_t zeilen = 0;
    };
    using Namen = std::vector<std::vector<std::string>>;    // je Linie ihre Namen

    static std::string_view ohneRand(std::string_view t) {
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t' || t.front() == '\n')) t.remove_prefix(1);
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\n')) t.remove_suffix(1);
        return t;
    }
    //  Teilt an Leerraum (trenner ' ') bzw. am Trenner, nie in Klammern.
    static void teile(std::string_view t, char trenner, std::vector<std::string_view>& aus) {
        int tiefe = 0;
        size_t a = 0;
        const auto weg = [&](size_t e) {
            const std::string_view w = ohneRand(t.substr(a, e - a));
            if (!w.empty()) aus.push_back(w);
        };
        for (size_t i = 0; i < t.size(); ++i) {
            const char c = t[i];
            if (c == '(' || c == '[') ++tiefe;
            else if ((c == ')' || c == ']') && tiefe > 0) --tiefe;
            else if (tiefe == 0 && (trenner == ' ' ? c == ' ' || c == '\t' || c == '\n' : c == trenner)) {
                weg(i);
                a = i + 1;
            }
        }
        weg(t.size());
    }
    static bool innen(std::string_view w, std::string_view name, std::string_view& arg) {
        if (w.size() < name.size() + 2 || w.substr(0, name.size()) != name || w[name.size()] != '(' || w.back() != ')')
            return false;
        arg = w.substr(name.size() + 1, w.size() - name.size() - 2);
        return true;
    }

    //  Laenge aus einem Rohwert: px, em, rem, %, vw, vh, pt, fr (`fr` gesetzt); NaN bei Unbekanntem oder Prozent ohne Bezug.
    float rohLaenge(std::string_view w, float em, float bezug, bool& fr) const {
        fr = false;
        float v = 0;
        const auto [p, ec] = std::from_chars(w.data(), w.data() + w.size(), v);
        if (ec != std::errc() || v < 0) return NAN;
        const std::string_view e(p, size_t(w.data() + w.size() - p));
        if (e == "px" || (e.empty() && v == 0)) return v;
        if (e == "fr") { fr = true; return v; }
        if (e == "em") return v * em;
        if (e == "rem") return v * px(m_s.stile[m_s.jeKnoten[m_d.html]].schriftGroesse);
        if (e == "%") return bezug >= 0 ? v * bezug / 100.f : NAN;
        if (e == "vw") return v * float(m_m.breite) / 100.f;
        if (e == "vh") return v * float(m_m.hoehe) / 100.f;
        if (e == "pt") return v * 4.f / 3.f;
        return NAN;
    }
    bool spurGrenze(std::string_view w, float em, float bezug, SArt& art, float& wert) const {
        if (w == "auto") { art = SArt::Auto; return true; }
        if (w == "min-content") { art = SArt::Min; return true; }
        if (w == "max-content") { art = SArt::Max; return true; }
        bool fr;
        const float v = rohLaenge(w, em, bezug, fr);
        if (std::isnan(v)) {
            //  Prozent ohne Bezug (intrinsische Breite) zaehlt wie auto.
            if (!w.empty() && w.back() == '%' && bezug < 0) { art = SArt::Auto; return true; }
            return false;
        }
        art = fr ? SArt::Fr : SArt::Fest;
        wert = v;
        return true;
    }
    bool spur(std::string_view w, float em, float bezug, Spur& s) const {
        std::string_view arg;
        if (innen(w, "minmax", arg)) {
            std::vector<std::string_view> t;
            teile(arg, ',', t);
            if (t.size() != 2 || !spurGrenze(t[0], em, bezug, s.minArt, s.minW) || !spurGrenze(t[1], em, bezug, s.maxArt, s.maxW))
                return false;
            if (s.minArt == SArt::Fr) s.minArt = SArt::Auto;
            return true;
        }
        if (innen(w, "fit-content", arg)) { s.minArt = SArt::Auto; s.maxArt = SArt::Max; return true; }
        if (!spurGrenze(w, em, bezug, s.maxArt, s.maxW)) return false;
        s.minArt = s.maxArt == SArt::Fr ? SArt::Auto : s.maxArt;
        s.minW = s.maxW;
        return true;
    }

    //  grid-template-columns/-rows ausrollen samt Linien-Namen (Index = Linie, 0 vor der ersten Spur); auto-fill/-fit
    //  nach `bezug` (ohne Bezug einmal). `fit` und der Bereich der Wiederholung kommen zurueck, damit leere Spuren nach
    //  dem Platzieren wegfallen.
    void spurListe(uint32_t roh, float em, float bezug, float lS, std::vector<Spur>& aus, Namen& namen, bool& fit,
                   size_t& wdhA, size_t& wdhN) const {
        aus.clear();
        namen.assign(1, {});
        fit = false;
        wdhA = wdhN = 0;
        if (roh == 0 || roh >= m_s.roh.size()) return;
        std::string t = m_s.roh[roh];
        for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
        const auto namenAus = [&](std::string_view w, std::vector<std::string>& z) {
            std::vector<std::string_view> n;
            teile(w.substr(1, w.size() - 2), ' ', n);
            for (std::string_view x : n) z.emplace_back(x);
        };
        //  Ein Abschnitt: Spuren mit den Namen der Linien davor und dahinter, `mal` Wiederholungen (-1: auto-fill/-fit).
        struct Abschnitt {
            std::vector<Spur> z;
            Namen n;
            int mal = 1;
        };
        std::vector<Abschnitt> abschnitte;
        const auto liste = [&](std::string_view l, Abschnitt& a) {
            std::vector<std::string_view> w;
            teile(l, ' ', w);
            a.n.assign(1, {});
            for (std::string_view x : w) {
                if (x.front() == '[') {
                    if (x.back() != ']') return false;
                    namenAus(x, a.n.back());
                    continue;
                }
                Spur s;
                if (!spur(x, em, bezug, s)) return false;
                a.z.push_back(s);
                a.n.emplace_back();
            }
            return !a.z.empty();
        };
        std::vector<std::string_view> teileA;
        teile(t, ' ', teileA);
        bool autoWdh = false;
        std::vector<std::string> vorn;
        for (std::string_view w : teileA) {
            if (w.front() == '[') {
                if (w.back() != ']') return;
                namenAus(w, vorn);
                continue;
            }
            Abschnitt a;
            std::string_view arg;
            if (innen(w, "repeat", arg)) {
                std::vector<std::string_view> r;
                teile(arg, ',', r);
                if (r.size() != 2 || !liste(r[1], a)) return;
                if (r[0] == "auto-fill" || r[0] == "auto-fit") {
                    if (autoWdh) return;
                    autoWdh = true;
                    fit = r[0] == "auto-fit";
                    a.mal = -1;
                } else {
                    const auto [p, ec] = std::from_chars(r[0].data(), r[0].data() + r[0].size(), a.mal);
                    if (ec != std::errc() || p != r[0].data() + r[0].size() || a.mal < 1) return;
                }
            } else {
                a.n.assign(2, {});
                a.z.push_back({});
                if (!spur(w, em, bezug, a.z.back())) return;
            }
            a.n[0].insert(a.n[0].begin(), vorn.begin(), vorn.end());
            vorn.clear();
            abschnitte.push_back(std::move(a));
        }
        //  So viele Wiederholungen, wie in den Bezug passen, mindestens eine; gemessen an der festen Grenze.
        int automal = 1;
        if (autoWdh && bezug >= 0) {
            const auto mass = [](const Spur& s) { return s.maxArt == SArt::Fest ? s.maxW : s.minArt == SArt::Fest ? s.minW : 0.f; };
            float andere = 0, wdh = 0;
            size_t nAndere = 0, nWdh = 0;
            for (const Abschnitt& a : abschnitte)
                for (const Spur& s : a.z) {
                    if (a.mal < 0) { wdh += mass(s); ++nWdh; }
                    else { andere += mass(s) * float(a.mal); nAndere += size_t(a.mal); }
                }
            const auto gesamt = [&](size_t n) { return andere + float(n) * wdh + lS * float(nAndere + n * nWdh - 1); };
            size_t n = 1;
            if (wdh + lS * float(nWdh) > 0)
                while (nAndere + (n + 1) * nWdh < kMaxSpalten && gesamt(n + 1) <= bezug) ++n;
            automal = int(n);
        }
        for (const Abschnitt& a : abschnitte) {
            const int mal = a.mal < 0 ? automal : a.mal;
            if (a.mal < 0) wdhA = aus.size();
            for (int i = 0; i < mal && aus.size() + a.z.size() <= kMaxSpalten; ++i) {
                for (const std::string& x : a.n[0]) namen.back().push_back(x);
                for (size_t j = 0; j < a.z.size(); ++j) {
                    aus.push_back(a.z[j]);
                    namen.push_back(a.n[j + 1]);
                }
            }
            if (a.mal < 0) wdhN = aus.size() - wdhA;
        }
        for (const std::string& x : vorn) namen.back().push_back(x);
    }

    //  grid-template-areas: je Zeichenkette eine Zeile; jedes Rechteck gibt seinen Linien „name-start" und „name-end".
    //  Nicht rechteckige Flaechen oder ungleiche Zeilen verwerfen die ganze Angabe.
    void flaechen(uint32_t roh, Namen& nS, Namen& nZ, uint32_t& breite, uint32_t& hoehe) const {
        breite = hoehe = 0;
        if (roh == 0 || roh >= m_s.roh.size()) return;
        std::string t = m_s.roh[roh];
        for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
        std::vector<std::vector<std::string>> raster;
        for (size_t i = 0; i < t.size();) {
            const char q = t[i];
            if (q == ' ' || q == '\t' || q == '\n') { ++i; continue; }
            if (q != '"' && q != '\'') return;
            const size_t e = t.find(q, i + 1);
            if (e == std::string::npos) return;
            std::vector<std::string> zeile;
            for (size_t j = i + 1; j < e;) {
                const char c = t[j];
                if (c == ' ' || c == '\t' || c == '\n') { ++j; continue; }
                size_t k = j;
                if (c == '.') {
                    while (k < e && t[k] == '.') ++k;
                    zeile.emplace_back();
                } else {
                    while (k < e && t[k] != '.' && t[k] != ' ' && t[k] != '\t' && t[k] != '\n') ++k;
                    zeile.push_back(t.substr(j, k - j));
                }
                j = k;
            }
            if (zeile.empty() || (!raster.empty() && zeile.size() != raster[0].size())) return;
            raster.push_back(std::move(zeile));
            if (raster.size() > kMaxSpalten || raster[0].size() > kMaxSpalten) return;
            i = e + 1;
        }
        if (raster.empty()) return;
        struct Rechteck4 { uint32_t z0, z1, s0, s1, n; };
        std::unordered_map<std::string, Rechteck4> rechtecke;
        for (uint32_t z = 0; z < raster.size(); ++z)
            for (uint32_t sp = 0; sp < raster[z].size(); ++sp) {
                const std::string& n = raster[z][sp];
                if (n.empty()) continue;
                auto [it, neu] = rechtecke.try_emplace(n, Rechteck4{ z, z + 1, sp, sp + 1, 0 });
                Rechteck4& r = it->second;
                r.z0 = std::min(r.z0, z); r.z1 = std::max(r.z1, z + 1);
                r.s0 = std::min(r.s0, sp); r.s1 = std::max(r.s1, sp + 1);
                ++r.n;
            }
        for (const auto& [n, r] : rechtecke) {
            if (r.n != (r.z1 - r.z0) * (r.s1 - r.s0)) return;
            for (uint32_t z = r.z0; z < r.z1; ++z)
                for (uint32_t sp = r.s0; sp < r.s1; ++sp)
                    if (raster[z][sp] != n) return;
        }
        hoehe = uint32_t(raster.size());
        breite = uint32_t(raster[0].size());
        if (nS.size() < breite + 1) nS.resize(breite + 1);
        if (nZ.size() < hoehe + 1) nZ.resize(hoehe + 1);
        for (const auto& [n, r] : rechtecke) {
            nS[r.s0].push_back(n + "-start");
            nS[r.s1].push_back(n + "-end");
            nZ[r.z0].push_back(n + "-start");
            nZ[r.z1].push_back(n + "-end");
        }
    }

    //  Eine Rasterlinie: art 0 auto, 1 Linie (Zahl, negativ vom Ende, und/oder Name), 2 span.
    struct GLinie {
        int art = 0, n = 0;
        std::string name;
    };
    GLinie linie(uint32_t roh) const {
        GLinie l;
        if (roh == 0 || roh >= m_s.roh.size()) return l;
        std::string t = m_s.roh[roh];
        for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
        std::vector<std::string_view> w;
        teile(t, ' ', w);
        bool span = false;
        for (std::string_view x : w) {
            if (x == "span") { span = true; continue; }
            int n = 0;
            const auto [p, ec] = std::from_chars(x.data(), x.data() + x.size(), n);
            if (ec == std::errc() && p == x.data() + x.size()) l.n = n;
            else if (x != "auto") l.name = std::string(x);
        }
        if (span) { l.art = 2; l.n = std::clamp(l.n > 0 ? l.n : 1, 1, int(kMaxSpalten)); }
        else if (l.n != 0 || !l.name.empty()) l.art = 1;
        return l;
    }

    //  Linie (0-basiert) einer Angabe; fehlen benannte Linien, zaehlen die impliziten hinter dem expliziten Raster.
    static int loese(const GLinie& l, const Namen& namen, uint32_t explizit, bool ende) {
        if (l.name.empty()) return l.n > 0 ? l.n - 1 : std::max(0, int(explizit) + 1 + l.n);
        const auto mit = [&](const std::string& name) {
            std::vector<int> t;
            for (size_t i = 0; i < namen.size(); ++i)
                if (std::find(namen[i].begin(), namen[i].end(), name) != namen[i].end()) t.push_back(int(i));
            return t;
        };
        if (l.n == 0) {
            const std::vector<int> flaeche = mit(l.name + (ende ? "-end" : "-start"));
            if (!flaeche.empty()) return flaeche.front();
        }
        const std::vector<int> t = mit(l.name);
        const int n = l.n == 0 ? 1 : l.n;
        if (n > 0) return n <= int(t.size()) ? t[size_t(n - 1)] : int(explizit) + n - int(t.size());
        return -n <= int(t.size()) ? t[t.size() + size_t(n)] : 0;
    }

    //  Lage auf einer Achse aus Anfang und Ende: `von` -1 = automatisch.
    static void bereich(const GLinie& a, const GLinie& b, const Namen& namen, uint32_t explizit, int& von, int& spanne) {
        const auto naechste = [&](int ab, const std::string& name, int richtung) {
            for (int i = ab + richtung; i >= 0 && i < int(namen.size()); i += richtung)
                if (std::find(namen[size_t(i)].begin(), namen[size_t(i)].end(), name) != namen[size_t(i)].end()) return i;
            return -1;
        };
        von = -1;
        spanne = 1;
        if (a.art == 1 && b.art == 1) {
            int x = loese(a, namen, explizit, false), y = loese(b, namen, explizit, true);
            if (y < x) std::swap(x, y);
            von = x;
            spanne = std::max(1, y - x);
        } else if (a.art == 1) {
            von = loese(a, namen, explizit, false);
            if (b.art == 2) {
                const int ziel = b.name.empty() ? -1 : naechste(von, b.name, 1);
                spanne = b.name.empty() ? b.n : ziel > von ? ziel - von : 1;
            }
        } else if (b.art == 1) {
            const int y = loese(b, namen, explizit, true);
            if (a.art == 2) {
                const int ziel = a.name.empty() ? -1 : naechste(y, a.name, -1);
                spanne = a.name.empty() ? a.n : ziel >= 0 && ziel < y ? y - ziel : 1;
            }
            von = std::max(0, y - spanne);
        } else {
            spanne = a.art == 2 && a.name.empty() ? a.n : b.art == 2 && b.name.empty() ? b.n : 1;
        }
        spanne = std::clamp(spanne, 1, int(kMaxSpalten));
        if (von >= 0) von = std::min(von, int(kMaxSpalten) - spanne);
    }

    std::vector<Spur> autoMuster(uint32_t roh, float em, float bezug) const {
        std::vector<Spur> muster;
        if (roh != 0 && roh < m_s.roh.size()) {
            std::string a = m_s.roh[roh];
            for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
            std::vector<std::string_view> w;
            teile(a, ' ', w);
            for (std::string_view x : w) {
                Spur sp;
                if (!spur(x, em, bezug, sp)) { muster.clear(); break; }
                muster.push_back(sp);
            }
        }
        if (muster.empty()) muster.emplace_back();
        return muster;
    }

    //  Spuren, Namen und Platzierung nach CSS Grid 8.5: erst festgelegte Elemente, dann die an eine Zeile (bei column-
    //  Fluss Spalte) gebundenen, dann die uebrigen mit Cursor (dense: jedes Mal vom Anfang). Implizite Spuren aus
    //  grid-auto-rows/-columns; Linien vor dem expliziten Raster gibt es nicht (eine solche Lage beginnt bei 0).
    void plane(uint32_t k, float cw, float ch, float lS, float lZ, GPlan& p) {
        const Stil& s = stil(k);
        const float em = px(s.schriftGroesse);
        Namen nS, nZ;
        bool fit, fitZ;
        size_t wdhA, wdhN, wdhZA, wdhZN;
        spurListe(s.gridSpalten, em, cw, lS, p.spuren, nS, fit, wdhA, wdhN);
        spurListe(s.gridZeilen, em, ch, lZ, p.zeilenSpuren, nZ, fitZ, wdhZA, wdhZN);
        uint32_t fB = 0, fH = 0;
        flaechen(s.gridFlaechen, nS, nZ, fB, fH);
        const uint32_t exS = std::max(uint32_t(p.spuren.size()), fB), exZ = std::max(uint32_t(p.zeilenSpuren.size()), fH);
        if (nS.size() < exS + 1) nS.resize(exS + 1);
        if (nZ.size() < exZ + 1) nZ.resize(exZ + 1);
        struct Teil {
            uint32_t c;
            int16_t ordnung;
            int sv, ss, zv, zs;
        };
        std::vector<Teil> teile;
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes) {
            const Kasten& C = kasten(c);
            if (C.art == KArt::Marke || (C.flags & KAusserFluss)) continue;
            Teil t{ c, 0, -1, 1, -1, 1 };
            if (!anonym(c)) {
                const Stil& cs = stil(c);
                t.ordnung = cs.reihenfolge;
                bereich(linie(cs.gridSpalteA), linie(cs.gridSpalteE), nS, exS, t.sv, t.ss);
                bereich(linie(cs.gridZeileA), linie(cs.gridZeileE), nZ, exZ, t.zv, t.zs);
            }
            teile.push_back(t);
        }
        std::stable_sort(teile.begin(), teile.end(), [](const Teil& a, const Teil& b) { return a.ordnung < b.ordnung; });
        //  Fest = die Achse, auf der der Cursor laeuft (Spalten bei row-Fluss), Wachsend = die andere.
        const bool spaltenFluss = s.gridFluss & 1, dicht = s.gridFluss & 2;
        const auto festV = [&](Teil& t) -> int& { return spaltenFluss ? t.zv : t.sv; };
        const auto festS = [&](Teil& t) -> int& { return spaltenFluss ? t.zs : t.ss; };
        const auto wachsV = [&](Teil& t) -> int& { return spaltenFluss ? t.sv : t.zv; };
        const auto wachsS = [&](Teil& t) -> int& { return spaltenFluss ? t.ss : t.zs; };
        uint32_t n = std::max(spaltenFluss ? exZ : exS, 1u);
        for (Teil& t : teile) {
            if (festV(t) >= 0) n = std::max(n, uint32_t(festV(t) + festS(t)));
            else if (uint32_t(festS(t)) > n && (spaltenFluss ? exZ : exS) == 0) n = uint32_t(festS(t));
        }
        n = std::min(n, kMaxSpalten);
        for (Teil& t : teile)
            if (festV(t) < 0) festS(t) = std::min(festS(t), int(n));
        std::vector<std::vector<uint8_t>> belegt;
        const auto frei = [&](int g, int gs, int f, int fs) {
            if (f < 0 || f + fs > int(n)) return false;
            for (int y = g; y < g + gs && y < int(belegt.size()); ++y)
                for (int x = f; x < f + fs; ++x)
                    if (belegt[size_t(y)][size_t(x)]) return false;
            return true;
        };
        const auto markiere = [&](Teil& t) {
            const int g = wachsV(t), gs = wachsS(t);
            if (size_t(g + gs) > belegt.size()) belegt.resize(size_t(g + gs), std::vector<uint8_t>(n, 0));
            for (int y = g; y < g + gs; ++y)
                for (int x = std::max(0, festV(t)); x < std::min(int(n), festV(t) + festS(t)); ++x) belegt[size_t(y)][size_t(x)] = 1;
        };
        constexpr int kMaxWachs = int(kMaxSpalten);
        for (Teil& t : teile)
            if (festV(t) >= 0 && wachsV(t) >= 0) markiere(t);
        //  An eine Linie der wachsenden Achse gebunden: die erste freie Stelle dahinter (dense: von vorn).
        std::vector<int> nachGebunden;
        for (Teil& t : teile) {
            if (!(wachsV(t) >= 0 && festV(t) < 0)) continue;
            const int g = wachsV(t);
            if (size_t(g) >= nachGebunden.size()) nachGebunden.resize(size_t(g) + 1, 0);
            int f = dicht ? 0 : nachGebunden[size_t(g)];
            while (f + festS(t) <= int(n) && !frei(g, wachsS(t), f, festS(t))) ++f;
            if (f + festS(t) > int(n)) f = 0;
            festV(t) = f;
            nachGebunden[size_t(g)] = f + festS(t);
            markiere(t);
        }
        int cg = 0, cf = 0;
        for (Teil& t : teile) {
            if (wachsV(t) >= 0) continue;
            if (festV(t) >= 0) {
                if (dicht) cg = 0;
                else if (festV(t) < cf) ++cg;
                cf = festV(t);
                while (cg < kMaxWachs && !frei(cg, wachsS(t), festV(t), festS(t))) ++cg;
            } else {
                if (dicht) cg = cf = 0;
                while (cg < kMaxWachs) {
                    while (cf + festS(t) <= int(n) && !frei(cg, wachsS(t), cf, festS(t))) ++cf;
                    if (cf + festS(t) <= int(n)) break;
                    ++cg;
                    cf = 0;
                }
                festV(t) = cf;
            }
            wachsV(t) = std::min(cg, kMaxWachs);
            markiere(t);
            if (!dicht) cf = festV(t) + festS(t);
        }
        uint32_t spalten = exS, zeilen = exZ;
        for (const Teil& t : teile) {
            p.el.push_back({ t.c, uint32_t(t.sv), uint32_t(t.ss), uint32_t(t.zv), uint32_t(t.zs) });
            spalten = std::max(spalten, uint32_t(t.sv + t.ss));
            zeilen = std::max(zeilen, uint32_t(t.zv + t.zs));
        }
        spalten = std::min(std::max(spalten, 1u), kMaxSpalten);
        //  Implizite Spuren nach grid-auto-columns/-rows; die Liste wiederholt sich ab der ersten impliziten Spur.
        const std::vector<Spur> autoS = autoMuster(s.gridAutoSpalten, em, cw), autoZ = autoMuster(s.gridAutoZeilen, em, ch);
        for (uint32_t i = 0, vor = uint32_t(p.spuren.size()); vor + i < spalten; ++i) p.spuren.push_back(autoS[i % autoS.size()]);
        for (uint32_t i = 0, vor = uint32_t(p.zeilenSpuren.size()); vor + i < zeilen; ++i)
            p.zeilenSpuren.push_back(autoZ[i % autoZ.size()]);
        p.zeilen = uint32_t(p.zeilenSpuren.size());
        if (!fit || wdhN == 0) return;
        //  auto-fit: leere Wiederholungen fallen samt Luecke weg.
        const uint32_t ns = uint32_t(p.spuren.size());
        std::vector<uint8_t> voll(ns, 0);
        for (const GEl& g : p.el)
            for (uint32_t j = g.spalte; j < g.spalte + g.spalten && j < ns; ++j) voll[j] = 1;
        std::vector<uint32_t> neu(ns + 1, 0);
        uint32_t m = 0;
        for (uint32_t j = 0; j < ns; ++j) {
            neu[j] = m;
            if (voll[j] || j < wdhA || j >= wdhA + wdhN) p.spuren[m++] = p.spuren[j];
        }
        neu[ns] = m;
        for (GEl& g : p.el) {
            const uint32_t ende = neu[g.spalte + g.spalten - 1] + 1;
            g.spalte = neu[g.spalte];
            g.spalten = ende - g.spalte;
        }
        p.spuren.resize(std::max(m, 1u));
    }

    //  Spurgroessen nach CSS Grid 11, verkuerzt und fuer beide Achsen: feste Spuren, Beitraege der Elemente mit Spanne 1,
    //  breitere verteilen ihren Mangel gleich auf die nicht festen (ueber eine fr-Spur hinweg zaehlen sie nicht: ihr
    //  automatisches Minimum ist dort 0, CSS Grid 6.6),
    //  dann freier Raum bis zur Grenze, fr aus dem Rest (eine fr-Spur, deren Inhalt mehr braucht, zaehlt fest), ohne
    //  festen Raum nach dem groessten Anteil je Faktor. `beitrag`: min/max je Element in `p.el`.
    struct GBeitrag {
        uint32_t von, spanne;
        float min, max;
    };
    std::vector<float> spurGroessen(const std::vector<Spur>& spuren, const std::vector<GBeitrag>& beitrag, float raum,
                                    float luecke, bool autoDehnen) const {
        const size_t n = spuren.size();
        std::vector<float> basis(n, 0), grenze(n, -1);
        for (size_t i = 0; i < n; ++i) {
            const Spur& s = spuren[i];
            if (s.minArt == SArt::Fest) basis[i] = s.minW;
            if (s.maxArt == SArt::Fest) grenze[i] = s.maxW;
        }
        std::vector<const GBeitrag*> breit;
        for (const GBeitrag& b : beitrag) {
            if (b.von >= n) continue;
            if (b.spanne > 1) { breit.push_back(&b); continue; }
            const Spur& s = spuren[b.von];
            float& ba = basis[b.von];
            float& z = grenze[b.von];
            if (s.minArt == SArt::Auto || s.minArt == SArt::Min) ba = std::max(ba, b.min);
            else if (s.minArt == SArt::Max) ba = std::max(ba, b.max);
            if (s.maxArt == SArt::Auto || s.maxArt == SArt::Max) z = std::max(z, b.max);
            else if (s.maxArt == SArt::Min) z = std::max(z, b.min);
        }
        for (const GBeitrag* g : breit) {
            const uint32_t ende = std::min(uint32_t(n), g->von + g->spanne);
            float hatB = luecke * float(ende - g->von - 1), faktoren = 0;
            uint32_t frei = 0;
            for (uint32_t i = g->von; i < ende; ++i) {
                hatB += basis[i];
                frei += spuren[i].minArt != SArt::Fest;
                if (spuren[i].maxArt == SArt::Fr) faktoren += spuren[i].maxW;
            }
            if (frei == 0 || faktoren > 0) continue;
            for (uint32_t i = g->von; i < ende; ++i)
                if (spuren[i].minArt != SArt::Fest && g->min > hatB) basis[i] += (g->min - hatB) / float(frei);
            //  Die Grenzen erst nach den Basen: sonst zaehlte der Beitrag doppelt.
            float hatZ = luecke * float(ende - g->von - 1);
            for (uint32_t i = g->von; i < ende; ++i) hatZ += std::max(basis[i], grenze[i]);
            if (g->max > hatZ)
                for (uint32_t i = g->von; i < ende; ++i)
                    if (spuren[i].minArt != SArt::Fest && spuren[i].maxArt != SArt::Fest)
                        grenze[i] = std::max(basis[i], grenze[i]) + (g->max - hatZ) / float(frei);
        }
        float summe = luecke * float(n - 1);
        for (size_t i = 0; i < n; ++i) {
            if (spuren[i].maxArt != SArt::Fr) grenze[i] = std::max(grenze[i] < 0 ? basis[i] : grenze[i], basis[i]);
            summe += basis[i];
        }
        if (raum < 0) {
            //  Ohne festen Raum: jede Spur bis zu ihrer Grenze, fr nach dem groessten Anteil je Faktor.
            float anteil = 0;
            for (size_t i = 0; i < n; ++i)
                if (spuren[i].maxArt == SArt::Fr && spuren[i].maxW > 0) anteil = std::max(anteil, basis[i] / spuren[i].maxW);
            for (size_t i = 0; i < n; ++i)
                basis[i] = spuren[i].maxArt == SArt::Fr ? std::max(basis[i], anteil * spuren[i].maxW) : grenze[i];
            return basis;
        }
        //  Maximieren: freier Platz gleich an die nicht-fr-Spuren bis zu ihrer Grenze.
        float frei = raum - summe;
        for (int runde = 0; frei > 1e-3f && runde < 64; ++runde) {
            uint32_t offen = 0;
            for (size_t i = 0; i < n; ++i) offen += spuren[i].maxArt != SArt::Fr && grenze[i] > basis[i];
            if (offen == 0) break;
            const float teil = frei / float(offen);
            for (size_t i = 0; i < n; ++i) {
                if (spuren[i].maxArt == SArt::Fr || grenze[i] <= basis[i]) continue;
                const float d = std::min(teil, grenze[i] - basis[i]);
                basis[i] += d;
                frei -= d;
            }
        }
        std::vector<uint8_t> starr(n, 0);
        bool frDa = false;
        for (int runde = 0; runde < 64; ++runde) {
            float rest = raum - luecke * float(n - 1), faktoren = 0;
            for (size_t i = 0; i < n; ++i) {
                if (spuren[i].maxArt == SArt::Fr && !starr[i]) faktoren += spuren[i].maxW;
                else rest -= basis[i];
            }
            if (faktoren == 0) break;
            frDa = true;
            const float fr = std::max(0.f, rest) / std::max(1.f, faktoren);
            bool weiter = false;
            for (size_t i = 0; i < n; ++i)
                if (spuren[i].maxArt == SArt::Fr && !starr[i] && fr * spuren[i].maxW < basis[i]) starr[i] = 1, weiter = true;
            if (weiter) continue;
            for (size_t i = 0; i < n; ++i)
                if (spuren[i].maxArt == SArt::Fr && !starr[i]) basis[i] = fr * spuren[i].maxW;
            break;
        }
        //  auto-Spuren nehmen den Rest (justify-/align-content normal oder stretch).
        if (!frDa && autoDehnen) {
            float belegt = luecke * float(n - 1);
            uint32_t autos = 0;
            for (size_t i = 0; i < n; ++i) {
                belegt += basis[i];
                autos += spuren[i].maxArt == SArt::Auto;
            }
            if (autos > 0 && raum > belegt)
                for (size_t i = 0; i < n; ++i)
                    if (spuren[i].maxArt == SArt::Auto) basis[i] += (raum - belegt) / float(autos);
        }
        return basis;
    }

    std::vector<float> spaltenGrid(const GPlan& p, float cw, float lS, Ausrichtung j) {
        std::vector<GBeitrag> b;
        b.reserve(p.el.size());
        for (const GEl& g : p.el) {
            const Intr in = intrinsisch(g.c);
            const Kanten e = kanten(g.c, -1);
            b.push_back({ g.spalte, g.spalten, in.min + e.a[1] + e.a[3], in.max + e.a[1] + e.a[3] });
        }
        return spurGroessen(p.spuren, b, cw, lS, j == Ausrichtung::Normal || j == Ausrichtung::Stretch);
    }

    //  justify-/align-content: Anfang und Zuschlag je Luecke fuer den Rest, der nach den Spuren bleibt.
    static void verteile(Ausrichtung a, float rest, size_t n, bool rtlLinks, float& anfang, float& zwischen) {
        anfang = zwischen = 0;
        if (a == Ausrichtung::Left || a == Ausrichtung::Right) a = (a == Ausrichtung::Left) != rtlLinks ? Ausrichtung::Start : Ausrichtung::End;
        switch (a) {
        case Ausrichtung::End: case Ausrichtung::FlexEnd: anfang = rest; break;
        case Ausrichtung::Center: anfang = rest / 2; break;
        case Ausrichtung::SpaceBetween: if (n > 1 && rest > 0) zwischen = rest / float(n - 1); break;
        case Ausrichtung::SpaceAround:
            if (rest > 0) { zwischen = rest / float(n); anfang = zwischen / 2; } else anfang = rest / 2;
            break;
        case Ausrichtung::SpaceEvenly:
            if (rest > 0) { zwischen = rest / float(n + 1); anfang = zwischen; } else anfang = rest / 2;
            break;
        default: break;
        }
    }

    //  min-/max-content eines Rasters: die Spuren ohne festen Raum, Spanne 1 als groesster Beitrag.
    Intr gridBreiten(uint32_t k) {
        GPlan p;
        const float lS = luecke(stil(k).spaltenAbstand, -1);
        plane(k, -1, -1, lS, 0, p);
        const size_t n = p.spuren.size();
        std::vector<GBeitrag> lo, hi;
        for (const GEl& g : p.el) {
            if (g.spalten != 1 || p.spuren[g.spalte].maxArt == SArt::Fest) continue;
            const Intr in = intrinsisch(g.c);
            const Kanten e = kanten(g.c, -1);
            lo.push_back({ g.spalte, 1, in.min + e.a[1] + e.a[3], in.min + e.a[1] + e.a[3] });
            hi.push_back({ g.spalte, 1, in.min + e.a[1] + e.a[3], in.max + e.a[1] + e.a[3] });
        }
        const std::vector<float> wLo = spurGroessen(p.spuren, lo, -1, lS, false), wHi = spurGroessen(p.spuren, hi, -1, lS, false);
        Intr r{ lS * float(n - 1), lS * float(n - 1) };
        for (size_t i = 0; i < n; ++i) {
            r.min += wLo[i];
            r.max += std::max(wLo[i], wHi[i]);
        }
        return r;
    }

    float legeGrid(uint32_t k, float cw, float ch, float mnH, float mxH, float x0, float y0, float& erste) {
        const Stil& s = stil(k);
        const bool rtl = s.richtung == Richtung::Rtl;
        const float lS = luecke(s.spaltenAbstand, cw), lZ = luecke(s.zeilenAbstand, ch);
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes) {
            if (kasten(c).art == KArt::Marke) m_bezug[c] = k;
            else if (kasten(c).flags & KAusserFluss) merke(c, k, x0, y0);
        }
        GPlan p;
        plane(k, cw, ch, lS, lZ, p);
        erste = -1;
        if (p.el.empty()) return 0;
        const std::vector<float> w = spaltenGrid(p, cw, lS, s.inhaltAusrichtung);
        float sAnfang = 0, sZwischen = 0;
        {
            float belegt = lS * float(w.size() - 1);
            for (float x : w) belegt += x;
            verteile(s.inhaltAusrichtung, cw - belegt, w.size(), rtl, sAnfang, sZwischen);
        }
        std::vector<float> sx(w.size() + 1, sAnfang);
        for (size_t i = 0; i < w.size(); ++i) sx[i + 1] = sx[i] + w[i] + lS + sZwischen;
        struct GL {
            Kanten e;
            Erg r;
            float flaeche = 0, bb = 0, x = 0, vorgabeH = -1, verschub = 0;
            Ausrichtung quer = Ausrichtung::Normal;
            bool dehnen = false, autoO = false, autoU = false;
        };
        std::vector<GL> gl(p.el.size());
        const Stand st0 = stand();
        for (size_t i = 0; i < p.el.size(); ++i) {
            const GEl& g = p.el[i];
            GL& l = gl[i];
            const uint32_t c = g.c;
            const bool an = anonym(c), ersetzt = kasten(c).flags & KErsetzt;
            const Stil& cs = stil(c);
            l.flaeche = sx[g.spalte + g.spalten] - sx[g.spalte] - lS - sZwischen;
            l.e = kanten(c, l.flaeche);
            Ausrichtung js = an ? Ausrichtung::Auto : cs.selbstJustierung;
            if (js == Ausrichtung::Auto) js = s.justierung;
            if (js == Ausrichtung::Left || js == Ausrichtung::Right)
                js = (js == Ausrichtung::Left) != rtl ? Ausrichtung::Start : Ausrichtung::End;
            const bool autoL = l.e.autoL, autoR = l.e.autoR;
            const float frei = std::max(0.f, l.flaeche - l.e.a[1] - l.e.a[3]);
            const bool fuellt = (js == Ausrichtung::Stretch || (js == Ausrichtung::Normal && !ersetzt))
                             && (an || cs.breite.art == LArt::Auto) && !autoL && !autoR;
            float fuellen = frei;
            if (!fuellt) {
                const Intr in = intrinsisch(c);
                fuellen = std::min(std::max(in.min, frei), in.max);
            }
            l.bb = randBreite(c, l.e, l.flaeche, fuellen);
            //  logische Lage in der Flaeche ab ihrem Anfang (rechts bei RTL)
            const float m0 = rtl ? l.e.a[1] : l.e.a[3], m1 = rtl ? l.e.a[3] : l.e.a[1];
            const bool auto0 = rtl ? autoR : autoL, auto1 = rtl ? autoL : autoR;
            const float rest = l.flaeche - l.bb - m0 - m1;
            if (auto0 && auto1) l.x = m0 + std::max(0.f, rest) / 2;
            else if (auto0) l.x = m0 + std::max(0.f, rest);
            else if (auto1) l.x = m0;
            else if (js == Ausrichtung::Center) l.x = m0 + rest / 2;
            else if (amEnde(js)) l.x = m0 + rest;
            else l.x = m0;
            l.quer = querVon(c, k);
            l.autoO = !an && cs.aussen[0].art == LArt::Auto;
            l.autoU = !an && cs.aussen[2].art == LArt::Auto;
            l.dehnen = (l.quer == Ausrichtung::Stretch || (l.quer == Ausrichtung::Normal && !ersetzt))
                    && (an || cs.hoehe.art == LArt::Auto) && !l.autoO && !l.autoU;
            l.r = block(c, l.flaeche, -1, l.bb, -1);
        }
        //  Grundlinien: je Zeile ruecken die baseline-Elemente mit Spanne 1 auf die tiefste erste Grundlinie.
        std::vector<float> zeilenGrund(p.zeilen, -1);
        const auto grund = [&](size_t i) { return gl[i].e.a[0] + (gl[i].r.erste >= 0 ? gl[i].r.erste : gl[i].r.hoehe); };
        const auto anGrund = [&](size_t i) {
            return gl[i].quer == Ausrichtung::Baseline && p.el[i].zeilen == 1 && !gl[i].autoO && !gl[i].autoU;
        };
        for (size_t i = 0; i < p.el.size(); ++i)
            if (anGrund(i)) zeilenGrund[p.el[i].zeile] = std::max(zeilenGrund[p.el[i].zeile], grund(i));
        std::vector<GBeitrag> beitrag;
        for (size_t i = 0; i < p.el.size(); ++i) {
            if (anGrund(i)) gl[i].verschub = zeilenGrund[p.el[i].zeile] - grund(i);
            const float h = gl[i].verschub + gl[i].r.hoehe + gl[i].e.a[0] + gl[i].e.a[2];
            beitrag.push_back({ p.el[i].zeile, p.el[i].zeilen, h, h });
        }
        //  Zeilen: ohne feste Hoehe nach dem Inhalt; align-content normal dehnt die auto-Zeilen auf eine hoehere Flaeche.
        std::vector<float> zh = spurGroessen(p.zeilenSpuren, beitrag, ch, lZ, false);
        float summe = lZ * float(p.zeilen - 1);
        for (float h : zh) summe += h;
        const float innenH = ch >= 0 ? ch : std::clamp(summe, mnH, std::max(mnH, mxH));
        float zAnfang = 0, zZwischen = 0;
        if (innenH > summe) {
            const Ausrichtung a = s.zeilenVerteilung;
            if (a == Ausrichtung::Normal || a == Ausrichtung::Stretch) {
                uint32_t autos = 0;
                for (const Spur& sp : p.zeilenSpuren) autos += sp.maxArt == SArt::Auto;
                for (size_t z = 0; z < zh.size() && autos > 0; ++z)
                    if (p.zeilenSpuren[z].maxArt == SArt::Auto) zh[z] += (innenH - summe) / float(autos);
            } else {
                verteile(a, innenH - summe, zh.size(), false, zAnfang, zZwischen);
            }
        }
        std::vector<float> sy(p.zeilen + 1, zAnfang);
        for (uint32_t z = 0; z < p.zeilen; ++z) sy[z + 1] = sy[z] + zh[z] + lZ + zZwischen;
        const auto flaecheH = [&](const GEl& g) { return sy[g.zeile + g.zeilen] - sy[g.zeile] - lZ - zZwischen; };
        bool neu = false;
        for (size_t i = 0; i < p.el.size(); ++i) {
            GL& l = gl[i];
            if (!l.dehnen) continue;
            const float z = randHoehe(p.el[i].c, l.e, -1, flaecheH(p.el[i]) - l.e.a[0] - l.e.a[2]);
            if (z == l.r.hoehe) continue;
            m_aus.lage[p.el[i].c].h = z;
            l.vorgabeH = z;
            neu |= hoeheWirkt(p.el[i].c);
        }
        if (neu) {
            zurueck(st0);
            for (size_t i = 0; i < p.el.size(); ++i) {
                GL& l = gl[i];
                l.r = block(p.el[i].c, l.flaeche, -1, l.bb, l.vorgabeH);
            }
        }
        for (size_t i = 0; i < p.el.size(); ++i) {
            const GEl& g = p.el[i];
            GL& l = gl[i];
            const float h = l.vorgabeH >= 0 ? l.vorgabeH : l.r.hoehe;
            m_aus.lage[g.c].h = h;
            const float fh = flaecheH(g);
            const float rest = fh - h - l.e.a[0] - l.e.a[2];
            float y = l.e.a[0];
            if (l.autoO && l.autoU) y += std::max(0.f, rest) / 2;
            else if (l.autoO) y += std::max(0.f, rest);
            else if (anGrund(i)) y += l.verschub;
            else if (!l.autoU && l.quer == Ausrichtung::Center) y += rest / 2;
            else if (!l.autoU && amEnde(l.quer)) y += rest;
            const float lx = sx[g.spalte] + l.x;
            setze(g.c, k, rtl ? x0 + cw - lx - l.bb : x0 + lx, y0 + sy[g.zeile] + y);
            versetze(g.c, l.flaeche, fh);
            if (erste < 0 && g.zeile == 0) erste = m_aus.lage[g.c].y + (l.r.erste >= 0 ? l.r.erste : h);
        }
        return innenH;
    }

    // ---- Tabellen ----

    struct RZelle {
        uint32_t kasten, zeile, spalte, zeilen, spalten;
        Intr intr;
        float hoehe = 0, erste = -1;
        size_t absaetze = 0;
    };
    struct RZeile {
        uint32_t kasten, gruppe;        // gruppe kKein: Zeile direkt in der Tabelle
    };
    struct Tabelle {
        std::vector<uint32_t> titel, gruppen;
        std::vector<std::pair<uint32_t, uint32_t>> gruppenZeilen;  // je Gruppe [erste Zeile, Ende)
        std::vector<RZeile> zeilen;
        std::vector<RZelle> zellen;     // nach Zeilen geordnet
        std::vector<float> min, max, prozent, fest;     // je Spalte; fest/prozent -1 = keine Angabe
        std::vector<float> colFest, colProzent;         // aus col/colgroup, Randkasten bzw. Prozent; -1 = keine
        std::vector<std::pair<uint32_t, float>> unten;  // Titel unter dem Raster, y ab dessen Unterkante
        uint32_t spalten = 0;
        float untenHoehe = 0;
        float hs = 0, vs = 0;           // border-spacing, 0 bei border-collapse
        bool kollaps = false, rtl = false, festLayout = false;
    };

    static int ganz(std::string_view t, int ersatz) {
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t' || t.front() == '\n')) t.remove_prefix(1);
        int n = ersatz;
        const auto r = std::from_chars(t.data(), t.data() + t.size(), n);
        return r.ec == std::errc() ? n : ersatz;
    }

    //  Zeilen in Anzeigereihenfolge (erster Kopf vorn, erster Fuss hinten), Zellen ins Raster nach dem HTML-Verfahren:
    //  jede Zelle nimmt die erste freie Spalte, rowspan endet an der Gruppe, rowspan=0 reicht bis zu ihrem Ende.
    Tabelle& struktur(uint32_t k) {
        if (const auto it = m_tabellen.find(k); it != m_tabellen.end()) return it->second;
        Tabelle& t = m_tabellen[k];
        const Stil& s = stil(k);
        t.kollaps = s.rahmenZusammen;
        t.rtl = s.richtung == Richtung::Rtl;
        if (!t.kollaps) { t.hs = px(s.rahmenAbstand[0]); t.vs = px(s.rahmenAbstand[1]); }
        uint32_t kopf = kKein, fuss = kKein;
        std::vector<uint32_t> teile;
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes) {
            const uint8_t r = kasten(c).rolle;
            if (kasten(c).flags & KAusserFluss) continue;
            if (r == TTitel) t.titel.push_back(c);
            else if (r == TKopf && kopf == kKein) kopf = c;
            else if (r == TFuss && fuss == kKein) fuss = c;
            else if (r == TZeile || istGruppe(r)) teile.push_back(c);
        }
        if (kopf != kKein) teile.insert(teile.begin(), kopf);
        if (fuss != kKein) teile.push_back(fuss);
        //  rowspan endet an der Gruppe; Zeilen direkt in der Tabelle bilden je Folge eine.
        std::vector<std::pair<uint32_t, uint32_t>> abschnitte;
        uint32_t lose = kKein;
        for (const uint32_t p : teile) {
            const uint32_t anfang = uint32_t(t.zeilen.size());
            if (kasten(p).rolle == TZeile) {
                if (lose == kKein) lose = anfang;
                t.zeilen.push_back({ p, kKein });
                continue;
            }
            if (lose != kKein) abschnitte.push_back({ lose, anfang });
            lose = kKein;
            t.gruppen.push_back(p);
            for (uint32_t z = kasten(p).erstesKind; z != kKein; z = kasten(z).naechstes)
                if (kasten(z).rolle == TZeile && !(kasten(z).flags & KAusserFluss)) t.zeilen.push_back({ z, p });
            t.gruppenZeilen.push_back({ anfang, uint32_t(t.zeilen.size()) });
            abschnitte.push_back(t.gruppenZeilen.back());
        }
        if (lose != kKein) abschnitte.push_back({ lose, uint32_t(t.zeilen.size()) });
        std::vector<uint32_t> belegt;   // je Spalte: wie viele Zeilen ab der laufenden noch belegt sind
        for (const auto& [anfang, ende] : abschnitte) {
            belegt.assign(belegt.size(), 0);
            for (uint32_t z = anfang; z < ende; ++z) {
                uint32_t sp = 0;
                for (uint32_t c = kasten(t.zeilen[z].kasten).erstesKind; c != kKein; c = kasten(c).naechstes) {
                    const Kasten& C = kasten(c);
                    if (C.rolle != TZelle || (C.flags & KAusserFluss)) continue;
                    while (sp < belegt.size() && belegt[sp] > 0) ++sp;
                    int breit = 1, hoch = 1;
                    if (!(C.flags & KAnonym) && C.pseudo == PKein && m_d.knoten[C.knoten].raum == Raum::Html) {
                        if (m_attrColspan != kKein) breit = std::clamp(ganz(m_d.attribut(C.knoten, m_attrColspan), 1), 1, 1000);
                        if (m_attrRowspan != kKein) hoch = std::clamp(ganz(m_d.attribut(C.knoten, m_attrRowspan), 1), 0, 65534);
                    }
                    if (hoch == 0 || hoch > int(ende - z)) hoch = int(ende - z);
                    //  Haertung: hoechstens kMaxSpalten Spalten, was dahinter laege, faellt in die letzte.
                    if (sp >= kMaxSpalten) sp = kMaxSpalten - 1;
                    breit = std::min<int>(breit, int(kMaxSpalten - sp));
                    if (belegt.size() < sp + breit) belegt.resize(sp + breit, 0);
                    for (uint32_t i = sp; i < sp + uint32_t(breit); ++i) belegt[i] = std::max(belegt[i], uint32_t(hoch));
                    t.zellen.push_back({ c, z, sp, uint32_t(hoch), uint32_t(breit), {}, 0, -1, 0 });
                    sp += uint32_t(breit);
                }
                for (uint32_t& b : belegt)
                    if (b > 0) --b;
            }
        }
        //  col und colgroup in Reihenfolge, je mit span; ein colgroup ohne col steht selbst fuer seine Spalten.
        if (const uint32_t tk = kasten(k).knoten; !anonym(k) && tk < m_d.knoten.size()) {
            const uint32_t attrSpan = m_d.nameSuchen("span");
            const auto spannweite = [&](uint32_t n) {
                return attrSpan == kKein ? 1 : std::clamp(ganz(m_d.attribut(n, attrSpan), 1), 1, 1000);
            };
            const auto nimm = [&](uint32_t n, int anzahl) {
                const Stil& cs = m_s.stile[m_s.jeKnoten[n]];
                float fest = -1, prozent = -1;
                if (cs.anzeige != Anzeige::None && cs.breite.art == LArt::Wert) {
                    if (cs.breite.mitProzent || cs.breite.prozent != 0) prozent = cs.breite.prozent / float(kQ);
                    else fest = px(cs.breite.px);
                }
                for (int i = 0; i < anzahl && t.colFest.size() < kMaxSpalten; ++i) {
                    t.colFest.push_back(fest);
                    t.colProzent.push_back(prozent);
                }
            };
            for (uint32_t g = m_d.knoten[tk].erstesKind; g != kKein; g = m_d.knoten[g].naechstes) {
                const Knoten& G = m_d.knoten[g];
                if (G.art != Art::Element || G.raum != Raum::Html || G.name != NColgroup) continue;
                bool mitCol = false;
                for (uint32_t c = G.erstesKind; c != kKein; c = m_d.knoten[c].naechstes)
                    if (m_d.knoten[c].art == Art::Element && m_d.knoten[c].name == NCol) { nimm(c, spannweite(c)); mitCol = true; }
                if (!mitCol) nimm(g, spannweite(g));
            }
        }
        t.festLayout = !anonym(k) && s.tabelleFest && s.breite.art == LArt::Wert;
        t.spalten = std::max(uint32_t(belegt.size()), uint32_t(t.colFest.size()));
        for (const RZelle& z : t.zellen) t.spalten = std::max(t.spalten, z.spalte + z.spalten);
        //  Zeilen und Gruppen malen keinen Rahmen; bei collapse loesen die Zellen ihn auf.
        for (const RZeile& z : t.zeilen) rahmenFuer(z.kasten);
        for (uint32_t g : t.gruppen) rahmenFuer(g);
        if (t.kollaps) kollabiere(k, t);
        return t;
    }

    LRahmen& rahmenFuer(uint32_t k) {
        if (m_aus.rahmenVon.empty()) m_aus.rahmenVon.assign(m_k.kaesten.size(), kKein);
        if (m_aus.rahmenVon[k] == kKein) {
            m_aus.rahmenVon[k] = uint32_t(m_aus.rahmen.size());
            m_aus.rahmen.emplace_back();
        }
        return m_aus.rahmen[m_aus.rahmenVon[k]];
    }

    struct Kandidat {
        float breite = 0;
        RahmenStil stil = RahmenStil::None;
        uint32_t farbe = 0;
        uint8_t herkunft = 0;           // Zelle 4, Zeile 3, Gruppe 2, Tabelle 1
    };

    //  CSS 2.1, 17.6.2.1: hidden gewinnt, none verliert, dann die Breite, der Stil, die Herkunft, zuletzt der Erste.
    static void waehle(Kandidat& w, const Kandidat& c) {
        static constexpr uint8_t rang[] = { 0, 0, 7, 6, 5, 8, 2, 4, 1, 3 };
        if (w.stil == RahmenStil::Hidden) return;
        if (c.stil == RahmenStil::Hidden) { w = c; w.breite = 0; return; }
        const float bw = w.stil == RahmenStil::None ? 0 : w.breite, bc = c.stil == RahmenStil::None ? 0 : c.breite;
        if (bc > bw || (bc == bw && bc > 0 && (rang[int(c.stil)] > rang[int(w.stil)]
                                               || (c.stil == w.stil && c.herkunft > w.herkunft))))
            w = c;
    }

    Kandidat seite(uint32_t k, int i, uint8_t herkunft) const {
        if (k == kKein || anonym(k)) return {};
        const Stil& s = stil(k);
        return { px(s.rahmenBreite[i]), s.rahmenStil[i], s.rahmenFarbe[i], herkunft };
    }

    //  border-collapse: je Zelle und Seite der Sieger gegen Nachbarzelle, Zeile, Gruppe und Tabelle. Jede Zelle malt
    //  oben und links, rechts bzw. unten nur ohne Nachbarn dort; im Randkasten liegt je die Haelfte.
    void kollabiere(uint32_t k, const Tabelle& t) {
        const uint32_t nz = uint32_t(t.zeilen.size()), ns = t.spalten;
        //  Haertung: das Raster nur bis 4 Mio. Felder; darueber zaehlen die Nachbarzellen nicht mit.
        std::vector<uint32_t> raster;
        if (uint64_t(nz) * ns <= 4'000'000) {
            raster.assign(size_t(nz) * ns, kKein);
            for (uint32_t i = 0; i < t.zellen.size(); ++i)
                for (uint32_t z = t.zellen[i].zeile; z < t.zellen[i].zeile + t.zellen[i].zeilen; ++z)
                    for (uint32_t sp = t.zellen[i].spalte; sp < t.zellen[i].spalte + t.zellen[i].spalten; ++sp)
                        raster[size_t(z) * ns + sp] = i;
        }
        const auto bei = [&](int64_t z, int64_t sp) -> uint32_t {
            if (raster.empty() || z < 0 || sp < 0 || z >= nz || sp >= ns) return kKein;
            return raster[size_t(z) * ns + size_t(sp)];
        };
        //  Physische Seite 1 (rechts) bzw. 3 (links) ist logisch Ende bzw. Anfang, in RTL umgekehrt.
        const int anfangS = t.rtl ? 1 : 3, endeS = t.rtl ? 3 : 1;
        float aussen[4] = {};
        for (uint32_t i = 0; i < t.zellen.size(); ++i) {
            const RZelle& z = t.zellen[i];
            const uint32_t zEnde = z.zeile + z.zeilen - 1, sEnde = z.spalte + z.spalten - 1;
            const RZeile& oben = t.zeilen[z.zeile];
            const RZeile& unten = t.zeilen[zEnde];
            Kandidat w[4];
            //  oben
            if (z.zeile == 0) waehle(w[0], seite(k, 0, 1));
            else {
                const RZeile& vor = t.zeilen[z.zeile - 1];
                if (vor.gruppe != oben.gruppe) waehle(w[0], seite(vor.gruppe, 2, 2));
                waehle(w[0], seite(vor.kasten, 2, 3));
                const uint32_t n = bei(z.zeile - 1, z.spalte);
                if (n != kKein) waehle(w[0], seite(t.zellen[n].kasten, 2, 4));
            }
            if (z.zeile == 0 || t.zeilen[z.zeile - 1].gruppe != oben.gruppe) waehle(w[0], seite(oben.gruppe, 0, 2));
            waehle(w[0], seite(oben.kasten, 0, 3));
            waehle(w[0], seite(z.kasten, 0, 4));
            //  unten
            waehle(w[2], seite(z.kasten, 2, 4));
            waehle(w[2], seite(unten.kasten, 2, 3));
            const bool gruppenEnde = zEnde + 1 == nz || t.zeilen[zEnde + 1].gruppe != unten.gruppe;
            if (gruppenEnde) waehle(w[2], seite(unten.gruppe, 2, 2));
            if (zEnde + 1 == nz) waehle(w[2], seite(k, 2, 1));
            else {
                const RZeile& nach = t.zeilen[zEnde + 1];
                if (gruppenEnde) waehle(w[2], seite(nach.gruppe, 0, 2));
                waehle(w[2], seite(nach.kasten, 0, 3));
                const uint32_t n = bei(zEnde + 1, z.spalte);
                if (n != kKein) waehle(w[2], seite(t.zellen[n].kasten, 0, 4));
            }
            //  Anfang (logisch links)
            if (z.spalte == 0) {
                waehle(w[anfangS], seite(k, anfangS, 1));
                waehle(w[anfangS], seite(oben.gruppe, anfangS, 2));
                waehle(w[anfangS], seite(oben.kasten, anfangS, 3));
            } else if (const uint32_t n = bei(z.zeile, z.spalte - 1); n != kKein) {
                waehle(w[anfangS], seite(t.zellen[n].kasten, endeS, 4));
            }
            waehle(w[anfangS], seite(z.kasten, anfangS, 4));
            //  Ende
            waehle(w[endeS], seite(z.kasten, endeS, 4));
            if (sEnde + 1 >= ns) {
                waehle(w[endeS], seite(oben.kasten, endeS, 3));
                waehle(w[endeS], seite(oben.gruppe, endeS, 2));
                waehle(w[endeS], seite(k, endeS, 1));
            } else if (const uint32_t n = bei(z.zeile, sEnde + 1); n != kKein) {
                waehle(w[endeS], seite(t.zellen[n].kasten, anfangS, 4));
            }
            //  Wer malt: oben und links immer, rechts bzw. unten nur ohne Nachbarzelle.
            const int64_t rechtsS = t.rtl ? int64_t(z.spalte) - 1 : int64_t(sEnde) + 1;
            const bool malt[4] = { true, bei(z.zeile, rechtsS) == kKein, bei(zEnde + 1, z.spalte) == kKein, true };
            LRahmen& r = rahmenFuer(z.kasten);
            for (int s = 0; s < 4; ++s) {
                const float b = w[s].stil == RahmenStil::None || w[s].stil == RahmenStil::Hidden ? 0 : w[s].breite;
                r.halb[s] = b / 2;
                r.breite[s] = malt[s] ? b : 0;
                r.farbe[s] = w[s].farbe;
                r.stil[s] = b > 0 ? w[s].stil : RahmenStil::None;
            }
            if (z.zeile == 0) aussen[0] = std::max(aussen[0], r.halb[0]);
            if (zEnde + 1 == nz) aussen[2] = std::max(aussen[2], r.halb[2]);
            if (z.spalte == 0) aussen[anfangS] = std::max(aussen[anfangS], r.halb[anfangS]);
            if (sEnde + 1 >= ns) aussen[endeS] = std::max(aussen[endeS], r.halb[endeS]);
        }
        //  Die Tabelle traegt die aeussere Haelfte, gemalt wird sie von den Zellen.
        LRahmen& r = rahmenFuer(k);
        r = LRahmen{};
        r.ohneInnen = true;
        if (t.zellen.empty()) {
            for (int i = 0; i < 4; ++i) r.halb[i] = seite(k, i, 1).stil == RahmenStil::None ? 0 : seite(k, i, 1).breite / 2;
        } else {
            std::copy(aussen, aussen + 4, r.halb);
        }
    }

    //  min-/max-content je Spalte: erst Zellen ueber eine Spalte, dann breitere nach Spannweite, der Mangel verteilt
    //  nach max-content. Breite der Zelle in px wirkt als Untergrenze von max, in % als Anteil.
    Intr tabellenBreiten(uint32_t k) {
        Tabelle& t = struktur(k);
        const Kanten e = kanten(k, -1);
        const uint32_t n = t.spalten;
        t.min.assign(n, 0);
        t.max.assign(n, 0);
        t.prozent.assign(n, -1);
        t.fest.assign(n, -1);
        std::vector<uint32_t> breit;
        for (uint32_t i = 0; i < t.zellen.size(); ++i) {
            RZelle& z = t.zellen[i];
            z.intr = intrinsisch(z.kasten);
            float fest = -1, prozent = -1;
            zellBreite(z.kasten, fest, prozent);
            if (fest >= 0) z.intr.max = std::max(z.intr.min, fest);
            if (z.spalten > 1) { breit.push_back(i); continue; }
            const uint32_t j = z.spalte;
            t.min[j] = std::max(t.min[j], z.intr.min);
            t.max[j] = std::max(t.max[j], z.intr.max);
            if (prozent >= 0) t.prozent[j] = std::max(t.prozent[j], prozent);
            else if (fest >= 0) t.fest[j] = std::max(t.fest[j], fest);
        }
        std::stable_sort(breit.begin(), breit.end(),
                         [&](uint32_t a, uint32_t b) { return t.zellen[a].spalten < t.zellen[b].spalten; });
        const auto verteile = [&](std::vector<float>& wert, uint32_t von, uint32_t anzahl, float mangel) {
            float summe = 0;
            for (uint32_t j = von; j < von + anzahl; ++j) summe += t.max[j];
            for (uint32_t j = von; j < von + anzahl; ++j)
                wert[j] += summe > 0 ? mangel * t.max[j] / summe : mangel / float(anzahl);
        };
        for (uint32_t i : breit) {
            const RZelle& z = t.zellen[i];
            const float abstand = float(z.spalten - 1) * t.hs;
            float hatMin = 0, hatMax = 0;
            for (uint32_t j = z.spalte; j < z.spalte + z.spalten; ++j) { hatMin += t.min[j]; hatMax += t.max[j]; }
            if (z.intr.min - abstand > hatMin) verteile(t.min, z.spalte, z.spalten, z.intr.min - abstand - hatMin);
            if (z.intr.max - abstand > hatMax) verteile(t.max, z.spalte, z.spalten, z.intr.max - abstand - hatMax);
            for (uint32_t j = z.spalte; j < z.spalte + z.spalten; ++j) t.max[j] = std::max(t.max[j], t.min[j]);
        }
        for (uint32_t j = 0; j < n && j < t.colFest.size(); ++j) {
            if (t.colProzent[j] >= 0) t.prozent[j] = std::max(t.prozent[j], t.colProzent[j]);
            else if (t.colFest[j] >= 0) t.fest[j] = std::max(t.fest[j], t.colFest[j]);
        }
        //  Feste Tabelle: nur ihre Breite und die festen Spalten zaehlen, der Inhalt nicht.
        if (t.festLayout) {
            float summe = e.horizontal() + float(n + 1) * t.hs;
            for (const float w : festeSpalten(t, 0)) summe += std::max(0.f, w);
            Intr r{ summe, summe };
            const Stil& s = stil(k);
            if (bestimmt(s.breite, -1)) r.max = std::max(r.min, s.randBox ? std::max(px(s.breite.px), e.horizontal())
                                                                          : px(s.breite.px) + e.horizontal());
            return r;
        }
        float summeMin = 0, summeMax = 0, summeProzent = 0, ohneProzent = 0, ausProzent = 0;
        for (uint32_t j = 0; j < n; ++j) {
            if (t.fest[j] >= 0) t.max[j] = std::max(t.min[j], t.fest[j]);
            summeMin += t.min[j];
            summeMax += t.max[j];
            if (t.prozent[j] > 0) {
                summeProzent += t.prozent[j];
                ausProzent = std::max(ausProzent, t.max[j] * 100 / t.prozent[j]);
            } else {
                ohneProzent += t.max[j];
            }
        }
        if (summeProzent > 0 && summeProzent < 100) ausProzent = std::max(ausProzent, ohneProzent * 100 / (100 - summeProzent));
        const float rand = e.horizontal() + float(n + 1) * t.hs;
        Intr r{ summeMin + rand, std::max(summeMax, ausProzent) + rand };
        for (uint32_t c : t.titel) {
            const Intr ci = intrinsisch(c);
            r.min = std::max(r.min, ci.min);
        }
        if (!anonym(k)) {
            const Stil& s = stil(k);
            const float h = e.horizontal();
            const auto zuRand = [&](float v) { return s.randBox ? std::max(v, h) : v + h; };
            if (s.breite.art == LArt::Wert && bestimmt(s.breite, -1)) r.max = std::max(r.min, zuRand(px(s.breite.px)));
            if (s.minBreite.art == LArt::Wert && bestimmt(s.minBreite, -1)) {
                const float m = zuRand(px(s.minBreite.px));
                r.min = std::max(r.min, m);
                r.max = std::max(r.max, m);
            }
        }
        r.max = std::max(r.max, r.min);
        return r;
    }

    //  width einer Zelle als Randkasten (fest) bzw. Prozent; -1 ohne Angabe.
    void zellBreite(uint32_t c, float& fest, float& prozent) {
        fest = prozent = -1;
        if (anonym(c)) return;
        const Stil& s = stil(c);
        if (s.breite.art != LArt::Wert) return;
        if (s.breite.mitProzent || s.breite.prozent != 0) { prozent = s.breite.prozent / float(kQ); return; }
        const Kanten ez = kanten(c, -1);
        fest = s.randBox ? std::max(px(s.breite.px), ez.horizontal()) : px(s.breite.px) + ez.horizontal();
    }

    //  table-layout fixed (CSS 2.1, 17.5.2.1): Breiten aus col, sonst aus der ersten Zeile, der Rest gleich auf die
    //  uebrigen Spalten; der Inhalt zaehlt nicht. Bleibt Raum, waechst jede Spalte anteilig.
    std::vector<float> festeSpalten(const Tabelle& t, float raster) {
        const uint32_t n = t.spalten;
        std::vector<float> w(n, -1);
        for (uint32_t j = 0; j < n && j < t.colFest.size(); ++j)
            w[j] = t.colProzent[j] >= 0 ? raster * t.colProzent[j] / 100 : t.colFest[j];
        for (const RZelle& z : t.zellen) {
            if (z.zeile != 0) continue;
            bool frei = true;
            for (uint32_t j = z.spalte; j < z.spalte + z.spalten && j < n; ++j) frei &= w[j] < 0;
            float fest, prozent;
            zellBreite(z.kasten, fest, prozent);
            if (!frei || (fest < 0 && prozent < 0)) continue;
            const float ganz = prozent >= 0 ? raster * prozent / 100 : fest;
            const float je = std::max(0.f, (ganz - float(z.spalten - 1) * t.hs) / float(z.spalten));
            for (uint32_t j = z.spalte; j < z.spalte + z.spalten && j < n; ++j) w[j] = je;
        }
        float belegt = 0;
        uint32_t offen = 0;
        for (float x : w) {
            if (x >= 0) belegt += x;
            else ++offen;
        }
        const float rest = std::max(0.f, raster - belegt);
        for (float& x : w)
            if (x < 0) x = offen > 0 ? rest / float(offen) : 0;
        if (offen == 0 && belegt > 0 && raster > belegt)
            for (float& x : w) x *= raster / belegt;
        return w;
    }

    //  Spaltenbreiten nach CSS Tables 3: zwischen den Stufen min, Prozent, fest, max wird linear verteilt; was ueber
    //  max hinausgeht, bekommen die freien Spalten nach max-content (sonst feste, dann Prozent-Spalten, sonst alle).
    std::vector<float> spaltenBreiten(const Tabelle& t, float raster) const {
        const uint32_t n = t.spalten;
        std::vector<float> stufe[4];
        for (auto& v : stufe) v.resize(n);
        float summe[4] = {};
        for (uint32_t j = 0; j < n; ++j) {
            const bool p = t.prozent[j] > 0, f = !p && t.fest[j] >= 0;
            stufe[0][j] = t.min[j];
            stufe[1][j] = p ? std::max(t.min[j], t.prozent[j] * raster / 100) : t.min[j];
            stufe[2][j] = f ? t.max[j] : stufe[1][j];
            stufe[3][j] = p || f ? stufe[2][j] : t.max[j];
            for (int s = 0; s < 4; ++s) summe[s] += stufe[s][j];
        }
        if (raster <= summe[0]) return stufe[0];
        for (int s = 0; s < 3; ++s) {
            if (raster > summe[s + 1]) continue;
            const float f = summe[s + 1] > summe[s] ? (raster - summe[s]) / (summe[s + 1] - summe[s]) : 1;
            std::vector<float> w(n);
            for (uint32_t j = 0; j < n; ++j) w[j] = stufe[s][j] + (stufe[s + 1][j] - stufe[s][j]) * f;
            return w;
        }
        std::vector<float> w = stufe[3];
        const float rest = raster - summe[3];
        const auto gib = [&](auto&& wer) {
            float s = 0, anzahl = 0;
            for (uint32_t j = 0; j < n; ++j)
                if (wer(j)) { s += w[j]; ++anzahl; }
            if (anzahl == 0) return false;
            for (uint32_t j = 0; j < n; ++j)
                if (wer(j)) w[j] += s > 0 ? rest * w[j] / s : rest / anzahl;
            return true;
        };
        if (!gib([&](uint32_t j) { return t.prozent[j] <= 0 && t.fest[j] < 0 && t.max[j] > 0; })
            && !gib([&](uint32_t j) { return t.prozent[j] <= 0 && t.fest[j] < 0; })
            && !gib([&](uint32_t j) { return t.prozent[j] <= 0 && t.fest[j] >= 0; })
            && !gib([&](uint32_t j) { return t.prozent[j] > 0; }))
            gib([](uint32_t) { return true; });
        return w;
    }

    //  Legt Titel, Gruppen, Zeilen und Zellen; gibt die Hoehe des Rasters samt Abstaenden zurueck. Zellen gelten gegen
    //  ihre Zeile, Zeilen gegen ihre Gruppe (sonst die Tabelle), Gruppen gegen die Tabelle.
    float legeTabelle(uint32_t k, float cw, float mindestH, float x0, float y0, float bb, float& erste, float& titelOben) {
        if (m_intr[k].min < 0) intrinsisch(k);
        Tabelle& t = struktur(k);
        const uint32_t nz = uint32_t(t.zeilen.size()), ns = t.spalten;
        const float raster = std::max(0.f, cw - float(ns + 1) * t.hs);
        const std::vector<float> w = t.festLayout ? festeSpalten(t, raster) : spaltenBreiten(t, raster);
        std::vector<float> sx(ns + 1, 0);      // logischer Anfang je Spalte ab der inneren Kante
        for (uint32_t j = 0; j < ns; ++j) sx[j + 1] = sx[j] + w[j] + t.hs;
        const float innen = std::max(0.f, cw - 2 * t.hs);
        const size_t absAnfang = m_absolut.size();
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes)
            if (kasten(c).flags & KAusserFluss) merke(c, k, x0, y0);
        //  Zellen legen, Hoehen sammeln
        std::vector<float> hoehe(nz, 0), grund(nz, -1), tiefe(nz, 0);
        for (RZelle& z : t.zellen) {
            const float zb = sx[z.spalte + z.spalten] - sx[z.spalte] - t.hs;
            z.absaetze = m_aus.absaetze.size();
            const Erg r = block(z.kasten, cw, -1, zb, -1);
            z.hoehe = r.hoehe;
            z.erste = r.erste;
            setze(z.kasten, t.zeilen[z.zeile].kasten, t.rtl ? innen - sx[z.spalte] - zb : sx[z.spalte], 0);
            if (z.zeilen == 1) hoehe[z.zeile] = std::max(hoehe[z.zeile], z.hoehe);
            if (grundlinienZelle(z.kasten)) {
                const float g = z.erste >= 0 ? z.erste : z.hoehe - unterInhalt(z.kasten);
                grund[z.zeile] = std::max(grund[z.zeile], g);
                if (z.zeilen == 1) tiefe[z.zeile] = std::max(tiefe[z.zeile], z.hoehe - g);
            }
        }
        //  visibility collapse nimmt die Zeile samt ihrem Abstand heraus; Prozent gelten gegen die Hoehe der Tabelle
        //  ohne die Abstaende (wie Chromium), nur wenn sie fest ist.
        std::vector<uint8_t> zu(nz, 0);
        uint32_t sichtbar = 0;
        for (uint32_t z = 0; z < nz; ++z) {
            const uint32_t zk = t.zeilen[z].kasten;
            zu[z] = !anonym(zk) && stil(zk).sichtbarkeit == Sichtbarkeit::Collapse;
            sichtbar += !zu[z];
        }
        const float prozentBezug = mindestH >= 0 ? std::max(0.f, mindestH - float(sichtbar + 1) * t.vs) : -1;
        for (uint32_t z = 0; z < nz; ++z) {
            if (grund[z] >= 0) hoehe[z] = std::max(hoehe[z], grund[z] + tiefe[z]);
            const uint32_t zk = t.zeilen[z].kasten;
            if (!anonym(zk) && bestimmt(stil(zk).hoehe, prozentBezug))
                hoehe[z] = std::max(hoehe[z], aufloesen(stil(zk).hoehe, prozentBezug));
        }
        //  Zeilenuebergreifende Zellen: der Mangel geht anteilig an ihre Zeilen.
        for (const RZelle& z : t.zellen) {
            if (z.zeilen < 2) continue;
            float hat = float(z.zeilen - 1) * t.vs, summe = 0;
            for (uint32_t r = z.zeile; r < z.zeile + z.zeilen; ++r) summe += hoehe[r];
            hat += summe;
            if (z.hoehe <= hat) continue;
            const float mangel = z.hoehe - hat;
            for (uint32_t r = z.zeile; r < z.zeile + z.zeilen; ++r)
                hoehe[r] += summe > 0 ? mangel * hoehe[r] / summe : mangel / float(z.zeilen);
        }
        for (uint32_t z = 0; z < nz; ++z)
            if (zu[z]) hoehe[z] = 0;
        float gesamt = nz > 0 ? float(sichtbar + 1) * t.vs : 0;
        for (float h : hoehe) gesamt += h;
        //  Hoehere Tabelle: der Rest geht an Zeilen ohne eigene height nach ihrer Hoehe, sind alle leer, zu gleichen
        //  Teilen; haben alle eine, an alle.
        if (mindestH > gesamt && nz > 0) {
            const auto frei = [&](uint32_t z) {
                const uint32_t r = t.zeilen[z].kasten;
                return !zu[z] && (anonym(r) || !bestimmt(stil(r).hoehe, prozentBezug));
            };
            float summe = 0, anzahl = 0;
            for (uint32_t z = 0; z < nz; ++z)
                if (frei(z)) { summe += hoehe[z]; ++anzahl; }
            const bool alle = anzahl == 0;
            if (alle)
                for (float h : hoehe) summe += h;
            const float mehr = mindestH - gesamt;
            for (uint32_t z = 0; z < nz; ++z) {
                if (zu[z] || (!alle && !frei(z))) continue;
                hoehe[z] += summe > 0 ? mehr * hoehe[z] / summe : mehr / (alle ? float(std::max(sichtbar, 1u)) : anzahl);
            }
            gesamt = mindestH;
        }
        //  Zeilen und Gruppen setzen
        std::vector<float> zy(nz + 1, 0);       // Oberkante je Zeile ab y0
        float y = t.vs;
        for (uint32_t z = 0; z < nz; ++z) {
            if (zu[z]) { zy[z] = std::max(0.f, y - t.vs); continue; }
            zy[z] = y;
            y += hoehe[z] + t.vs;
        }
        zy[nz] = y;
        const float gx = x0 + t.hs;
        std::vector<float> gy(t.gruppen.size(), 0);
        for (size_t i = 0; i < t.gruppen.size(); ++i) {
            const auto [anfang, ende] = t.gruppenZeilen[i];
            const float oben = anfang < nz ? zy[anfang] : std::max(0.f, zy[nz] - t.vs);
            gy[i] = oben;
            const uint32_t g = t.gruppen[i];
            setze(g, k, gx, y0 + oben);
            m_aus.lage[g].b = innen;
            m_aus.lage[g].h = ende > anfang ? zy[ende - 1] + hoehe[ende - 1] - oben : 0;
            versetze(g, cw, -1);
            for (uint32_t z = anfang; z < ende; ++z) setze(t.zeilen[z].kasten, g, 0, zy[z] - oben);
        }
        for (uint32_t z = 0; z < nz; ++z) {
            const uint32_t r = t.zeilen[z].kasten;
            if (t.zeilen[z].gruppe == kKein) setze(r, k, gx, y0 + zy[z]);
            m_aus.lage[r].b = innen;
            m_aus.lage[r].h = hoehe[z];
            versetze(r, cw, -1);
        }
        //  Zellen auf volle Hoehe, Inhalt nach vertical-align
        for (const RZelle& z : t.zellen) {
            const float h = zy[z.zeile + z.zeilen - 1] + hoehe[z.zeile + z.zeilen - 1] - zy[z.zeile];
            float d = 0;
            if (grundlinienZelle(z.kasten)) d = grund[z.zeile] - (z.erste >= 0 ? z.erste : z.hoehe - unterInhalt(z.kasten));
            else if (vertikal(z.kasten) == Vertikal::Middle) d = (h - z.hoehe) / 2;
            else if (vertikal(z.kasten) == Vertikal::Bottom) d = h - z.hoehe;
            if (d > 0) verschiebe(z.kasten, d, z.absaetze);
            m_aus.lage[z.kasten].h = h;
            versetze(z.kasten, cw, -1);
        }
        for (const RZeile& r : t.zeilen)
            if (istBezug(r.kasten)) absolute(absAnfang, r.kasten);
        for (uint32_t g : t.gruppen)
            if (istBezug(g)) absolute(absAnfang, g);
        //  Grundlinie der Tabelle: die der ersten Zeile, ohne Grundlinien-Zellen deren Unterkante.
        erste = nz == 0 ? -1 : y0 + zy[0] + (grund[0] >= 0 ? grund[0] : hoehe[0]);
        //  Titel: Breite der Tabelle, oben bzw. unten ausserhalb des Randkastens
        titelOben = 0;
        t.unten.clear();
        float& untenY = t.untenHoehe;
        untenY = 0;
        for (uint32_t c : t.titel) {
            const Erg r = block(c, bb, -1, -1, -1);
            const float oben = r.oben.wert(), ganz = oben + r.hoehe + r.unten.wert();
            if (stil(c).titelUnten) {
                t.unten.push_back({ c, untenY + oben });
                m_aus.lage[c].x = r.links;
                m_bezug[c] = k;
                untenY += ganz;
            } else {
                setze(c, k, r.links, titelOben + oben);
                titelOben += ganz;
            }
        }
        for (uint32_t c : t.titel)
            if (!stil(c).titelUnten) m_aus.lage[c].y -= titelOben;
        return gesamt;
    }

    //  Titel unter dem Raster: y ab der Unterkante des Randkastens; gibt ihre Hoehe zurueck.
    float titelUnten(uint32_t k, float hoehe) {
        const auto it = m_tabellen.find(k);
        if (it == m_tabellen.end()) return 0;
        for (const auto& [c, y] : it->second.unten) m_aus.lage[c].y = hoehe + y;
        return it->second.untenHoehe;
    }

    Vertikal vertikal(uint32_t k) const { return anonym(k) ? Vertikal::Baseline : stil(k).vertikal; }
    bool grundlinienZelle(uint32_t k) const {
        const Vertikal v = vertikal(k);
        return v != Vertikal::Middle && v != Vertikal::Bottom && v != Vertikal::Top;
    }
    //  Zelle ohne Zeilen: ihre Grundlinie ist die Unterkante des Inhalts.
    float unterInhalt(uint32_t k) const {
        const Kanten e = kanten(k, -1);
        return e.r[2] + e.i[2];
    }

    //  Inhalt eines Kastens senkrecht verschieben: alles, was gegen ihn gilt, samt seinen Zeilen.
    void verschiebe(uint32_t k, float d, size_t absaetze) {
        std::vector<uint32_t>& stapel = m_schieben;
        stapel.clear();
        for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes) stapel.push_back(c);
        while (!stapel.empty()) {
            const uint32_t c = stapel.back();
            stapel.pop_back();
            if (m_bezug[c] == k) m_aus.lage[c].y += d;
            if (kasten(c).art == KArt::Inline)
                for (uint32_t x = kasten(c).erstesKind; x != kKein; x = kasten(x).naechstes) stapel.push_back(x);
        }
        for (size_t i = absaetze; i < m_aus.absaetze.size(); ++i) {
            const Absatz& a = m_aus.absaetze[i];
            if (a.kasten != k) continue;
            for (uint32_t z = a.zeilenAnfang; z < a.zeilenAnfang + a.zeilenAnzahl; ++z) m_aus.zeilen[z].y += d;
            for (uint32_t l = a.laufAnfang; l < a.laufAnfang + a.laufAnzahl; ++l) {
                Lauf& lf = m_aus.laeufe[l];
                lf.grundlinie += d;
                for (uint32_t g = lf.anfang; g < lf.anfang + lf.anzahl; ++g) m_aus.glyphLagen[2 * g + 1] += d;
            }
            for (uint32_t f = a.fragmentAnfang; f < a.fragmentAnfang + a.fragmentAnzahl; ++f) m_aus.fragmente[f].r.y += d;
        }
        for (Absolut& a : m_absolut)
            if (a.rahmen == k) a.y += d;
    }

    void setze(uint32_t c, uint32_t bezug, float x, float y) {
        m_aus.lage[c].x = x;
        m_aus.lage[c].y = y;
        m_bezug[c] = bezug;
    }

    //  relative verschiebt nach dem Legen; sticky ruht ohne Rollen an seiner Stelle.
    void versetze(uint32_t c, float cbB, float cbH) {
        if (anonym(c)) return;
        const Stil& s = stil(c);
        if (s.position != Lage::Relative) return;
        const auto wert = [&](int i, float bezug) {
            const Laenge& l = s.versatz[i];
            return l.art == LArt::Wert && bestimmt(l, bezug) ? aufloesen(l, bezug) : NAN;
        };
        const float l = wert(3, cbB), r = wert(1, cbB), t = wert(0, cbH), b = wert(2, cbH);
        m_aus.lage[c].x += !std::isnan(l) ? l : !std::isnan(r) ? -r : 0;
        m_aus.lage[c].y += !std::isnan(t) ? t : !std::isnan(b) ? -b : 0;
    }

    // ---- absolute und fixed ----

    bool istBezug(uint32_t k) const {
        if (anonym(k)) return false;
        const Stil& s = stil(k);
        return s.position != Lage::Static || s.transformation != 0;
    }

    void merke(uint32_t c, uint32_t rahmen, float x, float y) {
        const bool fest = stil(c).position == Lage::Fixed;
        uint32_t cb = kKein;
        for (uint32_t a = kasten(c).eltern; a != kKein; a = kasten(a).eltern) {
            if (anonym(a)) continue;
            const Stil& as = stil(a);
            if (as.transformation != 0 || (!fest && as.position != Lage::Static)) { cb = a; break; }
        }
        while (cb != kKein && kasten(cb).art == KArt::Inline) cb = kasten(cb).eltern;
        m_absolut.push_back({ c, cb, rahmen, x, y });
    }

    void absolute(size_t anfang, uint32_t cb) {
        for (size_t i = anfang; i < m_absolut.size();) {
            if (m_absolut[i].bezug != cb) { ++i; continue; }
            const Absolut a = m_absolut[i];
            m_absolut.erase(m_absolut.begin() + std::ptrdiff_t(i));
            legeAbsolut(a);
        }
    }

    void legeAbsolut(const Absolut& a) {
        float cx = 0, cy = 0, cB = m_breite, cH = float(m_m.hoehe);
        if (a.bezug != kKein) {
            const Stil& bs = stil(a.bezug);
            cx = px(bs.rahmenBreite[3]);
            cy = px(bs.rahmenBreite[0]);
            cB = m_aus.lage[a.bezug].b - cx - px(bs.rahmenBreite[1]);
            cH = m_aus.lage[a.bezug].h - cy - px(bs.rahmenBreite[2]);
        }
        float sx = a.x, sy = a.y;
        for (uint32_t f = a.rahmen; f != a.bezug && f != kKein; f = m_bezug[f]) {
            sx += m_aus.lage[f].x;
            sy += m_aus.lage[f].y;
        }
        const uint32_t c = a.kasten;
        const Stil& s = stil(c);
        if (kasten(c).rolle == TTabelle) struktur(c);
        const Kanten e = kanten(c, cB);
        const auto wert = [&](int i, float bezug) {
            const Laenge& l = s.versatz[i];
            return l.art == LArt::Wert && bestimmt(l, bezug) ? aufloesen(l, bezug) : NAN;
        };
        const float L = wert(3, cB), R = wert(1, cB), T = wert(0, cH), B = wert(2, cH);
        float ml = e.autoL ? 0 : e.a[3], mr = e.autoR ? 0 : e.a[1];
        float mt = e.a[0], mb = e.a[2];
        float vorgabeB;
        if (s.breite.art == LArt::Auto && !(kasten(c).flags & KErsetzt)) {
            float fuellen;
            if (!std::isnan(L) && !std::isnan(R)) {
                fuellen = std::max(0.f, cB - L - R - ml - mr);
            } else {
                const Intr in = intrinsisch(c);
                const float verfuegbar = std::max(0.f, cB - (!std::isnan(L) ? L : !std::isnan(R) ? R : sx - cx) - ml - mr);
                fuellen = std::min(std::max(in.min, verfuegbar), in.max);
            }
            vorgabeB = randBreite(c, e, cB, fuellen);
        } else {
            vorgabeB = randBreite(c, e, cB, 0);
        }
        float vorgabeH = -1;
        if (s.hoehe.art == LArt::Auto && !std::isnan(T) && !std::isnan(B))
            vorgabeH = std::max(0.f, cH - T - B - mt - mb);
        const Erg r = block(c, cB, cH, vorgabeB, vorgabeH);
        const float bb = vorgabeB, bh = r.hoehe;
        sy += r.versatz;
        float x, y;
        if (!std::isnan(L)) {
            if (!std::isnan(R) && (e.autoL || e.autoR)) {
                const float rest = std::max(0.f, cB - L - R - bb - ml - mr);
                if (e.autoL && e.autoR) ml += rest / 2;
                else if (e.autoL) ml += rest;
            }
            x = cx + L + ml;
        } else if (!std::isnan(R)) {
            x = cx + cB - R - mr - bb;
        } else {
            x = sx + ml;
        }
        if (!std::isnan(T)) {
            if (!std::isnan(B) && s.aussen[0].art == LArt::Auto && s.aussen[2].art == LArt::Auto)
                mt += std::max(0.f, cH - T - B - bh) / 2;
            y = cy + T + mt;
        } else if (!std::isnan(B)) {
            y = cy + cH - B - mb - bh;
        } else {
            y = sy + mt;
        }
        if (!std::isnan(T) || !std::isnan(B)) y += r.versatz;
        setze(c, a.bezug, x, y);
    }

    // ---- Zeilen ----

    //  Gleiche Herkunft statt QTextFormat::operator==: der vergleicht alle Eigenschaften samt Familienliste.
    void format(Sammlung& sm, int32_t anfang, int32_t laenge, const QTextCharFormat& f, const void* herkunft = nullptr) {
        if (laenge <= 0) return;
        if (herkunft && herkunft == sm.letzteHerkunft && !sm.formate.isEmpty()) {
            QTextLayout::FormatRange& z = sm.formate.last();
            if (z.start + z.length == anfang) { z.length += laenge; return; }
        }
        sm.letzteHerkunft = herkunft;
        sm.formate.append(QTextLayout::FormatRange{ anfang, laenge, f });
    }

    //  Kante mit fester Breite: ein WORD JOINER, dessen Buchstabenabstand die Breite traegt.
    void kante(Sammlung& sm, float breite, const Schrift& f) {
        const int32_t pos = int32_t(sm.text.size());
        sm.text.append(QChar(kKante));
        QTextCharFormat fmt = format(f, sm.s);
        fmt.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        fmt.setFontLetterSpacing(breite * sm.s);
        format(sm, pos, 1, fmt);
    }

    //  Wort anhaengen, an ASCII-Paaren mit Fuge; `pos` sammelt die Fugen (nur bei Buchstabenabstand gebraucht).
    void wort(Sammlung& sm, const char* p, size_t n, std::vector<int32_t>* pos) {
        const qsizetype t = sm.text.size();
        const auto davor = [&](size_t i, size_t zurueck) -> char16_t {
            if (i >= zurueck) return uint8_t(p[i - zurueck]);
            const qsizetype j = t + qsizetype(i) - qsizetype(zurueck);
            return j >= 0 ? sm.text[j].unicode() : 0;
        };
        size_t a = 0;
        for (size_t i = 0; i < n; ++i) {
            const char16_t c = uint8_t(p[i]);
            if (!sichtbarAscii(c)) continue;
            const char16_t v = davor(i, 1);
            if (!sichtbarAscii(v) || (asciiAlnum(v) && asciiAlnum(c))) continue;
            const char16_t vv = davor(i, 2);
            const char16_t f = asciiFuge(vv, v, c);
            if (!f) continue;
            sm.text.append(QUtf8StringView(p + a, qsizetype(i - a)));
            if (pos) pos->push_back(int32_t(sm.text.size()));
            sm.text.append(QChar(f));
            a = i;
        }
        sm.text.append(QUtf8StringView(p + a, qsizetype(n - a)));
    }

    void text(Sammlung& sm, uint32_t k, const char* p, size_t n, float shift) {
        const Kasten& K = kasten(k);
        const Stil& s = stil(k);
        const Schrift& f = schrift(K.stil);
        const Leerraum ws = s.leerraum;
        const bool fasst = ws == Leerraum::Normal || ws == Leerraum::NoWrap || ws == Leerraum::PreLine;
        const bool zeilenBleiben = ws != Leerraum::Normal && ws != Leerraum::NoWrap;
        const bool fest = sm.umbricht && (ws == Leerraum::NoWrap || ws == Leerraum::Pre);
        const bool fugen = sm.umbricht && !fest && s.wortUmbruch != uint8_t(WortUmbruch::BreakAll);
        const int32_t anfang = int32_t(sm.text.size());
        m_fugen.clear();
        size_t i = 0;
        while (i < n) {
            size_t j = i;
            while (j < n && p[j] != ' ' && p[j] != '\t' && p[j] != '\n' && p[j] != '\f' && p[j] != '\r') ++j;
            if (j > i) {
                if (fugen) wort(sm, p + i, j - i, f.abstand != 0 ? &m_fugen : nullptr);
                else sm.text.append(QUtf8StringView(p + i, qsizetype(j - i)));
                sm.leerZuletzt = false;
                sm.leerPos = -1;
                sm.inhalt = true;
                i = j;
                continue;
            }
            const char c = p[i++];
            if (c == '\n' && zeilenBleiben) {
                if (fasst && sm.leerPos >= 0) sm.text[sm.leerPos] = QChar(kTrenner);
                sm.text.append(QChar(QChar::LineSeparator));
                sm.leerZuletzt = fasst;
                sm.leerPos = -1;
                sm.inhalt = true;
            } else if (fasst) {
                if (sm.leerZuletzt) continue;
                sm.leerPos = int32_t(sm.text.size());
                sm.text.append(QChar(fest ? kNbsp : u' '));
                sm.leerZuletzt = true;
            } else {
                sm.text.append(c == '\t' ? QChar(u'\t') : QChar(fest ? kNbsp : u' '));
                sm.leerZuletzt = false;
                sm.leerPos = -1;
                sm.inhalt = true;
            }
        }
        const int32_t laenge = int32_t(sm.text.size()) - anfang;
        if (laenge <= 0) return;
        //  Eine Fuge traegt keinen Buchstabenabstand, sonst stuende sie als Luecke im Wort.
        int32_t a = anfang;
        for (const int32_t q : m_fugen) {
            format(sm, a, q - a, format(f, sm.s), &f);
            QTextCharFormat ohne = format(f, sm.s);
            ohne.setFontLetterSpacingType(QFont::AbsoluteSpacing);
            ohne.setFontLetterSpacing(0);
            format(sm, q, 1, ohne);
            a = q + 1;
        }
        format(sm, a, anfang + laenge - a, format(f, sm.s), &f);
        sm.stuecke.push_back({ k, anfang, laenge, shift });
    }

    float verschiebung(const Stil& s, const Stil& eltern, const Schrift& f, const Schrift& ef) {
        const float g = px(eltern.schriftGroesse);
        switch (s.vertikal) {
        case Vertikal::Sub: return std::floor(g / 5 + 1);
        case Vertikal::Super: return -std::floor(g / 3 + 1);
        case Vertikal::Middle: return -ef.xHoehe / 2 + (f.oben - f.unten) / 2;
        case Vertikal::Wert: return -aufloesen(s.vertikalWert, zeilenHoehe(s, f));
        default: return 0;
        }
    }

    //  Inline-Inhalt von `k` in den Absatztext; `modus` Min/Max misst nur (Atome mit ihren Grenzbreiten).
    void sammle(uint32_t k, float cw, Modus modus, Sammlung& sm) {
        const Stil& s = stil(k);
        sm.umbricht = s.leerraum != Leerraum::Pre && s.leerraum != Leerraum::NoWrap;
        sm.s = skalaFuer(k);
        inlineKinder(k, k, cw, 0, modus, sm);
        if (sm.leerPos >= 0) sm.text[sm.leerPos] = QChar(kTrenner);
    }

    void inlineKinder(uint32_t eltern, uint32_t block, float cw, float shift, Modus modus, Sammlung& sm) {
        for (uint32_t c = kasten(eltern).erstesKind; c != kKein; c = kasten(c).naechstes) {
            const Kasten& C = kasten(c);
            if (C.art == KArt::Marke) { m_bezug[c] = block; continue; }
            if (C.flags & KAusserFluss) {
                if (modus == Modus::Legen) sm.ausserFluss.push_back({ c, int32_t(sm.text.size()) });
                continue;
            }
            if (modus == Modus::Legen) m_bezug[c] = block;
            switch (C.art) {
            case KArt::Text: {
                const std::string& quelle = (C.flags & KEigenText) ? m_k.text : m_d.text;
                if (C.anfang + uint64_t(C.laenge) <= quelle.size()) text(sm, c, quelle.data() + C.anfang, C.laenge, shift);
                break;
            }
            case KArt::Umbruch:
                if (sm.leerPos >= 0) sm.text[sm.leerPos] = QChar(kTrenner);
                sm.stuecke.push_back({ c, int32_t(sm.text.size()), 1 });
                if (modus == Modus::Legen) sm.umbrueche.push_back({ c, int32_t(sm.text.size()) });
                sm.text.append(QChar(QChar::LineSeparator));
                sm.leerZuletzt = true;
                sm.leerPos = -1;
                sm.inhalt = true;
                break;
            case KArt::Inline: inlineKasten(c, block, cw, shift, modus, sm); break;
            case KArt::Atom: atom(c, cw, shift, modus, sm); break;
            default: break;
            }
        }
    }

    void inlineKasten(uint32_t c, uint32_t block, float cw, float shift, Modus modus, Sammlung& sm) {
        const Kasten& C = kasten(c);
        const Stil& s = stil(c);
        const Schrift& f = schrift(C.stil);
        const uint32_t ep = C.eltern;
        const Schrift& ef = schrift(kasten(ep).stil);
        const float sh = shift + verschiebung(s, stil(ep), f, ef);
        const Kanten e = kanten(c, cw);
        const bool rtl = s.richtung == Richtung::Rtl;
        const float anfangB = rtl ? e.a[1] + e.r[1] + e.i[1] : e.a[3] + e.r[3] + e.i[3];
        const float endeB = rtl ? e.a[3] + e.r[3] + e.i[3] : e.a[1] + e.r[1] + e.i[1];
        const int32_t anfang = int32_t(sm.text.size());
        if (anfangB != 0) {
            kante(sm, anfangB, f);
            sm.inhalt |= (rtl ? e.r[1] + e.i[1] : e.r[3] + e.i[3]) > 0;
        }
        if (C.flags & KIsoliert) {
            sm.text.append(QChar(rtl ? 0x2067 : 0x2066));
            format(sm, int32_t(sm.text.size()) - 1, 1, format(f, sm.s), &f);
        }
        inlineKinder(c, block, cw, sh, modus, sm);
        if (C.flags & KIsoliert) {
            sm.text.append(QChar(0x2069));
            format(sm, int32_t(sm.text.size()) - 1, 1, format(f, sm.s), &f);
        }
        if (endeB != 0) {
            kante(sm, endeB, f);
            sm.inhalt |= (rtl ? e.r[3] + e.i[3] : e.r[1] + e.i[1]) > 0;
        }
        const int32_t ende = int32_t(sm.text.size());
        const float lh = zeilenHoehe(s, f);
        const float halb = (lh - (f.oben + f.unten)) / 2;
        sm.beitraege.push_back({ anfang, ende, sh - f.oben - halb, sh + f.unten + halb });
        sm.stuecke.push_back({ c, anfang, ende - anfang, sh });
        if (modus == Modus::Legen)
            sm.raender.push_back({ c, anfang, ende, sh, f.oben + e.r[0] + e.i[0], f.unten + e.r[2] + e.i[2],
                                   rtl ? e.a[1] : e.a[3], rtl ? e.a[3] : e.a[1] });
    }

    void atom(uint32_t c, float cw, float shift, Modus modus, Sammlung& sm) {
        const Kasten& C = kasten(c);
        const Stil& s = stil(c);
        if (C.rolle == TTabelle) struktur(c);
        const Kanten e = kanten(c, cw);
        const float ml = e.autoL ? 0 : e.a[3], mr = e.autoR ? 0 : e.a[1];
        float bb, hoehe = 0, grund = 0, versatz = 0;
        uint32_t absatzVon = 0;
        if (modus == Modus::Legen) {
            float fuellen = 0;
            if (s.breite.art == LArt::Auto && !(C.flags & KErsetzt)) {
                const Intr in = intrinsisch(c);
                fuellen = std::min(std::max(in.min, std::max(0.f, cw - ml - mr)), in.max);
            }
            bb = randBreite(c, e, cw, fuellen);
            absatzVon = uint32_t(m_aus.absaetze.size());
            const Erg r = block(c, cw, -1, bb, -1);
            versatz = r.versatz;
            hoehe = e.a[0] + r.hoehe + e.a[2];
            const bool eigeneGrund = r.grundlinie >= 0 && ((C.flags & KErsetzt)
                                  || (s.ueberlaufX == Ueberlauf::Visible && s.ueberlaufY == Ueberlauf::Visible));
            grund = eigeneGrund ? e.a[0] + r.grundlinie : hoehe;
        } else {
            const Intr in = intrinsisch(c);
            bb = modus == Modus::Min ? in.min : in.max;
        }
        const float breite = ml + bb + mr;
        const Schrift& f = schrift(kasten(C.eltern).stil);
        sm.text.append(QChar(kTrenner));
        format(sm, int32_t(sm.text.size()) - 1, 1, format(f, sm.s), &f);
        const int32_t pos = int32_t(sm.text.size());
        kante(sm, breite, f);
        sm.text.append(QChar(kTrenner));
        format(sm, int32_t(sm.text.size()) - 1, 1, format(f, sm.s), &f);
        sm.inhalt = true;
        sm.leerZuletzt = false;
        sm.leerPos = -1;
        sm.stuecke.push_back({ c, pos, 1, shift });
        if (modus != Modus::Legen) return;
        float oben = shift - grund;
        if (s.vertikal == Vertikal::Middle) oben = shift - f.xHoehe / 2 - hoehe / 2;
        else if (s.vertikal == Vertikal::Sub || s.vertikal == Vertikal::Super || s.vertikal == Vertikal::Wert)
            oben += verschiebung(s, stil(C.eltern), f, f);
        sm.beitraege.push_back({ pos, pos + 1, oben, oben + hoehe });
        sm.atome.push_back({ c, pos, ml, oben + e.a[0] + versatz, absatzVon, uint32_t(m_aus.absaetze.size()) });
    }

    QTextOption textOption(const Stil& s, const Schrift& f, bool umbricht, float skala) const {
        QTextOption o;
        const bool rtl = s.richtung == Richtung::Rtl;
        o.setTextDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
        if (!umbricht) o.setWrapMode(QTextOption::NoWrap);
        else if (s.wortUmbruch == uint8_t(WortUmbruch::BreakAll)) o.setWrapMode(QTextOption::WrapAnywhere);
        else if (s.wortUmbruch == uint8_t(WortUmbruch::BreakWord)) o.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        else o.setWrapMode(QTextOption::WordWrap);
        Qt::Alignment a;
        switch (s.textAusrichtung) {
        case TextAusrichtung::Left: a = Qt::AlignLeft | Qt::AlignAbsolute; break;
        case TextAusrichtung::Right: a = Qt::AlignRight | Qt::AlignAbsolute; break;
        case TextAusrichtung::Center: a = Qt::AlignHCenter; break;
        case TextAusrichtung::Justify: a = Qt::AlignJustify; break;
        case TextAusrichtung::End: a = (rtl ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignAbsolute; break;
        default: a = (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute; break;
        }
        o.setAlignment(a);
        o.setUseDesignMetrics(true);
        o.setTabStopDistance(8 * f.leer * skala);
        return o;
    }

    //  Breiteste Zeile bei Zeilenbreite `breite`: 1e7 ergibt max-content, 0 min-content.
    float absatzBreite(uint32_t k, const Sammlung& sm, float breite) {
        if (!sm.inhalt) return 0;
        const Stil& s = stil(k);
        const Schrift& f = schrift(kasten(k).stil);
        QTextLayout l(sm.text, skaliert(f, sm.s));
        l.setFormats(sm.formate);
        l.setTextOption(textOption(s, f, sm.umbricht, sm.s));
        float w = 0;
        l.beginLayout();
        for (QTextLine z = l.createLine(); z.isValid(); z = l.createLine()) {
            z.setLineWidth(breite * sm.s);
            w = std::max(w, float(z.naturalTextWidth()) / sm.s);
        }
        l.endLayout();
        return std::ceil(w * kQ) / kQ;
    }

    bool zeilen(uint32_t k, float cw, float x0, float y0, float& hoehe, float& grund) {
        Sammlung sm;
        sammle(k, cw, Modus::Legen, sm);
        if (!sm.inhalt) {
            for (const auto& [c, pos] : sm.ausserFluss) merke(c, k, x0, y0);
            return false;
        }
        const Stil& s = stil(k);
        const Schrift& f = schrift(kasten(k).stil);
        const float lh = zeilenHoehe(s, f);
        const float halb = (lh - (f.oben + f.unten)) / 2;
        const float strutOben = -f.oben - halb, strutUnten = f.unten + halb;
        std::stable_sort(sm.beitraege.begin(), sm.beitraege.end(),
                         [](const Beitrag& x, const Beitrag& y) { return x.anfang < y.anfang; });
        QTextLayout l(sm.text, skaliert(f, sm.s));
        l.setFormats(sm.formate);
        l.setTextOption(textOption(s, f, sm.umbricht, sm.s));
        const float sk = sm.s;
        const auto xVon = [&](const QTextLine& z, int32_t pos, QTextLine::Edge kante = QTextLine::Leading) {
            return float(z.cursorToX(pos, kante)) / sk;
        };
        const uint32_t absatz = uint32_t(m_aus.absaetze.size());
        const uint32_t ersteZeile = uint32_t(m_aus.zeilen.size());
        const uint32_t ersterLauf = uint32_t(m_aus.laeufe.size()), erstesFragment = uint32_t(m_aus.fragmente.size());
        std::vector<uint32_t>& texte = m_texte;
        texte.clear();
        for (uint32_t i = 0; i < sm.stuecke.size(); ++i)
            if (kasten(sm.stuecke[i].kasten).art == KArt::Text) texte.push_back(i);
        std::vector<Rechteck> raender(sm.raender.size(), Rechteck{ 0, 0, -1, -1 });
        size_t atom = 0, aus = 0, umbruch = 0;
        float y = y0;
        const int32_t n = int32_t(sm.text.size());
        l.beginLayout();
        for (QTextLine z = l.createLine(); z.isValid(); z = l.createLine()) {
            z.setLineWidth(cw * sk);
            const int32_t a = z.textStart(), e = a + z.textLength();
            //  Ein Umbruch am Absatzende eroeffnet keine leere Zeile.
            if (a == e && a == n && a > 0 && sm.text[a - 1] == QChar(QChar::LineSeparator)) break;
            float oben = strutOben, unten = strutUnten;
            for (const Beitrag& b : sm.beitraege) {
                if (b.anfang >= e && !(b.anfang == e && e == n)) break;
                if (b.ende <= a && !(b.anfang == b.ende && b.anfang == a)) continue;
                oben = std::min(oben, b.oben);
                unten = std::max(unten, b.unten);
            }
            const float zh = unten - oben, gl = -oben;
            m_aus.zeilen.push_back(Zeile{ x0, y, cw, zh, gl, a, e - a, absatz });
            glyphen(z, sm, texte, x0, y + gl, uint32_t(m_aus.zeilen.size() - 1));
            for (; atom < sm.atome.size() && sm.atome[atom].pos < e; ++atom) {
                const AtomLage& at = sm.atome[atom];
                const float xa = xVon(z, at.pos), xb = xVon(z, at.pos, QTextLine::Trailing);
                setze(at.kasten, k, x0 + std::min(xa, xb) + at.links, y + gl + at.oben);
                versetze(at.kasten, cw, -1);
            }
            for (; umbruch < sm.umbrueche.size() && sm.umbrueche[umbruch].second < e; ++umbruch) {
                const uint32_t c = sm.umbrueche[umbruch].first;
                const Schrift& uf = schrift(kasten(c).stil);
                m_aus.lage[c] = Rechteck{ x0 + xVon(z, sm.umbrueche[umbruch].second), y + gl - uf.oben, 0, uf.oben + uf.unten };
            }
            for (; aus < sm.ausserFluss.size() && sm.ausserFluss[aus].second < e; ++aus)
                merke(sm.ausserFluss[aus].first, k, x0 + xVon(z, sm.ausserFluss[aus].second), y);
            for (size_t i = 0; i < sm.raender.size(); ++i) {
                const Rand& r = sm.raender[i];
                if (r.anfang >= e || r.ende < a || (r.ende == a && a != r.anfang)) continue;
                const int32_t von = std::max(r.anfang, a), bis = std::min(r.ende, e);
                //  Rahmen der Glyphenlaeufe wie beim Markieren: cursorToX springt in RTL mit Harakat ueber Nachbarzeichen.
                float x1 = xVon(z, von), x2 = x1;
                if (bis > von) {
                    const QList<QGlyphRun> laeufe = z.glyphRuns(von, bis - von);
                    float l1 = INFINITY, l2 = -INFINITY;
                    for (const QGlyphRun& g : laeufe) {
                        const QRectF b = g.boundingRect();
                        l1 = std::min(l1, float(b.left()) / sk);
                        l2 = std::max(l2, float(b.right()) / sk);
                    }
                    if (l1 <= l2) { x1 = l1; x2 = l2; }
                }
                if (r.anfang >= a) x1 += r.links;
                if (r.ende <= e) x2 -= r.rechts;
                const float yo = y + gl + r.verschiebung - r.oben, yu = y + gl + r.verschiebung + r.unten;
                const bool anfangDa = r.anfang >= a, endeDa = r.ende <= e, rtl = stil(r.kasten).richtung == Richtung::Rtl;
                m_aus.fragmente.push_back({ Rechteck{ x0 + x1, yo, x2 - x1, yu - yo }, r.kasten, rtl ? endeDa : anfangDa,
                                            rtl ? anfangDa : endeDa });
                Rechteck& q = raender[i];
                if (q.b < 0) { q = Rechteck{ x1, yo, x2 - x1, yu - yo }; continue; }
                const float l2 = std::min(q.x, x1), o2 = std::min(q.y, yo);
                q.b = std::max(q.x + q.b, x2) - l2;
                q.h = std::max(q.y + q.h, yu) - o2;
                q.x = l2;
                q.y = o2;
            }
            grund = y + gl;
            y += zh;
        }
        l.endLayout();
        for (; aus < sm.ausserFluss.size(); ++aus) merke(sm.ausserFluss[aus].first, k, x0, y);
        for (size_t i = 0; i < sm.raender.size(); ++i) {
            const uint32_t c = sm.raender[i].kasten;
            const Rechteck& q = raender[i];
            m_aus.lage[c] = Rechteck{ x0 + q.x, q.y, std::max(0.f, q.b), std::max(0.f, q.h) };
            versetze(c, cw, -1);
        }
        Absatz ab;
        ab.kasten = k;
        ab.zeilenAnfang = ersteZeile;
        ab.zeilenAnzahl = uint32_t(m_aus.zeilen.size()) - ersteZeile;
        ab.laufAnfang = ersterLauf;
        ab.laufAnzahl = uint32_t(m_aus.laeufe.size()) - ersterLauf;
        ab.fragmentAnfang = erstesFragment;
        ab.fragmentAnzahl = uint32_t(m_aus.fragmente.size()) - erstesFragment;
        ab.text = std::move(sm.text);
        ab.stuecke = std::move(sm.stuecke);
        const bool zeilen = ab.zeilenAnzahl > 0;
        m_aus.absaetze.push_back(std::move(ab));
        for (const AtomLage& at : sm.atome)
            if (at.absatzVon < at.absatzBis) m_aus.einbettungen.push_back({ absatz, at.pos, at.absatzVon, at.absatzBis });
        hoehe = y - y0;
        return zeilen;
    }

    // ---- Glyphen ----

    static bool unsichtbar(QChar c) {
        const char16_t u = c.unicode();
        return u == u' ' || u == kNbsp || u == u'\t' || u == kKante || u == kTrenner || u == QChar::LineSeparator
            || (u >= 0x2066 && u <= 0x2069);
    }

    //  Die Laeufe gelten in s-facher Groesse; gezeichnet wird mit derselben Schrift in echter Groesse.
    uint32_t schriftVon(const QRawFont& roh, float sk) {
        for (size_t i = 0; i < m_rohSchriften.size(); ++i)
            if (m_rohSchriften[i].second == sk && m_rohSchriften[i].first == roh) return uint32_t(i);
        QRawFont f = roh;
        f.setPixelSize(roh.pixelSize() / sk);
        SchriftMass ms;
        ms.oben = float(f.ascent());
        ms.unterLage = float(f.underlinePosition());
        ms.unterDicke = float(f.lineThickness());
        //  OS/2 yStrikeoutSize und yStrikeoutPosition in Schrifteinheiten.
        const QByteArray os2 = f.fontTable("OS/2");
        const float je = float(f.pixelSize() / std::max(qreal(1), f.unitsPerEm()));
        if (os2.size() >= 30) {
            const auto be16 = [&](int i) { return int16_t(uint8_t(os2[i]) << 8 | uint8_t(os2[i + 1])); };
            ms.durchDicke = be16(26) * je;
            ms.durchLage = -be16(28) * je;
        }
        if (ms.durchDicke <= 0) { ms.durchDicke = ms.unterDicke; ms.durchLage = -float(f.xHeight()) / 2; }
        m_rohSchriften.emplace_back(roh, sk);
        m_aus.schriften.push_back(std::move(f));
        m_aus.masse.push_back(ms);
        return uint32_t(m_aus.schriften.size() - 1);
    }

    //  Zeichen der Zeile, zwischen dessen Caret-Kanten die Mitte einer Glyphe liegt; sonst das naechste.
    qsizetype zeichenBei(float mitte) const {
        qsizetype beste = 0;
        float abstand = INFINITY;
        for (size_t k = 0; k + 1 < m_caret.size(); ++k) {
            const float a = std::min(m_caret[k], m_caret[k + 1]), b = std::max(m_caret[k], m_caret[k + 1]);
            if (mitte >= a && mitte < b) return qsizetype(k);
            const float d = std::abs(mitte - (a + b) / 2);
            if (d < abstand) { abstand = d; beste = qsizetype(k); }
        }
        return beste;
    }

    //  Laeufe einer Zeile je Textkasten; `grund` ist die Grundlinie relativ zum Block wie Zeile::y.
    void glyphen(const QTextLine& z, const Sammlung& sm, const std::vector<uint32_t>& texte, float x0, float grund,
                 uint32_t zeile) {
        const float sk = sm.s, qtGrund = float(z.y() + z.ascent());
        const QList<QGlyphRun> laeufe = z.glyphRuns(-1, -1, QTextLayout::RetrieveGlyphIndexes
            | QTextLayout::RetrieveGlyphPositions | QTextLayout::RetrieveStringIndexes);
        bool caretDa = false;
        for (const QGlyphRun& g : laeufe) {
            const QList<quint32> id = g.glyphIndexes();
            const QList<QPointF> pos = g.positions();
            const QList<qsizetype> si = g.stringIndexes();
            if (id.isEmpty() || si.size() != id.size() || pos.size() != id.size()) continue;
            const QRawFont roh = g.rawFont();
            m_vorschub.resize(size_t(id.size()));
            roh.advancesForGlyphIndexes(id.constData(), m_vorschub.data(), int(id.size()));
            //  Qt 6.12: in einer RTL-Zeile mit Ersatzschrift tragen alle Laeufe ausser dem ersten nur die Anfangsstelle;
            //  die Glyphen fielen weg oder landeten im falschen Kasten. Dann gilt die Stelle aus den Caret-Lagen.
            const bool neu = laeufe.size() > 1 && si[0] == z.textStart()
                          && std::all_of(si.begin(), si.end(), [&](qsizetype v) { return v == si[0]; });
            if (neu && !caretDa) {
                m_caret.resize(size_t(z.textLength()) + 1);
                for (int k = 0; k <= z.textLength(); ++k) m_caret[size_t(k)] = float(z.cursorToX(z.textStart() + k));
                caretDa = true;
            }
            uint32_t schrift = kKein;
            Lauf* lauf = nullptr;
            qsizetype vorige = -1;
            for (qsizetype i = 0; i < id.size(); ++i) {
                //  Ein Zeichen ohne Vorschub (Haraka) ragt ueber die Caret-Kante; es gehoert zur Glyphe davor.
                qsizetype c = si[i];
                if (neu)
                    c = vorige >= 0 && m_vorschub[size_t(i)].x() == 0
                        ? vorige : z.textStart() + zeichenBei(float(pos[i].x() + m_vorschub[size_t(i)].x() / 2));
                vorige = c;
                if (c < 0 || c >= sm.text.size() || unsichtbar(sm.text[c])) continue;
                const auto it = std::upper_bound(texte.begin(), texte.end(), int32_t(c),
                    [&](int32_t v, uint32_t t) { return v < sm.stuecke[t].anfang; });
                if (it == texte.begin()) continue;
                const Stueck& st = sm.stuecke[*(it - 1)];
                if (c >= st.anfang + st.laenge) continue;
                if (!lauf || lauf->kasten != st.kasten) {
                    if (schrift == kKein) schrift = schriftVon(roh, sk);
                    m_aus.laeufe.push_back(Lauf{ st.kasten, schrift, zeile, uint32_t(m_aus.glyphen.size()), 0,
                                                 INFINITY, -INFINITY, grund + st.versatz });
                    lauf = &m_aus.laeufe.back();
                }
                const float x = x0 + float(pos[i].x()) / sk;
                m_aus.glyphen.push_back(id[i]);
                m_aus.glyphLagen.push_back(x);
                m_aus.glyphLagen.push_back(lauf->grundlinie + (float(pos[i].y()) - qtGrund) / sk);
                m_aus.glyphZeichen.push_back(int32_t(c));
                ++lauf->anzahl;
                lauf->x1 = std::min(lauf->x1, x);
                lauf->x2 = std::max(lauf->x2, x + float(m_vorschub[size_t(i)].x()) / sk);
            }
        }
    }

    //  Listenmarken ausserhalb: Ende an der Inhaltskante des Eintrags, Grundlinie seiner ersten Zeile.
    void marken() {
        std::vector<uint32_t> erste(m_k.kaesten.size(), kKein);
        for (uint32_t i = 0; i < m_aus.zeilen.size(); ++i)
            for (uint32_t b = m_aus.absaetze[m_aus.zeilen[i].absatz].kasten; b != kKein && erste[b] == kKein;
                 b = kasten(b).eltern)
                erste[b] = i;
        std::vector<uint32_t> stapel{ 0 };
        while (!stapel.empty()) {
            const uint32_t c = stapel.back();
            stapel.pop_back();
            for (uint32_t x = kasten(c).erstesKind; x != kKein; x = kasten(x).naechstes) stapel.push_back(x);
            const Kasten& C = kasten(c);
            if (C.art != KArt::Marke || C.eltern == kKein || C.anfang + uint64_t(C.laenge) > m_k.text.size()) continue;
            const uint32_t li = C.eltern;
            const Stil& s = stil(li);
            if (s.sichtbarkeit != Sichtbarkeit::Visible) continue;
            const Schrift& f = schrift(C.stil);
            const float sk = f.eigeneSkala;
            QTextLayout l(QString::fromUtf8(m_k.text.data() + C.anfang, qsizetype(C.laenge)), skaliert(f, sk));
            QTextOption o = textOption(s, f, false, sk);
            o.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
            l.setTextOption(o);
            l.beginLayout();
            QTextLine z = l.createLine();
            if (z.isValid()) z.setLineWidth(1e6);
            l.endLayout();
            if (!z.isValid()) continue;
            const float breite = float(z.naturalTextWidth()) / sk;
            const uint32_t ep = kasten(li).eltern;
            const Kanten e = kanten(li, ep != kKein ? m_aus.lage[ep].b : m_breite);
            const Rechteck& r = m_aus.lage[li];
            const float x = s.richtung == Richtung::Rtl ? r.x + r.b - e.r[1] - e.i[1] : r.x + e.r[3] + e.i[3] - breite;
            const float halb = (zeilenHoehe(s, f) - f.oben - f.unten) / 2;
            const float grund = erste[li] != kKein ? m_aus.zeilen[erste[li]].y + m_aus.zeilen[erste[li]].grundlinie
                                                   : r.y + e.r[0] + e.i[0] + halb + f.oben;
            m_aus.lage[c] = Rechteck{ x, grund - f.oben, breite, f.oben + f.unten };
            Sammlung sm;
            sm.text = l.text();
            sm.s = sk;
            sm.stuecke.push_back({ c, 0, int32_t(sm.text.size()), 0 });
            m_texte.assign(1, 0);
            const size_t vorher = m_aus.laeufe.size();
            glyphen(z, sm, m_texte, x, grund, kKein);
            for (size_t i = vorher; i < m_aus.laeufe.size(); ++i) m_aus.laeufe[i].zeile = kKein;
        }
    }

    //  Erst mit allen Lagen: der Bezug ist der naechste Blockvorfahr, sein Inhaltskasten die Grenze beim Rollen.
    void klebe() {
        for (uint32_t k = 0; k < m_k.kaesten.size(); ++k) {
            //  Text und Marken tragen den Stil ihres Elements, kleben aber nicht selbst.
            const KArt art = kasten(k).art;
            if (anonym(k) || stil(k).position != Lage::Sticky || kasten(k).eltern == kKein
                || (art != KArt::Block && art != KArt::Inline && art != KArt::Atom))
                continue;
            uint32_t cb = kasten(k).eltern;
            while (kasten(cb).art == KArt::Inline && kasten(cb).eltern != kKein) cb = kasten(cb).eltern;
            const uint32_t ueber = kasten(cb).eltern;
            const Kanten e = kanten(cb, ueber != kKein ? m_aus.lage[ueber].b : m_breite);
            const Rechteck& r = m_aus.lage[cb];
            Klebend kl{ k };
            kl.grenzeOben = r.y + e.r[0] + e.i[0];
            kl.grenzeUnten = r.y + r.h - e.r[2] - e.i[2];
            const float cbH = kl.grenzeUnten - kl.grenzeOben;
            const Laenge* v = stil(k).versatz;
            if (v[0].art == LArt::Wert && bestimmt(v[0], cbH)) kl.oben = aufloesen(v[0], cbH);
            if (v[2].art == LArt::Wert && bestimmt(v[2], cbH)) kl.unten = aufloesen(v[2], cbH);
            if (!std::isnan(kl.oben) || !std::isnan(kl.unten)) m_aus.klebend.push_back(kl);
        }
    }

    // ---- Dokumentkoordinaten ----

    void absolutieren() {
        std::vector<uint32_t> stapel{ 0 };
        float unten = 0;
        while (!stapel.empty()) {
            const uint32_t k = stapel.back();
            stapel.pop_back();
            Rechteck& r = m_aus.lage[k];
            if (const uint32_t b = m_bezug[k]; b != kKein) {
                r.x += m_aus.lage[b].x;
                r.y += m_aus.lage[b].y;
            }
            unten = std::max(unten, r.y + r.h);
            for (uint32_t c = kasten(k).erstesKind; c != kKein; c = kasten(c).naechstes) stapel.push_back(c);
        }
        for (Zeile& z : m_aus.zeilen) {
            const Rechteck& r = m_aus.lage[m_aus.absaetze[z.absatz].kasten];
            z.x += r.x;
            z.y += r.y;
        }
        for (const Absatz& a : m_aus.absaetze) {
            const Rechteck& r = m_aus.lage[a.kasten];
            for (uint32_t i = a.laufAnfang; i < a.laufAnfang + a.laufAnzahl; ++i) {
                Lauf& l = m_aus.laeufe[i];
                l.x1 += r.x;
                l.x2 += r.x;
                l.grundlinie += r.y;
                for (uint32_t g = l.anfang; g < l.anfang + l.anzahl; ++g) {
                    m_aus.glyphLagen[2 * g] += r.x;
                    m_aus.glyphLagen[2 * g + 1] += r.y;
                }
            }
            for (uint32_t i = a.fragmentAnfang; i < a.fragmentAnfang + a.fragmentAnzahl; ++i) {
                m_aus.fragmente[i].r.x += r.x;
                m_aus.fragmente[i].r.y += r.y;
            }
        }
        m_aus.hoehe = std::max(m_aus.hoehe, unten);
    }

    const Dokument& m_d;
    const Stile& m_s;
    const Kastenbaum& m_k;
    const Medium& m_m;
    const float m_breite;
    Layout& m_aus;
    float m_ptJePx = 0.75f;
    std::vector<uint32_t> m_schriftJeStil;
    std::deque<Schrift> m_schriften;    // deque: Verweise bleiben beim Anhaengen gueltig
    std::unordered_map<std::string, uint32_t> m_schriftIndex;
    SchriftWahl m_wahl;
    std::vector<float> m_groessen;
    std::vector<uint32_t> m_stapel, m_schieben;
    std::unordered_map<uint32_t, Tabelle> m_tabellen;   // Knoten bleiben beim Einfuegen stehen: Verweise halten
    std::vector<Intr> m_intr;
    std::vector<uint32_t> m_bezug;      // Kasten, gegen dessen Randkasten `lage` waehrend des Legens gilt
    std::vector<Absolut> m_absolut;
    size_t m_bloecke = 0;               // Aufrufe von block(): Budget des Nachlegens in flex/grid
    std::vector<uint32_t> m_texte;
    std::vector<QPointF> m_vorschub;
    std::vector<float> m_caret;
    std::vector<int32_t> m_fugen;       // Fugen des laufenden Textstuecks mit Buchstabenabstand
    std::vector<std::pair<QRawFont, float>> m_rohSchriften;     // je Layout::schriften die Schrift des Legens
    uint32_t m_attrBreite, m_attrHoehe, m_attrViewBox, m_attrTyp, m_attrWert, m_attrSize, m_attrCols, m_attrRows, m_attrSrc;
    uint32_t m_attrColspan, m_attrRowspan;
};

}   // namespace

QStringList SchriftWahl::familien(std::string_view r) {
    std::string schluessel(r);
    if (const auto it = m_familien.find(schluessel); it != m_familien.end()) return it->second;
    QStringList aus;
    //  Ueber den Stilhinweis wie fontconfig: der Name selbst haengt am Plattformthema (Wayland: serif -> Noto Sans).
    const auto gattung = [&](QFont::StyleHint h) {
        QFont f;
        f.setStyleHint(h);
        aus << f.defaultFamily();
    };
    size_t a = 0;
    while (a <= r.size() && !r.empty()) {
        const size_t e = std::min(r.find(',', a), r.size());
        const QString n = QString::fromUtf8(r.substr(a, e - a)).trimmed();
        const QString kl = n.toLower();
        if (kl == u"serif" || kl == u"ui-serif" || kl == u"math") gattung(QFont::Serif);
        else if (kl == u"sans-serif" || kl == u"system-ui" || kl == u"ui-sans-serif") gattung(QFont::SansSerif);
        else if (kl == u"monospace" || kl == u"ui-monospace") gattung(QFont::TypeWriter);
        else if (kl == u"cursive") gattung(QFont::Cursive);
        else if (kl == u"fantasy") gattung(QFont::Fantasy);
        //  Nicht installierte Familien fallen weg; Qt naehme sonst den fontconfig-Ersatz der ersten.
        else if (!n.isEmpty() && QFontDatabase::hasFamily(n)) aus << n;
        a = e + 1;
    }
    //  Fehlt ein Zeichen in allen Familien, versucht Chromium die Standardschrift vor dem Ersatz des Systems.
    gattung(QFont::Serif);
    //  Feste Ersatzschriften je Schriftsystem: Qts eigener Ersatz haengt am Plattformthema (Arabisch unter Wayland
    //  Kufi, offscreen Naskh). Naskh ist die Textschrift fuer Arabisch mit Harakat, CJK JP traegt die Kana.
    static const QStringList ersatz = [] {
        QStringList l;
        for (const char* n : { "Noto Naskh Arabic", "Geeza Pro", "Noto Sans CJK JP", "Hiragino Sans", "Yu Gothic UI",
                               "Meiryo" })
            if (QFontDatabase::hasFamily(QString::fromLatin1(n))) l << QString::fromLatin1(n);
        return l;
    }();
    aus << ersatz;
    aus.removeDuplicates();
    m_familien.emplace(std::move(schluessel), aus);
    return aus;
}

//  Gewicht wie CSS Fonts 4: ueber 500 zuerst schwerer, unter 400 zuerst leichter, dazwischen erst bis 500.
int SchriftWahl::gewicht(const QString& familie, int css, bool kursiv) {
    const QString schluessel = familie + (kursiv ? u'/' : u'|');
    auto it = m_gewichte.find(schluessel);
    if (it == m_gewichte.end()) {
        std::vector<int> g, aufrecht;
        for (const QString& st : QFontDatabase::styles(familie))
            (QFontDatabase::italic(familie, st) == kursiv ? g : aufrecht).push_back(QFontDatabase::weight(familie, st));
        if (g.empty()) g = std::move(aufrecht);
        std::sort(g.begin(), g.end());
        g.erase(std::unique(g.begin(), g.end()), g.end());
        it = m_gewichte.emplace(schluessel, std::move(g)).first;
    }
    const std::vector<int>& g = it->second;
    if (g.empty() || std::binary_search(g.begin(), g.end(), css)) return css;
    const auto schwerer = [&](int bis) {
        for (int w : g) if (w > css && w <= bis) return w;
        return 0;
    };
    const auto leichter = [&] {
        for (auto w = g.rbegin(); w != g.rend(); ++w) if (*w < css) return *w;
        return 0;
    };
    int w = 0;
    if (css >= 400 && css <= 500) w = schwerer(500) ? schwerer(500) : leichter() ? leichter() : schwerer(1000);
    else if (css < 400) w = leichter() ? leichter() : schwerer(1000);
    else w = schwerer(1000) ? schwerer(1000) : leichter();
    return w ? w : css;
}

void SchriftWahl::jeEm(std::string_view familien, int gewicht, bool kursiv, double& ch, double& ex) {
    std::string schluessel(familien);
    schluessel += char(kursiv ? 1 : 0);
    schluessel.append(reinterpret_cast<const char*>(&gewicht), sizeof gewicht);
    auto it = m_jeEm.find(schluessel);
    if (it == m_jeEm.end()) {
        //  Gross und ungehintet gemessen: der Vorschub waechst linear mit der Groesse, Chromium rundet ihn nicht.
        constexpr int kGross = 1000;
        const QStringList fam = this->familien(familien);
        QFont f;
        f.setFamilies(fam);
        f.setWeight(QFont::Weight(std::clamp(this->gewicht(fam.value(0), gewicht, kursiv), 1, 1000)));
        f.setStyle(kursiv ? QFont::StyleItalic : QFont::StyleNormal);
        f.setHintingPreference(QFont::PreferNoHinting);
        f.setPixelSize(kGross);
        const QFontMetricsF fm(f);
        const double c = fm.horizontalAdvance(QChar(u'0')) / kGross, x = fm.xHeight() / kGross;
        it = m_jeEm.emplace(std::move(schluessel), std::pair{ c > 0 ? c : 0.5, x > 0 ? x : 0.5 }).first;
    }
    ch = it->second.first;
    ex = it->second.second;
}

size_t Layout::bytes() const {
    size_t n = lage.capacity() * sizeof(Rechteck) + zeilen.capacity() * sizeof(Zeile) + absaetze.capacity() * sizeof(Absatz);
    for (const Absatz& a : absaetze) n += size_t(a.text.capacity()) * 2 + a.stuecke.capacity() * sizeof(Stueck);
    n += laeufe.capacity() * sizeof(Lauf) + fragmente.capacity() * sizeof(Fragment) + glyphen.capacity() * 4
       + glyphLagen.capacity() * 4 + glyphZeichen.capacity() * 4 + masse.capacity() * sizeof(SchriftMass)
       + rahmenVon.capacity() * 4 + rahmen.capacity() * sizeof(LRahmen) + einbettungen.capacity() * sizeof(Einbettung)
       + klebend.capacity() * sizeof(Klebend);
    return n;
}

Layout lege(const Dokument& d, const Stile& s, const Kastenbaum& k, const Medium& m, float breite) {
    Layout aus;
    Leger(d, s, k, m, breite, aus).lege();
    return aus;
}

}   // namespace mg::html
