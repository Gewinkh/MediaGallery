#pragma once
//  TableFilter - welche Zeilen stehen in der Anzeige, und in welcher Folge.
//  Filter und Sortierung zusammen; beide Tabellen-Controller benutzen es. Wie
//  das Sortieren aendert auch der Filter die Datei nicht.
#include "table/DelimitedText.h"
#include "table/TableSearch.h"
#include "table/TableSort.h"

#include <QList>
#include <QObject>
#include <QRunnable>
#include <QVariant>
#include <atomic>
#include <functional>
#include <memory>

namespace mg::table {

struct FilterRegel {
    int          spalte = -1;       // -1 = irgendeine gezeigte Spalte
    QString      text;
    SuchOptionen opt;
    bool         monatZuerst = false;   // Schraegstrich-Datum als MM/TT/JJJJ
    bool aktiv() const { return !text.isEmpty(); }
};

//  Was der Filtertext meint. Kein Ausdruck ist gewoehnlicher Text (enthaelt).
//    Zahl:   >200  <200  >=200  <=200  100->200
//    Datum:  2025-01-01+ (ab)  2025-01-01- (bis)  2025-01->2025-04 (Spanne),
//            dazu > < >= <= mit einem Datum; Jahr und Monat allein gelten ganz.
//  `->` statt eines Bindestrichs, weil der im Datum schon trennt.
struct FilterAusdruck {
    enum class Art { Text, Zahl, Datum };
    Art    art = Art::Text;
    double zahlVon = 0.0;
    double zahlBis = 0.0;
    bool   hatVon = false;
    bool   hatBis = false;
    bool   vonOffen = false;        // > statt >=
    bool   bisOffen = false;        // < statt <=
    int    datumVon = 0;            // JJJJMMTT, einschliesslich
    int    datumBis = 99999999;
};
FilterAusdruck deuteFilter(QStringView text, bool monatZuerst);
bool trifftAusdruck(const FilterAusdruck& a, const QString& zelle, bool monatZuerst);
//  Eine Zelle als Zahl fuer den Filter: wie beim Sortieren, dazu ohne Leerzeichen
//  und Waehrungszeichen - `200,00 €` soll bei `>100` mitkommen.
bool filterZahl(QStringView text, double* wert);

//  Die Zeilen in [von, bis), die der Regel entsprechen, als ABSOLUTE Nummern in
//  Dateireihenfolge. `spalten` wie bei `suche`: ausgeblendete zaehlen nicht.
QList<int> filtere(const QList<Zeile>& zeilen, int von, int bis, const FilterRegel& regel,
                   const QList<bool>* spalten = nullptr,
                   const std::atomic<bool>* abbruch = nullptr);

struct Ordnungsauftrag {
    int          von = 0;
    int          bis = 0;
    FilterRegel  filter;
    QList<bool>  spalten;           // leer = alle
    int          sortSpalte = -1;
    SortRichtung richtung = SortRichtung::Keine;
    bool         monatZuerst = false;
};

//  Filtern, dann sortieren. `aktiv` ist false, wenn weder das eine noch das
//  andere greift - dann gilt die Dateireihenfolge, und die Liste ist leer. Eine
//  leere Liste bei `aktiv` heisst dagegen: kein Treffer.
QList<int> ordne(const QList<Zeile>& zeilen, const Ordnungsauftrag& auftrag,
                 const std::atomic<bool>* abbruch, bool* aktiv);

//  Der Lauf im Arbeitsfaden, Muster wie `SortTask`. `zusatz` rechnet im selben
//  Faden etwas ueber die Auswahl (die DATEV-Summen) - im GUI-Faden waeren das
//  bei 50.000 Buchungen zwei volle Durchgaenge.
class OrdnungTask : public QRunnable {
public:
    using Zusatz = std::function<QVariant(const QList<Zeile>&, const QList<int>*)>;
    using Zurueck = std::function<void(QList<int>, bool, QVariant)>;

    OrdnungTask(QObject* owner, std::shared_ptr<const void> anker,
                const QList<Zeile>* zeilen, Ordnungsauftrag auftrag,
                std::shared_ptr<std::atomic<bool>> abbruch, Zurueck zurueck,
                Zusatz zusatz = {});

    void run() override;

private:
    QObject* m_owner;
    std::shared_ptr<const void> m_anker;
    const QList<Zeile>* m_zeilen;
    Ordnungsauftrag m_auftrag;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    Zurueck m_zurueck;
    Zusatz  m_zusatz;
};

}  // namespace mg::table
