#include <QCoreApplication>
#include "tags/TagManager.h"

#include "core/Strings.h"
#include "tags/TagUndoMark.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRunnable>
#include <QSaveFile>

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
    //  Ende des Durchlaufs = Ende des Rueckgaengig-Schritts. Was danach kommt,
    //  ist ein neuer Schritt (s. `beginUndoStep`).
    m_undoStepOpen = false;
    const bool t = m_tagsDirty, c = m_catsDirty;
    m_tagsDirty = m_catsDirty = false;   // VOR dem Melden - ein Empfaenger darf
    if (t) emit tagsChanged();           // erneut aendern, ohne dass es verfaellt
    if (c) emit categoriesChanged();
}

// ── Rueckgaengig fuer Tag-Vorgaenge (s. Header) ──────────────────────────────
void TagManager::beginUndoStep(const mg::tagmark::Mark& mark) {
    if (!m_storage) return;
    //  Jede neue Mutation macht den Wiederherstellen-Stapel hinfaellig.
    if (!m_redo.isEmpty()) { m_redo.clear(); emit undoStackChanged(); }
    //  In einer Gruppe entsteht GENAU EIN Schritt - beim ersten Mal, mit der
    //  Marke der Gruppe.
    if (m_undoGroupDepth > 0) {
        if (m_undoGroupStepId != 0) return;
    } else if (m_undoStepOpen) {
        return;                            // schon ein Schritt in diesem Durchlauf
    }

    UndoStep step;
    step.id     = m_undoNextId++;
    step.mark   = (m_undoGroupDepth > 0 && m_undoGroupHasMark) ? m_undoGroupMark : mark;
    step.folder = m_storage->folderPath();
    step.state  = m_storage->tagStateSnapshot();
    step.bytes  = step.state.size();
    m_undo.append(step);
    m_undoBytes += step.bytes;
    if (m_undoGroupDepth > 0) m_undoGroupStepId = step.id;
    pruneUndo();

    //  Ohne laufende Ereignisschleife kann der Null-Timer die Sammelgrenze nie
    //  setzen - dann wird auch nicht gesammelt (Testtreiber). Mit Schleife wird
    //  der Timer sicherheitshalber angestossen, selbst wenn keine Meldung
    //  ansteht: er ist es, der `m_undoStepOpen` wieder zurueckstellt.
    if (QCoreApplication::instance()) {
        m_undoStepOpen = true;
        if (!m_signalTimer.isActive()) m_signalTimer.start();
    }
    emit undoStackChanged();
}

void TagManager::beginUndoGroup(const mg::tagmark::Mark& mark, bool counted) {
    if (m_undoGroupDepth == 0) {
        m_undoGroupMark    = mark;
        m_undoGroupHasMark = true;
        m_undoGroupCounted = counted;
        m_undoGroupStepId  = 0;            // der Schritt entsteht erst bei Bedarf
    }
    ++m_undoGroupDepth;
}

void TagManager::endUndoGroup() {
    if (m_undoGroupDepth == 0) return;
    if (--m_undoGroupDepth == 0) {
        m_undoGroupMark    = {};
        m_undoGroupHasMark = false;
        m_undoGroupCounted = false;
        m_undoGroupStepId  = 0;
    }
}

void TagManager::noteForeignFolder(const QString& folderPath, const mg::tagmark::Mark& mark) {
    if (folderPath.isEmpty()) return;
    beginUndoStep(mark);
    if (m_undo.isEmpty()) return;
    UndoStep& step = m_undo.last();

    const QString side = folderPath + QLatin1Char('/')
                       + QFileInfo(folderPath).fileName() + QStringLiteral(".json");
    if (step.foreign.contains(side)) return;      // in diesem Schritt schon gesichert

    QByteArray before;                            // leer = gab es vorher nicht
    QFile f(side);
    if (f.open(QIODevice::ReadOnly)) before = f.readAll();
    if (step.foreign.size() >= kMaxSweepFolders
        || step.bytes + before.size() > kMaxSweepBytes) {
        step.foreignComplete = false;
        return;
    }
    step.bytes += before.size();
    m_undoBytes += before.size();
    step.foreign.insert(side, before);
    pruneUndo();
}

