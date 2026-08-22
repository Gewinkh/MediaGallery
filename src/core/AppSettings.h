#pragma once
#include "core/ISettings.h"
#include <QObject>
#include <QSettings>

class AppSettings : public QObject, public ISettings {
    Q_OBJECT

    // Schreibgeschützte Eigenschaft: aktives RHI-Backend (z. B. „vulkan").
    // Wird von SettingsGeneralTab.qml nur zur Anzeige verwendet.
    Q_PROPERTY(QString rhiBackend READ rhiBackend CONSTANT)

public:
    static AppSettings& instance();

    // Gibt das vom RhiProber erkannte und in QSettings gespeicherte
    // RHI-Backend zurück. Leer, wenn noch kein Probe gelaufen ist.
    QString rhiBackend() const;

    QSize  windowSize() const override;
    void   setWindowSize(const QSize& s) override;
    QPoint windowPos() const override;
    void   setWindowPos(const QPoint& p) override;
    bool   windowMaximized() const override;
    void   setWindowMaximized(bool m) override;

    QString lastFolder() const override;
    void    setLastFolder(const QString& path) override;

    Language language() const override;
    void     setLanguage(Language l) override;

    VideoPlayback videoPlayback() const override;
    void          setVideoPlayback(VideoPlayback v) override;

    PageTransition pageTransition() const override;
    void           setPageTransition(PageTransition t) override;

    ExtractSelectStyle extractSelectStyle() const override;
    void               setExtractSelectStyle(ExtractSelectStyle s) override;
    ExtractLayout      extractLayout() const override;
    void               setExtractLayout(ExtractLayout l) override;
    bool           audioAccentApple() const override;
    void           setAudioAccentApple(bool v) override;

    bool monoPlay() const override;
    void setMonoPlay(bool v) override;
    bool fileDropMove() const override;
    void setFileDropMove(bool v) override;
    bool showAllFiles() const override;
    void setShowAllFiles(bool v) override;
    QColor textPdfColor() const override;
    void   setTextPdfColor(const QColor& c) override;

    bool spellCheckEnabled() const override;
    void setSpellCheckEnabled(bool v) override;
    QString spellLanguage() const override;
    void    setSpellLanguage(const QString& lang) override;

    int  videoSeekStep() const override;
    void setVideoSeekStep(int seconds) override;

    QColor backgroundColor() const override;
    void   setBackgroundColor(const QColor& c) override;
    QColor accentColor() const override;
    void   setAccentColor(const QColor& c) override;

    TileArrangement tileArrangement() const override;
    void            setTileArrangement(TileArrangement a) override;
    int             manualAreaWidth()  const override;
    void            setManualAreaWidth(int w) override;

    int  tileWidth()  const override;
    int  tileHeight() const override;
    void setTileSize(int w, int h);   // atomic: saves both + emits tileSizeChanged once

    bool optionsVisible() const override;
    void setOptionsVisible(bool v) override;

    bool pdfEditPanelTop() const override;
    void setPdfEditPanelTop(bool v) override;
    bool pdfPageEditDestructive() const override;
    void setPdfPageEditDestructive(bool v) override;
    bool pdfExportLossless() const override;
    void setPdfExportLossless(bool v) override;
    bool pdfExportAsAnnotations() const override;
    void setPdfExportAsAnnotations(bool v) override;
    bool docxSaveDirect() const override;
    void setDocxSaveDirect(bool v) override;

    bool autoSaveEnabled() const override;
    void setAutoSaveEnabled(bool v) override;
    int  autoSaveIntervalSeconds() const override;
    void setAutoSaveIntervalSeconds(int s) override;

    DesignProfile designProfile() const override;
    void          setDesignProfile(DesignProfile p) override;
    ThemeColors   currentTheme() const override;
    ThemeColors   customTheme() const override;
    void          setCustomTheme(const ThemeColors& t) override;

    bool exportCustomTheme(const QString& filePath) const override;
    bool importCustomTheme(const QString& filePath) override;

    QStringList savedFolders() const override;
    void        setSavedFolders(const QStringList& paths) override;

    bool        audioEqEnabled() const override;
    void        setAudioEqEnabled(bool on) override;
    QList<double> audioEqBands() const override;
    void        setAudioEqBands(const QList<double>& db) override;
    double      audioEqPreamp() const override;
    void        setAudioEqPreamp(double db) override;
    QStringList audioEqPresets() const override;
    void        setAudioEqPresets(const QStringList& presets) override;
    bool        audioPlayerMode() const override;
    void        setAudioPlayerMode(bool on) override;
    bool        audioListLayout() const override;
    void        setAudioListLayout(bool on) override;
    bool        audioShowVideos() const override;
    void        setAudioShowVideos(bool on) override;
    bool        audioRememberLast() const override;
    int         docxPdfPaddingMm() const override;
    void        setDocxPdfPaddingMm(int mm) override;
    int         docxPdfPageNumberPos() const override;
    void        setDocxPdfPageNumberPos(int pos) override;
    int         docxPdfPageNumberStyle() const override;
    void        setDocxPdfPageNumberStyle(int style) override;
    bool        audioExtractInheritTags() const override;
    void        setAudioExtractInheritTags(bool on) override;
    bool        audioExtractToQueue() const override;
    void        setAudioExtractToQueue(bool on) override;
    void        setAudioRememberLast(bool on) override;
    QString     audioLastFile() const override;
    void        setAudioLastFile(const QString& path) override;
    qint64      audioLastPosition() const override;
    void        setAudioLastPosition(qint64 ms) override;
    double      audioVolume() const override;
    void        setAudioVolume(double v) override;
    bool        audioShuffle() const override;
    void        setAudioShuffle(bool on) override;
    int         audioRepeat() const override;
    void        setAudioRepeat(int mode) override;

    qreal   paneSplit() const override;
    void    setPaneSplit(qreal v) override;
    QString secondFolder() const override;
    void    setSecondFolder(const QString& path) override;

    QStringList bookmarkGroups() const override;
    void        setBookmarkGroups(const QStringList& groups) override;

    static ThemeColors themeForProfile(DesignProfile p);

    void sync() override;

signals:
    void languageChanged(Language l);
    void colorSchemeChanged();
    void themeChanged();
    void tileSizeChanged();
    void tileArrangementChanged();
    void autoSaveSettingsChanged();

private:
    explicit AppSettings(QObject* parent = nullptr);
    QSettings m_settings;
};
