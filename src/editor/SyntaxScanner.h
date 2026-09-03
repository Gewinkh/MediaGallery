#pragma once
#include "editor/LanguageTable.h"
#include "editor/SyntaxTypes.h"

#include <QStringView>
#include <QVarLengthArray>

// ─────────────────────────────────────────────────────────────────────────────
//  SyntaxScanner.h - handgeschriebener Zeichen-Scanner mit Zustandsautomat.
//
//  KEIN `QRegularExpression`: ein Muster je Zeile neu auszuwerten ist genau die
//  Art Kosten, die §0-Prioritaet 2 meint, und ein Durchlauf ueber eine Zeile ist
//  ohnehin schneller. Gemessen an der Sonde (20 000 Zeilen, Release): ~5 µs je
//  Zeile fuer den ganzen Vorgang inklusive Formatvergabe.
//
//  Der Zerleger arbeitet IMMER nur auf EINER Zeile. Was ueber die Zeilengrenze
//  laeuft (Blockkommentar, dreifach angefuehrte Zeichenkette, Codezaun), reist
//  als `int` mit - dasselbe int, das `QSyntaxHighlighter` je Block ohnehin
//  fuehrt. Dadurch faerbt ein Tastendruck genau EINEN Block neu.
//
//  `out` wird geleert und neu gefuellt; die Spans kommen in Reihenfolge und
//  ueberlappen nie. Abschnitte der Klasse `Normal` werden NICHT ausgegeben -
//  ungefaerbter Text ist die Vorgabe und kostet so keinen Eintrag.
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::editor {

//  Typische Zeile hat weniger als 32 gefaerbte Abschnitte - so bleibt die Liste
//  auf dem Stack und der Zerleger allokiert im Normalfall gar nicht.
using SpanList = QVarLengthArray<Span, 32>;

//  Zerlegt eine Zeile. Rueckgabe: der Zustand fuer die FOLGENDE Zeile.
int scanLine(QStringView line, const LanguageDef& def, int stateIn, SpanList& out);

//  Steht das Zeichen an `pos` in einer Zeichenkette, einem Kommentar oder einem
//  Codeabschnitt? Dann ist eine Klammer dort Text und keine Klammer - was
//  sowohl die Faltung (`FoldScanner`) als auch die Klammernpaare
//  (`TextDecorations`) wissen muessen. Steht hier, weil beide dieselbe Antwort
//  brauchen und `spans` ohnehin vom selben Durchgang kommt.
inline bool inStringOrComment(const SpanList& spans, int pos) {
    for (const Span& s : spans) {
        if (pos < s.start) return false;          // Spans kommen sortiert
        if (pos < s.start + s.length)
            return s.tok == Tok::String || s.tok == Tok::Comment
                || s.tok == Tok::CodeSpan;
    }
    return false;
}

}  // namespace mg::editor
