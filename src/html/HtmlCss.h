#pragma once
//  HtmlCss - Stilblaetter als flache Felder; Eigenschaften ausserhalb von `Eig` fallen beim Zerlegen weg.
#include "html/HtmlDom.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mg::html {

//  Erst die geerbten Langformen, dann die uebrigen, dann die Kurzformen. Seiten stehen immer oben, rechts, unten, links.
enum Eig : uint16_t {
    EColor, EFontFamily, EFontSize, EFontWeight, EFontStyle, ELineHeight, ELetterSpacing, ETextAlign, EDirection,
    EWhiteSpace, ETextTransform, EVisibility, EListStyleType, EListStylePosition, EWordBreak, EBorderCollapse,
    EBorderSpacing, ECaptionSide, EFill, EStroke, EStrokeWidth, EEmptyCells,
    EGeerbt,
    EDisplay = EGeerbt, EPosition, ETop, ERight, EBottom, ELeft, EZIndex, EWidth, EHeight, EMinWidth, EMinHeight,
    EMaxWidth, EMaxHeight, EBoxSizing,
    EMarginTop, EMarginRight, EMarginBottom, EMarginLeft,
    EPaddingTop, EPaddingRight, EPaddingBottom, EPaddingLeft,
    EBorderTopWidth, EBorderRightWidth, EBorderBottomWidth, EBorderLeftWidth,
    EBorderTopStyle, EBorderRightStyle, EBorderBottomStyle, EBorderLeftStyle,
    EBorderTopColor, EBorderRightColor, EBorderBottomColor, EBorderLeftColor,
    EBorderTopLeftRadius, EBorderTopRightRadius, EBorderBottomRightRadius, EBorderBottomLeftRadius,
    EBackgroundColor, EBackgroundImage, EBackgroundSize, EBackgroundPosition, EBackgroundRepeat, EOpacity, EOverflowX, EOverflowY,
    EFlexDirection, EFlexWrap, EFlexGrow, EFlexShrink, EFlexBasis, EJustifyContent, EAlignItems, EAlignSelf,
    EJustifyItems, EJustifySelf,
    ERowGap, EColumnGap, EGridTemplateColumns, EGridColumnStart, EGridColumnEnd, EGridRowStart, EGridRowEnd,
    EGridTemplateRows, EGridTemplateAreas, EGridAutoRows, EGridAutoColumns, EGridAutoFlow, EOrder, EAlignContent, ETableLayout,
    ETextDecorationLine, EVerticalAlign, EBoxShadow,
    ETransform, EContent, EBreakInside, EBreakBefore, EBreakAfter, EBackgroundClip, EScrollMarginTop, EBackgroundAttachment,
    ELanghand,
    EMargin = ELanghand, EPadding, EBorder, EBorderTop, EBorderRight, EBorderBottom, EBorderLeft, EBorderWidth,
    EBorderStyle, EBorderColor, EBorderRadius, EBackground, EInset, EGap, EPlaceItems, EFlex, EFlexFlow, EListStyle,
    EOverflow, ETextDecoration, EFont, EScrollMargin, EGridColumn, EGridRow, EGridArea,
    EVariable,
    EAnzahl
};

//  kKein, wenn die Eigenschaft hier nicht gefuehrt wird; Namen klein geschrieben.
uint16_t eigenschaftVon(std::string_view name);
std::string_view eigenschaftName(uint16_t eig);
//  Langformen einer Kurzform in `aus` (hoechstens 12); 0 bei einer Langform.
int langformen(uint16_t eig, uint16_t* aus);

enum class Teilart : uint8_t { Typ, Klasse, Id, Attribut, Pseudo, Kombinator };
enum class Kombinator : uint8_t { Nachfahr, Kind, Nachbar, Geschwister };
enum class AttrOp : uint8_t { Da, Gleich, Wort, Strich, Anfang, Ende, Enthaelt };
//  `Nie` steht fuer Zustaende, die eine ruhende Seite nie hat (:hover, :focus ...).
enum class Pseudo : uint8_t {
    Root, FirstChild, LastChild, OnlyChild, NthChild, NthLastChild, FirstOfType, LastOfType, OnlyOfType,
    NthOfType, NthLastOfType, Not, Is, Where, Checked, Disabled, Enabled, Empty, Link, Nie
};
//  Selektoren auf Pseudo-Elemente, die nicht gezeichnet werden (::placeholder, ::-webkit-...), treffen nie.
enum class PseudoElement : uint8_t { Kein, Vor, Nach, Nie };

//  Ein einfacher Selektor oder ein Kombinator. Attribut: `a`/`b` = Wert in `zeichen`; :nth: an+b;
//  :not/:is/:where: `a`/`b` = Bereich in `argumente`.
struct Teil {
    Teilart art = Teilart::Typ;
    uint8_t op = 0;
    uint8_t ohneFall = 0;
    uint8_t frei = 0;
    uint32_t atom = kKein;
    int32_t a = 0, b = 0;
};

struct Selektor {
    uint32_t anfang = 0, anzahl = 0;    // Teile von links nach rechts
    uint32_t regel = kKein;             // kKein: Argument von :not/:is/:where
    uint32_t spezifitaet = 0;           // Ids << 20 | Klassen << 10 | Typen
    PseudoElement pseudo = PseudoElement::Kein;
};

