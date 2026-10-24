#pragma once
//  HtmlStyle - Kaskade, Vererbung, var() und berechnete Werte je Element; gleiche Stile liegen einmal vor.
#include "html/HtmlCss.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace mg::html {

//  Laengen in 1/64 Pixel, Faktoren in 1/65536 als Ganzzahl: so hat ein Stil genau eine Byte-Darstellung und laesst
//  sich per memcmp vergleichen und hashen. Faktoren brauchen die Feinheit: line-height 1.65 waere in 1/64 schon 1.656.
inline constexpr int32_t kQ = 64;
inline constexpr int32_t kQZahl = 65536;

//  MinInhalt, MaxInhalt, Passend: min-content, max-content, fit-content.
enum class LArt : uint8_t { Wert, Auto, Keine, Normal, Zahl, MinInhalt, MaxInhalt, Passend };

//  Wert = px + prozent; calc(100% - 48px) bleibt beides. Zahl: Faktor (line-height) in `px`, in 1/65536.
//  `mitProzent` haelt 0% von 0px getrennt: flex-basis 0% verhaelt sich in unbestimmter Hoehe wie content.
struct Laenge {
    int32_t px = 0, prozent = 0;        // beide in 1/64
    LArt art = LArt::Wert;
    uint8_t mitProzent = 0;
    uint8_t frei[2] = {};
    bool operator==(const Laenge&) const = default;
};

enum class Anzeige : uint8_t {
    Inline, Block, InlineBlock, ListItem, Flex, InlineFlex, Grid, InlineGrid, Table, InlineTable, TableRowGroup,
    TableHeaderGroup, TableFooterGroup, TableRow, TableCell, TableColumnGroup, TableColumn, TableCaption, FlowRoot,
    Contents, Ruby, RubyText, None
};
enum class Lage : uint8_t { Static, Relative, Absolute, Fixed, Sticky };
enum class Ueberlauf : uint8_t { Visible, Hidden, Clip, Scroll, Auto };
enum class FlexRichtung : uint8_t { Row, RowReverse, Column, ColumnReverse };
enum class FlexUmbruch : uint8_t { NoWrap, Wrap, WrapReverse };
enum class Ausrichtung : uint8_t {
    Normal, Auto, Stretch, FlexStart, FlexEnd, Start, End, Center, Baseline, SpaceBetween, SpaceAround, SpaceEvenly,
    Left, Right
};
enum class Vertikal : uint8_t { Baseline, Sub, Super, Top, TextTop, Middle, Bottom, TextBottom, Wert };
enum class RahmenStil : uint8_t { None, Hidden, Solid, Dashed, Dotted, Double, Groove, Ridge, Inset, Outset };
enum class SchriftStil : uint8_t { Normal, Italic, Oblique };
enum class TextAusrichtung : uint8_t { Start, End, Left, Right, Center, Justify };
enum class Richtung : uint8_t { Ltr, Rtl };
enum class Leerraum : uint8_t { Normal, Pre, NoWrap, PreWrap, PreLine, BreakSpaces };
enum class TextTransform : uint8_t { None, Uppercase, Lowercase, Capitalize };
enum class Sichtbarkeit : uint8_t { Visible, Hidden, Collapse };
enum class ListenArt : uint8_t {
    Disc, Circle, Square, Decimal, DecimalLeadingZero, LowerAlpha, UpperAlpha, LowerRoman, UpperRoman, LowerGreek,
    DisclosureOpen, DisclosureClosed, None
};
enum class WortUmbruch : uint8_t { Normal, BreakAll, KeepAll, BreakWord };
enum TextDeko : uint8_t { DekoUnter = 1, DekoUeber = 2, DekoDurch = 4 };
inline constexpr int32_t kZAuto = INT32_MIN;

