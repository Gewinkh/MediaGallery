#include "audio/AudioEngine.h"
#include "audio/AudioSeekIndex.h"
#include "audio/MkvAudioExtract.h"

#include <QElapsedTimer>

#include "audio/AudioEqualizer.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioSink>
#include <QFileInfo>
#include <QMediaDevices>
#include <QUrl>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
//  Vorrat im Ring: ~200 ms. Genug, um jede Nachschub-Verzögerung einer Platte
//  zu überbrücken, und klein genug, dass der Speicher nicht auffällt
//  (Stereo/48 kHz ≈ 38 kB).
constexpr int kRingMs = 200;

//  Diagnose-Schalter (Muster wie MG_LOG_THUMBS in MediaModel): meldet, was der
//  Dekoder liefert und was die Ausgabe abholt. Der Audio-Pfad selbst zahlt dafür
//  nur einen Vergleich gegen eine statische Konstante.
const bool kLog = qEnvironmentVariableIntValue("MG_LOG_AUDIO") == 1;
}  // namespace

// Qt zieht daraus im Audio-Thread - hier nur lesen, filtern, zurueckgeben. Zwei
// Pflichten, ohne die nichts laeuft: bytesAvailable() muss stimmen (Vorgabe 0 laesst
// die Senke fuer immer im Leerlauf) und readyRead() muss bei Nachschub kommen.
class AudioPull : public QIODevice {
public:
    explicit AudioPull(AudioEngine* owner) : m_owner(owner) {}
    void notifyData() { emit readyRead(); }
    qint64 bytesAvailable() const override {
        return (m_owner ? m_owner->pullBytesAvailable() : 0) + QIODevice::bytesAvailable();
    }
protected:
    qint64 readData(char* data, qint64 maxSize) override {
        return m_owner ? m_owner->pullAudio(data, maxSize) : 0;
    }
    qint64 writeData(const char*, qint64) override { return 0; }
    bool isSequential() const override { return true; }
private:
    AudioEngine* m_owner;
};

AudioEngine::AudioEngine(AudioEqualizer& eq, QObject* parent)
    : QObject(parent), m_eq(eq)
{
    // "Zu Ende" ist erst, wenn der Ring leer ist UND nichts nachkommt - der Dekoder ist längst fertig, während die
    // Ausgabe spielt. Der Takt ist nötig, weil `bufferReady` nur beim Übergang "nichts da" -> "etwas da" feuert.
    m_feed.setInterval(30);
    connect(&m_feed, &QTimer::timeout, this, &AudioEngine::onBufferReady);

    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (m_state == State::Playing) emit positionChanged();

        // Hat die AUSGABE die Naht überschritten? Gefragt wird die Senke, nicht der Ring: `m_framesOut` ist das
        // Abgeholte, und sie hält davon noch ~280 ms - sonst spränge der Titelname eine Viertelsekunde zu früh um.
        if (m_boundaryFrames >= 0 && m_sink && m_state == State::Playing) {
            const qint64 rate = std::max(1, m_format.sampleRate());
            const qint64 playedFrames = m_sink->processedUSecs() * rate / 1000000;
            if (playedFrames >= m_boundaryFrames) promoteNext();
        }

        if (!m_decodeDone || m_state != State::Playing || !m_sink) return;
        //  Ein vorbereiteter Übergang heißt: es kommt noch etwas. Erst wenn auch
        //  der zweite Dekoder durch ist UND die Naht hinter uns liegt, kann die
        //  Kette zu Ende sein.
        if (m_boundaryFrames >= 0) return;
        if (m_ring.available() != 0) return;
        if (m_pendingAt < m_pending.size()) return;      // Rest wartet noch
        // Der leere Ring heißt NUR: alles ist an die Senke übergeben. Die hat ihren eigenen Puffer (~280 ms) - hier
        // schon "fertig" zu melden bräche den Titel vor seinem Ende ab.
        const qint64 rate = std::max(1, m_format.sampleRate());
        const qint64 neededUs = m_framesIn * 1000000 / rate;
        if (m_sink->processedUSecs() < neededUs) return;

        const QString done = m_path;
        teardown();
        setState(State::Stopped);
        if (!done.isEmpty()) emit finished();
    });
}

