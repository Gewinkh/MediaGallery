#include "core/AppSettings.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <algorithm>

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
    o["accentType"]      = static_cast<int>(accentType);
    o["accent"]          = accent.name();
    o["accentGradEnd"]   = accentGradEnd.name();
    o["glowRadius"]      = glowRadius;
    o["glowIntensity"]   = glowIntensity;
    o["bgIsGradient"]    = bgIsGradient;
    o["bgGradStart"]     = bgGradStart.name();
    o["bgGradEnd"]       = bgGradEnd.name();
    o["bgGradAngle"]     = bgGradAngle;
    o["tileBgType"]      = static_cast<int>(tileBgType);
    o["tileBgColor"]     = tileBgColor.name();
    o["tileBgGradEnd"]   = tileBgGradEnd.name();
    o["tileBgGradAngle"] = tileBgGradAngle;
    o["tileGlowOnHover"] = tileGlowOnHover;
    o["tileGlowRadius"]  = tileGlowRadius;
    o["pdfViewerBg"]    = pdfViewerBg.name();
    o["pdfThumbBg"]     = pdfThumbBg.name();
    o["pdfSidebarBg"]   = pdfSidebarBg.name();
    o["pdfToolbarBg"]   = pdfToolbarBg.name();
    o["pdfScrollbarBg"] = pdfScrollbarBg.name();
    o["buttonBg"]       = buttonBg.name(QColor::HexArgb);
    o["sidebarBg"]      = sidebarBg.name();
    o["editorBgText"]   = editorBgText.name();
    o["editorBgHtml"]   = editorBgHtml.name();
    o["menuBarBg"]      = menuBarBg.name();
    o["toolbarBg"]      = toolbarBg.name();
    o["filterBarBg"]    = filterBarBg.name();
    o["statusBarBg"]    = statusBarBg.name();
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
    {
        int raw = o["accentType"].toInt(0);
        t.accentType = (raw >= 0 && raw <= static_cast<int>(AccentType::Glow))
                       ? static_cast<AccentType>(raw) : AccentType::Solid;
    }
    t.accent          = QColor(o["accent"].toString("#00b4a0"));
    t.accentGradEnd   = QColor(o["accentGradEnd"].toString("#0078c8"));
    t.glowRadius      = static_cast<float>(o["glowRadius"].toDouble(8.0));
    t.glowIntensity   = static_cast<float>(o["glowIntensity"].toDouble(0.6));
    t.bgIsGradient    = o["bgIsGradient"].toBool(false);
    t.bgGradStart     = QColor(o["bgGradStart"].toString("#0a1216"));
    t.bgGradEnd       = QColor(o["bgGradEnd"].toString("#0a1216"));
    t.bgGradAngle     = o["bgGradAngle"].toInt(180);
    {
        int raw = o["tileBgType"].toInt(0);
        t.tileBgType = (raw >= 0 && raw <= static_cast<int>(TileBgType::Transparent))
                       ? static_cast<TileBgType>(raw) : TileBgType::Solid;
    }
    t.tileBgColor     = QColor(o["tileBgColor"].toString("#121c22"));
    t.tileBgGradEnd   = QColor(o["tileBgGradEnd"].toString("#121c22"));
    t.tileBgGradAngle = o["tileBgGradAngle"].toInt(180);
    t.tileGlowOnHover = o["tileGlowOnHover"].toBool(false);
    t.tileGlowRadius  = static_cast<float>(o["tileGlowRadius"].toDouble(6.0));
    t.pdfViewerBg    = QColor(o["pdfViewerBg"].toString("#0d1518"));
    t.pdfThumbBg     = QColor(o["pdfThumbBg"].toString("#ffffff"));
    t.pdfSidebarBg   = QColor(o["pdfSidebarBg"].toString("#0a1216"));
    t.pdfToolbarBg   = QColor(o["pdfToolbarBg"].toString("#121c22"));
    t.pdfScrollbarBg = QColor(o["pdfScrollbarBg"].toString("#121c22"));
    {
        QString btn = o["buttonBg"].toString("#00000000");
        t.buttonBg = QColor(btn.isEmpty() ? "#00000000" : btn);
    }
    t.sidebarBg      = QColor(o["sidebarBg"].toString("#121c22"));
    // Rückwärtskompatibel: ein altes Theme mit nur „editorBg" seedet BEIDE neuen
    // Farben (kein optischer Bruch); die getrennten Keys überschreiben, falls da.
    {
        const QString legacy = o["editorBg"].toString(t.card.name());
        t.editorBgText = QColor(o["editorBgText"].toString(legacy));
        t.editorBgHtml = QColor(o["editorBgHtml"].toString(legacy));
    }
    t.menuBarBg      = QColor(o["menuBarBg"].toString("#0c141a"));
    t.toolbarBg      = QColor(o["toolbarBg"].toString("#0c141a"));
    t.filterBarBg    = QColor(o["filterBarBg"].toString("#0c141a"));
    t.statusBarBg    = QColor(o["statusBarBg"].toString("#080e12"));
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
        t.menuBarBg     = QColor(12, 20, 26);
        t.toolbarBg     = QColor(12, 20, 26);
        t.filterBarBg   = QColor(12, 20, 26);
        t.statusBarBg   = QColor(8, 14, 18);
        t.sidebarBg     = QColor(18, 28, 34);
        t.pdfViewerBg   = QColor(13, 21, 24);
        t.pdfThumbBg    = QColor(255, 255, 255);
        t.pdfSidebarBg  = QColor(10, 18, 22);
        t.pdfToolbarBg  = QColor(18, 28, 34);
        t.pdfScrollbarBg= QColor(18, 28, 34);
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
        t.menuBarBg        = QColor(0, 0, 0);
        t.toolbarBg        = QColor(0, 0, 0);
        t.filterBarBg      = QColor(4, 4, 4);
        t.statusBarBg      = QColor(0, 0, 0);
        t.sidebarBg        = QColor(8, 8, 8);
        t.pdfViewerBg      = QColor(0, 0, 0);
        t.pdfThumbBg       = QColor(255, 255, 255);
        t.pdfSidebarBg     = QColor(0, 0, 0);
        t.pdfToolbarBg     = QColor(8, 8, 8);
        t.pdfScrollbarBg   = QColor(8, 8, 8);
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
        t.menuBarBg        = QColor(0, 5, 18);
        t.toolbarBg        = QColor(0, 5, 18);
        t.filterBarBg      = QColor(0, 8, 24);
        t.statusBarBg      = QColor(0, 3, 12);
        t.sidebarBg        = QColor(5, 12, 28);
        t.pdfViewerBg      = QColor(0, 3, 12);
        t.pdfThumbBg       = QColor(255, 255, 255);
        t.pdfSidebarBg     = QColor(0, 5, 18);
        t.pdfToolbarBg     = QColor(5, 12, 28);
        t.pdfScrollbarBg   = QColor(5, 12, 28);
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
        t.menuBarBg        = QColor(8, 3, 0);
        t.toolbarBg        = QColor(8, 3, 0);
        t.filterBarBg      = QColor(12, 4, 0);
        t.statusBarBg      = QColor(4, 1, 0);
        t.sidebarBg        = QColor(20, 8, 0);
        t.pdfViewerBg      = QColor(4, 1, 0);
        t.pdfThumbBg       = QColor(255, 255, 255);
        t.pdfSidebarBg     = QColor(8, 3, 0);
        t.pdfToolbarBg     = QColor(20, 8, 0);
        t.pdfScrollbarBg   = QColor(20, 8, 0);
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
        t.menuBarBg        = QColor(4, 0, 3);
        t.toolbarBg        = QColor(4, 0, 3);
        t.filterBarBg      = QColor(6, 1, 4);
        t.statusBarBg      = QColor(2, 0, 1);
        t.sidebarBg        = QColor(18, 6, 12);
        t.pdfViewerBg      = QColor(2, 0, 1);
        t.pdfThumbBg       = QColor(255, 255, 255);
        t.pdfSidebarBg     = QColor(6, 0, 4);
        t.pdfToolbarBg     = QColor(18, 6, 12);
        t.pdfScrollbarBg   = QColor(18, 6, 12);
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
        t.menuBarBg     = QColor(10, 8, 16);
        t.toolbarBg     = QColor(10, 8, 16);
        t.filterBarBg   = QColor(12, 10, 18);
        t.statusBarBg   = QColor(7, 5, 12);
        t.sidebarBg     = QColor(25, 20, 35);
        t.pdfViewerBg   = QColor(7, 5, 12);
        t.pdfThumbBg    = QColor(255, 255, 255);
        t.pdfSidebarBg  = QColor(15, 12, 20);
        t.pdfToolbarBg  = QColor(25, 20, 35);
        t.pdfScrollbarBg= QColor(25, 20, 35);
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
        t.menuBarBg     = QColor(22, 22, 22);
        t.toolbarBg     = QColor(22, 22, 22);
        t.filterBarBg   = QColor(25, 25, 25);
        t.statusBarBg   = QColor(18, 18, 18);
        t.sidebarBg     = QColor(45, 45, 45);
        t.pdfViewerBg   = QColor(22, 22, 22);
        t.pdfThumbBg    = QColor(255, 255, 255);
        t.pdfSidebarBg  = QColor(30, 30, 30);
        t.pdfToolbarBg  = QColor(45, 45, 45);
        t.pdfScrollbarBg= QColor(45, 45, 45);
        break;
    case DesignProfile::Custom:
        break;
    }
    // Editor-Hintergründe folgen standardmäßig der Karten-/Flächenfarbe des
    // Profils (frei überschreibbar im Custom-Profil über das Design-Tab).
    t.editorBgText = t.card;
    t.editorBgHtml = t.card;
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