//  Berechnet: Farben ARGB samt currentColor, em/rem/vw/vh in px; Rohwerte (Verlaeufe, Raster ...) Index in `roh`, 0 = keiner.
struct Stil {
    uint32_t farbe = 0xFF000000u, hintergrund = 0;
    uint32_t rahmenFarbe[4] = { 0xFF000000u, 0xFF000000u, 0xFF000000u, 0xFF000000u };
    int32_t schriftGroesse = 16 * kQ, buchstabenAbstand = 0;
    int32_t rahmenAbstand[2] = { 0, 0 };    // border-spacing waagrecht, senkrecht
    int32_t rollRand = 0;               // scroll-margin-top
    int32_t flexWachsen = 0, flexSchrumpfen = kQZahl, zIndex = kZAuto;      // flex in 1/65536
    int32_t rahmenBreite[4] = { 3 * kQ, 3 * kQ, 3 * kQ, 3 * kQ };
    uint32_t schrift = 0, hintergrundBild = 0, schatten = 0, transformation = 0, gridSpalten = 0,
             gridSpalteA = 0, gridSpalteE = 0, gridZeileA = 0, gridZeileE = 0, gridZeilen = 0, gridFlaechen = 0,
             gridAutoZeilen = 0, gridAutoSpalten = 0,
             inhalt = 0;
    uint32_t hintergrundGroesse = 0, hintergrundLage = 0, hintergrundWdh = 0;   // 0: was `hintergrundBild` traegt
    uint32_t svgFuellung = 0, svgStrich = 0, svgStrichBreite = 0;    // fill, stroke, stroke-width als Text in `roh`
    uint32_t hintergrundFest = 0;       // background-attachment als Liste; 0: alle scroll
    uint32_t svgEigen = 0;              // Bit 0 fill, 1 stroke, 2 stroke-width: eine Regel am Element selbst gewann
    Laenge zeilenHoehe{ 0, 0, LArt::Normal };
    Laenge aussen[4], innen[4], radius[4];
    Laenge breite{ 0, 0, LArt::Auto }, hoehe{ 0, 0, LArt::Auto };
    Laenge minBreite{ 0, 0, LArt::Auto }, minHoehe{ 0, 0, LArt::Auto };
    Laenge maxBreite{ 0, 0, LArt::Keine }, maxHoehe{ 0, 0, LArt::Keine };
    Laenge versatz[4] = { { 0, 0, LArt::Auto }, { 0, 0, LArt::Auto }, { 0, 0, LArt::Auto }, { 0, 0, LArt::Auto } };
    Laenge zeilenAbstand{ 0, 0, LArt::Normal }, spaltenAbstand{ 0, 0, LArt::Normal };
    Laenge flexBasis{ 0, 0, LArt::Auto }, vertikalWert;
    uint16_t gewicht = 400;
    Anzeige anzeige = Anzeige::Inline;
    Lage position = Lage::Static;
    uint8_t randBox = 0;                // box-sizing: 1 = border-box
    Ueberlauf ueberlaufX = Ueberlauf::Visible, ueberlaufY = Ueberlauf::Visible;
    FlexRichtung flexRichtung = FlexRichtung::Row;
    FlexUmbruch flexUmbruch = FlexUmbruch::NoWrap;
    Ausrichtung inhaltAusrichtung = Ausrichtung::Normal, elementAusrichtung = Ausrichtung::Normal,
                selbstAusrichtung = Ausrichtung::Auto;
    uint8_t dekoration = 0;             // TextDeko
    Vertikal vertikal = Vertikal::Baseline;
    uint8_t deckkraft = 255;
    RahmenStil rahmenStil[4] = {};
    SchriftStil schriftStil = SchriftStil::Normal;
    TextAusrichtung textAusrichtung = TextAusrichtung::Start;
    Richtung richtung = Richtung::Ltr;
    Leerraum leerraum = Leerraum::Normal;
    TextTransform textTransform = TextTransform::None;
    Sichtbarkeit sichtbarkeit = Sichtbarkeit::Visible;
    ListenArt listenArt = ListenArt::Disc;
    uint8_t listenInnen = 0, wortUmbruch = 0, rahmenZusammen = 0;
    uint8_t titelUnten = 0;             // caption-side: bottom
    uint8_t umbruchInnen = 0, umbruchVor = 0, umbruchNach = 0;    // break-*: 0 auto, 1 avoid, 2 Seite
    uint8_t hintergrundText = 0;        // background-clip: text
    Ausrichtung justierung = Ausrichtung::Normal, selbstJustierung = Ausrichtung::Auto;    // justify-items, -self
    int32_t reihenfolge = 0;            // order
    uint8_t gridFluss = 0;              // grid-auto-flow: Bit 0 column, Bit 1 dense
    Ausrichtung zeilenVerteilung = Ausrichtung::Normal;    // align-content
    uint8_t tabelleFest = 0, leereZellen = 0;              // table-layout fixed; empty-cells: 1 hide
};
static_assert(std::has_unique_object_representations_v<Stil>, "Stil braucht eine eindeutige Byte-Darstellung");

struct Stile {
    std::vector<Stil> stile;            // einmalig; [0] = Ausgangswerte
    std::vector<uint32_t> jeKnoten;     // Element: sein Stil; Text: der des Elternelements
    std::vector<uint32_t> vor, nach;    // ::before / ::after mit content, sonst kKein
    std::vector<std::string> roh;       // [0] leer
    size_t bytes() const;
};

//  `zustand[knoten]`: Bit 0 :checked, Bit 1 details[open]; leer: aus den Attributen checked, selected und open.
inline constexpr uint8_t kAngehakt = 1, kOffen = 2;
Stile berechne(const Dokument& d, const Stilblatt& blatt, const Medium& m, std::span<const uint8_t> zustand = {});

std::vector<uint8_t> anfangsZustand(const Dokument& d);
//  Klick ohne Skript: summary klappt details, label, Haken, Radiogruppe je Formular.
bool aktiviere(const Dokument& d, std::vector<uint8_t>& zustand, uint32_t element);
bool oeffneUm(const Dokument& d, std::vector<uint8_t>& zustand, uint32_t element);    // details darum auf; true: geaendert
//  Naechstes a[href] ueber `element`; ein naeherer Haken schaltet selbst (kKein).
uint32_t verweisUm(const Dokument& d, uint32_t element);
uint32_t sprungZiel(const Dokument& d, std::string_view fragment);  // id, sonst a[name]; 0: Seitenanfang

namespace stil {

struct Umfeld {
    const Stil* eltern = nullptr;
    int32_t wurzelSchrift = 16 * kQ;
    const Medium* medium = nullptr;
};

//  Raeumt fremde Rohwerte ein; ohne Pool (nullptr) wird nur geprueft.
class RohPool;

//  Setzt die Langform oder alle Langformen der Kurzform; false bei ungueltigem Wert, `s` bleibt dann unveraendert.
bool setze(uint16_t eig, std::string_view wert, Stil& s, const Umfeld& u, RohPool* pool);
void uebernimm(uint16_t eig, const Stil& von, Stil& nach);
bool farbe(std::string_view wert, uint32_t aktuell, uint32_t& argb);
//  inherit = 1, initial = 2, unset und revert = 3, sonst 0.
int global(std::string_view wert);

}   // namespace stil

}   // namespace mg::html
