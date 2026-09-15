#pragma once
//  TableSort - die Anzeigereihenfolge einer Tabelle; beide Tabellen-Controller
//  benutzen dieselbe Maschine. Sortiert wird NICHT die Datei: erzeugt wird nur
//  eine Reihenfolge, durch die die Anzeige liest. Nur so bleibt die Zusage
//  bestehen, dass eine Tabelle gezeigt und nie umgeschrieben wird.
#include "table/DelimitedText.h"

#include <QList>
#include <QObject>
#include <QRunnable>
#include <atomic>
#include <functional>
#include <memory>

namespace mg::table {

enum class SortRichtung { Keine, Auf, Ab };

//  Eine Zelle als Zahl. Zwei Schreibweisen kommen vor; kommen `,` und `.` beide
//  vor, ist das HINTERE das Dezimalzeichen (1.234,56 wie 1,234.56 werden damit
//  beide zu 1234,56). Steht nur eines da, gilt es als Dezimalzeichen - "1.234"
//  ist dann 1,234. Alles andere - Datumsangaben, Masse, Text mit Ziffern -
//  faellt durch und wird als Text sortiert.
double alsZahl(const QString& text, bool* ok);

//  Traegt die Spalte durchweg Zahlen? Ueber eine Probe der ersten Zeilen, nicht
//  ueber die ganze Datei. Leere Zellen zaehlen nicht.
bool spalteIstZahl(const QList<Zeile>& zeilen, int von, int bis, int spalte);

//  Ein Datum als Spanne [von, bis] in JJJJMMTT. Erkannt werden JJJJ-MM-TT,
//  TT.MM.JJJJ und die Schraegstrich-Form, deren Reihenfolge `monatZuerst`
//  festlegt - ohne die Angabe liesse 03/04/2025 zwei Lesarten zu. `teil` laesst
//  Jahr und Monat allein zu (JJJJ, JJJJ-MM, MM.JJJJ, MM/JJJJ) - fuer den Filter;
//  in einer Zelle zaehlt nur ein ganzer, gueltiger Tag.
bool datumsSpanne(QStringView text, bool monatZuerst, bool teil, int* von, int* bis);
bool alsDatum(const QString& text, int* schluessel, bool monatZuerst = false);
bool spalteIstDatum(const QList<Zeile>& zeilen, int von, int bis, int spalte,
                    bool monatZuerst = false);

//  Die Anzeigereihenfolge fuer [von, bis) als ABSOLUTE Zeilennummern; leer =
//  Dateireihenfolge. Stabil, und leere Zellen stehen in BEIDEN Richtungen am
//  Ende - eine Luecke ist kein kleiner Wert. `auswahl` sortiert nur diese
//  Zeilen (das Ergebnis eines Filters) statt des ganzen Bereichs.
QList<int> sortiere(const QList<Zeile>& zeilen, int von, int bis, int spalte,
                    SortRichtung richtung,
                    const std::atomic<bool>* abbruch = nullptr,
                    const QList<int>* auswahl = nullptr,
                    bool monatZuerst = false);

//  Der Sortierlauf im Arbeitsfaden, Muster wie `SuchTask`. Gemessen an 100.000
//  Zeilen: 55 ms als Text, 26 ms als Zahl - im GUI-Faden waere jeder Klick auf
//  eine Spalte ein Ruckler.
class SortTask : public QRunnable {
public:
    SortTask(QObject* owner, std::shared_ptr<const void> anker,
             const QList<Zeile>* zeilen, int von, int bis, int spalte,
             SortRichtung richtung, std::shared_ptr<std::atomic<bool>> abbruch,
             std::function<void(QList<int>)> zurueck);

    void run() override;

private:
    QObject* m_owner;
    std::shared_ptr<const void> m_anker;
    const QList<Zeile>* m_zeilen;
    int m_von;
    int m_bis;
    int m_spalte;
    SortRichtung m_richtung;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    std::function<void(QList<int>)> m_zurueck;
};

}  // namespace mg::table
