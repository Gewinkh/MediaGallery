#include "TagWidget.h"
#include <QColorDialog>
#include <QInputDialog>
#include <utility>
#include <QMenu>
#include <QAction>
#include <QStyle>
#include <QPainter>
#include <QFontMetrics>
#include <QStringListModel>
#include <QApplication>
#include <QScreen>

// ─────────────────────────────────────────────────────────────────────────────
// TagPill
// ─────────────────────────────────────────────────────────────────────────────

TagPill::TagPill(const QString& tag, const QColor& color, bool editable, QWidget* parent)
    : QWidget(parent), m_tag(tag)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setContentsMargins(0, 0, 0, 0);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(5);

    // Colored dot
    auto* dot = new QLabel(this);
    dot->setFixedSize(8, 8);
    dot->setAttribute(Qt::WA_StyledBackground, true);
    dot->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(color.name()));
    lay->addWidget(dot);

    // Tag text in tag color
    QString txtColor = (color.lightness() > 160) ? color.darker(160).name() : color.name();
    auto* lbl = new QLabel(tag, this);
    lbl->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 700; background: transparent;")
                           .arg(txtColor));
    lay->addWidget(lbl);

    if (editable) {
        auto* btn = new QToolButton(this);
        btn->setText("✕");
        btn->setFixedSize(14, 14);
        btn->setStyleSheet("QToolButton { border: none; background: transparent; "
                           "color: rgba(255,255,255,0.5); font-size: 10px; }");
        connect(btn, &QToolButton::clicked, this, [this]() {
            emit removeRequested(m_tag);
        });
        lay->addWidget(btn);
    }

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(tr("Farbe ändern"), this, [this]() {
            emit colorChangeRequested(m_tag);
        });
        menu.exec(mapToGlobal(pos));
    });

    setStyleSheet("TagPill { background: rgba(255,255,255,0.05); border-radius: 10px; }"
                  "TagPill:hover { background: rgba(255,255,255,0.10); }");
    setFixedHeight(22);
}

// ─────────────────────────────────────────────────────────────────────────────
// TagBar
// ─────────────────────────────────────────────────────────────────────────────

TagBar::TagBar(TagManager* mgr, QWidget* parent)
    : QWidget(parent), m_tagMgr(mgr)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(4);
    m_layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Tag hinzufügen…"));
    m_input->setFixedWidth(130);
    m_input->setFixedHeight(22);
    m_input->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 10px; color: white; padding: 0 8px; font-size: 11px; }"
        "QLineEdit:focus { border-color: rgba(0,200,180,0.6); }");

    m_completer = new QCompleter(mgr->allTags(), this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_input->setCompleter(m_completer);

    connect(m_input, &QLineEdit::returnPressed, this, &TagBar::addTagFromInput);
    connect(mgr, &TagManager::tagsChanged, this, [this]() {
        m_completer->setModel(new QStringListModel(m_tagMgr->allTags(), m_completer));
        refresh();
    });
    connect(mgr, &TagManager::tagColorChanged, this, [this]() {
        refresh();
    });
    connect(mgr, &TagManager::categoriesChanged, this, [this]() {
        refresh();
    });

    m_layout->addWidget(m_input);

    // ── Tag-Dropdown „▾" ──────────────────────────────────────────────────────
    m_dropBtn = new QToolButton(this);
    m_dropBtn->setText("▾");
    m_dropBtn->setFixedSize(20, 22);
    m_dropBtn->setStyleSheet(
        "QToolButton { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 8px; color: rgba(180,210,205,0.8); font-size: 11px; }"
        "QToolButton:hover { background: rgba(0,180,160,0.25); color: #00c8b4; }");
    m_dropBtn->setToolTip(tr("Tags auswählen"));
    connect(m_dropBtn, &QToolButton::clicked, this, &TagBar::showTagDropdown);
    m_layout->addWidget(m_dropBtn);

    // ── Kategorie-Dropdown „☰" ────────────────────────────────────────────────
    m_catBtn = new QToolButton(this);
    m_catBtn->setText("☰");
    m_catBtn->setFixedSize(20, 22);
    m_catBtn->setStyleSheet(
        "QToolButton { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.15);"
        "border-radius: 8px; color: rgba(180,210,205,0.8); font-size: 11px; }"
        "QToolButton:hover { background: rgba(0,180,160,0.25); color: #00c8b4; }");
    m_catBtn->setToolTip(tr("Nach Kategorie auswählen"));
    connect(m_catBtn, &QToolButton::clicked, this, &TagBar::showCategoryDropdown);
    m_layout->addWidget(m_catBtn);

    m_layout->addStretch();
}

