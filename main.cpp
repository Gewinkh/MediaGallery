#include <QGuiApplication>
#include <QFont>
#include <QElapsedTimer>
#include <QFile>
#include <QAbstractItemModel>
#include <QQmlComponent>
#include <QQmlApplicationEngine>
#include <QStyleHints>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QPalette>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QUrl>
#include <QTimer>
#include <QDebug>
#include <QKeyEvent>
#include <QShortcutEvent>

#include "core/RhiProber.h"
#include "core/FileBrowseModel.h"
#include "core/AppSettings.h"
#include "audio/AudioController.h"
#include "audio/AudioCoverProvider.h"
#include "app/QmlTypes.h"
#include "app/PaneController.h"
#include "app/PaneHost.h"
#include "tags/TagController.h"
#include "media/FolderService.h"
#include "core/JsonStorage.h"
#include "tags/TagManager.h"
#include "app/AppController.h"
#include "tags/TagController.h"
#include "app/ViewerController.h"
#include "media/ThumbnailLoader.h"
#include "pdf/PdfThumbnailProvider.h"
#include "pdf/PdfTextController.h"
#include "pdf/PdfAudioController.h"
#include "pdf/edit/PdfEditController.h"
#include "pdf/extract/PdfExtractController.h"
#include "image/edit/ImageEditController.h"
#include "docx/DocxController.h"
#include "docx/edit/DocxEditController.h"
#include "docx/edit/DocxTextArea.h"
#include "app/TransliterationController.h"
#include "app/WebEngineController.h"
#include "editor/EditorController.h"
#include "editor/CodeHighlighter.h"
#include "editor/TextGutter.h"
#include "editor/TextDecorations.h"
#include "editor/TextFoldBar.h"
#include "editor/TextMinimap.h"
#include "media/MediaModel.h"
#include "media/MediaProxyModel.h"
#include "media/GalleryRowModel.h"

namespace {
QElapsedTimer  g_startClock;
bool           g_startLog = false;

// grobe Speichermessung
long startRssKb() {
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray& line : f.readAll().split('\n'))
        if (line.startsWith("VmRSS:"))
            return line.mid(6).trimmed().split(' ').first().toLong();
    return -1;
}

void startMark(const char* what) {
    if (!g_startLog) return;
    qInfo("[START] %-32s %6lld ms   RSS %7ld KB", what,
          static_cast<long long>(g_startClock.elapsed()), startRssKb());
}

class KeyLogger : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        const QEvent::Type t = ev->type();
        if (t == QEvent::KeyPress || t == QEvent::ShortcutOverride) {
            const auto* k = static_cast<QKeyEvent*>(ev);
            qInfo("[KEY] %-16s key=0x%08X mods=0x%08X text=\"%s\" -> %s%s",
                  t == QEvent::KeyPress ? "KeyPress" : "ShortcutOverride",
                  k->key(), unsigned(k->modifiers().toInt()),
                  qPrintable(k->text()),
                  watched->metaObject()->className(),
                  ev->isAccepted() ? "  [bereits angenommen]" : "");
            // Tastaturfokus - greift nur, solange die Ansicht ihn hat
            if (t == QEvent::KeyPress) {
                if (auto* w = qobject_cast<QQuickWindow*>(watched)) {
                    QQuickItem* f = w->activeFocusItem();
                    QObject*    o = f ? f->parent() : nullptr;
                    qInfo("[KEY]     Fokus: %s   (gehoert zu: %s)   Fenster aktiv: %s",
                          f ? f->metaObject()->className() : "(keins)",
                          o ? o->metaObject()->className() : "-",
                          w->isActive() ? "ja" : "nein");
                }
            }
        } else if (t == QEvent::Shortcut) {
            const auto* sc = static_cast<QShortcutEvent*>(ev);
            qInfo("[KEY] Shortcut         \"%s\" ausgeloest -> %s",
                  qPrintable(sc->key().toString()),
                  watched->metaObject()->className());
        }
        return false;                       // NIE verbrauchen - nur zusehen
    }
};
}  // namespace

