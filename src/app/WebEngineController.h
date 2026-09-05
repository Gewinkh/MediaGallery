#pragma once
#include <QObject>
#include <mutex>

// WebEngine wird erst initialisiert, wenn wirklich eine HTML-Datei geoeffnet wird -
// vorher zahlten auch Nutzer ohne HTML die Chromium-Grundkosten. Genau eine
// Initialisierung (std::call_once), fremde Threads werden auf den GUI-Thread geleitet.
class WebEngineController : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool  ready READ ready NOTIFY stateChanged)

public:
    enum class State {
        NotInitialized,   // WebEngine noch nie angefordert - keine RAM-Kosten
        Initializing,     // Initialisierung läuft gerade (transient, GUI-Thread)
        Ready             // QtWebEngineQuick::initialize() erfolgreich gelaufen
    };
    Q_ENUM(State)

    explicit WebEngineController(QObject* parent = nullptr);

    State state() const { return m_state; }
    bool  ready() const { return m_state == State::Ready; }
    // Initialisiert WebEngine genau einmal (idempotent). Trigger: Öffnen einer
    // .html/.htm-Datei bzw. Anforderung der HTML-Vorschau. Läuft synchron auf
    // dem GUI-Thread - nach der Rückkehr ist ready() == true.
    Q_INVOKABLE void ensureInitializedForHtml();

signals:
    void stateChanged();

private:
    void setState(State s);

    State           m_state = State::NotInitialized;
    std::once_flag  m_once;   // garantiert genau EINE Initialisierung (thread-safe)
};
