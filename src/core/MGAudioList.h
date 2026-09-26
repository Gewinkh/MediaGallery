#pragma once
//  MGAudioList - die vom Nutzer gewaehlte Reihenfolge der Audiodateien eines
//  Ordners. Reines C++ wie `MGStorage`, damit es ohne Qt-Kontext lesbar bleibt:
//  "MGAL", Fassungsbyte, dann Bloecke aus Kennung, Laenge und Inhalt
//  ('F' Namen, 'O' Reihenfolge). Ein unbekannter Block wird ueber seine Laenge
//  uebersprungen.
//  Die Namen stehen SORTIERT und mit gemeinsamem Anfang, die Reihenfolge als
//  EIN Lauf fester 32-Bit-Zahlen dahinter - getrennt, weil eine Folge von
//  Varints ueber die ganze Datei verstreut nichts hergibt, ein Lauf gleich
//  breiter Zahlen dagegen am Stueck geprueft werden kann.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mg::audiolist {

inline constexpr std::uint8_t kFassung = 1;

//  Deckel gegen praeparierte Dateien.
inline constexpr std::uint32_t kMaxEintraege  = 1'000'000;
inline constexpr std::uint32_t kMaxTextLaenge = 1u << 16;

//  Die Dateinamen (ohne Ordner) in der Reihenfolge, in der sie laufen sollen.
//  Beim Lesen ist das eine PRUEFUNG, keine Annahme: ein Name mit Trenner oder
//  ein "." bzw. ".." macht die Datei ungueltig - der Aufrufer haengt ihn an
//  seinen Ordner, und die Datei kann von aussen kommen.
std::string schreibe(const std::vector<std::string>& reihenfolge);

//  Gibt false zurueck und laesst `reihenfolge` unangetastet, wenn die Datei
//  nicht passt; `fehler` nennt dann die Stelle.
bool lies(const char* daten, std::size_t laenge,
          std::vector<std::string>& reihenfolge, std::string* fehler);

//  Fuer den Aufrufer, der zwischen Formaten unterscheidet, ohne zu lesen.
bool istMGAudioList(const char* daten, std::size_t laenge);

//  Zum ANSEHEN in der App, nicht zum Einlesen.
std::string alsText(const std::vector<std::string>& reihenfolge,
                    const std::string& ueberschrift);

}  // namespace mg::audiolist
