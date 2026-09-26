#include "core/MGAudioList.h"

#include <algorithm>
#include <cstring>

namespace mg::audiolist {
namespace {

constexpr char kKennung[4] = { 'M', 'G', 'A', 'L' };
constexpr std::size_t kKopf = 5;

void schreibVarint(std::string& out, std::uint32_t wert) {
    while (true) {
        const std::uint8_t b = wert & 0x7Fu;
        wert >>= 7;
        out.push_back(static_cast<char>(wert ? (b | 0x80u) : b));
        if (!wert) return;
    }
}

void schreibBlock(std::string& out, char kennung, const std::string& inhalt) {
    out.push_back(kennung);
    schreibVarint(out, static_cast<std::uint32_t>(inhalt.size()));
    out.append(inhalt);
}

std::uint32_t gemeinsamerAnfang(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return static_cast<std::uint32_t>(i);
}

//  Ein Leser, der NIE ueber sein Ende hinausliest.
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
        m_kaputt = true;
        return 0;
    }

    bool textRoh(const char*& aus, std::uint32_t& laenge) {
        const std::uint32_t n = varint();
        if (m_kaputt || n > kMaxTextLaenge || rest() < n) { m_kaputt = true; return false; }
        aus = m_p;
        laenge = n;
        m_p += n;
        return true;
    }

    //  Der Lauf fester 32-Bit-Zahlen am Stueck. Er wird als GANZES geprueft:
    //  erst, ob so viele Byte ueberhaupt dastehen, dann jede Zahl gegen die
    //  Grenze - kein Varint, keine verstreuten Laengen.
    bool lauf(std::vector<std::uint32_t>& aus, std::uint32_t grenze) {
        const std::uint32_t n = varint();
        if (m_kaputt || n > kMaxEintraege) { m_kaputt = true; return false; }
        if (rest() / 4 < n) { m_kaputt = true; return false; }
        aus.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint32_t w = leseKlein(reinterpret_cast<const unsigned char*>(m_p));
            m_p += 4;
            if (w >= grenze) { m_kaputt = true; aus.clear(); return false; }
            aus[i] = w;
        }
        return true;
    }

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
    //  Die Datei steht little endian da, unabhaengig von der Maschine.
    static std::uint32_t leseKlein(const unsigned char* b) {
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8)
             | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
    }

    const char* m_p;
    const char* m_ende;
    bool        m_kaputt = false;
};

bool fehlschlag(std::string* fehler, const char* text) {
    if (fehler) *fehler = text;
    return false;
}

}  // namespace

std::string schreibe(const std::vector<std::string>& reihenfolge) {
    //  Sortiert abgelegt, damit der gemeinsame Anfang greift; die Reihenfolge
    //  selbst steht als Nummernlauf daneben.
    std::vector<std::string> namen(reihenfolge.begin(), reihenfolge.end());
    std::sort(namen.begin(), namen.end());
    namen.erase(std::unique(namen.begin(), namen.end()), namen.end());

    std::string bloecke;
    {
        std::string f;
        schreibVarint(f, static_cast<std::uint32_t>(namen.size()));
        std::string vorher;
        for (const std::string& n : namen) {
            const std::uint32_t gleich = gemeinsamerAnfang(vorher, n);
            schreibVarint(f, gleich);
            schreibVarint(f, static_cast<std::uint32_t>(n.size() - gleich));
            f.append(n, gleich, std::string::npos);
            vorher = n;
        }
        schreibBlock(bloecke, 'F', f);
    }
    {
        std::string o;
        schreibVarint(o, static_cast<std::uint32_t>(reihenfolge.size()));
        for (const std::string& n : reihenfolge) {
            const auto it = std::lower_bound(namen.begin(), namen.end(), n);
            const std::uint32_t nr = static_cast<std::uint32_t>(it - namen.begin());
            o.push_back(static_cast<char>(nr & 0xFFu));
            o.push_back(static_cast<char>((nr >> 8) & 0xFFu));
            o.push_back(static_cast<char>((nr >> 16) & 0xFFu));
            o.push_back(static_cast<char>((nr >> 24) & 0xFFu));
        }
        schreibBlock(bloecke, 'O', o);
    }

    std::string out;
    out.reserve(kKopf + bloecke.size());
    out.append(kKennung, 4);
    out.push_back(static_cast<char>(kFassung));
    out.append(bloecke);
    return out;
}

