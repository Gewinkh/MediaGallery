#include "audio/AudioController.h"

#include "core/ISettings.h"

#include <QDebug>
#include <QFileInfo>

namespace {
//  Diagnose wie in `AudioEngine` (MG_LOG_AUDIO=1): zeigt, WANN die Liste gesetzt
//  wird und was das Wiederherstellen daraus macht. Die Reihenfolge beim Start
//  war zweimal die Ursache eines Fehlers - sie muss nachvollziehbar sein.
const bool kLogC = qEnvironmentVariableIntValue("MG_LOG_AUDIO") == 1;
}  // namespace

namespace {
//  Eingebaute Klangbilder. Format wie die eigenen: Name, Preamp, zehn Bänder.
//  Bewusst zurückhaltend gewählt - ein Preset soll eine Richtung geben, nicht
//  den Titel verbiegen.
const char* const kBuiltins[] = {
    "Flach\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0",
    "Bass\t-4\t7\t6\t4\t2\t0\t0\t0\t0\t0\t0",
    "Stimme\t-2\t-3\t-2\t0\t2\t4\t4\t2\t0\t-1\t-2",
    "Klassik\t-2\t3\t2\t1\t0\t0\t0\t1\t2\t3\t3",
    "Elektronisch\t-4\t6\t5\t2\t0\t-2\t0\t2\t4\t5\t5",
};
}  // namespace

