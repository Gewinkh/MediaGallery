#include "TagManager.h"

#include <functional>

TagManager::TagManager(JsonStorage* storage, QObject* parent)
    : QObject(parent), m_storage(storage) {}

// ── Tag basics ────────────────────────────────────────────────────────────────
QStringList TagManager::allTags() const { return m_storage->allTags(); }
QColor      TagManager::tagColor(const QString& tag) const { return m_storage->tagColor(tag); }

void TagManager::setTagColor(const QString& tag, const QColor& c) {
    m_storage->setTagColor(tag, c);
    m_storage->saveCurrentFolder();
    emit tagColorChanged(tag, c);
    emit categoriesChanged();
}

void TagManager::addTagToFile(const QString& fileName, const QString& tag) {
    QStringList tags = m_storage->getTags(fileName);
    if (!tags.contains(tag)) {
        tags.append(tag);
        m_storage->setTags(fileName, tags);
        m_storage->saveCurrentFolder();
        emit tagsChanged();
    }
}

void TagManager::createTag(const QString& name, const QColor& color) {
    if (name.trimmed().isEmpty()) return;
    m_storage->ensureTagRegistered(name.trimmed());
    if (color.isValid())
        m_storage->setTagColor(name.trimmed(), color);
    m_storage->saveCurrentFolder();
    emit tagsChanged();
}

void TagManager::removeTagFromFile(const QString& fileName, const QString& tag) {
    QStringList tags = m_storage->getTags(fileName);
    if (tags.removeAll(tag) > 0) {
        m_storage->setTags(fileName, tags);
        m_storage->saveCurrentFolder();
        emit tagsChanged();
    }
}

QStringList TagManager::tagsForFile(const QString& fileName) const {
    return m_storage->getTags(fileName);
}

void TagManager::deleteTag(const QString& tag) {
    // Remove from all categories
    for (auto& cat : m_storage->categoriesRef())
        cat.tags.removeAll(tag);
    m_storage->deleteTag(tag);
    // Sofort persistieren — nicht erst beim nächsten anderweitigen Save.
    // JsonStorage::saveFolder prüft dabei selbst, ob danach noch Tags/
    // Kategorien/Datei-Metadaten übrig sind, und entfernt andernfalls die
    // JSON-Datei komplett statt eines leeren Stubs.
    m_storage->saveCurrentFolder();
    emit tagsChanged();
    emit categoriesChanged();
}

void TagManager::renameTag(const QString& oldName, const QString& newName) {
    QColor c = m_storage->tagColor(oldName);
    // Rename in all categories
    std::function<void(QList<TagCategory>&)> rename = [&](QList<TagCategory>& list){
        for (auto& cat : list) {
            int i = cat.tags.indexOf(oldName);
            if (i >= 0) cat.tags[i] = newName;
            rename(cat.children);
        }
    };
    rename(m_storage->categoriesRef());
    m_storage->deleteTag(oldName);
    m_storage->setTagColor(newName, c);
    m_storage->saveCurrentFolder();
    emit tagsChanged();
    emit categoriesChanged();
}

// ── Categories ────────────────────────────────────────────────────────────────
QList<TagCategory>& TagManager::categories() {
    return m_storage->categoriesRef();
}
const QList<TagCategory>& TagManager::categories() const {
    return m_storage->categoriesRef();
}

void TagManager::addCategory(const TagCategory& cat) {
    m_storage->categoriesRef().append(cat);
    m_storage->saveCurrentFolder();
    emit categoriesChanged();
}

void TagManager::addSubcategory(const QString& parentId, const TagCategory& sub) {
    TagCategory* parent = findById(m_storage->categoriesRef(), parentId);
    if (!parent) return;
    parent->children.append(sub);
    m_storage->saveCurrentFolder();
    emit categoriesChanged();
}

void TagManager::renameCategory(const QString& id, const QString& newName) {
    TagCategory* cat = findById(m_storage->categoriesRef(), id);
    if (!cat) return;
    cat->name = newName;
    m_storage->saveCurrentFolder();
    emit categoriesChanged();
}

void TagManager::deleteCategory(const QString& id) {
    removeById(m_storage->categoriesRef(), id);
    m_storage->saveCurrentFolder();
    emit categoriesChanged();
}

void TagManager::moveCategory(const QString& id, const QString& newParentId) {
    if (id.isEmpty() || id == newParentId) return;

    const TagCategory* node = findById(m_storage->categoriesRef(), id);
    if (!node) return;

    // Ziel darf nicht im Teilbaum des zu verschiebenden Knotens liegen.
    if (!newParentId.isEmpty()) {
        std::function<bool(const TagCategory&)> contains = [&](const TagCategory& c) {
            if (c.id == newParentId) return true;
            for (const TagCategory& ch : c.children)
                if (contains(ch)) return true;
            return false;
        };
        if (contains(*node)) return;
        // Ziel muss existieren, sonst nichts tun (kein stiller Wurzel-Fallback).
        if (!findById(m_storage->categoriesRef(), newParentId)) return;
    }

    TagCategory moved = *node;                    // tiefe Kopie inkl. Teilbaum
    removeById(m_storage->categoriesRef(), id);

    if (newParentId.isEmpty()) {
        m_storage->categoriesRef().append(moved); // → Hauptebene (Wurzel)
    } else {
        // Parent NACH dem Entfernen frisch suchen (Container kann realloziert sein).
        TagCategory* parent = findById(m_storage->categoriesRef(), newParentId);
        if (parent) parent->children.append(moved);
        else        m_storage->categoriesRef().append(moved);   // Absicherung
    }

    m_storage->saveCurrentFolder();
    emit categoriesChanged();
}

