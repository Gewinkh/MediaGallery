#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfTextReflow.h - ABSATZ-UMBRUCH in der eingebetteten Textebene
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  `PdfTextEditor` schiebt beim Tippen den Rest DERSELBEN Zeige-Anweisung mit -
//  wie ein Textverarbeitungsprogramm innerhalb einer Zeile. Text, den das PDF
//  über eigene Positionierung (`Td`/`Tm`) setzt, bleibt dagegen stehen: Eine
//  Zeile läuft über den rechten Rand hinaus, statt in die nächste umzubrechen
//  (README ▸ Planned). Dieses Modul schließt genau diese Lücke: Es erkennt den
//  ABSATZ um eine Zeichenposition, bricht ihn neu um und schreibt die Zeilen
//  zurück.
//
//  WARUM ALS ZWEITER GANG (nach dem Schreiben, nicht währenddessen)
//  ────────────────────────────────────────────────────────────────
//  Die Vorschubbreite JEDES Zeichens steht bereits im Layout der Seite
//  (`PdfTextLayout`, Breiten ausschließlich aus dem Dokument). Wird der Umbruch
//  auf die BEREITS geänderte Datei angewendet, tragen auch die neu getippten
//  Zeichen schon ihre gemessene Breite - es muss nichts geschätzt werden.
//
//  ABSATZ-ERKENNUNG (Geometrie, keine Semantik)
//  ────────────────────────────────────────────
//  Zeilen = Glyphen gleicher Grundlinie. Ein Absatz ist ein Block
//  aufeinanderfolgender Zeilen mit gleichem Zeilenabstand (±25 %), gleicher
//  Schriftgröße (±5 %) und gemeinsamer linker Kante (die ERSTE Zeile darf
//  eingezogen sein). Nach oben/unten begrenzt ihn eine Zeile, die den rechten
//  Rand NICHT ausfüllt - die endet den Absatz (klassische Heuristik; eine
//  kurze Zeile fließt nicht weiter).
//
//  UMBRUCH
//  ───────
//  Wortweise, gierig, mit den gemessenen Vorschubbreiten; jede Zeile behält
//  ihre EIGENE linke Kante (Einzug der ersten Zeile bleibt erhalten) und endet
//  am gemeinsamen rechten Rand.
//
//  WÄCHST DER ABSATZ?
//  ──────────────────
//  Passt der Text nicht mehr in die vorhandenen Zeilen, bekommt der Absatz eine
//  ZUSÄTZLICHE Zeile - und alles darunter rückt eine Zeilenhöhe nach unten.
//  Das ist nur zulässig, wenn
//   • unter dem Absatz ausschließlich TEXT steht (`PdfPageText::paints` ist dort
//     leer): Bilder und Vektorgrafik würden nicht mitwandern,
//   • jede Zeile darunter eine umschreibbare Positionierung hat
//     (`Tm` absolut -> y anpassen; `Td`/`TD` relativ -> EINMAL je Textobjekt;
//     `'`/`"` setzen die Zeile selbst -> nicht verschiebbar),
//   • der Zeilenabstand in TEXTRAUM bestimmbar ist (zwei Zeilen mit `Tm`, bzw.
//     der Sprung eines `Td`) - geraten wird nichts,
//   • und nach dem Verschieben nichts von der Seite fällt.
//  Sonst trägt die letzte Zeile den Rest und `overflow` meldet es.
//
//  ZURÜCKGESCHRIEBEN wird wie überall im PDF-Teil als INKREMENTELLES UPDATE
//  (Originalbytes 1:1 + neues Content-Objekt + XRef mit `/Prev`). Je Zeile
//  nimmt der ERSTE Zeigeoperator den neuen Text auf, die übrigen Operatoren
//  DERSELBEN Zeile werden geleert - dieselbe Technik, mit der
//  `PdfContentEditor` eine über mehrere Operatoren verteilte Zeile ersetzt.
//  Die Zeilen behalten dadurch ihre eigene Positionierung, es wird nichts
//  verschoben.
//
//  BEWUSST BEGRENZT (sonst false, der Aufrufer ändert dann nichts):
//   • unverschlüsselt, klassische xref, EIN `/Contents`-Strom je Seite,
//   • alle Zeilen des Absatzes nutzen DIESELBE Schrift (sonst ginge die
//     Auszeichnung eines fett gesetzten Wortes beim Zusammenziehen verloren),
//   • der neue Zeilentext muss in der Kodierung dieser Schrift darstellbar sein,
//   • Blocksatz wird nicht wiederhergestellt: die Zeilen stehen danach
//     linksbündig am gemeinsamen Rand (die Wortabstände einer neu umbrochenen
//     Zeile ließen sich sonst nur raten).
//  Ein Fehlschlag schreibt NICHTS (kein Fragment).
//
//  ABHÄNGIGKEITEN: Qt6::Core + ZLIB (über PdfObjects/PdfTextLayout).
//  Kein Q_OBJECT/moc; isoliert testbar.
// ══════════════════════════════════════════════════════════════════════════════

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
    //  Der Absatz hat eine Zeile GEWONNEN (`newLines` ist dann um eins länger
    //  als `oldLines`): Alles darunter rückt beim Schreiben eine Zeilenhöhe
    //  nach unten. Nur möglich, wenn unter dem Absatz ausschließlich Text steht
    //  (Grafik wandert nicht mit), dessen Positionierung umschreibbar ist und
    //  nichts von der Seite fällt - sonst bleibt es bei `overflow`.
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

    //  Plant und SCHREIBT (inkrementelles Update nach `outputPath`). Liefert
    //  false, wenn der Absatz nicht sicher umbrechbar ist; `plan` (optional)
    //  erhält auch dann das Zwischenergebnis, soweit es zustande kam.
    //  Ist nichts zu tun (`changed == false`), wird NICHT geschrieben und
    //  `false` mit `err == "unveraendert"` gemeldet.
    static bool reflowParagraph(const QString& inputPath, const QString& outputPath,
                                int pageIndex, int glyphIndex,
                                PdfReflowPlan* plan = nullptr, QString* err = nullptr);

    //  Wohin wandert eine Schreibmarke, die vor dem Umbruch beim Zeichen
    //  `glyphIndex` stand? Der Umbruch verschiebt LEERZEICHEN zwischen den
    //  Zeilen (der Strom trägt am Zeilenende keines) - die Folge der übrigen
    //  Zeichen bleibt dagegen unverändert. Genau daran wird die Marke geführt:
    //  Es zählt, wie viele NICHT-Leerzeichen vor ihr stehen. Liefert den neuen
    //  Index (bei `glyphIndex` vor dem Absatz unverändert).
    static int mapCaretIndex(const PdfReflowPlan& plan, int glyphIndex);
};

} // namespace mg
