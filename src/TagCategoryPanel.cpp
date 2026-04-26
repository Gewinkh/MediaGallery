#include "TagCategoryPanel.h"
#include "AppSettings.h"
#include <QScrollArea>
#include <QInputDialog>
#include <QColorDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QApplication>
#include <QMessageBox>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QCursor>
#include <QPointer>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
static QString panelBg()   { return "rgba(20,32,40,0.97)"; }
static QString headerBg(int depth) {
    // slightly lighter per depth level
    int base = 30 + depth * 8;
    return QString("rgba(%1,%1,%2,0.9)").arg(base).arg(base + 10);
}

// ─────────────────────────────────────────────────────────────────────────────
//  TagCategoryPanel
// ─────────────────────────────────────────────────────────────────────────────
TagCategoryPanel::TagCategoryPanel(TagManager* mgr, QWidget* parent)
    : QWidget(parent), m_mgr(mgr)
{
    setMinimumWidth(200);
    setMaximumWidth(320);
    setStyleSheet(QString("TagCategoryPanel { background: %1; }").arg(panelBg()));

    auto* rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    // ── Top toolbar ──────────────────────────────────────────────────────────
    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(36);
    toolbar->setStyleSheet("background: rgba(0,0,0,0.25);");
    auto* tbLay = new QHBoxLayout(toolbar);
    tbLay->setContentsMargins(8, 4, 8, 4);
    tbLay->setSpacing(6);

    auto* titleLbl = new QLabel("Tags", toolbar);
    titleLbl->setStyleSheet("color: rgba(200,220,215,0.9); font-weight: bold; font-size: 13px;");
    tbLay->addWidget(titleLbl);
    tbLay->addStretch(1);

    // AND / OR button
    m_andOrBtn = new QToolButton(toolbar);
    m_andOrBtn->setCheckable(true);
    m_andOrBtn->setChecked(m_andMode);
    m_andOrBtn->setText(m_andMode ? "AND" : "OR");
    m_andOrBtn->setToolTip("Filter: AND = all tags must match, OR = at least one must match");
    m_andOrBtn->setStyleSheet(
        "QToolButton { background: rgba(0,180,160,0.2); border: 1px solid rgba(0,180,160,0.5);"
        "border-radius: 5px; color: #00c8b4; font-size: 10px; font-weight: bold; padding: 2px 8px; }"
        "QToolButton:checked { background: rgba(0,180,160,0.4); }");
    connect(m_andOrBtn, &QToolButton::toggled, this, &TagCategoryPanel::onAndOrToggled);
    tbLay->addWidget(m_andOrBtn);

    // Add category button
    auto* addBtn = new QToolButton(toolbar);
    addBtn->setText("+");
    addBtn->setToolTip("Neue Kategorie");
    addBtn->setStyleSheet(
        "QToolButton { background: rgba(0,180,160,0.2); border: 1px solid rgba(0,180,160,0.4);"
        "border-radius: 5px; color: #00c8b4; font-size: 15px; font-weight: bold; padding: 0 8px; }"
        "QToolButton:hover { background: rgba(0,180,160,0.35); }");
    connect(addBtn, &QToolButton::clicked, this, &TagCategoryPanel::onAddCategory);
    tbLay->addWidget(addBtn);

    rootLay->addWidget(toolbar);

    // ── Scrollable content area ──────────────────────────────────────────────
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }"
                          "QScrollBar:vertical { width: 4px; background: transparent; }"
                          "QScrollBar::handle:vertical { background: rgba(255,255,255,0.15); border-radius: 2px; }");
    scroll->setWidgetResizable(true);

    m_inner    = new QWidget(scroll);
    m_innerLay = new QVBoxLayout(m_inner);
    m_innerLay->setContentsMargins(0, 4, 0, 8);
    m_innerLay->setSpacing(2);
    m_innerLay->addStretch(1);
    m_inner->setStyleSheet("background: transparent;");
    scroll->setWidget(m_inner);
    rootLay->addWidget(scroll, 1);

    // Connect tag manager signals
    connect(mgr, &TagManager::tagsChanged, this, &TagCategoryPanel::refresh);
    connect(mgr, &TagManager::categoriesChanged, this, &TagCategoryPanel::refresh);

    refresh();
}

