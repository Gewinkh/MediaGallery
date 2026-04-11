#include "FolderService.h"
#include "ISettings.h"
#include "JsonStorage.h"
#include <QDir>

FolderService::FolderService(ISettings& settings, JsonStorage& storage,
                              QObject* parent)
    : QObject(parent), m_settings(settings), m_storage(storage) {}

void FolderService::openFolder(const QString& path) {
    if (m_currentFolder == path) return;

    // Persist current folder's JSON before switching
    if (!m_currentFolder.isEmpty())
        m_storage.saveFolder(m_currentFolder);

    m_currentFolder = path;
    m_settings.setLastFolder(path);
    m_storage.loadFolder(path);

    emit folderOpened(path);
}

void FolderService::saveCurrentFolder() {
    if (!m_currentFolder.isEmpty())
        m_storage.saveFolder(m_currentFolder);
}

void FolderService::restoreLastFolder() {
    const QString last = m_settings.lastFolder();
    if (!last.isEmpty() && QDir(last).exists())
        openFolder(last);
}
