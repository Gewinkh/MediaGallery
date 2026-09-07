#pragma once
//  MGStorage - das Dateiformat der Tag-Ablage. Reines C++, damit es auch ohne
//  Qt-Kontext lesbar bleibt: "MGST", Fassungsbyte, dann Bloecke aus Kennung,
//  Laenge und Inhalt ('T' Tags, 'S' Tag-Saetze, 'K' Kategorien, 'F' Namen,
//  'Z' Zuordnungen). Ein unbekannter Block wird ueber seine Laenge uebersprungen.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mg::storage {

inline constexpr std::uint8_t kFassung = 1;

//  Deckel gegen praeparierte Dateien: eine kaputte soll nicht Speicher
//  anfordern, den es nicht gibt.
inline constexpr std::uint32_t kMaxEintraege = 5'000'000;
inline constexpr std::uint32_t kMaxTextLaenge = 1u << 16;

//  Farben sind 0xRRGGBB - dieser Wert liegt ausserhalb und heisst "keine".
inline constexpr std::uint32_t kKeineFarbe = 0xFF000000u;

struct Tag {
    std::string   name;
    std::uint32_t farbe = 0;
};

struct Kategorie {
    std::string   id;
    std::string   name;
    std::uint32_t farbe = 0;
    //  Bit 0: einheitliche Farbe · Bit 1: Farbe an Unterkategorien vererben
    std::uint8_t  schalter = 0;
    //  0 = Wurzel, sonst Nummer der Elternkategorie + 1
    std::uint32_t eltern = 0;
    std::vector<std::uint32_t> tags;
};

struct Datei {
    std::string   name;
    //  Nummer im Satzverzeichnis; `kOhneSatz` heisst: keine Tags.
    std::uint32_t satz = 0;
    std::uint32_t textfarbe = kKeineFarbe;
    std::vector<std::uint32_t> kategorien;
};

inline constexpr std::uint32_t kOhneSatz = 0xFFFFFFFFu;

struct Ablage {
    std::vector<Tag>                        tags;
    std::vector<std::vector<std::uint32_t>> saetze;
    std::vector<Kategorie>                  kategorien;
    std::vector<Datei>                      dateien;

    bool leer() const {
        return tags.empty() && kategorien.empty() && dateien.empty();
    }
};

//  Die Dateinamen werden dabei SORTIERT - nur so greift der gemeinsame Anfang,
//  und die Datei sieht bei gleichem Inhalt immer gleich aus.
std::string schreibe(const Ablage& ablage);

//  Gibt false zurueck und laesst `ablage` unangetastet, wenn die Datei nicht
//  passt; `fehler` nennt dann die Stelle.
bool lies(const char* daten, std::size_t laenge, Ablage& ablage, std::string* fehler);

//  Fuer den Aufrufer, der zwischen Formaten unterscheidet, ohne zu lesen.
bool istMGStorage(const char* daten, std::size_t laenge);

//  Zum ANSEHEN in der App, nicht zum Einlesen. `ueberschrift` steht in der
//  ersten Zeile - ueblicherweise der Dateiname.
std::string alsText(const Ablage& ablage, const std::string& ueberschrift);

}  // namespace mg::storage
