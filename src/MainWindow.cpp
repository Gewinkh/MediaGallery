#include "MainWindow.h"
#include "ISettings.h"
#include "SettingsDialog.h"
#include "Style.h"
#include "Strings.h"
#include <QVBoxLayout>
#include <QMenuBar>
#include <QFileDialog>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <utime.h>

// AppSettings is accessed only for signal connections (languageChanged, etc.)
// All reads/writes go through m_settings (ISettings&).
#include "AppSettings.h"

MainWindow::MainWindow(ISettings& settings, FolderService& folderService,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_folderService(folderService)
{
    m_storage = new JsonStorage(this);
    m_tagMgr  = new TagManager(m_storage, this);

    setupUi();
    setupMenus();
    applyTheme();

    resize(m_settings.windowSize());
    move(m_settings.windowPos());
    if (m_settings.windowMaximized()) showMaximized();

    m_optionsVisible = m_settings.optionsVisible();
    updateToggleAction();

    // React to folder-open events from FolderService
    connect(&m_folderService, &FolderService::folderOpened,
            this, &MainWindow::onFolderOpened);

    // Connect to AppSettings signals (theme / language) via the concrete type.
    // We cast because ISettings is a pure interface without QObject signals.
    auto* appSettings = static_cast<AppSettings*>(&m_settings);
    connect(appSettings, &AppSettings::languageChanged,
            this, &MainWindow::onLanguageChanged);
    connect(appSettings, &AppSettings::colorSchemeChanged,
            this, &MainWindow::applyTheme);
    connect(appSettings, &AppSettings::themeChanged,
            this, &MainWindow::applyTheme);

    setAcceptDrops(true);
    retranslateUi();

    // Restore last folder — triggers onFolderOpened via signal if a folder exists
    m_folderService.restoreLastFolder();
}

MainWindow::~MainWindow() {
    saveCurrentState();
}

void MainWindow::setupUi() {
    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);

    setupGalleryPage();

    m_fullscreenView = new FullscreenView(m_tagMgr, this);
    connect(m_fullscreenView, &FullscreenView::backRequested,
            this, &MainWindow::backFromFullscreen);
    connect(m_fullscreenView, &FullscreenView::nameChanged,
            this, &MainWindow::onNameChanged);
    connect(m_fullscreenView, &FullscreenView::tagsModified,
            this, &MainWindow::onTagsModified);
    connect(m_fullscreenView, &FullscreenView::editDateRequested,
            this, [this](int idx) { onEditDateRequested(idx, false); });
    connect(m_fullscreenView, &FullscreenView::editDateWithDayFocusRequested,
            this, [this](int idx) { onEditDateRequested(idx, true); });
    connect(m_fullscreenView, &FullscreenView::applyLastTagsRequested,
            this, [this](int globalIndex) {
        if (!m_hasLastEditedTags) return;
        auto& items = m_galleryView->allItems();
        if (globalIndex < 0 || globalIndex >= items.size()) return;
        m_storage->setTags(items[globalIndex].fileName(), m_lastEditedTags);
        m_folderService.saveCurrentFolder();
        m_filterBar->refreshTagList();
        applyFilter();
        m_fullscreenView->refreshTagBar();
    });

    m_stack->addWidget(m_galleryPage);
    m_stack->addWidget(m_fullscreenView);

    setStatusBar(new QStatusBar(this));
    statusBar()->setStyleSheet(
        "QStatusBar { background: rgba(8,14,18,0.9); color: #789891; "
        "border-top: 1px solid rgba(40,60,70,0.5); font-size: 11px; }");
}

