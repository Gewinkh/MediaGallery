#include "AppSettings.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

// ─────────────────────────────────────────────────────────────────────────────
//  ThemeColors  serialization
// ─────────────────────────────────────────────────────────────────────────────
QJsonObject ThemeColors::toJson() const {
    QJsonObject o;
    o["name"]            = name;
    o["background"]      = background.name();
    o["card"]            = card.name();
    o["textPrimary"]     = textPrimary.name();
    o["textMuted"]       = textMuted.name();
    o["border"]          = border.name();
    o["accentType"]      = (int)accentType;
    o["accent"]          = accent.name();
    o["accentGradEnd"]   = accentGradEnd.name();
    o["glowRadius"]      = glowRadius;
    o["glowIntensity"]   = glowIntensity;
    o["bgIsGradient"]    = bgIsGradient;
    o["bgGradStart"]     = bgGradStart.name();
    o["bgGradEnd"]       = bgGradEnd.name();
    o["bgGradAngle"]     = bgGradAngle;
    o["tileBgType"]      = (int)tileBgType;
    o["tileBgColor"]     = tileBgColor.name();
    o["tileBgGradEnd"]   = tileBgGradEnd.name();
    o["tileBgGradAngle"] = tileBgGradAngle;
    o["tileGlowOnHover"] = tileGlowOnHover;
    o["tileGlowRadius"]  = tileGlowRadius;
    return o;
}