//  Deckel durchsetzen: die AELTESTEN Schritte fallen zuerst - der jüngste ist
//  der, den der Nutzer gleich zurueckzunehmen versucht.
void TagManager::pruneUndo() {
    while (m_undo.size() > kMaxUndoSteps
           || (m_undoBytes > kMaxUndoBytes && m_undo.size() > 1)) {
        m_undoBytes -= m_undo.first().bytes;
        m_undo.removeFirst();
    }
    if (m_undoBytes < 0) m_undoBytes = 0;
}

TagManager::UndoStep* TagManager::undoStepById(quint64 id) {
    for (UndoStep& s : m_undo)
        if (s.id == id) return &s;
    return nullptr;
}

void TagManager::clearUndo() {
    //  `m_sweepsPending` bleibt stehen: der Durchgang ist noch unterwegs und
    //  meldet sich selbst ab (`attachSweepUndo`).
    //  Eine offene Gruppe wird dagegen GESCHLOSSEN: ihr Schritt gehoerte zum
    //  alten Ordner. Ein spaeteres `endUndoGroup` laeuft dann ins Leere - das
    //  ist der sichere Ausgang, sonst schluckte eine nie geschlossene Gruppe
    //  jeden weiteren Schritt.
    m_undoGroupDepth   = 0;
    m_undoGroupCounted = false;
    //  Auch die Sammelgrenze: sonst faellt eine Mutation im SELBEN Durchlauf
    //  wie das Leeren ohne eigenen Schritt hindurch.
    m_undoStepOpen     = false;
    m_undoGroupMark    = {};
    m_undoGroupHasMark = false;
    m_undoGroupStepId  = 0;
    const bool hatteRedo = !m_redo.isEmpty();
    m_redo.clear();
    if (m_undo.isEmpty()) {
        m_undoBytes = 0; m_undoSweepId = 0;
        if (hatteRedo) emit undoStackChanged();
        return;
    }
    m_undo.clear();
    m_undoBytes = 0;
    m_undoSweepId = 0;
    emit undoStackChanged();
}

void TagManager::undoLastStep() { applyStep(m_undo, m_redo, /*redo=*/false); }
void TagManager::redoLastStep() { applyStep(m_redo, m_undo, /*redo=*/true);  }

//  EIN Rumpf fuer beide Richtungen: der oberste Schritt von `from` wird
//  angewandt, und was VORHER da war, wandert als Gegenstueck auf `to`. Die
//  Marke bleibt dieselbe - sie beschreibt den Vorgang, nicht die Richtung.
void TagManager::applyStep(QList<UndoStep>& from, QList<UndoStep>& to, bool redo) {
    if (!m_storage) return;
    if (from.isEmpty() || m_sweepsPending != 0) return;

    UndoStep step = from.takeLast();
    if (&from == &m_undo) {
        m_undoBytes -= step.bytes;
        if (m_undoBytes < 0) m_undoBytes = 0;
    }
    //  Ein noch laufender Unterordner-Durchgang haengt an diesem Schritt - er
    //  darf danach nichts mehr anhaengen.
    if (m_undoSweepId == step.id) m_undoSweepId = 0;
    if (m_undoGroupStepId == step.id) m_undoGroupStepId = 0;

    //  Sicherung: der Stand gehoert zu EINEM Ordner. Zeigt der Speicher
    //  inzwischen woandershin, wird nichts zurueckgeschrieben - lieber kein
    //  Rueckgaengig als ein fremder Ordner mit fremden Tags.
    const QString folder = m_storage->folderPath();
    if (step.folder != folder) {
        m_undo.clear();
        m_redo.clear();
        m_undoBytes = 0;
        emit undoStackChanged();
        return;
    }

    //  Das Gegenstueck: derselbe Vorgang, aber mit dem Stand von JETZT.
    UndoStep back;
    back.id     = m_undoNextId++;
    back.mark   = step.mark;
    back.folder = folder;
    back.state  = m_storage->tagStateSnapshot();
    back.bytes  = back.state.size();
    back.foreignComplete = step.foreignComplete;

    //  Zuerst die fremden Sidecars (Unterordner), dann der offene Ordner.
    int restored = 0;
    for (auto it = step.foreign.cbegin(); it != step.foreign.cend(); ++it) {
        //  Fuer den Rueckweg festhalten, wie die Datei JETZT aussieht.
        QByteArray jetzt;
        QFile cur(it.key());
        if (cur.open(QIODevice::ReadOnly)) jetzt = cur.readAll();
        back.foreign.insert(it.key(), jetzt);
        back.bytes += jetzt.size();

        if (it.value().isEmpty()) {                 // gab es vorher nicht
            if (QFile::exists(it.key()) && QFile::remove(it.key())) ++restored;
            continue;
        }
        QSaveFile f(it.key());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) continue;
        if (f.write(it.value()) != it.value().size()) { f.cancelWriting(); continue; }
        if (f.commit()) ++restored;
    }

    m_storage->restoreTagState(step.state);
    if (!folder.isEmpty()) m_storage->saveFolder(folder);

    to.append(back);
    if (&to == &m_undo) {
        m_undoBytes += back.bytes;
        pruneUndo();
    }

    scheduleTagsChanged();
    scheduleCategoriesChanged();
    emit undoStackChanged();
    emit tagUndoApplied(mg::tagmark::plain(step.mark.forward), restored,
                        step.foreignComplete, redo);
}

