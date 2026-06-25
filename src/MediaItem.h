#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>

enum class MediaType { Image, Video, Audio, Pdf, Text, Unknown };

struct MediaItem {
    QString filePath;       // Full path on disk
    QString displayName;    // Shown in UI (may differ from filename)
    QStringList tags;
    QDateTime dateTime;     // Effective date (custom or file date)
    bool hasCustomDate = false;
    qint64 fileSize = 0;
    MediaType type = MediaType::Unknown;

    QString fileName() const { return QFileInfo(filePath).fileName(); }
    QString extension() const { return QFileInfo(filePath).suffix().toLower(); }
    QString audioFormatLabel() const { return QFileInfo(filePath).suffix().toUpper(); }

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
        QString ext = QFileInfo(path).suffix().toLower();
        if (imgExts.contains(ext)) return MediaType::Image;
        if (vidExts.contains(ext)) return MediaType::Video;
        if (audExts.contains(ext)) return MediaType::Audio;
        if (ext == "pdf") return MediaType::Pdf;
        if (txtExts.contains(ext)) return MediaType::Text;
        // Extension-less text files (e.g. "Makefile", "Dockerfile")
        const QString name = QFileInfo(path).fileName().toLower();
        if (name == "makefile" || name == "dockerfile") return MediaType::Text;
        return MediaType::Unknown;
    }
};
