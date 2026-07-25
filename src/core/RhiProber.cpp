#include "core/RhiProber.h"

#include <QLibrary>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QStringList>

// ─────────────────────────────────────────────────────────────────────────────
//  Hilfsfunktion: String → GraphicsApi
//  (Erreicht nur noch VALIDIERTE Namen — sanitizeBackend läuft immer davor.)
// ─────────────────────────────────────────────────────────────────────────────
static QSGRendererInterface::GraphicsApi toApi(const QString& name)
{
    if (name == u"vulkan")   return QSGRendererInterface::Vulkan;
    if (name == u"d3d11")    return QSGRendererInterface::Direct3D11;
    if (name == u"metal")    return QSGRendererInterface::Metal;
    if (name == u"opengl")   return QSGRendererInterface::OpenGL;
    return QSGRendererInterface::Software;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Vulkan-Loader-Vorabprüfung (nur dort, wo Vulkan wählbar ist)
//
//  Billige dlopen-Probe auf die Loader-Bibliothek — KEINE Instanz-Erzeugung
//  (zu teuer, und vor der QGuiApplication auch zu früh). Fehlt der Loader
//  (z. B. Linux ohne vulkan-icd-loader), würde ein Vulkan-Start sofort
//  scheitern und erst der Crash-Guard-Zyklus (Crash → Neustart → Fallback)
//  zu OpenGL führen — die Vorabprüfung erspart diesen Umweg komplett.
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
//  platformDefault — sicherer Ausgangspunkt je Plattform
//  OpenGL: auf allen drei Zielplattformen vorhanden und zugleich das
//  robusteste Backend für die WebEngine-Vorschau (s. Projektüberblick).
// ─────────────────────────────────────────────────────────────────────────────
QString RhiProber::platformDefault()
{
    return QStringLiteral("opengl");
}

// ─────────────────────────────────────────────────────────────────────────────
//  fallbackFor — Degradationskette nach einem Fehlschlag
//  vulkan/d3d11/metal → opengl → software (Software ist die letzte Stufe).
// ─────────────────────────────────────────────────────────────────────────────
QString RhiProber::fallbackFor(const QString& backend)
{
    if (backend == u"opengl" || backend == u"software")
        return QStringLiteral("software");
    return QStringLiteral("opengl");
}

// ─────────────────────────────────────────────────────────────────────────────
//  sanitizeBackend — Namens- und Plattformvalidierung
//  Unbekannte oder plattformfremde Werte (verwaiste/kopierte Configs) fallen
//  auf den Plattform-Standard zurück statt stillschweigend auf Software.
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
//  applyApi — setzt das Backend
// ─────────────────────────────────────────────────────────────────────────────
void RhiProber::applyApi(const QString& backend)
{
    QQuickWindow::setGraphicsApi(toApi(backend));
}

// ─────────────────────────────────────────────────────────────────────────────
//  applyStoredBackend — Haupt-Einstiegspunkt, VOR QGuiApplication aufrufen
//
//  Ablauf:
//  1. Crash-Guard prüfen: Ist „lastStartedWith" noch gesetzt, ist die App beim
//     letzten Start mit diesem Backend sofort abgestürzt → EINE Stufe der
//     Degradationskette zurückfallen (persistiert, Guard fürs Fallback neu
//     gesetzt — crasht auch das Fallback sofort, geht es weiter Richtung
//     Software). Verwaiste Guard-Werte (unbekannter Name) werden ignoriert
//     und gelöscht statt fälschlich einen Fallback auszulösen.
//  2. Gewünschtes Backend aus „rhi/backend" lesen und validieren (Name +
//     Plattform, Standard: opengl); für Vulkan zusätzlich die Loader-Probe.
//  3. Backend setzen + Crash-Guard schreiben. Korrigierte Werte werden
//     zurückgeschrieben, damit Anzeige (Settings.rhiBackend) und Persistenz
//     immer dem tatsächlich verwendeten Backend entsprechen.
// ─────────────────────────────────────────────────────────────────────────────
QString RhiProber::applyStoredBackend()
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));

    // ── Crash-Guard prüfen ────────────────────────────────────────────────
    const QString guardRaw = s.value(
        QString::fromUtf8(kKeyCrashGuard)).toString().trimmed().toLower();

    if (!guardRaw.isEmpty() && guardRaw != u"software") {
        static const QStringList knownNames = {
            QStringLiteral("vulkan"), QStringLiteral("d3d11"),
            QStringLiteral("metal"),  QStringLiteral("opengl")
        };
        if (knownNames.contains(guardRaw)) {
            // Letzter Start mit diesem Backend endete im Crash → eine Stufe
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
        // Verwaister/unbekannter Guard-Wert → kein Rückschluss möglich,
        // nur aufräumen und normal fortfahren.
        s.remove(QString::fromUtf8(kKeyCrashGuard));
    }

    // ── Gewünschtes Backend laden + validieren ────────────────────────────
    QString backend = sanitizeBackend(s.value(
        QString::fromUtf8(kKeyBackend), platformDefault()).toString());

#ifndef Q_OS_MACOS
    // Vulkan ohne Loader gar nicht erst versuchen (spart den Guard-Zyklus).
    if (backend == u"vulkan" && !vulkanLoaderAvailable())
        backend = QStringLiteral("opengl");
#endif

    // Korrigierte/gefallene Werte zurückschreiben → Anzeige und Persistenz
    // sind konsistent zum tatsächlich verwendeten Backend.
    if (backend != s.value(QString::fromUtf8(kKeyBackend)).toString())
        s.setValue(QString::fromUtf8(kKeyBackend), backend);

    // ── Crash-Guard setzen (wird beim sauberen Ende wieder gelöscht) ──────
    s.setValue(QString::fromUtf8(kKeyCrashGuard), backend);
    s.remove(QString::fromUtf8(kKeyFallback));
    s.sync();

    applyApi(backend);
    return backend;
}

