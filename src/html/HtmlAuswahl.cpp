#include "html/HtmlAuswahl.h"

#include <QTextBoundaryFinder>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mg::html {

namespace {

struct Glyphe {
    float x1 = 0, x2 = 0;
    int32_t zeichen = 0;
};

//  Nach x; jede reicht bis zur naechsten ihres Laufs, die letzte bis zum Laufende - so decken sie auch Leerzeichen.
//  Ungemalte Laeufe fehlen; x vor der Abbildung.
void zeilenGlyphen(const Layout& l, const Ordnung& o, uint32_t zeile, std::vector<Glyphe>& aus) {
    aus.clear();
    const Absatz& a = l.absaetze[l.zeilen[zeile].absatz];
    const auto nachX = [](const Glyphe& p, const Glyphe& q) { return p.x1 < q.x1; };
    for (uint32_t i = a.laufAnfang; i < a.laufAnfang + a.laufAnzahl && i < l.laeufe.size(); ++i) {
        const Lauf& la = l.laeufe[i];
        if (la.zeile != zeile || o.lauf[i] == kKein || size_t(la.anfang) + la.anzahl > l.glyphZeichen.size()) continue;
        const size_t erst = aus.size();
        for (uint32_t g = la.anfang; g < la.anfang + la.anzahl; ++g)
            aus.push_back({ l.glyphLagen[2 * size_t(g)], la.x2, l.glyphZeichen[g] });
        std::sort(aus.begin() + qsizetype(erst), aus.end(), nachX);
        for (size_t j = erst; j + 1 < aus.size(); ++j) aus[j].x2 = std::max(aus[j].x1, aus[j + 1].x1);
    }
    std::sort(aus.begin(), aus.end(), nachX);
}

//  Die Zeile, wie sie gemalt wird; leer, wenn nicht gemalt oder ganz beschnitten.
QRectF zeileGemalt(const Layout& l, const Ordnung& o, uint32_t zi) {
    const uint32_t s = zi < o.zeile.size() ? o.zeile[zi] : kKein;
    if (s == kKein) return {};
    const Zeile& z = l.zeilen[zi];
    const Abbildung& ab = o.abbildungen[s];
    return ab.t.mapRect(QRectF(z.x, z.y, z.b, z.h)) & ab.clip;
}

bool steuer(char16_t u) { return u == 0x2060 || u == 0x200B || (u >= 0x2066 && u <= 0x2069); }

//  Arabische Vokal- und Lesezeichen samt Tatweel: eine Suche ohne sie findet auch das vokalisierte Wort.
bool harakat(char16_t u) {
    return u == 0x0640 || (((u >= 0x0610 && u <= 0x06FF) || (u >= 0x08D3 && u <= 0x08FF))
                           && QChar::category(char32_t(u)) == QChar::Mark_NonSpacing);
}

//  Klartext von text[von, bis) samt Herkunft je Zeichen.
void saeubere(const QString& text, int32_t von, int32_t bis, QString& aus, std::vector<int32_t>* herkunft,
              bool ohneHarakat = false) {
    von = std::clamp(von, 0, int32_t(text.size()));
    bis = std::clamp(bis, von, int32_t(text.size()));
    for (int32_t i = von; i < bis; ++i) {
        const char16_t u = text[i].unicode();
        if (steuer(u) || (ohneHarakat && harakat(u))) continue;
        aus.append(u == QChar::LineSeparator ? QChar(u'\n') : u == 0x00A0 ? QChar(u' ') : QChar(u));
        if (herkunft) herkunft->push_back(i);
    }
}

bool wortZeichen(const QString& s, qsizetype i) {
    return i >= 0 && i < s.size() && (s[i].isLetterOrNumber() || s[i].isMark() || s[i] == u'_');
}

//  Der Abschnitt, in dem die Einfuegemarke steht; kKein ausserhalb.
uint32_t abschnitt(const Ordnung& o, const Stelle& s) {
    if (size_t(s.absatz) + 1 >= o.erster.size()) return kKein;
    uint32_t j = o.erster[s.absatz];
    const uint32_t ende = o.erster[s.absatz + 1];
    if (j == ende) return kKein;
    while (j + 1 < ende && o.folge[o.abschnitte[j + 1]].von <= s.zeichen) ++j;
    return o.abschnitte[j];
}

//  Abschnitte [r0, r1] mit den Grenzen des Bereichs; liefert false, wenn der Bereich nicht in der Ordnung liegt.
template<typename F>
bool jeAbschnitt(const Ordnung& o, const Bereich& b, F f) {
    if (!b.von.gueltig() || !b.bis.gueltig()) return false;
    const uint32_t r0 = abschnitt(o, b.von), r1 = abschnitt(o, b.bis);
    if (r0 == kKein || r1 == kKein) return false;
    for (uint32_t r = r0; r <= r1; ++r) {
        const Abschnitt& a = o.folge[r];
        f(a, r == r0 ? std::max(a.von, b.von.zeichen) : a.von, r == r1 ? std::min(a.bis, b.bis.zeichen) : a.bis);
    }
    return true;
}

}   // namespace

