#pragma once
#include "editor/LanguageTable.h"

#include <QList>

class QTextDocument;

// Eigener Durchgang statt Färber-Zustand: der führt je Block genau ein int, das schon belegt ist - eine Tiefe
// darin hieße, dass ein getipptes `{` alle folgenden Blöcke neu färbt (am 8-MB-Deckel rund 200.000 je
// Tastendruck). Kosten des eigenen Laufs: 6,58 -> 6,83 ms an 4722 Zeilen, also rund 4 %.
namespace mg::editor {

struct FoldRegion {
    int start = 0;   // Blocknummer der Zeile, die den Bereich EROEFFNET
    int end   = 0;   // letzte Blocknummer, die dazugehoert (immer > start)
    friend bool operator==(const FoldRegion& a, const FoldRegion& b) {
        return a.start == b.start && a.end == b.end;
    }
};

// Alle faltbaren Bereiche; leere Liste, wenn die Sprache nichts zu falten hat. `tabWidth` zählt nur für
// `FoldKind::Indent` - ein Tabulator rückt bis zum nächsten Vielfachen davon ein.
QList<FoldRegion> scanFolds(const QTextDocument* doc, const LanguageDef& def,
                            int tabWidth);

//  Zeichen, die in dieser Sprache einen Bereich eroeffnen oder schliessen
//  koennen. Wer Text einfuegt, in dem KEINES davon vorkommt, braucht keinen
//  neuen Durchgang - das erspart im Normalfall des Tippens die ganze Arbeit.
bool touchesFolding(QStringView eingefuegt, FoldKind kind);

}  // namespace mg::editor
