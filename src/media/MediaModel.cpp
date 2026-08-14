#include "media/MediaModel.h"
#include "core/JsonStorage.h"
#include "tags/TagManager.h"
#include "media/ThumbnailLoader.h"

#include <QFileSystemWatcher>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QUuid>

#include "core/MemoryUtils.h"   // mg::trimHeap — RSS-Rückgabe nach Ordnerwechsel

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
    connect(&m_loader, &ThumbnailLoader::thumbnailReady,
            this, &MediaModel::onThumbnailReady, Qt::QueuedConnection);
    connect(&m_loader, &ThumbnailLoader::thumbnailFailed,
            this, &MediaModel::onThumbnailFailed, Qt::QueuedConnection);

    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this]() { m_watchDebounce.start(); });

    m_watchDebounce.setSingleShot(true);
    m_watchDebounce.setInterval(400);
    connect(&m_watchDebounce, &QTimer::timeout, this, &MediaModel::onDirectoryChanged);

    // ── Inkrementelle Befüllung: 0-ms-Timer speist je Tick eine Charge ein ───
    m_fillTimer.setSingleShot(false);
    m_fillTimer.setInterval(0);   // „sobald die Event-Loop atmet“ — kein Blockieren
    connect(&m_fillTimer, &QTimer::timeout, this, [this]() {
        feedChunk(/*firstChunk=*/false);
        if (!m_pendingIt || !m_pendingIt->hasNext()) {
            m_fillTimer.stop();
            finishFill();
        }
    });

    // Tag-Änderungen aus anderen Quellen (z. B. Tag-Manager) → sichtbare Tags neu.
    connect(&m_tagManager, &TagManager::tagsChanged, this, [this]() {
        if (m_items.isEmpty()) return;
        for (auto& it : m_items)
            it.tags = m_tagManager.tagsForFile(it.fileName());
        emit dataChanged(index(0), index(m_items.size() - 1), { TagsRole });
    });
}

//  s. Header: hier ist QDirIterator vollständig bekannt.
MediaModel::~MediaModel() = default;

// ─── Enumeration / inkrementelle Befüllung ───────────────────────────────────
void MediaModel::rebuild(const QString& folderPath) {
    // Laufende Befüllung abbrechen.
    m_fillTimer.stop();
    m_pendingIt.reset();
    m_pendingSidecar.clear();

    // Leeres Modell SOFORT publizieren → die UI rendert ohne Verzögerung den
    // Leerzustand bzw. beginnt unmittelbar mit der ersten Charge.
    beginResetModel();
    m_items.clear();
    m_thumbUrls.clear();
    m_thumbState.clear();
    m_pathToRow.clear();
    endResetModel();
    emit countChanged();

    if (folderPath.isEmpty())
        return;

    // Die Ordner-Konfiguration liegt als Sidecar "<Ordnername>.json" IM Ordner
    // (siehe JsonStorage) — diese Datei ist keine Mediendatei und wird nicht als
    // Kachel angezeigt.
    m_pendingSidecar = QFileInfo(folderPath).fileName() + QStringLiteral(".json");

    // ── Verzeichnis STREAMEND lesen (QDirIterator) statt vorab als Liste ─────
    //  Vorher materialisierte `QDir::entryInfoList` den GESAMTEN Ordner als
    //  QFileInfoList, bevor die erste Kachel sichtbar wurde: je Eintrag ein
    //  QFileInfoPrivate samt QFileSystemEntry und QFileSystemMetaData. Bei
    //  20 000 Dateien waren das ~20 MB, die bis zum Ende der Befüllung liegen
    //  blieben — und der Nutzer wartete auf die Enumeration ALLER Einträge,
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
    m_pendingIt = std::make_unique<QDirIterator>(folderPath, QDir::Files,
                                                 QDirIterator::NoIteratorFlags);

    // Erste Charge SYNCHRON → Viewport ist sofort gefüllt (kein Flackern),
    // der Rest folgt gechunkt über den Timer.
    feedChunk(/*firstChunk=*/true);
    if (m_pendingIt && m_pendingIt->hasNext())
        m_fillTimer.start();
    else
        finishFill();
}

