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

// ─────────────────────────────────────────────────────────────────────────────
//  AppController - zentrale C++->QML-Bridge (Singleton).
//
//  Registrierung ausschließlich über qmlRegisterSingletonInstance in main.cpp -
//  keine QML_ELEMENT/QML_SINGLETON-Makros. Alle Referenzen sind nicht-besitzend;
//  die Backends leben in main().
// ─────────────────────────────────────────────────────────────────────────────
class AppController : public QObject {
    Q_OBJECT

    // ── Ordner-Status ───────────────────────────────────────────────────────
    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY folderChanged)
    //  Gibt es einen Rueckweg (Alt+<-)? s. openSubfolder/navigateBack.
    Q_PROPERTY(bool canNavigateBack READ canNavigateBack NOTIFY folderHistoryChanged)

    // ── Einstellungen (lesbar; Setter sind Q_INVOKABLE) ─────────────────────
    Q_PROPERTY(QColor  backgroundColor READ backgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(QColor  accentColor     READ accentColor     NOTIFY accentColorChanged)
    Q_PROPERTY(QString language        READ language        NOTIFY languageChanged)
    Q_PROPERTY(QString videoPlayback   READ videoPlayback   NOTIFY videoPlaybackChanged)
    Q_PROPERTY(QString pageTransition  READ pageTransition  NOTIFY pageTransitionChanged)
    Q_PROPERTY(QString extractSelectStyle READ extractSelectStyle NOTIFY extractSelectStyleChanged)
    Q_PROPERTY(QString extractLayout   READ extractLayout   NOTIFY extractLayoutChanged)
    Q_PROPERTY(bool    audioAccentApple READ audioAccentApple NOTIFY audioAccentChanged)
    Q_PROPERTY(bool    monoPlay        READ monoPlay        NOTIFY monoPlayChanged)
    //  Läuft gerade ein Zug mit einer Galerie-Kachel? Die Lesezeichen-Leiste
    //  zum Ablegen erscheint NUR währenddessen - ein Zug soll ein Ziel haben,
    //  ohne dass dauerhaft Platz dafür draufgeht.
    Q_PROPERTY(bool    tileDragActive  READ tileDragActive  NOTIFY tileDragActiveChanged)
    Q_PROPERTY(bool    dragLogging     READ dragLogging     CONSTANT)
    //  Ziehen auf ein Lesezeichen: verschieben (Standard) oder kopieren.
    Q_PROPERTY(bool    fileDropMove    READ fileDropMove    WRITE setFileDropMove NOTIFY fileDropMoveChanged)
    //  Versteckte Dateien mitzeigen. Wirkt beim naechsten Einlesen des Ordners -
    //  der Setter stoesst es deshalb selbst an.
    Q_PROPERTY(bool    showHiddenFiles READ showHiddenFiles WRITE setShowHiddenFiles NOTIFY showHiddenFilesChanged)
    //  Begleitdateien der App in Galerie und Dateiwähler mitzeigen.
    Q_PROPERTY(bool    showAllFiles    READ showAllFiles    WRITE setShowAllFiles NOTIFY showAllFilesChanged)
    //  Galerie als Liste (eine waagerechte Zeile je Eintrag) statt als
    //  Kachelraster. Vorgabe AUS. Im Player-Modus entscheidet weiter
    //  `Audio.listLayout` - s. `GalleryPane` ▸ `listMode`.
    Q_PROPERTY(bool    galleryListLayout READ galleryListLayout WRITE setGalleryListLayout NOTIFY galleryListLayoutChanged)
    //  Textdateien in der Galerie: Inhalt mit Syntaxfaerbung (AN) oder nur der
    //  Dateityp (AUS). Gilt fuer Kacheln UND Liste - beide zeigen dasselbe
    //  erzeugte Thumbnail.
    Q_PROPERTY(bool    textPreviewContent READ textPreviewContent WRITE setTextPreviewContent NOTIFY textPreviewContentChanged)
    //  Zeilenhöhe der Listen-Darstellung - das Gegenstück zu `tileHeight`.
    //  Eine Zeile ist immer so breit wie die Fläche; die Höhe ist die einzige
    //  Größe, die es zu wählen gibt.
    Q_PROPERTY(int     listRowHeight   READ listRowHeight   NOTIFY listRowHeightChanged)
    //  Breite des Bildschirms, auf dem das Fenster gerade steht. Gemeldet von
    //  der `ApplicationShell`, weil nur ein WINDOW das verlässlich weiß - das
    //  angehängte `Screen` eines Items in einem geschlossenen Popup liefert den
    //  PRIMÄREN Bildschirm und ändert sich nie (gemessen, s. `setScreenWidth`).
    Q_PROPERTY(int     screenWidth     READ screenWidth     NOTIFY screenWidthChanged)
    //  Reicht „Tag löschen" bis in die Unterordner? (Standard AN, s. ISettings)
    Q_PROPERTY(bool    deleteTagsInSubfolders READ deleteTagsInSubfolders WRITE setDeleteTagsInSubfolders NOTIFY deleteTagsInSubfoldersChanged)
    //  Vorgabe-Schriftfarbe des TXT->PDF-Exports; je Datei überschreibbar
    //  (MediaModel::fileTextPdfColor & Co. - die Ausnahme liegt im Sidecar des
    //  Ordners, dem die Datei gehört).
    Q_PROPERTY(QColor  textPdfColor    READ textPdfColor    WRITE setTextPdfColor NOTIFY textPdfColorChanged)
    Q_PROPERTY(int     videoSeekStep   READ videoSeekStep   NOTIFY videoSeekStepChanged)
    //  Rechtschreibprüfung: an/aus + Sprache. Die Kacheln lesen beides und
    //  reichen es an ihren Editor-Controller weiter (der kennt die globalen
    //  Einstellungen bewusst nicht).
    Q_PROPERTY(bool    spellCheck     READ spellCheck      NOTIFY spellCheckChanged)
    Q_PROPERTY(QString spellLanguage  READ spellLanguage   NOTIFY spellCheckChanged)
    Q_PROPERTY(bool    optionsVisible  READ optionsVisible  NOTIFY optionsVisibleChanged)

    // ── Editor / Auto-Save (Phase 4) ────────────────────────────────────────
    Q_PROPERTY(bool autoSaveEnabled  READ autoSaveEnabled  WRITE setAutoSaveEnabled  NOTIFY autoSaveChanged)
    Q_PROPERTY(int  autoSaveInterval READ autoSaveInterval WRITE setAutoSaveInterval NOTIFY autoSaveChanged)

    // ── Design / Theme (Phase 4) ────────────────────────────────────────────
    // designProfile == DesignProfile (0=Dark … 8=Custom). Live-Wechsel über themeChanged.
    Q_PROPERTY(int designProfile READ designProfile NOTIFY themeChanged)

    // ── Lesezeichen (gespeicherte Ordner) ───────────────────────────────────
    // FLACH, in Anzeigereihenfolge: { name, path, group, index } je Eintrag -
    // `index` ist der Platz in der gespeicherten Liste (Identität für
    // updateBookmark/removeBookmark/moveBookmark).
    Q_PROPERTY(QVariantList savedFolders READ savedFolders NOTIFY savedFoldersChanged)

    // ── Zwei-Fenster-Modus ──────────────────────────────────────────────────
    //  Die Hälften des Hauptfensters (1 oder 2) und die fokussierte davon.
    Q_PROPERTY(QVariantList panes READ panes NOTIFY panesChanged)
    //  DASSELBE als Modell - und NUR das gehoert an einen `Repeater`. Ueber die
    //  Liste oben baut er bei jeder Aenderung alle Delegates neu und zerstoert
    //  damit die andere Haelfte samt geoeffneter Datei (s. `PaneListModel`).
    Q_PROPERTY(QAbstractItemModel* panesModel READ panesModel CONSTANT)
    Q_PROPERTY(int paneCount READ paneCount NOTIFY panesChanged)
    Q_PROPERTY(int focusedPaneIndex READ focusedPaneIndex NOTIFY panesChanged)
    //  Teilungsverhältnis der beiden Hälften (0,15…0,85), bleibt erhalten.
    Q_PROPERTY(qreal paneSplit READ paneSplit WRITE setPaneSplit NOTIFY paneSplitChanged)
    //  Hälfte, auf die die ordnerbezogenen Einstellungen wirken (−1 = fokussierte).
    Q_PROPERTY(int settingsPaneIndex READ settingsPaneIndex NOTIFY panesChanged)
    // Der GANZE Baum als FLACHE Zeilenliste in Anzeigereihenfolge - eine Zeile
    // je Gruppe UND je Lesezeichen, in Tiefensuche (je Ebene erst die
    // Lesezeichen, dann die Untergruppen). Felder s. `bookmarkTree()`.
    // Flach, weil beide Verbraucher flach sind: das Menü ist eine Liste von
    // Einträgen, der Einstellungen-Reiter eine Liste von Zeilen. Ein
    // verschachteltes QVariantMap zwänge beide zu geschachtelten Repeatern,
    // über deren Grenzen hinweg sich nichts ziehen ließe.
    Q_PROPERTY(QVariantList bookmarkTree READ bookmarkTree NOTIFY savedFoldersChanged)

    // ── Galerie-View-State (Phase 2): Kachelgröße / Anordnung / Zoom ─────────
    // Persistiert über ISettings; GalleryView.qml bindet direkt an diese Props.
    Q_PROPERTY(int tileWidth        READ tileWidth        NOTIFY tileSizeChanged)
    Q_PROPERTY(int tileHeight       READ tileHeight       NOTIFY tileSizeChanged)
    Q_PROPERTY(int tileArrangement  READ tileArrangement  NOTIFY tileArrangementChanged)
    Q_PROPERTY(int manualAreaWidth  READ manualAreaWidth  NOTIFY tileArrangementChanged)
    // Dynamische Obergrenze der Kachelgröße = tatsächlich darstellbare
    // Galeriefläche (Fenster minus Leisten/Panel; das Fenster selbst ist durch
    // den Bildschirm begrenzt). Wird von der Shell über setTileSizeLimit
    // gemeldet; setTileSize/zoomIn klemmen dagegen. Dialog/Einstellungen
    // binden ihre SpinBox-/Drag-Grenzen an diese Properties.
    Q_PROPERTY(int maxTileWidth     READ maxTileWidth     NOTIFY tileSizeLimitChanged)
    Q_PROPERTY(int maxTileHeight    READ maxTileHeight    NOTIFY tileSizeLimitChanged)

    // ── Startgeometrie (einmalig gelesen, daher CONSTANT) ───────────────────
    Q_PROPERTY(int  initialWindowWidth  READ initialWindowWidth  CONSTANT)
    Q_PROPERTY(int  initialWindowHeight READ initialWindowHeight CONSTANT)
    Q_PROPERTY(int  initialWindowX      READ initialWindowX      CONSTANT)
    Q_PROPERTY(int  initialWindowY      READ initialWindowY      CONSTANT)
    Q_PROPERTY(bool startMaximized      READ startMaximized      CONSTANT)

    // ── Optionale Bibliotheken (Bauzeit-Entscheidung, daher CONSTANT) ───────
    //  QML graut die betroffenen Bedienelemente damit aus und zeigt beim
    //  Hovern, WELCHE Bibliothek fehlt. Das OCR-Gegenstück heißt
    //  PdfTextController::ocrAvailable (dort, weil je Kachel eine Instanz).
    Q_PROPERTY(bool docxAvailable       READ docxAvailable       CONSTANT)

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
    Q_PROPERTY(QColor themeEditorBgText READ themeEditorBgText NOTIFY themeChanged)
    Q_PROPERTY(QColor themeEditorBgHtml READ themeEditorBgHtml NOTIFY themeChanged)

public:
    //  Der ordnerbezogene Zustand gehört der HÄLFTE (`PaneController`), nicht
    //  mehr dieser Fassade: `setFocusedPane` sagt, welche Hälfte gerade gemeint
    //  ist. Alle Ordner-, Tag- und Datei-Wege unten reichen an sie weiter -
    //  bestehendes QML (`App.currentFolder` & Co.) bleibt damit gültig.
    explicit AppController(ISettings& settings, QObject* parent = nullptr);

    void setFocusedPane(PaneController* pane);
    PaneController* focusedPane() const { return m_pane; }

    // ── Die Hälften des Hauptfensters (Zwei-Fenster-Modus) ──────────────────
    //  `PaneController` braucht Einstellungen und den gemeinsamen Miniatur-Lader
    //  und lässt sich deshalb nicht aus QML heraus erzeugen. Die Fassade legt
    //  sie an, hält sie und gibt sie als Liste heraus.
    void setThumbnailLoader(ThumbnailLoader* loader) { m_loader = loader; }
    //  Die appweite `Tags`-Fassade (Einstellungen, Konverter) folgt der
    //  fokussierten Hälfte - innerhalb einer Hälfte gilt deren eigener
    //  `TagController` (s. PaneHost).
    void setTagsFacade(TagController* facade);
    //  Legt eine Hälfte an (bis `kMaxPanes`) und gibt sie zurück; ohne Platz
    //  oder ohne Lader `nullptr`.
    Q_INVOKABLE QObject* addPane();
    //  Schließt die Hälfte an dieser Stelle. Die letzte lässt sich nicht
    //  schließen - dann wäre gar keine Galerie mehr da.
    Q_INVOKABLE bool closePane(int index);
    //  Ordner beider Haelften in die Einstellungen schreiben (s. `.cpp`).
    void persistPaneFolders();
    //  Diese Hälfte ist jetzt gemeint (Fokus folgt dem Zeiger, s. GalleryPane).
    Q_INVOKABLE void focusPane(int index);
    //  Die beiden Hälften TAUSCHEN (Zug an der Leiste einer Hälfte). Ordner,
    //  Tags und offene Dateien wandern mit - es wechselt nur der Platz.
    Q_INVOKABLE bool swapPanes();
    //  Auf welche Hälfte sollen die ORDNERBEZOGENEN Einstellungen (Tags,
    //  Kategorien, Konverter) wirken? −1 = die fokussierte. Nur im
    //  Zwei-Fenster-Modus sinnvoll; der Dialog setzt es beim Öffnen zurück.
    Q_INVOKABLE void setSettingsPaneIndex(int index);
    int settingsPaneIndex() const { return m_settingsPane; }
    Q_INVOKABLE int  indexOfPane(QObject* pane) const;
    //  Der Ordner der zweiten Hälfte aus der letzten Sitzung ("" = es gab keine).
    Q_INVOKABLE QString secondFolder() const;
    QVariantList panes() const;
    QAbstractItemModel* panesModel() const;
    int paneCount() const { return int(m_panes.size()); }
    int focusedPaneIndex() const;

    // ── Ordner (Delegation an FolderService) ────────────────────────────────
    QString currentFolder() const;
    Q_INVOKABLE void openFolderUrl(const QUrl& url);
    Q_INVOKABLE void refreshCurrentFolder();

    // ── Hinein und zurueck (Unterordner) ────────────────────────────────────
    //  `openSubfolder` ist der EINZIGE Weg, der etwas auf den Rueckweg legt:
    //  nur ein Abstieg in einen Unterordner ist ein Schritt, den man zuruecknehmen
    //  will. Jeder andere Ordnerwechsel (Lesezeichen, Menue, Drop, letzter
    //  Ordner beim Start) verlaesst den Baum und LEERT den Stapel - sonst
    //  wuechse waehrend einer Sitzung eine lange Liste, durch die niemand
    //  zurueckgehen moechte (Festlegung des Nutzers).
    //  Ein Vorwaerts gibt es bewusst nicht: hinein fuehrt der Doppelklick.
    Q_INVOKABLE void openSubfolder(const QString& path);
    Q_INVOKABLE bool navigateBack();
    bool canNavigateBack() const;

    // Erstellt eine leere Datei im AKTUELLEN Ordner (FilterBar „Erstellen").
    // kind: "pdf" (eine leere A4-Seite via QPdfWriter) | "html" (Minimal-
    // Skelett, UTF-8) | "txt" (leer) | "docx" | "free". baseName ohne Endung;
    // Pfadtrenner werden entfernt, Namenskollisionen per „ (n)"-Suffix aufgelöst.
    //  **"free"**: der eingegebene Name gilt UNVERÄNDERT als Dateiname - die
    //  Endung wählt der Nutzer, oder es gibt gar keine (`LICENSE`, `NOTIZEN`);
    //  die Datei bleibt leer. Die Endung wird für die Kollisionsauflösung
    //  abgetrennt (`notiz (2).xyz`), Punkte und Leerzeichen am Ende fallen weg.
    //  Kennt `MediaItem::detectType` die Endung nicht und ist „Alle Dateien
    //  anzeigen" AUS, sagt die Statuszeile das ausdrücklich - die Einstellung
    //  wird NICHT hinter dem Rücken des Nutzers umgestellt. Schreibt atomar
    // (QSaveFile), meldet Erfolg/Fehler über statusMessage und stößt via
    // folderContentsChanged das Neuladen der Galerie an (Kachel erscheint
    // sofort, ohne auf den FileSystemWatcher zu warten). Liefert den vollen
    // Pfad der neuen Datei oder "" bei Fehler.
    //  `targetFolder` leer = der geoeffnete Ordner. Sonst muss er UNTERHALB
    //  davon liegen (Praefix-Pruefung) - die Kopfzeile eines aufgeklappten
    //  Bereichs legt so in IHREM Ordner an, ein Knopf kann aber nichts an
    //  beliebiger Stelle im Dateisystem erzeugen.
    Q_INVOKABLE QString createEmptyFile(const QString& kind, const QString& baseName,
                                        const QString& targetFolder = QString());
    Q_INVOKABLE void restoreLastFolder();

    // ── Drag & Drop von Ordnern/Dateien auf das Fenster ─────────────────────
    //  `targetFolder` leer = der geoeffnete Ordner. Sonst muss er UNTERHALB
    //  davon liegen (dieselbe Praefix-Pruefung wie `createEmptyFile`): ein
    //  Fremd-Drop darf ueber die Galerie nicht an beliebige Stellen schreiben.
    //  Faellt die Pruefung durch, wird still auf den offenen Ordner
    //  zurueckgefallen statt abzubrechen.
    Q_INVOKABLE void handleDroppedUrls(const QList<QUrl>& urls,
                                       const QString& targetFolder = QString());

    // ── Lesezeichen (Delegation an ISettings) ───────────────────────────────
    QVariantList savedFolders() const;
    QVariantList bookmarkTree() const;
    //  Der Zug einer Kachel meldet sich an und ab (die Kachel blockiert
    //  währenddessen in `Drag.active = true`, deshalb umklammert sie den Aufruf).
    //  Ein Zug meldet sich an und ab. Waehrend er laeuft, haengt hier auch der
    //  RAD-Filter (s. eventFilter): waehrend eines `QDrag` liefert Qt keine
    //  Radereignisse mehr an die QML-Elemente - die Zug-Maschinerie filtert sie
    //  vorher ab. Nur ein Filter auf der Anwendung kann sie noch sehen.
    Q_INVOKABLE void beginTileDrag();
    Q_INVOKABLE void endTileDrag();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

public:

    Q_INVOKABLE void openBookmark(const QString& path);
    // Phase 4: vollständige Lesezeichen-Verwaltung (für SettingsDialog/Bookmark-Tab)
    Q_INVOKABLE void addBookmark(const QString& name, const QString& path,
                                 const QString& group = QString());
    Q_INVOKABLE void updateBookmark(int index, const QString& name, const QString& path,
                                    const QString& group = QString());
    Q_INVOKABLE void removeBookmark(int index);

    // ── Lesezeichen-Gruppen: BELIEBIG TIEF verschachtelbar ──────────────────
    //  Eine Gruppe ist reine Anzeige-Ordnung: sie hält keinen Pfad, jedes
    //  Lesezeichen nennt SEINE Gruppe. Gruppen können Gruppen enthalten,
    //  Lesezeichen sind immer Blätter.
    //  IDENTITÄT ist der volle Pfad mit "/" als Trenner ("Persönlich/Lernen");
    //  ein Gruppenname darf deshalb weder "/" noch einen Tabulator enthalten
    //  (`isUsableGroupName`). Vergleiche laufen ohne Rücksicht auf Groß-/
    //  Kleinschreibung, "" heißt „ohne Gruppe" (oberste Ebene).
    //  Eine alte Konfiguration mit EINER Ebene ist unverändert gültig - ihre
    //  Namen sind bereits Pfade ohne Trenner.
    //  `parentPath` leer = oberste Ebene. Fehlt die Elterngruppe, wird sie
    //  samt ihrer Vorfahren angelegt statt den Aufruf zu verwerfen.
    Q_INVOKABLE void addBookmarkGroup(const QString& name,
                                      const QString& parentPath = QString());
    //  Benennt NUR das letzte Glied um; alle Untergruppen und Mitglieder
    //  ziehen mit (ihre Pfade tragen den alten Namen als Vorsilbe).
    Q_INVOKABLE void renameBookmarkGroup(const QString& path, const QString& newName);
    //  Löscht die Gruppe UND alle Untergruppen; deren Lesezeichen bleiben und
    //  rücken nach „ohne Gruppe" - eine Gruppe zu schließen kostet nie Pfade.
    Q_INVOKABLE void removeBookmarkGroup(const QString& path);
    Q_INVOKABLE void setBookmarkGroupCollapsed(const QString& path, bool collapsed);
    //  Hängt eine Gruppe unter eine andere (oder mit leerem `newParentPath` auf
    //  die oberste Ebene) und setzt sie dort an Position `pos` unter ihre
    //  Geschwister; `pos` < 0 = ans Ende. Eine Gruppe in sich selbst oder in
    //  eine ihrer Untergruppen zu hängen wird abgewiesen (das ergäbe einen Ring).
    Q_INVOKABLE void moveBookmarkGroup(const QString& path, const QString& newParentPath,
                                       int pos);
    //  Einen Eintrag in eine Gruppe geben und dort einsortieren. `pos` < 0 oder
    //  über der Mitgliederzahl = ans Ende der Gruppe.
    Q_INVOKABLE void moveBookmark(int index, const QString& targetGroup, int pos);
    //  Ist der Name als Glied eines Gruppenpfades brauchbar? (nicht leer, kein
    //  "/" und kein Tabulator). Die Oberfläche fragt damit, BEVOR sie anlegt.
    Q_INVOKABLE bool isUsableGroupName(const QString& name) const;

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

    // ── Schrift mit arabischem (Naskh-)/CJK-Fallback ────────────────────────
    //  QML kennt in Qt 6.4 kein `font.families`; daher liefern wir einen QFont
    //  mit Familien-Substitutionsliste aus C++ (führende Familie für Latein,
    //  Naskh/CJK je Glyphe als Rückfall). Für Editoren mit arabischem Text.
    Q_INVOKABLE QFont fallbackFont(const QString& family, qreal pixelSize,
                                   bool bold = false, bool italic = false,
                                   bool underline = false) const;

    // ── Galerie-View-State (Delegation an ISettings) ────────────────────────
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
    // Von der Shell bei jeder Größenänderung der Galeriefläche gemeldet.
    // Klemmt NUR künftige setTileSize-Aufrufe - eine bereits gespeicherte
    // größere Kachelgröße wird nicht zerstörerisch mitverkleinert (ein nur
    // vorübergehend kleines Fenster soll die Einstellung nicht überschreiben).
    Q_INVOKABLE void setTileSizeLimit(int w, int h);

    // ── Einstellungen (Delegation an ISettings) ─────────────────────────────
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
    //  Wörterbücher auf diesem Rechner (Einstellungsdialog).
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

    // ── Mono-Play: Wiedergabe-Koordination ──────────────────────────────────
    // Jede Wiedergabestelle (VideoSurface-Player, PDF-Audio-Fassade je Kachel)
    // meldet ihren Play-START mit einem eindeutigen Token; alle anderen Stellen
    // pausieren sich auf playbackStarted(fremdes Token) - Position bleibt
    // erhalten (Pause, kein Stop). Sendet NUR bei aktivem Mono-Play; ist die
    // Option aus, laufen Wiedergaben unkoordiniert (parallel) weiter.
    Q_INVOKABLE void announcePlayback(const QString& token);

    // Versucht das RHI-Backend zu wechseln. Gibt true zurück, wenn der Probe
    // erfolgreich war (Cache aktualisiert, Neustart nötig). Bei Fehler bleibt
    // der alte Wert erhalten.
    Q_INVOKABLE bool trySetRhiBackend(const QString& backend);

    Q_INVOKABLE void toggleOptions();

    // ── Fensterzustand (Delegation an ISettings) ────────────────────────────
    int  initialWindowWidth()  const;
    int  initialWindowHeight() const;
    int  initialWindowX()      const;
    int  initialWindowY()      const;
    bool startMaximized()      const;
    bool docxAvailable()       const;
    Q_INVOKABLE void saveWindowState(int w, int h, int x, int y, bool maximized);

    // ── Tags (Delegation an TagManager) ─────────────────────────────────────
    Q_INVOKABLE QStringList allTags() const;
    Q_INVOKABLE QColor      tagColor(const QString& tag) const;
    //  ENTFALLEN - alles, was EINE DATEI betrifft, liegt jetzt im Modell:
    //  `tagsForFile`/`addTagToFile`/`removeTagFromFile`, `setCustomDate`/
    //  `clearCustomDate` und `fileTextPdfColor` & Co. nahmen den blanken
    //  DATEINAMEN und trafen damit immer das Sidecar des GEOEFFNETEN Ordners.
    //  Fuer eine Datei aus einem aufgeklappten Unterordner war das das falsche.
    //  Der Ersatz nimmt den PFAD und routet auf den richtigen Ordner:
    //  `MediaModel::tagsOfFile`/`addTag`/`removeTag`/`setCustomDate`/
    //  `clearCustomDate`/`fileTextPdfColor` & Co. (s. `## Media`).

    // ── i18n (Delegation an Strings) ────────────────────────────────────────
    // key == Ganzzahlwert von StringKey (siehe Strings.h). Sugar-Enum-Export
    // für QML folgt in einer späteren Phase; hier nur das Fundament.
    Q_INVOKABLE QString text(int key) const;
    Q_INVOKABLE QString text(int key, const QString& arg1) const;
    // Reaktiver, namensbasierter Zugriff für QML: App.uiText(App.language, "KeyName").
    // Der lang-Parameter (= App.language) macht das Binding reaktiv bei Sprachwechsel.
    Q_INVOKABLE QString uiText(const QString& lang, const QString& key) const;
    // Lokalen Dateipfad in eine korrekt prozent-kodierte file://-URL umwandeln
    // (für Image.source u.ä.; behandelt Sonderzeichen, Leerzeichen, CJK korrekt).
    Q_INVOKABLE QString fileUrl(const QString& path) const;
    // Umkehrung dazu: file://-URL -> lokaler Pfad (für abgelegte Dateien, z. B.
    // eine Kachel, die auf einen Tag gezogen wurde). Ein bereits lokaler Pfad
    // kommt unverändert zurück.
    Q_INVOKABLE QString localPath(const QString& urlOrPath) const;

    //  ── Dateien in die Zwischenablage (Strg+C in der Galerie) ───────────────
    //  Abgelegt wird das, was ein Dateimanager erwartet: `text/uri-list` (die
    //  Dateien selbst), `text/plain` (die Pfade, fuer Terminal und Textfeld)
    //  und `x-special/gnome-copied-files` (Nautilus/Nemo/Caja verlangen es zum
    //  Einfuegen; Dolphin liest `text/uri-list`). Unter Windows/macOS bildet Qt
    //  die URL-Liste von selbst auf die Dateiliste des Systems ab.
    //  Rueckgabe: Anzahl der wirklich abgelegten Dateien.
    Q_INVOKABLE int copyFilesToClipboard(const QStringList& paths) const;
    //  Gegenstueck: die Dateien AUS der Zwischenablage (`Strg+V` in der
    //  Galerie). Gelesen wird `text/uri-list` - dasselbe Format, das ein
    //  Dateimanager beim Kopieren ablegt und das auch unser Ziehen benutzt.
    //  Zurueck kommen nur Adressen, die es wirklich gibt; Ordner bleiben
    //  aussen vor (sie werden nicht kopiert, s. `handleDroppedUrls`).
    Q_INVOKABLE QList<QUrl> clipboardFileUrls() const;

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
    bool tileDragActive() const { return m_tileDragActive; }
    //  Nur fuer die Fehlersuche: schaltet die QML-Seite ihre Protokollzeilen an.
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


    // ── Zeilenhöhe der Listen-Darstellung ───────────────────────────────────
    //  `zoomInList`/`zoomOutList` sind das Gegenstück zu `zoomIn`/`zoomOut`.
    //  WELCHE der beiden Paare gilt, entscheidet die OBERFLÄCHE, nicht der
    //  Controller: die Darstellung hängt an der HÄLFTE (im Player-Modus an
    //  `Audio.listLayout`, sonst an `galleryListLayout`), und zwei Hälften
    //  können verschieden stehen. Der Controller kennt die Hälfte nicht.
    int  listRowHeight() const;
    Q_INVOKABLE void setListRowHeight(int px);
    Q_INVOKABLE void zoomInList(int stepPx = 4);
    Q_INVOKABLE void zoomOutList(int stepPx = 4);

    // ── Bildschirm, auf dem das Fenster steht ───────────────────────────────
    //  Die `ApplicationShell` meldet es (`Window.screen` folgt dem Umziehen auf
    //  einen anderen Monitor); die Einstellungen binden ihre Obergrenze daran.
    //  Gemessen am 2026-09-02 auf zwei Monitoren (1536 primär / 1920): das an
    //  ein Item angehängte `Screen` meldet in einem GESCHLOSSENEN Popup immer
    //  1536 - den primären, nicht den, auf dem das Fenster steht. Genau deshalb
    //  kommt der Wert hier aus dem Fenster und nicht aus dem Reiter.
    int  screenWidth() const { return m_screenW; }
    Q_INVOKABLE void setScreenWidth(int w);
    // ── Klappzustand der Gruppen im Einstellungen-Fenster ───────────────────
    //  `key` ist ein STABILER Schlüssel je Gruppe (z. B. "view.tiles") - nicht
    //  ihre Überschrift, die ist übersetzt. Unbekannt = offen.
    //  Bewusst OHNE NOTIFY: eine Gruppe liest ihren Zustand einmal beim
    //  Entstehen und schreibt ihn beim Umschalten; es gibt keinen zweiten
    //  Leser, den ein Signal erreichen müsste.
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

    //  Der Abschnitt, in dem ein Gruppenpfad landet: der Pfad selbst in der
    //  GESPEICHERTEN Schreibweise, wenn es die Gruppe gibt - sonst "" (ohne
    //  Gruppe). Ein von Hand verstellter Pfad lässt damit nie ein Lesezeichen
    //  verschwinden, es rückt nach oben.
    QString bookmarkSection(const QString& group) const;
    //  Legt `fullPath` samt aller fehlenden Vorfahren an. Gibt den Pfad in der
    //  gespeicherten Schreibweise zurück (leer, wenn er unbrauchbar war).
    QString ensureBookmarkGroup(const QString& fullPath);


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
    //  Was WIR zuletzt kopiert haben. Grund: die Zwischenablage des Systems
    //  liefert eine lange Adressliste nicht vollstaendig zurueck (gemessen auf
    //  KDE/Wayland: 29 Adressen abgelegt, 3 wiedergelesen - s. LIMITATIONS.md).
    //  Solange WIR der Eigentuemer sind, ist diese Liste die Wahrheit; sobald
    //  ein anderes Programm kopiert, gilt wieder die Ablage des Systems.
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