//  Der Unterordner-Durchgang ist durch - seine Schnappschuesse gehoeren an den
//  Schritt, der das Loeschen ausgeloest hat. Ist der inzwischen weg (schon
//  zurueckgenommen, aus dem Stapel gefallen), wird nichts angehaengt.
void TagManager::attachSweepUndo(quint64 stepId,
                                 const QHash<QString, QByteArray>& before,
                                 bool complete) {
    if (stepId == 0) return;
    //  Erst abmelden - auch wenn der Schritt inzwischen weg ist (Ordnerwechsel,
    //  aus dem Stapel gefallen); sonst bliebe `canUndo` fuer immer falsch.
    if (m_sweepsPending > 0) --m_sweepsPending;
    UndoStep* step = undoStepById(stepId);
    if (!step) { emit undoStackChanged(); return; }
    //  HINEINMISCHEN, nicht ersetzen: im selben Durchlauf koennen zwei Tags
    //  geloescht worden sein - dann laufen zwei Durchgaenge auf denselben
    //  Schritt zu, und der zweite haette den ersten sonst weggeworfen. Wo
    //  beide denselben Ordner kennen, gilt der AELTERE Stand: er liegt weiter
    //  zurueck, und genau dorthin soll das Rueckgaengig fuehren.
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (step->foreign.contains(it.key())) continue;
        step->foreign.insert(it.key(), it.value());
        step->bytes  += it.value().size();
        m_undoBytes  += it.value().size();
    }
    step->foreignComplete = step->foreignComplete && complete;
    pruneUndo();
    emit undoStackChanged();
}

// ── Die Marken der einzelnen Vorgaenge (s. `TagUndoMark.h`) ──────────────────
namespace {
using namespace mg::tagmark;

//  Ist die Kategorie eine Wurzel oder eine Unterkategorie? Das entscheidet der
//  Pfad: leer = Hauptebene.
Thing catThing(const QStringList& path) {
    return path.isEmpty() ? Thing::Category : Thing::Subcategory;
}
}  // namespace

