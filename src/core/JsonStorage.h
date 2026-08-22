#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QColor>
#include <QList>
#include "media/MediaItem.h"
#include "tags/TagCategory.h"

struct TagInfo {
    QString name;
    QColor  color;
};

class JsonStorage : public QObject {
    Q_OBJECT
public:
    explicit JsonStorage(QObject* parent = nullptr);

    // Load/save for a folder
    void loadFolder(const QString& folderPath);
    void saveFolder(const QString& folderPath);
    void saveCurrentFolder();

    // Per-file metadata (displayName is NOT persisted - derived from filename)
    QStringList getTags(const QString& fileName) const;
    void        setTags(const QString& fileName, const QStringList& tags);


    // Text colour this file uses when exported to PDF (text editor "-> PDF").
    // Invalid colour == no own choice; the caller then falls back to the global
    // default in AppSettings.
    QColor textPdfColor(const QString& fileName) const;
    void   setTextPdfColor(const QString& fileName, const QColor& color);
    void   clearTextPdfColor(const QString& fileName);

    // Global tag registry
    QHash<QString, QColor> tagColors() const { return m_tagColors; }
    QColor tagColor(const QString& tag) const;
    void   setTagColor(const QString& tag, const QColor& color);
    void   ensureTagRegistered(const QString& tag);

    QStringList allTags() const;

    // Apply loaded data to MediaItem list
    void applyToItems(QVector<MediaItem>& items) const;

    // Update after rename
    void renameFile(const QString& oldName, const QString& newName);
    //  Persistierte Metadaten (Tags/Datum) einer gelöschten Datei entsorgen.
    void removeFile(const QString& fileName);

    // Tag management
    void deleteTag(const QString& tag);

    // Categories
    QList<TagCategory>&       categoriesRef()       { return m_categories; }
    const QList<TagCategory>& categoriesRef() const { return m_categories; }

private:
    QString m_folderPath;
    QString m_jsonPath;

    struct FileMeta {
        QStringList tags;
        //  KEIN Datum: das Änderungs- und das Erstellungsdatum stehen an der
        //  DATEI selbst (Festlegung des Nutzers 2026-08-21). Dieselbe Angabe
        //  zweimal zu führen hieße nur, dass sie auseinanderläuft, sobald
        //  jemand die Datei außerhalb der App anfasst.
        QColor      textPdfColor;   // invalid == follow the global default
    };

    QHash<QString, FileMeta> m_fileMeta;
    QHash<QString, QColor>   m_tagColors;
    QList<TagCategory>       m_categories;

    //  ── Zwei Fassungen DERSELBEN Datei ──────────────────────────────────────
    //  Seit dem Zwei-Fenster-Modus kann derselbe Ordner zweimal offen sein; dann
    //  hält jede Hälfte ihre eigene Fassung dieses Sidecars im Speicher. Wer
    //  zuletzt schreibt, würde die Änderung der anderen Seite überschreiben.
    //  Deshalb merkt sich das Objekt den Stand der Datei beim Lesen und
    //  Schreiben und übernimmt vor dem Schreiben fremde Änderungen (dasselbe
    //  schützt gegen Änderungen von außen).
    QDateTime m_diskMTime;
    qint64    m_diskSize = -1;
    void noteDiskStamp(const QString& path);
    bool diskChangedSince(const QString& path) const;
    void mergeForeignChanges(const QString& path);

    static QColor randomTagColor();

    // Category JSON helpers
    static QJsonObject categoryToJson(const TagCategory& cat);
    static TagCategory categoryFromJson(const QJsonObject& obj);

    // Converter - ready for cleanup
    // Load old tag-centric format: { "tags": { "TagName": { "color":"#...", "files":[...] } } }

    // Load new file-centric format: { "files": { "f.jpg": { "tags":[...], "date":"..." } }, "tagColors":{...} }
    void loadNewFormat(const QJsonObject& root);
};
