#include "SettingsDialog.h"
#include "Strings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QScrollArea>
#include <QFrame>
#include <QCheckBox>
#include <QMessageBox>
#include <QColorDialog>
#include <QInputDialog>
#include <QMenu>
#include <QFileDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QButtonGroup>
#include <QRadioButton>
#include <functional>

static const char* DIALOG_STYLE =
    "QDialog { background: #0d1a20; }"
    "QTabWidget::pane { border: 1px solid rgba(40,60,70,0.8); background: #0d1a20; border-radius: 6px; }"
    "QTabBar::tab { background: rgba(255,255,255,0.05); color: #789891; padding: 6px 18px;"
    "  border: 1px solid rgba(40,60,70,0.5); border-bottom: none; border-radius: 4px 4px 0 0; }"
    "QTabBar::tab:selected { background: rgba(0,180,160,0.15); color: #00c8b4;"
    "  border-color: rgba(0,180,160,0.4); }"
    "QGroupBox { border: 1px solid rgba(40,60,70,0.8); border-radius: 8px;"
    "  margin-top: 14px; color: #789891; font-size: 11px; padding: 10px; }"
    "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;"
    "  padding: 0 8px; left: 12px; }"
    "QLabel { color: #c8dbd5; font-size: 13px; }"
    "QRadioButton { color: #c8dbd5; spacing: 6px; }"
    "QRadioButton::indicator { width: 14px; height: 14px; border-radius: 7px;"
    "  border: 1px solid rgba(40,60,70,0.9); background: rgba(255,255,255,0.07); }"
    "QRadioButton::indicator:checked { background: #00b4a0; border-color: #00b4a0; }"
    "QPushButton { background: rgba(0,180,160,0.2); border: 1px solid rgba(0,180,160,0.4);"
    "  border-radius: 6px; color: #00c8b4; padding: 5px 12px; }"
    "QPushButton:hover { background: rgba(0,180,160,0.4); }"
    "QScrollArea { background: transparent; border: none; }";

static QString menuStyle() {
    return "QMenu { background: #1a2830; border: 1px solid rgba(255,255,255,0.15);"
           "border-radius: 6px; color: white; padding: 4px; }"
           "QMenu::item { padding: 5px 18px; border-radius: 4px; }"
           "QMenu::item:selected { background: rgba(0,180,160,0.3); }"
           "QMenu::separator { background: rgba(255,255,255,0.1); height: 1px; margin: 3px 8px; }";
}

