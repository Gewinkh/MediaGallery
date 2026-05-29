#include "TileSizeDialog.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QCursor>
#include <QApplication>
#include <QSizeGrip>

// ═════════════════════════════════════════════════════════════════════════════
// DragResizePreview
// ═════════════════════════════════════════════════════════════════════════════

DragResizePreview::DragResizePreview(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void DragResizePreview::setTileSize(int w, int h) {
    m_tileSize = QSize(qMax(MIN_W, w), qMax(MIN_H, h));
    update();
}

QRect DragResizePreview::handleRect() const {
    // Bottom-right corner of the first tile — origin = top-left content area
    // Margins and spacing must match GalleryView exactly: margin=12, spacing=8
    const int margin = 12;
    int tx = margin + m_tileSize.width();
    int ty = margin + m_tileSize.height();
    return QRect(tx - HANDLE_SIZE, ty - HANDLE_SIZE, HANDLE_SIZE * 2, HANDLE_SIZE * 2);
}

int DragResizePreview::computeCols() const {
    // Must match GalleryView::rebuildGrid(): margin=12 each side, spacing=8
    int avail = width() - 24;   // 12px margin each side
    return qMax(1, (avail + 8) / (m_tileSize.width() + 8));
}

void DragResizePreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor(14, 22, 28));

    // Match GalleryView exactly: contentsMargins=12, spacing=8
    const int margin  = 12;
    const int spacing = 8;
    const int cols    = computeCols();
    const int rows    = qMax(1, (height() - 2 * margin + spacing) / (m_tileSize.height() + spacing));

    // ── Draw example tiles ────────────────────────────────────────────────────
    for (int r = 0; r < rows + 1; ++r) {
        for (int c = 0; c < cols; ++c) {
            int x = margin + c * (m_tileSize.width()  + spacing);
            int y = margin + r * (m_tileSize.height() + spacing);

            // Clip to widget
            if (x + m_tileSize.width()  > width()  - margin) continue;
            if (y + m_tileSize.height() > height() - margin) continue;

            QRect tileRect(x, y, m_tileSize.width(), m_tileSize.height());
            bool  isFirst = (r == 0 && c == 0);

            // Tile background
            QColor bg = isFirst ? QColor(0, 140, 120) : QColor(22, 36, 44);
            QPainterPath path;
            path.addRoundedRect(tileRect, 6, 6);
            p.fillPath(path, bg);

            // Border
            p.setPen(QPen(isFirst ? QColor(0, 200, 175, 200) : QColor(40, 65, 78), 1.2));
            p.drawPath(path);

            // Thumbnail placeholder (checkerboard or gradient)
            if (m_tileSize.height() > 60) {
                QRect imgRect = tileRect.adjusted(6, 6, -6, isFirst ? -32 : -32);
                if (imgRect.isValid()) {
                    QLinearGradient grad(imgRect.topLeft(), imgRect.bottomRight());
                    grad.setColorAt(0, isFirst ? QColor(0, 90, 80) : QColor(18, 30, 38));
                    grad.setColorAt(1, isFirst ? QColor(0, 60, 55) : QColor(10, 20, 28));
                    QPainterPath imgPath;
                    imgPath.addRoundedRect(imgRect, 4, 4);
                    p.fillPath(imgPath, grad);

                    // Fake image icon
                    p.setPen(isFirst ? QColor(0, 220, 190, 160) : QColor(50, 80, 90, 120));
                    p.setFont(QFont("Arial", qMax(8, m_tileSize.width() / 12)));
                    p.drawText(imgRect, Qt::AlignCenter, "🖼");
                }
            }

            // Label bar at bottom
            if (m_tileSize.height() > 40) {
                QRect lblRect(tileRect.left() + 4, tileRect.bottom() - 28,
                              tileRect.width() - 8, 22);
                if (lblRect.isValid()) {
                    p.setPen(isFirst ? QColor(200, 255, 245) : QColor(130, 160, 155));
                    p.setFont(QFont("Arial", qMax(7, m_tileSize.width() / 16)));
                    p.drawText(lblRect, Qt::AlignVCenter | Qt::AlignLeft,
                               isFirst ? "example.jpg" : "media.jpg");
                }
            }
        }
    }

    // ── Resize handle on first tile ───────────────────────────────────────────
    QRect hr = handleRect();
    p.setPen(Qt::NoPen);

    // Shadow
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawEllipse(hr.translated(1, 1));

    // Main handle circle
    QRadialGradient hgrad(hr.center(), hr.width() / 2.0);
    hgrad.setColorAt(0.0, m_dragging ? QColor(0, 255, 215) : QColor(0, 200, 170));
    hgrad.setColorAt(1.0, m_dragging ? QColor(0, 180, 150) : QColor(0, 140, 115));
    p.setBrush(hgrad);
    p.setPen(QPen(QColor(0, 255, 220, 160), 1.5));
    p.drawEllipse(hr);

    // Arrow indicator
    p.setPen(QPen(QColor(255, 255, 255, 220), 1.5, Qt::SolidLine, Qt::RoundCap));
    QPoint c = hr.center();
    p.drawLine(c + QPoint(-3, 0), c + QPoint(3, 0));
    p.drawLine(c + QPoint(0, -3), c + QPoint(0, 3));
    p.drawLine(c + QPoint(1, 1), c + QPoint(4, 4));
    p.drawLine(c + QPoint(4, 4), c + QPoint(4, 2));
    p.drawLine(c + QPoint(4, 4), c + QPoint(2, 4));

    // Size readout on first tile
    QRect sizeReadout(margin + 4, margin + 4, m_tileSize.width() - 8, 20);
    if (sizeReadout.isValid()) {
        p.setPen(QColor(200, 255, 245));
        p.setFont(QFont("Arial", 9, QFont::Bold));
        p.drawText(sizeReadout, Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1 × %2 px").arg(m_tileSize.width()).arg(m_tileSize.height()));
    }

    // Instruction hint
    p.setPen(QColor(80, 110, 105));
    p.setFont(QFont("Arial", 9));
    p.drawText(rect().adjusted(0, 0, 0, -4),
               Qt::AlignBottom | Qt::AlignHCenter,
               tr("Drag the ● corner to resize  ·  Ctrl+Scroll or Ctrl+±  in the gallery"));
}

