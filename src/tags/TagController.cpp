#include "tags/TagController.h"

#include "core/Strings.h"
#include "tags/TagUndoMark.h"

#include <functional>

namespace {
//  Schliesst die Rueckgaengig-Gruppe auf JEDEM Rueckweg - die Konverter kehren
//  an mehreren Stellen vorzeitig um.
struct UndoGroupGuard {
    explicit UndoGroupGuard(TagManager* m) : mgr(m) {}
    ~UndoGroupGuard() { mgr->endUndoGroup(); }
    UndoGroupGuard(const UndoGroupGuard&) = delete;
    UndoGroupGuard& operator=(const UndoGroupGuard&) = delete;
    TagManager* mgr;
};
}

TagController::TagController(TagManager& mgr, QObject* parent)
    : QObject(parent), m_mgr(&mgr)
{
    setTagManager(mgr);
}

//  Umhängen: die alten Verbindungen lösen, die neuen knüpfen und die Anzeige
//  auffrischen - sonst zeigte die Fassade weiter die Tags der alten Hälfte.
void TagController::setTagManager(TagManager& mgr) {
    if (m_mgr == &mgr) {
        //  Erstaufbau (Konstruktor): Verbindungen fehlen noch.
        if (!m_wired) m_wired = true;
        else return;
    } else {
        disconnect(m_mgr, nullptr, this, nullptr);
        m_mgr = &mgr;
    }
    connect(m_mgr, &TagManager::tagsChanged,       this, &TagController::tagsChanged);
    connect(m_mgr, &TagManager::categoriesChanged, this, &TagController::categoriesChanged);
    // Tag-Farbänderungen sind auch ein "tagsChanged" für reine Listen-Bindings.
    connect(m_mgr, &TagManager::tagColorChanged,   this, [this](const QString&, const QColor&) {
        emit tagsChanged();
    });
    connect(m_mgr, &TagManager::undoStackChanged,  this, &TagController::undoStackChanged);
    emit tagsChanged();
    emit categoriesChanged();
    emit undoStackChanged();
}

// ── Rueckgaengig (s. Header) ─────────────────────────────────────────────────
bool         TagController::canUndo() const  { return m_mgr->canUndo(); }
bool         TagController::canRedo() const  { return m_mgr->canRedo(); }
QVariantList TagController::undoMark() const { return m_mgr->undoMark(); }
QVariantList TagController::redoMark() const { return m_mgr->redoMark(); }
QString      TagController::undoIcon() const { return m_mgr->undoIcon(); }
QString      TagController::redoIcon() const { return m_mgr->redoIcon(); }
QString      TagController::undoTip() const  { return mg::tagmark::plain(m_mgr->undoMark()); }
QString      TagController::redoTip() const  { return mg::tagmark::plain(m_mgr->redoMark()); }
void         TagController::undoLast()       { m_mgr->undoLastStep(); }
void         TagController::redoLast()       { m_mgr->redoLastStep(); }
void         TagController::endUndoGroup()   { m_mgr->endUndoGroup(); }

//  Der Tag-Modus der Galerie ist EINE Bedienung (Kachel um Kachel bis
//  „Fertig"). Die Marke zaehlt mit, was darin zu- und weggekommen ist -
//  `+3 T:a` gruen, `-1 T:a` rot, beides zusammen bei gemischten Sitzungen.
void TagController::beginTagModeGroup(const QString& tag) {
    m_mgr->beginUndoGroup(mg::tagmark::mkCounted(0, 0, mg::tagmark::Thing::Tag, tag, {}),
                          /*counted=*/true);
}

// ── Tags ─────────────────────────────────────────────────────────────────────
QStringList TagController::allTags() const            { return m_mgr->allTags(); }
QColor      TagController::tagColor(const QString& t) const { return m_mgr->tagColor(t); }

void TagController::setTagColor(const QString& tag, const QColor& c) { m_mgr->setTagColor(tag, c); }
void TagController::createTag(const QString& name, const QColor& color) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    m_mgr->createTag(n, color);
}
void TagController::deleteTag(const QString& tag)                       { m_mgr->deleteTag(tag); }
void TagController::renameTag(const QString& oldName, const QString& newName) {
    const QString n = newName.trimmed();
    if (n.isEmpty() || n == oldName) return;
    m_mgr->renameTag(oldName, n);
}