AudioEngine::~AudioEngine() { teardown(); }

void AudioEngine::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged();
}

qint64 AudioEngine::position() const {
    if (!m_format.isValid()) return 0;
    //  `m_frameOrigin` ist der Stand der Ausgabe, an dem der laufende Titel
    //  begann - nach einem lückenlosen Übergang ist das nicht mehr 0, weil die
    //  Senke seit dem VORIGEN Titel durchläuft.
    const qint64 played = m_framesOut.load(std::memory_order_relaxed) - m_frameOrigin;
    const qint64 frames = m_baseFrames + std::max<qint64>(0, played);
    return m_format.durationForFrames(int(std::min<qint64>(frames, INT32_MAX))) / 1000;
}

void AudioEngine::teardown() {
    m_tick.stop();
    m_feed.stop();
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (m_pull) {
        m_pull->close();
        m_pull->deleteLater();
        m_pull = nullptr;
    }
    if (m_dump) {
        m_dump->close();
        m_dump->deleteLater();
        m_dump = nullptr;
    }
    if (m_tail) {
        m_tail->close();
        m_tail->deleteLater();
        m_tail = nullptr;
    }
    if (m_decoder) {
        m_decoder->stop();
        m_decoder->deleteLater();
        m_decoder = nullptr;
    }
    clearNext();
    m_ring.clear();
    m_pending.clear();
    m_pendingAt = 0;
    m_frameOrigin = 0;
    m_decodeDone = false;
    m_framesOut.store(0, std::memory_order_relaxed);
    m_underruns.store(0, std::memory_order_relaxed);
    m_skipFrames = 0;
    m_framesIn = 0;
}

void AudioEngine::play(const QString& path) {
    const QString local = path.startsWith(QStringLiteral("file:"))
                          ? QUrl(path).toLocalFile() : path;
    if (local.isEmpty() || !QFileInfo::exists(local)) {
        emit error(QStringLiteral("Datei nicht gefunden: %1").arg(local));
        return;
    }
    teardown();
    m_nextPath.clear();          // der Nachfolger gehört zum vorigen Titel
    m_path = local;
    m_baseFrames = 0;
    m_durationMs = 0;
    emit currentPathChanged();
    emit durationChanged();
    startDecode(local, 0);
}

