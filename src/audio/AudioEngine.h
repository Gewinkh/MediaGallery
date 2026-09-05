#pragma once
#include <QAudioFormat>
#include <QIODevice>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QFile>
#include <QTimer>

#include "audio/AudioRing.h"

#include <vector>

class AudioEqualizer;
class AudioPull;
class QAudioDecoder;
class QAudioSink;

// Eigene Kette Datei -> QAudioDecoder -> AudioRing -> Equalizer -> QAudioSink, weil
// QMediaPlayer seine Samples nicht herausgibt. QAudioDecoder kann nicht springen:
// fuer einen Sprung wird er neu gestartet und bis zur Zielstelle verworfen.
class AudioEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)

public:
    enum class State { Stopped = 0, Playing = 1, Paused = 2 };
    Q_ENUM(State)

    explicit AudioEngine(AudioEqualizer& eq, QObject* parent = nullptr);
    ~AudioEngine() override;

    State   state() const { return m_state; }
    int     stateInt() const { return int(m_state); }
    QString currentPath() const { return m_path; }
    qint64  position() const;               // ms
    qint64  duration() const { return m_durationMs; }
    int     underruns() const { return m_underruns.load(std::memory_order_relaxed); }
    qreal   volume() const { return m_volume; }

    Q_INVOKABLE void play(const QString& path);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    void forgetCurrent();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void setVolume(qreal v);

    // Der Aufrufer meldet an, was NACH dem laufenden Titel kommt. Sobald der restlos im Ring liegt, hängt die Kette
    // den nächsten unmittelbar dahinter - kein Neustart. Eine zu späte Anmeldung gilt dem übernächsten Titel.
    void setNextTrack(const QString& path);
    QString nextTrack() const { return m_nextPath; }

signals:
    void stateChanged();
    void currentPathChanged();
    void positionChanged();
    void durationChanged();
    // Der Titel ist NATÜRLICH zu Ende - die Warteschlange entscheidet, was folgt. Kommt NUR, wenn kein nächster
    // angemeldet war: beim lückenlosen Übergang meldet die Kette stattdessen `advancedToNext`.
    void finished();
    //  Der lückenlose Übergang hat stattgefunden - ab jetzt ist `path` der
    //  laufende Titel. Die Warteschlange zieht damit ihren Zeiger nach.
    void advancedToNext(const QString& path);
    void volumeChanged();
    void error(const QString& message);