Ordnung ordne(const Layout& l, const Anzeigeliste* a, const Kastenbaum* k) {
    Ordnung o;
    const uint32_t n = uint32_t(l.absaetze.size());
    std::vector<uint32_t> sortiert(n);
    std::iota(sortiert.begin(), sortiert.end(), 0u);
    std::stable_sort(sortiert.begin(), sortiert.end(),
                     [&](uint32_t x, uint32_t y) { return l.absaetze[x].kasten < l.absaetze[y].kasten; });
    //  Innere Einbettungen stehen vorn und greifen zuerst: so traegt jeder Absatz seinen naechsten Wirt.
    const std::vector<Einbettung>& eb = l.einbettungen;
    std::vector<uint32_t> direkt(n, kKein), eErst(size_t(n) + 1, 0), kErst(eb.size() + 1, 0), kinder;
    for (uint32_t e = 0; e < eb.size(); ++e) {
        if (eb[e].wirt >= n) continue;
        ++eErst[eb[e].wirt + 1];
        for (uint32_t q = eb[e].von; q < eb[e].bis && q < eb[e].wirt; ++q)
            if (direkt[q] == kKein) { direkt[q] = e; ++kErst[e + 1]; }
    }
    std::partial_sum(eErst.begin(), eErst.end(), eErst.begin());
    std::partial_sum(kErst.begin(), kErst.end(), kErst.begin());
    kinder.resize(kErst.back());
    {
        std::vector<uint32_t> fuell(kErst.begin(), kErst.end() - 1);
        for (const uint32_t q : sortiert)
            if (direkt[q] != kKein) kinder[fuell[direkt[q]]++] = q;
    }
    //  Wirt bis hinter die Kante, dann die Absaetze des Inline-Blocks, dann der Rest; mit Stapel statt Rekursion.
    struct Rahmen { uint32_t absatz, e; int32_t von; bool anschluss; uint32_t kind; };
    std::vector<Rahmen> stapel;
    o.folge.reserve(size_t(n) + eb.size());
    for (const uint32_t start : sortiert) {
        if (direkt[start] != kKein) continue;
        stapel.push_back({ start, eErst[start], 0, false, kKein });
        while (!stapel.empty()) {
            Rahmen& r = stapel.back();
            const int32_t laenge = int32_t(l.absaetze[r.absatz].text.size());
            if (r.kind == kKein) {
                if (r.e < eErst[r.absatz + 1]) {
                    const int32_t bis = std::clamp(eb[r.e].stelle + 1, r.von, laenge);
                    o.folge.push_back({ r.absatz, r.von, bis, r.anschluss });
                    r.von = bis;
                    r.anschluss = true;
                    r.kind = kErst[r.e];
                } else {
                    o.folge.push_back({ r.absatz, r.von, laenge, r.anschluss });
                    stapel.pop_back();
                }
            } else if (r.kind < kErst[r.e + 1]) {
                const uint32_t q = kinder[r.kind];
                const bool erstes = r.kind == kErst[r.e];
                ++r.kind;
                stapel.push_back({ q, eErst[q], 0, erstes, kKein });
            } else {
                ++r.e;
                r.kind = kKein;
            }
        }
    }
    o.erster.assign(size_t(n) + 1, 0);
    for (const Abschnitt& a : o.folge) ++o.erster[a.absatz + 1];
    std::partial_sum(o.erster.begin(), o.erster.end(), o.erster.begin());
    o.abschnitte.resize(o.folge.size());
    {
        std::vector<uint32_t> fuell(o.erster.begin(), o.erster.end() - 1);
        for (uint32_t i = 0; i < o.folge.size(); ++i) o.abschnitte[fuell[o.folge[i].absatz]++] = i;
    }

    //  Die Zellen braucht nur klartext; eine Beschriftung (caption) gehoert keiner Zelle, auch nicht der aeusseren.
    if (k) {
        const std::vector<Kasten>& kk = k->kaesten;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t c = l.absaetze[i].kasten;
            while (c < kk.size() && kk[c].rolle != TZelle && kk[c].rolle != TTitel) c = kk[c].eltern;
            if (c >= kk.size() || kk[c].rolle != TZelle) continue;
            const uint32_t z = kk[c].eltern;
            if (z >= kk.size() || kk[z].rolle != TZeile) continue;
            uint32_t t = z;
            while (t < kk.size() && kk[t].rolle != TTabelle) t = kk[t].eltern;
            uint32_t spalte = 0;
            for (uint32_t g = kk[z].erstesKind; g != c && g < kk.size(); g = kk[g].naechstes) spalte += kk[g].rolle == TZelle;
            if (o.zellen.empty()) o.zellen.resize(n);
            o.zellen[i] = { z, t, spalte };
        }
    }

    const QRectF ueberall(-1e9, -1e9, 2e9, 2e9);
    o.abbildungen.push_back({ QTransform(), ueberall });
    o.lauf.assign(l.laeufe.size(), a ? kKein : 0u);
    if (a) {
        //  Derselbe Stapel wie in male(): QPainter::setTransform(t, true) setzt t vor die bisherige Abbildung.
        struct Zustand { QTransform t; QRectF clip; uint32_t index; };
        std::vector<Zustand> stapel{ { QTransform(), ueberall, 0 } };
        for (const Befehl& b : a->befehle) {
            switch (b.art) {
            case BArt::ClipAn:
            case BArt::ClipText: {
                Zustand z = stapel.back();
                z.clip &= z.t.mapRect(QRectF(b.r.x, b.r.y, b.r.b, b.r.h));
                z.index = kKein;
                stapel.push_back(z);
                break;
            }
            case BArt::TransformAn: {
                Zustand z = stapel.back();
                if (b.daten < a->transformationen.size()) z.t = a->transformationen[b.daten] * z.t;
                z.index = kKein;
                stapel.push_back(z);
                break;
            }
            case BArt::ClipAus:
            case BArt::TransformAus:
                if (stapel.size() > 1) stapel.pop_back();
                break;
            case BArt::Text:
                if (b.daten < o.lauf.size()) {
                    Zustand& z = stapel.back();
                    if (z.index == kKein) {
                        z.index = uint32_t(o.abbildungen.size());
                        o.abbildungen.push_back({ z.t, z.clip });
                    }
                    o.lauf[b.daten] = z.index;
                }
                break;
            default:
                break;
            }
        }
    }
    //  Zeilen ohne Glyphen (leer) bleiben trefferbar; mit Glyphen, aber keiner gemalt, nicht.
    o.zeile.assign(l.zeilen.size(), 0u);
    std::vector<uint8_t> hatLauf(l.zeilen.size(), 0);
    for (uint32_t i = 0; i < l.laeufe.size(); ++i) {
        const uint32_t z = l.laeufe[i].zeile;
        if (z >= l.zeilen.size()) continue;
        if (!hatLauf[z]) { hatLauf[z] = 1; o.zeile[z] = kKein; }
        if (o.zeile[z] == kKein && o.lauf[i] != kKein) o.zeile[z] = o.lauf[i];
    }
    return o;
}