PageTransition AppSettings::pageTransition() const {
    return static_cast<PageTransition>(m_settings.value("ui/pageTransition", 0).toInt());
}
void AppSettings::setPageTransition(PageTransition t) {
    m_settings.setValue("ui/pageTransition", static_cast<int>(t));
}
ExtractSelectStyle AppSettings::extractSelectStyle() const {
    // Default 0 = Frame (Akzent-Rahmen) - die dezentere Variante.
    return static_cast<ExtractSelectStyle>(
        m_settings.value("ui/extractSelectStyle", 0).toInt());
}
void AppSettings::setExtractSelectStyle(ExtractSelectStyle s) {
    m_settings.setValue("ui/extractSelectStyle", static_cast<int>(s));
}
ExtractLayout AppSettings::extractLayout() const {
    // Default 0 = Workbench (neues Drei-Panel-Layout).
    return static_cast<ExtractLayout>(
        m_settings.value("ui/extractLayout", 0).toInt());
}
void AppSettings::setExtractLayout(ExtractLayout l) {
    m_settings.setValue("ui/extractLayout", static_cast<int>(l));
}
bool AppSettings::audioAccentApple() const {
    return m_settings.value("ui/audioAccentApple", false).toBool();
}
void AppSettings::setAudioAccentApple(bool v) {
    m_settings.setValue("ui/audioAccentApple", v);
}
// Mono-Play: nur eine Audio-/Video-Wiedergabe gleichzeitig (Standard: AN).
//  ── Texteditor ──────────────────────────────────────────────────────────────
//  Eigene Gruppe [editor] in der Konfiguration - sie hat mit [design] nichts zu
//  tun und darf beim Ex-/Import eines OBERFLAECHEN-Themas nicht mitwandern.
int AppSettings::editorProfile() const {
    return m_settings.value("editor/profile", 0).toInt();
}
void AppSettings::setEditorProfile(int p) {
    m_settings.setValue("editor/profile", p);
}
QString AppSettings::editorCustomPalette() const {
    return m_settings.value("editor/customPalette", QString()).toString();
}
void AppSettings::setEditorCustomPalette(const QString& json) {
    m_settings.setValue("editor/customPalette", json);
}
bool AppSettings::editorLineNumbers() const {
    return m_settings.value("editor/lineNumbers", true).toBool();
}
void AppSettings::setEditorLineNumbers(bool v) {
    m_settings.setValue("editor/lineNumbers", v);
}
bool AppSettings::editorHighlightCurrentLine() const {
    return m_settings.value("editor/highlightCurrentLine", true).toBool();
}
void AppSettings::setEditorHighlightCurrentLine(bool v) {
    m_settings.setValue("editor/highlightCurrentLine", v);
}
bool AppSettings::editorSoftWrap() const {
    //  Vorgabe AN - so verhaelt sich der Editor heute schon, und Kate ebenso.
    return m_settings.value("editor/softWrap", true).toBool();
}
void AppSettings::setEditorSoftWrap(bool v) {
    m_settings.setValue("editor/softWrap", v);
}

