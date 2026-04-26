#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// MediaOverlayWidget.h
// ══════════════════════════════════════════════════════════════════════════════
//
// A transparent QWidget laid on top of QPdfView's viewport.
//
// Responsibilities:
//   • Receives hover/click events forwarded from the PdfViewer event filter
//   • Paints clickable media badges (speaker / film / link icon) at annotation
//     positions derived from PdfMediaHandler
//   • Shows a URL tooltip on hover for Link annotations
//   • Emits annotationClicked(index) so PdfViewer can start/stop playback
//     or open links
//
// ══════════════════════════════════════════════════════════════════════════════

#include "PdfMediaHandler.h"

#include <QWidget>
#include <QPdfDocument>
#include <QScrollBar>
#include <QVector>

class QPdfView;
class QLabel;

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

    // Update cursor and link tooltip based on hover position
    void updateHover(const QPoint& viewportPos);

    // Mark which annotation index is currently playing audio (for toggle badge)
    void setActiveAudioIndex(int idx);

signals:
    void annotationClicked(int index);

protected:
    void paintEvent(QPaintEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QPdfView*     m_view = nullptr;
    QPdfDocument* m_doc  = nullptr;

    QVector<MediaAnnotation> m_annotations;
    int  m_hoveredIdx    = -1;
    int  m_activeAudioIdx = -1;   // which audio annotation is currently playing

    // Floating link-URL tooltip label
    QLabel* m_linkTooltip = nullptr;

    // Map a normalised annotation rect to viewport pixel coordinates.
    QRect annotationViewportRect(const MediaAnnotation& ann) const;

    // Compute the actual rendered zoom factor QPdfView is currently using.
    // QPdfView::zoomFactor() only returns a meaningful value in Custom mode;
    // in FitInView/FitToWidth we must derive it from viewport + page geometry.
    double effectiveZoom() const;

    // Draw a single badge (speaker / film strip / link icon)
    void drawBadge(QPainter& painter, const QRect& rect,
                   MediaAnnotation::Type type, bool hovered,
                   bool activePlaying) const;

    void updateLinkTooltip(const QPoint& viewportPos, int annIdx);
};