void TagBar::setFile(const QString& fileName) { m_fileName = fileName; refresh(); }

void TagBar::setEditable(bool e) {
    m_editable = e;
    m_input->setVisible(e && !m_compact);
    if (m_dropBtn) m_dropBtn->setVisible(e);
    if (m_catBtn)  m_catBtn->setVisible(e);
    refresh();
}

void TagBar::refresh() { rebuildPills(); }
void TagBar::retranslate() { m_input->setPlaceholderText(tr("Tag hinzufügen…")); }

void TagBar::rebuildPills() {
    for (auto* p : std::as_const(m_pills)) { m_layout->removeWidget(p); p->deleteLater(); }
    m_pills.clear();
    if (m_fileName.isEmpty()) return;
    QStringList tags = m_tagMgr->tagsForFile(m_fileName);
    int insertPos = 0;
    for (const auto& tag : std::as_const(tags)) {
        QColor c = m_tagMgr->tagColor(tag);
        auto* pill = new TagPill(tag, c, m_editable, this);
        connect(pill, &TagPill::removeRequested,    this, &TagBar::removeTag);
        connect(pill, &TagPill::colorChangeRequested, this, &TagBar::onTagColorRequested);
        m_layout->insertWidget(insertPos++, pill);
        m_pills.append(pill);
    }
}

void TagBar::addTagFromInput() {
    QString tag = m_input->text().trimmed();
    if (tag.isEmpty() || m_fileName.isEmpty()) return;
    m_tagMgr->addTagToFile(m_fileName, tag);
    m_input->clear();
    emit tagsModified(m_fileName, m_tagMgr->tagsForFile(m_fileName));
}

void TagBar::removeTag(const QString& tag) {
    m_tagMgr->removeTagFromFile(m_fileName, tag);
    emit tagsModified(m_fileName, m_tagMgr->tagsForFile(m_fileName));
}

void TagBar::onTagColorRequested(const QString& tag) {
    QColor c = QColorDialog::getColor(m_tagMgr->tagColor(tag), this, tr("Tag-Farbe wählen"));
    if (c.isValid()) m_tagMgr->setTagColor(tag, c);
}

