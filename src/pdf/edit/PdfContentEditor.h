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

class PdfContentEditor {
public:
    //  Wendet die Ersetzungen an und schreibt `outputPath` (atomar). Liefert
    //  false, wenn irgendeine Vorbedingung (s. Header) nicht SICHER erfüllt ist
    //  ODER nichts ersetzt wurde — dann bleibt `outputPath` ungeschrieben und
    //  der Aufrufer weicht auf den Raster-Export aus. `err` (optional) erhält
    //  einen kurzen Grund.
    static bool editText(const QString& inputPath, const QString& outputPath,
                         const QVector<PdfTextEdit>& edits, QString* err = nullptr);
};

}  // namespace mg
