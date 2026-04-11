#include "MediaThumbnail.h"
#include "Style.h"
#include "AppSettings.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QGraphicsDropShadowEffect>
#include <QTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QApplication>
#include <QScreen>

MediaThumbnail::MediaThumbnail(TagManager* tagMgr, QWidget* parent)
    : QWidget(parent), m_tagMgr(tagMgr)
{
    setupUi();
    setMouseTracking(true);
}

void MediaThumbnail::setupUi() {
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(3);

    // Name editor (top)
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("Name…"));
    m_nameEdit->setAlignment(Qt::AlignCenter);
    m_nameEdit->setFixedHeight(24);
    m_nameEdit->setStyleSheet(
        "QLineEdit { background: rgba(0,0,0,0.5); border: 1px solid rgba(0,180,160,0.3);"
        "border-radius: 5px; color: #dcebd8; font-size: 11px; padding: 0 6px; }"
        "QLineEdit:focus { border-color: rgba(0,200,180,0.7); }");
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        emit nameChanged(m_index, m_nameEdit->text().trimmed());
    });
    m_layout->addWidget(m_nameEdit);

    // Image/thumbnail area
    m_imageLabel = new QLabel(this);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_imageLabel->setStyleSheet("background: transparent;");

    // Video overlay label
    m_typeOverlay = new QLabel(m_imageLabel);
    m_typeOverlay->setText("▶");
    m_typeOverlay->setStyleSheet(
        "background: rgba(0,0,0,0.55); color: white; font-size: 22px;"
        "border-radius: 20px; padding: 4px 8px;");
    m_typeOverlay->setFixedSize(44, 40);
    m_typeOverlay->setAlignment(Qt::AlignCenter);
    m_typeOverlay->hide();

    // Audio format overlay (e.g. "MP3", "FLAC")
    m_audioOverlay = new QLabel(m_imageLabel);
    m_audioOverlay->setStyleSheet(
        "background: rgba(80,60,120,0.85); color: #d0b0ff; font-size: 13px; font-weight: bold;"
        "border-radius: 8px; padding: 6px 12px; border: 1px solid rgba(180,140,255,0.5);");
    m_audioOverlay->setAlignment(Qt::AlignCenter);
    m_audioOverlay->hide();

    m_layout->addWidget(m_imageLabel, 1);

    // Date + Size row (tuple layout)
    auto* infoRow = new QWidget(this);
    infoRow->setStyleSheet("background: transparent;");
    auto* infoLay = new QHBoxLayout(infoRow);
    infoLay->setContentsMargins(4, 0, 4, 0);
    infoLay->setSpacing(8);
    m_dateLabel = new QLabel(this);
    m_dateLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_dateLabel->setStyleSheet("color: rgba(120,150,145,0.8); font-size: 10px;");
    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_sizeLabel->setStyleSheet("color: rgba(100,130,125,0.7); font-size: 10px;");
    infoLay->addWidget(m_dateLabel);
    infoLay->addWidget(m_sizeLabel);
    infoLay->addStretch();
    infoRow->setFixedHeight(16);
    m_layout->addWidget(infoRow);
    m_infoRow = infoRow;

    // Tag bar (bottom) — full-size or compact depending on tile width
    m_tagBarRow = new QWidget(this);
    m_tagBarRow->setStyleSheet("background: transparent;");
    auto* tagRowLay = new QHBoxLayout(m_tagBarRow);
    tagRowLay->setContentsMargins(0, 0, 0, 0);
    tagRowLay->setSpacing(2);

    // "Tags" hover label — only as wide as the text
    m_tagsHoverLabel = new QLabel("Tags", m_tagBarRow);
    m_tagsHoverLabel->setFixedHeight(22);
    m_tagsHoverLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_tagsHoverLabel->setStyleSheet(
        "QLabel { color: rgba(0,200,180,0.7); font-size: 10px; font-weight: 600;"
        "background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.1);"
        "border-radius: 8px; padding: 0 6px; }");
    m_tagsHoverLabel->setCursor(Qt::PointingHandCursor);
    m_tagsHoverLabel->installEventFilter(this);
    m_tagsHoverLabel->hide();
    tagRowLay->addWidget(m_tagsHoverLabel);
    tagRowLay->addStretch(1);  // push label to left, don't let it expand

    m_tagBar = new TagBar(m_tagMgr, m_tagBarRow);
    m_tagBar->setFixedHeight(28);
    m_tagBar->setEditable(true);
    connect(m_tagBar, &TagBar::tagsModified, this, [this](const QString&, const QStringList& tags) {
        emit tagsModified(m_index, tags);
    });
    tagRowLay->addWidget(m_tagBar, 1);

    m_layout->addWidget(m_tagBarRow);

    setOptionsVisible(true);
    setStyleSheet(
        "MediaThumbnail { background: rgb(18,28,34); border-radius: 8px;"
        "border: 1px solid rgba(40,60,70,0.7); }"
    );
}

