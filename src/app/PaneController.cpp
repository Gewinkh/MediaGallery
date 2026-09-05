#include "app/PaneController.h"

#include "core/PathUtils.h"
#include "core/Strings.h"
#include "media/ThumbnailLoader.h"

#include <QFileInfo>

//  Aufbau EINER Hälfte. Die Verdrahtung stand bis hierher in `main.cpp` und galt
//  dort für die eine, globale Galerie - sie gehört zur Hälfte und zieht deshalb
//  mit um.
PaneController::PaneController(ISettings& settings, ThumbnailLoader& loader,
                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_storage()
    , m_tags(&m_storage)
    , m_tagCtl(m_tags)
    , m_folders(settings, m_storage)
    , m_media(m_storage, m_tags, loader)
    , m_proxy()
{
    m_optionsVisible = settings.optionsVisible();

    m_proxy.setSourceModel(&m_media);
    m_proxy.setTagManager(&m_tags);

    connect(&m_folders, &FolderService::folderOpened, &m_media, &MediaModel::loadFolder);
    connect(&m_folders, &FolderService::folderOpened, this, &PaneController::folderOpened);
    connect(&m_folders, &FolderService::folderOpened, this, &PaneController::folderChanged);

    //  Rekursive Suche: ändert sich der Filter, durchsucht das Modell den Baum
    //  unterhalb des offenen Ordners. Hier verdrahtet, damit weder Proxy noch
    //  Modell einander kennen müssen.
    connect(&m_proxy, &MediaProxyModel::filterChanged, &m_media, [this]() {
        m_media.applyDeepFilter(m_proxy.criteria(), m_proxy.activeCategoryNames());
    });

    connect(&m_tags, &TagManager::tagsChanged,       this, &PaneController::tagsChanged);
    connect(&m_tags, &TagManager::categoriesChanged, this, &PaneController::categoriesChanged);

    //  Tag gelöscht -> auf Wunsch auch aus allen UNTERordnern. Die Entscheidung
    //  fällt hier, nicht im `TagManager`: der kennt weder die Einstellung noch
    //  den geöffneten Ordner. Standard ist AN (s. `ISettings`).
    connect(&m_tags, &TagManager::tagDeleted, this, [this](const QString& tag) {
        if (!m_settings.deleteTagsInSubfolders()) return;
        const QString folder = m_folders.currentFolder();
        if (folder.isEmpty()) return;
        m_tags.sweepSubfolders(folder, tag);
    });
    connect(&m_tags, &TagManager::subfolderSweepFinished, this,
            [this](const QString& tag, int count) {
        if (count <= 0) return;                 // nirgends vorgekommen - nichts zu melden
        m_media.dropScopeSidecars();
        emit statusMessage(Strings::get(StringKey::TagDeletedInSubfolders)
                               .arg(tag).arg(count));
    });

    connect(&m_folders, &FolderService::folderOpened, &m_tags, [this](const QString&) {
        m_tags.clearUndo();
    });
    connect(&m_tags, &TagManager::tagUndoApplied, this,
            [this](const QString& label, int subfolders, bool complete, bool redo) {
        if (subfolders > 0) m_media.dropScopeSidecars();
        if (!complete)
            emit statusMessage(Strings::get(StringKey::TagUndoPartial, label));
        else if (subfolders > 0)
            emit statusMessage(Strings::get(StringKey::TagUndoDoneSub)
                                   .arg(label).arg(subfolders));
        else
            emit statusMessage(Strings::get(redo ? StringKey::TagRedoDone
                                                 : StringKey::TagUndoDone, label));
    });

    m_media.setShowAllFiles(m_settings.showAllFiles());
}

PaneController::~PaneController() = default;

QString PaneController::currentFolder() const { return m_folders.currentFolder(); }

void PaneController::openFolderUrl(const QUrl& url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    openFolder(path);
}

void PaneController::openFolder(const QString& path) {
    if (path.isEmpty()) return;
    clearFolderHistory();
    m_folders.openFolder(path);
}

void PaneController::restoreLastFolder() {
    clearFolderHistory();
    m_folders.restoreLastFolder();
}

void PaneController::refreshCurrentFolder() {
    const QString folder = m_folders.currentFolder();
    if (folder.isEmpty()) return;
    m_storage.loadFolder(folder);
    // reload(), nicht loadFolder(): letzteres steigt beim selben Ordner sofort wieder aus,
    // ein Aktualisieren waere damit wirkungslos.
    m_media.reload();
    emit folderContentsChanged();
    emit statusMessage(Strings::get(StringKey::MenuRefresh));
}

void PaneController::openSubfolder(const QString& path) {
    if (path.isEmpty()) return;
    const QString current = m_folders.currentFolder();
    if (path == current) return;
    if (!QFileInfo(path).isDir()) return;

    if (!current.isEmpty()) {
        m_backStack.append(current);
        while (m_backStack.size() > kMaxFolderBack)
            m_backStack.removeFirst();
        emit folderHistoryChanged();
    }
    m_folders.openFolder(path);
}

bool PaneController::navigateBack() {
    while (!m_backStack.isEmpty()) {
        const QString prev = m_backStack.takeLast();
        if (prev.isEmpty() || prev == m_folders.currentFolder()
            || !QFileInfo(prev).isDir())
            continue;
        emit folderHistoryChanged();
        m_folders.openFolder(prev);
        return true;
    }
    emit folderHistoryChanged();
    return false;
}

void PaneController::clearFolderHistory() {
    if (m_backStack.isEmpty()) return;
    m_backStack.clear();
    emit folderHistoryChanged();
}

bool PaneController::adoptSiblingFile(const QString& sourcePath,
                                      const QString& newPath, bool inheritTags) {
    if (sourcePath.isEmpty() || newPath.isEmpty()) return false;
    const QString src = mg::toLocalPath(sourcePath);
    const QString dst = mg::toLocalPath(newPath);
    const QString folder = QFileInfo(dst).absolutePath();
    if (mg::normalizedFolder(folder) != mg::normalizedFolder(m_folders.currentFolder()))
        return false;

    m_media.reload();
    emit folderContentsChanged();
    if (!inheritTags) return true;

    for (const QString& tag : m_media.tagsOfFile(src))
        m_media.addTag(dst, tag);

    const QString srcName = QFileInfo(src).fileName();
    const QString dstName = QFileInfo(dst).fileName();
    for (const QString& catId : m_tags.categoryIdsForFile(srcName))
        m_tags.addFileToCategory(catId, dstName);
    return true;
}

// Mit zwei Hälften ist "der Ordner" nicht mehr eindeutig: eine Datei, die in Hälfte A entsteht, geht Hälfte B
// nichts an, solange dort ein anderer Ordner offen ist. Ohne Angabe gilt es für die eigene Hälfte.
void PaneController::notifyContentsChanged(const QString& folder) {
    if (!folder.isEmpty()
        && mg::normalizedFolder(folder) != mg::normalizedFolder(m_folders.currentFolder()))
        return;
    emit folderContentsChanged();
    m_media.reload();
}
