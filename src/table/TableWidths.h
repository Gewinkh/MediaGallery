#pragma once
//  TableWidths - von Hand gesetzte Spaltenbreiten in der Beidatei des Dokuments
//  (`<datei>.mgedit`, Abschnitt "spaltenbreiten"). Die Datei selbst kann sie
//  nicht tragen: eine CSV hat keinen Platz fuer Darstellung.
//  Beide Tabellen-Anbieter teilen sich diese beiden Funktionen, statt den
//  Lese- und Schreibweg zweimal zu fuehren.
#include <QHash>
#include <QString>

namespace mg::table {

//  Spaltennummer -> Breite in Pixeln. Leer, wenn nichts gesetzt ist.
QHash<int, int> liesBreiten(const QString& datei);

//  Ein leerer Bestand entfernt den Abschnitt wieder. Der Rest der Beidatei
//  bleibt stehen - dort liegen die Notizen der Editoren.
bool schreibeBreiten(const QString& datei, const QHash<int, int>& breiten);

//  Grenzen einer von Hand gesetzten Breite; 0 heisst „wieder automatisch".
constexpr int kMinBreite = 40;
constexpr int kMaxBreite = 1600;

}  // namespace mg::table
