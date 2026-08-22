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

// ─────────────────────────────────────────────────────────────────────────────
//  AudioEngine - die eigene Wiedergabekette für Audiodateien.
//
//      Datei -> QAudioDecoder -> AudioRing -> Preamp+Equalizer -> QAudioSink
//
//  WARUM NICHT `QMediaPlayer`: er gibt seine Samples nicht heraus, es gibt für
//  den Ton keine Abgriffstelle (für das Bild schon: `QVideoSink`). Ein echter
//  Equalizer braucht deshalb eine eigene Kette. Videos bleiben unangetastet und
//  laufen weiter über `MediaPlayer` (ohne EQ) - s. `README.md` ▸ Planned.
//
//  DREI EIGENHEITEN, die man kennen muss:
//   • **`QAudioDecoder` kann nicht springen.** Für einen Sprung wird er neu
//     gestartet und bis zur Zielstelle verworfen. Dekodieren läuft bei MP3/AAC
//     ~50–100× schneller als die Wiedergabe; die Kosten misst `bench_audio`.
//   • **Die Position rechnet die Kette selbst** aus den ausgegebenen Frames -
//     der Dekoder läuft der Ausgabe ja voraus.
//   • **Der Zuliefer-Ruf läuft in Qts Audio-Thread.** Dort passiert nur:
//     aus dem Ring lesen, Equalizer anwenden, fertig. Keine Zuweisung, kein
//     Signal, kein Datei-Zugriff.
// ─────────────────────────────────────────────────────────────────────────────
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
    //  Wie oft der Zieh-Ruf leer ausging und mit Stille auffüllen musste. Genau
    //  DAS hört man als Knacken/Rauschen - der Prüfstand liest es aus.
    int     underruns() const { return m_underruns.load(std::memory_order_relaxed); }
    qreal   volume() const { return m_volume; }

    Q_INVOKABLE void play(const QString& path);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    //  Den gemerkten Titel VERGESSEN (Ordnerwechsel). `stop()` allein behält ihn
    //  bewusst - die Leiste soll nach dem Anhalten noch zeigen, was gewählt ist.
    void forgetCurrent();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void setVolume(qreal v);

    //  ── Lückenloser Übergang ────────────────────────────────────────────────
    //  Der Aufrufer meldet an, was NACH dem laufenden Titel kommt (leer =
    //  nichts). Sobald der laufende Titel restlos im Ring liegt, hängt die Kette
    //  den nächsten UNMITTELBAR dahinter - dieselbe Senke, derselbe Ring, kein
    //  Neustart. Ohne Anmeldung endet die Kette wie zuvor mit `finished()`.
    //  Kommt die Anmeldung zu spät (der Übergang steht schon fest), wird sie
    //  für den ÜBERNÄCHSTEN Titel vorgemerkt.
    void setNextTrack(const QString& path);
    QString nextTrack() const { return m_nextPath; }

signals:
    void stateChanged();
    void currentPathChanged();
    void positionChanged();
    void durationChanged();
    //  Der Titel ist NATÜRLICH zu Ende (nicht gestoppt) - die Warteschlange
    //  entscheidet, was folgt. Kommt NUR, wenn kein nächster Titel angemeldet
    //  war: beim lückenlosen Übergang läuft die Kette weiter und meldet
    //  stattdessen `advancedToNext`.
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
    //  Den angemeldeten Titel hinter dem laufenden anhängen (zweiter Dekoder).
    void startNextDecoder();
    //  Die Ausgabe hat die Nahtstelle erreicht: der nächste Titel ist ab jetzt
    //  der laufende (Pfad, Dauer, Position, Dekoder).
    void promoteNext();
    //  Vorbereitung verwerfen (Sprung, Stopp, neuer Titel von Hand).
    void clearNext();
    //  Die Senke wird ERST gestartet, wenn genug im Ring liegt - sonst zieht sie
    //  eine Viertelsekunde Stille, bevor der erste Ton kommt (gemessen: 255 ms).
    void startSinkIfReady();
    void onBufferReady();
    void onDecodeFinished();
    void setState(State s);
    void teardown();
    //  Wird im AUDIO-Thread gerufen: Ring lesen, Equalizer anwenden.
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
    //  Von `seek()` vorbereiteter Strom (Hülle) - `startDecode` übernimmt ihn.
    QIODevice*             m_pendingStream = nullptr;
    //  Nach einem Sprung meldet der Dekoder die Dauer des RESTES - die wird
    //  dann nicht übernommen.
    bool                   m_keepDuration = false;
    QAudioSink*            m_sink = nullptr;
    AudioPull*             m_pull = nullptr;     // gehört uns
    AudioRing              m_ring;
    //  ZWEI Formate, bewusst getrennt: **gerechnet wird immer in `float`**
    //  (`m_work` - Dekoder, Ring und Equalizer), ausgegeben wird in dem, was
    //  das Gerät wirklich nimmt (`m_format`). Beides in einen Topf zu werfen
    //  war der Grund für das Rauschen: fiel das Gerät auf Int16 zurück, wurden
    //  Float-Bytes weiterhin roh hineingeschrieben.
    QAudioFormat           m_format;      // Senke
    QAudioFormat           m_work;        // Dekoder/Ring/Equalizer (immer Float)
    //  Zwischenpuffer des Zieh-Rufs (nur nötig, wenn gewandelt werden muss).
    std::vector<float>     m_convBuf;
    //  REST eines dekodierten Stücks, das nicht mehr in den Ring passte. Ohne
    //  ihn ging genau dieser Teil verloren - hörbar als Knacken/Rauschen, weil
    //  `AudioRing::write` beim vollen Ring nur schreibt, was hineinpasst.
    std::vector<float>     m_pending;
    size_t                 m_pendingAt = 0;

    QString m_path;
    State   m_state = State::Stopped;

    //  ── Lückenloser Übergang ────────────────────────────────────────────────
    //  Der zweite Dekoder schreibt in DENSELBEN Ring - er darf das, weil jeder
    //  Dekoder auf `m_work` (Float, Rate und Kanäle der Senke) festgelegt ist:
    //  zwei Titel unterscheiden sich im Ring nicht mehr voneinander.
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
    //  Wie viele Frames die Ausgabe schon gespielt hat (Grundlage der Position).
    std::atomic<qint64> m_framesOut { 0 };
    std::atomic<int>    m_underruns { 0 };
    //  Diagnose (`MG_AUDIO_DUMP=<datei>`): schreibt mit, was der Senke wirklich
    //  übergeben wird - die einzige Möglichkeit, „klingt falsch" zu belegen.
    QFile*  m_dump = nullptr;
    //  Beim Sprung übersprungene Frames - sie zählen zur Position dazu.
    qint64  m_baseFrames = 0;
    //  Noch zu verwerfende Frames (Sprung: der Dekoder beginnt immer vorn).
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
