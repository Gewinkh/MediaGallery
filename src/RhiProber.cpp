#include "RhiProber.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>

// ─────────────────────────────────────────────────────────────────────────────
//  Hilfsfunktion: String → GraphicsApi
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
//  1. Crash-Guard prüfen: Wenn „lastStartedWith" noch gesetzt ist, ist die
//     App beim letzten Mal mit diesem Backend abgestürzt → Software erzwingen.
//  2. Gewünschtes Backend aus „rhi/backend" lesen (Standard: opengl).
//  3. Backend setzen + Crash-Guard schreiben.
// ─────────────────────────────────────────────────────────────────────────────
QString RhiProber::applyStoredBackend()
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));

    // ── Crash-Guard prüfen ────────────────────────────────────────────────
    const QString lastCrashed = s.value(
        QString::fromUtf8(kKeyCrashGuard)).toString();

    if (!lastCrashed.isEmpty() && lastCrashed != u"software") {
        // Letzter Start mit diesem Backend endete im Crash → Software-Fallback
        s.setValue(QString::fromUtf8(kKeyBackend),    QStringLiteral("software"));
        s.setValue(QString::fromUtf8(kKeyFallback),   true);
        s.remove(QString::fromUtf8(kKeyCrashGuard));
        s.sync();
        applyApi(QStringLiteral("software"));
        return QStringLiteral("software");
    }

    // ── Gewünschtes Backend laden ─────────────────────────────────────────
    // Standard: opengl — sicherer plattformübergreifender Ausgangspunkt
    const QString backend = s.value(
        QString::fromUtf8(kKeyBackend),
        QStringLiteral("opengl")).toString().toLower();

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
//  setDesiredBackend — Backend für nächsten Start speichern
// ─────────────────────────────────────────────────────────────────────────────
void RhiProber::setDesiredBackend(const QString& backend)
{
    QSettings s(QStringLiteral("MediaGallery"), QStringLiteral("MediaGallery"));
    s.setValue(QString::fromUtf8(kKeyBackend), backend.toLower());
    s.sync();
}