void DragResizePreview::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && handleRect().contains(e->pos())) {
        m_dragging        = true;
        m_dragStart       = e->pos();
        m_sizeAtDragStart = m_tileSize;
        setCursor(Qt::SizeFDiagCursor);
        e->accept();
    }
}

void DragResizePreview::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging) {
        QPoint delta = e->pos() - m_dragStart;
        int newW = qMax(MIN_W, m_sizeAtDragStart.width()  + delta.x());
        int newH = qMax(MIN_H, m_sizeAtDragStart.height() + delta.y());
        if (QSize(newW, newH) != m_tileSize) {
            m_tileSize = QSize(newW, newH);
            update();
            emit tileSizeChanged(newW, newH);
        }
        e->accept();
        return;
    }
    // Change cursor near handle
    setCursor(handleRect().contains(e->pos()) ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
}

void DragResizePreview::mouseReleaseEvent(QMouseEvent* e) {
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
        update();
        e->accept();
    }
}

void DragResizePreview::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    update();
}

// ═════════════════════════════════════════════════════════════════════════════
// TileSizeDialog
// ═════════════════════════════════════════════════════════════════════════════

TileSizeDialog::TileSizeDialog(int currentW, int currentH, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tile Size"));
    setMinimumSize(480, 420);
    resize(600, 520);

    // ── Dark stylesheet ───────────────────────────────────────────────────────
    setStyleSheet(
        "QDialog { background: #0e161c; color: #c8dbd5; }"
        "QLabel  { color: #c8dbd5; }"
        "QSpinBox { background: #182028; color: #c8dbd5; border: 1px solid #284040;"
        "           border-radius: 4px; padding: 3px 6px; }"
        "QSpinBox::up-button, QSpinBox::down-button { background: #1e2e38; border: none; width: 18px; }"
        "QPushButton { background: rgba(0,180,160,0.18); border: 1px solid rgba(0,180,160,0.4);"
        "              border-radius: 6px; color: #00c8b4; padding: 6px 20px; }"
        "QPushButton:hover { background: rgba(0,180,160,0.35); }"
        "QPushButton[default=true] { border-color: rgba(0,200,180,0.7); }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // Title label
    auto* titleLbl = new QLabel(tr("Drag the corner handle to set your preferred tile size."), this);
    titleLbl->setStyleSheet("color: #78a090; font-size: 12px;");
    titleLbl->setWordWrap(true);
    root->addWidget(titleLbl);

    // Preview
    m_preview = new DragResizePreview(this);
    m_preview->setTileSize(currentW, currentH);
    root->addWidget(m_preview, 1);

    // Numeric controls
    auto* ctrlRow = new QHBoxLayout;
    ctrlRow->setSpacing(16);

    auto* wLabel = new QLabel(tr("Width:"),  this);
    m_wSpin = new QSpinBox(this);
    m_wSpin->setRange(40, 4096);
    m_wSpin->setValue(currentW);
    m_wSpin->setSuffix(" px");

    auto* hLabel = new QLabel(tr("Height:"), this);
    m_hSpin = new QSpinBox(this);
    m_hSpin->setRange(40, 4096);
    m_hSpin->setValue(currentH);
    m_hSpin->setSuffix(" px");

    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setStyleSheet("color: #00c8b4; font-weight: bold;");

    ctrlRow->addWidget(wLabel);
    ctrlRow->addWidget(m_wSpin);
    ctrlRow->addSpacing(8);
    ctrlRow->addWidget(hLabel);
    ctrlRow->addWidget(m_hSpin);
    ctrlRow->addStretch(1);
    ctrlRow->addWidget(m_sizeLabel);
    root->addLayout(ctrlRow);

    // Buttons
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btns);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_preview, &DragResizePreview::tileSizeChanged,
            this, &TileSizeDialog::onPreviewChanged);
    connect(m_wSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TileSizeDialog::onWidthSpinChanged);
    connect(m_hSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TileSizeDialog::onHeightSpinChanged);

    onPreviewChanged(currentW, currentH);   // initialise label
}

int TileSizeDialog::tileWidth()  const { return m_wSpin->value(); }
int TileSizeDialog::tileHeight() const { return m_hSpin->value(); }

void TileSizeDialog::onPreviewChanged(int w, int h) {
    if (m_updating) return;
    m_updating = true;
    m_wSpin->setValue(w);
    m_hSpin->setValue(h);
    m_sizeLabel->setText(QString("%1 × %2").arg(w).arg(h));
    m_updating = false;
}

void TileSizeDialog::onWidthSpinChanged(int v) {
    if (m_updating) return;
    m_updating = true;
    m_preview->setTileSize(v, m_hSpin->value());
    m_sizeLabel->setText(QString("%1 × %2").arg(v).arg(m_hSpin->value()));
    m_updating = false;
}

void TileSizeDialog::onHeightSpinChanged(int v) {
    if (m_updating) return;
    m_updating = true;
    m_preview->setTileSize(m_wSpin->value(), v);
    m_sizeLabel->setText(QString("%1 × %2").arg(m_wSpin->value()).arg(v));
    m_updating = false;
}