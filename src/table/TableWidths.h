#pragma once
//  TableWidths - was die Anzeige je SPALTE von Hand gesetzt bekommen hat:
//  Breite und Formatierung. Beides liegt in der Beidatei des Dokuments
//  (`<datei>.mgedit`, Abschnitte "spaltenbreiten" und "spaltenformate") - eine
//  CSV hat keinen Platz fuer Darstellung.
//  Beide Tabellen-Anbieter teilen sich diese Funktionen, statt den Lese- und
//  Schreibweg zweimal zu fuehren.
#include <QHash>
#include <QString>

namespace mg::table {

//  Je Spalte: Fettdruck, Textfarbe, Hintergrundfarbe. Die Farben stehen als
//  "#rrggbb"; leer heisst „nichts gesetzt" (Themenfarbe bzw. kein Hintergrund).
struct SpaltenFormat {
    bool    fett = false;
    QString farbe;
    QString hintergrund;

    bool leer() const { return !fett && farbe.isEmpty() && hintergrund.isEmpty(); }
    bool operator==(const SpaltenFormat& a) const = default;
};

//  Beide Abschnitte in EINEM Durchgang - getrennt gelesen ginge die Beidatei
//  bei jedem Oeffnen zweimal ueber die Platte und zweimal durch den Entpacker.
void liesSpalten(const QString& datei, QHash<int, int>& breiten,
                 QHash<int, SpaltenFormat>& formate);

//  Ein leerer Bestand entfernt den jeweiligen Abschnitt wieder; sind beide
//  leer und steht sonst nichts drin, faellt die Beidatei ganz weg. Der Rest
//  bleibt stehen - dort liegen die Notizen der Editoren.
bool schreibeSpalten(const QString& datei, const QHash<int, int>& breiten,
                     const QHash<int, SpaltenFormat>& formate);

//  Eine lesbare Textfarbe auf diesem Hintergrund ("#1a1a1a" oder "#f5f5f5").
//  Ohne sie verschwindet der Text einer hellen Spaltenflaeche im dunklen Thema:
//  der Hintergrund kommt aus der Beidatei, die Textfarbe aber aus dem Thema.
QString lesbarAuf(const QString& hintergrund);

//  Grenzen einer von Hand gesetzten Breite; 0 heisst „wieder automatisch".
constexpr int kMinBreite = 40;
constexpr int kMaxBreite = 1600;

}  // namespace mg::table
