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

// ─────────────────────────────────────────────────────────────────────────────
//  AudioController (`Audio`) - die EINE Bridge des Player-Modus nach QML.
//
//  Sie hält die drei Teile zusammen (`AudioEngine`, `PlayQueue`,
//  `AudioEqualizer`), verknüpft sie miteinander - „Titel zu Ende" fragt die
//  Warteschlange, was folgt - und legt alle Einstellungen unter `audio/` ab.
//
//  APPWEIT, nicht je Hälfte: es gibt EINE Wiedergabe in der App (wie bei
//  `monoPlay`). Welche Hälfte den Player zeigt, entscheidet die Oberfläche.
//
//  Presets stehen als eine Zeichenkette je Preset in den Einstellungen:
//      "Name\tPreamp\tg0\tg1…\tg9"
//  Ein einfaches Format, das sich von Hand lesen und notfalls reparieren lässt.
// ─────────────────────────────────────────────────────────────────────────────
class AudioController : public QObject {
    Q_OBJECT

    // ── Wiedergabe ──────────────────────────────────────────────────────────
    Q_PROPERTY(int     state       READ state       NOTIFY stateChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(qint64  position    READ position    NOTIFY positionChanged)
    Q_PROPERTY(qint64  duration    READ duration    NOTIFY durationChanged)
    Q_PROPERTY(qreal   volume      READ volume      WRITE setVolume NOTIFY volumeChanged)
    //  Läuft (oder pausiert) gerade etwas? Daran hängt die Player-Leiste - sie
    //  erscheint erst, wenn ein Titel gewählt wurde (Festlegung des Nutzers).
    Q_PROPERTY(bool    active      READ active      NOTIFY stateChanged)
    //  Ein Titel ist GEWÄHLT (läuft, pausiert oder wurde beim Start
    //  wiederhergestellt) - daran hängt, ob die Leiste steht.
    Q_PROPERTY(bool    hasTrack    READ hasTrack    NOTIFY currentChanged)

    // ── Reihenfolge ─────────────────────────────────────────────────────────
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(int  repeat  READ repeat  WRITE setRepeat  NOTIFY repeatChanged)

    // ── Equalizer ───────────────────────────────────────────────────────────
    Q_PROPERTY(bool eqEnabled READ eqEnabled WRITE setEqEnabled NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqGains READ eqGains NOTIFY eqChanged)
    Q_PROPERTY(qreal eqPreamp READ eqPreamp WRITE setEqPreamp NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqFrequencies READ eqFrequencies CONSTANT)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)

    // ── Warteschlange (die große Ansicht zeigt sie) ─────────────────────────
    Q_PROPERTY(QStringList queue      READ queue      NOTIFY queueChanged)
    Q_PROPERTY(int         queueIndex READ queueIndex NOTIFY currentChanged)
    //  WER spielt gerade: die Hälfte, die den Player gestartet hat. Nur dort
    //  gehört die Leiste hin - sonst stünde sie in beiden Galerien.
    Q_PROPERTY(QObject*    owner      READ owner      WRITE setOwner NOTIFY ownerChanged)

    // ── Optionen (Einstellungen ▸ Audio) ────────────────────────────────────
    Q_PROPERTY(bool showVideos   READ showVideos   WRITE setShowVideos   NOTIFY optionsChanged)
    //  War der Player-Modus beim letzten Beenden an? Die erste Hälfte nimmt ihn
    //  beim Start wieder auf - sonst stünde der wiederhergestellte Titel in
    //  einer Galerie, die gar nicht im Player-Modus ist.
    Q_PROPERTY(bool playerModeRemembered READ playerModeRemembered
               WRITE setPlayerModeRemembered NOTIFY optionsChanged)
    //  Darstellung im Player-Modus: Kacheln (false) oder Liste (true).
    Q_PROPERTY(bool listLayout   READ listLayout   WRITE setListLayout   NOTIFY optionsChanged)
    Q_PROPERTY(bool rememberLast READ rememberLast WRITE setRememberLast NOTIFY optionsChanged)

