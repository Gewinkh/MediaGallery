#include "Style.h"
#include "AppSettings.h"
#include <QColor>
#include <QPainterPath>
#include <QLinearGradient>

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
static QString rgba(const QColor& c, float alpha) {
    return QString("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha, 0, 'f', 2);
}

// Returns a CSS linear-gradient string for a Qt angle (0=up, 90=right, 180=down)
static QString cssGradient(int angleDeg, const QColor& from, const QColor& to) {
    // Qt angle: 0=top, 90=right, 180=bottom → CSS angle: 0=up=0deg, right=90deg
    return QString("qlineargradient(x1:%1,y1:%2,x2:%3,y2:%4,stop:0 %5,stop:1 %6)")
        .arg(angleDeg == 90 ? 0 : (angleDeg == 270 ? 1 : 0.5), 0, 'f', 1)
        .arg(angleDeg == 0  ? 1 : (angleDeg == 180 ? 0 : 0.5), 0, 'f', 1)
        .arg(angleDeg == 90 ? 1 : (angleDeg == 270 ? 0 : 0.5), 0, 'f', 1)
        .arg(angleDeg == 0  ? 0 : (angleDeg == 180 ? 1 : 0.5), 0, 'f', 1)
        .arg(from.name(), to.name());
}

// Build the accent color string for CSS (solid, gradient, or solid with shadow comment)
static QString accentCss(const ThemeColors& t) {
    if (t.accentType == AccentType::Gradient)
        return cssGradient(90, t.accent, t.accentGradEnd);
    return t.accent.name();
}

// Glow box-shadow string for QSS (simulated via border + background combo)
static QString glowBorder(const ThemeColors& t) {
    if (t.accentType == AccentType::Glow) {
        float i = t.glowIntensity;
        return QString("border: 1px solid %1;").arg(rgba(t.accent, i));
    }
    return QString("border: 1px solid %1;").arg(rgba(t.border, 0.9f));
}

// Tile background CSS
static QString tileBgCss(const ThemeColors& t) {
    if (t.tileBgType == TileBgType::Gradient)
        return cssGradient(t.tileBgGradAngle, t.tileBgColor, t.tileBgGradEnd);
    if (t.tileBgType == TileBgType::Transparent)
        return "transparent";
    return t.tileBgColor.name();
}

// Tile hover border — glowing if enabled
static QString tileHoverBorder(const ThemeColors& t) {
    if (t.tileGlowOnHover)
        return QString("border: 2px solid %1;").arg(rgba(t.accent, 0.85f));
    return QString("border: 1px solid %1;").arg(rgba(t.accent, 0.6f));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main stylesheet
// ─────────────────────────────────────────────────────────────────────────────
QString Style::mainStyleSheet(const QColor& /*bg*/, const QColor& /*accent*/) {
    ThemeColors t = AppSettings::instance().currentTheme();

    QString bgCss = t.bgIsGradient
        ? cssGradient(t.bgGradAngle, t.bgGradStart, t.bgGradEnd)
        : t.background.name();

    QString accSolid  = t.accent.name();
    QString accDim    = rgba(t.accent, 0.25f);
    QString accMid    = rgba(t.accent, 0.4f);
    QString accFull   = rgba(t.accent, 0.85f);
    QString menuBg    = t.card.name();
    QString textCol   = t.textPrimary.name();
    QString mutedCol  = t.textMuted.name();
    QString borderCol = t.border.name();

    // For glow themes: use a glowing accent on focus/hover
    QString focusBorder = (t.accentType == AccentType::Glow)
        ? QString("border: 1px solid %1; ").arg(rgba(t.accent, t.glowIntensity))
        : QString("border: 1px solid %1; ").arg(accSolid);

    // Scrollbar accent
    QString scrl = scrollBarStyle();

    // Main QSS — background uses bgCss (can be gradient)
    QString qss = QString(
"QMainWindow { background: %1; }\n"
"QDialog { background: %2; }\n"
"QWidget { background: transparent; color: %3; "
"  font-family: 'Segoe UI','SF Pro Display','Helvetica Neue',sans-serif; font-size: 13px; }\n"
"QScrollArea { background: transparent; border: none; }\n"
// MenuBar
"QMenuBar { background: %2; color: %4; border-bottom: 1px solid %5; padding: 2px; }\n"
"QMenuBar::item:selected { background: %6; border-radius: 4px; }\n"
// Menu
"QMenu { background: %2; border: 1px solid %5; border-radius: 8px; padding: 4px; color: %3; }\n"
"QMenu::item { padding: 5px 20px 5px 12px; }\n"
"QMenu::item:selected { background: %6; border-radius: 4px; }\n"
"QMenu::separator { height: 1px; background: %5; margin: 4px 8px; }\n"
// ToolTip
"QToolTip { background: %2; color: %3; border: 1px solid %5; border-radius: 4px; padding: 4px 8px; }\n"
// StatusBar
"QStatusBar { background: %2; color: %4; border-top: 1px solid %5; font-size: 11px; }\n"
// LineEdit
"QLineEdit { background: rgba(255,255,255,0.06); border: 1px solid %5; border-radius: 6px;"
"  color: %3; padding: 4px 10px; selection-background-color: %7; }\n"
"QLineEdit:focus { %8 }\n"
// Buttons
"QPushButton { background: rgba(255,255,255,0.07); border: 1px solid %5; border-radius: 6px;"
"  color: %3; padding: 5px 14px; }\n"
"QPushButton:hover { background: %6; border-color: %7; color: %7; }\n"
"QPushButton:pressed { background: %9; }\n"
// ToolButton
"QToolButton { background: transparent; border: none; color: %3; border-radius: 5px; padding: 3px; }\n"
"QToolButton:hover { background: %6; color: %7; }\n"
// ComboBox
"QComboBox { background: rgba(255,255,255,0.07); border: 1px solid %5; border-radius: 6px;"
"  color: %3; padding: 3px 8px; }\n"
"QComboBox QAbstractItemView { background: %2; border: 1px solid %5; border-radius: 6px;"
"  color: %3; selection-background-color: %7; }\n"
// Label
"QLabel { color: %3; }\n"
// Slider
"QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,0.15); border-radius: 2px; }\n"
"QSlider::sub-page:horizontal { background: %7; border-radius: 2px; }\n"
"QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; background: white; border-radius: 7px; }\n"
// CheckBox
"QCheckBox { color: %3; spacing: 6px; }\n"
"QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid %5; background: rgba(255,255,255,0.07); }\n"
"QCheckBox::indicator:checked { background: %7; border-color: %7; }\n"
// SpinBox / DateTimeEdit
"QSpinBox, QDateTimeEdit { background: rgba(255,255,255,0.07); border: 1px solid %5; border-radius: 6px; color: %3; padding: 3px 8px; }\n"
// GroupBox
"QGroupBox { border: 1px solid %5; border-radius: 8px; margin-top: 12px; color: %4; font-size: 11px; }\n"
"QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 8px; left: 12px; }\n"
// RadioButton
"QRadioButton { color: %3; spacing: 6px; }\n"
"QRadioButton::indicator { width: 14px; height: 14px; border-radius: 7px; border: 1px solid %5; background: rgba(255,255,255,0.07); }\n"
"QRadioButton::indicator:checked { background: %7; border-color: %7; }\n"
)
    .arg(bgCss,       // %1 main bg
         menuBg,      // %2 card/panel bg
         textCol,     // %3 text primary
         mutedCol,    // %4 text muted
         borderCol,   // %5 border
         accDim,      // %6 hover bg dim
         accSolid,    // %7 accent solid
         focusBorder, // %8 focus border rule
         accMid       // %9 pressed bg
    );

    qss += scrl;
    return qss;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Scrollbar
// ─────────────────────────────────────────────────────────────────────────────
QString Style::scrollBarStyle() {
    ThemeColors t = AppSettings::instance().currentTheme();
    QString dim = rgba(t.accent, 0.35f);
    QString hov = rgba(t.accent, 0.70f);
    return QString(
"QScrollBar:vertical { background: rgba(255,255,255,0.03); width: 8px; border-radius: 4px; margin: 0; }\n"
"QScrollBar::handle:vertical { background: %1; border-radius: 4px; min-height: 30px; }\n"
"QScrollBar::handle:vertical:hover { background: %2; }\n"
"QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }\n"
"QScrollBar:horizontal { background: rgba(255,255,255,0.03); height: 8px; border-radius: 4px; }\n"
"QScrollBar::handle:horizontal { background: %1; border-radius: 4px; min-width: 30px; }\n"
"QScrollBar::handle:horizontal:hover { background: %2; }\n"
"QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }\n"
    ).arg(dim, hov);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Thumbnail tile stylesheet
// ─────────────────────────────────────────────────────────────────────────────
QString Style::thumbnailStyle() {
    ThemeColors t = AppSettings::instance().currentTheme();
    QString bg       = tileBgCss(t);
    QString hoverBdr = tileHoverBorder(t);
    return QString(
"MediaThumbnail { background: %1; border-radius: 8px; border: 1px solid %2; }\n"
"MediaThumbnail:hover { %3 }\n"
    ).arg(bg, t.border.name(), hoverBdr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Generic accent button style
// ─────────────────────────────────────────────────────────────────────────────
QString Style::buttonStyle(const QColor& accent) {
    return QString(
        "QPushButton { background: rgba(%1,%2,%3,0.25); border: 1px solid rgba(%1,%2,%3,0.5);"
        "border-radius: 8px; color: %4; font-weight: 600; padding: 6px 16px; }"
        "QPushButton:hover { background: rgba(%1,%2,%3,0.45); }"
        "QPushButton:pressed { background: rgba(%1,%2,%3,0.6); }"
    ).arg(accent.red()).arg(accent.green()).arg(accent.blue()).arg(accent.name());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tile background pixmap painter (called from MediaThumbnail::paintEvent)
// ─────────────────────────────────────────────────────────────────────────────
void Style::paintTileBackground(QPainter& p, const QRect& rect) {
    ThemeColors t = AppSettings::instance().currentTheme();

    QPainterPath path;
    path.addRoundedRect(rect, 8, 8);
    p.setClipPath(path);

    if (t.tileBgType == TileBgType::Gradient) {
        // Convert angle to gradient direction
        float angle = t.tileBgGradAngle * M_PI / 180.0f;
        QPointF start(rect.center().x() - cos(angle) * rect.width() / 2.0,
                      rect.center().y() - sin(angle) * rect.height() / 2.0);
        QPointF end  (rect.center().x() + cos(angle) * rect.width() / 2.0,
                      rect.center().y() + sin(angle) * rect.height() / 2.0);
        QLinearGradient grad(start, end);
        grad.setColorAt(0, t.tileBgColor);
        grad.setColorAt(1, t.tileBgGradEnd);
        p.fillPath(path, grad);
    } else if (t.tileBgType == TileBgType::Transparent) {
        // nothing
    } else {
        p.fillPath(path, t.tileBgColor);
    }
    p.setClipping(false);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Accent color for painting (e.g. active chips, indicators)
// ─────────────────────────────────────────────────────────────────────────────
QColor Style::resolveAccent() {
    return AppSettings::instance().currentTheme().accent;
}

QLinearGradient Style::accentGradient(const QRectF& rect) {
    ThemeColors t = AppSettings::instance().currentTheme();
    QLinearGradient g(rect.topLeft(), rect.topRight());
    g.setColorAt(0, t.accent);
    g.setColorAt(1, t.accentType == AccentType::Gradient ? t.accentGradEnd : t.accent);
    return g;
}
