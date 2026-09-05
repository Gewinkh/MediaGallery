#include "media/MediaModel.h"

#include "media/ContentSniff.h"

#include "core/AppSettings.h"
#include "core/JsonStorage.h"
#include "core/Strings.h"
#include "media/MediaProxyModel.h"
#include "tags/TagCategory.h"
#include "tags/TagManager.h"
#include "tags/TagUndoMark.h"
#include "media/ThumbnailLoader.h"

#include <QFileSystemWatcher>
#include <QDir>
#include "core/PathUtils.h"

#include <QDirIterator>
#include <QTimeZone>
#include <QFileInfo>
#include <QFile>
#include <QUuid>
#include <functional>
#include <algorithm>
#include <QPointer>
#include <QRunnable>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>

#include "core/MemoryUtils.h"   // mg::trimHeap - RSS-Rückgabe nach Ordnerwechsel

// MG_LOG_THUMBS=1 protokolliert Anforderung, Abbestellung und Lieferung.
static const bool kLogThumbs = qEnvironmentVariableIntValue("MG_LOG_THUMBS") == 1;

namespace {

// Ein Verzeichnis kann Zehntausende Eintraege haben - auf dem GUI-Thread waere
// das ein Ruckler je sichtbarer Ordnerkachel.
class FolderCountTask : public QRunnable {
public:
    FolderCountTask(QPointer<MediaModel> owner, QString folder, QString sidecar,
                    bool showAll, int generation, int ticket)
        : m_owner(std::move(owner)), m_folder(std::move(folder))
        , m_sidecar(std::move(sidecar)), m_showAll(showAll), m_generation(generation)
        , m_ticket(ticket) {
        setAutoDelete(true);
    }

