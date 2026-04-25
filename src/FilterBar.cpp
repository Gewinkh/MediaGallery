#include "FilterBar.h"
#include "Icons.h"
#include "Strings.h"
#include <QScrollArea>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QScreen>
#include <QColorDialog>
#include <QInputDialog>
#include <QMenu>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QPointer>
#include <QCheckBox>
#include <functional>

static const char* kTagDragMime = "application/x-medgallery-tag";

// Forward declarations — defined further below, used in refreshTagList / onTagToggled
static void collectTagsDeep(const TagCategory& cat, QSet<QString>& out);
static bool collectTagsForId(const QList<TagCategory>& cats, const QString& id, QSet<QString>& out);

static QString menuStyle() {
    return "QMenu { background: #1a2830; border: 1px solid rgba(255,255,255,0.15);"
           "border-radius: 6px; color: white; padding: 4px; }"
           "QMenu::item { padding: 5px 18px; border-radius: 4px; }"
           "QMenu::item:selected { background: rgba(0,180,160,0.3); }"
           "QMenu::separator { background: rgba(255,255,255,0.1); height: 1px; margin: 3px 8px; }";
}

static QString panelStyle() {
    return "QFrame#dropPanel { background: #1a2830; border: 1px solid rgba(0,180,160,0.45);"
           "border-radius: 8px; }"
           "QScrollArea { background: transparent; border: none; }"
           "QScrollBar:vertical { width: 4px; background: transparent; }"
           "QScrollBar::handle:vertical { background: rgba(255,255,255,0.2); border-radius: 2px; }";
}

// ─────────────────────────────────────────────────────────────────────────────
//  FilterBar
// ─────────────────────────────────────────────────────────────────────────────
FilterBar::FilterBar(TagManager* mgr, QWidget* parent)
    : QWidget(parent), m_tagMgr(mgr)
{
    setFixedHeight(44);
    auto* mainLay = new QHBoxLayout(this);
    mainLay->setContentsMargins(8, 4, 8, 4);
    mainLay->setSpacing(8);

    // ── Medien hover button ────────────────────────────────────────────────
    m_mediaBtn = new MediaHoverButton(this, this);
    mainLay->addWidget(m_mediaBtn);

    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet("color: rgba(255,255,255,0.15);");
    mainLay->addWidget(sep1);

    // ── Sort ──────────────────────────────────────────────────────────────────
    m_sortLbl = new QLabel(Strings::get(StringKey::FilterSortBy), this);
    m_sortLbl->setStyleSheet("color: rgba(180,200,195,0.8); font-size: 11px;");
    mainLay->addWidget(m_sortLbl);

    m_sortField = new QComboBox(this);
    m_sortField->addItem(Strings::get(StringKey::FilterDate),     (int)SortField::Date);
    m_sortField->addItem(Strings::get(StringKey::FilterName),     (int)SortField::Name);
    m_sortField->addItem(Strings::get(StringKey::FilterTags),     (int)SortField::Tags);
    m_sortField->addItem(Strings::get(StringKey::FilterFileSize), (int)SortField::FileSize);
    m_sortField->setStyleSheet(
        "QComboBox { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 6px; color: white; padding: 2px 8px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #1a2830; color: white; selection-background-color: #00b4a0; }");
    for (int i = 0; i < m_sortField->count(); ++i)
        m_fieldOrder[i] = SortOrder::Descending;
    connect(m_sortField, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FilterBar::onSortFieldChanged);
    mainLay->addWidget(m_sortField);

    m_sortOrder = new QToolButton(this);
    m_sortOrder->setToolTip(Strings::get(StringKey::FilterReverseOrder));
    m_sortOrder->setIcon(currentFieldOrder() == SortOrder::Ascending ? Icons::arrowUp() : Icons::arrowDown());
    m_sortOrder->setIconSize(QSize(14, 14));
    m_sortOrder->setStyleSheet(
        "QToolButton { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 6px; color: white; font-size: 14px; padding: 1px 6px; }"
        "QToolButton:hover { background: rgba(0,200,180,0.2); }");
    connect(m_sortOrder, &QToolButton::clicked, this, &FilterBar::onSortOrderToggled);
    mainLay->addWidget(m_sortOrder);

    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet("color: rgba(255,255,255,0.15);");
    mainLay->addWidget(sep2);

    // ── Filter-mode hover button ───────────────────────────────────────────────
    auto* fmBtn = new FilterModeHoverButton(this, this);
    fmBtn->setObjectName("filterModeHoverBtn");
    fmBtn->setVisible2(false);
    mainLay->addWidget(fmBtn);

    auto* sep3 = new QFrame(this);
    sep3->setObjectName("filterModeSep");
    sep3->setFrameShape(QFrame::VLine);
    sep3->setStyleSheet("color: rgba(255,255,255,0.15);");
    sep3->setVisible(false);
    mainLay->addWidget(sep3);

    // ── Tags hover-dropdown ───────────────────────────────────────────────────
    m_tagsDropdown = new HoverDropdown("Tags", HoverDropdown::Mode::Tags, this, this);
    mainLay->addWidget(m_tagsDropdown);

    // ── Kategorien hover-dropdown (+ button is shown INSIDE the panel header) ─
    m_catsDropdown = new HoverDropdown("Kategorien", HoverDropdown::Mode::Categories, this, this);
    mainLay->addWidget(m_catsDropdown);

    auto* sep5 = new QFrame(this);
    sep5->setFrameShape(QFrame::VLine);
    sep5->setStyleSheet("color: rgba(255,255,255,0.15);");
    mainLay->addWidget(sep5);

    // ── Active chips strip ────────────────────────────────────────────────────
    m_activeScroll = new QScrollArea(this);
    m_activeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_activeScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_activeScroll->setFrameShape(QFrame::NoFrame);
    m_activeScroll->setStyleSheet("background: transparent;");
    m_activeScroll->setFixedHeight(32);
    m_activeScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_activeArea   = new QWidget(m_activeScroll);
    m_activeLayout = new QHBoxLayout(m_activeArea);
    m_activeLayout->setContentsMargins(2, 2, 2, 2);
    m_activeLayout->setSpacing(4);
    m_activeLayout->setAlignment(Qt::AlignLeft);
    m_activeArea->setLayout(m_activeLayout);
    m_activeScroll->setWidget(m_activeArea);
    m_activeScroll->setWidgetResizable(true);
    mainLay->addWidget(m_activeScroll, 1);

    connect(mgr, &TagManager::tagsChanged,       this, &FilterBar::refreshTagList);
    connect(mgr, &TagManager::categoriesChanged, this, &FilterBar::refreshTagList);

    buildActiveChips();
}

