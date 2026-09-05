#pragma once
//  DatevFormat - Katalog der Kopffelder je Formatversion, als Daten.
//  Die Spaltennamen der Buchungen stehen in der Datei selbst (Zeile 2); benannt
//  werden muss nur der Dateikopf. Benannt ist hier NUR, was sich an der Datei
//  selbst ablesen laesst - die uebrigen Felder erscheinen als "Feld N", bis der
//  offizielle Katalog vorliegt.
#include "core/Strings.h"

#include <QList>

namespace mg::datev {

struct KopfFeld {
    int       nummer;      // 1-basiert, wie im Dateikopf gezaehlt
    StringKey name;
};

//  Fuer eine unbekannte Version wird die zuletzt bekannte genommen: ein neuer
//  Versionssprung soll die Anzeige nicht auf "Feld N" zurueckfallen lassen.
QList<KopfFeld> kopfFelder(int version);

//  `20240130140440439` -> "30.01.2024 14:04:40". Leer, wenn es kein solcher
//  Zeitstempel ist.
QString erzeugtAmLesbar(const QString& roh);

}  // namespace mg::datev
