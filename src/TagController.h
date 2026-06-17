#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QVariantList>

#include "TagManager.h"

// ─────────────────────────────────────────────────────────────────────────────
//  TagController — C++→QML-Bridge (Singleton) für das Tag-/Kategorie-System.
//
//  Reine Delegation an TagManager (Backend bleibt unverändert). Ersetzt die
//  Widget-Klassen TagCategoryPanel/TagWidget als Datenquelle: QML rendert den
//  Baum aus categoriesTree() und mutiert über die Q_INVOKABLE-Methoden.
//
//  Registrierung via qmlRegisterSingletonInstance("MediaGallery",1,0,"Tags",…)
//  in main.cpp — keine QML_ELEMENT-Makros.
// ─────────────────────────────────────────────────────────────────────────────
class TagController : public QObject {
    Q_OBJECT
public:
    explicit TagController(TagManager& mgr, QObject* parent = nullptr);

    // ── Tags ────────────────────────────────────────────────────────────────
    Q_INVOKABLE QStringList allTags() const;
    Q_INVOKABLE QStringList uncategorizedTags() const;
    Q_INVOKABLE QColor      tagColor(const QString& tag) const;
    Q_INVOKABLE void        setTagColor(const QString& tag, const QColor& c);
    Q_INVOKABLE void        createTag(const QString& name, const QColor& color);
    Q_INVOKABLE void        deleteTag(const QString& tag);
    Q_INVOKABLE void        renameTag(const QString& oldName, const QString& newName);

    // ── Kategorie-Baum (rekursiv, für QML-Repeater/TreeView) ─────────────────
    // Knoten: { id, name, color, uniform, inherit, tags:[…], fileCount, children:[…] }
    Q_INVOKABLE QVariantList categoriesTree() const;
    Q_INVOKABLE QColor       categoryColor(const QString& id) const;

    Q_INVOKABLE void addRootCategory(const QString& name, const QColor& color, bool uniform);
    Q_INVOKABLE void addSubcategory(const QString& parentId, const QString& name,
                                    const QColor& color, bool uniform);
    Q_INVOKABLE void renameCategory(const QString& id, const QString& newName);
    Q_INVOKABLE void deleteCategory(const QString& id);
    Q_INVOKABLE void setCategoryUniformColor(const QString& id, bool uniform,
                                             const QColor& color, bool inheritToChildren);

    // ── Tag ↔ Kategorie ──────────────────────────────────────────────────────
    Q_INVOKABLE void addTagToCategory(const QString& catId, const QString& tag);
    Q_INVOKABLE void removeTagFromCategory(const QString& catId, const QString& tag);
    Q_INVOKABLE void moveTagToCategory(const QString& tag, const QString& fromCatId,
                                       const QString& toCatId);

    // ── Converter: Tag ↔ Unterkategorie (Phase 4) ────────────────────────────
    // Kombinierte Mehrschritt-Mutationen — bleiben als Geschäftslogik in C++.
    Q_INVOKABLE void convertTagToSubcategory(const QString& tag,
                                             const QString& parentCatId,
                                             const QString& newSubcatName);
    Q_INVOKABLE void convertSubcategoryToTag(const QString& subcatId);

    // ── Datei ↔ Kategorie ─────────────────────────────────────────────────────
    Q_INVOKABLE void addFileToCategory(const QString& catId, const QString& fileName);
    Q_INVOKABLE void removeFileFromCategory(const QString& catId, const QString& fileName);
    Q_INVOKABLE QStringList categoriesForFile(const QString& fileName) const;

signals:
    void tagsChanged();
    void categoriesChanged();
    void tagColorChanged(const QString& tag, const QColor& color);

private:
    QVariantList buildNodes(const QList<TagCategory>& cats) const;

    TagManager& m_mgr;
};