void TagBar::setCompact(bool compact) {
    m_compact = compact;
    m_input->setVisible(!compact && m_editable);
    auto styleBtn = [](QToolButton* btn, bool compact, const QString& txt, const QString& tip) {
        if (!btn) return;
        btn->setVisible(true);
        btn->setText(compact ? "+" : txt);
        btn->setToolTip(tip);
        btn->setFixedSize(compact ? 18 : 20, compact ? 18 : 22);
    };
    styleBtn(m_dropBtn, compact, "▾", tr("Tags auswählen"));
    styleBtn(m_catBtn,  compact, "☰", tr("Nach Kategorie auswählen"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel-Positionierung (nach oben klappen)
// ─────────────────────────────────────────────────────────────────────────────

void TagBar::positionPanelAbove(QFrame* panel, QWidget* anchor) {
    panel->adjustSize();
    QPoint ag = anchor->mapToGlobal(QPoint(0, 0));
    int x = ag.x();
    int y = ag.y() - panel->height() - 3;
    if (auto* scr = QApplication::screenAt(ag)) {
        QRect avail = scr->availableGeometry();
        if (x + panel->width() > avail.right())  x = avail.right() - panel->width();
        if (x < avail.left())                     x = avail.left();
        if (y < avail.top())                      y = avail.top();
    }
    panel->move(x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tag-Dropdown (alle Tags flach)
// ─────────────────────────────────────────────────────────────────────────────

static QString tagBtnStyle(bool has, const QColor& tc) {
    QString bga = tc.name();
    QString bgi = QString("rgba(%1,%2,%3,0.15)").arg(tc.red()).arg(tc.green()).arg(tc.blue());
    QString txt = (tc.lightness() > 130) ? "#111" : "#fff";
    return QString(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 10px;"
        "color: %3; font-size: 11px; font-weight: 600; padding: 0 10px; text-align: left; }"
        "QPushButton:hover { background: %4; }")
        .arg(has ? bga : bgi, tc.name(), has ? txt : tc.name(), tc.lighter(130).name());
}

QFrame* TagBar::buildTagPanel() {
    auto* panel = new QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    panel->setStyleSheet(
        "QFrame { background: #1a2830; border: 1px solid rgba(0,180,160,0.45); border-radius: 8px; }"
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 4px; background: transparent; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.2); border-radius: 2px; }");

    auto* outerLay = new QVBoxLayout(panel);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(0);

    auto* hdrRow = new QWidget(panel);
    hdrRow->setStyleSheet("background: rgba(0,0,0,0.2); border: none;");
    auto* hdrLay = new QHBoxLayout(hdrRow);
    hdrLay->setContentsMargins(10, 6, 10, 4);
    auto* hdrLbl = new QLabel(tr("Tags"), hdrRow);
    hdrLbl->setStyleSheet("color: rgba(0,200,180,0.9); font-size: 11px; font-weight: bold; background: none; border: none;");
    hdrLay->addWidget(hdrLbl);
    outerLay->addWidget(hdrRow);

    auto* sep = new QFrame(panel);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: rgba(255,255,255,0.1); background: rgba(255,255,255,0.1);");
    sep->setFixedHeight(1);
    outerLay->addWidget(sep);

    auto* scroll = new QScrollArea(panel);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setMaximumHeight(260);

    auto* inner = new QWidget(scroll);
    auto* lay   = new QVBoxLayout(inner);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(2);

    QStringList currentTags = m_tagMgr->tagsForFile(m_fileName);
    QStringList allTags     = m_tagMgr->allTags();

    if (allTags.isEmpty()) {
        auto* lbl = new QLabel(tr("(Keine Tags vorhanden)"), inner);
        lbl->setStyleSheet("color: rgba(200,220,215,0.4); font-size: 11px;");
        lay->addWidget(lbl);
    } else {
        for (const QString& tag : allTags) {
            bool has = currentTags.contains(tag);
            QColor tc = m_tagMgr->tagColor(tag);
            auto* btn = new QPushButton(inner);
            btn->setFixedHeight(24);
            btn->setCheckable(true);
            btn->setChecked(has);
            btn->setText((has ? "✓  " : "    ") + tag);
            btn->setStyleSheet(tagBtnStyle(has, tc));

            QString capturedTag = tag;
            QPointer<TagBar> guard(this);
            connect(btn, &QPushButton::clicked, this, [this, guard, capturedTag, btn]() {
                if (!guard) return;
                bool nowHas = m_tagMgr->tagsForFile(m_fileName).contains(capturedTag);
                if (nowHas) m_tagMgr->removeTagFromFile(m_fileName, capturedTag);
                else        m_tagMgr->addTagToFile(m_fileName, capturedTag);
                emit tagsModified(m_fileName, m_tagMgr->tagsForFile(m_fileName));
                bool newHas = m_tagMgr->tagsForFile(m_fileName).contains(capturedTag);
                QColor tc2  = m_tagMgr->tagColor(capturedTag);
                btn->setText((newHas ? "✓  " : "    ") + capturedTag);
                btn->setStyleSheet(tagBtnStyle(newHas, tc2));
            });
            lay->addWidget(btn);
        }
    }
    lay->addStretch(1);
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    outerLay->addWidget(scroll);
    return panel;
}

void TagBar::showTagDropdown() {
    if (m_catPanel) { m_catPanel->deleteLater(); m_catPanel = nullptr; }
    if (m_dropPanel) { m_dropPanel->deleteLater(); m_dropPanel = nullptr; return; }
    m_dropPanel = buildTagPanel();
    QWidget* anchor = m_dropBtn ? static_cast<QWidget*>(m_dropBtn) : this;
    positionPanelAbove(m_dropPanel, anchor);
    m_dropPanel->show();
    m_dropPanel->installEventFilter(this);
}

void TagBar::showTagDropdownAnchoredAt(QWidget* anchor) {
    if (m_catPanel) { m_catPanel->deleteLater(); m_catPanel = nullptr; }
    if (m_dropPanel) { m_dropPanel->deleteLater(); m_dropPanel = nullptr; return; }
    m_dropPanel = buildTagPanel();
    positionPanelAbove(m_dropPanel, anchor ? anchor : this);
    m_dropPanel->show();
    m_dropPanel->installEventFilter(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Kategorie-Dropdown (hierarchisch)
// ─────────────────────────────────────────────────────────────────────────────

void TagBar::addCategorySection(QVBoxLayout* lay, const TagCategory& cat,
                                const QStringList& currentTags, int indent)
{
    // Kategorie-Header
    auto* catLbl = new QLabel(cat.name);
    QString pad  = QString("padding-left: %1px;").arg(8 + indent * 14);
    catLbl->setStyleSheet(QString(
        "color: rgba(0,200,180,0.85); font-size: 11px; font-weight: bold;"
        "background: rgba(0,0,0,0.15); border: none; border-radius: 4px; %1 padding-top: 3px; padding-bottom: 3px;")
        .arg(pad));
    catLbl->setFixedHeight(22);
    lay->addWidget(catLbl);

    // Tags in dieser Kategorie
    for (const QString& tag : cat.tags) {
        bool has   = currentTags.contains(tag);
        QColor tc  = cat.uniformColor ? cat.color : m_tagMgr->tagColor(tag);
        auto* btn  = new QPushButton();
        btn->setFixedHeight(24);
        btn->setCheckable(true);
        btn->setChecked(has);
        btn->setText((has ? "✓  " : "    ") + tag);
        // Einrückung über linkes Padding im Stylesheet
        QString indentStyle = QString("padding-left: %1px;").arg(14 + indent * 14);
        btn->setStyleSheet(QString(
            "QPushButton { background: %1; border: 1px solid %2; border-radius: 10px;"
            "color: %3; font-size: 11px; font-weight: 600; %4 text-align: left; }"
            "QPushButton:hover { background: %5; }")
            .arg(has ? tc.name()
                     : QString("rgba(%1,%2,%3,0.15)").arg(tc.red()).arg(tc.green()).arg(tc.blue()),
                 tc.name(),
                 (tc.lightness() > 130) ? "#111" : "#fff",
                 indentStyle,
                 tc.lighter(130).name()));

        QString capturedTag = tag;
        QString capturedCatId = cat.id;
        QPointer<TagBar> guard(this);
        connect(btn, &QPushButton::clicked, this, [this, guard, capturedTag, capturedCatId, btn, indentStyle]() {
            if (!guard) return;
            bool nowHas = m_tagMgr->tagsForFile(m_fileName).contains(capturedTag);
            if (nowHas) m_tagMgr->removeTagFromFile(m_fileName, capturedTag);
            else        m_tagMgr->addTagToFile(m_fileName, capturedTag);
            emit tagsModified(m_fileName, m_tagMgr->tagsForFile(m_fileName));
            bool newHas = m_tagMgr->tagsForFile(m_fileName).contains(capturedTag);
            // Live-Lookup: uniformColor der Kategorie oder individuelle Tag-Farbe
            QColor tc2 = m_tagMgr->categoryColor(capturedCatId);
            // Falls keine uniformColor gesetzt ist, categoryColor liefert Teal-Default;
            // nutze dann die individuelle Tag-Farbe
            {
                // Re-check uniformColor via category lookup
                const auto& cats = m_tagMgr->categories();
                std::function<const TagCategory*(const QList<TagCategory>&)> findCat;
                findCat = [&](const QList<TagCategory>& list) -> const TagCategory* {
                    for (const auto& c : list) {
                        if (c.id == capturedCatId) return &c;
                        if (auto* f = findCat(c.children)) return f;
                    }
                    return nullptr;
                };
                if (const TagCategory* c = findCat(cats))
                    tc2 = c->uniformColor ? c->color : m_tagMgr->tagColor(capturedTag);
                else
                    tc2 = m_tagMgr->tagColor(capturedTag);
            }
            btn->setText((newHas ? "✓  " : "    ") + capturedTag);
            btn->setStyleSheet(QString(
                "QPushButton { background: %1; border: 1px solid %2; border-radius: 10px;"
                "color: %3; font-size: 11px; font-weight: 600; text-align: left; }"
                "QPushButton:hover { background: %4; }")
                .arg(newHas ? tc2.name()
                            : QString("rgba(%1,%2,%3,0.15)").arg(tc2.red()).arg(tc2.green()).arg(tc2.blue()),
                     tc2.name(),
                     (tc2.lightness() > 130) ? "#111" : "#fff",
                     tc2.lighter(130).name()));
        });
        lay->addWidget(btn);
    }

    // Rekursiv: Unterkategorien
    for (const TagCategory& child : cat.children)
        addCategorySection(lay, child, currentTags, indent + 1);
}

QFrame* TagBar::buildCategoryPanel() {
    auto* panel = new QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    panel->setStyleSheet(
        "QFrame { background: #1a2830; border: 1px solid rgba(0,180,160,0.45); border-radius: 8px; }"
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 4px; background: transparent; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.2); border-radius: 2px; }");

    auto* outerLay = new QVBoxLayout(panel);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(0);

    auto* hdrRow = new QWidget(panel);
    hdrRow->setStyleSheet("background: rgba(0,0,0,0.2); border: none;");
    auto* hdrLay = new QHBoxLayout(hdrRow);
    hdrLay->setContentsMargins(10, 6, 10, 4);
    auto* hdrLbl = new QLabel(tr("Medium in Kategorie"), hdrRow);
    hdrLbl->setStyleSheet("color: rgba(0,200,180,0.9); font-size: 11px; font-weight: bold; background: none; border: none;");
    hdrLay->addWidget(hdrLbl);
    outerLay->addWidget(hdrRow);

    auto* sep = new QFrame(panel);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: rgba(255,255,255,0.1); background: rgba(255,255,255,0.1);");
    sep->setFixedHeight(1);
    outerLay->addWidget(sep);

    auto* scroll = new QScrollArea(panel);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setMaximumHeight(320);

    auto* inner = new QWidget(scroll);
    auto* lay   = new QVBoxLayout(inner);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(2);

    const QList<TagCategory>& cats = m_tagMgr->categories();
    QStringList inCats = m_tagMgr->categoriesForFile(m_fileName);

    // Helper to build category rows recursively
    std::function<void(const QList<TagCategory>&, int)> buildRows =
        [&](const QList<TagCategory>& list, int indent) {
        for (const TagCategory& cat : list) {
            bool has = inCats.contains(cat.id);
            auto* btn = new QPushButton(inner);
            btn->setFixedHeight(24);
            btn->setCheckable(true);
            btn->setChecked(has);

            QString indentStyle = QString("padding-left: %1px;").arg(14 + indent * 14);
            QColor cc = cat.uniformColor ? cat.color : QColor(0, 180, 160);
            QString bga  = has ? cc.name()
                               : QString("rgba(%1,%2,%3,0.12)").arg(cc.red()).arg(cc.green()).arg(cc.blue());
            QString txtC = has ? ((cc.lightness() > 130) ? "#111" : "#fff") : cc.name();
            btn->setText((has ? "✓  " : "    ") + cat.name);
            btn->setStyleSheet(QString(
                "QPushButton { background: %1; border: 1px solid %2; border-radius: 10px;"
                "color: %3; font-size: 11px; font-weight: 600; %4 text-align: left; }"
                "QPushButton:hover { background: %5; }")
                .arg(bga, cc.name(), txtC, indentStyle, cc.lighter(130).name()));

            QString catId = cat.id;
            QString catName = cat.name;
            QPointer<TagBar> guard(this);
            connect(btn, &QPushButton::clicked, this, [this, guard, catId, catName, btn, indentStyle]() {
                if (!guard) return;
                bool nowHas = m_tagMgr->categoriesForFile(m_fileName).contains(catId);
                if (nowHas) m_tagMgr->removeFileFromCategory(catId, m_fileName);
                else        m_tagMgr->addFileToCategory(catId, m_fileName);
                bool newHas = m_tagMgr->categoriesForFile(m_fileName).contains(catId);
                // Live-Lookup: Farbe der Kategorie zum Zeitpunkt des Klicks abfragen
                QColor cc2   = m_tagMgr->categoryColor(catId);
                QString bga2  = newHas ? cc2.name()
                                       : QString("rgba(%1,%2,%3,0.12)").arg(cc2.red()).arg(cc2.green()).arg(cc2.blue());
                QString txtC2 = newHas ? ((cc2.lightness() > 130) ? "#111" : "#fff") : cc2.name();
                btn->setText((newHas ? "✓  " : "    ") + catName);
                btn->setStyleSheet(QString(
                    "QPushButton { background: %1; border: 1px solid %2; border-radius: 10px;"
                    "color: %3; font-size: 11px; font-weight: 600; %4 text-align: left; }"
                    "QPushButton:hover { background: %5; }")
                    .arg(bga2, cc2.name(), txtC2, indentStyle, cc2.lighter(130).name()));
            });
            lay->addWidget(btn);
            buildRows(cat.children, indent + 1);
        }
    };

    if (cats.isEmpty()) {
        auto* lbl = new QLabel(tr("(Keine Kategorien vorhanden)"), inner);
        lbl->setStyleSheet("color: rgba(200,220,215,0.4); font-size: 11px; background: transparent;");
        lay->addWidget(lbl);
    } else {
        buildRows(cats, 0);
    }

    lay->addStretch(1);
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    outerLay->addWidget(scroll);
    return panel;
}

void TagBar::showCategoryDropdown() {
    if (m_dropPanel) { m_dropPanel->deleteLater(); m_dropPanel = nullptr; }
    if (m_catPanel) { m_catPanel->deleteLater(); m_catPanel = nullptr; return; }
    m_catPanel = buildCategoryPanel();
    QWidget* anchor = m_catBtn ? static_cast<QWidget*>(m_catBtn) : this;
    positionPanelAbove(m_catPanel, anchor);
    m_catPanel->show();
    m_catPanel->installEventFilter(this);
}

void TagBar::showCategoryDropdownAnchoredAt(QWidget* anchor) {
    if (m_dropPanel) { m_dropPanel->deleteLater(); m_dropPanel = nullptr; }
    if (m_catPanel) { m_catPanel->deleteLater(); m_catPanel = nullptr; return; }
    m_catPanel = buildCategoryPanel();
    positionPanelAbove(m_catPanel, anchor ? anchor : this);
    m_catPanel->show();
    m_catPanel->installEventFilter(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// EventFilter (auto-close + Mausrad)
// ─────────────────────────────────────────────────────────────────────────────

bool TagBar::eventFilter(QObject* obj, QEvent* ev) {
    // Helper: reagiert auf Leave/Enter/Wheel für ein bestimmtes Panel
    auto handle = [&](QFrame*& panel, QTimer*& timer) {
        if (!panel || obj != panel) return;
        if (ev->type() == QEvent::Leave) {
            if (!timer) {
                timer = new QTimer(this);
                timer->setSingleShot(true);
                timer->setInterval(400);
            }
            // Verbinde jedes Mal neu, aber trenne vorher (Qt::UniqueConnection genügt nicht für Lambdas)
            disconnect(timer, &QTimer::timeout, nullptr, nullptr);
            if (&panel == &m_dropPanel) {
                connect(timer, &QTimer::timeout, this, [this]() {
                    if (m_dropPanel) { m_dropPanel->deleteLater(); m_dropPanel = nullptr; }
                });
            } else {
                connect(timer, &QTimer::timeout, this, [this]() {
                    if (m_catPanel) { m_catPanel->deleteLater(); m_catPanel = nullptr; }
                });
            }
            timer->start();
        }
        if (ev->type() == QEvent::Enter) {
            if (timer) timer->stop();
        }
        if (ev->type() == QEvent::Wheel) {
            QScrollArea* sa = panel->findChild<QScrollArea*>();
            if (sa) { QApplication::sendEvent(sa->verticalScrollBar(), ev); }
        }
    };
    handle(m_dropPanel, m_dropHideTimer);
    handle(m_catPanel,  m_catHideTimer);
    return QWidget::eventFilter(obj, ev);
}
