#pragma once
#include "editor/SyntaxTypes.h"

#include <QLatin1StringView>
#include <QStringList>
#include <QStringView>

// ─────────────────────────────────────────────────────────────────────────────
//  LanguageTable.h - die Sprachen als DATEN, nicht als Code.
//
//  Eine neue Sprache ist ein Eintrag in `LanguageTable.cpp` und sonst nichts:
//  Scanner-Art, Schluesselwoerter, Typwoerter, Kommentarzeichen, Begrenzer.
//  Der Zerleger (`SyntaxScanner`) kennt nur die vier Arten unten, nie eine
//  einzelne Sprache - sonst waechst mit jeder Sprache auch der Code, und genau
//  das wollte der Auftrag vermeiden.
//
//  Die Zuordnung Endung -> Sprache steht EBENFALLS hier (nicht als if-Kette).
//  `.h` ist der Sonderfall: es zaehlt als C/C++, weil das im Baum dieses
//  Projekts die einzige vorkommende Bedeutung ist.
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::editor {

//  Wie eine Zeile zerlegt wird. Die meisten Klammer-Sprachen unterscheiden sich
//  nur in der Wortliste - dafuer gibt es genau EINE Art.
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

//  Ein Wortfeld als Zeiger + Laenge. Die Felder liegen sortiert im Programm
//  (static const), damit die Suche binaer laufen kann und nichts allokiert.
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
    //  CSS-Eigenheiten. Als TABELLENFELD und nicht als Sonderweg im Zerleger:
    //  beides global zu machen ginge schief - `#` ist anderswo ein Operator,
    //  und „Wort vor Doppelpunkt" traefe in C++ jede Sprungmarke und jedes
    //  `case x:`, in Python jede Typangabe.
    bool hashColors    = false;       // #fff / #F2F1EC ist eine Farbe (Zahl)
    //  Wann ist ein Wort vor ':' ein Eigenschaftsname?
    //   Anywhere  - CSS: `body{background:x;color:y}` steht oft in EINER Zeile,
    //               ein Eigenschaftsname beginnt dort keine Zeile.
    //   LineStart - QML: nur wenn davor auf der Zeile nichts als Bezeichner und
    //               Punkte stehen (`anchors.fill:`). So bleibt der Doppelpunkt
    //               einer Bedingung (`a ? b : c`) ungefaerbt.
    ColonStyle propertyColon = ColonStyle::None;
    //  Grossgeschriebenes Wort vor '{' ist ein Typ (QML: `Rectangle {`).
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

//  Sprache zu einem Dateipfad. Nie nullptr: unbekannte Endungen liefern den
//  PlainText-Eintrag, damit der Aufrufer keinen Sonderfall braucht.
const LanguageDef& languageForPath(QStringView path);

//  Sprache ueber ihren Bezeichner ("cpp"). Nie nullptr, s. o.
const LanguageDef& languageForId(QStringView id);

//  Der PlainText-Eintrag - „keine Faerbung", aber ein gueltiger Eintrag.
const LanguageDef& plainTextLanguage();

//  Alle Endungen, die der Editor kennt (ohne Punkt, kleingeschrieben). Fuer
//  Pruefstaende und Gegenproben: die Galerie MUSS jede davon als Textdatei
//  fuehren, sonst faerbt der Editor eine Sprache, die sich nicht oeffnen laesst
//  (s. `media/MediaItem.h`).
QStringList knownExtensions();

//  Steht das Wort in der (sortierten) Liste? Binaersuche, ohne Allokation.
bool containsWord(const WordList& list, QStringView word);

}  // namespace mg::editor
