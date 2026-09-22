#include "core/MGStorage.h"

#include <algorithm>
#include <cstring>

namespace mg::storage {
namespace {

constexpr char kKennung[4] = { 'M', 'G', 'S', 'T' };

//  ── Schreiben ───────────────────────────────────────────────────────────────

void schreibVarint(std::string& out, std::uint32_t wert) {
    while (true) {
        const std::uint8_t b = wert & 0x7Fu;
        wert >>= 7;
        out.push_back(static_cast<char>(wert ? (b | 0x80u) : b));
        if (!wert) return;
    }
}

void schreibText(std::string& out, const std::string& s) {
    schreibVarint(out, static_cast<std::uint32_t>(s.size()));
    out.append(s);
}

void schreibFarbe(std::string& out, std::uint32_t farbe) {
    out.push_back(static_cast<char>((farbe >> 16) & 0xFFu));
    out.push_back(static_cast<char>((farbe >> 8) & 0xFFu));
    out.push_back(static_cast<char>(farbe & 0xFFu));
}

//  Sortierte Liste als Anzahl + ABSTAENDE: benachbarte Nummern kosten dann ein
//  Byte statt zweier oder dreier.
//  `puffer` ist geliehene Arbeitsflaeche: zum Sortieren braucht es eine Kopie,
//  und ein eigener Vektor je Aufruf waere eine Anforderung je DATEI.
void schreibListe(std::string& out, const std::vector<std::uint32_t>& werte,
                  std::vector<std::uint32_t>& puffer) {
    puffer.assign(werte.begin(), werte.end());
    std::sort(puffer.begin(), puffer.end());
    puffer.erase(std::unique(puffer.begin(), puffer.end()), puffer.end());
    schreibVarint(out, static_cast<std::uint32_t>(puffer.size()));
    std::uint32_t letzt = 0;
    for (const std::uint32_t w : puffer) {
        schreibVarint(out, w - letzt);
        letzt = w;
    }
}

//  Eine Liste, die ihre REIHENFOLGE behaelt (Absolutwerte statt Abstaende).
//  Die Tags einer Datei stehen in der Reihenfolge, in der sie vergeben wurden -
//  sie zu sortieren waere eine sichtbare Aenderung an den Chips unter der Kachel.
void schreibReihe(std::string& out, const std::vector<std::uint32_t>& werte) {
    schreibVarint(out, static_cast<std::uint32_t>(werte.size()));
    for (const std::uint32_t w : werte) schreibVarint(out, w);
}

void schreibBlock(std::string& out, char kennung, const std::string& inhalt) {
    out.push_back(kennung);
    schreibVarint(out, static_cast<std::uint32_t>(inhalt.size()));
    out.append(inhalt);
}

//  Wie viele Zeichen haben zwei Namen am Anfang gemeinsam? Gezaehlt wird in
//  BYTES; UTF-8 ist praefixfrei, ein halbes Zeichen kann dabei nicht entstehen,
//  solange beide Namen gueltig sind - und der Rest wird ohnehin roh angehaengt.
std::uint32_t gemeinsamerAnfang(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return static_cast<std::uint32_t>(i);
}

//  ── Lesen ───────────────────────────────────────────────────────────────────

//  Ein Leser, der NIE ueber sein Ende hinausliest. Jede Entnahme prueft zuerst,
//  ob noch genug dasteht, und setzt sonst `kaputt`.
class Leser {
public:
    Leser(const char* daten, std::size_t laenge) : m_p(daten), m_ende(daten + laenge) {}

    bool kaputt() const { return m_kaputt; }
    std::size_t rest() const { return m_kaputt ? 0 : std::size_t(m_ende - m_p); }

    std::uint8_t byte() {
        if (rest() < 1) { m_kaputt = true; return 0; }
        return static_cast<std::uint8_t>(*m_p++);
    }