bool vor(const Ordnung& o, const Stelle& a, const Stelle& b) {
    if (a.absatz == b.absatz) return a.zeichen < b.zeichen;
    const uint32_t x = abschnitt(o, a), y = abschnitt(o, b);
    if (x == kKein || y == kKein) return a.absatz < b.absatz;
    return x < y;
}

Bereich bereich(const Ordnung& o, const Stelle& a, const Stelle& b) {
    return vor(o, b, a) ? Bereich{ b, a } : Bereich{ a, b };
}

Stelle treffe(const Layout& l, const Ordnung& o, float x, float y, bool glyphe) {
    uint32_t beste = kKein;
    qreal abstand = INFINITY;
    for (uint32_t i = 0; i < l.zeilen.size(); ++i) {
        const QRectF z = zeileGemalt(l, o, i);
        if (z.isEmpty()) continue;
        const qreal dy = y < z.top() ? z.top() - y : y >= z.bottom() ? y - z.bottom() : 0;
        const qreal dx = x < z.left() ? z.left() - x : x > z.right() ? x - z.right() : 0;
        if (dx + dy < abstand) { abstand = dx + dy; beste = i; }
    }
    if (beste == kKein || l.zeilen[beste].absatz >= l.absaetze.size()) return {};
    const Zeile& z = l.zeilen[beste];
    x = float(o.abbildungen[o.zeile[beste]].t.inverted().map(QPointF(x, y)).x());
    std::vector<Glyphe> g;
    zeilenGlyphen(l, o, beste, g);
    if (g.empty()) return { z.absatz, z.anfang };
    const size_t n = g.size();
    size_t j = 0;
    while (j + 1 < n && x >= g[j].x2) ++j;
    if (glyphe) return { z.absatz, g[j].zeichen };
    //  Laufrichtung aus den Nachbarn: sinkt die Stelle nach rechts, ist es RTL.
    const bool rtl = (j + 1 < n && g[j + 1].zeichen < g[j].zeichen) || (j > 0 && g[j - 1].zeichen > g[j].zeichen);
    const bool rechts = x >= (g[j].x1 + g[j].x2) / 2;
    if (rechts == rtl) return { z.absatz, g[j].zeichen };
    int32_t danach = z.anfang + z.laenge;
    for (const Glyphe& h : g)
        if (h.zeichen > g[j].zeichen) danach = std::min(danach, h.zeichen);
    //  Vor einem Inline-Block endet die Stelle an seiner Kante, sonst laege sie schon hinter seinem Inhalt.
    if (size_t(z.absatz) + 1 < o.erster.size())
        for (uint32_t k = o.erster[z.absatz] + 1; k < o.erster[z.absatz + 1]; ++k) {
            const int32_t kante = o.folge[o.abschnitte[k]].von - 1;
            if (kante > g[j].zeichen && kante < danach) danach = kante;
        }
    return { z.absatz, std::max(danach, g[j].zeichen + 1) };
}

