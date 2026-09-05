#pragma once
#include <QString>
#include <QSize>
#include <QPoint>
#include <QColor>

enum class Language      { German, English };
enum class VideoPlayback { Native, External };
enum class PageTransition { Slide, Fade };   // Vollbild-Öffnen/Schließen-Animation

enum class ExtractSelectStyle { Frame, Overlay };

enum class ExtractLayout { Workbench, Compact };

enum class TileArrangement {
    Centered,       // tiles centred in viewport (current default)
    Left,           // flush left
    Right,          // flush right
    Manual          // user-defined fixed-width area
};

// ACHTUNG: der Wert wird als INT gespeichert. Fällt ein Eintrag heraus, rutschen alle dahinter um eins - wer
// "Simple" eingestellt hatte, sähe danach "Custom". Deshalb die SCHEMA-Nummer, die einen alten Wert umrechnet.
enum class DesignProfile {
    Dark, DarkOLED, OceanDepth, InfernoBlaze,
    MidnightRose, Elegant, Simple, Custom
};

enum class AccentType  { Solid, Gradient, Glow };
enum class TileBgType  { Solid, Gradient, Transparent };

#include <QJsonObject>

struct ThemeColors {
    QColor background    = QColor(10, 18, 22);
    QColor card          = QColor(18, 28, 34);
    QColor textPrimary   = QColor(220, 235, 230);
    QColor textMuted     = QColor(120, 150, 145);
    QColor border        = QColor(40, 60, 70);

    AccentType accentType  = AccentType::Solid;
    QColor accent          = QColor(0, 180, 160);
    QColor accentGradEnd   = QColor(0, 120, 200);
    float  glowRadius      = 8.0f;
    float  glowIntensity   = 0.6f;

    bool   bgIsGradient    = false;
    QColor bgGradStart     = QColor(10, 18, 22);
    QColor bgGradEnd       = QColor(10, 18, 22);
    int    bgGradAngle     = 180;

    TileBgType tileBgType  = TileBgType::Solid;
    QColor tileBgColor     = QColor(18, 28, 34);
    QColor tileBgGradEnd   = QColor(18, 28, 34);
    int    tileBgGradAngle = 180;

    bool   tileGlowOnHover = false;
    float  tileGlowRadius  = 6.0f;

    QColor pdfViewerBg    = QColor(13, 21, 24);   // background behind PDF pages / media
    QColor pdfThumbBg     = QColor(255, 255, 255); // background canvas in PDF tile thumbnails
    QColor pdfSidebarBg   = QColor(10, 18, 22);
    QColor pdfToolbarBg   = QColor(18, 28, 34);
    QColor pdfScrollbarBg = QColor(18, 28, 34);

    QColor buttonBg       = QColor(0, 0, 0, 0);  // transparent = derive from accent
    QColor sidebarBg      = QColor(18, 28, 34);
    QColor editorBgText   = QColor(18, 28, 34);
    QColor editorBgHtml   = QColor(18, 28, 34);

    QColor menuBarBg      = QColor(12, 20, 26);
    QColor toolbarBg      = QColor(12, 20, 26);
    QColor filterBarBg    = QColor(12, 20, 26);
    QColor statusBarBg    = QColor(8, 14, 18);

    QString name = "Dark";

    QJsonObject toJson() const;
    static ThemeColors fromJson(const QJsonObject& obj);
};

class ISettings {
public:
    virtual ~ISettings() = default;

    virtual QSize  windowSize() const = 0;
    virtual void   setWindowSize(const QSize& s) = 0;
    virtual QPoint windowPos() const = 0;
    virtual void   setWindowPos(const QPoint& p) = 0;
    virtual bool   windowMaximized() const = 0;
    virtual void   setWindowMaximized(bool m) = 0;

    virtual QString lastFolder() const = 0;
    virtual void    setLastFolder(const QString& path) = 0;

    virtual Language language() const = 0;
    virtual void     setLanguage(Language l) = 0;

    virtual VideoPlayback videoPlayback() const = 0;
    virtual void          setVideoPlayback(VideoPlayback v) = 0;

    virtual PageTransition pageTransition() const = 0;
    virtual void           setPageTransition(PageTransition t) = 0;

    virtual ExtractSelectStyle extractSelectStyle() const = 0;
    virtual void               setExtractSelectStyle(ExtractSelectStyle s) = 0;
    virtual ExtractLayout      extractLayout() const = 0;
    virtual void               setExtractLayout(ExtractLayout l) = 0;
    virtual bool           audioAccentApple() const = 0;
    virtual void           setAudioAccentApple(bool v) = 0;

