#pragma once
// Absatz-Umbruch in der eingebetteten Textebene - `PdfTextEditor` schiebt nur den Rest DERSELBEN Zeige-
// Anweisung mit, eine über `Td`/`Tm` gesetzte Zeile lief über den rechten Rand hinaus. ZWEITER Gang nach dem
// Schreiben: so tragen auch die neu getippten Zeichen ihre gemessene Breite. Blocksatz wird nicht wiederhergestellt.

#include <QString>
#include <QStringList>
#include <QVector>

#include "pdf/edit/PdfTextLayout.h"

namespace mg {

//  Ergebnis der Umbruch-PLANUNG (ohne Datei-Änderung) - auch die Grundlage der
//  Regressionsprüfung: Der Plan ist reine Rechnung und lässt sich gegen
//  vorab bestimmte Sollwerte prüfen.
struct PdfReflowPlan {
    int  firstLine = -1;        // Index der ersten Absatz-Zeile (0-basiert, seitenweit)
    int  firstGlyph = -1;       // Glyphen-Index des ersten Absatz-Zeichens (seitenweit)
    int  lineCount = 0;         // Zeilen des Absatzes
    QStringList oldLines;       // Zeilentexte VOR dem Umbruch
    QStringList newLines;       // Zeilentexte NACH dem Umbruch (gleiche Anzahl)
    bool changed  = false;      // unterscheiden sich alt und neu?
    bool overflow = false;      // Rest passte nicht -> letzte Zeile trägt ihn
    // Der Absatz hat eine Zeile GEWONNEN: alles darunter rückt eine Zeilenhöhe nach unten. Nur möglich, wenn dort
    // ausschließlich Text mit umschreibbarer Positionierung steht und nichts von der Seite fällt.
    bool grew     = false;
    //  Zeilenabstand im TEXTRAUM (nur bei `grew`): So weit rückt alles unter
    //  dem Absatz nach unten. Aus zwei aufeinanderfolgenden Zeilen abgelesen,
    //  nicht geschätzt - die Seiten-Matrix bleibt dabei außen vor.
    qreal growDyText = 0.0;
};

class PdfTextReflow {
public:
    //  Plant den Umbruch des Absatzes, in dem das Zeichen `glyphIndex` steht.
    //  Ändert NICHTS an der Datei. `page` ist das Layout der Seite
    //  (`PdfTextLayout::buildForPage`).
    static bool planParagraph(const PdfPageText& page, int glyphIndex,
                              PdfReflowPlan* out, QString* err = nullptr);

    // Plant und SCHREIBT (inkrementelles Update). false, wenn der Absatz nicht sicher umbrechbar ist; `plan` erhält
    // auch dann das Zwischenergebnis. Ist nichts zu tun, wird NICHT geschrieben.
    static bool reflowParagraph(const QString& inputPath, const QString& outputPath,
                                int pageIndex, int glyphIndex,
                                PdfReflowPlan* plan = nullptr, QString* err = nullptr);

    // Wohin wandert eine Schreibmarke, die vor dem Umbruch bei `glyphIndex` stand? Der Umbruch verschiebt nur
    // LEERZEICHEN zwischen den Zeilen - geführt wird die Marke deshalb an der Zahl der Nicht-Leerzeichen vor ihr.
    static int mapCaretIndex(const PdfReflowPlan& plan, int glyphIndex);
};

} // namespace mg
