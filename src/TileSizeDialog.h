#pragma once
#include <QDialog>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QScrollArea>
#include <QGridLayout>
#include <QTimer>
#include <QMouseEvent>
#include <QSize>

// ─── DragResizePreview ────────────────────────────────────────────────────────
// A canvas widget that renders a live grid of example tiles.
// The user can drag the bottom-right corner of the first tile to resize.
class DragResizePreview : public QWidget {
    Q_OBJECT
public:
    explicit DragResizePreview(QWidget* parent = nullptr);

    void setTileSize(int w, int h);
    QSize tileSize() const { return m_tileSize; }

signals:
    void tileSizeChanged(int w, int h);   // emitted continuously while dragging

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    QSize  m_tileSize  = {160, 200};
    bool   m_dragging  = false;
    QPoint m_dragStart;
    QSize  m_sizeAtDragStart;

    static constexpr int HANDLE_SIZE = 14;  // px — hit area of resize handle
    static constexpr int MIN_W = 40;
    static constexpr int MIN_H = 40;
    // No upper bound — preview reflows automatically

    QRect handleRect() const;
    int   computeCols() const;
};

// ─── TileSizeDialog ───────────────────────────────────────────────────────────
class TileSizeDialog : public QDialog {
    Q_OBJECT
public:
    explicit TileSizeDialog(int currentW, int currentH, QWidget* parent = nullptr);

    int tileWidth()  const;
    int tileHeight() const;

private slots:
    void onPreviewChanged(int w, int h);
    void onWidthSpinChanged(int v);
    void onHeightSpinChanged(int v);

private:
    DragResizePreview* m_preview;
    QSpinBox*          m_wSpin;
    QSpinBox*          m_hSpin;
    QLabel*            m_sizeLabel;
    bool               m_updating = false;   // guard against feedback loops
};
