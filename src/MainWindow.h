#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QAction>
#include <QMenu>
#include <QStatusBar>
#include <QString>
#include <QDateTime>
#include "GalleryView.h"
#include "FullscreenView.h"
#include "FilterBar.h"
#include "JsonStorage.h"
#include "TagManager.h"
#include "FolderService.h"
#include "MetadataEditor.h"

class ISettings;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // ISettings, FolderService and JsonStorage are owned externally (by main); injected here.
    explicit MainWindow(ISettings& settings, FolderService& folderService,
                        JsonStorage& storage, QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void openFolder();
    void onFolderOpened(const QString& path);
    void refreshFolder();
    void toggleOptions();
    void showFullscreen(int globalIndex);
    void backFromFullscreen();
    void onNameChanged(int globalIndex, const QString& name);
    void onTagsModified(int globalIndex, const QStringList& tags);
    void onEditDateRequested(int globalIndex, bool focusDaySection = false);
    void onDeleteMediaRequested(int globalIndex);
    void applyFilter();
    void showSettings();
    void applyTheme();
    void retranslateUi();
    void onLanguageChanged(Language l);

private:
    ISettings&     m_settings;
    FolderService& m_folderService;

    // Non-owning pointer to the shared JsonStorage (lifetime managed by main())
    JsonStorage* m_storage;
    TagManager*  m_tagMgr;  // owned by MainWindow

    // UI
    QStackedWidget* m_stack;
    QWidget*        m_galleryPage;
    GalleryView*    m_galleryView;
    FilterBar*      m_filterBar;
    FullscreenView* m_fullscreenView;

    // State
    bool m_optionsVisible = true;

    QDateTime      m_lastEditedDateTime;
    bool           m_hasLastEditedDate = false;
    MetadataEditor* m_metaDialog       = nullptr;
    QStringList    m_lastEditedTags;
    bool           m_hasLastEditedTags = false;
    QStringList    m_lastEditedCategories;   // category IDs of last modified medium
    bool           m_hasLastEditedCategories = false;

    // Menu / actions
    QMenu*   m_fileMenu     = nullptr;
    QMenu*   m_viewMenu     = nullptr;
    QMenu*   m_settingsMenu = nullptr;
    QMenu*   m_langMenu     = nullptr;
    QMenu*   m_vidMenu      = nullptr;
    QAction* m_openAct          = nullptr;
    QAction* m_refreshAct       = nullptr;
    QAction* m_quitAct          = nullptr;
    QAction* m_toggleOptionsAct = nullptr;
    QAction* m_settingsAct      = nullptr;
    QAction* m_langDeAct        = nullptr;
    QAction* m_langEnAct        = nullptr;
    QAction* m_videoNativeAct   = nullptr;
    QAction* m_videoExternalAct = nullptr;

    void setupUi();
    void setupMenus();
    void setupGalleryPage();
    void saveCurrentState();
    void updateToggleAction();
};