void FilterBar::buildActiveChips() {
    while (m_activeLayout->count()) {
        auto* item = m_activeLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    const QSet<QString> activeTags = m_activeTags;
    for (const QString& tag : activeTags) {
        QColor c = m_tagMgr->tagColor(tag);
        auto* btn = new QPushButton(tag, m_activeArea);
        btn->setFixedHeight(24);
        QString ac = c.name();
        QString tc = (c.lightness() > 130) ? "#111" : "#fff";
        btn->setStyleSheet(QString(
            "QPushButton { background: %1; border: 1px solid %1; border-radius: 10px;"
            "color: %2; font-size: 11px; font-weight: 600; padding: 0 10px; }"
            "QPushButton:hover { background: %3; }").arg(ac, tc, c.darker(120).name()));
        connect(btn, &QPushButton::clicked, this, [this, tag]{ onTagToggled(tag, false); });
        m_activeLayout->addWidget(btn);
    }

    std::function<void(const QList<TagCategory>&)> addCatChips = [&](const QList<TagCategory>& cats) {
        for (const TagCategory& cat : cats) {
            if (m_activeCategories.contains(cat.id)) {
                QColor c = cat.color;
                auto* btn = new QPushButton(cat.name, m_activeArea);
    btn->setIcon(Icons::folder());
    btn->setIconSize(QSize(14, 14));
                btn->setFixedHeight(24);
                btn->setStyleSheet(QString(
                    "QPushButton { background: rgba(%1,%2,%3,0.35); border: 1px solid rgba(%1,%2,%3,0.8);"
                    "border-radius: 10px; color: %4; font-size: 11px; font-weight: 700; padding: 0 10px; }"
                    "QPushButton:hover { background: rgba(%1,%2,%3,0.55); }")
                    .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.name()));
                QString catId = cat.id;
                connect(btn, &QPushButton::clicked, this, [this, catId]{ onCategoryToggled(catId, false); });
                m_activeLayout->addWidget(btn);
            }
            addCatChips(cat.children);
        }
    };
    addCatChips(m_tagMgr->categories());

    m_activeLayout->addStretch(1);
    bool hasFilter = !m_activeTags.isEmpty() || !m_activeCategories.isEmpty();
    // Show/hide filter mode button
    if (auto* fmBtn = findChild<FilterModeHoverButton*>("filterModeHoverBtn"))
        fmBtn->setVisible2(hasFilter);
    if (auto* sep3 = findChild<QFrame*>("filterModeSep"))
        sep3->setVisible(hasFilter);
}

void FilterBar::refreshTagList() {
    QStringList all = m_tagMgr->allTags();

    // Prune manual tags that no longer exist
    QSet<QString> prunedManual;
    for (const QString& t : m_manualTags)
        if (all.contains(t)) prunedManual.insert(t);
    m_manualTags = prunedManual;

    // Rebuild activeTags = surviving manual tags + all tags from still-active categories
    m_activeTags = m_manualTags;
    for (const QString& id : m_activeCategories)
        collectTagsForId(m_tagMgr->categories(), id, m_activeTags);

    m_tagsDropdown->rebuild();
    m_catsDropdown->rebuild();
    buildActiveChips();

    // Re-apply filter if any filter is active, so category/tag changes
    // are immediately reflected in the gallery view.
    if (!m_activeTags.isEmpty() || !m_activeCategories.isEmpty())
        emit filterChanged();
}

void FilterBar::onTagToggled(const QString& tag, bool on) {
    if (on) {
        m_manualTags.insert(tag);
        m_activeTags.insert(tag);
    } else {
        m_manualTags.remove(tag);
        // Only remove from activeTags if no active category still covers this tag
        QSet<QString> catCovered;
        for (const QString& id : m_activeCategories)
            collectTagsForId(m_tagMgr->categories(), id, catCovered);
        if (!catCovered.contains(tag))
            m_activeTags.remove(tag);
    }
    buildActiveChips();
    emit filterChanged();
}

// Recursively collect all tags from a category node and ALL its descendants.
static void collectTagsDeep(const TagCategory& cat, QSet<QString>& out) {
    for (const QString& t : cat.tags)
        out.insert(t);
    for (const TagCategory& child : cat.children)
        collectTagsDeep(child, out);
}

// Search the full category tree for a node with the given id and collect its tags.
static bool collectTagsForId(const QList<TagCategory>& cats,
                              const QString& id,
                              QSet<QString>& out)
{
    for (const TagCategory& cat : cats) {
        if (cat.id == id) {
            collectTagsDeep(cat, out);
            return true;
        }
        if (collectTagsForId(cat.children, id, out))
            return true;
    }
    return false;
}

void FilterBar::onCategoryToggled(const QString& catId, bool on) {
    // Collect all tags belonging to this category (recursively)
    QSet<QString> catTags;
    collectTagsForId(m_tagMgr->categories(), catId, catTags);

    if (on) {
        m_activeCategories.insert(catId);
        for (const QString& t : catTags)
            m_activeTags.insert(t);
    } else {
        m_activeCategories.remove(catId);
        // Rebuild the set of tags still covered by other active categories
        QSet<QString> stillCovered;
        for (const QString& id : m_activeCategories)
            collectTagsForId(m_tagMgr->categories(), id, stillCovered);
        // Remove tags that are neither covered by another category nor manually selected
        for (const QString& t : catTags) {
            if (!stillCovered.contains(t) && !m_manualTags.contains(t))
                m_activeTags.remove(t);
        }
    }
    buildActiveChips();
    emit filterChanged();
}

QStringList FilterBar::activeTagFilter() const {
    // Rebuild on-the-fly: manual tags + all tags from active categories.
    // This guards against any state where m_activeTags was not yet synced.
    QSet<QString> result = m_manualTags;
    for (const QString& id : m_activeCategories)
        collectTagsForId(m_tagMgr->categories(), id, result);
    return QStringList(result.begin(), result.end());
}

QStringList FilterBar::activeCategoryTagFilter() const {
    QSet<QString> result;
    const QList<TagCategory>& allCats = m_tagMgr->categories();
    for (const QString& id : m_activeCategories)
        collectTagsForId(allCats, id, result);
    return QStringList(result.begin(), result.end());
}

QStringList FilterBar::activeCategoryIds() const {
    return QStringList(m_activeCategories.begin(), m_activeCategories.end());
}

bool FilterBar::hasCategoryFilter() const {
    return !m_activeCategories.isEmpty();
}

void FilterBar::onFilterModeClicked(TagFilterMode mode) {
    m_filterMode = mode;
    if (auto* fmBtn = findChild<FilterModeHoverButton*>("filterModeHoverBtn"))
        static_cast<FilterModeHoverButton*>(fmBtn)->updateStyle(m_filterMode);
    emit filterChanged();
}

void FilterBar::updateFilterModeBtn() {
    if (auto* fmBtn = findChild<FilterModeHoverButton*>("filterModeHoverBtn"))
        static_cast<FilterModeHoverButton*>(fmBtn)->updateStyle(m_filterMode);
}