    // Mono-Play: nur EINE Wiedergabe gleichzeitig - eine neue pausiert die laufende (kein Stop). Das Farbprofil des
    // Editors ist bewusst von `designProfile` getrennt: er hat eigene Profile und einen eigenen Konfigurator.
    virtual int  editorProfile() const = 0;          // mg::editor::EditorProfile
    virtual void setEditorProfile(int p) = 0;
    virtual QString editorCustomPalette() const = 0; // JSON, leer = Vorgabe
    virtual void    setEditorCustomPalette(const QString& json) = 0;
    virtual bool editorLineNumbers() const = 0;
    virtual void setEditorLineNumbers(bool v) = 0;
    virtual bool editorHighlightCurrentLine() const = 0;
    virtual void setEditorHighlightCurrentLine(bool v) = 0;
    virtual bool editorSoftWrap() const = 0;
    virtual void setEditorSoftWrap(bool v) = 0;

    // Zwei getrennte Fragen: die Tabulator-BREITE ist Anzeige, die TASTE ändert den Dateiinhalt. Kate schreibt per
    // Vorgabe Leerzeichen - deshalb auch hier. Die Übersichtsspalte ist per Vorgabe aus, sie kostet Breite.
    virtual bool editorMinimap() const = 0;
    virtual void setEditorMinimap(bool v) = 0;
    virtual bool editorFolding() const = 0;
    virtual void setEditorFolding(bool v) = 0;
    virtual bool editorIndentGuides() const = 0;
    virtual void setEditorIndentGuides(bool v) = 0;
    virtual bool editorMatchBrackets() const = 0;
    virtual void setEditorMatchBrackets(bool v) = 0;
    virtual int  editorTabWidth() const = 0;
    virtual void setEditorTabWidth(int zeichen) = 0;
    virtual bool editorTabSpaces() const = 0;
    virtual void setEditorTabSpaces(bool v) = 0;

    virtual bool textPreviewContent() const = 0;
    virtual void setTextPreviewContent(bool v) = 0;

    virtual bool monoPlay() const = 0;
    virtual void setMonoPlay(bool v) = 0;
    // Versteckte Dateien per Vorgabe AUS: in einem Medienordner sind das fast immer Beiwerk (`.DS_Store`). Wer in
    // einem Projektordner arbeitet, will `.gitignore` aber sehen - deshalb schaltbar statt fest zu.
    virtual bool showHiddenFiles() const = 0;
    virtual void setShowHiddenFiles(bool v) = 0;
    virtual bool fileDropMove() const = 0;
    virtual void setFileDropMove(bool v) = 0;

    virtual bool showAllFiles() const = 0;
    virtual void setShowAllFiles(bool v) = 0;

    virtual bool galleryListLayout() const = 0;
    virtual void setGalleryListLayout(bool v) = 0;

    virtual QStringList collapsedSettingsGroups() const = 0;
    virtual void        setCollapsedSettingsGroups(const QStringList& keys) = 0;

    virtual int  galleryListRowHeight() const = 0;
    virtual void setGalleryListRowHeight(int px) = 0;

    // Jeder Ordner führt seine Verschlagwortung in einer eigenen Sidecar-Datei; ohne diesen Schalter blieb ein
    // gelöschter Tag dort stehen. Standard AN - wer einen Tag loswerden will, meint ihn meist im ganzen Baum.
    virtual bool deleteTagsInSubfolders() const = 0;
    virtual void setDeleteTagsInSubfolders(bool v) = 0;

    virtual QColor textPdfColor() const = 0;
    virtual void   setTextPdfColor(const QColor& c) = 0;

    // Spulschritt (Sekunden) der Pfeiltasten im Video-Vollbild. Standard: 15.
    //  Rechtschreibprüfung: an/aus und Sprache (Wörterbuch-Kürzel wie „de_DE").
    //  Leere Sprache = automatisch die erste gefundene.
    virtual bool spellCheckEnabled() const = 0;
    virtual void setSpellCheckEnabled(bool v) = 0;
    virtual QString spellLanguage() const = 0;
    virtual void    setSpellLanguage(const QString& lang) = 0;

    virtual int  videoSeekStep() const = 0;
    virtual void setVideoSeekStep(int seconds) = 0;

    virtual QColor backgroundColor() const = 0;
    virtual void   setBackgroundColor(const QColor& c) = 0;
    virtual QColor accentColor() const = 0;
    virtual void   setAccentColor(const QColor& c) = 0;

    virtual int  tileWidth()  const = 0;
    virtual int  tileHeight() const = 0;

    virtual TileArrangement tileArrangement() const = 0;
    virtual void            setTileArrangement(TileArrangement a) = 0;
    virtual int             manualAreaWidth()  const = 0;   // px; only used in Manual mode
    virtual void            setManualAreaWidth(int w) = 0;


    virtual bool optionsVisible() const = 0;
    virtual void setOptionsVisible(bool v) = 0;

    virtual bool pdfEditPanelTop() const = 0;
    virtual void setPdfEditPanelTop(bool v) = 0;

    // true (Standard) = verlustfrei bevorzugen: "Text ersetzen" geht direkt in die eingebettete Textebene, die
    // Seite bleibt vektoriell; wo das nicht sicher geht, fällt der Controller auf den Raster-Export zurück.
    virtual bool pdfExportLossless() const = 0;
    virtual void setPdfExportLossless(bool v) = 0;