ThemeColors ThemeColors::fromJson(const QJsonObject& o) {
    ThemeColors t;
    t.name            = o["name"].toString("Custom");
    t.background      = QColor(o["background"].toString("#0a1216"));
    t.card            = QColor(o["card"].toString("#121c22"));
    t.textPrimary     = QColor(o["textPrimary"].toString("#dcebd8"));
    t.textMuted       = QColor(o["textMuted"].toString("#789891"));
    t.border          = QColor(o["border"].toString("#28303c"));
    t.accentType      = (AccentType)o["accentType"].toInt(0);
    t.accent          = QColor(o["accent"].toString("#00b4a0"));
    t.accentGradEnd   = QColor(o["accentGradEnd"].toString("#0078c8"));
    t.glowRadius      = (float)o["glowRadius"].toDouble(8.0);
    t.glowIntensity   = (float)o["glowIntensity"].toDouble(0.6);
    t.bgIsGradient    = o["bgIsGradient"].toBool(false);
    t.bgGradStart     = QColor(o["bgGradStart"].toString("#0a1216"));
    t.bgGradEnd       = QColor(o["bgGradEnd"].toString("#0a1216"));
    t.bgGradAngle     = o["bgGradAngle"].toInt(180);
    t.tileBgType      = (TileBgType)o["tileBgType"].toInt(0);
    t.tileBgColor     = QColor(o["tileBgColor"].toString("#121c22"));
    t.tileBgGradEnd   = QColor(o["tileBgGradEnd"].toString("#121c22"));
    t.tileBgGradAngle = o["tileBgGradAngle"].toInt(180);
    t.tileGlowOnHover = o["tileGlowOnHover"].toBool(false);
    t.tileGlowRadius  = (float)o["tileGlowRadius"].toDouble(6.0);
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Built-in themes
// ─────────────────────────────────────────────────────────────────────────────
ThemeColors AppSettings::themeForProfile(DesignProfile p) {
    ThemeColors t;
    switch (p) {
    case DesignProfile::Dark:
        t.name          = "Dark";
        t.background    = QColor(10, 18, 22);
        t.card          = QColor(18, 28, 34);
        t.accent        = QColor(0, 180, 160);
        t.accentType    = AccentType::Solid;
        t.textPrimary   = QColor(220, 235, 230);
        t.textMuted     = QColor(120, 150, 145);
        t.border        = QColor(40, 60, 70);
        t.bgIsGradient  = false;
        t.tileBgType    = TileBgType::Solid;
        t.tileBgColor   = QColor(18, 28, 34);
        break;
    case DesignProfile::DarkOLED:
        t.name             = "Dark OLED";
        t.background       = QColor(0, 0, 0);
        t.card             = QColor(8, 8, 8);
        t.accent           = QColor(0, 230, 210);
        t.accentType       = AccentType::Glow;
        t.accentGradEnd    = QColor(0, 180, 255);
        t.glowRadius       = 12.0f;
        t.glowIntensity    = 0.85f;
        t.textPrimary      = QColor(240, 245, 245);
        t.textMuted        = QColor(100, 120, 118);
        t.border           = QColor(20, 20, 20);
        t.bgIsGradient     = false;
        t.tileBgType       = TileBgType::Solid;
        t.tileBgColor      = QColor(8, 8, 8);
        t.tileGlowOnHover  = true;
        t.tileGlowRadius   = 10.0f;
        break;
    case DesignProfile::OceanDepth:
        t.name             = "Ocean Depth";
        t.background       = QColor(0, 0, 0);
        t.card             = QColor(5, 12, 28);
        t.accent           = QColor(0, 160, 255);
        t.accentType       = AccentType::Gradient;
        t.accentGradEnd    = QColor(0, 80, 200);
        t.textPrimary      = QColor(200, 225, 255);
        t.textMuted        = QColor(80, 120, 170);
        t.border           = QColor(10, 30, 60);
        t.bgIsGradient     = true;
        t.bgGradStart      = QColor(0, 0, 0);
        t.bgGradEnd        = QColor(0, 18, 58);
        t.bgGradAngle      = 160;
        t.tileBgType       = TileBgType::Gradient;
        t.tileBgColor      = QColor(5, 12, 28);
        t.tileBgGradEnd    = QColor(2, 6, 18);
        t.tileBgGradAngle  = 180;
        t.tileGlowOnHover  = true;
        t.tileGlowRadius   = 8.0f;
        break;
    case DesignProfile::InfernoBlaze:
        t.name             = "Inferno Blaze";
        t.background       = QColor(0, 0, 0);
        t.card             = QColor(20, 8, 0);
        t.accent           = QColor(255, 120, 0);
        t.accentType       = AccentType::Gradient;
        t.accentGradEnd    = QColor(255, 50, 0);
        t.textPrimary      = QColor(255, 235, 210);
        t.textMuted        = QColor(160, 100, 60);
        t.border           = QColor(60, 20, 0);
        t.bgIsGradient     = true;
        t.bgGradStart      = QColor(0, 0, 0);
        t.bgGradEnd        = QColor(40, 10, 0);
        t.bgGradAngle      = 150;
        t.tileBgType       = TileBgType::Gradient;
        t.tileBgColor      = QColor(20, 8, 0);
        t.tileBgGradEnd    = QColor(8, 3, 0);
        t.tileBgGradAngle  = 180;
        t.tileGlowOnHover  = true;
        t.tileGlowRadius   = 8.0f;
        break;
    case DesignProfile::NeonPurple:
        t.name             = "Neon Purple";
        t.background       = QColor(4, 0, 10);
        t.card             = QColor(12, 5, 25);
        t.accent           = QColor(180, 0, 255);
        t.accentType       = AccentType::Glow;
        t.accentGradEnd    = QColor(80, 0, 200);
        t.glowRadius       = 14.0f;
        t.glowIntensity    = 0.9f;
        t.textPrimary      = QColor(230, 210, 255);
        t.textMuted        = QColor(120, 80, 170);
        t.border           = QColor(35, 10, 65);
        t.bgIsGradient     = true;
        t.bgGradStart      = QColor(4, 0, 10);
        t.bgGradEnd        = QColor(16, 0, 38);
        t.bgGradAngle      = 140;
        t.tileBgType       = TileBgType::Gradient;
        t.tileBgColor      = QColor(12, 5, 25);
        t.tileBgGradEnd    = QColor(6, 2, 14);
        t.tileBgGradAngle  = 180;
        t.tileGlowOnHover  = true;
        t.tileGlowRadius   = 12.0f;
        break;
    case DesignProfile::MidnightRose:
        t.name             = "Midnight Rose";
        t.background       = QColor(6, 0, 4);
        t.card             = QColor(18, 6, 12);
        t.accent           = QColor(230, 60, 100);
        t.accentType       = AccentType::Gradient;
        t.accentGradEnd    = QColor(160, 20, 60);
        t.textPrimary      = QColor(255, 220, 230);
        t.textMuted        = QColor(150, 80, 100);
        t.border           = QColor(55, 15, 28);
        t.bgIsGradient     = true;
        t.bgGradStart      = QColor(6, 0, 4);
        t.bgGradEnd        = QColor(30, 4, 14);
        t.bgGradAngle      = 155;
        t.tileBgType       = TileBgType::Gradient;
        t.tileBgColor      = QColor(18, 6, 12);
        t.tileBgGradEnd    = QColor(8, 2, 6);
        t.tileBgGradAngle  = 180;
        t.tileGlowOnHover  = true;
        t.tileGlowRadius   = 8.0f;
        break;
    case DesignProfile::Elegant:
        t.name          = "Elegant";
        t.background    = QColor(15, 12, 20);
        t.card          = QColor(25, 20, 35);
        t.accent        = QColor(180, 140, 255);
        t.accentType    = AccentType::Gradient;
        t.accentGradEnd = QColor(100, 60, 220);
        t.textPrimary   = QColor(240, 235, 255);
        t.textMuted     = QColor(160, 140, 190);
        t.border        = QColor(60, 50, 80);
        t.bgIsGradient  = true;
        t.bgGradStart   = QColor(15, 12, 20);
        t.bgGradEnd     = QColor(22, 18, 36);
        t.bgGradAngle   = 160;
        t.tileBgType    = TileBgType::Solid;
        t.tileBgColor   = QColor(25, 20, 35);
        break;
    case DesignProfile::Simple:
        t.name          = "Simple";
        t.background    = QColor(30, 30, 30);
        t.card          = QColor(45, 45, 45);
        t.accent        = QColor(100, 180, 100);
        t.accentType    = AccentType::Solid;
        t.textPrimary   = QColor(230, 230, 230);
        t.textMuted     = QColor(150, 150, 150);
        t.border        = QColor(70, 70, 70);
        t.bgIsGradient  = false;
        t.tileBgType    = TileBgType::Solid;
        t.tileBgColor   = QColor(45, 45, 45);
        break;
    case DesignProfile::Custom:
        break;
    }
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Singleton
// ─────────────────────────────────────────────────────────────────────────────
AppSettings& AppSettings::instance() {
    static AppSettings inst;
    return inst;
}
AppSettings::AppSettings(QObject* parent)
    : QObject(parent), m_settings("MediaGallery", "MediaGallery") {}

// ─── Window ───────────────────────────────────────────────────────────────────
QSize AppSettings::windowSize() const {
    return m_settings.value("window/size", QSize(1280, 800)).toSize();
}
void AppSettings::setWindowSize(const QSize& s) { m_settings.setValue("window/size", s); }

QPoint AppSettings::windowPos() const {
    return m_settings.value("window/pos", QPoint(100, 100)).toPoint();
}
void AppSettings::setWindowPos(const QPoint& p) { m_settings.setValue("window/pos", p); }

bool AppSettings::windowMaximized() const {
    return m_settings.value("window/maximized", false).toBool();
}
void AppSettings::setWindowMaximized(bool m) { m_settings.setValue("window/maximized", m); }

// ─── Folder ───────────────────────────────────────────────────────────────────
QString AppSettings::lastFolder() const {
    return m_settings.value("folder/last", QString()).toString();
}
void AppSettings::setLastFolder(const QString& path) { m_settings.setValue("folder/last", path); }

// ─── Language ─────────────────────────────────────────────────────────────────
Language AppSettings::language() const {
    return static_cast<Language>(m_settings.value("ui/language", 0).toInt());
}
void AppSettings::setLanguage(Language l) {
    m_settings.setValue("ui/language", static_cast<int>(l));
    emit languageChanged(l);
}

// ─── Video ────────────────────────────────────────────────────────────────────
VideoPlayback AppSettings::videoPlayback() const {
    return static_cast<VideoPlayback>(m_settings.value("video/playback", 0).toInt());
}
void AppSettings::setVideoPlayback(VideoPlayback v) {
    m_settings.setValue("video/playback", static_cast<int>(v));
}

// ─── Sort ─────────────────────────────────────────────────────────────────────
SortField AppSettings::sortField() const {
    return static_cast<SortField>(m_settings.value("sort/field", 0).toInt());
}
void AppSettings::setSortField(SortField f) { m_settings.setValue("sort/field", static_cast<int>(f)); }

SortOrder AppSettings::sortOrder() const {
    return static_cast<SortOrder>(m_settings.value("sort/order", 0).toInt());
}
void AppSettings::setSortOrder(SortOrder o) { m_settings.setValue("sort/order", static_cast<int>(o)); }

// ─── Legacy color helpers ─────────────────────────────────────────────────────
QColor AppSettings::backgroundColor() const { return currentTheme().background; }
void AppSettings::setBackgroundColor(const QColor& c) {
    m_settings.setValue("colors/background", c);
    emit colorSchemeChanged();
}
QColor AppSettings::accentColor() const { return currentTheme().accent; }
void AppSettings::setAccentColor(const QColor& c) {
    m_settings.setValue("colors/accent", c);
    emit colorSchemeChanged();
}

// ─── Grid / filter ────────────────────────────────────────────────────────────
int AppSettings::gridColumns() const { return m_settings.value("grid/columns", 1).toInt(); }
void AppSettings::setGridColumns(int c) { m_settings.setValue("grid/columns", qBound(1, c, 25)); }

bool AppSettings::tagFilterAnd() const { return m_settings.value("filter/tagAnd", true).toBool(); }
void AppSettings::setTagFilterAnd(bool v) { m_settings.setValue("filter/tagAnd", v); }

QColor AppSettings::andOrButtonColor() const {
    return m_settings.value("colors/andOrButton", QColor(0, 180, 160)).value<QColor>();
}
void AppSettings::setAndOrButtonColor(const QColor& c) {
    m_settings.setValue("colors/andOrButton", c);
    emit colorSchemeChanged();
}

bool AppSettings::optionsVisible() const { return m_settings.value("ui/optionsVisible", true).toBool(); }
void AppSettings::setOptionsVisible(bool v) { m_settings.setValue("ui/optionsVisible", v); }

bool AppSettings::showImages() const { return m_settings.value("filter/showImages", true).toBool(); }
void AppSettings::setShowImages(bool v) { m_settings.setValue("filter/showImages", v); }
bool AppSettings::showVideos() const { return m_settings.value("filter/showVideos", true).toBool(); }
void AppSettings::setShowVideos(bool v) { m_settings.setValue("filter/showVideos", v); }
bool AppSettings::showAudio()  const { return m_settings.value("filter/showAudio",  true).toBool(); }
void AppSettings::setShowAudio(bool v) { m_settings.setValue("filter/showAudio", v); }
bool AppSettings::showPdfs()   const { return m_settings.value("filter/showPdfs",   true).toBool(); }
void AppSettings::setShowPdfs(bool v)  { m_settings.setValue("filter/showPdfs",  v); }

// ─── Design / Theme ───────────────────────────────────────────────────────────
DesignProfile AppSettings::designProfile() const {
    return static_cast<DesignProfile>(m_settings.value("design/profile", 0).toInt());
}
void AppSettings::setDesignProfile(DesignProfile p) {
    m_settings.setValue("design/profile", static_cast<int>(p));
    emit themeChanged();
    emit colorSchemeChanged();
}

ThemeColors AppSettings::customTheme() const {
    QByteArray raw = m_settings.value("design/customJson").toByteArray();
    if (!raw.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (doc.isObject()) return ThemeColors::fromJson(doc.object());
    }
    ThemeColors t;
    t.name       = "Custom";
    t.background = m_settings.value("colors/background", QColor(10, 18, 22)).value<QColor>();
    t.card       = m_settings.value("custom/card",       QColor(18, 28, 34)).value<QColor>();
    t.accent     = m_settings.value("colors/accent",     QColor(0, 180, 160)).value<QColor>();
    t.textPrimary= m_settings.value("custom/textPrimary",QColor(220, 235, 230)).value<QColor>();
    t.textMuted  = m_settings.value("custom/textMuted",  QColor(120, 150, 145)).value<QColor>();
    t.border     = m_settings.value("custom/border",     QColor(40, 60, 70)).value<QColor>();
    return t;
}

void AppSettings::setCustomTheme(const ThemeColors& t) {
    QJsonDocument doc(t.toJson());
    m_settings.setValue("design/customJson", doc.toJson(QJsonDocument::Compact));
    emit themeChanged();
    emit colorSchemeChanged();
}

ThemeColors AppSettings::currentTheme() const {
    DesignProfile p = designProfile();
    if (p == DesignProfile::Custom) return customTheme();
    return themeForProfile(p);
}

// ─── JSON export / import ─────────────────────────────────────────────────────
bool AppSettings::exportCustomTheme(const QString& filePath) const {
    ThemeColors t = customTheme();
    QJsonDocument doc(t.toJson());
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool AppSettings::importCustomTheme(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    ThemeColors t = ThemeColors::fromJson(doc.object());
    setCustomTheme(t);
    setDesignProfile(DesignProfile::Custom);
    return true;
}

void AppSettings::sync() { m_settings.sync(); }