int main(int argc, char* argv[]) {
    g_startClock.start();
    g_startLog = qEnvironmentVariableIsSet("MG_STARTLOG");
    startMark("main betreten");

    // Vor der QGuiApplication - danach ignoriert Qt die Backend-Wahl.
    RhiProber::applyStoredBackend();

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QQuickStyle::setStyle(QStringLiteral("style"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));

    // Eigener Dateidialog statt des nativen.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    // Muss vor der QGuiApplication stehen; laedt hier noch kein Chromium.
    // WebEngine selbst wird erst beim ersten HTML gestartet.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);


    QGuiApplication app(argc, argv);
    startMark("QGuiApplication steht");

    if (qEnvironmentVariableIsSet("MG_KEYLOG")) {
        app.installEventFilter(new KeyLogger(&app));
        qInfo("[KEY] Mitschnitt an - jeder Tastendruck wird protokolliert.");
    }
    app.setApplicationName("MediaGallery");
    app.setOrganizationName("MediaGallery");
    app.setApplicationVersion("1.0.0");

    // Erst nach der QGuiApplication - vorher gibt es keine styleHints.
    app.styleHints()->setWheelScrollLines(6);

    {
        QFont appFont = app.font();
        const QString primary = appFont.family();
        appFont.setFamilies({
            primary,
            QStringLiteral("Noto Sans"),
            QStringLiteral("Amiri"),
            QStringLiteral("Noto Naskh Arabic"),
            QStringLiteral("Noto Sans Arabic"),
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

    AppSettings& settings = AppSettings::instance();

    // Ein Lader fuer alle Haelften.
    ThumbnailLoader thumbLoader;

    AppController       appController(settings);
    appController.setThumbnailLoader(&thumbLoader);
    auto* pane0 = qobject_cast<PaneController*>(appController.addPane());
    ViewerController    viewerController;
    PdfThumbnailProvider pdfThumbs;
    // Editor-Zustand liegt je Kachel in PdfSurface; hier nur die globale Einstellung.
    PdfEditController    pdfEdit(settings);
    PdfExtractController pdfExtract;
    // Editor-Zustand liegt je Kachel; hier nur das globale Speicherverhalten.
    DocxController       docx(settings);
    // Faerbung laeuft je Editor-Kachel; das Singleton haelt nur Profil und Palette.
    mg::editor::EditorController editor(settings);
    mg::editor::setActiveController(&editor);
    // Eine Wiedergabe fuer die ganze App.
    AudioController      audio(settings);
    TransliterationController translit;   // Live-Transliteration (Latein -> Arabisch/Kana)
    WebEngineController  webEngine;       // lazy WebEngine-Init (nur bei HTML-Bedarf)

    MediaModel&      mediaModel   = pane0->mediaModel();
    MediaProxyModel& galleryModel = pane0->galleryModel();

    QObject::connect(&appController, &AppController::showAllFilesChanged,
                     &mediaModel, [&settings, &mediaModel]() {
        mediaModel.setShowAllFiles(settings.showAllFiles());
    });

    // Kacheln ueber 512 px wuerden sonst aus der 512er-Cachedatei hochskaliert.
    thumbLoader.setTargetDim(qMax(settings.tileWidth(), settings.tileHeight()));
    QObject::connect(&appController, &AppController::tileSizeChanged,
                     &mediaModel, [&settings, &thumbLoader, &mediaModel]() {
        if (thumbLoader.setTargetDim(qMax(settings.tileWidth(),
                                          settings.tileHeight())))
            mediaModel.refreshThumbnails();
    });

    // Der Stil steckt in der Cache-Datei - eine Aenderung erzwingt Neuerzeugung.
    const auto textStilSetzen = [&settings, &editor, &thumbLoader, &mediaModel]() {
        if (thumbLoader.setTextPreviewStyle(settings.textPreviewContent(),
                                            editor.palette()))
            mediaModel.refreshThumbnails();
    };
    textStilSetzen();                       // Startwert, vor der ersten Kachel
    QObject::connect(&appController, &AppController::textPreviewContentChanged,
                     &mediaModel, textStilSetzen);
    QObject::connect(&editor, &mg::editor::EditorController::paletteChanged,
                     &mediaModel, textStilSetzen);

    // Standard-Controls zeichnen aus der QPalette, nicht aus dem QML-Theme.
    auto applyThemePalette = [&settings]() {
        const ThemeColors t = settings.currentTheme();
        const QColor bg     = t.background;
        const QColor card   = t.card;
        const QColor text   = t.textPrimary;
        const QColor muted  = t.textMuted;
        const QColor border = t.border;
        const QColor accent = t.accent;
        const QColor onAccent = accent.lightnessF() > 0.55 ? QColor(Qt::black) : QColor(Qt::white);

        QPalette p;
        p.setColor(QPalette::Window,          bg);
        p.setColor(QPalette::WindowText,      text);
        p.setColor(QPalette::Base,            card);
        p.setColor(QPalette::AlternateBase,   bg);
        p.setColor(QPalette::Text,            text);
        p.setColor(QPalette::Button,          card);
        p.setColor(QPalette::ButtonText,      text);
        p.setColor(QPalette::BrightText,      onAccent);
        p.setColor(QPalette::Highlight,       accent);
        p.setColor(QPalette::HighlightedText, onAccent);
        p.setColor(QPalette::ToolTipBase,     card);
        p.setColor(QPalette::ToolTipText,     text);
        p.setColor(QPalette::PlaceholderText, muted);
        p.setColor(QPalette::Link,            accent);
        p.setColor(QPalette::LinkVisited,     accent.darker(120));
        p.setColor(QPalette::Mid,             border);
        p.setColor(QPalette::Midlight,        border.lighter(120));
        p.setColor(QPalette::Light,           card.lighter(130));
        p.setColor(QPalette::Dark,            bg.darker(120));
        p.setColor(QPalette::Shadow,          bg.darker(160));
        p.setColor(QPalette::Accent,          accent);
        p.setColor(QPalette::Disabled, QPalette::WindowText, muted);
        p.setColor(QPalette::Disabled, QPalette::Text,       muted);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
        QGuiApplication::setPalette(p);
    };
    applyThemePalette();
    QObject::connect(&appController, &AppController::themeChanged, &app, applyThemePalette);

    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "App",       &appController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Settings",  &settings);
    // Fassade fuer alles ausserhalb der Haelften; drinnen ueberdeckt der eigene TagController.
    static TagController tagsFacade(pane0->tagManager());
    appController.setTagsFacade(&tagsFacade);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Tags",      &tagsFacade);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Viewer",    &viewerController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfThumbs", &pdfThumbs);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfEdit",   &pdfEdit);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfExtract", &pdfExtract);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Docx",      &docx);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Editor",    &editor);

    // Dieselbe Liste nutzt bench_shell.
    mg::registerQmlTypes();
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Audio",     &audio);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Translit",  &translit);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "WebEngine", &webEngine);

    startMark("Controller/Modelle stehen");

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(":/qml"));
    engine.rootContext()->setContextProperty("galleryModel", &galleryModel);
    engine.rootContext()->setContextProperty("mediaModel",   &mediaModel);

    // image://pdfthumb/<docId>/<page>
    engine.addImageProvider(QStringLiteral("pdfthumb"), pdfThumbs.createImageProvider());

    // image://audiocover/<pfad>?rev=<n>
    engine.addImageProvider(QStringLiteral("audiocover"), new AudioCoverProvider);

    if (g_startLog) {
        QQmlComponent probe(&engine);
        probe.setData("import QtQuick\nimport QtQuick.Controls\nItem {}",
                      QUrl(QStringLiteral("qrc:/startprobe.qml")));
        delete probe.create();
        startMark("Engine + QtQuick bereit");
    }

    engine.load(QUrl(QStringLiteral("qrc:/qml/ApplicationShell.qml")));
    startMark("QML geladen (Shell steht)");
    if (engine.rootObjects().isEmpty())
        return -1;

    if (g_startLog) {
        if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
            auto* c = new QMetaObject::Connection;
            *c = QObject::connect(w, &QQuickWindow::frameSwapped, &app, [c] {
                startMark("erster Rahmen gezeichnet");
                QObject::disconnect(*c);
                delete c;
            });
        }
        auto* cm = new QMetaObject::Connection;
        *cm = QObject::connect(&mediaModel, &QAbstractItemModel::rowsInserted, &app,
                               [cm](const QModelIndex&, int, int) {
            startMark("erste Kachel im Modell");
            QObject::disconnect(*cm);
            delete cm;
        });
        QObject::connect(&app, &QGuiApplication::aboutToQuit, &app, [] {
            startMark("Beenden beginnt (aboutToQuit)");
        });
    }

    // Ohne Slot beendet Qt bei einem Scene-Graph-Fehler hart; hier stattdessen ein
    // sichereres Backend fuer den naechsten Start merken.
    if (auto* rootWin = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        QObject::connect(rootWin, &QQuickWindow::sceneGraphError, &app,
                         [](QQuickWindow::SceneGraphError, const QString& message) {
                             qWarning().noquote()
                                 << "MediaGallery: Scene-Graph-Fehler:" << message
                                 << "- der nächste Start nutzt ein sichereres Render-Backend.";
                             RhiProber::noteRuntimeFailure();
                         });
    }

    if (const int autoQuit = qEnvironmentVariableIntValue("MG_AUTOQUIT"); autoQuit > 0)
        QTimer::singleShot(autoQuit, &app, [] {
            startMark("Beenden angefordert");
            QGuiApplication::quit();
        });

    // Guard frueh loeschen: ein spaeterer Crash (WebEngine-Teardown) ist kein Backend-Defekt.
    QTimer::singleShot(4000, &app, [] { RhiProber::markCleanShutdown(); });
    QObject::connect(&app, &QGuiApplication::aboutToQuit,
                     &app, [] { RhiProber::markCleanShutdown(); });

    const int ret = app.exec();
    startMark("Ereignisschleife verlassen");

    // Fallback für Exit-Pfade ohne aboutToQuit.
    RhiProber::markCleanShutdown();
    startMark("main verlassen (Abbau folgt)");
    return ret;
}