void MainWindow::setupGalleryPage() {
    m_galleryPage = new QWidget(this);
    auto* lay = new QVBoxLayout(m_galleryPage);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_filterBar = new FilterBar(m_tagMgr, m_galleryPage);
    m_filterBar->setStyleSheet(
        "FilterBar { background: rgba(12,20,26,0.95); "
        "border-bottom: 1px solid rgba(40,60,70,0.6); }");
    connect(m_filterBar, &FilterBar::filterChanged, this, &MainWindow::applyFilter);
    connect(m_filterBar, &FilterBar::sortChanged,   this, &MainWindow::applyFilter);
    lay->addWidget(m_filterBar);

    m_galleryView = new GalleryView(m_tagMgr, m_galleryPage);
    connect(m_galleryView, &GalleryView::itemDoubleClicked,
            this, &MainWindow::showFullscreen);
    connect(m_galleryView, &GalleryView::nameChanged,
            this, &MainWindow::onNameChanged);
    connect(m_galleryView, &GalleryView::tagsModified,
            this, &MainWindow::onTagsModified);
    connect(m_galleryView, &GalleryView::folderDropped,
            this, [this](const QString& path) { m_folderService.openFolder(path); });
    connect(m_galleryView, &GalleryView::statusMessage,
            this, [this](const QString& msg) { statusBar()->showMessage(msg, 3000); });
    connect(m_galleryView, &GalleryView::folderChanged,
            this, [this]() {
        m_storage->applyToItems(m_galleryView->allItems());
        applyFilter();
        m_filterBar->refreshTagList();
        statusBar()->showMessage(tr("Ordner aktualisiert"), 2000);
    });
    connect(m_filterBar, &FilterBar::enterAddToTagModeRequested,
            m_galleryView, &GalleryView::enterAddToTagMode);
    lay->addWidget(m_galleryView, 1);
}

void MainWindow::setupMenus() {
    m_fileMenu = menuBar()->addMenu(QString());
    m_openAct = m_fileMenu->addAction(QString(), this, &MainWindow::openFolder);
    m_openAct->setShortcut(QKeySequence::Open);
    m_refreshAct = m_fileMenu->addAction(QString(), this, &MainWindow::refreshFolder);
    m_refreshAct->setShortcut(Qt::Key_R);
    m_fileMenu->addSeparator();
    m_quitAct = m_fileMenu->addAction(QString(), this, &QWidget::close);

    m_viewMenu = menuBar()->addMenu(QString());
    m_toggleOptionsAct = m_viewMenu->addAction(QString(), this, &MainWindow::toggleOptions);
    m_toggleOptionsAct->setShortcut(Qt::Key_S);
    m_toggleOptionsAct->setCheckable(true);
    m_toggleOptionsAct->setChecked(m_optionsVisible);

    m_settingsMenu = menuBar()->addMenu(QString());
    m_settingsAct  = m_settingsMenu->addAction(QString(), this, &MainWindow::showSettings);
    m_settingsMenu->addSeparator();

    m_langMenu   = m_settingsMenu->addMenu(QString());
    m_langDeAct  = m_langMenu->addAction("Deutsch", this, [this]() {
        m_settings.setLanguage(Language::German);
    });
    m_langDeAct->setCheckable(true);
    m_langDeAct->setChecked(m_settings.language() == Language::German);

    m_langEnAct  = m_langMenu->addAction("English", this, [this]() {
        m_settings.setLanguage(Language::English);
    });
    m_langEnAct->setCheckable(true);
    m_langEnAct->setChecked(m_settings.language() == Language::English);

    m_settingsMenu->addSeparator();
    m_vidMenu = m_settingsMenu->addMenu(QString());

    m_videoNativeAct = m_vidMenu->addAction(QString(), this, [this]() {
        m_settings.setVideoPlayback(VideoPlayback::Native);
        m_videoNativeAct->setChecked(true);
        m_videoExternalAct->setChecked(false);
    });
    m_videoNativeAct->setCheckable(true);
    m_videoNativeAct->setChecked(
        m_settings.videoPlayback() == VideoPlayback::Native);

    m_videoExternalAct = m_vidMenu->addAction(QString(), this, [this]() {
        m_settings.setVideoPlayback(VideoPlayback::External);
        m_videoNativeAct->setChecked(false);
        m_videoExternalAct->setChecked(true);
    });
    m_videoExternalAct->setCheckable(true);
    m_videoExternalAct->setChecked(
        m_settings.videoPlayback() == VideoPlayback::External);
}

// ─── Folder handling via FolderService ───────────────────────────────────────

