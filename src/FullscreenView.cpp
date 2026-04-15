#include "FullscreenView.h"
#include "Icons.h"
#include "Strings.h"
#include <QLineEdit>
#include <QTimer>
#include <QPainter>
#include <QRandomGenerator>
#include <QFileInfo>
#include <QMouseEvent>
#include <QApplication>
#include <QDesktopServices>
#include <QImageReader>
#include <QStringListModel>

FullscreenView::FullscreenView(TagManager* tagMgr, QWidget* parent)
    : QWidget(parent), m_tagMgr(tagMgr)
{
    setMouseTracking(true);
    setStyleSheet("background: #0a1216;");

    // --- Top bar ---
    m_topBar = new QWidget(this);
    m_topBar->setFixedHeight(50);
    m_topBar->setStyleSheet(
        "background: qlineargradient(y1:0,y2:1,stop:0 rgba(0,0,0,0.85),stop:1 rgba(0,0,0,0));");
    auto* topLay = new QHBoxLayout(m_topBar);
    topLay->setContentsMargins(10, 6, 10, 6);
    topLay->setSpacing(10);

    m_backBtn = new QToolButton(m_topBar);
    m_backBtn->setStyleSheet(
        "QToolButton { background: rgba(0,0,0,0.5); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 8px; color: #c8dbd5; font-size: 13px; padding: 4px 12px; }"
        "QToolButton:hover { background: rgba(0,180,160,0.3); color: #00c8b4; }");
    connect(m_backBtn, &QToolButton::clicked, this, [this]() {
        m_videoPlayer->stop();
        emit backRequested();
    });
    topLay->addWidget(m_backBtn);

    m_nameEdit = new QLineEdit(m_topBar);
    m_nameEdit->setAlignment(Qt::AlignCenter);
    m_nameEdit->setStyleSheet(
        "QLineEdit { background: rgba(0,0,0,0.5); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 8px; color: #dcebd8; font-size: 14px; font-weight: 600; padding: 4px 10px; }"
        "QLineEdit:focus { border-color: rgba(0,200,180,0.6); }");
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &FullscreenView::onNameEdited);
    topLay->addWidget(m_nameEdit, 1);

    m_infoLabel = new QLabel(m_topBar);
    m_infoLabel->setStyleSheet("color: rgba(150,180,175,0.8); font-size: 11px;");
    m_infoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    topLay->addWidget(m_infoLabel);

    m_dateEditBtn = new QToolButton(m_topBar);
    m_dateEditBtn->setIcon(Icons::calendar());
    m_dateEditBtn->setIconSize(QSize(18, 18));
    m_dateEditBtn->setStyleSheet(
        "QToolButton { background: rgba(0,0,0,0.4); border: 1px solid rgba(255,255,255,0.1);"
        "border-radius: 6px; padding: 3px 6px; }"
        "QToolButton:hover { background: rgba(0,180,160,0.3); }");
    connect(m_dateEditBtn, &QToolButton::clicked, this, [this]() {
        emit editDateRequested(m_currentGlobalIndex);
    });
    topLay->addWidget(m_dateEditBtn);

    m_deleteBtn = new QToolButton(m_topBar);
    m_deleteBtn->setIcon(Icons::trash());
    m_deleteBtn->setIconSize(QSize(18, 18));
    m_deleteBtn->setStyleSheet(
        "QToolButton { background: rgba(180,40,40,0.35); border: 1px solid rgba(200,60,60,0.5);"
        "border-radius: 6px; padding: 3px 6px; }"
        "QToolButton:hover { background: rgba(220,50,50,0.65); }");
    m_deleteBtn->setToolTip(tr("Medium löschen"));
    connect(m_deleteBtn, &QToolButton::clicked, this, [this]() {
        emit deleteMediaRequested(m_currentGlobalIndex);
    });
    topLay->addWidget(m_deleteBtn);

    // --- Image label ---
    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("background: transparent;");
    m_imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // --- Video player ---
    m_videoPlayer = new VideoPlayer(this);
    m_videoPlayer->hide();
    connect(m_videoPlayer, &VideoPlayer::mediaFinished, this, &FullscreenView::showNext);

    // --- Bottom bar ---
    m_bottomBar = new QWidget(this);
    m_bottomBar->setFixedHeight(56);
    m_bottomBar->setStyleSheet(
        "background: qlineargradient(y1:0,y2:1,stop:0 rgba(0,0,0,0),stop:1 rgba(0,0,0,0.85));");
    auto* botLay = new QHBoxLayout(m_bottomBar);
    botLay->setContentsMargins(10, 6, 10, 6);
    botLay->setSpacing(10);

    m_prevBtn = new QToolButton(m_bottomBar);
    m_prevBtn->setFocusPolicy(Qt::NoFocus);
    m_prevBtn->setStyleSheet(
        "QToolButton { background: rgba(0,0,0,0.5); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 8px; color: #c8dbd5; font-size: 13px; padding: 5px 14px; }"
        "QToolButton:hover { background: rgba(0,180,160,0.3); color: #00c8b4; }");
    connect(m_prevBtn, &QToolButton::clicked, this, &FullscreenView::showPrev);
    botLay->addWidget(m_prevBtn);

    m_randomBtn = new QToolButton(m_bottomBar);
    m_randomBtn->setFocusPolicy(Qt::NoFocus);
    m_randomBtn->setCheckable(true);
    m_randomBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_randomBtn->setIcon(Icons::shuffle());
    m_randomBtn->setIconSize(QSize(16, 16));
    m_randomBtn->setToolTip(Strings::get(StringKey::FullscreenRandom));
    m_randomBtn->setStyleSheet(
        "QToolButton { background: rgba(0,0,0,0.5); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 8px; color: #c8dbd5; font-size: 13px; padding: 5px 14px; }"
        "QToolButton:checked { background: rgba(0,180,160,0.35); border-color: rgba(0,180,160,0.6); color: #00c8b4; }"
        "QToolButton:hover { background: rgba(0,180,160,0.2); }");
    connect(m_randomBtn, &QToolButton::toggled, this, &FullscreenView::setRandomNext);
    botLay->addWidget(m_randomBtn);

    m_tagBar = new TagBar(m_tagMgr, m_bottomBar);
    m_tagBar->setEditable(true);
    connect(m_tagBar, &TagBar::tagsModified, this, [this](const QString&, const QStringList& tags) {
        emit tagsModified(m_currentGlobalIndex, tags);
    });
    botLay->addWidget(m_tagBar, 1);

    m_nextBtn = new QToolButton(m_bottomBar);
    m_nextBtn->setFocusPolicy(Qt::NoFocus);
    m_nextBtn->setStyleSheet(m_prevBtn->styleSheet());
    connect(m_nextBtn, &QToolButton::clicked, this, &FullscreenView::showNext);
    botLay->addWidget(m_nextBtn);

    // Hide timer
    m_barHideTimer = new QTimer(this);
    m_barHideTimer->setSingleShot(true);
    m_barHideTimer->setInterval(4000);
    connect(m_barHideTimer, &QTimer::timeout, this, &FullscreenView::hideBars);

    retranslate(); // set initial texts
}

