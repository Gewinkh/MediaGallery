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

// Wurzel-Fenster der QML-UI: die Galerie-Seite trägt FilterBar und ein optional einblendbares
// TagCategoryPanel, die Vollbild-Seite des StackView den FullscreenViewer.
ApplicationWindow {
    id: shell
    visible: true

    // Nur ein WINDOW kennt den Bildschirm verlässlich - das an ein Item angehängte `Screen` meldet im
    // geschlossenen Popup den PRIMÄREN (gemessen 1536 statt 1920).
    readonly property int _screenW: shell.screen ? shell.screen.width : 0
    on_ScreenWChanged: App.setScreenWidth(shell._screenW)

    width:  App.initialWindowWidth  > 0 ? App.initialWindowWidth  : 1200
    height: App.initialWindowHeight > 0 ? App.initialWindowHeight : 800
    x: App.initialWindowX
    y: App.initialWindowY

    title: {
        const basis = App.currentFolder.length > 0
                      ? ("MediaGallery - " + folderName(App.currentFolder))
                      : "MediaGallery"
        return (!shell.galleryVisible && shell.activeFilePath.length > 0)
               ? (basis + " / " + folderName(shell.activeFilePath))
               : basis
    }

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
    // Hier noch einmal, obwohl main.cpp sie auf der QGuiApplication-Palette setzt: Dateidialoge öffnen als
    // eigenes Fenster und erben die Palette ihres ELTERNfensters. Werte identisch zu `applyThemePalette` halten.
    palette.light:      Qt.lighter(App.themeCard, 1.3)
    palette.midlight:   Qt.lighter(App.themeBorder, 1.2)
    palette.dark:       Qt.darker(App.themeBackground, 1.2)
    palette.shadow:     Qt.darker(App.themeBackground, 1.6)
    palette.alternateBase:   App.themeBackground
    palette.placeholderText: App.themeTextMuted

    property string statusText: ""

    property bool galleryVisible: true
    property Item firstPaneItem: null
    property bool swapPreview: false

    // Geometrie und Fensterzustand VOR dem Vollbild merken: `visibility` liefert dann nur noch FullScreen und
    // width/height die Bildschirmmaße - ein Beenden im Vollbild überschriebe sonst die gespeicherte Größe damit.
    property bool immersiveFullscreen: false
    property int  _preImmersiveVisibility: Window.Windowed
    property rect _preImmersiveGeometry: Qt.rect(0, 0, 0, 0)
    property bool _windowWasSwitched: false

    function setImmersive(on) {
        if (shell.immersiveFullscreen === on) return
        if (on) {
            shell._preImmersiveVisibility = shell.visibility
            shell._preImmersiveGeometry = Qt.rect(shell.x, shell.y, shell.width, shell.height)
            shell.immersiveFullscreen = true
            // War das Fenster schon im Vollbild, wird es nicht angefasst - F nimmt dann nur die Chrome weg. Blindes
            // Setzen und späteres Zurücksetzen warf es auf seine normale Größe zurück.
            shell._windowWasSwitched = (shell.visibility !== Window.FullScreen)
            if (shell._windowWasSwitched)
                shell.visibility = Window.FullScreen
        } else {
            shell.immersiveFullscreen = false
            if (!shell._windowWasSwitched)
                return                     // Fenster gehörte uns nie
            shell._windowWasSwitched = false
            // Erst in den normalen Zustand samt gemerkter Geometrie, dann maximieren: den direkten Sprung Vollbild ->
            // Maximiert verschlucken manche Fenstermanager.
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

    property bool _scanPending: false
    property bool _extractPending: false
    property string _extractFolder: ""
    function _extractTarget() {
        return shell._extractFolder.length > 0 ? shell._extractFolder
                                               : App.currentFolder
    }
    property string _extractName: ""

    Component.onCompleted: {
        App.setScreenWidth(shell._screenW)
        if (App.startMaximized)
            shell.visibility = Window.Maximized
        App.restoreLastFolder()
        // Die zweite Hälfte entsteht auch ohne Ordner, wenn sie im Player-Modus stand: dort zeigt sie die
        // Warteschlange, `secondFolder` bleibt leer, und sie wäre ersatzlos verschwunden.
        var second = App.secondFolder()
        if (second.length > 0 || Audio.playerModePaneRemembered(1)) {
            var p = App.addPane()
            if (p && second.length > 0) p.openFolder(second)
            App.focusPane(0)
        }
        Audio.restoreSession()
    }

    onClosing: function(close) {
        Audio.rememberSession()
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

    function _normalizedFolderPath(p) {
        return p.replace(/([^\/\\])[\/\\]+$/, "$1")
    }
    function _bookmarkPrefillPath() {
        var cur = App.currentFolder
        if (cur.length === 0) return ""
        var norm = _normalizedFolderPath(cur)
        var list = App.savedFolders
        for (var i = 0; i < list.length; i++)
            if (_normalizedFolderPath(list[i].path) === norm) return ""
        return cur
    }


    // EIGENE Menüleiste statt der nativen `MenuBar`: Fusion belegt die ALT-Taste für Mnemoniks, sprachabhängig
    // und per API nicht abschaltbar - das kollidierte mit Alt+S/Alt+Q/Alt+<-.
    menuBar: Rectangle {
        id: menuStrip
        visible: !shell.immersiveFullscreen
        implicitHeight: 32
        color: App.themeMenuBarBg
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: App.themeBorder }

        component PaneMenuBtn: Rectangle {
            id: pmb
            property string label: ""
            property bool   folder: false          // false = Datei, true = Ordner
            readonly property var paneItem: shell.firstPaneItem
            width: visible ? pmbLbl.implicitWidth + 22 : 0
            height: 24; radius: 5
            anchors.verticalCenter: parent.verticalCenter
            color: pmbHover.hovered ? App.themeCard : "transparent"
            Text { id: pmbLbl; anchors.centerIn: parent; text: pmb.label
                   color: App.themeTextPrimary; font.pixelSize: 13 }
            HoverHandler { id: pmbHover }
            TapHandler {
                onTapped: {
                    if (!pmb.paneItem) return
                    const open = pmb.folder ? pmb.paneItem.folderMenuOpen()
                                            : pmb.paneItem.fileMenuOpen()
                    if (open) { pmb.paneItem.closeMenus(); return }
                    if (pmb.folder) pmb.paneItem.popupFolderMenu(pmb)
                    else            pmb.paneItem.popupFileMenu(pmb)
                }
            }
        }

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

        ScrollableBar {
            id: menuBtnRow
            anchors { left: parent.left; leftMargin: 6; right: parent.right; rightMargin: 6
                      verticalCenter: parent.verticalCenter }
            height: parent.height
            spacing: 2
            PaneMenuBtn { label: App.menuFileText;  folder: false; visible: !paneArea.split }
            MenuBtn { label: App.menuViewText;      menu: viewMenu }
            MenuBtn { label: App.menuSettingsText;  menu: settingsMenu }
            PaneMenuBtn { label: App.menuBookmarksText; folder: true; visible: !paneArea.split }
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
                onTriggered: {
                    tileSizeLoader.active = true
                    tileSizeLoader.item.openDialog()
                }
            }
            MenuSeparator {}
            MenuItem {
                text: App.uiText(App.language, "ViewMenuImmersive")
                checkable: true
                checked: shell.immersiveFullscreen
                onTriggered: shell.toggleImmersive()
            }
            MenuSeparator {}
            MenuItem {
                text: App.paneCount > 1 ? App.uiText(App.language, "MenuUnsplitWindow")
                                        : App.uiText(App.language, "MenuSplitWindow")
                onTriggered: {
                    if (App.paneCount > 1) paneArea.unsplit()
                    else                   App.addPane()
                }
            }
            MenuItem {
                text: App.uiText(App.language, "MenuSwapPanes")
                enabled: App.paneCount > 1
                onTriggered: App.swapPanes()
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

    }

    Item {
        id: paneArea
        objectName: "paneArea"      // Griff für tests/bench
        anchors.fill: parent

        readonly property bool split: App.paneCount > 1

        function _paneEmpty(i) {
            const h = paneRepeater.itemAt(i)
            return !(h && h.item) || h.item.galleryActive
        }
        function unsplit() {
            const focused = Math.max(0, App.focusedPaneIndex)
            const other   = (focused === 0) ? 1 : 0
            if (_paneEmpty(focused) && !_paneEmpty(other)) App.closePane(focused)
            else                                          App.closePane(other)
        }
        readonly property real dividerW: 6
        readonly property real leftW: split
            ? Math.max(120, Math.min(width - dividerW - 120,
                                     Math.round((width - dividerW) * App.paneSplit)))
            : width

        Repeater {
            id: paneRepeater
            // Modell, NICHT `App.panes`: über eine Liste baut ein Repeater bei jeder Änderung ALLE Delegates neu - die
            // zweite Hälfte aufzumachen zerstörte damit die erste samt geöffneter Datei (gemessen).
            model: App.panesModel
            delegate: PaneHost {
                id: paneHost
                required property int index
                required property var paneObject

                pane: paneHost.paneObject
                source: "qrc:/qml/gallery/GalleryPane.qml"
                y: 0
                height: paneArea.height
                x: paneHost.index === 0 ? 0 : paneArea.leftW + paneArea.dividerW
                width: paneHost.index === 0
                       ? paneArea.leftW
                       : Math.max(0, paneArea.width - paneArea.leftW - paneArea.dividerW)

                onLoadFailed: function(err) { console.warn("Hälfte nicht ladbar:", err) }

                Binding {
                    target: shell
                    property: "firstPaneItem"
                    value: paneHost.item
                    when: paneHost.index === 0
                    restoreMode: Binding.RestoreNone
                }

                Binding { target: paneHost.item; property: "splitActive"; value: paneArea.split
                          when: paneHost.item !== null }
                Binding { target: paneHost.item; property: "showClose";   value: paneArea.split
                          when: paneHost.item !== null }
                Binding { target: paneHost.item; property: "paneIndex";   value: paneHost.index
                          when: paneHost.item !== null }
                Binding { target: paneHost.item; property: "paneFocused"
                          value: paneHost.index === App.focusedPaneIndex
                          when: paneHost.item !== null }
                Binding { target: paneHost.item; property: "immersive"
                          value: shell.immersiveFullscreen
                          when: paneHost.item !== null }

                Binding {
                    target: shell
                    property: "galleryVisible"
                    value: paneHost.item ? paneHost.item.galleryActive : true
                    when: paneHost.item !== null && paneHost.index === App.focusedPaneIndex
                }
                Binding {
                    target: shell
                    property: "activeFilePath"
                    value: paneHost.item ? paneHost.item.activeFilePath : ""
                    when: paneHost.item !== null && paneHost.index === App.focusedPaneIndex
                    restoreMode: Binding.RestoreNone
                }

                Connections {
                    target: paneHost.item
                    function onFocusRequested() { App.focusPane(paneHost.index) }
                    function onCloseRequested() { App.closePane(paneHost.index) }
                    function onStatusRequested(text) {
                        shell.statusText = text
                        statusClearTimer.restart()
                    }
                    function onExtractRequested(folder) {
                        App.focusPane(paneHost.index)
                        shell._extractFolder = folder
                        shell._scanPending = true
                        PdfExtract.scanFolder(shell._extractTarget())
                    }
                    function onFolderDropRequested(sourcePaths, folderPath) {
                        App.focusPane(paneHost.index)
                        shell._dropPathsIntoFolder(sourcePaths, folderPath)
                    }
                    function onExternalDropRequested(urls, folderPath) {
                        App.focusPane(paneHost.index)
                        App.handleDroppedUrls(urls, folderPath)
                    }
                    function onImmersiveToggleRequested() { shell.toggleImmersive() }
                    function onBarDragMoved(x) {
                        const p = paneArea.mapFromItem(null, x, 0)
                        shell.swapPreview = (paneHost.index === 0)
                                            ? (p.x > paneArea.leftW + paneArea.dividerW)
                                            : (p.x < paneArea.leftW)
                    }
                    function onBarDragReleased(x) {
                        const p = paneArea.mapFromItem(null, x, 0)
                        const over = (paneHost.index === 0)
                                     ? (p.x > paneArea.leftW + paneArea.dividerW)
                                     : (p.x < paneArea.leftW)
                        shell.swapPreview = false
                        if (over) App.swapPanes()
                    }
                }
            }
        }

        Rectangle {
            visible: shell.swapPreview
            anchors.fill: parent
            color: "transparent"
            border.color: App.themeAccent
            border.width: 3
            radius: 4
            z: 50
        }

        Rectangle {
            id: paneDivider
            visible: paneArea.split
            x: paneArea.leftW
            y: 0
            width: paneArea.dividerW
            height: paneArea.height
            color: (dividerHover.hovered || dividerDrag.active) ? App.themeAccent : App.themeBorder

            HoverHandler { id: dividerHover; cursorShape: Qt.SplitHCursor }
            DragHandler {
                id: dividerDrag
                target: null
                yAxis.enabled: false
                onCentroidChanged: {
                    if (!active || paneArea.width <= 0) return
                    var p = paneArea.mapFromItem(null, centroid.scenePosition.x, 0)
                    App.paneSplit = p.x / paneArea.width
                }
            }
        }
    }


    // `z: -1` ist PFLICHT: ein Zug geht immer nur an die OBERSTE annehmende DropArea unter dem Zeiger. Diese
    // füllt das ganze Fenster und verschluckte sonst die Ablegeflächen der Tag-Seitenleiste vollständig.
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

    // Während eines Zuges gehört der Zeiger dem Compositor, das Mausrad erreicht die Anwendung nicht - ein aus
    // dem Bild gescrollter Ordner wäre nur über Randscrollen erreichbar. Die Leiste bringt die Ziele zum Zeiger.
    Rectangle {
        id: folderDropBar
        z: 91
        visible: App.tileDragActive && shell.galleryVisible
                 && folderDropRepeater.count > 0
        anchors { left: parent.left; right: parent.right; bottom: bookmarkDropBar.top }
        anchors.bottomMargin: bookmarkDropBar.visible ? 6 : 0
        height: 70
        color: App.themeMenuBarBg
        border.color: App.themeAccent; border.width: 1

        // Ziele werden EINMAL beim Beginn des Zuges eingesammelt. NICHT an `visible` hängen: das fragt `count` ab,
        // der wiederum an `targets` hängt - die Leiste käme nie hoch.
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

    property int _dragPaneIndex: 0
    Connections {
        target: App
        function onTileDragActiveChanged() {
            if (App.tileDragActive)
                shell._dragPaneIndex = Math.max(0, App.focusedPaneIndex)
        }
    }
    function _paneAt(i) {
        var list = App.panes
        return (i >= 0 && i < list.length) ? list[i] : (list.length > 0 ? list[0] : null)
    }
    function _modelOwning(path) {
        var list = App.panes
        for (var i = 0; i < list.length; i++) {
            var m = list[i] ? list[i].mediaModel : null
            if (m && m.ownsFile(path)) return m
        }
        var p = shell._paneAt(shell._dragPaneIndex)
        return p ? p.mediaModel : null
    }

    function _visibleFolders() {
        var out = []
        var pane = shell._paneAt(shell._dragPaneIndex)
        if (!pane) return out
        var gm = pane.galleryModel
        if (pane.currentFolder.length > 0)
            out.push({ path: pane.currentFolder,
                       name: shell.folderName(pane.currentFolder),
                       depth: 0 })
        if (!gm) return out
        for (var i = 0; i < gm.count; ++i) {
            if (gm.mediaTypeAt(i) !== 7) continue
            out.push({ path: gm.filePathAt(i),
                       name: gm.displayNameAt(i),
                       depth: gm.depthAt(i) + 1 })
        }
        return out
    }

    function _dropUrlsOnFolder(urls, destFolder) {
        if (!urls || urls.length === 0 || destFolder.length === 0) return
        const src = App.localPath(urls[0])
        const owner = shell._modelOwning(src)
        if (owner && owner.ownsFile(src)) {
            const paths = []
            for (var i = 0; i < urls.length; ++i) {
                const p = App.localPath(urls[i])
                if (p.length === 0) continue
                const cut = Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"))
                if (cut > 0 && p.substring(0, cut) === destFolder) continue
                paths.push(p)
            }
            shell._dropPathsIntoFolder(paths, destFolder)
        } else {
            App.handleDroppedUrls(urls, destFolder)
        }
    }

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
                            shell._dropUrlsOnFolder(drop.urls, bmTarget.modelData.path)
                            drop.acceptProposedAction()
                        }
                    }
                }
            }
        }
    }

    // Ablegen auf einem Lesezeichen oder einer Ordnerkachel ist derselbe Vorgang; die Regeln stehen im
    // Modell (`transferToFolder`). Mehrere Dateien laufen als Warteschlange: bei belegtem Namen wird
    // ANGEHALTEN und gefragt - eine Sammelantwort gibt es bewusst nicht, jede Kollision ist eine eigene Entscheidung.
    property var    _dropQueue: []
    property string _dropDest: ""
    property int    _dropOk: 0
    property int    _dropFail: 0
    property string _dropLast: ""

    function _dropIntoFolder(path, destFolder) {
        shell._dropPathsIntoFolder([path], destFolder)
    }
    function _dropPathsIntoFolder(paths, destFolder) {
        if (!paths || paths.length === 0 || destFolder.length === 0) return
        shell._dropQueue = paths.slice()
        shell._dropDest  = destFolder
        shell._dropOk    = 0
        shell._dropFail  = 0
        shell._dropLast  = ""
        shell._dropNext()
    }
    function _dropNext() {
        const move = App.fileDropMove
        while (shell._dropQueue.length > 0) {
            const path = shell._dropQueue.shift()
            if (!path || path.length === 0) continue
            const owner = shell._modelOwning(path)
            if (!owner) { shell._dropFail++; continue }
            const r = owner.transferToFolder(path, shell._dropDest, move, 0)
            if (r === 1) {
                collisionDialog.srcPath    = path
                collisionDialog.destFolder = shell._dropDest
                collisionDialog.moveMode   = move
                collisionDialog.altName    = owner.transferTargetName(path, shell._dropDest)
                collisionDialog.open()
                return                       // der Dialog macht weiter
            }
            shell._noteTransfer(r, path)
        }
        shell._reportDropBatch(move)
    }
    function _noteTransfer(result, path) {
        if (result === 0) { shell._dropOk++; shell._dropLast = String(path).split("/").pop() }
        else              { shell._dropFail++ }
    }
    function _reportDropBatch(move) {
        if (shell._dropOk === 1 && shell._dropFail === 0)
            shell.statusText = App.uiText(App.language, move ? "DropMoved" : "DropCopied")
                               + shell._dropLast
        else if (shell._dropOk > 0)
            shell.statusText = App.uiText(App.language, move ? "DropMoved" : "DropCopied")
                               + App.uiText(App.language, "DropBatchCount")
                                     .replace("%1", shell._dropOk)
        else if (shell._dropFail > 0)
            shell.statusText = App.uiText(App.language, "DropFailed") + shell._dropLast
        else
            return
        statusClearTimer.restart()
    }


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
                                if (colBtn.modelData.mode !== 0) {
                                    const owner = shell._modelOwning(collisionDialog.srcPath)
                                    if (owner) {
                                        const r = owner.transferToFolder(
                                                      collisionDialog.srcPath,
                                                      collisionDialog.destFolder,
                                                      collisionDialog.moveMode,
                                                      colBtn.modelData.mode)
                                        shell._noteTransfer(r, collisionDialog.srcPath)
                                    }
                                }
                                shell._dropNext()
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: globalExtractComponent
        PdfPageSelectDialog {
            requireName: true
            titleText: App.uiText(App.language, "ExtractGlobalTitle")
            defaultName: ""
            onExtractRequested: (items, name) => {
                shell._extractPending = true
                shell._extractName    = name
                PdfExtract.extractOrdered(items, shell._extractTarget(), name)
            }
        }
    }
    Loader {
        id: globalExtractLoader
        anchors.fill: parent
        active: false
        sourceComponent: globalExtractComponent
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
            globalExtractLoader.active = true
            globalExtractLoader.item.openWith(files)
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
            if (ok) App.refreshCurrentFolder()
            shell.statusText = ok
                ? App.uiText(App.language, "ExtractOkToast")
                      .arg(String(targetPath).split("/").pop())
                : App.uiText(App.language, "ExtractFailToast")
            statusClearTimer.restart()
        }
    }

    Timer { id: statusClearTimer; interval: 4000; onTriggered: shell.statusText = "" }

    Rectangle {
        id: statusToast
        parent: Overlay.overlay
        z: 9999
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
        function onFolderOpened(path)  { shell.statusText = path; statusClearTimer.restart() }
    }

    Connections {
        target: Audio
        function onMessage(text) {
            if (!text || text.length === 0) return
            shell.statusText = text
            statusClearTimer.restart()
        }
    }



    Component { id: tileSizeComponent; TileSizeDialog {} }
    Loader {
        id: tileSizeLoader
        active: false
        sourceComponent: tileSizeComponent
    }

    Component { id: audioTrackComponent; AudioTrackDialog {} }
    Loader {
        id: audioTrackLoader
        active: false
        sourceComponent: audioTrackComponent
    }
    Connections {
        target: Audio
        function onTrackChoiceNeeded(source, tracks) {
            audioTrackLoader.active = true
            audioTrackLoader.item.openFor(source, tracks)
        }
    }

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
            settingsLoader.active = true   // -> onLoaded öffnet den Dialog
    }
}
