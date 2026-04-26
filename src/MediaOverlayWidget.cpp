// ══════════════════════════════════════════════════════════════════════════════
// MediaOverlayWidget.cpp
// ══════════════════════════════════════════════════════════════════════════════
#include "MediaOverlayWidget.h"

#include <QPdfView>
#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>

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
    // All mouse events pass through to underlying QPdfView for native text
    // selection. Badge/link clicks are intercepted in PdfViewer::eventFilter.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setMouseTracking(false);

    // Floating link-URL tooltip label
    m_linkTooltip = new QLabel(this);
    m_linkTooltip->setWordWrap(false);
    m_linkTooltip->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_linkTooltip->setStyleSheet(
        "QLabel {"
        "  background: rgba(10,25,30,0.93);"
        "  color: #00c8b4;"
        "  border: 1px solid rgba(0,180,160,0.5);"
        "  border-radius: 5px;"
        "  font-size: 11px;"
        "  padding: 3px 8px;"
        "}");
    m_linkTooltip->hide();
    m_linkTooltip->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_linkTooltip->raise();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public interface
// ─────────────────────────────────────────────────────────────────────────────
void MediaOverlayWidget::setAnnotations(const QVector<MediaAnnotation>& anns) {
    m_annotations    = anns;
    m_hoveredIdx     = -1;
    m_activeAudioIdx = -1;
    m_linkTooltip->hide();
    update();
}

void MediaOverlayWidget::setActiveAudioIndex(int idx) {
    if (m_activeAudioIdx != idx) {
        m_activeAudioIdx = idx;
        update();
    }
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
        // NOTE: do NOT call setCursor() here — this widget has
        // WA_TransparentForMouseEvents = true, so it never receives real mouse
        // events and its cursor setting has no effect. Cursor is managed by
        // PdfViewer::eventFilter() on the actual viewport widget.
        update();
    }
    updateLinkTooltip(viewportPos, idx);
}

