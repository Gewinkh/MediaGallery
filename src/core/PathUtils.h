#pragma once
// Gemeinsame, zustandslose Pfad-Helfer; zentralisiert `toLocalPath`, das zuvor byte-identisch in drei
// Controllern dupliziert war. Header-only - kein zusätzliches Kompilat, keine Verlinkung nötig.
#include <QString>
#include <QUrl>
#include <QLatin1String>

namespace mg {

// Begleitdateien: <Ordner>.json, <datei>.mgedit.json, <datei>.bak. Eine Stelle fuer die
// Regel, sonst filtern Galerie und Dateiwaehler unterschiedlich und der Nutzer saehe
// dieselbe Datei einmal so und einmal so.
inline bool isCompanionFile(const QString& fileName,
                            const QString& folderSidecar = QString()) {
    if (!folderSidecar.isEmpty() && fileName == folderSidecar)
        return true;
    return fileName.endsWith(QLatin1String(".mgedit.json"), Qt::CaseInsensitive)
        || fileName.endsWith(QLatin1String(".bak"), Qt::CaseInsensitive);
}

// Ordnerpfad OHNE abschließenden Trenner: bei "/pfad/ordner/" liefert `QFileInfo::fileName()` einen LEERSTRING,
// daraus wurde der Sidecar-Name ".json" - und die Ordner-JSON stand als Kachel in der Galerie.
inline QString normalizedFolder(const QString& folderPath) {
    QString n = folderPath;
    while (n.size() > 1 && (n.endsWith(QLatin1Char('/')) || n.endsWith(QLatin1Char('\\'))))
        n.chop(1);
    return n;
}

//  Name der Ordner-JSON eines Ordners: „<Ordnername>.json".
inline QString folderSidecarName(const QString& folderPath) {
    QString n = folderPath;
    while (n.endsWith(QLatin1Char('/')) || n.endsWith(QLatin1Char('\\')))
        n.chop(1);
    const int cut = qMax(n.lastIndexOf(QLatin1Char('/')), n.lastIndexOf(QLatin1Char('\\')));
    const QString base = (cut >= 0) ? n.mid(cut + 1) : n;
    return base.isEmpty() ? QString() : base + QStringLiteral(".json");
}

// Wandelt eine "file:"-URL in einen lokalen Dateipfad um; ein bereits lokaler
// Pfad wird unveraendert zurueckgegeben.
inline QString toLocalPath(const QString& s) {
    if (s.startsWith(QLatin1String("file:")))
        return QUrl(s).toLocalFile();
    return s;
}

} // namespace mg
