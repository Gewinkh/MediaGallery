#pragma once
#include <QString>
#include <QSize>
#include <QPoint>
#include <QColor>

enum class Language      { German, English };
enum class VideoPlayback { Native, External };
enum class PageTransition { Slide, Fade };   // Vollbild-Öffnen/Schließen-Animation

// Darstellung der Seitenauswahl in den PDF-Extraktionsdialogen:
//  Frame   = Akzent-Rahmen um die Kachel (Standard)
//  Overlay = halbtransparente Akzent-Überlagerung mit Häkchen
enum class ExtractSelectStyle { Frame, Overlay };

// Layout des PDF-Seitenauswahl-Dialogs:
//  Workbench = Drei-Panel-Werkbank (PDF-Liste + Seitengrid + Auswahlleiste mit
//              Drag&Drop-Reihenfolge) - Standard
//  Compact   = schlichtes Einzelraster + Namensdialog (minimalistisch)
enum class ExtractLayout { Workbench, Compact };

// ─── Tile arrangement / alignment mode ───────────────────────────────────────
enum class TileArrangement {
    Centered,       // tiles centred in viewport (current default)
    Left,           // flush left
    Right,          // flush right
    Manual          // user-defined fixed-width area
};

//  ACHTUNG bei Aenderungen: der Wert wird als INT gespeichert
//  (`design/profile`). Faellt ein Eintrag heraus, rutschen alle dahinter um
//  eins - wer „Simple" eingestellt hatte, saehe danach „Custom". Deshalb traegt
//  die Einstellung eine SCHEMA-Nummer (`design/profileSchema`), und
//  `AppSettings::designProfile` rechnet einen alten Wert einmalig um.
//  Zuletzt entfallen: `NeonPurple` (Schema 1 -> 2, 2026-09-02, Wunsch des
//  Nutzers - acht Profile fuellen zwei Reihen zu je vier vollstaendig aus).
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

    // PDF viewer colors
    QColor pdfViewerBg    = QColor(13, 21, 24);   // background behind PDF pages / media
    QColor pdfThumbBg     = QColor(255, 255, 255); // background canvas in PDF tile thumbnails
    QColor pdfSidebarBg   = QColor(10, 18, 22);
    QColor pdfToolbarBg   = QColor(18, 28, 34);
    QColor pdfScrollbarBg = QColor(18, 28, 34);

    // Extra UI overrides
    QColor buttonBg       = QColor(0, 0, 0, 0);  // transparent = derive from accent
    QColor sidebarBg      = QColor(18, 28, 34);
    // Hintergrund des Text-Editors - GETRENNT für TXT- und HTML-Quellansicht
    // (TextSurface wählt je Dateiendung). Standard folgt der Karten-/Flächen-
    // farbe (card); beide frei wählbar im Design-Tab (Custom).
    QColor editorBgText   = QColor(18, 28, 34);
    QColor editorBgHtml   = QColor(18, 28, 34);

    // Main-window chrome colors
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

    // Vollbild-Übergangsstil (Slide/Fade) + Audio-Player-Akzent (Theme/Apple-Blau)
    virtual PageTransition pageTransition() const = 0;
    virtual void           setPageTransition(PageTransition t) = 0;

    virtual ExtractSelectStyle extractSelectStyle() const = 0;
    virtual void               setExtractSelectStyle(ExtractSelectStyle s) = 0;
    virtual ExtractLayout      extractLayout() const = 0;
    virtual void               setExtractLayout(ExtractLayout l) = 0;
    virtual bool           audioAccentApple() const = 0;
    virtual void           setAudioAccentApple(bool v) = 0;

    // Mono-Play: nur EINE Audio-/Video-Wiedergabe gleichzeitig (relevant in der
    // geteilten Ansicht) - startet eine neue Wiedergabe, wird die bereits
    // laufende automatisch pausiert (nicht gestoppt). Standard: AN.
    //  ── Texteditor (Syntaxfaerbung + Verhalten) ─────────────────────────
    //  Das Farbprofil des Editors ist BEWUSST von `designProfile` getrennt:
    //  der Editor hat eigene Profile und einen eigenen Konfigurator
    //  (Festlegung des Nutzers 2026-09-02). Ein Themenwechsel der Oberflaeche
    //  faerbt den Editor deshalb nicht um.
    virtual int  editorProfile() const = 0;          // mg::editor::EditorProfile
    virtual void setEditorProfile(int p) = 0;
    virtual QString editorCustomPalette() const = 0; // JSON, leer = Vorgabe
    virtual void    setEditorCustomPalette(const QString& json) = 0;
    virtual bool editorLineNumbers() const = 0;
    virtual void setEditorLineNumbers(bool v) = 0;
    virtual bool editorHighlightCurrentLine() const = 0;
    virtual void setEditorHighlightCurrentLine(bool v) = 0;
    //  Sichtbarer („weicher") Umbruch wie in Kate: die Datei bekommt davon
    //  nichts mit, nur die Anzeige bricht um. Aus = die Zeile laeuft waagerecht
    //  weiter wie in VS Code.
    virtual bool editorSoftWrap() const = 0;
    virtual void setEditorSoftWrap(bool v) = 0;

    //  Tabulator: wie BREIT er dargestellt wird (2…8 Zeichen) und ob die
    //  Tabulatortaste ein `\t` schreibt oder LEERZEICHEN bis zum naechsten
    //  Halt. Zwei getrennte Fragen: die Breite ist Anzeige, die Taste aendert
    //  den Dateiinhalt. Kate und die meisten Editoren schreiben per Vorgabe
    //  Leerzeichen - deshalb ist das auch hier die Vorgabe.
    //  Uebersichtsspalte rechts (Kates „Minimap"). Vorgabe AUS - sie kostet
    //  Breite, und wer sie will, schaltet sie ein.
    virtual bool editorMinimap() const = 0;
    virtual void setEditorMinimap(bool v) = 0;
    //  Faltungsleiste rechts von den Zeilennummern. Vorgabe AN - sie erscheint
    //  ohnehin nur, wenn die Datei faltbare Bloecke hat.
    virtual bool editorFolding() const = 0;
    virtual void setEditorFolding(bool v) = 0;
    //  Senkrechte Fuehrungslinien je Einrueckungsstufe.
    virtual bool editorIndentGuides() const = 0;
    virtual void setEditorIndentGuides(bool v) = 0;
    //  Klammernpaar am Cursor hervorheben; fehlt die Partnerin, wird sie rot.
    virtual bool editorMatchBrackets() const = 0;
    virtual void setEditorMatchBrackets(bool v) = 0;
    virtual int  editorTabWidth() const = 0;
    virtual void setEditorTabWidth(int zeichen) = 0;
    virtual bool editorTabSpaces() const = 0;
    virtual void setEditorTabSpaces(bool v) = 0;

    //  Vorschaukarten von TEXTdateien in der Galerie: AN = die ersten Zeilen mit
    //  Syntaxfaerbung (dieselben Farben wie im Editor), AUS = nur der Dateityp
    //  in grossen Buchstaben (CPP, PY, MD …). Betrifft Kacheln UND Liste - beide
    //  zeigen dasselbe erzeugte Thumbnail.
    virtual bool textPreviewContent() const = 0;
    virtual void setTextPreviewContent(bool v) = 0;

    virtual bool monoPlay() const = 0;
    virtual void setMonoPlay(bool v) = 0;
    //  Was passiert, wenn eine Kachel auf ein LESEZEICHEN gezogen wird:
    //  true = verschieben (Standard), false = kopieren.
    //  Versteckte Dateien (Punkt am Anfang) mit einlesen? Vorgabe AUS: in einem
    //  Medienordner sind das fast immer Beiwerk (`.DS_Store`, `.thumbnails`).
    //  Wer in einem Projektordner arbeitet, will `.gitignore` aber sehen
    //  (vom Nutzer gemeldet) - deshalb schaltbar statt fest zu.
    virtual bool showHiddenFiles() const = 0;
    virtual void setShowHiddenFiles(bool v) = 0;
    virtual bool fileDropMove() const = 0;
    virtual void setFileDropMove(bool v) = 0;

    //  Zeigt die Galerie (und der eigene Dateiwähler) ALLE Dateien - auch die
    //  Begleitdateien der App: die Ordner-JSON mit Tags/Kategorien, die
    //  Editor-Sidecars `<datei>.mgedit.json` und Sicherungskopien `.bak`?
    //  Standard AUS: sie gehören zur Verwaltung, nicht zur Sammlung.
    virtual bool showAllFiles() const = 0;
    virtual void setShowAllFiles(bool v) = 0;

    //  Zeigt die Galerie ihre Einträge als KACHELRASTER (Vorgabe) oder als
    //  Liste - eine waagerechte Zeile je Ordner und je Datei? Es ist derselbe
    //  Aufbau, den der Player-Modus schon kennt (`audioListLayout`), hier aber
    //  OHNE dessen Filter: im Normalmodus bleibt jede Datei sichtbar.
    //  Beide Schalter bleiben getrennt - wer den Player als Liste will, will
    //  damit noch nicht seine Galerie als Liste.
    virtual bool galleryListLayout() const = 0;
    virtual void setGalleryListLayout(bool v) = 0;

    //  Welche Gruppen im Einstellungen-Fenster sind ZUGEKLAPPT? Gespeichert
    //  werden die stabilen Schlüssel der Gruppen (`SettingsGroup.key`), NICHT
    //  ihre Überschriften - die sind übersetzt und änderten sich mit der
    //  Sprache. Nicht genannt = offen; eine frische Installation zeigt also
    //  alles, was sie vorher auch zeigte.
    virtual QStringList collapsedSettingsGroups() const = 0;
    virtual void        setCollapsedSettingsGroups(const QStringList& keys) = 0;

    //  Zeilenhöhe der LISTEN-Darstellung der Galerie (`galleryListLayout` bzw.
    //  der Player-Modus). Das Gegenstück zur Kachelgröße - eine Zeile ist immer
    //  so breit wie die Fläche, ihre Höhe ist also die einzige Größe, die es zu
    //  wählen gibt. Vorgabe 46 = der Wert, der vorher fest im QML stand.
    //  Geklemmt auf 28…160 BEIM LESEN UND SCHREIBEN: eine von Hand verstellte
    //  Datei darf keine unbrauchbare Zeile in die Galerie durchreichen.
    virtual int  galleryListRowHeight() const = 0;
    virtual void setGalleryListRowHeight(int px) = 0;

    //  Löscht „Tag löschen" den Tag auch in ALLEN Unterordnern des offenen
    //  Ordners? Jeder Ordner führt seine Verschlagwortung in einer eigenen
    //  Sidecar-Datei; ohne diesen Schalter blieb ein gelöschter Tag dort
    //  stehen (Nutzerbefund). **Standard AN** - wer einen Tag loswerden will,
    //  meint ihn in aller Regel im ganzen Baum. Ausgeschaltet wirkt das
    //  Löschen nur im offenen Ordner, wie früher.
    virtual bool deleteTagsInSubfolders() const = 0;
    virtual void setDeleteTagsInSubfolders(bool v) = 0;

    //  Schriftfarbe beim Export eines Klartextes nach PDF (Texteditor „-> PDF").
    //  VORGABE für alle Dateien; eine einzelne Datei kann sie im Ordner-Sidecar
    //  überschreiben (JsonStorage::textPdfColor). Standard: Schwarz.
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

    // Tile arrangement / alignment
    virtual int  tileWidth()  const = 0;
    virtual int  tileHeight() const = 0;

    virtual TileArrangement tileArrangement() const = 0;
    virtual void            setTileArrangement(TileArrangement a) = 0;
    virtual int             manualAreaWidth()  const = 0;   // px; only used in Manual mode
    virtual void            setManualAreaWidth(int w) = 0;


    virtual bool optionsVisible() const = 0;
    virtual void setOptionsVisible(bool v) = 0;

    // PDF-Editor: Text-Eigenschaften als obere Leiste (Word-Stil, true) oder
    // als rechte Seitenleiste (false, Standard).
    virtual bool pdfEditPanelTop() const = 0;
    virtual void setPdfEditPanelTop(bool v) = 0;

    // PDF-Editor: Verhalten des EINEN Export-Knopfes. true (Standard) =
    // verlustfrei bevorzugen - „Text ersetzen"-Änderungen werden direkt in die
    // eingebettete Textebene geschrieben, die Seite bleibt vektoriell
    // (durchsuchbarer Text, eingebettete Schriften); wo das nicht sicher
    // möglich ist, fällt der Controller selbsttätig auf den Raster-Export
    // zurück. false = immer der Raster-Export (150 dpi Seitenbild).
    virtual bool pdfExportLossless() const = 0;
    virtual void setPdfExportLossless(bool v) = 0;

    // PDF-Editor: Eigene Notizen beim Export als ECHTE PDF-Annotationen
    // schreiben (Interchange) statt sie als Inhalt zu malen. true = in anderen
    // Betrachtern bleiben sie auswähl-, verschieb- und löschbar; false
    // (Standard) = gemalter Inhalt - der sieht überall gleich aus und lässt
    // sich nicht versehentlich wegklicken. Nicht abbildbare Notizen („Text
    // ersetzen", verkettete Textboxen) werden IMMER gemalt; enthält die Seite
    // solche, gilt der gemalte Weg für alles.
    virtual bool pdfExportAsAnnotations() const = 0;
    virtual void setPdfExportAsAnnotations(bool v) = 0;

    // ── DOCX-Editor ──────────────────────────────────────────────────────────
    //  true (Standard) = „Direkt speichern" (Original + einmalige .bak je
    //  Sitzung); false = „Kopie exportieren" (<Name>_edited(.n).docx).
    virtual bool docxSaveDirect() const = 0;
    virtual void setDocxSaveDirect(bool v) = 0;
    //  Seitenzahl unten: 0 = aus, 1 = links, 2 = mittig, 3 = rechts.
    virtual int  docxPdfPageNumberPos() const = 0;
    virtual void setDocxPdfPageNumberPos(int pos) = 0;
    //  Form der Seitenzahl: 0 = nur die Seite („3"), 1 = mit Gesamtzahl („3 / 12").
    virtual int  docxPdfPageNumberStyle() const = 0;
    virtual void setDocxPdfPageNumberStyle(int style) = 0;

    // Text editor / auto-save
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

    // Saved / bookmarked folders (persistent quick-access list)
    virtual QStringList savedFolders() const = 0;
    virtual void        setSavedFolders(const QStringList& paths) = 0;

    // Bookmark groups: display order of the named groups. One entry per group,
    // "Name" or "Name\t1" when the group is collapsed in the UI. Bookmarks
    // themselves carry their group name in their own entry (see AppController).
    virtual QStringList bookmarkGroups() const = 0;
    virtual void        setBookmarkGroups(const QStringList& groups) = 0;

    // Two-pane main screen: split ratio and the folder of the second pane
    // (empty = only one pane was open).
    virtual qreal   paneSplit() const = 0;
    virtual void    setPaneSplit(qreal v) = 0;
    virtual QString secondFolder() const = 0;
    virtual void    setSecondFolder(const QString& path) = 0;

    // ── Audio player mode (see src/audio/) ──────────────────────────────────
    //  Equalizer: on/off, ten band gains in dB, preamp in dB, own presets as
    //  "name<tab>preamp<tab>g0…g9". Plus the player's own options.
    virtual bool        audioEqEnabled() const = 0;
    virtual void        setAudioEqEnabled(bool on) = 0;
    virtual QList<double> audioEqBands() const = 0;
    virtual void        setAudioEqBands(const QList<double>& db) = 0;
    virtual double      audioEqPreamp() const = 0;
    virtual void        setAudioEqPreamp(double db) = 0;
    virtual QStringList audioEqPresets() const = 0;
    virtual void        setAudioEqPresets(const QStringList& presets) = 0;
    //  ── Die MITGELIEFERTEN Voreinstellungen sind veraenderbar ────────────────
    //  Sie stehen fest im Programm; was der Nutzer daran aendert, liegt
    //  DANEBEN - so bleibt jederzeit ein Rueckweg (Festlegung des Nutzers).
    //  `audioEqHiddenPresets`: Namen mitgelieferter Voreinstellungen, die
    //  geloescht wurden. Eine ueberschriebene steht schlicht als eigene mit
    //  demselben Namen in `audioEqPresets` und gewinnt dort.
    //  `audioEqAutoPreamp`: rechnet der Equalizer selbst gegen das
    //  Uebersteuern? AN bedeutet, dass die Vorverstaerkung bei jedem
    //  Reglerwechsel auf die Spitzenverstaerkung der Kette gesetzt wird. AUS
    //  heisst: es wird NICHTS gerechnet, der Regler gehoert ganz dem Nutzer -
    //  auch wenn er damit uebersteuert (Festlegung des Nutzers).
    virtual bool        audioEqAutoPreamp() const = 0;
    virtual void        setAudioEqAutoPreamp(bool on) = 0;
    virtual QStringList audioEqHiddenPresets() const = 0;
    virtual void        setAudioEqHiddenPresets(const QStringList& names) = 0;
    //  Anzeigereihenfolge (Namen). Was hier fehlt, haengt sich hinten an -
    //  eine neue mitgelieferte Voreinstellung geht damit nie verloren.
    virtual QStringList audioEqPresetOrder() const = 0;
    virtual void        setAudioEqPresetOrder(const QStringList& names) = 0;
    //  War der Player-Modus beim Beenden an? (Er soll den Neustart überleben.)
    virtual bool        audioPlayerMode() const = 0;
    virtual void        setAudioPlayerMode(bool on) = 0;
    //  … und in WELCHER Hälfte er stand. Ohne das bekam ihn beim Neustart
    //  schlicht die zuerst gebaute Hälfte - bei geteiltem Fenster also oft die
    //  falsche (vom Nutzer gemeldet: „links und rechts sind vertauscht").
    //  Bitmaske: Bit i = Haelfte i stand im Player-Modus. Eine einzelne
    //  Platznummer reichte nicht - bei geteiltem Fenster hat JEDE Haelfte einen
    //  eigenen Modus, und beide sollen so wiederkommen, wie sie standen.
    virtual int         audioPlayerModeMask() const = 0;
    virtual void        setAudioPlayerModeMask(int mask) = 0;
    //  Darstellung im Player-Modus: false = Kacheln wie sonst, true = Liste.
    virtual bool        audioListLayout() const = 0;
    virtual void        setAudioListLayout(bool on) = 0;
    virtual bool        audioShowVideos() const = 0;
    virtual void        setAudioShowVideos(bool on) = 0;
    virtual bool        audioRememberLast() const = 0;
    virtual void        setAudioRememberLast(bool on) = 0;
    //  „Ton aus Video sichern": erbt die neue Audiodatei die Verschlagwortung
    //  des Videos, und soll sie gleich in die Warteschlange?
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