void MediaOverlayWidget::updateLinkTooltip(const QPoint& viewportPos, int annIdx) {
    if (annIdx >= 0 && m_annotations[annIdx].type == MediaAnnotation::Type::Link) {
        const QString url = m_annotations[annIdx].sourceUrl;
        // Truncate long URLs for display
        QString display = url;
        if (display.length() > 72)
            display = display.left(69) + "...";
        m_linkTooltip->setText(display);
        m_linkTooltip->adjustSize();

        // Position below the cursor, clamped inside the widget
        int tx = viewportPos.x() + 12;
        int ty = viewportPos.y() + 20;
        if (tx + m_linkTooltip->width() > width() - 4)
            tx = width() - m_linkTooltip->width() - 4;
        if (ty + m_linkTooltip->height() > height() - 4)
            ty = viewportPos.y() - m_linkTooltip->height() - 6;
        m_linkTooltip->move(tx, ty);
        m_linkTooltip->raise();
        m_linkTooltip->show();
    } else {
        m_linkTooltip->hide();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate mapping
// ─────────────────────────────────────────────────────────────────────────────

// QPdfView internal layout defaults (from Qt6 open source):
//   documentMargins : QMargins(6, 6, 6, 6)  — space between page and viewport edge
//   pageSpacing     : 3 px                  — gap between consecutive pages
// These are not overridden anywhere in this project.
static constexpr int kDocMargin   = 6;   // left/right/top/bottom
static constexpr int kPageSpacing = 3;   // between pages

double MediaOverlayWidget::effectiveZoom() const {
    // QPdfView::zoomFactor() is only meaningful in ZoomMode::Custom.
    // In FitInView / FitToWidth it reflects the LAST manually set factor
    // (often still 1.0), not the actual rendered scale.
    //
    // We compute the true rendered zoom by replicating QPdfView's own logic:
    //   FitToWidth (and FitInView in MultiPage): zoom = usableWidth / page0PointWidth
    //   FitInView  in SinglePage              : zoom = min(usableW/pageW, usableH/pageH)
    //   Custom                                : zoom = m_view->zoomFactor()
    if (!m_view || !m_doc || m_doc->pageCount() == 0) return 1.0;

    if (m_view->zoomMode() == QPdfView::ZoomMode::Custom) {
        const double z = m_view->zoomFactor();
        return (z > 0.0) ? z : 1.0;
    }

    // Use page 0 as reference (all pages in one doc share the same "design size"
    // for zoom computation purposes).
    const QSizeF page0 = m_doc->pagePointSize(0);
    if (page0.isEmpty()) return 1.0;

    const int vpW = width();
    const int vpH = height();
    const double usableW = vpW - 2.0 * kDocMargin;
    const double usableH = vpH - 2.0 * kDocMargin;

    if (m_view->zoomMode() == QPdfView::ZoomMode::FitToWidth ||
        m_view->pageMode() == QPdfView::PageMode::MultiPage) {
        // In multi-page mode FitInView behaves like FitToWidth
        return (usableW > 0 && page0.width() > 0)
               ? usableW / page0.width()
               : 1.0;
    }

    // SinglePage FitInView
    const double zW = (page0.width()  > 0) ? usableW / page0.width()  : 1.0;
    const double zH = (page0.height() > 0) ? usableH / page0.height() : 1.0;
    return qMin(zW, zH);
}

QRect MediaOverlayWidget::annotationViewportRect(const MediaAnnotation& ann) const {
    if (!m_view || !m_doc) return {};

    const double zoom = effectiveZoom();

    const QSizeF ps = m_doc->pagePointSize(ann.page);
    if (ps.isEmpty()) return {};

    const double pageW = ps.width()  * zoom;
    const double pageH = ps.height() * zoom;

    // QPdfView centres each page horizontally within the usable area
    // (viewport width minus left+right document margins).
    const double usableW = width() - 2.0 * kDocMargin;
    const double pageX   = kDocMargin + qMax(0.0, (usableW - pageW) / 2.0);

    // Vertical: top documentMargin, then stacked pages separated by pageSpacing
    double pageY = kDocMargin;
    for (int i = 0; i < ann.page; ++i) {
        const QSizeF prev = m_doc->pagePointSize(i);
        pageY += prev.height() * zoom + kPageSpacing;
    }

    // Subtract current scroll offsets
    const int scrollY = (m_view->verticalScrollBar()   && m_view->verticalScrollBar()->isVisible())
                        ? m_view->verticalScrollBar()->value()   : 0;
    const int scrollX = (m_view->horizontalScrollBar() && m_view->horizontalScrollBar()->isVisible())
                        ? m_view->horizontalScrollBar()->value() : 0;

    const double rx = pageX + ann.rect.x()      * pageW - scrollX;
    const double ry = pageY + ann.rect.y()      * pageH - scrollY;
    const double rw = ann.rect.width()  * pageW;
    const double rh = ann.rect.height() * pageH;

    // Link annotations: use exact PDF rect (already the full clickable zone)
    if (ann.type == MediaAnnotation::Type::Link) {
        return QRect(static_cast<int>(rx), static_cast<int>(ry),
                     qMax(static_cast<int>(rw), 4),
                     qMax(static_cast<int>(rh), 4));
    }

    // Media badges: guarantee a minimum click target of 32×32 px
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

        const bool hovered       = (i == m_hoveredIdx);
        const bool activePlaying = (i == m_activeAudioIdx);
        drawBadge(painter, r, m_annotations[i].type, hovered, activePlaying);
    }
}

void MediaOverlayWidget::drawBadge(QPainter& painter, const QRect& rect,
                                    MediaAnnotation::Type type,
                                    bool hovered,
                                    bool activePlaying) const
{
    // ── Link annotations: draw a subtle underline highlight, no badge circle ──
    if (type == MediaAnnotation::Type::Link) {
        // Translucent blue fill on hover
        if (hovered) {
            painter.setBrush(QColor(30, 120, 220, 45));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(rect, 2, 2);
        }
        // Teal underline at the bottom of the link rect
        const QColor lineColor = hovered ? QColor(0, 200, 180, 220)
                                         : QColor(0, 160, 140, 110);
        painter.setPen(QPen(lineColor, hovered ? 1.5 : 1.0));
        painter.drawLine(rect.bottomLeft(), rect.bottomRight());
        return;
    }

    // ── Media badges (Audio / Video) ─────────────────────────────────────────
    const int bw = 36, bh = 36;
    const QRect badge(rect.left(), rect.top(), bw, bh);

    const QColor bgNormal  = activePlaying
                             ? QColor(200, 80,  0, 210)   // orange = playing
                             : QColor(0,  160, 140, 200);
    const QColor bgHovered = activePlaying
                             ? QColor(220, 110, 0, 235)
                             : QColor(0,  210, 185, 230);

    painter.setBrush(hovered ? bgHovered : bgNormal);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(badge);

    if (hovered) {
        painter.setPen(QPen(Qt::white, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(badge.adjusted(1, 1, -1, -1));
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);

    const double cx = badge.center().x();
    const double cy = badge.center().y();

    if (type == MediaAnnotation::Type::Video) {
        QPolygonF tri;
        tri << QPointF(cx - 6, cy - 8)
            << QPointF(cx - 6, cy + 8)
            << QPointF(cx + 9, cy);
        painter.drawPolygon(tri);
    } else {
        // Playing = draw a ■ stop icon; otherwise draw speaker
        if (activePlaying) {
            painter.drawRect(QRectF(cx - 7, cy - 7, 14, 14));
        } else {
            // Speaker icon body
            const QRectF body(cx - 9, cy - 5, 7, 10);
            painter.drawRect(body);
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
    }

    // Dashed outline around full annotation hit rect
    painter.setPen(QPen(hovered ? QColor(0,210,185,160) : QColor(0,160,140,90),
                        1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect.adjusted(1, 1, -1, -1));
}

void MediaOverlayWidget::leaveEvent(QEvent* e) {
    if (m_hoveredIdx >= 0) {
        m_hoveredIdx = -1;
        update();
    }
    m_linkTooltip->hide();
    QWidget::leaveEvent(e);
}
