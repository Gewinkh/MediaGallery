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

    // `QtWebEngineQuick::initialize()` muss auf dem GUI-Thread laufen: Aufrufe aus fremden Threads werden
    // blockierend umgeleitet, zusammen mit `call_once` passiert die Initialisierung garantiert genau einmal.
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, &WebEngineController::ensureInitializedForHtml,
                                  Qt::BlockingQueuedConnection);
        return;
    }

    std::call_once(m_once, [this]() {
        setState(State::Initializing);

        // Chromium ohne GPU: mit Beschleunigung stuerzt es unter Wayland reproduzierbar mit
        // SIGSEGV ab (GBM nicht unterstuetzt). Software rastert lokale Dokumente schnell genug.
        // Eine vom Nutzer gesetzte Variable bleibt unangetastet.
        if (!qEnvironmentVariableIsSet("QTWEBENGINE_CHROMIUM_FLAGS"))
            qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --enable-smooth-scrolling");

        // Nachträgliche Initialisierung ist erlaubt, weil main.cpp `Qt::AA_ShareOpenGLContexts` vor der QGuiApplication
        // setzt. Erst ab hier fallen die Chromium-RAM-Kosten an.
        QtWebEngineQuick::initialize();

        setState(State::Ready);
    });
}