    // ── Ton aus einem Video sichern (MP4 -> M4A, ohne Neukodierung) ─────────
    //  Läuft gerade eine Sicherung? QML sperrt damit den Menüpunkt.
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
    //  Nach dem Wiederherstellen liegt ein Titel BEREIT, ohne dass die Kette
    //  läuft - dann gilt der der Warteschlange, damit Leiste und Kachel ihn
    //  zeigen können.
    QString currentPath() const {
        const QString p = m_engine.currentPath();
        return p.isEmpty() ? m_queue.currentPath() : p;
    }
    //  Ist überhaupt ein Titel gewählt (auch pausiert/bereitgelegt)?
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
    void  setEqPreamp(qreal db);
    QVariantList eqFrequencies() const;
    QStringList  presetNames() const;

    //  In ABSPIEL-Reihenfolge: bei Zufall zeigt die Liste die Mischung, nicht
    //  die Ordnerfolge (sonst stimmte „als Nächstes" nicht).
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
    //  Titel und Interpret einer BELIEBIGEN Datei - für die Warteschlange, die
    //  auch die kommenden Titel benennen will. Ohne Bild, also billig.
    Q_INVOKABLE QString titleOf(const QString& pathOrUrl) const;

    bool extractBusy() const { return m_extractBusy; }
    bool extractInheritTags() const;
    void setExtractInheritTags(bool on);
    bool extractToQueue() const;
    void setExtractToQueue(bool on);

    // ── Bedienung aus QML ───────────────────────────────────────────────────
    //  Die Galerie reicht ihre SICHTBARE Liste mit - sie ist die Warteschlange.
    Q_INVOKABLE void playFile(const QString& path, const QStringList& queue);
    Q_INVOKABLE void setQueue(const QStringList& queue);
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();
    //  Anhalten UND die Warteschlange leeren (Ordnerwechsel): danach ist kein
    //  Titel mehr gewählt, die Leiste verschwindet.
    Q_INVOKABLE void stopAndClear();
    Q_INVOKABLE void seek(qint64 ms);
    //  Einen bestimmten Eintrag der Warteschlange spielen (große Ansicht).
    Q_INVOKABLE void playAt(int index);

    Q_INVOKABLE void setBandGain(int band, qreal db);
    Q_INVOKABLE void resetBands();
    Q_INVOKABLE qreal suggestedPreamp() const { return m_eq.suggestedPreamp(); }
    //  Eingebaute und eigene Presets: anwenden, speichern, löschen.
    Q_INVOKABLE void applyPreset(const QString& name);
    Q_INVOKABLE void savePreset(const QString& name);
    Q_INVOKABLE void deletePreset(const QString& name);
    //  Zeitangabe „m:ss" für die Leiste (QML soll nicht rechnen müssen).
    Q_INVOKABLE QString formatTime(qint64 ms) const;

    //  Beim Beenden: zuletzt gespielten Titel samt Stelle sichern (sofern die
    //  Option an ist). Ruft die Shell im `onClosing`.
    //  Der gemerkte Player-Modus gilt EINMAL je Programmlauf - für die Hälfte,
    //  die zuerst danach fragt. Ohne das übernähme ihn auch jede Hälfte, die
    //  später dazukommt: `paneIndex` steht beim Aufbau noch nicht fest (die
    //  Shell bindet ihn erst danach), und die zweite Hälfte ginge mit in den
    //  Player-Modus, sobald man den Bildschirm teilt.
    Q_INVOKABLE bool takePlayerModeRestore();

    // ── Ton sichern ─────────────────────────────────────────────────────────
    //  Lohnt sich der Menüpunkt für diese Datei? Reine ENDUNGS-Prüfung, damit
    //  das Öffnen eines Kontextmenüs keine Datei anfasst.
    Q_INVOKABLE bool canExtractAudio(const QString& pathOrUrl) const;
    //  Asynchron (Regel 8): eigener Pool, kooperativer Abbruch, Rückweg über
    //  die Ereignisschlange. Das Ergebnis kommt als `extractFinished` und als
    //  fertiger Meldungstext über `message`.
    //  `trackIndex` < 0 heißt „erst nachsehen": hat die Datei mehr als eine
    //  Tonspur, kommt `trackChoiceNeeded` und die Oberfläche fragt nach; sonst
    //  läuft es sofort mit der ersten. Ein Index >= 0 sichert genau diese Spur.
    Q_INVOKABLE void extractAudio(const QString& pathOrUrl, int trackIndex = -1);

