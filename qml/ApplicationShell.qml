import QtQuick
import QtQuick.Window
import QtQuick.Controls
import MediaGallery 1.0
import "common"
import "gallery"
import "pdf"
import "settings"
import "tags"
import "viewer"

// ─────────────────────────────────────────────────────────────────────────────
//  ApplicationShell.qml — Wurzel-Fenster der QML-UI.
//
//  Phase 3: Die Galerie-Seite trägt jetzt die FilterBar (oben) und ein optional
//  einblendbares TagCategoryPanel (rechte Seitenleiste). Die Vollbild-Seite des
//  StackView ist der vollständige FullscreenViewer (Bild/Video/PDF/Text/Audio).
// ─────────────────────────────────────────────────────────────────────────────
ApplicationWindow {
    id: shell
    visible: true

    width:  App.initialWindowWidth  > 0 ? App.initialWindowWidth  : 1200
    height: App.initialWindowHeight > 0 ? App.initialWindowHeight : 800
    x: App.initialWindowX
    y: App.initialWindowY

    //  Fenstertitel: „MediaGallery — <Ordner> / <Datei>". Der Dateiname steht
    //  hier, weil die Kopfleiste der Kachel ihn nicht mehr trägt (dort sitzt jetzt
    //  das Ansichts-Menü). Ohne offene Datei bleibt es beim Ordner.
    title: {
        const basis = App.currentFolder.length > 0
                      ? ("MediaGallery — " + folderName(App.currentFolder))
                      : "MediaGallery"
        return (stack.depth > 1 && shell.activeFilePath.length > 0)
               ? (basis + " / " + folderName(shell.activeFilePath))
               : basis
    }

    //  Pfad der Datei in der AKTIVEN Kachel; die Kacheln melden ihn über ein
    //  `Binding` mit `when: paneActive` (s. Repeater der Split-Seite).
    property string activeFilePath: ""

    color: App.themeBackground

    palette.window:     App.themeBackground
    palette.windowText: App.themeTextPrimary
    palette.base:       App.themeCard
    palette.text:       App.themeTextPrimary
    palette.button:     App.themeMenuBarBg
    palette.buttonText: App.themeTextPrimary
    palette.highlight:  App.themeAccent
    palette.highlightedText: App.themeBackground
    palette.mid:        App.themeBorder
    //  Schattierungs-Rollen: hier BEWUSST noch einmal, obwohl main.cpp sie
    //  bereits auf der QGuiApplication-Palette setzt. Datei-/Ordnerdialoge
    //  öffnen als EIGENES Fenster und erben die Palette ihres ELTERNfensters,
    //  nicht die der Anwendung (gemessen) — ohne diese Zeilen zeichnet Qts
    //  Dialog-Implementierung Rahmen, Trenner und Seitenleiste aus der
    //  Systempalette und wirkt als Fremdkörper im Theme.
    //  Werte identisch zu `applyThemePalette` in main.cpp halten.
    palette.light:      Qt.lighter(App.themeCard, 1.3)
    palette.midlight:   Qt.lighter(App.themeBorder, 1.2)
    palette.dark:       Qt.darker(App.themeBackground, 1.2)
    palette.shadow:     Qt.darker(App.themeBackground, 1.6)
    palette.alternateBase:   App.themeBackground
    palette.placeholderText: App.themeTextMuted

    property string statusText: ""

    // ── Immersives Vollbild (Taste F im Media Viewer) ─────────────────────────
    //  Blendet die App-Chrome aus: Fenster auf Vollbild (Titelleiste/Dekoration
    //  weg), Menüleiste weg, Kachel-Chrome der Viewer weg (dort: obere Leiste +
    //  untere Vor/Zurück-Navigation, s. FullscreenViewer.immersive). Erneutes F
    //  (oder Esc) stellt alles wieder her.
    //  Fenstergeometrie/-zustand VOR dem Vollbild merken: `visibility` liefert im
    //  Vollbild nur noch FullScreen und width/height die Bildschirmmaße — ohne
    //  diese Sicherung würde ein Beenden im Vollbild die gespeicherte
    //  Fensterposition/-größe mit den Bildschirmmaßen überschreiben.
    property bool immersiveFullscreen: false
    property int  _preImmersiveVisibility: Window.Windowed
    property rect _preImmersiveGeometry: Qt.rect(0, 0, 0, 0)
    //  Haben WIR den Fensterzustand umgeschaltet? Nur dann dürfen wir ihn beim
    //  Verlassen wieder anfassen.
    property bool _windowWasSwitched: false

    function setImmersive(on) {
        if (shell.immersiveFullscreen === on) return
        if (on) {
            shell._preImmersiveVisibility = shell.visibility
            shell._preImmersiveGeometry = Qt.rect(shell.x, shell.y, shell.width, shell.height)
            shell.immersiveFullscreen = true
            //  **War das Fenster schon im Vollbild, wird es NICHT angefasst** —
            //  weder beim Betreten noch beim Verlassen. Wer die App per
            //  Fenstermanager auf Vollbild gestellt hat, bekommt durch F nur die
            //  Chrome weg und behält seinen Fensterzustand. Ein blindes Setzen
            //  und späteres „Zurücksetzen" warf das Fenster hier auf seine
            //  normale Größe zurück.
            shell._windowWasSwitched = (shell.visibility !== Window.FullScreen)
            if (shell._windowWasSwitched)
                shell.visibility = Window.FullScreen
        } else {
            shell.immersiveFullscreen = false
            if (!shell._windowWasSwitched)
                return                     // Fenster gehörte uns nie
            shell._windowWasSwitched = false
            //  Deterministisch zurück: ERST in den normalen Zustand samt
            //  gemerkter Geometrie, DANN ggf. maximieren. Der direkte Sprung
            //  Vollbild → Maximiert ist eine reine Wertzuweisung, die manche
            //  Fenstermanager verschlucken; über den Zwischenschritt gibt es in
            //  jedem Fall einen echten Zustandswechsel, und die Geometrie sitzt
            //  danach auch als „normale" Fläche des Fensters richtig.
            shell.visibility = Window.Windowed
            const g = shell._preImmersiveGeometry
            if (g.width > 0) {
                shell.x = g.x; shell.y = g.y
                shell.width = g.width; shell.height = g.height
            }
            if (shell._preImmersiveVisibility === Window.Maximized)
                shell.visibility = Window.Maximized
        }
    }
    function toggleImmersive() { setImmersive(!shell.immersiveFullscreen) }

    // ── Globale PDF-Seiten-Extraktion (FilterBar ▸ „Extrahieren") ─────────────
    //  PdfExtract ist ein Singleton, das AUCH jede PdfSurface nutzt → diese
    //  Flags markieren, ob der laufende Scan/Auftrag von HIER stammt; nur dann
    //  reagiert die Shell auf die Ergebnis-Signale.
    property bool _scanPending: false
    property bool _extractPending: false
    //  Ordner, in dem die laufende Seiten-Extraktion arbeitet. Leer = der
    //  geoeffnete Ordner; die Kopfzeile eines aufgeklappten Bereichs setzt
    //  hier ihren eigenen.
    property string _extractFolder: ""
    function _extractTarget() {
        return shell._extractFolder.length > 0 ? shell._extractFolder
                                               : App.currentFolder
    }
    property string _extractName: ""

    Component.onCompleted: {
        if (App.startMaximized)
            shell.visibility = Window.Maximized
        App.restoreLastFolder()
    }

    onClosing: function(close) {
        // Im immersiven Vollbild den GEMERKTEN Fensterzustand sichern — sonst
        // startet die App beim nächsten Mal in Bildschirmgröße an Position 0,0.
        if (shell.immersiveFullscreen) {
            const g = shell._preImmersiveGeometry
            App.saveWindowState(g.width, g.height, g.x, g.y,
                                shell._preImmersiveVisibility === Window.Maximized)
            return
        }
        App.saveWindowState(shell.width, shell.height, shell.x, shell.y,
                            shell.visibility === Window.Maximized)
    }

    function folderName(path) {
        var n = path.replace(/[\/\\]+$/, "")
        var i = Math.max(n.lastIndexOf("/"), n.lastIndexOf("\\"))
        return i >= 0 ? n.substring(i + 1) : n
    }

    // ── „Ordner hinzufügen": Vorbefüllung mit dem aktuellen Ordner ────────────
    //  Pfad für den Duplikat-Vergleich normalisieren: NUR abschließende
    //  Separatoren entfernen („C:\Test" ≡ „C:\Test\"), Vergleich bleibt
    //  case-SENSITIV. Wurzelpfade („/", „C:\") bleiben unangetastet (das
    //  Muster verlangt ein Nicht-Separator-Zeichen vor den Separatoren).
    function _normalizedFolderPath(p) {
        return p.replace(/([^\/\\])[\/\\]+$/, "$1")
    }
    //  Liefert den aktuell geöffneten Ordner, sofern er noch NICHT in den
    //  gespeicherten Ordnern steht — sonst "" (Dialog öffnet dann leer).
    function _bookmarkPrefillPath() {
        var cur = App.currentFolder
        if (cur.length === 0) return ""
        var norm = _normalizedFolderPath(cur)
        var list = App.savedFolders
        for (var i = 0; i < list.length; i++)
            if (_normalizedFolderPath(list[i].path) === norm) return ""
        return cur
    }

    // ── Menüleiste ───────────────────────────────────────────────────────────
    //  ThemedMenu: bisher folgten die Menü-POPUPS (Datei/Ansicht/Einstellungen/
    //  Ordner + deren Untermenüs) NICHT der in Einstellungen ▸ Design gewählten
    //  Menüleisten-Farbe (App.themeMenuBarBg) — nur die Leiste selbst (via
    //  palette.button) war korrekt eingefärbt, die aufklappenden Popups nutzten
    //  weiterhin die Fusion-Standardfarbe. Analog zum bereits korrekt
    //  eingefärbten Filter-Popup (FilterBar.qml) bekommt jedes Menu hier
    //  denselben expliziten Hintergrund.
    //  `ThemedMenu` liegt jetzt als eigene Datei unter `qml/common/` — die
    //  Kontextmenüs (Kachel, PDF-Seite, Tag-Chips) brauchen dieselbe Fassung,
    //  und eine Inline-Komponente ist außerhalb dieser Datei nicht erreichbar.

    //  EIGENE Menüleiste statt der nativen `MenuBar`: Die Fusion-`MenuBar`
    //  belegt (wie ihr Widgets-Pendant) die ALT-Taste für Menü-Navigation/
    //  Mnemoniks — sprachabhängig und per API nicht abschaltbar; das kollidierte
    //  mit den App-Kürzeln (Alt+S/Alt+Q/Alt+←). Diese Leiste ist eine gethemte
    //  Button-Reihe, die die `Menu`-Popups per KLICK öffnet → die Alt-Taste
    //  gehört ausschließlich den App-Shortcuts, sprachunabhängig. Die
    //  Menü-Inhalte (inkl. Lesezeichen-Logik) sind unverändert.
    menuBar: Rectangle {
        id: menuStrip
        //  Nur auf der Galerie-Seite sichtbar: eine geöffnete Datei bekommt die
        //  ungeteilte Fensterfläche (dedizierte Vollbild-Ansicht) — die Kachel
        //  bringt ihre eigene Kopfleiste inkl. Zurück-Knopf mit. ApplicationWindow
        //  nimmt eine unsichtbare `menuBar` aus dem Layout, es bleibt also kein
        //  Leerstreifen stehen. Im immersiven Vollbild (F) ist sie ebenfalls weg.
        visible: stack.depth === 1 && !shell.immersiveFullscreen
        implicitHeight: 32
        color: App.themeMenuBarBg
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: App.themeBorder }

        component MenuBtn: Rectangle {
            property string label: ""
            property var menu: null
            width: mbLbl.implicitWidth + 22; height: 24; radius: 5
            anchors.verticalCenter: parent.verticalCenter
            color: (mbHover.hovered || (menu && menu.opened)) ? App.themeCard : "transparent"
            Text { id: mbLbl; anchors.centerIn: parent; text: parent.label
                   color: App.themeTextPrimary; font.pixelSize: 13 }
            HoverHandler { id: mbHover }
            TapHandler {
                onTapped: {
                    if (menu.opened) menu.close()
                    else menu.popup(parent, 0, parent.height + 3)
                }
            }
        }

        Row {
            id: menuBtnRow
            anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
            spacing: 2
            MenuBtn { label: App.menuFileText;      menu: fileMenu }
            MenuBtn { label: App.menuViewText;      menu: viewMenu }
            MenuBtn { label: App.menuSettingsText;  menu: settingsMenu }
            MenuBtn { label: App.menuBookmarksText; menu: bookmarksMenu }
        }

        // ── Menü-Popups (per Klick geöffnet — KEINE MenuBar/Alt-Navigation) ────
        ThemedMenu {
            id: fileMenu
            MenuItem { text: App.menuOpenFolderText; onTriggered: folderDialog.open() }
            MenuItem {
                text: App.menuRefreshText
                enabled: App.currentFolder.length > 0
                onTriggered: App.refreshCurrentFolder()
            }
            MenuSeparator {}
            MenuItem { text: App.menuQuitText; onTriggered: Qt.quit() }
        }

        ThemedMenu {
            id: viewMenu
            MenuItem {
                text: App.menuToggleOptionsText
                checkable: true
                checked: App.optionsVisible
                onTriggered: App.toggleOptions()
            }
            MenuSeparator {}
            MenuItem {
                text: App.uiText(App.language, "MenuTileSize")
                onTriggered: tileSizeDialog.openDialog()
            }
        }

        ThemedMenu {
            id: settingsMenu
            MenuItem {
                text: App.uiText(App.language, "MenuSettingsItem")
                onTriggered: shell.openSettings()
            }
            MenuSeparator {}
            ThemedMenu {
                title: App.menuLanguageText
                MenuItem { text: "Deutsch"; checkable: true; checked: App.language === "de"; onTriggered: App.setLanguage("de") }
                MenuItem { text: "English"; checkable: true; checked: App.language === "en"; onTriggered: App.setLanguage("en") }
            }
            ThemedMenu {
                title: App.menuVideoPlaybackText
                MenuItem { text: App.menuVideoNativeText;   checkable: true; checked: App.videoPlayback === "native";   onTriggered: App.setVideoPlayback("native") }
                MenuItem { text: App.menuVideoExternalText; checkable: true; checked: App.videoPlayback === "external"; onTriggered: App.setVideoPlayback("external") }
            }
        }

        ThemedMenu {
            id: bookmarksMenu

            MenuItem {
                text: App.bookmarkAddText
                // Öffnet denselben Hinzufügen-Dialog wie Einstellungen ▸ Lesezeichen
                // (Anzeigename + Pfad + Durchsuchen). Ist gerade ein Ordner geöffnet
                // und noch NICHT in den gespeicherten Ordnern, wird sein Pfad
                // vorbefüllt (case-sensitiver Vergleich, Trailing-Separatoren
                // normalisiert) — er bleibt vor dem Bestätigen frei änderbar.
                // Ohne offenen (oder mit bereits gespeichertem) Ordner öffnet der
                // Dialog wie bisher leer.
                onTriggered: bookmarkEditDialog.openAdd(shell._bookmarkPrefillPath())
            }
            MenuSeparator {
                id: bookmarksSeparator
                visible: App.savedFolders.length > 0
            }
            MenuItem {
                id: bookmarksEmpty
                text: App.menuBookmarksEmptyText
                enabled: false
                visible: App.savedFolders.length === 0
                //  Ein unsichtbarer `MenuItem` behält im Menü-ListView seine
                //  Höhe → zwischen „Ordner hinzufügen" und dem ersten Lesezeichen
                //  klaffte eine leere Zeile. Höhe explizit auf 0 klemmen.
                height: visible ? implicitHeight : 0
            }

            // Vorlage für dynamisch erzeugte Lesezeichen-Einträge.
            // Instantiator + insertItem() ist in Qt6 defekt (QTBUG-69922) →
            // Items werden manuell per rebuildBookmarks() erzeugt und verwaltet.
            Component {
                id: bookmarkItemComponent
                MenuItem {
                    property string bookmarkPath: ""
                    onTriggered: App.openBookmark(bookmarkPath)
                }
            }

            // Aktuell aktive dynamische Items (zum sauberen Entfernen beim Rebuild).
            property var dynamicBookmarkItems: []

            function rebuildBookmarks() {
                // Alte dynamische Items entfernen
                for (var i = 0; i < dynamicBookmarkItems.length; i++)
                    bookmarksMenu.removeItem(dynamicBookmarkItems[i])
                dynamicBookmarkItems = []

                // Neue Items für jeden gespeicherten Ordner anhängen
                var folders = App.savedFolders
                for (var j = 0; j < folders.length; j++) {
                    var item = bookmarkItemComponent.createObject(bookmarksMenu, {
                        text:         folders[j].name,
                        bookmarkPath: folders[j].path
                    })
                    bookmarksMenu.addItem(item)
                    dynamicBookmarkItems.push(item)
                }
            }

            Component.onCompleted: rebuildBookmarks()

            Connections {
                target: App
                function onSavedFoldersChanged() { bookmarksMenu.rebuildBookmarks() }
            }
        }
    }

    // ── Seiten-Stack (Galerie / Vollbild) ────────────────────────────────────
    StackView {
        id: stack
        anchors.fill: parent
        initialItem: galleryComponent

        //  Rückkehr zur Galerie beendet das immersive Vollbild — egal auf
        //  welchem Weg (Zurück, Esc, letzte Kachel geschlossen, „+"-Sprung).
        onDepthChanged: if (depth === 1) shell.setImmersive(false)

        // Übergangsstil aus den Einstellungen. Bewusst nur GPU-günstige Transforms
        // (x/scale/opacity) → kein Relayout/Neu-Rendern während der Animation. Die
        // schwere PDF-Last lädt FullscreenViewer erst NACH dem Übergang (StackView.
        // Active) → das Öffnen ruckelt nicht mehr.
        readonly property bool _txSlide: App.pageTransition === "slide"
        readonly property int  _txDur:   240

        pushEnter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: stack._txSlide ? 1 : 0;          to: 1; duration: stack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "x";       from: stack._txSlide ? stack.width : 0; to: 0; duration: stack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale";   from: stack._txSlide ? 1 : 0.97;        to: 1; duration: stack._txDur; easing.type: Easing.OutCubic }
            }
        }
        pushExit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x";       from: 0; to: stack._txSlide ? -stack.width * 0.22 : 0; duration: stack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "opacity"; from: 1; to: stack._txSlide ? 1 : 0;                    duration: stack._txDur; easing.type: Easing.InCubic }
            }
        }
        popEnter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x";       from: stack._txSlide ? -stack.width * 0.22 : 0; to: 0; duration: stack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "opacity"; from: stack._txSlide ? 1 : 0;                    to: 1; duration: stack._txDur; easing.type: Easing.OutCubic }
            }
        }
        popExit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x";       from: 0; to: stack._txSlide ? stack.width : 0; duration: stack._txDur; easing.type: Easing.InCubic }
                NumberAnimation { property: "opacity"; from: 1; to: stack._txSlide ? 1 : 0;            duration: stack._txDur; easing.type: Easing.InCubic }
                NumberAnimation { property: "scale";   from: 1; to: stack._txSlide ? 1 : 0.97;         duration: stack._txDur; easing.type: Easing.InCubic }
            }
        }
    }

    Component {
        id: galleryComponent
        Item {
            id: galleryPage

            //  Tastatur-Fokus an die Galerie, sobald die Seite aktiv ist — sonst
            //  scrollen ihre Pfeiltasten nie. Ein blosses `focus: true` an der
            //  GalleryView reicht NICHT: gemessen bleibt sie damit auf
            //  `activeFocus = false`, der Fokus liegt beim Wurzelelement. Gleiche
            //  Lehre wie bei der geteilten Ansicht (`_focusActivePane`).
            StackView.onActivated: galleryView.forceActiveFocus()
            Component.onCompleted: if (StackView.status === StackView.Active)
                                       galleryView.forceActiveFocus()

            // ── Tastenkürzel (nur auf der Galerie-Seite, nicht im Vollbild) ──
            //  Alt+S = Optionen umschalten (einheitlich mit dem Media Viewer),
            //  R = Ordner/Vorschau neu laden, B = Vorschau-Sperre (blockieren ⇄
            //  neu laden). Einzeltasten werden von fokussierten Textfeldern via
            //  Shortcut-Override unterdrückt.
            Shortcut {
                sequence: "Alt+S"; enabled: stack.depth === 1
                onActivated: App.toggleOptions()
            }
            Shortcut {
                sequence: "R"; enabled: stack.depth === 1
                onActivated: App.refreshCurrentFolder()
            }
            //  F5 = neu laden (Standard-Alias zu „R"); Ctrl+O = Ordner öffnen.
            //  Beide nur auf der Galerie-Seite (stack.depth===1).
            Shortcut {
                sequence: "F5"; enabled: stack.depth === 1
                onActivated: App.refreshCurrentFolder()
            }
            Shortcut {
                sequence: "Ctrl+O"; enabled: stack.depth === 1
                onActivated: folderDialog.open()
            }
            //  ── Alt+← : zurueck aus einem Unterordner ───────────────────────
            //  Nur der Rueckweg, kein Vorwaerts: hinein fuehrt der Doppelklick
            //  auf die Ordnerkachel. Auf den Stapel kommt ausschliesslich ein
            //  Abstieg (App.openSubfolder) — ein Lesezeichen oder ein Drop
            //  verlaesst den Baum und leert ihn.
            Shortcut {
                sequence: "Alt+Left"
                enabled: stack.depth === 1 && App.canNavigateBack
                onActivated: App.navigateBack()
            }
            //  Strg+F springt ins Suchfeld der Filterleiste (dieselbe Taste wie
            //  die Suche in der PDF-Ansicht — dort gehört sie der Kachel).
            Shortcut {
                sequence: "Ctrl+F"; enabled: stack.depth === 1
                onActivated: filterBar.focusSearch()
            }
            //  ── Rückgängig / Wiederholen für DATEI-Vorgänge ─────────────────
            //  Gilt NUR auf der Galerie-Seite: in den Editoren gehört Strg+Z dem
            //  Dokument. Und nicht, während jemand in einem Feld tippt — dort
            //  gehört die Taste dem Text (dieselbe Prüfung wie die Pfeiltasten
            //  der Galerie, deshalb ihre Funktion und keine zweite Kopie).
            Shortcut {
                sequence: StandardKey.Undo; enabled: stack.depth === 1
                onActivated: {
                    if (galleryView._editableTextFocused()) return
                    const name = mediaModel.undoFileOpName()
                    shell.statusText = mediaModel.undoFileOp()
                        ? App.uiText(App.language, "FileUndoRestored") + name
                        : App.uiText(App.language, "FileUndoNothing")
                    statusClearTimer.restart()
                }
            }
            Shortcut {
                sequence: StandardKey.Redo; enabled: stack.depth === 1
                onActivated: {
                    if (galleryView._editableTextFocused()) return
                    const name = mediaModel.redoFileOpName()
                    shell.statusText = mediaModel.redoFileOp()
                        ? App.uiText(App.language, "FileRedoDeleted") + name
                        : App.uiText(App.language, "FileRedoNothing")
                    statusClearTimer.restart()
                }
            }
            Shortcut {
                sequence: "B"; enabled: stack.depth === 1
                onActivated: {
                    if (galleryView.covered) {
                        galleryView.covered = false
                        App.refreshCurrentFolder()
                    } else {
                        galleryView.covered = true
                    }
                }
            }
            // Strg + '+'/'-' (inkl. '='): Kachelgröße ändern. Nur eindeutige
            // Sequenzen — StandardKey.ZoomIn würde zusätzlich "Ctrl++" liefern und
            // den Shortcut mehrdeutig machen (feuert dann gar nicht).
            Shortcut {
                sequences: ["Ctrl++", "Ctrl+="]
                enabled: stack.depth === 1
                onActivated: App.zoomIn(16)
            }
            Shortcut {
                sequence: "Ctrl+-"
                enabled: stack.depth === 1
                onActivated: App.zoomOut(16)
            }

            FilterBar {
                id: filterBar
                anchors { left: parent.left; right: parent.right; top: parent.top }
                onEnterAddToTagMode: function(tag) { galleryView.enterAddToTagMode(tag) }
                // Panel-Steuerung: Tag- und Kategorie-Abschnitt des Seitenpanels
                // INDIVIDUELL schaltbar; der Zustand lebt im TagCategoryPanel und
                // wird hier für die Aktiv-Anzeige der Toggle-Zeilen gespiegelt.
                tagPanelVisible: catPanel.showTagsSection
                categoryPanelVisible: catPanel.showCategoriesSection
                onTagPanelToggled:      catPanel.showTagsSection      = !catPanel.showTagsSection
                onCategoryPanelToggled: catPanel.showCategoriesSection = !catPanel.showCategoriesSection
                // „Extrahieren": Ordner asynchron nach PDFs durchsuchen; das
                // Ergebnis öffnet unten (onFolderPdfsReady) den Auswahldialog.
                // Das Flag grenzt uns gegen Scans anderer Aufrufer ab (Singleton).
                onNewFolderRequested: function(folder) {
                    galleryView.promptNewFolder(folder)
                }
                onExtractPagesRequested: function(folder) {
                    shell._extractFolder = folder
                    shell._scanPending = true
                    PdfExtract.scanFolder(shell._extractTarget())
                }
            }

            GalleryView {
                id: galleryView
                anchors {
                    left: parent.left
                    right: catPanel.visible ? catPanel.left : parent.right
                    top: filterBar.bottom
                    bottom: addBanner.visible ? addBanner.top
                            : (modeBanner.visible ? modeBanner.top : parent.bottom)
                }
                onActivated: function(filePath) {
                    if (shell.pendingAddFile) shell.addFileFromGallery(filePath)
                    else pushFullscreen(filePath)
                }
                //  Doppelklick auf eine Ordnerkachel: hinein. Waehrend der Modus
                //  „Datei zur geteilten Ansicht waehlen" laeuft, wird NICHT der
                //  Ordner gewechselt — der Modus wartet auf eine DATEI.
                onFolderOpenRequested: function(folderPath) {
                    if (shell.pendingAddFile) return
                    App.openSubfolder(folderPath)
                }
                //  Aktionen aus der Kopfzeile eines aufgeklappten Bereichs —
                //  sie zielen auf DESSEN Ordner.
                onCreateFileRequested: function(folderPath) {
                    filterBar.openCreateFor(folderPath)
                }
                onExtractPagesRequested: function(folderPath) {
                    shell._extractFolder = folderPath
                    shell._scanPending = true
                    PdfExtract.scanFolder(shell._extractTarget())
                }
                onFolderDropRequested: function(sourcePath, folderPath) {
                    shell._dropIntoFolder(sourcePath, folderPath)
                }
                //  Dateien von AUSSEN landen in dem Ordner, ueber dem
                //  losgelassen wurde (geprueft in AppController).
                onExternalDropRequested: function(urls, folderPath) {
                    App.handleDroppedUrls(urls, folderPath)
                }
                onStatusRequested: function(text) { shell.statusText = text }
            }

            TagCategoryPanel {
                id: catPanel
                // Beide Abschnitte starten ausgeblendet; das Panel erscheint,
                // sobald mindestens einer aktiviert wird (Filter ▸ Tags & Kategorien).
                showTagsSection: false
                showCategoriesSection: false
                visible: showTagsSection || showCategoriesSection
                width: 300
                anchors { right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
                onEnterAddToTagMode: function(tag) { galleryView.enterAddToTagMode(tag) }
                onEnterGroupMode: function(tag) { galleryView.enterGroupMode(tag) }
            }

            // ── Hinzufügen-Modus-Banner (Datei zur geteilten Ansicht wählen) ─
            Rectangle {
                id: addBanner
                visible: shell.pendingAddFile
                anchors { left: parent.left; right: parent.right
                          bottom: modeBanner.visible ? modeBanner.top : parent.bottom }
                height: 34
                color: App.themeAccent
                Row {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.uiText(App.language, "SplitPickPrompt")
                        color: App.themeBackground; font.pixelSize: 12; font.bold: true
                    }
                    Item { width: parent.width - 260; height: 1 }
                    Button {
                        anchors.verticalCenter: parent.verticalCenter
                        height: 24; text: App.uiText(App.language, "SplitCancel"); font.pixelSize: 11
                        onClicked: shell.cancelAddFile()
                    }
                }
            }

            // ── Modus-Banner (Gruppen-/Add-to-Tag-Modus verlassen) ───────────
            Rectangle {
                id: modeBanner
                visible: galleryView.tagMode !== 0
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 34
                color: App.themeAccent
                Row {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: (galleryView.tagMode === 1 ? App.uiText(App.language, "ModeGroup") : App.uiText(App.language, "ModeAddToTag"))
                              + ": " + galleryView.modeTag
                        color: App.themeBackground; font.pixelSize: 12; font.bold: true
                    }
                    Item { width: parent.width - 260; height: 1 }
                    Button {
                        anchors.verticalCenter: parent.verticalCenter
                        height: 24; text: "Fertig"; font.pixelSize: 11
                        onClicked: galleryView.exitModes()
                    }
                }
            }
        }
    }

    // ── Geteilte Ansicht (Splitscreen): bis zu 4 Dateien NEBENEINANDER ────────
    //  Anders als klassische Tabs zeigt die Vollbild-Seite mehrere Dateien
    //  GLEICHZEITIG — analog zum 2-/3-/4-Spieler-Splitscreen: zwei Dateien
    //  nebeneinander, drei als 2 oben + 1 unten (volle Breite), vier als 2×2.
    //  So lässt sich bequem parallel arbeiten/vergleichen.
    //
    //  • Jede Kachel ist ein eigenständiger FullscreenViewer (eigene Kopfleiste,
    //    eigene ←/→-Navigation über das Galerie-Modell).
    //  • „+" (Kopfleiste jeder Kachel, direkt neben dem Datum-Button) öffnet
    //    einen Datei-Dialog und fügt die gewählte Datei als weitere Kachel hinzu.
    //  • Zurück/Esc einer Kachel SCHLIESST genau diese Datei (RAM sofort frei);
    //    war es die letzte Kachel, geht es zurück zur Galerie.
    //  • Bei mehr als einer Kachel entfällt die untere Hover-Navigation der
    //    Kacheln (Pfeile + Zähler) — sie kehrt zurück, sobald nur noch eine
    //    Datei offen ist (splitActive-Flag je Viewer).
    readonly property int maxOpenFiles: 4
    ListModel { id: openFilesModel }   // Rolle: path (aktueller Pfad der Kachel)

    // Geometrie einer Kachel je Index/Anzahl (kleine Lücke g als Trennfuge).
    // Einstellbare Split-Verhältnisse (per Divider-Drag; auf sinnvolle Grenzen
    // geklemmt). splitV = vertikale Trennung (Spalten), splitH = horizontale.
    property real splitV: 0.5
    property real splitH: 0.5
    readonly property real _splitMin: 0.15
    readonly property real _splitMax: 0.85

    // ── Docking-Layoutzustand (Session-lokal, KEIN Persistieren) ──────────────
    //  Das bestehende paneRect-System wurde zum Slot-System erweitert:
    //  layout2:   Anordnung bei 2 Dateien — "cols" (nebeneinander, Standard)
    //             oder "rows" (übereinander).
    //  layout3:   Seite der GROSSEN Kachel bei 3 Dateien — "bottom" (Standard:
    //             zwei kleine oben, eine große unten), "top", "left", "right".
    //  slotOrder: Zuordnung Layout-Slot → Modell-Index (openFilesModel).
    //             Slots sind layoutfest definiert (s. _slotRect). Drag&Drop,
    //             Schließen und Hinzufügen ordnen NUR diese Liste bzw. die
    //             Layout-Variante um — das Modell selbst bleibt unangetastet,
    //             kein Viewer wird dadurch zerstört oder neu geladen (reine
    //             Geometrie-Bindings, performant identisch zum alten System).
    property string layout2: "cols"
    property string layout3: "bottom"
    property var    slotOrder: []

    // Reine Slot-Geometrie eines LAYOUTS (unabhängig vom Modell). Slots:
    //  count 2 "cols": 0 = links, 1 = rechts · "rows": 0 = oben, 1 = unten
    //  count 3: 0/1 = die zwei KLEINEN Kacheln (Lesereihenfolge), 2 = die GROSSE
    //  count 4: 0 = oben-links, 1 = oben-rechts, 2 = unten-links, 3 = unten-rechts
    function _slotRect(slot, count, l2, l3, W, H) {
        var g = 2
        var rV = Math.max(_splitMin, Math.min(shell.splitV, _splitMax))
        var rH = Math.max(_splitMin, Math.min(shell.splitH, _splitMax))
        if (count <= 1)
            return { x: 0, y: 0, w: W, h: H }
        if (count === 2) {
            if (l2 === "rows") {                             // zwei Zeilen übereinander
                var th2 = (H - g) * rH
                if (slot === 0) return { x: 0, y: 0,       w: W, h: th2 }
                return                 { x: 0, y: th2 + g, w: W, h: H - g - th2 }
            }
            var cwL = (W - g) * rV                           // zwei Spalten nebeneinander
            if (slot === 0) return { x: 0,       y: 0, w: cwL,           h: H }
            return                 { x: cwL + g, y: 0, w: W - g - cwL,   h: H }
        }
        if (count === 3) {
            var hs = (H - g) * rH                            // horizontale Trennlinie
            var vs = (W - g) * rV                            // vertikale Trennlinie
            if (l3 === "top") {                              // große Kachel oben
                if (slot === 2) return { x: 0,      y: 0,      w: W,           h: hs }
                if (slot === 0) return { x: 0,      y: hs + g, w: vs,          h: H - g - hs }
                return                 { x: vs + g, y: hs + g, w: W - g - vs,  h: H - g - hs }
            }
            if (l3 === "left") {                             // große Kachel links
                if (slot === 2) return { x: 0,      y: 0,      w: vs,          h: H }
                if (slot === 0) return { x: vs + g, y: 0,      w: W - g - vs,  h: hs }
                return                 { x: vs + g, y: hs + g, w: W - g - vs,  h: H - g - hs }
            }
            if (l3 === "right") {                            // große Kachel rechts
                if (slot === 2) return { x: vs + g, y: 0,      w: W - g - vs,  h: H }
                if (slot === 0) return { x: 0,      y: 0,      w: vs,          h: hs }
                return                 { x: 0,      y: hs + g, w: vs,          h: H - g - hs }
            }
            // "bottom" (Standard): zwei kleine oben, eine große unten
            if (slot === 0) return { x: 0,      y: 0,      w: vs,          h: hs }
            if (slot === 1) return { x: vs + g, y: 0,      w: W - g - vs,  h: hs }
            return                 { x: 0,      y: hs + g, w: W,           h: H - g - hs }
        }
        var topH = (H - g) * rH                              // 2×2
        var leftW = (W - g) * rV
        var col = slot % 2
        var rowi = Math.floor(slot / 2)
        return { x: col === 0 ? 0 : leftW + g,
                 y: rowi === 0 ? 0 : topH + g,
                 w: col === 0 ? leftW : W - g - leftW,
                 h: rowi === 0 ? topH : H - g - topH }
    }

    // Slot eines Modell-Index (defensive Identität, falls slotOrder gerade
    // nicht synchron ist — transient WÄHREND einer Modelländerung im selben
    // JS-Block; gerendert wird erst nach dessen Abschluss, also nie sichtbar).
    function _slotOfIndex(index, count) {
        if (slotOrder.length === count) {
            var s = slotOrder.indexOf(index)
            if (s >= 0) return s
        }
        return index
    }

    function paneRect(index, count, W, H) {
        return _slotRect(_slotOfIndex(index, count), count, layout2, layout3, W, H)
    }

    function _identity(n) { var a = []; for (var i = 0; i < n; i++) a.push(i); return a }

    // ── Slot-Übergänge bei Anzahl-Wechsel („sinngemäß erhalten") ──────────────
    //  Nach einem append (neuer Modell-Index = count-1) die Slot-Zuordnung so
    //  fortschreiben, dass die bestehenden Kacheln ihre relative Anordnung
    //  behalten und die neue Datei die frei werdende Fläche bekommt.
    function _paneAdded() {
        var n = openFilesModel.count
        var S = (slotOrder.length === n - 1) ? slotOrder.slice() : _identity(n - 1)
        var ni = n - 1
        if (n <= 1) { slotOrder = [0]; return }
        if (n === 2) { slotOrder = [S[0], ni]; return }
        if (n === 3) {
            // "cols" → beide bisherigen bleiben nebeneinander (obere Hälfte),
            //          die Neue wird große Kachel unten (Standard-3er-Layout);
            // "rows" → beide bisherigen bleiben übereinander (linke Hälfte),
            //          die Neue wird große Kachel rechts.
            layout3 = (layout2 === "rows") ? "right" : "bottom"
            slotOrder = [S[0], S[1], ni]
            return
        }
        // n === 4: Übergang ins 2×2 je bisherigem 3er-Layout — die große Kachel
        // behält den ersten Quadranten ihrer Hälfte, die Neue den frei werdenden.
        if (layout3 === "top")        slotOrder = [S[2], ni,   S[0], S[1]]
        else if (layout3 === "left")  slotOrder = [S[2], S[0], ni,   S[1]]
        else if (layout3 === "right") slotOrder = [S[0], S[2], S[1], ni]
        else /* bottom */             slotOrder = [S[0], S[1], S[2], ni]
    }

    //  Vor dem Entfernen von Modell-Index ri: neue Slot-Zuordnung + ggf. neues
    //  Layout bestimmen — die ANGRENZENDE Kachel übernimmt die frei werdende
    //  Fläche. Liefert die NEUE slotOrder (Indizes > ri bereits dekrementiert);
    //  der Aufrufer weist sie NACH dem Modell-remove zu.
    function _slotsAfterRemove(ri) {
        var n = openFilesModel.count
        var S = (slotOrder.length === n) ? slotOrder.slice() : _identity(n)
        var rs = S.indexOf(ri)
        var R = []
        if (n === 2) {
            R = [S[rs === 0 ? 1 : 0]]
        } else if (n === 3) {
            if (rs === 2) {
                // Große Kachel weg → die zwei kleinen behalten ihre Achse:
                // oben/unten-Layout hatte sie nebeneinander → Spalten;
                // links/rechts-Layout hatte sie übereinander → Zeilen.
                layout2 = (layout3 === "left" || layout3 === "right") ? "rows" : "cols"
                R = [S[0], S[1]]
            } else {
                // Kleine Kachel weg → verbleibende kleine + große teilen sich
                // die Fläche entlang der bisherigen Halbierungsachse; die
                // Reihenfolge spiegelt die bisherigen Positionen.
                var small = S[rs === 0 ? 1 : 0]
                var big   = S[2]
                if (layout3 === "bottom")    { layout2 = "rows"; R = [small, big] }
                else if (layout3 === "top")  { layout2 = "rows"; R = [big, small] }
                else if (layout3 === "left") { layout2 = "cols"; R = [big, small] }
                else /* right */             { layout2 = "cols"; R = [small, big] }
            }
        } else if (n === 4) {
            // 2×2 → 3: der ZEILEN-Nachbar des entfernten Quadranten wird zur
            // großen Kachel seiner Zeile (übernimmt die frei werdende Fläche).
            if (rs === 0)      { layout3 = "top";    R = [S[2], S[3], S[1]] }
            else if (rs === 1) { layout3 = "top";    R = [S[2], S[3], S[0]] }
            else if (rs === 2) { layout3 = "bottom"; R = [S[0], S[1], S[3]] }
            else               { layout3 = "bottom"; R = [S[0], S[1], S[2]] }
        }
        for (var i = 0; i < R.length; i++)
            if (R[i] > ri) R[i]--
        return R
    }

    function indexOfOpenFile(p) {
        for (var i = 0; i < openFilesModel.count; i++)
            if (openFilesModel.get(i).path === p) return i
        return -1
    }

    // ── Docking-Drag (Kopfleisten-Drag der Kacheln) ───────────────────────────
    //  Zustand + Zonenmodell (VS-Code-artig). Getroffen wird ein sichtbarer
    //  INDIKATOR (Rand-Zonen bei 2/3 Dateien, Ecken bei 3); außerhalb der
    //  Indikatoren gilt die Kachel unter dem Cursor als TAUSCH-Ziel. Bei
    //  4 Dateien gibt es AUSSCHLIESSLICH den Positionstausch (kein Layout-
    //  Wechsel) — daher dort keine Indikatoren.
    property bool dragActive: false
    property int  dragIndex: -1
    property real dragX: 0
    property real dragY: 0
    property var  dragZone: ({ kind: "none" })   // {kind:"edge"|"corner"|"swap"|"none", side|corner|target}

    function beginPaneDrag(i) {
        if (openFilesModel.count < 2 || i < 0 || i >= openFilesModel.count) return
        dragIndex = i
        dragZone = { kind: "none" }
        dragActive = true
    }
    function updatePaneDrag(x, y) {
        if (!dragActive) return
        dragX = x; dragY = y
        dragZone = _zoneAt(x, y)
    }
    function endPaneDrag(x, y) {
        if (!dragActive) return
        updatePaneDrag(x, y)
        var z = dragZone
        if (z.kind === "swap")        _applySwap(dragIndex, z.target)
        else if (z.kind === "edge")   _applyEdgeDrop(dragIndex, z.side)
        else if (z.kind === "corner") _applyCornerDrop(dragIndex, z.corner)
        cancelPaneDrag()
    }
    function cancelPaneDrag() {
        dragActive = false
        dragIndex = -1
        dragZone = { kind: "none" }
    }

    //  Sichtbare Drop-Indikatoren je Anzahl (Position/Größe in Split-Seiten-
    //  Koordinaten). Rand-Mitten bei 2 UND 3 Dateien, Ecken zusätzlich bei 3.
    function _zoneIndicators(count, W, H) {
        var s = 46, m = 14
        var list = []
        if (count === 2 || count === 3) {
            list.push({ kind: "edge", side: "left",   x: m,           y: (H - s) / 2, w: s, h: s })
            list.push({ kind: "edge", side: "right",  x: W - m - s,   y: (H - s) / 2, w: s, h: s })
            list.push({ kind: "edge", side: "top",    x: (W - s) / 2, y: m,           w: s, h: s })
            list.push({ kind: "edge", side: "bottom", x: (W - s) / 2, y: H - m - s,   w: s, h: s })
        }
        if (count === 3) {
            list.push({ kind: "corner", corner: "tl", x: m,           y: m,           w: s, h: s })
            list.push({ kind: "corner", corner: "tr", x: W - m - s,   y: m,           w: s, h: s })
            list.push({ kind: "corner", corner: "bl", x: m,           y: H - m - s,   w: s, h: s })
            list.push({ kind: "corner", corner: "br", x: W - m - s,   y: H - m - s,   w: s, h: s })
        }
        return list
    }

    function _zoneAt(x, y) {
        var pg = _splitPageItem
        if (!pg) return { kind: "none" }
        var count = openFilesModel.count
        var W = pg.width, H = pg.height
        var inds = _zoneIndicators(count, W, H)
        for (var i = 0; i < inds.length; i++) {
            var z = inds[i]
            if (x >= z.x && x <= z.x + z.w && y >= z.y && y <= z.y + z.h) return z
        }
        for (var j = 0; j < count; j++) {                    // Tausch: Kachel unter dem Cursor
            if (j === dragIndex) continue
            var r = paneRect(j, count, W, H)
            if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h)
                return { kind: "swap", target: j }
        }
        return { kind: "none" }
    }

    //  Vorschau der Zielfläche der GEZOGENEN Datei für die getroffene Zone —
    //  zeigt das Layout-Ergebnis an, BEVOR gedroppt wird.
    function _zonePreviewRect(z, W, H) {
        var count = openFilesModel.count
        if (z.kind === "swap")
            return paneRect(z.target, count, W, H)
        if (z.kind === "edge") {
            if (count === 2) {
                var l2 = (z.side === "top" || z.side === "bottom") ? "rows" : "cols"
                return _slotRect((z.side === "left" || z.side === "top") ? 0 : 1,
                                 2, l2, layout3, W, H)
            }
            return _slotRect(2, 3, layout2, z.side, W, H)    // große Kachel auf dieser Seite
        }
        if (z.kind === "corner") {
            var l3 = (z.corner === "tl" || z.corner === "tr") ? "bottom" : "top"
            return _slotRect((z.corner === "tl" || z.corner === "bl") ? 0 : 1,
                             3, layout2, l3, W, H)
        }
        return { x: 0, y: 0, w: 0, h: 0 }
    }

    //  Positionstausch zweier Kacheln — NUR slotOrder wird umgestellt (kein
    //  Modell-Umbau: die Viewer bleiben samt Zustand am Leben, nur die
    //  Geometrie-Bindings wechseln).
    function _applySwap(a, b) {
        if (a === b || a < 0 || b < 0) return
        var n = openFilesModel.count
        var S = (slotOrder.length === n) ? slotOrder.slice() : _identity(n)
        var sa = S.indexOf(a), sb = S.indexOf(b)
        if (sa < 0 || sb < 0) return
        S[sa] = b; S[sb] = a
        slotOrder = S
    }

    //  Zwei verbleibende Kacheln auf zwei Ziel-Slots verteilen: die Zuordnung
    //  mit dem geringeren Gesamtabstand der Kachel-MITTEN (aktuell → Ziel)
    //  gewinnt — jede Kachel bleibt so möglichst nah an ihrer bisherigen
    //  Position („sinngemäß"). Muss VOR dem Umstellen von layout*/slotOrder
    //  laufen (liest die aktuellen Positionen über paneRect).
    function _assignNearest(idxA, idxB, slotP, slotQ, l3) {
        var pg = _splitPageItem
        var W = pg ? pg.width : 1, H = pg ? pg.height : 1
        var n = openFilesModel.count
        function cx(r) { return r.x + r.w / 2 }
        function cy(r) { return r.y + r.h / 2 }
        function d2(r, t) { var dx = cx(r) - cx(t), dy = cy(r) - cy(t); return dx * dx + dy * dy }
        var ra = paneRect(idxA, n, W, H), rb = paneRect(idxB, n, W, H)
        var tp = _slotRect(slotP, 3, layout2, l3, W, H)
        var tq = _slotRect(slotQ, 3, layout2, l3, W, H)
        return (d2(ra, tp) + d2(rb, tq) <= d2(ra, tq) + d2(rb, tp))
               ? { p: idxA, q: idxB } : { p: idxB, q: idxA }
    }

    //  Rand-Zone: bei 2 Dateien Layout + Reihenfolge setzen (gezogene Datei
    //  bekommt die Zonen-Seite); bei 3 Dateien wird die gezogene Datei zur
    //  GROSSEN Kachel dieser Seite, die übrigen zwei teilen sich die andere
    //  Hälfte (Slot-Verteilung positionsnah).
    function _applyEdgeDrop(di, side) {
        var count = openFilesModel.count
        if (count === 2) {
            var other = (di === 0) ? 1 : 0
            layout2 = (side === "top" || side === "bottom") ? "rows" : "cols"
            slotOrder = (side === "left" || side === "top") ? [di, other] : [other, di]
            return
        }
        if (count !== 3) return
        var rest = []
        for (var j = 0; j < count; j++) if (j !== di) rest.push(j)
        var asg = _assignNearest(rest[0], rest[1], 0, 1, side)
        layout3 = side
        slotOrder = [asg.p, asg.q, di]
    }

    //  Ecken-Zone (nur 3 Dateien): die gezogene Datei wird KLEINE Kachel in
    //  diesem Quadranten; die frei werdende große Fläche geht vollständig an
    //  die angrenzende Kachel (positionsnahe Verteilung der übrigen zwei auf
    //  den zweiten kleinen Slot und den großen Slot der anderen Hälfte).
    function _applyCornerDrop(di, corner) {
        if (openFilesModel.count !== 3) return
        var l3 = (corner === "tl" || corner === "tr") ? "bottom" : "top"
        var dslot = (corner === "tl" || corner === "bl") ? 0 : 1
        var oslot = (dslot === 0) ? 1 : 0
        var rest = []
        for (var j = 0; j < 3; j++) if (j !== di) rest.push(j)
        var asg = _assignNearest(rest[0], rest[1], oslot, 2, l3)
        layout3 = l3
        var S = [0, 0, 0]
        S[dslot] = di; S[oslot] = asg.p; S[2] = asg.q
        slotOrder = S
    }

    // Kachel schließen (Zurück/Esc einer Datei). Letzte Kachel → zurück zur
    // Galerie. Die Slot-Zuordnung wird VOR dem remove berechnet („sinngemäß
    // erhalten": die angrenzende Kachel übernimmt die frei werdende Fläche)
    // und NACH dem remove zugewiesen — dazwischen greift die Identitäts-
    // Absicherung in _slotOfIndex (nie gerendert, gleicher JS-Block).
    function closePane(i) {
        if (i < 0 || i >= openFilesModel.count) return
        var R = _slotsAfterRemove(i)
        openFilesModel.remove(i)              // gibt den zugehörigen Viewer sofort frei
        slotOrder = R
        if (openFilesModel.count === 0)
            popFullscreen()
    }

    // „+": weitere Datei zur geteilten Ansicht hinzufügen. Statt eines
    // Datei-Dialogs kehrt die Ansicht zur GALERIE (Hauptfenster) zurück — die
    // offenen Kacheln bleiben im Modell erhalten. Ein Klick in der Galerie wählt
    // die nächste Datei; danach wird die geteilte Ansicht wiederhergestellt.
    //
    // WICHTIG (Issue-Fix): Die Split-Seite wird als PERSISTENTES Item genau
    // EINMAL erzeugt und dieses Item gepusht — nicht die Component. StackView
    // zerstört beim Pop nur selbst erzeugte Items; ein gepushtes Fremd-Item
    // überlebt den Pop (Qt blendet es aus und gibt die Ownership zurück).
    // Dadurch bleiben ALLE Viewer beim „+"-Rücksprung in die Galerie am Leben
    // — PDFs behalten Seite/Scrollposition, Bilder ihren Zoom, Texte ihre
    // Scrollstelle. Vorher zerstörte der Pop die per Component erzeugte Seite
    // samt Viewern; der erneute Push baute alles frisch → Seite 1.
    // popFullscreen() leert weiterhin das Modell → alle Viewer werden sofort
    // freigegeben (RAM-Priorität unverändert); nur der leere Seiten-Rahmen
    // bleibt für die Wiederverwendung bestehen.
    property Item _splitPageItem: null
    function _splitPage() {
        if (!_splitPageItem)
            _splitPageItem = fullscreenComponent.createObject(shell)
        return _splitPageItem
    }
    property bool pendingAddFile: false
    function requestAddFile() {
        if (openFilesModel.count >= shell.maxOpenFiles) {
            shell.statusText = App.uiText(App.language, "SplitMaxReached")
            statusClearTimer.restart()
            return
        }
        shell.pendingAddFile = true
        if (stack.depth > 1)
            stack.pop()                 // NICHT leeren — Kacheln bleiben im Modell UND am Leben
    }
    // Datei aus der Galerie zur geteilten Ansicht hinzufügen (Klick im Hinzufügen-Modus).
    function addFileFromGallery(p) {
        shell.pendingAddFile = false
        if (p !== undefined && p.length > 0
                && indexOfOpenFile(p) < 0
                && openFilesModel.count < shell.maxOpenFiles) {
            openFilesModel.append({ path: p })
            _paneAdded()                      // Slot-Zuordnung fortschreiben (Layout bleibt sinngemäß)
        }
        if (stack.depth < 2)
            stack.push(_splitPage())          // geteilte Ansicht wiederherstellen
    }
    // Hinzufügen abbrechen → zurück zur (unveränderten) geteilten Ansicht.
    function cancelAddFile() {
        shell.pendingAddFile = false
        if (stack.depth < 2 && openFilesModel.count > 0)
            stack.push(_splitPage())
    }

    Component {
        id: fullscreenComponent

        Item {
            id: splitPage

            // Lade-Gating: die schwere Medienlast erst NACH dem StackView-Übergang
            // anstoßen — die Viewer sitzen in Loadern UNTER der Seite (StackView.view
            // wäre dort null), daher gated die Seite die Loader-Aktivierung.
            // Nur bei Active setzen: das persistente Item existiert bereits VOR
            // dem ersten Push (s. _splitPage()) — dort ist StackView.view noch
            // null, das darf das Gating nicht auslösen. Einmal ready, bleibt
            // ready (beim „+"-Rücksprung laufen die Viewer bewusst weiter).
            property bool pageReady: false
            function _checkReady() {
                if (StackView.status === StackView.Active)
                    pageReady = true
            }
            //  Tastatur-Fokus auf die aktive Kachel legen. NÖTIG bei JEDEM
            //  Sichtbarwerden der Seite, nicht nur beim ersten: Beim zweiten
            //  Öffnen ist `pageReady` schon true (die Seite ist persistent), der
            //  Kachel-Loader lädt also SOFORT beim Befüllen des Modells — also
            //  VOR dem StackView-Push. Das `forceActiveFocus()` im `onLoaded`
            //  läuft dann auf einer Seite, die noch gar nicht die aktive ist,
            //  und der anschließende Push nimmt den Fokus wieder weg
            //  (`activeFocusItem` = QQuickRootItem). Ergebnis: die Kachel sah
            //  richtig aus, reagierte aber auf KEINE Taste mehr — F, ←/→ und Esc
            //  hängen alle am `Keys.onPressed` des FullscreenViewer.
            function _focusActivePane() {
                const l = paneRepeater.itemAt(splitPage.activePaneIndex)
                if (l && l.item) l.item.forceActiveFocus()
            }
            StackView.onStatusChanged: {
                _checkReady()
                if (StackView.status === StackView.Active) _focusActivePane()
            }
            onActivePaneIndexChanged: _focusActivePane()

            // Trennfugen-Hintergrund (scheint in der 2 px-Lücke zwischen Kacheln durch).
            Rectangle { anchors.fill: parent; color: "#0a0a0a" }

            readonly property int paneCount: openFilesModel.count

            //  Aktive Kachel (Split-View): genau EINE trägt die fensterweiten
            //  Tastenkürzel scharf (paneActive) → keine Mehrdeutigkeit bei 2–4
            //  offenen Dateien. Ein Klick in eine Kachel (paneActivated) oder das
            //  Laden einer neu hinzugefügten Kachel setzt den Index; beim
            //  Schließen wird er in den gültigen Bereich geklemmt.
            property int activePaneIndex: 0
            onPaneCountChanged: activePaneIndex =
                Math.max(0, Math.min(activePaneIndex, paneCount - 1))

            // ── Kacheln: ein FullscreenViewer je Datei, per Split-Layout platziert ─
            Repeater {
                id: paneRepeater
                model: openFilesModel
                delegate: Loader {
                    id: paneLoader
                    required property int index
                    required property string path

                    // Kachel-Geometrie nach Split-Layout (reagiert auf Anzahl/Größe).
                    readonly property var _r: shell.paneRect(index, splitPage.paneCount,
                                                             splitPage.width, splitPage.height)
                    x: _r.x; y: _r.y
                    width: _r.w; height: _r.h

                    // Alle sichtbaren Kacheln sind aktiv (Splitscreen zeigt sie parallel).
                    active: splitPage.pageReady

                    sourceComponent: FullscreenViewer {
                        id: paneViewer
                        startPath: paneLoader.path
                        // Mehr als eine Datei offen → untere Hover-Navigation der Kachel
                        // (Pfeile + Zähler) ausblenden; bei genau einer Datei wieder an.
                        splitActive: splitPage.paneCount > 1
                        canAddMore:  splitPage.paneCount < shell.maxOpenFiles
                        // Nur die aktive Kachel trägt ihre fensterweiten Kürzel scharf.
                        paneActive:  paneLoader.index === splitPage.activePaneIndex
                        onPaneActivated: splitPage.activePaneIndex = paneLoader.index
                        // Immersives Vollbild (F): Zustand kommt vom Shell, der
                        // Wunsch geht dorthin zurück (Fenster + Menüleiste).
                        immersive: shell.immersiveFullscreen
                        onImmersiveToggleRequested: shell.toggleImmersive()
                        onBackRequested:    shell.closePane(paneLoader.index)
                        onAddFileRequested: shell.requestAddFile()
                        // Docking: Kopfleisten-Drag dieser Kachel an die Shell
                        // durchreichen (Viewer-Koordinaten → Split-Seite; das
                        // Loader-Item sitzt bei (0,0) im Loader → identisch).
                        onPaneDragStarted:  shell.beginPaneDrag(paneLoader.index)
                        onPaneDragMoved: (x, y) => {
                            var p = paneLoader.mapToItem(splitPage, x, y)
                            shell.updatePaneDrag(p.x, p.y)
                        }
                        onPaneDragEnded: (x, y) => {
                            var p = paneLoader.mapToItem(splitPage, x, y)
                            shell.endPaneDrag(p.x, p.y)
                        }
                        onPaneDragCanceled: shell.cancelPaneDrag()

                        //  Fenstertitel: nur die AKTIVE Kachel schreibt ihren Pfad
                        //  in die Shell. `when` sorgt dafür, dass beim Wechsel der
                        //  aktiven Kachel genau eine Bindung gilt; `RestoreNone`
                        //  verhindert, dass eine inaktiv werdende Kachel den Wert
                        //  auf ihren alten Stand zurücksetzt.
                        Binding {
                            target: shell
                            property: "activeFilePath"
                            value: paneViewer.path
                            when: paneViewer.paneActive
                            restoreMode: Binding.RestoreNone
                        }
                    }
                    onLoaded: {
                        item.forceActiveFocus()
                        // Neu geladene (zuletzt hinzugefügte) Kachel wird aktiv.
                        splitPage.activePaneIndex = paneLoader.index
                    }

                    // Kachel-Pfad folgt der ←/→-Navigation IM Viewer (Modell nachziehen).
                    Connections {
                        target: paneLoader.item
                        ignoreUnknownSignals: true
                        function onPathChanged() {
                            if (paneLoader.item && paneLoader.index < openFilesModel.count)
                                openFilesModel.setProperty(paneLoader.index, "path",
                                                           paneLoader.item.path)
                        }
                    }
                }
            }

            // ── Ziehbare Trenner (Divider) ────────────────────────────────────
            //  Sichtbarkeit/Ausdehnung folgt der Layout-Variante:
            //  vertikal  — 2 Spalten ("cols"), alle 3er-Layouts (bei großer
            //              Kachel oben/unten nur in der Hälfte mit den kleinen
            //              Kacheln, bei links/rechts volle Höhe), 2×2;
            //  horizontal — 2 Zeilen ("rows"), alle 3er-Layouts (bei großer
            //              Kachel links/rechts nur in der kleinen Hälfte), 2×2.
            //  Ziehen setzt shell.splitV / shell.splitH (geklemmt) → paneRect folgt.
            readonly property real _gap: 2
            readonly property real _vX: (splitPage.width  - _gap) * Math.max(shell._splitMin, Math.min(shell.splitV, shell._splitMax))
            readonly property real _hY: (splitPage.height - _gap) * Math.max(shell._splitMin, Math.min(shell.splitH, shell._splitMax))

            Rectangle {                                       // vertikaler Trenner
                z: 50
                visible: splitPage.paneCount >= 4
                         || (splitPage.paneCount === 2 && shell.layout2 === "cols")
                         || splitPage.paneCount === 3
                x: splitPage._vX - 3
                y: (splitPage.paneCount === 3 && shell.layout3 === "top")
                   ? splitPage._hY : 0
                width: 8
                height: splitPage.paneCount === 3
                        ? (shell.layout3 === "bottom" ? splitPage._hY
                           : shell.layout3 === "top"  ? splitPage.height - splitPage._hY
                           : splitPage.height)
                        : splitPage.height
                color: vDivMA.pressed ? App.themeAccent : "transparent"
                MouseArea {
                    id: vDivMA
                    anchors.fill: parent
                    cursorShape: Qt.SplitHCursor
                    onPositionChanged: (m) => {
                        var p = mapToItem(splitPage, m.x, m.y)
                        shell.splitV = Math.max(shell._splitMin,
                            Math.min(p.x / Math.max(1, splitPage.width - splitPage._gap), shell._splitMax))
                    }
                }
            }
            Rectangle {                                       // horizontaler Trenner
                z: 50
                visible: splitPage.paneCount >= 4
                         || (splitPage.paneCount === 2 && shell.layout2 === "rows")
                         || splitPage.paneCount === 3
                x: (splitPage.paneCount === 3 && shell.layout3 === "left")
                   ? splitPage._vX : 0
                y: splitPage._hY - 3
                width: splitPage.paneCount === 3
                       ? (shell.layout3 === "right" ? splitPage._vX
                          : shell.layout3 === "left" ? splitPage.width - splitPage._vX
                          : splitPage.width)
                       : splitPage.width
                height: 8
                color: hDivMA.pressed ? App.themeAccent : "transparent"
                MouseArea {
                    id: hDivMA
                    anchors.fill: parent
                    cursorShape: Qt.SplitVCursor
                    onPositionChanged: (m) => {
                        var p = mapToItem(splitPage, m.x, m.y)
                        shell.splitH = Math.max(shell._splitMin,
                            Math.min(p.y / Math.max(1, splitPage.height - splitPage._gap), shell._splitMax))
                    }
                }
            }

            // ── Docking-Overlay: Drop-Zonen + Layout-Vorschau + Drag-Geist ────
            //  Nur während eines Kopfleisten-Drags sichtbar (shell.dragActive).
            //  Rand-Indikatoren (2/3 Dateien) + Ecken (3 Dateien); außerhalb der
            //  Indikatoren markiert die Kachel unter dem Cursor den Positions-
            //  tausch. Die Vorschaufläche zeigt das Layout-Ergebnis des Drops.
            Item {
                id: dockOverlay
                anchors.fill: parent
                visible: shell.dragActive
                z: 100

                // Dezentes Abdunkeln — hebt Zonen und Vorschau vom Inhalt ab.
                Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.25) }

                // Vorschau der Zielfläche (Layout-Ergebnis der getroffenen Zone).
                Rectangle {
                    readonly property var _pr: shell.dragActive
                        ? shell._zonePreviewRect(shell.dragZone, dockOverlay.width, dockOverlay.height)
                        : ({ x: 0, y: 0, w: 0, h: 0 })
                    visible: shell.dragActive && shell.dragZone.kind !== "none"
                    x: _pr.x; y: _pr.y
                    width: _pr.w; height: _pr.h
                    radius: 4
                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                    border.color: App.themeAccent
                    border.width: 2
                }

                // Rand-/Ecken-Indikatoren (bei 4 Dateien bewusst leer — nur Tausch).
                Repeater {
                    model: shell.dragActive
                           ? shell._zoneIndicators(splitPage.paneCount,
                                                   dockOverlay.width, dockOverlay.height)
                           : []
                    delegate: Rectangle {
                        id: zoneInd
                        required property var modelData
                        readonly property bool hot:
                            shell.dragZone.kind === modelData.kind
                            && (modelData.kind === "edge"
                                ? shell.dragZone.side === modelData.side
                                : shell.dragZone.corner === modelData.corner)
                        x: modelData.x; y: modelData.y
                        width: modelData.w; height: modelData.h
                        radius: 8
                        color: hot ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.85)
                                   : Qt.rgba(0.08, 0.10, 0.11, 0.85)
                        border.color: hot ? App.themeAccent : Qt.rgba(1, 1, 1, 0.35)
                        border.width: 1

                        // Rand-Glyphe: Richtungspfeil (universell verfügbare Zeichen).
                        Text {
                            anchors.centerIn: parent
                            visible: zoneInd.modelData.kind === "edge"
                            color: "#e8efed"; font.pixelSize: 18; font.bold: true
                            text: zoneInd.modelData.kind === "edge"
                                  ? ({ left: "\u2190", right: "\u2192",
                                       top: "\u2191", bottom: "\u2193" })[zoneInd.modelData.side]
                                  : ""
                        }
                        // Ecken-Glyphe: Quadrat-Umriss mit gefülltem Viertel —
                        // gezeichnet statt Sonderzeichen (fontunabhängig).
                        Item {
                            anchors.centerIn: parent
                            width: 20; height: 20
                            visible: zoneInd.modelData.kind === "corner"
                            Rectangle {
                                anchors.fill: parent; radius: 3; color: "transparent"
                                border.color: "#e8efed"; border.width: 1.4
                            }
                            Rectangle {
                                width: 8; height: 8; radius: 1; color: "#e8efed"
                                x: (zoneInd.modelData.corner === "tl"
                                    || zoneInd.modelData.corner === "bl") ? 3 : 9
                                y: (zoneInd.modelData.corner === "tl"
                                    || zoneInd.modelData.corner === "tr") ? 3 : 9
                            }
                        }
                    }
                }

                // Drag-Geist: Dateiname der gezogenen Kachel folgt dem Cursor.
                Rectangle {
                    visible: shell.dragActive && shell.dragIndex >= 0
                    x: shell.dragX + 14; y: shell.dragY + 10
                    width: ghostLabel.implicitWidth + 20; height: 28; radius: 6
                    color: Qt.rgba(0.08, 0.10, 0.11, 0.92)
                    border.color: App.themeAccent; border.width: 1
                    Text {
                        id: ghostLabel
                        anchors.centerIn: parent
                        color: "#e8efed"; font.pixelSize: 12; font.bold: true
                        text: (shell.dragIndex >= 0 && shell.dragIndex < openFilesModel.count)
                              ? shell.folderName(openFilesModel.get(shell.dragIndex).path) : ""
                    }
                }
            }
        }
    }

    // Navigations-API.
    //  Galerie-Doppelklick → frische Einzel-Kachel (die vorherige Ansicht ist beim
    //  Verlassen ohnehin geschlossen). Weitere Kacheln kommen über den „+"-Button.
    function pushFullscreen(filePath) {
        var p = filePath !== undefined ? filePath : ""
        if (p.length === 0) return
        openFilesModel.clear()
        openFilesModel.append({ path: p })
        shell.slotOrder = [0]
        if (stack.depth < 2)
            stack.push(_splitPage())
    }
    function popFullscreen() {
        openFilesModel.clear()          // Verlassen schließt alle Dateien (RAM frei)
        shell.slotOrder = []
        if (stack.depth > 1)
            stack.pop()                 // die leere Split-Seite überlebt (persistentes Item)
    }

    // ── Drag & Drop ───────────────────────────────────────────────────────────
    //  `z: -1` ist PFLICHT, nicht Kosmetik: Ein Zug wird immer nur an die
    //  OBERSTE annehmende DropArea unter dem Zeiger geliefert (gemessen —
    //  danach ist Schluss, tiefere Flächen sehen ihn nie). Diese Fläche hier
    //  füllt das ganze Fenster, steht ohne `keys` für JEDE Nutzlast offen und
    //  ist als spät deklariertes Kind der Wurzel automatisch obenauf — sie
    //  verschluckte damit die Ablegeflächen der Tag-/Kategorien-Seitenleiste
    //  vollständig. Nach unten gelegt greift sie nur noch dort, wo keine
    //  besondere Fläche zuständig ist.
    DropArea {
        z: -1
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls) {
                App.handleDroppedUrls(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    // ── Ablegeleiste für die ORDNER dieser Ansicht (nur während eines Zuges) ─
    //  Warum es sie gibt: während eines Zuges gehört der Zeiger dem Compositor,
    //  das Mausrad erreicht die Anwendung gar nicht (gemessen, s. `config.md` ▸
    //  „Ziehen"). Um einen Ordner zu erreichen, der aus dem Bild gescrollt ist,
    //  bliebe nur das Randscrollen. Diese Leiste bringt statt dessen die Ziele
    //  zum Zeiger: sie zeigt den geöffneten Ordner und jeden Ordner, der gerade
    //  in der Galerie steht — eingerückt nach Tiefe.
    //
    //  Dasselbe Muster wie die Lesezeichen-Leiste darunter; sie kostet keinen
    //  dauerhaften Platz.
    Rectangle {
        id: folderDropBar
        z: 91
        //  Nur auf der Galerie-Seite und nur, wenn es überhaupt Ordner gibt.
        visible: App.tileDragActive && stack.depth === 1
                 && folderDropRepeater.count > 0
        anchors { left: parent.left; right: parent.right; bottom: bookmarkDropBar.top }
        anchors.bottomMargin: bookmarkDropBar.visible ? 6 : 0
        height: 70
        color: App.themeMenuBarBg
        border.color: App.themeAccent; border.width: 1

        //  Die Ziele werden EINMAL beim Beginn des Zuges eingesammelt — während
        //  des Zuges ändert sich die Galerie nicht, und eine Bindung über alle
        //  Zeilen liefe bei jedem Ereignis neu.
        //  NICHT an `visible` hängen: `visible` fragt `folderDropRepeater.count`
        //  ab, der wiederum an `targets` hängt — die Leiste käme nie hoch.
        property var targets: []
        Connections {
            target: App
            function onTileDragActiveChanged() {
                if (App.tileDragActive)
                    folderDropBar.targets = shell._visibleFolders()
            }
        }

        Text {
            anchors { left: parent.left; leftMargin: 14; top: parent.top; topMargin: 6 }
            text: App.uiText(App.language, "FolderDropBar")
            color: App.themeTextMuted; font.pixelSize: 11
        }
        Flickable {
            anchors { left: parent.left; leftMargin: 14; right: parent.right
                      rightMargin: 14; bottom: parent.bottom; bottomMargin: 8 }
            height: 30
            contentWidth: folderRow.width
            clip: true
            Row {
                id: folderRow
                spacing: 8
                Repeater {
                    id: folderDropRepeater
                    model: folderDropBar.targets
                    delegate: Rectangle {
                        id: fTarget
                        required property var modelData
                        width: fLabel.implicitWidth + 34 + fTarget.modelData.depth * 10
                        height: 30
                        radius: 6
                        color: fDrop.containsDrag
                               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                         App.themeAccent.b, 0.30)
                               : App.themeCard
                        border.color: fDrop.containsDrag ? App.themeAccent : App.themeBorder
                        border.width: fDrop.containsDrag ? 2 : 1

                        DrawnIcon {
                            id: fIcon
                            anchors { left: parent.left
                                      leftMargin: 8 + fTarget.modelData.depth * 10
                                      verticalCenter: parent.verticalCenter }
                            name: "folder"
                            size: 14
                            color: App.themeAccent
                        }
                        Text {
                            id: fLabel
                            anchors { left: fIcon.right; leftMargin: 6
                                      verticalCenter: parent.verticalCenter }
                            text: fTarget.modelData.name
                            color: App.themeTextPrimary; font.pixelSize: 12
                        }
                        DropArea {
                            id: fDrop
                            anchors.fill: parent
                            keys: ["text/uri-list"]
                            onDropped: function(drop) {
                                if (!drop.hasUrls) { drop.accepted = false; return }
                                shell._dropUrlsOnFolder(drop.urls,
                                                        fTarget.modelData.path)
                                drop.acceptProposedAction()
                            }
                        }
                    }
                }
            }
        }
    }

    //  Der geöffnete Ordner und jeder Ordner, der gerade in der Galerie steht —
    //  in der sichtbaren Reihenfolge, mit ihrer Tiefe.
    function _visibleFolders() {
        var out = []
        if (App.currentFolder.length > 0)
            out.push({ path: App.currentFolder,
                       name: shell.folderName(App.currentFolder),
                       depth: 0 })
        for (var i = 0; i < galleryModel.count; ++i) {
            if (galleryModel.mediaTypeAt(i) !== 7) continue
            out.push({ path: galleryModel.filePathAt(i),
                       name: galleryModel.displayNameAt(i),
                       depth: galleryModel.depthAt(i) + 1 })
        }
        return out
    }

    //  Eine abgelegte Nutzlast in einen Ordner geben — app-intern verschieben
    //  bzw. kopieren, von aussen kopieren. Dieselbe Unterscheidung wie in der
    //  Galerie (`mediaModel.ownsFile`).
    function _dropUrlsOnFolder(urls, destFolder) {
        if (!urls || urls.length === 0 || destFolder.length === 0) return
        const src = App.localPath(urls[0])
        if (mediaModel.ownsFile(src)) {
            //  In den EIGENEN Ordner abzulegen ist keine Bewegung.
            const cut = Math.max(src.lastIndexOf("/"), src.lastIndexOf("\\"))
            if (cut > 0 && src.substring(0, cut) === destFolder) return
            shell._dropIntoFolder(src, destFolder)
        } else {
            App.handleDroppedUrls(urls, destFolder)
        }
    }

    // ── Ablegeleiste für Lesezeichen (nur WÄHREND eines Kachel-Zuges) ────────
    //  Der Zug einer Kachel ist ein PLATTFORM-Zug; landet er im eigenen Fenster,
    //  kommt er hier als gewöhnlicher Datei-Drop an. Die Leiste erscheint nur,
    //  solange gezogen wird (`App.tileDragActive`) — ein Ziel, das keinen
    //  dauerhaften Platz kostet. Sie liegt über allem, weil ein Zug immer nur an
    //  die OBERSTE annehmende Fläche geht.
    Rectangle {
        id: bookmarkDropBar
        z: 90
        visible: App.tileDragActive && App.savedFolders.length > 0
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.bottomMargin: 28
        height: 70
        color: App.themeMenuBarBg
        border.color: App.themeAccent; border.width: 1

        Text {
            id: dropBarHint
            anchors { left: parent.left; leftMargin: 14; top: parent.top; topMargin: 6 }
            text: App.uiText(App.language,
                             App.fileDropMove ? "DropBarMove" : "DropBarCopy")
            color: App.themeTextMuted; font.pixelSize: 11
        }
        Row {
            anchors { left: parent.left; leftMargin: 14; bottom: parent.bottom; bottomMargin: 8 }
            spacing: 8
            Repeater {
                model: App.savedFolders
                delegate: Rectangle {
                    id: bmTarget
                    required property var modelData
                    width: bmLabel.implicitWidth + 24; height: 30; radius: 6
                    color: bmDrop.containsDrag
                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
                           : App.themeCard
                    border.color: bmDrop.containsDrag ? App.themeAccent : App.themeBorder
                    border.width: bmDrop.containsDrag ? 2 : 1
                    Text {
                        id: bmLabel
                        anchors.centerIn: parent
                        text: bmTarget.modelData.name
                        color: App.themeTextPrimary; font.pixelSize: 12
                    }
                    DropArea {
                        id: bmDrop
                        anchors.fill: parent
                        keys: ["text/uri-list"]
                        onDropped: function(drop) {
                            if (!drop.hasUrls) { drop.accepted = false; return }
                            shell._dropIntoFolder(App.localPath(drop.urls[0]),
                                                  bmTarget.modelData.path)
                            drop.acceptProposedAction()
                        }
                    }
                }
            }
        }
    }

    //  Eine gezogene Datei in einen Ordner ablegen — auf einem LESEZEICHEN oder
    //  auf einer ORDNERKACHEL der Galerie; beide Wege sind derselbe Vorgang.
    //  Die Regeln stehen im Modell (`transferToFolder`); hier bleibt nur die
    //  Rückfrage bei einem belegten Namen und die Meldung.
    function _dropIntoFolder(path, destFolder) {
        if (path.length === 0 || destFolder.length === 0) return
        const move = App.fileDropMove
        const r = mediaModel.transferToFolder(path, destFolder, move, 0)
        if (r === 1) {
            collisionDialog.srcPath    = path
            collisionDialog.destFolder = destFolder
            collisionDialog.moveMode   = move
            collisionDialog.altName    = mediaModel.transferTargetName(path, destFolder)
            collisionDialog.open()
            return
        }
        shell._reportTransfer(r, move, path)
    }
    function _reportTransfer(result, move, path) {
        const name = String(path).split("/").pop()
        if (result === 0)
            shell.statusText = App.uiText(App.language, move ? "DropMoved" : "DropCopied") + name
        else
            shell.statusText = App.uiText(App.language, "DropFailed") + name
        statusClearTimer.restart()
    }

    // ── Rückfrage bei belegtem Namen (Ersetzen / Umbenennen / Abbrechen) ─────
    Dialog {
        id: collisionDialog
        property string srcPath: ""
        property string destFolder: ""
        property bool   moveMode: true
        property string altName: ""
        anchors.centerIn: parent
        modal: true
        padding: 18
        background: Rectangle {
            color: App.themeCard; radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            spacing: 10
            Text {
                text: App.uiText(App.language, "DropCollisionTitle")
                color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 320
                text: App.uiText(App.language, "DropCollisionText") + collisionDialog.altName
                color: App.themeTextMuted; font.pixelSize: 12; wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Repeater {
                    model: [
                        { key: "SettingsCancel",     mode: 0 },
                        { key: "DropCollisionRename", mode: 2 },
                        { key: "DropCollisionReplace", mode: 1 }
                    ]
                    delegate: Rectangle {
                        id: colBtn
                        required property var modelData
                        width: colLbl.implicitWidth + 24; height: 30; radius: 6
                        color: colHover.hovered
                               ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                         App.themeTextPrimary.b, 0.16)
                               : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                         App.themeTextPrimary.b, 0.07)
                        border.color: App.themeBorder; border.width: 1
                        Text {
                            id: colLbl
                            anchors.centerIn: parent
                            text: App.uiText(App.language, colBtn.modelData.key)
                            color: App.themeTextPrimary; font.pixelSize: 12
                        }
                        HoverHandler { id: colHover }
                        TapHandler {
                            onTapped: {
                                collisionDialog.close()
                                if (colBtn.modelData.mode === 0) return
                                const r = mediaModel.transferToFolder(
                                              collisionDialog.srcPath,
                                              collisionDialog.destFolder,
                                              collisionDialog.moveMode,
                                              colBtn.modelData.mode)
                                shell._reportTransfer(r, collisionDialog.moveMode,
                                                      collisionDialog.srcPath)
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Globale PDF-Seiten-Extraktion: Auswahldialog + Rückmeldungen ─────────
    //  Dieselbe Komponente wie in der PdfSurface, nur mit allen PDFs des
    //  Ordners und Namenspflicht (requireName) — die Ausgabe landet im
    //  aktuellen Ordner, deshalb hostet die Shell den Dialog (sie kennt
    //  Overlay und Statuszeile).
    PdfPageSelectDialog {
        id: globalExtractDlg
        anchors.fill: parent
        requireName: true
        titleText: App.uiText(App.language, "ExtractGlobalTitle")
        defaultName: ""
        onExtractRequested: (items, name) => {
            shell._extractPending = true
            shell._extractName    = name
            // items = [{path,page}] in Auswahlreihenfolge (Werkbank) bzw.
            // Originalreihenfolge (kompakt) → extractOrdered erhält die Reihenfolge.
            PdfExtract.extractOrdered(items, shell._extractTarget(), name)
        }
    }

    Connections {
        target: PdfExtract
        function onFolderPdfsReady(files) {
            if (!shell._scanPending) return          // Scan eines anderen Aufrufers
            shell._scanPending = false
            if (files.length === 0) {
                shell.statusText = App.uiText(App.language, "ExtractNoPdfs")
                statusClearTimer.restart()
                return
            }
            globalExtractDlg.openWith(files)
        }
        function onExtractProgress(done, total) {
            if (!shell._extractPending) return
            shell.statusText = App.uiText(App.language, "ExtractProgressToast")
                                   .arg(done).arg(total)
            statusClearTimer.restart()
        }
        function onExtractFinished(ok, targetPath, errorText) {
            if (!shell._extractPending) return
            shell._extractPending = false
            // ERST aktualisieren, DANN die eigene Meldung setzen: der Refresh
            // emittiert selbst eine statusMessage („Aktualisiert"), die sonst
            // unsere Erfolgsmeldung sofort wieder überschriebe.
            if (ok) App.refreshCurrentFolder()
            shell.statusText = ok
                ? App.uiText(App.language, "ExtractOkToast")
                      .arg(String(targetPath).split("/").pop())
                : App.uiText(App.language, "ExtractFailToast")
            statusClearTimer.restart()
        }
    }

    //  ── Meldungen: eingeblendeter TOAST statt Statusleiste ───────────────────
    //  Der frühere `footer` (24-px-Streifen) ist entfernt — er stand in JEDER
    //  Ansicht und kostete überall Fläche, im Vollbild lag er quer unter dem
    //  Video. `statusText` blieb als Sammelstelle bestehen; alle Melder
    //  (globale PDF-Extraktion, „max. 4 Dateien", Ordnerwechsel,
    //  `App.statusMessage`) schreiben unverändert dorthin. Angezeigt wird das
    //  jetzt wie in den Kacheln: ein Toast, der sich selbst ausblendet und
    //  KEINE Fläche kostet (Overlay, klickdurchlässig).
    Timer { id: statusClearTimer; interval: 4000; onTriggered: shell.statusText = "" }

    Rectangle {
        id: statusToast
        parent: Overlay.overlay
        z: 9999
        //  Unten mittig, mit Abstand zum Rand — im Vollbild wie in der Galerie.
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
        anchors.bottom: parent ? parent.bottom : undefined
        anchors.bottomMargin: 28
        width: Math.min(parent ? parent.width - 48 : 400, toastLabel.implicitWidth + 32)
        height: toastLabel.implicitHeight + 18
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.82)
        border.color: Qt.rgba(1, 1, 1, 0.18)
        visible: opacity > 0.01
        opacity: shell.statusText.length > 0 ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Text {
            id: toastLabel
            anchors.centerIn: parent
            width: Math.min(parent.width - 24, implicitWidth)
            text: shell.statusText
            color: "#ffffff"
            font.pixelSize: 12
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Connections {
        target: App
        function onStatusMessage(text) { shell.statusText = text; statusClearTimer.restart() }
        function onFolderOpened(path)  {
            // Ordnerwechsel: die ←/→-Navigation läuft über das Galerie-Modell des
            // aktuellen Ordners → beim Wechsel offene Kacheln (anderer Ordner)
            // verwerfen und einen laufenden Hinzufügen-Modus beenden.
            openFilesModel.clear()
            shell.slotOrder = []
            shell.pendingAddFile = false
            shell.statusText = path; statusClearTimer.restart()
        }
    }

    //  Eigener gethemter Wähler statt Qts FolderDialog (s. qml/common).
    FileChooser {
        id: folderDialog
        title: App.menuOpenFolderText
        fileMode: FileChooser.Directory
        onAccepted: App.openFolderUrl(folderDialog.selectedFolder)
    }

    // ── Lesezeichen anlegen/bearbeiten (geteilt mit SettingsBookmarksTab) ──────
    BookmarkEditDialog { id: bookmarkEditDialog }

    // ── Kachelgrößen-Dialog (Phase 4) ─────────────────────────────────────────
    TileSizeDialog { id: tileSizeDialog }

    // ── Einstellungs-Dialog (Phase 4) ─────────────────────────────────────────
    // Loader-gated: erst beim Öffnen instanziiert, beim Schließen wieder
    // freigegeben (RAM-Priorität — der Dialog mit acht Tabs lebt nicht dauerhaft).
    Component {
        id: settingsComponent
        SettingsDialog {}
    }
    Loader {
        id: settingsLoader
        active: false
        sourceComponent: settingsComponent
        onLoaded: item.open()
        Connections {
            target: settingsLoader.item
            ignoreUnknownSignals: true
            function onClosed() { settingsLoader.active = false }
        }
    }
    function openSettings() {
        if (settingsLoader.active && settingsLoader.item)
            settingsLoader.item.open()
        else
            settingsLoader.active = true   // → onLoaded öffnet den Dialog
    }
}
