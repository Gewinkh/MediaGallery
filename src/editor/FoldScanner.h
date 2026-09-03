#pragma once
#include "editor/LanguageTable.h"

#include <QList>

class QTextDocument;

// ─────────────────────────────────────────────────────────────────────────────
//  FoldScanner.h - welche Bloecke einer Datei lassen sich zuklappen?
//
//  Ergebnis ist eine flache, nach Startzeile sortierte Liste von Bereichen in
//  BLOCKNUMMERN. Geschachtelte Bereiche stehen beide darin (eine Funktion und
//  die `if`-Bloecke darin); wer den aeusseren zuklappt, verbirgt die inneren
//  mit - sie bleiben in der Liste, sind aber nicht mehr sichtbar.
//
//  WARUM EIN EIGENER DURCHGANG und nicht der Zustand des Faerbers: der Faerber
//  fuehrt je Block genau EIN int (`QSyntaxHighlighter`), und das ist mit dem
//  Zustand mehrzeiliger Zeichenketten bereits belegt. Vor allem aber wuerde
//  eine Verschachtelungstiefe IM Blockzustand bedeuten, dass ein getipptes `{`
//  jeden folgenden Block neu faerbt - am Lesedeckel von 8 MB waeren das rund
//  200 000 Bloecke JE TASTENDRUCK. Der eigene Durchgang laeuft stattdessen
//  gebuendelt, kurz nachdem die Eingabe steht (s. `TextFoldBar`).
//
//  Kosten: der Durchgang benutzt denselben Zerleger wie die Faerbung, weil ein
//  `{` in einer Zeichenkette oder einem Kommentar keine Klammer ist. Gemessen
//  auf `PdfEditController.cpp` (4 722 Zeilen): 6,58 ms fuer die reine Faerbung,
//  6,83 ms mit der Faltungserfassung - also rund 4 % Aufpreis.
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::editor {

struct FoldRegion {
    int start = 0;   // Blocknummer der Zeile, die den Bereich EROEFFNET
    int end   = 0;   // letzte Blocknummer, die dazugehoert (immer > start)
    friend bool operator==(const FoldRegion& a, const FoldRegion& b) {
        return a.start == b.start && a.end == b.end;
    }
};

//  Alle faltbaren Bereiche des Dokuments. Leere Liste, wenn die Sprache nichts
//  zu falten hat (`FoldKind::None`) oder das Dokument fehlt.
//  `tabWidth` zaehlt nur fuer `FoldKind::Indent` - ein Tabulator rueckt bis zum
//  naechsten Vielfachen davon ein.
QList<FoldRegion> scanFolds(const QTextDocument* doc, const LanguageDef& def,
                            int tabWidth);

//  Zeichen, die in dieser Sprache einen Bereich eroeffnen oder schliessen
//  koennen. Wer Text einfuegt, in dem KEINES davon vorkommt, braucht keinen
//  neuen Durchgang - das erspart im Normalfall des Tippens die ganze Arbeit.
bool touchesFolding(QStringView eingefuegt, FoldKind kind);

}  // namespace mg::editor
