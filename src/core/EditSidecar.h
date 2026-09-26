#pragma once
//  EditSidecar - die Beidatei der beiden Editoren (`<dokument>.mgedit`).
//  Derselbe JSON-Baum wie bisher, nur gepackt: 200 Notizen 29.811 -> 2.058 Byte.
//  Ein eigenes Binaerformat waere dabei GROESSER geworden (geschaetzt 53.850 Byte
//  gegen 31.040 bei einem Bestand mit Freihandstrichen).
//  Aufbau: "MGED" · Fassung · Art (0 = roher Text, 1 = Deflate) · Laenge des
//  entpackten Textes (uint32, little endian) · Daten.
#include "core/MGEditBin.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace mg::editsidecar {

//  Was in der Beidatei steht - entweder als Bytes (Art 2, heute geschrieben)
//  oder als JSON-Baum (Art 0/1, alte Dateien). Die Zeiger im `Doku` zeigen in
//  dessen eigenen Puffer, der Inhalt muss also leben, solange gelesen wird.
struct Inhalt {
    mg::mgeb::Doku bin;
    QJsonObject    json;
    bool           istBin = false;
    bool leer() const { return istBin ? !bin.ok() : json.isEmpty(); }
};

//  Der Weg fuer neue Verbraucher: liest beide Arten, ohne JSON zu erzwingen.
//  **Zieht eine alte Beidatei dabei still in die heutige Form um** - sonst
//  bliebe ein Dokument, das man nur ansieht, fuer immer im alten Format.
//  Umgeschrieben wird erst, wenn die neue Fassung IM SPEICHER schon gelesen und
//  gegengeprueft ist; scheitert etwas, bleibt die Datei, wie sie war.
//  `zieheUm = false` fuer Aufrufer, die gleich danach ohnehin schreiben.
Inhalt liesInhalt(const QString& dokument, bool zieheUm = true);

//  Schreibt den fertigen MGEB-Koerper (Art 2).
bool schreibeBin(const QString& dokument, const QByteArray& koerper);

//  Alle Abschnitte der Wurzel in den Schreiber uebernehmen, AUSSER dem eigenen.
//  Drei Teile der App teilen sich eine Beidatei; wer schreibt, darf die Notizen
//  der anderen nicht verlieren. Gibt die Zahl der uebernommenen Abschnitte.
int uebernimmFremde(mg::mgeb::Schreiber& s, const Inhalt& in, const QString& eigener);
//  Wer MEHRERE eigene Abschnitte schreibt, muss sie alle nennen: ein hier
//  vergessener kaeme als fremder mit und staende danach doppelt in der Datei.
int uebernimmFremde(mg::mgeb::Schreiber& s, const Inhalt& in, const QStringList& eigene);

//  Die heutige Beidatei und die JSON-Fassung davor. Gelesen werden beide,
//  geschrieben nur die erste; die alte faellt beim ersten Speichern weg.
QString pfad(const QString& dokument);
QString altPfad(const QString& dokument);

//  Leeres Objekt = keine oder kaputte Beidatei. Nur noch fuer ALTE Dateien -
//  eine im heutigen Format liefert hier nichts.
QJsonObject lies(const QString& dokument);

bool schreibe(const QString& dokument, const QJsonObject& wurzel);

//  Beide Namen; `true`, wenn danach keiner mehr existiert.
bool entferne(const QString& dokument);

//  Traegt der Anfang die Kennung? Der Betrachter fragt es an der DATEI, nicht
//  am Dokument daneben - er bekommt den Pfad der Beidatei selbst.
bool istBeidatei(const QByteArray& roh);

//  Der Inhalt dieser Beidatei als eingerueckter Text - damit sie sich ansehen
//  laesst wie die Ordner-Ablage. Leer, wenn sie sich nicht lesen laesst.
QString lesbar(const QString& beidatei);

}  // namespace mg::editsidecar
