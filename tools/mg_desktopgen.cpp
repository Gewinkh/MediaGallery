// mg_desktopgen - schreibt beim Bau die `.desktop`-Datei der Linux-Installation (s. src/app/DesktopEntry).
// Aufruf: mg_desktopgen <ziel.desktop> <programm> <icon>
#include "app/DesktopEntry.h"

#include <QCoreApplication>
#include <QSaveFile>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() != 4) {
        qWarning("Aufruf: mg_desktopgen <ziel.desktop> <programm> <icon>");
        return 2;
    }
    QStringList ohneTyp;
    mg::desktopMimeTypes(&ohneTyp);
    if (!ohneTyp.isEmpty())
        qInfo("mg_desktopgen: ohne MIME-Typ (nicht eintragbar): %s", qPrintable(ohneTyp.join(u' ')));
    QSaveFile f(a.at(1));
    const QByteArray inhalt = mg::desktopEntry(a.at(2), a.at(3)).toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(inhalt) != inhalt.size() || !f.commit()) return 1;
    return 0;
}