//  Steht in der Gruppe [gallery], nicht in [editor]: es ist eine Einstellung der
//  GALERIE-Anzeige. Die Farben dafuer kommen zwar aus der Editor-Palette, aber
//  geschaltet wird die Darstellung der Kacheln.
bool AppSettings::editorMinimap() const {
    return m_settings.value("editor/minimap", false).toBool();
}
void AppSettings::setEditorMinimap(bool v) {
    m_settings.setValue("editor/minimap", v);
}

bool AppSettings::editorFolding() const {
    return m_settings.value("editor/folding", true).toBool();
}
void AppSettings::setEditorFolding(bool v) {
    m_settings.setValue("editor/folding", v);
}

bool AppSettings::editorIndentGuides() const {
    return m_settings.value("editor/indentGuides", true).toBool();
}
void AppSettings::setEditorIndentGuides(bool v) {
    m_settings.setValue("editor/indentGuides", v);
}

bool AppSettings::editorMatchBrackets() const {
    return m_settings.value("editor/matchBrackets", true).toBool();
}
void AppSettings::setEditorMatchBrackets(bool v) {
    m_settings.setValue("editor/matchBrackets", v);
}

int AppSettings::editorTabWidth() const {
    //  Geklemmt beim LESEN: eine von Hand verfaelschte Konfiguration darf
    //  keinen unbrauchbaren Wert in den Editor durchreichen (0 hiesse
    //  Division durch null beim Einruecken).
    const int v = m_settings.value("editor/tabWidth", 4).toInt();
    return qBound(2, v, 8);
}
void AppSettings::setEditorTabWidth(int zeichen) {
    m_settings.setValue("editor/tabWidth", qBound(2, zeichen, 8));
}
bool AppSettings::editorTabSpaces() const {
    //  Vorgabe AN - so haelt es Kate, und so entstehen Dateien, die ueberall
    //  gleich aussehen.
    return m_settings.value("editor/tabSpaces", true).toBool();
}
void AppSettings::setEditorTabSpaces(bool v) {
    m_settings.setValue("editor/tabSpaces", v);
}