void FullscreenView::setItems(const QVector<MediaItem>* items, const QVector<int>* visibleIndices) {
    m_items = items;
    m_visibleIndices = visibleIndices;
}

void FullscreenView::showItem(int globalIndex) {
    m_currentGlobalIndex = globalIndex;
    m_currentVisiblePos = -1;
    if (m_visibleIndices) {
        for (int i = 0; i < m_visibleIndices->size(); ++i) {
            if ((*m_visibleIndices)[i] == globalIndex) {
                m_currentVisiblePos = i;
                break;
            }
        }
    }

    if (m_randomNext) {
        if (m_randomHistoryPos < m_randomHistory.size() - 1)
            m_randomHistory.resize(m_randomHistoryPos + 1);
        m_randomHistory.append(globalIndex);
        if (m_randomHistory.size() > 200)
            m_randomHistory.removeFirst();
        m_randomHistoryPos = m_randomHistory.size() - 1;
    }

    m_zoom = 1.0;
    m_panOffset = {0.0, 0.0};
    m_panning = false;
    setCursor(Qt::ArrowCursor);
    applyCurrentItem();
}

void FullscreenView::applyCurrentItem() {
    if (!m_items || m_currentGlobalIndex < 0 || m_currentGlobalIndex >= m_items->size()) return;
    const MediaItem& item = (*m_items)[m_currentGlobalIndex];

    m_nameEdit->setText(item.displayName.isEmpty() ? item.baseName() : item.displayName);

    QString info = item.dateTime.toString("dd.MM.yyyy HH:mm");
    if (item.fileSize > 0) {
        double mb = item.fileSize / 1024.0 / 1024.0;
        info += QString("  |  %1 MB").arg(mb, 0, 'f', 1);
    }
    if (item.isAudio()) info += QString("  |  %1").arg(item.audioFormatLabel());
    m_infoLabel->setText(info);

    m_tagBar->setFile(item.fileName());
    m_tagBar->refresh();

    bool isVid   = item.isVideo();
    bool isAudio = item.isAudio();
    bool isImg   = item.isImage();

    m_imageLabel->setVisible(!isVid);
    m_videoPlayer->setVisible(isVid || isAudio);

    if (isVid) {
        if (AppSettings::instance().videoPlayback() == VideoPlayback::Native) {
            m_videoPlayer->loadFile(item.filePath);
        } else {
            QDesktopServices::openUrl(QUrl::fromLocalFile(item.filePath));
            m_videoPlayer->stop();
        }
    } else if (isAudio) {
        // Use video player for audio (Qt Multimedia handles audio-only files)
        m_videoPlayer->loadFile(item.filePath);
        // Show a stylized audio background in imageLabel (hidden behind player widget)
        m_imageLabel->setVisible(false);
    } else {
        // Image
        m_videoPlayer->stop();

        QImageReader reader(item.filePath);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (!img.isNull()) {
            m_originalPixmap = QPixmap::fromImage(std::move(img));
        } else {
            m_originalPixmap = QPixmap();
            m_imageLabel->clear();
            m_imageLabel->setText(Strings::get(StringKey::FullscreenImageLoadError));
        }
        updateZoom();
    }

    updateDisplay();
    showBars();
}

