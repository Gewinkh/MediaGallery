#include "ManualZonePreview.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QFont>
#include <QFontMetrics>
#include <algorithm>

// ─── constants ───────────────────────────────────────────────────────────────
static constexpr int LABEL_H    = 22;   // px — bottom label bar height
static constexpr int MARGIN     = 8;    // px — padding inside widget

// ─── construction ────────────────────────────────────────────────────────────
ManualZonePreview::ManualZonePreview(QSize mainWindowSize, QWidget* parent)
    : QWidget(parent)
    , m_mainSize(mainWindowSize.isValid() ? mainWindowSize : QSize(1280, 800))
{
    setMinimumSize(280, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    m_emitTimer = new QTimer(this);
    m_emitTimer->setSingleShot(true);
    m_emitTimer->setInterval(60);   // 60 ms debounce — one emit per drag burst
    connect(m_emitTimer, &QTimer::timeout, this, [this]() {
        QSize zr = zoneReal();
        emit zoneChanged(zr.width(), m_autoHeight ? 0 : zr.height());
    });

    // Initialise zone to full-width, auto-height
    m_autoHeight = true;
    m_zone = QRect();   // computed in first resizeEvent / paint
}

// ─── public API ──────────────────────────────────────────────────────────────
void ManualZonePreview::setZone(int realW, int realH) {
    m_autoHeight = (realH == 0);
    QRect pa = previewArea();
    if (pa.isEmpty()) { update(); return; }

    double s = scaleFactor();
    int pw = qMax(MIN_ZONE, (int)std::round(realW * s));
    int ph = m_autoHeight ? pa.height() : qMax(MIN_ZONE, (int)std::round(realH * s));

    // Center horizontally, top-align vertically
    int px = pa.left() + (pa.width() - pw) / 2;
    int py = pa.top();
    m_zone = QRect(px, py, pw, ph);
    clampZone();
    update();
}

QSize ManualZonePreview::zoneReal() const {
    QSize r = previewToReal(m_zone);
    if (m_autoHeight) r.setHeight(0);
    return r;
}

// ─── geometry helpers ─────────────────────────────────────────────────────────
QRect ManualZonePreview::previewArea() const {
    return rect().adjusted(MARGIN, MARGIN, -MARGIN, -(MARGIN + LABEL_H));
}

double ManualZonePreview::scaleFactor() const {
    QRect pa = previewArea();
    if (pa.width() <= 0 || m_mainSize.width() <= 0) return 1.0;
    return static_cast<double>(pa.width()) / m_mainSize.width();
}

QRect ManualZonePreview::realToPreview(int rw, int rh) const {
    QRect pa = previewArea();
    double s  = scaleFactor();
    int pw = qMax(MIN_ZONE, (int)std::round(rw * s));
    int ph = m_autoHeight ? pa.height() : qMax(MIN_ZONE, (int)std::round(rh * s));
    int px = pa.left() + (pa.width() - pw) / 2;
    int py = pa.top();
    return QRect(px, py, pw, ph);
}

QSize ManualZonePreview::previewToReal(const QRect& pr) const {
    double s = scaleFactor();
    if (s <= 0.0) return QSize(800, 600);
    return QSize(qMax(40, (int)std::round(pr.width() / s)),
                 qMax(40, (int)std::round(pr.height() / s)));
}

ManualZonePreview::HitZone ManualZonePreview::hitTest(QPoint p) const {
    if (m_zone.isEmpty()) return HitZone::None;
    QRect se(m_zone.right() - HANDLE, m_zone.bottom() - HANDLE, HANDLE*2, HANDLE*2);
    QRect eBar(m_zone.right() - HANDLE, m_zone.top(), HANDLE*2, m_zone.height());
    QRect sBar(m_zone.left(), m_zone.bottom() - HANDLE, m_zone.width(), HANDLE*2);
    if (se.contains(p))    return HitZone::ResizeSE;
    if (eBar.contains(p))  return HitZone::ResizeE;
    if (sBar.contains(p) && !m_autoHeight)  return HitZone::ResizeS;
    if (m_zone.contains(p)) return HitZone::Move;
    return HitZone::None;
}

void ManualZonePreview::clampZone() {
    QRect pa = previewArea();
    if (pa.isEmpty() || m_zone.isEmpty()) return;

    if (m_autoHeight)
        m_zone.setHeight(pa.height());

    // Clamp size
    m_zone.setWidth(qMax(MIN_ZONE, qMin(m_zone.width(), pa.width())));
    m_zone.setHeight(qMax(MIN_ZONE, qMin(m_zone.height(), pa.height())));

    // Clamp position so zone stays inside previewArea
    int x = qBound(pa.left(), m_zone.left(), pa.right() - m_zone.width());
    int y = qBound(pa.top(),  m_zone.top(),  pa.bottom() - m_zone.height());
    m_zone.moveTopLeft(QPoint(x, y));
}

void ManualZonePreview::scheduleEmit() {
    m_emitTimer->start();
}

// ─── events ──────────────────────────────────────────────────────────────────
void ManualZonePreview::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (m_zone.isEmpty()) {
        // First layout: full-width zone
        QRect pa = previewArea();
        m_zone = pa;
    } else {
        // Re-map from real coords so zone scales with widget
        QSize real = previewToReal(m_zone);
        if (!m_autoHeight)
            setZone(real.width(), real.height());
        else
            setZone(real.width(), 0);
    }
    clampZone();
    update();
}