    void run() override {
        int n = 0;
        QDirIterator it(m_folder, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | MediaModel::hiddenFlag(),
                        QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isDir()) { ++n; continue; }
            if (!m_showAll && mg::isCompanionFile(fi.fileName(), m_sidecar)) continue;
            if (!m_showAll && MediaItem::detectType(fi.filePath()) == MediaType::Unknown)
                continue;
            ++n;
        }
        // Zugestellt wird am Besitzer, nicht an qApp: das Makro castet auf
        // QGuiApplication, was in einem Prozess ohne GUI ungueltig ist.
        MediaModel* owner = m_owner.data();
        if (!owner) return;
        const QString folder = m_folder;
        const int gen = m_generation;
        const int ticket = m_ticket;
        // Kein zweiter QPointer im Lambda - er traegt einen Referenzzaehler ueber die
        // Thread-Grenze, den Helgrind als Rennen meldet.
        QMetaObject::invokeMethod(owner, [owner, folder, n, gen, ticket]() {
            owner->noteFolderCount(folder, n, gen, ticket);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<MediaModel> m_owner;
    QString m_folder;
    QString m_sidecar;
    bool    m_showAll = false;
    int     m_generation = 0;
    int     m_ticket = 0;
};

// Je Ordner einmal das Sidecar lesen, dann jede Datei mit
// MediaProxyModel::acceptsFile pruefen - derselben Regel wie die Anzeige.
class DeepScanTask : public QRunnable {
public:
    DeepScanTask(QPointer<MediaModel> owner, QString root,
                 MediaProxyModel::FilterCriteria crit, QStringList categoryNames,
                 bool showAll, int generation,
                 std::shared_ptr<std::atomic<bool>> cancel)
        : m_owner(std::move(owner)), m_root(std::move(root))
        , m_crit(std::move(crit)), m_categoryNames(std::move(categoryNames))
        , m_showAll(showAll), m_generation(generation), m_cancel(std::move(cancel)) {
        setAutoDelete(true);
    }

    // Rein funktional, damit es gleichzeitig auf mehreren Faeden laufen darf.
    bool scanOne(const QString& dir, QStringList* subdirs) const {
        const bool isRoot = (dir == m_root);
            const QString sidecar = mg::folderSidecarName(dir);

            // Eigene Instanz im Worker; sie liest nur.
            JsonStorage st;
            st.loadFolder(dir);

            MediaProxyModel::FilterCriteria crit = m_crit;
            if (crit.categoryActive) {
                QSet<QString> files;
                std::function<void(const QList<TagCategory>&)> walk =
                    [&](const QList<TagCategory>& list) {
                        for (const TagCategory& c : list) {
                            if (m_categoryNames.contains(c.name))
                                for (const QString& f : c.files) files.insert(f);
                            walk(c.children);
                        }
                    };
                walk(st.categoriesRef());
                crit.catFiles = files;
            }

            bool hit = !isRoot && !crit.search.isEmpty()
                    && crit.pattern.contains(QFileInfo(dir).fileName());

            QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | MediaModel::hiddenFlag(),
                            QDirIterator::NoIteratorFlags);
            while (it.hasNext()) {
                if (m_cancel && m_cancel->load()) return false;
                it.next();
                const QFileInfo fi = it.fileInfo();
                if (fi.isDir()) {
                    if (!fi.isSymLink()) subdirs->append(fi.filePath());
                    continue;
                }
                if (isRoot || hit) continue;   // Wurzel ist ohnehin sichtbar
                if (!m_showAll && mg::isCompanionFile(fi.fileName(), sidecar)) continue;
                // Auch die Tiefensuche muss die mehrdeutige Endung nachbessern - sonst zaehlte
                // eine TypeScript-Datei beim Filter nur Videos als Video.
                const MediaType t = mg::refineType(fi.filePath(),
                                                   MediaItem::detectType(fi.filePath()));
                if (t == MediaType::Unknown && !m_showAll) continue;

                const QString name = fi.completeBaseName();
                if (MediaProxyModel::acceptsFile(static_cast<int>(t), name, fi.fileName(),
                                                 st.getTags(fi.fileName()), crit)) {
                    hit = true;
                }
            }
        return hit;
    }

    void run() override {
        // Einfaedig: billig (259 Ordner in wenigen Millisekunden), und der Baum ist
        // erst danach bekannt.
        QStringList dirs;
        {
            QStringList stack{ m_root };
            while (!stack.isEmpty() && dirs.size() < 20000) {
                if (m_cancel && m_cancel->load()) return;
                const QString dir = stack.takeLast();
                dirs.append(dir);
                QDirIterator it(dir, QDir::Dirs | QDir::NoDotAndDotDot,
                                QDirIterator::NoIteratorFlags);
                while (it.hasNext()) {
                    it.next();
                    const QFileInfo fi = it.fileInfo();
                    if (!fi.isSymLink()) stack.append(fi.filePath());
                }
            }
        }

        // Die Zahl der Faeden kommt vom Rechner (idealThreadCount); jeder Ordner ist
        // unabhaengig - eigener Sidecar, eigene Dateien.
        const int wanted = qEnvironmentVariableIntValue("MG_DEEPTHREADS");
        const int threads = wanted > 0 ? qBound(1, wanted, 64)
                                       : qBound(1, QThread::idealThreadCount(), 8);
        QElapsedTimer scanTimer;
        if (qEnvironmentVariableIntValue("MG_DEEPLOG") >= 1) scanTimer.start();
        QStringList hits;
        if (threads <= 1 || dirs.size() < 8) {
            for (const QString& d : std::as_const(dirs)) {
                if (m_cancel && m_cancel->load()) return;
                QStringList ignored;
                if (scanOne(d, &ignored)) hits.append(d);
            }
        } else {
            // Ein Pool nur fuer diesen Lauf, damit zwischen zwei Suchen keine Faeden im
            // Leerlauf haengen.
            QThreadPool pool;
            pool.setMaxThreadCount(threads);
            std::atomic<int> next{ 0 };
            QVector<QStringList> perThread(threads);
            QMutex done;
            QWaitCondition wake;
            int running = threads;

            for (int w = 0; w < threads; ++w) {
                pool.start([&, w] {
                    QStringList mine;
                    for (;;) {
                        const int i = next.fetch_add(1, std::memory_order_relaxed);
                        if (i >= dirs.size()) break;
                        if (m_cancel && m_cancel->load()) break;
                        QStringList ignored;
                        if (scanOne(dirs.at(i), &ignored)) mine.append(dirs.at(i));
                    }
                    QMutexLocker lock(&done);
                    perThread[w] = std::move(mine);
                    if (--running == 0) wake.wakeAll();
                });
            }
            {
                QMutexLocker lock(&done);
                while (running > 0) wake.wait(&done);
            }
            pool.waitForDone();
            if (m_cancel && m_cancel->load()) return;

            // Die Treffer sollen aufklappen wie der Nutzer den Baum sieht, nicht wie die
            // Faeden fertig wurden.
            QSet<QString> found;
            for (const QStringList& l : std::as_const(perThread))
                for (const QString& d : l) found.insert(d);
            for (const QString& d : std::as_const(dirs))
                if (found.contains(d)) hits.append(d);
        }

        // Nur mit MG_DEEPLOG=1: Dauer des Suchens ohne das Aufklappen danach.
        if (scanTimer.isValid())
            qInfo("[MG_DEEPLOG] %lld Ordner auf %d Faeden: %lld ms, %lld Treffer",
                  dirs.size(), threads, scanTimer.elapsed(), hits.size());

        MediaModel* owner = m_owner.data();
        if (!owner) return;
        const int gen = m_generation;
        QMetaObject::invokeMethod(owner, [owner, hits, gen]() {
            owner->noteDeepMatches(hits, gen);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<MediaModel> m_owner;
    QString              m_root;
    MediaProxyModel::FilterCriteria m_crit;
    QStringList          m_categoryNames;
    bool                 m_showAll = false;
    int                  m_generation = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

} // namespace

namespace {
// Erste Charge fuellt typische Viewports; Folgechargen sind groesser, da sie
// zwischen Event-Loop-Ticks laufen.
constexpr int kFirstChunk = 256;
constexpr int kChunk      = 512;
}

MediaModel::MediaModel(JsonStorage& storage,
                       TagManager& tagManager,
                       ThumbnailLoader& loader,
                       QObject* parent)
    : QAbstractListModel(parent)
    , m_storage(storage)
    , m_tagManager(tagManager)
    , m_loader(loader)
    , m_watcher(new QFileSystemWatcher(this))
{
    // Nur der Sidecar des offenen Ordners sammelt Schreibvorgaenge - dort fallen sie
    // im Rudel an, und jeder einzelne serialisiert die ganze Datei.
    m_storage.setDeferredSaves(true);

    connect(&m_loader, &ThumbnailLoader::thumbnailReady,
            this, &MediaModel::onThumbnailReady, Qt::QueuedConnection);
    connect(&m_loader, &ThumbnailLoader::thumbnailFailed,
            this, &MediaModel::onThumbnailFailed, Qt::QueuedConnection);

    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this]() { m_watchDebounce.start(); });

    m_watchDebounce.setSingleShot(true);
    m_watchDebounce.setInterval(400);
    connect(&m_watchDebounce, &QTimer::timeout, this, &MediaModel::onDirectoryChanged);

    m_cancelTimer.setSingleShot(true);
    m_cancelTimer.setInterval(0);
    connect(&m_cancelTimer, &QTimer::timeout, this, [this]() {
        for (const QString& p : std::as_const(m_cancelPending)) {
            const int row = rowForPath(p);
            if (row >= 0 && m_thumbState[row] == 1) continue;   // inzwischen da
            // Hat jemand im selben Durchlauf denselben Pfad angefordert, ist das die
            // uebernehmende Kachel - ihre Anforderung darf nicht weggeraeumt werden.
            if (m_thumbWanted.contains(p)) {
                if (kLogThumbs)
                    qInfo("[thumb] abbestellen VERWORFEN (uebernommen) %s",
                          qPrintable(QFileInfo(p).fileName()));
                continue;
            }
            if (kLogThumbs)
                qInfo("[thumb] abbestellen AUSGEFUEHRT %s",
                      qPrintable(QFileInfo(p).fileName()));
            m_loader.cancelThumbnail(p);
        }
        m_cancelPending.clear();
        m_thumbWanted.clear();
    });

    m_fillTimer.setSingleShot(false);
    m_fillTimer.setInterval(0);   // „sobald die Event-Loop atmet“ - kein Blockieren
    connect(&m_fillTimer, &QTimer::timeout, this, [this]() {
        feedChunk(/*firstChunk=*/false);
        if (!hasMoreToFill()) {
            m_fillTimer.stop();
            finishFill();
        }
    });

    // Nur Zeilen des geoeffneten Ordners: m_tagManager schluesselt nach Dateinamen,
    // eine gleichnamige Datei im Unterordner bekaeme sonst fremde Tags.
    connect(&m_tagManager, &TagManager::tagsChanged, this, [this]() {
        if (m_items.isEmpty()) return;
        for (auto& it : m_items) {
            if (it.scope != 0 || it.isFolder()) continue;
            it.tags = m_tagManager.tagsForFile(it.fileName());
        }
        emit dataChanged(index(0), index(m_items.size() - 1), { TagsRole });
    });
}

// Der Zaehl-Pool wird hier geleert, nicht erst als Kind in ~QObject: Kinder
// sterben nach dem Rumpf, ein laufender Auftrag saehe einen halben Besitzer.
MediaModel::~MediaModel() {
    if (m_deepCancel) m_deepCancel->store(true);
    if (m_countPool) {
        m_countPool->clear();
        m_countPool->waitForDone();
    }
    if (m_deepPool) {
        m_deepPool->clear();
        m_deepPool->waitForDone();
    }
}

void MediaModel::rebuild(const QString& folderPath) {
    m_fillTimer.stop();
    m_pendingIt.reset();
    m_pendingScope = -1;
    m_scanQueue.clear();
    m_cancelTimer.stop();
    m_cancelPending.clear();
    m_thumbWanted.clear();

    // Leeres Modell sofort publizieren, damit die UI ohne Verzoegerung rendert.
    beginResetModel();
    m_items.clear();
    m_thumbUrls.clear();
    m_thumbState.clear();
    m_selected.clear();
    recountSelection();
    m_pathToRow.clear();
    // m_expanded bleibt stehen: daraus setzt sich der Unterbaum waehrend des
    // Einlesens von selbst zusammen - ein Watcher-Reload klappt nicht alles zu.
    m_scopes.clear();
    m_scopeOfPath.clear();
    qDeleteAll(m_scopeStorage);
    m_scopeStorage.clear();
    // Gezaehlte Staende sind nach einem Neuaufbau nicht mehr zu trauen.
    ++m_countGeneration;
    m_folderCounts.clear();
    m_countPending.clear();
    // Die Marken steigen nur - Zuruecksetzen koennte einen laufenden Auftrag
    // wieder gueltig machen.
    endResetModel();
    emit countChanged();

    if (folderPath.isEmpty())
        return;

    // Der Sidecar-Name gehoert zum BEREICH: jeder aufgeklappte Unterordner hat
    // seinen eigenen.
    FolderScope root;
    root.path      = folderPath;
    // Ueber den Helfer, nicht QFileInfo::fileName: bei einem Pfad mit Trenner am
    // Ende liefert der einen Leerstring, und die Ordner-JSON stand als Kachel da.
    root.sidecar   = mg::folderSidecarName(folderPath);
    root.parent    = -1;
    root.depth     = 0;
    root.folderRow = -1;
    root.active    = true;
    m_scopes.append(root);
    m_scopeOfPath.insert(folderPath, 0);

    // Streamend lesen statt vorab als Liste: entryInfoList materialisierte den
    // ganzen Ordner, bevor die erste Kachel sichtbar wurde.
    m_pendingInvalidate = true;

    startScan(0);

    // Erste Charge synchron - der Viewport ist sofort gefuellt, kein Flackern.
    feedChunk(/*firstChunk=*/true);
    if (hasMoreToFill())
        m_fillTimer.start();
    else
        finishFill();
}

// Bei jedem Einlesen frisch gelesen, damit ein Umschalten nach dem naechsten
// Neuladen greift.
QDir::Filters MediaModel::hiddenFlag() {
    return AppSettings::instance().showHiddenFiles() ? QDir::Hidden : QDir::Filters();
}

// Verzeichnisse kommen mit - Unterordner sind jetzt Kacheln.
void MediaModel::startScan(int scope) {
    m_pendingIt.reset();
    m_pendingScope = -1;
    if (scope < 0 || scope >= m_scopes.size()) return;
    const QString path = m_scopes.at(scope).path;
    if (path.isEmpty()) return;
    m_pendingIt = std::make_unique<QDirIterator>(
        path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | MediaModel::hiddenFlag(),
        QDirIterator::NoIteratorFlags);
    m_pendingScope = scope;
}

bool MediaModel::hasMoreToFill() const {
    return (m_pendingIt && m_pendingIt->hasNext()) || !m_scanQueue.isEmpty();
}

QString MediaModel::sidecarOfScope(int scope) const {
    return (scope >= 0 && scope < m_scopes.size()) ? m_scopes.at(scope).sidecar
                                                   : QString();
}

void MediaModel::feedChunk(bool firstChunk) {
    const int budget = firstChunk ? kFirstChunk : kChunk;

    // Immer nur ein Bereich je Charge: nur so laesst sich fuer die ganze Charge auf
    // einen Schlag entscheiden, ob die Sidecar-Metadaten gelten.
    if (!m_pendingIt || !m_pendingIt->hasNext()) {
        if (m_scanQueue.isEmpty()) return;
        startScan(m_scanQueue.takeFirst());
        if (!m_pendingIt) return;
    }

    const int     scope   = m_pendingScope;
    const QString sidecar = sidecarOfScope(scope);

    QVector<MediaItem> batch;
    batch.reserve(budget);

    int produced = 0;
    while (m_pendingIt->hasNext() && produced < budget) {
        m_pendingIt->next();
        const QFileInfo fi = m_pendingIt->fileInfo();

        if (fi.isDir()) {
            MediaItem item;
            item.filePath = fi.filePath();
            // fileName(), nicht completeBaseName(): ein Ordner "Urlaub 2025.alt" heisst so.
            item.displayName = fi.fileName();
            item.type        = MediaType::Folder;
            item.dateTime    = fi.lastModified(QTimeZone::UTC);
            item.fileSize    = 0;
            item.scope       = scope;
            batch.append(std::move(item));
            ++produced;
            continue;
        }

        // Die Regel steht in mg::isCompanionFile, damit der Dateiwaehler gleich filtert.
        if (!m_showAllFiles && mg::isCompanionFile(fi.fileName(), sidecar))
            continue;

        // refineType schaut nur bei mehrdeutigen Endungen in die Datei (heute .ts).
        const MediaType t = mg::refineType(fi.filePath(),
                                           MediaItem::detectType(fi.filePath()));
        // Alle Dateien anzeigen heisst wirklich alle - sonst bliebe die .bak einer DOCX
        // unsichtbar, obwohl sie ausdruecklich gemeint war.
        if (t == MediaType::Unknown && !m_showAllFiles) continue;

        MediaItem item;
        item.filePath    = fi.filePath();
        item.displayName = fi.completeBaseName();
        item.fileSize    = fi.size();
        item.type        = t;
        item.dateTime    = fi.lastModified(QTimeZone::UTC);
        item.scope       = scope;
        batch.append(std::move(item));
        ++produced;
    }

    if (batch.isEmpty())
        return;   // diese Runde enthielt nur übersprungene Einträge

    // Aus dem Sidecar DIESES Bereichs - nur so tragen die Dateien eines
    // Unterordners ihre echten Zuordnungen statt der eines Namensvetters.
    if (JsonStorage* st = storageForScope(scope)) {
        st->applyToItems(batch);
        for (auto& item : batch) {
            if (item.isFolder()) continue;
            const QString name = item.fileName();
            // Kein Datum aus dem Sidecar: es steht an der Datei.
            (void)name;
        }
    }

    const int first = m_items.size();
    const int last  = first + batch.size() - 1;
    beginInsertRows(QModelIndex(), first, last);
    for (auto& item : batch) {
        m_pathToRow.insert(item.filePath, m_items.size());
        m_items.append(std::move(item));
        m_thumbUrls.append(QString());
        m_thumbState.append(0);
        m_selected.append(0);
    }
    endInsertRows();
    emit countChanged();

    queueExpandedFolders(first);
}

// So stellt sich ein ganzer aufgeklappter Unterbaum nach einem reload() von
// selbst wieder her, Ebene fuer Ebene.
void MediaModel::queueExpandedFolders(int firstRow) {
    if (m_expanded.isEmpty()) return;
    for (int r = qMax(0, firstRow); r < m_items.size(); ++r) {
        const MediaItem& it = m_items.at(r);
        if (!it.isFolder()) continue;
        if (!m_expanded.contains(it.filePath)) continue;
        const int existing = m_scopeOfPath.value(it.filePath, -1);
        if (existing >= 0 && m_scopes.at(existing).active) continue;
        const int s = ensureScope(it.filePath, it.scope, r);
        if (s > 0) m_scanQueue.append(s);
    }
}

void MediaModel::finishFill() {
    m_pendingIt.reset();
    m_pendingScope = -1;

    // Nur mit MG_DEEPLOG=1: Dauer des Aufklappens nach einer Suche.
    if (m_deepFillTimer.isValid()) {
        qInfo("[MG_DEEPLOG] Aufklappen: %lld Zeilen im Modell nach %lld ms",
              qint64(m_items.size()), m_deepFillTimer.elapsed());
        m_deepFillTimer.invalidate();
    }

    // Jetzt stehen die Zeilen - jetzt kann die Ansicht ihre Miniaturen anfordern.
    if (m_pendingInvalidate) {
        m_pendingInvalidate = false;
        emit thumbnailsInvalidated();
    }

    // rebuild wirft die gezaehlten Staende weg; angefordert hat sie aber nur die
    // Kachel beim ersten Erscheinen - deshalb hier erneut anstossen.
    if (!m_countWanted.isEmpty()) {
        // Ueber eine Kopie laufen: ensureFolderCount traegt selbst in die Menge ein.
        const QStringList wanted(m_countWanted.cbegin(), m_countWanted.cend());
        for (const QString& folder : wanted) {
            if (rowForPath(folder) < 0) continue;   // in diesem Bestand nicht (mehr) da
            ensureFolderCount(folder);
        }
    }

    // Trim genau hier: der Trim in loadFolder laeuft schon nach der ersten Charge
    // und kann den Speicher des vorherigen Ordners noch nicht vollstaendig fassen.
    mg::trimHeap();
}

int MediaModel::rowForPath(const QString& filePath) const {
    return m_pathToRow.value(filePath, -1);
}

int MediaModel::scopeDepthOf(int scope) const {
    return (scope > 0 && scope < m_scopes.size()) ? m_scopes.at(scope).depth : 0;
}

int MediaModel::scopeParentOf(int scope) const {
    if (scope <= 0 || scope >= m_scopes.size()) return -1;
    return m_scopes.at(scope).parent;
}

QString MediaModel::folderOfScope(int scope) const {
    if (scope < 0 || scope >= m_scopes.size()) return {};
    return m_scopes.at(scope).path;
}

const MediaItem* MediaModel::folderItemOfScope(int scope) const {
    if (scope <= 0 || scope >= m_scopes.size()) return nullptr;
    return itemAt(m_scopes.at(scope).folderRow);
}

int MediaModel::ensureScope(const QString& folderPath, int parentScope, int folderRow) {
    if (folderPath.isEmpty()) return -1;
    int idx = m_scopeOfPath.value(folderPath, -1);
    if (idx < 0) {
        FolderScope sc;
        sc.path    = folderPath;
        sc.sidecar = mg::folderSidecarName(folderPath);
        idx = m_scopes.size();
        m_scopes.append(sc);
        m_scopeOfPath.insert(folderPath, idx);
    }
    const int depth = scopeDepthOf(parentScope) + 1;
    FolderScope& sc  = m_scopes[idx];
    sc.parent    = parentScope;
    sc.depth     = depth;
    sc.folderRow = folderRow;
    sc.active    = true;
    return idx;
}

void MediaModel::collectDescendantScopes(int scope, QSet<int>& out) const {
    for (int i = 1; i < m_scopes.size(); ++i) {
        if (!m_scopes.at(i).active || i == scope) continue;
        for (int p = m_scopes.at(i).parent; p > 0; p = m_scopes.at(p).parent) {
            if (p == scope) { out.insert(i); break; }
        }
    }
}

// Die Bereiche selbst bleiben in der Tabelle stehen (s. FolderScope).
void MediaModel::removeRowsOfScopes(const QSet<int>& scopes) {
    if (scopes.isEmpty()) return;

    for (auto it = m_scanQueue.begin(); it != m_scanQueue.end(); ) {
        if (scopes.contains(*it)) it = m_scanQueue.erase(it);
        else                      ++it;
    }
    // Ein laufender Iterator muss sofort weg - sonst speist die naechste Charge
    // Zeilen in einen Bereich ein, den es nicht mehr gibt.
    if (m_pendingScope >= 0 && scopes.contains(m_pendingScope)) {
        m_pendingIt.reset();
        m_pendingScope = -1;
    }

    int removed = 0;
    int i = m_items.size() - 1;
    while (i >= 0) {
        if (!scopes.contains(m_items.at(i).scope)) { --i; continue; }
        const int last = i;
        while (i >= 0 && scopes.contains(m_items.at(i).scope)) --i;
        const int first = i + 1;
        const int n     = last - first + 1;
        beginRemoveRows(QModelIndex(), first, last);
        for (int r = first; r <= last; ++r)
            m_loader.cancelThumbnail(m_items.at(r).filePath);
        m_items.remove(first, n);
        m_thumbUrls.remove(first, n);
        m_thumbState.remove(first, n);
        m_selected.remove(first, n);
        endRemoveRows();
        removed += n;
        // Waren zugeklappte Zeilen ausgewaehlt, ist die Auswahl jetzt kleiner.
        recountSelection();
    }

    for (int s : scopes) {
        if (s <= 0 || s >= m_scopes.size()) continue;
        m_scopes[s].active    = false;
        m_scopes[s].folderRow = -1;
        // Ein Sidecar kann gross sein und wird beim Wiederaufklappen frisch gelesen.
        if (JsonStorage* st = m_scopeStorage.take(s)) {
            st->saveCurrentFolder();
            delete st;
        }
        const QString& p = m_scopes.at(s).path;
        if (m_watcher->directories().contains(p))
            m_watcher->removePath(p);
    }

    if (removed > 0) {
        rebuildPathIndex();
        emit countChanged();
    }
    if (!hasMoreToFill())
        m_fillTimer.stop();
}

void MediaModel::rebuildPathIndex() {
    m_pathToRow.clear();
    m_pathToRow.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i)
        m_pathToRow.insert(m_items.at(i).filePath, i);
    // Bereiche zeigen auf Zeilen - sonst klettert lessThan die Elternkette ins Leere.
    for (int s = 1; s < m_scopes.size(); ++s)
        m_scopes[s].folderRow = m_scopes.at(s).active
                                    ? m_pathToRow.value(m_scopes.at(s).path, -1)
                                    : -1;
}

bool MediaModel::expandFolder(const QString& folderPath) {
    const int row = rowForPath(folderPath);
    if (row < 0) return false;
    const MediaItem& it = m_items.at(row);
    if (!it.isFolder()) return false;
    if (m_expanded.contains(folderPath)) return false;

    // Schleifenschutz gegen Symlinks auf einen Vorfahren. Verglichen wird der
    // kanonische Pfad, und nur hier - je Eintrag waere das ein realpath() zu viel.
    const QString canon = QFileInfo(folderPath).canonicalFilePath();
    if (!canon.isEmpty()) {
        for (int s = it.scope; s >= 0; s = m_scopes.at(s).parent) {
            if (QFileInfo(m_scopes.at(s).path).canonicalFilePath() == canon)
                return false;
        }
    }

    m_expanded.insert(folderPath);
    const int scope = ensureScope(folderPath, it.scope, row);
    if (scope <= 0) { m_expanded.remove(folderPath); return false; }
    m_scanQueue.append(scope);
    if (!m_fillTimer.isActive())
        m_fillTimer.start();

    if (!m_watcher->directories().contains(folderPath))
        m_watcher->addPath(folderPath);

    emitRow(row, { ExpandedRole });
    emit expansionChanged();
    return true;
}

bool MediaModel::collapseFolder(const QString& folderPath) {
    // Nur dieser Ordner wird vergessen, die Enkel bleiben vorgemerkt.
    if (!m_expanded.remove(folderPath)) return false;

    const int scope = m_scopeOfPath.value(folderPath, -1);
    if (scope > 0 && m_scopes.at(scope).active) {
        QSet<int> gone;
        gone.insert(scope);
        collectDescendantScopes(scope, gone);
        removeRowsOfScopes(gone);
    }

    const int row = rowForPath(folderPath);
    if (row >= 0) emitRow(row, { ExpandedRole });
    emit expansionChanged();
    return true;
}

bool MediaModel::toggleFolder(const QString& folderPath) {
    return m_expanded.contains(folderPath) ? collapseFolder(folderPath)
                                           : expandFolder(folderPath);
}

void MediaModel::ensureFolderCount(const QString& folderPath) {
    if (folderPath.isEmpty()) return;
    // Der Wunsch wird gemerkt, BEVOR abgekuerzt wird: nach einem Neuaufbau stoesst
    // finishFill die Zaehlung daraus erneut an.
    m_countWanted.insert(folderPath);
    if (m_folderCounts.contains(folderPath)) return;   // schon gezaehlt
    if (m_countPending.contains(folderPath)) return;   // laeuft gerade
    const int row = rowForPath(folderPath);
    const MediaItem* it = itemAt(row);
    if (!it || !it->isFolder()) return;

    if (!m_countPool) {
        // Ein Thread: reine Platten-Arbeit, mehrere wuerden nur den Kopf hin- und herschicken.
        m_countPool = new QThreadPool(this);
        m_countPool->setMaxThreadCount(1);
    }
    m_countPending.insert(folderPath);
    const QString sidecar = mg::folderSidecarName(folderPath);
    m_countPool->start(new FolderCountTask(QPointer<MediaModel>(this), folderPath,
                                           sidecar, m_showAllFiles, m_countGeneration,
                                           m_countTicket.value(folderPath)));
}

// Ein Reload laeuft nicht immer - eine Verschiebung zwischen zwei aufgeklappten
// Unterordnern laesst den offenen Ordner unberuehrt.
void MediaModel::invalidateFolderCount(const QString& folderPath) {
    if (folderPath.isEmpty()) return;
    const bool known = m_folderCounts.contains(folderPath);
    const bool running = m_countPending.contains(folderPath);
    if (!known && !running) return;               // nie gezaehlt: nichts zu verwerfen
    m_folderCounts.remove(folderPath);
    // Die Marke steigt: das Ergebnis eines laufenden Auftrags faellt durch.
    ++m_countTicket[folderPath];
    m_countPending.remove(folderPath);
    const int row = rowForPath(folderPath);
    if (m_countWanted.contains(folderPath) && row >= 0) {
        ensureFolderCount(folderPath);            // sofort neu zaehlen
    } else if (row >= 0) {
        emitRow(row, { ChildCountRole });         // Zeile leeren (−1)
    }
}

void MediaModel::noteFolderCount(const QString& folderPath, int count, int generation,
                                 int ticket) {
    // Zuerst streichen: der Auftrag ist fertig, gleich ob sein Ergebnis noch gilt.
    m_countPending.remove(folderPath);
    // Veraltet heisst nicht aufgeben - der Wunsch besteht weiter, also gleich neu
    // zaehlen statt auf das naechste finishFill zu warten.
    if (generation != m_countGeneration                        // Neuaufbau
        || ticket != m_countTicket.value(folderPath)) {        // Inhalt geaendert
        if (m_countWanted.contains(folderPath) && rowForPath(folderPath) >= 0)
            ensureFolderCount(folderPath);
        return;
    }
    m_folderCounts.insert(folderPath, count);
    const int row = rowForPath(folderPath);
    if (row >= 0) emitRow(row, { ChildCountRole });
}

void MediaModel::collapseAll() {
    if (m_expanded.isEmpty()) return;
    m_expanded.clear();
    QSet<int> gone;
    for (int i = 1; i < m_scopes.size(); ++i)
        if (m_scopes.at(i).active) gone.insert(i);
    removeRowsOfScopes(gone);
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1), { ExpandedRole });
    emit expansionChanged();
}