void FilterBar::onAddCategory() {
    bool ok;
    QString name = QInputDialog::getText(this,
        Strings::get(StringKey::FilterNewCategory),
        Strings::get(StringKey::SettingsCatNewLabel),
        QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    TagCategory cat = TagCategory::create(name.trimmed());

    QDialog colorDlg(this);
    colorDlg.setWindowTitle(Strings::get(StringKey::FilterCategoryColor));
    colorDlg.setStyleSheet("QDialog{background:#1a2830;color:white;}"
                           "QLabel{color:white;}QPushButton{background:rgba(255,255,255,0.1);"
                           "border:1px solid rgba(255,255,255,0.2);border-radius:5px;color:white;padding:4px 12px;}");
    auto* dlgLay = new QVBoxLayout(&colorDlg);
    auto* uniformChk = new QCheckBox(Strings::get(StringKey::CatPanelSetColor), &colorDlg);
    uniformChk->setStyleSheet("QCheckBox{color:rgba(200,220,215,0.9);}");
    dlgLay->addWidget(uniformChk);
    QColor chosenColor(0, 180, 160);
    auto* colorBtn = new QPushButton(Strings::get(StringKey::FilterCatChangeColor), &colorDlg);
    colorBtn->setStyleSheet(QString("QPushButton{background:%1;border-radius:5px;color:white;padding:4px 12px;}").arg(chosenColor.name()));
    connect(colorBtn, &QPushButton::clicked, &colorDlg, [&]{
        QColor c = QColorDialog::getColor(chosenColor, &colorDlg,
            Strings::get(StringKey::FilterCategoryColor));
        if (c.isValid()) {
            chosenColor = c;
            colorBtn->setStyleSheet(QString("QPushButton{background:%1;border-radius:5px;color:white;padding:4px 12px;}").arg(c.name()));
        }
    });
    dlgLay->addWidget(colorBtn);
    auto* btnRow = new QHBoxLayout;
    auto* okBtn  = new QPushButton(Strings::get(StringKey::SettingsOk), &colorDlg);
    auto* canBtn = new QPushButton(Strings::get(StringKey::SettingsCancel), &colorDlg);
    connect(okBtn,  &QPushButton::clicked, &colorDlg, &QDialog::accept);
    connect(canBtn, &QPushButton::clicked, &colorDlg, &QDialog::reject);
    btnRow->addStretch(); btnRow->addWidget(okBtn); btnRow->addWidget(canBtn);
    dlgLay->addLayout(btnRow);

    if (colorDlg.exec() == QDialog::Accepted) {
        cat.uniformColor = uniformChk->isChecked();
        cat.color = chosenColor;
    }
    m_tagMgr->addCategory(cat);
}

bool FilterBar::tagFilterAnd() const { return m_filterMode == TagFilterMode::AND; }
SortField FilterBar::sortField() const { return static_cast<SortField>(m_sortField->currentData().toInt()); }
SortOrder FilterBar::sortOrder()  const { return currentFieldOrder(); }
bool FilterBar::showImages() const { return AppSettings::instance().showImages(); }
bool FilterBar::showVideos() const { return AppSettings::instance().showVideos(); }
bool FilterBar::showAudio()  const { return AppSettings::instance().showAudio(); }
bool FilterBar::showPdfs()   const { return AppSettings::instance().showPdfs(); }

SortOrder FilterBar::currentFieldOrder() const {
    return m_fieldOrder.value(m_sortField->currentIndex(), SortOrder::Descending);
}
void FilterBar::saveCurrentFieldOrder(SortOrder o) {
    m_fieldOrder[m_sortField->currentIndex()] = o;
}
void FilterBar::onSortFieldChanged(int) {
    m_sortOrder->setIcon(currentFieldOrder() == SortOrder::Ascending ? Icons::arrowUp() : Icons::arrowDown());
    m_sortOrder->setIconSize(QSize(14, 14));
    emit sortChanged();
}
void FilterBar::onSortOrderToggled() {
    SortOrder next = (currentFieldOrder() == SortOrder::Ascending)
                     ? SortOrder::Descending : SortOrder::Ascending;
    saveCurrentFieldOrder(next);
    m_sortOrder->setIcon(next == SortOrder::Ascending ? Icons::arrowUp() : Icons::arrowDown());
    m_sortOrder->setIconSize(QSize(14, 14));
    emit sortChanged();
}
void FilterBar::onTypeFilterChanged() { emit filterChanged(); }

void FilterBar::retranslate() {
    m_sortLbl->setText(Strings::get(StringKey::FilterSortBy));
    m_sortField->setItemText(0, Strings::get(StringKey::FilterDate));
    m_sortField->setItemText(1, Strings::get(StringKey::FilterName));
    m_sortField->setItemText(2, Strings::get(StringKey::FilterTags));
    m_sortField->setItemText(3, Strings::get(StringKey::FilterFileSize));
    m_sortOrder->setToolTip(Strings::get(StringKey::FilterReverseOrder));
    if (m_mediaBtn) m_mediaBtn->updateLabel();
    if (auto* fmBtn = findChild<FilterModeHoverButton*>("filterModeHoverBtn"))
        static_cast<FilterModeHoverButton*>(fmBtn)->updateStyle(m_filterMode);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MediaHoverButton
// ─────────────────────────────────────────────────────────────────────────────
MediaHoverButton::MediaHoverButton(FilterBar* bar, QWidget* parent)
    : QToolButton(parent), m_bar(bar)
{
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(200);
    connect(m_hideTimer, &QTimer::timeout, this, &MediaHoverButton::hidePanel);
    updateLabel();
    setStyleSheet(
        "QToolButton { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.2);"
        "border-radius: 8px; color: rgba(200,220,215,0.9); font-size: 12px; font-weight: 600;"
        "padding: 2px 12px; }"
        "QToolButton:hover { background: rgba(0,180,160,0.25); border-color: rgba(0,180,160,0.6); }");
}

void MediaHoverButton::updateLabel() {
    bool img = AppSettings::instance().showImages();
    bool vid = AppSettings::instance().showVideos();
    bool aud = AppSettings::instance().showAudio();
    bool pdf = AppSettings::instance().showPdfs();
    QStringList active;
    if (img) active << "IMG";
    if (vid) active << "VID";
    if (aud) active << "AUD";
    if (pdf) active << "PDF";
    setText(QString("Medien %1").arg(active.isEmpty() ? "—" : active.join("")));
}

void MediaHoverButton::enterEvent(QEnterEvent*) {
    m_hideTimer->stop();
    QTimer::singleShot(80, this, &MediaHoverButton::showPanel);
}

void MediaHoverButton::leaveEvent(QEvent*) {
    m_hideTimer->start();
}

void MediaHoverButton::showPanel() {
    if (m_panel) return;
    buildPanel();
}

void MediaHoverButton::hidePanel() {
    if (m_panel) { m_panel->deleteLater(); m_panel = nullptr; }
}

void MediaHoverButton::buildPanel() {
    m_panel = new QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    m_panel->setObjectName("dropPanel");
    m_panel->setStyleSheet(panelStyle());
    m_panel->installEventFilter(this);

    auto* lay = new QVBoxLayout(m_panel);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);

    auto* hdr = new QLabel("Medien", m_panel);
    hdr->setStyleSheet("color: rgba(0,200,180,0.9); font-size: 11px; font-weight: bold;");
    lay->addWidget(hdr);

    auto* line = new QFrame(m_panel);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: rgba(255,255,255,0.1);");
    lay->addWidget(line);

    struct MediaToggle { QIcon icon; StringKey labelKey; bool (AppSettings::*getter)() const; void (AppSettings::*setter)(bool); };
    QList<MediaToggle> toggles = {
        { Icons::image(),  StringKey::FilterImages, &AppSettings::showImages, &AppSettings::setShowImages },
        { Icons::play(),   StringKey::FilterVideos, &AppSettings::showVideos, &AppSettings::setShowVideos },
        { Icons::music(),  StringKey::FilterAudio,  &AppSettings::showAudio,  &AppSettings::setShowAudio  },
        { Icons::pdf(),    StringKey::FilterPdf,    &AppSettings::showPdfs,   &AppSettings::setShowPdfs   },
    };

    for (const MediaToggle& t : toggles) {
        bool active = (AppSettings::instance().*t.getter)();
        auto* btn = new QPushButton(Strings::get(t.labelKey), m_panel);
        btn->setIcon(t.icon);
        btn->setIconSize(QSize(16, 16));
        btn->setCheckable(true);
        btn->setChecked(active);
        btn->setFixedHeight(28);
        auto applyStyle = [btn](bool on) {
            btn->setStyleSheet(QString(
                "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px;"
                "color: %3; font-size: 12px; font-weight: 600; padding: 0 12px; text-align: left; }"
                "QPushButton:hover { opacity: 0.85; }")
                .arg(on ? "rgba(40,180,80,0.25)" : "rgba(60,60,60,0.4)",
                     on ? "rgba(40,180,80,0.6)"  : "rgba(100,100,100,0.4)",
                     on ? "#50e080" : "#888888"));
        };
        applyStyle(active);
        auto setter = t.setter;
        connect(btn, &QPushButton::toggled, this, [this, setter, applyStyle, btn](bool on) {
            (AppSettings::instance().*setter)(on);
            applyStyle(on);
            updateLabel();
            emit m_bar->filterChanged();
        });
        lay->addWidget(btn);
    }

    QPoint global = mapToGlobal(QPoint(0, height() + 3));
    m_panel->adjustSize();
    m_panel->move(global);
    m_panel->show();
}

bool MediaHoverButton::eventFilter(QObject* obj, QEvent* ev) {
    if (m_panel) {
        QWidget* w = qobject_cast<QWidget*>(obj);
        bool inPanel = (w == m_panel || (w && m_panel->isAncestorOf(w)));
        if (inPanel) {
            if (ev->type() == QEvent::Enter) m_hideTimer->stop();
            if (ev->type() == QEvent::Leave && w == m_panel) m_hideTimer->start();
        }
    }
    return QToolButton::eventFilter(obj, ev);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FilterModeHoverButton
// ─────────────────────────────────────────────────────────────────────────────
FilterModeHoverButton::FilterModeHoverButton(FilterBar* bar, QWidget* parent)
    : QToolButton(parent), m_bar(bar)
{
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(200);
    connect(m_hideTimer, &QTimer::timeout, this, &FilterModeHoverButton::hidePanel);
    updateStyle(TagFilterMode::OR);
}

void FilterModeHoverButton::setVisible2(bool v) {
    setVisible(v);
}

void FilterModeHoverButton::updateStyle(TagFilterMode mode) {
    struct Info { const char* label; const char* color; };
    static const Info infos[] = {
        { "ODER",      "#6ab0ff" },
        { "UND",       "#00c8b4" },
        { "NUR",       "#ff9060" },
        { "INKLUSSIV", "#c090ff" },
    };
    auto& info = infos[(int)mode];
    setText(info.label);
    setStyleSheet(QString(
        "QToolButton { background: rgba(255,255,255,0.07); border: 1px solid %1;"
        "border-radius: 6px; color: %1; font-size: 10px; font-weight: bold; padding: 2px 8px; }"
        "QToolButton:hover { background: rgba(255,255,255,0.12); }").arg(info.color));
}

void FilterModeHoverButton::enterEvent(QEnterEvent*) {
    m_hideTimer->stop();
    QTimer::singleShot(80, this, &FilterModeHoverButton::showPanel);
}

void FilterModeHoverButton::leaveEvent(QEvent*) {
    m_hideTimer->start();
}

void FilterModeHoverButton::showPanel() {
    if (m_panel) return;
    buildPanel();
}

void FilterModeHoverButton::hidePanel() {
    if (m_panel) { m_panel->deleteLater(); m_panel = nullptr; }
}

void FilterModeHoverButton::buildPanel() {
    m_panel = new QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    m_panel->setObjectName("dropPanel");
    m_panel->setStyleSheet(panelStyle());
    m_panel->installEventFilter(this);

    auto* lay = new QVBoxLayout(m_panel);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(5);

    auto* hdr = new QLabel(Strings::get(StringKey::FilterModePanelHeader), m_panel);
    hdr->setStyleSheet("color: rgba(0,200,180,0.9); font-size: 11px; font-weight: bold;");
    lay->addWidget(hdr);

    struct ModeInfo { TagFilterMode mode; StringKey labelKey; const char* color; StringKey tipKey; };
    const ModeInfo modes[] = {
        { TagFilterMode::OR,       StringKey::FilterTagModeOr,       "#6ab0ff", StringKey::FilterModeOrTip       },
        { TagFilterMode::AND,      StringKey::FilterTagModeAnd,      "#00c8b4", StringKey::FilterModeAndTip      },
        { TagFilterMode::NUR,      StringKey::FilterTagModeNur,      "#ff9060", StringKey::FilterModeNurTip      },
        { TagFilterMode::INKLUSIV, StringKey::FilterTagModeInklusiv, "#c090ff", StringKey::FilterModeInklusivTip },
    };

    TagFilterMode current = m_bar->tagFilterMode();
    for (const ModeInfo& m : modes) {
        auto* btn = new QPushButton(m_panel);
        bool selected = (current == m.mode);
        btn->setText(QString("%1  %2").arg(selected ? "●" : "○", Strings::get(m.labelKey)));
        btn->setToolTip(Strings::get(m.tipKey));
        btn->setFixedHeight(28);
        QString col = m.color;
        btn->setStyleSheet(QString(
            "QPushButton { background: %1; border: 1px solid %2; border-radius: 6px;"
            "color: %2; font-size: 11px; font-weight: %3; padding: 0 12px; text-align: left; }"
            "QPushButton:hover { background: %4; }")
            .arg(selected ? QString("rgba(%1,%2,%3,0.2)").arg(QColor(col).red()).arg(QColor(col).green()).arg(QColor(col).blue()) : "transparent",
                 col,
                 selected ? "bold" : "normal",
                 QString("rgba(%1,%2,%3,0.25)").arg(QColor(col).red()).arg(QColor(col).green()).arg(QColor(col).blue())));
        TagFilterMode capturedMode = m.mode;
        connect(btn, &QPushButton::clicked, this, [this, capturedMode]{
            m_bar->onFilterModeClicked(capturedMode);
            hidePanel();
        });
        lay->addWidget(btn);
    }

    QPoint global = mapToGlobal(QPoint(0, height() + 3));
    m_panel->adjustSize();
    m_panel->move(global);
    m_panel->show();
}

bool FilterModeHoverButton::eventFilter(QObject* obj, QEvent* ev) {
    if (m_panel) {
        QWidget* w = qobject_cast<QWidget*>(obj);
        bool inPanel = (w == m_panel || (w && m_panel->isAncestorOf(w)));
        if (inPanel) {
            if (ev->type() == QEvent::Enter) m_hideTimer->stop();
            if (ev->type() == QEvent::Leave && w == m_panel) m_hideTimer->start();
        }
    }
    return QToolButton::eventFilter(obj, ev);
}

// ─────────────────────────────────────────────────────────────────────────────
//  HoverDropdown
// ─────────────────────────────────────────────────────────────────────────────
HoverDropdown::HoverDropdown(const QString& label, Mode mode, FilterBar* bar, QWidget* parent)
    : QToolButton(parent), m_bar(bar), m_mode(mode), m_label(label)
{
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(200);
    connect(m_hideTimer, &QTimer::timeout, this, &HoverDropdown::hidePanel);

    m_dragOpenTimer = new QTimer(this);
    m_dragOpenTimer->setSingleShot(true);
    m_dragOpenTimer->setInterval(600);
    connect(m_dragOpenTimer, &QTimer::timeout, this, &HoverDropdown::showPanel);

    setText(label);
    setAcceptDrops(true);
    applyStyle();
}

void HoverDropdown::applyStyle() {
    setStyleSheet(
        "QToolButton { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.2);"
        "border-radius: 8px; color: rgba(200,220,215,0.9); font-size: 12px; font-weight: 600;"
        "padding: 2px 12px; }"
        "QToolButton:hover { background: rgba(0,180,160,0.25); border-color: rgba(0,180,160,0.6); }");
}

void HoverDropdown::rebuild() {
    if (m_panel) hidePanel();
}

void HoverDropdown::enterEvent(QEnterEvent*) {
    m_hideTimer->stop();
    QTimer::singleShot(80, this, &HoverDropdown::showPanel);
}

void HoverDropdown::leaveEvent(QEvent*) {
    m_hideTimer->start();
}

void HoverDropdown::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(kTagDragMime)) {
        e->acceptProposedAction();
        m_dragOpenTimer->start();
        setStyleSheet(
            "QToolButton { background: rgba(0,180,160,0.4); border: 1px solid #00c8b4;"
            "border-radius: 8px; color: #00c8b4; font-size: 12px; font-weight: 600;"
            "padding: 2px 12px; }");
    }
}

