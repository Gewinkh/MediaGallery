#pragma once
//  EditSidecar - die Beidatei der beiden Editoren (`<dokument>.mgedit`).
//  Derselbe JSON-Baum wie bisher, nur gepackt: 200 Notizen 29.811 -> 2.058 Byte.
//  Ein eigenes Binaerformat waere dabei GROESSER geworden (geschaetzt 53.850 Byte
//  gegen 31.040 bei einem Bestand mit Freihandstrichen).
//  Aufbau: "MGED" · Fassung · Art (0 = roher Text, 1 = Deflate) · Laenge des
//  entpackten Textes (uint32, little endian) · Daten.
#include <QJsonObject>
#include <QString>

namespace mg::editsidecar {

//  Die heutige Beidatei und die JSON-Fassung davor. Gelesen werden beide,
//  geschrieben nur die erste; die alte faellt beim ersten Speichern weg.
QString pfad(const QString& dokument);
QString altPfad(const QString& dokument);

//  Leeres Objekt = keine oder kaputte Beidatei.
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
