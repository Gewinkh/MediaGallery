#pragma once
#include <QWidget>
#include <QSize>
#include <QRect>
#include <QTimer>

// ─── ManualZonePreview ────────────────────────────────────────────────────────
// Scaled preview of the MainWindow viewport.
// Shows an editable rectangle (the manual tile zone) with drag/resize handles.
// All coordinates emitted via zoneChanged() are in real MainWindow pixels.
//
// Usage:
//   auto* p = new ManualZonePreview(mainWindowSize, this);
//   p->setZone(manualAreaWidth, manualAreaHeight);   // real px
//   connect(p, &ManualZonePreview::zoneChanged, this, [=](int w, int h){ ... });
class ManualZonePreview : public QWidget {
    Q_OBJECT
public:
    explicit ManualZonePreview(QSize mainWindowSize, QWidget* parent = nullptr);

    // Set the manual zone size in real MainWindow pixels.
    // h == 0  →  zone height = full viewport height (Auto)
    void setZone(int realW, int realH);

    QSize zoneReal() const; // current zone in real px (h==0 = auto)

signals:
    // Emitted (throttled) when the user drags/resizes the zone.
    // w / h are in real MainWindow pixels. h==0 means "auto".
    void zoneChanged(int realW, int realH);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    // ── geometry ──────────────────────────────────────────────────────────
    QSize  m_mainSize;      // real MainWindow size used for scaling
    QRect  m_zone;          // zone rect in PREVIEW coordinates
    bool   m_autoHeight = false; // if true, zone height tracks preview height

    // ── interaction ───────────────────────────────────────────────────────
    enum class HitZone { None, Move, ResizeE, ResizeS, ResizeSE };
    HitZone m_drag    = HitZone::None;
    QPoint  m_dragOrig;
    QRect   m_zoneAtDragStart;

    // ── emit throttle ─────────────────────────────────────────────────────
    QTimer* m_emitTimer = nullptr;

    static constexpr int HANDLE   = 10;   // px — resize handle hit area
    static constexpr int MIN_ZONE = 40;   // px in preview coords

    // ── helpers ───────────────────────────────────────────────────────────
    QRect  previewArea()  const;          // usable area inside widget (excludes label row)
    double scaleFactor()  const;          // previewWidth / mainWindowWidth
    QRect  realToPreview(int rw, int rh) const;
    QSize  previewToReal(const QRect& pr) const;
    HitZone hitTest(QPoint p) const;
    void   clampZone();
    void   scheduleEmit();
};