bool MediaThumbnail::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_tagsHoverLabel) {
        if (ev->type() == QEvent::Enter) {
            // Show tag tooltip panel
            if (!m_tagTooltipPanel) showTagTooltip();
        }
        if (ev->type() == QEvent::Leave) {
            // Delay close so user can move to panel
            if (!m_tagTooltipHideTimer) {
                m_tagTooltipHideTimer = new QTimer(this);
                m_tagTooltipHideTimer->setSingleShot(true);
                m_tagTooltipHideTimer->setInterval(350);
                connect(m_tagTooltipHideTimer, &QTimer::timeout, this, &MediaThumbnail::hideTagTooltip);
            }
            m_tagTooltipHideTimer->start();
        }
    }
    // Click on tag row → remove tag
    if (ev->type() == QEvent::MouseButtonRelease) {
        QVariant fileName = obj->property("tagRemoveFileName");
        QVariant tagVar   = obj->property("tagRemoveTag");
        if (fileName.isValid() && tagVar.isValid()) {
            m_tagMgr->removeTagFromFile(fileName.toString(), tagVar.toString());
            emit tagsModified(m_index, m_tagMgr->tagsForFile(fileName.toString()));
            hideTagTooltip();
            return true;
        }
    }
    // Hover over "+ Tag hinzufügen" opens the tag dropdown
    if (m_addHoverBtn && obj == m_addHoverBtn && ev->type() == QEvent::Enter) {
        hideTagTooltip();
        m_tagBar->showTagDropdownAnchoredAt(m_tagsHoverLabel);
    }
    if (m_tagTooltipPanel && obj == m_tagTooltipPanel) {
        if (ev->type() == QEvent::Enter  && m_tagTooltipHideTimer) m_tagTooltipHideTimer->stop();
        if (ev->type() == QEvent::Leave) {
            if (!m_tagTooltipHideTimer) {
                m_tagTooltipHideTimer = new QTimer(this);
                m_tagTooltipHideTimer->setSingleShot(true);
                m_tagTooltipHideTimer->setInterval(200);
                connect(m_tagTooltipHideTimer, &QTimer::timeout, this, &MediaThumbnail::hideTagTooltip);
            }
            m_tagTooltipHideTimer->start();
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void MediaThumbnail::showTagTooltip() {
    hideTagTooltip();
    QStringList tags = m_tagMgr->tagsForFile(m_item.fileName());

    m_tagTooltipPanel = new QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    m_tagTooltipPanel->setStyleSheet(
        "QFrame { background: #1a2830; border: 1px solid rgba(0,180,160,0.45); border-radius: 8px; }");
    m_tagTooltipPanel->installEventFilter(this);

    auto* lay = new QVBoxLayout(m_tagTooltipPanel);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(3);

    if (tags.isEmpty()) {
        auto* lbl = new QLabel("(Keine Tags)", m_tagTooltipPanel);
        lbl->setStyleSheet("color: rgba(200,220,215,0.4); font-size: 11px; background: transparent;");
        lay->addWidget(lbl);
    } else {
        for (const QString& tag : tags) {
            QColor tc = m_tagMgr->tagColor(tag);
            auto* row = new QWidget(m_tagTooltipPanel);
            row->setAttribute(Qt::WA_StyledBackground, true);
            row->setStyleSheet("QWidget { background: transparent; border-radius: 4px; }"
                               "QWidget:hover { background: rgba(255,255,255,0.06); }");
            row->setCursor(Qt::PointingHandCursor);
            row->setFixedHeight(20);
            auto* rowLay = new QHBoxLayout(row);
            rowLay->setContentsMargins(4, 0, 4, 0);
            rowLay->setSpacing(6);

            // Colored dot
            auto* dot = new QLabel(row);
            dot->setFixedSize(8, 8);
            dot->setAttribute(Qt::WA_StyledBackground, true);
            dot->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(tc.name()));

            // Tag text in tag color (darkened slightly for very light colors)
            QString txt = (tc.lightness() > 160) ? tc.darker(160).name() : tc.name();
            auto* lbl = new QLabel(tag, row);
            lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            lbl->setStyleSheet(QString("color: %1; font-size: 10px; font-weight: 700;"
                                       " background: transparent; border: none;").arg(txt));

            rowLay->addWidget(dot);
            rowLay->addWidget(lbl);
            rowLay->addStretch();

            row->installEventFilter(this);
            row->setProperty("tagRemoveFileName", m_item.fileName());
            row->setProperty("tagRemoveTag", tag);
            lay->addWidget(row);
        }
    }

    // "+ Tag hinzufügen" — opens tag dropdown on hover
    m_tagBar->setCompact(true);
    auto* addBtn = new QPushButton("+ Tag hinzufügen", m_tagTooltipPanel);
    addBtn->setFixedHeight(22);
    addBtn->setStyleSheet(
        "QPushButton { background: rgba(0,180,160,0.15); border: 1px solid rgba(0,180,160,0.4);"
        "border-radius: 9px; color: #00c8b4; font-size: 10px; font-weight: 600; padding: 0 8px; }"
        "QPushButton:hover { background: rgba(0,180,160,0.35); }");

    // Open tag dropdown on hover (Enter), also keep click working as fallback
    connect(addBtn, &QPushButton::clicked, this, [this]{
        hideTagTooltip();
        m_tagBar->showTagDropdownAnchoredAt(m_tagsHoverLabel);
    });
    addBtn->installEventFilter(this);
    m_addHoverBtn = addBtn;   // remember for eventFilter
    lay->addWidget(addBtn);

    m_tagTooltipPanel->adjustSize();
    QPoint gp = m_tagsHoverLabel->mapToGlobal(QPoint(0, m_tagsHoverLabel->height() + 3));
    m_tagTooltipPanel->move(gp);
    m_tagTooltipPanel->show();
}

void MediaThumbnail::hideTagTooltip() {
    if (m_tagTooltipPanel) {
        m_tagTooltipPanel->deleteLater();
        m_tagTooltipPanel = nullptr;
    }
    m_addHoverBtn = nullptr;
}

void MediaThumbnail::setItem(const MediaItem& item, int index) {
    m_item = item;
    m_index = index;

    m_nameEdit->setText(item.displayName.isEmpty() ? item.baseName() : item.displayName);
    m_typeOverlay->setVisible(item.isVideo());

    // Audio overlay
    bool isAudio = item.isAudio();
    m_audioOverlay->setVisible(isAudio);
    if (isAudio) {
        m_audioOverlay->setText(item.audioFormatLabel());
        m_audioOverlay->adjustSize();
    }

    m_dateLabel->setText(item.dateTime.isValid()
                             ? item.dateTime.toString("dd.MM.yyyy")
                             : "");
    m_sizeLabel->setText(item.fileSize > 0 ? formatSize(item.fileSize) : "");
    m_tagBar->setFile(item.fileName());
    m_tagBar->refresh();

    m_imageLabel->clear();
    if (isAudio) {
        m_imageLabel->setStyleSheet(
            "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 rgba(60,40,90,1), stop:1 rgba(30,20,50,1)); border-radius: 5px;");
    } else {
        m_imageLabel->setStyleSheet(
            "background: rgba(20,32,40,1); border-radius: 5px;");
    }
    updateCompactMode();
}

void MediaThumbnail::setThumbnail(const QPixmap& pix) {
    if (pix.isNull()) return;
    QSize sz = m_imageLabel->size();
    m_imageLabel->setPixmap(pix.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Reposition video overlay
    if (m_typeOverlay->isVisible()) {
        QPoint center = m_imageLabel->rect().center();
        m_typeOverlay->move(center.x() - m_typeOverlay->width()/2,
                            center.y() - m_typeOverlay->height()/2);
    }
    // Reposition audio overlay
    if (m_audioOverlay->isVisible()) {
        QPoint center = m_imageLabel->rect().center();
        m_audioOverlay->adjustSize();
        m_audioOverlay->move(center.x() - m_audioOverlay->width()/2,
                             center.y() - m_audioOverlay->height()/2);
    }
}

void MediaThumbnail::setOptionsVisible(bool v) {
    m_optionsVisible = v;
    m_nameEdit->setVisible(v);
    m_tagBarRow->setVisible(v);
    m_infoRow->setVisible(v);
    m_layout->setContentsMargins(v ? 4 : 2, v ? 4 : 2, v ? 4 : 2, v ? 4 : 2);
}

void MediaThumbnail::setCovered(bool covered) {
    m_covered = covered;
    if (covered) {
        // Clear the image content but keep the label visible so layout is unchanged
        m_imageLabel->clear();
        m_typeOverlay->hide();
        m_audioOverlay->hide();
    } else {
        // Restore overlays
        m_typeOverlay->setVisible(m_item.isVideo());
        m_audioOverlay->setVisible(m_item.isAudio());
    }
    update();
}

void MediaThumbnail::updateCompactMode() {
    // Always show the "Tags" hover button instead of the full pill bar
    m_tagsHoverLabel->setVisible(m_optionsVisible);
    m_tagBar->setVisible(false);
    m_tagBar->setCompact(true);
}

QString MediaThumbnail::formatSize(qint64 bytes) {
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return QString("%1 KB").arg(kb, 0, 'f', kb < 10 ? 2 : 1);
    double mb = kb / 1024.0;
    if (mb < 1024.0)
        return QString("%1 MB").arg(mb, 0, 'f', mb < 10 ? 2 : 1);
    double gb = mb / 1024.0;
    return QString("%1 GB").arg(gb, 0, 'f', 2);
}

void MediaThumbnail::setSelected(bool s) {
    m_selected = s;
    updateCompactMode();
    update();
}

void MediaThumbnail::paintEvent(QPaintEvent* e) {
    QWidget::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Draw tile background (supports gradient, solid, transparent from theme)
    Style::paintTileBackground(p, rect());

    // Selection / hover border
    QPainterPath path;
    path.addRoundedRect(rect(), 8, 8);

    ThemeColors theme = AppSettings::instance().currentTheme();

    if (m_selected) {
        // Glowing selection border when glow is enabled
        if (theme.accentType == AccentType::Glow) {
            QPen glowPen(QColor(theme.accent.red(), theme.accent.green(),
                                theme.accent.blue(),
                                int(255 * theme.glowIntensity)), 3);
            p.setPen(glowPen);
        } else if (theme.accentType == AccentType::Gradient) {
            QLinearGradient g = Style::accentGradient(QRectF(rect()));
            p.setPen(QPen(QBrush(g), 2));
        } else {
            p.setPen(QPen(theme.accent, 2));
        }
        p.drawPath(path);
    } else if (m_hovered) {
        if (theme.tileGlowOnHover) {
            QPen glowPen(QColor(theme.accent.red(), theme.accent.green(),
                                theme.accent.blue(), 180), 1.5);
            p.setPen(glowPen);
        } else {
            p.setPen(QPen(QColor(theme.accent.red(), theme.accent.green(),
                                 theme.accent.blue(), 140), 1.5));
        }
        p.drawPath(path);
    }

    // Draw cover overlay when privacy mode is active
    if (m_covered) {
        QRect imgRect = m_imageLabel->geometry();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 20, 25));
        p.drawRect(imgRect);
        p.setPen(QColor(60, 80, 85));
        p.setFont(QFont(font().family(), 22));
        p.drawText(imgRect, Qt::AlignCenter, "🔒");
    }
}

