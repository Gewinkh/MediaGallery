import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import MediaGallery 1.0

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

    title: App.currentFolder.length > 0
           ? ("MediaGallery — " + folderName(App.currentFolder))
           : "MediaGallery"

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

    property string statusText: ""

    Component.onCompleted: {
        if (App.startMaximized)
            shell.visibility = Window.Maximized
        App.restoreLastFolder()
    }

    onClosing: function(close) {
        App.saveWindowState(shell.width, shell.height, shell.x, shell.y,
                            shell.visibility === Window.Maximized)
    }

    function folderName(path) {
        var n = path.replace(/[\/\\]+$/, "")
        var i = Math.max(n.lastIndexOf("/"), n.lastIndexOf("\\"))
        return i >= 0 ? n.substring(i + 1) : n
    }

    // ── Menüleiste ───────────────────────────────────────────────────────────
    //  ThemedMenu: bisher folgten die Menü-POPUPS (Datei/Ansicht/Einstellungen/
    //  Ordner + deren Untermenüs) NICHT der in Einstellungen ▸ Design gewählten
    //  Menüleisten-Farbe (App.themeMenuBarBg) — nur die Leiste selbst (via
    //  palette.button) war korrekt eingefärbt, die aufklappenden Popups nutzten
    //  weiterhin die Fusion-Standardfarbe. Analog zum bereits korrekt
    //  eingefärbten Filter-Popup (FilterBar.qml) bekommt jedes Menu hier
    //  denselben expliziten Hintergrund.
    component ThemedMenu: Menu {
        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder; border.width: 1
            radius: 6
        }
    }

    menuBar: MenuBar {
        ThemedMenu {
            title: App.menuFileText
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
            title: App.menuViewText
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
            title: App.menuSettingsText
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
            title: App.menuBookmarksText

            MenuItem {
                text: App.bookmarkAddText
                // Öffnet denselben Hinzufügen-Dialog wie Einstellungen ▸ Lesezeichen
                // (Anzeigename + Pfad + Durchsuchen). Bewusst kein offener Ordner
                // mehr nötig — es lässt sich jeder Ordner als Lesezeichen anlegen.
                onTriggered: bookmarkEditDialog.openAdd()
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

    function paneRect(index, count, W, H) {
        var g = 2
        var rV = Math.max(_splitMin, Math.min(shell.splitV, _splitMax))
        var rH = Math.max(_splitMin, Math.min(shell.splitH, _splitMax))
        if (count <= 1)
            return { x: 0, y: 0, w: W, h: H }
        if (count === 2) {                                   // zwei Spalten nebeneinander
            var cwL = (W - g) * rV
            if (index === 0) return { x: 0,        y: 0, w: cwL,           h: H }
            return                { x: cwL + g,   y: 0, w: W - g - cwL,   h: H }
        }
        if (count === 3) {                                   // 2 oben, 1 unten (volle Breite)
            var thT = (H - g) * rH
            var cw3 = (W - g) * rV
            if (index === 0) return { x: 0,       y: 0,       w: cw3,         h: thT }
            if (index === 1) return { x: cw3 + g, y: 0,       w: W - g - cw3, h: thT }
            return                { x: 0,       y: thT + g, w: W,           h: H - g - thT }
        }
        var topH = (H - g) * rH                              // 2×2
        var leftW = (W - g) * rV
        var col = index % 2
        var rowi = Math.floor(index / 2)
        return { x: col === 0 ? 0 : leftW + g,
                 y: rowi === 0 ? 0 : topH + g,
                 w: col === 0 ? leftW : W - g - leftW,
                 h: rowi === 0 ? topH : H - g - topH }
    }

    function indexOfOpenFile(p) {
        for (var i = 0; i < openFilesModel.count; i++)
            if (openFilesModel.get(i).path === p) return i
        return -1
    }

    // Kachel schließen (Zurück/Esc einer Datei). Letzte Kachel → zurück zur Galerie.
    function closePane(i) {
        if (i < 0 || i >= openFilesModel.count) return
        openFilesModel.remove(i)              // gibt den zugehörigen Viewer sofort frei
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
                && openFilesModel.count < shell.maxOpenFiles)
            openFilesModel.append({ path: p })
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
            StackView.onStatusChanged: _checkReady()

            // Trennfugen-Hintergrund (scheint in der 2 px-Lücke zwischen Kacheln durch).
            Rectangle { anchors.fill: parent; color: "#0a0a0a" }

            readonly property int paneCount: openFilesModel.count

            // ── Kacheln: ein FullscreenViewer je Datei, per Split-Layout platziert ─
            Repeater {
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
                        startPath: paneLoader.path
                        // Mehr als eine Datei offen → untere Hover-Navigation der Kachel
                        // (Pfeile + Zähler) ausblenden; bei genau einer Datei wieder an.
                        splitActive: splitPage.paneCount > 1
                        canAddMore:  splitPage.paneCount < shell.maxOpenFiles
                        onBackRequested:    shell.closePane(paneLoader.index)
                        onAddFileRequested: shell.requestAddFile()
                    }
                    onLoaded: item.forceActiveFocus()

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
            //  Vertikal (Spalten) ab 2 Kacheln; horizontal (Zeilen) ab 3 Kacheln.
            //  Ziehen setzt shell.splitV / shell.splitH (geklemmt) → paneRect folgt.
            readonly property real _gap: 2
            readonly property real _vX: (splitPage.width  - _gap) * Math.max(shell._splitMin, Math.min(shell.splitV, shell._splitMax))
            readonly property real _hY: (splitPage.height - _gap) * Math.max(shell._splitMin, Math.min(shell.splitH, shell._splitMax))

            Rectangle {                                       // vertikaler Trenner
                z: 50
                visible: splitPage.paneCount >= 2
                x: splitPage._vX - 3; y: 0
                width: 8
                height: splitPage.paneCount === 3 ? splitPage._hY : splitPage.height
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
                visible: splitPage.paneCount >= 3
                x: 0; y: splitPage._hY - 3
                width: splitPage.width; height: 8
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
        if (stack.depth < 2)
            stack.push(_splitPage())
    }
    function popFullscreen() {
        openFilesModel.clear()          // Verlassen schließt alle Dateien (RAM frei)
        if (stack.depth > 1)
            stack.pop()                 // die leere Split-Seite überlebt (persistentes Item)
    }

    // ── Drag & Drop ───────────────────────────────────────────────────────────
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls) {
                App.handleDroppedUrls(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    // ── Statusleiste ─────────────────────────────────────────────────────────
    footer: Rectangle {
        implicitHeight: 24
        color: App.themeStatusBarBg
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: App.themeBorder }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 12; anchors.rightMargin: 12
            text: shell.statusText
            color: App.themeTextMuted; font.pixelSize: 11
            elide: Text.ElideRight
        }
    }

    Timer { id: statusClearTimer; interval: 4000; onTriggered: shell.statusText = "" }

    Connections {
        target: App
        function onStatusMessage(text) { shell.statusText = text; statusClearTimer.restart() }
        function onFolderOpened(path)  {
            // Ordnerwechsel: die ←/→-Navigation läuft über das Galerie-Modell des
            // aktuellen Ordners → beim Wechsel offene Kacheln (anderer Ordner)
            // verwerfen und einen laufenden Hinzufügen-Modus beenden.
            openFilesModel.clear()
            shell.pendingAddFile = false
            shell.statusText = path; statusClearTimer.restart()
        }
    }

    FolderDialog {
        id: folderDialog
        title: App.menuOpenFolderText
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