void MediaModel::feedChunk(bool firstChunk) {
    const int budget = firstChunk ? kFirstChunk : kChunk;

    QVector<MediaItem> batch;
    batch.reserve(budget);

    int produced = 0;
    while (m_pendingIt && m_pendingIt->hasNext() && produced < budget) {
        m_pendingIt->next();
        const QFileInfo fi = m_pendingIt->fileInfo();
        if (fi.fileName() == m_pendingSidecar) continue;   // eigene Konfig überspringen

        const MediaType t = MediaItem::detectType(fi.filePath());
        if (t == MediaType::Unknown) continue;

        MediaItem item;
        item.filePath    = fi.filePath();
        item.displayName = fi.completeBaseName();
        item.fileSize    = fi.size();
        item.type        = t;
        item.dateTime    = fi.lastModified();
        batch.append(std::move(item));
        ++produced;
    }

    if (batch.isEmpty())
        return;   // diese Runde enthielt nur übersprungene Einträge

    // Persistierte Metadaten (Tags + ggf. Custom-Datum) für die Charge anwenden.
    m_storage.applyToItems(batch);
    for (auto& item : batch) {
        const QString name = item.fileName();
        if (m_storage.hasCustomDate(name)) {
            item.dateTime      = m_storage.getCustomDate(name);
            item.hasCustomDate = true;
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
    }
    endInsertRows();
    emit countChanged();
}

void MediaModel::finishFill() {
    m_pendingIt.reset();
    m_pendingSidecar.clear();

    //  Trim GENAU HIER: erst jetzt ist die inkrementelle Befuellung wirklich
    //  fertig. Der Trim in loadFolder() laeuft schon nach der ERSTEN Charge und
    //  kann den Speicher des vorherigen Ordners daher noch nicht vollstaendig
    //  zurueckgeben.
    //
    //  BEWUSST KEIN squeeze() auf den Parallel-Vektoren: gemessen (20 000
    //  Dateien) betraegt die Wachstums-Reserve nur ~93 kB von 9,2 MB Nutzdaten
    //  (unter 1 %) — Qts Wachstumsstrategie ist keine reine Verdopplung. Dafuer
    //  wuerde squeeze() den gesamten Item-Vektor umkopieren und damit die
    //  SPITZE kurzzeitig verdoppeln. Schlechter Tausch (§0-Prio 2 vor 4).
    mg::trimHeap();
}

int MediaModel::rowForPath(const QString& filePath) const {
    return m_pathToRow.value(filePath, -1);
}

void MediaModel::loadFolder(const QString& folderPath) {
    if (folderPath == m_folder && !m_items.isEmpty()) return;

    m_loader.cancelAll();

    const QStringList watched = m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);

    m_folder = folderPath;
    //  Der Undo-Stapel gehört zum OFFENEN Ordner: eine Rücknahme in einen
    //  Ordner, den man gerade nicht sieht, wäre nicht nachvollziehbar.
    clearFileHistory();
    rebuild(folderPath);

    // Ordnerwechsel = große Freigabe (alte Item-Liste, Thumb-URLs, Sidecar-
    // Puffer des vorherigen Ordners) → freigegebenen Heap aktiv ans OS
    // zurückgeben. Bewusst NICHT in reload() (gleicher Ordner, kleine Deltas).
    // Der zweite, wichtigere Trim sitzt in finishFill(): DORT ist die
    // inkrementelle Befüllung tatsächlich abgeschlossen — hier läuft erst die
    // erste Charge, der Rest folgt über den Timer.
    mg::trimHeap();

    if (!folderPath.isEmpty())
        m_watcher->addPath(folderPath);

    emit folderChanged();
}

void MediaModel::reload() {
    if (m_folder.isEmpty()) return;
    m_loader.cancelAll();
    rebuild(m_folder);
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
    case DateTimeRole:    return it.dateTime;
    case FileSizeRole:    return it.fileSize;
    case ThumbUrlRole:    return m_thumbUrls[r];
    case ThumbStateRole:  return m_thumbState[r];
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
    };
}

