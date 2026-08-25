#include <QCoreApplication>
#include "tags/TagManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRunnable>

#include <functional>

TagManager::TagManager(JsonStorage* storage, QObject* parent)
    : QObject(parent), m_storage(storage) {
    //  Sammelndes Melden (s. Header): ein Null-Timer feuert am Ende des
    //  laufenden Ereignisdurchlaufs, alles darin wird zu EINER Meldung.
    m_signalTimer.setSingleShot(true);
    m_signalTimer.setInterval(0);
    connect(&m_signalTimer, &QTimer::timeout, this, &TagManager::flushPendingSignals);
    if (QCoreApplication* app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this,
                &TagManager::flushPendingSignals);
}

//  Ohne laufende Ereignisschleife kann der Timer nie feuern - dann wird sofort
//  gemeldet (Testtreiber, Kommandozeilenwege). Sicherer Fall zuerst.
void TagManager::scheduleTagsChanged() {
    if (!QCoreApplication::instance()) { emit tagsChanged(); return; }
    m_tagsDirty = true;
    if (!m_signalTimer.isActive()) m_signalTimer.start();
}

void TagManager::scheduleCategoriesChanged() {
    if (!QCoreApplication::instance()) { emit categoriesChanged(); return; }
    m_catsDirty = true;
    if (!m_signalTimer.isActive()) m_signalTimer.start();
}

void TagManager::flushPendingSignals() {
    m_signalTimer.stop();
    const bool t = m_tagsDirty, c = m_catsDirty;
    m_tagsDirty = m_catsDirty = false;   // VOR dem Melden - ein Empfaenger darf
    if (t) emit tagsChanged();           // erneut aendern, ohne dass es verfaellt
    if (c) emit categoriesChanged();
}

// ── Tag basics ────────────────────────────────────────────────────────────────
QStringList TagManager::allTags() const { return m_storage->allTags(); }
QColor      TagManager::tagColor(const QString& tag) const { return m_storage->tagColor(tag); }

void TagManager::setTagColor(const QString& tag, const QColor& c) {
    m_storage->setTagColor(tag, c);
    m_storage->saveCurrentFolder();
    emit tagColorChanged(tag, c);
    scheduleCategoriesChanged();
}

void TagManager::addTagToFile(const QString& fileName, const QString& tag) {
    QStringList tags = m_storage->getTags(fileName);
    if (!tags.contains(tag)) {
        tags.append(tag);
        m_storage->setTags(fileName, tags);
        m_storage->saveCurrentFolder();
        scheduleTagsChanged();
    }
}

void TagManager::createTag(const QString& name, const QColor& color) {
    if (name.trimmed().isEmpty()) return;
    m_storage->ensureTagRegistered(name.trimmed());
    if (color.isValid())
        m_storage->setTagColor(name.trimmed(), color);
    m_storage->saveCurrentFolder();
    scheduleTagsChanged();
}

void TagManager::removeTagFromFile(const QString& fileName, const QString& tag) {
    QStringList tags = m_storage->getTags(fileName);
    if (tags.removeAll(tag) > 0) {
        m_storage->setTags(fileName, tags);
        m_storage->saveCurrentFolder();
        scheduleTagsChanged();
    }
}

QStringList TagManager::tagsForFile(const QString& fileName) const {
    return m_storage->getTags(fileName);
}

void TagManager::deleteTag(const QString& tag) {
    //  Aus ALLEN Kategorien - auch den verschachtelten. Die Schleife lief
    //  früher nur über die oberste Ebene; in einer Unterkategorie blieb der
    //  Tag stehen (`renameTag` daneben war schon immer rekursiv).
    std::function<void(QList<TagCategory>&)> strip = [&](QList<TagCategory>& list) {
        for (auto& cat : list) {
            cat.tags.removeAll(tag);
            strip(cat.children);
        }
    };
    strip(m_storage->categoriesRef());
    m_storage->deleteTag(tag);
    // Sofort persistieren - nicht erst beim nächsten anderweitigen Save.
    // JsonStorage::saveFolder prüft dabei selbst, ob danach noch Tags/
    // Kategorien/Datei-Metadaten übrig sind, und entfernt andernfalls die
    // JSON-Datei komplett statt eines leeren Stubs.
    m_storage->saveCurrentFolder();
    scheduleTagsChanged();
    scheduleCategoriesChanged();
    emit tagDeleted(tag);
}

