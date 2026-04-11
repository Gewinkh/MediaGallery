#pragma once
#include <QWidget>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QSlider>
#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>

class VideoPlayer : public QWidget {
    Q_OBJECT
public:
    explicit VideoPlayer(QWidget* parent = nullptr);
    ~VideoPlayer();

    void loadFile(const QString& path);
    void play();
    void pause();
    void stop();
    bool isPlaying() const;

signals:
    void positionChanged(qint64 pos);
    void durationChanged(qint64 dur);
    void mediaFinished();

protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private slots:
    void onPositionChanged(qint64 pos);
    void onDurationChanged(qint64 dur);
    void onPlayPauseClicked();
    void onPlayerStateChanged();
    void hideControls();
    void showControls();

protected:
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    QMediaPlayer* m_player;
    QAudioOutput* m_audio;
    QVideoWidget* m_videoWidget;

    // Controls
    QWidget* m_controlBar;
    QToolButton* m_playPauseBtn;
    QSlider* m_seekSlider;
    QLabel* m_timeLabel;
    QToolButton* m_muteBtn;
    QSlider* m_volumeSlider;

    QTimer* m_hideTimer;
    bool m_userSeeking = false;

    static QString formatTime(qint64 ms);
};