// Ein leerer Zustand wird geloescht statt gespeichert - sonst wuechse die
// Tabelle mit jedem durchquerten Ordner.
void MediaModel::rememberExpansion(const QString& folderPath) {
    if (folderPath.isEmpty()) return;
    m_memoryOrder.removeAll(folderPath);
    if (m_expanded.isEmpty()) {
        m_expandedMemory.remove(folderPath);
        return;
    }
    m_expandedMemory.insert(folderPath, expandedFolders());
    m_memoryOrder.append(folderPath);
    while (m_memoryOrder.size() > kMaxFolderMemory)
        m_expandedMemory.remove(m_memoryOrder.takeFirst());
}

QStringList MediaModel::expandedFolders() const {
    QStringList list(m_expanded.begin(), m_expanded.end());
    // Sortiert, damit eine Momentaufnahme reproduzierbar ist (QSet hat keine Ordnung).
    list.sort();
    return list;
}

void MediaModel::setExpandedFolders(const QStringList& folderPaths) {
    const QSet<QString> want(folderPaths.begin(), folderPaths.end());
    if (want == m_expanded) return;
    m_expanded = want;

    // Welche der genannten Ordner es noch gibt, weiss erst das Einlesen.
    QSet<int> gone;
    for (int i = 1; i < m_scopes.size(); ++i)
        if (m_scopes.at(i).active) gone.insert(i);
    removeRowsOfScopes(gone);

    queueExpandedFolders(0);
    if (hasMoreToFill() && !m_fillTimer.isActive())
        m_fillTimer.start();

    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1), { ExpandedRole });
    emit expansionChanged();
}

void MediaModel::loadFolder(const QString& rawFolderPath) {
    // Einmal normalisieren: "…/ordner/" und "…/ordner" waren sonst zwei Ordner.
    const QString folderPath = mg::normalizedFolder(rawFolderPath);
    if (folderPath == m_folder && !m_items.isEmpty()) return;

    m_loader.cancelAll();

    const QStringList watched = m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);

    // Der Aufklapp-Zustand gilt nur unterhalb eines bestimmten Ordners; verlassen
    // und zurueckkehren soll ihn wiederbringen.
    rememberExpansion(m_folder);
    // Die Zaehl-Wuensche gehoeren zum verlassenen Ordner.
    m_countWanted.clear();
    m_folder = folderPath;
    // Der Undo-Stapel gehoert zum offenen Ordner - eine Ruecknahme anderswo waere
    // nicht nachvollziehbar.
    clearFileHistory();
    {
        // Die Liste muss zuerst in eine Variable: QHash::value liefert eine Kopie, und
        // begin() der einen mit end() der anderen zu paaren stuerzte reproduzierbar ab.
        const QStringList remembered = m_expandedMemory.value(folderPath);
        const QSet<QString> restored(remembered.begin(), remembered.end());
        if (restored != m_expanded) {
            m_expanded = restored;
            emit expansionChanged();
        }
    }
    rebuild(folderPath);

    // Ordnerwechsel ist eine grosse Freigabe - Heap aktiv ans OS zurueckgeben.
    // Bewusst nicht in reload(): gleicher Ordner, kleine Deltas.
    mg::trimHeap();

    if (!folderPath.isEmpty())
        m_watcher->addPath(folderPath);

    emit folderChanged();
}

void MediaModel::setShowAllFiles(bool v) {
    if (m_showAllFiles == v)
        return;
    m_showAllFiles = v;
    // Die Sichtbarkeit entscheidet sich beim Einlesen - ein Filter im Proxy muesste
    // die Regel ein zweites Mal kennen.
    reload();
}

void MediaModel::reload() {
    if (m_folder.isEmpty()) return;
    m_loader.cancelAll();
    rebuild(m_folder);
}

void MediaModel::dropScopeSidecars() {
    if (m_scopeStorage.isEmpty()) return;
    qDeleteAll(m_scopeStorage);         // bewusst OHNE saveCurrentFolder, s. Header
    m_scopeStorage.clear();
    reload();
}

int MediaModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MediaModel::data(const QModelIndex& index, int role) const {
    const int r = index.row();
    if (r < 0 || r >= m_items.size()) return {};
    const MediaItem& it = m_items[r];

    switch (role) {
    case FilePathRole:    return it.filePath;
    case FileNameRole:    return it.fileName();
    case DisplayNameRole: return it.displayName;
    case MediaTypeRole:   return static_cast<int>(it.type);
    case TypeLabelRole:   return typeLabel(it);
    case TagsRole:        return it.tags;
    // Gespeichert wird UTC, angezeigt Ortszeit. Die Umrechnung ruft tzset und
    // passiert deshalb je sichtbarer Zeile statt je Datei (1,54 -> 1,04 us).
    case DateTimeRole:    return it.dateTime.toLocalTime();
    case FileSizeRole:    return it.fileSize;
    case ThumbUrlRole:    return m_thumbUrls[r];
    case ThumbStateRole:  return m_thumbState[r];
    case OwnerFolderRole: return (it.scope >= 0 && it.scope < m_scopes.size())
                                     ? m_scopes.at(it.scope).path : QString();
    case DepthRole:       return scopeDepthOf(it.scope);
    case ExpandedRole:    return it.isFolder() && m_expanded.contains(it.filePath);
    case ChildCountRole:  return it.isFolder() ? m_folderCounts.value(it.filePath, -1) : -1;
    case SelectedRole:    return r < m_selected.size() && m_selected.at(r) != 0;
    default:              return {};
    }
}

QHash<int, QByteArray> MediaModel::roleNames() const {
    return {
        { FilePathRole,    "filePath"    },
        { FileNameRole,    "fileName"    },
        { DisplayNameRole, "displayName" },
        { MediaTypeRole,   "mediaType"   },
        { TypeLabelRole,   "typeLabel"   },
        { TagsRole,        "tags"        },
        { DateTimeRole,    "dateTime"    },
        { FileSizeRole,    "fileSize"    },
        { ThumbUrlRole,    "thumbUrl"    },
        { ThumbStateRole,  "thumbState"  },
        { OwnerFolderRole, "ownerFolder" },
        { DepthRole,       "depth"       },
        { ExpandedRole,    "expanded"    },
        { ChildCountRole,  "childCount"  },
        { SelectedRole,    "selected"    },
    };
}

QString MediaModel::typeLabel(const MediaItem& item) {
    switch (item.type) {
    case MediaType::Video: return item.extension().toUpper();
    case MediaType::Audio: return item.audioFormatLabel();
    case MediaType::Pdf:   return QStringLiteral("PDF");
    // Endungslose Textdateien haetten sonst kein Kuerzel - in der Liste steht dort
    // aber genau das statt einer Vorschau.
    case MediaType::Text: {
        const QString e = item.extension().toUpper();
        if (!e.isEmpty()) return e;
        const QString name = mg::baseNameView(item.filePath).toString().toUpper();
        return name.left(10);
    }
    // Nicht erkannte Typen waren an nichts zu erkennen: kein Thumbnail, kein Badge.
    case MediaType::Unknown: return item.extension().toUpper();
    default:               return {};
    }
}

void MediaModel::emitRow(int row, const QVector<int>& roles) {
    if (row < 0 || row >= m_items.size()) return;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
}

void MediaModel::emitRows(int first, int last, const QVector<int>& roles) {
    if (first < 0 || last < first || last >= m_items.size()) return;
    emit dataChanged(index(first), index(last), roles);
}

void MediaModel::refreshThumbnails() {
    if (m_items.isEmpty()) return;
    // In-flight-Ergebnisse der alten Zielgroesse verwerfen.
    m_loader.cancelAll();
    m_thumbUrls.fill(QString());
    m_thumbState.fill(0);
    emit dataChanged(index(0), index(m_items.size() - 1),
                     { ThumbUrlRole, ThumbStateRole });
    emit thumbnailsInvalidated();
}

void MediaModel::ensureThumbnail(const QString& filePath) {
    if (kLogThumbs)
        qInfo("[thumb] anfordern  %s (Zeile %d, Zustand %d)",
              qPrintable(QFileInfo(filePath).fileName()), rowForPath(filePath),
              rowForPath(filePath) >= 0 ? m_thumbState[rowForPath(filePath)] : -1);
    // Wer anfordert, hebt eine vorgemerkte Abbestellung auf - auch von anderer Kachel.
    m_cancelPending.remove(filePath);
    // Schuetzt gegen ein Abbestellen, das erst noch kommt: die abgebende Kachel
    // meldet sich nach der uebernehmenden.
    m_thumbWanted.insert(filePath);
    if (!m_cancelTimer.isActive())
        m_cancelTimer.start();
    const int row = rowForPath(filePath);
    if (row < 0) return;
    // Ordnerkacheln zeichnen sich selbst - der Loader kennt fuer sie keinen Erzeuger.
    if (m_items.at(row).isFolder()) return;
    if (m_thumbState[row] == 1) return;          // bereits geliefert
    m_loader.requestThumbnail(filePath);          // Treffer/Miss klärt der Loader
}

void MediaModel::cancelThumbnail(const QString& filePath) {
    if (kLogThumbs)
        qInfo("[thumb] abbestellen %s", qPrintable(QFileInfo(filePath).fileName()));
    const int row = rowForPath(filePath);
    if (row >= 0 && m_thumbState[row] == 1) return;  // schon fertig -> nichts abbrechen
    // Nicht sofort: eine andere Kachel kann denselben Pfad im selben Durchlauf
    // uebernommen und schon angefordert haben.
    m_cancelPending.insert(filePath);
    if (!m_cancelTimer.isActive())
        m_cancelTimer.start();
}

void MediaModel::onThumbnailReady(const QString& filePath, const QString& thumbUrl) {
    const int row = rowForPath(filePath);
    if (kLogThumbs)
        qInfo("[thumb] geliefert  %s (Zeile %d)",
              qPrintable(QFileInfo(filePath).fileName()), row);
    if (row < 0) return;
    m_thumbUrls[row]  = thumbUrl;
    m_thumbState[row] = 1;
    emitRow(row, { ThumbUrlRole, ThumbStateRole });
}

void MediaModel::onThumbnailFailed(const QString& filePath) {
    const int row = rowForPath(filePath);
    if (kLogThumbs)
        qInfo("[thumb] FEHLGESCHLAGEN %s (Zeile %d)",
              qPrintable(QFileInfo(filePath).fileName()), row);
    if (row < 0) return;
    m_thumbState[row] = 2;
    emitRow(row, { ThumbStateRole });
}

// Alles Folgende arbeitet mit m_storage/m_tagManager - die gehoeren dem
// geoeffneten Ordner.
bool MediaModel::isFileRow(int row) const {
    const MediaItem* it = itemAt(row);
    return it && !it->isFolder();
}

bool MediaModel::isRootFileRow(int row) const {
    const MediaItem* it = itemAt(row);
    return it && it->scope == 0 && !it->isFolder();
}

JsonStorage* MediaModel::storageForScope(int scope) {
    if (scope <= 0) return &m_storage;
    if (scope >= m_scopes.size()) return &m_storage;
    const auto it = m_scopeStorage.constFind(scope);
    if (it != m_scopeStorage.constEnd()) return it.value();
    // Lazy, erst beim ersten Zugriff; this als Elternteil raeumt sie mit ab.
    auto* st = new JsonStorage(this);
    st->loadFolder(m_scopes.at(scope).path);
    m_scopeStorage.insert(scope, st);
    return st;
}

JsonStorage* MediaModel::storageForFolder(const QString& folder) {
    if (folder.isEmpty()) return nullptr;
    if (folder == m_folder) return &m_storage;
    const int s = m_scopeOfPath.value(folder, -1);
    if (s > 0 && s < m_scopes.size() && m_scopes.at(s).active)
        return storageForScope(s);
    return nullptr;      // nicht offen -> kurzlebiger Weg (writeMetaToFolder)
}

// Ueber Namen, denn die IDs eines anderen Ordners sind andere. Fehlt drueben
// eine Kategorie, entsteht sie auf der Hauptebene - den Pfad nachzubilden waere Raten.
QStringList MediaModel::categoryNamesOf(JsonStorage& st, const QString& fileName) {
    QStringList out;
    std::function<void(const QList<TagCategory>&)> walk =
        [&](const QList<TagCategory>& list) {
            for (const TagCategory& c : list) {
                if (c.files.contains(fileName) && !out.contains(c.name))
                    out.append(c.name);
                walk(c.children);
            }
        };
    walk(st.categoriesRef());
    return out;
}

void MediaModel::attachCategories(JsonStorage& st, const QString& fileName,
                                  const QStringList& names) {
    if (names.isEmpty()) return;
    QList<TagCategory>& cats = st.categoriesRef();
    for (const QString& wanted : names) {
        TagCategory* found = nullptr;
        std::function<void(QList<TagCategory>&)> walk = [&](QList<TagCategory>& list) {
            for (TagCategory& c : list) {
                if (!found && c.name == wanted) { found = &c; return; }
                if (!found) walk(c.children);
            }
        };
        walk(cats);
        if (!found) {
            TagCategory fresh;
            fresh.id    = QUuid::createUuid().toString(QUuid::WithoutBraces);
            fresh.name  = wanted;
            fresh.color = QColor(0, 179, 161);
            cats.append(fresh);
            found = &cats.last();
        }
        if (!found->files.contains(fileName))
            found->files.append(fileName);
    }
}

void MediaModel::stripCategories(JsonStorage& st, const QString& fileName) {
    std::function<void(QList<TagCategory>&)> strip = [&](QList<TagCategory>& list) {
        for (TagCategory& c : list) {
            c.files.removeAll(fileName);
            strip(c.children);
        }
    };
    strip(st.categoriesRef());
}

void MediaModel::collectMetaAt(const QString& folder, const QString& fileName,
                               FileOp* op) const {
    if (folder == m_folder) { collectMeta(fileName, op); return; }
    auto* st = const_cast<MediaModel*>(this)->storageForFolder(folder);
    if (!st) return;
    op->tags          = st->getTags(fileName);
    op->categoryIds.clear();                       // IDs gelten nur im eigenen Baum
    op->categoryNames = categoryNamesOf(*st, fileName);
    // Kein Datum: es haengt an der Datei und wandert mit ihr.
}

void MediaModel::dropMetaAt(const QString& folder, const QString& fileName,
                            const FileOp& op) {
    if (folder == m_folder) { dropMeta(fileName, op); return; }
    JsonStorage* st = storageForFolder(folder);
    if (!st) { removeMetaFromFolder(folder, fileName); return; }
    st->removeFile(fileName);
    stripCategories(*st, fileName);
    st->saveCurrentFolder();
}

void MediaModel::restoreMetaAt(const QString& folder, const QString& fileName,
                               const FileOp& op) {
    if (folder == m_folder) { restoreMeta(fileName, op); return; }
    JsonStorage* st = storageForFolder(folder);
    if (!st) { writeMetaToFolder(folder, fileName, op, m_storage.tagColors()); return; }
    if (!op.tags.isEmpty()) {
        QStringList tags = st->getTags(fileName);
        const QHash<QString, QColor> colors = m_storage.tagColors();
        for (const QString& t : op.tags) {
            if (!tags.contains(t)) tags.append(t);
            st->ensureTagRegistered(t);
            const auto c = colors.constFind(t);
            if (c != colors.constEnd()) st->setTagColor(t, c.value());
        }
        st->setTags(fileName, tags);
    }
    attachCategories(*st, fileName, op.categoryNames);
    st->saveCurrentFolder();
}

void MediaModel::renameItem(const QString& filePath, const QString& newBaseName) {
    const int row = rowForPath(filePath);
    if (row < 0 || !isFileRow(row)) return;

    const QString trimmed = newBaseName.trimmed();
    if (trimmed.isEmpty()) return;

    const QFileInfo fi(filePath);
    const QString ext     = fi.suffix();
    const QString newName = ext.isEmpty() ? trimmed : (trimmed + QChar('.') + ext);
    const QString oldName = fi.fileName();
    if (newName == oldName) return;

    const QString newPath = QDir(fi.absolutePath()).filePath(newName);
    if (QFileInfo::exists(newPath)) return;        // Kollision: still ignorieren

    ++m_suppressWatch;
    const bool ok = QFile::rename(filePath, newPath);
    if (!ok) { --m_suppressWatch; return; }

    // Im Sidecar des Ordners, dem die Datei gehoert.
    if (JsonStorage* st = storageForScope(m_items.at(row).scope)) {
        st->renameFile(oldName, newName);
        st->saveCurrentFolder();
    }
    --m_suppressWatch;

    MediaItem& it = m_items[row];
    m_pathToRow.remove(it.filePath);
    it.filePath    = newPath;
    it.displayName = QFileInfo(newPath).completeBaseName();
    m_pathToRow.insert(newPath, row);

    // Der Thumbnail-Cache-Key haengt am Pfad.
    m_thumbUrls[row]  = QString();
    m_thumbState[row] = 0;
    emitRow(row, { FilePathRole, FileNameRole, DisplayNameRole, ThumbUrlRole, ThumbStateRole });
}

