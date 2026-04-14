#include "GalleryView.h"
#include "Strings.h"
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QMimeData>
#include <QUrl>
#include <QScrollBar>
#include <QApplication>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>
#include <QSet>
#include <QPainter>
#include <QFont>
#include <QFileSystemWatcher>

static const int TILE_MIN_WIDTH = 120;
static const int TILE_BASE_HEIGHT_OPTIONS = 260;
static const int TILE_BASE_HEIGHT_PLAIN = 180;

GalleryView::GalleryView(TagManager* tagMgr, QWidget* parent)
    : QScrollArea(parent), m_tagMgr(tagMgr)
{
    m_loader = new ThumbnailLoader(this);

    m_container = new QWidget(this);
    m_container->setObjectName("galleryContainer");
    m_container->setStyleSheet("#galleryContainer { background: transparent; }");

    m_grid = new QGridLayout(m_container);
    m_grid->setContentsMargins(12, 12, 12, 12);
    m_grid->setSpacing(8);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    setWidget(m_container);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAcceptDrops(true);
    setFrameShape(QFrame::NoFrame);

    m_visibilityTimer = new QTimer(this);
    m_visibilityTimer->setSingleShot(true);
    m_visibilityTimer->setInterval(100);
    connect(m_visibilityTimer, &QTimer::timeout, this, &GalleryView::updateVisibleThumbnails);

    connect(m_loader, &ThumbnailLoader::thumbnailReady,
            this, &GalleryView::onThumbnailReady);
    connect(m_loader, &ThumbnailLoader::thumbnailFailed,
            this, &GalleryView::onThumbnailFailed);

    // File system watcher for live folder updates
    m_watcher = new QFileSystemWatcher(this);
    m_watchTimer = new QTimer(this);
    m_watchTimer->setSingleShot(true);
    m_watchTimer->setInterval(300); // debounce: wait 300ms after last change
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) { m_watchTimer->start(); });
    connect(m_watchTimer, &QTimer::timeout,
            this, &GalleryView::onDirectoryChanged);
}

GalleryView::~GalleryView() {
    m_loader->cancelAll();
}

void GalleryView::loadFolder(const QString& folderPath) {
    m_loader->cancelAll();
    m_allItems.clear();
    m_failedIndices.clear();

    // Update file system watcher
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    m_currentFolder = folderPath;
    if (!folderPath.isEmpty())
        m_watcher->addPath(folderPath);

    QDir dir(folderPath);
    QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);

    for (const QFileInfo& fi : entries) {
        MediaType t = MediaItem::detectType(fi.filePath());
        if (t == MediaType::Unknown) continue;

        MediaItem item;
        item.filePath    = fi.filePath();
        item.displayName = fi.completeBaseName();
        item.fileSize    = fi.size();
        item.type        = t;
        item.dateTime    = fi.lastModified();
        m_allItems.append(item);
    }

    emit statusMessage(QString(tr("%1 Dateien geladen")).arg(m_allItems.size()));
    refresh();
}

void GalleryView::setItems(const QVector<MediaItem>& items) {
    m_allItems = items;
    refresh();
}