void ManualZonePreview::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    if (m_drag == HitZone::None)
        setCursor(Qt::ArrowCursor);
}

void ManualZonePreview::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_drag = hitTest(e->pos());
    if (m_drag != HitZone::None) {
        m_dragOrig        = e->pos();
        m_zoneAtDragStart = m_zone;
        e->accept();
    }
}

void ManualZonePreview::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag == HitZone::None) {
        // Cursor feedback
        switch (hitTest(e->pos())) {
        case HitZone::ResizeSE: setCursor(Qt::SizeFDiagCursor); break;
        case HitZone::ResizeE:  setCursor(Qt::SizeHorCursor);   break;
        case HitZone::ResizeS:  setCursor(Qt::SizeVerCursor);   break;
        case HitZone::Move:     setCursor(Qt::SizeAllCursor);   break;
        default:                setCursor(Qt::ArrowCursor);     break;
        }
        return;
    }

    QPoint delta = e->pos() - m_dragOrig;
    m_zone = m_zoneAtDragStart;

    switch (m_drag) {
    case HitZone::Move:
        m_zone.translate(delta);
        break;
    case HitZone::ResizeE:
        m_zone.setWidth(qMax(MIN_ZONE, m_zoneAtDragStart.width() + delta.x()));
        break;
    case HitZone::ResizeS:
        if (!m_autoHeight)
            m_zone.setHeight(qMax(MIN_ZONE, m_zoneAtDragStart.height() + delta.y()));
        break;
    case HitZone::ResizeSE:
        m_zone.setWidth(qMax(MIN_ZONE, m_zoneAtDragStart.width() + delta.x()));
        if (!m_autoHeight)
            m_zone.setHeight(qMax(MIN_ZONE, m_zoneAtDragStart.height() + delta.y()));
        break;
    default: break;
    }

    clampZone();
    update();
    scheduleEmit();
    e->accept();
}

void ManualZonePreview::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_drag != HitZone::None) {
        m_drag = HitZone::None;
        setCursor(Qt::ArrowCursor);
        // Emit immediately on release (not just debounced)
        m_emitTimer->stop();
        auto zr = zoneReal();
        emit zoneChanged(zr.width(), m_autoHeight ? 0 : zr.height());
        e->accept();
    }
}

