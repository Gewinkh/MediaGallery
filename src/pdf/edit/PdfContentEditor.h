#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfContentEditor.h — ECHTES Content-Stream-Editing (verlustfrei/vektoriell)
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Ersetzt eingebetteten PDF-Text DIREKT im Content-Stream (statt ihn mit einer
//  Deckfläche zu überdecken). Der Rest der Seite (Vektoren, Schriften, Bilder,
//  andere Textebene) bleibt UNVERÄNDERT — die Datei bleibt vektoriell.
//
//  VERFAHREN: INKREMENTELLES UPDATE (append-only)
//  ──────────────────────────────────────────────
//  Die Originalbytes werden 1:1 übernommen; ans Dateiende wird eine NEUE Version
//  nur der betroffenen Content-Stream-Objekte + eine kleine XRef-Sektion
//  (`/Prev` auf die alte) + Trailer angehängt. Kein Voll-Rewrite, minimaler
//  Eingriff, robust.
//
//  BEWUSST BEGRENZT & SICHER (sonst false → Aufrufer nutzt Raster-Export):
//   • unverschlüsselt (kein /Encrypt),
//   • Content-Stream unkomprimiert ODER genau /FlateDecode (ein Filter),
//   • /Contents ist EIN Stream (kein Array mehrerer Ströme),
//   • alle Fonts der Seite sind EINFACH (kein /Type0/CID) — ASCII-Byte==Zeichen,
//   • Original- UND Ersatztext rein ASCII (0x20–0x7E; dort stimmen WinAnsi/
//     Standard/MacRoman/PDFDoc mit ASCII überein → keine Encoding-Tabelle nötig),
//   • der Originaltext wird GENAU EINMAL auf der Seite gefunden (sonst
//     mehrdeutig → Fallback) — entweder als EIN `Tj`-String/`TJ`-Array oder
//     verteilt über eine FOLGE unmittelbar aufeinanderfolgender Zeige-
//     Operatoren (Erzeuger zerlegen eine Zeile oft in mehrere Tj/TJ). Eine
//     Folge zählt nur, solange zwischen ihren Gliedern ausschließlich
//     Leerraum steht: sobald Positionierung (Td/TD/Tm/T*) oder ein
//     Schriftwechsel (Tf) dazwischenliegt, bricht sie ab — der Ersatz kann
//     also nie über einen Zeilenumbruch oder eine Schriftgrenze hinweg
//     zusammengezogen werden. Der Ersatz landet im ERSTEN Glied, die
//     übrigen werden geleert.
//  Ein Fehlschlag schreibt NICHTS (kein Fragment).
//
//  ABHÄNGIGKEITEN: nur Qt6::Core + ZLIB (bestehende Projekt-Abhängigkeiten).
//  Kein Q_OBJECT/moc; isoliert testbar.
// ══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector>

namespace mg {

//  Eine gewünschte Textersetzung auf einer Seite. `original` muss der exakte,
//  zusammenhängende eingebettete Text sein (wie ihn `PdfTextController::
//  replaceProbe` liefert); `replacement` leer = löschen.
struct PdfTextEdit {
    int     page = 0;      // 0-basiert
    QString original;
    QString replacement;
};

//  Eine zu SCHWÄRZENDE Fläche. Anders als `PdfTextEdit` braucht sie den Text
//  NICHT zu kennen: entfernt wird, was geometrisch darunter liegt.
struct PdfRedactArea {
    int    page = 0;       // 0-basiert
    QRectF rect;           // PDF-Punkte, Ursprung OBEN-LINKS (wie PdfEditBox::rect)
};

class PdfContentEditor {
public:
    //  Wendet die Ersetzungen an und schreibt `outputPath` (atomar). Liefert
    //  false, wenn irgendeine Vorbedingung (s. Header) nicht SICHER erfüllt ist
    //  ODER nichts ersetzt wurde — dann bleibt `outputPath` ungeschrieben und
    //  der Aufrufer weicht auf den Raster-Export aus. `err` (optional) erhält
    //  einen kurzen Grund.
    static bool editText(const QString& inputPath, const QString& outputPath,
                         const QVector<PdfTextEdit>& edits, QString* err = nullptr);

    //  ── SCHWÄRZEN, GEOMETRISCH statt über den Text ─────────────────────────
    //  Entfernt jedes Zeichen, dessen Kasten eine der Flächen berührt — ohne zu
    //  wissen, WELCHER Text dort steht. Das ist der Unterschied zu `editText`:
    //  dort muss der Originaltext als Zeichenkette wiedergefunden werden, und
    //  jedes Scheitern (Text über mehrere Zeigeoperatoren, Sonderkodierung,
    //  Sonde ohne Fund) kostete bisher die GANZE Textebene, weil der Aufrufer
    //  dann die Seiten rastern musste.
    //
    //  Die entstehende Lücke wird mit einem TJ-Versatz exakt ausgeglichen
    //  (`PdfShowSpan::tjUnitPt`), damit der Rest der Zeile stehen bleibt; die
    //  Ausgabe ist ein inkrementelles Update wie bei `editText`.
    //
    //  SELBSTPRÜFUNG: Nach dem Schreiben wird die Ausgabe neu vermessen; steht
    //  noch eine Glyphe in einer der Flächen, gilt der Lauf als GESCHEITERT
    //  (Ausgabe wird gelöscht) — die Zusage „Text ist weg" wird gemessen, nicht
    //  angenommen. Ein `false` heißt für den Aufrufer wie gehabt: Raster-Weg.
    static bool redactAreas(const QString& inputPath, const QString& outputPath,
                            const QVector<PdfRedactArea>& areas, QString* err = nullptr);
};

}  // namespace mg