void GalleryView::applyFilter(FilterBar* fb) {
    m_visibleIndices.clear();

    QStringList tagFilter = fb->activeTagFilter();
    TagFilterMode mode    = fb->tagFilterMode();

    bool showImg = fb->showImages();
    bool showVid = fb->showVideos();
    bool showAud = fb->showAudio();
    SortField sf = fb->sortField();
    SortOrder so = fb->sortOrder();

    bool hasTagFilter      = !tagFilter.isEmpty();
    bool hasCategoryFilter = fb->hasCategoryFilter();
    bool hasAnyFilter      = hasTagFilter || hasCategoryFilter;

    QSet<QString> workingSet(tagFilter.begin(), tagFilter.end());

    // Collect the set of active category IDs (and their descendants) so we can
    // check direct file membership (cat.files) in addition to tag membership.
    QSet<QString> activeCatIds;
    if (hasCategoryFilter) {
        const QStringList ids = fb->activeCategoryIds();
        activeCatIds = QSet<QString>(ids.begin(), ids.end());
    }

    for (int i = 0; i < m_allItems.size(); ++i) {
        const MediaItem& item = m_allItems[i];

        // Media type filter
        if (item.isImage() && !showImg) continue;
        if (item.isVideo() && !showVid) continue;
        if (item.isAudio() && !showAud) continue;

        // No filter active: show everything
        if (!hasAnyFilter) {
            m_visibleIndices.append(i);
            continue;
        }

        // Check direct file↔category membership first.
        // An item assigned directly to an active category always passes,
        // regardless of its tags — this is the main fix for the category filter.
        if (hasCategoryFilter) {
            QStringList itemCats = m_tagMgr->categoriesForFile(item.fileName());
            for (const QString& cid : itemCats) {
                if (activeCatIds.contains(cid)) {
                    m_visibleIndices.append(i);
                    goto nextItem;
                }
            }
        }

        // If a category filter is active but resolved to no tags (empty
        // category, no direct file membership above), skip this item.
        if (!hasTagFilter) continue;

        {
        bool passes = false;
        const QStringList& itemTags = item.tags;

        switch (mode) {
        case TagFilterMode::OR:
        case TagFilterMode::INKLUSIV:
            for (const auto& t : itemTags) {
                if (workingSet.contains(t)) { passes = true; break; }
            }
            break;

        case TagFilterMode::AND:
            if (itemTags.isEmpty()) { passes = false; break; }
            passes = true;
            for (const auto& t : tagFilter) {
                if (!itemTags.contains(t)) { passes = false; break; }
            }
            break;

        case TagFilterMode::NUR:
            if (itemTags.isEmpty()) { passes = false; break; }
            {
                bool hasOne = false;
                for (const auto& t : itemTags) {
                    if (workingSet.contains(t)) { hasOne = true; break; }
                }
                if (!hasOne) { passes = false; break; }
                passes = true;
                for (const auto& t : itemTags) {
                    if (!workingSet.contains(t)) { passes = false; break; }
                }
            }
            break;
        }

        if (passes) m_visibleIndices.append(i);
        } // end tag-filter block
        nextItem:;
    }

    // Sort
    std::stable_sort(m_visibleIndices.begin(), m_visibleIndices.end(),
                     [&](int a, int b) {
        const MediaItem& ia = m_allItems[a];
        const MediaItem& ib = m_allItems[b];
        bool asc = (so == SortOrder::Ascending);
        int cmp = 0;
        switch (sf) {
        case SortField::Date:
            cmp = ia.dateTime < ib.dateTime ? -1 : ia.dateTime > ib.dateTime ? 1 : 0;
            break;
        case SortField::Name:
            cmp = ia.displayName.compare(ib.displayName, Qt::CaseInsensitive);
            break;
        case SortField::Tags:
            cmp = ia.tags.size() < ib.tags.size() ? -1 : ia.tags.size() > ib.tags.size() ? 1 : 0;
            break;
        case SortField::FileSize:
            cmp = ia.fileSize < ib.fileSize ? -1 : ia.fileSize > ib.fileSize ? 1 : 0;
            break;
        }
        return asc ? cmp < 0 : cmp > 0;
    });

    rebuildGrid();
}

void GalleryView::refresh() {
    // Default: show all, sorted by date descending
    m_visibleIndices.clear();
    for (int i = 0; i < m_allItems.size(); ++i)
        m_visibleIndices.append(i);

    std::stable_sort(m_visibleIndices.begin(), m_visibleIndices.end(),
                     [&](int a, int b) {
        return m_allItems[a].dateTime > m_allItems[b].dateTime;
    });

    rebuildGrid();
}

void GalleryView::setColumns(int c) {
    m_columns = qBound(1, c, 25);
    AppSettings::instance().setGridColumns(m_columns);
    rebuildGrid();
}

void GalleryView::setOptionsVisible(bool v) {
    m_optionsVisible = v;
    for (auto* tile : m_tiles)
        tile->setOptionsVisible(v);
    // Resize tiles
    rebuildGrid();
}

void GalleryView::setCovered(bool covered) {
    m_covered = covered;
    for (auto* tile : m_tiles)
        tile->setCovered(covered);
}