AudioController::AudioController(ISettings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_eq(this)
    , m_engine(m_eq, this)
    , m_queue(this)
{
    //  Weiterreichen, was QML sehen will.
    connect(&m_engine, &AudioEngine::stateChanged,    this, &AudioController::stateChanged);
    connect(&m_engine, &AudioEngine::currentPathChanged, this, &AudioController::currentChanged);
    connect(&m_engine, &AudioEngine::positionChanged, this, &AudioController::positionChanged);
    connect(&m_engine, &AudioEngine::durationChanged, this, &AudioController::durationChanged);
    connect(&m_engine, &AudioEngine::volumeChanged,   this, &AudioController::volumeChanged);
    connect(&m_engine, &AudioEngine::error, this, [this](const QString& msg) {
        emit message(msg);
    });
    connect(&m_eq,    &AudioEqualizer::changed, this, &AudioController::eqChanged);
    connect(&m_queue, &PlayQueue::currentChanged, this, &AudioController::currentChanged);
    connect(&m_queue, &PlayQueue::itemsChanged,   this, &AudioController::queueChanged);

    //  Der Titel ist NATÜRLICH zu Ende - jetzt entscheidet die Warteschlange.
    connect(&m_engine, &AudioEngine::finished, this, [this] {
        const QString next = m_queue.advance(/*natural=*/true);
        if (next.isEmpty()) return;                  // Ende der Liste
        m_engine.play(next);
    });

    loadSettings();
}

void AudioController::loadSettings() {
    m_eq.setEnabled(m_settings.audioEqEnabled());
    QVector<double> g;
    for (double v : m_settings.audioEqBands()) g.append(v);
    m_eq.setGains(g);
    m_eq.setPreamp(m_settings.audioEqPreamp());
    m_engine.setVolume(m_settings.audioVolume());
    m_queue.setShuffle(m_settings.audioShuffle());
    m_queue.setRepeat(static_cast<PlayQueue::Repeat>(m_settings.audioRepeat()));
}

void AudioController::applyGainsToSettings() {
    QList<double> out;
    for (double v : m_eq.gains()) out.append(v);
    m_settings.setAudioEqBands(out);
    m_settings.setAudioEqPreamp(m_eq.preamp());
    m_settings.setAudioEqEnabled(m_eq.enabled());
}

// ── Wiedergabe ───────────────────────────────────────────────────────────────
void AudioController::playFile(const QString& path, const QStringList& queue) {
    if (path.isEmpty()) return;
    m_queue.setItems(queue.isEmpty() ? QStringList { path } : queue);
    if (!m_queue.startAt(path)) m_queue.setItems(QStringList { path });
    m_pendingSeek = 0;
    m_engine.play(m_queue.currentPath().isEmpty() ? path : m_queue.currentPath());
}

void AudioController::setQueue(const QStringList& queue) {
    if (kLogC) qDebug("audio: setQueue(%lld) - vorher %lld, aktuell %s",
                      qint64(queue.size()), qint64(m_queue.items().size()),
                      qPrintable(currentPath()));
    m_queue.setItems(queue);
}

void AudioController::playAt(int index) {
    //  `index` ist der Platz in der ANGEZEIGTEN Folge (s. `queue`).
    const QString path = m_queue.pathAtOrder(index);
    if (path.isEmpty() || !m_queue.startAtOrder(index)) return;
    m_pendingSeek = 0;
    m_engine.play(path);
}

void AudioController::setOwner(QObject* o) {
    if (m_owner == o) return;
    m_owner = o;
    emit ownerChanged();
}

void AudioController::togglePlay() {
    switch (m_engine.state()) {
    case AudioEngine::State::Playing: m_engine.pause();  break;
    case AudioEngine::State::Paused:  m_engine.resume(); break;
    case AudioEngine::State::Stopped: {
        //  Nach dem Wiederherstellen liegt ein Titel bereit, aber nichts läuft.
        const QString p = m_queue.currentPath();
        if (p.isEmpty()) return;
        m_engine.play(p);
        if (m_pendingSeek > 0) {
            const qint64 to = m_pendingSeek;
            m_pendingSeek = 0;
            m_engine.seek(to);
        }
        break;
    }
    }
}

void AudioController::next() {
    //  Weitergeschaltet ⇒ NICHT natürlich: „eine wiederholen" gilt hier nicht.
    const QString p = m_queue.advance(/*natural=*/false);
    if (p.isEmpty()) { m_engine.stop(); return; }
    m_engine.play(p);
}

void AudioController::previous() {
    const QString p = m_queue.back();
    if (p.isEmpty()) return;
    m_engine.play(p);
}

void AudioController::stop() { m_engine.stop(); }

void AudioController::stopAndClear() {
    //  Der Ordner ist gewechselt: der Titel gehört nicht mehr hierher. Nur
    //  anzuhalten reichte nicht - die Leiste stand weiter da und zeigte einen
    //  Titel, der zu keinem sichtbaren Ordner mehr gehörte (Nutzerbefund).
    m_engine.stop();
    m_engine.forgetCurrent();       // sonst zeigte die Leiste den alten Titel weiter
    m_queue.setItems(QStringList {});
    m_pendingSeek = 0;
    emit currentChanged();
    emit queueChanged();
}
void AudioController::seek(qint64 ms) { m_engine.seek(ms); }

void AudioController::setVolume(qreal v) {
    m_engine.setVolume(v);
    m_settings.setAudioVolume(m_engine.volume());
}

void AudioController::setShuffle(bool on) {
    if (m_queue.shuffle() == on) return;
    m_queue.setShuffle(on);
    m_settings.setAudioShuffle(on);
    emit shuffleChanged();
    emit queueChanged();          // die Folge ist neu gemischt
    emit currentChanged();
}

void AudioController::setRepeat(int r) {
    if (m_queue.repeatInt() == r) return;
    m_queue.setRepeatInt(r);
    m_settings.setAudioRepeat(r);
    emit repeatChanged();
}

// ── Equalizer ────────────────────────────────────────────────────────────────
void AudioController::setEqEnabled(bool on) {
    m_eq.setEnabled(on);
    m_settings.setAudioEqEnabled(on);
}

QVariantList AudioController::eqGains() const {
    QVariantList out;
    for (double v : m_eq.gains()) out.append(v);
    return out;
}

QVariantList AudioController::eqFrequencies() const {
    QVariantList out;
    for (double f : AudioEqualizer::frequencies()) out.append(f);
    return out;
}

void AudioController::setBandGain(int band, qreal db) {
    m_eq.setBandGain(band, db);
    applyGainsToSettings();
}

void AudioController::resetBands() {
    m_eq.setGains(QVector<double>(AudioEqualizer::kBands, 0.0));
    m_eq.setPreamp(0.0);
    applyGainsToSettings();
}

void AudioController::setEqPreamp(qreal db) {
    m_eq.setPreamp(db);
    applyGainsToSettings();
}

QStringList AudioController::builtinPresetLines() const {
    QStringList out;
    for (const char* line : kBuiltins) out.append(QString::fromUtf8(line));
    return out;
}

QStringList AudioController::presetNames() const {
    QStringList out;
    for (const QString& line : builtinPresetLines())
        out.append(line.section(QLatin1Char('\t'), 0, 0));
    for (const QString& line : m_settings.audioEqPresets()) {
        const QString name = line.section(QLatin1Char('\t'), 0, 0).trimmed();
        if (!name.isEmpty() && !out.contains(name)) out.append(name);
    }
    return out;
}

void AudioController::applyPreset(const QString& name) {
    QStringList all = builtinPresetLines();
    all += m_settings.audioEqPresets();
    for (const QString& line : all) {
        if (line.section(QLatin1Char('\t'), 0, 0).trimmed() != name) continue;
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() < 2 + AudioEqualizer::kBands) continue;
        m_eq.setPreamp(parts.at(1).toDouble());
        QVector<double> g;
        for (int i = 0; i < AudioEqualizer::kBands; ++i)
            g.append(parts.at(2 + i).toDouble());
        m_eq.setGains(g);
        applyGainsToSettings();
        return;
    }
}

