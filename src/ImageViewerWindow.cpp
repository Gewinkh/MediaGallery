#include "ImageViewerWindow.h"
#include <QQuickView>
#include <QQuickItem>
#include <QWidget>
#include <QWindow>
#include <QUrl>

ImageViewerWindow::ImageViewerWindow(QWidget* ownerWidget, QObject* parent)
    : QObject(parent), m_owner(ownerWidget)
{
}

ImageViewerWindow::~ImageViewerWindow() {
    if (m_view) {
        m_view->deleteLater();
        m_view = nullptr;
    }
}

void ImageViewerWindow::ensureView() {
    if (m_view) return;

    m_view = new QQuickView;
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setColor(QColor(10, 10, 10));
    m_view->setTitle(QStringLiteral("MediaGallery — Image"));

    // Modal against the owning widget's top-level window
    if (m_owner) {
        if (QWindow* parentWin = m_owner->window()->windowHandle())
            m_view->setTransientParent(parentWin);
    }
    m_view->setModality(Qt::ApplicationModal);

    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/ImageViewer.qml")));

    if (QQuickItem* rootItem = m_view->rootObject()) {
        QObject::connect(rootItem, SIGNAL(closeRequested()),
                         this, SLOT(hide()));
        QObject::connect(rootItem, SIGNAL(closeRequested()),
                         this, SIGNAL(closeRequested()));
        QObject::connect(rootItem, SIGNAL(nextRequested()),
                         this, SIGNAL(nextRequested()));
        QObject::connect(rootItem, SIGNAL(previousRequested()),
                         this, SIGNAL(prevRequested()));
    }
}

void ImageViewerWindow::showImage(const QString& filePath) {
    ensureView();
    if (!m_view) return;

    if (QQuickItem* rootItem = m_view->rootObject())
        rootItem->setProperty("imageSource", QUrl::fromLocalFile(filePath));

    if (!m_view->isVisible())
        m_view->showFullScreen();
    m_view->raise();
    m_view->requestActivate();
}

void ImageViewerWindow::hide() {
    if (m_view && m_view->isVisible())
        m_view->hide();
}

bool ImageViewerWindow::isVisible() const {
    return m_view && m_view->isVisible();
}
