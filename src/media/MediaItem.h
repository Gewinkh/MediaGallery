#pragma once
#include <QString>
#include <QStringView>
#include <QStringList>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>

// Docx steht VOR Unknown (Viewer-Typtabelle: 5 = DOCX-Editor); Typen werden
// stets frisch aus der Endung erkannt — keine numerische Persistenz.
enum class MediaType { Image, Video, Audio, Pdf, Text, Docx, Unknown };

// ─────────────────────────────────────────────────────────────────────────────
//  Rein lexikalische Pfad-Zerlegung (kein QFileInfo, kein Dateisystemzugriff).
//
//  fileName()/extension() liegen im HEISSESTEN Pfad der Galerie: MediaModel::data
//  liefert FileNameRole je sichtbarer Zeile, MediaProxyModel::filterAcceptsRow
//  fragt sie fuer JEDE Zeile bei jedem Filterlauf ab, JsonStorage::applyToItems
//  fuer jedes Item jeder Ladecharge. Ein QFileInfo je Aufruf allozierte dafuer
//  jedes Mal ein QFileInfoPrivate + QFileSystemEntry und parste den Pfad neu.
//  Die Zerlegung ist rein textuell (beide Trenner, wie im uebrigen Projekt) und
//  liefert fuer lokale Pfade dasselbe Ergebnis.
// ─────────────────────────────────────────────────────────────────────────────
namespace mg {

inline QStringView baseNameView(QStringView path) {
    qsizetype sep = -1;
    for (qsizetype i = path.size() - 1; i >= 0; --i) {
        const QChar c = path.at(i);
        if (c == QLatin1Char('/') || c == QLatin1Char('\\')) { sep = i; break; }
    }
    return path.mid(sep + 1);
}

// Endung OHNE Punkt — identisch zu QFileInfo::suffix(): alles nach dem LETZTEN
// Punkt des Dateinamens. Ein fuehrender Punkt zaehlt dabei sehr wohl mit
// (".gitignore" → "gitignore"); genau darauf beruht die Erkennung der
// endungslos wirkenden Textdateien in detectType (txtExts enthaelt "gitignore",
// "gitattributes", "env"). Nur ein Name ganz OHNE Punkt hat keine Endung.
inline QStringView suffixView(QStringView path) {
    const QStringView name = baseNameView(path);
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) return {};
    return name.mid(dot + 1);
}

} // namespace mg

struct MediaItem {
    QString filePath;       // Full path on disk
    QString displayName;    // Shown in UI (may differ from filename)
    QStringList tags;
    QDateTime dateTime;     // Effective date (custom or file date)
    bool hasCustomDate = false;
    qint64 fileSize = 0;
    MediaType type = MediaType::Unknown;

    QString fileName() const { return mg::baseNameView(filePath).toString(); }
    QString extension() const { return mg::suffixView(filePath).toString().toLower(); }
    QString audioFormatLabel() const { return mg::suffixView(filePath).toString().toUpper(); }

    static MediaType detectType(const QString& path) {
        static const QSet<QString> imgExts = {
            "jpg","jpeg","png","gif","bmp","webp","tiff","tif",
            "heic","heif","avif","ico","svg","raw","cr2","nef","arw","dng"
        };
        static const QSet<QString> vidExts = {
            "mp4","mkv","avi","mov","wmv","flv","webm","m4v","mpg","mpeg",
            "3gp","ogv","ts","m2ts","vob","rmvb","asf","divx","xvid"
        };
        static const QSet<QString> audExts = {
            "mp3","flac","wav","ogg","aac","m4a","wma","opus","aiff","aif",
            "ape","mka","alac","dsf","dff","wv","tta","spx","amr","ac3",
            "dts","mpc","ra","rm","mid","midi","xm","mod","s3m","it"
        };
        static const QSet<QString> txtExts = {
            "txt","md","sql","cpp","c","h","hpp","hxx","cxx","cc","py","js","ts",
            "jsx","tsx","json","xml","html","htm","css","scss","less","yaml","yml",
            "toml","ini","cfg","conf","sh","bash","zsh","bat","cmd","ps1","java",
            "cs","go","rs","rb","php","swift","kt","lua","r","m","f90","cmake","mk",
            "log","csv","tsv","gitignore","gitattributes","env","dockerfile","makefile"
        };
        const QString ext = mg::suffixView(path).toString().toLower();
        if (imgExts.contains(ext)) return MediaType::Image;
        if (vidExts.contains(ext)) return MediaType::Video;
        if (audExts.contains(ext)) return MediaType::Audio;
        if (ext == "pdf") return MediaType::Pdf;
        if (ext == "docx") return MediaType::Docx;   // Word-Dokumente (DOCX-Editor)
        if (txtExts.contains(ext)) return MediaType::Text;
        // Extension-less text files (e.g. "Makefile", "Dockerfile")
        const QString name = mg::baseNameView(path).toString().toLower();
        if (name == "makefile" || name == "dockerfile") return MediaType::Text;
        return MediaType::Unknown;
    }
};
