#include "core/RhiProber.h"

#include <QLibrary>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QStringList>

static QSGRendererInterface::GraphicsApi toApi(const QString& name)
{
    if (name == u"vulkan")   return QSGRendererInterface::Vulkan;
    if (name == u"d3d11")    return QSGRendererInterface::Direct3D11;
    if (name == u"metal")    return QSGRendererInterface::Metal;
    if (name == u"opengl")   return QSGRendererInterface::OpenGL;
    return QSGRendererInterface::Software;
}

// Billige dlopen-Probe auf den Vulkan-Loader - KEINE Instanz-Erzeugung (zu teuer, und vor der QGuiApplication
// zu früh). Fehlt der Loader, führte sonst erst der Crash-Guard-Zyklus zu OpenGL.
#ifndef Q_OS_MACOS
static bool vulkanLoaderAvailable()
{
#ifdef Q_OS_WIN
    QLibrary lib(QStringLiteral("vulkan-1"));
#else
    QLibrary lib(QStringLiteral("vulkan"), 1);
#endif
    if (lib.load()) { lib.unload(); return true; }
    return false;
}
#endif

//  platformDefault - sicherer Ausgangspunkt je Plattform
//  OpenGL: auf allen drei Zielplattformen vorhanden und zugleich das
//  robusteste Backend für die WebEngine-Vorschau (s. Projektüberblick).
QString RhiProber::platformDefault()
{
    return QStringLiteral("opengl");
}

//  fallbackFor - Degradationskette nach einem Fehlschlag
//  vulkan/d3d11/metal -> opengl -> software (Software ist die letzte Stufe).
QString RhiProber::fallbackFor(const QString& backend)
{
    if (backend == u"opengl" || backend == u"software")
        return QStringLiteral("software");
    return QStringLiteral("opengl");
}

//  sanitizeBackend - Namens- und Plattformvalidierung
//  Unbekannte oder plattformfremde Werte (verwaiste/kopierte Configs) fallen
//  auf den Plattform-Standard zurück statt stillschweigend auf Software.
QString RhiProber::sanitizeBackend(QString backend)
{
    backend = backend.trimmed().toLower();
    if (backend == u"software")
        return backend;
#if defined(Q_OS_WIN)
    if (backend == u"d3d11" || backend == u"vulkan" || backend == u"opengl")
        return backend;
#elif defined(Q_OS_MACOS)
    if (backend == u"metal" || backend == u"opengl")
        return backend;
#else
    if (backend == u"vulkan" || backend == u"opengl")
        return backend;
#endif
    return platformDefault();
}

void RhiProber::applyApi(const QString& backend)
{
    QQuickWindow::setGraphicsApi(toApi(backend));
}

// Vor QGuiApplication aufrufen. Steht der Crash-Guard noch, ist der letzte Start
// sofort abgestuerzt -> eine Stufe der Kette zurueckfallen. Korrigierte Werte werden
// zurueckgeschrieben, damit Anzeige und Persistenz dem echten Backend entsprechen.
QString RhiProber::applyStoredBackend()
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));

    const QString guardRaw = s.value(
        QString::fromUtf8(kKeyCrashGuard)).toString().trimmed().toLower();

    if (!guardRaw.isEmpty() && guardRaw != u"software") {
        static const QStringList knownNames = {
            QStringLiteral("vulkan"), QStringLiteral("d3d11"),
            QStringLiteral("metal"),  QStringLiteral("opengl")
        };
        if (knownNames.contains(guardRaw)) {
            // Letzter Start mit diesem Backend endete im Crash -> eine Stufe
            // der Kette zurückfallen (nicht mehr pauschal Software).
            const QString fb = fallbackFor(guardRaw);
            s.setValue(QString::fromUtf8(kKeyBackend),    fb);
            s.setValue(QString::fromUtf8(kKeyFallback),   true);
            // Guard auch für das Fallback setzen: crasht z. B. auch OpenGL
            // sofort, fällt der übernächste Start weiter auf Software zurück.
            s.setValue(QString::fromUtf8(kKeyCrashGuard), fb);
            s.sync();
            applyApi(fb);
            return fb;
        }
        // Verwaister/unbekannter Guard-Wert -> kein Rückschluss möglich,
        // nur aufräumen und normal fortfahren.
        s.remove(QString::fromUtf8(kKeyCrashGuard));
    }

    QString backend = sanitizeBackend(s.value(
        QString::fromUtf8(kKeyBackend), platformDefault()).toString());

#ifndef Q_OS_MACOS
    if (backend == u"vulkan" && !vulkanLoaderAvailable())
        backend = QStringLiteral("opengl");
#endif

    // Korrigierte/gefallene Werte zurückschreiben -> Anzeige und Persistenz
    // sind konsistent zum tatsächlich verwendeten Backend.
    if (backend != s.value(QString::fromUtf8(kKeyBackend)).toString())
        s.setValue(QString::fromUtf8(kKeyBackend), backend);

    s.setValue(QString::fromUtf8(kKeyCrashGuard), backend);
    s.remove(QString::fromUtf8(kKeyFallback));
    s.sync();

    applyApi(backend);
    return backend;
}

void RhiProber::markCleanShutdown()
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));
    s.remove(QString::fromUtf8(kKeyCrashGuard));
    s.sync();
}

void RhiProber::setDesiredBackend(const QString& backend)
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));
    s.setValue(QString::fromUtf8(kKeyBackend), sanitizeBackend(backend));
    s.sync();
}

// Qt meldet `sceneGraphError` nur, wenn der Scene Graph den Fehler NICHT selbst beheben konnte - die laufende
// Sitzung ist auf diesem Gerät nicht mehr zu retten. Fürs nächste Ende wird das sicherere Backend persistiert.
void RhiProber::noteRuntimeFailure()
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));
    const QString cur = sanitizeBackend(s.value(
        QString::fromUtf8(kKeyBackend), platformDefault()).toString());
    if (cur == u"software")
        return;                        // letzte Stufe - nichts mehr zu degradieren
    s.setValue(QString::fromUtf8(kKeyBackend),  fallbackFor(cur));
    s.setValue(QString::fromUtf8(kKeyFallback), true);
    s.sync();
}
