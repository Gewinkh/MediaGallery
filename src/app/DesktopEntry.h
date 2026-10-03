#pragma once
#include <QString>
#include <QStringList>

// DesktopEntry - die `.desktop`-Datei der Linux-Installation. Die Dateitypen kommen aus denselben Endungen, mit
// denen die App ihre Dateien erkennt (`MediaItem::supportedExtensions`), dazu Ordner.
namespace mg {

inline constexpr char kAppId[] = "io.github.gewinkh.MediaGallery";

// MIME-Typen fuer `MimeType=`; `ohneTyp` sammelt Endungen, die die Typ-Datenbank nicht kennt.
QStringList desktopMimeTypes(QStringList* ohneTyp = nullptr);

// Der ganze Eintrag. `exec` ist das Programm, `icon` der Name in hicolor.
QString desktopEntry(const QString& exec, const QString& icon);

}  // namespace mg
