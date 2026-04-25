#pragma once
#include "ISettings.h"
#include <QObject>
#include <QSettings>

class AppSettings : public QObject, public ISettings {
    Q_OBJECT
public:
    static AppSettings& instance();

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

    SortField sortField() const override;
    void      setSortField(SortField f) override;
    SortOrder sortOrder() const override;
    void      setSortOrder(SortOrder o) override;

    QColor backgroundColor() const override;
    void   setBackgroundColor(const QColor& c) override;
    QColor accentColor() const override;
    void   setAccentColor(const QColor& c) override;

    int  gridColumns() const override;
    void setGridColumns(int c) override;

    bool tagFilterAnd() const override;
    void setTagFilterAnd(bool v) override;

    QColor andOrButtonColor() const override;
    void   setAndOrButtonColor(const QColor& c) override;

    bool optionsVisible() const override;
    void setOptionsVisible(bool v) override;

    bool showImages() const override; void setShowImages(bool v) override;
    bool showVideos() const override; void setShowVideos(bool v) override;
    bool showAudio()  const override; void setShowAudio(bool v) override;
    bool showPdfs()   const override; void setShowPdfs(bool v)  override;

    DesignProfile designProfile() const override;
    void          setDesignProfile(DesignProfile p) override;
    ThemeColors   currentTheme() const override;
    ThemeColors   customTheme() const override;
    void          setCustomTheme(const ThemeColors& t) override;

    bool exportCustomTheme(const QString& filePath) const override;
    bool importCustomTheme(const QString& filePath) override;

    static ThemeColors themeForProfile(DesignProfile p);

    void sync() override;

signals:
    void languageChanged(Language l);
    void colorSchemeChanged();
    void themeChanged();

private:
    explicit AppSettings(QObject* parent = nullptr);
    QSettings m_settings;
};
