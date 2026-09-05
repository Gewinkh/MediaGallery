pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"
import "../settings"
import "../tags"
import "../viewer"

// EINE vollständige Hälfte des Hauptfensters. `PaneHost` erzeugt sie mit einem EIGENEN QML-Kontext, in
// dem `mediaModel`, `galleryModel` und `Tags` auf die Objekte dieser Hälfte zeigen - deshalb steht hier
// nirgends "Hälfte A oder B". Fensterweites (Vollbild, Ablegeleisten, Dialoge) hostet die Shell.
Item {
    id: pane

    // EINE Quelle, weil zwei Dinge daran hängen: die Darstellung der Galerie UND worauf Strg + '+'/'-' bzw.
    // Strg + Rad wirken. Zwei Hälften können verschieden stehen.
    readonly property bool listMode: pane.playerMode ? Audio.listLayout
                                                     : App.galleryListLayout
    objectName: "galleryPane"      // Griff für tests/bench

    property bool showClose: false      // zwei Hälften ⇒ „Schließen" statt „Beenden"
    property bool paneFocused: true     // hat diese Hälfte den Fokus?
    property bool splitActive: false    // gibt es überhaupt zwei Hälften?
    property int  paneIndex: 0         // Platz in `App.panes` (Shell setzt ihn)
    property bool immersive: false      // Fenster-Vollbild (F) - gehört der Shell
    property string activeFilePath: ""
    //  Die aktive Vollbild-Kachel selbst - für die Übergabe an den Player-Modus
    //  (Stelle der Wiedergabe, Anhalten). Nur gesetzt, solange eine offen ist.
    property var    activeViewer: null

    signal extractRequested(string folder)     // „PDF-Seiten…" (Dialog hostet die Shell)
    signal folderDropRequested(var sourcePaths, string folderPath)
    signal externalDropRequested(var urls, string folderPath)
    signal statusRequested(string text)
    signal focusRequested()                    // diese Hälfte ist jetzt gemeint
    signal closeRequested()                    // Datei ▸ Schließen (nur bei zwei Hälften)
    signal immersiveToggleRequested()
    signal barDragMoved(real x)
    signal barDragReleased(real x)

    // Der Player-Modus filtert die Galerie auf Audio (und optional Video) und schaltet das Öffnen um: ein
    // Doppelklick SPIELT. Die Leiste erscheint erst, wenn wirklich etwas läuft.
    property bool playerMode: false

    //  Zustand des Tags/Kategorien-Panels. Er liegt HIER und nicht im Panel,
    //  weil das Panel erst entsteht, wenn einer der beiden Abschnitte an ist
    //  (s. `catPanelLoader`) - vorher gäbe es niemanden, der ihn hielte.
    property bool tagsSectionOn: false
    property bool catsSectionOn: false
    property var  _savedFilter: null

    //  Besitzt DIESE Hälfte die laufende Wiedergabe? Nur dann gehören ihr die
    //  Leiste und die große Ansicht - sonst stünde der Player in beiden Galerien.
    readonly property bool playerMine: Audio.owner === PaneCtl

    // Nicht `pane.paneIndex` allein: die Shell bindet ihn erst, wenn der PaneHost sein `item` hat - also NACH
    // `Component.onCompleted`, wo noch 0 stand und die zweite Hälfte ihren Player-Merker mit 0 abfragte.
    function _myIndex() {
        const i = App.indexOfPane(PaneCtl)
        return i >= 0 ? i : pane.paneIndex
    }

    function togglePlayerMode() { pane.setPlayerMode(!pane.playerMode) }
    function setPlayerMode(on) {
        if (pane.playerMode === on) return
        if (on) {
            pane._savedFilter = { images: galleryModel.showImages,
                                  videos: galleryModel.showVideos,
                                  audio:  galleryModel.showAudio,
                                  pdfs:   galleryModel.showPdfs,
                                  texts:  galleryModel.showTexts }
            // Die WEISSE Liste entscheidet (`MediaProxyModel::isPlayableType`); ohne sie lief alles Unbekannte (ZIP, XLSX)
            // an jedem Häkchen vorbei. Die Häkchen bleiben trotzdem stimmig.
            galleryModel.onlyPlayable = true
            galleryModel.showImages = false
            galleryModel.showPdfs   = false
            galleryModel.showTexts  = false
            galleryModel.showAudio  = true
            galleryModel.showVideos = Audio.showVideos
            pane.playerMode = true
            PaneCtl.playerMode = true            // überlebt das Neubauen der Hälfte
            Audio.rememberPlayerMode(true, pane._myIndex())
            if (!Audio.owner || !Audio.active) Audio.owner = PaneCtl
            pane._pushQueue()
            return
        }

        if (pane.playerMine) {
            Audio.stop()
            Audio.owner = null
        }
        PaneCtl.playerViewOpen = false
        if (pane._playerPage && paneStack.depth > 1) paneStack.pop()
        // Gemerkt wird der Zustand VOR dem Modus. Ist keiner da - oder war er selbst schon "nur Ton" -, gilt ALLES AN:
        // mit einem Rest-Filter "nur Audio" stünde man vor einer halbleeren Galerie und wüsste nicht, warum.
        galleryModel.onlyPlayable = false
        const f = pane._savedFilter
        const meaningful = f && (f.images || f.pdfs || f.texts)
        galleryModel.showImages = meaningful ? f.images : true
        galleryModel.showVideos = meaningful ? f.videos : true
        galleryModel.showAudio  = meaningful ? f.audio  : true
        galleryModel.showPdfs   = meaningful ? f.pdfs   : true
        galleryModel.showTexts  = meaningful ? f.texts  : true
        pane._savedFilter = null
        pane.playerMode = false
        PaneCtl.playerMode = false
        Audio.rememberPlayerMode(false, pane._myIndex())
    }

    Component.onCompleted: {
        if (PaneCtl.playerMode) {
            pane.playerMode = true
            if (!Audio.owner) Audio.owner = PaneCtl
            pane._pushQueue()
            if (PaneCtl.playerViewOpen) pane.openPlayerView()
        } else if (Audio.takePlayerModeRestore(pane._myIndex())) {
            pane.setPlayerMode(true)
        }
    }

    // Alt+A aus einer offenen Datei: zurück zur Galerie in den Player-Modus, eine Audiodatei läuft an derselben
    // Stelle weiter. Die Kachel spielt über `MediaPlayer`, der Modus über die eigene Kette - dort anhalten, hier aufnehmen.
    function enterPlayerFromViewer() {
        const path = pane.activeFilePath
        var pos = -1
        var wasRunning = false
        if (pane.activeViewer) {
            pos = pane.activeViewer.mediaPositionMs()
            wasRunning = pane.activeViewer.mediaRunning()
            pane.activeViewer.pauseMedia()
        }
        const row = path.length > 0 ? galleryModel.rowForPath(path) : -1
        const type = row >= 0 ? galleryModel.mediaTypeAt(row) : -1
        const isSound = type === 2 || (Audio.showVideos && type === 1)

        pane.popFullscreen()
        pane.setPlayerMode(true)
        if (!isSound || path.length === 0) return

        pane.playHere(path)
        if (pos > 0) Audio.seek(pos)
        if (!wasRunning) Audio.togglePlay()      // stand still ⇒ still bleiben
    }

    function playHere(filePath) {
        Audio.owner = PaneCtl
        if (Audio.currentPath !== filePath)
            Audio.playFile(filePath, pane._visibleAudioPaths())
    }

    property var _playerPage: null
    function openPlayerView() {
        if (!pane.playerMode) return
        if (paneStack.depth > 1) return              // Vollbild liegt schon oben
        if (!pane._playerPage)
            pane._playerPage = playerViewComponent.createObject(pane)
        if (pane._playerPage) {
            paneStack.push(pane._playerPage)
            PaneCtl.playerViewOpen = true      // überlebt das Neubauen der Hälfte
        }
    }

    function _visibleAudioPaths() {
        var out = []
        for (var i = 0; i < galleryModel.count; ++i) {
            var t = galleryModel.mediaTypeAt(i)     // 1 = Video, 2 = Audio
            if (t === 2 || (Audio.showVideos && t === 1))
                out.push(galleryModel.filePathAt(i))
        }
        return out
    }
    function _pushQueue() {
        if (!pane.playerMode || !(pane.playerMine || !Audio.active)) return
        const list = pane._visibleAudioPaths()
        if (list.length === 0 && Audio.queue.length > 0) return
        Audio.setQueue(list)
    }

    Connections {
        target: PaneCtl
        function onFolderOpened(path) {
            if (!pane.playerMode) return
            if (pane.playerMine) Audio.stopAndClear()
            if (paneStack.depth > 1) {
                PaneCtl.playerViewOpen = false
                pane.popFullscreen()
            }
            pane._pushQueue()
        }
    }
    Connections {
        target: Audio
        function onOptionsChanged() {
            if (pane.playerMode) galleryModel.showVideos = Audio.showVideos
        }
    }
    Connections {
        target: galleryModel
        function onFilterChanged() { pane._pushQueue() }
        function onSortChanged()   { pane._pushQueue() }
        function onCountChanged()  { pane._pushQueue() }
    }

    readonly property bool galleryActive: paneStack.depth === 1
    readonly property bool _keysLive: pane.galleryActive && pane.paneFocused
    readonly property bool playerPageActive: paneStack.depth > 1
                                             && pane._playerPage !== null
                                             && paneStack.currentItem === pane._playerPage
    readonly property bool _audioKeysLive: pane.paneFocused && pane.playerMode
                                           && pane.playerMine
                                           && (pane.galleryActive || pane.playerPageActive)

    function focusGallery()     { if (paneStack.currentItem) paneStack.currentItem.forceActiveFocus() }
    function openFolderDialog() { folderDialog.open() }
    function popupFileMenu(anchor)   { fileMenu.popup(anchor, 0, anchor.height + 3) }
    function popupFolderMenu(anchor) { bookmarksMenu.popup(anchor, 0, anchor.height + 3) }
    function fileMenuOpen()   { return fileMenu.opened }
    function folderMenuOpen() { return bookmarksMenu.opened }
    function closeMenus()     { fileMenu.close(); bookmarksMenu.close() }
    function _folderName(path) {
        if (!path) return ""
        var s = String(path)
        var cut = Math.max(s.lastIndexOf("/"), s.lastIndexOf("\\"))
        return cut >= 0 ? s.slice(cut + 1) : s
    }

    HoverHandler {
        onHoveredChanged: if (hovered) pane.focusRequested()
    }

    function _normalizedFolderPath(p) {
        if (!p) return ""
        var s = String(p)
        while (s.length > 1 && (s.endsWith("/") || s.endsWith("\\"))) s = s.slice(0, -1)
        return s
    }
    function _bookmarkPrefillPath() {
        var cur = pane._normalizedFolderPath(PaneCtl.currentFolder)
        if (cur.length === 0) return ""
        var list = App.savedFolders
        for (var i = 0; i < list.length; i++)
            if (pane._normalizedFolderPath(list[i].path) === cur) return ""
        return cur
    }

    Rectangle {
        id: paneMenuStrip
        anchors { left: parent.left; right: parent.right; top: parent.top }
        visible: pane.splitActive && pane.galleryActive && !pane.immersive
        height: visible ? 28 : 0
        color: App.themeMenuBarBg
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: App.themeBorder }
        Rectangle {
            visible: pane.splitActive && pane.paneFocused
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 2
            color: App.themeAccent
        }

        component MenuBtn: Rectangle {
            property string label: ""
            property var menu: null
            width: mbLbl.implicitWidth + 20; height: 22; radius: 5
            anchors.verticalCenter: parent.verticalCenter
            color: (mbHover.hovered || (menu && menu.opened)) ? App.themeCard : "transparent"
            Text { id: mbLbl; anchors.centerIn: parent; text: parent.label
                   color: App.themeTextPrimary; font.pixelSize: 12 }
            HoverHandler { id: mbHover }
            TapHandler {
                onTapped: {
                    pane.focusRequested()
                    if (menu.opened) menu.close()
                    else menu.popup(parent, 0, parent.height + 3)
                }
            }
        }

        ScrollableBar {
            anchors { left: parent.left; leftMargin: 6; right: folderLbl.left; rightMargin: 8
                      verticalCenter: parent.verticalCenter }
            height: parent.height
            spacing: 2
            MenuBtn { label: App.menuFileText;      menu: fileMenu }
            MenuBtn { label: App.menuBookmarksText; menu: bookmarksMenu }
        }

        // Die Leiste ist der GRIFF der Hälfte: an die andere Seite gezogen tauscht sie die beiden. Der Fänger liegt
        // hinter den Menüknöpfen, zieht also nur dort, wo nichts anderes zuständig ist.
        DragHandler {
            id: barDrag
            target: null
            yAxis.enabled: false
            cursorShape: Qt.ClosedHandCursor
            enabled: pane.splitActive
            onCentroidChanged: if (active) pane.barDragMoved(centroid.scenePosition.x)
            onActiveChanged: if (!active) pane.barDragReleased(centroid.scenePosition.x)
        }

        Text {
            id: folderLbl
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            width: Math.max(0, parent.width * 0.4)
            horizontalAlignment: Text.AlignRight
            text: pane._folderName(PaneCtl.currentFolder)
            color: App.themeTextMuted
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }

        ThemedMenu {
            id: fileMenu
            MenuItem { text: App.menuOpenFolderText; onTriggered: folderDialog.open() }
            MenuItem {
                text: App.menuRefreshText
                enabled: PaneCtl.currentFolder.length > 0
                onTriggered: PaneCtl.refreshCurrentFolder()
            }
            MenuSeparator {}
            MenuItem {
                text: pane.showClose ? App.uiText(App.language, "MenuClosePane")
                                     : App.menuQuitText
                onTriggered: {
                    if (pane.showClose) pane.closeRequested()
                    else                Qt.quit()
                }
            }
        }

    ThemedMenu {
        id: bookmarksMenu
        objectName: "bookmarksMenu"      // Griff für tests/bench

        MenuItem {
            text: App.bookmarkAddText
            onTriggered: pane.openBookmarkAdd(pane._bookmarkPrefillPath())
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
            //  Höhe -> zwischen „Ordner hinzufügen" und dem ersten Lesezeichen
            //  klaffte eine leere Zeile. Höhe explizit auf 0 klemmen.
            height: visible ? implicitHeight : 0
        }

        Component {
            id: bookmarkItemComponent
            MenuItem {
                property string bookmarkPath: ""
                onTriggered: PaneCtl.openFolder(bookmarkPath)
            }
        }

        // BEWUSST KEIN `MenuItem`: ein Menüeintrag schließt beim Auslösen das Menü - Auf- und Zuklappen soll es aber
        // offen lassen. Ein gewöhnliches `Item` im Menü-ListView tut genau das.
        Component {
            id: bookmarkGroupComponent
            Item {
                id: groupRow
                property string groupPath: ""
                property string groupName: ""
                property bool   collapsed: false
                property int    itemCount: 0
                property int    depth: 0

                implicitWidth: Math.max(200, groupLabel.implicitWidth + 64
                                             + groupRow.depth * 14)
                implicitHeight: 28
                width: parent ? parent.width : implicitWidth

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    radius: 4
                    color: groupHover.hovered
                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.14)
                           : "transparent"
                }
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10 + groupRow.depth * 14
                    anchors.rightMargin: 12
                    spacing: 6
                    DrawnIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: groupRow.collapsed ? "chevron-right" : "chevron-down"
                        size: 12
                        color: App.themeTextMuted
                    }
                    Text {
                        id: groupLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: groupRow.groupName
                        color: App.themeTextPrimary
                        font.pixelSize: 13; font.bold: true
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: groupRow.itemCount
                        color: App.themeTextMuted
                        font.pixelSize: 11
                    }
                }
                HoverHandler { id: groupHover }
                TapHandler {
                    onTapped: App.setBookmarkGroupCollapsed(groupRow.groupPath,
                                                            !groupRow.collapsed)
                }
            }
        }

        property var dynamicBookmarkItems: []

        // `App.bookmarkTree` ist eine FLACHE Zeilenliste in Anzeigereihenfolge mit `depth` als Einrücktiefe; Zeilen
        // unter einer zugeklappten Gruppe tragen `hidden`. C++ hat die Ebenen schon abgelaufen.
        function rebuildBookmarks() {
            for (var i = 0; i < dynamicBookmarkItems.length; i++)
                bookmarksMenu.removeItem(dynamicBookmarkItems[i])
            dynamicBookmarkItems = []

            var rows = App.bookmarkTree
            for (var r = 0; r < rows.length; r++) {
                var row = rows[r]
                if (row.hidden) continue
                if (row.kind === "group") {
                    var head = bookmarkGroupComponent.createObject(bookmarksMenu, {
                        groupPath: row.group,
                        groupName: row.name,
                        collapsed: row.collapsed,
                        itemCount: row.count,
                        depth:     row.depth
                    })
                    bookmarksMenu.addItem(head)
                    dynamicBookmarkItems.push(head)
                } else {
                    var item = bookmarkItemComponent.createObject(bookmarksMenu, {
                        text:         row.name,
                        bookmarkPath: row.path,
                        leftPadding:  10 + row.depth * 14
                    })
                    bookmarksMenu.addItem(item)
                    dynamicBookmarkItems.push(item)
                }
            }
        }

        Component.onCompleted: rebuildBookmarks()

        Connections {
            target: App
            function onSavedFoldersChanged() { bookmarksMenu.rebuildBookmarks() }
        }
    }
    }

    StackView {
        id: paneStack
        anchors { left: parent.left; right: parent.right
                  top: paneMenuStrip.bottom
                  bottom: playerBar.visible ? playerBar.top : parent.bottom }
        initialItem: galleryComponent

        readonly property bool _txSlide: App.pageTransition === "slide"
        readonly property int  _txDur:   240

        pushEnter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: paneStack._txSlide ? 1 : 0;             to: 1; duration: paneStack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "x";       from: paneStack._txSlide ? paneStack.width : 0; to: 0; duration: paneStack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale";   from: paneStack._txSlide ? 1 : 0.97;           to: 1; duration: paneStack._txDur; easing.type: Easing.OutCubic }
            }
        }
        pushExit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x";       from: 0; to: paneStack._txSlide ? -paneStack.width * 0.22 : 0; duration: paneStack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "opacity"; from: 1; to: paneStack._txSlide ? 1 : 0;                        duration: paneStack._txDur; easing.type: Easing.InCubic }
            }
        }
        popEnter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x";       from: paneStack._txSlide ? -paneStack.width * 0.22 : 0; to: 0; duration: paneStack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "opacity"; from: paneStack._txSlide ? 1 : 0;                        to: 1; duration: paneStack._txDur; easing.type: Easing.OutCubic }
            }
        }
        popExit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x";       from: 0; to: paneStack._txSlide ? paneStack.width : 0; duration: paneStack._txDur; easing.type: Easing.InCubic }
                NumberAnimation { property: "opacity"; from: 1; to: paneStack._txSlide ? 1 : 0;                duration: paneStack._txDur; easing.type: Easing.InCubic }
                NumberAnimation { property: "scale";   from: 1; to: paneStack._txSlide ? 1 : 0.97;             duration: paneStack._txDur; easing.type: Easing.InCubic }
            }
        }
    }

    Component {
        id: galleryComponent
        Item {
            id: galleryPage

            StackView.onActivated: galleryView.forceActiveFocus()
            Component.onCompleted: if (StackView.status === StackView.Active)
                                       galleryView.forceActiveFocus()

            Shortcut {
                sequence: "Alt+S"; enabled: pane._keysLive
                onActivated: PaneCtl.optionsVisible = !PaneCtl.optionsVisible
            }
            Shortcut {
                sequence: "R"; enabled: pane._keysLive
                onActivated: PaneCtl.refreshCurrentFolder()
            }
            Shortcut {
                sequence: "F5"; enabled: pane._keysLive
                onActivated: PaneCtl.refreshCurrentFolder()
            }
            Shortcut {
                sequence: "Ctrl+O"; enabled: pane._keysLive
                onActivated: folderDialog.open()
            }
            Shortcut {
                sequence: "Alt+Left"
                enabled: pane._keysLive && PaneCtl.canNavigateBack
                onActivated: PaneCtl.navigateBack()
            }
            Shortcut {
                sequence: "F"; enabled: pane._keysLive
                onActivated: {
                    if (galleryView._editableTextFocused()) return
                    pane.immersiveToggleRequested()
                }
            }
            Shortcut {
                sequence: "Ctrl+F"; enabled: pane._keysLive
                onActivated: filterBar.focusSearch()
            }
            Shortcut {
                sequence: "Alt+A"
                enabled: pane.paneFocused
                onActivated: pane.galleryActive || pane.playerPageActive
                             ? pane.togglePlayerMode()
                             : pane.enterPlayerFromViewer()
            }
            Shortcut {
                sequence: "Space"
                enabled: pane._audioKeysLive
                onActivated: if (!galleryView._editableTextFocused()) Audio.togglePlay()
            }
            Shortcut {
                sequence: "Right"
                enabled: pane._audioKeysLive && Audio.hasTrack
                onActivated: if (!galleryView._editableTextFocused()) Audio.next()
            }
            Shortcut {
                sequence: "Left"
                enabled: pane._audioKeysLive && Audio.hasTrack
                onActivated: if (!galleryView._editableTextFocused()) Audio.previous()
            }
            // Undo/Redo für DATEI-Vorgänge, nur auf der Galerie-Seite und nicht während jemand in einem Feld tippt.
            // Tastenfolgen AUSGESCHRIEBEN statt über StandardKey: `QKeySequence::keyBindings` geht über das
            // Plattform-Thema - offscreen vier Redo-Folgen, unter Wayland nur Ctrl+Shift+Z, Strg+Y an nichts gebunden.
            Shortcut {
                sequences: [ "Ctrl+Z" ]; enabled: pane._keysLive
                onActivated: {
                    if (galleryView._editableTextFocused()) return
                    const name = mediaModel.undoFileOpName()
                    const n = mediaModel.undoFileOpCount()
                    pane.statusRequested(mediaModel.undoFileOp()
                        ? App.uiText(App.language, "FileUndoRestored") + name
                          + (n > 1 ? App.uiText(App.language, "FileOpAndMore")
                                        .replace("%1", n - 1) : "")
                        : App.uiText(App.language, "FileUndoNothing"))
                }
            }
            Shortcut {
                sequences: [ "Ctrl+Shift+Z", "Ctrl+Y" ]; enabled: pane._keysLive
                onActivated: {
                    if (galleryView._editableTextFocused()) return
                    const name = mediaModel.redoFileOpName()
                    const n = mediaModel.redoFileOpCount()
                    pane.statusRequested(mediaModel.redoFileOp()
                        ? App.uiText(App.language, "FileRedoDeleted") + name
                          + (n > 1 ? App.uiText(App.language, "FileOpAndMore")
                                        .replace("%1", n - 1) : "")
                        : App.uiText(App.language, "FileRedoNothing"))
                }
            }
            // Strg+A wählt, was der Filter durchlässt - man kann nur wählen, was man sieht. Esc gehört NICHT hierher,
            // sondern an `Keys.onEscapePressed` der GalleryView: Kürzel werden VOR den Tastenereignissen ausgewertet,
            // ein Shortcut nähme einem offenen Dialog seine Escape-Taste. Beim Tippen abgeschaltet - ein aktives Kürzel VERBRAUCHT die Taste.
            Shortcut {
                sequence: "Ctrl+A"
                enabled: pane._keysLive && !galleryView._editableTextFocused()
                onActivated: galleryView.selectAll()
            }
            Shortcut {
                sequence: "Ctrl+C"
                enabled: pane._keysLive && !galleryView._editableTextFocused()
                onActivated: {
                    if (mediaModel.selectionCount === 0) return
                    const list = galleryModel.selectedPaths(true)
                    const n = App.copyFilesToClipboard(list)
                    if (n > 0)
                        pane.statusRequested(App.uiText(App.language, "SelCopied")
                                             .replace("%1", n))
                }
            }
            Shortcut {
                sequence: "Ctrl+V"
                enabled: pane._keysLive && !galleryView._editableTextFocused()
                onActivated: {
                    const urls = App.clipboardFileUrls()
                    if (!urls || urls.length === 0) {
                        pane.statusRequested(App.uiText(App.language, "SelPasteEmpty"))
                        return
                    }
                    App.handleDroppedUrls(urls, PaneCtl.currentFolder)
                    pane.statusRequested(App.uiText(App.language, "SelPasted")
                                         .replace("%1", urls.length))
                }
            }
            Connections {
                target: mediaModel
                function onSelectionChanged() {
                    if (mediaModel.selectionCount > 1)
                        pane.statusRequested(App.uiText(App.language, "SelCountStatus")
                                             .replace("%1", mediaModel.selectionCount))
                }
            }
            Shortcut {
                sequence: "B"; enabled: pane._keysLive
                onActivated: {
                    if (galleryView.covered) {
                        galleryView.covered = false
                        PaneCtl.refreshCurrentFolder()
                    } else {
                        galleryView.covered = true
                    }
                }
            }
            // Nur eindeutige Sequenzen - `StandardKey.ZoomIn` liefert zusätzlich "Ctrl++" und macht den Shortcut
            // mehrdeutig, er feuert dann gar nicht. Im Listen-Modus gilt die Zeilenhöhe: sie startet bei 46 px, nicht 200.
            Shortcut {
                sequences: ["Ctrl++", "Ctrl+="]
                enabled: pane._keysLive
                onActivated: pane.listMode ? App.zoomInList(4) : App.zoomIn(16)
            }
            Shortcut {
                sequence: "Ctrl+-"
                enabled: pane._keysLive
                onActivated: pane.listMode ? App.zoomOutList(4) : App.zoomOut(16)
            }

            FilterBar {
                id: filterBar
                tagsCtl: PaneCtl.tags
                audioOnly: pane.playerMode
                anchors { left: parent.left; right: parent.right; top: parent.top }
                onEnterAddToTagMode: function(tag) { galleryView.enterAddToTagMode(tag) }
                tagPanelVisible: pane.tagsSectionOn
                categoryPanelVisible: pane.catsSectionOn
                onTagPanelToggled:      pane.tagsSectionOn = !pane.tagsSectionOn
                onCategoryPanelToggled: pane.catsSectionOn = !pane.catsSectionOn
                onNewFolderRequested: function(folder) {
                    galleryView.promptNewFolder(folder)
                }
                onExtractPagesRequested: function(folder) {
                    pane.extractRequested(folder)
                }
            }

            GalleryView {
                id: galleryView
                tagsCtl: PaneCtl.tags
                // Zwei getrennte Schalter, weil es zwei Fragen sind: im Player-Modus entscheidet `Audio.listLayout`
                // (Vorgabe AN, so sieht man den Modus), sonst `App.galleryListLayout`. Der Filter bleibt unberührt -
                // `onlyPlayable` hängt allein am Player-Modus.
                listMode: pane.listMode
                optionsVisible: PaneCtl.optionsVisible
                anchors {
                    left: parent.left
                    right: catPanelLoader.active ? catPanelLoader.left : parent.right
                    top: filterBar.bottom
                    bottom: addBanner.visible ? addBanner.top
                            : (modeBanner.visible ? modeBanner.top : parent.bottom)
                }
                onActivated: function(filePath) {
                    if (pane.pendingAddFile) { pane.addFileFromGallery(filePath); return }
                    if (pane.playerMode) {
                        pane.playHere(filePath)
                        pane.openPlayerView()
                        return
                    }
                    pane.pushFullscreen(filePath)
                }
                onFileClicked: function(filePath) {
                    if (pane.playerMode && !pane.pendingAddFile) pane.playHere(filePath)
                }
                onFolderOpenRequested: function(folderPath) {
                    if (pane.pendingAddFile) return
                    PaneCtl.openSubfolder(folderPath)
                }
                onCreateFileRequested: function(folderPath) {
                    filterBar.openCreateFor(folderPath)
                }
                onExtractPagesRequested: function(folderPath) {
                    pane.extractRequested(folderPath)
                }
                onFolderDropRequested: function(sourcePaths, folderPath) {
                    pane.folderDropRequested(sourcePaths, folderPath)
                }
                onExternalDropRequested: function(urls, folderPath) {
                    pane.externalDropRequested(urls, folderPath)
                }
                onStatusRequested: function(text) { pane.statusRequested(text) }
                onAudioExtractRequested: function(filePath) {
                    Audio.extractAudio(filePath)
                }
            }

            Connections {
                target: Audio
                function onExtractFinished(ok, source, target) {
                    if (!ok) return
                    PaneCtl.adoptSiblingFile(source, target, Audio.extractInheritTags)
                }
            }

            // Panel erst bauen, wenn es gebraucht wird: beide Abschnitte starten ausgeblendet, es war also bei jedem Start
            // da, ohne je sichtbar zu sein (165 der 218 ms Aufbauzeit). Der ZUSTAND liegt deshalb hier in der Hälfte.
            Loader {
                id: catPanelLoader
                active: pane.tagsSectionOn || pane.catsSectionOn
                visible: active
                width: Math.min(300, galleryPage.width * 0.45)
                anchors { right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
                source: active ? "qrc:/qml/tags/TagCategoryPanel.qml" : ""
                onLoaded: {
                    item.tagsCtl = PaneCtl.tags
                    item.folderSource = PaneCtl
                }
                Binding {
                    target: catPanelLoader.item; when: catPanelLoader.item !== null
                    property: "showTagsSection"; value: pane.tagsSectionOn
                }
                Binding {
                    target: catPanelLoader.item; when: catPanelLoader.item !== null
                    property: "showCategoriesSection"; value: pane.catsSectionOn
                }
                Connections {
                    target: catPanelLoader.item
                    ignoreUnknownSignals: true
                    function onEnterAddToTagMode(tag) { galleryView.enterAddToTagMode(tag) }
                    function onEnterGroupMode(tag)    { galleryView.enterGroupMode(tag) }
                }
            }

            Rectangle {
                id: addBanner
                visible: pane.pendingAddFile
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
                        onClicked: pane.cancelAddFile()
                    }
                }
            }

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

    // Geteilte Ansicht: bis zu vier Dateien gleichzeitig (2 nebeneinander, 3 als 2 oben + 1 unten, 4 als 2x2).
    // Jede Kachel ist ein eigenständiger FullscreenViewer mit eigener Navigation; Zurück/Esc schliesst genau
    // diese Datei und gibt ihren RAM sofort frei. Bei zwei Hälften zeigt jede höchstens zwei.
    readonly property int maxOpenFiles: pane.splitActive ? 2 : 4
    ListModel { id: openFilesModel }   // Rolle: path (aktueller Pfad der Kachel)

    property real splitV: 0.5
    property real splitH: 0.5
    readonly property real _splitMin: 0.15
    readonly property real _splitMax: 0.85

    // Docking-Layout, session-lokal und bewusst nicht persistiert. `slotOrder` bildet Layout-Slot -> Modell-
    // Index ab; Ziehen, Schliessen und Hinzufügen ordnen NUR diese Liste um - das Modell bleibt unangetastet,
    // kein Viewer wird dadurch zerstört oder neu geladen.
    property string layout2: "cols"
    property string layout3: "bottom"
    property var    slotOrder: []

    // Reine Slot-Geometrie eines Layouts, unabhängig vom Modell. Bei 3 Kacheln sind 0/1 die kleinen
    // (Lesereihenfolge) und 2 die große; bei 4 läuft es zeilenweise von oben-links.
    function _slotRect(slot, count, l2, l3, W, H) {
        var g = 2
        var rV = Math.max(_splitMin, Math.min(pane.splitV, _splitMax))
        var rH = Math.max(_splitMin, Math.min(pane.splitH, _splitMax))
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

    function _paneAdded() {
        var n = openFilesModel.count
        var S = (slotOrder.length === n - 1) ? slotOrder.slice() : _identity(n - 1)
        var ni = n - 1
        if (n <= 1) { slotOrder = [0]; return }
        if (n === 2) { slotOrder = [S[0], ni]; return }
        if (n === 3) {
            layout3 = (layout2 === "rows") ? "right" : "bottom"
            slotOrder = [S[0], S[1], ni]
            return
        }
        if (layout3 === "top")        slotOrder = [S[2], ni,   S[0], S[1]]
        else if (layout3 === "left")  slotOrder = [S[2], S[0], ni,   S[1]]
        else if (layout3 === "right") slotOrder = [S[0], S[2], S[1], ni]
        else /* bottom */             slotOrder = [S[0], S[1], S[2], ni]
    }

    function _slotsAfterRemove(ri) {
        var n = openFilesModel.count
        var S = (slotOrder.length === n) ? slotOrder.slice() : _identity(n)
        var rs = S.indexOf(ri)
        var R = []
        if (n === 2) {
            R = [S[rs === 0 ? 1 : 0]]
        } else if (n === 3) {
            if (rs === 2) {
                layout2 = (layout3 === "left" || layout3 === "right") ? "rows" : "cols"
                R = [S[0], S[1]]
            } else {
                var small = S[rs === 0 ? 1 : 0]
                var big   = S[2]
                if (layout3 === "bottom")    { layout2 = "rows"; R = [small, big] }
                else if (layout3 === "top")  { layout2 = "rows"; R = [big, small] }
                else if (layout3 === "left") { layout2 = "cols"; R = [big, small] }
                else /* right */             { layout2 = "cols"; R = [small, big] }
            }
        } else if (n === 4) {
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

    // Getroffen wird ein sichtbarer INDIKATOR (Randzonen bei 2/3 Dateien, Ecken bei 3); außerhalb gilt die Kachel
    // unter dem Zeiger als Tausch-Ziel. Bei vier Dateien gibt es nur den Positionstausch, daher keine Indikatoren.
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

    function _applySwap(a, b) {
        if (a === b || a < 0 || b < 0) return
        var n = openFilesModel.count
        var S = (slotOrder.length === n) ? slotOrder.slice() : _identity(n)
        var sa = S.indexOf(a), sb = S.indexOf(b)
        if (sa < 0 || sb < 0) return
        S[sa] = b; S[sb] = a
        slotOrder = S
    }

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

    function closeFilePane(i) {
        if (i < 0 || i >= openFilesModel.count) return
        var R = _slotsAfterRemove(i)
        openFilesModel.remove(i)              // gibt den zugehörigen Viewer sofort frei
        slotOrder = R
        if (openFilesModel.count === 0)
            popFullscreen()
    }

    // Die Split-Seite wird als PERSISTENTES Item genau einmal erzeugt und dieses Item gepusht, nicht die
    // Component: StackView zerstört beim Pop nur selbst erzeugte Items. So überleben alle Viewer den
    // "+"-Rücksprung in die Galerie - vorher baute der erneute Push alles frisch auf Seite 1.
    property Item _splitPageItem: null
    function _splitPage() {
        if (!_splitPageItem)
            _splitPageItem = fullscreenComponent.createObject(pane)
        return _splitPageItem
    }
    property bool pendingAddFile: false
    function requestAddFile() {
        if (openFilesModel.count >= pane.maxOpenFiles) {
            pane.statusRequested(App.uiText(App.language, "SplitMaxReached"))
            
            return
        }
        pane.pendingAddFile = true
        if (paneStack.depth > 1)
            paneStack.pop()                 // NICHT leeren - Kacheln bleiben im Modell UND am Leben
    }
    function addFileFromGallery(p) {
        pane.pendingAddFile = false
        if (p !== undefined && p.length > 0
                && indexOfOpenFile(p) < 0
                && openFilesModel.count < pane.maxOpenFiles) {
            openFilesModel.append({ path: p })
            _paneAdded()                      // Slot-Zuordnung fortschreiben (Layout bleibt sinngemäß)
        }
        if (paneStack.depth < 2)
            paneStack.push(_splitPage())          // geteilte Ansicht wiederherstellen
    }
    function cancelAddFile() {
        pane.pendingAddFile = false
        if (paneStack.depth < 2 && openFilesModel.count > 0)
            paneStack.push(_splitPage())
    }

    Component {
        id: fullscreenComponent

        Item {
            id: splitPage

            // Lade-Gating: die schwere Medienlast erst NACH dem StackView-Übergang anstoßen - die Viewer sitzen in Loadern
            // unter der Seite. Nur bei Active setzen: das persistente Item existiert schon vor dem ersten Push.
            property bool pageReady: false
            function _checkReady() {
                if (StackView.status === StackView.Active)
                    pageReady = true
            }
            // Fokus bei JEDEM Sichtbarwerden setzen, nicht nur beim ersten: die Seite ist persistent, der Kachel-Loader
            // lädt schon beim Befüllen des Modells und damit VOR dem StackView-Push, der den Fokus danach wieder
            // wegnimmt. Ergebnis war eine Kachel, die richtig aussah, aber auf keine Taste mehr reagierte.
            function _focusActivePane() {
                const l = paneRepeater.itemAt(splitPage.activePaneIndex)
                if (l && l.item) l.item.forceActiveFocus()
            }
            StackView.onStatusChanged: {
                _checkReady()
                if (StackView.status === StackView.Active) _focusActivePane()
            }
            onActivePaneIndexChanged: _focusActivePane()

            Rectangle { anchors.fill: parent; color: "#0a0a0a" }

            readonly property int paneCount: openFilesModel.count

            property int activePaneIndex: 0
            onPaneCountChanged: activePaneIndex =
                Math.max(0, Math.min(activePaneIndex, paneCount - 1))

            Repeater {
                id: paneRepeater
                model: openFilesModel
                delegate: Loader {
                    id: paneLoader
                    required property int index
                    required property string path

                    readonly property var _r: pane.paneRect(index, splitPage.paneCount,
                                                             splitPage.width, splitPage.height)
                    x: _r.x; y: _r.y
                    width: _r.w; height: _r.h

                    active: splitPage.pageReady

                    sourceComponent: FullscreenViewer {
                        id: paneViewer
                        startPath: paneLoader.path
                        splitActive: splitPage.paneCount > 1
                        canAddMore:  splitPage.paneCount < pane.maxOpenFiles
                        paneActive:  paneLoader.index === splitPage.activePaneIndex
                        onPaneActivated: splitPage.activePaneIndex = paneLoader.index
                        immersive: pane.immersive
                        optionsVisible: PaneCtl.optionsVisible
                        onImmersiveToggleRequested: pane.immersiveToggleRequested()
                        onBackRequested:    pane.closeFilePane(paneLoader.index)
                        onAddFileRequested: pane.requestAddFile()
                        onPaneDragStarted:  pane.beginPaneDrag(paneLoader.index)
                        onPaneDragMoved: (x, y) => {
                            var p = paneLoader.mapToItem(splitPage, x, y)
                            pane.updatePaneDrag(p.x, p.y)
                        }
                        onPaneDragEnded: (x, y) => {
                            var p = paneLoader.mapToItem(splitPage, x, y)
                            pane.endPaneDrag(p.x, p.y)
                        }
                        onPaneDragCanceled: pane.cancelPaneDrag()

                        Binding {
                            target: pane
                            property: "activeFilePath"
                            value: paneViewer.path
                            when: paneViewer.paneActive
                            restoreMode: Binding.RestoreNone
                        }
                        Binding {
                            target: pane
                            property: "activeViewer"
                            value: paneViewer
                            when: paneViewer.paneActive
                            restoreMode: Binding.RestoreNone
                        }
                    }
                    onLoaded: {
                        item.forceActiveFocus()
                        splitPage.activePaneIndex = paneLoader.index
                    }

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

            // Sichtbarkeit und Ausdehnung folgen der Layout-Variante; bei den 3er-Layouts läuft der Trenner nur durch die
            // Hälfte mit den kleinen Kacheln. Ziehen setzt `pane.splitV`/`splitH` geklemmt, `paneRect` folgt.
            readonly property real _gap: 2
            readonly property real _vX: (splitPage.width  - _gap) * Math.max(pane._splitMin, Math.min(pane.splitV, pane._splitMax))
            readonly property real _hY: (splitPage.height - _gap) * Math.max(pane._splitMin, Math.min(pane.splitH, pane._splitMax))

            Rectangle {                                       // vertikaler Trenner
                z: 50
                visible: splitPage.paneCount >= 4
                         || (splitPage.paneCount === 2 && pane.layout2 === "cols")
                         || splitPage.paneCount === 3
                x: splitPage._vX - 3
                y: (splitPage.paneCount === 3 && pane.layout3 === "top")
                   ? splitPage._hY : 0
                width: 8
                height: splitPage.paneCount === 3
                        ? (pane.layout3 === "bottom" ? splitPage._hY
                           : pane.layout3 === "top"  ? splitPage.height - splitPage._hY
                           : splitPage.height)
                        : splitPage.height
                color: vDivMA.pressed ? App.themeAccent : "transparent"
                MouseArea {
                    id: vDivMA
                    anchors.fill: parent
                    cursorShape: Qt.SplitHCursor
                    onPositionChanged: (m) => {
                        var p = mapToItem(splitPage, m.x, m.y)
                        pane.splitV = Math.max(pane._splitMin,
                            Math.min(p.x / Math.max(1, splitPage.width - splitPage._gap), pane._splitMax))
                    }
                }
            }
            Rectangle {                                       // horizontaler Trenner
                z: 50
                visible: splitPage.paneCount >= 4
                         || (splitPage.paneCount === 2 && pane.layout2 === "rows")
                         || splitPage.paneCount === 3
                x: (splitPage.paneCount === 3 && pane.layout3 === "left")
                   ? splitPage._vX : 0
                y: splitPage._hY - 3
                width: splitPage.paneCount === 3
                       ? (pane.layout3 === "right" ? splitPage._vX
                          : pane.layout3 === "left" ? splitPage.width - splitPage._vX
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
                        pane.splitH = Math.max(pane._splitMin,
                            Math.min(p.y / Math.max(1, splitPage.height - splitPage._gap), pane._splitMax))
                    }
                }
            }

            Item {
                id: dockOverlay
                anchors.fill: parent
                visible: pane.dragActive
                z: 100

                Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.25) }

                Rectangle {
                    readonly property var _pr: pane.dragActive
                        ? pane._zonePreviewRect(pane.dragZone, dockOverlay.width, dockOverlay.height)
                        : ({ x: 0, y: 0, w: 0, h: 0 })
                    visible: pane.dragActive && pane.dragZone.kind !== "none"
                    x: _pr.x; y: _pr.y
                    width: _pr.w; height: _pr.h
                    radius: 4
                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                    border.color: App.themeAccent
                    border.width: 2
                }

                Repeater {
                    model: pane.dragActive
                           ? pane._zoneIndicators(splitPage.paneCount,
                                                   dockOverlay.width, dockOverlay.height)
                           : []
                    delegate: Rectangle {
                        id: zoneInd
                        required property var modelData
                        readonly property bool hot:
                            pane.dragZone.kind === modelData.kind
                            && (modelData.kind === "edge"
                                ? pane.dragZone.side === modelData.side
                                : pane.dragZone.corner === modelData.corner)
                        x: modelData.x; y: modelData.y
                        width: modelData.w; height: modelData.h
                        radius: 8
                        color: hot ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.85)
                                   : Qt.rgba(0.08, 0.10, 0.11, 0.85)
                        border.color: hot ? App.themeAccent : Qt.rgba(1, 1, 1, 0.35)
                        border.width: 1

                        DrawnIcon {
                            anchors.centerIn: parent
                            visible: zoneInd.modelData.kind === "edge"
                            size: 20
                            color: "#e8efed"
                            name: zoneInd.modelData.kind === "edge"
                                  ? ({ left: "arrow-left", right: "arrow-right",
                                       top: "arrow-up", bottom: "arrow-down" })[zoneInd.modelData.side]
                                  : ""
                        }
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

                Rectangle {
                    visible: pane.dragActive && pane.dragIndex >= 0
                    x: pane.dragX + 14; y: pane.dragY + 10
                    width: ghostLabel.implicitWidth + 20; height: 28; radius: 6
                    color: Qt.rgba(0.08, 0.10, 0.11, 0.92)
                    border.color: App.themeAccent; border.width: 1
                    Text {
                        id: ghostLabel
                        anchors.centerIn: parent
                        color: "#e8efed"; font.pixelSize: 12; font.bold: true
                        text: (pane.dragIndex >= 0 && pane.dragIndex < openFilesModel.count)
                              ? pane._folderName(openFilesModel.get(pane.dragIndex).path) : ""
                    }
                }
            }
        }
    }

    function pushFullscreen(filePath) {
        var p = filePath !== undefined ? filePath : ""
        if (p.length === 0) return
        openFilesModel.clear()
        openFilesModel.append({ path: p })
        pane.slotOrder = [0]
        if (paneStack.depth < 2)
            paneStack.push(_splitPage())
    }
    function popFullscreen() {
        openFilesModel.clear()          // Verlassen schließt alle Dateien (RAM frei)
        pane.slotOrder = []
        if (paneStack.depth > 1)
            paneStack.pop()                 // die leere Split-Seite überlebt (persistentes Item)
    }

    // LAZY: die Leiste entstand beim Bau JEDER Galerie-Hälfte, obwohl sie nur im Player-Modus sichtbar ist -
    // gemessen ~9 ms und ~4 MB RSS. Entladen kostet nichts, `AudioPlayerBar` hält ausser `expandRequested`
    // keinen eigenen Zustand. Über eine URL geladen, damit die Datei erst zur Laufzeit übersetzt wird.
    Loader {
        id: playerBar
        active: pane.playerMode && pane.playerMine && Audio.hasTrack
                && !pane.playerPageActive
        // `visible` MUSS an `active` hängen: der `paneStack` ankert sein unteres Ende an `playerBar.visible`, und ein
        // Loader ist von sich aus sichtbar - die Bedingung wäre sonst immer wahr.
        visible: playerBar.active
        source: "qrc:/qml/gallery/AudioPlayerBar.qml"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        Connections {
            target: playerBar.item
            function onExpandRequested() { pane.openPlayerView() }
        }
    }

    Component {
        id: playerViewComponent
        AudioPlayerView {
            onBackRequested: {
                PaneCtl.playerViewOpen = false
                if (paneStack.depth > 1) paneStack.pop()
            }
        }
    }

    Connections {
        target: mediaModel
        function onFileDateNotWritten(fileName) {
            pane.statusRequested(App.uiText(App.language, "DateNotWrittenToFile")
                                 .arg(fileName))
        }
    }

    FileChooser {
        id: folderDialog
        title: App.menuOpenFolderText
        fileMode: FileChooser.Directory
        onAccepted: PaneCtl.openFolderUrl(folderDialog.selectedFolder)
    }
    // LAZY: der Dialog entstand beim Bau JEDER Galerie-Hälfte, obwohl man ihn erst bei "Ordner hinzufügen" braucht
    // - und er bringt einen vollständigen FileChooser mit. Über eine URL geladen, sonst liefe das Übersetzen beim Start mit.
    Loader {
        id: bookmarkEditLoader
        active: false
        source: "qrc:/qml/settings/BookmarkEditDialog.qml"
    }
    function openBookmarkAdd(prefillPath) {
        bookmarkEditLoader.active = true
        if (bookmarkEditLoader.item) bookmarkEditLoader.item.openAdd(prefillPath)
    }
}
