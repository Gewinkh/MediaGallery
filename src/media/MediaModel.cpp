#include "media/MediaModel.h"

#include "core/AppSettings.h"
#include "core/JsonStorage.h"
#include "media/MediaProxyModel.h"
#include "tags/TagCategory.h"
#include "tags/TagManager.h"
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

//  Diagnose-Protokoll fuer die Miniaturen: `MG_LOG_THUMBS=1` schreibt jeden
//  Anforderungs-, Abbestell- und Liefervorgang nach stderr. Kostet ohne die
//  Umgebungsvariable nichts (ein `const bool`, einmal beim Start gelesen) und
//  beantwortet die einzige Frage, die von aussen nicht zu sehen ist: Wurde eine
//  Vorschau nie angefordert, unterwegs abbestellt oder nie geliefert?
static const bool kLogThumbs = qEnvironmentVariableIntValue("MG_LOG_THUMBS") == 1;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
//  Ein Verzeichnis ZAEHLEN - im Worker, nach dem Hausmuster (Regel 8).
//  Gezaehlt wird, was die Galerie dort zeigen wuerde: Unterordner und erkannte
//  Mediendateien. Ein Verzeichnis kann Zehntausende Eintraege haben; auf dem
//  GUI-Thread waere das ein Ruckler je sichtbarer Ordnerkachel.
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
        //  Zugestellt wird am BESITZER, nicht an `qApp`: das `qApp`-Makro
        //  castet `QCoreApplication::instance()` auf `QGuiApplication` - in
        //  einem Prozess ohne GUI (jeder Testtreiber) ist das ein ungueltiger
        //  Downcast und damit undefiniertes Verhalten. UBSan meldet genau das
        //  („downcast of address … which does not point to an object of type
        //  'QGuiApplication'").
        //
        //  Der Zeiger ist gueltig, weil `~MediaModel` den Pool vorher leert und
        //  auslaufen laesst - es kann also kein Auftrag mehr laufen, waehrend
        //  der Besitzer stirbt. Der QPointer bleibt als zweite Sicherung im
        //  GUI-Thread (Muster PdfScanTask).
        MediaModel* owner = m_owner.data();
        if (!owner) return;
        const QString folder = m_folder;
        const int gen = m_generation;
        const int ticket = m_ticket;
        //  KEIN zweiter QPointer im Lambda: er waere ueberfluessig (der Pool
        //  laeuft vor der Zerstoerung aus, und Qt entfernt gepostete Ereignisse
        //  eines sterbenden QObject ohnehin) und traegt einen Referenzzaehler
        //  ueber die Thread-Grenze, den Helgrind als Rennen meldet.
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

