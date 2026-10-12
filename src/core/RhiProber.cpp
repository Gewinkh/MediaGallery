#include "core/RhiProber.h"

#include <QFileInfo>
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

    QString guardRaw = s.value(
        QString::fromUtf8(kKeyCrashGuard)).toString().trimmed().toLower();

#if defined(Q_OS_LINUX)
    // Stand beim abgestuerzten Start die Mesa-Weiche, faellt zuerst nur sie weg - das Backend bleibt.
    if (s.value(QString::fromUtf8(kKeyEglPinned)).toBool()) {
        s.remove(QString::fromUtf8(kKeyEglPinned));
        if (!guardRaw.isEmpty()) {
            s.remove(QString::fromUtf8(kKeyLastMesa));
            s.remove(QString::fromUtf8(kKeyCrashGuard));
            guardRaw.clear();
        }
    }
#endif

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

#if defined(Q_OS_LINUX)
    // GLVND fragt sonst jeden Anbieter ab und laedt NVIDIA mit, obwohl auf Mesa gezeichnet wird (70 MB RSS).
    if (backend == u"opengl") {
        const QString datei = mesaPinFile(s.value(QString::fromUtf8(kKeyLastMesa)).toBool(),
                                          { QStringLiteral("/usr/share/glvnd/egl_vendor.d/50_mesa.json"),
                                            QStringLiteral("/etc/glvnd/egl_vendor.d/50_mesa.json") });
        if (!datei.isEmpty()) {
            qputenv("__EGL_VENDOR_LIBRARY_FILENAMES", QFile::encodeName(datei));
            s.setValue(QString::fromUtf8(kKeyEglPinned), true);
        }
    }
#endif

    s.setValue(QString::fromUtf8(kKeyCrashGuard), backend);
    s.remove(QString::fromUtf8(kKeyFallback));
    s.sync();

    applyApi(backend);
    return backend;
}

QString RhiProber::mesaPinFile(bool lastMesa, const QStringList& candidates)
{
    if (!lastMesa)
        return {};
    // Wer selbst einen Anbieter oder die zweite Karte waehlt, behaelt seine Wahl.
    for (const char* v : { "__EGL_VENDOR_LIBRARY_FILENAMES", "__EGL_VENDOR_LIBRARY_DIRS",
                           "__NV_PRIME_RENDER_OFFLOAD", "DRI_PRIME" })
        if (qEnvironmentVariableIsSet(v))
            return {};
    for (const QString& c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return {};
}

void RhiProber::noteRenderer(const QString& glVersion, const QString& glRenderer)
{
    // llvmpipe/softpipe melden auch Mesa, sind aber Software - darauf darf die Weiche nicht festhalten.
    const bool mesa = glVersion.contains(u"Mesa") && !glRenderer.contains(u"llvmpipe")
                      && !glRenderer.contains(u"softpipe");
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));
    if (s.value(QString::fromUtf8(kKeyLastMesa)).toBool() == mesa && s.contains(QString::fromUtf8(kKeyLastMesa)))
        return;
    s.setValue(QString::fromUtf8(kKeyLastMesa), mesa);
    s.sync();
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
