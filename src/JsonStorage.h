#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QColor>
#include <QList>
#include "MediaItem.h"
#include "TagCategory.h"

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

    // Per-file metadata (displayName is NOT persisted — derived from filename)
    QStringList getTags(const QString& fileName) const;
    void        setTags(const QString& fileName, const QStringList& tags);

    bool      hasCustomDate(const QString& fileName) const;
    QDateTime getCustomDate(const QString& fileName) const;
    void      setCustomDate(const QString& fileName, const QDateTime& dt);
    void      clearCustomDate(const QString& fileName);

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
        QDateTime   customDate;
        bool        hasCustomDate = false;
    };

    QHash<QString, FileMeta> m_fileMeta;
    QHash<QString, QColor>   m_tagColors;
    QList<TagCategory>       m_categories;

    static QColor randomTagColor();

    // Category JSON helpers
    static QJsonObject categoryToJson(const TagCategory& cat);
    static TagCategory categoryFromJson(const QJsonObject& obj);
};