QString MediaModel::typeLabel(const MediaItem& item) {
    switch (item.type) {
    case MediaType::Video: return item.extension().toUpper();
    case MediaType::Audio: return item.audioFormatLabel();
    case MediaType::Pdf:   return QStringLiteral("PDF");
    case MediaType::Text:  return item.extension().toUpper();
    default:               return {};
    }
}

void MediaModel::emitRow(int row, const QVector<int>& roles) {
    if (row < 0 || row >= m_items.size()) return;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
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
    const int row = rowForPath(filePath);
    if (row < 0) return;
    if (m_thumbState[row] == 1) return;          // bereits geliefert
    m_loader.requestThumbnail(filePath);          // Treffer/Miss klärt der Loader
}

void MediaModel::cancelThumbnail(const QString& filePath) {
    const int row = rowForPath(filePath);
    if (row >= 0 && m_thumbState[row] == 1) return;  // schon fertig → nichts abbrechen
    m_loader.cancelThumbnail(filePath);
}

void MediaModel::onThumbnailReady(const QString& filePath, const QString& thumbUrl) {
    const int row = rowForPath(filePath);
    if (row < 0) return;
    m_thumbUrls[row]  = thumbUrl;
    m_thumbState[row] = 1;
    emitRow(row, { ThumbUrlRole, ThumbStateRole });
}

void MediaModel::onThumbnailFailed(const QString& filePath) {
    const int row = rowForPath(filePath);
    if (row < 0) return;
    m_thumbState[row] = 2;
    emitRow(row, { ThumbStateRole });
}

// ─── Mutationen ──────────────────────────────────────────────────────────────
void MediaModel::renameItem(const QString& filePath, const QString& newBaseName) {
    const int row = rowForPath(filePath);
    if (row < 0) return;

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

    // Persistierte Metadaten (Tags/Datum) auf neuen Dateinamen umziehen.
    m_storage.renameFile(oldName, newName);
    m_storage.saveCurrentFolder();
    --m_suppressWatch;

    MediaItem& it = m_items[row];
    m_pathToRow.remove(it.filePath);
    it.filePath    = newPath;
    it.displayName = QFileInfo(newPath).completeBaseName();
    m_pathToRow.insert(newPath, row);

    // Thumbnail-Cache-Key hängt am Pfad → neu anfordern.
    m_thumbUrls[row]  = QString();
    m_thumbState[row] = 0;
    emitRow(row, { FilePathRole, FileNameRole, DisplayNameRole, ThumbUrlRole, ThumbStateRole });
}

//  Datei in den Papierkorb + alle Metadaten sichern und entfernen. Füllt `op`
//  mit allem, was zum Zurückholen nötig ist. Kein Zeilen-/Modell-Anteil — den
//  macht der Aufrufer, damit Löschen und Wiederholen denselben Kern nutzen.
bool MediaModel::trashFile(const QString& filePath, FileOp* op) {
    const QString name = QFileInfo(filePath).fileName();

    op->kind = FileOp::Kind::Delete;
    op->path = filePath;
    op->trashPath.clear();
    op->sidecarPath.clear();
    op->sidecarTrashPath.clear();
    //  VOR dem Löschen sichern — danach sind sie weg.
    collectMeta(name, op);

    // Watcher unterdrücken: das Löschen löst sonst einen kompletten
    // Ordner-Reload aus — die Zeile entfernt der Aufrufer gezielt selbst.
    ++m_suppressWatch;
    // Bevorzugt in den Papierkorb (reversibel); nur wenn das System keinen
    // bietet (moveToTrash schlägt fehl), endgültig löschen. Der Rückgabepfad
    // ist der ganze Rückweg — ohne ihn gäbe es kein Undo.
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
        dropMeta(name, *op);
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
        //  Papierkorb auf einem anderen Dateisystem → kopieren und aufräumen.
        ok = QFile::copy(op.trashPath, op.path);
        if (ok) QFile::remove(op.trashPath);
    }
    if (ok) {
        if (!op.sidecarTrashPath.isEmpty() && QFile::exists(op.sidecarTrashPath)
            && !QFileInfo::exists(op.sidecarPath)) {
            if (!QFile::rename(op.sidecarTrashPath, op.sidecarPath))
                QFile::copy(op.sidecarTrashPath, op.sidecarPath);
        }
        restoreMeta(QFileInfo(op.path).fileName(), op);
    }
    --m_suppressWatch;
    return ok;
}