void HoverDropdown::dragLeaveEvent(QDragLeaveEvent*) {
    m_dragOpenTimer->stop();
    applyStyle();
}

void HoverDropdown::showPanel() {
    if (m_panel) return;
    buildPanel();
}

void HoverDropdown::hidePanel() {
    if (m_panel) { m_panel->deleteLater(); m_panel = nullptr; }
}

void HoverDropdown::buildPanel() {
    m_panel = new QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    m_panel->setObjectName("dropPanel");
    m_panel->setStyleSheet(panelStyle());
    m_panel->setAcceptDrops(true);
    m_panel->installEventFilter(this);

    auto* outerLay = new QVBoxLayout(m_panel);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(0);

    auto* hdrRow = new QWidget(m_panel);
    hdrRow->setStyleSheet("background: none; border: none;");
    auto* hdrLay = new QHBoxLayout(hdrRow);
    hdrLay->setContentsMargins(10, 6, 6, 4);
    hdrLay->setSpacing(4);

    auto* hdr = new QLabel(m_label, hdrRow);
    hdr->setStyleSheet("color: rgba(0,200,180,0.9); font-size: 11px; font-weight: bold;"
                       "background: none; border: none;");
    hdrLay->addWidget(hdr, 1);

    // „+" button only visible for Categories panel
    if (m_mode == Mode::Categories) {
        auto* addCatBtn = new QToolButton(hdrRow);
        addCatBtn->setText("+");
        addCatBtn->setToolTip(Strings::get(StringKey::FilterAddCategory));
        addCatBtn->setFixedSize(20, 20);
        addCatBtn->setStyleSheet(
            "QToolButton { background: rgba(0,180,160,0.18); border: 1px solid rgba(0,180,160,0.45);"
            "border-radius: 10px; color: #00c8b4; font-size: 14px; font-weight: bold; }"
            "QToolButton:hover { background: rgba(0,180,160,0.4); }");
        // We need to call onAddCategory on the bar, but close the panel after
        FilterBar* bar = m_bar;
        connect(addCatBtn, &QToolButton::clicked, this, [this, bar]{
            hidePanel();
            bar->onAddCategory();
        });
        hdrLay->addWidget(addCatBtn);
    }

    outerLay->addWidget(hdrRow);

    auto* line = new QFrame(m_panel);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: rgba(255,255,255,0.1);");
    outerLay->addWidget(line);

    auto* scroll = new QScrollArea(m_panel);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setMaximumHeight(300);
    scroll->installEventFilter(this);

    auto* inner = new QWidget(scroll);
    inner->setAcceptDrops(true);
    inner->installEventFilter(this);
    auto* lay = new QVBoxLayout(inner);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(3);

    if (m_mode == Mode::Tags)
        populateTagsPanel(lay);
    else
        populateCatsPanel(lay);

    lay->addStretch(1);
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    outerLay->addWidget(scroll);

    QPoint global = mapToGlobal(QPoint(0, height() + 3));
    m_panel->move(global);
    m_panel->adjustSize();

    auto* scr = QApplication::screenAt(global);
    if (scr) {
        QRect avail = scr->availableGeometry();
        int x = global.x();
        if (x + m_panel->width() > avail.right())
            x = avail.right() - m_panel->width();
        m_panel->move(x, global.y());
    }

    m_panel->show();
}