bool AppSettings::textPreviewContent() const {
    return m_settings.value("gallery/textPreviewContent", true).toBool();
}
void AppSettings::setTextPreviewContent(bool v) {
    m_settings.setValue("gallery/textPreviewContent", v);
}

bool AppSettings::monoPlay() const {
    return m_settings.value("ui/monoPlay", true).toBool();
}
void AppSettings::setMonoPlay(bool v) {
    m_settings.setValue("ui/monoPlay", v);
}
//  Ziehen auf ein Lesezeichen: verschieben (Standard) oder kopieren.
bool AppSettings::showHiddenFiles() const {
    return m_settings.value("gallery/showHidden", false).toBool();
}
void AppSettings::setShowHiddenFiles(bool v) { m_settings.setValue("gallery/showHidden", v); }

bool AppSettings::fileDropMove() const {
    return m_settings.value("ui/fileDropMove", true).toBool();
}
void AppSettings::setFileDropMove(bool v) {
    m_settings.setValue("ui/fileDropMove", v);
}
//  „Alle Dateien anzeigen" - Begleitdateien der App sichtbar machen.
bool AppSettings::showAllFiles() const {
    return m_settings.value("ui/showAllFiles", false).toBool();
}
void AppSettings::setShowAllFiles(bool v) {
    m_settings.setValue("ui/showAllFiles", v);
}
//  Galerie als Liste statt als Kachelraster - Vorgabe AUS (s. ISettings.h).
//  Eigener Schlüssel neben `audio/listLayout`: die beiden Ansichten werden
//  getrennt eingestellt.
bool AppSettings::galleryListLayout() const {
    return m_settings.value("gallery/listLayout", false).toBool();
}
void AppSettings::setGalleryListLayout(bool v) {
    m_settings.setValue("gallery/listLayout", v);
}
//  Zugeklappte Gruppen des Einstellungen-Fensters (s. ISettings.h). Leer =
//  alles offen, und genau das ist der Auslieferungszustand.
QStringList AppSettings::collapsedSettingsGroups() const {
    return m_settings.value("ui/collapsedGroups").toStringList();
}
void AppSettings::setCollapsedSettingsGroups(const QStringList& keys) {
    if (keys.isEmpty()) m_settings.remove("ui/collapsedGroups");
    else                m_settings.setValue("ui/collapsedGroups", keys);
}
//  Zeilenhöhe der Listen-Darstellung (s. ISettings.h). Geklemmt in BEIDE
//  Richtungen - wie `videoSeekStep`, und aus demselben Grund.
int AppSettings::galleryListRowHeight() const {
    const int v = m_settings.value("gallery/listRowHeight", 46).toInt();
    return std::clamp(v, 28, 160);
}
void AppSettings::setGalleryListRowHeight(int px) {
    m_settings.setValue("gallery/listRowHeight", std::clamp(px, 28, 160));
}
//  „Tag auch in den Unterordnern löschen" - Standard AN (s. ISettings.h).
bool AppSettings::deleteTagsInSubfolders() const {
    return m_settings.value("tags/deleteInSubfolders", true).toBool();
}
void AppSettings::setDeleteTagsInSubfolders(bool v) {
    m_settings.setValue("tags/deleteInSubfolders", v);
}
//  Schriftfarbe des TXT->PDF-Exports (Vorgabe, je Datei überschreibbar).
//  Als NAME gespeichert und beim Lesen geprüft: ein von Hand verfälschter Wert
//  ergäbe sonst eine ungültige QColor und damit unsichtbaren Text im Export.
QColor AppSettings::textPdfColor() const {
    const QColor c(m_settings.value("editor/textPdfColor",
                                    QStringLiteral("#000000")).toString());
    return c.isValid() ? c : QColor(Qt::black);
}
void AppSettings::setTextPdfColor(const QColor& c) {
    m_settings.setValue("editor/textPdfColor",
                        (c.isValid() ? c : QColor(Qt::black)).name(QColor::HexRgb));
}
// Spulschritt der Pfeiltasten im Video-Vollbild (Sekunden, Standard 15).
// Beim Lesen geklemmt, damit eine von Hand verfälschte Konfiguration nicht in
// einen 0- oder Riesen-Sprung mündet.
bool AppSettings::spellCheckEnabled() const {
    return m_settings.value("ui/spellCheck", false).toBool();
}
void AppSettings::setSpellCheckEnabled(bool v) {
    m_settings.setValue("ui/spellCheck", v);
}
QString AppSettings::spellLanguage() const {
    return m_settings.value("ui/spellLanguage", QString()).toString();
}
void AppSettings::setSpellLanguage(const QString& lang) {
    m_settings.setValue("ui/spellLanguage", lang);
}

