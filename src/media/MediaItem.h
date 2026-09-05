#pragma once
#include <QString>
#include <QStringView>
#include <QStringList>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>

// Docx steht VOR Unknown (Viewer-Typtabelle: 5 = DOCX-Editor); Typen werden stets frisch aus der Endung erkannt.
// Folder steht am ENDE, weil die Zahlen in QML festgeschrieben sind - ein Einschub hätte jede Stelle verschoben.
enum class MediaType { Image, Video, Audio, Pdf, Text, Docx, Unknown, Folder };

// Rein lexikalische Pfad-Zerlegung, kein QFileInfo: `fileName()`/`extension()` liegen im heißesten Pfad der
// Galerie (je sichtbarer Zeile, je Filterlauf, je Item jeder Ladecharge), und QFileInfo allozierte jedes Mal neu.
namespace mg {

inline QStringView baseNameView(QStringView path) {
    qsizetype sep = -1;
    for (qsizetype i = path.size() - 1; i >= 0; --i) {
        const QChar c = path.at(i);
        if (c == QLatin1Char('/') || c == QLatin1Char('\\')) { sep = i; break; }
    }
    return path.mid(sep + 1);
}

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
    // Ob ein Datum von Hand gesetzt wurde, wird bei Bedarf aus der DATEI beantwortet (Änderungs- gegen
    // Erstellungsdatum). Das Feld bleibt nur als Platzhalter für den Aufbau der Struktur und wird nie gelesen.
    qint64 fileSize = 0;
    MediaType type = MediaType::Unknown;

    // Ordner-Geltungsbereich als Index in die Bereichstabelle (0 = geöffneter Ordner). Bewusst ein int und kein
    // Pfad-String: Filter und Sortierung brauchen ihn bei JEDEM Vergleich, und nur der Index führt in O(1) zur Kette.
    int scope = 0;

    bool isFolder() const { return type == MediaType::Folder; }

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
        // `eac3`/`ec3`/`mp2`/`aac` stehen hier, weil die App sie SELBST erzeugt ("Audio extrahieren"). Fehlten sie,
        // landete das eigene Ergebnis als unbekannter Typ in der Galerie - nicht abspielbar, ohne Kachelbild.
        static const QSet<QString> audExts = {
            "mp3","flac","wav","ogg","oga","aac","m4a","m4b","wma","opus",
            "aiff","aif","ape","mka","alac","dsf","dff","wv","tta","spx","amr",
            "ac3","eac3","ec3","mp2","dts","mpc","ra","rm","mid","midi",
            "xm","mod","s3m","it"
        };
        static const QSet<QString> txtExts = {
            "txt","md","sql","cpp","c","h","hpp","hxx","cxx","cc","py","js","ts",
            "jsx","tsx","json","xml","html","htm","css","scss","less","yaml","yml",
            "toml","ini","cfg","conf","sh","bash","zsh","bat","cmd","ps1","java",
            "cs","go","rs","rb","php","swift","kt","lua","r","m","f90","cmake","mk",
            "log","csv","tsv","gitignore","gitattributes","env","dockerfile","makefile",
            "qml","qrc","pro","pri","supp",
            // Diese Liste MUSS jede Endung enthalten, die `LanguageTable.cpp` kennt - sonst färbt der Editor eine Sprache,
            // die sich gar nicht öffnen lässt (so passiert mit `.dart` und `.pl`). `tst_mediaitem` vergleicht beide Listen.
            "dart","pl","pm"
        };
        const QString ext = mg::suffixView(path).toString().toLower();
        if (imgExts.contains(ext)) return MediaType::Image;
        if (vidExts.contains(ext)) return MediaType::Video;
        if (audExts.contains(ext)) return MediaType::Audio;
        if (ext == "pdf") return MediaType::Pdf;
        if (ext == "docx") return MediaType::Docx;   // Word-Dokumente (DOCX-Editor)
        if (txtExts.contains(ext)) return MediaType::Text;
        const QString name = mg::baseNameView(path).toString().toLower();
        // Endungslose Textdateien, die in jedem Projekt vorkommen: ohne sie meldet der Viewer "Kein
        // Vorschau-Renderer für diesen Typen" - eine LICENSE ließ sich dadurch gar nicht ansehen.
        static const QSet<QString> textNamen = {
            "makefile", "dockerfile", "license", "licence", "copying",
            "notice", "authors", "contributors", "changelog", "changes",
            "readme", "todo", "install", "version", "manifest"
        };
        if (textNamen.contains(name)) return MediaType::Text;
        return MediaType::Unknown;
    }
};