//  Der Dekoder liefert immer VON VORN - für einen Sprung wird er neu gestartet
//  und bis zur Zielstelle verworfen (`m_skipFrames`).
void AudioEngine::startDecode(const QString& path, qint64 skipMs, qint64 byteOffset) {
    //  Zwei Formate festlegen. GERECHNET wird immer in Gleitkomma; die SENKE
    //  bekommt, was sie kann. Nimmt sie Float, ist die Wandlung ein `memcpy`.
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    QAudioFormat fmt = dev.preferredFormat();
    if (fmt.sampleRate() <= 0) fmt.setSampleRate(48000);
    if (fmt.channelCount() <= 0) fmt.setChannelCount(2);
    QAudioFormat asFloat = fmt;
    asFloat.setSampleFormat(QAudioFormat::Float);
    if (dev.isFormatSupported(asFloat)) fmt = asFloat;
    m_format = fmt;
    m_work = fmt;
    m_work.setSampleFormat(QAudioFormat::Float);
    if (kLog) qDebug("audio: Senke %d Hz, %d Kanäle, Format %d (Float=%d)",
                     m_format.sampleRate(), m_format.channelCount(),
                     int(m_format.sampleFormat()), int(QAudioFormat::Float));

    m_eq.configure(m_format.sampleRate(), m_format.channelCount());
    m_eq.resetState();

    const qint64 ringSamples = qint64(m_format.sampleRate()) * m_format.channelCount()
                               * kRingMs / 1000;
    m_ring.resize(size_t(std::max<qint64>(ringSamples, 4096)));
    m_skipFrames = skipMs > 0 ? m_format.framesForDuration(skipMs * 1000) : 0;
    //  Der Zwischenpuffer des Zieh-Rufs wird HIER bemessen: im Audio-Thread
    //  darf nichts mehr angefordert werden. Ein Ruf holt nie mehr als den Ring.
    m_convBuf.assign(m_ring.capacity() + 4096, 0.0f);

    const QString dumpPath = qEnvironmentVariable("MG_AUDIO_DUMP");
    if (!dumpPath.isEmpty()) {
        m_dump = new QFile(dumpPath, this);
        if (!m_dump->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            delete m_dump;
            m_dump = nullptr;
        } else {
            qDebug("audio: Mitschnitt nach %s (%d Hz, %d Kanäle, Format %d)",
                   qPrintable(dumpPath), m_format.sampleRate(),
                   m_format.channelCount(), int(m_format.sampleFormat()));
        }
    }
    m_framesOut.store(0, std::memory_order_relaxed);
    m_framesIn = 0;
    m_decodeDone = false;

    m_decoder = new QAudioDecoder(this);
    m_decoder->setAudioFormat(m_work);

    // Mit Byte-Versatz bekommt der Dekoder ein Gerät, das genau am Zielrahmen BEGINNT. Ein `seek()` nützt nichts:
    // `setSourceDevice` ignoriert die Position und spult auf 0 zurück (gemessen: bitgenau der Dateianfang).
    if (m_pendingStream) {
        m_tail = m_pendingStream;
        m_pendingStream = nullptr;
        m_tail->setParent(this);
        m_keepDuration = true;
        m_decoder->setSourceDevice(m_tail);
    } else if (byteOffset > 0) {
        auto* tail = new AudioSeek::TailDevice(path, byteOffset, this);
        if (tail->open(QIODevice::ReadOnly)) {
            m_tail = tail;
            m_keepDuration = true;
            m_decoder->setSourceDevice(m_tail);
        } else {
            delete tail;
            m_decoder->setSource(QUrl::fromLocalFile(path));
        }
    } else {
        m_keepDuration = false;
        m_decoder->setSource(QUrl::fromLocalFile(path));
    }
    connect(m_decoder, &QAudioDecoder::bufferReady, this, &AudioEngine::onBufferReady);
    connect(m_decoder, &QAudioDecoder::finished,    this, &AudioEngine::onDecodeFinished);
    connect(m_decoder, &QAudioDecoder::durationChanged, this, [this](qint64 ms) {
        if (m_keepDuration) return;
        if (ms > 0 && ms != m_durationMs) { m_durationMs = ms; emit durationChanged(); }
    });
    // In Qt 6 heißt das Signal schlicht `error(QAudioDecoder::Error)`, den Text holt man beim Dekoder ab. `error`
    // gibt es zweimal - als Abfrage und als Signal -, daher die ausdrückliche Auswahl.
    connect(m_decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
            this, [this](QAudioDecoder::Error) {
        const QString msg = m_decoder ? m_decoder->errorString() : QString();
        emit error(msg);
        teardown();
        setState(State::Stopped);
    });

    m_pull = new AudioPull(this);
    m_pull->open(QIODevice::ReadOnly);
    m_decoder->start();
    m_feed.start();
    m_tick.start();
    setState(State::Playing);
}

//  Anlauf: erst mit Vorrat starten. Vorher zöge die Senke Stille - hörbar als
//  Verzögerung zwischen Klick und Ton.
void AudioEngine::startSinkIfReady() {
    if (m_sink || !m_pull || m_state != State::Playing) return;
    const qint64 needed = qint64(m_work.sampleRate()) * m_work.channelCount() * 60 / 1000;
    if (!m_decodeDone && qint64(m_ring.available()) < needed) return;

    m_sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), m_format, this);
    m_sink->setVolume(float(m_volume));
    m_sink->start(m_pull);
}

// Passt nichts mehr in den Ring, wird gewartet: `bufferReady` bleibt anstehen. Beide Dekoder sind auf `m_work`
// festgelegt, ihre Werte liegen ununterscheidbar hintereinander - genau das macht den Übergang lückenlos.
void AudioEngine::onBufferReady() {
    if (!feedFrom(m_decoder, m_pending, m_pendingAt)) return;
    if (!m_decodeDone) return;
    if (m_decoder && m_decoder->bufferAvailable()) return;

    if (m_boundaryFrames < 0) {
        if (m_nextPath.isEmpty()) return;              // nichts angemeldet
        m_boundaryFrames = m_framesIn;
        m_queuedPath = m_nextPath;
        m_queuedDurationMs = 0;
        m_nextPath.clear();
        startNextDecoder();
        if (kLog) qDebug("audio: Naht bei %lld Frames -> %s",
                         m_boundaryFrames, qPrintable(m_queuedPath));
    }
    feedFrom(m_nextDec, m_nextPending, m_nextPendingAt);
}

