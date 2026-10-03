#include "app/DesktopEntry.h"

#include "media/MediaItem.h"

#include <QMimeDatabase>

namespace mg {

QStringList desktopMimeTypes(QStringList* ohneTyp) {
    const QMimeDatabase db;
    QStringList out;
    for (const QString& e : MediaItem::supportedExtensions()) {
        //  Nur Typen, die zur Art der Endung passen: `.pm` ist auch PageMaker, `.tsx` auch ein Kachelsatz -
        //  eingetragen boete sich die App fuer fremde Dateien an.
        const QString name = QStringLiteral("x.") + e;
        const QMimeType bester = db.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
        QString art;
        bool nurBester = false;
        switch (MediaItem::detectType(name)) {
        case MediaType::Image: art = QStringLiteral("image/"); break;
        case MediaType::Video: art = QStringLiteral("video/"); break;
        case MediaType::Audio: art = QStringLiteral("audio/"); break;
        case MediaType::Pdf:
        case MediaType::Docx:  nurBester = true; break;
        default: break;
        }
        int neu = 0;
        for (const QMimeType& t : db.mimeTypesForFileName(name)) {
            if (!t.isValid() || t.isDefault()) continue;
            //  Bei Text sogar ohne Ausnahme fuer den besten Treffer: fuer `.pm` und `.tsx` IST er der falsche.
            //  `text/x-shellscript` stammt in Qts Datenbank NICHT von `text/plain` ab - deshalb auch `text/`.
            const bool passt = nurBester ? t == bester
                : art.isEmpty() ? (t.name().startsWith(QStringLiteral("text/")) || t.inherits(QStringLiteral("text/plain")))
                                : (t == bester || t.name().startsWith(art));
            if (!passt) continue;
            out.append(t.name());
            ++neu;
        }
        if (neu == 0 && ohneTyp) ohneTyp->append(e);
    }
    out << QStringLiteral("text/plain") << QStringLiteral("inode/directory");
    out.sort();
    out.removeDuplicates();
    return out;
}

QString desktopEntry(const QString& exec, const QString& icon) {
    QString s;
    s += QStringLiteral("[Desktop Entry]\n");
    s += QStringLiteral("Type=Application\n");
    s += QStringLiteral("Name=MediaGallery\n");
    s += QStringLiteral("GenericName=Media Gallery\n");
    s += QStringLiteral("GenericName[de]=Mediengalerie\n");
    s += QStringLiteral("Comment=Browse, tag and view local media folders\n");
    s += QStringLiteral("Comment[de]=Lokale Medienordner durchstöbern, verschlagworten und ansehen\n");
    //  %U: der Dateimanager reicht Adressen; mehrere auf einmal kommen in EINEN Prozess, geoeffnet wird die erste.
    s += QStringLiteral("Exec=") + exec + QStringLiteral(" %U\n");
    s += QStringLiteral("Icon=") + icon + QStringLiteral("\n");
    s += QStringLiteral("Terminal=false\n");
    s += QStringLiteral("StartupNotify=true\n");
    s += QStringLiteral("StartupWMClass=MediaGallery\n");
    s += QStringLiteral("Categories=AudioVideo;Graphics;Viewer;Utility;\n");
    s += QStringLiteral("Keywords=gallery;media;photo;video;audio;pdf;tags;\n");
    s += QStringLiteral("MimeType=") + desktopMimeTypes().join(u';') + QStringLiteral(";\n");
    return s;
}

}  // namespace mg
