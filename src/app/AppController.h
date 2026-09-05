#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QFont>
#include <QDateTime>
#include <QUrl>
#include <QList>
#include <QAbstractItemModel>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <vector>

#include "core/ISettings.h"

class FolderService;
class JsonStorage;
class TagManager;
class PaneController;
class PaneListModel;
class ThumbnailLoader;
class TagController;

// Zentrale C++-nach-QML-Bridge, registriert ausschließlich über `qmlRegisterSingletonInstance` in main.cpp.
// Alle Referenzen sind nicht-besitzend; die Backends leben in `main()`.
class AppController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY folderChanged)
    Q_PROPERTY(bool canNavigateBack READ canNavigateBack NOTIFY folderHistoryChanged)

    Q_PROPERTY(QColor  backgroundColor READ backgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(QColor  accentColor     READ accentColor     NOTIFY accentColorChanged)
    Q_PROPERTY(QString language        READ language        NOTIFY languageChanged)
    Q_PROPERTY(QString videoPlayback   READ videoPlayback   NOTIFY videoPlaybackChanged)
    Q_PROPERTY(QString pageTransition  READ pageTransition  NOTIFY pageTransitionChanged)
    Q_PROPERTY(QString extractSelectStyle READ extractSelectStyle NOTIFY extractSelectStyleChanged)
    Q_PROPERTY(QString extractLayout   READ extractLayout   NOTIFY extractLayoutChanged)
    Q_PROPERTY(bool    audioAccentApple READ audioAccentApple NOTIFY audioAccentChanged)
    Q_PROPERTY(bool    monoPlay        READ monoPlay        NOTIFY monoPlayChanged)
    Q_PROPERTY(bool    tileDragActive  READ tileDragActive  NOTIFY tileDragActiveChanged)
    Q_PROPERTY(bool    dragLogging     READ dragLogging     CONSTANT)
    Q_PROPERTY(bool    fileDropMove    READ fileDropMove    WRITE setFileDropMove NOTIFY fileDropMoveChanged)
    Q_PROPERTY(bool    showHiddenFiles READ showHiddenFiles WRITE setShowHiddenFiles NOTIFY showHiddenFilesChanged)
    Q_PROPERTY(bool    showAllFiles    READ showAllFiles    WRITE setShowAllFiles NOTIFY showAllFilesChanged)
    Q_PROPERTY(bool    galleryListLayout READ galleryListLayout WRITE setGalleryListLayout NOTIFY galleryListLayoutChanged)
    Q_PROPERTY(bool    textPreviewContent READ textPreviewContent WRITE setTextPreviewContent NOTIFY textPreviewContentChanged)
    Q_PROPERTY(int     listRowHeight   READ listRowHeight   NOTIFY listRowHeightChanged)
    Q_PROPERTY(int     screenWidth     READ screenWidth     NOTIFY screenWidthChanged)
    Q_PROPERTY(bool    deleteTagsInSubfolders READ deleteTagsInSubfolders WRITE setDeleteTagsInSubfolders NOTIFY deleteTagsInSubfoldersChanged)
    Q_PROPERTY(QColor  textPdfColor    READ textPdfColor    WRITE setTextPdfColor NOTIFY textPdfColorChanged)
    Q_PROPERTY(int     videoSeekStep   READ videoSeekStep   NOTIFY videoSeekStepChanged)
    Q_PROPERTY(bool    spellCheck     READ spellCheck      NOTIFY spellCheckChanged)
    Q_PROPERTY(QString spellLanguage  READ spellLanguage   NOTIFY spellCheckChanged)
    Q_PROPERTY(bool    optionsVisible  READ optionsVisible  NOTIFY optionsVisibleChanged)

    Q_PROPERTY(bool autoSaveEnabled  READ autoSaveEnabled  WRITE setAutoSaveEnabled  NOTIFY autoSaveChanged)
    Q_PROPERTY(int  autoSaveInterval READ autoSaveInterval WRITE setAutoSaveInterval NOTIFY autoSaveChanged)

    Q_PROPERTY(int designProfile READ designProfile NOTIFY themeChanged)

    Q_PROPERTY(QVariantList savedFolders READ savedFolders NOTIFY savedFoldersChanged)

    Q_PROPERTY(QVariantList panes READ panes NOTIFY panesChanged)
    //  DASSELBE als Modell - und NUR das gehoert an einen `Repeater`. Ueber die
    //  Liste oben baut er bei jeder Aenderung alle Delegates neu und zerstoert
    //  damit die andere Haelfte samt geoeffneter Datei (s. `PaneListModel`).
    Q_PROPERTY(QAbstractItemModel* panesModel READ panesModel CONSTANT)
    Q_PROPERTY(int paneCount READ paneCount NOTIFY panesChanged)
    Q_PROPERTY(int focusedPaneIndex READ focusedPaneIndex NOTIFY panesChanged)
    Q_PROPERTY(qreal paneSplit READ paneSplit WRITE setPaneSplit NOTIFY paneSplitChanged)
    Q_PROPERTY(int settingsPaneIndex READ settingsPaneIndex NOTIFY panesChanged)
    // Der ganze Baum als FLACHE Zeilenliste in Anzeigereihenfolge - flach, weil beide Verbraucher flach sind.
    // Verschachtelt zwänge es beide zu geschachtelten Repeatern, über deren Grenzen hinweg sich nichts ziehen ließe.
    Q_PROPERTY(QVariantList bookmarkTree READ bookmarkTree NOTIFY savedFoldersChanged)

    Q_PROPERTY(int tileWidth        READ tileWidth        NOTIFY tileSizeChanged)
    Q_PROPERTY(int tileHeight       READ tileHeight       NOTIFY tileSizeChanged)
    Q_PROPERTY(int tileArrangement  READ tileArrangement  NOTIFY tileArrangementChanged)
    Q_PROPERTY(int manualAreaWidth  READ manualAreaWidth  NOTIFY tileArrangementChanged)
    // Dynamische Obergrenze der Kachelgröße = die tatsächlich darstellbare Galeriefläche; von der Shell über
    // `setTileSizeLimit` gemeldet, `setTileSize`/`zoomIn` klemmen dagegen.
    Q_PROPERTY(int maxTileWidth     READ maxTileWidth     NOTIFY tileSizeLimitChanged)
    Q_PROPERTY(int maxTileHeight    READ maxTileHeight    NOTIFY tileSizeLimitChanged)

    Q_PROPERTY(int  initialWindowWidth  READ initialWindowWidth  CONSTANT)
    Q_PROPERTY(int  initialWindowHeight READ initialWindowHeight CONSTANT)
    Q_PROPERTY(int  initialWindowX      READ initialWindowX      CONSTANT)
    Q_PROPERTY(int  initialWindowY      READ initialWindowY      CONSTANT)
    Q_PROPERTY(bool startMaximized      READ startMaximized      CONSTANT)

    Q_PROPERTY(bool docxAvailable       READ docxAvailable       CONSTANT)

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
    Q_PROPERTY(QColor themeEditorBgText READ themeEditorBgText NOTIFY themeChanged)
    Q_PROPERTY(QColor themeEditorBgHtml READ themeEditorBgHtml NOTIFY themeChanged)

public:
    // Der ordnerbezogene Zustand gehört der HÄLFTE (`PaneController`), nicht dieser Fassade: `setFocusedPane` sagt,
    // welche gemeint ist, und alle Wege unten reichen dorthin weiter - bestehendes QML bleibt gültig.
    explicit AppController(ISettings& settings, QObject* parent = nullptr);

    void setFocusedPane(PaneController* pane);
    PaneController* focusedPane() const { return m_pane; }

    void setThumbnailLoader(ThumbnailLoader* loader) { m_loader = loader; }
    void setTagsFacade(TagController* facade);
    Q_INVOKABLE QObject* addPane();
    Q_INVOKABLE bool closePane(int index);
    void persistPaneFolders();
    Q_INVOKABLE void focusPane(int index);
    Q_INVOKABLE bool swapPanes();
    Q_INVOKABLE void setSettingsPaneIndex(int index);
    int settingsPaneIndex() const { return m_settingsPane; }
    Q_INVOKABLE int  indexOfPane(QObject* pane) const;
    Q_INVOKABLE QString secondFolder() const;
    QVariantList panes() const;
    QAbstractItemModel* panesModel() const;
    int paneCount() const { return int(m_panes.size()); }
    int focusedPaneIndex() const;

    QString currentFolder() const;
    Q_INVOKABLE void openFolderUrl(const QUrl& url);
    Q_INVOKABLE void refreshCurrentFolder();

    // `openSubfolder` ist der EINZIGE Weg, der etwas auf den Rückweg legt - nur ein Abstieg ist ein Schritt, den
    // man zurücknehmen will. Jeder andere Ordnerwechsel LEERT den Stapel; ein Vorwärts gibt es bewusst nicht.
    Q_INVOKABLE void openSubfolder(const QString& path);
    Q_INVOKABLE bool navigateBack();
    bool canNavigateBack() const;

    Q_INVOKABLE QString createEmptyFile(const QString& kind, const QString& baseName,
                                        const QString& targetFolder = QString());
    Q_INVOKABLE void restoreLastFolder();

    Q_INVOKABLE void handleDroppedUrls(const QList<QUrl>& urls,
                                       const QString& targetFolder = QString());

    QVariantList savedFolders() const;
    QVariantList bookmarkTree() const;
    Q_INVOKABLE void beginTileDrag();
    Q_INVOKABLE void endTileDrag();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

public:

    Q_INVOKABLE void openBookmark(const QString& path);
    Q_INVOKABLE void addBookmark(const QString& name, const QString& path,
                                 const QString& group = QString());
    Q_INVOKABLE void updateBookmark(int index, const QString& name, const QString& path,
                                    const QString& group = QString());
    Q_INVOKABLE void removeBookmark(int index);

    // Identitaet einer Gruppe ist ihr voller Pfad mit "/"; ein Name darf deshalb weder
    // "/" noch Tabulator tragen. "" heisst oberste Ebene. Fehlt die Elterngruppe, wird
    // sie samt Vorfahren angelegt statt den Aufruf zu verwerfen.
    Q_INVOKABLE void addBookmarkGroup(const QString& name,
                                      const QString& parentPath = QString());
    Q_INVOKABLE void renameBookmarkGroup(const QString& path, const QString& newName);
    Q_INVOKABLE void removeBookmarkGroup(const QString& path);
    Q_INVOKABLE void setBookmarkGroupCollapsed(const QString& path, bool collapsed);
    Q_INVOKABLE void moveBookmarkGroup(const QString& path, const QString& newParentPath,
                                       int pos);
    Q_INVOKABLE void moveBookmark(int index, const QString& targetGroup, int pos);
    Q_INVOKABLE bool isUsableGroupName(const QString& name) const;

    bool autoSaveEnabled()  const;
    int  autoSaveInterval() const;
    Q_INVOKABLE void setAutoSaveEnabled(bool v);
    Q_INVOKABLE void setAutoSaveInterval(int seconds);

    int  designProfile() const;                    // == DesignProfile
    Q_INVOKABLE void setDesignProfile(int profile);
    Q_INVOKABLE QVariantList designProfiles() const;
    Q_INVOKABLE QVariantMap customThemeMap() const;
    Q_INVOKABLE void setCustomThemeFromMap(const QVariantMap& m);  // Live-Vorschau, wenn Custom aktiv
    Q_INVOKABLE bool exportCustomTheme(const QUrl& fileUrl);
    Q_INVOKABLE bool importCustomTheme(const QUrl& fileUrl);

    // QML kennt in Qt 6.4 kein `font.families`; deshalb kommt aus C++ ein QFont mit Substitutionsliste (führende
    // Familie für Latein, Naskh/CJK je Glyphe als Rückfall).
    Q_INVOKABLE QFont fallbackFont(const QString& family, qreal pixelSize,
                                   bool bold = false, bool italic = false,
                                   bool underline = false) const;

    int  tileWidth()        const;
    int  tileHeight()       const;
    int  tileArrangement()  const;   // == TileArrangement (Centered/Left/Right/Manual)
    int  manualAreaWidth()  const;
    Q_INVOKABLE void setTileSize(int w, int h);
    Q_INVOKABLE void zoomIn(int stepPx = 16);
    Q_INVOKABLE void zoomOut(int stepPx = 16);
    Q_INVOKABLE void setTileArrangement(int arrangement);
    Q_INVOKABLE void setManualAreaWidth(int w);
    int  maxTileWidth()  const { return m_maxTileW; }
    int  maxTileHeight() const { return m_maxTileH; }
    Q_INVOKABLE void setTileSizeLimit(int w, int h);

    QColor  backgroundColor() const;
    QColor  accentColor()     const;
    QString language()        const;   // "de" | "en"
    QString videoPlayback()   const;   // "native" | "external"
    QString pageTransition()  const;   // "slide" | "fade"
    QString extractSelectStyle() const;   // "frame" | "overlay"
    QString extractLayout() const;        // "workbench" | "compact"
    bool    audioAccentApple() const;  // true = Apple-Blau, false = Theme-Akzent
    bool    monoPlay()        const;   // true = nur EINE Wiedergabe gleichzeitig
    int     videoSeekStep()   const;   // Spulschritt der Pfeiltasten (Sekunden)
    bool    spellCheck()      const;
    QString spellLanguage()   const;
    Q_INVOKABLE void setSpellCheck(bool v);
    Q_INVOKABLE void setSpellLanguage(const QString& lang);
    Q_INVOKABLE QStringList spellLanguages() const;
    bool    optionsVisible()  const;
    Q_INVOKABLE void setBackgroundColor(const QColor& c);
    Q_INVOKABLE void setAccentColor(const QColor& c);
    Q_INVOKABLE void setLanguage(const QString& code);       // "de" | "en"
    Q_INVOKABLE void setVideoPlayback(const QString& mode);  // "native" | "external"
    Q_INVOKABLE void setPageTransition(const QString& mode); // "slide" | "fade"
    Q_INVOKABLE void setExtractSelectStyle(const QString& style); // "frame" | "overlay"
    Q_INVOKABLE void setExtractLayout(const QString& layout);     // "workbench" | "compact"
    Q_INVOKABLE void setAudioAccentApple(bool apple);
    Q_INVOKABLE void setMonoPlay(bool on);
    Q_INVOKABLE void setVideoSeekStep(int seconds);

    Q_INVOKABLE void announcePlayback(const QString& token);

    Q_INVOKABLE bool trySetRhiBackend(const QString& backend);

    Q_INVOKABLE void toggleOptions();

    int  initialWindowWidth()  const;
    int  initialWindowHeight() const;
    int  initialWindowX()      const;
    int  initialWindowY()      const;
    bool startMaximized()      const;
    bool docxAvailable()       const;
    Q_INVOKABLE void saveWindowState(int w, int h, int x, int y, bool maximized);

    Q_INVOKABLE QStringList allTags() const;
    Q_INVOKABLE QColor      tagColor(const QString& tag) const;

    Q_INVOKABLE QString text(int key) const;
    Q_INVOKABLE QString text(int key, const QString& arg1) const;
    Q_INVOKABLE QString uiText(const QString& lang, const QString& key) const;
    Q_INVOKABLE QString fileUrl(const QString& path) const;
    Q_INVOKABLE QString localPath(const QString& urlOrPath) const;

    // Abgelegt wird, was ein Dateimanager erwartet: `text/uri-list`, `text/plain` und `x-special/gnome-copied-files`
    // (Nautilus/Nemo verlangen es, Dolphin liest uri-list). Unter Windows/macOS bildet Qt die Dateiliste selbst ab.
    Q_INVOKABLE int copyFilesToClipboard(const QStringList& paths) const;
    // Gegenstück: die Dateien AUS der Zwischenablage, gelesen als `text/uri-list`. Zurück kommen nur Adressen, die
    // es wirklich gibt; Ordner bleiben außen vor.
    Q_INVOKABLE QList<QUrl> clipboardFileUrls() const;

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
    bool tileDragActive() const { return m_tileDragActive; }
    bool dragLogging() const { return qEnvironmentVariableIsSet("MG_DRAGLOG"); }
    bool fileDropMove() const;
    bool showHiddenFiles() const;
    void setShowHiddenFiles(bool v);
    bool showAllFiles() const;
    bool galleryListLayout() const;
    bool textPreviewContent() const;
    void setTextPreviewContent(bool v);
    bool deleteTagsInSubfolders() const;
    void setDeleteTagsInSubfolders(bool v);
    void setShowAllFiles(bool v);
    void setGalleryListLayout(bool v);


    int  listRowHeight() const;
    Q_INVOKABLE void setListRowHeight(int px);
    Q_INVOKABLE void zoomInList(int stepPx = 4);
    Q_INVOKABLE void zoomOutList(int stepPx = 4);

    int  screenWidth() const { return m_screenW; }
    Q_INVOKABLE void setScreenWidth(int w);
    // `key` ist ein STABILER Schlüssel je Gruppe ("view.tiles"), nicht die übersetzte Überschrift; unbekannt =
    // offen. Bewusst OHNE NOTIFY: eine Gruppe liest einmal beim Entstehen und schreibt beim Umschalten.
    Q_INVOKABLE bool settingsGroupCollapsed(const QString& key) const;
    Q_INVOKABLE void setSettingsGroupCollapsed(const QString& key, bool collapsed);
    QColor textPdfColor() const;
    void   setTextPdfColor(const QColor& c);
    void setFileDropMove(bool v);

    QString menuBookmarksText()      const;
    QString menuBookmarksEmptyText() const;
    QString bookmarkAddText()        const;


signals:
    void folderOpened(const QString& path);
    void folderHistoryChanged();
    void folderChanged();
    void folderContentsChanged();   // Inhalt änderte sich (Drop/Refresh) -> Galerie neu laden (Phase 2)
    void statusMessage(const QString& text);
    void backgroundColorChanged();
    void accentColorChanged();
    void languageChanged();
    void videoPlaybackChanged();
    void pageTransitionChanged();
    void extractSelectStyleChanged();
    void extractLayoutChanged();
    void audioAccentChanged();
    void monoPlayChanged();
    void videoSeekStepChanged();
    void spellCheckChanged();
    // Mono-Play: eine Wiedergabestelle hat gestartet (nur bei aktiver Option).
    void playbackStarted(const QString& token);
    void optionsVisibleChanged();
    void savedFoldersChanged();
    void panesChanged();
    void paneSplitChanged();
    void tileDragActiveChanged();
    //  Mausrad WAEHREND eines Zuges - `angleDelta().y()`. Die Galerie scrollt
    //  daraufhin selbst. Kommt nichts an, hat die Plattform das Rad im Zug
    //  ueberhaupt nicht weitergereicht (dann bleibt das Randscrollen).
    void dragWheel(int angleDeltaY);
    void fileDropMoveChanged();
    void showHiddenFilesChanged();
    void showAllFilesChanged();
    void galleryListLayoutChanged();
    //  Die Kacheln muessen danach NEU erzeugt werden - main.cpp haengt daran.
    void textPreviewContentChanged();
    void listRowHeightChanged();
    void screenWidthChanged();
    void deleteTagsInSubfoldersChanged();
    void textPdfColorChanged();
    void themeChanged();
    void autoSaveChanged();
    void tileSizeChanged();
    void tileSizeLimitChanged();
    void tileArrangementChanged();
    void tagsChanged();
    void categoriesChanged();

private:
    //  Der Rückweg (Alt+<-) gehört zur Hälfte - s. `PaneController`.

    // Der Abschnitt, in dem ein Gruppenpfad landet: der Pfad in der GESPEICHERTEN Schreibweise, wenn es die Gruppe
    // gibt, sonst "". Ein von Hand verstellter Pfad lässt damit nie ein Lesezeichen verschwinden.
    QString bookmarkSection(const QString& group) const;
    //  Legt `fullPath` samt aller fehlenden Vorfahren an. Gibt den Pfad in der
    //  gespeicherten Schreibweise zurück (leer, wenn er unbrauchbar war).
    QString ensureBookmarkGroup(const QString& fullPath);


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
    QColor themeEditorBgText() const;
    QColor themeEditorBgHtml() const;

    ISettings&     m_settings;
    //  Die FOKUSSIERTE Hälfte (nicht besitzend). Alle ordnerbezogenen Aufrufe
    //  gehen dorthin; ohne Hälfte tun sie nichts, statt ins Leere zu greifen.
    PaneController* m_pane = nullptr;
    //  Die Hälften gehören dieser Fassade (sie überleben QML-Neuaufbauten).
    static constexpr int kMaxPanes = 2;
    std::vector<PaneController*> m_panes;
    //  Sicht auf `m_panes` fuer QML-Repeater; wird von addPane/closePane/
    //  swapPanes um jede Aenderung geklammert.
    PaneListModel* m_panesModel = nullptr;
    ThumbnailLoader* m_loader = nullptr;
    TagController*   m_tagsFacade = nullptr;
    int              m_settingsPane = -1;

public:
    qreal paneSplit() const;
    void  setPaneSplit(qreal v);
private:
    bool           m_tileDragActive = false;
    // Was WIR zuletzt kopiert haben: die Systemablage liefert eine lange Adressliste nicht vollständig zurück
    // (gemessen auf KDE/Wayland: 29 abgelegt, 3 wiedergelesen). Solange wir Eigentümer sind, gilt diese Liste.
    QStringList    m_ownClipFiles;
    //  Kam die letzte Aenderung der Ablage von UNS? Dann bleibt `m_ownClipFiles`
    //  stehen, sonst wird sie verworfen (ein anderes Programm hat kopiert).
    bool           m_clipSelfSet = false;
    //  Nur fuer die Fehlersuche (Umgebungsvariable MG_DRAGLOG=1): zaehlt, welche
    //  Ereignistypen waehrend eines Zuges ueberhaupt bei der Anwendung ankommen.
    bool               m_dragLog = false;
    QHash<int, int>    m_dragEventCounts;



    // Kachelgrößen-Obergrenze (Galeriefläche; von der Shell gemeldet).
    // Startwert = großzügiger Fallback, bis die Shell die reale Fläche meldet.
    int m_maxTileW = 4096;
    int     m_screenW = 0;   // 0 = noch nicht gemeldet
    int m_maxTileH = 4096;
};
