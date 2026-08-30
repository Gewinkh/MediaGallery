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
    void openFolder(const QString& rawPath);

    // Saves the current folder's JSON (if any is open).
    void saveCurrentFolder();

    //  Schreibt dieser Dienst den Startordner (`lastFolder`) mit?
    //
    //  JEDE Haelfte hat ihren eigenen FolderService, aber es gibt nur EINEN
    //  Schluessel dafuer. Schrieben alle hinein, ueberschriebe die zuletzt
    //  benutzte Haelfte den Ordner der ersten - und beim naechsten Start
    //  standen BEIDE auf demselben Ordner (vom Nutzer gemeldet). Deshalb
    //  schreibt nur die erste Haelfte; der Ordner der zweiten gehoert zum
    //  Fensterzustand (`ISettings::secondFolder`).
    void setPersistsLastFolder(bool on) { m_persistsLast = on; }
    bool persistsLastFolder() const { return m_persistsLast; }

    // Restores the last folder from settings (call once after UI is ready).
    void restoreLastFolder();

signals:
    // Emitted after a new folder has been loaded and data applied.
    void folderOpened(const QString& path);

private:
    ISettings&   m_settings;
    JsonStorage& m_storage;
    bool         m_persistsLast = true;   // s. setPersistsLastFolder
    QString      m_currentFolder;
};