struct Deklaration {
    uint32_t anfang = 0, laenge = 0;    // Wert in `text`, ohne !important
    uint32_t variable = kKein;          // Atom des Namens bei --name
    uint16_t eig = 0;
    uint8_t wichtig = 0, mitVar = 0;
};

struct Regel {
    uint32_t dekl = 0, anzahl = 0;
    uint32_t medien = kKein;            // Index in `medien`, kKein = immer
    uint32_t element = kKein;           // style-Attribut dieses Elements, dann ohne Selektor
    bool hinweis = false;               // aus HTML-Attributen (width, bgcolor ...): vor allen Autor-Regeln
};

//  Anfrage: Typ und Merkmale, alle muessen gelten; Liste: eine Anfrage genuegt, dazu die umschliessende Liste.
struct MedienMerkmal { uint8_t art = 0, vergleich = 0; int32_t wert = 0; };
struct MedienAnfrage { uint32_t anfang = 0, anzahl = 0; uint8_t typ = 0, nicht = 0; };
struct Medienliste { uint32_t anfang = 0, anzahl = 0, eltern = kKein; };
//  Eine @page-Regel ohne Seitenauswahl: Werte roh, leer = nicht gesetzt; Raender oben, rechts, unten, links.
struct SeitenRegel {
    uint32_t medien = kKein;
    std::string groesse, rand[4];
};

//  ch (Vorschub der Ziffer 0) und ex (x-Hoehe) je em der ersten Schrift: die Kaskade selbst kennt keine Schriften.
class SchriftMesser {
public:
    virtual ~SchriftMesser() = default;
    virtual void jeEm(std::string_view familien, int gewicht, bool kursiv, double& ch, double& ex) = 0;
};

struct Medium {
    int breite = 1024, hoehe = 768;     // CSS-Pixel des Sichtfensters
    bool druck = false;
    float pixelFaktor = 1;              // Geraetepixel je CSS-Pixel: Rahmenbreiten rasten darauf ein
    SchriftMesser* messer = nullptr;    // ohne: ch und ex je 0,5 em
    //  Gesetzt, sobald ein Wert die Fensterhoehe liest: nur dann muss eine neue Hoehe neu legen.
    bool* hoeheGenutzt = nullptr;
    void nutztHoehe() const { if (hoeheGenutzt) *hoeheGenutzt = true; }
};

class Stilblatt {
public:
    //  Haengt die Regeln an; `medien` gilt zusaetzlich fuer alle (media-Attribut des style-Elements).
    void zerlege(std::string_view css, uint32_t medien = kKein);
    void zerlegeAttribut(uint32_t element, std::string_view deklarationen, bool hinweis = false);
    uint32_t medienliste(std::string_view text, uint32_t eltern = kKein);
    bool gilt(uint32_t medienliste, const Medium& m) const;

    uint32_t atomSuchen(std::string_view s) const;
    std::string_view atomVon(uint32_t a) const { return a < m_atome.size() ? std::string_view(m_atome[a]) : std::string_view(); }
    size_t atomAnzahl() const { return m_atome.size(); }
    std::string_view wert(const Deklaration& d) const { return std::string_view(text).substr(d.anfang, d.laenge); }

    size_t bytes() const;

    std::string text;                   // Quelle ohne Kommentare; Werte zeigen hinein
    std::string zeichen;                // Attributwerte der Selektoren, Escapes aufgeloest
    std::vector<Regel> regeln;
    std::vector<Deklaration> deklarationen;
    std::vector<Selektor> selektoren;
    std::vector<Teil> teile;
    std::vector<uint32_t> argumente;    // Selektoren in :not/:is/:where
    std::vector<Medienliste> medien;
    std::vector<MedienAnfrage> anfragen;
    std::vector<MedienMerkmal> merkmale;
    std::vector<SeitenRegel> seiten;
    //  Selektoren nach dem rechten Glied: je Atom fuer Id, Klasse, Typ; `uebrige` ohne solches Glied.
    std::vector<std::vector<uint32_t>> nachId, nachKlasse, nachTyp;
    std::vector<uint32_t> uebrige;
    bool mitKlassen = false;            // Klassen- oder Id-Selektoren vorhanden

private:
    uint32_t atom(std::string_view s);
    void block(size_t von, size_t bis, uint32_t medien, int tiefe);
    void deklarationsblock(size_t von, size_t bis);
    void seitenblock(std::string_view auswahl, std::string_view inhalt, uint32_t medien);
    bool selektorliste(std::string_view s, uint32_t regel, std::vector<uint32_t>* innen, bool nachsichtig, int tiefe);
    bool komplex(std::string_view s, Selektor& sel, int tiefe);
    void einsortieren(uint32_t sel);

    struct Hash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    std::unordered_map<std::string, uint32_t, Hash, std::equal_to<>> m_index;
    std::vector<std::string> m_atome;
};

//  Erstes Zeichen aus `stopp` ab `i` ausserhalb von Klammern und Strings, sonst `bis`.
size_t bisAussen(std::string_view s, size_t i, size_t bis, std::string_view stopp);
std::string_view ohneRand(std::string_view s);

Stilblatt stilblattVon(const Dokument& d);

}   // namespace mg::html
