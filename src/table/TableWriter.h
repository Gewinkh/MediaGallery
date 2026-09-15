#pragma once
//  TableWriter - eine bearbeitete Tabelle zurueck in Bytes.
//  Unberuehrte Datensaetze kommen Byte fuer Byte aus der Grundlage (Zeilenenden,
//  Kodierung, Klammerung); in einem geaenderten bleiben die unveraenderten
//  Felder ebenso roh stehen. Neu gesetzt wird nur, was sich geaendert hat.
#include "table/DelimitedText.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace mg::table {

struct SchreibZeile {
    const Zeile* zeile = nullptr;
    int  herkunft  = -1;        // Datensatz der Grundlage; -1 = neu oder umgebaut
    bool geaendert = false;     // Inhalt kann von der Grundlage abweichen
};

struct SchreibErgebnis {
    bool       ok = false;
    QString    fehler;
    int        fehlerZeile = -1;    // Index in `zeilen`
    QByteArray bytes;
    //  Quellzeile je geschriebenem Datensatz (1-basiert) - die Grundlage fuer
    //  das naechste Speichern.
    QList<int> zeilenNr;
};

//  `grundlage` sind die Bytes, aus denen `grundZeilenNr` (s. `Datei::zeilenNr`)
//  entstand. Passt beides nicht zusammen, wird nichts gebaut.
SchreibErgebnis baueDatei(const QByteArray& grundlage, const QList<int>& grundZeilenNr,
                          QChar trenner, bool cp1252, const QList<SchreibZeile>& zeilen);

//  Ein Feld fuer die Datei. Geklammert wird, wenn der Wert es verlangt (Trenner,
//  Anfuehrungszeichen, Zeilenumbruch) oder `klammern` es vorgibt.
QString feldText(const QString& wert, QChar trenner, bool klammern, const QString& umbruch);

}  // namespace mg::table