bool istMGAudioList(const char* daten, std::size_t laenge) {
    return daten && laenge >= kKopf && std::memcmp(daten, kKennung, 4) == 0;
}

bool lies(const char* daten, std::size_t laenge,
          std::vector<std::string>& reihenfolge, std::string* fehler) {
    if (fehler) fehler->clear();
    if (!istMGAudioList(daten, laenge)) return fehlschlag(fehler, "keine MGAL-Datei");
    if (static_cast<std::uint8_t>(daten[4]) > kFassung)
        return fehlschlag(fehler, "neuere Fassung");

    Leser l(daten + kKopf, laenge - kKopf);
    std::vector<std::string>   namen;
    std::vector<std::uint32_t> folge;
    bool hatFolge = false;

    char kennung = 0;
    const char* inhalt = nullptr;
    std::size_t n = 0;
    while (l.block(kennung, inhalt, n)) {
        Leser b(inhalt, n);
        if (kennung == 'F') {
            const std::uint32_t anzahl = b.varint();
            if (b.kaputt() || anzahl > kMaxEintraege || anzahl > b.rest() + 1)
                return fehlschlag(fehler, "Namensblock");
            namen.reserve(anzahl);
            std::string vorher;
            for (std::uint32_t i = 0; i < anzahl; ++i) {
                const std::uint32_t gleich = b.varint();
                if (b.kaputt() || gleich > vorher.size())
                    return fehlschlag(fehler, "gemeinsamer Anfang");
                const char* roh = nullptr;
                std::uint32_t rl = 0;
                if (!b.textRoh(roh, rl)) return fehlschlag(fehler, "Name");
                std::string name(vorher, 0, gleich);
                name.append(roh, rl);
                //  Ein NAME, kein Pfad. Was daraus wird, haengt der Aufrufer an
                //  seinen Ordner - ein "../" oder ein "/" darin zeigte damit
                //  irgendwohin, und die Datei kommt von aussen.
                if (name.empty() || name == "." || name == ".."
                    || name.find('/') != std::string::npos
                    || name.find('\\') != std::string::npos)
                    return fehlschlag(fehler, "Name ist kein Dateiname");
                namen.push_back(name);
                vorher = std::move(name);
            }
        } else if (kennung == 'O') {
            //  Die Grenze kennt erst der Namensblock - er steht davor.
            if (!b.lauf(folge, static_cast<std::uint32_t>(namen.size())))
                return fehlschlag(fehler, "Reihenfolge");
            hatFolge = true;
        }
        //  Alles andere wird ueber seine Laenge uebersprungen.
    }
    if (l.kaputt()) return fehlschlag(fehler, "Blockgrenze");
    if (!hatFolge) return fehlschlag(fehler, "Reihenfolge fehlt");

    reihenfolge.clear();
    reihenfolge.reserve(folge.size());
    for (const std::uint32_t nr : folge) reihenfolge.push_back(namen[nr]);
    return true;
}

std::string alsText(const std::vector<std::string>& reihenfolge,
                    const std::string& ueberschrift) {
    std::string out = ueberschrift;
    out += "\n";
    out += "MGAL - " + std::to_string(reihenfolge.size()) + " Titel\n\n";
    std::size_t i = 1;
    for (const std::string& n : reihenfolge)
        out += "  " + std::to_string(i++) + ". " + n + "\n";
    return out;
}

}  // namespace mg::audiolist
