#include "VideoPlayer.h"
#include "Icons.h"
#include <QMouseEvent>
#include <QTime>

VideoPlayer::VideoPlayer(QWidget* parent) : QWidget(parent) {
    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setStyleSheet("background: black;");
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLay->addWidget(m_videoWidget);

    // Control bar
    m_controlBar = new QWidget(this);
    m_controlBar->setFixedHeight(48);
    m_controlBar->setStyleSheet(
        "background: qlineargradient(y1:0, y2:1, stop:0 rgba(0,0,0,0), stop:1 rgba(0,0,0,0.85));"
    );
    auto* ctrlLay = new QHBoxLayout(m_controlBar);
    ctrlLay->setContentsMargins(10, 4, 10, 4);
    ctrlLay->setSpacing(8);

    m_playPauseBtn = new QToolButton(m_controlBar);
    m_playPauseBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_playPauseBtn->setIcon(Icons::playBare());
    m_playPauseBtn->setIconSize(QSize(22, 22));
    m_playPauseBtn->setFixedSize(32, 32);
    m_playPauseBtn->setStyleSheet(
        "QToolButton { background: rgba(255,255,255,0.15); border: 1px solid rgba(255,255,255,0.25);"
        "border-radius: 16px; padding: 0px; }"
        "QToolButton:hover { background: rgba(255,255,255,0.28); }");
    ctrlLay->addWidget(m_playPauseBtn);

    m_seekSlider = new QSlider(Qt::Horizontal, m_controlBar);
    m_seekSlider->setRange(0, 1000);
    m_seekSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,0.2); border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #00c8b4; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0;"
        "background: white; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #00c8b4; }");
    ctrlLay->addWidget(m_seekSlider, 1);

    m_timeLabel = new QLabel("0:00 / 0:00", m_controlBar);
    m_timeLabel->setStyleSheet("color: rgba(200,220,215,0.9); font-size: 11px;");
    ctrlLay->addWidget(m_timeLabel);

    m_muteBtn = new QToolButton(m_controlBar);
    m_muteBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_muteBtn->setIcon(Icons::volumeOn());
    m_muteBtn->setIconSize(QSize(16, 16));
    m_muteBtn->setFixedSize(28, 28);
    m_muteBtn->setStyleSheet(
        "QToolButton { background: transparent; border: none; }"
        "QToolButton:hover { background: rgba(0,200,180,0.15); }");
    ctrlLay->addWidget(m_muteBtn);

    m_volumeSlider = new QSlider(Qt::Horizontal, m_controlBar);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedWidth(70);
    m_volumeSlider->setStyleSheet(m_seekSlider->styleSheet());
    ctrlLay->addWidget(m_volumeSlider);

    mainLay->addWidget(m_controlBar);

    // Media player
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_audio->setVolume(0.8f);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_videoWidget);

    // Hide timer
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(3000);

    // Connections
    connect(m_player, &QMediaPlayer::positionChanged, this, &VideoPlayer::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &VideoPlayer::onDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &VideoPlayer::onPlayerStateChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            emit mediaFinished();
    });
    connect(m_playPauseBtn, &QToolButton::clicked, this, &VideoPlayer::onPlayPauseClicked);
    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() { m_userSeeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_userSeeking = false;
        qint64 dur = m_player->duration();
        if (dur > 0)
            m_player->setPosition(qint64(m_seekSlider->value()) * dur / 1000);
    });
    connect(m_seekSlider, &QSlider::valueChanged, this, [this](int val) {
        if (m_userSeeking) {
            qint64 dur = m_player->duration();
            if (dur > 0) m_player->setPosition(qint64(val) * dur / 1000);
        }
    });
    connect(m_muteBtn, &QToolButton::clicked, this, [this]() {
        bool muted = m_audio->isMuted();
        m_audio->setMuted(!muted);
        m_muteBtn->setIcon(muted ? Icons::volumeOn() : Icons::volumeOff());
    });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_audio->setVolume(v / 100.0f);
    });
    connect(m_hideTimer, &QTimer::timeout, this, &VideoPlayer::hideControls);

    setMouseTracking(true);
    m_videoWidget->setMouseTracking(true);
}

VideoPlayer::~VideoPlayer() { m_player->stop(); }

void VideoPlayer::loadFile(const QString& path) {
    m_player->stop();
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->play();
}

void VideoPlayer::play() { m_player->play(); }
void VideoPlayer::pause() { m_player->pause(); }
void VideoPlayer::stop() { m_player->stop(); }
bool VideoPlayer::isPlaying() const {
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

void VideoPlayer::onPositionChanged(qint64 pos) {
    if (!m_userSeeking) {
        qint64 dur = m_player->duration();
        if (dur > 0) m_seekSlider->setValue(int(pos * 1000 / dur));
    }
    m_timeLabel->setText(formatTime(pos) + " / " + formatTime(m_player->duration()));
    emit positionChanged(pos);
}

void VideoPlayer::onDurationChanged(qint64 dur) {
    m_timeLabel->setText("0:00 / " + formatTime(dur));
    emit durationChanged(dur);
}

void VideoPlayer::onPlayPauseClicked() {
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void VideoPlayer::onPlayerStateChanged() {
    bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
    m_playPauseBtn->setIcon(playing ? Icons::pause() : Icons::playBare());
    if (playing) { m_hideTimer->start(); }
    else { m_hideTimer->stop(); showControls(); }
}

void VideoPlayer::hideControls() { m_controlBar->hide(); }
void VideoPlayer::showControls() { m_controlBar->show(); m_hideTimer->start(); }

void VideoPlayer::mouseMoveEvent(QMouseEvent* e) {
    QWidget::mouseMoveEvent(e);
    showControls();
}

void VideoPlayer::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        onPlayPauseClicked();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void VideoPlayer::mouseDoubleClickEvent(QMouseEvent* e) {
    QWidget::mouseDoubleClickEvent(e);
    onPlayPauseClicked();
}

QString VideoPlayer::formatTime(qint64 ms) {
    qint64 s = ms / 1000;
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}