//  Eine Zuordnung: eroeffnet einen Schritt und schreibt seine `+n/-m`-Marke
//  fort. Der Schritt sammelt, was im selben Durchlauf bzw. in derselben
//  Zuordnungs-Sitzung anfaellt - daran haengt, ob „drei einzelne Klicks" drei
//  Schritte sind oder einer (Festlegung des Nutzers: EINZELN, ausser es war
//  wirklich eine Gruppe).
void TagManager::beginCountedStep(bool added, mg::tagmark::Thing t,
                                  const QString& name, const QStringList& path) {
    //  Gehoert die Marke einer FREMDEN Gruppe (Konverter), bleibt sie stehen -
    //  eine Zuordnung, die nebenbei darin passiert, ist nicht der Vorgang.
    const bool fremdeGruppe = m_undoGroupDepth > 0 && m_undoGroupHasMark
                              && !m_undoGroupCounted;
    beginUndoStep(mkCounted(added ? 1 : 0, added ? 0 : 1, t, name, path));
    if (m_undo.isEmpty() || fremdeGruppe) return;

    UndoStep& step = m_undo.last();
    if (!step.addCounts) {                 // erster Zaehler dieses Schrittes
        step.addCounts = true;
        step.cntThing  = t;
        step.cntName   = name;
        step.cntPath   = path;
    } else if (step.cntThing != t || step.cntName != name || step.cntPath != path) {
        step.cntMixed = true;              // mehrere Gegenstaende in EINEM Schritt
    }
    if (added) ++step.addN; else ++step.delN;
    step.mark = step.cntMixed
                    ? mkCounted(step.addN, step.delN, step.cntThing, QString(), {})
                    : mkCounted(step.addN, step.delN, step.cntThing,
                                step.cntName, step.cntPath);
}

// ── Tag basics ────────────────────────────────────────────────────────────────
QStringList TagManager::allTags() const { return m_storage->allTags(); }
QColor      TagManager::tagColor(const QString& tag) const { return m_storage->tagColor(tag); }

void TagManager::setTagColor(const QString& tag, const QColor& c) {
    beginUndoStep(mg::tagmark::mkRecolor(mg::tagmark::Thing::Tag, tag, {},
                                       m_storage->tagColor(tag), c));
    m_storage->setTagColor(tag, c);
    m_storage->saveCurrentFolder();
    emit tagColorChanged(tag, c);
    scheduleCategoriesChanged();
}

void TagManager::addTagToFile(const QString& fileName, const QString& tag) {
    QStringList tags = m_storage->getTags(fileName);
    if (!tags.contains(tag)) {
        beginCountedStep(true, mg::tagmark::Thing::Tag, tag, {});
        tags.append(tag);
        m_storage->setTags(fileName, tags);
        m_storage->saveCurrentFolder();
        scheduleTagsChanged();
    }
}

void TagManager::createTag(const QString& name, const QColor& color) {
    if (name.trimmed().isEmpty()) return;
    beginUndoStep(mg::tagmark::mkSimple(mg::tagmark::Verb::Create,
                                      mg::tagmark::Thing::Tag, name.trimmed(), {}));
    m_storage->ensureTagRegistered(name.trimmed());
    if (color.isValid())
        m_storage->setTagColor(name.trimmed(), color);
    m_storage->saveCurrentFolder();
    scheduleTagsChanged();
}

void TagManager::removeTagFromFile(const QString& fileName, const QString& tag) {
    QStringList tags = m_storage->getTags(fileName);
    if (tags.contains(tag)) {
        beginCountedStep(false, mg::tagmark::Thing::Tag, tag, {});
        tags.removeAll(tag);
        m_storage->setTags(fileName, tags);
        m_storage->saveCurrentFolder();
        scheduleTagsChanged();
    }
}

QStringList TagManager::tagsForFile(const QString& fileName) const {
    return m_storage->getTags(fileName);
}

QStringList TagManager::filesWithTag(const QString& tag) const {
    return m_storage->filesWithTag(tag);
}