void TagCategoryPanel::refresh() {
    // Use a queued/deferred rebuild so that the CategoryNode which triggered
    // a signal (e.g. onSetUniformColor) is fully off the call stack before
    // its widget gets deleted by rebuildNodes().
    QMetaObject::invokeMethod(this, &TagCategoryPanel::rebuildNodes, Qt::QueuedConnection);
}

void TagCategoryPanel::enterGroupMode(const QString& tag) {
    m_groupMode    = true;
    m_groupModeTag = tag;
    emit groupModeChanged(true, tag);
}

void TagCategoryPanel::exitGroupMode() {
    m_groupMode    = false;
    m_groupModeTag.clear();
    emit groupModeChanged(false, QString());
}

void TagCategoryPanel::enterAddToTagMode(const QString& tag) {
    m_addToTagMode    = true;
    m_addToTagModeTag = tag;
    emit addToTagModeChanged(true, tag);
}

void TagCategoryPanel::exitAddToTagMode() {
    m_addToTagMode    = false;
    m_addToTagModeTag.clear();
    emit addToTagModeChanged(false, QString());
}

void TagCategoryPanel::rebuildNodes() {
    // Remove all but the trailing stretch
    while (m_innerLay->count() > 1)
        delete m_innerLay->takeAt(0)->widget();

    auto& cats = m_mgr->categories();
    for (auto& cat : cats) {
        addCategoryNode(cat, m_innerLay, 0);
    }
}

void TagCategoryPanel::addCategoryNode(TagCategory& cat, QVBoxLayout* lay, int depth) {
    auto* node = new CategoryNode(cat, this, depth, m_inner);
    lay->insertWidget(lay->count() - 1, node); // before the stretch
}

QStringList TagCategoryPanel::activeTagFilter() const {
    return QStringList(m_activeTags.begin(), m_activeTags.end());
}

void TagCategoryPanel::setTagFilterAnd(bool v) {
    m_andMode = v;
    m_andOrBtn->setChecked(v);
    m_andOrBtn->setText(v ? "AND" : "OR");
}

void TagCategoryPanel::onTagToggled(const QString& tag, bool on) {
    if (on) m_activeTags.insert(tag);
    else     m_activeTags.remove(tag);
    emit filterChanged();
}

void TagCategoryPanel::onAndOrToggled() {
    m_andMode = m_andOrBtn->isChecked();
    m_andOrBtn->setText(m_andMode ? "AND" : "OR");
    AppSettings::instance().setTagFilterAnd(m_andMode);
    emit filterChanged();
}

void TagCategoryPanel::onAddCategory() {
    bool ok;
    QString name = QInputDialog::getText(this, "Neue Kategorie", "Name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    m_mgr->addCategory(TagCategory::create(name.trimmed()));
}

// ─────────────────────────────────────────────────────────────────────────────
//  CategoryNode
// ─────────────────────────────────────────────────────────────────────────────
CategoryNode::CategoryNode(TagCategory& cat, TagCategoryPanel* panel,
                           int depth, QWidget* parent)
    : QFrame(parent), m_cat(cat), m_panel(panel), m_depth(depth)
{
    setAcceptDrops(true);
    setFrameShape(QFrame::NoFrame);

    auto* outerLay = new QVBoxLayout(this);
    outerLay->setContentsMargins(depth * 12, 0, 0, 0);
    outerLay->setSpacing(0);

    // ── Header ───────────────────────────────────────────────────────────────
    m_header = new QWidget(this);
    m_header->setFixedHeight(30);
    auto* hLay = new QHBoxLayout(m_header);
    hLay->setContentsMargins(6, 2, 6, 2);
    hLay->setSpacing(4);

    m_collapseBtn = new QToolButton(m_header);
    m_collapseBtn->setText("▾");
    m_collapseBtn->setStyleSheet("QToolButton { color: rgba(200,220,215,0.6); background: none; border: none; font-size: 10px; }");
    m_collapseBtn->setFixedSize(16, 16);
    connect(m_collapseBtn, &QToolButton::clicked, this, &CategoryNode::onToggleCollapse);
    hLay->addWidget(m_collapseBtn);

    // Color dot (if uniform color)
    if (m_cat.uniformColor) {
        auto* dot = new QLabel(m_header);
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(m_cat.color.name()));
        hLay->addWidget(dot);
    }

    m_titleLbl = new QLabel(m_cat.name, m_header);
    m_titleLbl->setStyleSheet("color: rgba(210,230,225,0.95); font-size: 12px; font-weight: 600;");
    hLay->addWidget(m_titleLbl, 1);

    // Tag count badge
    int tc = m_cat.tagCount();
    if (tc > 0) {
        auto* badge = new QLabel(QString::number(tc), m_header);
        badge->setStyleSheet(
            "color: rgba(150,180,175,0.7); font-size: 10px;"
            "background: rgba(255,255,255,0.07); border-radius: 7px; padding: 1px 5px;");
        hLay->addWidget(badge);
    }

    // Context menu on right-click
    m_header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_header, &QWidget::customContextMenuRequested,
            this, &CategoryNode::onContextMenu);

    updateHeaderStyle();
    outerLay->addWidget(m_header);

    // ── Body ─────────────────────────────────────────────────────────────────
    m_body    = new QWidget(this);
    m_bodyLay = new QVBoxLayout(m_body);
    m_bodyLay->setContentsMargins(0, 2, 0, 4);
    m_bodyLay->setSpacing(3);
    m_body->setStyleSheet("background: rgba(0,0,0,0.12);");

    // Chip area (wrapping row of tag buttons)
    m_chipArea = new QWidget(m_body);
    m_chipLay  = new QHBoxLayout(m_chipArea);
    m_chipLay->setContentsMargins(8, 2, 4, 2);
    m_chipLay->setSpacing(4);
    m_chipLay->setAlignment(Qt::AlignLeft);
    m_chipArea->setStyleSheet("background: transparent;");
    m_bodyLay->addWidget(m_chipArea);

    // Children area
    m_childrenLay = new QVBoxLayout();
    m_childrenLay->setContentsMargins(0, 0, 0, 0);
    m_childrenLay->setSpacing(2);
    m_bodyLay->addLayout(m_childrenLay);

    outerLay->addWidget(m_body);

    buildChips();
    buildChildren();
}

