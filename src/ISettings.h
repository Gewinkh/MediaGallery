#pragma once
#include <QObject>
#include <QString>
#include <QSize>
#include <QPoint>
#include <QColor>

enum class Language      { German, English };
enum class VideoPlayback { Native, External };
enum class SortField     { Date, Name, Tags, FileSize };
enum class SortOrder     { Ascending, Descending };

enum class DesignProfile {
    Dark, DarkOLED, OceanDepth, InfernoBlaze,
    NeonPurple, MidnightRose, Elegant, Simple, Custom
};

struct GradStop { float pos; QColor color; };

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

    virtual SortField sortField() const = 0;
    virtual void      setSortField(SortField f) = 0;
    virtual SortOrder sortOrder() const = 0;
    virtual void      setSortOrder(SortOrder o) = 0;

    virtual QColor backgroundColor() const = 0;
    virtual void   setBackgroundColor(const QColor& c) = 0;
    virtual QColor accentColor() const = 0;
    virtual void   setAccentColor(const QColor& c) = 0;

    virtual int  gridColumns() const = 0;
    virtual void setGridColumns(int c) = 0;

    virtual bool tagFilterAnd() const = 0;
    virtual void setTagFilterAnd(bool v) = 0;

    virtual QColor andOrButtonColor() const = 0;
    virtual void   setAndOrButtonColor(const QColor& c) = 0;

    virtual bool optionsVisible() const = 0;
    virtual void setOptionsVisible(bool v) = 0;

    virtual bool showImages() const = 0; virtual void setShowImages(bool v) = 0;
    virtual bool showVideos() const = 0; virtual void setShowVideos(bool v) = 0;
    virtual bool showAudio()  const = 0; virtual void setShowAudio(bool v) = 0;
    virtual bool showPdfs()   const = 0; virtual void setShowPdfs(bool v)  = 0;

    virtual DesignProfile designProfile() const = 0;
    virtual void          setDesignProfile(DesignProfile p) = 0;
    virtual ThemeColors   currentTheme() const = 0;
    virtual ThemeColors   customTheme() const = 0;
    virtual void          setCustomTheme(const ThemeColors& t) = 0;

    virtual bool exportCustomTheme(const QString& filePath) const = 0;
    virtual bool importCustomTheme(const QString& filePath) = 0;

    virtual void sync() = 0;
};