void MainWindow::openFolder() {
    const QString& current = m_folderService.currentFolder();
    QString path = QFileDialog::getExistingDirectory(
        this, Strings::get(StringKey::MenuOpenFolder),
        current.isEmpty() ? QDir::homePath() : current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty())
        m_folderService.openFolder(path);
}

// Slot: called by FolderService::folderOpened after storage is loaded.
void MainWindow::onFolderOpened(const QString& path) {
    m_galleryView->loadFolder(path);
    m_storage->applyToItems(m_galleryView->allItems());
    m_galleryView->refresh();
    m_filterBar->refreshTagList();

    setWindowTitle(QString("MediaGallery — %1").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(
        Strings::get(StringKey::StatusFolderLoaded) + path, 4000);
}

void MainWindow::refreshFolder() {
    if (m_galleryView->isCovered())
        m_galleryView->setCovered(false);
    m_galleryView->reloadAllThumbnails();
}

void MainWindow::toggleOptions() {
    m_optionsVisible = !m_optionsVisible;
    m_settings.setOptionsVisible(m_optionsVisible);
    m_galleryView->setOptionsVisible(m_optionsVisible);
    m_filterBar->setVisible(m_optionsVisible);
    m_fullscreenView->setOptionsVisible(m_optionsVisible);
    updateToggleAction();
}

void MainWindow::updateToggleAction() {
    if (m_toggleOptionsAct)
        m_toggleOptionsAct->setChecked(m_optionsVisible);
}

void MainWindow::showFullscreen(int globalIndex) {
    m_fullscreenView->setItems(&m_galleryView->allItems(),
                               &m_galleryView->visibleIndices());
    m_fullscreenView->showItem(globalIndex);
    m_stack->setCurrentWidget(m_fullscreenView);
    m_fullscreenView->setFocus();
}

void MainWindow::backFromFullscreen() {
    m_stack->setCurrentWidget(m_galleryPage);
    m_folderService.saveCurrentFolder();
}

void MainWindow::onNameChanged(int globalIndex, const QString& name) {
    auto& items = m_galleryView->allItems();
    if (globalIndex < 0 || globalIndex >= items.size()) return;
    MediaItem& item = items[globalIndex];
    const QString oldFileName = item.fileName();
    const QString newFileName = name + "." + item.extension();
    const QString newPath = QFileInfo(item.filePath).dir().filePath(newFileName);

    if (QFile::rename(item.filePath, newPath)) {
        m_storage->renameFile(oldFileName, newFileName);
        item.filePath    = newPath;
        item.displayName = name;
        m_folderService.saveCurrentFolder();
        statusBar()->showMessage(
            Strings::get(StringKey::StatusRenamed) + newFileName, 3000);
    } else {
        item.displayName = name;
        m_folderService.saveCurrentFolder();
    }
}

void MainWindow::onTagsModified(int globalIndex, const QStringList& tags) {
    m_lastEditedTags    = tags;
    m_hasLastEditedTags = true;
    auto& items = m_galleryView->allItems();
    if (globalIndex < 0 || globalIndex >= items.size()) return;
    MediaItem& item = items[globalIndex];
    item.tags = tags;
    m_storage->setTags(item.fileName(), tags);
    m_folderService.saveCurrentFolder();
    m_filterBar->refreshTagList();
    applyFilter();
}

void MainWindow::onEditDateRequested(int globalIndex, bool focusDaySection) {
    if (m_metaDialog) {
        m_metaDialog->accept();
        return;
    }
    auto& items = m_galleryView->allItems();
    if (globalIndex < 0 || globalIndex >= items.size()) return;
    MediaItem& item = items[globalIndex];

    m_metaDialog = new MetadataEditor(
        item,
        m_hasLastEditedDate ? m_lastEditedDateTime : QDateTime(),
        this,
        focusDaySection);

    connect(m_metaDialog, &QDialog::finished, this,
            [this, globalIndex](int result) {
        if (!m_metaDialog) return;
        auto& items2 = m_galleryView->allItems();
        if (globalIndex >= 0 && globalIndex < items2.size()) {
            MediaItem& item2 = items2[globalIndex];
            if (result == QDialog::Accepted) {
                item2.dateTime      = m_metaDialog->selectedDateTime();
                item2.hasCustomDate = true;

                struct utimbuf times;
                time_t t = static_cast<time_t>(item2.dateTime.toSecsSinceEpoch());
                times.actime  = t;
                times.modtime = t;
                utime(item2.filePath.toLocal8Bit().constData(), &times);

                m_lastEditedDateTime = m_metaDialog->selectedDateTime();
                m_hasLastEditedDate  = true;
            } else if (result == 2) {
                item2.hasCustomDate = false;
                item2.dateTime      = QFileInfo(item2.filePath).lastModified();
            }
        }
        m_metaDialog->deleteLater();
        m_metaDialog = nullptr;
    });

    m_metaDialog->open();
}

void MainWindow::applyFilter() {
    m_galleryView->applyFilter(m_filterBar);
    m_fullscreenView->setItems(&m_galleryView->allItems(),
                               &m_galleryView->visibleIndices());
}

void MainWindow::showSettings() {
    auto* dlg = new SettingsDialog(m_tagMgr, this);
    connect(dlg, &SettingsDialog::settingsChanged, this, &MainWindow::applyTheme);
    connect(dlg, &SettingsDialog::settingsChanged, this, &MainWindow::retranslateUi);
    connect(dlg, &SettingsDialog::settingsChanged, this, [this]() {
        m_folderService.saveCurrentFolder();
        m_filterBar->refreshTagList();
    });
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::applyTheme() {
    ThemeColors theme = m_settings.currentTheme();
    qApp->setStyleSheet(Style::mainStyleSheet(theme.background, theme.accent));
}

void MainWindow::retranslateUi() {
    if (m_fileMenu)         m_fileMenu->setTitle(Strings::get(StringKey::MenuFile));
    if (m_viewMenu)         m_viewMenu->setTitle(Strings::get(StringKey::MenuView));
    if (m_settingsMenu)     m_settingsMenu->setTitle(Strings::get(StringKey::MenuSettings));
    if (m_openAct)          m_openAct->setText(Strings::get(StringKey::MenuOpenFolder));
    if (m_refreshAct)       m_refreshAct->setText(Strings::get(StringKey::MenuRefresh));
    if (m_quitAct)          m_quitAct->setText(Strings::get(StringKey::MenuQuit));
    if (m_toggleOptionsAct) m_toggleOptionsAct->setText(Strings::get(StringKey::MenuToggleOptions));
    if (m_settingsAct)      m_settingsAct->setText(Strings::get(StringKey::MenuSettingsItem));
    if (m_langMenu)         m_langMenu->setTitle(Strings::get(StringKey::MenuLanguage));
    if (m_vidMenu)          m_vidMenu->setTitle(Strings::get(StringKey::MenuVideoPlayback));
    if (m_videoNativeAct)   m_videoNativeAct->setText(Strings::get(StringKey::MenuVideoNative));
    if (m_videoExternalAct) m_videoExternalAct->setText(Strings::get(StringKey::MenuVideoExternal));

    m_filterBar->retranslate();
    m_fullscreenView->retranslate();
}

void MainWindow::onLanguageChanged(Language l) {
    retranslateUi();
    m_langDeAct->setChecked(l == Language::German);
    m_langEnAct->setChecked(l == Language::English);
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_S && m_stack->currentWidget() == m_galleryPage) {
        toggleOptions();
        return;
    }
    if (e->key() == Qt::Key_B && m_stack->currentWidget() == m_galleryPage) {
        if (m_galleryView->isCovered())
            refreshFolder();
        else
            m_galleryView->setCovered(true);
        return;
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    saveCurrentState();
    QMainWindow::closeEvent(e);
}

void MainWindow::saveCurrentState() {
    m_settings.setWindowSize(size());
    m_settings.setWindowPos(pos());
    m_settings.setWindowMaximized(isMaximized());
    m_settings.sync();
    m_folderService.saveCurrentFolder();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& url : e->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (QFileInfo(path).isDir()) {
            m_folderService.openFolder(path);
            break;
        }
    }
}