    std::uint32_t varint() {
        std::uint32_t wert = 0;
        for (int schritt = 0; schritt < 5; ++schritt) {
            if (rest() < 1) { m_kaputt = true; return 0; }
            const std::uint8_t b = static_cast<std::uint8_t>(*m_p++);
            wert |= std::uint32_t(b & 0x7Fu) << (7 * schritt);
            if (!(b & 0x80u)) return wert;
        }
        m_kaputt = true;                 // laenger als fuenf Byte gibt es nicht
        return 0;
    }

    std::string text() {
        const char* p = nullptr;
        std::uint32_t n = 0;
        if (!textRoh(p, n)) return {};
        return std::string(p, n);
    }

    //  Derselbe Text als AUSSCHNITT der Eingabe - gueltig, solange die Bytes leben.
    bool textRoh(const char*& aus, std::uint32_t& laenge) {
        const std::uint32_t n = varint();
        if (m_kaputt || n > kMaxTextLaenge || rest() < n) { m_kaputt = true; return false; }
        aus = m_p;
        laenge = n;
        m_p += n;
        return true;
    }

    std::uint32_t farbe() {
        if (rest() < 3) { m_kaputt = true; return 0; }
        const std::uint32_t r = static_cast<std::uint8_t>(*m_p++);
        const std::uint32_t g = static_cast<std::uint8_t>(*m_p++);
        const std::uint32_t b = static_cast<std::uint8_t>(*m_p++);
        return (r << 16) | (g << 8) | b;
    }

    //  Anzahl + Absolutwerte, Reihenfolge bleibt.
    std::vector<std::uint32_t> reihe(std::uint32_t grenze) {
        std::vector<std::uint32_t> out;
        const std::uint32_t n = varint();
        if (m_kaputt || n > kMaxEintraege || n > rest() + 1) { m_kaputt = true; return out; }
        out.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint32_t w = varint();
            if (m_kaputt || w >= grenze) { m_kaputt = true; return out; }
            out.push_back(w);
        }
        return out;
    }

    //  Anzahl + Abstaende. `grenze` ist die groesste zulaessige Nummer + 1 -
    //  eine Nummer, die ins Leere zeigt, macht die Datei ungueltig. In den
    //  Vektor des Aufrufers geschrieben: eine leere Liste kostet damit nichts.
    void listeIn(std::vector<std::uint32_t>& out, std::uint32_t grenze) {
        const std::uint32_t n = varint();
        if (m_kaputt || n > kMaxEintraege || n > rest() + 1) { m_kaputt = true; return; }
        if (!n) return;
        out.reserve(n);
        std::uint32_t letzt = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint32_t d = varint();
            if (m_kaputt) return;
            letzt += d;
            if (letzt >= grenze) { m_kaputt = true; return; }
            out.push_back(letzt);
        }
    }

    std::vector<std::uint32_t> liste(std::uint32_t grenze) {
        std::vector<std::uint32_t> out;
        listeIn(out, grenze);
        return out;
    }

    //  Ein ganzer Block: Kennung und Inhalt, oder false am Ende der Datei.
    bool block(char& kennung, const char*& inhalt, std::size_t& laenge) {
        if (rest() < 1) return false;
        kennung = *m_p++;
        const std::uint32_t n = varint();
        if (m_kaputt || rest() < n) { m_kaputt = true; return false; }
        inhalt = m_p;
        laenge = n;
        m_p += n;
        return true;
    }

private:
    const char* m_p;
    const char* m_ende;
    bool        m_kaputt = false;
};

bool lesTags(const char* d, std::size_t n, Ablage& a) {
    Leser l(d, n);
    const std::uint32_t anzahl = l.varint();
    if (l.kaputt() || anzahl > kMaxEintraege) return false;
    a.tags.reserve(anzahl);
    for (std::uint32_t i = 0; i < anzahl; ++i) {
        Tag t;
        t.name  = l.text();
        t.farbe = l.farbe();
        if (l.kaputt()) return false;
        a.tags.push_back(std::move(t));
    }
    return true;
}

