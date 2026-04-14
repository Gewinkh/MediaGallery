#pragma once
#include <QWidget>
#include <QPixmap>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QTimer>
#include <QFrame>
#include <QPushButton>
#include "MediaItem.h"
#include "TagWidget.h"
#include "TagManager.h"

class MediaThumbnail : public QWidget {
    Q_OBJECT
public:
    explicit MediaThumbnail(TagManager* tagMgr, QWidget* parent = nullptr);

    void setItem(const MediaItem& item, int index);
    void setThumbnail(const QPixmap& pix);
    void setOptionsVisible(bool v);
    void setSelected(bool s);
    void setCovered(bool covered);
    int  itemIndex() const { return m_index; }
    QString filePath() const { return m_item.filePath; }

signals:
    void clicked(int index);
    void doubleClicked(int index);
    void nameChanged(int index, const QString& newName);
    void tagsModified(int index, const QStringList& tags);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    MediaItem m_item;
    int  m_index        = -1;
    bool m_hovered      = false;
    bool m_selected     = false;
    bool m_optionsVisible = true;
    bool m_covered      = false;

    QLabel*    m_imageLabel;
    QLineEdit* m_nameEdit;
    QWidget*   m_tagBarRow;
    QLabel*    m_tagsHoverLabel;   // shown in compact mode
    TagBar*    m_tagBar;
    QLabel*    m_typeOverlay;      // "▶" for video
    QLabel*    m_audioOverlay;     // "MP3", "FLAC", etc.
    QLabel*    m_dateLabel;
    QLabel*    m_sizeLabel;
    QWidget*   m_infoRow;

    // Tag tooltip panel (compact mode)
    QFrame*      m_tagTooltipPanel    = nullptr;
    QTimer*      m_tagTooltipHideTimer = nullptr;
    QPushButton* m_addHoverBtn        = nullptr;

    // Category hover label (compact mode)
    QLabel*      m_catsHoverLabel     = nullptr;

    // Category tooltip panel (compact mode)
    QFrame*      m_catTooltipPanel       = nullptr;
    QTimer*      m_catTooltipHideTimer   = nullptr;
    QPushButton* m_addCatHoverBtn        = nullptr;

    QVBoxLayout* m_layout;
    TagManager*  m_tagMgr;

    void setupUi();
    void updateCompactMode();
    void showTagTooltip();
    void hideTagTooltip();
    void scheduleHide();
    void showCategoryTooltip();
    void hideCategoryTooltip();
    void scheduleCatHide();
    bool mouseOverPanel() const;
    bool mouseOverCatPanel() const;
    static QString formatSize(qint64 bytes);

    bool m_appFilterInstalled = false;
    void installClickAwayFilter();
    void removeClickAwayFilter();
};
