#include "app/WebEngineController.h"

#include <QtWebEngineQuick>
#include <QThread>
#include <QMetaObject>
#include <QCoreApplication>

WebEngineController::WebEngineController(QObject* parent)
    : QObject(parent) {}

void WebEngineController::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged();
}

void WebEngineController::ensureInitializedForHtml() {
    // Schneller Ausstieg ohne Lock - Ready ist ein Endzustand.
    if (m_state == State::Ready)
        return;

    // QtWebEngineQuick::initialize() muss auf dem GUI-Thread laufen. Aufrufe
    // aus fremden Threads blockierend umleiten (Rückkehr erst nach Ready) -
    // zusammen mit call_once ist die Initialisierung damit thread-safe und
    // passiert garantiert genau einmal.
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, &WebEngineController::ensureInitializedForHtml,
                                  Qt::BlockingQueuedConnection);
        return;
    }

    std::call_once(m_once, [this]() {
        setState(State::Initializing);

        // ── Chromium OHNE GPU-Beschleunigung starten ─────────────────────────
        //  Gemessen (2026-07-22, Arch, Wayland UND Xwayland, isolierte
        //  WebEngine-Probe): Mit GPU-Beschleunigung meldet Chromium
        //  „GBM is not supported with the current configuration. Fallback to
        //  Vulkan rendering in Chromium." und stürzt reproduzierbar mit
        //  SIGSEGV ab. Mit --disable-gpu läuft dieselbe Sequenz (Datei laden ->
        //  about:blank -> erneut laden -> fehlschlagendes Laden -> Beenden)
        //  fehlerfrei durch, auf JEDEM Scene-Graph-Backend (OpenGL, Vulkan,
        //  Software - s. RhiProber) und mit korrektem Bild.
        //  Das erklärt alle drei HTML-Befunde: „Rendern schlägt gelegentlich
        //  fehl", „App friert ein", „Schließen hängt das System auf".
        //
        //  Kosten/Nutzen (§0): Lokale Text-/Lernzettel-Dokumente rastert
        //  Chromium in Software schnell genug; dafür entfällt der GPU-Prozess
        //  komplett -> weniger RAM (Regel 9) und ein schnellerer erster Aufbau.
        //  Eine bereits vom Nutzer gesetzte Variable bleibt unangetastet
        //  (bewusste Übersteuerung ohne Codeänderung möglich).
        //
        //  Zusätzlich `--enable-smooth-scrolling`: aktiviert Chromiums animiertes
        //  Scroll-Offset (Mausrad/Tastatur) - die gerenderte HTML-Vorschau
        //  scrollt damit weich statt sprunghaft, konsistent zum web-artigen
        //  Smooth-Scrolling der übrigen Flächen (Galerie/PDF/DOCX/Text nutzen
        //  eine `NumberAnimation` auf `contentY`; die WebEngine kapselt ihr
        //  Scrollen selbst, daher der Chromium-Weg). Funktioniert auch mit
        //  `--disable-gpu` (Blink-Feature, keine GPU nötig).
        if (!qEnvironmentVariableIsSet("QTWEBENGINE_CHROMIUM_FLAGS"))
            qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --enable-smooth-scrolling");

        // Nachträgliche Initialisierung: erlaubt, da main.cpp
        // Qt::AA_ShareOpenGLContexts bereits VOR der QGuiApplication gesetzt
        // hat (Qt quittiert den späten Aufruf nur mit einer Deprecation-
        // Warnung). Erst ab hier - und mit der ersten WebEngineView - fallen
        // die Chromium-RAM-Kosten an.
        QtWebEngineQuick::initialize();

        setState(State::Ready);
    });
}
