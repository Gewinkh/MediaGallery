#pragma once
//  DatevCsv - die DATEV-Schicht ueber dem allgemeinen Leser (table/DelimitedText.h).
//  Sie kennt nur, was DATEV ausmacht: die Kennung in Zeile 1, die Spaltennamen
//  in Zeile 2, ab Zeile 3 die Buchungen, und die Summen Soll/Haben.
//  NUR LESEND.
#include "table/DelimitedText.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace mg::datev {

using mg::table::Warnung;
using mg::table::Zeile;

struct Datei {
    bool              ok = false;
    QString           fehler;
    QStringList       kopf;         // Zeile 1, alle Felder roh
    QStringList       spalten;      // Zeile 2
    QList<Zeile>      buchungen;    // ab Zeile 3
    QList<Warnung>    warnungen;
    bool              cp1252 = false;
    bool              abgeschnitten = false;

    //  Wird beim Zerlegen mitgerechnet, also im Arbeitsfaden: ueber 50.000
    //  Buchungen zu laufen kostet Zeit, und im GUI-Faden waere das ein Ruckler.
    double            soll = 0.0;
    double            haben = 0.0;
    //  Je Spalte: steht in irgendeiner Buchung etwas darin? 125 Spalten sind
    //  nicht lesbar, die Anzeige zeigt deshalb nur die gefuellten.
    QList<bool>       spalteGefuellt;
};

//  Sieht der Dateianfang nach DATEV aus? Entschieden wird an der KENNUNG in
//  Zeile 1, nicht an der Endung - dieselbe Datei wird als .csv UND als .txt
//  ausgeliefert. Eine Datei ohne Kennung ist eine gewoehnliche Tabelle.
bool looksLikeDatev(const QByteArray& anfang);

//  Rohe Bytes -> Datei. Trennzeichen ist bei DATEV immer `;`.
Datei parse(const QByteArray& raw);

//  Die beiden Spalten der Summen, am NAMEN aus Zeile 2 gesucht; die Position
//  ist nur der Notnagel, falls eine Version anders ueberschreibt.
struct Summenspalten {
    int betrag = -1;
    int sollHaben = -1;
};
Summenspalten summenSpalten(const QStringList& spalten);

//  Soll und Haben ueber die Buchungen in `auswahl` oder, ohne sie, ueber alle.
void summiere(const QList<Zeile>& buchungen, Summenspalten s, const QList<int>* auswahl,
              double* soll, double* haben);

//  Betrag in DATEV-Schreibweise (Komma als Dezimaltrenner, Punkt als
//  Tausenderpunkt) -> Zahl. `ok` bleibt false, wenn nichts Zaehlbares dasteht.
double parseBetrag(QStringView s, bool* ok = nullptr);

}  // namespace mg::datev
