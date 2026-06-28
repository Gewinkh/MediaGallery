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
    menuBar: MenuBar {
        Menu {
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

        Menu {
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

        Menu {
            title: App.menuSettingsText
            MenuItem {
                text: App.uiText(App.language, "MenuSettingsItem")
                onTriggered: shell.openSettings()
            }
            MenuSeparator {}
            Menu {
                title: App.menuLanguageText
                MenuItem { text: "Deutsch"; checkable: true; checked: App.language === "de"; onTriggered: App.setLanguage("de") }
                MenuItem { text: "English"; checkable: true; checked: App.language === "en"; onTriggered: App.setLanguage("en") }
            }
            Menu {
                title: App.menuVideoPlaybackText
                MenuItem { text: App.menuVideoNativeText;   checkable: true; checked: App.videoPlayback === "native";   onTriggered: App.setVideoPlayback("native") }
                MenuItem { text: App.menuVideoExternalText; checkable: true; checked: App.videoPlayback === "external"; onTriggered: App.setVideoPlayback("external") }
            }
        }

        Menu {
            id: bookmarksMenu
            title: App.menuBookmarksText
            MenuItem {
                text: App.bookmarkAddText
                enabled: App.currentFolder.length > 0
                onTriggered: App.addCurrentFolderAsBookmark()
            }
            MenuSeparator { visible: App.savedFolders.length > 0 }
            MenuItem {
                text: App.menuBookmarksEmptyText
                enabled: false
                visible: App.savedFolders.length === 0
            }
            Instantiator {
                model: App.savedFolders
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name
                    onTriggered: App.openBookmark(modelData.path)
                }
                onObjectAdded: (index, object) => bookmarksMenu.insertItem(index + 3, object)
                onObjectRemoved: (index, object) => bookmarksMenu.removeItem(object)
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
            //  S = Optionen umschalten, R = Ordner/Vorschau neu laden,
            //  B = Vorschau-Sperre (blockieren ⇄ neu laden). Einzeltasten werden
            //  von fokussierten Textfeldern via Shortcut-Override unterdrückt.
            Shortcut {
                sequence: "S"; enabled: stack.depth === 1
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
                onCategoryPanelToggled: catPanel.visible = !catPanel.visible
            }

            GalleryView {
                id: galleryView
                anchors {
                    left: parent.left
                    right: catPanel.visible ? catPanel.left : parent.right
                    top: filterBar.bottom
                    bottom: modeBanner.visible ? modeBanner.top : parent.bottom
                }
                onActivated: function(filePath) { pushFullscreen(filePath) }
            }

            TagCategoryPanel {
                id: catPanel
                visible: false
                width: 300
                anchors { right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
                onEnterAddToTagMode: function(tag) { galleryView.enterAddToTagMode(tag) }
                onEnterGroupMode: function(tag) { galleryView.enterGroupMode(tag) }
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

    Component {
        id: fullscreenComponent
        FullscreenViewer {
            startPath: shell.pendingFullscreenPath
            onBackRequested: popFullscreen()
        }
    }

    // Navigations-API.
    property string pendingFullscreenPath: ""
    function pushFullscreen(filePath) {
        pendingFullscreenPath = filePath !== undefined ? filePath : ""
        if (stack.depth < 2)
            stack.push(fullscreenComponent)
    }
    function popFullscreen() {
        if (stack.depth > 1)
            stack.pop()
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
        function onFolderOpened(path)  { shell.statusText = path; statusClearTimer.restart() }
    }

    FolderDialog {
        id: folderDialog
        title: App.menuOpenFolderText
        onAccepted: App.openFolderUrl(folderDialog.selectedFolder)
    }

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
