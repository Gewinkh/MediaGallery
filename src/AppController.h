#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QDateTime>
#include <QUrl>
#include <QList>
#include <QSize>
#include <QPoint>
#include <QVariantList>
#include <QVariantMap>

#include "ISettings.h"

class FolderService;
class JsonStorage;
class TagManager;

// ─────────────────────────────────────────────────────────────────────────────
//  AppController — zentrale C++→QML-Bridge (Singleton).
//
//  Reine DELEGATION an bestehende Backends (ISettings, FolderService,
//  JsonStorage, TagManager). KEINE Geschäftslogik außer der Koordination, die
//  zuvor in MainWindow lag (Ordner-Drop, Lesezeichen, Fensterzustand). Diese
//  wandert hier ins Backend, weil MainWindow ab Phase 1 entfällt.
//
//  Registrierung ausschließlich über qmlRegisterSingletonInstance in main.cpp —
//  keine QML_ELEMENT/QML_SINGLETON-Makros. Alle Referenzen sind nicht-besitzend;
//  die Backends leben in main().
// ─────────────────────────────────────────────────────────────────────────────
class AppController : public QObject {
    Q_OBJECT

    // ── Ordner-Status ───────────────────────────────────────────────────────
    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY folderChanged)

    // ── Einstellungen (lesbar; Setter sind Q_INVOKABLE) ─────────────────────
    Q_PROPERTY(QColor  backgroundColor READ backgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(QColor  accentColor     READ accentColor     NOTIFY accentColorChanged)
    Q_PROPERTY(QString language        READ language        NOTIFY languageChanged)
    Q_PROPERTY(QString videoPlayback   READ videoPlayback   NOTIFY videoPlaybackChanged)
    Q_PROPERTY(bool    optionsVisible  READ optionsVisible  NOTIFY optionsVisibleChanged)

    // ── Editor / Auto-Save (Phase 4) ────────────────────────────────────────
    Q_PROPERTY(bool autoSaveEnabled  READ autoSaveEnabled  WRITE setAutoSaveEnabled  NOTIFY autoSaveChanged)
    Q_PROPERTY(int  autoSaveInterval READ autoSaveInterval WRITE setAutoSaveInterval NOTIFY autoSaveChanged)

    // ── Design / Theme (Phase 4) ────────────────────────────────────────────
    // designProfile == DesignProfile (0=Dark … 8=Custom). Live-Wechsel über themeChanged.
    Q_PROPERTY(int designProfile READ designProfile NOTIFY themeChanged)

    // ── Lesezeichen (gespeicherte Ordner) ───────────────────────────────────
    // Liste von { "name": QString, "path": QString } für die QML-Repeater.
    Q_PROPERTY(QVariantList savedFolders READ savedFolders NOTIFY savedFoldersChanged)

    // ── Galerie-View-State (Phase 2): Kachelgröße / Anordnung / Zoom ─────────
    // Persistiert über ISettings; GalleryView.qml bindet direkt an diese Props.
    Q_PROPERTY(int tileWidth        READ tileWidth        NOTIFY tileSizeChanged)
    Q_PROPERTY(int tileHeight       READ tileHeight       NOTIFY tileSizeChanged)
    Q_PROPERTY(int tileArrangement  READ tileArrangement  NOTIFY tileArrangementChanged)
    Q_PROPERTY(int manualAreaWidth  READ manualAreaWidth  NOTIFY tileArrangementChanged)
    Q_PROPERTY(int manualAreaHeight READ manualAreaHeight NOTIFY tileArrangementChanged)

    // ── Startgeometrie (einmalig gelesen, daher CONSTANT) ───────────────────
    Q_PROPERTY(int  initialWindowWidth  READ initialWindowWidth  CONSTANT)
    Q_PROPERTY(int  initialWindowHeight READ initialWindowHeight CONSTANT)
    Q_PROPERTY(int  initialWindowX      READ initialWindowX      CONSTANT)
    Q_PROPERTY(int  initialWindowY      READ initialWindowY      CONSTANT)
    Q_PROPERTY(bool startMaximized      READ startMaximized      CONSTANT)

    // ── Shell-Beschriftungen (i18n, reaktiv bei languageChanged) ────────────
    Q_PROPERTY(QString menuFileText           READ menuFileText           NOTIFY languageChanged)
    Q_PROPERTY(QString menuViewText           READ menuViewText           NOTIFY languageChanged)
    Q_PROPERTY(QString menuSettingsText       READ menuSettingsText       NOTIFY languageChanged)
    Q_PROPERTY(QString menuOpenFolderText     READ menuOpenFolderText     NOTIFY languageChanged)
    Q_PROPERTY(QString menuRefreshText        READ menuRefreshText        NOTIFY languageChanged)
    Q_PROPERTY(QString menuQuitText           READ menuQuitText           NOTIFY languageChanged)
    Q_PROPERTY(QString menuToggleOptionsText  READ menuToggleOptionsText  NOTIFY languageChanged)
    Q_PROPERTY(QString menuLanguageText       READ menuLanguageText       NOTIFY languageChanged)
    Q_PROPERTY(QString menuVideoPlaybackText  READ menuVideoPlaybackText  NOTIFY languageChanged)
    Q_PROPERTY(QString menuVideoNativeText    READ menuVideoNativeText    NOTIFY languageChanged)
    Q_PROPERTY(QString menuVideoExternalText  READ menuVideoExternalText  NOTIFY languageChanged)
    Q_PROPERTY(QString menuBookmarksText      READ menuBookmarksText      NOTIFY languageChanged)
    Q_PROPERTY(QString menuBookmarksEmptyText READ menuBookmarksEmptyText NOTIFY languageChanged)
    Q_PROPERTY(QString bookmarkAddText        READ bookmarkAddText        NOTIFY languageChanged)

    // ── Theme-Farben (Style-Helper, lesbar; alle über themeChanged) ─────────
    Q_PROPERTY(QColor themeBackground  READ themeBackground  NOTIFY themeChanged)
    Q_PROPERTY(QColor themeCard        READ themeCard        NOTIFY themeChanged)
    Q_PROPERTY(QColor themeTextPrimary READ themeTextPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor themeTextMuted   READ themeTextMuted   NOTIFY themeChanged)
    Q_PROPERTY(QColor themeBorder      READ themeBorder      NOTIFY themeChanged)
    Q_PROPERTY(QColor themeAccent      READ themeAccent      NOTIFY themeChanged)
    Q_PROPERTY(QColor themeMenuBarBg   READ themeMenuBarBg   NOTIFY themeChanged)
    Q_PROPERTY(QColor themeToolbarBg   READ themeToolbarBg   NOTIFY themeChanged)
    Q_PROPERTY(QColor themeFilterBarBg READ themeFilterBarBg NOTIFY themeChanged)
    Q_PROPERTY(QColor themeStatusBarBg READ themeStatusBarBg NOTIFY themeChanged)
    Q_PROPERTY(QColor themeSidebarBg   READ themeSidebarBg   NOTIFY themeChanged)

public:
    AppController(ISettings& settings,
                  FolderService& folderService,
                  JsonStorage& storage,
                  TagManager& tagManager,
                  QObject* parent = nullptr);

    // ── Ordner (Delegation an FolderService) ────────────────────────────────
    QString currentFolder() const;
    Q_INVOKABLE void openFolder(const QString& path);
    Q_INVOKABLE void openFolderUrl(const QUrl& url);
    Q_INVOKABLE void refreshCurrentFolder();
    Q_INVOKABLE void restoreLastFolder();
    Q_INVOKABLE void saveCurrentFolder();

    // ── Drag & Drop von Ordnern/Dateien auf das Fenster ─────────────────────
    Q_INVOKABLE void handleDroppedUrls(const QList<QUrl>& urls);

    // ── Lesezeichen (Delegation an ISettings) ───────────────────────────────
    QVariantList savedFolders() const;
    Q_INVOKABLE void openBookmark(const QString& path);
    Q_INVOKABLE void addCurrentFolderAsBookmark();
    // Phase 4: vollständige Lesezeichen-Verwaltung (für SettingsDialog/Bookmark-Tab)
    Q_INVOKABLE void addBookmark(const QString& name, const QString& path);
    Q_INVOKABLE void updateBookmark(int index, const QString& name, const QString& path);
    Q_INVOKABLE void removeBookmark(int index);

    // ── Editor / Auto-Save (Phase 4) ────────────────────────────────────────
    bool autoSaveEnabled()  const;
    int  autoSaveInterval() const;
    Q_INVOKABLE void setAutoSaveEnabled(bool v);
    Q_INVOKABLE void setAutoSaveInterval(int seconds);

    // ── Design / Theme (Phase 4) ────────────────────────────────────────────
    int  designProfile() const;                    // == DesignProfile
    Q_INVOKABLE void setDesignProfile(int profile);
    // Liste der eingebauten Profile für die QML-Auswahlkarten:
    //   [{ index, name, icon, description, accent, card, background }]
    Q_INVOKABLE QVariantList designProfiles() const;
    // Alle ThemeColors-Felder des Custom-Themes als Map (Farben als QColor,
    // Enums als int, Floats als real). Für zweiseitige Bindung im Design-Tab.
    Q_INVOKABLE QVariantMap customThemeMap() const;
    Q_INVOKABLE void setCustomThemeFromMap(const QVariantMap& m);  // Live-Vorschau, wenn Custom aktiv
    Q_INVOKABLE bool exportCustomTheme(const QUrl& fileUrl);
    Q_INVOKABLE bool importCustomTheme(const QUrl& fileUrl);

    // ── Galerie-View-State (Delegation an ISettings) ────────────────────────
    int  tileWidth()        const;
    int  tileHeight()       const;
    int  tileArrangement()  const;   // == TileArrangement (Centered/Left/Right/Manual)
    int  manualAreaWidth()  const;
    int  manualAreaHeight() const;
    Q_INVOKABLE void setTileSize(int w, int h);
    Q_INVOKABLE void zoomIn(int stepPx = 16);
    Q_INVOKABLE void zoomOut(int stepPx = 16);
    Q_INVOKABLE void setTileArrangement(int arrangement);
    Q_INVOKABLE void setManualAreaWidth(int w);
    Q_INVOKABLE void setManualAreaHeight(int h);

    // ── Einstellungen (Delegation an ISettings) ─────────────────────────────
    QColor  backgroundColor() const;
    QColor  accentColor()     const;
    QString language()        const;   // "de" | "en"
    QString videoPlayback()   const;   // "native" | "external"
    bool    optionsVisible()  const;
    Q_INVOKABLE void setBackgroundColor(const QColor& c);
    Q_INVOKABLE void setAccentColor(const QColor& c);
    Q_INVOKABLE void setLanguage(const QString& code);       // "de" | "en"
    Q_INVOKABLE void setVideoPlayback(const QString& mode);  // "native" | "external"
    Q_INVOKABLE void toggleOptions();

    // ── Fensterzustand (Delegation an ISettings) ────────────────────────────
    int  initialWindowWidth()  const;
    int  initialWindowHeight() const;
    int  initialWindowX()      const;
    int  initialWindowY()      const;
    bool startMaximized()      const;
    Q_INVOKABLE void saveWindowState(int w, int h, int x, int y, bool maximized);

    // ── Tags (Delegation an TagManager) ─────────────────────────────────────
    Q_INVOKABLE QStringList allTags() const;
    Q_INVOKABLE QColor      tagColor(const QString& tag) const;
    Q_INVOKABLE QStringList tagsForFile(const QString& fileName) const;
    Q_INVOKABLE QStringList categoriesForFile(const QString& fileName) const;
    Q_INVOKABLE void addTagToFile(const QString& fileName, const QString& tag);
    Q_INVOKABLE void removeTagFromFile(const QString& fileName, const QString& tag);
    Q_INVOKABLE void setTagsForFile(const QString& fileName, const QStringList& tags);

    // ── Datei-Metadaten (Delegation an JsonStorage) ─────────────────────────
    Q_INVOKABLE bool      hasCustomDate(const QString& fileName) const;
    Q_INVOKABLE QDateTime customDate(const QString& fileName) const;
    Q_INVOKABLE void      setCustomDate(const QString& fileName, const QDateTime& dt);
    Q_INVOKABLE void      clearCustomDate(const QString& fileName);

    // ── i18n (Delegation an Strings) ────────────────────────────────────────
    // key == Ganzzahlwert von StringKey (siehe Strings.h). Sugar-Enum-Export
    // für QML folgt in einer späteren Phase; hier nur das Fundament.
    Q_INVOKABLE QString text(int key) const;
    Q_INVOKABLE QString text(int key, const QString& arg1) const;

    // Shell-Beschriftungen (reaktiv über languageChanged)
    QString menuFileText()           const;
    QString menuViewText()           const;
    QString menuSettingsText()       const;
    QString menuOpenFolderText()     const;
    QString menuRefreshText()        const;
    QString menuQuitText()           const;
    QString menuToggleOptionsText()  const;
    QString menuLanguageText()       const;
    QString menuVideoPlaybackText()  const;
    QString menuVideoNativeText()    const;
    QString menuVideoExternalText()  const;
    QString menuBookmarksText()      const;
    QString menuBookmarksEmptyText() const;
    QString bookmarkAddText()        const;

    // ── Icons (Delegation an Icons) ─────────────────────────────────────────
    // Liefert ein "data:image/png;base64,..."-URI, direkt als Image.source
    // nutzbar — KEIN QQuickImageProvider (siehe Performance-Vorgaben).
    Q_INVOKABLE QString iconUri(const QString& name, int size = 16) const;

signals:
    void folderOpened(const QString& path);
    void folderChanged();
    void folderContentsChanged();   // Inhalt änderte sich (Drop/Refresh) → Galerie neu laden (Phase 2)
    void statusMessage(const QString& text);
    void backgroundColorChanged();
    void accentColorChanged();
    void languageChanged();
    void videoPlaybackChanged();
    void optionsVisibleChanged();
    void savedFoldersChanged();
    void themeChanged();
    void autoSaveChanged();
    void tileSizeChanged();
    void tileArrangementChanged();
    void tagsChanged();
    void categoriesChanged();

private:
    // Theme-Lese-Helfer
    QColor themeBackground()  const;
    QColor themeCard()        const;
    QColor themeTextPrimary() const;
    QColor themeTextMuted()   const;
    QColor themeBorder()      const;
    QColor themeAccent()      const;
    QColor themeMenuBarBg()   const;
    QColor themeToolbarBg()   const;
    QColor themeFilterBarBg() const;
    QColor themeStatusBarBg() const;
    QColor themeSidebarBg()   const;

    ISettings&     m_settings;
    FolderService& m_folderService;
    JsonStorage&   m_storage;
    TagManager&    m_tagManager;
};
