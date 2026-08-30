#include "audio/AudioController.h"

#include "audio/AudioCoverProvider.h"
#include "audio/Mp4AudioExtract.h"
#include "audio/MkvAudioExtract.h"
#include "core/ISettings.h"
#include "core/PathUtils.h"
#include "core/Strings.h"

#include <QDebug>
#include <QFileInfo>
#include <QPointer>
#include <QRunnable>

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
        //  Kein lückenloser Übergang möglich gewesen (nichts angemeldet, oder
        //  Zufall am Listenende): der gewohnte Weg mit kurzem Absetzen.
        const QString next = m_queue.advance(/*natural=*/true);
        if (next.isEmpty()) return;                  // Ende der Liste
        m_engine.play(next);
    });

    //  Der lückenlose Übergang hat schon stattgefunden - die Warteschlange zieht
    //  nur noch ihren Zeiger nach und meldet den ÜBERNÄCHSTEN Titel an.
    connect(&m_engine, &AudioEngine::advancedToNext, this, [this](const QString& path) {
        const QString moved = m_queue.advance(/*natural=*/true);
        //  Sicherheitsnetz: liefe die Warteschlange auseinander (Filterwechsel
        //  während des Übergangs), zählt WAS SPIELT - der Zeiger folgt der
        //  Wiedergabe, nicht umgekehrt.
        if (moved != path) m_queue.startAt(path);
        armNextTrack();
    });

    //  Wechselt der Titel, gelten andere Tags.
    connect(this, &AudioController::currentChanged, this, &AudioController::refreshTags);

    //  Ändert sich die Liste oder die Reihenfolge, ändert sich auch, was folgt.
    connect(&m_queue, &PlayQueue::itemsChanged,   this, &AudioController::armNextTrack);
    connect(&m_queue, &PlayQueue::currentChanged, this, &AudioController::armNextTrack);

    loadSettings();
}

AudioController::~AudioController() {
    if (m_extractCancel) m_extractCancel->store(true, std::memory_order_relaxed);
    m_extractPool.clear();               // noch nicht begonnene Aufträge weg
    m_extractPool.waitForDone(3000);     // der laufende bricht selbst ab
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
    armNextTrack();
}

