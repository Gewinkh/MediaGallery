#include "TagController.h"

#include <functional>

TagController::TagController(TagManager& mgr, QObject* parent)
    : QObject(parent), m_mgr(mgr)
{
    connect(&m_mgr, &TagManager::tagsChanged,       this, &TagController::tagsChanged);
    connect(&m_mgr, &TagManager::categoriesChanged, this, &TagController::categoriesChanged);
    // Tag-Farbänderungen sind auch ein "tagsChanged" für reine Listen-Bindings.
    connect(&m_mgr, &TagManager::tagColorChanged,   this, [this](const QString&, const QColor&) {
        emit tagsChanged();
    });
}

// ── Tags ─────────────────────────────────────────────────────────────────────
QStringList TagController::allTags() const            { return m_mgr.allTags(); }
QColor      TagController::tagColor(const QString& t) const { return m_mgr.tagColor(t); }

void TagController::setTagColor(const QString& tag, const QColor& c) { m_mgr.setTagColor(tag, c); }
void TagController::createTag(const QString& name, const QColor& color) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    m_mgr.createTag(n, color);
}
void TagController::deleteTag(const QString& tag)                       { m_mgr.deleteTag(tag); }
void TagController::renameTag(const QString& oldName, const QString& newName) {
    const QString n = newName.trimmed();
    if (n.isEmpty() || n == oldName) return;
    m_mgr.renameTag(oldName, n);
}

// ── Kategorie-Baum ────────────────────────────────────────────────────────────
QVariantList TagController::buildNodes(const QList<TagCategory>& cats) const {
    QVariantList out;
    out.reserve(cats.size());
    for (const TagCategory& c : cats) {
        QVariantMap node;
        node.insert("id",        c.id);
        node.insert("name",      c.name);
        node.insert("color",     c.uniformColor ? c.color : m_mgr.categoryColor(c.id));
        node.insert("uniform",   c.uniformColor);
        node.insert("inherit",   c.inheritColorToChildren);
        node.insert("tags",      c.tags);
        node.insert("fileCount", c.files.size());
        node.insert("children",  buildNodes(c.children));
        out.append(node);
    }
    return out;
}

QVariantList TagController::categoriesTree() const { return buildNodes(m_mgr.categories()); }
QColor       TagController::categoryColor(const QString& id) const { return m_mgr.categoryColor(id); }

QString TagController::addRootCategory(const QString& name, const QColor& color, bool uniform) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return {};
    TagCategory cat = TagCategory::create(n);
    cat.uniformColor = uniform;
    if (color.isValid()) cat.color = color;
    m_mgr.addCategory(cat);
    return cat.id;    // fuer QML-Aufrufer, die die neue Kategorie direkt referenzieren
}

void TagController::addSubcategory(const QString& parentId, const QString& name,
                                   const QColor& color, bool uniform) {
    const QString n = name.trimmed();
    if (n.isEmpty() || parentId.isEmpty()) return;
    TagCategory sub = TagCategory::create(n);
    sub.uniformColor = uniform;
    if (color.isValid()) sub.color = color;
    m_mgr.addSubcategory(parentId, sub);
}

void TagController::renameCategory(const QString& id, const QString& newName) {
    const QString n = newName.trimmed();
    if (n.isEmpty()) return;
    m_mgr.renameCategory(id, n);
}
void TagController::deleteCategory(const QString& id) { m_mgr.deleteCategory(id); }

void TagController::setCategoryUniformColor(const QString& id, bool uniform,
                                            const QColor& color, bool inheritToChildren) {
    m_mgr.setCategoryUniformColor(id, uniform, color, inheritToChildren);
}

void TagController::moveCategory(const QString& id, const QString& newParentId) {
    m_mgr.moveCategory(id, newParentId);
}

// ── Tag ↔ Kategorie ───────────────────────────────────────────────────────────
void TagController::addTagToCategory(const QString& catId, const QString& tag) {
    m_mgr.addTagToCategory(catId, tag);
}
void TagController::removeTagFromCategory(const QString& catId, const QString& tag) {
    m_mgr.removeTagFromCategory(catId, tag);
}
void TagController::moveTagToCategory(const QString& tag, const QString& fromCatId,
                                      const QString& toCatId) {
    if (fromCatId == toCatId) return;
    m_mgr.moveTagToCategory(tag, fromCatId, toCatId);
}

