#pragma once
#include <QString>
#include <QStringList>

// StartTarget - was "Oeffnen mit" der App mitgibt: ein Ordner, oder eine Datei samt ihrem Ordner.
namespace mg {

struct StartTarget {
    QString folder;   // leer = wie gewohnt den zuletzt geoeffneten Ordner
    QString file;     // leer = nur den Ordner
};

// Das erste Argument, das kein Schalter ist (`args` wie `QCoreApplication::arguments()`, [0] ist das Programm).
// Pfad oder `file://`-Adresse, relativ zum Arbeitsordner; was es nicht gibt, wird uebergangen.
StartTarget parseStartTarget(const QStringList& args, const QString& workingDir);

}  // namespace mg