int AppSettings::videoSeekStep() const {
    const int v = m_settings.value("ui/videoSeekStep", 15).toInt();
    return std::clamp(v, 1, 600);
}
void AppSettings::setVideoSeekStep(int seconds) {
    m_settings.setValue("ui/videoSeekStep", std::clamp(seconds, 1, 600));
}

// ─── PDF-Editor ───────────────────────────────────────────────────────────────
bool AppSettings::pdfEditPanelTop() const {
    // Standard: false -> Text-Eigenschaften als rechte Seitenleiste.
    return m_settings.value("pdfedit/panelTop", false).toBool();
}
void AppSettings::setPdfEditPanelTop(bool v) {
    m_settings.setValue("pdfedit/panelTop", v);
}

// ─── DOCX-Editor ──────────────────────────────────────────────────────────────
bool AppSettings::docxSaveDirect() const {
    // Standard: true -> Direkt speichern (mit einmaliger .bak je Sitzung).
    return m_settings.value("docx/saveDirect", true).toBool();
}
void AppSettings::setDocxSaveDirect(bool v) {
    m_settings.setValue("docx/saveDirect", v);
}

int AppSettings::docxPdfPageNumberPos() const {
    return qBound(0, m_settings.value("docx/pdfPageNumberPos", 0).toInt(), 3);
}
void AppSettings::setDocxPdfPageNumberPos(int pos) {
    m_settings.setValue("docx/pdfPageNumberPos", qBound(0, pos, 3));
}
int AppSettings::docxPdfPageNumberStyle() const {
    return qBound(0, m_settings.value("docx/pdfPageNumberStyle", 1).toInt(), 1);
}
void AppSettings::setDocxPdfPageNumberStyle(int style) {
    m_settings.setValue("docx/pdfPageNumberStyle", qBound(0, style, 1));
}

bool AppSettings::pdfExportLossless() const {
    // Standard: true -> verlustfrei bevorzugen. Das ist der schonendere Weg
    // (Text bleibt durchsuchbar, Vektorgrafik/Schriften bleiben erhalten) und
    // kann nichts kaputt machen: wo er nicht sicher anwendbar ist, weicht der
    // Controller selbsttätig auf den Raster-Export aus.
    return m_settings.value("pdfedit/exportLossless", true).toBool();
}
void AppSettings::setPdfExportLossless(bool v) {
    m_settings.setValue("pdfedit/exportLossless", v);
}

