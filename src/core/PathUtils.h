#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  PathUtils.h — gemeinsame, zustandslose Pfad-Helfer.
//
//  Zentralisiert `toLocalPath`, das zuvor byte-identisch in ViewerController,
//  PdfTextController und PdfThumbnailProvider dupliziert war. Header-only
//  (inline) → kein zusaetzliches Kompilat, keine Verlinkung noetig.
// ─────────────────────────────────────────────────────────────────────────────
#include <QString>
#include <QUrl>
#include <QLatin1String>

namespace mg {

// ─────────────────────────────────────────────────────────────────────────────
//  Begleitdateien der App — nichts, was der Nutzer als „Medium" ansieht:
//    • `<Ordner>.json`      Tags, Kategorien, Datum, PDF-Schriftfarbe
//    • `<datei>.mgedit.json` Notizen/Zeichnungen des PDF- bzw. Bild-Editors
//    • `<datei>.bak`         Sicherungskopie des DOCX-Editors
//  Sie werden standardmäßig ausgeblendet; der Schalter „Alle Dateien anzeigen"
//  (ISettings::showAllFiles) macht sie sichtbar.
//
//  EINE Stelle für die Regel: Galerie (MediaModel) und Dateiwähler
//  (FileBrowseModel) filtern sonst unterschiedlich, und der Nutzer sähe dieselbe
//  Datei einmal so und einmal so.
//  folderSidecar = Name der Ordner-JSON (leer = unbekannt, dann nur die Endungen).
inline bool isCompanionFile(const QString& fileName,
                            const QString& folderSidecar = QString()) {
    if (!folderSidecar.isEmpty() && fileName == folderSidecar)
        return true;
    return fileName.endsWith(QLatin1String(".mgedit.json"), Qt::CaseInsensitive)
        || fileName.endsWith(QLatin1String(".bak"), Qt::CaseInsensitive);
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
