#pragma once
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>

#include <atomic>
#include <memory>

#include "audio/AudioEngine.h"
#include "audio/AudioTags.h"
#include "audio/AudioEqualizer.h"
#include "audio/PlayQueue.h"

class ISettings;

// Die EINE Bridge des Player-Modus nach QML; hält `AudioEngine`, `PlayQueue` und `AudioEqualizer` zusammen.
// APPWEIT, nicht je Hälfte: es gibt EINE Wiedergabe in der App. Presets: eine Zeichenkette je Preset.
class AudioController : public QObject {
    Q_OBJECT

    Q_PROPERTY(int     state       READ state       NOTIFY stateChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(qint64  position    READ position    NOTIFY positionChanged)
    Q_PROPERTY(qint64  duration    READ duration    NOTIFY durationChanged)
    Q_PROPERTY(qreal   volume      READ volume      WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool    active      READ active      NOTIFY stateChanged)
    Q_PROPERTY(bool    hasTrack    READ hasTrack    NOTIFY currentChanged)
    // Diese sechs waren nie nach QML freigegeben - `Audio.trackTitle` war `undefined` und die Kopfzeile zeigte
    // dauerhaft "Kein Titel gewählt". NOTIFY `tagsChanged`: die Angaben werden beim Titelwechsel neu gelesen.
    Q_PROPERTY(QString trackTitle    READ trackTitle    NOTIFY tagsChanged)
    Q_PROPERTY(QString trackArtist   READ trackArtist   NOTIFY tagsChanged)
    Q_PROPERTY(QString trackAlbum    READ trackAlbum    NOTIFY tagsChanged)
    Q_PROPERTY(QString trackSubtitle READ trackSubtitle NOTIFY tagsChanged)
    Q_PROPERTY(bool    trackHasCover READ trackHasCover NOTIFY tagsChanged)
    Q_PROPERTY(QString coverSource   READ coverSource   NOTIFY tagsChanged)

    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(int  repeat  READ repeat  WRITE setRepeat  NOTIFY repeatChanged)

    Q_PROPERTY(bool eqEnabled READ eqEnabled WRITE setEqEnabled NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqGains READ eqGains NOTIFY eqChanged)
    Q_PROPERTY(qreal eqPreamp READ eqPreamp WRITE setEqPreamp NOTIFY eqChanged)
    Q_PROPERTY(bool eqAutoPreamp READ eqAutoPreamp WRITE setEqAutoPreamp
               NOTIFY optionsChanged)
    Q_PROPERTY(QVariantList eqFrequencies READ eqFrequencies CONSTANT)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
    Q_PROPERTY(QString activePreset READ activePreset NOTIFY presetsChanged)
    Q_PROPERTY(bool presetsModified READ presetsModified NOTIFY presetsChanged)

    Q_PROPERTY(QStringList queue      READ queue      NOTIFY queueChanged)
    Q_PROPERTY(int         queueIndex READ queueIndex NOTIFY currentChanged)
    //  WER spielt gerade: die Hälfte, die den Player gestartet hat. Nur dort
    //  gehört die Leiste hin - sonst stünde sie in beiden Galerien.
    Q_PROPERTY(QObject*    owner      READ owner      WRITE setOwner NOTIFY ownerChanged)

    Q_PROPERTY(bool showVideos   READ showVideos   WRITE setShowVideos   NOTIFY optionsChanged)
    Q_PROPERTY(bool playerModeRemembered READ playerModeRemembered
               WRITE setPlayerModeRemembered NOTIFY optionsChanged)
    Q_PROPERTY(bool listLayout   READ listLayout   WRITE setListLayout   NOTIFY optionsChanged)
    Q_PROPERTY(bool rememberLast READ rememberLast WRITE setRememberLast NOTIFY optionsChanged)

    Q_PROPERTY(bool extractBusy READ extractBusy NOTIFY extractBusyChanged)
    Q_PROPERTY(bool extractInheritTags READ extractInheritTags
               WRITE setExtractInheritTags NOTIFY optionsChanged)
    Q_PROPERTY(bool extractToQueue READ extractToQueue
               WRITE setExtractToQueue NOTIFY optionsChanged)

public:
    explicit AudioController(ISettings& settings, QObject* parent = nullptr);
    //  Beim Abräumen wird ein laufendes Sichern abgebrochen und abgewartet:
    //  der Arbeiter hält einen rohen Zeiger auf DIESES Objekt (Rückweg per
    //  `invokeMethod`) - liefe er weiter, zeigte der ins Leere.
    ~AudioController() override;

    int     state() const { return m_engine.stateInt(); }
    bool    active() const { return m_engine.state() != AudioEngine::State::Stopped; }
    QString currentPath() const {
        const QString p = m_engine.currentPath();
        return p.isEmpty() ? m_queue.currentPath() : p;
    }
    bool    hasTrack() const { return !currentPath().isEmpty(); }
    qint64  position() const { return m_engine.position(); }
    qint64  duration() const { return m_engine.duration(); }
    qreal   volume() const { return m_engine.volume(); }
    void    setVolume(qreal v);

    bool shuffle() const { return m_queue.shuffle(); }
    void setShuffle(bool on);
    int  repeat() const { return m_queue.repeatInt(); }
    void setRepeat(int r);

    bool eqEnabled() const { return m_eq.enabled(); }
    void setEqEnabled(bool on);
    QVariantList eqGains() const;
    qreal eqPreamp() const { return m_eq.preamp(); }
    bool  eqAutoPreamp() const;
    void  setEqAutoPreamp(bool on);
    void  setEqPreamp(qreal db);
    QVariantList eqFrequencies() const;
    QStringList  presetNames() const;
    QString      activePreset() const { return m_activePreset; }
    bool         presetsModified() const;

    QStringList queue() const { return m_queue.orderedItems(); }
    int         queueIndex() const { return m_queue.orderedPos(); }
    QObject*    owner() const { return m_owner; }
    void        setOwner(QObject* o);

    bool showVideos() const;
    void setShowVideos(bool on);
    bool playerModeRemembered() const;
    void setPlayerModeRemembered(bool on);
    bool listLayout() const;
    void setListLayout(bool on);
    bool rememberLast() const;
    void setRememberLast(bool on);
    QString trackTitle() const;
    QString trackArtist() const;
    QString trackAlbum() const;
    QString trackSubtitle() const;
    bool    trackHasCover() const;
    QString coverSource() const;
    Q_INVOKABLE QString titleOf(const QString& pathOrUrl) const;

    bool extractBusy() const { return m_extractBusy; }
    bool extractInheritTags() const;
    void setExtractInheritTags(bool on);
    bool extractToQueue() const;
    void setExtractToQueue(bool on);

    Q_INVOKABLE void playFile(const QString& path, const QStringList& queue);
    Q_INVOKABLE void setQueue(const QStringList& queue);
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void stopAndClear();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void playAt(int index);

    Q_INVOKABLE void setBandGain(int band, qreal db);
    Q_INVOKABLE void resetBands();
    Q_INVOKABLE qreal suggestedPreamp() const { return m_eq.suggestedPreamp(); }
    Q_INVOKABLE void applyPreset(const QString& name);
    Q_INVOKABLE void savePreset(const QString& name);
    Q_INVOKABLE void deletePreset(const QString& name);
    Q_INVOKABLE void resetPreset(const QString& name);
    Q_INVOKABLE void resetAllPresets();
    Q_INVOKABLE void movePreset(int from, int to);
    Q_INVOKABLE bool presetIsBuiltin(const QString& name) const;
    Q_INVOKABLE bool presetIsModified(const QString& name) const;
    Q_INVOKABLE QString formatTime(qint64 ms) const;

    Q_INVOKABLE bool takePlayerModeRestore(int paneIndex);
    Q_INVOKABLE bool playerModePaneRemembered(int paneIndex);
    Q_INVOKABLE void rememberPlayerMode(bool on, int paneIndex);

    Q_INVOKABLE bool canExtractAudio(const QString& pathOrUrl) const;
    // Asynchron mit eigenem Pool und kooperativem Abbruch. `trackIndex` < 0 heißt "erst nachsehen": bei mehr als
    // einer Tonspur kommt `trackChoiceNeeded`, sonst läuft es sofort mit der ersten.
    Q_INVOKABLE void extractAudio(const QString& pathOrUrl, int trackIndex = -1);

    Q_INVOKABLE void rememberSession();
    Q_INVOKABLE void restoreSession();

signals:
    void stateChanged();
    void currentChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void shuffleChanged();
    void repeatChanged();
    void eqChanged();
    void presetsChanged();
    void optionsChanged();
    void queueChanged();
    void ownerChanged();
    void tagsChanged();
    void extractBusyChanged();
    void extractFinished(bool ok, const QString& source, const QString& target);
    void trackChoiceNeeded(const QString& source, const QVariantList& tracks);
    void message(const QString& text);

private:
    void armNextTrack();
    void refreshTags();
    friend class AudioExtractTask;
    friend class AudioProbeTask;
    void probeTaskDone(const QString& source, const QVariantList& tracks);
    void extractTaskDone(bool ok, int messageKey, const QString& source,
                         const QString& target, int audioTracks, int generation,
                         int trackIndex = 0);
    void loadSettings();
    void applyGainsToSettings();
    QStringList builtinPresetLines() const;
    QString     presetLine(const QString& name) const;
    QStringList inStoredOrder(const QStringList& names) const;
    QString     m_activePreset;

    ISettings&      m_settings;
    AudioEqualizer  m_eq;
    AudioEngine     m_engine;
    PlayQueue       m_queue;
    qint64          m_pendingSeek = 0;
    //  Nur beobachtet, nie besessen: verschwindet die Hälfte, zeigt der Zeiger
    //  ins Leere - deshalb `QPointer`.
    QPointer<QObject> m_owner;
    void applyAutoPreamp();

    int               m_restoreMask = -1;

    AudioTags::Tags m_tags;
    QString         m_tagsPath;
    int             m_coverRev = 0;
    mutable QHash<QString, QString> m_titleCache;

    QThreadPool                          m_extractPool;
    std::shared_ptr<std::atomic<bool>>   m_extractCancel;
    int                                  m_extractGen  = 0;
    bool                                 m_extractBusy = false;
};
