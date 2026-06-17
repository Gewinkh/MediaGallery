#include "AppController.h"

#include "FolderService.h"
#include "JsonStorage.h"
#include "TagManager.h"
#include "AppSettings.h"   // konkrete Settings-Signale für NOTIFY-Weiterleitung
#include "Strings.h"
#include "Icons.h"
#include "MediaItem.h"     // MediaItem::detectType für Drop-Behandlung

#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QVariant>
#include <QVariantMap>

AppController::AppController(ISettings& settings,
                            FolderService& folderService,
                            JsonStorage& storage,
                            TagManager& tagManager,
                            QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_folderService(folderService)
    , m_storage(storage)
    , m_tagManager(tagManager)
{
    // FolderService → QML
    connect(&m_folderService, &FolderService::folderOpened,
            this, &AppController::folderOpened);
    connect(&m_folderService, &FolderService::folderOpened,
            this, &AppController::folderChanged);

    // Settings (konkrete Instanz für Signale; Datenzugriff über ISettings&)
    AppSettings& as = AppSettings::instance();
    connect(&as, &AppSettings::colorSchemeChanged, this, &AppController::backgroundColorChanged);
    connect(&as, &AppSettings::colorSchemeChanged, this, &AppController::accentColorChanged);
    connect(&as, &AppSettings::colorSchemeChanged, this, &AppController::themeChanged);
    connect(&as, &AppSettings::themeChanged,       this, &AppController::themeChanged);
    connect(&as, &AppSettings::languageChanged,    this, &AppController::languageChanged);
    connect(&as, &AppSettings::tileSizeChanged,        this, &AppController::tileSizeChanged);
    connect(&as, &AppSettings::tileArrangementChanged, this, &AppController::tileArrangementChanged);
    connect(&as, &AppSettings::autoSaveSettingsChanged, this, &AppController::autoSaveChanged);

    // TagManager → QML
    connect(&m_tagManager, &TagManager::tagsChanged,       this, &AppController::tagsChanged);
    connect(&m_tagManager, &TagManager::categoriesChanged, this, &AppController::categoriesChanged);
}

// ── Ordner ───────────────────────────────────────────────────────────────────
QString AppController::currentFolder() const { return m_folderService.currentFolder(); }
void    AppController::openFolder(const QString& path)  { m_folderService.openFolder(path); }
void    AppController::saveCurrentFolder()              { m_folderService.saveCurrentFolder(); }
void    AppController::restoreLastFolder()              { m_folderService.restoreLastFolder(); }

void AppController::openFolderUrl(const QUrl& url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!path.isEmpty())
        m_folderService.openFolder(path);
}

void AppController::refreshCurrentFolder() {
    const QString folder = m_folderService.currentFolder();
    if (folder.isEmpty()) return;
    m_storage.loadFolder(folder);
    emit folderContentsChanged();
    emit statusMessage(Strings::get(StringKey::MenuRefresh));
}

// ── Drag & Drop ──────────────────────────────────────────────────────────────
void AppController::handleDroppedUrls(const QList<QUrl>& urls) {
    // 1) Verzeichnis im Drop → als Galerie-Ordner öffnen (erstes gewinnt).
    for (const QUrl& url : urls) {
        const QString path = url.toLocalFile();
        if (path.isEmpty()) continue;
        if (QFileInfo(path).isDir()) {
            m_folderService.openFolder(path);
            return;
        }
    }

    // 2) Mediendateien → in den aktuellen Ordner kopieren.
    const QString folder = m_folderService.currentFolder();
    const bool de = (m_settings.language() == Language::German);
    if (folder.isEmpty()) {
        emit statusMessage(de ? QStringLiteral("Kein Ordner geöffnet — Dateien können nicht hinzugefügt werden.")
                              : QStringLiteral("No folder open — files cannot be added."));
        return;
    }

    int copied = 0, skipped = 0;
    for (const QUrl& url : urls) {
        const QString src = url.toLocalFile();
        if (src.isEmpty()) continue;
        const QFileInfo fi(src);
        if (!fi.exists() || fi.isDir()) continue;
        if (MediaItem::detectType(src) == MediaType::Unknown) continue;

        const QString dest = QDir(folder).filePath(fi.fileName());
        if (QFileInfo::exists(dest)) { ++skipped; continue; }
        if (QFile::copy(src, dest)) ++copied;
    }

    if (copied > 0) {
        m_storage.loadFolder(folder);   // Metadaten für neuen Bestand anwenden
        emit folderContentsChanged();   // Galerie lädt neu (Phase 2)
    }

    if (copied > 0)
        emit statusMessage(de ? QStringLiteral("%1 Datei(en) hinzugefügt.").arg(copied)
                              : QStringLiteral("%1 file(s) added.").arg(copied));
    else if (skipped > 0)
        emit statusMessage(de ? QStringLiteral("%1 Datei(en) übersprungen (bereits vorhanden).").arg(skipped)
                              : QStringLiteral("%1 file(s) skipped (already present).").arg(skipped));
}

