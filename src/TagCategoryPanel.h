#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QFrame>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSet>
#include <QPointer>
#include "TagCategory.h"
#include "TagManager.h"

inline const char* kTagDragMime = "application/x-medgallery-tag";

class CategoryNode;
class DraggableChip;

// ── TagCategoryPanel ──────────────────────────────────────────────────────────
class TagCategoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit TagCategoryPanel(TagManager* mgr, QWidget* parent = nullptr);

    QStringList activeTagFilter() const;
    bool        tagFilterAnd()    const { return m_andMode; }
    void        setTagFilterAnd(bool v);
    void        refresh();
    TagManager* mgr() const { return m_mgr; }

    // Group mode: tag-based right-click media selection
    void enterGroupMode(const QString& tag);
    void exitGroupMode();

    // Add-to-Tag mode: left-click on media toggles tag membership (visual border for tagged items)
    void enterAddToTagMode(const QString& tag);
    void exitAddToTagMode();
    bool isAddToTagMode() const { return m_addToTagMode; }
    QString addToTagModeTag() const { return m_addToTagModeTag; }

signals:
    void filterChanged();
    void groupModeChanged(bool active, const QString& tag);
    void addToTagModeChanged(bool active, const QString& tag);

private slots:
    void onAddCategory();
    void onAndOrToggled();

private:
    TagManager*   m_mgr;
    bool          m_andMode       = false;
    bool          m_groupMode     = false;
    QString       m_groupModeTag;
    bool          m_addToTagMode  = false;
    QString       m_addToTagModeTag;
    QSet<QString> m_activeTags;
    QWidget*      m_inner;
    QVBoxLayout*  m_innerLay;
    QToolButton*  m_andOrBtn;

    void rebuildNodes();
    void addCategoryNode(TagCategory& cat, QVBoxLayout* lay, int depth = 0);
    void onTagToggled(const QString& tag, bool on);

    friend class CategoryNode;
    friend class DraggableChip;
};

// ── CategoryNode ──────────────────────────────────────────────────────────────
class CategoryNode : public QFrame {
    Q_OBJECT
public:
    CategoryNode(TagCategory& cat, TagCategoryPanel* panel,
                 int depth, QWidget* parent = nullptr);
    void refreshChips();

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent*   e) override;
    void dropEvent(QDropEvent*           e) override;

private slots:
    void onToggleCollapse();
    void onContextMenu(const QPoint& pos);
    void onRenameCategory();
    void onDeleteCategory();
    void onSetUniformColor();
    void onClearUniformColor();
    void onAddSubcategory();
    void onAddTag();

private:
    TagCategory&      m_cat;
    TagCategoryPanel* m_panel;
    int               m_depth;
    QWidget*          m_header;
    QLabel*           m_titleLbl;
    QToolButton*      m_collapseBtn;
    QWidget*          m_body;
    QVBoxLayout*      m_bodyLay;
    QWidget*          m_chipArea;
    QHBoxLayout*      m_chipLay;
    QVBoxLayout*      m_childrenLay;
    bool              m_collapsed = false;

    void buildChips();
    void buildChildren();
    void updateHeaderStyle();
};

// ── DraggableChip ─────────────────────────────────────────────────────────────
class DraggableChip : public QPushButton {
    Q_OBJECT
public:
    DraggableChip(const QString& tag, const QString& catId,
                  TagCategoryPanel* panel, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent*       e) override;
    void mouseMoveEvent(QMouseEvent*        e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    QString           m_tag;
    QString           m_catId;
    TagCategoryPanel* m_panel;
    QPoint            m_dragStart;
};
