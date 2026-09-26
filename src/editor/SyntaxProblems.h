#pragma once
//  SyntaxProblems - was sich AUS DER DATEI ALLEIN als falsch entscheiden laesst.
//  Kein Wissen ueber Bibliotheken, keine Vermutung: was hier gemeldet wird, ist
//  falsch. Ob eine Funktion existiert, kann dieser Lauf NICHT beantworten - das
//  waere ein Compiler-Frontend, kein Zerleger.
//  Eigener Durchgang wie `FoldScanner`, aus demselben Grund: der Faerber fuehrt
//  je Block genau ein int, und das ist belegt.
#include "editor/LanguageTable.h"

#include <QList>
#include <QString>
#include <QStringView>

class QTextDocument;

namespace mg::editor {

//  Die Art bestimmt den SATZ, den die Oberflaeche zeigt - der Zerleger kennt
//  keine Sprache. `Format` traegt die Meldung des jeweiligen Lesers in `detail`.
enum class ProblemKind {
    UnmatchedOpen,          // Klammer geht auf und nie wieder zu
    UnmatchedClose,         // Klammer schliesst, ohne offen zu sein
    MismatchedClose,        // `(` mit `]` geschlossen
    UnterminatedComment,    // Blockkommentar laeuft bis zum Dateiende
    UnterminatedString,     // mehrzeilige Zeichenkette laeuft bis zum Dateiende
    Format,                 // JSON/XML: der Leser nennt den Grund
    MixedIndent             // Tabulator NACH einem Leerzeichen in der Einrueckung
};

struct Problem {
    int         block  = 0;     // Blocknummer, 0-basiert
    int         start  = 0;     // Spalte in dieser Zeile
    int         length = 1;
    ProblemKind kind   = ProblemKind::UnmatchedOpen;
    QString     detail;         // nur bei `Format`

    friend bool operator==(const Problem& a, const Problem& b) {
        return a.block == b.block && a.start == b.start && a.length == b.length
            && a.kind == b.kind && a.detail == b.detail;
    }
};

//  Alle Fundstellen des Dokuments. Leer heisst: nichts, was sich aus der Datei
//  allein widerlegen laesst.
QList<Problem> scanProblems(const QTextDocument* doc, const LanguageDef& def,
                            const QString& pfad);

//  Wer Text einfuegt, in dem keines dieser Zeichen vorkommt, braucht keinen
//  neuen Durchgang - das erspart im Normalfall des Tippens die ganze Arbeit.
bool touchesProblems(QStringView eingefuegt);

//  Deckel: ueber dieser Groesse laeuft gar nichts. Der Durchgang liegt im
//  GUI-Faden, und eine 8-MB-Datei je Tastendruck zu pruefen waere ein Ruckler.
inline constexpr int kMaxZeichen = 4 << 20;

}  // namespace mg::editor
