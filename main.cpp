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
#include <QQuickWindow>              // Laufzeit-Guard: sceneGraphError (GPU-Wechsel/Device-Lost)
#include <QUrl>
#include <QTimer>                    // verzögertes Löschen des RHI-Crash-Guards nach Start
#include <QDebug>                    // Warnung bei Scene-Graph-Laufzeitfehlern
#include <QKeyEvent>                 // Tasten-Mitschnitt (MG_KEYLOG, s. KeyLogger)
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

// ─────────────────────────────────────────────────────────────────────────────
//  Start- und Schliess-Messung (`MG_STARTLOG=1`)
//
//  Worauf der Nutzer beim Start wartet, laesst sich nur IM Start messen - ein
//  Pruefstand kann ihn nicht nachstellen (RHI-Wahl, QML-Uebersetzung, Fenster,
//  erster Rahmen). Deshalb ein paar Marken direkt hier, nach dem Muster von
//  `MG_DEEPLOG` im Suchlauf: standardmaessig aus, kostet dann eine
//  Umgebungsabfrage beim Start und sonst nichts.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
QElapsedTimer  g_startClock;
bool           g_startLog = false;

//  Belegter Speicher zur Marke - „schnell" und „schlank" gehoeren zusammen
//  (§0-Prioritaet 2 und 4), und ohne die Zahl sieht man nicht, was ein
//  vorgezogener Aufbau kostet.
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