// ─────────────────────────────────────────────────────────────────────────────
SettingsDialog::SettingsDialog(TagManager* tagMgr, QWidget* parent)
    : QDialog(parent), m_tagMgr(tagMgr)
{
    setWindowTitle(Strings::get(StringKey::SettingsTitle));
    setMinimumSize(520, 520);
    setStyleSheet(DIALOG_STYLE);

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setSpacing(10);
    mainLay->setContentsMargins(16, 16, 16, 16);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildGeneralTab(),   Strings::get(StringKey::SettingsTabGeneral));
    m_tabs->addTab(buildTagTab(),       Strings::get(StringKey::SettingsTabTags));
    m_tabs->addTab(buildCategoryTab(),  Strings::get(StringKey::SettingsTabCategories));
    m_tabs->addTab(buildConverterTab(), Strings::get(StringKey::ConverterTabTitle));
    m_tabs->addTab(buildDesignTab(),    Strings::get(StringKey::SettingsTabDesign));
    mainLay->addWidget(m_tabs, 1);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(Strings::get(StringKey::SettingsOk));
    btnBox->button(QDialogButtonBox::Cancel)->setText(Strings::get(StringKey::SettingsCancel));
    // OK schließt nur den Dialog — Einstellungen werden bereits live angewendet
    connect(btnBox, &QDialogButtonBox::accepted, this, [this]() {
        applySettings(); // Speichert Sprache + Video-Modus (nicht-live Einstellungen)
        accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLay->addWidget(btnBox);

    // Refresh category tab when data changes — QueuedConnection so that the
    // widget that triggered the signal is fully off the call stack before
    // buildCategoryTree() deletes and recreates all child widgets.
    connect(tagMgr, &TagManager::tagsChanged,       this, &SettingsDialog::buildCategoryTree, Qt::QueuedConnection);
    connect(tagMgr, &TagManager::categoriesChanged, this, &SettingsDialog::buildCategoryTree, Qt::QueuedConnection);
    connect(tagMgr, &TagManager::tagsChanged,       this, &SettingsDialog::buildTagList);
}

// ─────────────────────────────────────────────────────────────────────────────
//  General tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildGeneralTab() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setSpacing(10);
    lay->setContentsMargins(8, 8, 8, 8);

    auto* langGroup = new QGroupBox(Strings::get(StringKey::SettingsLanguageGroup), page);
    auto* langLay = new QHBoxLayout(langGroup);
    m_langBox = new QComboBox(langGroup);
    m_langBox->addItem("Deutsch", (int)Language::German);
    m_langBox->addItem("English", (int)Language::English);
    m_langBox->setCurrentIndex((int)AppSettings::instance().language());
    m_langBox->setStyleSheet(
        "QComboBox { background: rgba(255,255,255,0.07); border: 1px solid rgba(40,60,70,0.9);"
        "border-radius: 6px; color: #dcebd8; padding: 3px 8px; min-width: 120px; }"
        "QComboBox QAbstractItemView { background: #111e24; color: #dcebd8;"
        "selection-background-color: #00b4a0; }");
    langLay->addWidget(new QLabel(Strings::get(StringKey::SettingsLanguageLabel), langGroup));
    langLay->addWidget(m_langBox);
    langLay->addStretch();
    lay->addWidget(langGroup);

    auto* vidGroup = new QGroupBox(Strings::get(StringKey::SettingsVideoGroup), page);
    auto* vidLay = new QVBoxLayout(vidGroup);
    m_videoNative   = new QRadioButton(Strings::get(StringKey::SettingsVideoNative), vidGroup);
    m_videoExternal = new QRadioButton(Strings::get(StringKey::SettingsVideoExternal), vidGroup);
    bool isNative = AppSettings::instance().videoPlayback() == VideoPlayback::Native;
    m_videoNative->setChecked(isNative);
    m_videoExternal->setChecked(!isNative);
    vidLay->addWidget(m_videoNative);
    vidLay->addWidget(m_videoExternal);
    lay->addWidget(vidGroup);
    lay->addStretch();
    return page;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tag tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildTagTab() {
    m_tagTab = new QWidget(this);
    auto* outerLay = new QVBoxLayout(m_tagTab);
    outerLay->setContentsMargins(8, 8, 8, 8);
    outerLay->setSpacing(6);

    // Hint + "Neuer Tag" button in a row (same pattern as category tab)
    auto* topRow = new QHBoxLayout;
    auto* hint = new QLabel(Strings::get(StringKey::SettingsTagsHint), m_tagTab);
    hint->setStyleSheet("color: rgba(150,180,175,0.7); font-size: 11px;");
    hint->setWordWrap(true);
    topRow->addWidget(hint, 1);

    auto* addTagBtn = new QPushButton(Strings::get(StringKey::SettingsTagAdd), m_tagTab);
    addTagBtn->setFixedHeight(28);
    addTagBtn->setStyleSheet(
        "QPushButton { background: rgba(0,180,160,0.2); border: 1px solid rgba(0,180,160,0.5);"
        "border-radius: 6px; color: #00c8b4; font-size: 12px; font-weight: bold; padding: 2px 12px; }"
        "QPushButton:hover { background: rgba(0,180,160,0.4); }");
    connect(addTagBtn, &QPushButton::clicked, this, [this] {
        // Step 1: ask for tag name
        bool ok;
        QString name = QInputDialog::getText(this,
            Strings::get(StringKey::SettingsTagNewTitle),
            Strings::get(StringKey::SettingsTagNewLabel),
            QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        name = name.trimmed();

        // Check for duplicate
        if (m_tagMgr->allTags().contains(name)) {
            QMessageBox::warning(this,
                Strings::get(StringKey::SettingsTagNewTitle),
                QString("\"%1\" existiert bereits.").arg(name));
            return;
        }

        // Step 2: pick color (pre-seeded with a pleasant teal)
        QColor color = QColorDialog::getColor(QColor(0, 180, 160), this,
            Strings::get(StringKey::SettingsTagColorTitle));
        if (!color.isValid()) color = QColor(0, 180, 160);

        m_tagMgr->createTag(name, color);
    });
    topRow->addWidget(addTagBtn);
    outerLay->addLayout(topRow);

    auto* sep = new QFrame(m_tagTab);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: rgba(40,60,70,0.6);");
    outerLay->addWidget(sep);

    auto* scroll = new QScrollArea(m_tagTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollBar:vertical { width: 4px; background: transparent; }"
                          "QScrollBar::handle:vertical { background: rgba(255,255,255,0.15); border-radius: 2px; }");

    auto* listWidget = new QWidget(scroll);
    m_tagListLayout = new QVBoxLayout(listWidget);
    m_tagListLayout->setSpacing(4);
    m_tagListLayout->setContentsMargins(2, 2, 2, 2);
    m_tagListLayout->setAlignment(Qt::AlignTop);
    scroll->setWidget(listWidget);
    outerLay->addWidget(scroll, 1);

    buildTagList();
    return m_tagTab;
}

void SettingsDialog::buildTagList() {
    if (!m_tagListLayout) return;
    QLayoutItem* child;
    while ((child = m_tagListLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    QStringList tags = m_tagMgr->allTags();
    if (tags.isEmpty()) {
        auto* empty = new QLabel(Strings::get(StringKey::SettingsTagEmpty), m_tagTab);
        empty->setStyleSheet("color: rgba(150,170,165,0.6); font-size: 12px; padding: 8px;");
        empty->setAlignment(Qt::AlignCenter);
        m_tagListLayout->addWidget(empty);
        return;
    }

    for (const QString& tag : std::as_const(tags)) {
        auto* row = new QWidget(m_tagTab);
        auto* rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(4, 2, 4, 2);
        rowLay->setSpacing(8);

        QColor c = m_tagMgr->tagColor(tag);

        // Color picker
        auto* colorBtn = new ColorPickerButton(c, row);
        colorBtn->setFixedSize(28, 28);
        colorBtn->setToolTip(Strings::get(StringKey::SettingsTagChangeColor));
        connect(colorBtn, &ColorPickerButton::colorChanged, this, [this, tag](const QColor& nc) {
            m_tagMgr->setTagColor(tag, nc);
        });
        rowLay->addWidget(colorBtn);

        // Tag name label
        auto* nameLabel = new QLabel(tag, row);
        nameLabel->setStyleSheet(QString(
            "QLabel { background: rgba(%1,%2,%3,0.2); border: 1px solid %4;"
            "border-radius: 8px; color: %5; font-size: 12px; font-weight: 600; padding: 2px 10px; }")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.name()).arg(c.name()));
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        rowLay->addWidget(nameLabel, 1);

        // Rename button
        auto* renBtn = new QPushButton("✏", row);
        renBtn->setFixedSize(26, 26);
        renBtn->setToolTip(Strings::get(StringKey::SettingsTagRenameTitle));
        renBtn->setStyleSheet(
            "QPushButton { background: rgba(100,160,255,0.15); border: 1px solid rgba(100,160,255,0.35);"
            "border-radius: 6px; color: #80a8ff; font-size: 12px; padding: 0; }"
            "QPushButton:hover { background: rgba(100,160,255,0.4); color: white; }");
        connect(renBtn, &QPushButton::clicked, this, [this, tag, nameLabel]() {
            bool ok;
            QString n = QInputDialog::getText(this,
                Strings::get(StringKey::SettingsTagRenameTitle),
                Strings::get(StringKey::SettingsTagRenameLabel),
                QLineEdit::Normal, tag, &ok);
            if (!ok || n.trimmed().isEmpty()) return;
            m_tagMgr->renameTag(tag, n.trimmed());
            emit settingsChanged();
            // buildTagList will be called via tagsChanged signal
        });
        rowLay->addWidget(renBtn);

        // "Zu Kategorie" button
        auto* catBtn = new QPushButton("📂", row);
        catBtn->setFixedSize(26, 26);
        catBtn->setToolTip(Strings::get(StringKey::CatPanelAddTag));
        catBtn->setStyleSheet(
            "QPushButton { background: rgba(0,180,160,0.15); border: 1px solid rgba(0,180,160,0.35);"
            "border-radius: 6px; color: #00c8b4; font-size: 12px; padding: 0; }"
            "QPushButton:hover { background: rgba(0,180,160,0.4); }");
        connect(catBtn, &QPushButton::clicked, this, [this, tag, catBtn]() {
            QMenu menu(catBtn);
            menu.setStyleSheet(menuStyle());

            std::function<void(QMenu*, const QList<TagCategory>&)> populate;
            populate = [&](QMenu* parent, const QList<TagCategory>& cats) {
                for (const TagCategory& cat : cats) {
                    bool alreadyIn = cat.tags.contains(tag);
                    if (!cat.children.isEmpty()) {
                        QMenu* sub = parent->addMenu(cat.name);
                        sub->setStyleSheet(menuStyle());
                        QAction* selfAct = sub->addAction(
                            alreadyIn ? Strings::get(StringKey::FilterTagAlreadyIn, cat.name)
                                      : Strings::get(StringKey::FilterTagAddTo, cat.name));
                        selfAct->setEnabled(!alreadyIn);
                        QString catId = cat.id;
                        connect(selfAct, &QAction::triggered, this, [this, catId, tag]{
                            m_tagMgr->addTagToCategory(catId, tag);
                        });
                        sub->addSeparator();
                        populate(sub, cat.children);
                    } else {
                        QAction* act = parent->addAction(
                            alreadyIn ? Strings::get(StringKey::FilterTagAlreadyIn, cat.name)
                                      : Strings::get(StringKey::FilterTagAddTo, cat.name));
                        act->setEnabled(!alreadyIn);
                        QString catId = cat.id;
                        connect(act, &QAction::triggered, this, [this, catId, tag]{
                            m_tagMgr->addTagToCategory(catId, tag);
                        });
                    }
                }
            };
            populate(&menu, m_tagMgr->categories());
            if (menu.isEmpty())
                menu.addAction("(keine Kategorien vorhanden)")->setEnabled(false);
            menu.exec(catBtn->mapToGlobal(QPoint(0, catBtn->height())));
        });
        rowLay->addWidget(catBtn);

        // Delete button
        auto* delBtn = new QPushButton("✕", row);
        delBtn->setFixedSize(26, 26);
        delBtn->setToolTip(Strings::get(StringKey::SettingsTagDelete));
        delBtn->setStyleSheet(
            "QPushButton { background: rgba(200,60,60,0.2); border: 1px solid rgba(200,60,60,0.4);"
            "border-radius: 6px; color: #e05050; font-size: 12px; font-weight: bold; padding: 0; }"
            "QPushButton:hover { background: rgba(200,60,60,0.5); color: white; }");
        connect(delBtn, &QPushButton::clicked, this, [this, tag]() {
            auto reply = QMessageBox::question(this,
                Strings::get(StringKey::SettingsTagDeleteTitle),
                Strings::get(StringKey::SettingsTagDeleteMsg, tag),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                m_tagMgr->deleteTag(tag);
                emit settingsChanged();
            }
        });
        rowLay->addWidget(delBtn);

        row->setStyleSheet("QWidget { background: rgba(255,255,255,0.03); border-radius: 6px; }"
                           "QWidget:hover { background: rgba(255,255,255,0.06); }");
        m_tagListLayout->addWidget(row);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Category tab
// ─────────────────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildCategoryTab() {
    m_catTab = new QWidget(this);
    auto* outerLay = new QVBoxLayout(m_catTab);
    outerLay->setContentsMargins(8, 8, 8, 8);
    outerLay->setSpacing(6);

    // Hint + "Neue Kategorie" button in a row
    auto* topRow = new QHBoxLayout;
    auto* hint = new QLabel(Strings::get(StringKey::SettingsCatHint), m_catTab);
    hint->setStyleSheet("color: rgba(150,180,175,0.7); font-size: 11px;");
    topRow->addWidget(hint, 1);

    auto* addCatBtn = new QPushButton(Strings::get(StringKey::SettingsCatAdd), m_catTab);
    addCatBtn->setFixedHeight(28);
    addCatBtn->setStyleSheet(
        "QPushButton { background: rgba(0,180,160,0.2); border: 1px solid rgba(0,180,160,0.5);"
        "border-radius: 6px; color: #00c8b4; font-size: 12px; font-weight: bold; padding: 2px 12px; }"
        "QPushButton:hover { background: rgba(0,180,160,0.4); }");
    connect(addCatBtn, &QPushButton::clicked, this, [this]{
        bool ok;
        QString n = QInputDialog::getText(this,
            Strings::get(StringKey::SettingsCatNewTitle),
            Strings::get(StringKey::SettingsCatNewLabel),
            QLineEdit::Normal, "", &ok);
        if (!ok || n.trimmed().isEmpty()) return;
        TagCategory cat = TagCategory::create(n.trimmed());
        QColor c = QColorDialog::getColor(QColor(0, 180, 160), this,
            Strings::get(StringKey::SettingsCatColorTitle));
        if (c.isValid()) cat.color = c;
        m_tagMgr->addCategory(cat);
    });
    topRow->addWidget(addCatBtn);
    outerLay->addLayout(topRow);

    auto* sep = new QFrame(m_catTab);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: rgba(40,60,70,0.6);");
    outerLay->addWidget(sep);

    auto* scroll = new QScrollArea(m_catTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollBar:vertical { width: 4px; background: transparent; }"
                          "QScrollBar::handle:vertical { background: rgba(255,255,255,0.15); border-radius: 2px; }");

    auto* treeWidget = new QWidget(scroll);
    m_catTreeLayout = new QVBoxLayout(treeWidget);
    m_catTreeLayout->setSpacing(4);
    m_catTreeLayout->setContentsMargins(2, 2, 2, 2);
    m_catTreeLayout->setAlignment(Qt::AlignTop);
    scroll->setWidget(treeWidget);
    outerLay->addWidget(scroll, 1);

    buildCategoryTree();
    return m_catTab;
}

void SettingsDialog::buildCategoryTree() {
    if (!m_catTreeLayout) return;
    QLayoutItem* child;
    while ((child = m_catTreeLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    auto& cats = m_tagMgr->categories();
    if (cats.isEmpty()) {
        auto* empty = new QLabel(Strings::get(StringKey::SettingsCatEmpty), m_catTab);
        empty->setStyleSheet("color: rgba(150,170,165,0.6); font-size: 12px; padding: 8px;");
        empty->setAlignment(Qt::AlignCenter);
        m_catTreeLayout->addWidget(empty);
        return;
    }

    for (TagCategory& cat : cats)
        addCategoryBlock(m_catTreeLayout, cat, 0);
}

void SettingsDialog::addCategoryBlock(QVBoxLayout* lay, TagCategory& cat, int depth) {
    QColor c = cat.color;
    int leftPad = 4 + depth * 20;

    // ── Category header row ───────────────────────────────────────────────────
    auto* headerRow = new QWidget(lay->parentWidget());
    auto* hLay = new QHBoxLayout(headerRow);
    hLay->setContentsMargins(leftPad, 2, 4, 2);
    hLay->setSpacing(6);

    // Colored indicator bar
    auto* bar = new QFrame(headerRow);
    bar->setFixedSize(3, 20);
    bar->setStyleSheet(QString("background: %1; border-radius: 1px;").arg(c.name()));
    hLay->addWidget(bar);

    // Category name + depth indicator
    QString prefix = depth > 0 ? QString("↳ ").repeated(1) : "";
    auto* nameLbl = new QLabel(prefix + cat.name, headerRow);
    nameLbl->setStyleSheet(QString(
        "QLabel { color: %1; font-size: %2px; font-weight: bold; background: none; border: none; }")
        .arg(c.name()).arg(depth == 0 ? 13 : 12));
    nameLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    hLay->addWidget(nameLbl, 1);

    // Tag count badge
    int tc = cat.tagCount();
    if (tc > 0) {
        auto* badge = new QLabel(QString("%1 Tag%2").arg(tc).arg(tc == 1 ? "" : "s"), headerRow);
        badge->setStyleSheet(QString(
            "QLabel { background: rgba(%1,%2,%3,0.2); border: 1px solid rgba(%1,%2,%3,0.4);"
            "border-radius: 8px; color: %4; font-size: 10px; padding: 1px 7px; }")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.name()));
        hLay->addWidget(badge);
    }

    // Color button + uniformColor toggle
    auto* colorBtn = new ColorPickerButton(c, headerRow);
    colorBtn->setFixedSize(22, 22);
    colorBtn->setToolTip("Farbe ändern");
    QString catId = cat.id;
    bool uni = cat.uniformColor;
    bool inh = cat.inheritColorToChildren;

    // Toggle: "Einheitliche Farbe"
    auto* uniToggle = new QToolButton(headerRow);
    uniToggle->setText("⬤");
    uniToggle->setFixedSize(22, 22);
    uniToggle->setCheckable(true);
    uniToggle->setChecked(uni);
    uniToggle->setToolTip(uni ? "Einheitliche Farbe aktiv – klicken zum Deaktivieren"
                               : "Einheitliche Farbe aktivieren");
    auto uniToggleStyle = [uniToggle](bool active) {
        uniToggle->setToolTip(active ? "Einheitliche Farbe aktiv – klicken zum Deaktivieren"
                                     : "Einheitliche Farbe aktivieren");
        uniToggle->setStyleSheet(active
            ? "QToolButton { background: rgba(0,180,160,0.35); border: 1px solid rgba(0,180,160,0.7);"
              "border-radius: 5px; color: #00c8b4; font-size: 9px; }"
              "QToolButton:hover { background: rgba(0,180,160,0.55); }"
            : "QToolButton { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.15);"
              "border-radius: 5px; color: rgba(150,180,175,0.5); font-size: 9px; }"
              "QToolButton:hover { background: rgba(255,255,255,0.12); color: #00c8b4; }");
    };
    uniToggleStyle(uni);

    connect(uniToggle, &QToolButton::toggled, this, [this, catId, uniToggle, colorBtn, uniToggleStyle](bool on) {
        uniToggleStyle(on);
        if (on) {
            // Activate: pick color and ask about inheritance
            QColor cur = colorBtn->color().isValid() ? colorBtn->color() : QColor(0, 180, 160);
            QColor c2 = QColorDialog::getColor(cur, this, "Einheitsfarbe wählen");
            if (!c2.isValid()) { uniToggle->setChecked(false); return; }
            colorBtn->setColor(c2);

            // Ask about inheritance
            QMessageBox msgBox(this);
            msgBox.setWindowTitle("Farbe vererben");
            msgBox.setText("Sollen alle Unterkategorien (jetzt und neue) dieselbe Farbe übernehmen?");
            msgBox.setIcon(QMessageBox::Question);
            msgBox.setStyleSheet(
                "QMessageBox { background: #1a2830; color: white; }"
                "QMessageBox QLabel { color: white; }"
                "QPushButton { background: rgba(0,180,160,0.25); border: 1px solid rgba(0,180,160,0.5);"
                "  border-radius: 5px; color: #00c8b4; padding: 5px 14px; }"
                "QPushButton:hover { background: rgba(0,180,160,0.45); }");
            QPushButton* yesBtn = msgBox.addButton("Ja, vererben", QMessageBox::YesRole);
            msgBox.addButton("Nein", QMessageBox::NoRole);
            msgBox.exec();
            bool inherit = (msgBox.clickedButton() == yesBtn);
            m_tagMgr->setCategoryUniformColor(catId, true, c2, inherit);
        } else {
            m_tagMgr->setCategoryUniformColor(catId, false, {});
        }
    });

    connect(colorBtn, &ColorPickerButton::colorChanged, this, [this, catId, uniToggle](const QColor& nc){
        // Color changed via picker: apply with current uniformColor + inherit state
        const bool currentUni = uniToggle->isChecked();
        if (!currentUni) {
            // Activate uniformColor implicitly when user picks a color
            uniToggle->setChecked(true); // triggers toggled → will open inherit dialog
            return;
        }
        // uniformColor already on: just update color, preserve inherit flag
        const TagCategory* cat2 = m_tagMgr->categoryById(catId);
        bool currentInherit = cat2 ? cat2->inheritColorToChildren : false;
        m_tagMgr->setCategoryUniformColor(catId, true, nc, currentInherit);
    });
    hLay->addWidget(uniToggle);
    hLay->addWidget(colorBtn);

    // Rename button
    auto* renBtn = new QToolButton(headerRow);
    renBtn->setText("✏");
    renBtn->setFixedSize(22, 22);
    renBtn->setToolTip(Strings::get(StringKey::CatPanelRename));
    renBtn->setStyleSheet(
        "QToolButton { background: rgba(100,160,255,0.15); border: 1px solid rgba(100,160,255,0.3);"
        "border-radius: 5px; color: #80a8ff; font-size: 11px; }"
        "QToolButton:hover { background: rgba(100,160,255,0.4); }");
    connect(renBtn, &QToolButton::clicked, this, [this, catId, &cat]{
        bool ok;
        QString n = QInputDialog::getText(this,
            Strings::get(StringKey::SettingsCatRenameTitle),
            Strings::get(StringKey::SettingsCatRenameLabel),
            QLineEdit::Normal, cat.name, &ok);
        if (ok && !n.trimmed().isEmpty())
            m_tagMgr->renameCategory(catId, n.trimmed());
    });
    hLay->addWidget(renBtn);

    // Add subcategory button
    auto* subBtn = new QToolButton(headerRow);
    subBtn->setText("+");
    subBtn->setFixedSize(22, 22);
    subBtn->setToolTip(Strings::get(StringKey::CatPanelNewSubcategory));
    subBtn->setStyleSheet(
        "QToolButton { background: rgba(0,180,160,0.15); border: 1px solid rgba(0,180,160,0.3);"
        "border-radius: 5px; color: #00c8b4; font-size: 14px; font-weight: bold; }"
        "QToolButton:hover { background: rgba(0,180,160,0.35); }");
    connect(subBtn, &QToolButton::clicked, this, [this, catId]{
        bool ok;
        QString n = QInputDialog::getText(this,
            Strings::get(StringKey::SettingsSubNewTitle),
            Strings::get(StringKey::SettingsSubNewLabel),
            QLineEdit::Normal, "", &ok);
        if (!ok || n.trimmed().isEmpty()) return;
        TagCategory sub = TagCategory::create(n.trimmed());
        // If parent has color-inheritance active, skip color dialog and inherit
        const TagCategory* parentCat = m_tagMgr->categoryById(catId);
        if (parentCat && parentCat->inheritColorToChildren) {
            sub.uniformColor = true;
            sub.color        = parentCat->color;
        } else {
            QColor col = QColorDialog::getColor(QColor(0, 180, 160), this,
                Strings::get(StringKey::SettingsSubColorTitle));
            if (col.isValid()) sub.color = col;
        }
        m_tagMgr->addSubcategory(catId, sub);
    });
    hLay->addWidget(subBtn);

    // Delete button
    auto* delBtn = new QToolButton(headerRow);
    delBtn->setText("🗑");
    delBtn->setFixedSize(22, 22);
    delBtn->setToolTip(Strings::get(StringKey::CatPanelDelete));
    delBtn->setStyleSheet(
        "QToolButton { background: rgba(200,60,60,0.15); border: 1px solid rgba(200,60,60,0.3);"
        "border-radius: 5px; color: #e05050; font-size: 11px; }"
        "QToolButton:hover { background: rgba(200,60,60,0.45); color: white; }");
    connect(delBtn, &QToolButton::clicked, this, [this, catId, &cat]{
        if (QMessageBox::question(this,
                Strings::get(StringKey::SettingsCatDeleteTitle),
                Strings::get(StringKey::SettingsCatDeleteMsg, cat.name),
                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
            m_tagMgr->deleteCategory(catId);
    });
    hLay->addWidget(delBtn);

    headerRow->setStyleSheet(QString(
        "QWidget { background: rgba(%1,%2,%3,0.08); border-radius: 6px; }"
        "QWidget:hover { background: rgba(%1,%2,%3,0.15); }")
        .arg(c.red()).arg(c.green()).arg(c.blue()));
    lay->addWidget(headerRow);

    // ── Tags inside this category ─────────────────────────────────────────────
    if (!cat.tags.isEmpty()) {
        auto* tagsContainer = new QWidget(lay->parentWidget());
        auto* tLay = new QVBoxLayout(tagsContainer);
        tLay->setContentsMargins(leftPad + 12, 2, 4, 2);
        tLay->setSpacing(3);

        for (const QString& tag : cat.tags) {
            QColor tc = cat.uniformColor ? cat.color : m_tagMgr->tagColor(tag);
            auto* tagRow = new QWidget(tagsContainer);
            auto* trLay = new QHBoxLayout(tagRow);
            trLay->setContentsMargins(2, 1, 2, 1);
            trLay->setSpacing(6);

            auto* dot = new QFrame(tagRow);
            dot->setFixedSize(6, 6);
            dot->setStyleSheet(QString("background: %1; border-radius: 3px;").arg(tc.name()));
            trLay->addWidget(dot);

            auto* tagLbl = new QLabel(tag, tagRow);
            tagLbl->setStyleSheet(QString(
                "QLabel { color: %1; font-size: 11px; font-weight: 600; background: none; border: none; }")
                .arg(tc.name()));
            tagLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            trLay->addWidget(tagLbl, 1);

            // Rename tag
            auto* renTagBtn = new QToolButton(tagRow);
            renTagBtn->setText("✏");
            renTagBtn->setFixedSize(18, 18);
            renTagBtn->setStyleSheet(
                "QToolButton { background: rgba(100,160,255,0.1); border: 1px solid rgba(100,160,255,0.25);"
                "border-radius: 4px; color: #80a8ff; font-size: 9px; }"
                "QToolButton:hover { background: rgba(100,160,255,0.35); }");
            connect(renTagBtn, &QToolButton::clicked, this, [this, tag]{
                bool ok;
                QString n = QInputDialog::getText(this,
                    Strings::get(StringKey::SettingsTagRenameTitle),
                    Strings::get(StringKey::SettingsTagRenameLabel),
                    QLineEdit::Normal, tag, &ok);
                if (ok && !n.trimmed().isEmpty())
                    m_tagMgr->renameTag(tag, n.trimmed());
            });
            trLay->addWidget(renTagBtn);

            // Remove from category
            auto* remTagBtn = new QToolButton(tagRow);
            remTagBtn->setText("✕");
            remTagBtn->setFixedSize(18, 18);
            remTagBtn->setToolTip(Strings::get(StringKey::CatPanelRename));
            remTagBtn->setStyleSheet(
                "QToolButton { background: rgba(200,60,60,0.1); border: 1px solid rgba(200,60,60,0.25);"
                "border-radius: 4px; color: #e05050; font-size: 9px; }"
                "QToolButton:hover { background: rgba(200,60,60,0.4); color: white; }");
            connect(remTagBtn, &QToolButton::clicked, this, [this, catId, tag]{
                m_tagMgr->removeTagFromCategory(catId, tag);
            });
            trLay->addWidget(remTagBtn);

            tagRow->setStyleSheet(
                "QWidget { background: rgba(255,255,255,0.02); border-radius: 4px; }"
                "QWidget:hover { background: rgba(255,255,255,0.05); }");
            tLay->addWidget(tagRow);
        }
        lay->addWidget(tagsContainer);
    }

    // ── Subcategories ─────────────────────────────────────────────────────────
    for (TagCategory& child : cat.children)
        addCategoryBlock(lay, child, depth + 1);

    // Small separator between root categories
    if (depth == 0 && !cat.children.isEmpty()) {
        auto* sep = new QFrame(lay->parentWidget());
        sep->setFrameShape(QFrame::HLine);
        sep->setFixedHeight(1);
        sep->setStyleSheet("color: rgba(40,60,70,0.4);");
        lay->addWidget(sep);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Converter tab  (Tag ↔ Unterkategorie)
// ─────────────────────────────────────────────────────────────────────────────
// Converter - ready for cleanup
QWidget* SettingsDialog::buildConverterTab() {
    auto* page = new QWidget(this);
    auto* outerLay = new QVBoxLayout(page);
    outerLay->setContentsMargins(10, 10, 10, 10);
    outerLay->setSpacing(12);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget(scroll);
    scroll->setWidget(inner);
    auto* lay = new QVBoxLayout(inner);
    lay->setSpacing(14);
    lay->setContentsMargins(2, 2, 2, 8);
    outerLay->addWidget(scroll, 1);

    // ── Section: Tag → Unterkategorie ────────────────────────────────────────
    auto* t2sGroup = new QGroupBox(Strings::get(StringKey::ConverterTagToSubcat), inner);
    auto* t2sLay   = new QVBoxLayout(t2sGroup);
    t2sLay->setSpacing(8);

    auto* t2sHint = new QLabel(
        "Wählt einen bestehenden Tag und eine Eltern-Kategorie.\n"
        "Der Tag wird als neue Unterkategorie angelegt, alle Dateien\n"
        "die diesen Tag haben werden der Unterkategorie zugeordnet\n"
        "und der Tag aus der globalen Registry entfernt.", t2sGroup);
    t2sHint->setStyleSheet("color: rgba(150,180,175,0.7); font-size: 11px;");
    t2sHint->setWordWrap(true);
    t2sLay->addWidget(t2sHint);

    // Tag selector
    auto* t2sTagRow = new QHBoxLayout;
    auto* t2sTagLbl = new QLabel(Strings::get(StringKey::ConverterSelectTag), t2sGroup);
    t2sTagLbl->setFixedWidth(180);
    auto* t2sTagBox = new QComboBox(t2sGroup);
    t2sTagBox->setStyleSheet(
        "QComboBox { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.2);"
        "border-radius: 6px; color: white; padding: 3px 8px; }"
        "QComboBox QAbstractItemView { background: #1a2830; color: white; selection-background-color: #00b4a0; }");
    for (const QString& t : m_tagMgr->allTags())
        t2sTagBox->addItem(t, t);
    t2sTagRow->addWidget(t2sTagLbl);
    t2sTagRow->addWidget(t2sTagBox, 1);
    t2sLay->addLayout(t2sTagRow);

    // Parent category selector
    auto* t2sCatRow = new QHBoxLayout;
    auto* t2sCatLbl = new QLabel(Strings::get(StringKey::ConverterSelectParentCat), t2sGroup);
    t2sCatLbl->setFixedWidth(180);
    auto* t2sCatBox = new QComboBox(t2sGroup);
    t2sCatBox->setStyleSheet(t2sTagBox->styleSheet());

    // Populate flat list of all categories (recursive)
    std::function<void(const QList<TagCategory>&, int)> populateCatBox;
    populateCatBox = [&](const QList<TagCategory>& cats, int depth) {
        for (const TagCategory& cat : cats) {
            QString indent = QString("  ").repeated(depth);
            t2sCatBox->addItem(indent + cat.name, cat.id);
            populateCatBox(cat.children, depth + 1);
        }
    };
    populateCatBox(m_tagMgr->categories(), 0);

    t2sCatRow->addWidget(t2sCatLbl);
    t2sCatRow->addWidget(t2sCatBox, 1);
    t2sLay->addLayout(t2sCatRow);

    // New subcategory name
    auto* t2sNameRow = new QHBoxLayout;
    auto* t2sNameLbl = new QLabel(Strings::get(StringKey::ConverterNewSubcatName), t2sGroup);
    t2sNameLbl->setFixedWidth(180);
    auto* t2sNameEdit = new QLineEdit(t2sGroup);
    t2sNameEdit->setPlaceholderText("(leer = Tag-Name übernehmen)");
    t2sNameEdit->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.2);"
        "border-radius: 6px; color: white; padding: 3px 8px; }");
    // Auto-fill name from selected tag
    connect(t2sTagBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [t2sTagBox, t2sNameEdit](int) {
        if (t2sNameEdit->text().isEmpty())
            t2sNameEdit->setPlaceholderText(t2sTagBox->currentText());
    });
    t2sNameRow->addWidget(t2sNameLbl);
    t2sNameRow->addWidget(t2sNameEdit, 1);
    t2sLay->addLayout(t2sNameRow);

    auto* t2sConvertBtn = new QPushButton(Strings::get(StringKey::ConverterConvert), t2sGroup);
    t2sConvertBtn->setFixedHeight(32);
    t2sConvertBtn->setStyleSheet(
        "QPushButton { background: rgba(0,180,160,0.25); border: 1px solid rgba(0,180,160,0.5);"
        "border-radius: 6px; color: #00c8b4; font-weight: bold; padding: 4px 16px; }"
        "QPushButton:hover { background: rgba(0,180,160,0.45); }");
    connect(t2sConvertBtn, &QPushButton::clicked, this, [=, this] {
        QString tag    = t2sTagBox->currentData().toString();
        QString catId  = t2sCatBox->currentData().toString();
        QString name   = t2sNameEdit->text().trimmed();
        if (name.isEmpty()) name = tag;

        if (tag.isEmpty()) {
            QMessageBox::warning(this, Strings::get(StringKey::ConverterTagToSubcat),
                                 Strings::get(StringKey::ConverterErrorNoTag));
            return;
        }
        if (catId.isEmpty()) {
            QMessageBox::warning(this, Strings::get(StringKey::ConverterTagToSubcat),
                                 Strings::get(StringKey::ConverterErrorNoCat));
            return;
        }
        convertTagToSubcategory(tag, catId, name);
        QMessageBox::information(this, Strings::get(StringKey::ConverterTagToSubcat),
                                 Strings::get(StringKey::ConverterSuccess));
        // Refresh converter combos
        t2sTagBox->clear();
        for (const QString& t : m_tagMgr->allTags()) t2sTagBox->addItem(t, t);
        t2sNameEdit->clear();
    });
    t2sLay->addWidget(t2sConvertBtn);
    lay->addWidget(t2sGroup);

    // ── Section: Unterkategorie → Tag ─────────────────────────────────────────
    auto* s2tGroup = new QGroupBox(Strings::get(StringKey::ConverterSubcatToTag), inner);
    auto* s2tLay   = new QVBoxLayout(s2tGroup);
    s2tLay->setSpacing(8);

    auto* s2tHint = new QLabel(
        "Wählt eine Unterkategorie.\n"
        "Deren Name wird als neuer Tag angelegt, alle Dateien\n"
        "in der Unterkategorie erhalten diesen Tag, und die\n"
        "Unterkategorie wird anschließend gelöscht.", s2tGroup);
    s2tHint->setStyleSheet("color: rgba(150,180,175,0.7); font-size: 11px;");
    s2tHint->setWordWrap(true);
    s2tLay->addWidget(s2tHint);

    auto* s2tSubRow = new QHBoxLayout;
    auto* s2tSubLbl = new QLabel(Strings::get(StringKey::ConverterSelectSubcat), s2tGroup);
    s2tSubLbl->setFixedWidth(180);
    auto* s2tSubBox = new QComboBox(s2tGroup);
    s2tSubBox->setStyleSheet(t2sTagBox->styleSheet());

    // Populate only subcategories (depth > 0)
    std::function<void(const QList<TagCategory>&, int)> populateSubBox;
    populateSubBox = [&](const QList<TagCategory>& cats, int depth) {
        for (const TagCategory& cat : cats) {
            if (depth > 0) {
                QString indent = QString("  ").repeated(depth - 1);
                s2tSubBox->addItem(indent + "↳ " + cat.name, cat.id);
            }
            populateSubBox(cat.children, depth + 1);
        }
    };
    populateSubBox(m_tagMgr->categories(), 0);

    s2tSubRow->addWidget(s2tSubLbl);
    s2tSubRow->addWidget(s2tSubBox, 1);
    s2tLay->addLayout(s2tSubRow);

    auto* s2tConvertBtn = new QPushButton(Strings::get(StringKey::ConverterConvert), s2tGroup);
    s2tConvertBtn->setFixedHeight(32);
    s2tConvertBtn->setStyleSheet(t2sConvertBtn->styleSheet());
    connect(s2tConvertBtn, &QPushButton::clicked, this, [=, this] {
        QString subcatId = s2tSubBox->currentData().toString();
        if (subcatId.isEmpty()) {
            QMessageBox::warning(this, Strings::get(StringKey::ConverterSubcatToTag),
                                 Strings::get(StringKey::ConverterErrorNoSubcat));
            return;
        }
        convertSubcategoryToTag(subcatId);
        QMessageBox::information(this, Strings::get(StringKey::ConverterSubcatToTag),
                                 Strings::get(StringKey::ConverterSuccess));
        // Rebuild subcat combo inline (populateSubBox is out of scope)
        s2tSubBox->clear();
        std::function<void(const QList<TagCategory>&, int)> refill;
        refill = [&](const QList<TagCategory>& cats, int depth) {
            for (const TagCategory& cat : cats) {
                if (depth > 0) {
                    QString ind = QString("  ").repeated(depth - 1);
                    s2tSubBox->addItem(ind + "↳ " + cat.name, cat.id);
                }
                refill(cat.children, depth + 1);
            }
        };
        refill(m_tagMgr->categories(), 0);
    });
    s2tLay->addWidget(s2tConvertBtn);
    lay->addWidget(s2tGroup);

    // ── Section: JSON-Migration ───────────────────────────────────────────────
    auto* migrGroup = new QGroupBox(Strings::get(StringKey::ConverterMigrateJson), inner);
    auto* migrLay   = new QVBoxLayout(migrGroup);

    auto* migrHint = new QLabel(
        "Ältere JSON-Dateien nutzen ein Tag-zentriertes Format.\n"
        "Diese Funktion schreibt die aktuelle Ordner-JSON ins\n"
        "kompaktere datei-zentrierte Format (v2) um.\n"
        "Bitte vorher ein Backup anlegen.", migrGroup);
    migrHint->setStyleSheet("color: rgba(150,180,175,0.7); font-size: 11px;");
    migrHint->setWordWrap(true);
    migrLay->addWidget(migrHint);

    auto* migrBtn = new QPushButton(Strings::get(StringKey::ConverterMigrateJson), migrGroup);
    migrBtn->setFixedHeight(32);
    migrBtn->setStyleSheet(
        "QPushButton { background: rgba(200,160,40,0.2); border: 1px solid rgba(200,160,40,0.5);"
        "border-radius: 6px; color: #e0b840; font-weight: bold; padding: 4px 16px; }"
        "QPushButton:hover { background: rgba(200,160,40,0.4); }");
    connect(migrBtn, &QPushButton::clicked, this, [this] {
        auto reply = QMessageBox::question(this,
            Strings::get(StringKey::ConverterMigrateJson),
            Strings::get(StringKey::ConverterMigrateConfirm),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) return;

        // Converter - ready for cleanup
        // Trigger a save via settingsChanged → MainWindow calls saveCurrentFolder().
        // JsonStorage::saveFolder() always writes v=2 (new compact format),
        // so emitting settingsChanged is sufficient to migrate the file on disk.
        emit settingsChanged();
        QMessageBox::information(this,
            Strings::get(StringKey::ConverterMigrateJson),
            Strings::get(StringKey::ConverterMigrateSuccess));
    });
    migrLay->addWidget(migrBtn);
    lay->addWidget(migrGroup);

    lay->addStretch();
    return page;
}

// Converter - ready for cleanup
void SettingsDialog::convertTagToSubcategory(const QString& tag,
                                              const QString& parentCatId,
                                              const QString& newSubcatName) {
    // 1. Create subcategory under parentCatId with the tag's color
    TagCategory sub = TagCategory::create(newSubcatName);
    sub.color        = m_tagMgr->tagColor(tag);
    sub.uniformColor = true;

    // 2. Find all files that currently have this tag and add them to the new subcat.
    //    We'll do this after addSubcategory so we have the ID.
    m_tagMgr->addSubcategory(parentCatId, sub);

    // 3. Find the newly created subcategory (last child of parentCatId)
    //    and record its files.
    const TagCategory* parent = m_tagMgr->categoryById(parentCatId);
    if (!parent || parent->children.isEmpty()) return;
    QString newSubId = parent->children.last().id;

    // 4. Add the tag as a tag member of the new subcategory
    m_tagMgr->addTagToCategory(newSubId, tag);

    // 5. Remove the tag from the global registry (it now lives as subcategory)
    //    This also removes the tag from all files.
    m_tagMgr->deleteTag(tag);

    emit settingsChanged();
}

// Converter - ready for cleanup
void SettingsDialog::convertSubcategoryToTag(const QString& subcatId) {
    // 1. Find the subcategory
    const TagCategory* subcat = m_tagMgr->categoryById(subcatId);
    if (!subcat) return;

    QString tagName = subcat->name;
    QColor  tagColor = subcat->color;

    // 2. Register the new tag with the subcategory's color
    m_tagMgr->setTagColor(tagName, tagColor);

    // 3. All files in the subcategory get this tag
    for (const QString& fileName : subcat->files)
        m_tagMgr->addTagToFile(fileName, tagName);

    // 4. All tags that were in the subcategory get added to the files too
    //    (the subcategory itself had tags listed → these remain as regular tags)
    // (tags listed inside the subcat stay in their files unchanged)

    // 5. Delete the subcategory
    m_tagMgr->deleteCategory(subcatId);

    emit settingsChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsDialog::applySettings() {
    AppSettings::instance().setLanguage(
        static_cast<Language>(m_langBox->currentData().toInt()));
    AppSettings::instance().setVideoPlayback(
        m_videoNative->isChecked() ? VideoPlayback::Native : VideoPlayback::External);
    AppSettings::instance().sync();
    emit settingsChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Design tab
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
//  Design tab  (full rewrite with 8 profiles + gradient / glow / tile editor)
// ─────────────────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::buildDesignTab() {
    auto* page = new QWidget(this);
    auto* outerLay = new QVBoxLayout(page);
    outerLay->setContentsMargins(6, 6, 6, 6);
    outerLay->setSpacing(8);

    // ── Scrollable area for the whole tab ────────────────────────────────────
    auto* scroll = new QScrollArea(page);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget(scroll);
    scroll->setWidget(inner);
    auto* lay = new QVBoxLayout(inner);
    lay->setSpacing(10);
    lay->setContentsMargins(4, 4, 4, 8);
    outerLay->addWidget(scroll, 1);

    // ── Profile grid ─────────────────────────────────────────────────────────
    auto* profileGroup = new QGroupBox(Strings::get(StringKey::SettingsDesignProfileGroup), inner);
    auto* pgLay = new QGridLayout(profileGroup);
    pgLay->setSpacing(6);

    struct ProfileEntry {
        DesignProfile p;
        const char*   icon;
        StringKey     labelKey;
        StringKey     descKey;
    };
    static const ProfileEntry entries[] = {
        { DesignProfile::Dark,         "🌙", StringKey::SettingsDesignProfileDark,    StringKey::SettingsDesignProfileDarkDesc    },
        { DesignProfile::DarkOLED,     "⚫", StringKey::SettingsDesignProfileOLED,    StringKey::SettingsDesignProfileOLEDDesc    },
        { DesignProfile::OceanDepth,   "🌊", StringKey::SettingsDesignProfileOcean,   StringKey::SettingsDesignProfileOceanDesc   },
        { DesignProfile::InfernoBlaze, "🔥", StringKey::SettingsDesignProfileInferno, StringKey::SettingsDesignProfileInfernoDesc },
        { DesignProfile::NeonPurple,   "⚡", StringKey::SettingsDesignProfileNeon,    StringKey::SettingsDesignProfileNeonDesc    },
        { DesignProfile::MidnightRose, "🌹", StringKey::SettingsDesignProfileRose,    StringKey::SettingsDesignProfileRoseDesc    },
        { DesignProfile::Elegant,      "✨", StringKey::SettingsDesignProfileElegant, StringKey::SettingsDesignProfileElegantDesc },
        { DesignProfile::Simple,       "☀",  StringKey::SettingsDesignProfileSimple,  StringKey::SettingsDesignProfileSimpleDesc  },
        { DesignProfile::Custom,       "🎨", StringKey::SettingsDesignProfileCustom,  StringKey::SettingsDesignProfileCustomDesc  },
    };

    DesignProfile current = AppSettings::instance().designProfile();
    auto* profileBtnGroup = new QButtonGroup(inner);

    for (int i = 0; i < 9; ++i) {
        const ProfileEntry& e = entries[i];
        auto* card = new QWidget(profileGroup);
        card->setFixedHeight(62);
        auto* cLay = new QHBoxLayout(card);
        cLay->setContentsMargins(8, 4, 8, 4);
        cLay->setSpacing(8);

        auto* radio = new QRadioButton(card);
        radio->setChecked(e.p == current);
        profileBtnGroup->addButton(radio, (int)e.p);
        cLay->addWidget(radio);

        // Color swatch strip
        ThemeColors th = (e.p == DesignProfile::Custom)
            ? AppSettings::instance().customTheme()
            : AppSettings::instance().themeForProfile(e.p);
        auto* swatchWidget = new QWidget(card);
        swatchWidget->setFixedSize(28, 40);
        swatchWidget->setStyleSheet(
            QString("background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                    "stop:0 %1, stop:0.5 %2, stop:1 %3); border-radius: 4px;")
                .arg(th.accent.name(), th.card.name(), th.background.name()));
        cLay->addWidget(swatchWidget);

        auto* textW = new QWidget(card);
        auto* textL = new QVBoxLayout(textW);
        textL->setContentsMargins(0,0,0,0);
        textL->setSpacing(1);
        auto* lbl1 = new QLabel(QString("%1  <b>%2</b>").arg(e.icon, Strings::get(e.labelKey)), textW);
        lbl1->setTextFormat(Qt::RichText);
        auto* lbl2 = new QLabel(Strings::get(e.descKey), textW);
        lbl2->setStyleSheet("color: rgba(160,180,175,0.6); font-size: 10px;");
        textL->addWidget(lbl1);
        textL->addWidget(lbl2);
        cLay->addWidget(textW, 1);

        DesignProfile cp = e.p;
        connect(radio, &QRadioButton::toggled, this, [cp, this](bool on){
            if (!on) return;
            AppSettings::instance().setDesignProfile(cp);
            AppSettings::instance().sync();
            emit settingsChanged();
        });

        bool sel = (e.p == current);
        card->setStyleSheet(QString(
            "QWidget { background: rgba(%1,%2,%3,%4); border-radius: 8px; }"
            "QWidget:hover { background: rgba(%1,%2,%3,0.18); }")
            .arg(th.accent.red()).arg(th.accent.green()).arg(th.accent.blue())
            .arg(sel ? "0.16" : "0.06"));

        pgLay->addWidget(card, i / 3, i % 3);
    }
    lay->addWidget(profileGroup);

    // ── Custom theme editor ───────────────────────────────────────────────────
    auto* customGroup = new QGroupBox(Strings::get(StringKey::SettingsCustomThemeEditor), inner);
    customGroup->setEnabled(current == DesignProfile::Custom);
    auto* cgLay = new QVBoxLayout(customGroup);

    ThemeColors cust = AppSettings::instance().customTheme();

    // Helper: color row
    auto colorRow = [&](QFormLayout* fl, const QString& label,
                         const QColor& color) -> ColorPickerButton* {
        auto* btn = new ColorPickerButton(color, customGroup);
        btn->setFixedSize(100, 24);
        fl->addRow(label, btn);
        return btn;
    };

    // ── Basic colors section ───────────────────────────────────────────────
    auto* basicGroup = new QGroupBox(Strings::get(StringKey::SettingsDesignBaseColors), customGroup);
    auto* basicForm  = new QFormLayout(basicGroup);
    auto* bgBtn      = colorRow(basicForm, Strings::get(StringKey::SettingsDesignColorBg2),    cust.background);
    auto* cardBtn    = colorRow(basicForm, Strings::get(StringKey::SettingsDesignColorCard),   cust.card);
    auto* textBtn    = colorRow(basicForm, Strings::get(StringKey::SettingsDesignColorText),   cust.textPrimary);
    auto* mutedBtn   = colorRow(basicForm, Strings::get(StringKey::SettingsDesignColorMuted),  cust.textMuted);
    auto* borderBtn  = colorRow(basicForm, Strings::get(StringKey::SettingsDesignColorBorder), cust.border);
    cgLay->addWidget(basicGroup);

    // ── Background gradient section ────────────────────────────────────────
    auto* bgGradGroup  = new QGroupBox(Strings::get(StringKey::SettingsDesignBgGradient), customGroup);
    auto* bgGradLay    = new QFormLayout(bgGradGroup);
    auto* bgGradChk    = new QCheckBox(Strings::get(StringKey::SettingsDesignBgGradientEnable), bgGradGroup);
    bgGradChk->setChecked(cust.bgIsGradient);
    bgGradLay->addRow(bgGradChk);
    auto* bgGradStartBtn = colorRow(bgGradLay, Strings::get(StringKey::SettingsDesignGradStart), cust.bgGradStart);
    auto* bgGradEndBtn   = colorRow(bgGradLay, Strings::get(StringKey::SettingsDesignGradEnd),   cust.bgGradEnd);
    auto* bgAngleSpin    = new QSpinBox(bgGradGroup);
    bgAngleSpin->setRange(0, 360);
    bgAngleSpin->setValue(cust.bgGradAngle);
    bgAngleSpin->setSuffix("°");
    bgGradLay->addRow(Strings::get(StringKey::SettingsDesignAngle), bgAngleSpin);
    cgLay->addWidget(bgGradGroup);

    // ── Accent section ─────────────────────────────────────────────────────
    auto* accGroup = new QGroupBox(Strings::get(StringKey::SettingsDesignAccent), customGroup);
    auto* accForm  = new QFormLayout(accGroup);

    auto* accTypeCombo = new QComboBox(accGroup);
    accTypeCombo->addItem(Strings::get(StringKey::SettingsDesignAccentSolid),    (int)AccentType::Solid);
    accTypeCombo->addItem(Strings::get(StringKey::SettingsDesignAccentGradient), (int)AccentType::Gradient);
    accTypeCombo->addItem(Strings::get(StringKey::SettingsDesignAccentGlow),     (int)AccentType::Glow);
    accTypeCombo->setCurrentIndex((int)cust.accentType);
    accForm->addRow(Strings::get(StringKey::SettingsDesignType), accTypeCombo);

    auto* accBtn     = colorRow(accForm, Strings::get(StringKey::SettingsDesignAccentColor),    cust.accent);
    auto* accGradBtn = colorRow(accForm, Strings::get(StringKey::SettingsDesignGradEnd),        cust.accentGradEnd);

    auto* glowRadiusSlider = new QSlider(Qt::Horizontal, accGroup);
    glowRadiusSlider->setRange(2, 40);
    glowRadiusSlider->setValue((int)cust.glowRadius);
    accForm->addRow(Strings::get(StringKey::SettingsDesignGlowRadius), glowRadiusSlider);

    auto* glowIntSlider = new QSlider(Qt::Horizontal, accGroup);
    glowIntSlider->setRange(10, 100);
    glowIntSlider->setValue((int)(cust.glowIntensity * 100));
    accForm->addRow(Strings::get(StringKey::SettingsDesignGlowIntensity), glowIntSlider);
    cgLay->addWidget(accGroup);

    // ── Tile background section ────────────────────────────────────────────
    auto* tileGroup = new QGroupBox(Strings::get(StringKey::SettingsDesignTileGroup), customGroup);
    auto* tileLay   = new QFormLayout(tileGroup);

    auto* tileBgTypeCombo = new QComboBox(tileGroup);
    tileBgTypeCombo->addItem(Strings::get(StringKey::SettingsDesignTileSolid),       (int)TileBgType::Solid);
    tileBgTypeCombo->addItem(Strings::get(StringKey::SettingsDesignTileGradient),    (int)TileBgType::Gradient);
    tileBgTypeCombo->addItem(Strings::get(StringKey::SettingsDesignTileTransparent), (int)TileBgType::Transparent);
    tileBgTypeCombo->setCurrentIndex((int)cust.tileBgType);
    tileLay->addRow(Strings::get(StringKey::SettingsDesignTileBgType), tileBgTypeCombo);

    auto* tileBgBtn      = colorRow(tileLay, Strings::get(StringKey::SettingsDesignTileBg),      cust.tileBgColor);
    auto* tileBgGradBtn  = colorRow(tileLay, Strings::get(StringKey::SettingsDesignTileGradEnd), cust.tileBgGradEnd);
    auto* tileAngleSpin  = new QSpinBox(tileGroup);
    tileAngleSpin->setRange(0, 360);
    tileAngleSpin->setValue(cust.tileBgGradAngle);
    tileAngleSpin->setSuffix("°");
    tileLay->addRow(Strings::get(StringKey::SettingsDesignTileAngle), tileAngleSpin);

    auto* tileGlowChk    = new QCheckBox(Strings::get(StringKey::SettingsDesignTileGlow), tileGroup);
    tileGlowChk->setChecked(cust.tileGlowOnHover);
    tileLay->addRow(tileGlowChk);

    auto* tileGlowRadius = new QSlider(Qt::Horizontal, tileGroup);
    tileGlowRadius->setRange(2, 30);
    tileGlowRadius->setValue((int)cust.tileGlowRadius);
    tileLay->addRow(Strings::get(StringKey::SettingsDesignTileGlowRadius), tileGlowRadius);
    cgLay->addWidget(tileGroup);

    // ── Theme name ────────────────────────────────────────────────────────
    auto* nameEdit = new QLineEdit(cust.name, customGroup);
    nameEdit->setPlaceholderText(Strings::get(StringKey::SettingsDesignThemeName));
    auto* nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(Strings::get(StringKey::SettingsDesignThemeName), customGroup));
    nameRow->addWidget(nameEdit, 1);
    cgLay->addLayout(nameRow);

    // ── Apply / Reset buttons ─────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;

    auto* applyBtn = new QPushButton(Strings::get(StringKey::SettingsDesignApply), customGroup);
    applyBtn->setStyleSheet(
        "QPushButton { background: rgba(0,180,160,0.25); border: 1px solid rgba(0,180,160,0.5);"
        "border-radius: 6px; color: #00c8b4; padding: 5px 14px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(0,180,160,0.45); }");

    auto collectTheme = [=]() -> ThemeColors {
        ThemeColors t;
        t.name             = nameEdit->text().trimmed().isEmpty()
                             ? Strings::get(StringKey::SettingsDesignProfileCustom)
                             : nameEdit->text().trimmed();
        t.background       = bgBtn->color();
        t.card             = cardBtn->color();
        t.textPrimary      = textBtn->color();
        t.textMuted        = mutedBtn->color();
        t.border           = borderBtn->color();
        t.bgIsGradient     = bgGradChk->isChecked();
        t.bgGradStart      = bgGradStartBtn->color();
        t.bgGradEnd        = bgGradEndBtn->color();
        t.bgGradAngle      = bgAngleSpin->value();
        t.accentType       = (AccentType)accTypeCombo->currentData().toInt();
        t.accent           = accBtn->color();
        t.accentGradEnd    = accGradBtn->color();
        t.glowRadius       = (float)glowRadiusSlider->value();
        t.glowIntensity    = glowIntSlider->value() / 100.0f;
        t.tileBgType       = (TileBgType)tileBgTypeCombo->currentData().toInt();
        t.tileBgColor      = tileBgBtn->color();
        t.tileBgGradEnd    = tileBgGradBtn->color();
        t.tileBgGradAngle  = tileAngleSpin->value();
        t.tileGlowOnHover  = tileGlowChk->isChecked();
        t.tileGlowRadius   = (float)tileGlowRadius->value();
        return t;
    };

    connect(applyBtn, &QPushButton::clicked, this, [=, this]{
        AppSettings::instance().setCustomTheme(collectTheme());
        AppSettings::instance().setDesignProfile(DesignProfile::Custom);
        AppSettings::instance().sync();
        emit settingsChanged();
    });
    btnRow->addWidget(applyBtn);

    // Export / Import JSON buttons
    auto* exportBtn = new QPushButton(Strings::get(StringKey::SettingsDesignExport), customGroup);
    exportBtn->setStyleSheet(
        "QPushButton { background: rgba(80,120,200,0.2); border: 1px solid rgba(80,120,200,0.4);"
        "border-radius: 6px; color: #80a8ff; padding: 5px 12px; }"
        "QPushButton:hover { background: rgba(80,120,200,0.4); }");
    connect(exportBtn, &QPushButton::clicked, this, [=, this]{
        QString path = QFileDialog::getSaveFileName(this,
            Strings::get(StringKey::SettingsDesignExportTitle),
            nameEdit->text().trimmed().isEmpty() ? "custom_theme" : nameEdit->text().trimmed(),
            "JSON Theme (*.json)");
        if (path.isEmpty()) return;
        ThemeColors t = collectTheme();
        AppSettings::instance().setCustomTheme(t);
        if (AppSettings::instance().exportCustomTheme(path))
            QMessageBox::information(this, Strings::get(StringKey::SettingsDesignExportTitle),
                Strings::get(StringKey::SettingsDesignExportOk) + path);
        else
            QMessageBox::warning(this, Strings::get(StringKey::SettingsDesignExportTitle),
                Strings::get(StringKey::SettingsDesignExportErr));
    });
    btnRow->addWidget(exportBtn);

    auto* importBtn = new QPushButton(Strings::get(StringKey::SettingsDesignImport), customGroup);
    importBtn->setStyleSheet(
        "QPushButton { background: rgba(80,200,120,0.2); border: 1px solid rgba(80,200,120,0.4);"
        "border-radius: 6px; color: #50d080; padding: 5px 12px; }"
        "QPushButton:hover { background: rgba(80,200,120,0.4); }");
    connect(importBtn, &QPushButton::clicked, this, [=, this]{
        QString path = QFileDialog::getOpenFileName(this,
            Strings::get(StringKey::SettingsDesignImportTitle), QString(),
            "JSON Theme (*.json)");
        if (path.isEmpty()) return;
        if (AppSettings::instance().importCustomTheme(path)) {
            emit settingsChanged();
            ThemeColors loaded = AppSettings::instance().customTheme();
            bgBtn->setColor(loaded.background);
            cardBtn->setColor(loaded.card);
            textBtn->setColor(loaded.textPrimary);
            mutedBtn->setColor(loaded.textMuted);
            borderBtn->setColor(loaded.border);
            bgGradChk->setChecked(loaded.bgIsGradient);
            bgGradStartBtn->setColor(loaded.bgGradStart);
            bgGradEndBtn->setColor(loaded.bgGradEnd);
            bgAngleSpin->setValue(loaded.bgGradAngle);
            accTypeCombo->setCurrentIndex((int)loaded.accentType);
            accBtn->setColor(loaded.accent);
            accGradBtn->setColor(loaded.accentGradEnd);
            glowRadiusSlider->setValue((int)loaded.glowRadius);
            glowIntSlider->setValue((int)(loaded.glowIntensity * 100));
            tileBgTypeCombo->setCurrentIndex((int)loaded.tileBgType);
            tileBgBtn->setColor(loaded.tileBgColor);
            tileBgGradBtn->setColor(loaded.tileBgGradEnd);
            tileAngleSpin->setValue(loaded.tileBgGradAngle);
            tileGlowChk->setChecked(loaded.tileGlowOnHover);
            tileGlowRadius->setValue((int)loaded.tileGlowRadius);
            nameEdit->setText(loaded.name);
            QMessageBox::information(this, Strings::get(StringKey::SettingsDesignImportTitle),
                Strings::get(StringKey::SettingsDesignImportOk) + loaded.name);
        } else {
            QMessageBox::warning(this, Strings::get(StringKey::SettingsDesignImportTitle),
                Strings::get(StringKey::SettingsDesignImportErr));
        }
    });
    btnRow->addWidget(importBtn);

    // ── Live-Vorschau: jede Änderung sofort anwenden ─────────────────────────
    auto liveApply = [=, this]() {
        if (AppSettings::instance().designProfile() != DesignProfile::Custom) return;
        AppSettings::instance().setCustomTheme(collectTheme());
        emit settingsChanged();
    };
    connect(bgBtn,          &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(cardBtn,        &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(textBtn,        &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(mutedBtn,       &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(borderBtn,      &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(bgGradChk,      &QCheckBox::toggled,              this, [liveApply](bool){ liveApply(); });
    connect(bgGradStartBtn, &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(bgGradEndBtn,   &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(bgAngleSpin,    QOverload<int>::of(&QSpinBox::valueChanged), this, [liveApply](int){ liveApply(); });
    connect(accTypeCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, [liveApply](int){ liveApply(); });
    connect(accBtn,         &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(accGradBtn,     &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(glowRadiusSlider, &QSlider::valueChanged,         this, [liveApply](int){ liveApply(); });
    connect(glowIntSlider,    &QSlider::valueChanged,         this, [liveApply](int){ liveApply(); });
    connect(tileBgTypeCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, [liveApply](int){ liveApply(); });
    connect(tileBgBtn,        &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(tileBgGradBtn,    &ColorPickerButton::colorChanged, this, [liveApply](const QColor&){ liveApply(); });
    connect(tileAngleSpin,    QOverload<int>::of(&QSpinBox::valueChanged), this, [liveApply](int){ liveApply(); });
    connect(tileGlowChk,      &QCheckBox::toggled,              this, [liveApply](bool){ liveApply(); });
    connect(tileGlowRadius,   &QSlider::valueChanged,           this, [liveApply](int){ liveApply(); });

    cgLay->addLayout(btnRow);
    lay->addWidget(customGroup);

    // ── Enable/disable custom group when Custom profile selected ─────────────
    connect(profileBtnGroup, &QButtonGroup::idToggled, this,
            [customGroup](int id, bool checked){
        if (checked) customGroup->setEnabled(id == (int)DesignProfile::Custom);
    });

    lay->addStretch();
    return page;
}
