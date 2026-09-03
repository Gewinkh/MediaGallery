#pragma once
#include <QtGlobal>

// ─────────────────────────────────────────────────────────────────────────────
//  SyntaxTypes.h - die gemeinsame Waehrung der Syntaxfaerbung.
//
//  EIN Satz Token-Klassen fuer ALLE Sprachen, bewusst so und nicht je Sprache
//  eigene: die Farben stehen in den Einstellungen, und ein Satz je Sprache
//  hiesse, dass die Einstellungsseite mit jeder neuen Sprache mitwaechst. Eine
//  neue Sprache bildet stattdessen in die vorhandenen Klassen ab - genau das
//  macht „eine Sprache = ein Tabelleneintrag" ueberhaupt erst moeglich.
//
//  Die vier letzten Klassen sind fuer Auszeichnungssprachen gedacht (Markdown,
//  XML/HTML), werden aber nicht dafuer reserviert: `Heading` faerbt in XML den
//  Tag-Namen, `Link` den Attributnamen. Lieber eine Klasse doppelt nutzen als
//  eine sechzehnte einfuehren, die im Einstellungsdialog erklaert werden muss.
// ─────────────────────────────────────────────────────────────────────────────
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
//  Dokument (§0-Prioritaet 4).
struct Span {
    qsizetype start;
    qsizetype length;
    Tok       tok;
};

//  Zustand, den eine Zeile an die naechste weiterreicht. `QSyntaxHighlighter`
//  merkt sich je Block genau ein int - mehr braucht es nicht, solange jeder
//  offene Zustand hier hineinpasst.
//    Bits 0-3  : was gerade offen ist (BlockState)
//    Bits 4-11 : Begrenzerzeichen einer offenen mehrzeiligen Zeichenkette
//                bzw. Laenge des offenen Markdown-Codezauns
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