// ─────────────────────────────────────────────────────────────────────────────
//  markCleanShutdown — Crash-Guard löschen
// ─────────────────────────────────────────────────────────────────────────────
void RhiProber::markCleanShutdown()
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));
    s.remove(QString::fromUtf8(kKeyCrashGuard));
    s.sync();
}

// ─────────────────────────────────────────────────────────────────────────────
//  setDesiredBackend — Backend für nächsten Start speichern (validiert)
// ─────────────────────────────────────────────────────────────────────────────
void RhiProber::setDesiredBackend(const QString& backend)
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));
    s.setValue(QString::fromUtf8(kKeyBackend), sanitizeBackend(backend));
    s.sync();
}

// ─────────────────────────────────────────────────────────────────────────────
//  noteRuntimeFailure — Laufzeit-Guard (Gerätewechsel/Device-Lost)
//
//  Qt meldet sceneGraphError nur, wenn der Scene Graph den Fehler NICHT
//  selbst beheben konnte — die laufende Sitzung ist auf diesem Gerät nicht
//  mehr zu retten. Hier wird deshalb fürs NÄCHSTE Programm-Ende das
//  nächstsicherere Backend persistiert; der Neustart läuft damit garantiert
//  wieder (schlimmstenfalls Software).
// ─────────────────────────────────────────────────────────────────────────────
void RhiProber::noteRuntimeFailure()
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));
    const QString cur = sanitizeBackend(s.value(
        QString::fromUtf8(kKeyBackend), platformDefault()).toString());
    if (cur == u"software")
        return;                        // letzte Stufe — nichts mehr zu degradieren
    s.setValue(QString::fromUtf8(kKeyBackend),  fallbackFor(cur));
    s.setValue(QString::fromUtf8(kKeyFallback), true);
    s.sync();
}
