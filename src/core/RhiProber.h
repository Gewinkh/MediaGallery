#pragma once
#include <QString>
#include <QStringList>

// Crash-Guard statt Kindprozess: ein Flag beim Start, gelöscht beim sauberen Ende. Steht es noch, ist die App
// abgestürzt und fällt EINE Stufe der Kette zurück (vulkan/d3d11/metal -> opengl -> software), nicht direkt
// auf Software. Dazu Namens- und Plattformvalidierung sowie eine Vulkan-Loader-Vorabprüfung.
class RhiProber {
public:
    // Liest das gewünschte Backend aus QSettings (Name und Plattform validiert), setzt es über
    // `QQuickWindow::setGraphicsApi()` und schreibt den Crash-Guard. Muss VOR der QGuiApplication laufen.
    static QString applyStoredBackend();

    // Muss beim sauberen App-Ende aufgerufen werden (löscht Crash-Guard).
    static void markCleanShutdown();

    // Schreibt das gewünschte Backend in QSettings (wirkt beim nächsten Start).
    static void setDesiredBackend(const QString& backend);

    // Laufzeitfehler des Scene Graphs (Gerätewechsel/Device-Lost, den der
    // Treiber nicht überlebt): fürs NÄCHSTE Programm-Start ein sichereres
    // Backend persistieren. Ändert die laufende Sitzung nicht.
    static void noteRuntimeFailure();

    // Linux: merkt sich aus GL_VERSION/GL_RENDERER, ob auf einer Mesa-Hardware gezeichnet wurde (Grundlage der Weiche).
    static void noteRenderer(const QString& glVersion, const QString& glRenderer);

    // Linux: Mesa-Anbieterdatei fuer `__EGL_VENDOR_LIBRARY_FILENAMES`, oder leer, wenn die Weiche nicht greifen darf.
    static QString mesaPinFile(bool lastMesa, const QStringList& candidates);

private:
    static void    applyApi(const QString& backend);
    static QString sanitizeBackend(QString backend);    // Name + Plattform prüfen
    static QString fallbackFor(const QString& backend); // Degradationskette
    static QString platformDefault();

    static constexpr const char* kKeyBackend      = "rhi/backend";
    static constexpr const char* kKeyCrashGuard   = "rhi/lastStartedWith";
    static constexpr const char* kKeyFallback      = "rhi/softwareFallback";
    static constexpr const char* kKeyLastMesa     = "rhi/lastRendererMesa";
    static constexpr const char* kKeyEglPinned    = "rhi/eglPinnedMesa";
};