// Kein Zeilen-/Modellanteil - den macht der Aufrufer, damit Loeschen und
// Wiederholen denselben Kern nutzen.
bool MediaModel::trashFile(const QString& filePath, FileOp* op) {
    const QFileInfo info(filePath);
    const QString name   = info.fileName();
    const QString folder = info.absolutePath();

    op->kind = FileOp::Kind::Delete;
    op->path = filePath;
    op->trashPath.clear();
    op->sidecarPath.clear();
    op->sidecarTrashPath.clear();
    op->bakPath.clear();
    op->bakTrashPath.clear();
    // Vor dem Loeschen sichern - danach sind sie weg.
    collectMetaAt(folder, name, op);

    // Watcher unterdruecken: das Loeschen loeste sonst einen ganzen Reload aus.
    ++m_suppressWatch;
    // Bevorzugt Papierkorb; ohne ihn gibt es keinen Rueckweg und damit kein Undo.
    QString inTrash;
    bool ok = QFile::moveToTrash(filePath, &inTrash);
    if (ok)
        op->trashPath = inTrash;
    else
        ok = QFile::remove(filePath);
    if (ok) {
        // PDF-Editor-Sidecar mit entsorgen.
        const QString sidecar = filePath + QStringLiteral(".mgedit.json");
        if (QFile::exists(sidecar)) {
            op->sidecarPath = sidecar;
            QString sidecarTrash;
            if (QFile::moveToTrash(sidecar, &sidecarTrash))
                op->sidecarTrashPath = sidecarTrash;
            else
                QFile::remove(sidecar);
        }
        // Auch die DOCX-Sicherung - ueber den Papierkorb, damit Strg+Z sie zusammen
        // mit der Datei zurueckholt.
        const QString bak = filePath + QStringLiteral(".bak");
        if (QFile::exists(bak)) {
            op->bakPath = bak;
            QString bakTrash;
            if (QFile::moveToTrash(bak, &bakTrash))
                op->bakTrashPath = bakTrash;
            else
                QFile::remove(bak);
        }
        dropMetaAt(folder, name, *op);
    }
    --m_suppressWatch;
    return ok;
}

bool MediaModel::restoreFile(const FileOp& op) {
    if (op.trashPath.isEmpty() || !QFile::exists(op.trashPath))
        return false;
    // Steht dort inzwischen wieder etwas, wird nichts ueberschrieben.
    if (QFileInfo::exists(op.path))
        return false;

    ++m_suppressWatch;
    bool ok = QFile::rename(op.trashPath, op.path);
    if (!ok) {
        // Papierkorb auf einem anderen Dateisystem: kopieren und aufraeumen.
        ok = QFile::copy(op.trashPath, op.path);
        if (ok) QFile::remove(op.trashPath);
    }
    if (ok) {
        if (!op.sidecarTrashPath.isEmpty() && QFile::exists(op.sidecarTrashPath)
            && !QFileInfo::exists(op.sidecarPath)) {
            if (!QFile::rename(op.sidecarTrashPath, op.sidecarPath))
                QFile::copy(op.sidecarTrashPath, op.sidecarPath);
        }
        if (!op.bakTrashPath.isEmpty() && QFile::exists(op.bakTrashPath)
            && !QFileInfo::exists(op.bakPath)) {
            if (!QFile::rename(op.bakTrashPath, op.bakPath))
                QFile::copy(op.bakTrashPath, op.bakPath);
        }
        restoreMetaAt(QFileInfo(op.path).absolutePath(),
                      QFileInfo(op.path).fileName(), op);
        invalidateFolderCount(QFileInfo(op.path).absolutePath());
    }
    --m_suppressWatch;
    return ok;
}

// Ans Ende haengen - die Reihenfolge macht der Proxy.
void MediaModel::appendRowFor(const QString& filePath) {
    const QFileInfo fi(filePath);
    if (!fi.exists()) return;

    // Dieser Weg umgeht den Verzeichnis-Leser, also muss der Filter hier erneut stehen.
    if (fi.isHidden() && !AppSettings::instance().showHiddenFiles()) return;

    // Eine zurueckgeholte Begleitdatei darf nur als Kachel erscheinen, wenn der
    // Nutzer sie sehen will.
    const QString parent = fi.absolutePath();
    int scope = 0;
    if (parent != m_folder) {
        scope = m_scopeOfPath.value(parent, -1);
        if (scope <= 0 || scope >= m_scopes.size() || !m_scopes.at(scope).active)
            return;
    }

    // Der Sidecar-Name kommt aus dem Bereich - das Feld der laufenden Befuellung
    // war zu diesem Zeitpunkt laengst geleert.
    if (!m_showAllFiles && mg::isCompanionFile(fi.fileName(), sidecarOfScope(scope)))
        return;

    MediaItem item;
    item.filePath    = fi.filePath();
    item.displayName = fi.completeBaseName();
    item.fileSize    = fi.size();
    // refineType schaut nur bei mehrdeutigen Endungen in die Datei.
    item.type        = fi.isDir() ? MediaType::Folder
                                  : mg::refineType(fi.filePath(),
                                        MediaItem::detectType(fi.filePath()));
    item.dateTime    = fi.lastModified(QTimeZone::UTC);
    item.scope       = scope;
    if (item.isFolder()) {
        item.displayName = fi.fileName();      // nicht completeBaseName
        item.fileSize    = 0;
    }
    if (item.type == MediaType::Unknown && !m_showAllFiles) return;

    QVector<MediaItem> batch{ item };
    if (JsonStorage* st = storageForScope(scope)) {
        st->applyToItems(batch);
        const QString name = batch.first().fileName();
    }

    const int at = m_items.size();
    beginInsertRows(QModelIndex(), at, at);
    m_pathToRow.insert(batch.first().filePath, at);
    m_items.append(std::move(batch.first()));
    m_thumbUrls.append(QString());
    m_thumbState.append(0);
    m_selected.append(0);
    endInsertRows();
    emit countChanged();
}

// Pfad->Zeile-Hash neu aufbauen: alle nachfolgenden Indizes verschieben sich.
bool MediaModel::dropRowFor(const QString& filePath) {
    const int row = rowForPath(filePath);
    if (row < 0) return false;
    beginRemoveRows(QModelIndex(), row, row);
    m_items.removeAt(row);
    m_thumbUrls.removeAt(row);
    m_thumbState.removeAt(row);
    m_selected.removeAt(row);
    rebuildPathIndex();
    endRemoveRows();
    recountSelection();
    emit countChanged();
    return true;
}

void MediaModel::clearFileHistory() {
    if (m_undoOps.isEmpty() && m_redoOps.isEmpty()) return;
    m_undoOps.clear();
    m_redoOps.clear();
    emit fileHistoryChanged();
}

void MediaModel::collectMeta(const QString& fileName, FileOp* op) const {
    op->tags          = m_tagManager.tagsForFile(fileName);
    op->categoryIds   = m_tagManager.categoryIdsForFile(fileName);
}

void MediaModel::dropMeta(const QString& fileName, const FileOp& op) {
    m_storage.removeFile(fileName);
    // Kategorien liegen nicht in den Datei-Metadaten, sondern in den Kategorien
    // selbst - ohne diesen Schritt bliebe der Name dort als Waise.
    for (const QString& id : op.categoryIds)
        m_tagManager.removeFileFromCategory(id, fileName);
    m_storage.saveCurrentFolder();
}

void MediaModel::restoreMeta(const QString& fileName, const FileOp& op) {
    for (const QString& tag : op.tags)
        m_tagManager.addTagToFile(fileName, tag);
    for (const QString& id : op.categoryIds)
        m_tagManager.addFileToCategory(id, fileName);
    m_storage.saveCurrentFolder();
}

// Eigene, kurzlebige Instanz: die laufende JsonStorage gehoert dem offenen
// Ordner und darf nicht umgeschaltet werden.
void MediaModel::writeMetaToFolder(const QString& folder, const QString& fileName,
                                   const FileOp& op,
                                   const QHash<QString, QColor>& tagColors) {
    if (op.tags.isEmpty() && op.categoryIds.isEmpty())
        return;
    JsonStorage dest;
    dest.loadFolder(folder);
    if (!op.tags.isEmpty()) {
        QStringList tags = dest.getTags(fileName);
        for (const QString& t : op.tags) {
            // Kennt der Zielordner den Tag schon, behaelt er seine Farbe - sonst faerbte
            // eine hereingeschobene Datei alle Dateien mit diesem Tag um.
            const bool knownThere = dest.tagColors().contains(t);
            if (!tags.contains(t)) tags.append(t);
            dest.ensureTagRegistered(t);
            const auto it = tagColors.constFind(t);
            if (!knownThere && it != tagColors.constEnd())
                dest.setTagColor(t, it.value());
        }
        dest.setTags(fileName, tags);
    }
    if (!op.categoryNames.isEmpty()) {
        QList<TagCategory>& cats = dest.categoriesRef();
        for (const QString& wanted : op.categoryNames) {
            TagCategory* found = nullptr;
            std::function<void(QList<TagCategory>&)> walk = [&](QList<TagCategory>& list) {
                for (TagCategory& c : list) {
                    if (!found && c.name == wanted) { found = &c; return; }
                    if (!found) walk(c.children);
                }
            };
            walk(cats);
            if (!found) {
                TagCategory fresh;
                fresh.id    = QUuid::createUuid().toString(QUuid::WithoutBraces);
                fresh.name  = wanted;
                fresh.color = QColor(0, 179, 161);
                cats.append(fresh);
                found = &cats.last();
            }
            if (!found->files.contains(fileName))
                found->files.append(fileName);
        }
    }
    dest.saveFolder(folder);
}

void MediaModel::removeMetaFromFolder(const QString& folder, const QString& fileName) {
    JsonStorage dest;
    dest.loadFolder(folder);
    dest.removeFile(fileName);
    std::function<void(QList<TagCategory>&)> strip = [&](QList<TagCategory>& list) {
        for (TagCategory& c : list) {
            c.files.removeAll(fileName);
            strip(c.children);
        }
    };
    strip(dest.categoriesRef());
    dest.saveFolder(folder);
}

namespace {
bool sameCriteria(const MediaProxyModel::FilterCriteria& a,
                  const MediaProxyModel::FilterCriteria& b) {
    return a.search == b.search && a.tags == b.tags && a.mode == b.mode
        && a.categoryActive == b.categoryActive
        && a.showImages == b.showImages && a.showVideos == b.showVideos
        && a.showAudio  == b.showAudio  && a.showPdfs   == b.showPdfs
        && a.showTexts  == b.showTexts;
}
}  // namespace

void MediaModel::applyDeepFilter(const MediaProxyModel::FilterCriteria& c,
                                 const QStringList& categoryNames) {
    if (!m_deepTimer.isSingleShot()) {
        m_deepTimer.setSingleShot(true);
        // Entprellt das Tippen: sonst sprangen die Treffer im Takt der Tastatur.
        m_deepTimer.setInterval(250);
        connect(&m_deepTimer, &QTimer::timeout, this, &MediaModel::startDeepScan);
    }

    if (c.isEmpty()) {
        // Filter weg: zurueck auf den Stand vor der Suche. Von Hand geoeffnete Ordner
        // bleiben offen, was die Suche aufklappte schliesst sich.
        m_deepTimer.stop();
        ++m_deepGeneration;
        if (m_deepCancel) m_deepCancel->store(true);
        if (m_deepActive) {
            m_deepActive = false;
            m_deepChain.clear();
            const QStringList back = m_deepSnapshot;
            m_deepSnapshot.clear();
            setExpandedFolders(back);
            // Der Proxy bewertet nur neu, was sich meldet.
            if (!m_items.isEmpty())
                emit dataChanged(index(0), index(m_items.size() - 1));
        }
        return;
    }

    if (!m_deepActive) {
        m_deepActive   = true;
        m_deepChain.clear();
        m_deepSnapshot = expandedFolders();     // Stand vor der Suche merken
        // Bis der Lauf Ergebnisse bringt, gehoert kein Ordner zum Ergebnis.
        if (!m_items.isEmpty())
            emit dataChanged(index(0), index(m_items.size() - 1));
    } else if (sameCriteria(m_deepCriteria, c)
               && m_deepCategoryNames == categoryNames) {
        return;                                  // nichts Neues zu suchen
    }
    m_deepCriteria      = c;
    m_deepCategoryNames = categoryNames;
    m_deepTimer.start();
}