    Q_INVOKABLE void rememberSession();
    //  Beim Start: den gemerkten Titel LADEN, aber nicht abspielen.
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
    //  `source` und `target` sind LOKALE Pfade. Der Aufrufer hängt daran das
    //  Erben der Tags - dazu braucht er beide Namen.
    void extractFinished(bool ok, const QString& source, const QString& target);
    //  Mehrere Tonspuren: die Oberfläche soll fragen. Jeder Eintrag der Liste
    //  ist eine Karte mit `index`, `label` und `supported` - fertige Texte, die
    //  QML nur noch anzeigt.
    void trackChoiceNeeded(const QString& source, const QVariantList& tracks);
    void message(const QString& text);

private:
    //  Den Nachfolger bei der Kette anmelden (lückenloser Übergang).
    void armNextTrack();
    //  Die Tags des laufenden Titels neu lesen (Pfadwechsel). Ohne Bild -
    //  das holt der Bild-Anbieter in seinem eigenen Faden.
    void refreshTags();
    friend class AudioExtractTask;
    friend class AudioProbeTask;
    //  Rückweg des Nachsehens: die Spurliste steht, jetzt entscheidet der
    //  GUI-Faden - eine Spur = sofort sichern, mehrere = fragen lassen.
    void probeTaskDone(const QString& source, const QVariantList& tracks);
    //  Rückweg des Arbeiters (immer im GUI-Thread, per QueuedConnection).
    //  `messageKey` ist ein `StringKey` - der Arbeiter wählt ihn, den Satz baut
    //  der GUI-Faden.
    void extractTaskDone(bool ok, int messageKey, const QString& source,
                         const QString& target, int audioTracks, int generation,
                         int trackIndex = 0);
    void loadSettings();
    void applyGainsToSettings();
    QStringList builtinPresetLines() const;

    ISettings&      m_settings;
    AudioEqualizer  m_eq;
    AudioEngine     m_engine;
    PlayQueue       m_queue;
    //  Beim Wiederherstellen wird geladen, aber NICHT gespielt: diese Stelle
    //  wartet dann auf den ersten Druck auf ⏯.
    qint64          m_pendingSeek = 0;
    //  Nur beobachtet, nie besessen: verschwindet die Hälfte, zeigt der Zeiger
    //  ins Leere - deshalb `QPointer`.
    QPointer<QObject> m_owner;
    bool              m_restoreTaken = false;

    //  Tags des laufenden Titels; `m_tagsPath` merkt, wofür sie gelten, damit
    //  ein wiederholtes `currentChanged` die Datei nicht erneut liest.
    AudioTags::Tags m_tags;
    QString         m_tagsPath;
    //  Cache-Brecher für die Bildadresse (jeder neue Titel zählt hoch).
    int             m_coverRev = 0;
    //  Titel je Pfad, damit die Warteschlangen-Liste beim Blättern nicht bei
    //  JEDER Zeile erneut in die Datei greift. Beim Überlaufen wird schlicht
    //  geleert - eine echte LRU wäre für ein paar hundert Namen zu viel Technik.
    mutable QHash<QString, QString> m_titleCache;

    //  Sichern läuft in EINEM eigenen Faden - mehrere gleichzeitig brächten
    //  nichts (die Arbeit ist Ein-/Ausgabe) und würden die Platte nur belasten.
    QThreadPool                          m_extractPool;
    std::shared_ptr<std::atomic<bool>>   m_extractCancel;
    int                                  m_extractGen  = 0;
    bool                                 m_extractBusy = false;
};
