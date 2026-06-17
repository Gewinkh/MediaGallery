#pragma once
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  RhiProber — RHI-Backend-Verwaltung ohne Kindprozesse
//
//  Strategie: Crash-Guard
//  Beim Start wird ein „startedWith:<backend>"-Flag in QSettings gesetzt.
//  Beim sauberen Beenden wird es gelöscht.
//  Wenn beim nächsten Start das Flag noch da ist → die App ist mit diesem
//  Backend abgestürzt → automatisch auf Software zurückfallen.
//
//  Backend-Wechsel: einfach in QSettings schreiben + Neustart.
//  Kein Kindprozess, kein fork(), kein QProcess.
// ─────────────────────────────────────────────────────────────────────────────
class RhiProber {
public:
    // Liest das gewünschte Backend aus QSettings, setzt es via
    // QQuickWindow::setGraphicsApi() und schreibt den Crash-Guard.
    // Fallback auf Software falls letzter Start damit crashte.
    // Muss VOR QGuiApplication-Konstruktion aufgerufen werden.
    static QString applyStoredBackend();

    // Muss beim sauberen App-Ende aufgerufen werden (löscht Crash-Guard).
    static void markCleanShutdown();

    // Schreibt das gewünschte Backend in QSettings (wirkt beim nächsten Start).
    static void setDesiredBackend(const QString& backend);
    static QString storedBackend();

private:
    static void applyApi(const QString& backend);

    static constexpr const char* kKeyBackend      = "rhi/backend";
    static constexpr const char* kKeyCrashGuard   = "rhi/lastStartedWith";
    static constexpr const char* kKeyFallback      = "rhi/softwareFallback";
};