QSize GalleryView::tileSize() const {
    int availW = viewport()->width() - m_grid->contentsMargins().left()
                 - m_grid->contentsMargins().right()
                 - (m_columns - 1) * m_grid->spacing();
    int w = qMax(TILE_MIN_WIDTH, availW / m_columns);
    int h = m_optionsVisible ? TILE_BASE_HEIGHT_OPTIONS : TILE_BASE_HEIGHT_PLAIN;
    // Scale height proportionally for wider tiles
    if (m_columns == 1) h = qMin(600, h + w / 2);
    return QSize(w, h);
}

void GalleryView::rebuildGrid() {
    // Remove all widgets from grid
    while (QLayoutItem* item = m_grid->takeAt(0)) {
        if (item->widget()) item->widget()->hide();
        delete item;
    }
    m_indexToTile.clear();

    QSize ts = tileSize();
    int count = m_visibleIndices.size();

    // If tile size changed, cancel pending loads and reset thumbnail cache
    if (ts != m_lastTileSize) {
        m_loader->cancelAll();
        m_lastTileSize = ts;
        for (int gi : std::as_const(m_visibleIndices))
            m_allItems[gi].thumbnailLoaded = false;
    }

    // Create/reuse tiles
    while (m_tiles.size() < count) {
        auto* tile = new MediaThumbnail(m_tagMgr, m_container);
        connect(tile, &MediaThumbnail::doubleClicked, this, &GalleryView::itemDoubleClicked);
        connect(tile, &MediaThumbnail::nameChanged, this, &GalleryView::onNameChanged);
        connect(tile, &MediaThumbnail::tagsModified, this, &GalleryView::onTagsModified);
        m_tiles.append(tile);
    }

    for (int vi = 0; vi < count; ++vi) {
        int gi = m_visibleIndices[vi];
        MediaThumbnail* tile = m_tiles[vi];
        tile->setItem(m_allItems[gi], gi);
        tile->setOptionsVisible(m_optionsVisible);
        tile->setCovered(m_covered);
        tile->setFixedSize(ts);
        tile->show();

        // Restore cached thumbnail immediately so tiles don't go black on filter/tag changes
        if (m_allItems[gi].thumbnailLoaded && !m_allItems[gi].thumbnail.isNull())
            tile->setThumbnail(m_allItems[gi].thumbnail);

        int row = vi / m_columns;
        int col = vi % m_columns;
        m_grid->addWidget(tile, row, col);
        m_indexToTile[gi] = tile;
    }

    // Hide unused tiles
    for (int i = count; i < m_tiles.size(); ++i)
        m_tiles[i]->hide();

    scheduleVisibilityUpdate();
}

void GalleryView::requestVisibleThumbnails() {
    QRect viewportRect = viewport()->rect();
    QSize ts = tileSize();

    for (int vi = 0; vi < m_visibleIndices.size(); ++vi) {
        int gi = m_visibleIndices[vi];
        if (!m_indexToTile.contains(gi)) continue;
        MediaThumbnail* tile = m_indexToTile[gi];
        if (!tile->isVisible()) continue;

        // Check if tile is in viewport
        QRect tileRect = tile->geometry();
        QRect scrolledRect = tileRect.translated(0, -verticalScrollBar()->value());
        if (!viewportRect.intersects(scrolledRect)) continue;

        // Request thumbnail
        const MediaItem& item = m_allItems[gi];
        if (!item.thumbnailLoaded) {
            m_loader->requestThumbnail(item.filePath, ts, gi);
        }
    }
}

void GalleryView::onThumbnailReady(int index, const QString& path, const QPixmap& pix) {
    if (index < 0 || index >= m_allItems.size()) return;
    m_allItems[index].thumbnailLoaded = true;
    m_allItems[index].thumbnail = pix;  // cache for rebuildGrid reuse
    if (m_indexToTile.contains(index)) {
        m_indexToTile[index]->setThumbnail(pix);
        // If gallery is in covered mode, immediately re-cover this tile
        if (m_covered)
            m_indexToTile[index]->setCovered(true);
    }
}

