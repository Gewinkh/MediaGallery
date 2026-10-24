#pragma once
//  HtmlBox - Kastenbaum aus Dokument und berechneten Stilen: anonyme Bloecke, ::before/::after, Listenmarken,
//  ersetzte Elemente, anonyme Tabellenteile. Reines C++ ohne Masse; die Lage der Kaesten rechnet HtmlLayout.
#include "html/HtmlStyle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mg::html {

enum class KArt : uint8_t {
    Block,      // ordnet Bloecke oder Zeilen an
    Inline,     // liegt in den Zeilen seines Blocks
    Atom,       // inline-block und ersetzte Elemente: in der Zeile ein Zeichen, innen ein Block
    Text,
    Umbruch,    // <br>
    Marke,      // Listenmarke ausserhalb des Inhalts, nimmt keinen Platz
};

enum KFlag : uint8_t {
    KAnonym = 1,        // nur geerbte Eigenschaften gelten: keine Raender, Breite auto
    KZeilen = 2,        // Kinder sind Inline-Inhalt, der Block ordnet Zeilen an
    KErsetzt = 4,       // svg, img, input ...: keine Kinder im Baum
    KAusserFluss = 8,   // absolute oder fixed
    KEigenText = 16,    // Text liegt in Kastenbaum::text statt im Dokument
    KIsoliert = 32,     // dir-Attribut oder bdi: Richtung in den Zeilen eingeschlossen
    KLeer = 64,         // Zelle ohne Inhalt im Fluss (Leerraum zaehlt nicht), fuer empty-cells
};

enum KPseudo : uint8_t { PKein, PVor, PNach, PMarke };

//  Rolle im Tabellenmodell; sie steht am Kasten, weil anonyme Ergaenzungen den Stil ihres Elternkastens tragen.
enum TRolle : uint8_t { TKeine, TTabelle, TKopf, TRumpf, TFuss, TZeile, TZelle, TTitel };
inline bool istGruppe(uint8_t r) { return r == TKopf || r == TRumpf || r == TFuss; }

struct Kasten {
    uint32_t knoten = kKein;    // Element bzw. Textknoten; bei Pseudo-Elementen das erzeugende Element
    uint32_t stil = 0;          // Index in Stile::stile; anonyme Bloecke tragen den des Elternkastens
    uint32_t eltern = kKein, erstesKind = kKein, letztesKind = kKein, naechstes = kKein;
    uint32_t anfang = 0, laenge = 0;    // Text: Bytes im Dokument bzw. in Kastenbaum::text
    KArt art = KArt::Block;
    uint8_t flags = 0;
    uint8_t pseudo = PKein;
    uint8_t rolle = TKeine;
};

struct Kastenbaum {
    std::vector<Kasten> kaesten;    // [0] ist die Wurzel; aus der Kette geloeste Kaesten bleiben ungenutzt stehen
    std::vector<uint32_t> kastenVon;    // je Dokumentknoten sein Kasten, kKein ohne
    std::string text;               // Text von ::before/::after und Marken
    size_t bytes() const;
};

//  `zustand` wie bei berechne(): ein per Klick aufgeklapptes details baut seinen Inhalt.
Kastenbaum baueKaesten(const Dokument& d, const Stile& s, std::span<const uint8_t> zustand = {});

}   // namespace mg::html