//  Zeile ans ENDE hängen — die Reihenfolge macht der Proxy (er sortiert), die
//  Datei erscheint also sofort an ihrem richtigen Platz.
void MediaModel::appendRowFor(const QString& filePath) {
    const QFileInfo fi(filePath);
    if (!fi.exists()) return;

    MediaItem item;
    item.filePath    = fi.filePath();
    item.displayName = fi.completeBaseName();
    item.fileSize    = fi.size();
    item.type        = MediaItem::detectType(fi.filePath());
    item.dateTime    = fi.lastModified();
    if (item.type == MediaType::Unknown) return;

    QVector<MediaItem> batch{ item };
    m_storage.applyToItems(batch);
    const QString name = batch.first().fileName();
    if (m_storage.hasCustomDate(name)) {
        batch.first().dateTime      = m_storage.getCustomDate(name);
        batch.first().hasCustomDate = true;
    }

    const int at = m_items.size();
    beginInsertRows(QModelIndex(), at, at);
    m_pathToRow.insert(batch.first().filePath, at);
    m_items.append(std::move(batch.first()));
    m_thumbUrls.append(QString());
    m_thumbState.append(0);
    endInsertRows();
    emit countChanged();
}

//  Zeile + Parallelvektoren entfernen; Pfad→Zeile-Hash neu aufbauen
//  (alle nachfolgenden Zeilenindizes verschieben sich).
bool MediaModel::dropRowFor(const QString& filePath) {
    const int row = rowForPath(filePath);
    if (row < 0) return false;
    beginRemoveRows(QModelIndex(), row, row);
    m_items.removeAt(row);
    m_thumbUrls.removeAt(row);
    m_thumbState.removeAt(row);
    m_pathToRow.clear();
    for (int i = 0; i < m_items.size(); ++i)
        m_pathToRow.insert(m_items.at(i).filePath, i);
    endRemoveRows();
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
    op->hasCustomDate = m_storage.hasCustomDate(fileName);
    if (op->hasCustomDate)
        op->customDate = m_storage.getCustomDate(fileName);
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
    if (op.hasCustomDate)
        m_storage.setCustomDate(fileName, op.customDate);
    m_storage.saveCurrentFolder();
}

//  ── Sidecar eines FREMDEN Ordners ──────────────────────────────────────────
//  Eigene, kurzlebige Instanz: die laufende `JsonStorage` gehört dem offenen
//  Ordner (Modell, Tag-Panel und Filter hängen daran) und darf für einen
//  Transfer nicht umgeschaltet werden.
//
//  KATEGORIEN wandern über den NAMEN: gibt es drüben keine Kategorie dieses
//  Namens, entsteht sie auf der HAUPTEBENE. Den ganzen Baumpfad nachzubilden
//  wäre Rätselraten — der Name ist das, was der Nutzer sieht.
void MediaModel::writeMetaToFolder(const QString& folder, const QString& fileName,
                                   const FileOp& op,
                                   const QHash<QString, QColor>& tagColors) {
    if (op.tags.isEmpty() && op.categoryIds.isEmpty() && !op.hasCustomDate)
        return;
    JsonStorage dest;
    dest.loadFolder(folder);
    if (!op.tags.isEmpty()) {
        QStringList tags = dest.getTags(fileName);
        for (const QString& t : op.tags) {
            if (!tags.contains(t)) tags.append(t);
            dest.ensureTagRegistered(t);
            const auto it = tagColors.constFind(t);
            if (it != tagColors.constEnd()) dest.setTagColor(t, it.value());
        }
        dest.setTags(fileName, tags);
    }
    if (op.hasCustomDate)
        dest.setCustomDate(fileName, op.customDate);
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

bool MediaModel::pushUndo(const FileOp& op) {
    m_undoOps.push_back(op);
    if (m_undoOps.size() > kMaxFileOps)
        m_undoOps.removeFirst();
    m_redoOps.clear();          // neue Tat → der Redo-Zweig ist überholt
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
    if (rowForPath(filePath) < 0)      return 2;      // fremde Datei
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
    collectMeta(name, &op);
    //  Die Kategorien wandern über ihre NAMEN (die IDs des Zielordners sind
    //  andere) — vor dem Entfernen einsammeln.
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
        dropMeta(name, op);
        writeMetaToFolder(destFolder, QFileInfo(target).fileName(), op,
                          m_storage.tagColors());
    }
    --m_suppressWatch;
    if (!ok) return 2;

    if (move) {
        dropRowFor(filePath);
        pushUndo(op);
    }
    return 0;
}

