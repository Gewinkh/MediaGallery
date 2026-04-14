#pragma once
#include <QScrollArea>
#include <QWidget>
#include <QGridLayout>
#include <QVector>
#include <QTimer>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileSystemWatcher>
#include "MediaItem.h"
#include "MediaThumbnail.h"
#include "ThumbnailLoader.h"
#include "TagManager.h"
#include "FilterBar.h"
#include "AppSettings.h"

class GalleryView : public QScrollArea {
    Q_OBJECT
public:
    explicit GalleryView(TagManager* tagMgr, QWidget* parent = nullptr);
    ~GalleryView();

    void loadFolder(const QString& folderPath);
    void setItems(const QVector<MediaItem>& items);
    void applyFilter(FilterBar* filterBar);
    void setOptionsVisible(bool v);
    void setCovered(bool covered);
    bool isCovered() const { return m_covered; }
    void refresh();
    void retryFailedThumbnails();
    void reloadAllThumbnails();   // Force-reload every thumbnail (fixes black previews)

    int columns() const { return m_columns; }
    void setColumns(int c);

    QVector<MediaItem>& allItems() { return m_allItems; }
    const QVector<int>& visibleIndices() const { return m_visibleIndices; }

signals:
    void itemDoubleClicked(int globalIndex);
    void nameChanged(int globalIndex, const QString& name);
    void tagsModified(int globalIndex, const QStringList& tags);
    void folderDropped(const QString& folderPath);
    void mediaFilesDropped(const QStringList& filePaths);
    void statusMessage(const QString& msg);
    void folderChanged(); // emitted when files are added/removed by OS

public slots:
    // Group mode: highlight only items tagged with 'tag', allow right-click toggle
    void enterGroupMode(const QString& tag);
    void exitGroupMode();

    // Add-to-Tag mode: left-click toggles tag membership, bright border shows tagged items
    void enterAddToTagMode(const QString& tag);
    void exitAddToTagMode();

protected:
    void wheelEvent(QWheelEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;

private slots:
    void onThumbnailReady(int index, const QString& path, const QPixmap& pix);
    void onThumbnailFailed(int index, const QString& path);
    void onNameChanged(int index, const QString& name);
    void onTagsModified(int index, const QStringList& tags);
    void scheduleVisibilityUpdate();
    void updateVisibleThumbnails();
    void onDirectoryChanged(); // called (debounced) when folder contents change

private:
    TagManager* m_tagMgr;
    ThumbnailLoader* m_loader;

    QWidget* m_container;
    QGridLayout* m_grid;

    QVector<MediaItem> m_allItems;       // All loaded items
    QVector<int> m_visibleIndices;       // Currently filtered indices
    QVector<MediaThumbnail*> m_tiles;    // Grid tiles (reused)

    int m_columns = 1;
    bool m_optionsVisible = true;
    bool m_covered        = false;
    int m_selectedIndex = -1;
    QSize m_lastTileSize;

    // Group mode
    bool    m_groupMode    = false;
    QString m_groupModeTag;
    QWidget* m_groupModeBanner = nullptr;

    // Add-to-Tag mode
    bool    m_addToTagMode    = false;
    QString m_addToTagModeTag;
    QWidget* m_addToTagBanner = nullptr;

    QTimer* m_visibilityTimer;
    QTimer* m_resizeTimer = nullptr;
    QTimer* m_wheelTimer  = nullptr;   // debounce for Shift+Wheel column changes
    int     m_pendingColumns = -1;     // column target queued by wheel

    // Live folder watching
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer*             m_watchTimer = nullptr;
    QString             m_currentFolder;

    void rebuildGrid();
    void requestVisibleThumbnails();
    QSize tileSize() const;
    int indexInVisible(int globalIdx) const;

    // Map from globalIndex -> tile pointer (for thumbnail delivery)
    QHash<int, MediaThumbnail*> m_indexToTile;
    QSet<int> m_failedIndices;  // global indices where thumbnail load failed
};