void MediaModel::startDeepScan() {
    if (m_folder.isEmpty() || !m_deepActive) return;
    if (!m_deepPool) {
        // Ein Thread: die Suche ist Platten-Arbeit, ein Sidecar je Ordner.
        m_deepPool = new QThreadPool(this);
        m_deepPool->setMaxThreadCount(1);
    }
    // Ein laufender Lauf gehoert zu einem aelteren Suchtext.
    if (m_deepCancel) m_deepCancel->store(true);
    m_deepCancel = std::make_shared<std::atomic<bool>>(false);
    ++m_deepGeneration;
    m_deepPool->start(new DeepScanTask(QPointer<MediaModel>(this), m_folder,
                                       m_deepCriteria, m_deepCategoryNames,
                                       m_showAllFiles, m_deepGeneration, m_deepCancel));
}

void MediaModel::noteDeepMatches(const QStringList& folders, int generation) {
    if (generation != m_deepGeneration || !m_deepActive) return;
    // Kein frueher Ausstieg bei leerer Liste: ohne Treffer muss die Kette der
    // vorigen Suche verschwinden.
    if (qEnvironmentVariableIntValue("MG_DEEPLOG") >= 1 && !folders.isEmpty())
        m_deepFillTimer.start();

    // Jeder Treffer-Ordner UND seine Kette bis zum offenen Ordner - sonst waere der
    // Treffer aufgeklappt, aber nicht zu sehen.
    QSet<QString> chain;
    for (const QString& f : folders) {
        QString p = f;
        while (p.size() > m_folder.size() && p.startsWith(m_folder)) {
            chain.insert(p);
            const int cut = p.lastIndexOf(QLatin1Char('/'));
            if (cut <= 0) break;
            p = p.left(cut);
        }
    }
    m_deepChain = chain;

    const QSet<QString> next = m_expanded + chain;
    if (next != m_expanded)
        setExpandedFolders(QStringList(next.begin(), next.end()));
    // Auch ohne neue Zeilen muss die Sichtbarkeit neu bewertet werden.
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1));
}

QStringList MediaModel::visibleTags() const {
    QStringList out = m_storage.allTags();
    QSet<QString> seen(out.begin(), out.end());
    for (int scope = 1; scope < m_scopes.size(); ++scope) {
        if (!m_scopes.at(scope).active) continue;
        JsonStorage* st = m_scopeStorage.value(scope, nullptr);
        if (!st) continue;
        for (const QString& t : st->allTags())
            if (!seen.contains(t)) { seen.insert(t); out.append(t); }
    }
    // Deterministisch, sonst spraengen die Chips der Filterleiste bei jedem
    // Aufklappen umher.
    std::sort(out.begin() + m_storage.allTags().size(), out.end());
    return out;
}

QColor MediaModel::visibleTagColor(const QString& tag) const {
    // Nicht ueber JsonStorage::tagColor - das liefert fuer einen unbekannten Tag
    // eine Ersatzfarbe, und dann waere kennt ihn niemand nicht unterscheidbar.
    const QHash<QString, QColor> rootColors = m_storage.tagColors();
    const auto own = rootColors.constFind(tag);
    if (own != rootColors.constEnd()) return own.value();

    // Ueber den Bereichs-Index laufen, nicht ueber m_scopeStorage: dessen
    // QHash-Reihenfolge ist je Programmlauf anders ausgewuerfelt.
    for (int scope = 1; scope < m_scopes.size(); ++scope) {
        if (!m_scopes.at(scope).active) continue;
        JsonStorage* st = m_scopeStorage.value(scope, nullptr);
        if (!st) continue;
        const QHash<QString, QColor> cols = st->tagColors();
        const auto c = cols.constFind(tag);
        if (c != cols.constEnd()) return c.value();
    }
    return {};      // ungueltig = niemand im sichtbaren Baum kennt ihn
}

void MediaModel::fillCategoryFilesByScope(const QStringList& categoryNames,
                                          QHash<int, QSet<QString>>& out) const {
    if (categoryNames.isEmpty()) return;
    for (auto it = m_scopeStorage.constBegin(); it != m_scopeStorage.constEnd(); ++it) {
        const int scope = it.key();
        if (scope <= 0 || scope >= m_scopes.size() || !m_scopes.at(scope).active)
            continue;
        QSet<QString> files;
        std::function<void(const QList<TagCategory>&)> walk =
            [&](const QList<TagCategory>& list) {
                for (const TagCategory& c : list) {
                    if (categoryNames.contains(c.name))
                        for (const QString& f : c.files) files.insert(f);
                    walk(c.children);
                }
            };
        walk(it.value()->categoriesRef());
        out.insert(scope, files);
    }
}

bool MediaModel::isVisibleFolder(const QString& folderPath) const {
    if (folderPath.isEmpty()) return false;
    if (folderPath == m_folder) return true;
    const int s = m_scopeOfPath.value(folderPath, -1);
    return s > 0 && s < m_scopes.size() && m_scopes.at(s).active;
}

void MediaModel::remapExpanded(const QString& oldPath, const QString& newPath) {
    if (m_expanded.isEmpty() || oldPath.isEmpty()) return;
    const QString prefix = oldPath + QLatin1Char('/');
    QSet<QString> next;
    next.reserve(m_expanded.size());
    for (const QString& p : std::as_const(m_expanded)) {
        if (p == oldPath)                next.insert(newPath);
        else if (p.startsWith(prefix))   next.insert(newPath + p.mid(oldPath.size()));
        else                             next.insert(p);
    }
    m_expanded = next;
}

// Ein Name, kein Pfad: sonst legte ein Knopf Ordner an beliebiger Stelle an.
static bool usableFolderName(const QString& name) {
    const QString t = name.trimmed();
    if (t.isEmpty() || t == QStringLiteral(".") || t == QStringLiteral(".."))
        return false;
    return !t.contains(QLatin1Char('/')) && !t.contains(QLatin1Char('\\'));
}

int MediaModel::createFolder(const QString& parentFolder, const QString& name) {
    const QString parent = parentFolder.isEmpty() ? m_folder : parentFolder;
    if (!isVisibleFolder(parent))    return 3;
    if (!usableFolderName(name))     return 1;

    const QString target = QDir(parent).filePath(name.trimmed());
    // Ein gleichnamiger Eintrag jeder Art zaehlt als gibt es schon.
    if (QFileInfo::exists(target))   return 2;
    if (!QDir(parent).mkdir(name.trimmed())) return 3;

    reload();
    return 0;
}

int MediaModel::renameFolder(const QString& folderPath, const QString& newName) {
    const int row = rowForPath(folderPath);
    const MediaItem* it = itemAt(row);
    if (!it || !it->isFolder())      return 3;
    if (!usableFolderName(newName))  return 1;

    const QFileInfo fi(folderPath);
    const QString trimmed = newName.trimmed();
    if (trimmed == fi.fileName())    return 0;      // nichts zu tun
    const QString target = QDir(fi.absolutePath()).filePath(trimmed);
    if (QFileInfo::exists(target))   return 2;

    ++m_suppressWatch;
    const bool ok = QDir().rename(folderPath, target);
    --m_suppressWatch;
    if (!ok) return 3;

    // Der Aufklapp-Zustand haengt an Pfaden - mitsamt Enkeln umhaengen.
    remapExpanded(folderPath, target);
    reload();
    return 0;
}

bool MediaModel::trashFolderAt(const QString& folderPath, bool reloadNow) {
    FileOp op;
    op.kind = FileOp::Kind::Folder;
    op.path = folderPath;

    ++m_suppressWatch;
    QString inTrash;
    bool ok = QFile::moveToTrash(folderPath, &inTrash);
    if (ok) op.trashPath = inTrash;
    // Kein Fallback auf rekursives Loeschen: ohne Papierkorb gibt es keinen Rueckweg.
    --m_suppressWatch;
    if (!ok) return false;

    // Der Ordner ist weg - sein Aufklapp-Zustand auch.
    const QString prefix = folderPath + QLatin1Char('/');
    QSet<QString> keep;
    for (const QString& p : std::as_const(m_expanded))
        if (p != folderPath && !p.startsWith(prefix)) keep.insert(p);
    m_expanded = keep;

    // reloadNow == false kommt aus deleteSelected: ein Reload je Ordner haette den
    // naechsten unauffindbar gemacht.
    if (reloadNow) reload();
    pushUndo(op);
    return true;
}

bool MediaModel::deleteFolder(const QString& folderPath) {
    const int row = rowForPath(folderPath);
    const MediaItem* it = itemAt(row);
    if (!it || !it->isFolder()) return false;
    return trashFolderAt(folderPath, /*reloadNow=*/true);
}

bool MediaModel::pushUndo(const FileOp& op) {
    m_undoOps.push_back(op);
    if (m_undoOps.size() > kMaxFileOps)
        m_undoOps.removeFirst();
    m_redoOps.clear();          // neue Tat -> der Redo-Zweig ist überholt
    emit fileHistoryChanged();
    return true;
}

static QString freeTargetPath(const QString& destFolder, const QString& fileName) {
    const QFileInfo fi(fileName);
    const QString base = fi.completeBaseName();
    const QString suffix = fi.suffix().isEmpty() ? QString()
                                                 : QStringLiteral(".") + fi.suffix();
    QString candidate = QDir(destFolder).filePath(fileName);
    for (int n = 2; QFileInfo::exists(candidate) && n < 10000; ++n)
        candidate = QDir(destFolder).filePath(base + QStringLiteral(" (")
                                              + QString::number(n) + QStringLiteral(")") + suffix);
    return candidate;
}

QString MediaModel::transferTargetName(const QString& filePath,
                                       const QString& destFolder) const {
    const QString name = QFileInfo(filePath).fileName();
    if (name.isEmpty() || destFolder.isEmpty()) return {};
    return QFileInfo(freeTargetPath(destFolder, name)).fileName();
}

int MediaModel::transferToFolder(const QString& filePath, const QString& destFolder,
                                 bool move, int collision) {
    if (!isFileRow(rowForPath(filePath))) return 2;     // fremde Datei / Ordner
    const QFileInfo src(filePath);
    if (!src.exists() || !QFileInfo(destFolder).isDir()) return 2;
    // Derselbe Ordner: Ersetzen wuerde die Datei mit sich selbst ueberschreiben.
    if (QFileInfo(src.absolutePath()).canonicalFilePath()
        == QFileInfo(destFolder).canonicalFilePath()) return 2;

    const QString name = src.fileName();
    QString target = QDir(destFolder).filePath(name);
    if (QFileInfo::exists(target)) {
        if (collision == 0) return 1;                 // Aufrufer fragt nach
        if (collision == 2) target = freeTargetPath(destFolder, name);
        else if (!QFile::remove(target)) return 2;    // ersetzen
    }

    FileOp op;
    op.kind    = move ? FileOp::Kind::Move : FileOp::Kind::Delete;
    op.path    = filePath;
    op.movedTo = target;
    // Die Kategorien wandern ueber ihre Namen - vor dem Entfernen einsammeln.
    const QString srcFolder = src.absolutePath();
    collectMetaAt(srcFolder, name, &op);
    if (srcFolder == m_folder)
        op.categoryNames = m_tagManager.categoriesForFile(name);

    ++m_suppressWatch;
    bool ok = false;
    if (move) {
        ok = QFile::rename(filePath, target);
        if (!ok) {                                    // anderes Dateisystem
            ok = QFile::copy(filePath, target);
            if (ok && !QFile::remove(filePath)) { QFile::remove(target); ok = false; }
        }
    } else {
        ok = QFile::copy(filePath, target);
    }
    if (ok && move) {
        dropMetaAt(srcFolder, name, op);
        writeMetaToFolder(destFolder, QFileInfo(target).fileName(), op,
                          m_storage.tagColors());
    }
    --m_suppressWatch;
    if (!ok) return 2;

    if (move) {
        dropRowFor(filePath);
        pushUndo(op);
        invalidateFolderCount(srcFolder);
    }
    // Auch beim Kopieren waechst der Zielordner.
    invalidateFolderCount(mg::normalizedFolder(destFolder));
    return 0;
}

