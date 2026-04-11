#pragma once
#include <QObject>
#include <QString>

class ISettings;
class JsonStorage;

// FolderService coordinates folder-open operations:
//   - persisting the last-opened folder via ISettings
//   - loading project JSON data via JsonStorage
//   - preventing redundant reloads
//
// The UI calls openFolder(path); the service emits folderOpened when
// the folder has been loaded and is ready to display.
class FolderService : public QObject {
    Q_OBJECT
public:
    explicit FolderService(ISettings& settings, JsonStorage& storage,
                           QObject* parent = nullptr);

    // Returns the currently open folder path (empty if none).
    const QString& currentFolder() const { return m_currentFolder; }

    // Opens the folder at path.  No-op if path == currentFolder().
    // Saves current folder's JSON before switching.
    void openFolder(const QString& path);

    // Saves the current folder's JSON (if any is open).
    void saveCurrentFolder();

    // Restores the last folder from settings (call once after UI is ready).
    void restoreLastFolder();

signals:
    // Emitted after a new folder has been loaded and data applied.
    void folderOpened(const QString& path);

private:
    ISettings&   m_settings;
    JsonStorage& m_storage;
    QString      m_currentFolder;
};