// ─────────────────────────────────────────────────────────────────────────────
//  Den Baum unterhalb des offenen Ordners nach Treffern durchsuchen.
//
//  Je Ordner wird EINMAL sein Sidecar gelesen (Tags und Kategorien liegen dort)
//  und dann jede Datei mit `MediaProxyModel::acceptsFile` geprueft - derselben
//  Funktion wie in der Anzeige. Ein Ordner zaehlt auch dann als Treffer, wenn
//  sein eigener NAME passt: sonst waere ein tief liegender Ordner mit
//  passendem Namen nicht erreichbar.
//
//  Symlinks werden NICHT verfolgt: das schuetzt vor Verzeichnisschleifen und
//  spart je Eintrag ein `realpath` (Festlegung des Nutzers: sicher UND schnell).
// ─────────────────────────────────────────────────────────────────────────────
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

    //  ── EIN Ordner ────────────────────────────────────────────────────────
    //  Rein funktional: alles, was gebraucht wird, kommt als Argument, das
    //  Ergebnis geht als Rückgabewert zurück. Nur so darf das gleichzeitig auf
    //  mehreren Fäden laufen (`JsonStorage` entsteht hier und stirbt hier,
    //  `MediaProxyModel::acceptsFile` ist statisch und rührt nichts an).
    //  `subdirs` sammelt die Unterordner ein - wer sie weiterverfolgt,
    //  entscheidet der Aufrufer.
    bool scanOne(const QString& dir, QStringList* subdirs) const {
        const bool isRoot = (dir == m_root);
            const QString sidecar = mg::folderSidecarName(dir);

            //  Sidecar dieses Ordners: Tags, eigenes Datum, Kategorien.
            //  Eine EIGENE Instanz im Worker; sie liest nur.
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
                    && QFileInfo(dir).fileName().contains(crit.search, Qt::CaseInsensitive);

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
                const MediaType t = MediaItem::detectType(fi.filePath());
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
        //  ── 1) Die Ordner einsammeln ──────────────────────────────────────
        //  Das bleibt EINFÄDIG: es ist billig (gemessen: 259 Ordner in
        //  wenigen Millisekunden), und der Baum ist erst danach bekannt.
        //  Deckel gegen einen versehentlich riesigen Baum: die Suche soll
        //  Ordner finden, nicht das halbe Dateisystem indizieren.
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

        //  ── 2) Die Ordner auf die Kerne verteilen ─────────────────────────
        //  Jeder Ordner ist unabhängig: eigener Sidecar, eigene Dateien. Die
        //  Zahl der Fäden kommt vom RECHNER (`idealThreadCount`), nicht aus
        //  einer Konstanten - ein Zweikern-Laptop soll nicht acht Fäden
        //  starten, ein Achtkerner nicht auf einem sitzen bleiben. Gedeckelt
        //  auf 8: der Engpass ist ab da die Platte, nicht der Kern.
        //  Bei wenigen Ordnern lohnt das Verteilen nicht - dann läuft es hier.
        //  `MG_DEEPTHREADS` überschreibt die Zahl - für Messungen (1 = wie
        //  früher) und als Notausgang auf einem Rechner, dem das nicht bekommt.
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
            //  Ein Pool NUR für diesen Lauf: er entsteht mit der Suche und
            //  verschwindet mit ihr (Kosten je Faden: Bruchteile einer
            //  Millisekunde gegen eine Suche im Sekundenbereich). Damit hängen
            //  zwischen zwei Suchen keine Fäden im Leerlauf.
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

            //  Wieder in die Reihenfolge des Baumes bringen: die Treffer sollen
            //  aufklappen wie der Nutzer den Baum sieht, nicht wie die Fäden
            //  fertig wurden.
            QSet<QString> found;
            for (const QStringList& l : std::as_const(perThread))
                for (const QString& d : l) found.insert(d);
            for (const QString& d : std::as_const(dirs))
                if (found.contains(d)) hits.append(d);
        }

        //  Diagnose (nur mit `MG_DEEPLOG=1`): wie lange hat das SUCHEN
        //  gedauert - ohne das Aufklappen der Treffer, das danach im GUI-Faden
        //  passiert und den größeren Teil der Wartezeit ausmacht.
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
// Größe der ersten (synchronen) Charge: genug, um typische Viewports sofort zu
// füllen. Folgechargen sind größer, da sie ohnehin zwischen Event-Loop-Ticks
// laufen und nicht den ersten Frame blockieren.
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
    //  Der Sidecar des OFFENEN Ordners darf seine Schreibvorgänge sammeln:
    //  dort fallen sie im Rudel an (100 Dateien auf einen Tag ziehen), und
    //  jeder einzelne serialisiert die ganze Datei. Die Sidecars aufgeklappter
    //  UNTERordner (`m_scopeStorage`) bleiben bewusst sofort schreibend - s.
    //  `JsonStorage::setDeferredSaves`.
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

    // ── Abbestellungen erst am Ende des Durchlaufs ausfuehren ────────────────
    m_cancelTimer.setSingleShot(true);
    m_cancelTimer.setInterval(0);
    connect(&m_cancelTimer, &QTimer::timeout, this, [this]() {
        for (const QString& p : std::as_const(m_cancelPending)) {
            const int row = rowForPath(p);
            if (row >= 0 && m_thumbState[row] == 1) continue;   // inzwischen da
            //  In diesem Durchlauf hat jemand denselben Pfad angefordert - das
            //  ist die uebernehmende Kachel. Ihre Anforderung darf die
            //  abgebende Kachel nicht wegraeumen (s. Header).
            //  In diesem Durchlauf hat jemand denselben Pfad angefordert - das
            //  ist die uebernehmende Kachel. Ihre Anforderung darf die
            //  abgebende Kachel nicht wegraeumen (s. Header).
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

    // ── Inkrementelle Befüllung: 0-ms-Timer speist je Tick eine Charge ein ───
    m_fillTimer.setSingleShot(false);
    m_fillTimer.setInterval(0);   // „sobald die Event-Loop atmet“ - kein Blockieren
    connect(&m_fillTimer, &QTimer::timeout, this, [this]() {
        feedChunk(/*firstChunk=*/false);
        if (!hasMoreToFill()) {
            m_fillTimer.stop();
            finishFill();
        }
    });

    // Tag-Änderungen aus anderen Quellen (z. B. Tag-Manager) -> sichtbare Tags neu.
    //
    //  NUR Zeilen des geoeffneten Ordners: `m_tagManager` haengt an dessen
    //  Sidecar und schluesselt nach DATEINAMEN. Eine gleichnamige Datei in einem
    //  aufgeklappten Unterordner bekaeme sonst fremde Tags untergeschoben.
    connect(&m_tagManager, &TagManager::tagsChanged, this, [this]() {
        if (m_items.isEmpty()) return;
        for (auto& it : m_items) {
            if (it.scope != 0 || it.isFolder()) continue;
            it.tags = m_tagManager.tagsForFile(it.fileName());
        }
        emit dataChanged(index(0), index(m_items.size() - 1), { TagsRole });
    });
}

//  s. Header: hier ist QDirIterator vollständig bekannt.
//
//  Der Zaehl-Pool wird HIER geleert und ausgelaufen - nicht erst als Kind in
//  `~QObject`. Kinder sterben nach dem Rumpf dieses Destruktors; ein noch
//  laufender Auftrag haette dann einen halb zerstoerten Besitzer vor sich.
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

// ─── Enumeration / inkrementelle Befüllung ───────────────────────────────────
void MediaModel::rebuild(const QString& folderPath) {
    // Laufende Befüllung abbrechen.
    m_fillTimer.stop();
    m_pendingIt.reset();
    m_pendingScope = -1;
    m_scanQueue.clear();
    //  Vormerkungen gehoeren zum alten Bestand.
    m_cancelTimer.stop();
    m_cancelPending.clear();
    m_thumbWanted.clear();

    // Leeres Modell SOFORT publizieren -> die UI rendert ohne Verzögerung den
    // Leerzustand bzw. beginnt unmittelbar mit der ersten Charge.
    beginResetModel();
    m_items.clear();
    m_thumbUrls.clear();
    m_thumbState.clear();
    //  Die Auswahl gehoert zur ALTEN Ansicht und faellt mit ihr weg.
    m_selected.clear();
    recountSelection();
    m_pathToRow.clear();
    //  Die Bereichstabelle wird beim Neuaufbau geleert, `m_expanded` NICHT:
    //  daraus setzt sich der aufgeklappte Unterbaum waehrend des Einlesens von
    //  selbst wieder zusammen (queueExpandedFolders) - ein Watcher-Reload
    //  klappt also nicht alles zu.
    m_scopes.clear();
    m_scopeOfPath.clear();
    qDeleteAll(m_scopeStorage);
    m_scopeStorage.clear();
    //  Gezaehlte Ordnerstaende sind nach einem Neuaufbau nicht mehr zu trauen
    //  (der Watcher laeuft ja genau dann, wenn sich etwas geaendert hat).
    ++m_countGeneration;
    m_folderCounts.clear();
    m_countPending.clear();
    //  Die Marken bleiben stehen (sie steigen nur) - zuruecksetzen koennte
    //  einen noch laufenden Auftrag wieder gueltig machen.
    endResetModel();
    emit countChanged();

    if (folderPath.isEmpty())
        return;

    // Wurzelbereich anlegen. Die Ordner-Konfiguration liegt als Sidecar
    // "<Ordnername>.json" IM Ordner (siehe JsonStorage) - diese Datei ist keine
    // Mediendatei und wird nicht als Kachel angezeigt. Der Name gehoert deshalb
    // zum BEREICH: jeder aufgeklappte Unterordner hat seinen eigenen.
    FolderScope root;
    root.path      = folderPath;
    //  Ueber den Helfer, NICHT ueber `QFileInfo::fileName()`: bei einem Pfad
    //  mit abschliessendem Trenner liefert der einen Leerstring, und die
    //  Ordner-JSON stand als Kachel in der Galerie (vom Nutzer gemeldet).
    root.sidecar   = mg::folderSidecarName(folderPath);
    root.parent    = -1;
    root.depth     = 0;
    root.folderRow = -1;
    root.active    = true;
    m_scopes.append(root);
    m_scopeOfPath.insert(folderPath, 0);

    // ── Verzeichnis STREAMEND lesen (QDirIterator) statt vorab als Liste ─────
    //  Vorher materialisierte `QDir::entryInfoList` den GESAMTEN Ordner als
    //  QFileInfoList, bevor die erste Kachel sichtbar wurde: je Eintrag ein
    //  QFileInfoPrivate samt QFileSystemEntry und QFileSystemMetaData. Bei
    //  20 000 Dateien waren das ~20 MB, die bis zum Ende der Befüllung liegen
    //  blieben - und der Nutzer wartete auf die Enumeration ALLER Einträge,
    //  obwohl der Viewport nur die ersten paar Dutzend zeigt.
    //  Der Iterator hält dagegen immer nur EINEN Eintrag; jede Charge liest
    //  genau so viele Einträge, wie sie einspeist.
    //
    //  KEINE Sortierung (QDirIterator sortiert grundsätzlich nicht, und
    //  `QDir::Name` wäre ohnehin verschenkt): die sichtbare Reihenfolge legt
    //  ausschließlich `MediaProxyModel` fest (`sort(0, …)` im Konstruktor,
    //  `dynamicSortFilter`), dessen `lessThan` bei Gleichstand deterministisch
    //  über den Anzeigenamen entscheidet. Damit ist die Anzeige unverändert,
    //  eine Voll-Sortierung aller Namen entfällt aber.
    //  ALLE Miniaturen sind jetzt „ausstehend" - das muss die Ansicht erfahren.
    //  Seit das Zeilenmodell als DIFF einspielt, ueberleben die Kacheln einen
    //  Reset und behalten ihr `requestedPath`; sie halten sich fuer fertig und
    //  fordern von selbst nie wieder an.
    //
    //  Das Signal darf aber ERST kommen, wenn die Zeilen wieder da sind: die
    //  Kacheln antworten darauf mit `ensureThumbnail(pfad)`, und das laeuft ins
    //  Leere, solange `rowForPath` den Pfad nicht kennt. Genau daran scheiterte
    //  der erste Versuch - nach einem Reload kam ohne Fenstergroesse-Aendern
    //  oder Scrollen KEINE einzige Miniatur zurueck (am Pruefstand als
    //  „0 von 40" reproduziert). Vorgemerkt hier, gesendet in `finishFill`.
    m_pendingInvalidate = true;

    startScan(0);

    // Erste Charge SYNCHRON -> Viewport ist sofort gefüllt (kein Flackern),
    // der Rest folgt gechunkt über den Timer.
    feedChunk(/*firstChunk=*/true);
    if (hasMoreToFill())
        m_fillTimer.start();
    else
        finishFill();
}

//  Versteckte Dateien nur auf Wunsch. In einem Medienordner sind sie fast immer
//  Beiwerk; in einem Projektordner will man `.gitignore` aber sehen (vom Nutzer
//  gemeldet). Die Einstellung wird bei JEDEM Einlesen frisch gelesen, damit ein
//  Umschalten nach dem naechsten Neuladen greift.
QDir::Filters MediaModel::hiddenFlag() {
    return AppSettings::instance().showHiddenFiles() ? QDir::Hidden : QDir::Filters();
}

//  Iterator fuer GENAU EINEN Bereich. Verzeichnisse kommen mit (`QDir::Dirs`),
//  denn Unterordner sind jetzt Kacheln.
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

    //  Immer nur EIN Bereich je Charge: die Zeilen einer Charge teilen sich den
    //  Bereich, und nur so laesst sich fuer die ganze Charge auf einen Schlag
    //  entscheiden, ob die Sidecar-Metadaten des offenen Ordners gelten.
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
            //  fileName(), NICHT completeBaseName(): ein Ordner „Urlaub 2025.alt"
            //  heisst so und nicht „Urlaub 2025".
            item.displayName = fi.fileName();
            item.type        = MediaType::Folder;
            item.dateTime    = fi.lastModified(QTimeZone::UTC);
            item.fileSize    = 0;
            item.scope       = scope;
            batch.append(std::move(item));
            ++produced;
            continue;
        }

        //  Begleitdateien der App (Ordner-JSON, Editor-Sidecar, `.bak`) -
        //  ausgeblendet, solange der Schalter aus ist. Die Regel steht in
        //  `mg::isCompanionFile`, damit der Dateiwähler nicht anders filtert.
        if (!m_showAllFiles && mg::isCompanionFile(fi.fileName(), sidecar))
            continue;

        const MediaType t = MediaItem::detectType(fi.filePath());
        //  „Alle Dateien anzeigen" heißt WIRKLICH alle: auch was die App nicht
        //  als Medium erkennt (`.bak`, Archive, Programme). Sonst hielte der
        //  Schalter sein Versprechen nur halb - die Sicherungskopie einer DOCX
        //  bliebe unsichtbar, obwohl sie ausdrücklich gemeint war.
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

    //  Persistierte Metadaten (Tags + ggf. Custom-Datum) aus dem Sidecar DIESES
    //  Bereichs. Ein aufgeklappter Unterordner hat sein eigenes - nur so tragen
    //  seine Dateien ihre echten Zuordnungen, statt die eines gleichnamigen
    //  Nachbarn im Elternordner zu erben.
    if (JsonStorage* st = storageForScope(scope)) {
        st->applyToItems(batch);
        for (auto& item : batch) {
            if (item.isFolder()) continue;
            const QString name = item.fileName();
            //  KEIN Datum aus dem Sidecar: es steht an der Datei und wurde
            //  beim Einlesen schon von dort genommen.
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

    //  Ordnerzeilen dieser Charge, die offen sein SOLLEN, gleich einreihen.
    queueExpandedFolders(first);
}

//  Nach dem Einspeisen: alles ab `firstRow` durchsehen und jeden Ordner, der in
//  `m_expanded` steht, mit einem Bereich versehen und zum Einlesen vormerken.
//  Dadurch stellt sich ein ganzer aufgeklappter Unterbaum nach einem reload()
//  von selbst wieder her - Ebene fuer Ebene, wie er gefunden wird.
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

    //  Diagnose (nur mit `MG_DEEPLOG=1`): wie lange hat das AUFKLAPPEN nach
    //  einer Suche gedauert - der Teil, den der Nutzer als Wartezeit erlebt,
    //  nachdem der Suchlauf selbst längst fertig ist.
    if (m_deepFillTimer.isValid()) {
        qInfo("[MG_DEEPLOG] Aufklappen: %lld Zeilen im Modell nach %lld ms",
              qint64(m_items.size()), m_deepFillTimer.elapsed());
        m_deepFillTimer.invalidate();
    }

    //  Jetzt stehen die Zeilen - jetzt kann die Ansicht ihre Miniaturen neu
    //  anfordern (s. rebuild()).
    if (m_pendingInvalidate) {
        m_pendingInvalidate = false;
        emit thumbnailsInvalidated();
    }

    //  … und jetzt die MEDIEN-ANZAHL der Ordnerkacheln. `rebuild()` wirft die
    //  gezaehlten Staende weg - zu Recht, denn ein Reload laeuft genau dann,
    //  wenn sich etwas geaendert hat. Angefordert hat sie aber nur die Kachel
    //  selbst (`Component.onCompleted`/`onFilePathChanged`), und die ueberlebt
    //  den Reload mit unveraendertem Pfad: niemand fragte mehr, die Zeile blieb
    //  leer, bis ein Ordnerwechsel die Kacheln neu baute (Nutzerbefund).
    //  Gezaehlt wird weiterhin nur, wonach schon einmal gefragt wurde - die
    //  Sichtbarkeitssteuerung bleibt also erhalten.
    if (!m_countWanted.isEmpty()) {
        //  Ueber eine KOPIE laufen: `ensureFolderCount` traegt selbst in die
        //  Menge ein.
        const QStringList wanted(m_countWanted.cbegin(), m_countWanted.cend());
        for (const QString& folder : wanted) {
            if (rowForPath(folder) < 0) continue;   // in diesem Bestand nicht (mehr) da
            ensureFolderCount(folder);
        }
    }

    //  Trim GENAU HIER: erst jetzt ist die inkrementelle Befuellung wirklich
    //  fertig. Der Trim in loadFolder() laeuft schon nach der ERSTEN Charge und
    //  kann den Speicher des vorherigen Ordners daher noch nicht vollstaendig
    //  zurueckgeben.
    //
    //  BEWUSST KEIN squeeze() auf den Parallel-Vektoren: gemessen (20 000
    //  Dateien) betraegt die Wachstums-Reserve nur ~93 kB von 9,2 MB Nutzdaten
    //  (unter 1 %) - Qts Wachstumsstrategie ist keine reine Verdopplung. Dafuer
    //  wuerde squeeze() den gesamten Item-Vektor umkopieren und damit die
    //  SPITZE kurzzeitig verdoppeln. Schlechter Tausch (§0-Prio 2 vor 4).
    mg::trimHeap();
}

int MediaModel::rowForPath(const QString& filePath) const {
    return m_pathToRow.value(filePath, -1);
}

// ─── Bereiche & Aufklappen ───────────────────────────────────────────────────
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

//  Bereiche vom Modell ABHAENGEN: wartende und laufende Einlesevorgaenge
//  verwerfen, ihre Zeilen entfernen, Beobachtung beenden, Bereiche stilllegen.
//  Die Bereiche selbst bleiben in der Tabelle stehen (s. FolderScope).
void MediaModel::removeRowsOfScopes(const QSet<int>& scopes) {
    if (scopes.isEmpty()) return;

    for (auto it = m_scanQueue.begin(); it != m_scanQueue.end(); ) {
        if (scopes.contains(*it)) it = m_scanQueue.erase(it);
        else                      ++it;
    }
    //  Ein LAUFENDER Iterator dieses Bereichs muss sofort weg - sonst speiste
    //  die naechste Charge Zeilen in einen Bereich ein, den es nicht mehr gibt.
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
        //  Waren zugeklappte Zeilen ausgewaehlt, ist die Auswahl jetzt kleiner.
        recountSelection();
    }

    for (int s : scopes) {
        if (s <= 0 || s >= m_scopes.size()) continue;
        m_scopes[s].active    = false;
        m_scopes[s].folderRow = -1;
        //  Das Sidecar dieses Bereichs wird nicht mehr gebraucht - es kann
        //  gross sein und wird beim Wiederaufklappen frisch gelesen.
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
    //  Bereiche zeigen auf ZEILEN - nach jeder Verschiebung nachziehen, sonst
    //  klettert lessThan die Elternkette ins Leere.
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

    //  Schleifenschutz: ein Symlink, der auf einen Vorfahren zeigt, schachtelte
    //  die Ansicht endlos. Verglichen wird der KANONISCHE Pfad - und nur hier,
    //  beim Aufklappen; je Verzeichniseintrag waere das ein realpath() zu viel.
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
    //  Nur DIESER Ordner wird vergessen - die Enkel bleiben vorgemerkt. Klappt
    //  man ihn wieder auf, steht sein Unterbaum wieder so da wie vorher.
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
    //  Der Wunsch wird gemerkt, BEVOR abgekuerzt wird: nach einem Neuaufbau
    //  stoesst `finishFill` die Zaehlung daraus erneut an (die Kachel fragt
    //  kein zweites Mal, s. dort).
    m_countWanted.insert(folderPath);
    if (m_folderCounts.contains(folderPath)) return;   // schon gezaehlt
    if (m_countPending.contains(folderPath)) return;   // laeuft gerade
    const int row = rowForPath(folderPath);
    const MediaItem* it = itemAt(row);
    if (!it || !it->isFolder()) return;

    if (!m_countPool) {
        //  EIN Thread: die Zaehlung ist reine Platten-Arbeit, mehrere Threads
        //  wuerden nur den Kopf der Platte hin- und herschicken.
        m_countPool = new QThreadPool(this);
        m_countPool->setMaxThreadCount(1);
    }
    m_countPending.insert(folderPath);
    const QString sidecar = mg::folderSidecarName(folderPath);
    m_countPool->start(new FolderCountTask(QPointer<MediaModel>(this), folderPath,
                                           sidecar, m_showAllFiles, m_countGeneration,
                                           m_countTicket.value(folderPath)));
}

//  Ein Vorgang hat den Inhalt dieses Ordners geaendert (Datei hinein, heraus,
//  geloescht). Ein Reload laeuft dabei NICHT immer - eine Verschiebung zwischen
//  zwei aufgeklappten Unterordnern laesst den offenen Ordner unberuehrt, der
//  Watcher schweigt also. Ohne diesen Weg bliebe die Kachel auf ihrer alten
//  Zahl stehen.
void MediaModel::invalidateFolderCount(const QString& folderPath) {
    if (folderPath.isEmpty()) return;
    const bool known = m_folderCounts.contains(folderPath);
    const bool running = m_countPending.contains(folderPath);
    if (!known && !running) return;               // nie gezaehlt: nichts zu verwerfen
    m_folderCounts.remove(folderPath);
    //  Die Marke steigt: das Ergebnis eines noch laufenden Auftrags faellt
    //  durch, und der Ordner gilt nicht mehr als „laeuft gerade".
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
    //  ZUERST streichen: der Auftrag ist fertig, ganz gleich ob sein Ergebnis
    //  noch gilt. HAERTUNG, kein belegter Fehler - heute raeumt `rebuild()` die
    //  Vormerkungen ohnehin mit auf, und `invalidateFolderCount` tut es selbst.
    //  Stuende das Streichen aber weiter HINTER den Pruefungen, liesse jeder
    //  kuenftige Weg, der die Marke hebt ohne die Vormerkung zu raeumen, den
    //  Ordner fuer immer als „laeuft gerade" gelten - und `ensureFolderCount`
    //  kehrte danach bei jedem Versuch sofort um.
    m_countPending.remove(folderPath);
    //  Veraltet? Dann NICHT einfach aufgeben: der Wunsch besteht weiter, also
    //  gleich neu zaehlen, statt auf das naechste `finishFill` zu warten.
    //  Ist der Ordner inzwischen weg (`rowForPath < 0`), passiert nichts.
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

//  Momentaufnahme eines Ordners ins Sitzungs-Gedaechtnis legen. Ein leerer
//  Zustand wird geloescht statt gespeichert - sonst wuechse die Tabelle mit
//  jedem durchquerten Ordner, ohne etwas zu tragen.
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
    //  Sortiert, damit eine Momentaufnahme reproduzierbar ist (QSet hat keine
    //  Reihenfolge) - der Rueckweg soll denselben Baum ergeben.
    list.sort();
    return list;
}

void MediaModel::setExpandedFolders(const QStringList& folderPaths) {
    const QSet<QString> want(folderPaths.begin(), folderPaths.end());
    if (want == m_expanded) return;
    m_expanded = want;

    //  Der aufgeklappte Teil wird komplett neu aufgebaut: welche der genannten
    //  Ordner es ueberhaupt (noch) gibt, weiss erst das Einlesen.
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
    //  EINMAL normalisieren, dann gilt ueberall derselbe Pfad: der Vergleich
    //  mit `m_folder`, die Praefix-Pruefungen der Zielordner und die
    //  Bereichstabelle. Ein „…/ordner/" und ein „…/ordner" waren sonst zwei
    //  verschiedene Ordner.
    const QString folderPath = mg::normalizedFolder(rawFolderPath);
    if (folderPath == m_folder && !m_items.isEmpty()) return;

    m_loader.cancelAll();

    const QStringList watched = m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);

    //  Aufklapp-Zustand des BISHERIGEN Ordners merken, den des neuen holen.
    //  Er beschreibt Ordner unterhalb eines bestimmten Ordners und ergibt
    //  woanders keinen Sinn - verlassen und zurueckkehren soll ihn aber
    //  wiederbringen (Rueckweg Alt+<-).
    rememberExpansion(m_folder);
    //  Die Merkliste der Zaehl-Wuensche gehoert zum verlassenen Ordner; im
    //  neuen fragen die Kacheln beim Erscheinen ohnehin selbst.
    m_countWanted.clear();
    m_folder = folderPath;
    //  Der Undo-Stapel gehört zum OFFENEN Ordner: eine Rücknahme in einen
    //  Ordner, den man gerade nicht sieht, wäre nicht nachvollziehbar.
    clearFileHistory();
    {
        //  Die Liste MUSS zuerst in eine eigene Variable: `QHash::value` liefert
        //  eine KOPIE, zwei Aufrufe also zwei verschiedene Temporaries -
        //  `begin()` des einen mit `end()` des anderen zu paaren, laeuft ins
        //  Leere (im Test reproduzierbar als Absturz).
        const QStringList remembered = m_expandedMemory.value(folderPath);
        const QSet<QString> restored(remembered.begin(), remembered.end());
        if (restored != m_expanded) {
            m_expanded = restored;
            emit expansionChanged();
        }
    }
    rebuild(folderPath);

    // Ordnerwechsel = große Freigabe (alte Item-Liste, Thumb-URLs, Sidecar-
    // Puffer des vorherigen Ordners) -> freigegebenen Heap aktiv ans OS
    // zurückgeben. Bewusst NICHT in reload() (gleicher Ordner, kleine Deltas).
    // Der zweite, wichtigere Trim sitzt in finishFill(): DORT ist die
    // inkrementelle Befüllung tatsächlich abgeschlossen - hier läuft erst die
    // erste Charge, der Rest folgt über den Timer.
    mg::trimHeap();

    if (!folderPath.isEmpty())
        m_watcher->addPath(folderPath);

    emit folderChanged();
}

