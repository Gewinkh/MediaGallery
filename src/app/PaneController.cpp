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
    //  Startwert des Optionen-Modus aus den Einstellungen (s. Q_PROPERTY).
    m_optionsVisible = settings.optionsVisible();

    m_proxy.setSourceModel(&m_media);
    m_proxy.setTagManager(&m_tags);

    //  Ordner öffnen -> Modell lädt ihn.
    connect(&m_folders, &FolderService::folderOpened, &m_media, &MediaModel::loadFolder);
    connect(&m_folders, &FolderService::folderOpened, this, &PaneController::folderOpened);
    connect(&m_folders, &FolderService::folderOpened, this, &PaneController::folderChanged);

    //  Rekursive Suche: ändert sich der Filter, durchsucht das Modell den Baum
    //  unterhalb des offenen Ordners. Hier verdrahtet, damit weder Proxy noch
    //  Modell einander kennen müssen.
    connect(&m_proxy, &MediaProxyModel::filterChanged, &m_media, [this]() {
        m_media.applyDeepFilter(m_proxy.criteria(), m_proxy.activeCategoryNames());
    });

    //  Tag-/Kategorie-Änderungen dieser Hälfte weiterreichen.
    connect(&m_tags, &TagManager::tagsChanged,       this, &PaneController::tagsChanged);
    connect(&m_tags, &TagManager::categoriesChanged, this, &PaneController::categoriesChanged);

    //  „Alle Dateien anzeigen": der Schalter lebt in den Einstellungen, die
    //  Regel im Modell - beim Umschalten liest es den Ordner neu.
    m_media.setShowAllFiles(m_settings.showAllFiles());
}

PaneController::~PaneController() = default;

QString PaneController::currentFolder() const { return m_folders.currentFolder(); }

// ── Ordner öffnen ────────────────────────────────────────────────────────────
void PaneController::openFolderUrl(const QUrl& url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    openFolder(path);
}

void PaneController::openFolder(const QString& path) {
    if (path.isEmpty()) return;
    //  Ein AUSGEWÄHLTER Ordner ist kein Abstieg - der Rückweg bezieht sich auf
    //  den vorherigen Baum und ist damit hinfällig.
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
    emit folderContentsChanged();
    emit statusMessage(Strings::get(StringKey::MenuRefresh));
}

// ── Hinein und zurück ────────────────────────────────────────────────────────
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
    //  Einträge, die nicht mehr taugen (Ordner gelöscht, schon offen), werden
    //  verworfen statt den Rückweg zu blockieren - dieselbe Linie wie beim
    //  Datei-Undo in MediaModel.
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
    //  Nur die Hälfte, in der die Datei wirklich liegt, macht etwas. Mit zwei
    //  Galerien setzen beide denselben Ruf ab - die andere fällt hier heraus.
    if (mg::normalizedFolder(folder) != mg::normalizedFolder(m_folders.currentFolder()))
        return false;

    //  ERST neu einlesen: ohne Zeile im Modell hätte die neue Datei nichts, an
    //  dem ein Tag hängen könnte (`MediaModel::addTag` sucht die Zeile).
    m_media.reload();
    emit folderContentsChanged();
    if (!inheritTags) return true;

    for (const QString& tag : m_media.tagsOfFile(src))
        m_media.addTag(dst, tag);

    //  Kategorien führen DATEINAMEN, nicht Pfade (s. TagManager).
    const QString srcName = QFileInfo(src).fileName();
    const QString dstName = QFileInfo(dst).fileName();
    for (const QString& catId : m_tags.categoryIdsForFile(srcName))
        m_tags.addFileToCategory(catId, dstName);
    return true;
}

// ── Inhalt hat sich geändert ─────────────────────────────────────────────────
//  Mit zwei Hälften ist „der Ordner" nicht mehr eindeutig: eine Datei, die in
//  Hälfte A angelegt wird, geht Hälfte B nichts an, solange dort ein anderer
//  Ordner offen ist. Ohne Angabe gilt es für die eigene Hälfte.
void PaneController::notifyContentsChanged(const QString& folder) {
    if (!folder.isEmpty()
        && mg::normalizedFolder(folder) != mg::normalizedFolder(m_folders.currentFolder()))
        return;
    emit folderContentsChanged();
    m_media.reload();
}
