#include "MediaModel.h"
#include "JsonStorage.h"
#include "TagManager.h"
#include "ThumbnailLoader.h"

#include <QFileSystemWatcher>
#include <QDir>
#include <QFileInfo>
#include <QFile>

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
        if (m_pendingIndex >= m_pendingEntries.size()) {
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

// ─── Enumeration / inkrementelle Befüllung ───────────────────────────────────
void MediaModel::rebuild(const QString& folderPath) {
    // Laufende Befüllung abbrechen.
    m_fillTimer.stop();
    m_pendingEntries.clear();
    m_pendingIndex = 0;
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

    // entryInfoList sortiert nur über die Dateinamen; die teuren stat()-Aufrufe
    // (Größe/mtime) erfolgen erst beim Aufbau der MediaItems je Charge — also
    // verteilt statt in einem Block vorab.
    m_pendingEntries = QDir(folderPath).entryInfoList(QDir::Files, QDir::Name);

    const int upperBound = m_pendingEntries.size();
    m_items.reserve(upperBound);
    m_thumbUrls.reserve(upperBound);
    m_thumbState.reserve(upperBound);
    m_pathToRow.reserve(upperBound);

    // Erste Charge SYNCHRON → Viewport ist sofort gefüllt (kein Flackern),
    // der Rest folgt gechunkt über den Timer.
    feedChunk(/*firstChunk=*/true);
    if (m_pendingIndex < m_pendingEntries.size())
        m_fillTimer.start();
    else
        finishFill();
}

void MediaModel::feedChunk(bool firstChunk) {
    const int budget = firstChunk ? kFirstChunk : kChunk;

    QVector<MediaItem> batch;
    batch.reserve(budget);

    int produced = 0;
    while (m_pendingIndex < m_pendingEntries.size() && produced < budget) {
        const QFileInfo& fi = m_pendingEntries.at(m_pendingIndex++);
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
    m_pendingEntries.clear();
    m_pendingIndex = 0;
    m_pendingSidecar.clear();
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
    rebuild(folderPath);

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

// ─── Watcher ─────────────────────────────────────────────────────────────────
void MediaModel::onDirectoryChanged() {
    if (m_suppressWatch > 0) return;     // interne Mutation, kein Reload
    if (m_folder.isEmpty()) return;
    reload();
    emit folderContentsChanged();
}