Bereich wortUm(const Layout& l, const Stelle& s) {
    if (!s.gueltig() || s.absatz >= l.absaetze.size()) return {};
    const QString& t = l.absaetze[s.absatz].text;
    const int32_t p = std::clamp(s.zeichen, 0, int32_t(t.size()));
    QTextBoundaryFinder f(QTextBoundaryFinder::Word, t);
    f.setPosition(p);
    const bool anfang = f.isAtBoundary() && (f.boundaryReasons() & QTextBoundaryFinder::StartOfItem);
    const qsizetype von = anfang ? p : f.toPreviousBoundary();
    f.setPosition(p);
    const qsizetype bis = f.toNextBoundary();
    return { { s.absatz, int32_t(std::max<qsizetype>(von, 0)) },
             { s.absatz, int32_t(bis < 0 ? t.size() : bis) } };
}

Bereich absatzUm(const Layout& l, const Stelle& s) {
    if (!s.gueltig() || s.absatz >= l.absaetze.size()) return {};
    return { { s.absatz, 0 }, { s.absatz, int32_t(l.absaetze[s.absatz].text.size()) } };
}

Bereich alles(const Layout&, const Ordnung& o) {
    if (o.folge.empty()) return {};
    return { { o.folge.front().absatz, o.folge.front().von }, { o.folge.back().absatz, o.folge.back().bis } };
}