void CategoryNode::buildChips() {
    // Clear old chips
    while (m_chipLay->count())
        delete m_chipLay->takeAt(0)->widget();

    QColor baseColor = m_cat.uniformColor ? m_cat.color : QColor();

    for (const QString& tag : m_cat.tags) {
        QColor c = m_cat.uniformColor ? baseColor : m_panel->m_mgr->tagColor(tag);
        bool active = m_panel->m_activeTags.contains(tag);

        auto* chip = new DraggableChip(tag, m_cat.id, m_panel, m_chipArea);
        chip->setCheckable(true);
        chip->setChecked(active);
        chip->setFixedHeight(22);

        QString activeBg  = c.name();
        QString inactiveBg = QString("rgba(%1,%2,%3,0.18)").arg(c.red()).arg(c.green()).arg(c.blue());
        QString txtActive  = (c.lightness() > 130) ? "#111" : "#fff";
        chip->setStyleSheet(QString(
                                "QPushButton { background: %1; border: 1px solid %2; border-radius: 9px;"
                                "color: %3; font-size: 11px; font-weight: 600; padding: 0 9px; }"
                                "QPushButton:checked { background: %4; color: %5; border-color: %4; }"
                                "QPushButton:hover { opacity: 0.85; }")
                                .arg(active ? activeBg : inactiveBg)
                                .arg(c.name())
                                .arg(active ? txtActive : c.name())
                                .arg(activeBg)
                                .arg(txtActive));

        connect(chip, &QPushButton::toggled, this, [this, tag](bool on){
            m_panel->onTagToggled(tag, on);
        });
        m_chipLay->addWidget(chip);
    }

    // "+tag" add button
    auto* addTag = new QToolButton(m_chipArea);
    addTag->setText("+");
    addTag->setToolTip(tr("Add tag"));
    addTag->setFixedSize(22, 22);
    addTag->setStyleSheet(
        "QToolButton { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 11px; color: rgba(200,220,215,0.6); font-size: 13px; font-weight: bold; }"
        "QToolButton:hover { background: rgba(0,180,160,0.2); color: #00c8b4; }");
    connect(addTag, &QToolButton::clicked, this, &CategoryNode::onAddTag);
    m_chipLay->addWidget(addTag);
    m_chipLay->addStretch(1);
}

void CategoryNode::buildChildren() {
    while (m_childrenLay->count())
        delete m_childrenLay->takeAt(0)->widget();

    for (auto& child : m_cat.children) {
        auto* node = new CategoryNode(child, m_panel, m_depth + 1, m_body);
        m_childrenLay->addWidget(node);
    }
}

