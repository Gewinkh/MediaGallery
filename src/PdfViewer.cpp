// ══════════════════════════════════════════════════════════════════════════════
// PdfViewer.cpp  –  unified Audio/Video annotation support
// ══════════════════════════════════════════════════════════════════════════════
#include "PdfViewer.h"
#include "Icons.h"
#include "Style.h"
#include "Strings.h"

#include <QKeyEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTime>
#include <QSizePolicy>
#include <QListWidgetItem>
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <QEvent>
#include <QScrollBar>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QPdfPageNavigator>
#include <QStandardPaths>
#include <QCursor>
#include <QFrame>
#include <QUrl>
#include <QDesktopServices>
#include <QPalette>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
QString PdfViewer::toolButtonStyle() {
    return QStringLiteral(
        "QToolButton {"
        "  background: rgba(0,0,0,0.45);"
        "  border: 1px solid rgba(255,255,255,0.15);"
        "  border-radius: 6px;"
        "  color: #c8dbd5;"
        "  font-size: 12px;"
        "  padding: 3px 8px;"
        "}"
        "QToolButton:hover { background: rgba(0,180,160,0.35); color: #00c8b4; }"
        "QToolButton:pressed { background: rgba(0,140,125,0.5); }"
        "QToolButton:checked { background: rgba(0,180,160,0.4); border-color: rgba(0,200,180,0.6); }"
        "QToolButton:disabled { color: rgba(200,210,205,0.35); border-color: rgba(255,255,255,0.07); }");
}

QString PdfViewer::formatTime(qint64 ms) {
    int s = static_cast<int>(ms / 1000) % 60;
    int m = static_cast<int>(ms / 60000) % 60;
    int h = static_cast<int>(ms / 3600000);
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
    return QString("%1:%2").arg(m).arg(s,2,10,QChar('0'));
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
PdfViewer::PdfViewer(QWidget* parent) : QWidget(parent) {
    m_doc          = new QPdfDocument(this);
    m_mediaHandler = new PdfMediaHandler(m_doc, this);

    m_audioPlayer = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_audioPlayer->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.8f);

    m_videoPlayer = new QMediaPlayer(this);
    m_videoAudio  = new QAudioOutput(this);
    m_videoPlayer->setAudioOutput(m_videoAudio);
    m_videoAudio->setVolume(0.8f);

    setupUi();

    connect(m_doc, &QPdfDocument::statusChanged,
            this, &PdfViewer::onDocumentStatusChanged);
    connect(m_audioPlayer, &QMediaPlayer::playbackStateChanged,
            this, &PdfViewer::onAudioPlayerStateChanged);
    connect(m_audioPlayer, &QMediaPlayer::positionChanged,
            this, &PdfViewer::onAudioPositionChanged);
    connect(m_audioPlayer, &QMediaPlayer::durationChanged,
            this, &PdfViewer::onAudioDurationChanged);
    connect(m_videoPlayer, &QMediaPlayer::playbackStateChanged,
            this, &PdfViewer::onVideoPlayerStateChanged);
    connect(m_videoPlayer, &QMediaPlayer::positionChanged,
            this, &PdfViewer::onVideoPositionChanged);
    connect(m_videoPlayer, &QMediaPlayer::durationChanged,
            this, &PdfViewer::onVideoDurationChanged);
}

