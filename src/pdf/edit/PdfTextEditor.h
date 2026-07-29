#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfTextEditor.h — ZEICHENWEISES Bearbeiten der eingebetteten Textebene
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Fügt an einer beliebigen Zeichenposition einer Seite Text ein oder löscht
//  Zeichen — DIREKT im Content-Stream, ohne die Seite zu rastern. Das ist die
//  Modellseite von „Textebene direkt bearbeiten" (README ▸ Planned): Die
//  Oberfläche liefert über `PdfTextLayout::hitTest` einen Zeichenindex, hier
//  wird daraus die Änderung an der Datei.
//
//  Unterschied zu `PdfContentEditor::editText`: dort wird eine bekannte
//  ZEICHENKETTE gesucht und ersetzt (Werkzeug „Text ersetzen"). Hier zählt die
//  POSITION — genau das, was ein Caret braucht.
//
//  VERFAHREN
//  ─────────
//  `PdfTextLayout` liefert Glyphen samt Zeigeoperator-Bereichen. Die Änderung
//  trifft die Rohbytes GENAU EINES Operanden; er wird neu geschrieben, der
//  Rest des Stroms bleibt unangetastet. Geschrieben wird als INKREMENTELLES
//  UPDATE (Originalbytes 1:1 + neues Content-Objekt + XRef mit `/Prev`), wie
//  im übrigen PDF-Teil des Projekts.
//
//  ZUR BREITE: Eingefügte Zeichen verschieben den nachfolgenden Text derselben
//  Zeige-Anweisung — genau wie in einem Textverarbeitungsprogramm. Text, der
//  über eigene Positionierung (`Td`/`Tm`) gesetzt ist, bleibt stehen; ein
//  echter Absatz-Umbruch über Zeilen hinweg ist damit AUSDRÜCKLICH NICHT
//  abgedeckt (s. README).
//
//  BEWUSST BEGRENZT (sonst false, der Aufrufer ändert dann nichts):
//   • unverschlüsselt, klassische xref, EIN `/Contents`-Strom je Seite,
//   • die Änderung muss innerhalb EINES Zeigeoperators liegen,
//   • der neue Text muss in der Kodierung der dort aktiven Schrift
//     darstellbar sein (sonst stünden Bytes im PDF, die die Schrift nicht hat),
//   • bei `TJ`-Arrays wird der gesamte Operand durch EINEN String ersetzt —
//     die Kerning-Feinabstände dieses Operanden gehen dabei verloren; das ist
//     der Preis dafür, dass der Text überhaupt bearbeitbar wird, und betrifft
//     nur den angefassten Operanden.
//  Ein Fehlschlag schreibt NICHTS (kein Fragment).
//
//  ABHÄNGIGKEITEN: Qt6::Core + ZLIB. Kein Q_OBJECT/moc; isoliert testbar.
// ══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <QString>

#include "pdf/edit/PdfEncodings.h"

namespace mg {

class PdfTextEditor {
public:
    //  Kodierung EINER Seiten-Schrift auflösen (`fontRes` = Ressourcenname wie
    //  „/F1", s. `PdfShowSpan::fontRes`): /Resources → /Font → Font-Objekt →
    //  /Encoding bzw. /ToUnicode bei Identity-H. Öffentlich, weil der
    //  Absatz-Umbruch (`PdfTextReflow`) dieselbe Auflösung braucht — eine
    //  zweite Kopie dieser Kette wäre eine Fehlerquelle.
    static bool encodingForPageFont(const QByteArray& pdfBytes, int pageIndex,
                                    const QByteArray& fontRes,
                                    pdfenc::Encoding* out);

    //  Fügt `text` VOR dem Zeichen mit dem Index `glyphIndex` ein
    //  (`glyphIndex == Anzahl` → ans Ende der letzten Zeige-Anweisung).
    static bool insertText(const QString& inputPath, const QString& outputPath,
                           int pageIndex, int glyphIndex, const QString& text,
                           QString* err = nullptr);

    //  Löscht `count` Zeichen ab `glyphIndex`.
    static bool deleteText(const QString& inputPath, const QString& outputPath,
                           int pageIndex, int glyphIndex, int count,
                           QString* err = nullptr);
};

} // namespace mg
