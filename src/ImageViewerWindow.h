#pragma once
#include <QObject>
#include <QString>

class QQuickView;
class QWidget;

// ─────────────────────────────────────────────────────────────────────────────
//  ImageViewerWindow – owns a QQuickView that renders qml/ImageViewer.qml as a
//  modal, fullscreen image viewer.  Image path is pushed into QML via the
//  `imageSource` property; navigation / close come back as Qt signals.
// ─────────────────────────────────────────────────────────────────────────────
class ImageViewerWindow : public QObject {
    Q_OBJECT
public:
    explicit ImageViewerWindow(QWidget* ownerWidget, QObject* parent = nullptr);
    ~ImageViewerWindow() override;

    // Loads the image and shows the window fullscreen (modal to the owner).
    void showImage(const QString& filePath);

    void hide();
    bool isVisible() const;

signals:
    void closeRequested();
    void nextRequested();
    void prevRequested();

private:
    QWidget*     m_owner = nullptr;
    QQuickView*  m_view  = nullptr;

    void ensureView();
};