void TagManager::setCategoryUniformColor(const QString& id, bool uniform, const QColor& color,
                                         bool inheritToChildren) {
    TagCategory* cat = findById(m_storage->categoriesRef(), id);
    if (!cat) return;
    cat->uniformColor = uniform;
    cat->inheritColorToChildren = uniform && inheritToChildren;
    if (uniform) {
        cat->color = color;
        if (inheritToChildren)
            applyColorToChildren(cat->children, color);
    }
    m_storage->saveCurrentFolder();
    emit categoriesChanged();
}

void TagManager::applyColorToChildren(QList<TagCategory>& children, const QColor& color) {
    for (auto& child : children) {
        child.uniformColor = true;
        child.color = color;
        // Keep or reset inheritColorToChildren on child — children inherit but
        // don't automatically re-propagate unless explicitly set.
        applyColorToChildren(child.children, color);
    }
}

void TagManager::addTagToCategory(const QString& catId, const QString& tag) {
    m_storage->ensureTagRegistered(tag);
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat) return;
    if (!cat->tags.contains(tag)) cat->tags.append(tag);
    // Emit first so UI updates, then save — prevents any signal-triggered
    // rebuild from racing with the write.
    emit tagsChanged();
    emit categoriesChanged();
    m_storage->saveCurrentFolder();
}

void TagManager::removeTagFromCategory(const QString& catId, const QString& tag) {
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat) return;
    cat->tags.removeAll(tag);
    m_storage->saveCurrentFolder();
    emit categoriesChanged();
}

void TagManager::moveTagToCategory(const QString& tag,
                                   const QString& fromCatId,
                                   const QString& toCatId) {
    removeTagFromCategory(fromCatId, tag);
    addTagToCategory(toCatId, tag);
}

// ── Datei ↔ Kategorie (direkte Mitgliedschaft) ────────────────────────────────
void TagManager::addFileToCategory(const QString& catId, const QString& fileName) {
    if (fileName.isEmpty()) return;
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat || cat->files.contains(fileName)) return;   // idempotent
    cat->files.append(fileName);
    m_storage->saveCurrentFolder();
    // categoriesChanged zieht den Proxy-Kategoriefilter (m_activeCatFiles) und
    // alle QML-Ansichten (fileCount, Panels) nach.
    emit categoriesChanged();
}

void TagManager::removeFileFromCategory(const QString& catId, const QString& fileName) {
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat || cat->files.removeAll(fileName) == 0) return;
    m_storage->saveCurrentFolder();
    emit categoriesChanged();
}

bool TagManager::fileInCategory(const QString& catId, const QString& fileName) const {
    const TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    return cat && cat->files.contains(fileName);
}

namespace {
// Rekursiver Sammler: alle Kategorien, denen die Datei DIREKT angehört.
// pick wählt, ob Name oder ID gesammelt wird.
void collectFileCategories(const QList<TagCategory>& list, const QString& fileName,
                           QStringList& out, bool ids) {
    for (const auto& cat : list) {
        if (cat.files.contains(fileName))
            out.append(ids ? cat.id : cat.name);
        collectFileCategories(cat.children, fileName, out, ids);
    }
}
}

QStringList TagManager::categoriesForFile(const QString& fileName) const {
    QStringList out;
    collectFileCategories(m_storage->categoriesRef(), fileName, out, /*ids=*/false);
    return out;
}

QStringList TagManager::categoryIdsForFile(const QString& fileName) const {
    QStringList out;
    collectFileCategories(m_storage->categoriesRef(), fileName, out, /*ids=*/true);
    return out;
}

// ── Static helpers ────────────────────────────────────────────────────────────
TagCategory* TagManager::findById(QList<TagCategory>& list, const QString& id) {
    for (auto& cat : list) {
        if (cat.id == id) return &cat;
        TagCategory* found = findById(cat.children, id);
        if (found) return found;
    }
    return nullptr;
}

const TagCategory* TagManager::findById(const QList<TagCategory>& list, const QString& id) {
    for (const auto& cat : list) {
        if (cat.id == id) return &cat;
        const TagCategory* found = findById(cat.children, id);
        if (found) return found;
    }
    return nullptr;
}

QColor TagManager::categoryColor(const QString& id) const {
    const TagCategory* cat = findById(m_storage->categoriesRef(), id);
    if (cat && cat->uniformColor) return cat->color;
    return QColor(0, 180, 160); // default teal
}

const TagCategory* TagManager::categoryById(const QString& id) const {
    return findById(m_storage->categoriesRef(), id);
}

bool TagManager::removeById(QList<TagCategory>& list, const QString& id) {
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].id == id) { list.removeAt(i); return true; }
        if (removeById(list[i].children, id)) return true;
    }
    return false;
}
