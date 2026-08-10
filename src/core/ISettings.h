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
//              Drag&Drop-Reihenfolge) — Standard
//  Compact   = schlichtes Einzelraster + Namensdialog (minimalistisch)
enum class ExtractLayout { Workbench, Compact };

// ─── Tile arrangement / alignment mode ───────────────────────────────────────
enum class TileArrangement {
    Centered,       // tiles centred in viewport (current default)
    Left,           // flush left
    Right,          // flush right
    Manual          // user-defined fixed-width area
};

enum class DesignProfile {
    Dark, DarkOLED, OceanDepth, InfernoBlaze,
    NeonPurple, MidnightRose, Elegant, Simple, Custom
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
    // Hintergrund des Text-Editors — GETRENNT für TXT- und HTML-Quellansicht
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
    // geteilten Ansicht) — startet eine neue Wiedergabe, wird die bereits
    // laufende automatisch pausiert (nicht gestoppt). Standard: AN.
    virtual bool monoPlay() const = 0;
    virtual void setMonoPlay(bool v) = 0;

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

    // PDF-Editor: Seiten hinzufügen/entfernen (Aufgabe 3). false (Standard) =
    // nicht-destruktiv (Änderungen im Sidecar, wirken erst beim Export); true =
    // destruktiv (Original-PDF wird bei jeder Änderung sofort neu geschrieben).
    virtual bool pdfPageEditDestructive() const = 0;
    virtual void setPdfPageEditDestructive(bool v) = 0;

    // PDF-Editor: Verhalten des EINEN Export-Knopfes. true (Standard) =
    // verlustfrei bevorzugen — „Text ersetzen"-Änderungen werden direkt in die
    // eingebettete Textebene geschrieben, die Seite bleibt vektoriell
    // (durchsuchbarer Text, eingebettete Schriften); wo das nicht sicher
    // möglich ist, fällt der Controller selbsttätig auf den Raster-Export
    // zurück. false = immer der Raster-Export (150 dpi Seitenbild).
    virtual bool pdfExportLossless() const = 0;
    virtual void setPdfExportLossless(bool v) = 0;

    // PDF-Editor: Eigene Notizen beim Export als ECHTE PDF-Annotationen
    // schreiben (Interchange) statt sie als Inhalt zu malen. true = in anderen
    // Betrachtern bleiben sie auswähl-, verschieb- und löschbar; false
    // (Standard) = gemalter Inhalt — der sieht überall gleich aus und lässt
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

    virtual QString rhiBackend() const = 0;   // liest "rhi/detectedBackend" aus QSettings

    virtual void sync() = 0;
};
