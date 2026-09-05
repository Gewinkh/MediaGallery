import QtQuick
import QtQuick.Controls
import QtQuick.Window
import MediaGallery 1.0

// Model/View-Galerie: statt eines Widgets je Datei recycelt ein GridView seine Delegates und hält nur
// sichtbare Kacheln - flacher RAM auch bei 10-50k Medien. Jede Kachel fordert ihr Thumbnail einmal an und
// BRICHT die Anforderung der abgegebenen Datei ab; `cacheBuffer` ~ 2 Zeilen glättet schnelles Scrollen.
Rectangle {
    id: root
    color: App.themeBackground

    signal activated(string filePath)
    signal fileClicked(string filePath)
    signal folderOpenRequested(string folderPath)
    signal audioExtractRequested(string filePath)
    signal createFileRequested(string folderPath)
    signal extractPagesRequested(string folderPath)
    signal statusRequested(string text)
    signal folderDropRequested(var sourcePaths, string folderPath)
    signal externalDropRequested(var urls, string folderPath)

    // EINE Fläche mit Treffersuche statt einer je Kachel: gemessen (`bench_dnd`) ~2,5 µs je Mausbewegung und flach
    // über die Kachelzahl, gegen 17 µs bei 200 Kacheln - linear wachsend, abgeschaltet noch 10 µs.
    property string hoverFolder: ""

    function folderAtPoint(px, py) {
        const fallback = mediaModel.folder
        if (galleryModel.count === 0) return fallback
        const cx = px - grid.x
        const cy = py - grid.y + grid.contentY
        if (cx < 0 || cx > grid.width) return fallback
        const r = grid.indexAt(cx, cy)
        if (r < 0) return fallback
        const info = rowModel.rowInfo(r)
        if (!info || info.ownerFolder === undefined) return fallback
        if (info.kind === 1) return info.ownerFolder        // Kopfzeile

        const rel = cx - info.depth * root.levelInset
        const col = Math.floor(rel / root.cellW)
        if (rel >= 0 && col >= 0 && col < info.count) {
            const proxyRow = info.first + col
            if (galleryModel.mediaTypeAt(proxyRow) === 7)
                return galleryModel.filePathAt(proxyRow)
        }
        return info.ownerFolder
    }

    // Der Zustand liegt im Modell (`mediaModel.selected*`); hier steht nur, was die ANSICHTS-Reihenfolge braucht.
    // `selAnchor` ist die zuletzt mit Strg oder Umschalt angefasste Zeile - ohne ihn hätte Umschalt keinen Anfang.
    property int selAnchor: -1

    function selectFromTile(proxyRow, mods) {
        if (proxyRow < 0) return
        root.forceActiveFocus()
        const shift = (mods & Qt.ShiftModifier) !== 0
        const ctrl  = (mods & Qt.ControlModifier) !== 0
        if (shift && root.selAnchor >= 0) {
            galleryModel.selectRange(root.selAnchor, proxyRow, ctrl)
            return
        }
        const path = galleryModel.filePathAt(proxyRow)
        if (path.length === 0) return
        mediaModel.toggleSelected(path)
        root.selAnchor = proxyRow
    }

    function selectAll() {
        galleryModel.selectAllVisible()
        root.selAnchor = galleryModel.count > 0 ? 0 : -1
    }
    function clearSelection() {
        mediaModel.clearSelection()
        root.selAnchor = -1
    }

    function copyToClipboard(path) {
        const many = mediaModel.selectionCount > 1 && mediaModel.isSelected(path)
        const list = many ? galleryModel.selectedPaths(true)
                          : (path.length > 0 ? [path] : [])
        const n = App.copyFilesToClipboard(list)
        if (n > 0)
            root.statusRequested(App.uiText(App.language, "SelCopied")
                                 .replace("%1", n))
    }

    function _appOwnsFile(path) {
        if (mediaModel.ownsFile(path)) return true
        const list = App.panes
        for (var i = 0; i < list.length; ++i) {
            const m = list[i] ? list[i].mediaModel : null
            if (m && m.ownsFile(path)) return true
        }
        return false
    }

    function _parentOf(path) {
        const cut = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return cut > 0 ? path.substring(0, cut) : ""
    }

    readonly property int headerHeight: 30

    function promptNewFolder(folderPath) {
        newFolderDialog.openFor(folderPath.length > 0 ? folderPath : mediaModel.folder)
    }

    component HeaderAction: Rectangle {
        id: ha
        property string label: ""
        signal triggered()
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        width: haText.implicitWidth + 16
        height: 22
        radius: 5
        color: haHover.hovered
               ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                         App.themeTextPrimary.b, 0.14)
               : "transparent"
        border.width: 1
        border.color: App.themeBorder
        Text {
            id: haText
            anchors.centerIn: parent
            text: ha.label
            color: App.themeTextMuted
            font.pixelSize: 11
        }
        HoverHandler { id: haHover }
        TapHandler { onTapped: ha.triggered() }
    }

    // Bewusst NICHT der eingebaute Tastenfluss des GridView: der bewegt `currentIndex`, also eine Auswahl - die
    // Galerie hat keine. Gescrollt wird über dieselbe Animation wie beim Mausrad.
    focus: true
    function requestDeleteSelection() {
        const n = mediaModel.selectionCount
        if (n <= 0) return false
        selDeleteDialog.count = n
        selDeleteDialog.open()
        return true
    }

    function _editableTextFocused() {
        var f = root.Window.activeFocusItem
        if (!f) return false
        return (f.cursorPosition !== undefined) && (f.readOnly !== true)
    }
    // Scroll-Grenzen sind ORIGIN-bewusst: ein GridView verschiebt seinen `originY`, wenn das Modell Zeilen entfernt
    // oder ergänzt. Gegen 0 geklemmt schiebt die Ansicht über den Inhalt hinaus - oben leer, unten unerreichbar.
    readonly property real minContentY: grid.originY
    readonly property real maxContentY: Math.max(grid.originY,
                                                 grid.originY + grid.contentHeight - grid.height)
    function clampContentY(v) {
        return Math.max(root.minContentY, Math.min(v, root.maxContentY))
    }
    function scrollByPixels(dy) {
        if (root.maxContentY <= root.minContentY) return
        var base = gridScroll.running ? gridScroll.to : grid.contentY
        gridScroll.from = grid.contentY
        gridScroll.to = root.clampContentY(base + dy)
        gridScroll.restart()
    }
    readonly property real _keyStep: Math.max(40, root.cellH * 0.5)
    Keys.onUpPressed: function(event) {
        if (root._editableTextFocused()) return
        root.scrollByPixels(-root._keyStep); event.accepted = true
    }
    Keys.onDownPressed: function(event) {
        if (root._editableTextFocused()) return
        root.scrollByPixels(root._keyStep); event.accepted = true
    }
    Keys.onEscapePressed: function(event) {
        if (root._editableTextFocused()) return
        if (mediaModel.selectionCount === 0) return
        root.clearSelection()
        event.accepted = true
    }
    Keys.onPressed: function(event) {
        if (root._editableTextFocused()) return
        // Entf löscht die Auswahl mit derselben Rückfrage wie das Kontextmenü. BEWUSST hier und nicht als `Shortcut`:
        // gemessen (`bench_shell del`) kam Strg+A an, Entf NICHT - auch mit Fokus auf der Hälfte.
        if (event.key === Qt.Key_Delete) {
            if (root.requestDeleteSelection()) event.accepted = true
            return
        }
        if (event.key === Qt.Key_PageDown) {
            root.scrollByPixels(grid.height * 0.9); event.accepted = true
        } else if (event.key === Qt.Key_PageUp) {
            root.scrollByPixels(-grid.height * 0.9); event.accepted = true
        } else if (event.key === Qt.Key_Home) {
            root.scrollByPixels(-grid.contentHeight); event.accepted = true
        } else if (event.key === Qt.Key_End) {
            root.scrollByPixels(grid.contentHeight); event.accepted = true
        }
    }

    property bool covered: false

    Connections {
        target: App
        function onFolderOpened(path) { root.covered = false }
    }

    property int    tagMode: 0
    property string modeTag: ""

    property var tagsCtl: Tags

    function enterGroupMode(tag)     { root._startTagMode(tag, 1) }
    function enterAddToTagMode(tag)  { root._startTagMode(tag, 2) }
    function exitModes() {
        if (root.tagMode !== 0) root.tagsCtl.endUndoGroup()
        modeTag = ""; tagMode = 0
    }
    function _startTagMode(tag, mode) {
        root.exitModes()                 // ein laufender Modus wird sauber beendet
        root.tagsCtl.beginTagModeGroup(tag)
        modeTag = tag; tagMode = mode
    }

    readonly property int margin: 12
    readonly property int spacing: 8
    property bool listMode: false
    property bool optionsVisible: App.optionsVisible
    readonly property int tileW: root.listMode ? Math.max(200, root.areaW - spacing)
                                               : App.tileWidth
    readonly property int tileH: root.listMode ? App.listRowHeight : App.tileHeight
    readonly property int cellW: tileW + spacing
    readonly property int cellH: tileH + spacing

    // Obergrenze = was diese Galeriefläche VOLLSTÄNDIG darstellen kann (Seitenränder, Zellen-Padding, eine Zelle
    // muss in die Höhe passen). `App.setTileSize` klemmt dagegen, Dialoge binden an `App.maxTileWidth/-Height`.
    function _reportTileLimit() {
        if (width > 0 && height > 0)
            App.setTileSizeLimit(width - 2 * margin - spacing, height - spacing)
    }
    onWidthChanged: _reportTileLimit()
    onHeightChanged: _reportTileLimit()
    Component.onCompleted: _reportTileLimit()

    readonly property int areaW: App.tileArrangement === 3   // Manual
                                 ? Math.min(App.manualAreaWidth, root.width - 2 * margin)
                                 : root.width - 2 * margin
    readonly property int columns: Math.max(1, Math.floor(areaW / cellW))
    readonly property int gridW: columns * cellW
    readonly property int gridX: {
        switch (App.tileArrangement) {
        case 1: return margin                              // Left
        case 2: return Math.max(margin, root.width - margin - gridW)  // Right
        default: return Math.max(margin, (root.width - gridW) / 2)    // Centered/Manual
        }
    }

    // Ein Band liegt unter den Kacheln seines Ordners und deckt genau dessen Zeilen ab. Die Ebenen
    // unterscheiden sich NUR über die Farbe: eingerückte Bänder müssten die Kachelzahl je Tiefe verringern,
    // sonst ragte die letzte Kachel hinaus. Die Aufhellung dreht mit dem Theme, sonst wäre das Band weiss auf weiss.
    readonly property bool darkTheme: (0.299 * App.themeBackground.r
                                     + 0.587 * App.themeBackground.g
                                     + 0.114 * App.themeBackground.b) < 0.5
    readonly property int  bandPad: 12       // Luft INNERHALB des Bandes
    readonly property int  bandRadius: 12
    readonly property int  bandGap: 14
    readonly property int  levelInset: 26
    // DECKEND, nicht halbtransparent: mit Alpha addieren sich Deckstreifen und Fläche darunter zu einem helleren
    // Streifen, und die abgedeckte Rundung blitzt trotzdem durch. Der Ton wird einmal über den Grund gerechnet.
    function bandColor(level) {
        var l = Math.min(Math.max(level, 1), 4)
        var a = root.darkTheme ? 0.05 * l : 0.035 * l
        var t = root.darkTheme ? 1.0 : 0.0          // Ziel: Weiß bzw. Schwarz
        var bg = App.themeBackground
        return Qt.rgba(bg.r * (1 - a) + t * a,
                       bg.g * (1 - a) + t * a,
                       bg.b * (1 - a) + t * a, 1)
    }

    Text {
        anchors.centerIn: parent
        visible: galleryModel.count === 0
        width: Math.min(root.width - 48, 640)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: App.currentFolder.length > 0
              ? App.uiText(App.language, "GalleryNoMedia")
              : App.uiText(App.language, "GalleryNoFolder")
        color: App.themeTextMuted
        font.pixelSize: 14
    }

    GalleryRowModel {
        id: rowModel
        source: galleryModel
        contentWidth: root.gridW
        cellWidth: root.cellW
        levelInset: root.levelInset
    }

    // Gummiband auf leerer Fläche: die Fläche liegt VOR der ListView im Baum und damit unter ihr - eine Kachel
    // nimmt den Druck zuerst, freier Raum fällt hierher durch, ein `z` braucht es nicht. Es wird NICHT
    // mitgescrollt: was oben hinausrutscht, wäre weder sichtbar gewählt noch abgewählt (s. LIMITATIONS.md).
    property bool bandActive: false
    property real bandAx: 0
    property real bandAy: 0
    property real bandBx: 0
    property real bandBy: 0
    readonly property real _bandL: Math.min(root.bandAx, root.bandBx)
    readonly property real _bandR: Math.max(root.bandAx, root.bandBx)
    readonly property real _bandT: Math.min(root.bandAy, root.bandBy)
    readonly property real _bandB: Math.max(root.bandAy, root.bandBy)

    function bandRanges() {
        const out = []
        if (rowModel.count === 0) return out
        let r = grid.indexAt(1, Math.max(0, grid.contentY + 1))
        if (r < 0) r = 0
        for (; r < rowModel.count; ++r) {
            const it = grid.itemAtIndex(r)
            if (!it) break                       // ab hier ist nichts ausgelegt
            if (it.y > root._bandB) break        // der Rahmen endet darüber
            if (it.y + it.height < root._bandT) continue
            const info = rowModel.rowInfo(r)
            if (!info || info.kind !== 0 || info.count <= 0) continue
            const padTop = (info.openMask !== 0) ? root.bandPad : 0
            const top    = it.y + padTop
            if (top > root._bandB || top + root.cellH < root._bandT) continue
            const inset = info.depth * root.levelInset
            const relL  = root._bandL - inset
            const relR  = root._bandR - inset
            if (relR < 0 || relL > info.count * root.cellW) continue
            const c0 = Math.max(0, Math.floor(relL / root.cellW))
            const c1 = Math.min(info.count - 1, Math.floor(relR / root.cellW))
            if (c1 < c0) continue
            out.push(info.first + c0, info.first + c1)
        }
        return out
    }

    MouseArea {
        id: bandArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        property real pressX: 0
        property real pressY: 0
        property bool armed: false
        property bool additive: false

        onPressed: function(mouse) {
            root.forceActiveFocus()          // s. selectFromTile
            bandArea.pressX  = mouse.x - grid.x
            bandArea.pressY  = mouse.y - grid.y + grid.contentY
            bandArea.additive = (mouse.modifiers & Qt.ControlModifier) !== 0
            bandArea.armed   = true
            root.bandActive  = false
        }
        onPositionChanged: function(mouse) {
            if (!bandArea.armed) return
            const cx = mouse.x - grid.x
            const cy = mouse.y - grid.y + grid.contentY
            if (!root.bandActive) {
                if (Math.abs(cx - bandArea.pressX) < 8
                    && Math.abs(cy - bandArea.pressY) < 8)
                    return                        // noch ein Klick, kein Zug
                root.bandActive = true
                root.bandAx = bandArea.pressX
                root.bandAy = bandArea.pressY
                galleryModel.beginBand(bandArea.additive)
            }
            root.bandBx = cx
            root.bandBy = cy
            galleryModel.updateBand(root.bandRanges())
        }
        onReleased: {
            bandArea.armed = false
            if (root.bandActive) {
                galleryModel.endBand()
                root.bandActive = false
                root.selAnchor = -1
            } else {
                root.clearSelection()
            }
        }
        onCanceled: {
            bandArea.armed = false
            if (root.bandActive) { galleryModel.endBand(); root.bandActive = false }
        }
    }

    // Eine ListView über ZEILEN, kein GridView über Kacheln: ein GridView hat gleich hohe Zellen und kann eine
    // Zeile nicht umbrechen - aufgeklappte Unterordner brauchen genau das.
    ListView {
        id: grid
        objectName: "galleryRows"
        y: 0
        x: root.gridX
        width: root.gridW
        height: root.height
        clip: true

        model: rowModel

        reuseItems: true
        cacheBuffer: root.cellH * 2
        boundsBehavior: Flickable.StopAtBounds
        // KEIN Ziehen-zum-Scrollen: ein Zug gehört der KACHEL, die die Datei nach draußen zieht - beides gleichzeitig
        // ging nicht, die Ansicht riss den Griff an sich. Gescrollt wird per Rad, Bildlaufleiste und Pfeiltasten.
        interactive: false

        // Zurück in den gültigen Bereich, sobald Inhalt oder Geometrie sich ändern: mit `interactive: false` holt sich
        // das Flickable nicht selbst zurück, die Ansicht blieb nach Suche oder Ordnerwechsel außerhalb stehen.
        function _clampNow() {
            if (gridScroll.running) return          // die Animation klemmt selbst
            const v = root.clampContentY(contentY)
            if (Math.abs(v - contentY) > 0.5) contentY = v
        }
        onContentHeightChanged: _clampNow()
        onOriginYChanged:       _clampNow()
        onHeightChanged:        _clampNow()
        onCountChanged:         _clampNow()

        ScrollBar.vertical: ScrollBar {
            id: vScroll
            parent: root
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            policy: ScrollBar.AsNeeded
        }

        delegate: Item {
            id: rowItem
            width: grid.width

            required property int    kind        // 0 = Kacheln, 1 = Kopfzeile
            required property string ownerName
            required property string ownerFolder
            required property int depth
            required property int firstIndex
            required property int tileCount
            required property int openMask
            required property int closeMask
            required property var tiles

            readonly property int padTop:    (rowItem.openMask  !== 0) ? root.bandPad : 0
            readonly property int padBottom: (rowItem.closeMask !== 0) ? root.bandPad : 0
            readonly property int gapBottom: (rowItem.closeMask !== 0) ? root.bandGap : 0

            height: (rowItem.kind === 1 ? root.headerHeight : root.cellH)
                    + rowItem.padTop + rowItem.padBottom + rowItem.gapBottom

            Repeater {
                model: rowItem.depth

                delegate: Item {
                    id: bandItem
                    required property int index
                    readonly property int   level: bandItem.index + 1
                    readonly property bool  opens:  (rowItem.openMask  & (1 << bandItem.level)) !== 0
                    readonly property bool  closes: (rowItem.closeMask & (1 << bandItem.level)) !== 0
                    readonly property color tone:  root.bandColor(bandItem.level)
                    x: 0
                    y: 0
                    width: rowItem.width
                    height: rowItem.height - rowItem.gapBottom

                    Rectangle {
                        anchors.fill: parent
                        radius: root.bandRadius
                        color: bandItem.tone
                    }
                    Rectangle {
                        visible: !bandItem.opens
                        x: 0; y: 0
                        width: parent.width; height: root.bandRadius
                        color: bandItem.tone
                    }
                    Rectangle {
                        visible: !bandItem.closes
                        x: 0; y: parent.height - root.bandRadius
                        width: parent.width; height: root.bandRadius
                        color: bandItem.tone
                    }
                }
            }

            Row {
                visible: rowItem.kind === 1
                x: rowItem.depth * root.levelInset + 4
                y: rowItem.padTop
                height: root.headerHeight
                spacing: 8

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.ownerName
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                    font.bold: true
                }
                HeaderAction {
                    label: App.uiText(App.language, "CtxFolderOpen")
                    onTriggered: root.folderOpenRequested(rowItem.ownerFolder)
                }
                HeaderAction {
                    label: "+ " + App.uiText(App.language, "CreateFileBtn")
                    onTriggered: root.createFileRequested(rowItem.ownerFolder)
                }
                HeaderAction {
                    label: App.uiText(App.language, "FolderNew")
                    onTriggered: newFolderDialog.openFor(rowItem.ownerFolder)
                }
                HeaderAction {
                    label: App.uiText(App.language, "FilterExtractBtn")
                    onTriggered: root.extractPagesRequested(rowItem.ownerFolder)
                }
            }

            Repeater {
                model: rowItem.tileCount

                delegate: Item {
                    id: cell
                    required property int index

                    readonly property var d: (rowItem.tiles && cell.index < rowItem.tiles.length)
                                             ? rowItem.tiles[cell.index] : null
                    readonly property string filePath: cell.d ? cell.d.filePath : ""
                    readonly property int    mediaType: cell.d ? cell.d.mediaType : 6

                    x: rowItem.depth * root.levelInset + cell.index * root.cellW
                    y: rowItem.padTop
                    // Im Listen-Modus füllt eine Kachel die Zeile und zieht die Einrückung von ihrer Länge ab. ABGEZOGEN WIRD
                    // NUR LINKS: so bleibt die rechte Kante auf allen Ebenen dieselbe; beidseitig würde jede Ebene zusätzlich
                    // kürzer und die rechten Kanten stünden treppenförmig versetzt.
                    width: root.listMode
                           ? Math.max(120, root.cellW - rowItem.depth * root.levelInset)
                           : root.cellW
                    height: root.cellH

                    property string requestedPath: ""

                    function syncThumb() {
                        if (cell.mediaType === 7) return
                        if (cell.requestedPath === cell.filePath) return
                        if (cell.requestedPath.length > 0)
                            mediaModel.cancelThumbnail(cell.requestedPath)
                        cell.requestedPath = cell.filePath
                        if (cell.filePath.length > 0)
                            mediaModel.ensureThumbnail(cell.filePath)
                    }

                    Component.onCompleted: syncThumb()
                    onFilePathChanged: syncThumb()

                    Connections {
                        target: mediaModel
                        function onThumbnailsInvalidated() {
                            cell.requestedPath = ""
                            cell.syncThumb()
                        }
                    }

                    // Sicherheitsnetz, kein Hauptweg: die Kachel merkt sich ihre Anforderung und fragt von sich aus nie wieder -
                    // geht eine unterwegs verloren (abbestellt, oder gestellt während das Modell neu aufbaut), bliebe sie dauerhaft
                    // ohne Bild. Läuft nur ohne Vorschau und bei `thumbState === 2` gar nicht: dort gibt es nichts zu holen.
                    Timer {
                        interval: 1500
                        repeat: true
                        running: cell.filePath.length > 0 && cell.mediaType !== 7
                                 && cell.d !== null && cell.d.thumbState === 0
                        onTriggered: {
                            cell.requestedPath = ""
                            cell.syncThumb()
                        }
                    }

                    Component.onDestruction: {
                        if (cell.requestedPath.length > 0)
                            mediaModel.cancelThumbnail(cell.requestedPath)
                    }

                    MediaTile {
                        anchors.centerIn: parent
                        // Im Listen-Modus füllt die Kachel ihre ZELLE, und die ist bei aufgeklapptem Unterordner schmaler. Die feste
                        // Zeilenbreite `root.tileW` hob die Einrückung auf - x=16 auf Ebene 0, 1 und 2 (gemessen).
                        width: root.listMode
                               ? Math.max(120, cell.width - root.spacing)
                               : root.tileW
                        height: root.tileH
                        listMode: root.listMode
                        optionsVisible: root.optionsVisible

                        filePath:    cell.filePath
                        displayName: cell.d ? cell.d.displayName : ""
                        mediaType:   cell.mediaType
                        typeLabel:   cell.d ? cell.d.typeLabel : ""
                        tags:        cell.d ? cell.d.tags : []
                        dateTime:    cell.d ? cell.d.dateTime : undefined
                        thumbUrl:    cell.d ? cell.d.thumbUrl : ""
                        thumbState:  cell.d ? cell.d.thumbState : 0
                        expanded:    cell.d ? cell.d.expanded : false
                        childCount:  cell.d && cell.d.childCount !== undefined
                                     ? cell.d.childCount : -1

                        tagMode: root.tagMode
                        modeTag: root.modeTag
                        covered: root.covered

                        proxyRow: cell.d && cell.d.row !== undefined ? cell.d.row : -1
                        selected: mediaModel.selectionRevision >= 0
                                  && mediaModel.isSelected(cell.filePath)

                        onSelectRequested: function(row, mods) {
                            root.selectFromTile(row, mods)
                        }
                        onSelectionResetRequested: mediaModel.clearSelection()
                        onCopyRequested: function(p) { root.copyToClipboard(p) }
                        onDeleteSelectionRequested: root.requestDeleteSelection()

                        onActivated: function(p) { root.activated(p) }
                        onFileClicked: function(p) { root.fileClicked(p) }
                        onFolderOpenRequested: function(p) { root.folderOpenRequested(p) }
                        onDragStartRequested: function(p, url) {
                            root.requestFileDrag(p, url)
                        }
                        onFolderRenameRequested: function(p, n) {
                            newFolderDialog.openRename(p, n)
                        }
                        onFolderDeleteRequested: function(p, n, c) {
                            folderDeleteDialog.askFor(p, n, c)
                        }
                        dropTarget: cell.mediaType === 7
                                    && root.hoverFolder.length > 0
                                    && root.hoverFolder === cell.filePath
                        onRenameRequested: function(p, n) {
                            fileRenameDialog.openFor(p, n)
                        }
                        onNewTagRequested: function(p) {
                            newForFileDialog.openFor(p, true)
                        }
                        onNewCategoryRequested: function(p) {
                            newForFileDialog.openFor(p, false)
                        }
                        onDeleteRequested: function(p, n) {
                            deleteDialog.targetPath = p
                            deleteDialog.targetName = n
                            deleteDialog.open()
                        }
                        onAudioExtractRequested: function(p) {
                            root.audioExtractRequested(p)
                        }
                        onCompanionRemoveRequested: function(p, kind) {
                            companionDialog.targetPath = p
                            companionDialog.kind = kind
                            companionDialog.open()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: newForFileDialog
        property string filePath: ""
        property bool   forTag: true
        function openFor(path, tag) {
            filePath = path
            forTag = tag
            newForFileField.text = ""
            open(); newForFileField.forceActiveFocus()
        }
        function apply() {
            const v = newForFileField.text.trim()
            if (v.length > 0 && newForFileDialog.filePath.length > 0) {
                if (newForFileDialog.forTag) {
                    mediaModel.addTag(newForFileDialog.filePath, v)
                } else {
                    const fn = newForFileDialog.filePath.substring(
                        Math.max(newForFileDialog.filePath.lastIndexOf("/"),
                                 newForFileDialog.filePath.lastIndexOf("\\")) + 1)
                    const id = Tags.addRootCategory(v, Qt.rgba(0, 0.7, 0.63, 1), false)
                    if (id && id.length > 0) Tags.toggleFileInCategory(id, fn)
                }
            }
            close()
        }
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
                text: newForFileDialog.forTag
                      ? App.uiText(App.language, "CatPanelNewTag")
                      : App.uiText(App.language, "CatPanelAddCategory")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            TextField {
                id: newForFileField
                width: 300
                color: App.themeTextPrimary
                onAccepted: newForFileDialog.apply()
            }
            Row {
                spacing: 8
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: newForFileDialog.close()
                }
                Button {
                    text: App.uiText(App.language, "SettingsOk")
                    onClicked: newForFileDialog.apply()
                }
            }
        }
    }

    Dialog {
        id: fileRenameDialog
        property string filePath: ""
        function openFor(path, currentName) {
            filePath = path
            fileNameField.text = currentName
            open(); fileNameField.forceActiveFocus(); fileNameField.selectAll()
        }
        function apply() {
            const t = fileNameField.text.trim()
            if (t.length > 0 && fileRenameDialog.filePath.length > 0)
                mediaModel.renameItem(fileRenameDialog.filePath, t)
            close()
        }
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
                text: App.uiText(App.language, "CtxRenameFile")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            TextField {
                id: fileNameField
                width: 300
                color: App.themeTextPrimary
                onAccepted: fileRenameDialog.apply()
            }
            Row {
                spacing: 8
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: fileRenameDialog.close()
                }
                Button {
                    text: App.uiText(App.language, "SettingsOk")
                    onClicked: fileRenameDialog.apply()
                }
            }
        }
    }

    Dialog {
        id: newFolderDialog
        property string parentFolder: ""     // anlegen
        property string renamePath: ""       // umbenennen (leer = anlegen)
        property string errorText: ""

        function openFor(folder) {
            parentFolder = folder; renamePath = ""; errorText = ""
            nameField.text = ""
            open(); nameField.forceActiveFocus()
        }
        function openRename(path, currentName) {
            parentFolder = ""; renamePath = path; errorText = ""
            nameField.text = currentName
            open(); nameField.forceActiveFocus(); nameField.selectAll()
        }
        function apply() {
            if (nameField.text.trim().length === 0) return
            var code = renamePath.length > 0
                       ? mediaModel.renameFolder(renamePath, nameField.text)
                       : mediaModel.createFolder(parentFolder, nameField.text)
            if (code === 0) { close(); return }
            errorText = code === 1 ? App.uiText(App.language, "FolderNameInvalid")
                      : code === 2 ? App.uiText(App.language, "FolderExists")
                                   : App.uiText(App.language, "FolderCreateFailed")
        }

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
                text: newFolderDialog.renamePath.length > 0
                      ? App.uiText(App.language, "FolderRenameTitle")
                      : App.uiText(App.language, "FolderNewTitle")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            TextField {
                id: nameField
                width: 300
                placeholderText: App.uiText(App.language, "FolderNamePlaceholder")
                onAccepted: newFolderDialog.apply()
                onTextChanged: newFolderDialog.errorText = ""
            }
            Text {
                width: 300
                visible: newFolderDialog.errorText.length > 0
                text: newFolderDialog.errorText
                color: "#e08080"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: newFolderDialog.close()
                }
                Button {
                    text: App.uiText(App.language, "SettingsOk")
                    enabled: nameField.text.trim().length > 0
                    onClicked: newFolderDialog.apply()
                }
            }
        }
    }

    Dialog {
        id: folderDeleteDialog
        property string targetPath: ""
        property string targetName: ""
        property int    targetCount: -1
        function askFor(path, name, count) {
            targetPath = path; targetName = name; targetCount = count
            open()
        }
        anchors.centerIn: parent
        modal: true
        focus: true
        onAccepted: {
            if (!mediaModel.deleteFolder(folderDeleteDialog.targetPath))
                root.statusRequested(App.uiText(App.language, "DeleteFolderNoTrash"))
        }
        padding: 18
        background: Rectangle {
            color: App.themeCard; radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { folderDeleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { folderDeleteDialog.accept(); e.accepted = true }
            spacing: 10
            Text {
                text: App.uiText(App.language, "DeleteFolderTitle")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 340
                text: App.uiText(App.language, "DeleteFolderText")
                          .arg(folderDeleteDialog.targetName)
                          .arg(Math.max(0, folderDeleteDialog.targetCount))
                color: App.themeTextMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: folderDeleteDialog.close()
                }
                Button {
                    text: App.uiText(App.language, "DeleteMediaConfirm")
                    onClicked: folderDeleteDialog.accept()
                }
            }
        }
    }

    Dialog {
        id: companionDialog
        property string targetPath: ""
        property int    kind: 1
        anchors.centerIn: parent
        modal: true
        focus: true
        onAccepted: mediaModel.removeCompanion(companionDialog.targetPath,
                                               companionDialog.kind)
        padding: 18
        background: Rectangle {
            color: App.themeCard; radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { companionDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { companionDialog.accept(); e.accepted = true }
            spacing: 10
            Text {
                text: companionDialog.kind === 1
                      ? App.uiText(App.language, "CtxRemoveEdits")
                      : App.uiText(App.language, "CtxRemoveBackup")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 320
                text: companionDialog.kind === 1
                      ? App.uiText(App.language, "CtxRemoveEditsAsk")
                      : App.uiText(App.language, "CtxRemoveBackupAsk")
                color: App.themeTextMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Rectangle {
                    width: compCancel.implicitWidth + 24; height: 30; radius: 6
                    color: "transparent"; border.color: App.themeBorder; border.width: 1
                    Text { id: compCancel; anchors.centerIn: parent
                           text: App.uiText(App.language, "SettingsCancel")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    TapHandler { onTapped: companionDialog.close() }
                }
                Rectangle {
                    width: compOk.implicitWidth + 24; height: 30; radius: 6
                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                   App.themeAccent.b, 0.28)
                    border.color: App.themeAccent; border.width: 1
                    Text { id: compOk; anchors.centerIn: parent
                           text: App.uiText(App.language, "SettingsOk")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    TapHandler { onTapped: companionDialog.accept() }
                }
            }
        }
    }

    Dialog {
        id: deleteDialog
        property string targetPath: ""
        property string targetName: ""
        anchors.centerIn: parent
        modal: true
        focus: true
        onAccepted: {
            mediaModel.deleteItem(deleteDialog.targetPath)
            root.forceActiveFocus()          // s. selDeleteDialog
        }
        padding: 18
        background: Rectangle {
            color: App.themeCard
            radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { deleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { deleteDialog.accept(); e.accepted = true }
            spacing: 10
            Text {
                text: App.uiText(App.language, "DeleteMediaTitle")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 300
                text: App.uiText(App.language, "DeleteMediaText").arg(deleteDialog.targetName)
                color: App.themeTextMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Rectangle {
                    width: cancelLbl.implicitWidth + 24; height: 30; radius: 6
                    color: cancelHover.hovered
                           ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                           : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                    border.color: App.themeBorder; border.width: 1
                    Text { id: cancelLbl; anchors.centerIn: parent
                           text: App.uiText(App.language, "SettingsCancel")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: cancelHover }
                    TapHandler { onTapped: deleteDialog.close() }
                }
                Rectangle {
                    width: delLbl.implicitWidth + 24; height: 30; radius: 6
                    color: delHover.hovered ? Qt.rgba(0.88, 0.35, 0.35, 0.30)
                                            : Qt.rgba(0.88, 0.35, 0.35, 0.16)
                    border.color: "#c25a5a"; border.width: 1
                    Text { id: delLbl; anchors.centerIn: parent
                           text: App.uiText(App.language, "DeleteMediaConfirm")
                           color: "#e08080"; font.pixelSize: 12 }
                    HoverHandler { id: delHover }
                    TapHandler { onTapped: deleteDialog.accept() }
                }
            }
        }
    }

    Dialog {
        id: selDeleteDialog
        property int count: 0
        anchors.centerIn: parent
        modal: true
        focus: true
        onAccepted: {
            const n = mediaModel.deleteSelected()
            root.selAnchor = -1
            root.forceActiveFocus()
            if (n > 0)
                root.statusRequested(
                    App.uiText(App.language, "SelDeleted").replace("%1", n))
        }
        padding: 18
        background: Rectangle {
            color: App.themeCard
            radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { selDeleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { selDeleteDialog.accept(); e.accepted = true }
            spacing: 10
            Text {
                text: App.uiText(App.language, "SelDeleteTitle")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 320
                text: App.uiText(App.language, "SelDeleteText")
                          .replace("%1", selDeleteDialog.count)
                color: App.themeTextMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Rectangle {
                    width: selCancelLbl.implicitWidth + 24; height: 30; radius: 6
                    color: selCancelHover.hovered
                           ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                           : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                    border.color: App.themeBorder; border.width: 1
                    Text { id: selCancelLbl; anchors.centerIn: parent
                           text: App.uiText(App.language, "SettingsCancel")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: selCancelHover }
                    TapHandler { onTapped: selDeleteDialog.close() }
                }
                Rectangle {
                    width: selDelLbl.implicitWidth + 24; height: 30; radius: 6
                    color: selDelHover.hovered ? Qt.rgba(0.88, 0.35, 0.35, 0.30)
                                               : Qt.rgba(0.88, 0.35, 0.35, 0.16)
                    border.color: "#c25a5a"; border.width: 1
                    Text { id: selDelLbl; anchors.centerIn: parent
                           text: App.uiText(App.language, "DeleteMediaConfirm")
                           color: "#e08080"; font.pixelSize: 12 }
                    HoverHandler { id: selDelHover }
                    TapHandler { onTapped: selDeleteDialog.accept() }
                }
            }
        }
    }

    // Während eines QDrag reicht Qt Radereignisse nicht mehr an die Elemente durch; `App` sieht sie über einen
    // Filter auf der Anwendung und meldet sie hier weiter (`AppController::eventFilter`).
    Connections {
        target: App
        function onDragWheel(angleDeltaY) {
            if (root.maxContentY <= root.minContentY) return
            //  Dieselbe Schrittweite wie beim gewöhnlichen Scrollen, aber OHNE
            //  Animation: während eines Zuges soll die Ansicht dem Rad direkt
            //  folgen und nicht nachschwingen.
            const step = (angleDeltaY / 120) * (grid.height * 0.45)
            grid.contentY = root.clampContentY(grid.contentY - step)
            const f = root.folderAtPoint(galleryDrop.lastX, galleryDrop.lastY)
            if (f !== root.hoverFolder) root.hoverFolder = f
        }
    }

    // EIN Zug-Träger für die ganze Galerie. `Drag.Automatic` + `Drag.active = true` ist der einzige Weg aus dem
    // Fenster heraus (`text/uri-list`, nur kopieren - mit `MoveAction` dürfte ein Zielprogramm die Datei
    // entfernen). Er liegt hier, weil die Zuweisung blockiert: ein Scrollen räumte sonst seine Kachel weg -> Absturz.
    Item {
        id: dragPayload
        width: 1; height: 1
        visible: false
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction
    }

    //  Zwei Schritte: erst den Handler der Kachel verlassen (`Qt.callLater`),
    //  dann den blockierenden Zug starten. Sonst stünde die Kachel weiterhin
    //  auf dem Stapel und dürfte nicht weggeräumt werden.
    function requestFileDrag(filePath, thumbUrl) {
        if (filePath.length === 0) return
        // Gehört die angefasste Kachel zur Auswahl, wandert die GANZE Auswahl mit, sonst nur sie selbst. Ordner bleiben
        // außen vor: ein Ordner-Zug ins Fremdprogramm ist eine eigene Zusage, keine Nebenwirkung.
        const many = mediaModel.selectionCount > 1 && mediaModel.isSelected(filePath)
        const list = many ? galleryModel.selectedPaths(true) : [filePath]
        if (list.length === 0) return
        Qt.callLater(root._runFileDrag, list, thumbUrl)
    }
    // `text/uri-list` trägt mehrere Adressen, je eine Zeile (RFC 2483: CRLF). Eigene Funktion, damit der Prüfstand
    // sie ohne echten Zug aufrufen kann - der blockiert bis zum Loslassen (s. tests/media/tst_bandselect).
    function dragUriList(paths) {
        const uris = []
        for (var i = 0; i < paths.length; ++i) {
            if (!paths[i] || paths[i].length === 0) continue
            uris.push(App.fileUrl(paths[i]))
        }
        return uris.join("\r\n")
    }
    function _runFileDrag(paths, thumbUrl) {
        const filePath = paths[0]
        dragPayload.Drag.mimeData = { "text/uri-list": root.dragUriList(paths) }
        //  Am Zeiger hängt die Miniatur - sonst zieht man ins Blaue.
        dragPayload.Drag.imageSource = thumbUrl
        App.beginTileDrag()
        //  BLOCKIERT bis zum Ende des Zuges und setzt `active` danach selbst
        //  zurück; ein deklaratives `Drag.active: false` gehört NICHT daneben.
        dragPayload.Drag.active = true
        App.endTileDrag()
        galleryDrop.autoDir = 0
        autoScroll.stop()
        root.hoverFolder = ""
    }

    DropArea {
        id: galleryDrop
        anchors.fill: parent
        keys: ["text/uri-list"]

        // Randscrollen ist der EINZIGE Weg, während eines Zuges zu scrollen: unter Wayland gehört der Zeiger dem
        // Compositor. Gemessen (MG_DRAGLOG, echter Zug): 892 DragMove, aber kein einziges Wheel und kein MouseMove.
        // Das Tempo wächst mit der Nähe zum Rand - mit fester Geschwindigkeit war der Effekt zu leicht zu übersehen.
        readonly property int edgeZone: 72
        readonly property int minStep: 4
        readonly property int maxStep: 34
        property int  autoDir: 0         // −1 hoch · 0 aus · +1 runter
        property real autoStep: 0        // Pixel je Takt
        //  Letzte bekannte Zeigerposition: waehrend die Ansicht von selbst
        //  scrollt, bewegt sich der Zeiger nicht - das Ziel muss trotzdem
        //  laufend neu bestimmt werden, und zwar an SEINER Stelle.
        property real lastX: 0
        property real lastY: 0

        //  DIAGNOSE (nur mit MG_DRAGLOG=1, s. AppController): kommt der Zug hier
        //  ueberhaupt an, und laeuft das Randscrollen?
        readonly property bool logging: App.dragLogging
        property int moves: 0
        property int ticks: 0

        onPositionChanged: function(d) {
            galleryDrop.lastX = d.x
            galleryDrop.lastY = d.y
            if (galleryDrop.logging) {
                galleryDrop.moves++
                if (galleryDrop.moves % 20 === 1)
                    console.log("[MG_DRAGLOG] DropArea positionChanged #"
                                + galleryDrop.moves + " y=" + Math.round(d.y)
                                + " Hoehe=" + Math.round(root.height))
            }
            const f = root.folderAtPoint(d.x, d.y)
            if (f !== root.hoverFolder) root.hoverFolder = f

            //  Wie tief steckt der Zeiger in der Randzone? 0 = gerade eben
            //  drin, 1 = ganz am Rand.
            const near = galleryDrop.edgeZone
            let depth = 0
            if (d.y < near) {
                galleryDrop.autoDir = -1
                depth = (near - d.y) / near
            } else if (d.y > root.height - near) {
                galleryDrop.autoDir = +1
                depth = (d.y - (root.height - near)) / near
            } else {
                galleryDrop.autoDir = 0
            }
            galleryDrop.autoStep = galleryDrop.minStep
                                 + (galleryDrop.maxStep - galleryDrop.minStep)
                                   * Math.max(0, Math.min(1, depth))
            if (galleryDrop.logging && galleryDrop.autoDir !== 0
                && galleryDrop.moves % 20 === 1)
                console.log("[MG_DRAGLOG] Randzone: Richtung "
                            + galleryDrop.autoDir + ", Schritt "
                            + Math.round(galleryDrop.autoStep))
        }
        // Der Takt läuft während des GANZEN Zuges: ihn bei jedem Wackeln über die Zonengrenze zu stoppen und neu zu
        // starten machte das Scrollen ruckelig, und ein 16-ms-Timer, der meistens nichts tut, kostet nichts.
        onEntered: {
            galleryDrop.autoDir = 0
            autoScroll.start()
        }
        onExited: {
            root.hoverFolder = ""
            galleryDrop.autoDir = 0
            autoScroll.stop()
        }

        //  Ein Takt statt einer Animation: die Richtung kann sich bei jeder
        //  Bewegung ändern, und die Ansicht soll dabei nicht nachschwingen.
        Timer {
            id: autoScroll
            interval: 16
            repeat: true
            onTriggered: {
                if (galleryDrop.autoDir === 0) return        // laeuft weiter, tut nichts
                const v = root.clampContentY(
                    grid.contentY + galleryDrop.autoDir * galleryDrop.autoStep)
                if (galleryDrop.logging) {
                    galleryDrop.ticks++
                    if (galleryDrop.ticks % 20 === 1)
                        console.log("[MG_DRAGLOG] Scroll-Takt #" + galleryDrop.ticks
                                    + " contentY " + Math.round(grid.contentY)
                                    + " -> " + Math.round(v)
                                    + " (Grenzen " + Math.round(root.minContentY)
                                    + ".." + Math.round(root.maxContentY) + ")")
                }
                if (Math.abs(v - grid.contentY) < 0.5) return   // am Anschlag
                grid.contentY = v
                //  Unter dem Zeiger liegt jetzt ein anderer Ordner - bestimmt
                //  an seiner ZULETZT BEKANNTEN Stelle, nicht in der Mitte.
                const f = root.folderAtPoint(galleryDrop.lastX, galleryDrop.lastY)
                if (f !== root.hoverFolder) root.hoverFolder = f
            }
        }

        onDropped: function(drop) {
            autoScroll.stop()
            galleryDrop.autoDir = 0
            const target = root.folderAtPoint(drop.x, drop.y)
            root.hoverFolder = ""
            if (!drop.hasUrls || target.length === 0) { drop.accepted = false; return }

            //  Ein Zug kann MEHRERE Dateien tragen (Mehrfachauswahl). Ob er
            //  aus der App kommt, entscheidet die erste Adresse - gemischt
            //  kommt eine Nutzlast nicht vor.
            const src = App.localPath(drop.urls[0])
            if (root._appOwnsFile(src)) {
                //  Ein app-interner Zug: verschieben/kopieren. In den EIGENEN
                //  Ordner abzulegen ist keine Bewegung - das Modell meldete
                //  sonst „nicht möglich" und die Shell zeigte einen Fehler.
                const moves = []
                for (var i = 0; i < drop.urls.length; ++i) {
                    const p = App.localPath(drop.urls[i])
                    if (p.length > 0 && root._parentOf(p) !== target) moves.push(p)
                }
                if (moves.length > 0)
                    root.folderDropRequested(moves, target)
            } else {
                root.externalDropRequested(drop.urls, target)
            }
            drop.acceptProposedAction()
        }
    }

    //  Der sichtbare Rahmen. Reine Anzeige - er faengt nichts ab (kein
    //  MouseArea darin), sonst naehme er dem Zug seine eigenen Ereignisse.
    Rectangle {
        visible: root.bandActive
        z: 3
        x: grid.x + root._bandL
        y: grid.y + root._bandT - grid.contentY
        width:  Math.max(0, root._bandR - root._bandL)
        height: Math.max(0, root._bandB - root._bandT)
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.16)
        border.color: App.themeAccent
        border.width: 1
        radius: 2
    }

    // Eine MouseArea(NoButton) fängt die Radereignisse zuverlässig ab - ein WheelHandler oder das GridView selbst
    // verschluckt sie sonst; NoButton lässt Klicks, Doppelklicks und Hover ungehindert durch.
    NumberAnimation {
        id: gridScroll
        target: grid; property: "contentY"
        duration: 180; easing.type: Easing.OutCubic
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        z: 2
        onWheel: function(wheel) {
            if (wheel.modifiers & Qt.ControlModifier) {
                // Im Listen-Modus wächst die ZEILENHÖHE, nicht die Kachel: sonst tat sich beim Zoomen sichtbar nichts.
                // Schrittweite 4 statt 16 - eine Zeile startet bei 46 px, eine Kachel bei 200.
                if (wheel.angleDelta.y > 0)
                    root.listMode ? App.zoomInList(4)  : App.zoomIn(16)
                else if (wheel.angleDelta.y < 0)
                    root.listMode ? App.zoomOutList(4) : App.zoomOut(16)
                wheel.accepted = true
                return
            }
            if (root.maxContentY <= root.minContentY) { wheel.accepted = true; return }
            var raw = (wheel.angleDelta.y !== 0)
                      ? (wheel.angleDelta.y / 120) * (grid.height * 0.45)
                      : wheel.pixelDelta.y * 1.6
            var base = gridScroll.running ? gridScroll.to : grid.contentY
            var tgt = root.clampContentY(base - raw)
            gridScroll.from = grid.contentY
            gridScroll.to = tgt
            gridScroll.restart()
            wheel.accepted = true
        }
    }
}