bool lesSaetze(const char* d, std::size_t n, Ablage& a) {
    Leser l(d, n);
    const std::uint32_t anzahl = l.varint();
    if (l.kaputt() || anzahl > kMaxEintraege) return false;
    const std::uint32_t grenze = static_cast<std::uint32_t>(a.tags.size());
    a.saetze.reserve(anzahl);
    for (std::uint32_t i = 0; i < anzahl; ++i) {
        a.saetze.push_back(l.reihe(grenze));
        if (l.kaputt()) return false;
    }
    return true;
}

bool lesKategorien(const char* d, std::size_t n, Ablage& a) {
    Leser l(d, n);
    const std::uint32_t anzahl = l.varint();
    if (l.kaputt() || anzahl > kMaxEintraege) return false;
    const std::uint32_t grenze = static_cast<std::uint32_t>(a.tags.size());
    a.kategorien.reserve(anzahl);
    for (std::uint32_t i = 0; i < anzahl; ++i) {
        Kategorie k;
        k.id       = l.text();
        k.name     = l.text();
        k.farbe    = l.farbe();
        k.schalter = l.byte();
        k.eltern   = l.varint();
        k.tags     = l.liste(grenze);
        //  Ein Elternteil, den es nicht gibt - oder man selbst.
        if (l.kaputt() || k.eltern > anzahl || k.eltern == i + 1) return false;
        a.kategorien.push_back(std::move(k));
    }
    return true;
}

bool lesNamen(const char* d, std::size_t n, Ablage& a) {
    Leser l(d, n);
    const std::uint32_t anzahl = l.varint();
    if (l.kaputt() || anzahl > kMaxEintraege) return false;
    a.dateien.reserve(anzahl);
    //  `vorher` wird nur gekuerzt und verlaengert und fordert nach den ersten
    //  Dateien nichts mehr an; ueber `substr` und `+` waeren es drei Ketten je Datei.
    std::string vorher;
    for (std::uint32_t i = 0; i < anzahl; ++i) {
        const std::uint32_t gemeinsam = l.varint();
        if (l.kaputt() || gemeinsam > vorher.size()) return false;
        const char* rest = nullptr;
        std::uint32_t restLaenge = 0;
        if (!l.textRoh(rest, restLaenge)) return false;
        vorher.resize(gemeinsam);
        vorher.append(rest, restLaenge);
        a.dateien.emplace_back();
        a.dateien.back().name = vorher;
    }
    return true;
}

bool lesZuordnungen(const char* d, std::size_t n, Ablage& a) {
    Leser l(d, n);
    const std::uint32_t katGrenze = static_cast<std::uint32_t>(a.kategorien.size());
    for (Datei& f : a.dateien) {
        const std::uint8_t schalter = l.byte();
        if (l.kaputt()) return false;
        f.textfarbe = (schalter & 0x01u) ? l.farbe() : kKeineFarbe;
        const std::uint32_t satz = l.varint();
        if (l.kaputt() || satz > a.saetze.size()) return false;
        f.satz = satz ? satz - 1 : kOhneSatz;
        l.listeIn(f.kategorien, katGrenze);
        if (l.kaputt()) return false;
    }
    return !l.kaputt();
}

}  // namespace

