#pragma once
//  HtmlLayout - Lage aller Kaesten: Blockfluss mit Randverschmelzung, Zeilen ueber QTextLayout (Umbruch, RTL, CJK,
//  Schrift-Ersatz), inline-block, Tabellen, Flex, Grid, absolute und fixed am Bezugskasten.
#include "html/HtmlBox.h"

#include <QRawFont>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mg::html {

struct Rechteck {
    float x = 0, y = 0, b = 0, h = 0;
};

//  anfang/laenge in UTF-16 im Text des Absatzes; grundlinie ab y.
struct Zeile {
    float x = 0, y = 0, b = 0, h = 0, grundlinie = 0;
    int32_t anfang = 0, laenge = 0;
    uint32_t absatz = 0;
};

struct Stueck {
    uint32_t kasten = kKein;
    int32_t anfang = 0, laenge = 0;
    float versatz = 0;                  // gegen die Grundlinie der Zeile: sub, super, vertical-align
};

//  Glyphen eines Textkastens in einer Zeile mit einer Schrift, ohne Leer- und Steuerzeichen; x1/x2 samt Vorschub.
struct Lauf {
    uint32_t kasten = kKein, schrift = 0, zeile = kKein;    // zeile kKein: Listenmarke
    uint32_t anfang = 0, anzahl = 0;    // in Layout::glyphen und Layout::glyphLagen
    float x1 = 0, x2 = 0, grundlinie = 0;
};

struct SchriftMass {                    // CSS-px, unten positiv
    float oben = 0, unterLage = 0, unterDicke = 1, durchLage = 0, durchDicke = 1;
};

//  Ein Inline-Kasten in einer Zeile (Randkasten); links/rechts: die Seite traegt Rahmen und Innenabstand.
struct Fragment {
    Rechteck r;
    uint32_t kasten = kKein;
    bool links = false, rechts = false;
};

//  Tabellen-Rahmen statt des Stils: `halb` liegt im Randkasten, `breite` wird gemalt (0: malt der Nachbar bzw. keiner).
struct LRahmen {
    float halb[4] = {}, breite[4] = {};
    uint32_t farbe[4] = {};
    RahmenStil stil[4] = {};
    bool ohneInnen = false;             // Tabelle bei border-collapse: kein Innenabstand
};

struct Absatz {
    uint32_t kasten = kKein;            // der Block, der die Zeilen ordnet
    uint32_t zeilenAnfang = 0, zeilenAnzahl = 0;
    uint32_t laufAnfang = 0, laufAnzahl = 0, fragmentAnfang = 0, fragmentAnzahl = 0;
    QString text;                       // Leerraum zusammengefasst, Kanten und Atome als Steuerzeichen
    std::vector<Stueck> stuecke;
};

struct Einbettung {
    uint32_t wirt = kKein;              // Absatz, in dem der Inline-Block steht; Layout::einbettungen danach aufsteigend
    int32_t stelle = 0;                 // seine Kante im Text des Wirts
    uint32_t von = 0, bis = 0;          // seine Absaetze
};

//  position: sticky - Versaetze (NAN bei auto) und der Inhaltskasten des Bezugs, in dem es klebt; Dokumentkoordinaten.
struct Klebend {
    uint32_t kasten = kKein;
    float oben = NAN, unten = NAN;
    float grenzeOben = 0, grenzeUnten = 0;
};

struct Layout {
    std::vector<Rechteck> lage;         // je Kasten der Randkasten (border-box) in Dokumentkoordinaten, CSS-px
    std::vector<Zeile> zeilen;          // Dokumentkoordinaten
    std::vector<Absatz> absaetze;
    std::vector<Lauf> laeufe;           // je Absatz zusammenhaengend, die Listenmarken am Ende
    std::vector<Fragment> fragmente;
    std::vector<Einbettung> einbettungen;
    std::vector<uint32_t> glyphen;
    std::vector<float> glyphLagen;      // x, y je Glyphe auf der Grundlinie, Dokumentkoordinaten
    std::vector<int32_t> glyphZeichen;  // je Glyphe die erste UTF-16-Stelle im Absatztext (Markieren, Suchen)
    std::vector<QRawFont> schriften;    // in echter Bruchteil-Groesse, nicht die s-fache des Legens
    std::vector<SchriftMass> masse;     // je Schrift
    std::vector<uint32_t> rahmenVon;    // je Kasten Index in `rahmen` oder kKein; leer ohne Tabellen
    std::vector<LRahmen> rahmen;
    std::vector<Klebend> klebend;       // aufsteigend nach Kasten
    float breite = 0, hoehe = 0;
    float fensterHoehe = 0;             // Hoehe des Anfangsblocks: Bezug fuer background-attachment fixed
    size_t bytes() const;
};

//  `breite`: Anfangsblock ohne Rollleiste; Hoehe und Pixelfaktor kommen aus `m`.
Layout lege(const Dokument& d, const Stile& s, const Kastenbaum& k, const Medium& m, float breite);

//  Schriftwahl wie das Legen: installierte Familien der Liste, Gattungen, feste Ersatzschriften; misst fuer die Kaskade
//  ch und ex. Je Faden eine.
class SchriftWahl final : public SchriftMesser {
public:
    QStringList familien(std::string_view roh);
    int gewicht(const QString& familie, int css, bool kursiv);
    void jeEm(std::string_view familien, int gewicht, bool kursiv, double& ch, double& ex) override;

private:
    std::unordered_map<std::string, QStringList> m_familien;
    std::unordered_map<QString, std::vector<int>> m_gewichte;
    std::unordered_map<std::string, std::pair<double, double>> m_jeEm;
};

}   // namespace mg::html
