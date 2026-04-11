#include <QApplication>
#include <QTranslator>
#include "src/MainWindow.h"
#include "src/AppSettings.h"
#include "src/FolderService.h"
#include "src/JsonStorage.h"
#include "src/Style.h"

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("MediaGallery");
    app.setOrganizationName("MediaGallery");
    app.setApplicationVersion("1.0.0");

    // Settings — single concrete instance, exposed as ISettings& to consumers
    AppSettings& settings = AppSettings::instance();

    // Apply initial theme before showing the window
    app.setStyleSheet(Style::mainStyleSheet(
        settings.backgroundColor(),
        settings.accentColor()));

    // Install translator
    QTranslator translator;
    Language lang = settings.language();
    QString qmPath = (lang == Language::German)
                         ? ":/translations/mediagallery_de"
                         : ":/translations/mediagallery_en";
    if (translator.load(qmPath))
        app.installTranslator(&translator);

    // Persistence layer (JSON project data)
    JsonStorage storage;

    // Application service: owns folder-open coordination
    FolderService folderService(settings, storage);

    // UI — receives abstractions, not concrete singletons
    MainWindow w(settings, folderService, storage);
    w.show();

    return app.exec();
}