// Wahrheitsquelle ist m_selected, ein Byte je Zeile parallel zu m_items.

void MediaModel::noteSelectionChanged() {
    ++m_selRevision;
    emit selectionChanged();
}

void MediaModel::recountSelection() {
    int n = 0;
    for (const quint8 v : std::as_const(m_selected))
        if (v) ++n;
    if (n == m_selCount) return;
    m_selCount = n;
    noteSelectionChanged();
}

bool MediaModel::isSelected(const QString& filePath) const {
    const int row = rowForPath(mg::toLocalPath(filePath));
    return row >= 0 && row < m_selected.size() && m_selected.at(row) != 0;
}

void MediaModel::setSelected(const QString& filePath, bool on) {
    const int row = rowForPath(mg::toLocalPath(filePath));
    if (row < 0 || row >= m_selected.size()) return;
    const quint8 want = on ? 1 : 0;
    if (m_selected.at(row) == want) return;
    m_selected[row] = want;
    m_selCount += on ? 1 : -1;
    emitRow(row, { SelectedRole });
    noteSelectionChanged();
}

void MediaModel::toggleSelected(const QString& filePath) {
    setSelected(filePath, !isSelected(filePath));
}

void MediaModel::clearSelection() {
    if (m_selCount == 0) return;
    // In zusammenhaengenden Laeufen melden: 40 geleerte Kacheln waeren sonst 40
    // Signale, und jedes liesse das Zeilenmodell neu bauen.
    int runFirst = -1;
    for (int r = 0; r < m_selected.size(); ++r) {
        if (m_selected.at(r)) {
            m_selected[r] = 0;
            if (runFirst < 0) runFirst = r;
        } else if (runFirst >= 0) {
            emitRows(runFirst, r - 1, { SelectedRole });
            runFirst = -1;
        }
    }
    if (runFirst >= 0)
        emitRows(runFirst, m_selected.size() - 1, { SelectedRole });
    m_selCount = 0;
    noteSelectionChanged();
}

QStringList MediaModel::selectedPaths(bool filesOnly) const {
    QStringList out;
    out.reserve(m_selCount);
    for (int r = 0; r < m_selected.size() && r < m_items.size(); ++r) {
        if (!m_selected.at(r)) continue;
        if (filesOnly && m_items.at(r).isFolder()) continue;
        out.append(m_items.at(r).filePath);
    }
    return out;
}

QStringList MediaModel::selectedFileNames() const {
    QStringList out;
    out.reserve(m_selCount);
    for (int r = 0; r < m_selected.size() && r < m_items.size(); ++r) {
        if (!m_selected.at(r) || m_items.at(r).isFolder()) continue;
        out.append(m_items.at(r).fileName());
    }
    return out;
}

QStringList MediaModel::tagsOfSelection() const {
    QStringList out;
    bool first = true;
    for (int r = 0; r < m_selected.size() && r < m_items.size(); ++r) {
        if (!m_selected.at(r) || m_items.at(r).isFolder()) continue;
        const QStringList& t = m_items.at(r).tags;
        if (first) { out = t; first = false; continue; }
        for (int k = out.size() - 1; k >= 0; --k)
            if (!t.contains(out.at(k))) out.removeAt(k);
        if (out.isEmpty()) break;
    }
    return out;
}

void MediaModel::setTagOnSelection(const QString& tag, bool on) {
    // Ueber die Pfade statt die Zeilen - setTagOnRow schreibt in das Sidecar des
    // Ordners, dem die Zeile gehoert.
    const QStringList paths = selectedPaths(/*filesOnly=*/true);
    for (const QString& path : paths) {
        const int row = rowForPath(path);
        if (row < 0) continue;
        const bool has = m_items.at(row).tags.contains(tag);
        if (has == on) continue;
        setTagOnRow(row, tag, on);
    }
}

QVector<int> MediaModel::selectedRows() const {
    QVector<int> out;
    out.reserve(m_selCount);
    for (int r = 0; r < m_selected.size(); ++r)
        if (m_selected.at(r)) out.append(r);
    return out;
}

void MediaModel::setSelectedRows(const QVector<int>& sortedRows) {
    // Gemeldet wird nur, was sich wirklich aendert - der Auswahlrahmen ruft das je
    // Mausbewegung.
    int k = 0;
    int n = 0;
    int lo = -1, hi = -1;
    for (int r = 0; r < m_selected.size(); ++r) {
        while (k < sortedRows.size() && sortedRows.at(k) < r) ++k;
        const quint8 want = (k < sortedRows.size() && sortedRows.at(k) == r) ? 1 : 0;
        if (want) ++n;
        if (m_selected.at(r) == want) continue;
        m_selected[r] = want;
        if (lo < 0) lo = r;
        hi = r;
    }
    // Eine Meldung ueber den ganzen beruehrten Bereich: der Rahmen aendert in
    // Ansichts-Ordnung, und die faellt nicht mit der Modellordnung zusammen.
    if (lo >= 0)
        emitRows(lo, hi, { SelectedRole });
    m_selCount = n;
    // Gemeldet wird, sobald sich irgendetwas aendert - ein wandernder Rahmen kann
    // gleich viele, aber andere Kacheln treffen.
    if (lo >= 0)
        noteSelectionChanged();
}

int MediaModel::deleteSelected() {
    // Erst Dateien, dann Ordner unter einer Gruppennummer: deleteFolder liest den
    // Ordner neu ein, danach faende keine Datei mehr ihre Zeile.
    QStringList files, folders;
    for (int r = 0; r < m_selected.size() && r < m_items.size(); ++r) {
        if (!m_selected.at(r)) continue;
        if (m_items.at(r).isFolder()) folders.append(m_items.at(r).filePath);
        else                          files.append(m_items.at(r).filePath);
    }
    if (files.isEmpty() && folders.isEmpty()) return 0;

    const int group = (files.size() + folders.size() > 1) ? ++m_opGroup : 0;
    int done = 0;
    // Die Gruppennummer wird nachtraeglich aufgedrueckt - ein Loeschen ohne
    // Papierkorb kommt gar nicht erst auf den Stapel.
    const auto stamp = [this, group] {
        if (group != 0 && !m_undoOps.isEmpty())
            m_undoOps.last().group = group;
    };
    for (const QString& path : std::as_const(files)) {
        if (!deleteItem(path)) continue;
        stamp();
        ++done;
    }
    for (const QString& path : std::as_const(folders)) {
        if (!trashFolderAt(path, /*reloadNow=*/false)) continue;
        stamp();
        ++done;
    }
    if (!folders.isEmpty())
        reload();               // EINMAL, nicht je Ordner
    if (done > 0)
        recountSelection();
    return done;
}

bool MediaModel::deleteItem(const QString& filePath) {
    if (!isFileRow(rowForPath(filePath)))
        return false;

    FileOp op;
    if (!trashFile(filePath, &op))
        return false;
    dropRowFor(filePath);
    invalidateFolderCount(QFileInfo(filePath).absolutePath());

    // Nur was im Papierkorb liegt, ist zurueckholbar.
    if (!op.trashPath.isEmpty()) {
        pushUndo(op);
    } else if (!m_undoOps.isEmpty() || !m_redoOps.isEmpty()) {
        clearFileHistory();
    }
    return true;
}

// Scheitert der Rueckweg, bleibt alles wie es ist.
bool MediaModel::undoMove(const FileOp& op) {
    if (op.movedTo.isEmpty() || !QFileInfo::exists(op.movedTo)) return false;
    if (QFileInfo::exists(op.path)) return false;

    ++m_suppressWatch;
    bool ok = QFile::rename(op.movedTo, op.path);
    if (!ok) {
        ok = QFile::copy(op.movedTo, op.path);
        if (ok && !QFile::remove(op.movedTo)) { QFile::remove(op.path); ok = false; }
    }
    if (ok) {
        removeMetaFromFolder(QFileInfo(op.movedTo).absolutePath(),
                             QFileInfo(op.movedTo).fileName());
        restoreMetaAt(QFileInfo(op.path).absolutePath(),
                      QFileInfo(op.path).fileName(), op);
        invalidateFolderCount(QFileInfo(op.movedTo).absolutePath());
        invalidateFolderCount(QFileInfo(op.path).absolutePath());
    }
    --m_suppressWatch;
    return ok;
}

namespace {
QString companionPathOf(const QString& filePath, int kind) {
    if (kind == 1) return filePath + QStringLiteral(".mgedit.json");
    if (kind == 2) return filePath + QStringLiteral(".bak");
    return QString();
}
}  // namespace

int MediaModel::companionKinds(const QString& filePath) const {
    const QString p = mg::toLocalPath(filePath);
    int mask = 0;
    if (QFileInfo::exists(companionPathOf(p, 1))) mask |= 1;
    if (QFileInfo::exists(companionPathOf(p, 2))) mask |= 2;
    return mask;
}

bool MediaModel::removeCompanion(const QString& filePath, int kind) {
    const QString p  = mg::toLocalPath(filePath);
    if (!isFileRow(rowForPath(p)))
        return false;
    const QString cp = companionPathOf(p, kind);
    if (cp.isEmpty() || !QFileInfo::exists(cp))
        return false;

    FileOp op;
    op.kind = FileOp::Kind::Companion;
    op.path = cp;                       // die BEGLEITdatei ist hier der Vorgang

    // Watcher unterdruecken: die Begleitdatei taucht in der Galerie gar nicht auf.
    ++m_suppressWatch;
    QString inTrash;
    bool ok = QFile::moveToTrash(cp, &inTrash);
    if (ok) op.trashPath = inTrash;
    else    ok = QFile::remove(cp);
    if (m_suppressWatch > 0) --m_suppressWatch;
    if (!ok)
        return false;

    // Nur was im Papierkorb liegt, ist zurueckholbar.
    if (!op.trashPath.isEmpty())
        pushUndo(op);
    else if (!m_undoOps.isEmpty() || !m_redoOps.isEmpty())
        clearFileHistory();

    // Ist die Begleitdatei sichtbar, verschwindet ihre Kachel.
    if (rowForPath(cp) >= 0)
        dropRowFor(cp);
    return true;
}

// Anders als bei einer Datei gibt es keine Metadaten - die liegen im Sidecar
// IM Ordner und sind mitgewandert.
bool MediaModel::restoreFolder(const FileOp& op, bool reloadNow) {
    if (op.trashPath.isEmpty() || !QFileInfo::exists(op.trashPath)) return false;
    if (QFileInfo::exists(op.path)) return false;      // Platz wieder belegt

    ++m_suppressWatch;
    const bool ok = QDir().rename(op.trashPath, op.path);
    --m_suppressWatch;
    // reloadNow == false kommt aus dem Gruppen-Rueckweg: erst alle Dateien, dann
    // einmal neu einlesen.
    if (ok && reloadNow) reload();
    return ok;
}

bool MediaModel::undoFileOp() {
    while (!m_undoOps.isEmpty()) {
        // Alle Eintraege einer Gruppe liegen im Stapel nebeneinander und gehen zusammen.
        const int grp = m_undoOps.last().group;
        QVector<FileOp> batch;
        if (grp == 0) {
            batch.append(m_undoOps.takeLast());
        } else {
            while (!m_undoOps.isEmpty() && m_undoOps.last().group == grp)
                batch.append(m_undoOps.takeLast());
        }

        // Innerhalb einer Gruppe zuerst die Dateien: ein Ordner-Rueckweg liest neu ein,
        // und appendRowFor waehrend eines Neuaufbaus legte die Zeile doppelt an.
        int  back      = 0;
        bool hadFolder = false;
        for (int pass = 0; pass < 2; ++pass) {
            for (const FileOp& op : std::as_const(batch)) {
                const bool isFolder = (op.kind == FileOp::Kind::Folder);
                if (isFolder != (pass == 1)) continue;
                const bool ok = (op.kind == FileOp::Kind::Move) ? undoMove(op)
                              : isFolder ? restoreFolder(op, /*reloadNow=*/false)
                              : restoreFile(op);   // Companion nutzt denselben Rückweg
                // Nicht zurueckholbar: Eintrag verwerfen und weitermachen statt haengen bleiben.
                if (!ok) continue;
                // Eine Datei kommt gezielt als Zeile zurueck, ein Ordner braucht Neu-Einlesen.
                if (isFolder) hadFolder = true;
                else          appendRowFor(op.path);
                m_redoOps.push_back(op);
                ++back;
            }
        }
        if (hadFolder) reload();
        emit fileHistoryChanged();
        if (back > 0) return true;
    }
    return false;
}