// ─── painting ────────────────────────────────────────────────────────────────
void ManualZonePreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect pa = previewArea();

    // ── Background (simulated viewport) ──────────────────────────────────────
    p.fillRect(rect(), QColor(10, 16, 20));
    {
        QPainterPath bg;
        bg.addRoundedRect(pa, 4, 4);
        p.fillPath(bg, QColor(14, 22, 28));
        p.setPen(QPen(QColor(30, 50, 60), 1));
        p.drawPath(bg);
    }

    // ── Simulated tile grid OUTSIDE zone (dimmed) ─────────────────────────────
    {
        static const int TW = 24, TH = 30, SP = 3;
        p.setPen(Qt::NoPen);
        for (int y = pa.top() + SP; y + TH <= pa.bottom(); y += TH + SP) {
            for (int x = pa.left() + SP; x + TW <= pa.right(); x += TW + SP) {
                QRect tr(x, y, TW, TH);
                // dim if outside zone
                bool inside = !m_zone.isEmpty() && m_zone.contains(tr);
                QColor c = inside ? QColor(0, 90, 78, 180) : QColor(22, 38, 46, 100);
                p.setBrush(c);
                p.drawRoundedRect(tr, 2, 2);
            }
        }
    }

    // ── Zone rectangle ────────────────────────────────────────────────────────
    if (!m_zone.isEmpty()) {
        // Zone fill
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 180, 160, 18));
        p.drawRoundedRect(m_zone, 3, 3);

        // Zone border
        p.setPen(QPen(QColor(0, 200, 175, 220), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(m_zone, 3, 3);

        // ── Drag handle: right edge center ──────────────────────────────────
        QPoint em(m_zone.right(), m_zone.top() + m_zone.height() / 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 200, 175, 200));
        p.drawEllipse(em, 5, 5);
        p.setPen(QPen(QColor(0, 255, 220, 160), 1.2));
        p.drawLine(em + QPoint(-2, 0), em + QPoint(2, 0));
        p.drawLine(em + QPoint(1, -2), em + QPoint(3, 0));
        p.drawLine(em + QPoint(1,  2), em + QPoint(3, 0));

        if (!m_autoHeight) {
            // ── Drag handle: bottom edge center ─────────────────────────────
            QPoint sm(m_zone.left() + m_zone.width() / 2, m_zone.bottom());
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 200, 175, 200));
            p.drawEllipse(sm, 5, 5);
            p.setPen(QPen(QColor(0, 255, 220, 160), 1.2));
            p.drawLine(sm + QPoint(0, -2), sm + QPoint(0, 2));
            p.drawLine(sm + QPoint(-2, 1), sm + QPoint(0, 3));
            p.drawLine(sm + QPoint( 2, 1), sm + QPoint(0, 3));
        }

        // ── Corner SE handle ─────────────────────────────────────────────────
        QPoint se = m_zone.bottomRight();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 220, 190, 220));
        p.drawEllipse(se, 6, 6);
        p.setPen(QPen(QColor(255, 255, 255, 200), 1.5));
        p.drawLine(se + QPoint(-3, 0), se + QPoint(3, 0));
        p.drawLine(se + QPoint(0, -3), se + QPoint(0, 3));

        // ── Size readout ─────────────────────────────────────────────────────
        QSize real = previewToReal(m_zone);
        QString sizeStr = m_autoHeight
            ? QString("%1 px  ×  auto").arg(real.width())
            : QString("%1 × %2 px").arg(real.width()).arg(real.height());
        QFont sf("Arial", 8, QFont::Bold);
        p.setFont(sf);
        QFontMetrics fm(sf);
        QRect tr = fm.boundingRect(sizeStr).adjusted(-4, -2, 4, 2);
        tr.moveTopLeft(m_zone.topLeft() + QPoint(4, 4));
        // Clamp inside zone
        tr.moveRight(qMin(tr.right(), m_zone.right() - 2));
        tr.moveBottom(qMin(tr.bottom(), m_zone.bottom() - 2));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawRoundedRect(tr, 3, 3);
        p.setPen(QColor(180, 255, 245));
        p.drawText(tr, Qt::AlignCenter, sizeStr);
    }

    // ── Bottom label bar ─────────────────────────────────────────────────────
    QRect lb(MARGIN, height() - LABEL_H, width() - 2*MARGIN, LABEL_H);
    p.setPen(QColor(80, 110, 105));
    p.setFont(QFont("Arial", 8));
    QString hint = m_autoHeight
        ? tr("← Breite ziehen   ·   SE-Ecke: frei skalieren")
        : tr("← Breite  ↓ Höhe  ·   SE-Ecke: frei skalieren");
    p.drawText(lb, Qt::AlignVCenter | Qt::AlignHCenter, hint);
}
