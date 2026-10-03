#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "core/ISettings.h"
#include "core/JsonStorage.h"
#include "media/FolderService.h"
#include "media/MediaModel.h"
#include "media/MediaProxyModel.h"
#include "tags/TagController.h"
#include "tags/TagManager.h"

class ThumbnailLoader;

// Der Zustand EINER Galerie-Haelfte: alles, was an einen geoeffneten Ordner gebunden
// ist. Appweites (Theme, Einstellungen, Lesezeichen) bleibt im AppController, und der
// Miniatur-Lader bleibt einer fuer die ganze App - zwei wuerden den Speicher verdoppeln.
class PaneController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY folderChanged)
    //  Player-Modus DIESER Hälfte. Er steht hier und nicht im QML, weil die
    //  Shell ihre Hälften-Delegates neu baut, sobald eine dazukommt oder
    //  wegfällt - im QML wäre der Modus dabei jedes Mal verloren.
    Q_PROPERTY(bool playerMode READ playerMode WRITE setPlayerMode NOTIFY playerModeChanged)
    // Optionen-Modus DIESER Hälfte: er gehört zum Ordner, nicht zur App - mit zwei Galerien nebeneinander soll er
    // nur dort gelten, wo man ihn eingeschaltet hat.
    Q_PROPERTY(bool optionsVisible READ optionsVisible WRITE setOptionsVisible
               NOTIFY optionsVisibleChanged)
    //  Lag die große Player-Ansicht oben? Aus demselben Grund hier und nicht im
    //  QML: die Shell baut ihre Hälften-Delegates neu, sobald eine dazukommt.
    Q_PROPERTY(bool playerViewOpen READ playerViewOpen WRITE setPlayerViewOpen
               NOTIFY playerViewOpenChanged)
    Q_PROPERTY(bool canNavigateBack READ canNavigateBack NOTIFY folderHistoryChanged)
    //  "Oeffnen mit" einer Datei: sie geht ins Vollbild, sobald die Galerie sie kennt; danach wieder leer.
    Q_PROPERTY(QString pendingFullscreen READ pendingFullscreen WRITE setPendingFullscreen
               NOTIFY pendingFullscreenChanged)

    //  Die Modelle dieser Hälfte. Als `QObject*`, weil QML sie nur weiterreicht
    //  (die Galerie bindet daran); CONSTANT - sie leben so lange wie die Hälfte.
    Q_PROPERTY(QObject* galleryModel READ galleryModelObject CONSTANT)
    Q_PROPERTY(QObject* mediaModel   READ mediaModelObject   CONSTANT)
    Q_PROPERTY(QObject* tags         READ tagsObject         CONSTANT)

public:
    PaneController(ISettings& settings, ThumbnailLoader& loader,
                   QObject* parent = nullptr);
    ~PaneController() override;

    QString currentFolder() const;
    bool    canNavigateBack() const { return !m_backStack.isEmpty(); }

    bool optionsVisible() const { return m_optionsVisible; }
    //  Gilt je Haelfte, gemerkt wird der letzte Stand: neue Haelfte und naechster Start nehmen ihn.
    void setOptionsVisible(bool on);

    QString pendingFullscreen() const { return m_pendingFullscreen; }
    void setPendingFullscreen(const QString& p) {
        if (m_pendingFullscreen == p) return;
        m_pendingFullscreen = p;
        emit pendingFullscreenChanged();
    }

    bool playerViewOpen() const { return m_playerViewOpen; }
    void setPlayerViewOpen(bool on) {
        if (m_playerViewOpen == on) return;
        m_playerViewOpen = on;
        emit playerViewOpenChanged();
    }

    bool playerMode() const { return m_playerMode; }
    void setPlayerMode(bool on) {
        if (m_playerMode == on) return;
        m_playerMode = on;
        emit playerModeChanged();
    }

    QObject* galleryModelObject() { return &m_proxy; }
    QObject* mediaModelObject()   { return &m_media; }
    QObject* tagsObject()         { return &m_tagCtl; }

    JsonStorage&     storage()      { return m_storage; }
    TagManager&      tagManager()   { return m_tags; }
    FolderService&   folderService(){ return m_folders; }
    MediaModel&      mediaModel()   { return m_media; }
    MediaProxyModel& galleryModel() { return m_proxy; }

    Q_INVOKABLE void openFolderUrl(const QUrl& url);
    Q_INVOKABLE void openFolder(const QString& path);
    Q_INVOKABLE void restoreLastFolder();
    Q_INVOKABLE void refreshCurrentFolder();

    Q_INVOKABLE void openSubfolder(const QString& path);
    Q_INVOKABLE bool navigateBack();
    void clearFolderHistory();

    void notifyContentsChanged(const QString& folder = QString());

    //  Eine andere Haelfte hat den Sidecar DIESES Ordners geschrieben: von der
    //  Platte nachlesen. Die Galerie wird nicht neu gebaut - nur die Zeilen,
    //  deren Tags sich wirklich geaendert haben, melden sich.
    void uebernimmFremdenStand();

    // Eine NEBEN einer Quelldatei entstandene Datei aufnehmen: Ordner neu einlesen und auf Wunsch Tags und
    // Kategorien übernehmen. Gehört der Ordner nicht dieser Hälfte, passiert nichts - beide rufen blind.
    Q_INVOKABLE bool adoptSiblingFile(const QString& sourcePath,
                                      const QString& newPath,
                                      bool inheritTags);

signals:
    void playerModeChanged();
    void playerViewOpenChanged();
    void pendingFullscreenChanged();
    void optionsVisibleChanged();
    void folderChanged();
    void folderOpened(const QString& path);
    //  Die Haelfte hat ihren Rueckgaengig-Stapel abgelegt und einen leeren
    //  bekommen - der `AppController` teilt jetzt den richtigen zu.
    void undoStackDetached();
    void folderHistoryChanged();
    void folderContentsChanged();
    void statusMessage(const QString& text);
    void tagsChanged();
    void categoriesChanged();

private:
    bool m_playerMode = false;
    bool m_playerViewOpen = false;
    QString m_pendingFullscreen;
    bool m_optionsVisible = false;      // Startwert setzt der Konstruktor


    //  Rückweg für Alt+<- - nur Abstiege in Unterordner, gedeckelt. Ohne Deckel
    //  bliebe jeder Abstieg einer langen Sitzung liegen.
    static constexpr int kMaxFolderBack = 64;
    QStringList m_backStack;

    ISettings&       m_settings;
    JsonStorage      m_storage;
    TagManager       m_tags;
    TagController    m_tagCtl;
    FolderService    m_folders;
    MediaModel       m_media;
    MediaProxyModel  m_proxy;
};