void CategoryNode::refreshChips() {
    buildChips();
    buildChildren();
}

void CategoryNode::updateHeaderStyle() {
    QString bg = m_cat.uniformColor
                     ? QString("rgba(%1,%2,%3,0.25)").arg(m_cat.color.red()).arg(m_cat.color.green()).arg(m_cat.color.blue())
                     : headerBg(m_depth);
    m_header->setStyleSheet(QString(
                                "QWidget { background: %1; border-radius: 6px; }"
                                "QWidget:hover { background: rgba(255,255,255,0.05); }").arg(bg));
}

void CategoryNode::onToggleCollapse() {
    m_collapsed = !m_collapsed;
    m_body->setVisible(!m_collapsed);
    m_collapseBtn->setText(m_collapsed ? "▸" : "▾");
}

// ── Context menu ─────────────────────────────────────────────────────────────
void CategoryNode::onContextMenu(const QPoint& pos) {
    Q_UNUSED(pos)
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #1a2830; border: 1px solid rgba(255,255,255,0.15); border-radius: 6px; color: white; padding: 4px; }"
        "QMenu::item { padding: 5px 20px; border-radius: 4px; }"
        "QMenu::item:selected { background: rgba(0,180,160,0.3); }");

    menu.addAction("✏  Rename",           this, &CategoryNode::onRenameCategory);
    menu.addAction("🎨  Set uniform color", this, &CategoryNode::onSetUniformColor);
    if (m_cat.uniformColor)
        menu.addAction("✖  Remove uniform color", this, &CategoryNode::onClearUniformColor);
    menu.addSeparator();
    menu.addAction("+ Add subcategory",   this, &CategoryNode::onAddSubcategory);
    menu.addAction("+ Add tag",           this, &CategoryNode::onAddTag);
    menu.addSeparator();
    menu.addAction("🗑  Delete category",  this, &CategoryNode::onDeleteCategory);

    menu.exec(QCursor::pos());
}

void CategoryNode::onRenameCategory() {
    const QString catId   = m_cat.id;
    const QString catName = m_cat.name;
    TagManager*   mgr     = m_panel->m_mgr;
    bool ok;
    QString n = QInputDialog::getText(this, "Umbenennen", "Neuer Name:",
                                      QLineEdit::Normal, catName, &ok);
    if (!ok || n.trimmed().isEmpty()) return;
    mgr->renameCategory(catId, n.trimmed());
}