void flaechen(const Layout& l, const Ordnung& o, const Bereich& b, float y0, float y1, std::vector<Rechteck>& aus) {
    std::vector<Glyphe> g;
    jeAbschnitt(o, b, [&](const Abschnitt& teil, int32_t von, int32_t bis) {
        if (von >= bis) return;
        const Absatz& a = l.absaetze[teil.absatz];
        for (uint32_t zi = a.zeilenAnfang; zi < a.zeilenAnfang + a.zeilenAnzahl && zi < l.zeilen.size(); ++zi) {
            const Zeile& z = l.zeilen[zi];
            if (z.anfang + z.laenge <= von || z.anfang >= bis) continue;
            const QRectF gemalt = zeileGemalt(l, o, zi);
            if (gemalt.isEmpty() || gemalt.bottom() <= y0 || gemalt.top() >= y1) continue;
            const Abbildung& ab = o.abbildungen[o.zeile[zi]];
            zeilenGlyphen(l, o, zi, g);
            for (size_t j = 0; j < g.size();) {
                if (g[j].zeichen < von || g[j].zeichen >= bis) { ++j; continue; }
                const float x1 = g[j].x1;
                while (j < g.size() && g[j].zeichen >= von && g[j].zeichen < bis) ++j;
                const QRectF r = ab.t.mapRect(QRectF(x1, z.y, g[j - 1].x2 - x1, z.h)) & ab.clip;
                if (!r.isEmpty()) aus.push_back({ float(r.x()), float(r.y()), float(r.width()), float(r.height()) });
            }
        }
    });
}

QString klartext(const Layout& l, const Ordnung& o, const Bereich& b) {
    QString aus;
    //  Der Umbruch eines leeren Abschnitts wartet auf den naechsten mit Text.
    bool umbruch = false;
    uint32_t zuletzt = kKein;
    const auto zelle = [&](uint32_t absatz) {
        return absatz < o.zellen.size() && o.zellen[absatz].zeile != kKein ? &o.zellen[absatz] : nullptr;
    };
    jeAbschnitt(o, b, [&](const Abschnitt& teil, int32_t von, int32_t bis) {
        umbruch |= !teil.anschluss;
        const qsizetype vorher = aus.size();
        if (umbruch && !aus.isEmpty()) {
            //  Wie die Browser: Zellen einer Zeile mit Tabulator, leere mitgezaehlt, damit die Spalten beim Einfuegen stehen.
            const Zelle *p = zelle(zuletzt), *q = zelle(teil.absatz);
            if (p && q && p->zeile == q->zeile && q->spalte > p->spalte) {
                aus.append(QString(qsizetype(q->spalte - p->spalte), u'\t'));
            } else {
                aus.append(u'\n');
                if (p && q && p->tabelle == q->tabelle && p->zeile != q->zeile) aus.append(QString(qsizetype(q->spalte), u'\t'));
            }
        }
        const qsizetype mitTrenner = aus.size();
        saeubere(l.absaetze[teil.absatz].text, von, bis, aus, nullptr);
        if (aus.size() == mitTrenner) aus.truncate(vorher);
        else { umbruch = false; zuletzt = teil.absatz; }
    });
    return aus;
}