void AudioController::savePreset(const QString& name) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    //  Ein eingebauter Name lässt sich nicht überschreiben - sonst wäre „Flach"
    //  irgendwann nicht mehr flach.
    for (const QString& line : builtinPresetLines())
        if (line.section(QLatin1Char('\t'), 0, 0).compare(n, Qt::CaseInsensitive) == 0)
            return;

    QStringList fields { n, QString::number(m_eq.preamp(), 'f', 2) };
    for (double v : m_eq.gains()) fields.append(QString::number(v, 'f', 2));
    const QString line = fields.join(QLatin1Char('\t'));

    QStringList own = m_settings.audioEqPresets();
    bool replaced = false;
    for (QString& existing : own) {
        if (existing.section(QLatin1Char('\t'), 0, 0).compare(n, Qt::CaseInsensitive) != 0)
            continue;
        existing = line;
        replaced = true;
        break;
    }
    if (!replaced) own.append(line);
    m_settings.setAudioEqPresets(own);
    emit presetsChanged();
}

void AudioController::deletePreset(const QString& name) {
    QStringList own = m_settings.audioEqPresets();
    const int before = own.size();
    for (int i = own.size() - 1; i >= 0; --i)
        if (own.at(i).section(QLatin1Char('\t'), 0, 0).compare(name, Qt::CaseInsensitive) == 0)
            own.removeAt(i);
    if (own.size() == before) return;
    m_settings.setAudioEqPresets(own);
    emit presetsChanged();
}

// ── Optionen ─────────────────────────────────────────────────────────────────
bool AudioController::showVideos() const { return m_settings.audioShowVideos(); }
void AudioController::setShowVideos(bool on) {
    if (m_settings.audioShowVideos() == on) return;
    m_settings.setAudioShowVideos(on);
    emit optionsChanged();
}
bool AudioController::playerModeRemembered() const { return m_settings.audioPlayerMode(); }
void AudioController::setPlayerModeRemembered(bool on) {
    if (m_settings.audioPlayerMode() == on) return;
    m_settings.setAudioPlayerMode(on);
    emit optionsChanged();
}
bool AudioController::listLayout() const { return m_settings.audioListLayout(); }
void AudioController::setListLayout(bool on) {
    if (m_settings.audioListLayout() == on) return;
    m_settings.setAudioListLayout(on);
    emit optionsChanged();
}
bool AudioController::rememberLast() const { return m_settings.audioRememberLast(); }
void AudioController::setRememberLast(bool on) {
    if (m_settings.audioRememberLast() == on) return;
    m_settings.setAudioRememberLast(on);
    if (!on) {                       // ausgeschaltet ⇒ nichts Gemerktes behalten
        m_settings.setAudioLastFile(QString());
        m_settings.setAudioLastPosition(0);
    }
    emit optionsChanged();
}

bool AudioController::takePlayerModeRestore() {
    if (m_restoreTaken) return false;
    m_restoreTaken = true;
    return m_settings.audioPlayerMode();
}

// ── Sitzung ──────────────────────────────────────────────────────────────────
void AudioController::rememberSession() {
    if (!m_settings.audioRememberLast()) return;
    m_settings.setAudioLastFile(m_engine.currentPath());
    m_settings.setAudioLastPosition(m_engine.position());
}

void AudioController::restoreSession() {
    if (!m_settings.audioRememberLast()) return;
    const QString last = m_settings.audioLastFile();
    if (last.isEmpty() || !QFileInfo::exists(last)) return;
    //  NUR bereitlegen: es wird nicht von selbst gespielt (Festlegung des
    //  Nutzers). Der erste Druck auf ⏯ nimmt Titel und Stelle auf.
    //
    //  Steht die Liste des Ordners schon (die Hälfte reicht sie beim Aufnehmen
    //  des Player-Modus nach), wird sie NICHT ersetzt - sonst bliebe nach dem
    //  Start genau ein Titel übrig und die Wiedergabe wäre nach ihm zu Ende
    //  (Nutzerbefund). Wer zuerst kommt, gibt die Liste vor; der gemerkte Titel
    //  wird nur eingestellt.
    if (kLogC) qDebug("audio: restoreSession(%s) - Liste hat %lld Einträge",
                      qPrintable(last), qint64(m_queue.items().size()));
    if (!m_queue.items().contains(last))
        m_queue.setItems(QStringList { last });
    m_queue.startAt(last);
    m_pendingSeek = m_settings.audioLastPosition();
    emit currentChanged();
}

QString AudioController::formatTime(qint64 ms) const {
    if (ms < 0) ms = 0;
    const qint64 total = ms / 1000;
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}
