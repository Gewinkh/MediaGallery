#include "core/FolderImages.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QUrl>
#include <QVariantMap>

namespace mg {

QVariantList folderImages(const QString& fileOrFolder, int maxCount,
                          bool includeNonImages) {
    QVariantList out;
    if (fileOrFolder.isEmpty()) return out;

    const QFileInfo in(fileOrFolder);
    const QString dirPath = in.isDir() ? in.absoluteFilePath() : in.absolutePath();
    if (dirPath.isEmpty()) return out;

    QStringList filters;
    const auto fmts = QImageReader::supportedImageFormats();
    filters.reserve(fmts.size() + 1);
    for (const QByteArray& f : fmts) {
        const QString ext = QString::fromLatin1(f).toLower();
        //  Ob „pdf" in der Liste steht, hängt an den installierten Qt-Plugins —
        //  die Aufnahme darf nicht davon abhängen, sondern von der Absicht des
        //  Aufrufers. Deshalb hier IMMER heraus und unten gezielt wieder rein.
        if (ext == QLatin1String("pdf")) continue;
        filters << QStringLiteral("*.") + ext;
    }
    if (includeNonImages)
        filters << QStringLiteral("*.pdf");

    QDir d(dirPath);
    const QFileInfoList files = d.entryInfoList(filters, QDir::Files | QDir::Readable,
                                                QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : files) {
        if (out.size() >= maxCount) break;
        QVariantMap m;
        m.insert(QStringLiteral("name"), fi.fileName());
        m.insert(QStringLiteral("url"),
                 QUrl::fromLocalFile(fi.absoluteFilePath()).toString());
        out.append(m);
    }
    return out;
}

}   // namespace mg