std::string schreibe(const Ablage& ablage) {
    //  Sortiert speichern: nur dann greift der gemeinsame Anfang. Sortiert
    //  werden ZEIGER - ein Tausch bewegte sonst je Vergleich einen Namen samt
    //  Kategorienliste (gemessen 3,3 % aller Befehle in `std::swap<Datei>`).
    std::vector<const Datei*> reihe;
    reihe.reserve(ablage.dateien.size());
    for (const Datei& d : ablage.dateien) reihe.push_back(&d);
    const auto nachNamen = [](const Datei* a, const Datei* b) { return a->name < b->name; };
    //  Nach einem Laden steht die Reihe schon sortiert da, und gespeichert wird
    //  meist genau danach. Die Probe kostet n Vergleiche, das Sortieren n log n.
    if (!std::is_sorted(reihe.begin(), reihe.end(), nachNamen))
        std::sort(reihe.begin(), reihe.end(), nachNamen);

    std::string t;
    schreibVarint(t, static_cast<std::uint32_t>(ablage.tags.size()));
    for (const Tag& tag : ablage.tags) {
        schreibText(t, tag.name);
        schreibFarbe(t, tag.farbe);
    }

    std::string s;
    schreibVarint(s, static_cast<std::uint32_t>(ablage.saetze.size()));
    for (const std::vector<std::uint32_t>& satz : ablage.saetze) schreibReihe(s, satz);

    std::vector<std::uint32_t> puffer;
    std::string k;
    schreibVarint(k, static_cast<std::uint32_t>(ablage.kategorien.size()));
    for (const Kategorie& kat : ablage.kategorien) {
        schreibText(k, kat.id);
        schreibText(k, kat.name);
        schreibFarbe(k, kat.farbe);
        k.push_back(static_cast<char>(kat.schalter));
        schreibVarint(k, kat.eltern);
        schreibListe(k, kat.tags, puffer);
    }

    std::string f;
    schreibVarint(f, static_cast<std::uint32_t>(ablage.dateien.size()));
    const std::string* vorher = nullptr;
    for (const Datei* d : reihe) {
        const Datei& datei = *d;
        const std::uint32_t gemeinsam =
            vorher ? gemeinsamerAnfang(*vorher, datei.name) : 0u;
        schreibVarint(f, gemeinsam);
        schreibText(f, datei.name.substr(gemeinsam));
        vorher = &datei.name;
    }

    std::string z;
    for (const Datei* d : reihe) {
        const Datei& datei = *d;
        const bool hatFarbe = datei.textfarbe != kKeineFarbe;
        z.push_back(static_cast<char>(hatFarbe ? 0x01 : 0x00));
        if (hatFarbe) schreibFarbe(z, datei.textfarbe);
        schreibVarint(z, datei.satz == kOhneSatz ? 0u : datei.satz + 1u);
        schreibListe(z, datei.kategorien, puffer);
    }

    std::string out;
    out.reserve(t.size() + s.size() + k.size() + f.size() + z.size() + 32);
    out.append(kKennung, sizeof(kKennung));
    out.push_back(static_cast<char>(kFassung));
    schreibBlock(out, 'T', t);
    schreibBlock(out, 'S', s);
    schreibBlock(out, 'K', k);
    schreibBlock(out, 'F', f);
    schreibBlock(out, 'Z', z);
    return out;
}

bool istMGStorage(const char* daten, std::size_t laenge) {
    return daten && laenge >= sizeof(kKennung) + 1
           && std::memcmp(daten, kKennung, sizeof(kKennung)) == 0;
}

bool lies(const char* daten, std::size_t laenge, Ablage& ablage, std::string* fehler) {
    const auto scheitern = [fehler](const char* text) {
        if (fehler) *fehler = text;
        return false;
    };
    if (!istMGStorage(daten, laenge)) return scheitern("keine MGStorage-Datei");
    if (static_cast<std::uint8_t>(daten[4]) > kFassung)
        return scheitern("neuere Fassung als dieses Programm kennt");

    //  In eine eigene Ablage lesen und erst am Ende uebernehmen: bricht es
    //  mittendrin ab, bleibt der bisherige Stand stehen statt halb ueberschrieben.
    Ablage neu;
    Leser l(daten + 5, laenge - 5);
    char kennung = 0;
    const char* inhalt = nullptr;
    std::size_t n = 0;
    std::string gesehen;
    while (l.block(kennung, inhalt, n)) {
        bool ok = true;
        switch (kennung) {
        case 'T': ok = lesTags(inhalt, n, neu);        break;
        case 'S': ok = lesSaetze(inhalt, n, neu);      break;
        case 'K': ok = lesKategorien(inhalt, n, neu);  break;
        case 'F': ok = lesNamen(inhalt, n, neu);       break;
        case 'Z': ok = lesZuordnungen(inhalt, n, neu); break;
        default:  break;                 // unbekannt: ueberspringen, nicht scheitern
        }
        if (!ok) return scheitern("Block unlesbar");
        gesehen.push_back(kennung);
    }
    if (l.kaputt()) return scheitern("Datei bricht mitten im Block ab");

    //  ALLE fuenf Bloecke muessen dasein. Eine abgeschnittene Datei, der die
    //  hinteren fehlen, ist sonst „gueltig" - mit stillschweigend verlorenen
    //  Zuordnungen, die beim naechsten Speichern endgueltig weg waeren.
    for (const char k : { 'T', 'S', 'K', 'F', 'Z' })
        if (gesehen.find(k) == std::string::npos) return scheitern("Block fehlt");

    ablage = std::move(neu);
    return true;
}

