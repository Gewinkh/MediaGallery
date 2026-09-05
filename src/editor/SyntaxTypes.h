#pragma once
#include <QtGlobal>

// Ein Satz Token-Klassen fuer alle Sprachen: die Farben stehen in den Einstellungen,
// ein Satz je Sprache liesse die Einstellungsseite mitwachsen. Eine neue Sprache
// bildet in die vorhandenen Klassen ab - erst das macht "eine Sprache = ein Eintrag".
namespace mg::editor {

enum class Tok : quint8 {
    Normal = 0,   // ungefaerbt - nimmt die Schriftfarbe der Flaeche
    Keyword,      // if, while, class, def, return
    Type,         // int, bool, void, str - eingebaute Typen
    String,       // "…", '…', `…`
    Number,       // 42, 0x2a, 3.14f, 1_000
    Comment,      // //, #, /* … */
    Preproc,      // #include, #define, Shebang, <?xml …?>
    Function,     // Bezeichner unmittelbar vor einer oeffnenden Klammer
    Operator,     // + - * / = < > ! & | sowie Klammern und Satzzeichen
    Heading,      // Markdown-Ueberschrift; in XML/HTML der Tag-Name
    Emphasis,     // *kursiv*, **fett**, _unterstrichen_
    Link,         // [Text](Ziel), <ziel>; in XML/HTML der Attributname
    CodeSpan,     // `code`, ``` … ```, eingerueckter Codeblock
    Count
};

//  Ein gefaerbter Abschnitt INNERHALB einer Zeile. Der Zerleger fuellt davon
//  eine kleine Liste je Zeile; es entsteht nie eine Liste ueber das ganze
//  Dokument.
struct Span {
    qsizetype start;
    qsizetype length;
    Tok       tok;
};

// Zustand, den eine Zeile an die nächste weiterreicht - `QSyntaxHighlighter` merkt sich je Block genau ein int.
// Bits 0-3: was offen ist; Bits 4-11: Begrenzerzeichen bzw. Länge des offenen Markdown-Codezauns.
enum class BlockState : int {
    None        = 0,
    BlockComment,     // /* … */ laeuft weiter
    MultiString,      // ''' … ''' bzw. """ … """ (Python), R"( … )" (C++)
    CodeFence,        // ``` bzw. ~~~ (Markdown)
    XmlComment,       // <!-- … -->
    //  In HTML eingebettete Fremdsprache. Die NUTZLAST traegt dabei den
    //  Zustand des eingebetteten Zerlegers (offener Blockkommentar in CSS/JS) -
    //  eine zweite Ebene passt so noch in dasselbe eine `int`.
    EmbeddedCss,      // zwischen <style> und </style>
    EmbeddedJs        // zwischen <script> und </script>
};

constexpr int makeState(BlockState s, int nutzlast = 0) {
    return int(s) | (nutzlast << 4);
}
constexpr BlockState stateKind(int s) {
    return s <= 0 ? BlockState::None : BlockState(s & 0x0F);
}
constexpr int statePayload(int s) {
    return s <= 0 ? 0 : (s >> 4);
}

}  // namespace mg::editor
