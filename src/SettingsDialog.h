#pragma once
#include <QDialog>
#include <QComboBox>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QTabWidget>
#include "ColorPickerButton.h"
#include "AppSettings.h"
#include "TagManager.h"

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(TagManager* tagMgr, QWidget* parent = nullptr);

signals:
    void settingsChanged();

private:
    TagManager* m_tagMgr;

    QTabWidget* m_tabs;
    QComboBox*  m_langBox;
    QRadioButton* m_videoNative;
    QRadioButton* m_videoExternal;

    // Tag tab
    QWidget*     m_tagTab;
    QVBoxLayout* m_tagListLayout;

    // Category tab
    QWidget*     m_catTab;
    QVBoxLayout* m_catTreeLayout;

    void applySettings();
    void buildTagList();
    void buildCategoryTree();

    QWidget* buildGeneralTab();
    QWidget* buildTagTab();
    QWidget* buildCategoryTab();
    QWidget* buildDesignTab();

    // Recursive helper: builds one category block with all edit controls
    void addCategoryBlock(QVBoxLayout* lay, TagCategory& cat, int depth);
};