void GalleryView::onThumbnailFailed(int index, const QString& /*path*/) {
    if (index < 0 || index >= m_allItems.size()) return;
    // Mark as not loaded so retry can re-request it
    m_allItems[index].thumbnailLoaded = false;
    m_failedIndices.insert(index);
    // Show a placeholder so the tile doesn't stay blank
    if (m_indexToTile.contains(index)) {
        QSize ts = tileSize();
        QPixmap ph(ts);
        ph.fill(QColor(30, 40, 48));
        QPainter p(&ph);
        p.setPen(QColor(120, 140, 150));
        p.setFont(QFont("Arial", 10));
        p.drawText(ph.rect(), Qt::AlignCenter, "⚠ Ladefehler");
        p.end();
        m_indexToTile[index]->setThumbnail(ph);
    }
}

void GalleryView::retryFailedThumbnails() {
    if (m_failedIndices.isEmpty()) return;
    QSize ts = tileSize();
    for (int gi : std::as_const(m_failedIndices)) {
        if (gi < 0 || gi >= m_allItems.size()) continue;
        m_allItems[gi].thumbnailLoaded = false;
        m_loader->requestThumbnail(m_allItems[gi].filePath, ts, gi);
    }
    m_failedIndices.clear();
}

void GalleryView::reloadAllThumbnails() {
    m_loader->cancelAll();
    m_failedIndices.clear();
    QSize ts = tileSize();
    for (int gi = 0; gi < m_allItems.size(); ++gi) {
        m_allItems[gi].thumbnailLoaded = false;
    }
    // Request only the currently visible thumbnails first; the rest follow on scroll
    requestVisibleThumbnails();
}

void GalleryView::onNameChanged(int index, const QString& name) {
    if (index >= 0 && index < m_allItems.size()) {
        m_allItems[index].displayName = name;
    }
    emit nameChanged(index, name);
}

void GalleryView::onTagsModified(int index, const QStringList& tags) {
    if (index >= 0 && index < m_allItems.size()) {
        m_allItems[index].tags = tags;
    }
    emit tagsModified(index, tags);
}

void GalleryView::scheduleVisibilityUpdate() {
    m_visibilityTimer->start();
}

void GalleryView::updateVisibleThumbnails() {
    requestVisibleThumbnails();
}

void GalleryView::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ShiftModifier) {
        int delta = e->angleDelta().y();
        // Accumulate the target column count without rebuilding the grid yet
        int current = (m_pendingColumns >= 1) ? m_pendingColumns : m_columns;
        if (delta > 0 && current > 1)  m_pendingColumns = current - 1;
        else if (delta < 0 && current < 25) m_pendingColumns = current + 1;
        else { e->accept(); return; }

        // Lazy-create the debounce timer (100 ms feels instant, no stutter)
        if (!m_wheelTimer) {
            m_wheelTimer = new QTimer(this);
            m_wheelTimer->setSingleShot(true);
            m_wheelTimer->setInterval(80);
            connect(m_wheelTimer, &QTimer::timeout, this, [this]() {
                if (m_pendingColumns >= 1) {
                    m_columns = qBound(1, m_pendingColumns, 25);
                    AppSettings::instance().setGridColumns(m_columns);
                    m_pendingColumns = -1;
                    rebuildGrid();
                }
            });
        }
        m_wheelTimer->start(); // restart on every tick → fires once after burst ends
        e->accept();
        return;
    }
    QScrollArea::wheelEvent(e);
}

void GalleryView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void GalleryView::dropEvent(QDropEvent* e) {
    QStringList mediaFiles;
    for (const QUrl& url : e->mimeData()->urls()) {
        QString path = url.toLocalFile();
        QFileInfo fi(path);
        if (fi.isDir()) {
            emit folderDropped(path);
            return;  // folder drop: open folder, ignore any files
        }
        if (fi.exists() && MediaItem::detectType(path) != MediaType::Unknown)
            mediaFiles << path;
    }
    if (!mediaFiles.isEmpty())
        emit mediaFilesDropped(mediaFiles);
}

void GalleryView::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    // Use a debounce timer so rapid resize events don't hammer rebuildGrid
    if (!m_resizeTimer) {
        m_resizeTimer = new QTimer(this);
        m_resizeTimer->setSingleShot(true);
        m_resizeTimer->setInterval(120);
        connect(m_resizeTimer, &QTimer::timeout, this, [this]() {
            rebuildGrid();
        });
    }
    m_resizeTimer->start();
}