void MediaThumbnail::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        // Add-to-Tag mode: left click toggles tag membership with bright border feedback
        bool addToTagMode = property("addToTagMode").toBool();
        if (addToTagMode) {
            QString tag    = property("addToTagModeTag").toString();
            bool tagged    = m_item.tags.contains(tag);
            QStringList tags = m_item.tags;
            if (tagged) tags.removeAll(tag);
            else        tags.append(tag);
            emit tagsModified(m_index, tags);
            setProperty("addToTagTagged", !tagged);
            setSelected(!tagged);
            if (!tagged)
                // Gerade HINZUGEFÜGT → hell leuchtender Rahmen + grüner Hintergrund
                setStyleSheet(
                    "MediaThumbnail { background: rgba(0,200,160,0.13); border-radius: 8px;"
                    "border: 3px solid #00ffdd; }");
            else
                // Gerade ENTFERNT → gedimmt
                setStyleSheet(
                    "MediaThumbnail { background: rgba(10,18,22,0.6); border-radius: 8px;"
                    "border: 1px solid rgba(40,60,70,0.4); }");
            update();
            return;
        }
        emit clicked(m_index);
    }
    if (e->button() == Qt::RightButton) {
        // Group mode: toggle tag membership on right-click
        bool groupMode = property("groupMode").toBool();
        if (groupMode) {
            bool tagged = m_item.tags.contains(property("groupModeTag").toString());
            // Toggle: add or remove tag
            QStringList tags = m_item.tags;
            QString tag = property("groupModeTag").toString();
            if (tagged) tags.removeAll(tag);
            else        tags.append(tag);
            emit tagsModified(m_index, tags);
            // Update visual state
            setProperty("groupTagged", !tagged);
            setSelected(!tagged);
            if (tagged)
                setStyleSheet("MediaThumbnail { background: rgb(18,28,34); border-radius: 8px;"
                              "border: 1px solid rgba(40,60,70,0.4); }");
            else
                setStyleSheet("MediaThumbnail { background: rgb(30,22,48); border-radius: 8px;"
                              "border: 2px solid rgba(180,120,255,0.7); }");
            update();
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

void MediaThumbnail::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit doubleClicked(m_index);
    QWidget::mouseDoubleClickEvent(e);
}

void MediaThumbnail::enterEvent(QEnterEvent* e) {
    m_hovered = true;
    update();
    QWidget::enterEvent(e);
}

void MediaThumbnail::leaveEvent(QEvent* e) {
    m_hovered = false;
    update();
    QWidget::leaveEvent(e);
}

void MediaThumbnail::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    updateCompactMode();
}