private:
    friend class AudioPull;                 // der Zuliefer-Ruf der Ausgabe

    //  `byteOffset` > 0: der Dekoder beginnt an dieser Stelle der Datei
    //  (Sprung ohne Vorlauf, s. `AudioSeekIndex.h`); `skipMs` ist dann nur noch
    //  der Rest bis zur genauen Zielstelle.
    void startDecode(const QString& path, qint64 skipMs, qint64 byteOffset = 0);
    //  Einen Dekoder in den Ring leeren. `false` = der Ring ist voll, der Rest
    //  wartet im zugehörigen `pending` auf den nächsten Takt.
    bool feedFrom(QAudioDecoder* dec, std::vector<float>& pending, size_t& at);
    void startNextDecoder();
    //  Die Ausgabe hat die Nahtstelle erreicht: der nächste Titel ist ab jetzt
    //  der laufende (Pfad, Dauer, Position, Dekoder).
    void promoteNext();
    void clearNext();
    //  Die Senke wird ERST gestartet, wenn genug im Ring liegt - sonst zieht sie
    //  eine Viertelsekunde Stille, bevor der erste Ton kommt (gemessen: 255 ms).
    void startSinkIfReady();
    void onBufferReady();
    void onDecodeFinished();
    void setState(State s);
    void teardown();
    qint64 pullAudio(char* data, qint64 maxSize);
    //  Wie viel die Ausgabe JETZT abholen könnte (Bytes). Ohne diese Auskunft
    //  fragt `QAudioSink` im Zieh-Betrieb gar nicht erst an - s. AudioEngine.cpp.
    qint64 pullBytesAvailable() const;

    //  Wandelt die gerechneten Float-Werte in das Format der Senke. Läuft im
    //  AUDIO-Thread - ohne Zuweisung, der Zwischenpuffer steht vorher bereit.
    void convertOut(const float* in, char* out, qint64 values) const;

    AudioEqualizer&        m_eq;
    QAudioDecoder*         m_decoder = nullptr;
    //  Gerät für den Sprung: zeigt die Datei ab dem Zielrahmen. Gehört der
    //  Kette und wird in `teardown()` mit abgeräumt.
    QIODevice*             m_tail = nullptr;
    QIODevice*             m_pendingStream = nullptr;
    //  Nach einem Sprung meldet der Dekoder die Dauer des RESTES - die wird
    //  dann nicht übernommen.
    bool                   m_keepDuration = false;
    QAudioSink*            m_sink = nullptr;
    AudioPull*             m_pull = nullptr;     // gehört uns
    AudioRing              m_ring;
    // ZWEI Formate, bewusst getrennt: gerechnet wird in float (`m_work`), ausgegeben in dem, was das Gerät nimmt
    // (`m_format`). Beides in einen Topf war der Grund für das Rauschen - bei Int16 gingen Float-Bytes roh hinein.
    QAudioFormat           m_format;      // Senke
    QAudioFormat           m_work;        // Dekoder/Ring/Equalizer (immer Float)
    std::vector<float>     m_convBuf;
    //  REST eines dekodierten Stücks, das nicht mehr in den Ring passte. Ohne
    //  ihn ging genau dieser Teil verloren - hörbar als Knacken/Rauschen, weil
    //  `AudioRing::write` beim vollen Ring nur schreibt, was hineinpasst.
    std::vector<float>     m_pending;
    size_t                 m_pendingAt = 0;

    QString m_path;
    State   m_state = State::Stopped;

    // Der zweite Dekoder schreibt in DENSELBEN Ring - er darf das, weil jeder Dekoder auf `m_work` festgelegt ist:
    // zwei Titel unterscheiden sich im Ring nicht mehr voneinander.
    QAudioDecoder*     m_nextDec = nullptr;
    QString            m_nextPath;         // angemeldet, noch nicht begonnen
    QString            m_queuedPath;       // wird gerade nachgeschoben
    qint64             m_queuedDurationMs = 0;
    //  Stand von `m_framesIn`, an dem der laufende Titel endet und der nächste
    //  beginnt. −1 = kein Übergang vorbereitet.
    qint64             m_boundaryFrames = -1;
    //  Stand von `m_framesOut`, an dem der LAUFENDE Titel begann - die Position
    //  zählt ab hier, nicht ab dem Start der Senke.
    qint64             m_frameOrigin = 0;
    bool               m_nextDecodeDone = false;
    std::vector<float> m_nextPending;
    size_t             m_nextPendingAt = 0;
    qreal   m_volume = 0.85;
    qint64  m_durationMs = 0;
    std::atomic<qint64> m_framesOut { 0 };
    std::atomic<int>    m_underruns { 0 };
    //  Diagnose (`MG_AUDIO_DUMP=<datei>`): schreibt mit, was der Senke wirklich
    //  übergeben wird - die einzige Möglichkeit, „klingt falsch" zu belegen.
    QFile*  m_dump = nullptr;
    qint64  m_baseFrames = 0;
    qint64  m_skipFrames = 0;
    //  Wie viele Frames insgesamt in den Ring geschrieben wurden. Daran hängt
    //  die Ende-Erkennung: gespielt ist erst, was die SENKE ausgegeben hat.
    qint64  m_framesIn = 0;
    bool    m_decodeDone = false;
    QTimer  m_tick;          // Positionsanzeige + Ende-Erkennung
    //  Nachschub-Takt: `bufferReady` kommt NICHT erneut, solange schon Stücke
    //  bereitliegen - war der Ring gerade voll, holt sie sonst niemand mehr ab.
    QTimer  m_feed;
};