void TagManager::deleteTag(const QString& tag) {
    beginUndoStep(mg::tagmark::mkSimple(mg::tagmark::Verb::Delete,
                                      mg::tagmark::Thing::Tag, tag, {}));
    //  Der Unterordner-Durchgang, den `tagDeleted` gleich anstoesst, haengt
    //  seine Schnappschuesse an DIESEN Schritt (s. `sweepSubfolders`).
    m_undoSweepId = m_undo.isEmpty() ? 0 : m_undo.last().id;
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
        m_undoSweepId = 0;              // sonst griffe der naechste Durchgang danach
        emit subfolderSweepFinished(tag, 0);
        return;
    }
    m_sweepPool.setMaxThreadCount(1);
    //  An DIESEN Rueckgaengig-Schritt haengt der Durchgang seine
    //  Schnappschuesse (gesetzt von `deleteTag`, 0 = keiner). Solange er
    //  unterwegs ist, ist der Schritt nicht umkehrbar (s. `canUndo`).
    const quint64 undoId = m_undoSweepId;
    m_undoSweepId = 0;
    if (undoId != 0) {
        ++m_sweepsPending;
        emit undoStackChanged();
    }

    class SweepTask : public QRunnable {
    public:
        SweepTask(TagManager* owner, QString root, QString tag, quint64 undoId)
            : m_owner(owner), m_root(std::move(root)), m_tag(std::move(tag)),
              m_undoId(undoId) {
            setAutoDelete(true);
        }
        void run() override {
            int touched = 0;
            //  Fuer das Rueckgaengig: der ROHE Inhalt jedes angefassten
            //  Sidecars, BEVOR er ueberschrieben wird. Gedeckelt (§0-Prio 4) -
            //  reisst der Deckel, bleibt der offene Ordner umkehrbar und der
            //  Baum nicht; `complete` sagt das dem Nutzer.
            QHash<QString, QByteArray> before;
            qint64 beforeBytes = 0;
            bool   complete = true;
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
                if (m_undoId != 0) {
                    QByteArray raw;
                    QFile f(side);
                    if (f.open(QIODevice::ReadOnly)) raw = f.readAll();
                    if (before.size() >= kMaxSweepFolders
                        || beforeBytes + raw.size() > kMaxSweepBytes) {
                        complete = false;
                    } else {
                        beforeBytes += raw.size();
                        before.insert(side, raw);
                    }
                }
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
            const quint64 undoId = m_undoId;
            QMetaObject::invokeMethod(owner,
                [owner, tag, touched, undoId, before, complete] {
                owner->attachSweepUndo(undoId, before, complete);
                emit owner->subfolderSweepFinished(tag, touched);
            }, Qt::QueuedConnection);
        }
    private:
        TagManager* m_owner;
        QString     m_root;
        QString     m_tag;
        quint64     m_undoId;
    };

    m_sweepPool.start(new SweepTask(this, rootFolder, tag, undoId));
}

void TagManager::renameTag(const QString& oldName, const QString& newName) {
    beginUndoStep(mg::tagmark::mkTransition(mg::tagmark::Verb::Rename,
                                          mg::tagmark::Thing::Tag, oldName, {},
                                          mg::tagmark::Thing::Tag, newName, {}));
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
    //  UMBENENNEN, nicht loeschen-und-neu-anlegen: der alte Weg nahm den Tag
    //  jeder Datei weg (Nutzerbefund 2026-09-03, nachgemessen).
    m_storage->renameTag(oldName, newName);
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
    beginUndoStep(mg::tagmark::mkSimple(mg::tagmark::Verb::Create,
                                      mg::tagmark::Thing::Category, cat.name, {}));
    m_storage->categoriesRef().append(cat);
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
}

void TagManager::addSubcategory(const QString& parentId, const TagCategory& sub) {
    TagCategory* parent = findById(m_storage->categoriesRef(), parentId);
    if (!parent) return;
    {
        QStringList p = mg::tagmark::pathOf(m_storage->categoriesRef(), parentId);
        p.append(parent->name);
        beginUndoStep(mg::tagmark::mkSimple(mg::tagmark::Verb::Create,
                                          mg::tagmark::Thing::Subcategory, sub.name, p));
    }
    parent->children.append(sub);
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
}

void TagManager::renameCategory(const QString& id, const QString& newName) {
    TagCategory* cat = findById(m_storage->categoriesRef(), id);
    if (!cat) return;
    {
        const QStringList p = mg::tagmark::pathOf(m_storage->categoriesRef(), id);
        beginUndoStep(mg::tagmark::mkTransition(mg::tagmark::Verb::Rename,
                                              catThing(p), cat->name, p,
                                              catThing(p), newName,   p));
    }
    cat->name = newName;
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
}

