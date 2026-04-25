// ══════════════════════════════════════════════════════════════════════════════
// MediaOverlayWidget.cpp
// ══════════════════════════════════════════════════════════════════════════════
#include "MediaOverlayWidget.h"

#include <QPdfView>
#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QApplication>

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
MediaOverlayWidget::MediaOverlayWidget(QPdfView* view,
                                       QPdfDocument* doc,
                                       QWidget* parent)
    : QWidget(parent), m_view(view), m_doc(doc)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setMouseTracking(true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public interface
// ─────────────────────────────────────────────────────────────────────────────
void MediaOverlayWidget::setAnnotations(const QVector<MediaAnnotation>& anns) {
    m_annotations = anns;
    m_hoveredIdx  = -1;
    update();
}

int MediaOverlayWidget::hitTest(const QPoint& pos) const {
    for (int i = 0; i < m_annotations.size(); ++i)
        if (annotationViewportRect(m_annotations[i]).contains(pos))
            return i;
    return -1;
}

void MediaOverlayWidget::updateHover(const QPoint& viewportPos) {
    const int idx = hitTest(viewportPos);
    if (idx != m_hoveredIdx) {
        m_hoveredIdx = idx;
        setCursor(idx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate mapping
// ─────────────────────────────────────────────────────────────────────────────
QRect MediaOverlayWidget::annotationViewportRect(const MediaAnnotation& ann) const {
    if (!m_view || !m_doc) return {};

    double zoom = m_view->zoomFactor();
    if (zoom <= 0.0) zoom = 1.0;

    const QSizeF ps = m_doc->pagePointSize(ann.page);
    if (ps.isEmpty()) return {};

    const double pageW = ps.width()  * zoom;
    const double pageH = ps.height() * zoom;

    // QPdfView centres pages horizontally inside its viewport
    const int vpW = width();
    const double pageX = (vpW - pageW) / 2.0;

    // Cumulative vertical offset of the target page (MultiPage mode)
    // QPdfView inserts a fixed inter-page gap (typically 6 px, not exposed).
    constexpr double kPageGap = 6.0;
    double pageY = 0.0;
    for (int i = 0; i < ann.page; ++i) {
        const QSizeF prev = m_doc->pagePointSize(i);
        pageY += prev.height() * zoom + kPageGap;
    }

    // Subtract current scroll offsets
    const int scrollY = m_view->verticalScrollBar()
                        ? m_view->verticalScrollBar()->value() : 0;
    const int scrollX = m_view->horizontalScrollBar()
                        ? m_view->horizontalScrollBar()->value() : 0;

    const double rx = pageX + ann.rect.x()     * pageW - scrollX;
    const double ry = pageY + ann.rect.y()     * pageH - scrollY;
    const double rw = ann.rect.width()  * pageW;
    const double rh = ann.rect.height() * pageH;

    // Guarantee a minimum click target of 32×32 px
    const double minSz = 32.0;
    const double fx    = rw < minSz ? rx - (minSz - rw) / 2.0 : rx;
    const double fy    = rh < minSz ? ry - (minSz - rh) / 2.0 : ry;
    const double fw    = qMax(rw, minSz);
    const double fh    = qMax(rh, minSz);

    return QRect(static_cast<int>(fx), static_cast<int>(fy),
                 static_cast<int>(fw), static_cast<int>(fh));
}

// ─────────────────────────────────────────────────────────────────────────────
// Paint
// ─────────────────────────────────────────────────────────────────────────────
void MediaOverlayWidget::paintEvent(QPaintEvent*) {
    if (m_annotations.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    for (int i = 0; i < m_annotations.size(); ++i) {
        const QRect r = annotationViewportRect(m_annotations[i]);
        if (r.isEmpty() || !rect().intersects(r)) continue;
        drawBadge(painter, r, m_annotations[i].type, i == m_hoveredIdx);
    }
}

void MediaOverlayWidget::drawBadge(QPainter& painter, const QRect& rect,
                                    MediaAnnotation::Type type,
                                    bool hovered) const {
    // Badge size: fixed 36×36 px, top-left aligned inside the annotation rect
    const int bw = 36, bh = 36;
    const QRect badge(rect.left(), rect.top(), bw, bh);

    // Teal background circle
    const QColor bgNormal  = QColor(0, 160, 140, 200);
    const QColor bgHovered = QColor(0, 210, 185, 230);
    painter.setBrush(hovered ? bgHovered : bgNormal);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(badge);

    // Draw a thin border on hover
    if (hovered) {
        painter.setPen(QPen(Qt::white, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(badge.adjusted(1, 1, -1, -1));
    }

    // Icon (drawn with QPainter primitives so no external resources needed)
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);

    const double cx = badge.center().x();
    const double cy = badge.center().y();

    if (type == MediaAnnotation::Type::Video) {
        // Simple play-triangle (film strip would need SVG; triangle is clear)
        QPolygonF tri;
        tri << QPointF(cx - 6, cy - 8)
            << QPointF(cx - 6, cy + 8)
            << QPointF(cx + 9, cy);
        painter.drawPolygon(tri);
    } else {
        // Speaker icon – rectangle body + two arcs
        // Body
        const QRectF body(cx - 9, cy - 5, 7, 10);
        painter.drawRect(body);

        // Cone (triangle)
        QPolygonF cone;
        cone << QPointF(cx - 2, cy - 5)
             << QPointF(cx - 2, cy + 5)
             << QPointF(cx + 6, cy + 9)
             << QPointF(cx + 6, cy - 9);
        painter.drawPolygon(cone);

        // Sound wave arcs
        painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(cx + 4, cy - 6, 6, 12), -30*16, 60*16);
        painter.drawArc(QRectF(cx + 7, cy - 9, 10, 18), -30*16, 60*16);
    }

    // Semi-transparent outline around the full annotation hit rect
    painter.setPen(QPen(hovered ? QColor(0,210,185,160) : QColor(0,160,140,90),
                        1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect.adjusted(1, 1, -1, -1));
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse events
// ─────────────────────────────────────────────────────────────────────────────
void MediaOverlayWidget::mouseMoveEvent(QMouseEvent* e) {
    updateHover(e->pos());
    QWidget::mouseMoveEvent(e);
}

void MediaOverlayWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const int idx = hitTest(e->pos());
        if (idx >= 0) {
            emit annotationClicked(idx);
            e->accept();
            return;
        }
    }
    // Forward to the underlying QPdfView via its viewport
    if (m_view && m_view->viewport()) {
        const QPoint viewportPos = m_view->viewport()->mapFromGlobal(
                                       e->globalPosition().toPoint());
        QMouseEvent fwd(e->type(), viewportPos, e->globalPosition(),
                        e->button(), e->buttons(), e->modifiers());
        QApplication::sendEvent(m_view->viewport(), &fwd);
    }
    QWidget::mousePressEvent(e);
}

void MediaOverlayWidget::leaveEvent(QEvent* e) {
    if (m_hoveredIdx >= 0) {
        m_hoveredIdx = -1;
        unsetCursor();
        update();
    }
    QWidget::leaveEvent(e);
}
