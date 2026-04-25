#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// MediaOverlayWidget.h
// ══════════════════════════════════════════════════════════════════════════════
//
// A transparent QWidget laid on top of QPdfView's viewport.
//
// Responsibilities:
//   • Receives hover/click events forwarded from the PdfViewer event filter
//   • Paints clickable media badges (speaker / film icon) at the annotation
//     positions derived from PdfMediaHandler
//   • Emits annotationClicked(index) so PdfViewer can start playback
//
// Design:
//   The widget has WA_TransparentForMouseEvents = false so it can accept
//   mouse events, and WA_NoSystemBackground + setAttribute(WA_TranslucentBackground)
//   so QPdfView renders through it wherever there is no badge.
//
//   It is resized in lockstep with the viewport via a QResizeEvent on the
//   viewport (handled in PdfViewer::eventFilter).
// ══════════════════════════════════════════════════════════════════════════════

#include "PdfMediaHandler.h"

#include <QWidget>
#include <QPdfDocument>
#include <QScrollBar>
#include <QVector>

class QPdfView;

class MediaOverlayWidget : public QWidget {
    Q_OBJECT
public:
    explicit MediaOverlayWidget(QPdfView* view,
                                QPdfDocument* doc,
                                QWidget* parent = nullptr);

    // Replace the annotation list (called by PdfViewer after each scan)
    void setAnnotations(const QVector<MediaAnnotation>& annotations);

    // Hit-test a viewport-coordinate point.
    // Returns annotation index, or -1 if none.
    int hitTest(const QPoint& viewportPos) const;

    // Update cursor based on hover position
    void updateHover(const QPoint& viewportPos);

signals:
    void annotationClicked(int index);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QPdfView*     m_view = nullptr;
    QPdfDocument* m_doc  = nullptr;

    QVector<MediaAnnotation> m_annotations;
    int m_hoveredIdx = -1;

    // Map a normalised annotation rect to viewport pixel coordinates.
    QRect annotationViewportRect(const MediaAnnotation& ann) const;

    // Draw a single badge (speaker or film strip icon)
    void drawBadge(QPainter& painter, const QRect& rect,
                   MediaAnnotation::Type type, bool hovered) const;
};