// ── Kategorie-Baum ────────────────────────────────────────────────────────────
QVariantList TagController::buildNodes(const QList<TagCategory>& cats,
                                       const QColor& inherited) const {
    QVariantList out;
    out.reserve(cats.size());
    for (const TagCategory& c : cats) {
        // Kaskade: Erbt dieser Knoten eine Farbe von einem Vorfahren, gewinnt
        // der Vorfahr - der gesamte Teilbaum trägt dann dieselbe Farbe
        // (Anforderung: "alle Tags, Unter- und verschachtelten Unterkategorien").
        const bool   cascaded  = inherited.isValid();
        const QColor own       = c.uniformColor ? c.color : m_mgr->categoryColor(c.id);
        const QColor nodeColor = cascaded ? inherited : own;
        // Tag-Chips dieses Knotens tragen die Einheitsfarbe, wenn eine Kaskade
        // wirkt ODER die Kategorie selbst eine Einheitsfarbe gesetzt hat.
        const bool   tagUniform = cascaded || c.uniformColor;
        const QColor tagColor   = cascaded ? inherited : c.color;
        // An die Kinder weitergereichte Kaskade: bestehende fortsetzen, sonst
        // nur starten, wenn diese Kategorie uniform UND vererbend ist. Nicht
        // destruktiv - die Eigenfarben der Kinder bleiben unangetastet und
        // kehren beim Deaktivieren automatisch zurück.
        const QColor childCascade = cascaded
            ? inherited
            : (c.uniformColor && c.inheritColorToChildren ? c.color : QColor());

        QVariantMap node;
        node.insert("id",         c.id);
        node.insert("name",       c.name);
        node.insert("color",      nodeColor);
        node.insert("uniform",    c.uniformColor);
        node.insert("inherit",    c.inheritColorToChildren);
        node.insert("tagUniform", tagUniform);
        node.insert("tagColor",   tagColor);
        node.insert("tags",       c.tags);
        node.insert("fileCount",  c.files.size());
        node.insert("children",   buildNodes(c.children, childCascade));
        out.append(node);
    }
    return out;
}

QVariantList TagController::categoriesTree() const { return buildNodes(m_mgr->categories()); }
QColor       TagController::categoryColor(const QString& id) const { return m_mgr->categoryColor(id); }

QString TagController::addRootCategory(const QString& name, const QColor& color, bool uniform) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return {};
    TagCategory cat = TagCategory::create(n);
    cat.uniformColor = uniform;
    if (color.isValid()) cat.color = color;
    m_mgr->addCategory(cat);
    return cat.id;    // fuer QML-Aufrufer, die die neue Kategorie direkt referenzieren
}

void TagController::addSubcategory(const QString& parentId, const QString& name,
                                   const QColor& color, bool uniform) {
    const QString n = name.trimmed();
    if (n.isEmpty() || parentId.isEmpty()) return;
    TagCategory sub = TagCategory::create(n);
    sub.uniformColor = uniform;
    if (color.isValid()) sub.color = color;
    m_mgr->addSubcategory(parentId, sub);
}

void TagController::renameCategory(const QString& id, const QString& newName) {
    const QString n = newName.trimmed();
    if (n.isEmpty()) return;
    m_mgr->renameCategory(id, n);
}
void TagController::deleteCategory(const QString& id) { m_mgr->deleteCategory(id); }

void TagController::setCategoryUniformColor(const QString& id, bool uniform,
                                            const QColor& color, bool inheritToChildren) {
    m_mgr->setCategoryUniformColor(id, uniform, color, inheritToChildren);
}

void TagController::swapCategories(const QString& a, const QString& b) {
    m_mgr->swapCategories(a, b);
}

void TagController::moveCategory(const QString& id, const QString& newParentId) {
    m_mgr->moveCategory(id, newParentId);
}

// ── Tag ↔ Kategorie ───────────────────────────────────────────────────────────
void TagController::addTagToCategory(const QString& catId, const QString& tag) {
    m_mgr->addTagToCategory(catId, tag);
}
void TagController::removeTagFromCategory(const QString& catId, const QString& tag) {
    m_mgr->removeTagFromCategory(catId, tag);
}
void TagController::moveTagToCategory(const QString& tag, const QString& fromCatId,
                                      const QString& toCatId) {
    if (fromCatId == toCatId) return;
    m_mgr->moveTagToCategory(tag, fromCatId, toCatId);
}

