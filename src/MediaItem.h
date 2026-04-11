#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QPixmap>
#include <QFileInfo>
#include <QSet>

enum class MediaType { Image, Video, Audio, Unknown };

struct MediaItem {
    QString filePath;       // Full path on disk
    QString displayName;    // Shown in UI (may differ from filename)
    QStringList tags;
    QDateTime dateTime;     // Effective date (custom or file date)
    bool hasCustomDate = false;
    qint64 fileSize = 0;
    MediaType type = MediaType::Unknown;
    int width = 0;
    int height = 0;
    double duration = 0.0; // seconds, for videos

    // Thumbnail (loaded lazily)
    QPixmap thumbnail;
    bool thumbnailLoaded = false;

    QString fileName() const { return QFileInfo(filePath).fileName(); }
    QString baseName() const { return QFileInfo(filePath).completeBaseName(); }
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
        QString ext = QFileInfo(path).suffix().toLower();
        if (imgExts.contains(ext)) return MediaType::Image;
        if (vidExts.contains(ext)) return MediaType::Video;
        if (audExts.contains(ext)) return MediaType::Audio;
        return MediaType::Unknown;
    }

    bool isImage() const { return type == MediaType::Image; }
    bool isVideo() const { return type == MediaType::Video; }
    bool isAudio() const { return type == MediaType::Audio; }

    bool matchesTagFilter(const QStringList& filterTags, bool andMode) const {
        if (filterTags.isEmpty()) return true;
        if (andMode) {
            for (const auto& t : filterTags)
                if (!tags.contains(t)) return false;
            return true;
        } else {
            for (const auto& t : filterTags)
                if (tags.contains(t)) return true;
            return false;
        }
    }

    // Extended filter with TagFilterMode enum values
    // mode: 0=OR, 1=AND, 2=ONLY, 3=INCLUSIVE
    bool matchesTagFilterMode(const QStringList& filterTags, int mode) const {
        if (filterTags.isEmpty()) return true;
        switch (mode) {
        case 0: // OR
            for (const auto& t : filterTags)
                if (tags.contains(t)) return true;
            return false;
        case 1: // AND
            for (const auto& t : filterTags)
                if (!tags.contains(t)) return false;
            return true;
        case 2: // ONLY – item must contain ALL filterTags and NO other tags
            // All filterTags must be present
            for (const auto& t : filterTags)
                if (!tags.contains(t)) return false;
            // No extra tags allowed outside the filterTags set
            for (const auto& t : tags)
                if (!filterTags.contains(t)) return false;
            return true;
        case 3: // INCLUSIVE – item contains the tag (+ anything else is fine)
            for (const auto& t : filterTags)
                if (tags.contains(t)) return true;
            return false;
        default: return true;
        }
    }
};
