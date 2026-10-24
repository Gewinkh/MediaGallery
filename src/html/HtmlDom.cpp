#include "html/HtmlDom.h"

#include <array>

namespace mg::html {

namespace {

constexpr std::array<std::string_view, NFesteNamen> kFesteNamen = {
    "html", "head", "body", "title", "meta", "link", "style", "script", "base", "noscript", "template",
    "p", "div", "li", "ul", "ol", "dl", "dt", "dd", "pre", "listing", "textarea", "xmp", "iframe", "noembed", "noframes",
    "plaintext", "table", "caption", "colgroup", "col", "thead", "tbody", "tfoot", "tr", "td", "th",
    "area", "br", "embed", "hr", "img", "input", "source", "track", "wbr", "param",
    "address", "article", "aside", "blockquote", "details", "dialog", "fieldset", "figcaption", "figure",
    "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hgroup", "main", "menu", "nav", "section",
    "summary", "button", "a", "option", "optgroup", "svg", "math", "b", "big", "center", "code", "em", "font", "i",
    "nobr", "ruby", "s", "small", "span", "strike", "strong", "sub", "sup", "tt", "u", "var", "marquee", "object",
    "applet", "foreignObject", "desc", "id", "class",
};

}   // namespace

Dokument::Dokument() {
    m_index.reserve(256);
    m_namen.reserve(256);
    for (std::string_view n : kFesteNamen) nameEintragen(n);
}

uint32_t Dokument::nameEintragen(std::string_view n) {
    if (auto it = m_index.find(n); it != m_index.end()) return it->second;
    const auto id = uint32_t(m_namen.size());
    //  Knoten einer unordered_map wandern beim Umhaengen nicht - die Sicht auf den Schluessel bleibt gueltig.
    m_namen.push_back(m_index.emplace(std::string(n), id).first->first);
    return id;
}

uint32_t Dokument::nameSuchen(std::string_view n) const {
    const auto it = m_index.find(n);
    return it == m_index.end() ? kKein : it->second;
}

std::string_view Dokument::attribut(uint32_t element, uint32_t name, bool* da) const {
    if (da) *da = false;
    if (element >= knoten.size() || knoten[element].art != Art::Element) return {};
    const Knoten& k = knoten[element];
    for (uint32_t i = k.anfang; i < k.anfang + k.anzahl; ++i)
        if (attribute[i].name == name) {
            if (da) *da = true;
            return wert(attribute[i]);
        }
    return {};
}

size_t Dokument::bytes() const {
    size_t n = knoten.capacity() * sizeof(Knoten) + attribute.capacity() * sizeof(Attribut) + text.capacity()
             + m_namen.capacity() * sizeof(std::string_view) + m_index.bucket_count() * sizeof(void*);
    for (const auto& [name, id] : m_index)
        n += sizeof(std::string) + sizeof(uint32_t) + 2 * sizeof(void*) + (name.size() > 15 ? name.size() + 1 : 0);
    return n;
}

}   // namespace mg::html
