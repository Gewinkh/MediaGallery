#include "html/HtmlPdf.h"

#include "core/PdfGlyphRuns.h"
#include "core/PdfVorschau.h"
#include "html/HtmlPaint.h"
#include "html/HtmlStyle.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

namespace mg::html {

namespace {

constexpr int   kAufloesung = 96;       // 1 CSS-px = 1 px im PDF, wie Browser drucken
constexpr qreal kBandMm = 8.0;          // oben und unten, darin die Seitenzahl
constexpr qreal kFussPt = 8.0;

//  CSS-Laenge in mm; Prozent von `bezug`. Ohne Einheit nur 0.
bool inMm(std::string_view t, double bezug, double& aus) {
    t = ohneRand(t);
    double zahl = 0;
    const auto [ende, fehler] = std::from_chars(t.data(), t.data() + t.size(), zahl);
    if (fehler != std::errc() || !std::isfinite(zahl)) return false;
    std::string einheit(ende, t.data() + t.size());
    for (char& c : einheit) c = char(std::tolower(uint8_t(c)));
    static const std::pair<std::string_view, double> tab[] = {
        { "mm", 1 }, { "cm", 10 }, { "in", 25.4 }, { "pt", 25.4 / 72 }, { "pc", 25.4 / 6 }, { "px", 25.4 / 96 }, { "q", 0.25 },
    };
    if (einheit == "%") { aus = bezug * zahl / 100; return true; }
    if (einheit.empty()) { aus = 0; return zahl == 0; }
    for (const auto& [e, f] : tab)
        if (einheit == e) { aus = zahl * f; return true; }
    return false;
}

//  @page des Druckstils: Blattgroesse und Lage stechen den Dialog wie in Chromium; Raender roh, leer = Vorgabe.
struct Seitenvorgabe {
    std::optional<QPageSize> groesse;
    std::optional<QPageLayout::Orientation> lage;
    std::string rand[4];
};

void groesseLesen(std::string_view t, Seitenvorgabe& v) {
    static const std::pair<std::string_view, QPageSize::PageSizeId> namen[] = {
        { "a3", QPageSize::A3 }, { "a4", QPageSize::A4 }, { "a5", QPageSize::A5 }, { "b4", QPageSize::B4 },
        { "b5", QPageSize::B5 }, { "jis-b4", QPageSize::JisB4 }, { "jis-b5", QPageSize::JisB5 },
        { "letter", QPageSize::Letter }, { "legal", QPageSize::Legal }, { "ledger", QPageSize::Ledger },
    };
    std::optional<QPageSize> groesse;
    std::optional<QPageLayout::Orientation> lage;
    std::vector<double> laengen;
    for (size_t a = 0; a < t.size();) {
        while (a < t.size() && std::isspace(uint8_t(t[a]))) ++a;
        size_t e = a;
        while (e < t.size() && !std::isspace(uint8_t(t[e]))) ++e;
        if (e == a) break;
        std::string w(t.substr(a, e - a));
        a = e;
        for (char& c : w) c = char(std::tolower(uint8_t(c)));
        double mm = 0;
        if (w == "auto") return;
        if (w == "portrait" || w == "landscape") {
            if (lage) return;
            lage = w == "portrait" ? QPageLayout::Portrait : QPageLayout::Landscape;
        } else if (const auto* n = std::find_if(std::begin(namen), std::end(namen), [&](const auto& x) { return x.first == w; });
                   n != std::end(namen)) {
            if (groesse) return;
            groesse = QPageSize(n->second);
        } else if (inMm(w, 0, mm) && mm > 0 && w.back() != '%') {
            laengen.push_back(mm);
        } else {
            return;
        }
    }
    //  Laengen schliessen Name und Lage aus; eine Laenge ist ein Quadrat.
    if (!laengen.empty()) {
        if (laengen.size() > 2 || groesse || lage) return;
        v.groesse = QPageSize(QSizeF(laengen[0], laengen.back()), QPageSize::Millimeter, {}, QPageSize::ExactMatch);
        v.lage = QPageLayout::Portrait;
        return;
    }
    if (groesse) v.groesse = groesse;
    if (groesse || lage) v.lage = lage ? *lage : QPageLayout::Portrait;
}

Seitenvorgabe seitenVorgabe(const Stilblatt& b, const Medium& m) {
    Seitenvorgabe v;
    for (const SeitenRegel& r : b.seiten) {
        if (!b.gilt(r.medien, m)) continue;
        if (!r.groesse.empty()) groesseLesen(r.groesse, v);
        for (int i = 0; i < 4; ++i)
            if (!r.rand[i].empty()) v.rand[i] = r.rand[i];
    }
    return v;
}


}   // namespace

VerweisZiel verweisZiel(const Dokument& d, uint32_t verweis, const QString& quelle) {
    const std::string_view href = d.attribut(verweis, d.nameSuchen("href"));
    const QString roh = QString::fromUtf8(href.data(), qsizetype(href.size())).trimmed();
    const QUrl url = QUrl::fromLocalFile(quelle).resolved(QUrl(roh));
    const QString schema = url.scheme().toLower();
    if (schema == u"http" || schema == u"https" || schema == u"mailto" || schema == u"ftp") return { url };
    //  Ohne Quelle (Pruefstand, Vorschau aus dem Speicher) zaehlt nur das reine `#ziel` als dieselbe Datei.
    if (!roh.startsWith(u'#')) {
        if (!url.isLocalFile()) return {};
        const QString pfad = QDir::cleanPath(url.toLocalFile());
        if (pfad != QDir::cleanPath(quelle)) return { {}, kKein, QFileInfo(pfad).isFile() ? pfad : QString() };
    }
    const QByteArray fragment = url.fragment(QUrl::FullyDecoded).toUtf8();
    return { {}, sprungZiel(d, std::string_view(fragment.constData(), size_t(fragment.size()))) };
}

std::vector<float> seitenSchnitte(const Dokument& d, const Layout& l, const Kastenbaum& k, const Stile& s,
                                  float seitenHoehe, float ersteMehr, std::vector<SeitenKopf>* koepfeAus) {
    const float ende = std::max(l.hoehe, 1.f);
    std::vector<float> aus{ 0.f };
    if (koepfeAus) koepfeAus->clear();
    if (!(seitenHoehe > 1)) {
        aus.push_back(ende);
        if (koepfeAus) koepfeAus->push_back({});
        return aus;
    }
    const uint32_t n = uint32_t(std::min(k.kaesten.size(), l.lage.size()));
    //  0 offen, 1 fest, 2 nicht fest; Eltern stehen nicht immer vor den Kindern, darum gemerkt statt in einem Lauf.
    std::vector<uint8_t> festVon(n, 0);
    const auto fest = [&](uint32_t c) {
        uint32_t x = c;
        while (x < n && !festVon[x] && s.stile[k.kaesten[x].stil].position != Lage::Fixed) x = k.kaesten[x].eltern;
        const uint8_t w = x >= n ? 2 : festVon[x] ? festVon[x] : 1;
        for (uint32_t y = c; y != x && y < n; y = k.kaesten[y].eltern) festVon[y] = w;
        return w == 1;
    };
    struct Spanne { float oben, unten; };
    const auto passt = [&](Spanne x) { return x.unten > x.oben && x.unten - x.oben <= seitenHoehe; };
    //  Inhalt = Zeilen und ersetzte Elemente; je Kasten sein erster und letzter, fuer die Kanten unten.
    std::vector<Spanne> inhalt;
    inhalt.reserve(l.zeilen.size());
    constexpr float kWeit = 1e30f;
    std::vector<Spanne> erster(n, { kWeit, kWeit }), letzter(n, { -kWeit, -kWeit });
    const auto melde = [&](uint32_t c, Spanne x) {
        inhalt.push_back(x);
        for (uint32_t y = c; y < n; y = k.kaesten[y].eltern) {
            if (x.oben < erster[y].oben) erster[y] = x;
            if (x.unten > letzter[y].unten) letzter[y] = x;
        }
    };
    for (const Zeile& z : l.zeilen)
        if (z.h > 0 && z.h <= seitenHoehe && z.absatz < l.absaetze.size() && l.absaetze[z.absatz].kasten < n
            && !fest(l.absaetze[z.absatz].kasten))
            melde(l.absaetze[z.absatz].kasten, { z.y, z.y + z.h });
    for (uint32_t i = 0; i < n; ++i) {
        const Rechteck& r = l.lage[i];
        if ((k.kaesten[i].flags & KErsetzt) && r.h > 0 && r.h <= seitenHoehe && !fest(i)) melde(i, { r.y, r.y + r.h });
    }
    const auto nachOben = [](const Spanne& a, const Spanne& b) { return a.oben < b.oben; };
    std::sort(inhalt.begin(), inhalt.end(), nachOben);

    //  Wie Chromium kein Schnitt zwischen Kastenkante und erster bzw. letzter Zeile: sonst stand am Seitenende nur der
    //  obere Rand einer Karte. Ganz bleiben break-inside: avoid, Tabellenzeilen, leere Kaesten und Karten (Rahmen oder
    //  Hintergrund) bis zu einer Viertelseite - ihr avoid steht oft nur unter @media print, das Original nicht liest.
    //  Ueberschriften und Tabellenkoepfe gehen mit dem folgenden Inhalt (wie break-after: avoid).
    const auto karte = [](const Stil& st) {
        bool rahmen = false;
        for (int i = 0; i < 4; ++i)
            rahmen |= st.rahmenBreite[i] > 0 && st.rahmenStil[i] != RahmenStil::None && st.rahmenStil[i] != RahmenStil::Hidden;
        return rahmen || (st.hintergrund >> 24) != 0 || st.hintergrundBild != 0;
    };
    std::vector<Spanne> alle = inhalt;
    std::vector<float> zwang;
    struct Kopf { float anfang, oben; uint32_t stufe; };     // anfang: samt Vorzeile (Kapitel-Etikett)
    std::vector<Kopf> koepfe;
    //  thead wiederholt sich wie im Browser auf jeder Folgeseite seiner Tabelle, bis zu einer Viertelseite hoch.
    struct TabKopf { float kopfOben, kopfUnten, tabUnten; };
    std::vector<TabKopf> tabKoepfe;
    for (uint32_t i = 0; i < n; ++i) {
        const Kasten& c = k.kaesten[i];
        const Rechteck& r = l.lage[i];
        if (c.art != KArt::Block || r.h <= 0 || fest(i)) continue;
        const Stil& st = s.stile[c.stil];
        const Spanne ganz{ r.y, r.y + r.h };
        if (!(c.flags & KAnonym)) {
            if (st.umbruchVor == 2) zwang.push_back(ganz.oben);
            if (st.umbruchNach == 2) zwang.push_back(ganz.unten);
        }
        const bool klein = r.h <= seitenHoehe / 4 && karte(st);
        //  Auch eine ganz gehaltene Karte kann Ueberschrift sein (Kapitel mit Linie darunter): die Regeln unten gelten weiter.
        const bool ganzFest = (st.umbruchInnen || klein || c.rolle == TZeile || erster[i].oben == kWeit) && passt(ganz);
        if (ganzFest) alle.push_back(ganz);
        else if (erster[i].oben == kWeit) continue;
        if (!ganzFest) {
            if (const Spanne x{ r.y, erster[i].unten }; passt(x)) alle.push_back(x);
            if (const Spanne x{ letzter[i].oben, ganz.unten }; passt(x)) alle.push_back(x);
        }
        if (c.rolle == TTabelle && koepfeAus)
            for (uint32_t g = c.erstesKind; g < n; g = k.kaesten[g].naechstes)
                if (const Rechteck& kr = l.lage[g]; k.kaesten[g].rolle == TKopf && kr.h > 0 && kr.h <= seitenHoehe / 4) {
                    tabKoepfe.push_back({ kr.y, kr.y + kr.h, r.y + r.h });
                    break;
                }
        if (c.rolle == TTabelle && !ganzFest) {
            //  Erste Zeile mit der zweiten: oft ist sie der Kopf, auch ohne thead.
            uint32_t zeilen[2] = { kKein, kKein }, z = 0;
            for (uint32_t g = c.erstesKind; g < n && z < 2; g = k.kaesten[g].naechstes) {
                if (k.kaesten[g].rolle == TZeile) zeilen[z++] = g;
                else if (istGruppe(k.kaesten[g].rolle))
                    for (uint32_t x = k.kaesten[g].erstesKind; x < n && z < 2; x = k.kaesten[x].naechstes)
                        if (k.kaesten[x].rolle == TZeile) zeilen[z++] = x;
            }
            if (z == 2) {
                const Rechteck& a = l.lage[zeilen[0]];
                const Rechteck& b = l.lage[zeilen[1]];
                if (passt({ a.y, b.y + b.h })) alle.push_back({ a.y, b.y + b.h });
            }
        }
        if (c.flags & KAnonym) continue;
        const uint32_t name = c.knoten < d.knoten.size() ? d.knoten[c.knoten].name : kKein;
        const bool ueberschrift = name >= NH1 && name <= NH6;
        if (ueberschrift || c.rolle == TKopf || st.umbruchNach == 1) {
            //  Ein einzeiliges, kleineres Geschwister direkt davor (Kapitel-Etikett) geht mit, sonst stuende es allein unten.
            float anfang = r.y;
            uint32_t vor = kKein;
            if (ueberschrift && c.eltern < n)
                for (uint32_t g = k.kaesten[c.eltern].erstesKind; g < n && g != i; g = k.kaesten[g].naechstes)
                    if (k.kaesten[g].art == KArt::Block && l.lage[g].h > 0) vor = g;
            if (vor != kKein && erster[vor].oben == letzter[vor].oben && erster[vor].oben != kWeit
                && erster[i].oben != kWeit
                && erster[vor].unten - erster[vor].oben < erster[i].unten - erster[i].oben)
                anfang = l.lage[vor].y;
            const auto folgt = std::lower_bound(inhalt.begin(), inhalt.end(), Spanne{ ganz.unten - 0.5f, 0 }, nachOben);
            if (folgt != inhalt.end() && passt({ anfang, folgt->unten })) alle.push_back({ anfang, folgt->unten });
            if (ueberschrift && passt(ganz)) koepfe.push_back({ anfang, r.y, name });
        }
        if (st.umbruchVor == 1) {
            const auto davor = std::lower_bound(inhalt.begin(), inhalt.end(), Spanne{ r.y, 0 }, nachOben);
            if (davor != inhalt.begin() && passt({ std::prev(davor)->oben, erster[i].unten }))
                alle.push_back({ std::prev(davor)->oben, erster[i].unten });
        }
    }
    std::sort(alle.begin(), alle.end(), nachOben);
    std::sort(zwang.begin(), zwang.end());
    std::sort(koepfe.begin(), koepfe.end(), [](const Kopf& a, const Kopf& b) { return a.oben < b.oben; });
    //  Ohne lange Spannen (break-inside: avoid ueber halbe Seiten): fuer Seiten, die sonst fast leer blieben.
    std::vector<Spanne> fein;
    for (const Spanne& x : alle) if (x.unten - x.oben <= seitenHoehe / 2) fein.push_back(x);

    //  Hochschieben, bis keine Spanne mehr ueber den Schnitt reicht; nur Spannen, die auf der Seite beginnen, zaehlen.
    const auto schiebe = [&](const std::vector<Spanne>& sp, float anfang, float schnitt) {
        const auto ab = std::upper_bound(sp.begin(), sp.end(), Spanne{ anfang, 0 }, nachOben);
        for (bool weiter = true; weiter;) {
            weiter = false;
            for (auto it = ab; it != sp.end() && it->oben < schnitt; ++it)
                if (it->unten > schnitt) { schnitt = it->oben; weiter = true; }
        }
        return schnitt;
    };
    for (;;) {
        const float anfang = aus.back();
        float hoehe = aus.size() == 1 ? seitenHoehe + ersteMehr : seitenHoehe;
        //  Beginnt die Seite im Rumpf einer Tabelle mit thead, steht der Kopf oben und nimmt ihr Platz; die innerste zaehlt.
        SeitenKopf kopf;
        for (const TabKopf& t : tabKoepfe)
            if (anfang >= t.kopfUnten - 0.5f && anfang < t.tabUnten - 0.5f && t.kopfOben >= kopf.oben)
                kopf = { t.kopfOben, t.kopfUnten };
        hoehe -= kopf.unten - kopf.oben;
        if (koepfeAus) koepfeAus->push_back(kopf);
        //  Ein erzwungener Wechsel am Anfang einer Seite oder am Ende des Dokuments bleibt ohne leere Seite.
        const auto z = std::upper_bound(zwang.begin(), zwang.end(), anfang + 1);
        const bool erzwungen = z != zwang.end() && *z < ende - 1 && *z < anfang + hoehe;
        if (erzwungen) { aus.push_back(*z); continue; }
        if (anfang + hoehe >= ende) break;
        float schnitt = schiebe(alle, anfang, anfang + hoehe);
        //  Ketten aus Kanten koennen eine ganze Seite fuellen: dann nur noch Zeilen schuetzen, zuletzt hart schneiden.
        //  Unter einem Drittel gefuellt, weil ein grosser Block ganz bleiben wollte: dann lieber ihn teilen.
        if (schnitt < anfang + hoehe / 3) schnitt = std::max(schnitt, schiebe(fein, anfang, anfang + hoehe));
        if (schnitt <= anfang + 1) schnitt = schiebe(inhalt, anfang, anfang + hoehe);
        if (schnitt <= anfang + 1) schnitt = anfang + hoehe;
        //  Letzte Ueberschrift im unteren Drittel, ihr Abschnitt laeuft weiter: sie geht mit (wiederholt: h3, dann h2).
        for (;;) {
            const auto kopfNach = std::lower_bound(koepfe.begin(), koepfe.end(), schnitt,
                                                   [](const Kopf& a, float y) { return a.oben < y; });
            if (kopfNach == koepfe.begin()) break;
            const Kopf& h = *std::prev(kopfNach);
            const auto weiter = std::lower_bound(inhalt.begin(), inhalt.end(), Spanne{ schnitt - 0.5f, 0 }, nachOben);
            //  Eine tiefere Ueberschrift danach (h3 unter h2) gehoert noch zum Abschnitt.
            const bool laeuftWeiter = weiter != inhalt.end() && (kopfNach == koepfe.end() || weiter->oben < kopfNach->oben
                                                               || kopfNach->stufe > h.stufe);
            if (!(h.anfang > anfang + 1 && schnitt - h.anfang < hoehe / 3 && laeuftWeiter)) break;
            //  Eine Kette aus Karten zog den Schnitt sonst bis an den Seitenanfang hoch.
            float neu = schiebe(alle, anfang, h.anfang);
            if (neu < anfang + hoehe / 3) neu = std::max(neu, schiebe(fein, anfang, h.anfang));
            if (!(neu >= anfang + hoehe / 3 && neu < schnitt)) break;
            schnitt = neu;
        }
        aus.push_back(schnitt);
    }
    aus.push_back(ende);
    return aus;
}

int schreibePdf(const Dokument& d, std::span<const uint8_t> zustand, const PdfOptionen& o, const QString& ziel,
                QString* fehler, const std::atomic<bool>* abbruch) {
    auto fehlschlag = [fehler](const QString& t) { if (fehler) *fehler = t; return -1; };
    auto abgebrochen = [abbruch] { return abbruch && abbruch->load(std::memory_order_relaxed); };

    QByteArray bytes;
    QBuffer sink(&bytes);
    sink.open(QIODevice::WriteOnly);
    //  Zeiger, damit er vor dem Zusammenfassen stirbt: erst sein Ende schreibt den Abschluss der Datei.
    auto schreiber = std::make_unique<QPdfWriter>(&sink);
    QPdfWriter& writer = *schreiber;
    const Stilblatt b = stilblattVon(d);
    const Seitenvorgabe vorgabe = o.druck ? seitenVorgabe(b, Medium{ 794, 1123, true }) : Seitenvorgabe{};
    writer.setPageSize(vorgabe.groesse ? *vorgabe.groesse : QPageSize(QPageSize::A4));
    writer.setPageOrientation(vorgabe.lage ? *vorgabe.lage : o.quer ? QPageLayout::Landscape : QPageLayout::Portrait);
    //  Keine Raender im Schreiber: sonst endete die Leinwand an der Randkante statt am Blattrand.
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    writer.setResolution(kAufloesung);
    if (!ziel.isEmpty()) writer.setTitle(QFileInfo(ziel).completeBaseName());

    const QRectF blatt = writer.pageLayout().paintRectPixels(writer.resolution());
    //  Beide Stile fuellen das Blatt wie die Seite das Fenster, in gleicher Groesse und mit gleichen Umbruechen; nur
    //  Original laesst auf Seite 1 das obere Band weg (sonst ein heller Streifen ueber dunkler Kopfleiste).
    //  Ganze Pixel: Flaechen rasten auf Geraetepixel ein, die Schrift nicht - ein halber Rand verschob sie gegeneinander.
    //  Raender aus @page (Prozent oben/unten von der Blatthoehe, seitlich von der Breite); sonst die Baender, seitlich keiner.
    qreal rand[4] = { std::round(kBandMm / 25.4 * kAufloesung), 0, std::round(kBandMm / 25.4 * kAufloesung), 0 };
    const QSizeF blattMm = writer.pageLayout().fullRect(QPageLayout::Millimeter).size();
    for (int i = 0; i < 4; ++i)
        if (double mm = 0; inMm(vorgabe.rand[i], i % 2 ? blattMm.width() : blattMm.height(), mm) && mm >= 0)
            rand[i] = std::round(mm / 25.4 * kAufloesung);
    if (rand[1] + rand[3] > blatt.width() - 50) rand[1] = rand[3] = 0;
    if (rand[0] + rand[2] > blatt.height() - 50) rand[0] = rand[2] = std::round(kBandMm / 25.4 * kAufloesung);
    const qreal randOben = rand[0], randLinks = rand[3];
    const qreal ersteMehr = o.druck ? 0.0 : randOben;
    const float breite = float(std::max(1.0, blatt.width() - rand[1] - rand[3]));
    QFont fussSchrift;
    fussSchrift.setPointSizeF(kFussPt);
    const qreal fussH = rand[2];
    const float hoehe = float(std::max(1.0, std::floor(blatt.height() - rand[0] - rand[2])));

    SchriftWahl wahl;
    const Medium m{ int(std::lround(breite)), int(std::lround(hoehe)), o.druck, 1, &wahl };
    Stile s = berechne(d, b, m, zustand);
    //  Papier rollt nicht: eine seitwaerts rollende Leiste (Navigation) bricht um, statt am Blattrand abgeschnitten zu werden.
    for (Stil& st : s.stile)
        if ((st.anzeige == Anzeige::Flex || st.anzeige == Anzeige::InlineFlex)
            && (st.flexRichtung == FlexRichtung::Row || st.flexRichtung == FlexRichtung::RowReverse)
            && (st.ueberlaufX == Ueberlauf::Auto || st.ueberlaufX == Ueberlauf::Scroll) && st.flexUmbruch == FlexUmbruch::NoWrap)
            st.flexUmbruch = FlexUmbruch::Wrap;
    if (abgebrochen()) return fehlschlag(QStringLiteral("Abgebrochen."));
    const Kastenbaum k = baueKaesten(d, s, zustand);
    const Layout l = lege(d, s, k, m, breite);
    if (abgebrochen()) return fehlschlag(QStringLiteral("Abgebrochen."));
    std::vector<SeitenKopf> tabKoepfe;
    const std::vector<float> schnitte = seitenSchnitte(d, l, k, s, hoehe, float(ersteMehr), &tabKoepfe);
    const int seiten = int(schnitte.size()) - 1;
    if (ziel.isEmpty()) return seiten;

    Anzeigeliste a = baueAnzeige(d, s, k, l, o.druck);
    if (o.druck) {
        a.leinwand = 0xFFFFFFFFu;
        std::erase_if(a.befehle, [](const Befehl& x) { return x.hinten; });
        for (Befehl& x : a.befehle)
            if (x.art == BArt::Text || x.art == BArt::Linie) x.farbe = dunkler(x.farbe);
    }
    const QColor leinwand = a.leinwand == 0xFFFFFFFFu ? QColor(Qt::white) : QColor::fromRgba(a.leinwand);
    //  Die Fusszeile steht auf der Leinwand: hell auf dunkler, sonst dunkel.
    QColor fuss = leinwand.lightnessF() < 0.5 ? QColor(Qt::white) : QColor(Qt::black);
    fuss.setAlphaF(0.55f);

    const QList<int> folge = mg::seitenFolge(seiten, o.von, o.bis, o.seiten);
    //  Je Verweis-Kasten sein Ziel; ein Sprung zeigt auf die gedruckte Seite seines Ziels und entfaellt, wenn sie fehlt.
    struct Verweis {
        QByteArray uri;
        int ziel = -1;                  // Index in `folge`
        float zielY = 0;
    };
    std::vector<Verweis> verweise;
    std::vector<uint32_t> verweisVon(l.lage.size(), kKein);
    const uint32_t href = d.nameSuchen("href");
    for (uint32_t n = 0; href != kKein && n < d.knoten.size() && n < k.kastenVon.size(); ++n) {
        bool da = false;
        const uint32_t kn = k.kastenVon[n];
        if (kn >= l.lage.size() || d.knoten[n].art != Art::Element) continue;
        d.attribut(n, href, &da);
        if (!da || verweisUm(d, n) != n) continue;
        const VerweisZiel z = verweisZiel(d, n, o.quelle);
        Verweis v;
        if (!z.aussen.isEmpty() || !z.datei.isEmpty()) {
            v.uri = z.datei.isEmpty() ? z.aussen.toEncoded() : QUrl::fromLocalFile(z.datei).toEncoded();
        } else {
            const uint32_t zk = z.knoten == 0 ? 0 : z.knoten < k.kastenVon.size() ? k.kastenVon[z.knoten] : kKein;
            if (zk >= l.lage.size()) continue;
            const float y = z.knoten == 0 ? 0.f : l.lage[zk].y;
            const int nr = int(std::upper_bound(schnitte.begin() + 1, schnitte.end() - 1, y) - schnitte.begin());
            v.ziel = int(folge.indexOf(nr));
            if (v.ziel < 0) continue;
            const SeitenKopf& kz = tabKoepfe[size_t(nr - 1)];
            v.zielY = float(y + randOben + (kz.unten - kz.oben) - schnitte[size_t(nr - 1)] - (nr == 1 ? ersteMehr : 0.0));
        }
        verweisVon[kn] = uint32_t(verweise.size());
        verweise.push_back(std::move(v));
    }
    QList<mg::pdfglyphs::Link> links;
    constexpr qreal kPunkt = 72.0 / kAufloesung;

    PdfMalen malen;
    QPainter p;
    if (!p.begin(&writer)) return fehlschlag(QStringLiteral("PDF nicht anlegbar."));
    for (int i = 0; i < folge.size(); ++i) {
        const int nr = folge[i];
        if (abgebrochen()) return fehlschlag(QStringLiteral("Abgebrochen."));
        if (i > 0) writer.newPage();
        //  Zwei Pixel ueber das Blatt hinaus: `paintRectPixels` rundet.
        p.fillRect(blatt.adjusted(-2, -2, 2, 2), leinwand);
        const float oben = schnitte[size_t(nr - 1)], unten = schnitte[size_t(nr)];
        const SeitenKopf& kopf = tabKoepfe[size_t(nr - 1)];
        const float kopfH = kopf.unten - kopf.oben;
        if (kopfH > 0) {
            //  Feste Schichten liegen dabei weit weg: sie kommen mit dem Inhalt einmal auf die Seite.
            for (uint32_t t : a.fest) a.transformationen[t] = QTransform::fromTranslate(0, 1e7);
            malen.blatt = {};
            p.save();
            p.translate(randLinks, randOben - kopf.oben);
            const Rechteck kb{ 0, kopf.oben, breite, kopfH };
            p.setClipRect(QRectF(kb.x, kb.y, kb.b, kb.h));
            male(p, a, l, kb, &malen);
            p.restore();
        }
        for (uint32_t t : a.fest) a.transformationen[t] = QTransform::fromTranslate(0, oben - kopfH);
        malen.blatt = Rechteck{ 0, oben - kopfH, breite, hoehe + float(nr == 1 ? ersteMehr : 0.0) };
        p.save();
        p.translate(randLinks, randOben + kopfH - oben - (nr == 1 ? ersteMehr : 0.0));
        //  Beschnitten an der Seitenbreite wie im Browser: rechts ausserhalb geschobene Schubladen blieben sonst im Rand.
        const Rechteck band{ 0, oben, breite, unten - oben };
        p.setClipRect(QRectF(band.x, band.y, band.b, band.h));
        male(p, a, l, band, &malen);
        for (const PdfMalen::Verweis& f : malen.verweise)
            if (f.kasten < verweisVon.size() && verweisVon[f.kasten] != kKein) {
                const Verweis& v = verweise[verweisVon[f.kasten]];
                links.append({ i, QTransform::fromScale(kPunkt, kPunkt).mapRect(f.r), v.uri, v.ziel, v.zielY * kPunkt });
            }
        malen.verweise.clear();
        p.restore();

        p.setFont(fussSchrift);
        p.setPen(fuss);
        p.drawText(QRectF(0, blatt.height() - fussH, blatt.width(), fussH),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("%1/%2").arg(nr).arg(seiten));
    }
    p.end();
    schreiber.reset();
    sink.close();

    //  Qt schreibt ein Textobjekt je Glyphe (die Suche faende keine Woerter) und Verlaeufe je Fuellung neu.
    const QByteArray fertig =
        mg::pdfglyphs::addLinks(mg::pdfglyphs::dropDuplicates(mg::pdfglyphs::mergeGlyphRuns(bytes)), links);
    QSaveFile out(ziel);
    if (!out.open(QIODevice::WriteOnly) || out.write(fertig) != fertig.size() || !out.commit())
        return fehlschlag(QStringLiteral("Schreiben fehlgeschlagen."));
    return seiten;
}

}   // namespace mg::html