bool AppSettings::pdfExportAsAnnotations() const {
    // Standard: false -> gemalter Inhalt. Er sieht in JEDEM Betrachter und im
    // Druck gleich aus; echte Annotationen sind zwar weiterbearbeitbar, aber
    // eben auch mit einem Klick zu löschen und werden nicht überall gleich
    // dargestellt. Wer den Austausch will, schaltet es bewusst ein.
    return m_settings.value("pdfedit/exportAsAnnotations", false).toBool();
}
void AppSettings::setPdfExportAsAnnotations(bool v) {
    m_settings.setValue("pdfedit/exportAsAnnotations", v);
}

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
int  AppSettings::tileWidth()  const { return m_settings.value("grid/tileWidth",  160).toInt(); }
int  AppSettings::tileHeight() const { return m_settings.value("grid/tileHeight", 200).toInt(); }
void AppSettings::setTileSize(int w, int h) {
    m_settings.setValue("grid/tileWidth",  qMax(40, w));
    m_settings.setValue("grid/tileHeight", qMax(40, h));
    emit tileSizeChanged();
}

TileArrangement AppSettings::tileArrangement() const {
    int v = m_settings.value("grid/arrangement", 0).toInt();
    if (v < 0 || v > static_cast<int>(TileArrangement::Manual))
        return TileArrangement::Centered;
    return static_cast<TileArrangement>(v);
}
void AppSettings::setTileArrangement(TileArrangement a) {
    m_settings.setValue("grid/arrangement", static_cast<int>(a));
    emit tileArrangementChanged();
}
int  AppSettings::manualAreaWidth()  const { return m_settings.value("grid/manualAreaWidth", 800).toInt(); }
void AppSettings::setManualAreaWidth(int w) { m_settings.setValue("grid/manualAreaWidth", qMax(40, w)); }

bool AppSettings::optionsVisible() const { return m_settings.value("ui/optionsVisible", true).toBool(); }
void AppSettings::setOptionsVisible(bool v) { m_settings.setValue("ui/optionsVisible", v); }

// ─── Text editor / auto-save ──────────────────────────────────────────────────
bool AppSettings::autoSaveEnabled() const {
    return m_settings.value("editor/autoSaveEnabled", false).toBool();
}
void AppSettings::setAutoSaveEnabled(bool v) {
    m_settings.setValue("editor/autoSaveEnabled", v);
    emit autoSaveSettingsChanged();
}
int AppSettings::autoSaveIntervalSeconds() const {
    return qBound(5, m_settings.value("editor/autoSaveIntervalSeconds", 30).toInt(), 3600);
}
void AppSettings::setAutoSaveIntervalSeconds(int s) {
    m_settings.setValue("editor/autoSaveIntervalSeconds", qBound(5, s, 3600));
    emit autoSaveSettingsChanged();
}