void CategoryNode::onDeleteCategory() {
    const QString catId   = m_cat.id;
    const QString catName = m_cat.name;
    TagManager*   mgr     = m_panel->m_mgr;
    if (QMessageBox::question(this, tr("Delete Category"),
                              tr("Really delete category \"%1\"?\nContained tags will be kept.").arg(catName),
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
        mgr->deleteCategory(catId);
}

void CategoryNode::onSetUniformColor() {
    const QString catId = m_cat.id;
    const QColor  cur   = m_cat.color;
    TagManager*   mgr   = m_panel->m_mgr;

    QColor c = QColorDialog::getColor(cur, this, tr("Pick uniform color"));
    if (!c.isValid()) return;

    // Ask whether subcategories should inherit the color
    bool inherit = false;
    if (!m_cat.children.isEmpty() || true) {
        // Always offer the option so it can be pre-set for future subcategories
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Inherit Color"));
        msgBox.setText(tr("Should all subcategories (now and future) inherit this color?"));
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setStyleSheet(
            "QMessageBox { background: #1a2830; color: white; }"
            "QMessageBox QLabel { color: white; }"
            "QPushButton { background: rgba(0,180,160,0.25); border: 1px solid rgba(0,180,160,0.5);"
            "  border-radius: 5px; color: #00c8b4; padding: 5px 14px; }"
            "QPushButton:hover { background: rgba(0,180,160,0.45); }");
        QPushButton* yesBtn = msgBox.addButton(tr("Yes, inherit"), QMessageBox::YesRole);
        msgBox.addButton(tr("No"), QMessageBox::NoRole);
        msgBox.exec();
        inherit = (msgBox.clickedButton() == yesBtn);
    }

    mgr->setCategoryUniformColor(catId, true, c, inherit);
}

void CategoryNode::onClearUniformColor() {
    const QString catId = m_cat.id;
    m_panel->m_mgr->setCategoryUniformColor(catId, false, {});
}

void CategoryNode::onAddSubcategory() {
    const QString catId           = m_cat.id;
    const bool    inheritActive   = m_cat.inheritColorToChildren;
    const QColor  inheritedColor  = m_cat.color;
    TagManager*   mgr             = m_panel->m_mgr;
    bool ok;
    QString n = QInputDialog::getText(this, "Neue Unterkategorie", "Name:", QLineEdit::Normal, "", &ok);
    if (!ok || n.trimmed().isEmpty()) return;

    TagCategory sub = TagCategory::create(n.trimmed());
    // If the parent has color-inheritance active, pre-set the new subcategory's color
    if (inheritActive) {
        sub.uniformColor = true;
        sub.color        = inheritedColor;
    }
    mgr->addSubcategory(catId, sub);
}

void CategoryNode::onAddTag() {
    // Capture catId by value before any dialog — the CategoryNode may be
    // destroyed by a refresh() triggered inside addTagToCategory(), so we
    // must not access 'm_cat' or 'm_panel' after that call.
    const QString catId  = m_cat.id;
    TagManager*   mgr    = m_panel->m_mgr;

    // Show all tags not yet in this category
    QStringList all = mgr->allTags();
    QStringList available;
    for (const QString& t : all)
        if (!m_cat.tags.contains(t)) available.append(t);

    if (available.isEmpty()) {
        // Allow creating a brand-new tag
        bool ok;
        QString newTag = QInputDialog::getText(this, "Neuer Tag", "Tag-Name:", QLineEdit::Normal, "", &ok);
        if (!ok || newTag.trimmed().isEmpty()) return;
        mgr->addTagToCategory(catId, newTag.trimmed());
        return;
    }

    // Let user pick from existing or create new
    available.prepend("<Neuen Tag erstellen…>");
    bool ok;
    QString chosen = QInputDialog::getItem(this, tr("Add Tag"),
                                           tr("Select tag:"), available, 0, false, &ok);
    if (!ok) return;

    if (chosen == available.first()) {
        chosen = QInputDialog::getText(this, "Neuer Tag", "Name:", QLineEdit::Normal, "", &ok).trimmed();
        if (!ok || chosen.isEmpty()) return;
    }
    // 'this' may be deleted after this call — do not touch any members afterwards
    mgr->addTagToCategory(catId, chosen);
}

// ── Drag & Drop ───────────────────────────────────────────────────────────────
void CategoryNode::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(kTagDragMime))
        e->acceptProposedAction();
}

void CategoryNode::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat(kTagDragMime))
        e->acceptProposedAction();
}

void CategoryNode::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasFormat(kTagDragMime)) return;
    QByteArray raw = e->mimeData()->data(kTagDragMime);
    // format: "tagName\nsourceCatId"
    QStringList parts = QString::fromUtf8(raw).split('\n');
    if (parts.size() < 2) return;
    QString tag       = parts[0];
    QString sourceCat = parts[1];
    if (sourceCat == m_cat.id) return; // same category, no-op

    m_panel->m_mgr->moveTagToCategory(tag, sourceCat, m_cat.id);
    e->acceptProposedAction();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DraggableChip
// ─────────────────────────────────────────────────────────────────────────────
DraggableChip::DraggableChip(const QString& tag, const QString& catId,
                             TagCategoryPanel* panel, QWidget* parent)
    : QPushButton(tag, parent), m_tag(tag), m_catId(catId), m_panel(panel) {}

void DraggableChip::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        m_dragStart = e->pos();
    QPushButton::mousePressEvent(e);
}

void DraggableChip::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    if ((e->pos() - m_dragStart).manhattanLength() < QApplication::startDragDistance()) return;

    QPointer<DraggableChip> guard(this);

    auto* drag = new QDrag(this);
    auto* mime = new QMimeData;
    mime->setData(kTagDragMime, QString("%1\n%2").arg(m_tag, m_catId).toUtf8());
    drag->setMimeData(mime);

    QPixmap px(size());
    px.fill(Qt::transparent);
    render(&px);
    drag->setPixmap(px);
    drag->setHotSpot(e->pos());

    Qt::DropAction result = drag->exec(Qt::MoveAction);

    // If widget was not destroyed and drop was ignored (dropped outside any category),
    // remove the tag from its source category
    if (guard && result == Qt::IgnoreAction && !m_catId.isEmpty()) {
        m_panel->mgr()->removeTagFromCategory(m_catId, m_tag);
    }
}

