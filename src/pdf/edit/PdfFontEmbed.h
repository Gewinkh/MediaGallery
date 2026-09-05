#pragma once
// Bettet eine installierte TrueType-Schrift ein; vorher ersetzte der Vektor-Export jede Familie
// ohne Standard-14-Entsprechung stillschweigend durch Helvetica. Eingebettet wird das GANZE
// Schriftprogramm - Subsetting müsste glyf/loca/cmap/hmtx konsistent neu bauen, ein Fehler dort ergibt eine unlesbare Datei.

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