bool MediaModel::deleteItem(const QString& filePath) {
    if (rowForPath(filePath) < 0)
        return false;

    FileOp op;
    if (!trashFile(filePath, &op))
        return false;
    dropRowFor(filePath);

    //  Nur was im Papierkorb liegt, ist zurückholbar. Ohne Papierkorb (Fallback
    //  „endgültig") kommt der Vorgang gar nicht erst auf den Stapel — ein Undo,
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
        restoreMeta(QFileInfo(op.path).fileName(), op);
    }
    --m_suppressWatch;
    return ok;
}

bool MediaModel::undoFileOp() {
    while (!m_undoOps.isEmpty()) {
        const FileOp op = m_undoOps.takeLast();
        const bool back = (op.kind == FileOp::Kind::Move) ? undoMove(op) : restoreFile(op);
        if (!back) {
            //  Nicht zurückholbar (Papierkorb geleert, Platz wieder belegt) —
            //  Eintrag verwerfen und den nächsten versuchen, statt hängen zu
            //  bleiben.
            emit fileHistoryChanged();
            continue;
        }
        appendRowFor(op.path);
        m_redoOps.push_back(op);
        emit fileHistoryChanged();
        return true;
    }
    return false;
}

bool MediaModel::redoFileOp() {
    while (!m_redoOps.isEmpty()) {
        FileOp op = m_redoOps.takeLast();
        if (rowForPath(op.path) < 0) { emit fileHistoryChanged(); continue; }

        bool ok = false;
        if (op.kind == FileOp::Kind::Move) {
            //  Erneut verschieben — über denselben Weg wie beim ersten Mal,
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

void MediaModel::toggleTag(const QString& filePath, const QString& tag) {
    const int row = rowForPath(filePath);
    if (row < 0 || tag.isEmpty()) return;

    MediaItem& it = m_items[row];
    const QString name = it.fileName();

    ++m_suppressWatch;
    if (it.tags.contains(tag))
        m_tagManager.removeTagFromFile(name, tag);
    else
        m_tagManager.addTagToFile(name, tag);
    --m_suppressWatch;

    it.tags = m_tagManager.tagsForFile(name);
    emitRow(row, { TagsRole });
}

void MediaModel::addTag(const QString& filePath, const QString& tag) {
    const int row = rowForPath(filePath);
    if (row < 0 || tag.isEmpty()) return;

    MediaItem& it = m_items[row];
    if (it.tags.contains(tag)) return;          // schon dran → nichts zu tun

    //  Wie toggleTag: die eigene Änderung darf den Ordner-Watcher nicht in ein
    //  reload() treiben (das würde die Auswahl und die Miniaturen wegwerfen).
    ++m_suppressWatch;
    m_tagManager.addTagToFile(it.fileName(), tag);
    --m_suppressWatch;

    it.tags = m_tagManager.tagsForFile(it.fileName());
    emitRow(row, { TagsRole });
}

// ─── Watcher ─────────────────────────────────────────────────────────────────
void MediaModel::onDirectoryChanged() {
    if (m_suppressWatch > 0) return;     // interne Mutation, kein Reload
    if (m_folder.isEmpty()) return;
    reload();
    emit folderContentsChanged();
}