void DraggableChip::contextMenuEvent(QContextMenuEvent* e) {
    QString menuStyleStr =
        "QMenu { background: #1a2830; border: 1px solid rgba(255,255,255,0.15); border-radius: 6px; color: white; padding: 4px; }"
        "QMenu::item { padding: 5px 18px; border-radius: 4px; }"
        "QMenu::item:selected { background: rgba(0,180,160,0.3); }";

    QMenu menu(this);
    menu.setStyleSheet(menuStyleStr);

    // ── Change color ──────────────────────────────────────────────────────────
    menu.addAction(QString("🎨  Change color of \"%1\"").arg(m_tag), this, [this]{
        QColor cur = m_panel->mgr()->tagColor(m_tag);
        QColor c   = QColorDialog::getColor(cur, this, "Tag Color");
        if (c.isValid()) m_panel->mgr()->setTagColor(m_tag, c);
    });

    // ── Rename ────────────────────────────────────────────────────────────────
    menu.addAction(QString("✏  Rename \"%1\"").arg(m_tag), this, [this]{
        bool ok;
        QString n = QInputDialog::getText(this, "Rename Tag", "New name:",
                                          QLineEdit::Normal, m_tag, &ok);
        if (ok && !n.trimmed().isEmpty())
            m_panel->mgr()->renameTag(m_tag, n.trimmed());
    });

    // ── Add to category ───────────────────────────────────────────────────────
    QMenu* addToCatMenu = menu.addMenu("📂  Add to category");
    addToCatMenu->setStyleSheet(menuStyleStr);

    // Capture everything by value — 'this' (the chip) may be destroyed when
    // addTagToCategory() triggers a refresh() that rebuilds all CategoryNodes.
    TagManager* mgr    = m_panel->mgr();
    QString     tag    = m_tag;

    std::function<void(QMenu*, const QList<TagCategory>&)> populateCats;
    populateCats = [&, mgr, tag](QMenu* parent, const QList<TagCategory>& cats) {
        for (const TagCategory& cat : cats) {
            bool alreadyIn = cat.tags.contains(tag);
            QString catId  = cat.id;
            if (!cat.children.isEmpty()) {
                QMenu* sub = parent->addMenu(cat.name);
                sub->setStyleSheet(menuStyleStr);
                QAction* selfAct = sub->addAction(
                    alreadyIn ? QString("✓ %1 (bereits enthalten)").arg(cat.name)
                              : QString("→ %1").arg(cat.name));
                selfAct->setEnabled(!alreadyIn);
                connect(selfAct, &QAction::triggered, [mgr, catId, tag]{
                    mgr->addTagToCategory(catId, tag);
                });
                sub->addSeparator();
                populateCats(sub, cat.children);
            } else {
                QAction* act = parent->addAction(
                    alreadyIn ? QString("✓ %1 (bereits enthalten)").arg(cat.name)
                              : cat.name);
                act->setEnabled(!alreadyIn);
                connect(act, &QAction::triggered, [mgr, catId, tag]{
                    mgr->addTagToCategory(catId, tag);
                });
            }
        }
    };
    populateCats(addToCatMenu, mgr->categories());
    if (addToCatMenu->isEmpty())
        addToCatMenu->addAction("(no other categories)")->setEnabled(false);

    menu.addSeparator();

    // ── Remove from category ──────────────────────────────────────────────────
    menu.addAction("✖  Remove from category", this, [this]{
        m_panel->mgr()->removeTagFromCategory(m_catId, m_tag);
    });

    menu.addSeparator();

    // ── Add-to-Tag mode ───────────────────────────────────────────────────────
    menu.addAction(QString("🏷  Add-to-Tag Modus: \"%1\"…").arg(m_tag), this, [this]{
        m_panel->enterAddToTagMode(m_tag);
    });

    menu.addSeparator();

    // ── Group mode: tag-based media selection ─────────────────────────────────
    menu.addAction(QString("🗂  Group mode for \"%1\"…").arg(m_tag), this, [this]{
        m_panel->enterGroupMode(m_tag);
    });

    menu.exec(QCursor::pos());
}