void HoverDropdown::populateTagsPanel(QVBoxLayout* lay) {
    // Show ALL tags as a flat list — no group headers
    QStringList all = m_bar->m_tagMgr->allTags();

    if (all.isEmpty()) {
        auto* lbl = new QLabel(Strings::get(StringKey::FilterNoTags), lay->parentWidget());
        lbl->setStyleSheet("color: rgba(200,220,215,0.4); font-size: 11px; background: none; border: none;");
        lay->addWidget(lbl);
        return;
    }

    const QStringList allConst = all;
    for (const QString& tag : allConst) {
        auto* chip = new FilterTagChip(tag, m_bar, lay->parentWidget());
        chip->setChecked(m_bar->m_activeTags.contains(tag));
        chip->applyStyle();
        chip->installEventFilter(this);
        connect(chip, &QPushButton::toggled, this, [this, tag](bool on){
            m_bar->onTagToggled(tag, on);
        });
        lay->addWidget(chip);
    }
}

void HoverDropdown::populateCatsPanel(QVBoxLayout* lay) {
    const auto& cats = m_bar->m_tagMgr->categories();
    if (cats.isEmpty()) {
        auto* lbl = new QLabel(Strings::get(StringKey::FilterNoCategories), lay->parentWidget());
        lbl->setStyleSheet("color: rgba(200,220,215,0.4); font-size: 11px; background: none; border: none;");
        lay->addWidget(lbl);
        return;
    }
    for (const TagCategory& cat : cats)
        addCatSection(lay, cat, 0);
}

