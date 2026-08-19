#pragma once
#include <QObject>
#include <mutex>

// ─────────────────────────────────────────────────────────────────────────────
//  WebEngineController - vollständig LAZY Initialisierung von Qt WebEngine.
//
//  RAM-Baseline: QtWebEngineQuick::initialize() lief bisher bedingungslos beim
//  Programmstart - Nutzer ohne HTML-Dateien zahlten die Chromium-Grundkosten.
//  Jetzt wird WebEngine erst initialisiert, wenn TATSÄCHLICH eine .html/.htm
//  geöffnet bzw. HTML-Rendering angefordert wird (ensureInitializedForHtml()).
//
//  Voraussetzung (main.cpp): Qt::AA_ShareOpenGLContexts wird VOR der
//  QGuiApplication gesetzt (kostenlos, lädt KEIN Chromium). Damit ist der
//  nachträgliche QtWebEngineQuick::initialize()-Aufruf erlaubt (Qt gibt nur
//  eine Deprecation-Warnung aus); die eigentlichen RAM-Kosten (Render-Prozess)
//  entstehen ohnehin erst mit der ersten WebEngineView.
//
//  Zustände:  NotInitialized -> Initializing -> Ready  (nie rückwärts).
//  Thread-safe: genau EINE Initialisierung (std::call_once); Aufrufe aus
//  fremden Threads werden blockierend auf den GUI-Thread umgeleitet, da
//  QtWebEngineQuick::initialize() dort laufen muss.
//
//  CHROMIUM OHNE GPU (2026-07-22, gemessen): Mit GPU-Beschleunigung meldet
//  Chromium hier „GBM is not supported with the current configuration.
//  Fallback to Vulkan rendering in Chromium." und stürzt reproduzierbar mit
//  SIGSEGV ab - die Ursache der gemeldeten HTML-Fehlerbilder (Rendern schlägt
//  fehl, App hängt, Hänger beim Schließen). Deshalb wird VOR initialize()
//  --disable-gpu gesetzt (s. cpp): damit läuft dieselbe Sequenz stabil, und
//  zwar auf ALLEN Scene-Graph-Backends (OpenGL/Vulkan/Software, s. RhiProber).
//
//  QML-Singleton "WebEngine": FullscreenViewer/HtmlSurface gaten die
//  WebEngineView-Instanziierung über WebEngine.ready - solange nicht Ready,
//  fällt HTML immer auf die Quelltext-Ansicht (TextSurface) zurück.
// ─────────────────────────────────────────────────────────────────────────────
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