// ── Datei ↔ Kategorie ─────────────────────────────────────────────────────────
// Flache Liste des (rekursiven) Kategorienbaums für Menüs: Der Anzeigename ist
// der Pfad „Eltern / Kind", damit gleichnamige Unterkategorien unterscheidbar
// bleiben. Reihenfolge = Baumreihenfolge (Tiefensuche).
QVariantList TagController::categoriesFlat() const {
    QVariantList out;
    // Farbe wie im Baum kaskadieren, damit Menü-Farbpunkte zur Panel-Anzeige passen.
    std::function<void(const QList<TagCategory>&, const QString&, const QColor&)> walk =
        [&](const QList<TagCategory>& cats, const QString& prefix, const QColor& inherited) {
            for (const TagCategory& c : cats) {
                const QString path = prefix.isEmpty() ? c.name
                                                      : prefix + QStringLiteral(" / ") + c.name;
                const bool   cascaded = inherited.isValid();
                const QColor col = cascaded ? inherited
                                            : (c.uniformColor ? c.color : m_mgr->categoryColor(c.id));
                const QColor childCascade = cascaded
                    ? inherited
                    : (c.uniformColor && c.inheritColorToChildren ? c.color : QColor());
                QVariantMap node;
                node.insert("id",    c.id);
                node.insert("name",  path);
                node.insert("color", col);
                out.append(node);
                walk(c.children, path, childCascade);
            }
        };
    walk(m_mgr->categories(), QString(), QColor());
    return out;
}

void TagController::toggleFileInCategory(const QString& catId, const QString& fileName) {
    if (m_mgr->fileInCategory(catId, fileName))
        m_mgr->removeFileFromCategory(catId, fileName);
    else
        m_mgr->addFileToCategory(catId, fileName);
}

bool TagController::fileInCategory(const QString& catId, const QString& fileName) const {
    return m_mgr->fileInCategory(catId, fileName);
}

QStringList TagController::categoriesForFile(const QString& fileName) const {
    return m_mgr->categoriesForFile(fileName);
}

QStringList TagController::categoryIdsForFile(const QString& fileName) const {
    return m_mgr->categoryIdsForFile(fileName);
}

QStringList TagController::categoryIdsForFiles(const QStringList& fileNames) const {
    if (fileNames.isEmpty()) return {};
    QStringList out = m_mgr->categoryIdsForFile(fileNames.first());
    for (int i = 1; i < fileNames.size() && !out.isEmpty(); ++i) {
        const QStringList ids = m_mgr->categoryIdsForFile(fileNames.at(i));
        for (int k = out.size() - 1; k >= 0; --k)
            if (!ids.contains(out.at(k))) out.removeAt(k);
    }
    return out;
}

void TagController::setFilesInCategory(const QString& catId,
                                       const QStringList& fileNames, bool on) {
    for (const QString& name : fileNames) {
        if (on == m_mgr->fileInCategory(catId, name)) continue;
        if (on) m_mgr->addFileToCategory(catId, name);
        else    m_mgr->removeFileFromCategory(catId, name);
    }
}