bool MediaModel::redoFileOp() {
    while (!m_redoOps.isEmpty()) {
        // Eine Gruppe geht auch vorwaerts als ein Schritt - sonst braeuchte es N-mal Strg+Y.
        const int grp = m_redoOps.last().group;
        if (grp != 0) {
            // Die ganze Gruppe zuerst abheben: trashFolderAt leert beim Ablegen den
            // Redo-Zweig, mitten in der Schleife waeren die Eintraege weg gewesen.
            QVector<FileOp> batch;
            while (!m_redoOps.isEmpty() && m_redoOps.last().group == grp)
                batch.append(m_redoOps.takeLast());

            int  again      = 0;
            bool hadFolder  = false;
            for (FileOp gop : batch) {
                if (rowForPath(gop.path) < 0) continue;
                if (gop.kind == FileOp::Kind::Folder) {
                    if (!trashFolderAt(gop.path, /*reloadNow=*/false)) continue;
                    hadFolder = true;
                    if (!m_undoOps.isEmpty()) m_undoOps.last().group = grp;
                    ++again;
                    continue;
                }
                if (!trashFile(gop.path, &gop)) continue;
                dropRowFor(gop.path);
                if (gop.trashPath.isEmpty()) continue;
                gop.group = grp;
                m_undoOps.push_back(gop);
                if (m_undoOps.size() > kMaxFileOps) m_undoOps.removeFirst();
                ++again;
            }
            if (hadFolder) reload();          // EINMAL, nicht je Ordner
            emit fileHistoryChanged();
            if (again > 0) return true;
            continue;
        }

        FileOp op = m_redoOps.takeLast();
        if (rowForPath(op.path) < 0) { emit fileHistoryChanged(); continue; }

        bool ok = false;
        if (op.kind == FileOp::Kind::Folder) {
            if (deleteFolder(op.path)) {
                // deleteFolder verbucht den Vorgang selbst und leert dabei den Redo-Zweig.
                emit fileHistoryChanged();
                return true;
            }
            emit fileHistoryChanged();
            continue;
        }
        if (op.kind == FileOp::Kind::Move) {
            // Ueber denselben Weg wie beim ersten Mal, damit die Metadaten mitwandern.
            const QString destFolder = QFileInfo(op.movedTo).absolutePath();
            ok = (transferToFolder(op.path, destFolder, /*move=*/true, /*collision=*/2) == 0);
            if (ok) { emit fileHistoryChanged(); return true; }   // legt selbst ab
        } else {
            ok = trashFile(op.path, &op);
            if (ok) {
                dropRowFor(op.path);
                if (!op.trashPath.isEmpty()) {
                    m_undoOps.push_back(op);
                    if (m_undoOps.size() > kMaxFileOps)
                        m_undoOps.removeFirst();
                }
                emit fileHistoryChanged();
                return true;
            }
        }
        emit fileHistoryChanged();
    }
    return false;
}

QString MediaModel::undoFileOpName() const {
    return m_undoOps.isEmpty() ? QString()
                               : QFileInfo(m_undoOps.last().path).fileName();
}

QString MediaModel::redoFileOpName() const {
    return m_redoOps.isEmpty() ? QString()
                               : QFileInfo(m_redoOps.last().path).fileName();
}

// 1 bei Einzelvorgang, sonst die Gruppenlaenge - daran haengt das und N weitere.
int MediaModel::undoFileOpCount() const {
    if (m_undoOps.isEmpty()) return 0;
    const int grp = m_undoOps.last().group;
    if (grp == 0) return 1;
    int n = 0;
    for (int i = m_undoOps.size() - 1; i >= 0 && m_undoOps.at(i).group == grp; --i)
        ++n;
    return n;
}

int MediaModel::redoFileOpCount() const {
    if (m_redoOps.isEmpty()) return 0;
    const int grp = m_redoOps.last().group;
    if (grp == 0) return 1;
    int n = 0;
    for (int i = m_redoOps.size() - 1; i >= 0 && m_redoOps.at(i).group == grp; --i)
        ++n;
    return n;
}

// Fuer den offenen Ordner ueber den TagManager - nur so erfahren Seitenleiste
// und Filter davon; ein Unterordner hat keinen.
void MediaModel::setTagOnRow(int row, const QString& tag, bool on) {
    MediaItem& it = m_items[row];
    const QString name = it.fileName();

    // Die eigene Aenderung darf den Watcher nicht in ein reload() treiben.
    ++m_suppressWatch;
    if (it.scope == 0) {
        if (on) m_tagManager.addTagToFile(name, tag);
        else    m_tagManager.removeTagFromFile(name, tag);
        it.tags = m_tagManager.tagsForFile(name);
    } else if (JsonStorage* st = storageForScope(it.scope)) {
        QStringList tags = st->getTags(name);
        // Dieser Sidecar gehoert einem Unterordner und laeuft nicht ueber den TagManager -
        // er muss ausdruecklich in den laufenden Undo-Schritt aufgenommen werden.
        if (on != tags.contains(tag))
            m_tagManager.noteForeignFolder(st->folderPath(),
                mg::tagmark::mkCounted(on ? 1 : 0, on ? 0 : 1,
                                       mg::tagmark::Thing::Tag, tag, {}));
        bool changed = false;
        if (on && !tags.contains(tag)) {
            tags.append(tag);
            st->ensureTagRegistered(tag);
            const QHash<QString, QColor> colors = m_storage.tagColors();
            const auto c = colors.constFind(tag);
            if (c != colors.constEnd()) st->setTagColor(tag, c.value());
            changed = true;
        } else if (!on && tags.removeAll(tag) > 0) {
            changed = true;
        }
        if (changed) {
            st->setTags(name, tags);
            st->saveCurrentFolder();
        }
        it.tags = st->getTags(name);
    }
    --m_suppressWatch;

    emitRow(row, { TagsRole });
}

// nullptr, wenn die Datei nicht zur Ansicht gehoert; row liefert die Zeile mit.
JsonStorage* MediaModel::storageOfFile(const QString& filePath, int* row) {
    const int r = rowForPath(filePath);
    if (row) *row = r;
    if (!isFileRow(r)) return nullptr;
    return storageForScope(m_items.at(r).scope);
}

QStringList MediaModel::tagsOfFile(const QString& filePath) const {
    const int r = rowForPath(filePath);
    return isFileRow(r) ? m_items.at(r).tags : QStringList();
}

void MediaModel::removeTag(const QString& filePath, const QString& tag) {
    const int row = rowForPath(filePath);
    if (row < 0 || tag.isEmpty() || !isFileRow(row)) return;
    if (!m_items.at(row).tags.contains(tag)) return;
    setTagOnRow(row, tag, false);
}

// Beantwortet aus der Datei: weicht das Aenderungs- vom Erstellungsdatum ab,
// fuehrt Zuruecksetzen zu etwas. Kein Merker noetig.
bool MediaModel::hasCustomDate(const QString& filePath) const {
    const QFileInfo fi(mg::toLocalPath(filePath));
    const QDateTime born = fi.birthTime(QTimeZone::UTC);
    const QDateTime mod  = fi.lastModified(QTimeZone::UTC);
    return born.isValid() && mod.isValid() && born != mod;
}

QDateTime MediaModel::customDate(const QString& filePath) const {
    const int r = rowForPath(filePath);
    return isFileRow(r) ? m_items.at(r).dateTime.toLocalTime() : QDateTime();
}

void MediaModel::setCustomDate(const QString& filePath, const QDateTime& dt) {
    const int row = rowForPath(filePath);
    if (!isFileRow(row)) return;

    // Das Datum gehoert an die Datei und nur dorthin - dieselbe Angabe zweimal zu
    // fuehren hiesse nur, dass sie auseinanderlaeuft.
    ++m_suppressWatch;
    bool wroteFile = false;
    {
        QFile f(filePath);
        if (f.open(QIODevice::ReadWrite)) {
            wroteFile = f.setFileTime(dt, QFileDevice::FileModificationTime);
            f.close();
        }
    }
    --m_suppressWatch;

    if (!wroteFile) {
        // Schreibgeschuetzt oder fremdes Dateisystem: dann passiert nichts.
        emit fileDateNotWritten(QFileInfo(filePath).fileName());
        return;
    }

    // Zuruecklesen: manche Dateisysteme (FAT) runden auf zwei Sekunden.
    const QDateTime after = QFileInfo(filePath).lastModified(QTimeZone::UTC);
    m_items[row].dateTime = after.isValid() ? after : dt;
    emitRow(row, { DateTimeRole });
}

void MediaModel::clearCustomDate(const QString& filePath) {
    const int row = rowForPath(filePath);
    if (!isFileRow(row)) return;

    // Zurueck heisst auf das Erstellungsdatum - es aendert sich nie, man kommt also
    // immer an denselben Punkt zurueck.
    const QDateTime born = QFileInfo(filePath).birthTime(QTimeZone::UTC);
    if (!born.isValid()) {
        emit fileDateNotWritten(QFileInfo(filePath).fileName());
        return;
    }

    ++m_suppressWatch;
    bool wrote = false;
    {
        QFile f(filePath);
        if (f.open(QIODevice::ReadWrite)) {
            wrote = f.setFileTime(born, QFileDevice::FileModificationTime);
            f.close();
        }
    }
    --m_suppressWatch;

    if (!wrote) { emit fileDateNotWritten(QFileInfo(filePath).fileName()); return; }

    m_items[row].dateTime = QFileInfo(filePath).lastModified(QTimeZone::UTC);
    emitRow(row, { DateTimeRole });
}

QColor MediaModel::fileTextPdfColor(const QString& filePath) const {
    const QString p = mg::toLocalPath(filePath);
    const int r = rowForPath(p);
    if (!isFileRow(r)) return {};
    auto* self = const_cast<MediaModel*>(this);
    JsonStorage* st = self->storageForScope(m_items.at(r).scope);
    return st ? st->textPdfColor(m_items.at(r).fileName()) : QColor();
}

bool MediaModel::hasFileTextPdfColor(const QString& filePath) const {
    return fileTextPdfColor(filePath).isValid();
}

void MediaModel::setFileTextPdfColor(const QString& filePath, const QColor& c) {
    int row = -1;
    JsonStorage* st = storageOfFile(mg::toLocalPath(filePath), &row);
    if (!st) return;
    ++m_suppressWatch;
    st->setTextPdfColor(m_items.at(row).fileName(), c);
    st->saveCurrentFolder();
    --m_suppressWatch;
}

void MediaModel::clearFileTextPdfColor(const QString& filePath) {
    int row = -1;
    JsonStorage* st = storageOfFile(mg::toLocalPath(filePath), &row);
    if (!st) return;
    ++m_suppressWatch;
    st->clearTextPdfColor(m_items.at(row).fileName());
    st->saveCurrentFolder();
    --m_suppressWatch;
}

void MediaModel::toggleTag(const QString& filePath, const QString& tag) {
    const int row = rowForPath(filePath);
    if (row < 0 || tag.isEmpty() || !isFileRow(row)) return;
    setTagOnRow(row, tag, !m_items.at(row).tags.contains(tag));
}

void MediaModel::addTag(const QString& filePath, const QString& tag) {
    const int row = rowForPath(filePath);
    if (row < 0 || tag.isEmpty() || !isFileRow(row)) return;
    if (m_items.at(row).tags.contains(tag)) return;   // schon dran
    setTagOnRow(row, tag, true);
}

void MediaModel::onDirectoryChanged() {
    if (m_suppressWatch > 0) return;     // interne Mutation, kein Reload
    if (m_folder.isEmpty()) return;
    reload();
    emit folderContentsChanged();
}