std::vector<Bereich> suche(const Layout& l, const Ordnung& o, const QString& muster, bool gross, bool woerter,
                           size_t grenze, bool* ueberlauf, const std::atomic<bool>* abbruch) {
    std::vector<Bereich> treffer;
    if (ueberlauf) *ueberlauf = false;
    if (muster.isEmpty()) return treffer;
    const Qt::CaseSensitivity cs = gross ? Qt::CaseSensitive : Qt::CaseInsensitive;
    //  Mit Harakat im Muster wird genau gesucht, sonst ohne sie im Text.
    const bool genau = std::any_of(muster.cbegin(), muster.cend(), [](QChar c) { return harakat(c.unicode()); });
    QString klar;
    std::vector<int32_t> herkunft;
    for (const Abschnitt& teil : o.folge) {
        if (abbruch && abbruch->load(std::memory_order_relaxed)) break;
        const uint32_t ai = teil.absatz;
        const QString& t = l.absaetze[ai].text;
        klar.clear();
        herkunft.clear();
        saeubere(t, teil.von, teil.bis, klar, &herkunft, !genau);
        qsizetype ab = 0;
        for (qsizetype i; (i = klar.indexOf(muster, ab, cs)) >= 0;) {
            const qsizetype e = i + muster.size();
            if (woerter && (wortZeichen(klar, i - 1) || wortZeichen(klar, e))) { ab = i + 1; continue; }
            if (treffer.size() >= grenze) {
                if (ueberlauf) *ueberlauf = true;
                return treffer;
            }
            Bereich b{ { ai, herkunft[size_t(i)] }, { ai, herkunft[size_t(e - 1)] + 1 } };
            while (!genau && b.bis.zeichen < t.size() && harakat(t[b.bis.zeichen].unicode())) ++b.bis.zeichen;
            ab = e;
            if (std::isnan(zeilenOben(l, o, b.von))) continue;     // nicht gemalt: dorthin fuehrt kein Sprung
            treffer.push_back(b);
        }
    }
    return treffer;
}

float zeilenOben(const Layout& l, const Ordnung& o, const Stelle& s) {
    if (!s.gueltig() || s.absatz >= l.absaetze.size()) return NAN;
    const Absatz& a = l.absaetze[s.absatz];
    uint32_t zeile = kKein;
    for (uint32_t zi = a.zeilenAnfang; zi < a.zeilenAnfang + a.zeilenAnzahl && zi < l.zeilen.size(); ++zi) {
        zeile = zi;
        if (s.zeichen < l.zeilen[zi].anfang + l.zeilen[zi].laenge) break;
    }
    if (zeile == kKein) return NAN;
    const QRectF r = zeileGemalt(l, o, zeile);
    return r.isEmpty() ? NAN : float(r.top());
}

Stelle obersteZeile(const Layout& l, const Ordnung& o, float y0, float y1) {
    uint32_t beste = kKein, ueber = kKein;
    QRectF b, u;
    for (uint32_t zi = 0; zi < l.zeilen.size(); ++zi) {
        const QRectF r = zeileGemalt(l, o, zi);
        if (r.isEmpty() || r.top() >= y1 || r.bottom() <= y0) continue;
        if (r.top() >= y0) {
            if (beste == kKein || r.top() < b.top() || (r.top() == b.top() && r.left() < b.left())) { beste = zi; b = r; }
        }
        else if (ueber == kKein || r.top() > u.top()) { ueber = zi; u = r; }
    }
    if (beste == kKein) beste = ueber;
    if (beste == kKein) return {};
    return Stelle{ l.zeilen[beste].absatz, l.zeilen[beste].anfang };
}

}   // namespace mg::html