// ── Converter: Tag ↔ Unterkategorie (Phase 4) ────────────────────────────────
//  DIE DATEIEN ZIEHEN MIT. Ein Tag ist nur ueber die Dateien etwas wert, die
//  ihn tragen; wird er zur Kategorie, gehoeren sie in deren `files`-Liste -
//  daran haengen die Anzeige (`fileCount`), der Kategorie-Filter und der
//  Rueckweg `convertSubcategoryToTag`. Ohne das stand nach dem Umwandeln eine
//  LEERE Kategorie da (Nutzerbefund 2026-09-03).
//
//  Der Tag selbst wird dabei AUFGEBRAUCHT: er verschwindet aus der Registry und
//  von den Dateien. Frueher trug ihn die neue Kategorie hinterher noch als
//  Tag-Chip - der filterte nach dem Umwandeln aber nichts mehr, weil keine
//  Datei ihn mehr hatte. So ist der Weg Tag -> Kategorie -> Tag verlustfrei.
void TagController::convertTagToSubcategory(const QString& tag,
                                            const QString& parentCatId,
                                            const QString& newSubcatName) {
    const QString t = tag.trimmed();
    if (t.isEmpty() || parentCatId.isEmpty()) return;
    //  ERST pruefen, DANN anfassen: gibt es das Ziel oder den Tag nicht, darf
    //  auch nichts passieren - sonst verbrauchte eine gescheiterte Umwandlung
    //  den Tag trotzdem (Schritt 2 laeuft unabhaengig von Schritt 1).
    if (!m_mgr->categoryById(parentCatId)) return;
    if (!m_mgr->allTags().contains(t))     return;

    QString name = newSubcatName.trimmed();
    if (name.isEmpty()) name = t;

    //  Vier Mutationen, EIN Rueckgaengig-Schritt (s. `TagManager`).
    {
        QStringList ziel = mg::tagmark::pathOf(m_mgr->categories(), parentCatId);
        if (const TagCategory* p = m_mgr->categoryById(parentCatId)) ziel.append(p->name);
        m_mgr->beginUndoGroup(mg::tagmark::mkTransition(
            mg::tagmark::Verb::Convert,
            mg::tagmark::Thing::Tag,         t,    {},
            mg::tagmark::Thing::Subcategory, name, ziel));
    }
    const UndoGroupGuard guard(m_mgr);

    // 1. Unterkategorie unter parentCatId mit der Tag-Farbe anlegen - MIT den
    //    Dateien, die den Tag tragen (s. Kopf dieses Abschnitts).
    TagCategory sub  = TagCategory::create(name);
    sub.color        = m_mgr->tagColor(t);
    sub.uniformColor = true;
    sub.files        = m_mgr->filesWithTag(t);
    m_mgr->addSubcategory(parentCatId, sub);

    // 2. Tag aufbrauchen: aus der Registry, von den Dateien, aus allen
    //    Kategorien. Er lebt ab jetzt als Unterkategorie weiter.
    m_mgr->deleteTag(t);
}

void TagController::convertTagToRootCategory(const QString& tag, const QString& newName) {
    const QString t = tag.trimmed();
    if (t.isEmpty()) return;
    if (!m_mgr->allTags().contains(t)) return;      // s. oben

    QString name = newName.trimmed();
    if (name.isEmpty()) name = t;

    m_mgr->beginUndoGroup(mg::tagmark::mkTransition(
        mg::tagmark::Verb::Convert,
        mg::tagmark::Thing::Tag,      t,    {},
        mg::tagmark::Thing::Category, name, {}));
    const UndoGroupGuard guard(m_mgr);

    // 1. Hauptkategorie mit der Tag-Farbe anlegen (ID ist vorab bekannt:
    //    create()) - MIT den Dateien, die den Tag tragen.
    TagCategory cat  = TagCategory::create(name);
    cat.color        = m_mgr->tagColor(t);
    cat.uniformColor = true;
    cat.files        = m_mgr->filesWithTag(t);
    m_mgr->addCategory(cat);

    // 2. Tag aufbrauchen (s. Kopf dieses Abschnitts).
    m_mgr->deleteTag(t);
}

void TagController::convertSubcategoryToTag(const QString& subcatId) {
    if (subcatId.isEmpty()) return;

    const TagCategory* subcat = m_mgr->categoryById(subcatId);
    if (!subcat) return;

    const QString tagName  = subcat->name;
    const QColor  tagColor = subcat->color;

    {
        const QStringList p = mg::tagmark::pathOf(m_mgr->categories(), subcatId);
        m_mgr->beginUndoGroup(mg::tagmark::mkTransition(
            mg::tagmark::Verb::Convert,
            p.isEmpty() ? mg::tagmark::Thing::Category
                        : mg::tagmark::Thing::Subcategory, tagName, p,
            mg::tagmark::Thing::Tag, tagName, {}));
    }
    const UndoGroupGuard guard(m_mgr);

    // 1. Neuen Tag mit der Farbe der Unterkategorie registrieren.
    m_mgr->setTagColor(tagName, tagColor);

    // 2. Alle Dateien der Unterkategorie erhalten diesen Tag - der Rueckweg
    //    zu Schritt 1 der beiden Konverter oben.
    const QStringList files = subcat->files;
    for (const QString& fileName : files)
        m_mgr->addTagToFile(fileName, tagName);

    // 3. Unterkategorie löschen.
    m_mgr->deleteCategory(subcatId);
}