void TagManager::deleteCategory(const QString& id) {
    const TagCategory* doomed = findById(m_storage->categoriesRef(), id);
    if (!doomed) return;
    {
        const QStringList p = mg::tagmark::pathOf(m_storage->categoriesRef(), id);
        beginUndoStep(mg::tagmark::mkSimple(mg::tagmark::Verb::Delete,
                                            catThing(p), doomed->name, p));
    }
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

    {
        const QStringList alt = mg::tagmark::pathOf(m_storage->categoriesRef(), id);
        QStringList neu;
        if (!newParentId.isEmpty()) {
            neu = mg::tagmark::pathOf(m_storage->categoriesRef(), newParentId);
            if (const TagCategory* np = findById(m_storage->categoriesRef(), newParentId))
                neu.append(np->name);
        }
        beginUndoStep(mg::tagmark::mkTransition(mg::tagmark::Verb::Move,
                                              catThing(alt), node->name, alt,
                                              catThing(neu), node->name, neu));
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
    {
        const QStringList p = mg::tagmark::pathOf(m_storage->categoriesRef(), id);
        beginUndoStep(mg::tagmark::mkRecolor(catThing(p), cat->name, p,
                                           cat->uniformColor ? cat->color : QColor(),
                                           uniform ? color : QColor()));
    }
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
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat) return;
    {
        QStringList p = mg::tagmark::pathOf(m_storage->categoriesRef(), catId);
        p.append(cat->name);
        beginUndoStep(mg::tagmark::mkSimple(mg::tagmark::Verb::Create,
                                          mg::tagmark::Thing::Tag, tag, p));
    }
    m_storage->ensureTagRegistered(tag);
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
    {
        QStringList p = mg::tagmark::pathOf(m_storage->categoriesRef(), catId);
        p.append(cat->name);
        beginUndoStep(mg::tagmark::mkSimple(mg::tagmark::Verb::Remove,
                                          mg::tagmark::Thing::Tag, tag, p));
    }
    cat->tags.removeAll(tag);
    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
}

void TagManager::moveTagToCategory(const QString& tag,
                                   const QString& fromCatId,
                                   const QString& toCatId) {
    //  Zwei Mutationen, EIN Rueckgaengig-Schritt (s. Header).
    {
        QStringList von = mg::tagmark::pathOf(m_storage->categoriesRef(), fromCatId);
        if (const TagCategory* c = findById(m_storage->categoriesRef(), fromCatId))
            von.append(c->name);
        QStringList nach = mg::tagmark::pathOf(m_storage->categoriesRef(), toCatId);
        if (const TagCategory* c = findById(m_storage->categoriesRef(), toCatId))
            nach.append(c->name);
        beginUndoGroup(mg::tagmark::mkTransition(mg::tagmark::Verb::Move,
                                               mg::tagmark::Thing::Tag, tag, von,
                                               mg::tagmark::Thing::Tag, tag, nach));
    }
    removeTagFromCategory(fromCatId, tag);
    addTagToCategory(toCatId, tag);
    endUndoGroup();
}

// ── Datei ↔ Kategorie (direkte Mitgliedschaft) ────────────────────────────────
void TagManager::addFileToCategory(const QString& catId, const QString& fileName) {
    if (fileName.isEmpty()) return;
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat || cat->files.contains(fileName)) return;   // idempotent
    {
        const QStringList p = mg::tagmark::pathOf(m_storage->categoriesRef(), catId);
        beginCountedStep(true, catThing(p), cat->name, p);
    }
    cat->files.append(fileName);
    m_storage->saveCurrentFolder();
    // categoriesChanged zieht den Proxy-Kategoriefilter (m_activeCatFiles) und
    // alle QML-Ansichten (fileCount, Panels) nach.
    scheduleCategoriesChanged();
}

