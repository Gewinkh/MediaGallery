#include <QGuiApplication>
#include <QTranslator>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QUrl>

#include "src/RhiProber.h"
#include "src/AppSettings.h"
#include "src/FolderService.h"
#include "src/JsonStorage.h"
#include "src/TagManager.h"
#include "src/AppController.h"
#include "src/TagController.h"
#include "src/ViewerController.h"
#include "src/ThumbnailLoader.h"
#include "src/MediaModel.h"
#include "src/MediaProxyModel.h"

int main(int argc, char* argv[]) {
    // ── RHI-Backend setzen ────────────────────────────────────────────────────
    // Muss VOR allen Qt-Klassen aufgerufen werden.
    // Liest das gewählte Backend aus QSettings, prüft den Crash-Guard
    // (→ automatischer Software-Fallback nach Crash) und ruft
    // QQuickWindow::setGraphicsApi() auf.
    RhiProber::applyStoredBackend();

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QGuiApplication app(argc, argv);
    app.setApplicationName("MediaGallery");
    app.setOrganizationName("MediaGallery");
    app.setApplicationVersion("1.0.0");

    // Settings — einzige konkrete Instanz, als ISettings& weitergereicht
    AppSettings& settings = AppSettings::instance();

    QTranslator translator;
    const Language lang = settings.language();
    const QString qmPath = (lang == Language::German)
                         ? QStringLiteral(":/translations/mediagallery_de")
                         : QStringLiteral(":/translations/mediagallery_en");
    if (translator.load(qmPath))
        app.installTranslator(&translator);

    // Persistenz- und Service-Schicht
    JsonStorage   storage;
    FolderService folderService(settings, storage);
    TagManager    tagManager(&storage);

    // QML-Bridges
    AppController    appController(settings, folderService, storage, tagManager);
    TagController    tagController(tagManager);
    ViewerController viewerController;

    // ── Galerie-Backend ──────────────────────────────────────────────────────
    ThumbnailLoader  thumbLoader;
    MediaModel       mediaModel(storage, tagManager, thumbLoader);
    MediaProxyModel  galleryModel;
    galleryModel.setSourceModel(&mediaModel);
    galleryModel.setTagManager(&tagManager);

    QObject::connect(&appController, &AppController::folderOpened,
                     &mediaModel, &MediaModel::loadFolder);
    QObject::connect(&appController, &AppController::folderContentsChanged,
                     &mediaModel, &MediaModel::reload);

    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "App",      &appController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Settings", &settings);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Tags",     &tagController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Viewer",   &viewerController);

    // ── QML-Wurzel ───────────────────────────────────────────────────────────
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("galleryModel", &galleryModel);
    engine.rootContext()->setContextProperty("mediaModel",   &mediaModel);

    engine.load(QUrl(QStringLiteral("qrc:/qml/ApplicationShell.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    const int ret = app.exec();

    // ── Sauberes Ende: Crash-Guard löschen ───────────────────────────────────
    RhiProber::markCleanShutdown();
    return ret;
}