// ─────────────────────────────────────────────────────────────────────────────
//  Tasten-Mitschnitt (`MG_KEYLOG=1`)
//
//  Wenn ein Kürzel „nicht wirkt", gibt es genau zwei Möglichkeiten, und sie
//  verlangen völlig verschiedene Antworten: entweder kommt die Taste gar nicht
//  bei uns an (Eingabemethode, Fenstermanager, globales Kürzel des Desktops) -
//  dann ist im Code nichts zu suchen -, oder sie kommt an und jemand im Baum
//  nimmt sie vorher. Beides ist nur AM LAUFENDEN FENSTER zu unterscheiden, ein
//  Prüfstand kann es nicht: der hat weder Eingabemethode noch Fenstermanager.
//
//  Deshalb ein Filter, der genau die drei Ereignisse mitschreibt, an denen sich
//  das entscheidet - `ShortcutOverride` (wer beansprucht die Taste?),
//  `KeyPress` (ist sie überhaupt angekommen?) und `Shortcut` (welche Aktion hat
//  ausgelöst?). Standardmäßig AUS; dann kostet er eine Umgebungsabfrage beim
//  Start und sonst nichts (der Filter wird gar nicht erst eingehängt).
// ─────────────────────────────────────────────────────────────────────────────
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
            //  WER hat den Tastaturfokus? Ohne diese Zeile beantwortet der
            //  Mitschnitt nur „ist die Taste angekommen?" - nicht „warum tut
            //  sie nichts?". Genau das war bei der Entf-Taste die offene
            //  Frage: sie haengt an `GalleryView.Keys.onPressed`, greift also
            //  NUR, solange die Ansicht den Fokus hat. Ein Textfeld im Baum
            //  (Suche, Umbenennen) schluckt sie lautlos.
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

    // ── RHI-Backend setzen ────────────────────────────────────────────────────
    // Muss VOR allen Qt-Klassen aufgerufen werden.
    // Liest das gewählte Backend aus QSettings, prüft den Crash-Guard
    // (-> automatischer Software-Fallback nach Crash) und ruft
    // QQuickWindow::setGraphicsApi() auf.
    RhiProber::applyStoredBackend();

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    //  Eigener, vollständig gethemter Control-Stil (qml/style).
    //  Der Stilname MUSS dem Verzeichnisnamen entsprechen, in dem die Stil-
    //  Dateien liegen - deshalb "style" (Suchpfad ":/qml", s. addImportPath).
    //  Nicht abgedeckte Controls fallen auf Fusion zurück (setFallbackStyle).
    QQuickStyle::setStyle(QStringLiteral("style"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));

    //  Datei-/Ordnerdialoge NICHT nativ öffnen.
    //  Der native Dialog (auf dem Linux-Desktop der GTK-/Portal-Dialog) folgt
    //  ausschließlich den Systemfarben - im dunklen App-Theme sitzt dann ein
    //  helles Fremdfenster mitten in der Anwendung, und seine Liste scrollt in
    //  festen Rastungen. Er liest KEINE Qt-Palette, ist also von hier aus
    //  überhaupt nicht einfärbbar. Qts eigene QML-Fassung dagegen erbt die
    //  Palette ihres Elternfensters - und die setzt ApplicationShell.qml
    //  bereits aus dem App-Theme. EINE Zeile deckt damit alle sieben
    //  Dialog-Stellen ab (Ordner öffnen, Lesezeichen, Design-Import/-Export,
    //  PDF-Bild/-Anhang, DOCX-Bild).
    //  Preis, bewusst in Kauf genommen: die Desktop-Integration des nativen
    //  Dialogs (GTK-Lesezeichen, zuletzt benutzte Orte) entfällt.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    // ── Qt WebEngine: NICHT mehr beim Start initialisieren (RAM-Baseline) ────
    // Viele Nutzer öffnen nie eine HTML-Datei - die Chromium-Grundkosten von
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
    //  (s. ApplicationShell.qml) - die Alt-Taste gehört damit ohnehin allein den
    //  App-Kürzeln, und die App vergibt selbst keine „&"-Mnemoniks.
    //  Das generische Theme hatte zwei unerwünschte Nebenwirkungen:
    //    • Datei-/Farbdialoge fielen auf Qts eingebaute QML-Variante zurück
    //      (fremde Optik, ungenutzte Leerflächen) statt der desktop-nativen;
    //    • die Anwendungspalette kam aus Qts hellem Standardschema - helle
    //      Standard-Controls in der dunklen App.
    //  Beides entfällt mit dem Desktop-Theme. Von dessen Palette ist die App
    //  unabhängig: sie setzt ihre QPalette weiter unten aus dem eigenen
    //  Farbschema (applyThemePalette).

    QGuiApplication app(argc, argv);
    startMark("QGuiApplication steht");

    //  Tasten-Mitschnitt nur auf Verlangen (s. KeyLogger oben): beantwortet die
    //  Frage „kommt das Kürzel überhaupt an?", die kein Prüfstand beantworten
    //  kann. Ohne `MG_KEYLOG` wird gar nichts eingehängt.
    if (qEnvironmentVariableIsSet("MG_KEYLOG")) {
        app.installEventFilter(new KeyLogger(&app));
        qInfo("[KEY] Mitschnitt an - jeder Tastendruck wird protokolliert.");
    }
    app.setApplicationName("MediaGallery");
    app.setOrganizationName("MediaGallery");
    app.setApplicationVersion("1.0.0");

    // ── Rad-Schrittweite von Qts EIGENEN Dialogen ─────────────────────────────
    // Der Datei-/Ordnerdialog ist Qts eigener (AA_DontUseNativeDialogs, s.o.);
    // seine Dateiliste ist eine schlichte QQuickListView und kroch mit Qts
    // Vorgabe von 3 Zeilen je Rastung durch lange Ordner. Gemessen an genau
    // dieser Liste (Höhe 329, contentHeight 5400): 3 Zeilen = 72 px, 6 = 144 px,
    // 12 = 288 px je Rastung - der Wert wirkt also linear.
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
    // Nastaliq Urdu" (schräger, kalligrafischer Urdu-Stil) - daher hängen wir hier
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

    // Settings - einzige konkrete Instanz, als ISettings& weitergereicht
    AppSettings& settings = AppSettings::instance();

    //  ── Der Zustand EINER Galerie-Hälfte ────────────────────────────────
    //  Sidecar, Tags, Ordnerdienst und Modelle gehören zusammen und liegen im
    //  `PaneController` (s. src/app/PaneController.h). Der Miniatur-Lader bleibt
    //  bewusst EINER für die ganze App: ein Pool, eine Zielgröße.
    ThumbnailLoader thumbLoader;

    // QML-Bridges
    AppController       appController(settings);
    //  Die Hälften gehören der Fassade (QML kann sie nicht selbst erzeugen -
    //  sie brauchen Einstellungen und den gemeinsamen Lader). Beim Start gibt
    //  es eine; die zweite kommt über „Ansicht ▸ Teilen".
    appController.setThumbnailLoader(&thumbLoader);
    auto* pane0 = qobject_cast<PaneController*>(appController.addPane());
    ViewerController    viewerController;
    PdfThumbnailProvider pdfThumbs;
    // PDF-Editor/-Text/-Audio sind jetzt DEZENTRAL: je geöffneter PDF-Kachel
    // (PdfSurface) erzeugt QML eine EIGENE Instanz (qmlRegisterType unten) ->
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
    //  Texteditor: das Singleton traegt Farbprofil und Verhalten (appweit), der
    //  eigentliche Faerber lebt DEZENTRAL je Kachel (CodeHighlighter als Typ
    //  unten) - in der geteilten Ansicht sind bis zu vier Dateien mit je eigener
    //  Sprache offen. `setActiveController` reicht die Palette an die Faerber
    //  weiter, ohne dass die den Controller kennen muessen.
    mg::editor::EditorController editor(settings);
    mg::editor::setActiveController(&editor);
    //  Audio-Player mit Equalizer: EINE Wiedergabe für die ganze App (wie
    //  `monoPlay`), deshalb ein Singleton - welche Hälfte ihn zeigt, entscheidet
    //  die Oberfläche (s. src/audio/AudioController.h).
    AudioController      audio(settings);
    TransliterationController translit;   // Live-Transliteration (Latein -> Arabisch/Kana)
    WebEngineController  webEngine;       // lazy WebEngine-Init (nur bei HTML-Bedarf)

    // ── Galerie-Backend ──────────────────────────────────────────────────────
    //  Ordnerdienst, Sidecar, Tags und Modelle liegen in `pane0`; ihre
    //  Verdrahtung untereinander macht der `PaneController` selbst. Hier bleibt
    //  nur, was APPWEIT gilt und alle Hälften betrifft.
    MediaModel&      mediaModel   = pane0->mediaModel();
    MediaProxyModel& galleryModel = pane0->galleryModel();

    //  „Alle Dateien anzeigen": der Schalter lebt in den Einstellungen, die
    //  Regel im Modell - beim Umschalten liest es den Ordner neu.
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

    // ── Aussehen der TEXT-Kacheln ───────────────────────────────────────────
    //  Die Vorschaukarte einer Textdatei zeigt deren erste Zeilen in genau den
    //  Farben des Editors - oder nur den Dateityp, wenn die Vorschau
    //  abgeschaltet ist. Beides steckt in der erzeugten Cache-Datei, ein
    //  Umzeichnen genuegt also nicht: aendert sich die Einstellung ODER eine
    //  Farbe des Editors, muessen die sichtbaren Kacheln neu erzeugt werden.
    //  Der Stil wird HIER gesetzt und in jeden Task kopiert - der Faerber im
    //  Pool fasst den Controller nie an.
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
        //  Heller Text auf dunklem Grund (oder umgekehrt) -> passende Kontrastfarbe
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
    //  `Tags` als APPWEITE Fassade: sie folgt der fokussierten Hälfte. Innerhalb
    //  einer Hälfte überdeckt deren eigener `TagController` diesen Namen
    //  (Kontext-Eigenschaft, s. src/app/PaneHost.h) - der Singleton bedient
    //  damit genau das, was AUSSERHALB der Hälften liegt: die Einstellungen.
    static TagController tagsFacade(pane0->tagManager());
    appController.setTagsFacade(&tagsFacade);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Tags",      &tagsFacade);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Viewer",    &viewerController);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfThumbs", &pdfThumbs);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfEdit",   &pdfEdit);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "PdfExtract", &pdfExtract);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Docx",      &docx);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Editor",    &editor);

    //  Alle QML-TYPEN an einer Stelle - dieselbe Funktion ruft der Pruefstand
    //  `tests/bench/bench_shell.cpp`. Frueher fuehrte jede Datei ihre eigene
    //  Liste, und dem Pruefstand fehlte regelmaessig ein neuer Typ
    //  (s. `src/app/QmlTypes.h`). Die SINGLETONS bleiben hier: sie brauchen
    //  die Instanzen von oben.
    mg::registerQmlTypes();
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Audio",     &audio);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "Translit",  &translit);
    qmlRegisterSingletonInstance("MediaGallery", 1, 0, "WebEngine", &webEngine);

    // ── QML-Wurzel ───────────────────────────────────────────────────────────
    startMark("Controller/Modelle stehen");

    QQmlApplicationEngine engine;
    //  Suchpfad für den eigenen Control-Stil (liegt in den Ressourcen):
    //  der Stil "style" wird als ":/qml/style/" aufgelöst.
    engine.addImportPath(QStringLiteral(":/qml"));
    engine.rootContext()->setContextProperty("galleryModel", &galleryModel);
    engine.rootContext()->setContextProperty("mediaModel",   &mediaModel);

    // RAM-Vorschauen der PDF-Seitenleiste: "image://pdfthumb/<docId>/<page>".
    // Eigentum des Providers geht an die Engine ueber.
    engine.addImageProvider(QStringLiteral("pdfthumb"), pdfThumbs.createImageProvider());

    //  Das im Titel eingebettete Bild: "image://audiocover/<pfad>?rev=<n>".
    //  Liest im Bild-Faden von Qt Quick, nicht im GUI-Faden (s. AudioCoverProvider.h).
    engine.addImageProvider(QStringLiteral("audiocover"), new AudioCoverProvider);

    //  Aufteilen, wohin die Startzeit geht: Ein winziges Stück QML zwingt die
    //  Engine und die QtQuick-Erweiterungen dazu, sich einzurichten. Alles
    //  danach ist UNSERE Oberfläche. Ohne diese Trennung raet man, welche
    //  Haelfte teuer ist (nur mit `MG_STARTLOG`).
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

    //  Erster gezeichneter Rahmen und erste Kachel im Modell - das ist, was der
    //  Nutzer als „die App ist da" erlebt. Beide Verbindungen loesen sich nach
    //  dem ersten Mal selbst.
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

    // ── Laufzeit-Guard: Scene-Graph-Fehler (GPU-Wechsel/Device-Lost) ──────────
    // Ohne verbundenen Slot beendet Qt die App bei einem NICHT behebbaren
    // Scene-Graph-Fehler hart (qFatal). Mit Slot behalten wir die Kontrolle:
    // der Fehler wird geloggt und über RhiProber::noteRuntimeFailure() fürs
    // NÄCHSTE Programm-Start ein sichereres Backend persistiert
    // (Degradationskette vulkan/d3d11/metal -> opengl -> software). Die laufende
    // Sitzung kann das verlorene Gerät zwar nicht mehr nutzen (das Fenster
    // rendert ggf. nicht weiter), aber der Neustart läuft garantiert wieder -
    // ergänzt den Start-Crash-Guard, der bewusst nur die ersten ~4 s abdeckt.

    if (auto* rootWin = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        QObject::connect(rootWin, &QQuickWindow::sceneGraphError, &app,
                         [](QQuickWindow::SceneGraphError, const QString& message) {
                             qWarning().noquote()
                                 << "MediaGallery: Scene-Graph-Fehler:" << message
                                 << "- der nächste Start nutzt ein sichereres Render-Backend.";
                             RhiProber::noteRuntimeFailure();
                         });
    }

    // ── Crash-Guard früh entschärfen (WICHTIG seit WebEngine-Vorschau) ────────
    // Der RHI-Crash-Guard soll NUR Backends abfangen, die gar nicht starten/
    // rendern können - solche Defekte schlagen sofort beim Start zu (< wenige
    // Sekunden). Ein SPÄTERER Crash ist kein Backend-Defekt und darf den Guard
    // nicht auslösen. Qt WebEngine stürzt auf manchen Linux-Grafikstacks beim
    // Teardown (App schließen) ab; läge das Löschen des Guards - wie zuvor -
    // erst NACH app.exec(), bliebe der Guard nach so einem Crash gesetzt und der
    // nächste Start würde fälschlich auf Software-Rendering zurückfallen
    // (= massiver Lag der HTML-Vorschau, Endlosschleife). Deshalb den Guard
    //   (a) kurz nach erfolgreichem Start  und
    //   (b) beim regulären Beenden (aboutToQuit, VOR dem Teardown)
    // löschen - ein WebEngine-Schließen-Crash oder ein per kill beendeter
    // Hänger kann dann nie wieder Software erzwingen.
    //  `MG_AUTOQUIT=<ms>` beendet die App von selbst - damit laesst sich das
    //  SCHLIESSEN messen (bis dahin gab es keinen Weg, an einen sauberen
    //  Abbau heranzukommen, ohne von Hand zu klicken). Nur mit `MG_STARTLOG`
    //  sinnvoll; standardmaessig aus.
    if (const int autoQuit = qEnvironmentVariableIntValue("MG_AUTOQUIT"); autoQuit > 0)
        QTimer::singleShot(autoQuit, &app, [] {
            startMark("Beenden angefordert");
            QGuiApplication::quit();
        });

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
