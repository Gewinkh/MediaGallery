#pragma once
// Gemeinsame, zustandslose Pfad-Helfer; zentralisiert `toLocalPath`, das zuvor byte-identisch in drei
// Controllern dupliziert war. Header-only - kein zusätzliches Kompilat, keine Verlinkung nötig.
#include <QString>
#include <QUrl>
#include <QLatin1String>

namespace mg {

// Eine Stelle fuer die Regel, sonst filtern Galerie und Dateiwaehler
// unterschiedlich und dieselbe Datei erschiene einmal so und einmal so.
inline bool isCompanionFile(const QString& fileName,
                            const QString& folderSidecar = QString()) {
    if (!folderSidecar.isEmpty() && fileName == folderSidecar)
        return true;
    //  Die alte Ablage bleibt versteckt, bis sie beim ersten Speichern weg ist.
    if (!folderSidecar.isEmpty() && folderSidecar.endsWith(QLatin1String(".mgstore"))) {
        QString alt = folderSidecar;
        alt.chop(8);
        if (fileName == alt + QLatin1String(".json")) return true;
    }
    //  JEDE `.mgstore` ist die eigene Ablage, nicht nur die zum aktuellen
    //  Ordnernamen: nach einem Umbenennen heisst sie noch wie der alte Ordner
    //  und stuende sonst als Kachel da.
    return fileName.endsWith(QLatin1String(".mgstore"), Qt::CaseInsensitive)
        || fileName.endsWith(QLatin1String(".mgedit"), Qt::CaseInsensitive)
        || fileName.endsWith(QLatin1String(".mgedit.json"), Qt::CaseInsensitive)
        || fileName.endsWith(QLatin1String(".bak"), Qt::CaseInsensitive);
}

// Ordnerpfad OHNE abschließenden Trenner: bei "/pfad/ordner/" liefert `QFileInfo::fileName()` einen LEERSTRING,
// daraus wurde der Sidecar-Name ".mgstore" - und die Ablage stand als Kachel in der Galerie.
inline QString normalizedFolder(const QString& folderPath) {
    QString n = folderPath;
    while (n.size() > 1 && (n.endsWith(QLatin1Char('/')) || n.endsWith(QLatin1Char('\\'))))
        n.chop(1);
    return n;
}

inline QString folderSidecarName(const QString& folderPath) {
    QString n = folderPath;
    while (n.endsWith(QLatin1Char('/')) || n.endsWith(QLatin1Char('\\')))
        n.chop(1);
    const int cut = qMax(n.lastIndexOf(QLatin1Char('/')), n.lastIndexOf(QLatin1Char('\\')));
    const QString base = (cut >= 0) ? n.mid(cut + 1) : n;
    return base.isEmpty() ? QString() : base + QStringLiteral(".mgstore");
}

// Wandelt eine "file:"-URL in einen lokalen Dateipfad um; ein bereits lokaler
// Pfad wird unveraendert zurueckgegeben.
inline QString toLocalPath(const QString& s) {
    if (s.startsWith(QLatin1String("file:")))
        return QUrl(s).toLocalFile();
    return s;
}

} // namespace mg