// ─── Design / Theme ───────────────────────────────────────────────────────────
DesignProfile AppSettings::designProfile() const {
    int v = m_settings.value("design/profile", 0).toInt();

    //  ── Einmalige Umrechnung alter Werte ────────────────────────────────────
    //  Schema 1 hatte `NeonPurple` an Position 4. Ohne diese Umrechnung
    //  bekaeme jeder, der eines der Profile DAHINTER eingestellt hatte, still
    //  ein anderes - „Simple" (7) waere zu „Custom" geworden.
    const int schema = m_settings.value("design/profileSchema", 1).toInt();
    if (schema < 2) {
        if (v == 4)      v = 0;      // NeonPurple gibt es nicht mehr -> Dark
        else if (v > 4)  v -= 1;     // alles dahinter rueckt eine Stelle vor
        m_settings.setValue("design/profile", v);
        m_settings.setValue("design/profileSchema", 2);
    }

    if (v < 0 || v > static_cast<int>(DesignProfile::Custom))
        v = 0;
    return static_cast<DesignProfile>(v);
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

// ─── RHI-Backend (schreibgeschützt, vom RhiProber gesetzt) ───────────────────
QString AppSettings::rhiBackend() const {
    return m_settings.value(QStringLiteral("rhi/backend"),
                             QStringLiteral("opengl")).toString();
}

// ─── Saved / bookmarked folders ───────────────────────────────────────────────
QStringList AppSettings::savedFolders() const {
    return m_settings.value("bookmarks/folders").toStringList();
}

void AppSettings::setSavedFolders(const QStringList& paths) {
    m_settings.setValue("bookmarks/folders", paths);
}

// ─── Audio-Player: Equalizer und Optionen ─────────────────────────────────────
//  Alles unter `audio/`. Werte werden beim LESEN geklemmt - eine von Hand
//  verstellte Datei darf keinen Regler an den Anschlag oder darüber schicken.
bool AppSettings::audioEqEnabled() const { return m_settings.value("audio/eqEnabled", false).toBool(); }
void AppSettings::setAudioEqEnabled(bool on) { m_settings.setValue("audio/eqEnabled", on); }

QList<double> AppSettings::audioEqBands() const {
    const QStringList raw = m_settings.value("audio/eqBands").toStringList();
    QList<double> out;
    out.reserve(10);
    for (int i = 0; i < 10; ++i) {
        const double v = (i < raw.size()) ? raw.at(i).toDouble() : 0.0;
        out.append(qBound(-12.0, v, 12.0));
    }
    return out;
}
void AppSettings::setAudioEqBands(const QList<double>& db) {
    QStringList raw;
    raw.reserve(db.size());
    for (double v : db) raw.append(QString::number(qBound(-12.0, v, 12.0), 'f', 2));
    m_settings.setValue("audio/eqBands", raw);
}

double AppSettings::audioEqPreamp() const {
    return qBound(-12.0, m_settings.value("audio/eqPreamp", 0.0).toDouble(), 12.0);
}
void AppSettings::setAudioEqPreamp(double db) {
    m_settings.setValue("audio/eqPreamp", qBound(-12.0, db, 12.0));
}

QStringList AppSettings::audioEqPresets() const {
    return m_settings.value("audio/eqPresets").toStringList();
}
void AppSettings::setAudioEqPresets(const QStringList& presets) {
    m_settings.setValue("audio/eqPresets", presets);
}

//  Geloeschte MITGELIEFERTE Voreinstellungen. Nur ihre Namen - die Vorlagen
//  selbst stehen im Programm und kommen beim Zuruecksetzen von dort.
//  Vorgabe AUS (Festlegung des Nutzers 2026-08-29). Die Gegenrechnung wirkt,
//  aber sie macht die Wiedergabe hoerbar leiser - ein Band auf +12 dB kostet
//  rund 12 dB Pegel. Wer sauberen Klang ueber Lautstaerke stellt, schaltet sie
//  in den Einstellungen ein; die Messwerte dazu stehen in `AudioEqualizer`.
bool AppSettings::audioEqAutoPreamp() const {
    return m_settings.value("audio/eqAutoPreamp", false).toBool();
}
void AppSettings::setAudioEqAutoPreamp(bool on) {
    m_settings.setValue("audio/eqAutoPreamp", on);
}

QStringList AppSettings::audioEqHiddenPresets() const {
    return m_settings.value("audio/eqHiddenPresets").toStringList();
}
void AppSettings::setAudioEqHiddenPresets(const QStringList& names) {
    m_settings.setValue("audio/eqHiddenPresets", names);
}

//  Anzeigereihenfolge. Bewusst als NAMENSLISTE und nicht als Indizes: kommt
//  eine mitgelieferte Voreinstellung dazu oder faellt eine weg, bleibt die
//  gespeicherte Ordnung der uebrigen gueltig.
QStringList AppSettings::audioEqPresetOrder() const {
    return m_settings.value("audio/eqPresetOrder").toStringList();
}
void AppSettings::setAudioEqPresetOrder(const QStringList& names) {
    m_settings.setValue("audio/eqPresetOrder", names);
}

bool AppSettings::audioPlayerMode() const { return m_settings.value("audio/playerMode", false).toBool(); }
void AppSettings::setAudioPlayerMode(bool on) { m_settings.setValue("audio/playerMode", on); }
//  Welche Hälften im Player-Modus standen, als Bitmaske (Bit 0 = erste
//  Hälfte). Geklemmt auf vier Hälften - mehr gibt es nicht, und ein verstellter
//  Wert soll den Start nicht stören.
//
//  Fehlt die Maske, wird sie aus der früheren Schreibweise abgeleitet (EIN
//  Schalter plus EIN Platz): so verliert niemand beim Aktualisieren seinen
//  zuletzt eingestellten Zustand.
int AppSettings::audioPlayerModeMask() const {
    if (m_settings.contains("audio/playerModeMask"))
        return m_settings.value("audio/playerModeMask", 0).toInt() & 0x0F;
    if (!audioPlayerMode()) return 0;
    const int pane = qBound(0, m_settings.value("audio/playerModePane", 0).toInt(), 3);
    return 1 << pane;
}
void AppSettings::setAudioPlayerModeMask(int mask) {
    m_settings.setValue("audio/playerModeMask", mask & 0x0F);
}
bool AppSettings::audioListLayout() const { return m_settings.value("audio/listLayout", true).toBool(); }
void AppSettings::setAudioListLayout(bool on) { m_settings.setValue("audio/listLayout", on); }
//  Tags erben ist die Vorgabe: eine frisch gesicherte Tonspur steht sonst
//  unverschlagwortet in derselben Galerie wie ihr Video.
bool AppSettings::audioExtractInheritTags() const { return m_settings.value("audio/extractInheritTags", true).toBool(); }
void AppSettings::setAudioExtractInheritTags(bool on) { m_settings.setValue("audio/extractInheritTags", on); }
bool AppSettings::audioExtractToQueue() const { return m_settings.value("audio/extractToQueue", false).toBool(); }
void AppSettings::setAudioExtractToQueue(bool on) { m_settings.setValue("audio/extractToQueue", on); }
bool AppSettings::audioShowVideos() const { return m_settings.value("audio/showVideos", false).toBool(); }
void AppSettings::setAudioShowVideos(bool on) { m_settings.setValue("audio/showVideos", on); }

bool AppSettings::audioRememberLast() const { return m_settings.value("audio/rememberLast", true).toBool(); }
void AppSettings::setAudioRememberLast(bool on) { m_settings.setValue("audio/rememberLast", on); }

QString AppSettings::audioLastFile() const { return m_settings.value("audio/lastFile").toString(); }
void AppSettings::setAudioLastFile(const QString& path) { m_settings.setValue("audio/lastFile", path); }

qint64 AppSettings::audioLastPosition() const {
    return qMax<qint64>(0, m_settings.value("audio/lastPosition", 0).toLongLong());
}
void AppSettings::setAudioLastPosition(qint64 ms) {
    m_settings.setValue("audio/lastPosition", qMax<qint64>(0, ms));
}

double AppSettings::audioVolume() const {
    return qBound(0.0, m_settings.value("audio/volume", 0.85).toDouble(), 1.0);
}
void AppSettings::setAudioVolume(double v) { m_settings.setValue("audio/volume", qBound(0.0, v, 1.0)); }

bool AppSettings::audioShuffle() const { return m_settings.value("audio/shuffle", false).toBool(); }
void AppSettings::setAudioShuffle(bool on) { m_settings.setValue("audio/shuffle", on); }

int AppSettings::audioRepeat() const {
    return qBound(0, m_settings.value("audio/repeat", 0).toInt(), 2);
}
void AppSettings::setAudioRepeat(int mode) { m_settings.setValue("audio/repeat", qBound(0, mode, 2)); }

// ─── Zwei-Fenster-Modus: Verhältnis und zweiter Ordner ────────────────────────
//  Das Verhältnis wird beim LESEN geklemmt: eine von Hand verstellte Datei darf
//  keine Hälfte auf null Breite schicken (dieselbe Linie wie beim Spulschritt).
qreal AppSettings::paneSplit() const {
    const qreal v = m_settings.value("ui/paneSplit", 0.5).toDouble();
    return qBound(0.15, v, 0.85);
}
void AppSettings::setPaneSplit(qreal v) {
    m_settings.setValue("ui/paneSplit", qBound(0.15, v, 0.85));
}
QString AppSettings::secondFolder() const {
    return m_settings.value("folder/second", QString()).toString();
}
void AppSettings::setSecondFolder(const QString& path) {
    m_settings.setValue("folder/second", path);
}

// ─── Bookmark groups (display order + collapsed flag) ─────────────────────────
QStringList AppSettings::bookmarkGroups() const {
    return m_settings.value("bookmarks/groups").toStringList();
}

void AppSettings::setBookmarkGroups(const QStringList& groups) {
    m_settings.setValue("bookmarks/groups", groups);
}