PdfViewer::~PdfViewer() {
    m_audioPlayer->stop();
    m_videoPlayer->stop();
    m_mediaHandler->cleanup();
    m_doc->close();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Setup
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::setupUi() {
    // background set via applyTheme(); initial fallback only
    setStyleSheet("background: #0d1518;");
    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    setupToolbar();
    mainLay->addWidget(m_toolbar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(3);
    m_splitter->setStyleSheet(
        "QSplitter::handle { background: rgba(0,180,160,0.18); }"
        "QSplitter::handle:hover { background: rgba(0,180,160,0.45); }");

    m_view = new QPdfView(m_splitter);
    m_view->setPageMode(QPdfView::PageMode::MultiPage);
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_view->setStyleSheet("background: transparent; border: none;");

    // ── Blue text-selection highlight ─────────────────────────────────────────
    // QPdfView (Qt 6.4+) renders the text selection using QPalette::Highlight.
    // Set it on the view AND the viewport so whichever internal widget does the
    // painting picks it up. Must be done before first show().
    {
        QPalette pal = m_view->palette();
        pal.setColor(QPalette::Active,   QPalette::Highlight, QColor(60, 130, 255, 160));
        pal.setColor(QPalette::Inactive, QPalette::Highlight, QColor(60, 130, 255, 120));
        pal.setColor(QPalette::Active,   QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::Inactive, QPalette::HighlightedText, Qt::white);
        m_view->setPalette(pal);
        if (m_view->viewport())
            m_view->viewport()->setPalette(pal);
    }

    // Overlay sits inside the viewport, z-order on top
    m_overlay = new MediaOverlayWidget(m_view, m_doc, m_view->viewport());
    m_overlay->resize(m_view->viewport()->size());
    m_overlay->show();
    connect(m_overlay, &MediaOverlayWidget::annotationClicked,
            this, &PdfViewer::onAnnotationClicked);

    m_view->installEventFilter(this);
    if (m_view->viewport()) {
        m_view->viewport()->installEventFilter(this);
        m_view->viewport()->setMouseTracking(true);
    }

    if (auto* nav = m_view->pageNavigator()) {
        connect(nav, &QPdfPageNavigator::currentPageChanged,
                this, &PdfViewer::onNavigatorPageChanged);
    }

    m_thumbPanel = new QListWidget(m_splitter);
    m_thumbPanel->setViewMode(QListView::IconMode);
    m_thumbPanel->setIconSize(QSize(130, 170));
    m_thumbPanel->setGridSize(QSize(154, 200));
    m_thumbPanel->setResizeMode(QListView::Adjust);
    m_thumbPanel->setMovement(QListView::Static);
    m_thumbPanel->setSpacing(6);
    m_thumbPanel->setFixedWidth(178);
    m_thumbPanel->setStyleSheet(Style::pdfViewerStyle());
    m_thumbPanel->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_thumbPanel->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_thumbPanel, &QListWidget::currentRowChanged,
            this, &PdfViewer::onThumbnailClicked);

    m_splitter->addWidget(m_view);
    m_splitter->addWidget(m_thumbPanel);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    mainLay->addWidget(m_splitter, 1);

    setupAudioPanel();
    mainLay->addWidget(m_audioPanel);
    m_audioPanel->hide();

    setupVideoDialog();
}

void PdfViewer::setupToolbar() {
    m_toolbar = new QWidget(this);
    m_toolbar->setFixedHeight(46);
    m_toolbar->setStyleSheet(
        "background: qlineargradient(y1:0,y2:1,"
        "stop:0 rgba(15,25,30,0.97),stop:1 rgba(10,18,22,0.97));"
        "border-bottom: 1px solid rgba(0,180,160,0.18);");

    auto* lay = new QHBoxLayout(m_toolbar);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(4);
    const QString btnStyle = toolButtonStyle();

    m_docTitleLabel = new QLabel(m_toolbar);
    m_docTitleLabel->setStyleSheet("color: rgba(200,220,215,0.75); font-size: 12px;");
    m_docTitleLabel->setMaximumWidth(220);
    m_docTitleLabel->setTextFormat(Qt::PlainText);
    lay->addWidget(m_docTitleLabel);
    lay->addStretch(1);

    m_prevBtn = new QToolButton(m_toolbar);
    m_prevBtn->setText("◀");
    m_prevBtn->setToolTip(Strings::get(StringKey::PdfPrevPageTooltip));
    m_prevBtn->setStyleSheet(btnStyle);
    connect(m_prevBtn, &QToolButton::clicked, this, &PdfViewer::goToPrevPage);
    lay->addWidget(m_prevBtn);

    m_pageSpin = new QSpinBox(m_toolbar);
    m_pageSpin->setMinimum(1); m_pageSpin->setMaximum(1);
    m_pageSpin->setFixedWidth(54);
    m_pageSpin->setAlignment(Qt::AlignCenter);
    m_pageSpin->setStyleSheet(
        "QSpinBox { background: rgba(0,0,0,0.45); border: 1px solid rgba(255,255,255,0.15);"
        "  border-radius: 6px; color: #c8dbd5; font-size: 12px; padding: 2px 2px 2px 6px; }"
        "QSpinBox::up-button, QSpinBox::down-button { width:0; height:0; border:none; }");
    connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PdfViewer::onPageSpinChanged);
    lay->addWidget(m_pageSpin);

    m_totalPagesLabel = new QLabel(" / 1", m_toolbar);
    m_totalPagesLabel->setStyleSheet("color: rgba(180,200,196,0.75); font-size: 12px;");
    lay->addWidget(m_totalPagesLabel);

    m_nextBtn = new QToolButton(m_toolbar);
    m_nextBtn->setText("▶");
    m_nextBtn->setToolTip(Strings::get(StringKey::PdfNextPageTooltip));
    m_nextBtn->setStyleSheet(btnStyle);
    connect(m_nextBtn, &QToolButton::clicked, this, &PdfViewer::goToNextPage);
    lay->addWidget(m_nextBtn);

    auto* sep1 = new QFrame(m_toolbar);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet("color: rgba(255,255,255,0.12);");
    lay->addWidget(sep1);

    m_zoomOutBtn = new QToolButton(m_toolbar);
    m_zoomOutBtn->setText("–");
    m_zoomOutBtn->setToolTip(Strings::get(StringKey::PdfZoomOutTooltip));
    m_zoomOutBtn->setStyleSheet(btnStyle);
    connect(m_zoomOutBtn, &QToolButton::clicked, this, &PdfViewer::zoomOut);
    lay->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel("100 %", m_toolbar);
    m_zoomLabel->setFixedWidth(52);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setStyleSheet(
        "QLabel { background: rgba(0,0,0,0.45); border: 1px solid rgba(255,255,255,0.15);"
        "  border-radius: 6px; color: #c8dbd5; font-size: 12px; padding: 3px 4px; }");
    lay->addWidget(m_zoomLabel);

    m_zoomInBtn = new QToolButton(m_toolbar);
    m_zoomInBtn->setText("+");
    m_zoomInBtn->setToolTip(Strings::get(StringKey::PdfZoomInTooltip));
    m_zoomInBtn->setStyleSheet(btnStyle);
    connect(m_zoomInBtn, &QToolButton::clicked, this, &PdfViewer::zoomIn);
    lay->addWidget(m_zoomInBtn);

    auto* sep2 = new QFrame(m_toolbar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet("color: rgba(255,255,255,0.12);");
    lay->addWidget(sep2);

    m_scrollModeBtn = new QToolButton(m_toolbar);
    m_scrollModeBtn->setText(Strings::get(StringKey::PdfScrollModeMulti));
    m_scrollModeBtn->setCheckable(true);
    m_scrollModeBtn->setChecked(true);
    m_scrollModeBtn->setToolTip(Strings::get(StringKey::PdfScrollModeTooltip));
    m_scrollModeBtn->setStyleSheet(btnStyle);
    connect(m_scrollModeBtn, &QToolButton::clicked, this, &PdfViewer::toggleScrollMode);
    lay->addWidget(m_scrollModeBtn);

    auto* sep3 = new QFrame(m_toolbar);
    sep3->setFrameShape(QFrame::VLine);
    sep3->setStyleSheet("color: rgba(255,255,255,0.12);");
    lay->addWidget(sep3);

    m_thumbToggleBtn = new QToolButton(m_toolbar);
    m_thumbToggleBtn->setText("⊟");
    m_thumbToggleBtn->setToolTip(Strings::get(StringKey::PdfSidebarTooltip));
    m_thumbToggleBtn->setCheckable(true);
    m_thumbToggleBtn->setChecked(true);
    m_thumbToggleBtn->setStyleSheet(btnStyle);
    connect(m_thumbToggleBtn, &QToolButton::clicked, [this](bool checked){
        m_thumbPanel->setVisible(checked);
        m_thumbToggleBtn->setText(checked ? "⊟" : "⊞");
    });
    lay->addWidget(m_thumbToggleBtn);
}

void PdfViewer::setupAudioPanel() {
    m_audioPanel = new QWidget(this);
    m_audioPanel->setFixedHeight(52);
    m_audioPanel->setStyleSheet(
        "background: qlineargradient(y1:0,y2:1,"
        "stop:0 rgba(10,30,35,0.98),stop:1 rgba(5,20,25,0.98));"
        "border-top: 1px solid rgba(0,180,160,0.25);");
    auto* lay = new QHBoxLayout(m_audioPanel);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(8);

    m_audioLabel = new QLabel(Strings::get(StringKey::PdfAudioLabel), m_audioPanel);
    m_audioLabel->setStyleSheet("color: rgba(0,200,180,0.9); font-size: 12px;");
    lay->addWidget(m_audioLabel);

    const QString btnStyle = toolButtonStyle();

    m_audioPlayBtn = new QToolButton(m_audioPanel);
    m_audioPlayBtn->setIcon(Icons::playBare());
    m_audioPlayBtn->setIconSize(QSize(18, 18));
    m_audioPlayBtn->setFixedSize(30, 30);
    m_audioPlayBtn->setStyleSheet(btnStyle);
    connect(m_audioPlayBtn, &QToolButton::clicked, this, &PdfViewer::onPlayAudioClicked);
    lay->addWidget(m_audioPlayBtn);

    m_audioSeekSlider = new QSlider(Qt::Horizontal, m_audioPanel);
    m_audioSeekSlider->setRange(0, 1000);
    m_audioSeekSlider->setStyleSheet(
        "QSlider::groove:horizontal { height:4px; background:rgba(255,255,255,0.2); border-radius:2px; }"
        "QSlider::sub-page:horizontal { background:#00c8b4; border-radius:2px; }"
        "QSlider::handle:horizontal { width:12px; height:12px; margin:-4px 0;"
        "  background:white; border-radius:6px; }");
    connect(m_audioSeekSlider, &QSlider::sliderPressed,  [this]{ m_audioSeeking = true; });
    connect(m_audioSeekSlider, &QSlider::sliderReleased, [this]{
        m_audioSeeking = false;
        qint64 dur = m_audioPlayer->duration();
        if (dur > 0)
            m_audioPlayer->setPosition(
                static_cast<qint64>(m_audioSeekSlider->value() / 1000.0 * dur));
    });
    lay->addWidget(m_audioSeekSlider, 1);

    m_audioTimeLabel = new QLabel("0:00 / 0:00", m_audioPanel);
    m_audioTimeLabel->setStyleSheet("color: rgba(180,200,196,0.8); font-size: 11px;");
    m_audioTimeLabel->setFixedWidth(80);
    lay->addWidget(m_audioTimeLabel);

    m_audioMuteBtn = new QToolButton(m_audioPanel);
    m_audioMuteBtn->setIcon(Icons::volumeOn());
    m_audioMuteBtn->setIconSize(QSize(14, 14));
    m_audioMuteBtn->setFixedSize(26, 26);
    m_audioMuteBtn->setStyleSheet(btnStyle);
    connect(m_audioMuteBtn, &QToolButton::clicked, [this]{
        bool muted = !m_audioOutput->isMuted();
        m_audioOutput->setMuted(muted);
        m_audioMuteBtn->setIcon(muted ? Icons::volumeOff() : Icons::volumeOn());
    });
    lay->addWidget(m_audioMuteBtn);

    m_audioVolSlider = new QSlider(Qt::Horizontal, m_audioPanel);
    m_audioVolSlider->setRange(0, 100);
    m_audioVolSlider->setValue(80);
    m_audioVolSlider->setFixedWidth(70);
    m_audioVolSlider->setStyleSheet(m_audioSeekSlider->styleSheet());
    connect(m_audioVolSlider, &QSlider::valueChanged, [this](int v){
        m_audioOutput->setVolume(v / 100.0f);
    });
    lay->addWidget(m_audioVolSlider);
}

void PdfViewer::setupVideoDialog() {
    m_videoDialog = new QDialog(this,
        Qt::Tool | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    m_videoDialog->setWindowTitle(Strings::get(StringKey::PdfVideoTitle));
    m_videoDialog->setMinimumSize(480, 360);
    m_videoDialog->resize(640, 420);
    m_videoDialog->setStyleSheet("background: #080f12;");

    auto* vlay = new QVBoxLayout(m_videoDialog);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    m_videoLabel = new QLabel(m_videoDialog);
    m_videoLabel->setStyleSheet(
        "color: rgba(0,200,180,0.85); font-size: 12px; padding: 6px 10px;");
    vlay->addWidget(m_videoLabel);

    m_videoWidget = new QVideoWidget(m_videoDialog);
    m_videoWidget->setStyleSheet("background: black;");
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vlay->addWidget(m_videoWidget, 1);

    auto* ctrlBar = new QWidget(m_videoDialog);
    ctrlBar->setFixedHeight(50);
    ctrlBar->setStyleSheet(
        "background: qlineargradient(y1:0,y2:1,"
        "stop:0 rgba(5,15,18,0.98),stop:1 rgba(3,10,13,0.98));"
        "border-top: 1px solid rgba(0,180,160,0.2);");
    auto* cLay = new QHBoxLayout(ctrlBar);
    cLay->setContentsMargins(10, 5, 10, 5);
    cLay->setSpacing(8);

    const QString btnStyle = toolButtonStyle();

    m_videoPlayBtn = new QToolButton(ctrlBar);
    m_videoPlayBtn->setIcon(Icons::playBare());
    m_videoPlayBtn->setIconSize(QSize(18, 18));
    m_videoPlayBtn->setFixedSize(30, 30);
    m_videoPlayBtn->setStyleSheet(btnStyle);
    connect(m_videoPlayBtn, &QToolButton::clicked, [this]{
        if (m_videoPlayer->playbackState() == QMediaPlayer::PlayingState)
            m_videoPlayer->pause();
        else
            m_videoPlayer->play();
    });
    cLay->addWidget(m_videoPlayBtn);

    m_videoSeekSlider = new QSlider(Qt::Horizontal, ctrlBar);
    m_videoSeekSlider->setRange(0, 1000);
    m_videoSeekSlider->setStyleSheet(m_audioSeekSlider->styleSheet());
    connect(m_videoSeekSlider, &QSlider::sliderPressed,  [this]{ m_videoSeeking = true; });
    connect(m_videoSeekSlider, &QSlider::sliderReleased, [this]{
        m_videoSeeking = false;
        qint64 dur = m_videoPlayer->duration();
        if (dur > 0)
            m_videoPlayer->setPosition(
                static_cast<qint64>(m_videoSeekSlider->value() / 1000.0 * dur));
    });
    cLay->addWidget(m_videoSeekSlider, 1);

    m_videoTimeLabel = new QLabel("0:00 / 0:00", ctrlBar);
    m_videoTimeLabel->setStyleSheet("color: rgba(180,200,196,0.8); font-size: 11px;");
    m_videoTimeLabel->setFixedWidth(80);
    cLay->addWidget(m_videoTimeLabel);

    vlay->addWidget(ctrlBar);

    m_videoPlayer->setVideoOutput(m_videoWidget);
    connect(m_videoDialog, &QDialog::finished, [this]{ m_videoPlayer->stop(); });
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlay geometry
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::repositionOverlay() {
    if (m_overlay && m_view && m_view->viewport())
        m_overlay->resize(m_view->viewport()->size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Public interface
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::loadFile(const QString& path) {
    m_audioPlayer->stop();
    m_videoPlayer->stop();
    m_videoDialog->hide();
    m_audioPanel->hide();
    m_mediaHandler->cleanup();
    m_currentPath = path;
    m_thumbPanel->clear();
    m_currentPage = 0;
    m_activeAudioAnnIdx = -1;
    if (m_overlay) {
        m_overlay->setAnnotations({});
        m_overlay->setActiveAudioIndex(-1);
    }

    // Detach the view before closing/reloading so QPdfView does not try to
    // render stale page data during the load.  Reattach before load() so the
    // internal renderer is wired up when the Ready signal fires.
    m_view->setDocument(nullptr);
    m_doc->close();
    m_view->setDocument(m_doc);
    m_doc->load(path);
}

void PdfViewer::closeDocument() {
    m_audioPlayer->stop();
    m_videoPlayer->stop();
    m_videoDialog->hide();
    m_doc->close();
    m_audioPanel->hide();
    m_thumbPanel->clear();
    m_mediaHandler->cleanup();
    m_activeAudioAnnIdx = -1;
    if (m_overlay) {
        m_overlay->setAnnotations({});
        m_overlay->setActiveAudioIndex(-1);
    }
}

void PdfViewer::retranslate() {
    m_prevBtn->setToolTip(Strings::get(StringKey::PdfPrevPageTooltip));
    m_nextBtn->setToolTip(Strings::get(StringKey::PdfNextPageTooltip));
    m_zoomOutBtn->setToolTip(Strings::get(StringKey::PdfZoomOutTooltip));
    m_zoomInBtn->setToolTip(Strings::get(StringKey::PdfZoomInTooltip));
    m_scrollModeBtn->setToolTip(Strings::get(StringKey::PdfScrollModeTooltip));
    m_thumbToggleBtn->setToolTip(Strings::get(StringKey::PdfSidebarTooltip));
    // ScrollModeBtn-Text hängt vom aktuellen Modus ab
    m_scrollModeBtn->setText(m_continuousMode
        ? Strings::get(StringKey::PdfScrollModeMulti)
        : Strings::get(StringKey::PdfScrollModeSingle));
    m_videoDialog->setWindowTitle(Strings::get(StringKey::PdfVideoTitle));
    m_audioLabel->setText(Strings::get(StringKey::PdfAudioLabel));
}

// ─────────────────────────────────────────────────────────────────────────────
// Document ready
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::onDocumentStatusChanged(QPdfDocument::Status status) {
    if (status != QPdfDocument::Status::Ready) return;

    // setDocument(m_doc) was already called before load() in loadFile(),
    // so the internal renderer is connected and will have rendered by now.
    // Do NOT call setDocument again here — it resets the renderer state.

    const int total = m_doc->pageCount();
    m_pageSpin->setMaximum(qMax(1, total));
    m_pageSpin->setValue(1);
    m_totalPagesLabel->setText(QString(" / %1").arg(total));
    m_prevBtn->setEnabled(total > 1);
    m_nextBtn->setEnabled(total > 1);

    QString title = m_doc->metaData(QPdfDocument::MetaDataField::Title).toString();
    if (title.isEmpty()) title = QFileInfo(m_currentPath).fileName();
    m_docTitleLabel->setText(title);

    // Defer setZoomMode until the event loop has fully processed the Ready
    // signal, the pending show/resize events from setVisible(), and the
    // geometry assignment in updateDisplay().  50 ms is enough on every
    // platform tested; 0 ms is not reliable when the widget was just made
    // visible for the first time.
    QTimer::singleShot(50, this, [this]() {
        if (!m_view || m_doc->status() != QPdfDocument::Status::Ready) return;
        // Re-send a genuine resize with the now-correct viewport size so
        // QPdfView recomputes its page layout before we switch zoom mode.
        QResizeEvent re(m_view->viewport()->size(), m_view->viewport()->size());
        QApplication::sendEvent(m_view, &re);
        m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
        updateZoomLabel();
        if (m_view->viewport())
            m_view->viewport()->update();
    });

    emit pageChanged(1, total);

    QTimer::singleShot(120, this, &PdfViewer::buildThumbnails);

    // Qt limitation: QPdfDocument has no public annotation API.
    // PdfMediaHandler works around this by parsing the raw PDF byte stream.
    m_mediaHandler->scanDocument(m_currentPath);

    if (m_mediaHandler->hasMedia() || m_mediaHandler->hasLinks()) {
        m_overlay->setAnnotations(m_mediaHandler->allAnnotations());
        int nAudio = 0, nVideo = 0, nLink = 0;
        for (const auto& a : m_mediaHandler->allAnnotations()) {
            if (a.type == MediaAnnotation::Type::Video) ++nVideo;
            else if (a.type == MediaAnnotation::Type::Audio) ++nAudio;
            else if (a.type == MediaAnnotation::Type::Link)  ++nLink;
        }
        QStringList parts;
        if (nAudio > 0) parts << Strings::get(StringKey::PdfAudioCount).arg(nAudio);
        if (nVideo > 0) parts << Strings::get(StringKey::PdfVideoCount).arg(nVideo);
        if (nLink  > 0) parts << tr("%1 Link(s)").arg(nLink);
        if (m_mediaHandler->hasMedia()) {
            m_audioLabel->setText(Strings::get(StringKey::PdfMediaClickHint).arg(parts.join(", ")));
            m_audioPanel->show();
        }
    } else {
        extractAndPrepareAudio();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Annotation click dispatch
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::onAnnotationClicked(int index) {
    const auto& anns = m_mediaHandler->allAnnotations();
    if (index < 0 || index >= anns.size()) return;
    const MediaAnnotation& ann = anns[index];

    if (ann.type == MediaAnnotation::Type::Link) {
        // Open hyperlink in default browser
        const QString url = ann.sourceUrl;
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl(url));
        return;
    }

    if (ann.type == MediaAnnotation::Type::Video) {
        playVideoAnnotation(ann);
        return;
    }

    // Audio: toggle play/stop if clicking the same badge that is active
    if (ann.type == MediaAnnotation::Type::Audio ||
        ann.type == MediaAnnotation::Type::Unknown) {
        if (m_activeAudioAnnIdx == index &&
            m_audioPlayer->playbackState() == QMediaPlayer::PlayingState) {
            // Same badge clicked while playing → stop
            m_audioPlayer->stop();
            m_activeAudioAnnIdx = -1;
            if (m_overlay) m_overlay->setActiveAudioIndex(-1);
            return;
        }
        m_activeAudioAnnIdx = index;
        if (m_overlay) m_overlay->setActiveAudioIndex(index);
        playAudioAnnotation(ann);
    }
}

void PdfViewer::playAudioAnnotation(const MediaAnnotation& ann) {
    QString src = ann.resolvedUri();
    if (src.isEmpty() || (!src.startsWith("http") && !QFile::exists(src))) {
        QFileInfo fi(m_currentPath);
        static const QStringList exts = {"mp3","wav","ogg","flac","aac","m4a","opus","aiff"};
        for (const QString& e : exts) {
            QString c = fi.dir().filePath(fi.completeBaseName() + "." + e);
            if (QFile::exists(c)) { src = c; break; }
        }
    }
    if (src.isEmpty()) {
        m_audioLabel->setText(Strings::get(StringKey::PdfAudioNotFound).arg(ann.label));
        m_audioPanel->show();
        return;
    }
    loadAudioSource(src, QString("🔊  %1").arg(ann.label));
    m_audioPlayer->play();
}

void PdfViewer::playVideoAnnotation(const MediaAnnotation& ann) {
    QString src = ann.resolvedUri();
    if (src.isEmpty()) {
        QFileInfo fi(m_currentPath);
        static const QStringList exts = {"mp4","avi","mkv","mov","webm","ogv"};
        for (const QString& e : exts) {
            QString c = fi.dir().filePath(fi.completeBaseName() + "." + e);
            if (QFile::exists(c)) { src = c; break; }
        }
    }
    if (src.isEmpty()) {
        m_audioLabel->setText(Strings::get(StringKey::PdfVideoNotFound).arg(ann.label));
        m_audioPanel->show();
        return;
    }
    m_videoPlayer->stop();
    const QUrl url = src.startsWith("http") ? QUrl(src) : QUrl::fromLocalFile(src);
    m_videoPlayer->setSource(url);
    m_videoLabel->setText(QString("🎬  %1").arg(ann.label));
    m_videoDialog->setWindowTitle(ann.label.isEmpty()
        ? Strings::get(StringKey::PdfVideoTitle) : ann.label);
    m_videoDialog->show();
    m_videoDialog->raise();
    m_videoPlayer->play();
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigator / thumbnail sync
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::onNavigatorPageChanged(int page) {
    if (page == m_currentPage) return;
    m_currentPage = page;
    syncPageSpinner(page);
    syncThumbnailSelection(page);
    emit pageChanged(page + 1, m_doc->pageCount());
    if (m_overlay) m_overlay->update();
}

void PdfViewer::syncPageSpinner(int page0based) {
    m_pageSpin->blockSignals(true);
    m_pageSpin->setValue(page0based + 1);
    m_pageSpin->blockSignals(false);
    m_prevBtn->setEnabled(page0based > 0);
    m_nextBtn->setEnabled(page0based < m_doc->pageCount() - 1);
}

void PdfViewer::syncThumbnailSelection(int page0based) {
    m_updatingThumbs = true;
    m_thumbPanel->setCurrentRow(page0based);
    if (auto* item = m_thumbPanel->item(page0based))
        m_thumbPanel->scrollToItem(item, QAbstractItemView::EnsureVisible);
    m_updatingThumbs = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Thumbnails
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::buildThumbnails() {
    if (m_doc->status() != QPdfDocument::Status::Ready) return;
    m_updatingThumbs = true;
    m_thumbPanel->clear();
    const int total = m_doc->pageCount();
    for (int i = 0; i < total; ++i) {
        QSizeF ps = m_doc->pagePointSize(i);
        double ratio = ps.width() > 0 ? ps.height() / ps.width() : 1.41;
        int tw = 130, th = qMin(170, static_cast<int>(tw * ratio));
        QImage img = m_doc->render(i, QSize(tw, th));
        QPixmap pix = QPixmap::fromImage(img);
        if (pix.isNull()) {
            pix = QPixmap(tw, th);
            pix.fill(QColor(40, 55, 60));
            QPainter p(&pix);
            p.setPen(Qt::white);
            p.drawText(pix.rect(), Qt::AlignCenter, QString::number(i + 1));
        }
        const auto pageAnns = m_mediaHandler->annotationsForPage(i);
        bool hasAudio = false, hasVideo = false;
        for (const auto& ann : pageAnns) {
            if (ann.type == MediaAnnotation::Type::Video) hasVideo = true;
            else hasAudio = true;
        }
        if (hasAudio || hasVideo) {
            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(QColor(0, 180, 160, 200));
            p.setPen(Qt::NoPen);
            p.drawEllipse(pix.width()-22, pix.height()-22, 18, 18);
            p.setPen(Qt::white);
            p.setFont(QFont("Arial", 9));
            p.drawText(QRect(pix.width()-22, pix.height()-22, 18, 18),
                       Qt::AlignCenter, hasVideo ? "▶" : "♪");
        }
        auto* item = new QListWidgetItem(QIcon(pix), QString::number(i + 1));
        item->setTextAlignment(Qt::AlignCenter);
        m_thumbPanel->addItem(item);
    }
    m_thumbPanel->setCurrentRow(m_currentPage);
    m_updatingThumbs = false;
}

void PdfViewer::onThumbnailClicked(int row) {
    if (m_updatingThumbs || row < 0) return;
    auto* nav = m_view->pageNavigator();
    if (!nav) return;
    nav->jump(row, {});
}

// ─────────────────────────────────────────────────────────────────────────────
// Sidecar audio fallback
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::extractAndPrepareAudio() {
    if (m_currentPath.isEmpty()) return;
    QFileInfo fi(m_currentPath);
    QDir dir = fi.absoluteDir();
    const QString base = fi.completeBaseName();
    static const QStringList exts = {
        "mp3","wav","ogg","flac","aac","m4a","opus","wma","aiff","aif"};
    QString found;
    for (const QString& e : exts) {
        QString c = dir.filePath(base + "." + e);
        if (QFile::exists(c)) { found = c; break; }
    }
    if (found.isEmpty()) {
        QStringList filters;
        for (const QString& e : exts) filters << "*." + e;
        const QStringList cands = dir.entryList(filters, QDir::Files, QDir::Name);
        if (!cands.isEmpty()) found = dir.filePath(cands.first());
    }
    if (!found.isEmpty())
        loadAudioSource(found, QString("🎧  %1").arg(QFileInfo(found).fileName()));
}

void PdfViewer::loadAudioSource(const QString& path, const QString& label) {
    const QUrl url = path.startsWith("http")
                     ? QUrl(path) : QUrl::fromLocalFile(path);
    m_audioPlayer->setSource(url);
    m_audioLabel->setText(label);
    m_audioPanel->show();
}

// ─────────────────────────────────────────────────────────────────────────────
// Event filter
// ─────────────────────────────────────────────────────────────────────────────
bool PdfViewer::eventFilter(QObject* obj, QEvent* e) {
    const bool isViewport = m_view && obj == m_view->viewport();

    if (isViewport && e->type() == QEvent::Resize) {
        repositionOverlay();
        if (m_overlay) m_overlay->update();
    }

    if (isViewport) {
        switch (e->type()) {
        case QEvent::Wheel: {
            auto* we = static_cast<QWheelEvent*>(e);
            if (we->modifiers() & Qt::ControlModifier) {
                handleWheelZoom(we);
                if (m_overlay) m_overlay->update();
                return true;
            }
            QTimer::singleShot(0, this, [this]{ if(m_overlay) m_overlay->update(); });
            break;
        }
        case QEvent::MouseMove: {
            if (m_overlay) {
                auto* me = static_cast<QMouseEvent*>(e);
                const int idx = m_overlay->hitTest(me->pos());
                m_overlay->updateHover(me->pos());
                m_overlay->update();
                // Only force PointingHand when over a badge/link.
                // For everything else: unsetCursor() so QPdfView manages its own
                // cursor (IBeam over text, Arrow over margins) — do NOT hardcode IBeam,
                // that would permanently override QPdfView's cursor logic.
                if (auto* vp = m_view->viewport()) {
                    if (idx >= 0)
                        vp->setCursor(Qt::PointingHandCursor);
                    else
                        vp->unsetCursor();
                }
            }
            break;
        }
        case QEvent::MouseButtonPress: {
            if (m_overlay) {
                auto* me = static_cast<QMouseEvent*>(e);
                if (me->button() == Qt::LeftButton) {
                    const int idx = m_overlay->hitTest(me->pos());
                    if (idx >= 0) {
                        // Badge/link click: handle in PdfViewer, block QPdfView.
                        emit m_overlay->annotationClicked(idx);
                        return true; // consumed — QPdfView won't start a selection
                    }
                }
            }
            break;
        }
        case QEvent::ContextMenu: {
            // Show a minimal context menu with Copy so the user can copy
            // selected text even without knowing the Ctrl+C shortcut.
            auto* ce = static_cast<QContextMenuEvent*>(e);
            const QString selected = QApplication::clipboard()->text(QClipboard::Selection);
            // Qt on Linux puts the selected text into the X11 selection buffer;
            // on all platforms we also try to trigger QPdfView's own copy action.
            QMenu menu(m_view);
            QAction* copyAct = menu.addAction(tr("Kopieren"));
            copyAct->setShortcut(QKeySequence::Copy);
            // Only enable if there is something in the selection buffer or clipboard
            const bool hasSelection = !selected.isEmpty()
                || !QApplication::clipboard()->text().isEmpty();
            copyAct->setEnabled(true); // always show; user knows best
            connect(copyAct, &QAction::triggered, this, [this]() {
                // Send Ctrl+C to QPdfView so its internal handler copies the selection
                QKeyEvent ke(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
                QApplication::sendEvent(m_view, &ke);
            });
            menu.exec(ce->globalPos());
            return true;
        }
        case QEvent::Leave:
            if (m_overlay) m_overlay->update();
            break;
        default: break;
        }
    }
    return QWidget::eventFilter(obj, e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scroll mode / Zoom
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::toggleScrollMode() {
    m_continuousMode = !m_continuousMode;
    m_view->setPageMode(m_continuousMode
                        ? QPdfView::PageMode::MultiPage
                        : QPdfView::PageMode::SinglePage);
    m_scrollModeBtn->setText(m_continuousMode
        ? Strings::get(StringKey::PdfScrollModeMulti)
        : Strings::get(StringKey::PdfScrollModeSingle));
    m_scrollModeBtn->setChecked(m_continuousMode);
    if (m_overlay) m_overlay->update();
}

void PdfViewer::updateZoomLabel() {
    int pct;
    if (m_view->zoomMode() == QPdfView::ZoomMode::Custom)
        pct = qRound(m_zoomFactor * 100.0);
    else
        pct = qRound(qMax(0.1, m_view->zoomFactor()) * 100.0);
    if (m_zoomLabel)
        m_zoomLabel->setText(QString("%1 %").arg(pct));
}

void PdfViewer::applyZoom(double factor) {
    m_zoomFactor = factor;
    m_view->setZoomFactor(factor);
    if (m_overlay) m_overlay->update();
}

void PdfViewer::zoomIn() {
    const double steps[] = {0.5,0.6,0.75,0.85,1.0,1.1,1.25,1.5,1.75,2.0,2.5,3.0,4.0};
    for (double s : steps)
        if (s > m_zoomFactor + 0.01) {
            m_view->setZoomMode(QPdfView::ZoomMode::Custom);
            applyZoom(s); updateZoomLabel(); return;
        }
}

void PdfViewer::zoomOut() {
    const double steps[] = {4.0,3.0,2.5,2.0,1.75,1.5,1.25,1.1,1.0,0.85,0.75,0.6,0.5};
    for (double s : steps)
        if (s < m_zoomFactor - 0.01) {
            m_view->setZoomMode(QPdfView::ZoomMode::Custom);
            applyZoom(s); updateZoomLabel(); return;
        }
}

void PdfViewer::fitPage() {
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
    updateZoomLabel();
    if (m_overlay) m_overlay->update();
}

void PdfViewer::fitWidth() {
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    updateZoomLabel();
    if (m_overlay) m_overlay->update();
}

// updateZoomCombo removed – replaced by updateZoomLabel()

void PdfViewer::handleWheelZoom(QWheelEvent* we) {
    double base = (m_view->zoomMode() == QPdfView::ZoomMode::Custom)
                  ? m_zoomFactor : qMax(0.1, m_view->zoomFactor());
    double steps   = we->angleDelta().y() / 120.0;
    double newFact = qBound(0.1, base * qPow(1.12, steps), 8.0);
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
    applyZoom(newFact);
    updateZoomLabel();
    we->accept();
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::goToPrevPage() {
    auto* nav = m_view->pageNavigator();
    if (!nav || m_currentPage <= 0) return;
    nav->jump(m_currentPage - 1, {});
}

void PdfViewer::goToNextPage() {
    auto* nav = m_view->pageNavigator();
    if (!nav || m_currentPage >= m_doc->pageCount() - 1) return;
    nav->jump(m_currentPage + 1, {});
}

void PdfViewer::onPageSpinChanged(int page) {
    auto* nav = m_view->pageNavigator();
    if (!nav) return;
    nav->jump(qBound(0, page - 1, m_doc->pageCount() - 1), {});
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio playback slots
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::onPlayAudioClicked() {
    if (m_audioPlayer->playbackState() == QMediaPlayer::PlayingState) {
        m_audioPlayer->pause();
        if (m_overlay) m_overlay->setActiveAudioIndex(-1);
    } else {
        m_audioPlayer->play();
        if (m_overlay) m_overlay->setActiveAudioIndex(m_activeAudioAnnIdx);
    }
}

void PdfViewer::onAudioPlayerStateChanged() {
    const bool playing = (m_audioPlayer->playbackState() == QMediaPlayer::PlayingState);
    m_audioPlayBtn->setIcon(playing ? Icons::pause() : Icons::playBare());
    if (m_overlay) {
        m_overlay->setActiveAudioIndex(playing ? m_activeAudioAnnIdx : -1);
    }
    if (!playing) {
        // If audio finished naturally reset so next badge click starts fresh
        if (m_audioPlayer->playbackState() == QMediaPlayer::StoppedState)
            m_activeAudioAnnIdx = -1;
    }
}

void PdfViewer::onAudioPositionChanged(qint64 pos) {
    if (m_audioSeeking) return;
    qint64 dur = m_audioPlayer->duration();
    if (dur > 0)
        m_audioSeekSlider->setValue(static_cast<int>(pos * 1000 / dur));
    m_audioTimeLabel->setText(formatTime(pos) + " / " + formatTime(dur));
}

void PdfViewer::onAudioDurationChanged(qint64 dur) {
    m_audioTimeLabel->setText("0:00 / " + formatTime(dur));
}

// ─────────────────────────────────────────────────────────────────────────────
// Video playback slots
// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::onVideoPlayerStateChanged() {
    bool playing = (m_videoPlayer->playbackState() == QMediaPlayer::PlayingState);
    m_videoPlayBtn->setIcon(playing ? Icons::pause() : Icons::playBare());
}

void PdfViewer::onVideoPositionChanged(qint64 pos) {
    if (m_videoSeeking) return;
    qint64 dur = m_videoPlayer->duration();
    if (dur > 0)
        m_videoSeekSlider->setValue(static_cast<int>(pos * 1000 / dur));
    m_videoTimeLabel->setText(formatTime(pos) + " / " + formatTime(dur));
}

void PdfViewer::onVideoDurationChanged(qint64 dur) {
    m_videoTimeLabel->setText("0:00 / " + formatTime(dur));
}

// ─────────────────────────────────────────────────────────────────────────────
void PdfViewer::updatePageControls() {
    auto* nav = m_view->pageNavigator();
    if (!nav) return;
    onNavigatorPageChanged(nav->currentPage());
}

void PdfViewer::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_PageUp:   goToPrevPage(); break;
    case Qt::Key_Right:
    case Qt::Key_PageDown: goToNextPage(); break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:    zoomIn(); break;
    case Qt::Key_Minus:    zoomOut(); break;
    case Qt::Key_Home:
        if (e->modifiers() & Qt::ControlModifier)
            if (auto* nav = m_view->pageNavigator()) nav->jump(0, {});
        break;
    case Qt::Key_End:
        if (e->modifiers() & Qt::ControlModifier)
            if (auto* nav = m_view->pageNavigator())
                nav->jump(m_doc->pageCount() - 1, {});
        break;
    default:
        QWidget::keyPressEvent(e);
    }
}

void PdfViewer::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // QPdfView initialises its internal page renderer lazily.  If a document
    // is already loaded (Ready) when the widget becomes visible for the first
    // time we must kick a relayout, because the viewport was 0×0 before show.
    // Use a small delay (10 ms) rather than 0 ms to let the geometry manager
    // finish positioning the widget before we ask QPdfView to fit the page.
    if (m_doc && m_doc->status() == QPdfDocument::Status::Ready) {
        QTimer::singleShot(10, this, [this]() {
            if (!m_view || m_doc->status() != QPdfDocument::Status::Ready) return;
            QResizeEvent re(m_view->viewport()->size(), m_view->viewport()->size());
            QApplication::sendEvent(m_view, &re);
            m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
            updateZoomLabel();
            if (m_view->viewport())
                m_view->viewport()->update();
        });
    }
}

void PdfViewer::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    repositionOverlay();
}


