#pragma once
#include <QWidget>
#include <QStringList>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMap>
#include <QFrame>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QPointer>
#include "TagManager.h"
#include "AppSettings.h"
#include "TagCategory.h"

// ── Tag filter mode ────────────────────────────────────────────────────────────
enum class TagFilterMode { OR, AND, NUR, INKLUSIV };

// ── Forward ───────────────────────────────────────────────────────────────────
class HoverDropdown;
class FilterTagChip;
class MediaHoverButton;

// ── FilterBar ─────────────────────────────────────────────────────────────────
class FilterBar : public QWidget {
    Q_OBJECT
public:
    explicit FilterBar(TagManager* mgr, QWidget* parent = nullptr);

    QStringList   activeTagFilter()         const;  // explicitly selected tag chips
    QStringList   activeCategoryTagFilter() const;  // all tags from active categories
    QStringList   activeCategoryIds()       const;  // IDs of all active category nodes
    bool          hasCategoryFilter()       const;  // any category active?
    bool          tagFilterAnd()            const;
    TagFilterMode tagFilterMode()           const { return m_filterMode; }
    SortField     sortField()         const;
    SortOrder     sortOrder()         const;
    bool          showImages()        const;
    bool          showVideos()        const;
    bool          showAudio()         const;

    void refreshTagList();
    void retranslate();

    // Called by FilterModeHoverButton
    void onFilterModeClicked(TagFilterMode mode);
    // Called by HoverDropdown "+" button
    void onAddCategory();

signals:
    void filterChanged();
    void sortChanged();
    void enterAddToTagModeRequested(const QString& tag);

private slots:
    void onSortFieldChanged(int idx);
    void onSortOrderToggled();
    void onTypeFilterChanged();

private:
    TagManager*    m_tagMgr;
    TagFilterMode  m_filterMode = TagFilterMode::OR;
    QSet<QString>  m_activeTags;        // union: manual + category-injected tags
    QSet<QString>  m_manualTags;        // only tags toggled explicitly by the user
    QSet<QString>  m_activeCategories;

    // Media toggle button
    MediaHoverButton* m_mediaBtn = nullptr;

    QLabel*       m_sortLbl;
    QComboBox*    m_sortField;
    QToolButton*  m_sortOrder;
    QMap<int, SortOrder> m_fieldOrder;

    HoverDropdown* m_tagsDropdown;
    HoverDropdown* m_catsDropdown;

    QWidget*      m_activeArea;
    QHBoxLayout*  m_activeLayout;
    QScrollArea*  m_activeScroll;

    void buildActiveChips();
    void updateFilterModeBtn();
    SortOrder currentFieldOrder() const;
    void      saveCurrentFieldOrder(SortOrder o);

public:
    void onTagToggled(const QString& tag, bool on);
    void onCategoryToggled(const QString& catId, bool on);
    void updateTagModeStyle() {}

    friend class HoverDropdown;
    friend class FilterTagChip;
};

// ── MediaHoverButton ──────────────────────────────────────────────────────────
// Hover button that shows a panel to toggle Images/Videos/Audio
class MediaHoverButton : public QToolButton {
    Q_OBJECT
public:
    MediaHoverButton(FilterBar* bar, QWidget* parent = nullptr);
    void updateLabel();

protected:
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    FilterBar* m_bar;
    QFrame*    m_panel = nullptr;
    QTimer*    m_hideTimer;

    void showPanel();
    void hidePanel();
    void buildPanel();
};

// ── HoverDropdown ─────────────────────────────────────────────────────────────
class HoverDropdown : public QToolButton {
    Q_OBJECT
public:
    enum class Mode { Tags, Categories };

    HoverDropdown(const QString& label, Mode mode, FilterBar* bar, QWidget* parent = nullptr);
    void rebuild();

protected:
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e)      override;
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;

private slots:
    void showPanel();
    void hidePanel();

private:
    FilterBar* m_bar;
    Mode       m_mode;
    QString    m_label;
    QFrame*    m_panel = nullptr;
    QTimer*    m_hideTimer;
    QTimer*    m_dragOpenTimer;

    void buildPanel();
    void populateTagsPanel(QVBoxLayout* lay);
    void populateCatsPanel(QVBoxLayout* lay);
    void addCatSection(QVBoxLayout* lay, const TagCategory& cat, int depth);
    void applyStyle();
};

// ── FilterModeHoverButton ──────────────────────────────────────────────────────
// Hover button that shows all 4 filter mode options
class FilterModeHoverButton : public QToolButton {
    Q_OBJECT
public:
    FilterModeHoverButton(FilterBar* bar, QWidget* parent = nullptr);
    void updateStyle(TagFilterMode mode);
    void setVisible2(bool v);  // wraps setVisible

protected:
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    FilterBar* m_bar;
    QFrame*    m_panel = nullptr;
    QTimer*    m_hideTimer;

    void showPanel();
    void hidePanel();
    void buildPanel();
};

// ── FilterTagChip ─────────────────────────────────────────────────────────────
class FilterTagChip : public QPushButton {
    Q_OBJECT
public:
    FilterTagChip(const QString& tag, FilterBar* bar, QWidget* parent = nullptr);
    void applyStyle();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e)  override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private slots:
    void showInfoPanel();
    void hideInfoPanel();

private:
    QString    m_tag;
    FilterBar* m_bar;
    QPoint     m_dragStart;

    // Hover info panel
    QTimer*  m_hoverTimer  = nullptr;
    QFrame*  m_infoPanel   = nullptr;
};
