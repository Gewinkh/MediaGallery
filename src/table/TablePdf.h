#pragma once
#include "table/DelimitedText.h"
#include "table/TableFormula.h"
#include "table/TableWidths.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QList>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

// TablePdf - Tabellen als A4-PDF; laeuft im Arbeitsfaden. Kopfzeile auf jeder Seite, zu breite Tabellen werden
// bis zur Mindestschrift verkleinert und danach spaltenweise auf Folgeseiten geteilt.
namespace mg::table {

struct PdfSpalte {
    QString titel;
    int  px = 0;                 // von Hand gesetzte Breite, 0 = gemessen
    bool zahl = false;           // im Druckstil rechtsbuendig
    SpaltenFormat format;
};

struct PdfTeil {
    QString titel;               // ueber der Tabelle, leer = keiner
    QList<PdfSpalte> spalten;
    int zeilen = 0;
    //  Zeile 0..zeilen-1, Spalte = Stelle in `spalten`. Wird im Arbeitsfaden gerufen und darf nur lesen.
    std::function<QString(int, int)> zelle;
};

struct PdfOptionen {
    bool druck = false;          // Graustufen auf weiss mit duennen Linien statt der Farben der Ansicht
    bool quer = false;
    bool gitter = false;         // auch im Original Linien statt Streifen
    int  von = 1;
    int  bis = 0;                // 0 = bis zum Ende
    QFont schrift;               // die Zellschrift der Ansicht
    //  Farben der Ansicht (nur ohne `druck`).
    QColor grund, text, kopfGrund, kopfText;
};

// Leeres `ziel` zaehlt nur. Liefert die Seitenzahl, -1 bei Fehler (`*err`).
int schreibePdf(const QList<PdfTeil>& teile, const PdfOptionen& opt, const QString& ziel,
                QString* err = nullptr, const std::atomic<bool>* abbruch = nullptr);

// Alle Bloecke einer Datei, so wie sie dastehen; `datei` haelt die Zeilen fuer die Lesefunktionen am Leben.
QList<PdfTeil> teileAusDatei(std::shared_ptr<const Datei> datei, const QList<Bereich>& bloecke,
                             std::shared_ptr<const Werte> werte, const QHash<int, int>& breiten,
                             const QHash<int, SpaltenFormat>& formate, bool gruppiert, bool komma);

// Eine Tabellendatei von Platte lesen und schreiben (Galerie). Formeln werden gerechnet, Breiten und
// Formate kommen aus der Beidatei.
bool dateiAlsPdf(const QString& quelle, const PdfOptionen& opt, bool gruppiert, const QString& ziel,
                 QString* err = nullptr, const std::atomic<bool>* abbruch = nullptr);

}  // namespace mg::table
