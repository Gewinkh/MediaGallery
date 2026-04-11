#pragma once
#include <QWidget>
#include <QStringList>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QLineEdit>
#include <QCompleter>
#include <QFrame>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include "TagManager.h"

// Single tag pill widget
class TagPill : public QWidget {
    Q_OBJECT
public:
    TagPill(const QString& tag, const QColor& color, bool editable, QWidget* parent = nullptr);

signals:
    void removeRequested(const QString& tag);
    void colorChangeRequested(const QString& tag);

private:
    QString m_tag;
};

// Tag bar: shows all tags for a file, allows add/remove
class TagBar : public QWidget {
    Q_OBJECT
public:
    explicit TagBar(TagManager* mgr, QWidget* parent = nullptr);

    void setFile(const QString& fileName);
    void setEditable(bool e);
    void refresh();
    void retranslate();
    void setCompact(bool compact);

    void showTagDropdown();
    void showCategoryDropdown();
    void showTagDropdownAnchoredAt(QWidget* anchor);
    void showCategoryDropdownAnchoredAt(QWidget* anchor);

    bool isDropdownOpen() const { return m_dropPanel != nullptr || m_catPanel != nullptr; }
    void closeDropdown() {
        if (m_dropPanel) { m_dropPanel->deleteLater(); m_dropPanel = nullptr; }
        if (m_catPanel)  { m_catPanel->deleteLater();  m_catPanel  = nullptr; }
    }

signals:
    void tagsModified(const QString& fileName, const QStringList& tags);

private slots:
    void addTagFromInput();
    void removeTag(const QString& tag);
    void onTagColorRequested(const QString& tag);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    // helpers
    QFrame* buildTagPanel();
    QFrame* buildCategoryPanel();
    void    positionPanelAbove(QFrame* panel, QWidget* anchor);
    void    addCategorySection(QVBoxLayout* lay, const TagCategory& cat,
                               const QStringList& currentTags, int indent);

    TagManager*  m_tagMgr;
    QString      m_fileName;
    bool         m_editable = true;
    bool         m_compact  = false;
    QHBoxLayout* m_layout;
    QLineEdit*   m_input;
    QToolButton* m_dropBtn    = nullptr;
    QToolButton* m_catBtn     = nullptr;
    QCompleter*  m_completer;
    QList<TagPill*> m_pills;
    QFrame*      m_dropPanel  = nullptr;
    QFrame*      m_catPanel   = nullptr;
    QTimer*      m_dropHideTimer = nullptr;
    QTimer*      m_catHideTimer  = nullptr;

    void rebuildPills();
};
