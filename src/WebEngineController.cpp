#include "WebEngineController.h"

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
    // Schneller Ausstieg ohne Lock — Ready ist ein Endzustand.
    if (m_state == State::Ready)
        return;

    // QtWebEngineQuick::initialize() muss auf dem GUI-Thread laufen. Aufrufe
    // aus fremden Threads blockierend umleiten (Rückkehr erst nach Ready) —
    // zusammen mit call_once ist die Initialisierung damit thread-safe und
    // passiert garantiert genau einmal.
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, &WebEngineController::ensureInitializedForHtml,
                                  Qt::BlockingQueuedConnection);
        return;
    }

    std::call_once(m_once, [this]() {
        setState(State::Initializing);

        // Nachträgliche Initialisierung: erlaubt, da main.cpp
        // Qt::AA_ShareOpenGLContexts bereits VOR der QGuiApplication gesetzt
        // hat (Qt quittiert den späten Aufruf nur mit einer Deprecation-
        // Warnung). Erst ab hier — und mit der ersten WebEngineView — fallen
        // die Chromium-RAM-Kosten an.
        QtWebEngineQuick::initialize();

        setState(State::Ready);
    });
}