bool AudioEngine::feedFrom(QAudioDecoder* dec, std::vector<float>& pending, size_t& at) {
    if (!dec) return true;
    const int chOut = std::max(1, m_work.channelCount());

    //  EINE Stelle, die in den Ring schreibt - und die den REST behält. Früher
    //  wurde der Rückgabewert von `write` verworfen: beim vollen Ring fiel der
    //  überzählige Teil jedes Stücks weg, und genau das rauschte.
    const auto push = [&](const float* src, qint64 values) -> qint64 {
        const size_t took = m_ring.write(src, size_t(values));
        m_framesIn += qint64(took) / chOut;
        if (took > 0 && m_pull) m_pull->notifyData();
        return values - qint64(took);
    };

    if (at < pending.size()) {
        const qint64 left = qint64(pending.size() - at);
        const qint64 rest = push(pending.data() + at, left);
        at = pending.size() - size_t(rest);
        if (rest > 0) return false;                     // Ring immer noch voll
        pending.clear();
        at = 0;
    }

    while (dec->bufferAvailable()) {
        if (m_ring.space() == 0) { startSinkIfReady(); return false; }
        const QAudioBuffer buf = dec->read();
        if (!buf.isValid()) continue;

        const int ch = std::max(1, buf.format().channelCount());
        //  Der Dekoder BEKAM `m_work` (Float) vorgegeben; liefert er trotzdem
        //  etwas anderes, wären die Bytes als Float gelesen reines Rauschen.
        if (buf.format().sampleFormat() != QAudioFormat::Float) {
            if (kLog) qDebug("audio: Dekoder liefert Format %d statt Float - verworfen",
                             int(buf.format().sampleFormat()));
            continue;
        }
        const float* src = buf.constData<float>();
        qint64 frames = buf.frameCount();
        if (!src || frames <= 0) continue;

        if (dec == m_decoder && m_skipFrames > 0) {
            const qint64 drop = std::min(m_skipFrames, frames);
            m_skipFrames -= drop;
            src    += drop * ch;
            frames -= drop;
            if (frames <= 0) continue;
        }

        const qint64 values = frames * ch;
        const qint64 rest = push(src, values);
        if (kLog) qDebug("audio: dekodiert %lld Frames, Ring %zu/%zu, Rest %lld",
                         frames, m_ring.available(), m_ring.capacity(), rest);
        if (rest > 0) {
            pending.assign(src + (values - rest), src + values);
            at = 0;
            startSinkIfReady();
            return false;
        }
        startSinkIfReady();
    }
    return true;
}

