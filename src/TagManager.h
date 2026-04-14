#pragma once
#include <QObject>
#include <QString>
#include <QColor>
#include <QStringList>
#include <QList>
#include "JsonStorage.h"
#include "TagCategory.h"

class TagManager : public QObject {
    Q_OBJECT
public:
    explicit TagManager(JsonStorage* storage, QObject* parent = nullptr);

    // ── Tag basics ────────────────────────────────────────────────────────────
    QStringList allTags() const;
    QColor      tagColor(const QString& tag) const;
    void        setTagColor(const QString& tag, const QColor& c);

    void addTagToFile(const QString& fileName, const QString& tag);
    void removeTagFromFile(const QString& fileName, const QString& tag);
    void setTagsForFile(const QString& fileName, const QStringList& tags);
    QStringList tagsForFile(const QString& fileName) const;

    void createTag(const QString& name, const QColor& color); // create a global tag with a specific color
    void deleteTag(const QString& tag);
    void renameTag(const QString& oldName, const QString& newName);

    // ── Categories ────────────────────────────────────────────────────────────
    QList<TagCategory>&       categories();
    const QList<TagCategory>& categories() const;

    void addCategory(const TagCategory& cat);
    void addSubcategory(const QString& parentId, const TagCategory& sub);
    void renameCategory(const QString& id, const QString& newName);
    void deleteCategory(const QString& id);
    void setCategoryUniformColor(const QString& id, bool uniform, const QColor& color,
                                 bool inheritToChildren = false);
    // Returns the effective display color for a category (uniform color if set,
    // or the default teal). Useful for dropdowns that need a live color lookup.
    QColor categoryColor(const QString& id) const;
    const TagCategory* categoryById(const QString& id) const;

    // Tag <-> category membership
    void addTagToCategory(const QString& catId, const QString& tag);
    void removeTagFromCategory(const QString& catId, const QString& tag);
    void moveTagToCategory(const QString& tag,
                           const QString& fromCatId,
                           const QString& toCatId);

    // File <-> category membership
    void addFileToCategory(const QString& catId, const QString& fileName);
    void removeFileFromCategory(const QString& catId, const QString& fileName);
    QStringList categoriesForFile(const QString& fileName) const;  // returns category IDs

    // Tags not yet in any category
    QStringList uncategorizedTags() const;

signals:
    void tagsChanged();
    void tagColorChanged(const QString& tag, const QColor& color);
    void categoriesChanged();

private:
    JsonStorage* m_storage;

    static TagCategory* findById(QList<TagCategory>& list, const QString& id);
    static const TagCategory* findById(const QList<TagCategory>& list, const QString& id);
    static bool         removeById(QList<TagCategory>& list, const QString& id);
    static QStringList  allTagsInTree(const QList<TagCategory>& list);
    static void         applyColorToChildren(QList<TagCategory>& children, const QColor& color);
};