// ── Denselben Tag aus den Sidecars aller UNTERordner entfernen ───────────────
//  Jeder Ordner fuehrt seine Verschlagwortung in einer eigenen Datei
//  `<ordner>/<ordnername>.json`. Ohne diesen Durchgang blieb ein geloeschter
//  Tag in jedem Unterordner stehen (Nutzerbefund).
//
//  Der Durchgang laeuft im Hintergrund (Regel 8) und fasst NUR die Ordner an,
//  in denen der Tag wirklich vorkommt - ein Baum mit hundert Ordnern schreibt
//  sonst hundert Dateien neu, obwohl drei betroffen sind.
//
//  `JsonStorage` wird IM FADEN erzeugt und dort auch wieder abgeraeumt (sein
//  `QTimer` gehoert damit diesem Faden); gespeichert wird ueber `saveFolder`
//  (sofort), nie ueber `saveCurrentFolder` (sammelnder Timer).
void TagManager::sweepSubfolders(const QString& rootFolder, const QString& tag) {
    if (rootFolder.isEmpty() || tag.isEmpty()) {
        emit subfolderSweepFinished(tag, 0);
        return;
    }
    m_sweepPool.setMaxThreadCount(1);

    class SweepTask : public QRunnable {
    public:
        SweepTask(TagManager* owner, QString root, QString tag)
            : m_owner(owner), m_root(std::move(root)), m_tag(std::move(tag)) {
            setAutoDelete(true);
        }
        void run() override {
            int touched = 0;
            QDirIterator it(m_root, QDir::Dirs | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString dir = it.next();
                const QString side = dir + QLatin1Char('/')
                                   + QFileInfo(dir).fileName() + QStringLiteral(".json");
                if (!QFile::exists(side))
                    continue;               // Ordner ohne Sidecar: nichts zu tun
                JsonStorage st;
                st.loadFolder(dir);
                if (!st.allTags().contains(m_tag))
                    continue;               // kennt den Tag nicht - Datei bleibt, wie sie ist
                std::function<void(QList<TagCategory>&)> strip =
                    [&](QList<TagCategory>& list) {
                        for (auto& cat : list) {
                            cat.tags.removeAll(m_tag);
                            strip(cat.children);
                        }
                    };
                strip(st.categoriesRef());
                st.deleteTag(m_tag);
                st.saveFolder(dir);
                ++touched;
            }
            TagManager* owner = m_owner;
            const QString tag = m_tag;
            QMetaObject::invokeMethod(owner, [owner, tag, touched] {
                emit owner->subfolderSweepFinished(tag, touched);
            }, Qt::QueuedConnection);
        }
    private:
        TagManager* m_owner;
        QString     m_root;
        QString     m_tag;
    };

    m_sweepPool.start(new SweepTask(this, rootFolder, tag));
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
    scheduleTagsChanged();
    scheduleCategoriesChanged();
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
    scheduleCategoriesChanged();
}

void TagManager::addSubcategory(const QString& parentId, const TagCategory& sub) {
    TagCategory* parent = findById(m_storage->categoriesRef(), parentId);
    if (!parent) return;
    parent->children.append(sub);
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
}

void TagManager::renameCategory(const QString& id, const QString& newName) {
    TagCategory* cat = findById(m_storage->categoriesRef(), id);
    if (!cat) return;
    cat->name = newName;
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
}

void TagManager::deleteCategory(const QString& id) {
    removeById(m_storage->categoriesRef(), id);
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
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
        m_storage->categoriesRef().append(moved); // -> Hauptebene (Wurzel)
    } else {
        // Parent NACH dem Entfernen frisch suchen (Container kann realloziert sein).
        TagCategory* parent = findById(m_storage->categoriesRef(), newParentId);
        if (parent) parent->children.append(moved);
        else        m_storage->categoriesRef().append(moved);   // Absicherung
    }

    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
}

void TagManager::setCategoryUniformColor(const QString& id, bool uniform, const QColor& color,
                                         bool inheritToChildren) {
    TagCategory* cat = findById(m_storage->categoriesRef(), id);
    if (!cat) return;
    cat->uniformColor = uniform;
    cat->inheritColorToChildren = uniform && inheritToChildren;
    if (uniform)
        cat->color = color;
    // NICHT destruktiv: die Eigenfarben der Kinder werden NICHT überschrieben.
    // Die Vererbung wird beim Aufbau des Baums (TagController::buildNodes) rein
    // rechnerisch angewandt -> beim Deaktivieren kehrt jede Farbe automatisch
    // zur Eigenfarbe zurück (Anforderung: "restore original color").
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
}

void TagManager::addTagToCategory(const QString& catId, const QString& tag) {
    m_storage->ensureTagRegistered(tag);
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat) return;
    if (!cat->tags.contains(tag)) cat->tags.append(tag);
    // Emit first so UI updates, then save - prevents any signal-triggered
    // rebuild from racing with the write.
    scheduleTagsChanged();
    scheduleCategoriesChanged();
    m_storage->saveCurrentFolder();
}

void TagManager::removeTagFromCategory(const QString& catId, const QString& tag) {
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat) return;
    cat->tags.removeAll(tag);
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
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
    scheduleCategoriesChanged();
}

void TagManager::removeFileFromCategory(const QString& catId, const QString& fileName) {
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat || cat->files.removeAll(fileName) == 0) return;
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
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