// ── Datei ↔ Kategorie ─────────────────────────────────────────────────────────
// Flache Liste des (rekursiven) Kategorienbaums für Menüs: Der Anzeigename ist
// der Pfad „Eltern / Kind", damit gleichnamige Unterkategorien unterscheidbar
// bleiben. Reihenfolge = Baumreihenfolge (Tiefensuche).
QVariantList TagController::categoriesFlat() const {
    QVariantList out;
    std::function<void(const QList<TagCategory>&, const QString&)> walk =
        [&](const QList<TagCategory>& cats, const QString& prefix) {
            for (const TagCategory& c : cats) {
                const QString path = prefix.isEmpty() ? c.name
                                                      : prefix + QStringLiteral(" / ") + c.name;
                QVariantMap node;
                node.insert("id",    c.id);
                node.insert("name",  path);
                node.insert("color", c.uniformColor ? c.color : m_mgr.categoryColor(c.id));
                out.append(node);
                walk(c.children, path);
            }
        };
    walk(m_mgr.categories(), QString());
    return out;
}

void TagController::toggleFileInCategory(const QString& catId, const QString& fileName) {
    if (m_mgr.fileInCategory(catId, fileName))
        m_mgr.removeFileFromCategory(catId, fileName);
    else
        m_mgr.addFileToCategory(catId, fileName);
}

bool TagController::fileInCategory(const QString& catId, const QString& fileName) const {
    return m_mgr.fileInCategory(catId, fileName);
}

QStringList TagController::categoriesForFile(const QString& fileName) const {
    return m_mgr.categoriesForFile(fileName);
}

QStringList TagController::categoryIdsForFile(const QString& fileName) const {
    return m_mgr.categoryIdsForFile(fileName);
}

// ── Converter: Tag ↔ Unterkategorie (Phase 4) ────────────────────────────────
// Portiert aus SettingsDialog::convertTagToSubcategory/convertSubcategoryToTag.
void TagController::convertTagToSubcategory(const QString& tag,
                                            const QString& parentCatId,
                                            const QString& newSubcatName) {
    const QString t = tag.trimmed();
    if (t.isEmpty() || parentCatId.isEmpty()) return;

    QString name = newSubcatName.trimmed();
    if (name.isEmpty()) name = t;

    // 1. Unterkategorie unter parentCatId mit der Tag-Farbe anlegen.
    TagCategory sub  = TagCategory::create(name);
    sub.color        = m_mgr.tagColor(t);
    sub.uniformColor = true;
    m_mgr.addSubcategory(parentCatId, sub);

    // 2. Neu erstellte Unterkategorie (letztes Kind des Parents) finden.
    const TagCategory* parent = m_mgr.categoryById(parentCatId);
    if (!parent || parent->children.isEmpty()) return;
    const QString newSubId = parent->children.last().id;

    // 3. Tag als Mitglied der neuen Unterkategorie eintragen.
    m_mgr.addTagToCategory(newSubId, t);

    // 4. Tag aus der globalen Registry entfernen (lebt jetzt als Unterkategorie).
    m_mgr.deleteTag(t);
}

void TagController::convertTagToRootCategory(const QString& tag, const QString& newName) {
    const QString t = tag.trimmed();
    if (t.isEmpty()) return;

    QString name = newName.trimmed();
    if (name.isEmpty()) name = t;

    // 1. Hauptkategorie mit der Tag-Farbe anlegen (ID ist vorab bekannt: create()).
    TagCategory cat  = TagCategory::create(name);
    cat.color        = m_mgr.tagColor(t);
    cat.uniformColor = true;
    m_mgr.addCategory(cat);

    // 2. Tag ERST aus der globalen Registry entfernen, DANACH als Kategorie-Tag
    //    eintragen — deleteTag() räumt die Tag-Listen der WURZEL-Kategorien auf
    //    und würde den soeben gesetzten Eintrag sonst gleich wieder löschen
    //    (beim Unterkategorie-Konverter unkritisch, da dort ein Kind-Knoten).
    m_mgr.deleteTag(t);
    m_mgr.addTagToCategory(cat.id, t);
}

void TagController::convertSubcategoryToTag(const QString& subcatId) {
    if (subcatId.isEmpty()) return;

    const TagCategory* subcat = m_mgr.categoryById(subcatId);
    if (!subcat) return;

    const QString tagName  = subcat->name;
    const QColor  tagColor = subcat->color;

    // 1. Neuen Tag mit der Farbe der Unterkategorie registrieren.
    m_mgr.setTagColor(tagName, tagColor);

    // 2. Alle Dateien der Unterkategorie erhalten diesen Tag.
    for (const QString& fileName : subcat->files)
        m_mgr.addTagToFile(fileName, tagName);

    // 3. Unterkategorie löschen.
    m_mgr.deleteCategory(subcatId);
}
