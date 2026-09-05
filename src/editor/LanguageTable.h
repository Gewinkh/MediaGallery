#pragma once
#include "editor/SyntaxTypes.h"

#include <QLatin1StringView>
#include <QStringList>
#include <QStringView>

// Die Sprachen als DATEN, nicht als Code: eine neue Sprache ist ein Eintrag in `LanguageTable.cpp` und sonst
// nichts. Der Zerleger kennt nur die vier Arten unten, nie eine einzelne Sprache. `.h` zählt als C/C++.
namespace mg::editor {

enum class ScannerKind : quint8 {
    PlainText,   // keine Faerbung (txt, log, csv …)
    CLike,       // // und /* */, "…" '…', Praeprozessor mit #
    Script,      // # bis Zeilenende, "…" '…', dreifache Anfuehrung mehrzeilig
    Markup,      // <tag attr="wert">, <!-- … -->
    Ini,         // [Abschnitt], schluessel = wert, # bzw. ; als Kommentar
    Markdown     // Ueberschriften, Betonung, Listen, Codezaeune, Verweise
};

//  Woran erkennt man in dieser Sprache einen faltbaren Block? Als eigenes
//  Tabellenfeld und NICHT aus `ScannerKind` abgeleitet: `.supp` etwa wird wie
//  ein Skript gefaerbt (`#`-Kommentare), traegt seine Bloecke aber in `{ }`.
enum class FoldKind : quint8 {
    None,       // Klartext, Log, CSV - es gibt nichts zu verbergen
    Braces,     // `{` … `}` ausserhalb von Zeichenkette und Kommentar
    Indent,     // Python, YAML - tiefer eingerueckt heisst „gehoert dazu"
    Headings,   // Markdown - `#` bis zur naechsten gleich- oder hoeherrangigen
    Sections,   // INI, TOML - `[Abschnitt]` bis zum naechsten
    Tags,       // HTML/XML - `<tag>` bis `</tag>`
    Keywords    // Lua/Ruby/CMake - `function`/`do`/`if` bis `end`/`endif`
};

enum class ColonStyle : quint8 { None, Anywhere, LineStart };

struct WordList {
    const QLatin1StringView* words = nullptr;
    int                      count = 0;
};

struct LanguageDef {
    QLatin1StringView id;             // "cpp", "markdown" - auch der Anzeigename
    QLatin1StringView label;          // was in der Statuszeile steht
    ScannerKind       kind = ScannerKind::PlainText;

    WordList keywords;                // sortiert!
    WordList types;                   // sortiert!

    QLatin1StringView lineComment;    // "//" bzw. "#" - leer = keiner
    QLatin1StringView lineComment2;   // zweite Form, z. B. ";" in INI
    QLatin1StringView blockOpen;      // "/*"
    QLatin1StringView blockClose;     // "*/"

    bool preprocHash   = false;       // '#' als erstes Zeichen = Direktive
    // CSS-Eigenheiten als TABELLENFELD, nicht als Sonderweg im Zerleger: global gemacht ginge es schief - `#` ist
    // anderswo ein Operator, und "Wort vor Doppelpunkt" träfe jede Sprungmarke und jedes `case x:`.
    bool hashColors    = false;       // #fff / #F2F1EC ist eine Farbe (Zahl)
    // Wann ist ein Wort vor ':' ein Eigenschaftsname? `Anywhere` für CSS, wo mehrere Regeln in einer Zeile stehen;
    // `LineStart` für QML, damit der Doppelpunkt einer Bedingung (`a ? b : c`) ungefärbt bleibt.
    ColonStyle propertyColon = ColonStyle::None;
    bool typeBeforeBrace = false;
    bool tripleQuotes  = false;       // ''' bzw. """ ueber mehrere Zeilen
    bool rawStrings    = false;       // R"( … )" ueber mehrere Zeilen
    //  Vorlagenzeichenketten mit Gravis (JavaScript/QML) - duerfen ueber
    //  Zeilen laufen, deshalb ein eigener Merker.
    bool templateStrings = false;
    bool caseSensitive = true;
    FoldKind fold = FoldKind::None;
    //  Nur fuer `FoldKind::Keywords`: die Woerter, die einen Block oeffnen bzw.
    //  schliessen. Sortiert, wie alle Wortlisten hier.
    WordList foldOpen;
    WordList foldClose;
};

const LanguageDef& languageForPath(QStringView path);

const LanguageDef& languageForId(QStringView id);

const LanguageDef& plainTextLanguage();

// Alle Endungen, die der Editor kennt. Für Prüfstände und Gegenproben: die Galerie MUSS jede davon als
// Textdatei führen, sonst färbt der Editor eine Sprache, die sich nicht öffnen lässt.
QStringList knownExtensions();

bool containsWord(const WordList& list, QStringView word);

}  // namespace mg::editor