namespace {

std::string farbeHex(std::uint32_t f) {
    if (f == kKeineFarbe) return "-";
    static const char* z = "0123456789abcdef";
    std::string out = "#";
    for (int schub = 20; schub >= 0; schub -= 4)
        out.push_back(z[(f >> schub) & 0xF]);
    return out;
}

//  Von der Wurzel her: "Urlaub / 2019".
std::string katPfad(const std::vector<Kategorie>& kats, std::size_t i) {
    std::string out = kats[i].name;
    std::uint32_t eltern = kats[i].eltern;
    //  Deckel: die Datei koennte praepariert sein.
    for (std::size_t schutz = 0; eltern != 0 && schutz < kats.size(); ++schutz) {
        const std::size_t e = eltern - 1;
        if (e >= kats.size()) break;
        out = kats[e].name + " / " + out;
        eltern = kats[e].eltern;
    }
    return out;
}

}  // namespace

std::string alsText(const Ablage& ablage, const std::string& ueberschrift) {
    std::string out;
    out.reserve(4096);
    out += "MGStorage - " + ueberschrift + "\n";
    out += "Fassung " + std::to_string(int(kFassung)) + "\n\n";

    out += "TAGS (" + std::to_string(ablage.tags.size()) + ")\n";
    for (std::size_t i = 0; i < ablage.tags.size(); ++i)
        out += "  " + std::to_string(i) + "  " + ablage.tags[i].name
             + "  " + farbeHex(ablage.tags[i].farbe) + "\n";

    out += "\nKATEGORIEN (" + std::to_string(ablage.kategorien.size()) + ")\n";
    for (std::size_t i = 0; i < ablage.kategorien.size(); ++i) {
        const Kategorie& k = ablage.kategorien[i];
        out += "  " + katPfad(ablage.kategorien, i);
        if (!k.tags.empty()) {
            out += "  [";
            for (std::size_t j = 0; j < k.tags.size(); ++j) {
                if (j) out += ", ";
                out += k.tags[j] < ablage.tags.size()
                       ? ablage.tags[k.tags[j]].name : "?";
            }
            out += "]";
        }
        out += "\n";
    }

    out += "\nDATEIEN (" + std::to_string(ablage.dateien.size()) + ")\n";
    for (const Datei& d : ablage.dateien) {
        out += "  " + d.name;
        if (d.satz != kOhneSatz && d.satz < ablage.saetze.size()) {
            const std::vector<std::uint32_t>& satz = ablage.saetze[d.satz];
            out += "  [";
            for (std::size_t j = 0; j < satz.size(); ++j) {
                if (j) out += ", ";
                out += satz[j] < ablage.tags.size() ? ablage.tags[satz[j]].name : "?";
            }
            out += "]";
        }
        for (const std::uint32_t k : d.kategorien)
            if (k < ablage.kategorien.size())
                out += "  <" + katPfad(ablage.kategorien, k) + ">";
        if (d.textfarbe != kKeineFarbe)
            out += "  PDF-Schriftfarbe " + farbeHex(d.textfarbe);
        out += "\n";
    }
    return out;
}

}  // namespace mg::storage