//  Der zweite Dekoder. Er hängt an DERSELBEN Kette; nur sein Ende und seine
//  Fehler werden getrennt behandelt - ein defekter Folgetitel darf den
//  laufenden nicht abwürgen.
void AudioEngine::startNextDecoder() {
    if (m_nextDec || m_queuedPath.isEmpty()) return;
    m_nextDecodeDone = false;
    m_nextPending.clear();
    m_nextPendingAt = 0;

    m_nextDec = new QAudioDecoder(this);
    m_nextDec->setAudioFormat(m_work);
    m_nextDec->setSource(QUrl::fromLocalFile(m_queuedPath));
    connect(m_nextDec, &QAudioDecoder::bufferReady, this, &AudioEngine::onBufferReady);
    // ACHTUNG: dieser Dekoder wird im Lauf zum laufenden (`promoteNext`), seine Meldungen kommen oft erst danach.
    // Deshalb entscheidet der Ruf selbst, wer gemeint ist (gemessen: 4694 Unterläufe und kein `finished`).
    connect(m_nextDec, &QAudioDecoder::finished, this, [this, dec = m_nextDec] {
        if (dec == m_decoder) onDecodeFinished();      // schon befördert
        else                  m_nextDecodeDone = true;
    });
    connect(m_nextDec, &QAudioDecoder::durationChanged, this,
            [this, dec = m_nextDec](qint64 ms) {
        if (ms <= 0) return;
        // Dieselbe Falle wie bei `finished`: nach der Beförderung ist es die Dauer des LAUFENDEN Titels. Meldet der
        // Dekoder sie erst danach, stünde sonst weiter die Dauer des vorigen Stücks in der Leiste.
        if (dec == m_decoder) {
            if (ms != m_durationMs) { m_durationMs = ms; emit durationChanged(); }
        } else {
            m_queuedDurationMs = ms;
        }
    });
    connect(m_nextDec, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
            this, [this, dec = m_nextDec](QAudioDecoder::Error) {
        const QString msg = dec ? dec->errorString() : QString();
        if (dec == m_decoder) {
            emit error(msg);
            teardown();
            setState(State::Stopped);
            return;
        }
        // Hat er noch nichts beigetragen, wird der Übergang verworfen und der laufende Titel endet wie ohne Anmeldung.
        // Steckt schon etwas von ihm im Ring, bleibt die Naht bestehen; das Bruchstück läuft zu Ende.
        if (m_framesIn <= m_boundaryFrames) {
            clearNext();
        } else {
            m_nextDecodeDone = true;
        }
        emit error(msg);
    });
    m_nextDec->start();
}

void AudioEngine::setNextTrack(const QString& path) {
    const QString local = path.startsWith(QStringLiteral("file:"))
                          ? QUrl(path).toLocalFile() : path;
    //  Steht der Übergang schon fest, gilt die Anmeldung dem ÜBERNÄCHSTEN
    //  Titel - sie geht also nicht verloren, sondern wartet auf die nächste
    //  Naht (s. `promoteNext`).
    if (m_nextPath == local) return;
    m_nextPath = local;
}

void AudioEngine::clearNext() {
    // Der WUNSCH bleibt bestehen, nur die Vorbereitung fällt weg: nach einem Sprung wird derselbe Titel gleich
    // wieder vorbereitet. `play` löscht ihn ausdrücklich - sonst hinge der alte Nachfolger am neuen Stück.
    if (m_nextPath.isEmpty() && !m_queuedPath.isEmpty()) m_nextPath = m_queuedPath;
    if (m_nextDec) {
        m_nextDec->stop();
        m_nextDec->deleteLater();
        m_nextDec = nullptr;
    }
    m_queuedPath.clear();
    m_queuedDurationMs = 0;
    m_boundaryFrames = -1;
    m_nextDecodeDone = false;
    m_nextPending.clear();
    m_nextPendingAt = 0;
}

// Die Ausgabe hat die Naht erreicht: der vorbereitete Titel IST jetzt der laufende. Senke, Ring und Equalizer
// bleiben unangetastet, es wechseln nur Buchhaltung und Dekoder.
void AudioEngine::promoteNext() {
    if (m_queuedPath.isEmpty()) return;

    if (m_decoder) {                       // der alte ist fertig und geht
        m_decoder->stop();
        m_decoder->deleteLater();
    }
    m_decoder    = m_nextDec;
    m_nextDec    = nullptr;
    m_decodeDone = m_nextDecodeDone;
    m_pending    = std::move(m_nextPending);
    m_pendingAt  = m_nextPendingAt;
    m_nextPending.clear();
    m_nextPendingAt = 0;
    m_nextDecodeDone = false;

    m_path        = m_queuedPath;
    m_durationMs  = m_queuedDurationMs;
    m_frameOrigin = m_boundaryFrames;      // ab hier zählt die Position neu
    m_baseFrames  = 0;
    m_queuedPath.clear();
    m_queuedDurationMs = 0;
    m_boundaryFrames = -1;

    if (kLog) qDebug("audio: Übergang vollzogen -> %s", qPrintable(m_path));
    emit currentPathChanged();
    emit durationChanged();
    emit positionChanged();
    emit advancedToNext(m_path);
}