void MediaModel::setShowAllFiles(bool v) {
    if (m_showAllFiles == v)
        return;
    m_showAllFiles = v;
    //  Der Ordner wird neu gelesen: die Sichtbarkeit entscheidet sich beim
    //  Einlesen, nicht beim Anzeigen - ein Filter im Proxy müsste die Regel ein
    //  zweites Mal kennen.
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

// ─── QAbstractListModel ──────────────────────────────────────────────────────
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
    //  Gespeichert wird UTC, angezeigt die Ortszeit. Die Umrechnung ist teuer
    //  (sie ruft `tzset`) - deshalb passiert sie HIER, also je sichtbarer
    //  Zeile, statt beim Einlesen je Datei. Gemessen: 1,54 -> 1,04 µs je Datei
    //  beim Einlesen, bei 77.958 Dateien rund 39 ms.
    //  Sortiert und verglichen wird auf dem Struct (`fieldLess`) - eine
    //  Zeitangabe ist absolut, die Zeitzone spielt dabei keine Rolle.
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
    case MediaType::Text:  return item.extension().toUpper();
    //  Nicht erkannte Typen (`.bak`, Archive, Programme) stehen nur bei „Alle
    //  Dateien anzeigen" in der Galerie - und waren dort an NICHTS zu erkennen:
    //  kein Thumbnail, kein Badge. Die Endung ist genau die Auskunft, die fehlt.
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

// ─── Thumbnails (sichtbarkeitsgesteuert) ─────────────────────────────────────
void MediaModel::refreshThumbnails() {
    if (m_items.isEmpty()) return;
    // In-flight-Ergebnisse der alten Zielgröße verwerfen (Generationswechsel).
    m_loader.cancelAll();
    m_thumbUrls.fill(QString());
    m_thumbState.fill(0);
    emit dataChanged(index(0), index(m_items.size() - 1),
                     { ThumbUrlRole, ThumbStateRole });
    // Sichtbare Delegates fordern daraufhin gezielt neu an (GalleryView).
    emit thumbnailsInvalidated();
}

void MediaModel::ensureThumbnail(const QString& filePath) {
    if (kLogThumbs)
        qInfo("[thumb] anfordern  %s (Zeile %d, Zustand %d)",
              qPrintable(QFileInfo(filePath).fileName()), rowForPath(filePath),
              rowForPath(filePath) >= 0 ? m_thumbState[rowForPath(filePath)] : -1);
    //  Wer anfordert, hebt eine vorgemerkte Abbestellung auf - auch wenn es
    //  eine ANDERE Kachel war, die eben noch abbestellt hat.
    m_cancelPending.remove(filePath);
    //  … und schuetzt sich gegen ein Abbestellen, das ERST NOCH kommt: die
    //  abgebende Kachel meldet sich nach der uebernehmenden (s. Header). Der
    //  Timer raeumt beide Mengen wieder ab; er laeuft ohnehin im selben
    //  Durchlauf, wird hier aber angestossen, falls gar nicht abbestellt wird.
    m_thumbWanted.insert(filePath);
    if (!m_cancelTimer.isActive())
        m_cancelTimer.start();
    const int row = rowForPath(filePath);
    if (row < 0) return;
    //  Ordnerkacheln zeichnen sich selbst (Regel 28) - der Loader kennt für sie
    //  keinen Erzeuger und liefe nur in seinen Fehlpfad.
    if (m_items.at(row).isFolder()) return;
    if (m_thumbState[row] == 1) return;          // bereits geliefert
    m_loader.requestThumbnail(filePath);          // Treffer/Miss klärt der Loader
}

void MediaModel::cancelThumbnail(const QString& filePath) {
    if (kLogThumbs)
        qInfo("[thumb] abbestellen %s", qPrintable(QFileInfo(filePath).fileName()));
    const int row = rowForPath(filePath);
    if (row >= 0 && m_thumbState[row] == 1) return;  // schon fertig -> nichts abbrechen
    //  NICHT sofort: eine andere Kachel kann denselben Pfad im selben Durchlauf
    //  uebernommen und schon angefordert haben (s. Header).
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

// ─── Mutationen ──────────────────────────────────────────────────────────────
//  ── Wer darf mutiert werden? ────────────────────────────────────────────────
//  Alles unten Folgende arbeitet mit `m_storage`/`m_tagManager` - und die
//  gehoeren dem GEOEFFNETEN Ordner und schluesseln nach DATEINAMEN. Eine Zeile
//  aus einem aufgeklappten Unterordner wuerde dort die Metadaten ihres
//  gleichnamigen Nachbarn treffen. Bis die Sidecar-Sammlung steht, bleiben
//  solche Zeilen unangetastet; Ordnerzeilen ebenso (ihre Vorgaenge kommen mit
//  den Ordner-Operationen).
bool MediaModel::isFileRow(int row) const {
    const MediaItem* it = itemAt(row);
    return it && !it->isFolder();
}

bool MediaModel::isRootFileRow(int row) const {
    const MediaItem* it = itemAt(row);
    return it && it->scope == 0 && !it->isFolder();
}

// ─── Sidecar-Sammlung ────────────────────────────────────────────────────────
JsonStorage* MediaModel::storageForScope(int scope) {
    if (scope <= 0) return &m_storage;
    if (scope >= m_scopes.size()) return &m_storage;
    const auto it = m_scopeStorage.constFind(scope);
    if (it != m_scopeStorage.constEnd()) return it.value();
    //  Lazy: erst beim ersten Zugriff lesen. `this` als Elternteil - beim Ende
    //  des Modells verschwinden die Instanzen mit.
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

//  ── Kategorien eines fremden Sidecars ueber NAMEN ──────────────────────────
//  Die IDs eines anderen Ordners sind andere; der Name ist das, was der Nutzer
//  sieht. Fehlt drueben eine Kategorie dieses Namens, entsteht sie auf der
//  HAUPTEBENE - den ganzen Baumpfad nachzubilden waere Raten.
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

//  ── Metadaten einer Zeile - aus dem Sidecar IHRES Ordners ──────────────────
void MediaModel::collectMetaAt(const QString& folder, const QString& fileName,
                               FileOp* op) const {
    if (folder == m_folder) { collectMeta(fileName, op); return; }
    auto* st = const_cast<MediaModel*>(this)->storageForFolder(folder);
    if (!st) return;
    op->tags          = st->getTags(fileName);
    op->categoryIds.clear();                       // IDs gelten nur im eigenen Baum
    op->categoryNames = categoryNamesOf(*st, fileName);
    //  Kein Datum: es hängt an der Datei und wandert mit ihr (Verschieben und
    //  Papierkorb erhalten die Zeitstempel).
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

    // Persistierte Metadaten (Tags/Datum) auf neuen Dateinamen umziehen - im
    // Sidecar DES ORDNERS, dem die Datei gehoert.
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

    // Thumbnail-Cache-Key hängt am Pfad -> neu anfordern.
    m_thumbUrls[row]  = QString();
    m_thumbState[row] = 0;
    emitRow(row, { FilePathRole, FileNameRole, DisplayNameRole, ThumbUrlRole, ThumbStateRole });
}

//  Datei in den Papierkorb + alle Metadaten sichern und entfernen. Füllt `op`
//  mit allem, was zum Zurückholen nötig ist. Kein Zeilen-/Modell-Anteil - den
//  macht der Aufrufer, damit Löschen und Wiederholen denselben Kern nutzen.
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
    //  VOR dem Löschen sichern - danach sind sie weg. Aus dem Sidecar DES
    //  Ordners, dem die Datei gehoert (Unterordner haben ihr eigenes).
    collectMetaAt(folder, name, op);

    // Watcher unterdrücken: das Löschen löst sonst einen kompletten
    // Ordner-Reload aus - die Zeile entfernt der Aufrufer gezielt selbst.
    ++m_suppressWatch;
    // Bevorzugt in den Papierkorb (reversibel); nur wenn das System keinen
    // bietet (moveToTrash schlägt fehl), endgültig löschen. Der Rückgabepfad
    // ist der ganze Rückweg - ohne ihn gäbe es kein Undo.
    QString inTrash;
    bool ok = QFile::moveToTrash(filePath, &inTrash);
    if (ok)
        op->trashPath = inTrash;
    else
        ok = QFile::remove(filePath);
    if (ok) {
        // PDF-Editor-Sidecar mit entsorgen (Overlay-Notizen der Datei).
        const QString sidecar = filePath + QStringLiteral(".mgedit.json");
        if (QFile::exists(sidecar)) {
            op->sidecarPath = sidecar;
            QString sidecarTrash;
            if (QFile::moveToTrash(sidecar, &sidecarTrash))
                op->sidecarTrashPath = sidecarTrash;
            else
                QFile::remove(sidecar);
        }
        //  … und die DOCX-Sicherungskopie. Sie gehoert zur Datei; blieb sie
        //  liegen, stand neben der geloeschten DOCX eine verwaiste `.bak`
        //  (vom Nutzer gemeldet). Ueber den Papierkorb, damit `Strg+Z` sie
        //  zusammen mit der Datei zurueckholt.
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

//  Gegenstück: aus dem Papierkorb zurück an seinen Platz, Metadaten wieder an.
bool MediaModel::restoreFile(const FileOp& op) {
    if (op.trashPath.isEmpty() || !QFile::exists(op.trashPath))
        return false;
    //  Steht dort inzwischen wieder etwas, wird NICHTS überschrieben.
    if (QFileInfo::exists(op.path))
        return false;

    ++m_suppressWatch;
    bool ok = QFile::rename(op.trashPath, op.path);
    if (!ok) {
        //  Papierkorb auf einem anderen Dateisystem -> kopieren und aufräumen.
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

//  Zeile ans ENDE hängen - die Reihenfolge macht der Proxy (er sortiert), die
//  Datei erscheint also sofort an ihrem richtigen Platz.
void MediaModel::appendRowFor(const QString& filePath) {
    const QFileInfo fi(filePath);
    if (!fi.exists()) return;

    //  Versteckte Dateien nur, wenn der Nutzer sie sehen will. Dieser Weg
    //  UMGEHT den Verzeichnis-Leser (er haengt an einer einzelnen Datei), also
    //  muss der Filter hier noch einmal stehen - sonst stand nach einem
    //  Rueckgaengig ploetzlich eine `.gitignore` in der Galerie, obwohl beide
    //  Schalter aus waren (vom Nutzer gemeldet: „manchmal ist sie zu sehen").
    if (fi.isHidden() && !AppSettings::instance().showHiddenFiles()) return;

    //  Eine zurückgeholte BEGLEITdatei darf nur dann als Kachel erscheinen, wenn
    //  der Nutzer sie sehen will - sonst legte ein `Strg+Z` plötzlich eine
    //  `.mgedit.json` in die Galerie, die dort nie stand.
    //  In WELCHEN Bereich gehoert die Zeile? Eine zurueckgeholte Datei aus
    //  einem aufgeklappten Unterordner gehoert dorthin, nicht in den offenen
    //  Ordner. Ist ihr Ordner gerade nicht sichtbar, entsteht auch keine Kachel.
    const QString parent = fi.absolutePath();
    int scope = 0;
    if (parent != m_folder) {
        scope = m_scopeOfPath.value(parent, -1);
        if (scope <= 0 || scope >= m_scopes.size() || !m_scopes.at(scope).active)
            return;
    }

    //  Der Sidecar-Name kommt aus dem BEREICH, nicht mehr aus einem Feld der
    //  laufenden Befuellung: das war zu diesem Zeitpunkt laengst geleert, die
    //  Ordner-JSON also nicht als Begleitdatei erkennbar.
    if (!m_showAllFiles && mg::isCompanionFile(fi.fileName(), sidecarOfScope(scope)))
        return;

    MediaItem item;
    item.filePath    = fi.filePath();
    item.displayName = fi.completeBaseName();
    item.fileSize    = fi.size();
    item.type        = fi.isDir() ? MediaType::Folder
                                  : MediaItem::detectType(fi.filePath());
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

//  Zeile + Parallelvektoren entfernen; Pfad->Zeile-Hash neu aufbauen
//  (alle nachfolgenden Zeilenindizes verschieben sich).
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

//  ── Metadaten des OFFENEN Ordners: einsammeln, entfernen, zurückgeben ──────
void MediaModel::collectMeta(const QString& fileName, FileOp* op) const {
    op->tags          = m_tagManager.tagsForFile(fileName);
    op->categoryIds   = m_tagManager.categoryIdsForFile(fileName);
    //  Kein Datum - s. `collectMetaAt`.
}

void MediaModel::dropMeta(const QString& fileName, const FileOp& op) {
    // Persistierte Metadaten (Tags/Datum) aufräumen …
    m_storage.removeFile(fileName);
    // … und die Kategorien-Mitgliedschaften: sie liegen NICHT in den
    // Datei-Metadaten, sondern in den Kategorien selbst. Ohne diesen Schritt
    // bliebe der Name dort als Waise stehen und zählte weiter mit.
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

//  ── Sidecar eines FREMDEN Ordners ──────────────────────────────────────────
//  Eigene, kurzlebige Instanz: die laufende `JsonStorage` gehört dem offenen
//  Ordner (Modell, Tag-Panel und Filter hängen daran) und darf für einen
//  Transfer nicht umgeschaltet werden.
//
//  KATEGORIEN wandern über den NAMEN: gibt es drüben keine Kategorie dieses
//  Namens, entsteht sie auf der HAUPTEBENE. Den ganzen Baumpfad nachzubilden
//  wäre Rätselraten - der Name ist das, was der Nutzer sieht.
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
            //  Kennt der ZIELordner den Tag schon, behaelt er seine eigene Farbe.
            //  Sonst faerbte eine hereingeschobene Datei alle Dateien um, die
            //  drueben denselben Tag tragen - die Farbe ist eine Entscheidung
            //  des Zielordners, die Mitschrift soll nur eine Luecke fuellen.
            //  Vor `ensureTagRegistered` fragen: das traegt selbst eine
            //  Zufallsfarbe ein und machte den Tag sonst sofort „bekannt".
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

// ─── Rekursive Suche ─────────────────────────────────────────────────────────
namespace {
//  Aendert sich am Filter ueberhaupt etwas, das eine neue Suche rechtfertigt?
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
        //  Entprellt das Tippen: bei jedem Zeichen den Baum zu durchsuchen
        //  waere Verschwendung, und die Treffer sprangen im Takt der Tastatur.
        m_deepTimer.setInterval(250);
        connect(&m_deepTimer, &QTimer::timeout, this, &MediaModel::startDeepScan);
    }

    if (c.isEmpty()) {
        //  Filter weg -> zurueck auf den Stand VOR der Suche. Von Hand
        //  geoeffnete Ordner stehen darin und bleiben damit offen; was die
        //  Suche aufgeklappt hat, schliesst sich.
        m_deepTimer.stop();
        ++m_deepGeneration;
        if (m_deepCancel) m_deepCancel->store(true);
        if (m_deepActive) {
            m_deepActive = false;
            m_deepChain.clear();
            const QStringList back = m_deepSnapshot;
            m_deepSnapshot.clear();
            setExpandedFolders(back);
            //  Die Ordner, die waehrend der Suche ausgeblendet waren, muessen
            //  wieder auftauchen - der Proxy bewertet nur neu, was sich meldet.
            if (!m_items.isEmpty())
                emit dataChanged(index(0), index(m_items.size() - 1));
        }
        return;
    }

    if (!m_deepActive) {
        m_deepActive   = true;
        m_deepChain.clear();
        m_deepSnapshot = expandedFolders();     // Stand vor der Suche merken
        //  Bis der Lauf Ergebnisse bringt, gehoert noch kein Ordner zum
        //  Ergebnis - die Ansicht muss das sofort erfahren.
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
        //  EIN Thread: die Suche ist Platten-Arbeit (ein Sidecar je Ordner).
        m_deepPool = new QThreadPool(this);
        m_deepPool->setMaxThreadCount(1);
    }
    //  Einen noch laufenden Lauf abbrechen - er gehoert zu einem aelteren
    //  Suchtext und wuerde nur Arbeit verbrennen.
    if (m_deepCancel) m_deepCancel->store(true);
    m_deepCancel = std::make_shared<std::atomic<bool>>(false);
    ++m_deepGeneration;
    m_deepPool->start(new DeepScanTask(QPointer<MediaModel>(this), m_folder,
                                       m_deepCriteria, m_deepCategoryNames,
                                       m_showAllFiles, m_deepGeneration, m_deepCancel));
}

void MediaModel::noteDeepMatches(const QStringList& folders, int generation) {
    if (generation != m_deepGeneration || !m_deepActive) return;
    if (folders.isEmpty()) return;
    if (qEnvironmentVariableIntValue("MG_DEEPLOG") >= 1) m_deepFillTimer.start();

    //  Jeder Treffer-Ordner UND seine ganze Kette bis zum offenen Ordner -
    //  sonst waere der Treffer zwar aufgeklappt, aber nicht zu sehen.
    //  Diese Kette wird SEPARAT gehalten: sie entscheidet, welche Ordner im
    //  Ergebnis stehen duerfen. Ein von Hand geoeffneter Ordner ohne Treffer
    //  bleibt zwar aufgeklappt, gehoert aber nicht dazu.
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
    //  Auch ohne neue Zeilen muss die Sichtbarkeit der Ordner neu bewertet
    //  werden - `filterAcceptsRow` fragt `isOnDeepChain`.
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1));
}

// ─── Tags über den sichtbaren Baum ───────────────────────────────────────────
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
    //  Deterministisch: der offene Ordner bringt seine Reihenfolge mit, die
    //  Ergaenzungen aus Unterordnern haengen sich in fester Ordnung an - sonst
    //  spraengen die Chips der Filterleiste bei jedem Aufklappen umher.
    std::sort(out.begin() + m_storage.allTags().size(), out.end());
    return out;
}

QColor MediaModel::visibleTagColor(const QString& tag) const {
    //  NICHT ueber `JsonStorage::tagColor` - das liefert fuer einen unbekannten
    //  Tag eine gueltige Ersatzfarbe, und dann waere „kennt ihn niemand" nicht
    //  mehr von „kennt ihn, Farbe ist der Standard" zu unterscheiden. Gefragt
    //  wird deshalb die Registry selbst.
    const QHash<QString, QColor> rootColors = m_storage.tagColors();
    const auto own = rootColors.constFind(tag);
    if (own != rootColors.constEnd()) return own.value();

    //  Ueber den BEREICHS-INDEX laufen, nicht ueber `m_scopeStorage` - dessen
    //  QHash-Reihenfolge ist je Programmlauf anders ausgewuerfelt. Kennen zwei
    //  Unterordner denselben Tag mit verschiedenen Farben, bekam der Chip der
    //  Filterleiste sonst bei jedem Start eine andere Farbe (gemessen: in 12
    //  Laeufen sprang sie zwischen zwei Werten). Der Index folgt der Reihenfolge,
    //  in der die Ordner aufgeklappt wurden - also dem, was der Betrachter sieht.
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

// ─── Ordner-Operationen ──────────────────────────────────────────────────────
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

//  Ein NAME, kein Pfad: Trenner, `.` und `..` sind abzulehnen, sonst legte ein
//  Knopf in der Galerie Ordner an beliebiger Stelle an (dieselbe Regel wie in
//  FileBrowseModel::createFolder).
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
    //  Ein gleichnamiger Eintrag JEDER Art zaehlt als „gibt es schon".
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

    //  Der Aufklapp-Zustand haengt an PFADEN - mitsamt der Enkel umhaengen,
    //  sonst waere der ganze Unterbaum nach dem Umbenennen zu.
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
    //  KEIN Fallback auf rekursives Loeschen. Ein Ordner kann beliebig viel
    //  enthalten; ohne Papierkorb gibt es keinen Rueckweg, und ein
    //  unwiderrufliches `removeRecursively` waere hier die falsche Zusage.
    --m_suppressWatch;
    if (!ok) return false;

    //  Der Ordner ist weg - sein Aufklapp-Zustand (und der seiner Enkel) auch.
    const QString prefix = folderPath + QLatin1Char('/');
    QSet<QString> keep;
    for (const QString& p : std::as_const(m_expanded))
        if (p != folderPath && !p.startsWith(prefix)) keep.insert(p);
    m_expanded = keep;

    //  `reloadNow == false` kommt aus `deleteSelected`: dort werden mehrere
    //  Ordner nacheinander geloescht, und ein Neu-Einlesen JE Ordner haette
    //  den naechsten unauffindbar gemacht (der Aufbau laeuft gechunkt).
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

//  Freier Zielname im Zielordner („Name (2).ext", „ (3)" …).
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
    //  Derselbe Ordner: es gibt nichts zu tun (und ein „Ersetzen" würde die
    //  Datei mit sich selbst überschreiben).
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
    //  Aus dem Sidecar des Ordners, dem die Datei gehoert. Die Kategorien
    //  wandern über ihre NAMEN (die IDs des Zielordners sind andere) - vor dem
    //  Entfernen einsammeln.
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
    //  Auch beim KOPIEREN waechst der Zielordner.
    invalidateFolderCount(mg::normalizedFolder(destFolder));
    return 0;
}

// ─── Mehrfachauswahl ─────────────────────────────────────────────────────────
//  Wahrheitsquelle ist `m_selected` (ein Byte je Zeile, parallel zu `m_items`).
//  Gemeldet wird IMMER zweigleisig: `dataChanged(SelectedRole)` faerbt die
//  betroffenen Kacheln nach (das Zeilenmodell reicht es an die sichtbaren
//  Zeilen weiter), `selectionChanged` traegt die Anzahl an die Oberflaeche.

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
    //  In ZUSAMMENHAENGENDEN Laeufen melden statt Zeile fuer Zeile: eine
    //  geleerte Auswahl ueber 40 Kacheln waeren sonst 40 Signale, und jedes
    //  liesse das Zeilenmodell seine Kacheldaten neu bauen.
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
    //  Ueber die PFADE und nicht ueber die Zeilen: `setTagOnRow` schreibt in das
    //  Sidecar des Ordners, dem die Zeile gehoert, und kann dabei melden - die
    //  Zeilennummern bleiben aber stabil, es wird nichts eingefuegt oder
    //  entfernt. Der Umweg ueber die Pfadliste haelt es trotzdem robust.
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
    //  Ein Durchlauf ueber alle Zeilen mit einem Zeiger in die Sollmenge:
    //  gemeldet wird nur, was sich wirklich aendert. Genau darauf beruht der
    //  Auswahlrahmen - er ruft das je Mausbewegung, und ohne den Vergleich
    //  baute das Zeilenmodell dabei jedes Mal alle sichtbaren Kacheln neu.
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
    //  EINE Meldung ueber den gesamten beruehrten Bereich statt einer je
    //  zusammenhaengendem Lauf: der Rahmen aendert in ANSICHTS-Ordnung, und die
    //  faellt nicht mit der Modellordnung zusammen - es entstuenden also viele
    //  kleine Laeufe. (Gemessen hat das allein NICHTS gebracht, 1150 -> 1145 µs;
    //  die Kosten lagen woanders, s. `GalleryRowModel::onSourceDataChanged`.
    //  Es bleibt, weil es einfacher ist, nicht weil es schneller waere.)
    if (lo >= 0)
        emitRows(lo, hi, { SelectedRole });
    m_selCount = n;
    //  Gemeldet wird, sobald sich IRGENDETWAS geaendert hat - nicht erst bei
    //  anderer Anzahl: ein wandernder Auswahlrahmen kann gleich viele, aber
    //  andere Kacheln treffen, und die Kacheln haengen an der Meldung.
    if (lo >= 0)
        noteSelectionChanged();
}

int MediaModel::deleteSelected() {
    //  Erst die Dateien, dann die Ordner - und beides unter EINER
    //  Gruppennummer, damit `Strg+Z` alles in einem Schritt zurueckholt.
    //  Die Reihenfolge ist kein Geschmack: `deleteFolder` liest den Ordner neu
    //  ein, und danach faende keine Datei mehr ihre Zeile.
    QStringList files, folders;
    for (int r = 0; r < m_selected.size() && r < m_items.size(); ++r) {
        if (!m_selected.at(r)) continue;
        if (m_items.at(r).isFolder()) folders.append(m_items.at(r).filePath);
        else                          files.append(m_items.at(r).filePath);
    }
    if (files.isEmpty() && folders.isEmpty()) return 0;

    const int group = (files.size() + folders.size() > 1) ? ++m_opGroup : 0;
    int done = 0;
    //  Die Gruppennummer wird dem Vorgang NACHTRAEGLICH aufgedrueckt: er landet
    //  im jeweiligen Loesch-Weg selbst auf dem Stapel, und ein Loeschen ohne
    //  Papierkorb kommt gar nicht erst darauf (dann steht nichts nachzutragen).
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

    //  Nur was im Papierkorb liegt, ist zurückholbar. Ohne Papierkorb (Fallback
    //  „endgültig") kommt der Vorgang gar nicht erst auf den Stapel - ein Undo,
    //  das nichts finden kann, wäre ein Versprechen ohne Deckung.
    if (!op.trashPath.isEmpty()) {
        pushUndo(op);
    } else if (!m_undoOps.isEmpty() || !m_redoOps.isEmpty()) {
        clearFileHistory();
    }
    return true;
}

//  Eine Verschiebung zurücknehmen: Datei zurück, Metadaten drüben weg und hier
//  wieder an. Scheitert der Rückweg (Ziel gelöscht, Platz wieder belegt), bleibt
//  alles, wie es ist.
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

//  ── Begleitdateien einer Datei ───────────────────────────────────────────────
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

    //  Watcher unterdrücken: die Begleitdatei taucht in der Galerie gar nicht
    //  auf (außer bei „Alle Dateien anzeigen"), ein Reload wäre nur Unruhe.
    ++m_suppressWatch;
    QString inTrash;
    bool ok = QFile::moveToTrash(cp, &inTrash);
    if (ok) op.trashPath = inTrash;
    else    ok = QFile::remove(cp);
    if (m_suppressWatch > 0) --m_suppressWatch;
    if (!ok)
        return false;

    //  Nur was im Papierkorb liegt, ist zurückholbar (wie beim Löschen einer
    //  Datei): ohne Papierkorb kommt der Vorgang nicht auf den Stapel.
    if (!op.trashPath.isEmpty())
        pushUndo(op);
    else if (!m_undoOps.isEmpty() || !m_redoOps.isEmpty())
        clearFileHistory();

    //  Ist die Begleitdatei sichtbar (Schalter an), verschwindet ihre Kachel.
    if (rowForPath(cp) >= 0)
        dropRowFor(cp);
    return true;
}

//  Einen Ordner aus dem Papierkorb zurueckholen. Anders als bei einer Datei
//  gibt es keine Metadaten einzusammeln: die liegen im Sidecar IM Ordner und
//  sind mitgewandert.
bool MediaModel::restoreFolder(const FileOp& op, bool reloadNow) {
    if (op.trashPath.isEmpty() || !QFileInfo::exists(op.trashPath)) return false;
    if (QFileInfo::exists(op.path)) return false;      // Platz wieder belegt

    ++m_suppressWatch;
    const bool ok = QDir().rename(op.trashPath, op.path);
    --m_suppressWatch;
    //  `reloadNow == false` kommt aus dem Gruppen-Rueckweg: dort werden erst
    //  alle Dateien als Zeilen zurueckgelegt und danach EINMAL neu eingelesen.
    if (ok && reloadNow) reload();
    return ok;
}

bool MediaModel::undoFileOp() {
    while (!m_undoOps.isEmpty()) {
        //  Ein GRUPPEN-Vorgang (geloeschte Mehrfachauswahl) geht als EIN Schritt
        //  zurueck: alle Eintraege mit derselben Gruppennummer liegen im Stapel
        //  unmittelbar nebeneinander und werden zusammen abgehoben.
        const int grp = m_undoOps.last().group;
        QVector<FileOp> batch;
        if (grp == 0) {
            batch.append(m_undoOps.takeLast());
        } else {
            while (!m_undoOps.isEmpty() && m_undoOps.last().group == grp)
                batch.append(m_undoOps.takeLast());
        }

        //  Innerhalb einer Gruppe kommen die DATEIEN zuerst zurueck und die
        //  Ordner zuletzt: ein Ordner-Rueckweg liest den Ordner neu ein, und
        //  ein `appendRowFor` waehrend eines laufenden Neuaufbaus legte die
        //  Zeile ein zweites Mal an.
        int  back      = 0;
        bool hadFolder = false;
        for (int pass = 0; pass < 2; ++pass) {
            for (const FileOp& op : std::as_const(batch)) {
                const bool isFolder = (op.kind == FileOp::Kind::Folder);
                if (isFolder != (pass == 1)) continue;
                const bool ok = (op.kind == FileOp::Kind::Move) ? undoMove(op)
                              : isFolder ? restoreFolder(op, /*reloadNow=*/false)
                              : restoreFile(op);   // Companion nutzt denselben Rückweg
                //  Nicht zurückholbar (Papierkorb geleert, Platz wieder belegt) -
                //  Eintrag verwerfen und den nächsten versuchen, statt hängen zu
                //  bleiben.
                if (!ok) continue;
                //  Eine Datei kommt gezielt als Zeile zurueck (kein Reload,
                //  keine neuen Miniaturen); ein Ordner braucht das Neu-Einlesen.
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
        //  Gegenstueck zu `undoFileOp`: eine Gruppe geht auch VORWAERTS als EIN
        //  Schritt. Die Eintraege werden einzeln abgearbeitet, aber ohne
        //  Rueckkehr zwischendurch - sonst braeuchte es N-mal Strg+Y.
        const int grp = m_redoOps.last().group;
        if (grp != 0) {
            //  Die ganze Gruppe ZUERST abheben, dann arbeiten: `trashFolderAt`
            //  legt seinen Vorgang selbst ab und LEERT dabei den Redo-Zweig
            //  (`pushUndo`) - haette man mitten in der Schleife noch Eintraege
            //  darin liegen, waeren sie weg gewesen.
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
                //  `deleteFolder` legt den Vorgang selbst auf den Undo-Stapel
                //  und leert dabei den Redo-Zweig - der Eintrag ist damit
                //  bereits verbucht.
                emit fileHistoryChanged();
                return true;
            }
            emit fileHistoryChanged();
            continue;
        }
        if (op.kind == FileOp::Kind::Move) {
            //  Erneut verschieben - über denselben Weg wie beim ersten Mal,
            //  damit auch die Metadaten wieder mitwandern.
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

//  Wie viele Dateien haengen am naechsten Schritt? Bei einem Einzelvorgang 1,
//  bei einer Gruppe deren Laenge - die Oberflaeche haengt daran ihr
//  „und N weitere".
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

//  Tag setzen/entfernen im Sidecar DES ORDNERS, dem die Datei gehoert.
//  Fuer den geoeffneten Ordner laeuft das ueber den TagManager (nur so erfahren
//  Seitenleiste und Filter davon); ein aufgeklappter Unterordner hat keinen
//  eigenen TagManager und wird direkt bedient.
//
//  DIE DEFINITION WANDERT MIT: Name und Farbe des Tags werden im Sidecar des
//  Unterordners eingetragen. Sonst stuende dort ein Tag ohne Farbe, sobald man
//  den Unterordner spaeter direkt oeffnet - die Zuordnung waere da, ihre
//  Bedeutung nicht.
void MediaModel::setTagOnRow(int row, const QString& tag, bool on) {
    MediaItem& it = m_items[row];
    const QString name = it.fileName();

    //  Die eigene Änderung darf den Ordner-Watcher nicht in ein reload() treiben
    //  (das würde die Auswahl und die Miniaturen wegwerfen).
    ++m_suppressWatch;
    if (it.scope == 0) {
        if (on) m_tagManager.addTagToFile(name, tag);
        else    m_tagManager.removeTagFromFile(name, tag);
        it.tags = m_tagManager.tagsForFile(name);
    } else if (JsonStorage* st = storageForScope(it.scope)) {
        QStringList tags = st->getTags(name);
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

// ─── Datei-Metadaten über den Pfad (Sidecar DES eigenen Ordners) ─────────────
//  Das Sidecar einer Zeile, oder nullptr wenn die Datei nicht zur Ansicht
//  gehoert. `row` liefert nebenbei die Zeile fuer das Auffrischen der Anzeige.
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

//  „Gibt es hier etwas zurückzusetzen?" - genau das fragt die Oberfläche.
//  Beantwortet wird es aus der DATEI: weicht ihr Änderungsdatum vom
//  Erstellungsdatum ab, führt „Zurücksetzen" zu etwas. Kein Merker nötig.
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

    //  ── Das Datum gehört an die DATEI - und NUR dorthin ────────────────────
    //  Kein Eintrag im Sidecar: Änderungs- und Erstellungsdatum stehen ohnehin
    //  im Dateisystem, und dieselbe Angabe zweimal zu führen hieße nur, dass
    //  sie auseinanderläuft, sobald jemand die Datei außerhalb der App anfasst
    //  (Festlegung des Nutzers 2026-08-21). „Zurücksetzen" braucht dafür auch
    //  keinen Merker mehr: es nimmt das ERSTELLUNGSdatum, das sich nie ändert -
    //  egal, wie oft hier ein neues gesetzt wird.
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
        //  Schreibgeschützt oder fremdes Dateisystem: dann passiert NICHTS -
        //  ein Datum, das nur die App kennt, wäre genau die Doppelablage, die
        //  hier verschwinden sollte.
        emit fileDateNotWritten(QFileInfo(filePath).fileName());
        return;
    }

    //  Zurücklesen: manche Dateisysteme (FAT) runden auf zwei Sekunden.
    //  Angezeigt wird, was WIRKLICH an der Datei steht.
    const QDateTime after = QFileInfo(filePath).lastModified(QTimeZone::UTC);
    m_items[row].dateTime = after.isValid() ? after : dt;
    emitRow(row, { DateTimeRole });
}

void MediaModel::clearCustomDate(const QString& filePath) {
    const int row = rowForPath(filePath);
    if (!isFileRow(row)) return;

    //  Zurück heißt: auf das ERSTELLUNGSdatum der Datei. Es ändert sich nie -
    //  man kann also beliebig oft ein neues Datum setzen und kommt immer an
    //  denselben Punkt zurück. Kennt das Dateisystem kein Erstellungsdatum
    //  (FAT, ältere ext4), wird nichts angefasst: raten wäre schlimmer als
    //  stehenlassen.
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

// ─── Watcher ─────────────────────────────────────────────────────────────────
void MediaModel::onDirectoryChanged() {
    if (m_suppressWatch > 0) return;     // interne Mutation, kein Reload
    if (m_folder.isEmpty()) return;
    reload();
    emit folderContentsChanged();
}
