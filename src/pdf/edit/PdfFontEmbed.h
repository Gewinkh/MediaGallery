#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfFontEmbed.h - eine installierte TrueType-Schrift in ein PDF einbetten
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Der Vektor-Export kannte bisher nur die 14 Standard-PDF-Schriften. Eine
//  Notiz in einer anderen Familie wurde dabei STILLSCHWEIGEND durch Helvetica
//  ersetzt - die exportierte Datei sah anders aus als der Bildschirm. Mit
//  dieser Einheit reist die tatsächliche Schrift mit.
//
//  WAS ERZEUGT WIRD
//  ────────────────
//  Ein vollständiges TrueType-Schriftprogramm (`sfnt`), aus den Tabellen der
//  installierten Schrift wieder zusammengesetzt (`QRawFont::fontTable`), plus
//  die zugehörigen PDF-Objekte: Font-Dict (`/Subtype /TrueType`,
//  `/WinAnsiEncoding`), `/FontDescriptor` und `/FontFile2`.
//  `/Widths` stammen aus den echten Vorschubbreiten der Schrift.
//
//  BEWUSST NICHT GETAN: Teilmengen-Bildung (Subsetting). Eingebettet wird das
//  GANZE Schriftprogramm. Subsetting müsste `glyf`/`loca`/`cmap`/`hmtx`
//  konsistent neu aufbauen - ein eigenes Vorhaben, bei dem ein Fehler eine
//  unlesbare Datei erzeugt. Der Preis ist Dateigröße; deshalb bettet der
//  Aufrufer NUR ein, wenn die Familie wirklich keine Standard-14-Entsprechung
//  hat (s. `needsEmbedding`).
//
//  Schlägt irgendetwas fehl (Tabellen nicht lesbar, Schrift nicht gefunden),
//  liefert `build` false - der Aufrufer weicht dann auf die Ersetzung bzw. den
//  Raster-Export aus, statt eine kaputte Datei zu schreiben.
//
//  ABHÄNGIGKEITEN: Qt6::Core + Qt6::Gui (QRawFont). Kein Q_OBJECT/moc.
// ══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <QString>
#include <QVector>

namespace mg {

struct EmbeddedFont {
    QByteArray psName;        // PostScript-tauglicher Name (ohne Leerzeichen)
    QByteArray sfnt;          // vollständiges TrueType-Programm
    QVector<int> widths;      // Breiten 1/1000 em für die Codes 32…255
    int   firstChar = 32;
    int   flags     = 32;     // /Flags des Deskriptors (32 = nichtsymbolisch)
    qreal ascent = 0, descent = 0, capHeight = 0, italicAngle = 0;
    qreal bbox[4] = { 0, 0, 1000, 1000 };
    bool  valid = false;
};

class PdfFontEmbed {
public:
    //  Braucht diese Familie überhaupt eine Einbettung? False für alles, was
    //  eine echte Entsprechung unter den 14 Standardschriften hat
    //  (Helvetica/Arial, Times, Courier und die üblichen Synonyme).
    static bool needsEmbedding(const QString& family);

    //  Baut das Schriftprogramm und die Metriken für `family` (+ Fett/Kursiv).
    static bool build(const QString& family, bool bold, bool italic,
                      EmbeddedFont* out, QString* err = nullptr);
};

} // namespace mg
