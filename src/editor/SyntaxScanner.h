#pragma once
#include "editor/LanguageTable.h"
#include "editor/SyntaxTypes.h"

#include <QStringView>
#include <QVarLengthArray>

// Kein QRegularExpression: ein Durchlauf ueber die Zeile ist schneller (~5 us je
// Zeile). Der Zerleger arbeitet immer nur auf EINER Zeile; was darueber laeuft, reist
// als int mit - dasselbe, das QSyntaxHighlighter je Block ohnehin fuehrt.
namespace mg::editor {

//  Typische Zeile hat weniger als 32 gefaerbte Abschnitte - so bleibt die Liste
//  auf dem Stack und der Zerleger allokiert im Normalfall gar nicht.
using SpanList = QVarLengthArray<Span, 32>;

//  Zerlegt eine Zeile. Rueckgabe: der Zustand fuer die FOLGENDE Zeile.
int scanLine(QStringView line, const LanguageDef& def, int stateIn, SpanList& out);

// Steht das Zeichen in einer Zeichenkette, einem Kommentar oder einem Codeabschnitt? Dann ist eine Klammer dort
// Text. Steht hier, weil Faltung und Klammernpaare dieselbe Antwort brauchen und `spans` vom selben Durchgang kommt.
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
