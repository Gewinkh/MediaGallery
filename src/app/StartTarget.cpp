#include "app/StartTarget.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

namespace mg {

StartTarget parseStartTarget(const QStringList& args, const QString& workingDir) {
    StartTarget t;
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a.isEmpty() || a.startsWith(u'-')) continue;
        //  Dateimanager reichen mit `%U` Adressen, die Kommandozeile Pfade.
        const QUrl url(a);
        QString pfad = url.isLocalFile() ? url.toLocalFile() : a;
        if (QDir::isRelativePath(pfad)) pfad = QDir(workingDir).absoluteFilePath(pfad);
        const QFileInfo fi(QDir::cleanPath(pfad));
        if (fi.isDir()) {
            t.folder = fi.absoluteFilePath();
        } else if (fi.isFile()) {
            t.folder = fi.absolutePath();
            t.file = fi.absoluteFilePath();
        } else {
            continue;
        }
        break;
    }
    return t;
}

}  // namespace mg
