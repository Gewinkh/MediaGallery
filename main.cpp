#include <QGuiApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickImageProvider>
#include <QUrl>
#include <QTimer>                    // verzögertes Löschen des RHI-Crash-Guards nach Start
#include <QtWebEngineQuick>          // WebEngine-Vorschau (HtmlSurface): initialize() vor QGuiApplication

#include "src/RhiProber.h"
#include "src/AppSettings.h"
#include "src/FolderService.h"
#include "src/JsonStorage.h"
#include "src/TagManager.h"
#include "src/AppController.h"
#include "src/TagController.h"
#include "src/ViewerController.h"
#include "src/ThumbnailLoader.h"
#include "src/PdfThumbnailProvider.h"
#include "src/PdfTextController.h"
#include "src/PdfAudioController.h"
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

    // ── Qt WebEngine initialisieren ───────────────────────────────────────────
    // Richtet den zwischen GUI- und Render-Prozess geteilten OpenGL-Kontext ein
    // (entspricht Qt::AA_ShareOpenGLContexts). MUSS vor dem Erzeugen der
    // QGuiApplication aufgerufen werden — danach ist es deprecated und kann
    // fehlschlagen. Wird von HtmlSurface.qml (HTML-Vorschau) benötigt.
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName("MediaGallery");
    app.setOrganizationName("MediaGallery");
    app.setApplicationVersion("1.0.0");

    // ── Schriftart mit CJK-Fallback ───────────────────────────────────────────
    // Hängt eine Familien-Fallbackkette an die Standardschrift, damit Zeichen
    // ohne Glyphe in der Standardfamilie (z.B. japanische/chinesische Zeichen in
    // Dateinamen) aus einer installierten Fallback-Familie gerendert werden statt
    // als Tofu (□). Qt wählt pro Glyphe die erste Familie, die sie besitzt.
    // Plattformübergreifend: Linux (Noto/Source Han), Windows (YaHei/Yu Gothic/
    // Meiryo), macOS (Hiragino). Voraussetzung: mind. eine CJK-Familie installiert
    // (Arch: `noto-fonts-cjk`).
    {
        QFont appFont = app.font();
        const QString primary = appFont.family();
        appFont.setFamilies({
            primary,
            QStringLiteral("Noto Sans"),
            QStringLiteral("Noto Sans CJK JP"),
            QStringLiteral("Noto Sans CJK SC"),
            QStringLiteral("Noto Sans CJK KR"),
            QStringLiteral("Source Han Sans"),
            QStringLiteral("Microsoft YaHei"),
            QStringLiteral("Yu Gothic"),
            QStringLiteral("Meiryo"),
            QStringLiteral("Hiragino Sans"),
            QStringLiteral("Hiragino Kaku Gothic ProN"),
            QStringLiteral("sans-serif")
        });
        app.setFont(appFont);
    }

    // Settings — einzige konkrete Instanz, als ISettings& weitergereicht
    AppSettings& settings = AppSettings::instance();

    // Persistenz- und Service-Schicht
    JsonStorage   storage;
    FolderService folderService(settings, storage);
    TagManager    tagManager(&storage);

    // QML-Bridges
    AppController       appController(settings, folderService, storage, tagManager);
    TagController       tagController(tagManager);
    ViewerController    viewerController;
    PdfThumbnailProvider pdfThumbs;
    PdfTextController    pdfText;
    PdfAudioController   pdfAudio;

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

    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "App",       &appController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Settings",  &settings);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Tags",      &tagController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Viewer",    &viewerController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfThumbs", &pdfThumbs);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfText",   &pdfText);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfAudio",  &pdfAudio);

    // ── QML-Wurzel ───────────────────────────────────────────────────────────
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("galleryModel", &galleryModel);
    engine.rootContext()->setContextProperty("mediaModel",   &mediaModel);

    // RAM-Vorschauen der PDF-Seitenleiste: "image://pdfthumb/<docId>/<page>".
    // Eigentum des Providers geht an die Engine ueber.
    engine.addImageProvider(QStringLiteral("pdfthumb"), pdfThumbs.createImageProvider());

    engine.load(QUrl(QStringLiteral("qrc:/qml/ApplicationShell.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    // ── Crash-Guard früh entschärfen (WICHTIG seit WebEngine-Vorschau) ────────
    // Der RHI-Crash-Guard soll NUR Backends abfangen, die gar nicht starten/
    // rendern können — solche Defekte schlagen sofort beim Start zu (< wenige
    // Sekunden). Ein SPÄTERER Crash ist kein Backend-Defekt und darf den Guard
    // nicht auslösen. Qt WebEngine stürzt auf manchen Linux-Grafikstacks beim
    // Teardown (App schließen) ab; läge das Löschen des Guards — wie zuvor —
    // erst NACH app.exec(), bliebe der Guard nach so einem Crash gesetzt und der
    // nächste Start würde fälschlich auf Software-Rendering zurückfallen
    // (= massiver Lag der HTML-Vorschau, Endlosschleife). Deshalb den Guard
    //   (a) kurz nach erfolgreichem Start  und
    //   (b) beim regulären Beenden (aboutToQuit, VOR dem Teardown)
    // löschen — ein WebEngine-Schließen-Crash oder ein per kill beendeter
    // Hänger kann dann nie wieder Software erzwingen.
    QTimer::singleShot(4000, &app, [] { RhiProber::markCleanShutdown(); });
    QObject::connect(&app, &QGuiApplication::aboutToQuit,
                     &app, [] { RhiProber::markCleanShutdown(); });

    const int ret = app.exec();

    // Fallback für Exit-Pfade ohne aboutToQuit.
    RhiProber::markCleanShutdown();
    return ret;
}
