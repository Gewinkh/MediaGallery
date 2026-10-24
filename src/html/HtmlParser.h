#pragma once
//  HtmlParser - HTML nach dem WHATWG-Verfahren, verkuerzt auf die Anzeige; reines C++ fuer den Arbeitsfaden.
#include "html/HtmlDom.h"

#include <string_view>

namespace mg::html {

//  Darueber wird nur der Anfang zerlegt (`Dokument::abgeschnitten`): die Indizes sind 32 Bit, und Texte koennen beim
//  Aufloesen auf das Dreifache wachsen.
inline constexpr size_t kGroesstesDokument = size_t(64) << 20;

//  Erwartet UTF-8; ungueltige Bytes laufen unveraendert durch.
Dokument zerlege(std::string_view quelle);

}   // namespace mg::html
