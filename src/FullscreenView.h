#pragma once
#include <QWidget>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QVector>
#include <QLineEdit>
#include "MediaItem.h"
#include "VideoPlayer.h"
#include "TagWidget.h"
#include "TagManager.h"
#include "AppSettings.h"

class FullscreenView : public QWidget {
    Q_OBJECT
public:
    explicit FullscreenView(TagManager* tagMgr, QWidget* parent = nullptr);

    void setItems(const QVector<MediaItem>* items, const QVector<int>* visibleIndices);
    void showItem(int globalIndex);
    void refreshTagBar();
    void setOptionsVisible(bool v);
    bool randomNext() const { return m_randomNext; }
    void setRandomNext(bool r) { m_randomNext = r; updateRandomBtn(); }
    void retranslate();

signals:
    void backRequested();
    void nameChanged(int globalIndex, const QString& name);
    void tagsModified(int globalIndex, const QStringList& tags);
    void editDateRequested(int globalIndex);
    void applyLastTagsRequested(int globalIndex);
    void editDateWithDayFocusRequested(int globalIndex);

protected:
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private slots:
    void showPrev();
    void showNext();
    void showRandom();
    void onNameEdited();

private:
    const QVector<MediaItem>* m_items = nullptr;
    const QVector<int>* m_visibleIndices = nullptr;
    int m_currentGlobalIndex = -1;
    int m_currentVisiblePos = -1;
    bool m_randomNext = false;
    bool m_optionsVisible = true;

    // Random history for back-navigation in random mode
    QVector<int> m_randomHistory;   // list of globalIndex values visited
    int m_randomHistoryPos = -1;    // current position in history

    // Scale
    double m_zoom = 1.0;
    static constexpr double MAX_ZOOM = 10.0;
    static constexpr double MIN_ZOOM = 0.1;

    // Pan
    QPixmap m_originalPixmap;
    QPointF m_panOffset = {0.0, 0.0};
    QPoint m_panStart;
    bool m_panning = false;

    // UI elements
    QLabel* m_imageLabel;
    VideoPlayer* m_videoPlayer;
    QWidget* m_topBar;
    QWidget* m_bottomBar;
    QToolButton* m_backBtn;
    QToolButton* m_prevBtn;
    QToolButton* m_nextBtn;
    QToolButton* m_randomBtn;
    QLineEdit* m_nameEdit;
    TagBar* m_tagBar;
    QLabel* m_infoLabel;
    QToolButton* m_dateEditBtn;
    TagManager* m_tagMgr;

    QTimer* m_barHideTimer;

    void updateDisplay();
    void updateZoom();
    void updateRandomBtn();
    void applyCurrentItem();
    void showBars();
    void hideBars();
    int nextVisiblePos() const;
    int prevVisiblePos() const;
    int randomVisiblePos() const;
};