void AudioEngine::onDecodeFinished() {
    m_decodeDone = true;
    startSinkIfReady();          // sehr kurze Datei: nie 60 ms Vorrat erreicht
    //  Jetzt ist der Platz hinter dem laufenden Titel bekannt - der angemeldete
    //  nächste kann anfangen, dahinter zu schreiben (`onBufferReady` setzt die
    //  Naht, sobald wirklich alles im Ring liegt).
    onBufferReady();
}

void AudioEngine::pause() {
    if (m_state != State::Playing) return;
    //  Auch gültig, BEVOR die Senke steht (kurzer Moment nach dem Start): der
    //  Zustand zählt, `startSinkIfReady` legt dann nicht los.
    if (m_sink) m_sink->suspend();
    setState(State::Paused);
}

void AudioEngine::resume() {
    if (m_state != State::Paused) return;
    setState(State::Playing);
    if (m_sink) m_sink->resume();
    else        startSinkIfReady();
}

void AudioEngine::stop() {
    if (m_state == State::Stopped) return;
    teardown();
    setState(State::Stopped);
    emit positionChanged();
}

void AudioEngine::forgetCurrent() {
    if (m_path.isEmpty() && m_durationMs == 0) return;
    m_path.clear();
    m_durationMs = 0;
    m_baseFrames = 0;
    m_framesOut.store(0, std::memory_order_relaxed);
    emit currentPathChanged();
    emit durationChanged();
    emit positionChanged();
}

void AudioEngine::seek(qint64 ms) {
    if (m_path.isEmpty()) return;
    const qint64 target = std::max<qint64>(0, ms);
    const QString path = m_path;

    // `QAudioDecoder` kann nicht springen: ohne Hilfe bleibt nur, von vorn zu dekodieren und alles davor
    // wegzuwerfen - 2813 ms bei 45 min. Selbstrahmende Ströme (MP3/AC-3/AAC) laufen stattdessen die Rahmenköpfe
    // entlang (3 ms auf 45 min); Hüllen tragen ihren Index anderswo.
    qint64 byteOffset = 0;
    qint64 skipMs     = target;
    //  Diagnose (nur mit `MG_AUDIOLOG=1`): WELCHEN Weg nimmt der Sprung, und
    //  was kostet er? Ohne die Variable kostet die Zeile nichts.
    const bool logSeek = qEnvironmentVariableIntValue("MG_AUDIOLOG") >= 1;
    QElapsedTimer seekTimer;
    if (logSeek) seekTimer.start();
    qint64 findMs = 0;

    QIODevice* stream = nullptr;
    if (target > 0 && AudioSeek::isSelfFraming(path)) {
        const AudioSeek::Position p = AudioSeek::findFrame(path, target);
        if (logSeek) findMs = seekTimer.elapsed();
        if (p.ok && p.byteOffset > 0 && p.ms() <= target) {
            byteOffset = p.byteOffset;
            skipMs     = target - p.ms();       // Rest bis zur genauen Stelle
        }
    } else if (target > 0 && MkvAudio::isCandidate(path)) {
        //  Hülle: ein Stück aus der Mitte einer Matroska ist für sich nicht
        //  lesbar. Der Leser holt die Tonspur ab dem Cluster der Zielstelle als
        //  rohen Strom heraus - genau das kann ein Dekoder aufnehmen.
        qint64 actualMs = 0;
        stream = MkvAudio::openRawStream(path, 0, target, &actualMs, nullptr);
        if (logSeek) findMs = seekTimer.elapsed();
        if (stream) skipMs = std::max<qint64>(0, target - actualMs);
    }
    if (logSeek)
        qInfo("[MG_AUDIOLOG] Sprung auf %lld ms in %s: %s (Suche %lld ms, "
              "Byte %lld, Rest %lld ms)",
              target, qPrintable(QFileInfo(path).fileName()),
              stream       ? "SCHNELL (Hülle, Strom ab Cluster)"
              : byteOffset > 0 ? "SCHNELL (Rahmenkopf)"
                               : "langsam (von vorn dekodieren)",
              findMs, byteOffset, skipMs);

    teardown();
    m_baseFrames = m_format.isValid() ? m_format.framesForDuration(target * 1000) : 0;
    m_pendingStream = stream;              // s. startDecode
    startDecode(path, skipMs, byteOffset);
    emit positionChanged();
}

