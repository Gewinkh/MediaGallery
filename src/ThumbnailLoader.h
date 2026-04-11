#pragma once
#include <QObject>
#include <QRunnable>
#include <QThreadPool>
#include <QPixmap>
#include <QImage>
#include <QString>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QSize>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QEventLoop>
#include <QTimer>

class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject* parent = nullptr);
    ~ThumbnailLoader();

    void requestThumbnail(const QString& filePath, const QSize& size, int index);
    void cancelAll();

signals:
    void thumbnailReady(int index, const QString& filePath, const QPixmap& pixmap);
    void thumbnailFailed(int index, const QString& filePath);

private:
    QThreadPool* m_pool;
    QMutex m_mutex;
    QSet<QString> m_pending;
};

class ThumbnailTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    ThumbnailTask(const QString& path, const QSize& size, int index);
    void run() override;

signals:
    void done(int index, const QString& path, const QPixmap& pixmap);

private:
    QString m_path;
    QSize m_size;
    int m_index;

    static QPixmap generateVideoThumbnail(const QString& path, const QSize& size);
    static QPixmap generateImageThumbnail(const QString& path, const QSize& size);
    static QPixmap generateAudioThumbnail(const QString& path, const QSize& size);
};