// ── Lesezeichen ──────────────────────────────────────────────────────────────
QVariantList AppController::savedFolders() const {
    QVariantList out;
    const QStringList entries = m_settings.savedFolders();
    out.reserve(entries.size());
    for (const QString& entry : entries) {
        const int tab = entry.indexOf(QLatin1Char('\t'));
        QString name, path;
        if (tab >= 0) {
            name = entry.left(tab).trimmed();
            path = entry.mid(tab + 1).trimmed();
        } else {
            path = entry.trimmed();
        }
        if (path.isEmpty()) continue;
        if (name.isEmpty()) name = QFileInfo(path).fileName();

        QVariantMap m;
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("path"), path);
        out.append(m);
    }
    return out;
}

void AppController::openBookmark(const QString& path) {
    if (!path.isEmpty())
        m_folderService.openFolder(path);
}

void AppController::addCurrentFolderAsBookmark() {
    const QString folder = m_folderService.currentFolder();
    if (folder.isEmpty()) return;

    const QString name  = QFileInfo(folder).fileName();
    const QString entry = name.isEmpty() ? folder
                                         : (name + QLatin1Char('\t') + folder);

    QStringList entries = m_settings.savedFolders();
    if (entries.contains(entry)) return;          // Duplikat vermeiden
    entries.append(entry);
    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

// ── Lesezeichen-Verwaltung (Phase 4) ─────────────────────────────────────────
static QString makeBookmarkEntry(const QString& name, const QString& path) {
    const QString p = path.trimmed();
    const QString n = name.trimmed();
    return n.isEmpty() ? p : (n + QLatin1Char('\t') + p);
}

void AppController::addBookmark(const QString& name, const QString& path) {
    if (path.trimmed().isEmpty()) return;
    QStringList entries = m_settings.savedFolders();
    entries.append(makeBookmarkEntry(name, path));
    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

void AppController::updateBookmark(int index, const QString& name, const QString& path) {
    if (path.trimmed().isEmpty()) return;
    QStringList entries = m_settings.savedFolders();
    if (index < 0 || index >= entries.size()) return;
    entries[index] = makeBookmarkEntry(name, path);
    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

void AppController::removeBookmark(int index) {
    QStringList entries = m_settings.savedFolders();
    if (index < 0 || index >= entries.size()) return;
    entries.removeAt(index);
    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

// ── Galerie-View-State ───────────────────────────────────────────────────────
int AppController::tileWidth()        const { return m_settings.tileWidth(); }
int AppController::tileHeight()       const { return m_settings.tileHeight(); }
int AppController::tileArrangement()  const { return static_cast<int>(m_settings.tileArrangement()); }
int AppController::manualAreaWidth()  const { return m_settings.manualAreaWidth(); }
int AppController::manualAreaHeight() const { return m_settings.manualAreaHeight(); }

void AppController::setTileSize(int w, int h) {
    const int nw = qMax(40, w);
    const int nh = qMax(40, h);
    if (m_settings.tileWidth() == nw && m_settings.tileHeight() == nh) return;
    // AppSettings::setTileSize emittiert tileSizeChanged genau einmal (weitergeleitet).
    AppSettings::instance().setTileSize(nw, nh);
    m_settings.sync();
}

void AppController::zoomIn(int stepPx) {
    setTileSize(m_settings.tileWidth() + stepPx, m_settings.tileHeight() + stepPx);
}

void AppController::zoomOut(int stepPx) {
    setTileSize(m_settings.tileWidth() - stepPx, m_settings.tileHeight() - stepPx);
}

void AppController::setTileArrangement(int arrangement) {
    const auto a = static_cast<TileArrangement>(arrangement);
    if (m_settings.tileArrangement() == a) return;
    m_settings.setTileArrangement(a);   // emittiert tileArrangementChanged (weitergeleitet)
    m_settings.sync();
}

void AppController::setManualAreaWidth(int w) {
    const int nw = qMax(40, w);
    if (m_settings.manualAreaWidth() == nw) return;
    m_settings.setManualAreaWidth(nw);
    m_settings.sync();
    emit tileArrangementChanged();
}

void AppController::setManualAreaHeight(int h) {
    const int nh = qMax(0, h);
    if (m_settings.manualAreaHeight() == nh) return;
    m_settings.setManualAreaHeight(nh);
    m_settings.sync();
    emit tileArrangementChanged();
}

// ── Einstellungen ────────────────────────────────────────────────────────────
QColor AppController::backgroundColor() const { return m_settings.backgroundColor(); }
QColor AppController::accentColor()     const { return m_settings.accentColor(); }

QString AppController::language() const {
    return m_settings.language() == Language::English
               ? QStringLiteral("en")
               : QStringLiteral("de");
}

QString AppController::videoPlayback() const {
    return m_settings.videoPlayback() == VideoPlayback::External
               ? QStringLiteral("external")
               : QStringLiteral("native");
}

bool AppController::optionsVisible() const { return m_settings.optionsVisible(); }

void AppController::setBackgroundColor(const QColor& c) {
    if (m_settings.backgroundColor() == c) return;
    m_settings.setBackgroundColor(c);   // emittiert colorSchemeChanged → weitergeleitet
}

void AppController::setAccentColor(const QColor& c) {
    if (m_settings.accentColor() == c) return;
    m_settings.setAccentColor(c);
}

void AppController::setLanguage(const QString& code) {
    const Language l = (code.compare(QLatin1String("en"), Qt::CaseInsensitive) == 0)
                           ? Language::English
                           : Language::German;
    if (m_settings.language() == l) return;
    m_settings.setLanguage(l);          // emittiert languageChanged → weitergeleitet
}

void AppController::setVideoPlayback(const QString& mode) {
    const VideoPlayback v = (mode.compare(QLatin1String("external"), Qt::CaseInsensitive) == 0)
                                ? VideoPlayback::External
                                : VideoPlayback::Native;
    if (m_settings.videoPlayback() == v) return;
    m_settings.setVideoPlayback(v);
    m_settings.sync();
    emit videoPlaybackChanged();        // AppSettings sendet hierfür kein Signal
}

void AppController::toggleOptions() {
    const bool v = !m_settings.optionsVisible();
    m_settings.setOptionsVisible(v);
    m_settings.sync();
    emit optionsVisibleChanged();       // AppSettings sendet hierfür kein Signal
}

// ── Fensterzustand ───────────────────────────────────────────────────────────
int  AppController::initialWindowWidth()  const { return m_settings.windowSize().width(); }
int  AppController::initialWindowHeight() const { return m_settings.windowSize().height(); }
int  AppController::initialWindowX()      const { return m_settings.windowPos().x(); }
int  AppController::initialWindowY()      const { return m_settings.windowPos().y(); }
bool AppController::startMaximized()      const { return m_settings.windowMaximized(); }

void AppController::saveWindowState(int w, int h, int x, int y, bool maximized) {
    m_settings.setWindowMaximized(maximized);
    if (!maximized) {                   // im maximierten Zustand keine sinnvolle Normalgeometrie
        m_settings.setWindowSize(QSize(w, h));
        m_settings.setWindowPos(QPoint(x, y));
    }
    m_settings.sync();
    m_folderService.saveCurrentFolder();
}

// ── Tags ─────────────────────────────────────────────────────────────────────
QStringList AppController::allTags() const                       { return m_tagManager.allTags(); }
QColor      AppController::tagColor(const QString& tag) const     { return m_tagManager.tagColor(tag); }
QStringList AppController::tagsForFile(const QString& f) const    { return m_tagManager.tagsForFile(f); }
QStringList AppController::categoriesForFile(const QString& f) const { return m_tagManager.categoriesForFile(f); }

void AppController::addTagToFile(const QString& f, const QString& tag)      { m_tagManager.addTagToFile(f, tag); }
void AppController::removeTagFromFile(const QString& f, const QString& tag) { m_tagManager.removeTagFromFile(f, tag); }
void AppController::setTagsForFile(const QString& f, const QStringList& t)  { m_tagManager.setTagsForFile(f, t); }

// ── Datei-Metadaten ──────────────────────────────────────────────────────────
bool      AppController::hasCustomDate(const QString& f) const { return m_storage.hasCustomDate(f); }
QDateTime AppController::customDate(const QString& f) const     { return m_storage.getCustomDate(f); }
void      AppController::setCustomDate(const QString& f, const QDateTime& dt) { m_storage.setCustomDate(f, dt); }
void      AppController::clearCustomDate(const QString& f)      { m_storage.clearCustomDate(f); }

// ── i18n ─────────────────────────────────────────────────────────────────────
QString AppController::text(int key) const {
    return Strings::get(static_cast<StringKey>(key));
}
QString AppController::text(int key, const QString& arg1) const {
    return Strings::get(static_cast<StringKey>(key), arg1);
}

QString AppController::menuFileText()           const { return Strings::get(StringKey::MenuFile); }
QString AppController::menuViewText()           const { return Strings::get(StringKey::MenuView); }
QString AppController::menuSettingsText()       const { return Strings::get(StringKey::MenuSettings); }
QString AppController::menuOpenFolderText()     const { return Strings::get(StringKey::MenuOpenFolder); }
QString AppController::menuRefreshText()        const { return Strings::get(StringKey::MenuRefresh); }
QString AppController::menuQuitText()           const { return Strings::get(StringKey::MenuQuit); }
QString AppController::menuToggleOptionsText()  const { return Strings::get(StringKey::MenuToggleOptions); }
QString AppController::menuLanguageText()       const { return Strings::get(StringKey::MenuLanguage); }
QString AppController::menuVideoPlaybackText()  const { return Strings::get(StringKey::MenuVideoPlayback); }
QString AppController::menuVideoNativeText()    const { return Strings::get(StringKey::MenuVideoNative); }
QString AppController::menuVideoExternalText()  const { return Strings::get(StringKey::MenuVideoExternal); }
QString AppController::menuBookmarksText()      const { return Strings::get(StringKey::MenuBookmarks); }
QString AppController::menuBookmarksEmptyText() const { return Strings::get(StringKey::MenuBookmarksEmpty); }
QString AppController::bookmarkAddText()        const { return Strings::get(StringKey::BookmarkAdd); }

// ── Icons ────────────────────────────────────────────────────────────────────
QString AppController::iconUri(const QString& name, int size) const {
    static const QHash<QString, QIcon (*)()> table = {
        { QStringLiteral("calendar"),  &Icons::calendar  },
        { QStringLiteral("trash"),     &Icons::trash     },
        { QStringLiteral("pencil"),    &Icons::pencil    },
        { QStringLiteral("folder"),    &Icons::folder    },
        { QStringLiteral("tag"),       &Icons::tag       },
        { QStringLiteral("palette"),   &Icons::palette   },
        { QStringLiteral("image"),     &Icons::image     },
        { QStringLiteral("play"),      &Icons::play      },
        { QStringLiteral("pause"),     &Icons::pause     },
        { QStringLiteral("music"),     &Icons::music     },
        { QStringLiteral("pdf"),       &Icons::pdf       },
        { QStringLiteral("text"),      &Icons::text      },
        { QStringLiteral("shuffle"),   &Icons::shuffle   },
        { QStringLiteral("xMark"),     &Icons::xMark     },
        { QStringLiteral("plusMark"),  &Icons::plusMark  },
        { QStringLiteral("warning"),   &Icons::warning   },
        { QStringLiteral("lock"),      &Icons::lock      },
        { QStringLiteral("arrowUp"),   &Icons::arrowUp   },
        { QStringLiteral("arrowDown"), &Icons::arrowDown },
    };

    const auto it = table.constFind(name);
    if (it == table.constEnd() || size <= 0)
        return QString();

    const QIcon icon = (it.value())();
    const QPixmap pix = icon.pixmap(QSize(size, size));
    if (pix.isNull())
        return QString();

    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly))
        return QString();
    if (!pix.toImage().save(&buffer, "PNG"))
        return QString();

    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

// ── Theme-Farben ─────────────────────────────────────────────────────────────
QColor AppController::themeBackground()  const { return m_settings.currentTheme().background; }
QColor AppController::themeCard()        const { return m_settings.currentTheme().card; }
QColor AppController::themeTextPrimary() const { return m_settings.currentTheme().textPrimary; }
QColor AppController::themeTextMuted()   const { return m_settings.currentTheme().textMuted; }
QColor AppController::themeBorder()      const { return m_settings.currentTheme().border; }
QColor AppController::themeAccent()      const { return m_settings.currentTheme().accent; }
QColor AppController::themeMenuBarBg()   const { return m_settings.currentTheme().menuBarBg; }
QColor AppController::themeToolbarBg()   const { return m_settings.currentTheme().toolbarBg; }
QColor AppController::themeFilterBarBg() const { return m_settings.currentTheme().filterBarBg; }
QColor AppController::themeStatusBarBg() const { return m_settings.currentTheme().statusBarBg; }
QColor AppController::themeSidebarBg()   const { return m_settings.currentTheme().sidebarBg; }

// ── Editor / Auto-Save (Phase 4) ─────────────────────────────────────────────
bool AppController::autoSaveEnabled()  const { return m_settings.autoSaveEnabled(); }
int  AppController::autoSaveInterval() const { return m_settings.autoSaveIntervalSeconds(); }

void AppController::setAutoSaveEnabled(bool v) {
    if (m_settings.autoSaveEnabled() == v) return;
    m_settings.setAutoSaveEnabled(v);   // emittiert autoSaveSettingsChanged → weitergeleitet
    m_settings.sync();
}

void AppController::setAutoSaveInterval(int seconds) {
    if (m_settings.autoSaveIntervalSeconds() == seconds) return;
    m_settings.setAutoSaveIntervalSeconds(seconds);
    m_settings.sync();
}

// ── Design / Theme (Phase 4) ─────────────────────────────────────────────────
int AppController::designProfile() const {
    return static_cast<int>(m_settings.designProfile());
}

void AppController::setDesignProfile(int profile) {
    const int clamped = qBound(0, profile, static_cast<int>(DesignProfile::Custom));
    const auto p = static_cast<DesignProfile>(clamped);
    if (m_settings.designProfile() == p) return;
    m_settings.setDesignProfile(p);     // emittiert themeChanged + colorSchemeChanged → weitergeleitet
    m_settings.sync();
}

QVariantList AppController::designProfiles() const {
    struct Entry { DesignProfile p; const char* icon; const char* desc; };
    static const Entry entries[] = {
        { DesignProfile::Dark,         "\xF0\x9F\x8C\x99", "Klassisch dunkel, ruhiges Teal-Akzent" },
        { DesignProfile::DarkOLED,     "\xE2\x9A\xAB",     "Reines Schwarz mit Glow — ideal für OLED" },
        { DesignProfile::OceanDepth,   "\xF0\x9F\x8C\x8A", "Tiefes Blau mit Verlauf" },
        { DesignProfile::InfernoBlaze, "\xF0\x9F\x94\xA5", "Warmes Orange-Rot" },
        { DesignProfile::NeonPurple,   "\xE2\x9A\xA1",     "Leuchtendes Violett mit Glow" },
        { DesignProfile::MidnightRose, "\xF0\x9F\x8C\xB9", "Dunkles Rosé" },
        { DesignProfile::Elegant,      "\xE2\x9C\xA8",     "Sanftes Lavendel, elegant" },
        { DesignProfile::Simple,       "\xE2\x98\x80",     "Neutrales Graustufen-Theme" },
        { DesignProfile::Custom,       "\xF0\x9F\x8E\xA8", "Eigene Farben (unten anpassbar)" },
    };

    QVariantList out;
    for (const Entry& e : entries) {
        const ThemeColors th = (e.p == DesignProfile::Custom)
                                   ? m_settings.customTheme()
                                   : AppSettings::themeForProfile(e.p);
        QVariantMap m;
        m.insert("index",       static_cast<int>(e.p));
        m.insert("name",        th.name);
        m.insert("icon",        QString::fromUtf8(e.icon));
        m.insert("description", QString::fromUtf8(e.desc));
        m.insert("accent",      th.accent);
        m.insert("card",        th.card);
        m.insert("background",  th.background);
        out.append(m);
    }
    return out;
}

QVariantMap AppController::customThemeMap() const {
    const ThemeColors t = m_settings.customTheme();
    QVariantMap m;
    m.insert("name",            t.name);
    m.insert("background",      t.background);
    m.insert("card",            t.card);
    m.insert("textPrimary",     t.textPrimary);
    m.insert("textMuted",       t.textMuted);
    m.insert("border",          t.border);
    m.insert("accentType",      static_cast<int>(t.accentType));
    m.insert("accent",          t.accent);
    m.insert("accentGradEnd",   t.accentGradEnd);
    m.insert("glowRadius",      static_cast<double>(t.glowRadius));
    m.insert("glowIntensity",   static_cast<double>(t.glowIntensity));
    m.insert("bgIsGradient",    t.bgIsGradient);
    m.insert("bgGradStart",     t.bgGradStart);
    m.insert("bgGradEnd",       t.bgGradEnd);
    m.insert("bgGradAngle",     t.bgGradAngle);
    m.insert("tileBgType",      static_cast<int>(t.tileBgType));
    m.insert("tileBgColor",     t.tileBgColor);
    m.insert("tileBgGradEnd",   t.tileBgGradEnd);
    m.insert("tileBgGradAngle", t.tileBgGradAngle);
    m.insert("tileGlowOnHover", t.tileGlowOnHover);
    m.insert("tileGlowRadius",  static_cast<double>(t.tileGlowRadius));
    m.insert("pdfViewerBg",     t.pdfViewerBg);
    m.insert("pdfThumbBg",      t.pdfThumbBg);
    m.insert("pdfSidebarBg",    t.pdfSidebarBg);
    m.insert("pdfToolbarBg",    t.pdfToolbarBg);
    m.insert("pdfScrollbarBg",  t.pdfScrollbarBg);
    m.insert("sidebarBg",       t.sidebarBg);
    m.insert("menuBarBg",       t.menuBarBg);
    m.insert("toolbarBg",       t.toolbarBg);
    m.insert("filterBarBg",     t.filterBarBg);
    m.insert("statusBarBg",     t.statusBarBg);
    return m;
}

void AppController::setCustomThemeFromMap(const QVariantMap& m) {
    // Auf bestehendem Custom-Theme aufsetzen, damit fehlende Schlüssel erhalten bleiben.
    ThemeColors t = m_settings.customTheme();

    auto col = [&](const char* key, QColor fallback) -> QColor {
        const auto it = m.constFind(QLatin1String(key));
        if (it == m.constEnd()) return fallback;
        const QColor c = it->value<QColor>();
        return c.isValid() ? c : fallback;
    };
    auto ival = [&](const char* key, int fallback) -> int {
        const auto it = m.constFind(QLatin1String(key));
        return it == m.constEnd() ? fallback : it->toInt();
    };
    auto fval = [&](const char* key, float fallback) -> float {
        const auto it = m.constFind(QLatin1String(key));
        return it == m.constEnd() ? fallback : static_cast<float>(it->toDouble());
    };
    auto bval = [&](const char* key, bool fallback) -> bool {
        const auto it = m.constFind(QLatin1String(key));
        return it == m.constEnd() ? fallback : it->toBool();
    };

    if (m.contains("name")) t.name = m.value("name").toString();
    t.background      = col("background",      t.background);
    t.card            = col("card",            t.card);
    t.textPrimary     = col("textPrimary",     t.textPrimary);
    t.textMuted       = col("textMuted",       t.textMuted);
    t.border          = col("border",          t.border);
    t.accentType      = static_cast<AccentType>(qBound(0, ival("accentType", static_cast<int>(t.accentType)),
                                                       static_cast<int>(AccentType::Glow)));
    t.accent          = col("accent",          t.accent);
    t.accentGradEnd   = col("accentGradEnd",   t.accentGradEnd);
    t.glowRadius      = fval("glowRadius",     t.glowRadius);
    t.glowIntensity   = fval("glowIntensity",  t.glowIntensity);
    t.bgIsGradient    = bval("bgIsGradient",   t.bgIsGradient);
    t.bgGradStart     = col("bgGradStart",     t.bgGradStart);
    t.bgGradEnd       = col("bgGradEnd",       t.bgGradEnd);
    t.bgGradAngle     = ival("bgGradAngle",    t.bgGradAngle);
    t.tileBgType      = static_cast<TileBgType>(qBound(0, ival("tileBgType", static_cast<int>(t.tileBgType)),
                                                       static_cast<int>(TileBgType::Transparent)));
    t.tileBgColor     = col("tileBgColor",     t.tileBgColor);
    t.tileBgGradEnd   = col("tileBgGradEnd",   t.tileBgGradEnd);
    t.tileBgGradAngle = ival("tileBgGradAngle", t.tileBgGradAngle);
    t.tileGlowOnHover = bval("tileGlowOnHover", t.tileGlowOnHover);
    t.tileGlowRadius  = fval("tileGlowRadius",  t.tileGlowRadius);
    t.pdfViewerBg     = col("pdfViewerBg",     t.pdfViewerBg);
    t.pdfThumbBg      = col("pdfThumbBg",      t.pdfThumbBg);
    t.pdfSidebarBg    = col("pdfSidebarBg",    t.pdfSidebarBg);
    t.pdfToolbarBg    = col("pdfToolbarBg",    t.pdfToolbarBg);
    t.pdfScrollbarBg  = col("pdfScrollbarBg",  t.pdfScrollbarBg);
    t.sidebarBg       = col("sidebarBg",       t.sidebarBg);
    t.menuBarBg       = col("menuBarBg",       t.menuBarBg);
    t.toolbarBg       = col("toolbarBg",       t.toolbarBg);
    t.filterBarBg     = col("filterBarBg",     t.filterBarBg);
    t.statusBarBg     = col("statusBarBg",     t.statusBarBg);

    m_settings.setCustomTheme(t);   // emittiert themeChanged + colorSchemeChanged → Live-Vorschau
    m_settings.sync();
}

bool AppController::exportCustomTheme(const QUrl& fileUrl) {
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) return false;
    return m_settings.exportCustomTheme(path);
}

bool AppController::importCustomTheme(const QUrl& fileUrl) {
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) return false;
    const bool ok = m_settings.importCustomTheme(path);  // setzt customTheme + Profil=Custom
    if (ok) m_settings.sync();
    return ok;
}