void AudioEngine::setVolume(qreal v) {
    const qreal nv = std::clamp<qreal>(v, 0.0, 1.0);
    if (qFuzzyCompare(m_volume + 1.0, nv + 1.0)) return;
    m_volume = nv;
    if (m_sink) m_sink->setVolume(float(m_volume));
    emit volumeChanged();
}

//  Was die Ausgabe jetzt holen könnte. Bei Unterlauf wird bewusst NICHT 0
//  gemeldet: der Ruf füllt dann mit Stille auf: fiele die Senke stattdessen in
//  den Leerlauf, bliebe die Wiedergabe bei der ersten Verzögerung stehen.
qint64 AudioEngine::pullBytesAvailable() const {
    if (m_state == State::Stopped || !m_format.isValid()) return 0;
    const int bps = m_format.bytesPerSample() > 0 ? m_format.bytesPerSample() : 4;
    //  Mindestens 20 ms, damit auch der erste Ruf vor dem ersten Dekodier-Stück
    //  zustande kommt.
    const qint64 floorVals = qint64(m_format.sampleRate()) * m_format.channelCount() / 50;
    return std::max<qint64>(qint64(m_ring.available()), floorVals) * bps;
}

qint64 AudioEngine::pullAudio(char* data, qint64 maxSize) {
    if (!data || maxSize <= 0) return 0;
    const int ch  = std::max(1, m_format.channelCount());
    const int bpf = m_format.bytesPerFrame() > 0 ? m_format.bytesPerFrame() : ch * 4;
    const int bps = m_format.bytesPerSample() > 0 ? m_format.bytesPerSample() : 4;
    qint64 wanted = (maxSize / bpf) * ch;              // Einzelwerte, frame-genau
    if (wanted <= 0) return 0;
    if (wanted > qint64(m_convBuf.size())) wanted = (qint64(m_convBuf.size()) / ch) * ch;

    //  Bei einer Float-Senke wird IM Zielpuffer gerechnet (kein Umweg), sonst
    //  im Zwischenpuffer und danach gewandelt.
    const bool direct = m_format.sampleFormat() == QAudioFormat::Float;
    float* work = direct ? reinterpret_cast<float*>(data) : m_convBuf.data();

    const qint64 got = qint64(m_ring.read(work, size_t(wanted)));
    if (kLog) qDebug("audio: Ausgabe will %lld, bekommt %lld", wanted, got);

    // KEINE Stille auffüllen: die Senke holt beim Start ihren GANZEN Puffer auf einmal, dadurch standen 200-250 ms
    // Stille vor dem ersten Ton (gemessen). Nichts zu liefern heißt 0 - aus dem Leerlauf holt `readyRead()` sie zurück.
    if (got <= 0) {
        if (m_state == State::Playing) m_underruns.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    m_eq.process(work, int(got / ch));
    if (!direct) convertOut(work, data, got);
    m_framesOut.fetch_add(got / ch, std::memory_order_relaxed);
    if (m_dump) m_dump->write(data, got * bps);
    return got * bps;
}

//  Float -> Zielformat. Geklemmt wird HIER (nicht im Equalizer): eine Anhebung
//  darf den Wertebereich der Senke nicht verlassen, sonst knackt es.
void AudioEngine::convertOut(const float* in, char* out, qint64 values) const {
    const auto clamp1 = [](float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); };
    switch (m_format.sampleFormat()) {
    case QAudioFormat::Int16: {
        auto* d = reinterpret_cast<qint16*>(out);
        for (qint64 i = 0; i < values; ++i) d[i] = qint16(clamp1(in[i]) * 32767.0f);
        break;
    }
    case QAudioFormat::Int32: {
        auto* d = reinterpret_cast<qint32*>(out);
        for (qint64 i = 0; i < values; ++i)
            d[i] = qint32(double(clamp1(in[i])) * 2147483647.0);
        break;
    }
    case QAudioFormat::UInt8: {
        auto* d = reinterpret_cast<quint8*>(out);
        for (qint64 i = 0; i < values; ++i)
            d[i] = quint8(std::lround(clamp1(in[i]) * 127.0f) + 128);
        break;
    }
    default:
        std::memcpy(out, in, size_t(values) * sizeof(float));
        break;
    }
}
