#include <QGuiApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QStyleHints>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QPalette>
#include <QQuickImageProvider>
#include <QQuickWindow>              // Laufzeit-Guard: sceneGraphError (GPU-Wechsel/Device-Lost)
#include <QUrl>
#include <QTimer>                    // verzögertes Löschen des RHI-Crash-Guards nach Start
#include <QDebug>                    // Warnung bei Scene-Graph-Laufzeitfehlern

#include "core/RhiProber.h"
#include "core/FileBrowseModel.h"
#include "core/AppSettings.h"
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
#include "media/MediaModel.h"
#include "media/MediaProxyModel.h"

int main(int argc, char* argv[]) {
    // ── RHI-Backend setzen ────────────────────────────────────────────────────
    // Muss VOR allen Qt-Klassen aufgerufen werden.
    // Liest das gewählte Backend aus QSettings, prüft den Crash-Guard
    // (→ automatischer Software-Fallback nach Crash) und ruft
    // QQuickWindow::setGraphicsApi() auf.
    RhiProber::applyStoredBackend();

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    //  Eigener, vollständig gethemter Control-Stil (qml/style).
    //  Der Stilname MUSS dem Verzeichnisnamen entsprechen, in dem die Stil-
    //  Dateien liegen — deshalb "style" (Suchpfad ":/qml", s. addImportPath).
    //  Nicht abgedeckte Controls fallen auf Fusion zurück (setFallbackStyle).
    QQuickStyle::setStyle(QStringLiteral("style"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));

    //  Datei-/Ordnerdialoge NICHT nativ öffnen.
    //  Der native Dialog (auf dem Linux-Desktop der GTK-/Portal-Dialog) folgt
    //  ausschließlich den Systemfarben — im dunklen App-Theme sitzt dann ein
    //  helles Fremdfenster mitten in der Anwendung, und seine Liste scrollt in
    //  festen Rastungen. Er liest KEINE Qt-Palette, ist also von hier aus
    //  überhaupt nicht einfärbbar. Qts eigene QML-Fassung dagegen erbt die
    //  Palette ihres Elternfensters — und die setzt ApplicationShell.qml
    //  bereits aus dem App-Theme. EINE Zeile deckt damit alle sieben
    //  Dialog-Stellen ab (Ordner öffnen, Lesezeichen, Design-Import/-Export,
    //  PDF-Bild/-Anhang, DOCX-Bild).
    //  Preis, bewusst in Kauf genommen: die Desktop-Integration des nativen
    //  Dialogs (GTK-Lesezeichen, zuletzt benutzte Orte) entfällt.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    // ── Qt WebEngine: NICHT mehr beim Start initialisieren (RAM-Baseline) ────
    // Viele Nutzer öffnen nie eine HTML-Datei — die Chromium-Grundkosten von
    // QtWebEngineQuick::initialize() beim Start wären reine Verschwendung.
    // Hier wird nur das kostenlose Kontext-Sharing-Attribut gesetzt (MUSS vor
    // der QGuiApplication passieren, lädt KEIN Chromium). Die eigentliche
    // Initialisierung übernimmt WebEngineController::ensureInitializedForHtml()
    // lazy beim ersten Öffnen einer .html/.htm bzw. bei angeforderter Vorschau.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // ── Plattform-Theme: bewusst das des Desktops ────────────────────────────
    //  Eine Zwischenstufe erzwang hier `QT_QPA_PLATFORMTHEME=generic`, um die
    //  Alt-Mnemoniks der Qt-Quick-`MenuBar` loszuwerden. Das ist nicht mehr nötig:
    //  die Menüleiste ist inzwischen eine eigene, per Klick bediente Button-Reihe
    //  (s. ApplicationShell.qml) — die Alt-Taste gehört damit ohnehin allein den
    //  App-Kürzeln, und die App vergibt selbst keine „&"-Mnemoniks.
    //  Das generische Theme hatte zwei unerwünschte Nebenwirkungen:
    //    • Datei-/Farbdialoge fielen auf Qts eingebaute QML-Variante zurück
    //      (fremde Optik, ungenutzte Leerflächen) statt der desktop-nativen;
    //    • die Anwendungspalette kam aus Qts hellem Standardschema — helle
    //      Standard-Controls in der dunklen App.
    //  Beides entfällt mit dem Desktop-Theme. Von dessen Palette ist die App
    //  unabhängig: sie setzt ihre QPalette weiter unten aus dem eigenen
    //  Farbschema (applyThemePalette).

    QGuiApplication app(argc, argv);
    app.setApplicationName("MediaGallery");
    app.setOrganizationName("MediaGallery");
    app.setApplicationVersion("1.0.0");

    // ── Rad-Schrittweite von Qts EIGENEN Dialogen ─────────────────────────────
    // Der Datei-/Ordnerdialog ist Qts eigener (AA_DontUseNativeDialogs, s.o.);
    // seine Dateiliste ist eine schlichte QQuickListView und kroch mit Qts
    // Vorgabe von 3 Zeilen je Rastung durch lange Ordner. Gemessen an genau
    // dieser Liste (Höhe 329, contentHeight 5400): 3 Zeilen = 72 px, 6 = 144 px,
    // 12 = 288 px je Rastung — der Wert wirkt also linear.
    // MUSS nach der QGuiApplication stehen (vorher gibt es keine styleHints).
    // Die Einstellung gilt GLOBAL für jedes Qt-Flickable; die Listen der App
    // selbst sind davon unberührt, soweit sie `SmoothWheelArea` benutzen (die
    // rechnet aus `angleDelta` und ihrem eigenen stepFactor).
    app.styleHints()->setWheelScrollLines(6);

    // ── Schriftart mit CJK-Fallback ───────────────────────────────────────────
    // Hängt eine Familien-Fallbackkette an die Standardschrift, damit Zeichen
    // ohne Glyphe in der Standardfamilie (z.B. japanische/chinesische Zeichen in
    // Dateinamen) aus einer installierten Fallback-Familie gerendert werden statt
    // als Tofu (□). Qt wählt pro Glyphe die erste Familie, die sie besitzt.
    // Plattformübergreifend: Linux (Noto/Source Han), Windows (YaHei/Yu Gothic/
    // Meiryo), macOS (Hiragino). Voraussetzung: mind. eine CJK-Familie installiert
    // (Arch: `noto-fonts-cjk`).
    //
    // ARABISCH: ohne explizite arabische Familie greift fontconfig oft „Noto
    // Nastaliq Urdu" (schräger, kalligrafischer Urdu-Stil) — daher hängen wir hier
    // eine saubere Naskh-Druckschrift VOR den generischen Fallback. Amiri sitzt bei
    // voll vokalisiertem Text (viele Harakat) am besten, Noto Naskh/Sans Arabic als
    // breite Absicherung. Voraussetzung (Arch): `noto-fonts` (Naskh + Sans Arabic)
    // und optional `ttf-amiri`.
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
    // PDF-Editor/-Text/-Audio sind jetzt DEZENTRAL: je geöffneter PDF-Kachel
    // (PdfSurface) erzeugt QML eine EIGENE Instanz (qmlRegisterType unten) →
    // getrennter Editmodus/Boxen/Auswahl/Text-Selektion/Audio pro Datei. Der
    // PdfEdit-Singleton bleibt allein für die globale Einstellung panelOnTop
    // (Einstellungen ▸ Editor) erhalten.
    PdfEditController    pdfEdit(settings);
    // Globaler PDF-Seiten-Extraktor: EINE Instanz genügt (eigener 1-Thread-Pool
    // seriell); wird sowohl aus jeder PdfSurface als auch aus der FilterBar/Shell
    // (globale Ordner-Extraktion) angesprochen.
    PdfExtractController pdfExtract;
    //  DOCX-Editor: der Editor-Zustand ist DEZENTRAL (DocxEditController je
    //  Kachel via qmlRegisterType unten); das Docx-Singleton trägt allein die
    //  globale Speicherverhalten-Einstellung (direkt / Kopie exportieren).
    DocxController       docx(settings);
    TransliterationController translit;   // Live-Transliteration (Latein → Arabisch/Kana)
    WebEngineController  webEngine;       // lazy WebEngine-Init (nur bei HTML-Bedarf)

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

    //  „Alle Dateien anzeigen": der Schalter lebt in den Einstellungen, die
    //  Regel im Modell — beim Umschalten liest es den Ordner neu.
    mediaModel.setShowAllFiles(settings.showAllFiles());
    QObject::connect(&appController, &AppController::showAllFilesChanged,
                     &mediaModel, [&settings, &mediaModel]() {
        mediaModel.setShowAllFiles(settings.showAllFiles());
    });

    // Thumbnail-Zielgröße folgt der Kachelgröße (Stufen 512/1024/2048/4096):
    // Kacheln > 512 px würden sonst aus der 512er-Cache-Datei hochskaliert
    // (unscharf). Initial setzen; bei Stufenwechsel Thumbnails neu anfordern.
    thumbLoader.setTargetDim(qMax(settings.tileWidth(), settings.tileHeight()));
    QObject::connect(&appController, &AppController::tileSizeChanged,
                     &mediaModel, [&settings, &thumbLoader, &mediaModel]() {
        if (thumbLoader.setTargetDim(qMax(settings.tileWidth(),
                                          settings.tileHeight())))
            mediaModel.refreshThumbnails();
    });

    // ── Anwendungs-Palette folgt dem App-Farbschema ──────────────────────────
    //  Die Standard-Controls (alles, was NICHT vom eigenen Stil abgedeckt ist:
    //  Menüs, Dialoge, Beschriftungen, BusyIndicator …) zeichnen aus der
    //  QPalette. Ohne diesen Schritt stammt sie vom Plattform-Theme, die App
    //  hinge also am Farbschema des Desktops (und sähe auf einem hellen Desktop
    //  aus wie helle Controls in einer dunklen App). Die Palette wird daher aus
    //  dem eingestellten Thema abgeleitet und bei jedem Themenwechsel neu
    //  gesetzt (Live-Wirkung wie bei den QML-Farben).
    auto applyThemePalette = [&settings]() {
        const ThemeColors t = settings.currentTheme();
        const QColor bg     = t.background;
        const QColor card   = t.card;
        const QColor text   = t.textPrimary;
        const QColor muted  = t.textMuted;
        const QColor border = t.border;
        const QColor accent = t.accent;
        //  Heller Text auf dunklem Grund (oder umgekehrt) → passende Kontrastfarbe
        //  für gefüllte Akzentflächen.
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
        //  Deaktivierte Zustände: gedämpfter Text, sonst wie oben.
        p.setColor(QPalette::Disabled, QPalette::WindowText, muted);
        p.setColor(QPalette::Disabled, QPalette::Text,       muted);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
        QGuiApplication::setPalette(p);
    };
    applyThemePalette();
    QObject::connect(&appController, &AppController::themeChanged, &app, applyThemePalette);

    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "App",       &appController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Settings",  &settings);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Tags",      &tagController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Viewer",    &viewerController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfThumbs", &pdfThumbs);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfEdit",   &pdfEdit);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfExtract", &pdfExtract);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Docx",      &docx);

    // Dezentrale, pro PdfSurface (PDF-Kachel) instanziierbare Editor-Controller —
    // eigener Zustand je geöffneter Datei (kein QML_ELEMENT-Makro, manuelle
    // Registrierung wie die übrigen Typen).
    qmlRegisterType<PdfTextController> ("MediaGallery", 1, 0, "PdfTextController");
    qmlRegisterType<PdfAudioController>("MediaGallery", 1, 0, "PdfAudioController");
    qmlRegisterType<PdfEditController> ("MediaGallery", 1, 0, "PdfEditController");
    // Dezentraler Bild-Editor: je ImageSurface-Kachel eine eigene Instanz.
    qmlRegisterType<ImageEditController>("MediaGallery", 1, 0, "ImageEditController");
    qmlRegisterType<DocxEditController>("MediaGallery", 1, 0, "DocxEditController");
    qmlRegisterType<DocxTextArea>      ("MediaGallery", 1, 0, "DocxTextArea");
    //  Seiten-Miniatur des DOCX-Editors (Delegate der Miniaturen-Leiste; malt
    //  über DocxTextArea::paintPageInto, hält also selbst kein Bild).
    qmlRegisterType<DocxPageThumb>     ("MediaGallery", 1, 0, "DocxPageThumb");
    //  Verzeichnis-Inhalt für den eigenen Datei-/Ordnerwähler
    //  (`qml/common/FileChooser.qml`) — ein Modell je Wähler, damit zwei
    //  geöffnete Wähler nicht im selben Verzeichnis stehen.
    qmlRegisterType<FileBrowseModel>   ("MediaGallery", 1, 0, "FileBrowseModel");
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Translit",  &translit);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "WebEngine", &webEngine);

    // ── QML-Wurzel ───────────────────────────────────────────────────────────
    QQmlApplicationEngine engine;
    //  Suchpfad für den eigenen Control-Stil (liegt in den Ressourcen):
    //  der Stil "style" wird als ":/qml/style/" aufgelöst.
    engine.addImportPath(QStringLiteral(":/qml"));
    engine.rootContext()->setContextProperty("galleryModel", &galleryModel);
    engine.rootContext()->setContextProperty("mediaModel",   &mediaModel);

    // RAM-Vorschauen der PDF-Seitenleiste: "image://pdfthumb/<docId>/<page>".
    // Eigentum des Providers geht an die Engine ueber.
    engine.addImageProvider(QStringLiteral("pdfthumb"), pdfThumbs.createImageProvider());

    engine.load(QUrl(QStringLiteral("qrc:/qml/ApplicationShell.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    // ── Laufzeit-Guard: Scene-Graph-Fehler (GPU-Wechsel/Device-Lost) ──────────
    // Ohne verbundenen Slot beendet Qt die App bei einem NICHT behebbaren
    // Scene-Graph-Fehler hart (qFatal). Mit Slot behalten wir die Kontrolle:
    // der Fehler wird geloggt und über RhiProber::noteRuntimeFailure() fürs
    // NÄCHSTE Programm-Start ein sichereres Backend persistiert
    // (Degradationskette vulkan/d3d11/metal → opengl → software). Die laufende
    // Sitzung kann das verlorene Gerät zwar nicht mehr nutzen (das Fenster
    // rendert ggf. nicht weiter), aber der Neustart läuft garantiert wieder —
    // ergänzt den Start-Crash-Guard, der bewusst nur die ersten ~4 s abdeckt.

    if (auto* rootWin = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        QObject::connect(rootWin, &QQuickWindow::sceneGraphError, &app,
                         [](QQuickWindow::SceneGraphError, const QString& message) {
                             qWarning().noquote()
                                 << "MediaGallery: Scene-Graph-Fehler:" << message
                                 << "— der nächste Start nutzt ein sichereres Render-Backend.";
                             RhiProber::noteRuntimeFailure();
                         });
    }

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