void HoverDropdown::addCatSection(QVBoxLayout* lay, const TagCategory& cat, int depth) {
    QColor c = cat.color;

    auto* catBtn = new QPushButton(lay->parentWidget());
    catBtn->setCheckable(true);
    catBtn->setChecked(m_bar->m_activeCategories.contains(cat.id));
    catBtn->setFixedHeight(26);
    catBtn->setAcceptDrops(true);
    catBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    catBtn->installEventFilter(this);

    QString indent = QString("  ").repeated(depth);
    catBtn->setText(indent + (depth > 0 ? "↳ " : "") + cat.name);

    catBtn->setStyleSheet(QString(
        "QPushButton { background: rgba(%1,%2,%3,0.12); border: 1px solid rgba(%1,%2,%3,0.45);"
        "border-radius: 8px; color: %4; font-size: 11px; font-weight: 700; padding: 0 10px;"
        "text-align: left; }"
        "QPushButton:checked { background: rgba(%1,%2,%3,0.4); border-color: %4; }"
        "QPushButton:hover   { background: rgba(%1,%2,%3,0.3); }")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.name()));

    QString catId = cat.id;

    // ── Safe context menu ─────────────────────────────────────────────────────
    connect(catBtn, &QPushButton::customContextMenuRequested, this, [this, catId](const QPoint&) {
        std::function<const TagCategory*(const QList<TagCategory>&, const QString&)> findCat;
        findCat = [&](const QList<TagCategory>& cats, const QString& id) -> const TagCategory* {
            for (const TagCategory& c : cats) {
                if (c.id == id) return &c;
                if (auto* ch = findCat(c.children, id)) return ch;
            }
            return nullptr;
        };
        const TagCategory* cat = findCat(m_bar->m_tagMgr->categories(), catId);
        if (!cat) return;

        QMenu menu;
        menu.setStyleSheet(menuStyle());
        menu.addAction(Strings::get(StringKey::FilterCatRename), this, [this, catId]{
            bool ok;
            std::function<const TagCategory*(const QList<TagCategory>&, const QString&)> fc;
            fc = [&](const QList<TagCategory>& cats, const QString& id) -> const TagCategory* {
                for (const TagCategory& c : cats) { if (c.id == id) return &c; if (auto* ch = fc(c.children, id)) return ch; }
                return nullptr;
            };
            const TagCategory* c = fc(m_bar->m_tagMgr->categories(), catId);
            QString cur = c ? c->name : "";
            QString n = QInputDialog::getText(this,
                Strings::get(StringKey::FilterCatRenamePrompt),
                Strings::get(StringKey::FilterCatNewName),
                QLineEdit::Normal, cur, &ok);
            if (ok && !n.trimmed().isEmpty())
                m_bar->m_tagMgr->renameCategory(catId, n.trimmed());
        });
        QColor catColor = cat->color;
        bool catUniform = cat->uniformColor;
        menu.addAction(Strings::get(StringKey::FilterCatChangeColor), this, [this, catId, catColor, catUniform]{
            QColor c = QColorDialog::getColor(catColor, this,
                Strings::get(StringKey::FilterCatChangeColor));
            if (c.isValid())
                m_bar->m_tagMgr->setCategoryUniformColor(catId, catUniform, c);
        });
        menu.addSeparator();
        menu.addAction(Strings::get(StringKey::FilterCatAddSub), this, [this, catId]{
            bool ok;
            QString n = QInputDialog::getText(this,
                Strings::get(StringKey::FilterCatNewSubPrompt),
                Strings::get(StringKey::FilterCatNewName),
                QLineEdit::Normal, "", &ok);
            if (ok && !n.trimmed().isEmpty())
                m_bar->m_tagMgr->addSubcategory(catId, TagCategory::create(n.trimmed()));
        });
        // ── Add Tag to this category ──────────────────────────────────────────
        menu.addSeparator();
        // Sub-menu: existing uncategorized tags first, then "New tag..."
        QMenu* addTagMenu = menu.addMenu(Strings::get(StringKey::CatPanelAddTag));
        addTagMenu->setStyleSheet(menuStyle());
        {
            // Offer all existing tags that are not yet in this category
            QStringList all = m_bar->m_tagMgr->allTags();
            std::function<const TagCategory*(const QList<TagCategory>&, const QString&)> fcTag;
            fcTag = [&](const QList<TagCategory>& cats, const QString& id) -> const TagCategory* {
                for (const TagCategory& c : cats) { if (c.id == id) return &c; if (auto* ch = fcTag(c.children, id)) return ch; }
                return nullptr;
            };
            const TagCategory* thisCat = fcTag(m_bar->m_tagMgr->categories(), catId);
            QStringList alreadyIn = thisCat ? thisCat->tags : QStringList();
            bool addedAny = false;
            for (const QString& t : all) {
                if (alreadyIn.contains(t)) continue;
                QAction* act = addTagMenu->addAction(t);
                connect(act, &QAction::triggered, this, [this, catId, t]{
                    m_bar->m_tagMgr->addTagToCategory(catId, t);
                });
                addedAny = true;
            }
            if (addedAny) addTagMenu->addSeparator();
            // "New tag..." option
            QAction* newTagAct = addTagMenu->addAction(Strings::get(StringKey::FilterNewTag));
            connect(newTagAct, &QAction::triggered, this, [this, catId]{
                bool ok;
                QString n = QInputDialog::getText(this,
                    Strings::get(StringKey::CatPanelAddTag),
                    Strings::get(StringKey::FilterNewTagLabel),
                    QLineEdit::Normal, "", &ok);
                if (ok && !n.trimmed().isEmpty())
                    m_bar->m_tagMgr->addTagToCategory(catId, n.trimmed());
            });
        }
        menu.addSeparator();
        menu.addAction(Strings::get(StringKey::FilterCatDelete), this, [this, catId]{
            m_bar->m_tagMgr->deleteCategory(catId);
        });
        menu.exec(QCursor::pos());
    });

    connect(catBtn, &QPushButton::toggled, this, [this, catId](bool on){
        m_bar->onCategoryToggled(catId, on);
    });
    lay->addWidget(catBtn);

    // Individual tags in this category
    if (!cat.tags.isEmpty()) {
        auto* tagsWidget = new QWidget(lay->parentWidget());
        tagsWidget->setAcceptDrops(true);
        tagsWidget->installEventFilter(this);
        auto* tagsLay = new QHBoxLayout(tagsWidget);
        tagsLay->setContentsMargins((depth + 1) * 10, 0, 0, 2);
        tagsLay->setSpacing(3);

        for (const QString& tag : cat.tags) {
            QColor tc = cat.uniformColor ? cat.color : m_bar->m_tagMgr->tagColor(tag);
            auto* tagBtn = new FilterTagChip(tag, m_bar, tagsWidget);
            tagBtn->setChecked(m_bar->m_activeTags.contains(tag));
            tagBtn->setFixedHeight(20);
            tagBtn->setStyleSheet(QString(
                "QPushButton { background: rgba(%1,%2,%3,0.15); border: 1px solid rgba(%1,%2,%3,0.4);"
                "border-radius: 8px; color: %4; font-size: 10px; font-weight: 600; padding: 0 7px; }"
                "QPushButton:checked { background: rgba(%1,%2,%3,0.4); border-color: %4; }"
                "QPushButton:hover   { background: rgba(%1,%2,%3,0.3); }")
                .arg(tc.red()).arg(tc.green()).arg(tc.blue()).arg(tc.name()));
            tagBtn->installEventFilter(this);
            connect(tagBtn, &QPushButton::toggled, this, [this, tag](bool on){
                m_bar->onTagToggled(tag, on);
            });
            tagsLay->addWidget(tagBtn);
        }
        tagsLay->addStretch(1);
        lay->addWidget(tagsWidget);
    }

    for (const TagCategory& child : cat.children)
        addCatSection(lay, child, depth + 1);
}