void TagManager::removeFileFromCategory(const QString& catId, const QString& fileName) {
    TagCategory* cat = findById(m_storage->categoriesRef(), catId);
    if (!cat || !cat->files.contains(fileName)) return;
    {
        const QStringList p = mg::tagmark::pathOf(m_storage->categoriesRef(), catId);
        beginCountedStep(false, catThing(p), cat->name, p);
    }
    cat->files.removeAll(fileName);
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

QList<int> TagManager::indexPath(const QList<TagCategory>& list, const QString& id) {
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).id == id) return { i };
        const QList<int> tief = indexPath(list.at(i).children, id);
        if (!tief.isEmpty()) return QList<int>{ i } + tief;
    }
    return {};
}

TagCategory* TagManager::atPath(QList<TagCategory>& list, const QList<int>& path) {
    if (path.isEmpty()) return nullptr;
    QList<TagCategory>* cur = &list;
    TagCategory* node = nullptr;
    for (int idx : path) {
        if (idx < 0 || idx >= cur->size()) return nullptr;
        node = &(*cur)[idx];
        cur  = &node->children;
    }
    return node;
}

bool TagManager::subtreeContains(const TagCategory& node, const QString& id) {
    if (node.id == id) return true;
    for (const TagCategory& c : node.children)
        if (subtreeContains(c, id)) return true;
    return false;
}

void TagManager::swapCategories(const QString& aId, const QString& bId) {
    if (aId.isEmpty() || bId.isEmpty() || aId == bId) return;
    QList<TagCategory>& roots = m_storage->categoriesRef();
    const TagCategory* na = findById(roots, aId);
    const TagCategory* nb = findById(roots, bId);
    if (!na || !nb) return;

    const bool bInA = subtreeContains(*na, bId);
    const bool aInB = subtreeContains(*nb, aId);

    const QStringList pa = mg::tagmark::pathOf(roots, aId);
    const QStringList pb = mg::tagmark::pathOf(roots, bId);
    beginUndoStep(mg::tagmark::mkTransition(
        mg::tagmark::Verb::Move,
        pa.isEmpty() ? mg::tagmark::Thing::Category
                     : mg::tagmark::Thing::Subcategory, na->name, pa,
        pb.isEmpty() ? mg::tagmark::Thing::Category
                     : mg::tagmark::Thing::Subcategory, nb->name, pb));

    if (!bInA && !aInB) {
        //  Unabhaengig: beide Plaetze ueber ihren INDEXWEG beschreiben. Die
        //  Wege bleiben gueltig, weil sich an keiner Liste die Groesse aendert.
        const QList<int> wa = indexPath(roots, aId);
        const QList<int> wb = indexPath(roots, bId);
        TagCategory* pA = atPath(roots, wa);
        TagCategory* pB = atPath(roots, wb);
        if (!pA || !pB) return;
        const TagCategory kopieA = *pA;
        const TagCategory kopieB = *pB;
        *atPath(roots, wa) = kopieB;
        *atPath(roots, wb) = kopieA;
    } else {
        //  Einer ist Vorfahr des anderen: der NACHFAHR nimmt den Platz des
        //  Vorfahren ein, der Vorfahr wird sein Kind. Der Vorfahr behaelt dabei
        //  alle seine uebrigen Kinder - nur der Nachfahr wechselt die Seite.
        const QString hochId = bInA ? aId : bId;
        const QString tiefId = bInA ? bId : aId;

        TagCategory tief = *findById(roots, tiefId);   // tiefe Kopie
        removeById(roots, tiefId);                     // aus dem Vorfahren heraus

        const QList<int> wh = indexPath(roots, hochId);
        TagCategory* hoch = atPath(roots, wh);
        if (!hoch) return;
        tief.children.append(*hoch);                   // Vorfahr wird sein Kind
        *atPath(roots, wh) = tief;                     // an dieselbe Stelle
    }

    m_storage->saveCurrentFolder();
    scheduleCategoriesChanged();
}

bool TagManager::removeById(QList<TagCategory>& list, const QString& id) {
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].id == id) { list.removeAt(i); return true; }
        if (removeById(list[i].children, id)) return true;
    }
    return false;
}
