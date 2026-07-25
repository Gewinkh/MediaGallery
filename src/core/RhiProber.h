#pragma once
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  RhiProber — RHI-Backend-Verwaltung ohne Kindprozesse
//
//  Strategie: Crash-Guard + Degradationskette
//  Beim Start wird ein „startedWith:<backend>"-Flag in QSettings gesetzt.
//  Beim sauberen Beenden (bzw. ~4 s nach erfolgreichem Start, s. main.cpp)
//  wird es gelöscht. Ist das Flag beim nächsten Start noch da, ist die App mit
//  diesem Backend sofort abgestürzt → es wird EINE Stufe der Degradationskette
//  zurückgefallen (vulkan/d3d11/metal → opengl → software) statt direkt auf
//  Software: ein defekter Vulkan-Treiber erzwingt so kein unnötig langsames
//  Software-Rendering, solange OpenGL funktioniert. Crasht auch das Fallback
//  sofort, fällt der übernächste Start weiter (bis Software).
//
//  Zusätzlich:
//   • Namens-/Plattformvalidierung: unbekannte oder plattformfremde Werte in
//     QSettings (z. B. „d3d11" in einer nach Linux kopierten Config) fallen
//     auf den Plattform-Standard (OpenGL) zurück statt — wie zuvor über den
//     toApi-Default — stillschweigend auf Software.
//   • Vulkan-Loader-Vorabprüfung: fehlt der Loader komplett (System ohne
//     ICD/vulkan-icd-loader), wird direkt OpenGL gewählt, ohne erst den
//     Crash-Guard-Zyklus (Crash → Neustart → Fallback) zu durchlaufen.
//   • Laufzeit-Guard für Gerätewechsel: main.cpp verbindet
//     QQuickWindow::sceneGraphError mit noteRuntimeFailure() — ein nicht
//     behebbarer Scene-Graph-Fehler (GPU-Wechsel/Device-Lost) persistiert das
//     nächstsicherere Backend für den NÄCHSTEN Start, statt dass Qt die App
//     per qFatal hart beendet.
//
//  Backend-Wechsel: einfach in QSettings schreiben + Neustart.
//  Kein Kindprozess, kein fork(), kein QProcess.
// ─────────────────────────────────────────────────────────────────────────────
class RhiProber {
public:
    // Liest das gewünschte Backend aus QSettings (validiert Name + Plattform),
    // setzt es via QQuickWindow::setGraphicsApi() und schreibt den Crash-Guard.
    // Nach einem Crash-Start: eine Stufe der Degradationskette zurückfallen.
    // Muss VOR QGuiApplication-Konstruktion aufgerufen werden.
    static QString applyStoredBackend();

    // Muss beim sauberen App-Ende aufgerufen werden (löscht Crash-Guard).
    static void markCleanShutdown();

    // Schreibt das gewünschte Backend in QSettings (wirkt beim nächsten Start).
    static void setDesiredBackend(const QString& backend);

    // Laufzeitfehler des Scene Graphs (Gerätewechsel/Device-Lost, den der
    // Treiber nicht überlebt): fürs NÄCHSTE Programm-Start ein sichereres
    // Backend persistieren. Ändert die laufende Sitzung nicht.
    static void noteRuntimeFailure();

private:
    static void    applyApi(const QString& backend);
    static QString sanitizeBackend(QString backend);    // Name + Plattform prüfen
    static QString fallbackFor(const QString& backend); // Degradationskette
    static QString platformDefault();

    static constexpr const char* kKeyBackend      = "rhi/backend";
    static constexpr const char* kKeyCrashGuard   = "rhi/lastStartedWith";
    static constexpr const char* kKeyFallback      = "rhi/softwareFallback";
};