//  Was nach dem laufenden Titel kommt, der Kette anmelden - sie hängt es dann
//  ohne Absetzen hinter den laufenden. Wird bei JEDER Änderung neu gerufen
//  (Titelwechsel, Liste, Zufall, Wiederholung), weil sich damit auch der
//  Nachfolger ändert.
void AudioController::armNextTrack() {
    m_engine.setNextTrack(m_queue.peekNext(/*natural=*/true));
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

//  Jeder Griff an einen Regler hebt die Voreinstellung auf: danach passt kein
//  Name mehr zu dem, was eingestellt ist.
//  Die Gegenrechnung gegen das Uebersteuern.
//
//  Ohne sie klemmt `AudioEqualizer::process` am Ausgang hart - gemessen bei
//  -1 dBFS Eingang: EIN Band auf +12 dB laesst 1,4 % der Werte am Anschlag
//  haengen, drei angehobene 3,9 %, alle zehn 66,5 %, bei 13-35 % Klirr. Ein
//  weicher Begrenzer half nachweislich nicht (12,4 % statt 13,4 % Klirr);
//  was hilft, ist Luft nach oben zu schaffen.
//
//  Ist die Automatik AUS, wird NICHTS gerechnet - `peakGainDb` laeuft dann gar
//  nicht erst an, und der Regler gehoert ganz dem Nutzer (Festlegung des
//  Nutzers: wer uebersteuern will, soll das duerfen).
bool AudioController::eqAutoPreamp() const { return m_settings.audioEqAutoPreamp(); }

void AudioController::applyAutoPreamp() {
    if (!m_settings.audioEqAutoPreamp()) return;
    m_eq.setPreamp(m_eq.suggestedPreamp());
}

void AudioController::setEqAutoPreamp(bool on) {
    if (m_settings.audioEqAutoPreamp() == on) return;
    m_settings.setAudioEqAutoPreamp(on);
    //  Beim Einschalten sofort wirken lassen; beim Ausschalten bleibt der
    //  zuletzt errechnete Wert stehen - er ist ein gueltiger Startpunkt, und
    //  ihn ungefragt auf 0 zu reissen waere ein Lautstaerkesprung.
    if (on) { applyAutoPreamp(); applyGainsToSettings(); }
    emit optionsChanged();
    emit eqChanged();
}

void AudioController::setBandGain(int band, qreal db) {
    m_eq.setBandGain(band, db);
    applyAutoPreamp();
    if (!m_activePreset.isEmpty()) { m_activePreset.clear(); emit presetsChanged(); }
    applyGainsToSettings();
}

void AudioController::resetBands() {
    m_eq.setGains(QVector<double>(AudioEqualizer::kBands, 0.0));
    m_eq.setPreamp(0.0);
    if (!m_activePreset.isEmpty()) { m_activePreset.clear(); emit presetsChanged(); }
    applyGainsToSettings();
}

void AudioController::setEqPreamp(qreal db) {
    m_eq.setPreamp(db);
    if (!m_activePreset.isEmpty()) { m_activePreset.clear(); emit presetsChanged(); }
    applyGainsToSettings();
}

QStringList AudioController::builtinPresetLines() const {
    QStringList out;
    for (const char* line : kBuiltins) out.append(QString::fromUtf8(line));
    return out;
}

//  Die Zeile einer Voreinstellung. EIGENE gewinnen ueber gleichnamige
//  mitgelieferte - genau daran haengt das Ueberschreiben.
QString AudioController::presetLine(const QString& name) const {
    for (const QString& line : m_settings.audioEqPresets())
        if (line.section(QLatin1Char('\t'), 0, 0).trimmed()
                .compare(name, Qt::CaseInsensitive) == 0)
            return line;
    if (m_settings.audioEqHiddenPresets().contains(name, Qt::CaseInsensitive))
        return {};                                  // geloescht
    for (const QString& line : builtinPresetLines())
        if (line.section(QLatin1Char('\t'), 0, 0).compare(name, Qt::CaseInsensitive) == 0)
            return line;
    return {};
}

bool AudioController::presetIsBuiltin(const QString& name) const {
    for (const QString& line : builtinPresetLines())
        if (line.section(QLatin1Char('\t'), 0, 0).compare(name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

bool AudioController::presetIsModified(const QString& name) const {
    if (!presetIsBuiltin(name)) return false;
    for (const QString& line : m_settings.audioEqPresets())
        if (line.section(QLatin1Char('\t'), 0, 0).trimmed()
                .compare(name, Qt::CaseInsensitive) == 0)
            return true;                            // ueberschrieben
    return false;
}

bool AudioController::presetsModified() const {
    if (!m_settings.audioEqHiddenPresets().isEmpty()) return true;
    for (const QString& line : builtinPresetLines())
        if (presetIsModified(line.section(QLatin1Char('\t'), 0, 0))) return true;
    return false;
}

//  Namen in die gespeicherte Reihenfolge bringen. Was dort NICHT steht, haengt
//  sich hinten an - eine spaeter dazugekommene mitgelieferte Voreinstellung
//  bleibt damit sichtbar, statt an einer Indexluecke zu verschwinden.
QStringList AudioController::inStoredOrder(const QStringList& names) const {
    const QStringList order = m_settings.audioEqPresetOrder();
    if (order.isEmpty()) return names;
    QStringList out;
    for (const QString& want : order)
        for (const QString& have : names)
            if (have.compare(want, Qt::CaseInsensitive) == 0 && !out.contains(have)) {
                out.append(have);
                break;
            }
    for (const QString& have : names)
        if (!out.contains(have)) out.append(have);
    return out;
}

QStringList AudioController::presetNames() const {
    QStringList out;
    const QStringList hidden = m_settings.audioEqHiddenPresets();
    //  Mitgelieferte zuerst (in ihrer Programm-Reihenfolge), aber ohne die
    //  geloeschten; eine ueberschriebene bleibt an ihrem Platz stehen.
    for (const QString& line : builtinPresetLines()) {
        const QString n = line.section(QLatin1Char('\t'), 0, 0);
        if (hidden.contains(n, Qt::CaseInsensitive)) continue;
        out.append(n);
    }
    //  Danach die eigenen - eine, die eine mitgelieferte ueberschreibt, steht
    //  schon oben und kommt nicht doppelt.
    for (const QString& line : m_settings.audioEqPresets()) {
        const QString n = line.section(QLatin1Char('\t'), 0, 0).trimmed();
        if (n.isEmpty() || out.contains(n, Qt::CaseInsensitive)) continue;
        out.append(n);
    }
    return inStoredOrder(out);
}

void AudioController::applyPreset(const QString& name) {
    const QString line = presetLine(name);
    if (line.isEmpty()) return;
    const QStringList parts = line.split(QLatin1Char('\t'));
    if (parts.size() < 2 + AudioEqualizer::kBands) return;
    m_eq.setPreamp(parts.at(1).toDouble());
    QVector<double> g;
    for (int i = 0; i < AudioEqualizer::kBands; ++i)
        g.append(parts.at(2 + i).toDouble());
    m_eq.setGains(g);
    //  Die Voreinstellung bringt ihre eigene Vorverstaerkung mit. Reicht sie
    //  nicht, korrigiert die Automatik sie nach oben bzw. unten.
    applyAutoPreamp();
    applyGainsToSettings();
    m_activePreset = name;
    emit presetsChanged();
}

//  Sichern - AUCH auf einen mitgelieferten Namen. Frueher wurde das abgewiesen
//  („sonst waere Flach irgendwann nicht mehr flach"); der Nutzer will diese
//  Freiheit ausdruecklich. Der Rueckweg bleibt: die Vorlage steht im Programm,
//  `resetPreset` holt sie zurueck.
void AudioController::savePreset(const QString& name) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;

    QStringList fields { n, QString::number(m_eq.preamp(), 'f', 2) };
    for (double v : m_eq.gains()) fields.append(QString::number(v, 'f', 2));
    const QString line = fields.join(QLatin1Char('\t'));

    QStringList own = m_settings.audioEqPresets();
    bool replaced = false;
    for (QString& existing : own) {
        if (existing.section(QLatin1Char('\t'), 0, 0).trimmed()
                .compare(n, Qt::CaseInsensitive) != 0)
            continue;
        existing = line;
        replaced = true;
        break;
    }
    if (!replaced) own.append(line);
    m_settings.setAudioEqPresets(own);

    //  Wurde derselbe Name zuvor geloescht, kommt er mit dem Sichern zurueck.
    QStringList hidden = m_settings.audioEqHiddenPresets();
    if (hidden.removeIf([&n](const QString& h) {
            return h.compare(n, Qt::CaseInsensitive) == 0; }) > 0)
        m_settings.setAudioEqHiddenPresets(hidden);

    m_activePreset = n;
    emit presetsChanged();
}

//  Loeschen entfernt den EINTRAG, nicht die Einstellung: die Regler bleiben
//  stehen, wo sie stehen (Festlegung des Nutzers). Eine mitgelieferte wird
//  ausgeblendet statt entfernt - sie steht im Programm und kommt ueber
//  `resetPreset` zurueck.
void AudioController::deletePreset(const QString& name) {
    bool changed = false;

    QStringList own = m_settings.audioEqPresets();
    if (own.removeIf([&name](const QString& l) {
            return l.section(QLatin1Char('\t'), 0, 0).trimmed()
                       .compare(name, Qt::CaseInsensitive) == 0; }) > 0) {
        m_settings.setAudioEqPresets(own);
        changed = true;
    }
    if (presetIsBuiltin(name)) {
        QStringList hidden = m_settings.audioEqHiddenPresets();
        if (!hidden.contains(name, Qt::CaseInsensitive)) {
            hidden.append(name);
            m_settings.setAudioEqHiddenPresets(hidden);
            changed = true;
        }
    }
    if (!changed) return;
    //  Der Klang bleibt - nur die Zuordnung „das ist gerade jene" faellt weg.
    if (m_activePreset.compare(name, Qt::CaseInsensitive) == 0)
        m_activePreset.clear();
    emit presetsChanged();
}

void AudioController::resetPreset(const QString& name) {
    if (!presetIsBuiltin(name)) return;
    bool changed = false;

    QStringList own = m_settings.audioEqPresets();
    if (own.removeIf([&name](const QString& l) {
            return l.section(QLatin1Char('\t'), 0, 0).trimmed()
                       .compare(name, Qt::CaseInsensitive) == 0; }) > 0) {
        m_settings.setAudioEqPresets(own);
        changed = true;
    }
    QStringList hidden = m_settings.audioEqHiddenPresets();
    if (hidden.removeIf([&name](const QString& h) {
            return h.compare(name, Qt::CaseInsensitive) == 0; }) > 0) {
        m_settings.setAudioEqHiddenPresets(hidden);
        changed = true;
    }
    if (changed) emit presetsChanged();
}

void AudioController::resetAllPresets() {
    QStringList own = m_settings.audioEqPresets();
    const int before = own.size();
    own.removeIf([this](const QString& l) {
        return presetIsBuiltin(l.section(QLatin1Char('\t'), 0, 0).trimmed()); });
    bool changed = (own.size() != before);
    if (changed) m_settings.setAudioEqPresets(own);
    if (!m_settings.audioEqHiddenPresets().isEmpty()) {
        m_settings.setAudioEqHiddenPresets({});
        changed = true;
    }
    if (changed) emit presetsChanged();
}

//  Reihenfolge aendern. Gespeichert werden NAMEN, nicht Plaetze - s.
//  `inStoredOrder`.
void AudioController::movePreset(int from, int to) {
    QStringList names = presetNames();
    if (from < 0 || from >= names.size()) return;
    to = qBound(0, to, names.size() - 1);
    if (from == to) return;
    names.move(from, to);
    m_settings.setAudioEqPresetOrder(names);
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

// ── Was im Titel steht ───────────────────────────────────────────────────────
void AudioController::refreshTags() {
    const QString path = currentPath();
    if (path == m_tagsPath) return;              // derselbe Titel, nichts zu tun
    m_tagsPath = path;
    //  OHNE Bild: das kostet nur den Kopf der Datei und darf deshalb im
    //  GUI-Faden laufen. Das Bild holt der Anbieter später selbst.
    m_tags = path.isEmpty() ? AudioTags::Tags {}
                            : AudioTags::read(path, /*withCover=*/false);
    if (!path.isEmpty()) m_titleCache.insert(path, m_tags.displayTitle(path));
    ++m_coverRev;
    emit tagsChanged();
}

QString AudioController::trackTitle() const {
    const QString path = currentPath();
    if (path.isEmpty()) return {};
    return m_tags.displayTitle(path);
}
QString AudioController::trackArtist() const { return m_tags.artist; }
QString AudioController::trackAlbum() const  { return m_tags.album; }
QString AudioController::trackSubtitle() const { return m_tags.subtitle(); }
bool    AudioController::trackHasCover() const { return m_tags.hasCover; }

QString AudioController::coverSource() const {
    if (!m_tags.hasCover) return {};
    return AudioCoverProvider::sourceFor(currentPath(), m_coverRev);
}

QString AudioController::titleOf(const QString& pathOrUrl) const {
    const QString path = mg::toLocalPath(pathOrUrl);
    if (path.isEmpty()) return {};
    if (path == m_tagsPath) return trackTitle();     // der laufende: schon gelesen
    const auto hit = m_titleCache.constFind(path);
    if (hit != m_titleCache.constEnd()) return *hit;

    const AudioTags::Tags t = AudioTags::read(path, /*withCover=*/false);
    const QString title = t.displayTitle(path);
    if (m_titleCache.size() > 512) m_titleCache.clear();
    m_titleCache.insert(path, title);
    return title;
}

bool AudioController::extractInheritTags() const { return m_settings.audioExtractInheritTags(); }
void AudioController::setExtractInheritTags(bool on) {
    if (m_settings.audioExtractInheritTags() == on) return;
    m_settings.setAudioExtractInheritTags(on);
    emit optionsChanged();
}
bool AudioController::extractToQueue() const { return m_settings.audioExtractToQueue(); }
void AudioController::setExtractToQueue(bool on) {
    if (m_settings.audioExtractToQueue() == on) return;
    m_settings.setAudioExtractToQueue(on);
    emit optionsChanged();
}

// ── Ton aus einem Video sichern ──────────────────────────────────────────────
//  Der Arbeiter fasst NUR Pfade an (kein Modell, kein Qt-Objekt des GUI-Fadens)
//  und meldet sich ausschließlich über die Ereignisschlange zurück - Muster
//  `PdfExtractTask`.
class AudioExtractTask : public QRunnable {
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    AudioExtractTask(AudioController* owner, QString source, int generation,
                     CancelFlag cancel, int trackIndex = 0)
        : m_owner(owner)
        , m_source(std::move(source))
        , m_gen(generation)
        , m_cancel(std::move(cancel))
        , m_track(trackIndex) {
        setAutoDelete(true);
    }

    //  ZWEI Hüllenfamilien, ein Weg: MP4/M4V/MOV geht über `Mp4AudioExtract`
    //  (Tonspur -> `.m4a`), MKV/WEBM/MKA über `MkvAudioExtract` (Opus/Vorbis ->
    //  Ogg). Welcher der beiden zuständig ist, entscheidet die Endung; der
    //  Zielname entsteht deshalb HIER und nicht im GUI-Faden - bei MKV muss
    //  dafür die Datei kurz angelesen werden (die Endung hängt am Codec).
    void run() override {
        QString target;
        int tracks = 0;
        //  Was der Nutzer lesen soll: der Schlüssel wird im GUI-Faden zum Satz.
        int key = int(StringKey::AudioExtractFailNotMp4);
        bool ok = false;

        if (Mp4Audio::isCandidate(m_source)) {
            target = Mp4Audio::targetPathFor(m_source);
            Mp4Audio::Info info;
            const auto r = Mp4Audio::extract(m_source, target, &info, m_cancel.get(),
                                             m_track);
            tracks = info.audioTracks;
            ok = (r == Mp4Audio::Result::Ok);
            switch (r) {
                case Mp4Audio::Result::Ok:            key = int(StringKey::AudioExtractOk); break;
                case Mp4Audio::Result::NotMp4:        key = int(StringKey::AudioExtractFailNotMp4); break;
                case Mp4Audio::Result::Fragmented:    key = int(StringKey::AudioExtractFailFragmented); break;
                case Mp4Audio::Result::NoAudioTrack:  key = int(StringKey::AudioExtractFailNoTrack); break;
                case Mp4Audio::Result::ExternalMedia: key = int(StringKey::AudioExtractFailExternal); break;
                case Mp4Audio::Result::TooLarge:      key = int(StringKey::AudioExtractFailTooLarge); break;
                case Mp4Audio::Result::WriteFailed:   key = int(StringKey::AudioExtractFailWrite); break;
                case Mp4Audio::Result::NotOpenable:   key = int(StringKey::AudioExtractFailRead); break;
                default:                              key = int(StringKey::AudioExtractFailDamaged); break;
            }
        } else if (MkvAudio::isCandidate(m_source)) {
            //  Die ENDUNG hängt an der gewählten Spur - eine Datei kann Opus
            //  und E-AC-3 nebeneinander führen.
            target = MkvAudio::targetPathFor(m_source, m_track);
            MkvAudio::Info info;
            const auto r = MkvAudio::extract(m_source, target, &info, m_cancel.get(),
                                             m_track);
            tracks = info.audioTracks;
            ok = (r == MkvAudio::Result::Ok);
            switch (r) {
                case MkvAudio::Result::Ok:               key = int(StringKey::AudioExtractOk); break;
                case MkvAudio::Result::NotMatroska:      key = int(StringKey::AudioExtractFailNotMp4); break;
                case MkvAudio::Result::NoAudioTrack:     key = int(StringKey::AudioExtractFailNoTrack); break;
                case MkvAudio::Result::UnsupportedCodec: key = int(StringKey::AudioExtractFailCodec); break;
                case MkvAudio::Result::TooLarge:         key = int(StringKey::AudioExtractFailTooLarge); break;
                case MkvAudio::Result::WriteFailed:      key = int(StringKey::AudioExtractFailWrite); break;
                case MkvAudio::Result::NotOpenable:      key = int(StringKey::AudioExtractFailRead); break;
                default:                                 key = int(StringKey::AudioExtractFailDamaged); break;
            }
        }

        AudioController* owner = m_owner;
        const QString src = m_source, dst = ok ? target : QString();
        const int gen = m_gen;
        const int track = m_track;
        QMetaObject::invokeMethod(owner, [owner, ok, key, src, dst, tracks, gen, track] {
            owner->extractTaskDone(ok, key, src, dst, tracks, gen, track);
        }, Qt::QueuedConnection);
    }

private:
    AudioController* m_owner;
    QString    m_source;
    int        m_gen;
    CancelFlag m_cancel;
    int        m_track = 0;      // welche Tonspur (0 = die erste)
};

//  Nachsehen, WELCHE Tonspuren die Datei hat - im eigenen Faden, weil dafür
//  der Kopf der Datei gelesen wird (bei MKV bis zu 16 MB). Muster wie
//  `AudioExtractTask`: nur Pfade, Rückweg über die Ereignisschlange.
class AudioProbeTask : public QRunnable {
public:
    AudioProbeTask(AudioController* owner, QString source)
        : m_owner(owner), m_source(std::move(source)) { setAutoDelete(true); }

    //  Die Kanalzahl in Worte - die Zahl allein sagt beim Wählen wenig.
    static QString channelText(int ch) {
        if (ch == 1) return Strings::get(StringKey::AudioChMono);
        if (ch == 2) return Strings::get(StringKey::AudioChStereo);
        if (ch == 6) return QStringLiteral("5.1");
        if (ch == 8) return QStringLiteral("7.1");
        if (ch > 0)  return Strings::get(StringKey::AudioChMulti).arg(ch);
        return QString();
    }

    //  Eine Zeile für die Auswahl: „Spur 2 · ENG · Kommentar · Opus · Stereo".
    static QVariantMap describe(int index, const QString& codec, const QString& language,
                                const QString& name, int channels, bool supported) {
        QStringList parts;
        parts << Strings::get(StringKey::AudioTrackNumber).arg(index + 1);
        if (!language.isEmpty()) parts << language.toUpper();
        if (!name.isEmpty())     parts << name;
        //  Der Codec-Name der Datei („A_EAC3", „mp4a") ist für den Nutzer
        //  nichts wert, für die Unterscheidung zweier Spuren aber alles.
        if (!codec.isEmpty())    parts << codec;
        const QString ch = channelText(channels);
        if (!ch.isEmpty())       parts << ch;
        if (!supported)
            parts << Strings::get(StringKey::AudioTrackUnsupported);

        QVariantMap m;
        m.insert(QStringLiteral("index"), index);
        m.insert(QStringLiteral("label"), parts.join(QStringLiteral(" · ")));
        m.insert(QStringLiteral("supported"), supported);
        return m;
    }

    void run() override {
        QVariantList list;
        if (Mp4Audio::isCandidate(m_source)) {
            const Mp4Audio::Info info = Mp4Audio::probe(m_source);
            for (int i = 0; i < info.tracks.size(); ++i) {
                const Mp4Audio::TrackDesc& d = info.tracks.at(i);
                list.append(describe(i, d.codec, d.language, d.name, d.channels,
                                     d.supported));
            }
        } else if (MkvAudio::isCandidate(m_source)) {
            const MkvAudio::Info info = MkvAudio::probe(m_source);
            for (int i = 0; i < info.tracks.size(); ++i) {
                const MkvAudio::TrackDesc& d = info.tracks.at(i);
                list.append(describe(i, d.codec, d.language, d.name, d.channels,
                                     d.supported));
            }
        }
        AudioController* owner = m_owner;
        const QString src = m_source;
        QMetaObject::invokeMethod(owner, [owner, src, list] {
            owner->probeTaskDone(src, list);
        }, Qt::QueuedConnection);
    }

private:
    AudioController* m_owner;
    QString          m_source;
};

bool AudioController::canExtractAudio(const QString& pathOrUrl) const {
    const QString p = mg::toLocalPath(pathOrUrl);
    return Mp4Audio::isCandidate(p) || MkvAudio::isCandidate(p);
}

void AudioController::extractAudio(const QString& pathOrUrl, int trackIndex) {
    const QString src = mg::toLocalPath(pathOrUrl);
    if (src.isEmpty() || !QFileInfo::exists(src)) {
        emit message(Strings::get(StringKey::AudioExtractFailRead)
                         .arg(QFileInfo(src).fileName()));
        emit extractFinished(false, src, QString());
        return;
    }

    //  Noch keine Spur gewählt: erst nachsehen, wie viele es überhaupt sind.
    //  Das kostet nur den Kopf der Datei und läuft im eigenen Faden; die
    //  Entscheidung fällt danach in `probeTaskDone`.
    if (trackIndex < 0) {
        if (m_extractPool.maxThreadCount() != 1) m_extractPool.setMaxThreadCount(1);
        //  Schon das Nachsehen zählt als „beschäftigt": es liest die Datei an,
        //  und der Knopf, der es ausgelöst hat, soll nicht zweimal gehen.
        if (!m_extractBusy) {
            m_extractBusy = true;
            emit extractBusyChanged();
        }
        m_extractPool.start(new AudioProbeTask(this, src));
        return;
    }
    //  Eine Sicherung nach der anderen: der Pool hat einen Faden, ein zweiter
    //  Auftrag reiht sich ein statt die Platte zu teilen.
    if (m_extractPool.maxThreadCount() != 1) m_extractPool.setMaxThreadCount(1);
    if (!m_extractCancel) m_extractCancel = std::make_shared<std::atomic<bool>>(false);

    if (!m_extractBusy) {
        m_extractBusy = true;
        emit extractBusyChanged();
    }
    emit message(Strings::get(StringKey::AudioExtractRunning));
    //  Der Zielname entsteht im Arbeiter: bei MKV hängt die Endung am Codec.
    m_extractPool.start(new AudioExtractTask(this, src, ++m_extractGen, m_extractCancel,
                                             trackIndex));
}

void AudioController::probeTaskDone(const QString& source, const QVariantList& tracks) {
    //  Eine Spur (oder gar keine Auskunft): nicht fragen, einfach machen.
    //  Fragen, wo es nichts zu wählen gibt, ist ein Klick zu viel.
    if (tracks.size() < 2) {
        extractAudio(source, 0);
        return;
    }
    //  Jetzt liegt es beim Nutzer - und solange gearbeitet wird nicht.
    if (m_extractBusy) {
        m_extractBusy = false;
        emit extractBusyChanged();
    }
    emit trackChoiceNeeded(source, tracks);
}

void AudioController::extractTaskDone(bool ok, int messageKey, const QString& source,
                                      const QString& target, int audioTracks,
                                      int generation, int trackIndex) {
    //  Nur der ZULETZT gestartete Auftrag schaltet den Zustand zurück - sonst
    //  räumte ein früher fertiger Auftrag den späteren mit ab.
    if (generation == m_extractGen && m_extractBusy) {
        m_extractBusy = false;
        emit extractBusyChanged();
    }

    const QString name = QFileInfo(source).fileName();
    if (!ok) {
        emit message(Strings::get(static_cast<StringKey>(messageKey)).arg(name));
        emit extractFinished(false, source, QString());
        return;
    }

    //  Auf Wunsch reiht sich die gesicherte Tonspur gleich ein. Bewusst
    //  ANHÄNGEN und nicht ersetzen: die laufende Liste bleibt, wie sie ist.
    if (m_settings.audioExtractToQueue()) {
        QStringList items = m_queue.items();
        if (!items.contains(target)) {
            items.append(target);
            m_queue.setItems(items);
        }
    }

    const QString targetName = QFileInfo(target).fileName();
    //  Mehrere Tonspuren: dann steht in der Meldung, WELCHE gesichert wurde -
    //  bei zwei Sprachen ist das der einzige Unterschied, den man später sieht.
    emit message(audioTracks > 1
                     ? Strings::get(StringKey::AudioExtractManyTracks)
                           .arg(targetName).arg(audioTracks).arg(trackIndex + 1)
                     : Strings::get(StringKey::AudioExtractOk).arg(targetName));
    emit extractFinished(true, source, target);
}

bool AudioController::playerModePaneRemembered(int paneIndex) {
    if (paneIndex < 0 || paneIndex > 3) return false;
    if (m_restoreMask < 0) m_restoreMask = m_settings.audioPlayerModeMask();
    return (m_restoreMask & (1 << paneIndex)) != 0;
}

bool AudioController::takePlayerModeRestore(int paneIndex) {
    if (paneIndex < 0 || paneIndex > 3) return false;
    //  Der Stand beim Beenden wird EINMAL gelesen und danach nur noch
    //  abgetragen: die Einstellungen aendern sich waehrend des Laufs mit jeder
    //  Umschaltung, der Wiederherstell-Auftrag darf das nicht mitbekommen.
    if (m_restoreMask < 0) m_restoreMask = m_settings.audioPlayerModeMask();
    const int bit = 1 << paneIndex;
    if (!(m_restoreMask & bit)) return false;
    m_restoreMask &= ~bit;                 // gilt genau einmal je Haelfte
    return true;
}

void AudioController::rememberPlayerMode(bool on, int paneIndex) {
    //  JE HAELFTE merken, nicht nur „irgendeine": bei geteiltem Fenster stand
    //  sonst nach dem Neustart der Player auf der falschen Seite (vom Nutzer
    //  gemeldet). Die andere Haelfte behaelt dabei ihr eigenes Bit.
    if (paneIndex >= 0 && paneIndex <= 3) {
        const int before = m_settings.audioPlayerModeMask();
        const int bit    = 1 << paneIndex;
        const int after  = on ? (before | bit) : (before & ~bit);
        if (after != before) m_settings.setAudioPlayerModeMask(after);
        //  Der alte Schalter bleibt die Kurzantwort „stand irgendwo ein Player".
        const bool any = after != 0;
        if (m_settings.audioPlayerMode() != any) {
            m_settings.setAudioPlayerMode(any);
            emit optionsChanged();
        }
        return;
    }
    if (m_settings.audioPlayerMode() == on) return;
    m_settings.setAudioPlayerMode(on);
    emit optionsChanged();
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