void GalleryView::scrollContentsBy(int dx, int dy) {
    QScrollArea::scrollContentsBy(dx, dy);
    scheduleVisibilityUpdate();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Group Mode
// ─────────────────────────────────────────────────────────────────────────────
void GalleryView::enterGroupMode(const QString& tag) {
    m_groupMode    = true;
    m_groupModeTag = tag;

    // Create banner at top of gallery
    if (!m_groupModeBanner) {
        m_groupModeBanner = new QWidget(this);
        m_groupModeBanner->setFixedHeight(38);
        m_groupModeBanner->setStyleSheet(
            "QWidget { background: rgba(180,120,255,0.18); border-bottom: 1px solid rgba(180,120,255,0.5); }");
        auto* bannerLay = new QHBoxLayout(m_groupModeBanner);
        bannerLay->setContentsMargins(12, 4, 12, 4);

        auto* lbl = new QLabel(QString(), m_groupModeBanner);
        lbl->setObjectName("groupModeLbl");
        lbl->setText(QString("🗂  Group mode: Tag \"%1\"  —  Right-click items to toggle tag membership").arg(tag));
        lbl->setStyleSheet("color: #d0a0ff; font-size: 12px; font-weight: 600; background: transparent;");
        bannerLay->addWidget(lbl, 1);

        auto* cancelBtn = new QPushButton("✕  Cancel", m_groupModeBanner);
        cancelBtn->setStyleSheet(
            "QPushButton { background: rgba(200,100,100,0.25); border: 1px solid rgba(200,100,100,0.5);"
            "border-radius: 6px; color: #ff9090; padding: 3px 12px; font-size: 11px; }"
            "QPushButton:hover { background: rgba(200,100,100,0.5); }");
        connect(cancelBtn, &QPushButton::clicked, this, &GalleryView::exitGroupMode);
        bannerLay->addWidget(cancelBtn);
    }
    m_groupModeBanner->setVisible(true);

    // Reposition banner
    m_groupModeBanner->setGeometry(0, 0, viewport()->width(), 38);
    m_groupModeBanner->raise();

    // Update tiles: dim items not tagged, enable right-click toggle
    for (int vi = 0; vi < m_visibleIndices.size(); ++vi) {
        int gi = m_visibleIndices[vi];
        if (!m_indexToTile.contains(gi)) continue;
        MediaThumbnail* tile = m_indexToTile[gi];
        bool hasTag = m_allItems[gi].tags.contains(tag);
        tile->setProperty("groupMode",    true);
        tile->setProperty("groupModeTag", tag);
        tile->setProperty("groupTagged",  hasTag);
        tile->setSelected(hasTag);
        // Style dimming
        if (!hasTag)
            tile->setStyleSheet("MediaThumbnail { background: rgb(18,28,34); border-radius: 8px;"
                                "border: 1px solid rgba(40,60,70,0.4); opacity: 0.5; }");
        else
            tile->setStyleSheet("MediaThumbnail { background: rgb(30,22,48); border-radius: 8px;"
                                "border: 2px solid rgba(180,120,255,0.7); }");
    }
}

void GalleryView::exitGroupMode() {
    m_groupMode = false;
    m_groupModeTag.clear();

    if (m_groupModeBanner) {
        m_groupModeBanner->setVisible(false);
    }

    // Reset tile styles
    for (auto* tile : m_tiles) {
        tile->setProperty("groupMode", false);
        tile->setSelected(false);
        tile->setStyleSheet(""); // restore default
        tile->update();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Add-to-Tag Mode
// ─────────────────────────────────────────────────────────────────────────────
void GalleryView::enterAddToTagMode(const QString& tag) {
    // Exit any existing modes first
    if (m_groupMode) exitGroupMode();
    if (m_addToTagMode) exitAddToTagMode();

    m_addToTagMode    = true;
    m_addToTagModeTag = tag;

    // Create banner
    if (!m_addToTagBanner) {
        m_addToTagBanner = new QWidget(this);
        m_addToTagBanner->setFixedHeight(38);
        m_addToTagBanner->setStyleSheet(
            "QWidget { background: rgba(0,180,160,0.18); border-bottom: 1px solid rgba(0,200,180,0.5); }");
        auto* bannerLay = new QHBoxLayout(m_addToTagBanner);
        bannerLay->setContentsMargins(12, 4, 12, 4);

        auto* lbl = new QLabel(QString(), m_addToTagBanner);
        lbl->setObjectName("addToTagLabel");
        lbl->setStyleSheet("color: #00e8d0; font-size: 12px; font-weight: 600; background: transparent;");
        bannerLay->addWidget(lbl, 1);

        auto* cancelBtn = new QPushButton("✕  Beenden", m_addToTagBanner);
        cancelBtn->setStyleSheet(
            "QPushButton { background: rgba(200,100,100,0.25); border: 1px solid rgba(200,100,100,0.5);"
            "border-radius: 6px; color: #ff9090; padding: 3px 12px; font-size: 11px; }"
            "QPushButton:hover { background: rgba(200,100,100,0.5); }");
        connect(cancelBtn, &QPushButton::clicked, this, &GalleryView::exitAddToTagMode);
        bannerLay->addWidget(cancelBtn);
    }

    // Update label text
    if (auto* lbl = m_addToTagBanner->findChild<QLabel*>("addToTagLabel"))
        lbl->setText(QString("🏷  Add-to-Tag Modus: \"%1\"  —  Linksklick zum Hinzufügen/Entfernen").arg(tag));

    m_addToTagBanner->setGeometry(0, 0, viewport()->width(), 38);
    m_addToTagBanner->setVisible(true);
    m_addToTagBanner->raise();

    // Style all tiles: bright border if tagged, dimmed if not
    for (int vi = 0; vi < m_visibleIndices.size(); ++vi) {
        int gi = m_visibleIndices[vi];
        if (!m_indexToTile.contains(gi)) continue;
        MediaThumbnail* tile = m_indexToTile[gi];
        bool hasTag = m_allItems[gi].tags.contains(tag);
        tile->setProperty("addToTagMode",    true);
        tile->setProperty("addToTagModeTag", tag);
        tile->setProperty("addToTagTagged",  hasTag);
        tile->setSelected(hasTag);
        if (hasTag)
            tile->setStyleSheet(
                "MediaThumbnail { background: rgba(0,200,160,0.13); border-radius: 8px;"
                "border: 3px solid #00ffdd; }");
        else
            tile->setStyleSheet(
                "MediaThumbnail { background: rgba(10,18,22,0.6); border-radius: 8px;"
                "border: 1px solid rgba(40,60,70,0.4); opacity: 0.7; }");
    }
}

void GalleryView::exitAddToTagMode() {
    m_addToTagMode = false;
    m_addToTagModeTag.clear();

    if (m_addToTagBanner)
        m_addToTagBanner->setVisible(false);

    for (auto* tile : m_tiles) {
        tile->setProperty("addToTagMode", false);
        tile->setSelected(false);
        tile->setStyleSheet("");
        tile->update();
    }
}

void GalleryView::onDirectoryChanged() {
    if (m_currentFolder.isEmpty()) return;

    // Build set of current files on disk
    QDir dir(m_currentFolder);
    QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
    QSet<QString> onDisk;
    for (const QFileInfo& fi : entries) {
        if (MediaItem::detectType(fi.filePath()) != MediaType::Unknown)
            onDisk.insert(fi.filePath());
    }

    // Build set of currently loaded files
    QSet<QString> loaded;
    for (const MediaItem& item : m_allItems)
        loaded.insert(item.filePath);

    bool changed = false;

    // Remove items no longer on disk
    for (int i = m_allItems.size() - 1; i >= 0; --i) {
        if (!onDisk.contains(m_allItems[i].filePath)) {
            m_allItems.removeAt(i);
            changed = true;
        }
    }

    // Add new items found on disk
    for (const QFileInfo& fi : entries) {
        if (loaded.contains(fi.filePath())) continue;
        MediaType t = MediaItem::detectType(fi.filePath());
        if (t == MediaType::Unknown) continue;

        MediaItem item;
        item.filePath    = fi.filePath();
        item.displayName = fi.completeBaseName();
        item.fileSize    = fi.size();
        item.type        = t;
        item.dateTime    = fi.lastModified();
        m_allItems.append(item);
        changed = true;
    }

    if (changed) {
        emit folderChanged();
        refresh();
    }
}