void FullscreenView::updateDisplay() {
    m_topBar->setGeometry(0, 0, width(), m_topBar->height());
    m_bottomBar->setGeometry(0, height() - m_bottomBar->height(), width(), m_bottomBar->height());
    int contentH = height() - m_topBar->height() - m_bottomBar->height();
    m_videoPlayer->setGeometry(0, m_topBar->height(), width(), contentH);
    updateZoom();
}

void FullscreenView::updateZoom() {
    if (m_originalPixmap.isNull() || m_imageLabel->isHidden()) return;

    QSize imgSz = m_originalPixmap.size();
    QSize viewSz = size();
    viewSz.setHeight(viewSz.height() - m_topBar->height() - m_bottomBar->height());

    double fitScaleW = double(viewSz.width())  / imgSz.width();
    double fitScaleH = double(viewSz.height()) / imgSz.height();
    double fitScale  = qMin(fitScaleW, fitScaleH);
    double scale     = fitScale * m_zoom;

    QSize scaled = QSize(int(imgSz.width() * scale), int(imgSz.height() * scale));

    m_imageLabel->setFixedSize(scaled);
    m_imageLabel->setPixmap(
        m_originalPixmap.scaled(scaled, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    int baseX = (width()  - scaled.width())  / 2;
    int baseY = m_topBar->height() + (viewSz.height() - scaled.height()) / 2;

    int x = baseX + int(m_panOffset.x());
    int y = baseY + int(m_panOffset.y());

    const int margin = 60;
    x = qBound(-(scaled.width()  - margin), x, width()  - margin);
    y = qBound(m_topBar->height() - (scaled.height() - margin),
               y,
               height() - m_bottomBar->height() - margin);

    m_imageLabel->move(x, y);
}

void FullscreenView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    m_topBar->setGeometry(0, 0, width(), m_topBar->height());
    m_bottomBar->setGeometry(0, height() - m_bottomBar->height(), width(), m_bottomBar->height());
    int contentH = height() - m_topBar->height() - m_bottomBar->height();
    m_videoPlayer->setGeometry(0, m_topBar->height(), width(), contentH);
    updateZoom();
}

void FullscreenView::wheelEvent(QWheelEvent* e) {
    double factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    m_zoom = qBound(MIN_ZOOM, m_zoom * factor, MAX_ZOOM);
    if (m_zoom <= 1.0) {
        m_panOffset = {0.0, 0.0};
        setCursor(Qt::ArrowCursor);
    } else {
        setCursor(Qt::OpenHandCursor);
    }
    updateZoom();
    e->accept();
}

void FullscreenView::keyPressEvent(QKeyEvent* e) {
    // Space: always toggle play/pause – never delegate to focused buttons
    if (e->key() == Qt::Key_Space) {
        if (m_videoPlayer->isVisible()) {
            if (m_videoPlayer->isPlaying())
                m_videoPlayer->pause();
            else
                m_videoPlayer->play();
        }
        e->accept();
        return;
    }
    // Don't intercept keys when the name editor has focus
    if (m_nameEdit->hasFocus()) {
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
    case Qt::Key_Left:  showPrev(); break;
    case Qt::Key_Right: showNext(); break;
    case Qt::Key_Escape:
        m_videoPlayer->stop();
        emit backRequested();
        break;
    case Qt::Key_T:
        // Erstes T: Dropdown öffnen. Zweites T (wenn Dropdown offen): Tags vom letzten übernehmen.
        showBars();
        if (m_tagBar->isDropdownOpen()) {
            m_tagBar->closeDropdown();
            emit applyLastTagsRequested(m_currentGlobalIndex);
        } else {
            m_tagBar->showTagDropdownAnchoredAt(m_dateEditBtn);
        }
        e->accept();
        break;
    case Qt::Key_K:
        // Erstes K: Kategorie-Dropdown öffnen. Zweites K (wenn Dropdown offen): Kategorien vom letzten übernehmen.
        showBars();
        if (m_tagBar->isDropdownOpen()) {
            m_tagBar->closeDropdown();
            emit applyLastCategoriesRequested(m_currentGlobalIndex);
        } else {
            m_tagBar->showCategoryDropdownAnchoredAt(m_dateEditBtn);
        }
        e->accept();
        break;
    case Qt::Key_D:
        // Open date editor with day section pre-selected
        emit editDateWithDayFocusRequested(m_currentGlobalIndex);
        e->accept();
        break;
    default: QWidget::keyPressEvent(e);
    }
}

void FullscreenView::mousePressEvent(QMouseEvent* e) {
    // Determine whether any input widget currently has focus
    bool nameActive = m_nameEdit->hasFocus();
    // Check if any child of the bottom bar (tag input) has focus
    QWidget* fw = QApplication::focusWidget();
    bool tagActive  = fw && m_bottomBar->isAncestorOf(fw);
    bool inputActive = nameActive || tagActive;

    QPoint globalPos = e->globalPosition().toPoint();

    // ── Click-away for nameEdit ───────────────────────────────────────────────
    if (nameActive) {
        QRect editRect(m_nameEdit->mapToGlobal(QPoint(0,0)), m_nameEdit->size());
        if (!editRect.contains(globalPos)) {
            onNameEdited();
            m_nameEdit->clearFocus();
            setFocus();
        }
    }

    // ── Click-away for tag bar ────────────────────────────────────────────────
    QRect bottomBarRect(m_bottomBar->mapToGlobal(QPoint(0,0)), m_bottomBar->size());
    if (!bottomBarRect.contains(globalPos)) {
        if (m_tagBar->isDropdownOpen())
            m_tagBar->closeDropdown();
        QWidget* focused = QApplication::focusWidget();
        if (focused && m_bottomBar->isAncestorOf(focused)) {
            focused->clearFocus();
            setFocus();
        }
    }

    // ── Video area: click commits any active input AND toggles play/pause ────
    if (e->button() == Qt::LeftButton && m_videoPlayer->isVisible()) {
        QRect videoRect = m_videoPlayer->geometry();
        if (videoRect.contains(e->pos())) {
            // Always toggle play/pause — input cleanup already happened above
            if (m_videoPlayer->isPlaying())
                m_videoPlayer->pause();
            else
                m_videoPlayer->play();
            e->accept();
            return;
        }
    }

    m_inputWasActive = false;

    if (e->button() == Qt::LeftButton && m_zoom > 1.01) {
        m_panning = true;
        m_panStart = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void FullscreenView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        setCursor(m_zoom > 1.01 ? Qt::OpenHandCursor : Qt::ArrowCursor);
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void FullscreenView::mouseMoveEvent(QMouseEvent* e) {
    if (m_panning) {
        QPointF delta = e->pos() - m_panStart;
        m_panOffset += delta;
        m_panStart = e->pos();
        updateZoom();
        e->accept();
        return;
    }
    showBars();
    QWidget::mouseMoveEvent(e);
}

void FullscreenView::showBars() {
    m_topBar->show();
    m_bottomBar->show();
    m_barHideTimer->start();
}

void FullscreenView::hideBars() {
    if (!m_optionsVisible) {
        m_topBar->hide();
        m_bottomBar->hide();
    }
}

void FullscreenView::setOptionsVisible(bool v) {
    m_optionsVisible = v;
    if (v) {
        showBars();
        m_barHideTimer->stop();
    }
}

void FullscreenView::showPrev() {
    if (m_randomNext && m_randomHistoryPos > 0) {
        m_randomHistoryPos--;
        int globalIdx = m_randomHistory[m_randomHistoryPos];
        bool wasRandom = m_randomNext;
        m_randomNext = false;
        showItem(globalIdx);
        m_randomNext = wasRandom;
        m_randomHistoryPos = m_randomHistory.indexOf(globalIdx);
        return;
    }
    int pos = prevVisiblePos();
    if (pos >= 0) showItem((*m_visibleIndices)[pos]);
}

void FullscreenView::showNext() {
    int pos = m_randomNext ? randomVisiblePos() : nextVisiblePos();
    if (pos >= 0) showItem((*m_visibleIndices)[pos]);
}

void FullscreenView::showRandom() {
    int pos = randomVisiblePos();
    if (pos >= 0) showItem((*m_visibleIndices)[pos]);
}

int FullscreenView::nextVisiblePos() const {
    if (!m_visibleIndices || m_visibleIndices->isEmpty()) return -1;
    int next = m_currentVisiblePos + 1;
    if (next >= m_visibleIndices->size()) next = 0;
    return next;
}

int FullscreenView::prevVisiblePos() const {
    if (!m_visibleIndices || m_visibleIndices->isEmpty()) return -1;
    int prev = m_currentVisiblePos - 1;
    if (prev < 0) prev = m_visibleIndices->size() - 1;
    return prev;
}

int FullscreenView::randomVisiblePos() const {
    if (!m_visibleIndices || m_visibleIndices->isEmpty()) return -1;
    return QRandomGenerator::global()->bounded(m_visibleIndices->size());
}

void FullscreenView::updateRandomBtn() {
    m_randomBtn->setChecked(m_randomNext);
}

void FullscreenView::onNameEdited() {
    emit nameChanged(m_currentGlobalIndex, m_nameEdit->text().trimmed());
}

void FullscreenView::refreshTagBar() {
    m_tagBar->refresh();
}

void FullscreenView::retranslate() {
    m_backBtn->setText(Strings::get(StringKey::FullscreenBack));
    m_nameEdit->setPlaceholderText(Strings::get(StringKey::FullscreenFilenamePlaceholder));
    m_dateEditBtn->setToolTip(Strings::get(StringKey::FullscreenEditDate));
    m_deleteBtn->setToolTip(tr("Medium löschen"));
    m_prevBtn->setText(Strings::get(StringKey::FullscreenPrev));
    m_randomBtn->setToolTip(Strings::get(StringKey::FullscreenRandom));
    m_nextBtn->setText(Strings::get(StringKey::FullscreenNext));
    m_tagBar->retranslate();
}