    // true = Notizen als echte PDF-Annotationen (anderswo auswähl- und löschbar), false (Standard) = gemalter Inhalt,
    // der überall gleich aussieht. Nicht abbildbare Notizen werden IMMER gemalt - dann gilt der gemalte Weg für alles.
    virtual bool pdfExportAsAnnotations() const = 0;
    virtual void setPdfExportAsAnnotations(bool v) = 0;

    virtual bool docxSaveDirect() const = 0;
    virtual void setDocxSaveDirect(bool v) = 0;
    virtual int  docxPdfPageNumberPos() const = 0;
    virtual void setDocxPdfPageNumberPos(int pos) = 0;
    virtual int  docxPdfPageNumberStyle() const = 0;
    virtual void setDocxPdfPageNumberStyle(int style) = 0;

    virtual bool autoSaveEnabled() const = 0;
    virtual void setAutoSaveEnabled(bool v) = 0;
    virtual int  autoSaveIntervalSeconds() const = 0;
    virtual void setAutoSaveIntervalSeconds(int s) = 0;

    virtual DesignProfile designProfile() const = 0;
    virtual void          setDesignProfile(DesignProfile p) = 0;
    virtual ThemeColors   currentTheme() const = 0;
    virtual ThemeColors   customTheme() const = 0;
    virtual void          setCustomTheme(const ThemeColors& t) = 0;

    virtual bool exportCustomTheme(const QString& filePath) const = 0;
    virtual bool importCustomTheme(const QString& filePath) = 0;

    virtual QStringList savedFolders() const = 0;
    virtual void        setSavedFolders(const QStringList& paths) = 0;

    virtual QStringList bookmarkGroups() const = 0;
    virtual void        setBookmarkGroups(const QStringList& groups) = 0;

    virtual qreal   paneSplit() const = 0;
    virtual void    setPaneSplit(qreal v) = 0;
    virtual QString secondFolder() const = 0;
    virtual void    setSecondFolder(const QString& path) = 0;

    virtual bool        audioEqEnabled() const = 0;
    virtual void        setAudioEqEnabled(bool on) = 0;
    virtual QList<double> audioEqBands() const = 0;
    virtual void        setAudioEqBands(const QList<double>& db) = 0;
    virtual double      audioEqPreamp() const = 0;
    virtual void        setAudioEqPreamp(double db) = 0;
    virtual QStringList audioEqPresets() const = 0;
    virtual void        setAudioEqPresets(const QStringList& presets) = 0;
    // Mitgelieferte Voreinstellungen sind veraenderbar: was der Nutzer aendert, liegt
    // DANEBEN, so bleibt ein Rueckweg. audioEqAutoPreamp AUS heisst, es wird nichts
    // gerechnet - der Regler gehoert ganz dem Nutzer, auch beim Uebersteuern.
    virtual bool        audioEqAutoPreamp() const = 0;
    virtual void        setAudioEqAutoPreamp(bool on) = 0;
    virtual QStringList audioEqHiddenPresets() const = 0;
    virtual void        setAudioEqHiddenPresets(const QStringList& names) = 0;
    virtual QStringList audioEqPresetOrder() const = 0;
    virtual void        setAudioEqPresetOrder(const QStringList& names) = 0;
    virtual bool        audioPlayerMode() const = 0;
    virtual void        setAudioPlayerMode(bool on) = 0;
    // Bitmaske: Bit i = Hälfte i stand im Player-Modus. Eine einzelne Platznummer reichte nicht - bei geteiltem
    // Fenster hat jede Hälfte einen eigenen Modus, und ohne den Platz bekam ihn die zuerst gebaute Hälfte.
    virtual int         audioPlayerModeMask() const = 0;
    virtual void        setAudioPlayerModeMask(int mask) = 0;
    virtual bool        audioListLayout() const = 0;
    virtual void        setAudioListLayout(bool on) = 0;
    virtual bool        audioShowVideos() const = 0;
    virtual void        setAudioShowVideos(bool on) = 0;
    virtual bool        audioRememberLast() const = 0;
    virtual void        setAudioRememberLast(bool on) = 0;
    virtual bool        audioExtractInheritTags() const = 0;
    virtual void        setAudioExtractInheritTags(bool on) = 0;
    virtual bool        audioExtractToQueue() const = 0;
    virtual void        setAudioExtractToQueue(bool on) = 0;
    virtual QString     audioLastFile() const = 0;
    virtual void        setAudioLastFile(const QString& path) = 0;
    virtual qint64      audioLastPosition() const = 0;
    virtual void        setAudioLastPosition(qint64 ms) = 0;
    virtual double      audioVolume() const = 0;
    virtual void        setAudioVolume(double v) = 0;
    virtual bool        audioShuffle() const = 0;
    virtual void        setAudioShuffle(bool on) = 0;
    virtual int         audioRepeat() const = 0;
    virtual void        setAudioRepeat(int mode) = 0;

    virtual QString rhiBackend() const = 0;   // liest "rhi/detectedBackend" aus QSettings

    virtual void sync() = 0;
};