bool HoverDropdown::eventFilter(QObject* obj, QEvent* ev) {
    if (m_panel) {
        QWidget* w = qobject_cast<QWidget*>(obj);
        bool isInPanel = (w == m_panel || (w && m_panel->isAncestorOf(w)));
        if (isInPanel) {
            if (ev->type() == QEvent::Enter)  m_hideTimer->stop();
            if (ev->type() == QEvent::Leave) {
                if (w == m_panel) m_hideTimer->start();
            }
            if (ev->type() == QEvent::DragLeave) {
                m_hideTimer->start();
                applyStyle();
            }
        }
    }
    return QToolButton::eventFilter(obj, ev);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FilterTagChip
// ─────────────────────────────────────────────────────────────────────────────
FilterTagChip::FilterTagChip(const QString& tag, FilterBar* bar, QWidget* parent)
    : QPushButton(tag, parent), m_tag(tag), m_bar(bar)
{
    setCheckable(true);
    setFixedHeight(24);
    // Use DefaultContextMenu so contextMenuEvent() is called directly on right-click
    setContextMenuPolicy(Qt::DefaultContextMenu);
    applyStyle();
    connect(this, &QPushButton::toggled, this, [this](bool){ applyStyle(); });

    // Hover timer for info tooltip panel
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(500);
    connect(m_hoverTimer, &QTimer::timeout, this, &FilterTagChip::showInfoPanel);
}

void FilterTagChip::applyStyle() {
    QColor c     = m_bar->m_tagMgr->tagColor(m_tag);
    bool active  = isChecked();
    QString abg  = c.name();
    QString ibg  = QString("rgba(%1,%2,%3,0.18)").arg(c.red()).arg(c.green()).arg(c.blue());
    QString tac  = (c.lightness() > 130) ? "#111" : "#fff";
    setStyleSheet(QString(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 10px;"
        "color: %3; font-size: 11px; font-weight: 600; padding: 0 10px; }"
        "QPushButton:checked { background: %4; color: %5; border-color: %4; }"
        "QPushButton:hover { background: %6; }")
        .arg(active ? abg : ibg, c.name(),
             active ? tac : c.name(),
             abg, tac,
             c.lighter(130).name()));
}

void FilterTagChip::showInfoPanel() {
    hideInfoPanel();

    // Collect categories this tag belongs to
    QStringList memberOf;
    std::function<void(const QList<TagCategory>&)> collect = [&](const QList<TagCategory>& cats) {
        for (const TagCategory& cat : cats) {
            if (cat.tags.contains(m_tag)) memberOf << cat.name;
            collect(cat.children);
        }
    };
    collect(m_bar->m_tagMgr->categories());

    m_infoPanel = new QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    m_infoPanel->setStyleSheet(
        "QFrame { background: #1a2830; border: 1px solid rgba(0,180,160,0.5);"
        "border-radius: 8px; }"
        "QLabel { background: transparent; border: none; }");

    auto* lay = new QVBoxLayout(m_infoPanel);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(4);

    // Tag name header with color swatch
    QColor tc = m_bar->m_tagMgr->tagColor(m_tag);
    auto* nameRow = new QHBoxLayout;
    auto* dot = new QLabel(m_infoPanel);
    dot->setFixedSize(10, 10);
    dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(tc.name()));
    nameRow->addWidget(dot);
    auto* nameLbl = new QLabel(QString("<b>%1</b>").arg(m_tag), m_infoPanel);
    nameLbl->setStyleSheet(QString("color: %1; font-size: 12px;").arg(tc.name()));
    nameLbl->setTextFormat(Qt::RichText);
    nameRow->addWidget(nameLbl);
    nameRow->addStretch();
    lay->addLayout(nameRow);

    // Separator
    auto* sep = new QFrame(m_infoPanel);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: rgba(255,255,255,0.1); background: rgba(255,255,255,0.1);");
    sep->setFixedHeight(1);
    lay->addWidget(sep);

    // Category memberships
    if (memberOf.isEmpty()) {
        auto* lbl = new QLabel(Strings::get(StringKey::FilterNoCategories), m_infoPanel);
        lbl->setStyleSheet("color: rgba(180,200,195,0.45); font-size: 10px; font-style: italic;");
        lay->addWidget(lbl);
    } else {
        auto* hdr = new QLabel(Strings::get(StringKey::SettingsTabCategories) + ":", m_infoPanel);
        hdr->setStyleSheet("color: rgba(0,200,180,0.7); font-size: 10px; font-weight: bold;");
        lay->addWidget(hdr);
        for (const QString& catName : memberOf) {
            auto* cl = new QLabel(QString("  %1").arg(catName), m_infoPanel);
            cl->setStyleSheet("color: rgba(200,220,215,0.85); font-size: 11px;");
            lay->addWidget(cl);
        }
    }

    auto* hint = new QLabel(Strings::get(StringKey::TagBarCompactTooltip), m_infoPanel);
    hint->setStyleSheet("color: rgba(150,170,165,0.4); font-size: 9px; font-style: italic;");
    lay->addWidget(hint);

    m_infoPanel->adjustSize();
    QPoint gp = mapToGlobal(QPoint(0, height() + 4));
    // Clamp to screen
    if (auto* scr = QApplication::screenAt(gp)) {
        QRect avail = scr->availableGeometry();
        if (gp.x() + m_infoPanel->width() > avail.right())
            gp.setX(avail.right() - m_infoPanel->width());
    }
    m_infoPanel->move(gp);
    m_infoPanel->show();
}

