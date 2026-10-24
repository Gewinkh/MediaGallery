#pragma once
//  HtmlDom - Dokumentbaum als flache Felder: Indizes statt Zeiger, alle Texte in EINEM Puffer, Namen einmal.
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mg::html {

inline constexpr uint32_t kKein = UINT32_MAX;

//  Namen, die der Zerleger selbst unterscheidet, stehen in dieser Folge vorn in jeder Namenstabelle -
//  er vergleicht dann Zahlen statt Zeichenketten. Reihenfolge = `kFesteNamen` in HtmlDom.cpp.
enum Name : uint32_t {
    NHtml, NHead, NBody, NTitle, NMeta, NLink, NStyle, NScript, NBase, NNoscript, NTemplate,
    NP, NDiv, NLi, NUl, NOl, NDl, NDt, NDd, NPre, NListing, NTextarea, NXmp, NIframe, NNoembed, NNoframes,
    NPlaintext, NTable, NCaption, NColgroup, NCol, NThead, NTbody, NTfoot, NTr, NTd, NTh,
    NArea, NBr, NEmbed, NHr, NImg, NInput, NSource, NTrack, NWbr, NParam,
    NAddress, NArticle, NAside, NBlockquote, NDetails, NDialog, NFieldset, NFigcaption, NFigure,
    NFooter, NForm, NH1, NH2, NH3, NH4, NH5, NH6, NHeader, NHgroup, NMain, NMenu, NNav, NSection,
    NSummary, NButton, NA, NOption, NOptgroup, NSvg, NMath, NB, NBig, NCenter, NCode, NEm, NFont, NI,
    NNobr, NRuby, NS, NSmall, NSpan, NStrike, NStrong, NSub, NSup, NTt, NU, NVar, NMarquee, NObject,
    NApplet, NForeignObject, NDesc, NId, NClass,
    NFesteNamen
};

enum class Art : uint8_t { Dokument, Element, Text };
//  Namensraum: in SVG und MathML gelten Gross-/Kleinschreibung und `/>` anders als in HTML.
enum class Raum : uint8_t { Html, Svg, MathMl };

struct Knoten {
    uint32_t eltern = kKein, erstesKind = kKein, letztesKind = kKein, naechstes = kKein;
    uint32_t name = kKein;
    uint32_t anfang = 0, anzahl = 0;    // Element: Bereich in `attribute`; Text: Bytes in `text`
    Art  art  = Art::Element;
    Raum raum = Raum::Html;
};

struct Attribut {
    uint32_t name = kKein;
    uint32_t anfang = 0, laenge = 0;    // Wert in `text`
};

class Dokument {
public:
    Dokument();
    //  Kopieren verboten: `m_namen` zeigt in die Schluessel von `m_index`. Verschieben laesst die Knoten der Tabelle stehen.
    Dokument(const Dokument&) = delete;
    Dokument& operator=(const Dokument&) = delete;
    Dokument(Dokument&&) = default;
    Dokument& operator=(Dokument&&) = default;

    std::vector<Knoten>   knoten;       // [0] ist die Wurzel
    std::vector<Attribut> attribute;
    std::string           text;         // UTF-8, Entitaeten aufgeloest, Zeilenenden als \n
    uint32_t html = kKein, head = kKein, body = kKein;
    bool hatSkript = false;             // ausgefuehrt wird nie; die Ansicht weist darauf hin
    bool abgeschnitten = false;         // Quelle ueber der Groessengrenze, nur der Anfang zerlegt
    std::string basis;                  // Ordner der Datei fuer relative Bilder; leer: nur data:- und file:-URLs

    uint32_t         nameEintragen(std::string_view n);
    uint32_t         nameSuchen(std::string_view n) const;     // kKein, wenn unbekannt
    std::string_view nameVon(uint32_t id) const { return id < m_namen.size() ? m_namen[id] : std::string_view(); }
    size_t namenAnzahl() const { return m_namen.size(); }

    std::string_view textVon(const Knoten& k) const { return std::string_view(text).substr(k.anfang, k.anzahl); }
    std::string_view wert(const Attribut& a) const  { return std::string_view(text).substr(a.anfang, a.laenge); }
    //  Wert des Attributs `name` am Element; `da` sagt, ob es ueberhaupt steht (leerer Wert ist erlaubt).
    std::string_view attribut(uint32_t element, uint32_t name, bool* da = nullptr) const;

    size_t bytes() const;

private:
    struct Hash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    std::unordered_map<std::string, uint32_t, Hash, std::equal_to<>> m_index;
    std::vector<std::string_view> m_namen;
};

}   // namespace mg::html