void FilterTagChip::hideInfoPanel() {
    if (m_infoPanel) {
        m_infoPanel->deleteLater();
        m_infoPanel = nullptr;
    }
}

void FilterTagChip::enterEvent(QEnterEvent* e) {
    m_hoverTimer->start();
    QPushButton::enterEvent(e);
}

void FilterTagChip::leaveEvent(QEvent* e) {
    m_hoverTimer->stop();
    hideInfoPanel();
    QPushButton::leaveEvent(e);
}

void FilterTagChip::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragStart = e->pos();
        // Hide info panel on any click
        m_hoverTimer->stop();
        hideInfoPanel();
        QPushButton::mousePressEvent(e);
    }
    // Right-click: do NOT call base — contextMenuEvent handles it
}

void FilterTagChip::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    if ((e->pos() - m_dragStart).manhattanLength() < QApplication::startDragDistance()) return;

    QPointer<FilterTagChip> guard(this);

    auto* drag = new QDrag(this);
    auto* mime = new QMimeData;
    mime->setData(kTagDragMime, QString("%1\n").arg(m_tag).toUtf8());
    drag->setMimeData(mime);
    QPixmap px(size()); px.fill(Qt::transparent);
    render(&px);
    drag->setPixmap(px);
    drag->setHotSpot(e->pos());
    drag->exec(Qt::CopyAction);
}

void FilterTagChip::contextMenuEvent(QContextMenuEvent*) {
    // Stop hover timer and close info panel BEFORE opening menu
    m_hoverTimer->stop();
    hideInfoPanel();

    // Guard against self-deletion while menu is open
    QPointer<FilterTagChip> guard(this);

    // Use nullptr as parent so the menu is not destroyed with the chip
    QMenu menu;
    menu.setStyleSheet(menuStyle());

    // Capture tag and bar as local values — safe even if 'this' is deleted
    const QString  tag = m_tag;
    FilterBar*     bar = m_bar;
    TagManager*    mgr = m_bar->m_tagMgr;

    // ── Color ────────────────────────────────────────────────────────────────
    menu.addAction(QString("%1 \"%2\"").arg(Strings::get(StringKey::FilterCatChangeColor), tag), [guard, tag, mgr]{
        if (!guard) return;
        QColor c = QColorDialog::getColor(mgr->tagColor(tag), nullptr,
            Strings::get(StringKey::FilterCatChangeColor));
        if (c.isValid()) {
            mgr->setTagColor(tag, c);
            if (guard) guard->applyStyle();
        }
    });

    menu.addAction(QString("%1 \"%2\"").arg(Strings::get(StringKey::FilterTagRename), tag), [guard, tag, mgr]{
        if (!guard) return;
        bool ok;
        QString n = QInputDialog::getText(nullptr,
            Strings::get(StringKey::FilterTagRenamePrompt),
            Strings::get(StringKey::FilterTagNewName),
            QLineEdit::Normal, tag, &ok);
        if (ok && !n.trimmed().isEmpty())
            mgr->renameTag(tag, n.trimmed());
    });

    menu.addSeparator();

    // ── Remove from categories (show one action per category it's in) ────────
    bool anyRemove = false;
    std::function<void(const QList<TagCategory>&)> addRemove;
    addRemove = [&](const QList<TagCategory>& cats) {
        for (const TagCategory& cat : cats) {
            if (cat.tags.contains(tag)) {
                anyRemove = true;
                QString catId   = cat.id;
                QString catName = cat.name;
                menu.addAction(QString("%1 \"%2\"").arg(
                    Strings::get(StringKey::CatPanelDelete), catName),
                               [guard, catId, tag, mgr]{
                    if (!guard) return;
                    mgr->removeTagFromCategory(catId, tag);
                });
            }
            addRemove(cat.children);
        }
    };
    addRemove(mgr->categories());
    if (anyRemove) menu.addSeparator();

    // ── Add to category / subcategory ─────────────────────────────────────────
    QMenu* addMenu = menu.addMenu(Strings::get(StringKey::CatPanelAddCategory));
    addMenu->setStyleSheet(menuStyle());

    std::function<void(QMenu*, const QList<TagCategory>&)> populate;
    populate = [&](QMenu* parent, const QList<TagCategory>& cats) {
        for (const TagCategory& cat : cats) {
            bool alreadyIn = cat.tags.contains(tag);
            QString catId   = cat.id;
            QString catName = cat.name;
            if (!cat.children.isEmpty()) {
                QMenu* sub = parent->addMenu(catName);
                sub->setStyleSheet(menuStyle());
                QAction* selfAct = sub->addAction(
                    alreadyIn ? Strings::get(StringKey::FilterTagAlreadyIn, catName)
                              : Strings::get(StringKey::FilterTagAddTo, catName));
                selfAct->setEnabled(!alreadyIn);
                connect(selfAct, &QAction::triggered, [guard, catId, tag, mgr]{
                    if (!guard) return;
                    mgr->addTagToCategory(catId, tag);
                });
                sub->addSeparator();
                populate(sub, cat.children);
            } else {
                QAction* act = parent->addAction(
                    alreadyIn ? Strings::get(StringKey::FilterTagAlreadyIn, catName)
                              : Strings::get(StringKey::FilterTagAddTo, catName));
                act->setEnabled(!alreadyIn);
                connect(act, &QAction::triggered, [guard, catId, tag, mgr]{
                    if (!guard) return;
                    mgr->addTagToCategory(catId, tag);
                });
            }
        }
    };
    populate(addMenu, mgr->categories());
    if (addMenu->isEmpty())
        addMenu->addAction(Strings::get(StringKey::FilterNoCategories))->setEnabled(false);

    menu.addSeparator();
    menu.addAction(Strings::get(StringKey::SettingsTagDelete), [guard, tag, mgr]{
        if (!guard) return;
        mgr->deleteTag(tag);
    });

    menu.addSeparator();
    // ── Medien hinzufügen Modus ───────────────────────────────────────────────
    menu.addAction(Strings::get(StringKey::CatPanelAddToTagMode, tag), [guard, bar, tag]{
        if (!guard) return;
        emit bar->enterAddToTagModeRequested(tag);
    });

    menu.exec(QCursor::pos());
    // 'this' may be invalid here — do not touch any members
}
