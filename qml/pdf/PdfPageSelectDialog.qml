import QtQuick
import QtQuick.Window
import QtQuick.Controls
import MediaGallery 1.0

// Seitenauswahl der Extraktion - EINE Komponente für beide Aufrufwege (in-PDF und global) und zwei Layouts:
// Werkbank (Reihenfolge der unteren Leiste = Ausgabereihenfolge) oder kompaktes Einzelraster in
// Originalreihenfolge. `barModel` hält die Auswahl geordnet, `_inSel` ist die schnelle Mitgliedschaftsmenge.
Item {
    id: root

    property var    files: []
    property bool   requireName: false     // global: Name ist Pflicht
    property string titleText: ""
    property string defaultName: ""        // Platzhalter des Namensdialogs
    // Zweitverwendung "Seiten einfügen": dort entsteht keine neue Datei, also wird kein Name abgefragt.
    // `askName=false` zieht zugleich die Bindung an `PdfExtract.busy` heraus, der bei diesem Weg nicht läuft.
    property bool   askName: true
    property string confirmText: ""        // leer = Standardtext „Erstellen"

    signal extractRequested(var orderedItems, string baseName)

    readonly property bool _workbench: App.extractLayout !== "compact"

    ListModel { id: barModel }             // gewählte Seiten in Reihenfolge
    property var  _inSel: ({})             // "fileIdx:page" -> true
    // Gewählte Seiten JE DATEI fortlaufend mitzählen: ohne den Zähler durchzählte `_selCountFor` die ganze Datei,
    // und weil er an `_selRev` hängt, bei JEDER Auswahl erneut - "ganzes PDF wählen" wurde damit quadratisch.
    property var  _selCounts: ({})         // fileIdx -> Anzahl
    property int  _selRev: 0               // Binding-Refresh (Kachel-Optik)
    property int  _selBatch: 0
    property int  _activeFileIdx: 0        // links gewähltes PDF (rechtes Raster)

    property bool   _ctrlDown: false
    property string _hoverPath: ""
    property int    _hoverPage: -1
    property bool   _previewOk: false
    property int    _prevRev: 0

    function openWith(fileList, activeIdx) {
        files = fileList || []
        barModel.clear()
        _inSel = ({})
        _selCounts = ({})
        _selBatch = 0
        _selRev++
        _activeFileIdx = (activeIdx > 0 && activeIdx < files.length) ? activeIdx : 0
        _clearHover()
        dlg.open()
    }

    function _key(fileIdx, page) { return fileIdx + ":" + page }
    function _touchSel()  { if (_selBatch === 0) _selRev++ }
    function _beginBatch() { _selBatch++ }
    function _endBatch()   { if (--_selBatch <= 0) { _selBatch = 0; _selRev++ } }
    function _isSelected(fileIdx, page) {
        void _selRev
        return _inSel[_key(fileIdx, page)] === true
    }
    function _fileName(fileIdx) {
        var f = files[fileIdx]
        if (!f) return ""
        return f.name !== undefined ? f.name : String(f.path).split("/").pop()
    }
    function _addSel(fileIdx, page) {
        var k = _key(fileIdx, page)
        if (_inSel[k]) return
        _inSel[k] = true
        _selCounts[fileIdx] = (_selCounts[fileIdx] || 0) + 1
        barModel.append({ path: files[fileIdx].path, page: page,
                          fileIdx: fileIdx, fileName: _fileName(fileIdx) })
        _touchSel()
    }
    function _removeSel(fileIdx, page) {
        var k = _key(fileIdx, page)
        if (!_inSel[k]) return
        delete _inSel[k]
        _selCounts[fileIdx] = Math.max(0, (_selCounts[fileIdx] || 0) - 1)
        for (var i = 0; i < barModel.count; i++) {
            var it = barModel.get(i)
            if (it.fileIdx === fileIdx && it.page === page) { barModel.remove(i, 1); break }
        }
        _touchSel()
    }
    function _toggleSel(fileIdx, page) {
        if (_isSelected(fileIdx, page)) _removeSel(fileIdx, page)
        else                            _addSel(fileIdx, page)
    }
    function _addWholePdf(fileIdx) {
        var f = files[fileIdx]
        if (!f) return
        _beginBatch()
        for (var p = 0; p < f.pageCount; p++) _addSel(fileIdx, p)
        _endBatch()
    }
    //  Rückwärts EINMAL durch die Leiste statt je Seite von vorn zu suchen -
    //  aus n Durchläufen über die Leiste wird einer.
    function _removeWholePdf(fileIdx) {
        var f = files[fileIdx]
        if (!f) return
        _beginBatch()
        for (var i = barModel.count - 1; i >= 0; i--) {
            var it = barModel.get(i)
            if (it.fileIdx !== fileIdx) continue
            delete _inSel[_key(fileIdx, it.page)]
            barModel.remove(i, 1)
        }
        _selCounts[fileIdx] = 0
        _endBatch()
    }
    function _selCountFor(fileIdx) {
        void _selRev
        return _selCounts[fileIdx] || 0
    }
    function _removeBarAt(idx) {
        if (idx < 0 || idx >= barModel.count) return
        var it = barModel.get(idx)
        delete _inSel[_key(it.fileIdx, it.page)]
        _selCounts[it.fileIdx] = Math.max(0, (_selCounts[it.fileIdx] || 0) - 1)
        barModel.remove(idx, 1)
        _selRev++
    }

    function _clearHover() { _hoverPath = ""; _hoverPage = -1; _previewOk = false }
    function _setHover(path, page) {
        if (_hoverPath === path && _hoverPage === page) return
        _hoverPath = path; _hoverPage = page; _previewOk = false
        _maybePreview()
    }
    function _maybePreview() {
        if (!_ctrlDown || _hoverPath === "" || _hoverPage < 0) return
        var px = Math.round(Math.max(dlg.width, dlg.height) * 0.8 * Screen.devicePixelRatio)
        PdfThumbs.requestLargePreview(_hoverPath, _hoverPage, px)
    }

    function _submit(name) {
        var items = []
        if (_workbench) {
            for (var i = 0; i < barModel.count; i++) {
                var it = barModel.get(i)
                items.push({ path: it.path, page: it.page })
            }
        } else {
            for (var f = 0; f < files.length; f++)
                for (var p = 0; p < files[f].pageCount; p++)
                    if (_inSel[_key(f, p)]) items.push({ path: files[f].path, page: p })
        }
        dlg.close()
        if (items.length > 0) root.extractRequested(items, name)
    }

    Connections {
        target: PdfThumbs
        function onLargePreviewReady(path, page) {
            if (path === root._hoverPath && page === root._hoverPage) {
                root._prevRev++; root._previewOk = true
            }
        }
    }

    component PageThumb: Item {
        id: thumb
        property string path: ""
        property int    page: 0
        property int    _docId: 0
        property int    _rev: 0
        Component.onCompleted: thumb._docId = PdfThumbs.ensureDocument(thumb.path, thumb.page)
        Connections {
            target: PdfThumbs
            function onPageReady(d, p) { if (d === thumb._docId && p === thumb.page) thumb._rev++ }
        }
        Image {
            anchors.fill: parent
            asynchronous: true; cache: false
            fillMode: Image.PreserveAspectFit
            source: thumb._docId > 0
                    ? "image://pdfthumb/" + thumb._docId + "/" + thumb.page + "?r=" + thumb._rev
                    : ""
        }
    }

    Popup {
        id: dlg
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width:  Math.min(root.width  - 40, Math.max(640, root.width  * 0.94))
        height: Math.min(root.height - 40, Math.max(460, root.height * 0.94))
        padding: 14
        closePolicy: Popup.CloseOnEscape
        onClosed: { root._ctrlDown = false; root._clearHover() }

        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder; border.width: 1
            radius: 6
        }

        contentItem: FocusScope {
            id: dlgContentRoot
            focus: true
            Keys.onPressed: (e) => {
                if (e.key === Qt.Key_Control) { root._ctrlDown = true; root._maybePreview() }
            }
            Keys.onReleased: (e) => {
                if (e.key === Qt.Key_Control && !e.isAutoRepeat) root._ctrlDown = false
            }

            Column {
                anchors.fill: parent
                spacing: 10

                Text {
                    text: root.titleText
                    color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true
                }
                Text {
                    width: parent.width
                    text: App.uiText(App.language, root._workbench ? "ExtractWorkHint" : "ExtractHintCtrl")
                    color: App.themeTextMuted; font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Loader {
                    id: bodyLoader
                    width: parent.width
                    height: parent.height - y - footer.height - 10
                    sourceComponent: root._workbench ? workbenchComp : compactComp
                }

                Item {
                    id: footer
                    width: parent.width
                    height: 32
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.uiText(App.language, "ExtractSelectedCount").arg(barModel.count)
                        color: App.themeTextPrimary; font.pixelSize: 12
                    }
                    Row {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        spacing: 8
                        Button {
                            height: 28; font.pixelSize: 12
                            text: App.uiText(App.language, "SettingsCancel")
                            onClicked: dlg.close()
                        }
                        Button {
                            height: 28; font.pixelSize: 12
                            enabled: barModel.count > 0 && (!root.askName || !PdfExtract.busy)
                            text: root.confirmText.length > 0
                                  ? root.confirmText
                                  : App.uiText(App.language, "ExtractCreateBtn")
                            palette.buttonText: enabled ? App.themeAccent : App.themeTextMuted
                            onClicked: root.askName ? nameDlg.openFor(root.defaultName,
                                                                      root.requireName)
                                                    : root._submit("")
                        }
                    }
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width:  parent.width  * 0.8
                height: parent.height * 0.8
                visible: root._ctrlDown && root._previewOk
                color: Qt.rgba(0, 0, 0, 0.85)
                border.color: App.themeBorder; border.width: 1
                radius: 8; z: 100
                Image {
                    anchors.fill: parent; anchors.margins: 10
                    asynchronous: true; cache: false
                    fillMode: Image.PreserveAspectFit
                    source: parent.visible ? "image://pdfthumb/preview?r=" + root._prevRev : ""
                }
            }
        }
    }

    Component {
        id: workbenchComp
        Item {
            anchors.fill: parent

            readonly property int barH: 108
            readonly property bool barHasItems: barModel.count > 0
            readonly property int barZoneH: barHasItems ? barH : 46

            Item {
                id: topZone
                anchors { left: parent.left; right: parent.right; top: parent.top }
                anchors.bottom: barWrap.top
                anchors.bottomMargin: 10

                Rectangle {
                    id: leftPane
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: Math.max(190, parent.width * 0.26)
                    color: Qt.rgba(1, 1, 1, 0.02)
                    border.color: App.themeBorder; radius: 6
                    clip: true

                    Column {
                        anchors.fill: parent; anchors.margins: 8; spacing: 6
                        Text {
                            text: App.uiText(App.language, "ExtractWorkPdfs")
                            color: App.themeTextMuted; font.pixelSize: 11; font.bold: true
                        }
                        Item {
                            id: pdfListWrap
                            width: parent.width
                            height: parent.height - y

                        ListView {
                            id: pdfList
                            anchors.fill: parent
                            clip: true
                            model: root.files
                            spacing: 4
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            // Die vorausgewählte Datei muss auch SICHTBAR sein. Beim ersten Öffnen entsteht dieser Baum erst nach
                            // `openWith` (Popup-Inhalt lazy), später steht er schon - deshalb beides.
                            currentIndex: root._activeFileIdx
                            onCurrentIndexChanged: positionViewAtIndex(currentIndex,
                                                                       ListView.Contain)
                            Component.onCompleted: positionViewAtIndex(root._activeFileIdx,
                                                                       ListView.Contain)

                            delegate: Rectangle {
                                id: fileRow
                                required property int index
                                required property var modelData
                                width: pdfList.width
                                height: 46
                                radius: 5
                                readonly property bool active: root._activeFileIdx === index
                                readonly property int selN: root._selCountFor(index)
                                readonly property bool full: modelData.pageCount > 0
                                                             && selN === modelData.pageCount
                                color: active
                                       ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                       : (rowHover.hovered
                                          ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                                    App.themeTextPrimary.b, 0.06)
                                          : "transparent")
                                border.color: full ? App.themeAccent
                                              : (active ? App.themeAccent : App.themeBorder)
                                border.width: (full || active) ? 2 : 1

                                HoverHandler { id: rowHover }

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8; anchors.rightMargin: 6
                                    spacing: 8
                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 16; height: 16; radius: 8
                                        color: fileRow.full ? App.themeAccent
                                               : (fileRow.selN > 0
                                                  ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                            App.themeAccent.b, 0.35)
                                                  : "transparent")
                                        border.color: fileRow.selN > 0 ? App.themeAccent : App.themeTextMuted
                                        border.width: 1
                                        Text {
                                            anchors.centerIn: parent
                                            visible: fileRow.full
                                            text: "✓"; color: "white"
                                            font.pixelSize: 11; font.bold: true
                                        }
                                    }
                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - 16 - 8 - toggleAllBtn.width - 8
                                        Text {
                                            width: parent.width
                                            text: fileRow.modelData.name !== undefined
                                                  ? fileRow.modelData.name
                                                  : String(fileRow.modelData.path).split("/").pop()
                                            color: fileRow.active ? App.themeTextPrimary : App.themeTextPrimary
                                            font.pixelSize: 12
                                            elide: Text.ElideMiddle
                                        }
                                        Text {
                                            text: fileRow.selN > 0
                                                  ? (fileRow.selN + " / " + fileRow.modelData.pageCount)
                                                  : (fileRow.modelData.pageCount + " " +
                                                     App.uiText(App.language, "ExtractWorkPages"))
                                            color: fileRow.selN > 0 ? App.themeAccent : App.themeTextMuted
                                            font.pixelSize: 10
                                        }
                                    }
                                    Rectangle {
                                        id: toggleAllBtn
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 24; height: 24; radius: 5
                                        color: taHover.hovered ? App.themeCard : "transparent"
                                        border.color: App.themeBorder; border.width: 1
                                        Text {
                                            anchors.centerIn: parent
                                            text: fileRow.full ? "−" : "+"
                                            color: App.themeTextPrimary; font.pixelSize: 15; font.bold: true
                                        }
                                        HoverHandler { id: taHover }
                                        TapHandler {
                                            onTapped: fileRow.full ? root._removeWholePdf(fileRow.index)
                                                                   : root._addWholePdf(fileRow.index)
                                        }
                                    }
                                }

                                TapHandler {
                                    onTapped: root._activeFileIdx = fileRow.index
                                }

                                // Ziehen = ganzes PDF in die Auswahlleiste. DragHandler
                                // (target: null) kollidiert nicht mit dem TapHandler
                                // oben (Tap = laden, Drag ab Schwelle = hineinziehen).
                                DragHandler {
                                    id: fileDrag
                                    target: null
                                    onActiveChanged: {
                                        if (active) fileGhost.dragFileIdx = fileRow.index
                                        else        fileGhost.Drag.drop()
                                    }
                                }
                                Item {
                                    id: fileGhost
                                    parent: dlgContentRoot
                                    visible: fileDrag.active
                                    width: 120; height: 34
                                    property int dragFileIdx: -1
                                    property string dragKind: "file"
                                    readonly property point _p: dlgContentRoot.mapFromItem(
                                        fileRow, fileDrag.centroid.position.x, fileDrag.centroid.position.y)
                                    x: _p.x - width / 2
                                    y: _p.y - height / 2
                                    Drag.active: fileDrag.active
                                    Drag.hotSpot.x: width/2; Drag.hotSpot.y: height/2
                                    Drag.keys: ["extract-file"]
                                    Rectangle {
                                        anchors.fill: parent; radius: 5
                                        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.9)
                                        Text {
                                            anchors.centerIn: parent
                                            text: "PDF ->"; color: "white"
                                            font.pixelSize: 12; font.bold: true
                                        }
                                    }
                                }
                            }
                        }

                            NumberAnimation {
                                id: listScroll
                                target: pdfList; property: "contentY"
                                duration: 180; easing.type: Easing.OutCubic
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                z: 2
                                onWheel: function(wheel) {
                                    var maxY = Math.max(0, pdfList.contentHeight - pdfList.height)
                                    if (maxY <= 0) { wheel.accepted = true; return }
                                    var raw = (wheel.angleDelta.y !== 0)
                                              ? (wheel.angleDelta.y / 120) * (pdfList.height * 0.5)
                                              : wheel.pixelDelta.y * 1.6
                                    var base = listScroll.running ? listScroll.to : pdfList.contentY
                                    var tgt = Math.max(0, Math.min(base - raw, maxY))
                                    listScroll.from = pdfList.contentY
                                    listScroll.to = tgt
                                    listScroll.restart()
                                    wheel.accepted = true
                                }
                            }
                        }
                    }
                }

                Item {
                    id: rightPane
                    anchors { left: leftPane.right; leftMargin: 10; right: parent.right
                              top: parent.top; bottom: parent.bottom }

                    Column {
                        anchors.fill: parent; spacing: 6
                        Text {
                            text: root._fileName(root._activeFileIdx) + " · "
                                  + App.uiText(App.language, "ExtractWorkPages")
                            color: App.themeTextMuted; font.pixelSize: 11; font.bold: true
                            elide: Text.ElideMiddle; width: parent.width
                        }

                        Item {
                            width: parent.width
                            height: parent.height - y

                            GridView {
                                id: pageGrid
                                anchors.fill: parent
                                anchors.rightMargin: pageVsb.width
                                clip: true
                                cellWidth:  App.tileWidth + 14
                                cellHeight: App.tileHeight + 30
                                boundsBehavior: Flickable.StopAtBounds
                                model: {
                                    var f = root.files[root._activeFileIdx]
                                    return f ? f.pageCount : 0
                                }

                                delegate: Item {
                                    id: pcell
                                    required property int index
                                    width: pageGrid.cellWidth
                                    height: pageGrid.cellHeight
                                    readonly property int fileIdx: root._activeFileIdx
                                    readonly property string path: {
                                        var f = root.files[fileIdx]; return f ? f.path : ""
                                    }
                                    readonly property bool selected: root._isSelected(fileIdx, index)

                                    Rectangle {
                                        id: ptile
                                        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
                                        width: App.tileWidth; height: App.tileHeight
                                        color: "white"; radius: 3
                                        border.color: pcell.selected && App.extractSelectStyle === "frame"
                                                      ? App.themeAccent : App.themeBorder
                                        border.width: pcell.selected && App.extractSelectStyle === "frame" ? 3 : 1

                                        PageThumb {
                                            anchors.fill: parent; anchors.margins: 3
                                            path: pcell.path; page: pcell.index
                                        }
                                        Rectangle {
                                            anchors.fill: parent; radius: 3
                                            visible: pcell.selected && App.extractSelectStyle === "overlay"
                                            color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                           App.themeAccent.b, 0.35)
                                            Text {
                                                anchors { top: parent.top; right: parent.right; margins: 4 }
                                                text: "✓"; color: "white"; font.pixelSize: 15; font.bold: true
                                            }
                                        }

                                        MouseArea {
                                            id: pma
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            // Klick = an-/abwählen, Ziehen ab Schwelle = Seite in die Leiste; kein `drag.target`, das bewegte den Geist
                                            // im falschen Koordinatensystem. `preventStealing`: der umgebende GridView darf den Grab nicht übernehmen,
                                            // sonst würde ein Seiten-Zug zum Scrollen. Auf Leerflächen flickt der Grid weiterhin normal.
                                            preventStealing: true
                                            property bool dragging: false
                                            property bool suppressClick: false
                                            property real _px: 0
                                            property real _py: 0
                                            onPressed: (m) => { dragging = false; _px = m.x; _py = m.y }
                                            onPositionChanged: (m) => {
                                                // `positionChanged` feuert bei `hoverEnabled` AUCH ohne gedrückte Taste - ohne diesen Guard folgte der
                                                // Zieh-Geist schon beim bloßen Hovern und blieb kleben.
                                                if (!pressed) return
                                                if (!dragging && (Math.abs(m.x - _px) > 8 || Math.abs(m.y - _py) > 8))
                                                    dragging = true
                                                if (dragging) {
                                                    var pt = dlgContentRoot.mapFromItem(pma, m.x, m.y)
                                                    pageGhost.x = pt.x - pageGhost.width / 2
                                                    pageGhost.y = pt.y - pageGhost.height / 2
                                                }
                                            }
                                            onReleased: {
                                                if (dragging) { pageGhost.Drag.drop(); suppressClick = true }
                                                dragging = false
                                            }
                                            //  Wird der Grab gestohlen (Flick/anderer
                                            //  Handler), feuert KEIN onReleased -> sonst
                                            //  bliebe der Geist „in der Luft" hängen.
                                            onCanceled: dragging = false
                                            onClicked: {
                                                if (suppressClick) { suppressClick = false; return }
                                                root._toggleSel(pcell.fileIdx, pcell.index)
                                            }
                                            onEntered: root._setHover(pcell.path, pcell.index)
                                            onExited: {
                                                if (root._hoverPath === pcell.path && root._hoverPage === pcell.index)
                                                    root._clearHover()
                                            }
                                        }
                                    }
                                    Text {
                                        anchors { top: ptile.bottom; topMargin: 3
                                                  horizontalCenter: parent.horizontalCenter }
                                        text: App.uiText(App.language, "ExtractPageShort").arg(pcell.index + 1)
                                        color: pcell.selected ? App.themeAccent : App.themeTextMuted
                                        font.pixelSize: 10
                                    }

                                    Item {
                                        id: pageGhost
                                        // An `pma.pressed` gekoppelt: sobald die Taste los ist, verschwindet der Geist sofort - auch wenn
                                        // `onReleased`/`onCanceled` einmal ausbleibt.
                                        visible: pma.pressed && pma.dragging
                                        parent: dlgContentRoot
                                        width: 54; height: 68
                                        property string dragKind: "page"
                                        property int dragFileIdx: pcell.fileIdx
                                        property int dragPage: pcell.index
                                        Drag.active: pma.pressed && pma.dragging
                                        Drag.hotSpot.x: width/2; Drag.hotSpot.y: height/2
                                        Drag.keys: ["extract-page"]
                                        Rectangle {
                                            anchors.fill: parent; color: "white"
                                            border.color: App.themeAccent; border.width: 2; radius: 3
                                            PageThumb { anchors.fill: parent; anchors.margins: 2
                                                        path: pcell.path; page: pcell.index }
                                        }
                                    }
                                }
                            }

                            NumberAnimation {
                                id: gridScroll
                                target: pageGrid; property: "contentY"
                                duration: 180; easing.type: Easing.OutCubic
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                z: 2
                                onWheel: function(wheel) {
                                    var maxY = Math.max(0, pageGrid.contentHeight - pageGrid.height)
                                    if (maxY <= 0) { wheel.accepted = true; return }
                                    var raw = (wheel.angleDelta.y !== 0)
                                              ? (wheel.angleDelta.y / 120) * (pageGrid.height * 0.45)
                                              : wheel.pixelDelta.y * 1.6
                                    var base = gridScroll.running ? gridScroll.to : pageGrid.contentY
                                    var tgt = Math.max(0, Math.min(base - raw, maxY))
                                    gridScroll.from = pageGrid.contentY
                                    gridScroll.to = tgt
                                    gridScroll.restart()
                                    wheel.accepted = true
                                }
                            }

                            ScrollBar {
                                id: pageVsb
                                anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
                                orientation: Qt.Vertical
                                policy: ScrollBar.AsNeeded
                                size: pageGrid.visibleArea.heightRatio
                                position: pageGrid.visibleArea.yPosition
                                active: pageGrid.movingVertically || pressed || hovered
                                z: 3
                                onPositionChanged: {
                                    if (pressed) pageGrid.contentY = position * pageGrid.contentHeight
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: barWrap
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: barZoneH
                color: externalBarDrop.containsDrag
                       ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.12)
                       : Qt.rgba(1, 1, 1, 0.03)
                border.color: externalBarDrop.containsDrag ? App.themeAccent : App.themeBorder
                border.width: externalBarDrop.containsDrag ? 2 : 1
                radius: 6
                clip: true
                Behavior on height { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

                Column {
                    anchors.fill: parent; anchors.margins: 8; spacing: 4
                    Text {
                        visible: barHasItems
                        text: App.uiText(App.language, "ExtractWorkOrder")
                        color: App.themeTextMuted; font.pixelSize: 10; font.bold: true
                    }

                    Item {
                        id: barContainer
                        width: parent.width
                        height: parent.height - y

                        // Aufnahme-Zone für externe Drops und Rückfang für Leisten-Items (verhindert versehentliches Entfernen beim
                        // Loslassen über der Leiste). IMMER aktiv, damit auch das ERSTE Element hineingezogen werden kann.
                        DropArea {
                            id: externalBarDrop
                            anchors.fill: parent
                            keys: ["extract-page", "extract-file", "bar-item"]
                            onDropped: (drop) => {
                                var s = drop.source
                                if (!s) return
                                if (s.dragKind === "page") root._addSel(s.dragFileIdx, s.dragPage)
                                else if (s.dragKind === "file") root._addWholePdf(s.dragFileIdx)
                                drop.accept()
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !barHasItems
                            text: "⤓  " + App.uiText(App.language, "ExtractWorkDropHint")
                            color: externalBarDrop.containsDrag ? App.themeAccent : App.themeTextMuted
                            font.pixelSize: 12
                        }

                        ListView {
                            id: barList
                            anchors.fill: parent
                            visible: barHasItems
                            orientation: ListView.Horizontal
                            spacing: 6
                            clip: true
                            model: barModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                            moveDisplaced: Transition { NumberAnimation { properties: "x,y"; duration: 160 } }

                            readonly property int itemW: 62

                            delegate: MouseArea {
                                id: barCellMA
                                required property int index
                                required property string path
                                required property int page
                                required property int fileIdx
                                required property string fileName
                                width: barList.itemW
                                height: barList.height
                                property bool held: false
                                drag.target: held ? barCard : undefined
                                hoverEnabled: true

                                property bool isBarItem: true
                                property string dragKind: "bar-item"

                                onPressAndHold: held = true
                                onPressed: held = true
                                onReleased: {
                                    // Loslasspunkt VOR dem Zurücksetzen von `held` bestimmen - danach springt die Karte per ParentChange an ihren
                                    // Listenplatz. Geometrie statt `barCard.Drag.drop()`: das lieferte durch das Umparenten unzuverlässig
                                    // `Qt.IgnoreAction`, ein INNERHALB der Leiste losgelassenes Item wurde dann entfernt statt umsortiert.
                                    var cx = barCard.x + barCard.width / 2
                                    var cy = barCard.y + barCard.height / 2
                                    var wasHeld = held
                                    held = false
                                    if (!wasHeld) return
                                    var inside = cx >= 0 && cx <= barContainer.width
                                                 && cy >= -14 && cy <= barContainer.height + 14
                                    if (!inside) root._removeBarAt(barCellMA.index)
                                }
                                onCanceled: held = false
                                onEntered: root._setHover(barCellMA.path, barCellMA.page)
                                onExited: {
                                    if (root._hoverPath === barCellMA.path && root._hoverPage === barCellMA.page)
                                        root._clearHover()
                                }

                                Rectangle {
                                    id: barCard
                                    width: barList.itemW - 4
                                    height: barList.height - 4
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: 4
                                    color: "white"
                                    border.color: App.themeAccent; border.width: 1.5
                                    scale: barCellMA.held ? 1.06 : 1.0
                                    opacity: barCellMA.held ? 0.9 : 1.0
                                    z: barCellMA.held ? 5 : 0
                                    Behavior on scale { NumberAnimation { duration: 90 } }

                                    Drag.active: barCellMA.held
                                    Drag.source: barCellMA
                                    Drag.hotSpot.x: width/2; Drag.hotSpot.y: height/2
                                    Drag.keys: ["bar-item"]

                                    states: State {
                                        when: barCellMA.held
                                        ParentChange { target: barCard; parent: barContainer }
                                        AnchorChanges { target: barCard
                                            anchors.horizontalCenter: undefined
                                            anchors.verticalCenter: undefined }
                                    }

                                    Column {
                                        anchors.fill: parent; anchors.margins: 3; spacing: 1
                                        PageThumb {
                                            width: parent.width
                                            height: parent.height - ordLbl.height - 2
                                            path: barCellMA.path; page: barCellMA.page
                                        }
                                        Text {
                                            id: ordLbl
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: (barCellMA.index + 1) + "·"
                                                  + App.uiText(App.language, "ExtractPageShort").arg(barCellMA.page + 1)
                                            color: App.themeTextMuted; font.pixelSize: 8
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Rectangle {
                                        anchors { top: parent.top; right: parent.right; margins: 1 }
                                        width: 14; height: 14; radius: 7
                                        color: Qt.rgba(0, 0, 0, 0.55)
                                        visible: barCellMA.containsMouse && !barCellMA.held
                                        Text { anchors.centerIn: parent; text: "×"
                                               color: "white"; font.pixelSize: 10; font.bold: true }
                                        TapHandler { onTapped: root._removeBarAt(barCellMA.index) }
                                    }
                                }

                                // Umsortieren: zieht ein anderes Leisten-Item über dieses, wandert es live hierher. Über Entfernen oder
                                // Behalten entscheidet `onReleased` geometrisch - kein `onDropped` nötig.
                                DropArea {
                                    anchors.fill: parent
                                    keys: ["bar-item"]
                                    onEntered: (drag) => {
                                        var from = drag.source.index
                                        var to = barCellMA.index
                                        if (from !== to && from >= 0) barModel.move(from, to, 1)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: compactComp
        Item {
            anchors.fill: parent

            property var _flat: {
                var m = []
                for (var i = 0; i < root.files.length; i++)
                    for (var p = 0; p < root.files[i].pageCount; p++)
                        m.push({ path: root.files[i].path, page: p,
                                 fileName: root._fileName(i), fileIdx: i })
                return m
            }

            GridView {
                id: grid
                anchors.fill: parent
                clip: true
                model: parent._flat
                cellWidth:  App.tileWidth + 14
                cellHeight: App.tileHeight + 34
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: cell
                    required property var modelData
                    width: grid.cellWidth; height: grid.cellHeight
                    readonly property bool selected: root._isSelected(cell.modelData.fileIdx, cell.modelData.page)

                    Rectangle {
                        id: tile
                        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
                        width: App.tileWidth; height: App.tileHeight
                        color: "white"; radius: 3
                        border.color: cell.selected && App.extractSelectStyle === "frame"
                                      ? App.themeAccent : App.themeBorder
                        border.width: cell.selected && App.extractSelectStyle === "frame" ? 3 : 1

                        PageThumb {
                            anchors.fill: parent; anchors.margins: 3
                            path: cell.modelData.path; page: cell.modelData.page
                        }
                        Rectangle {
                            anchors.fill: parent; radius: 3
                            visible: cell.selected && App.extractSelectStyle === "overlay"
                            color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.35)
                            Text {
                                anchors { top: parent.top; right: parent.right; margins: 4 }
                                text: "✓"; color: "white"; font.pixelSize: 15; font.bold: true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root._toggleSel(cell.modelData.fileIdx, cell.modelData.page)
                            onEntered: root._setHover(cell.modelData.path, cell.modelData.page)
                            onExited: {
                                if (root._hoverPath === cell.modelData.path && root._hoverPage === cell.modelData.page)
                                    root._clearHover()
                            }
                        }
                    }
                    Text {
                        anchors { top: tile.bottom; topMargin: 4; horizontalCenter: parent.horizontalCenter }
                        width: grid.cellWidth - 10
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        color: cell.selected ? App.themeAccent : App.themeTextMuted
                        font.pixelSize: 10
                        text: (root.files.length > 1 ? cell.modelData.fileName + " · " : "")
                              + App.uiText(App.language, "ExtractPageShort").arg(cell.modelData.page + 1)
                    }
                }
            }

            NumberAnimation {
                id: cGridScroll
                target: grid; property: "contentY"
                duration: 180; easing.type: Easing.OutCubic
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                z: 2
                onWheel: function(wheel) {
                    var maxY = Math.max(0, grid.contentHeight - grid.height)
                    if (maxY <= 0) { wheel.accepted = true; return }
                    var raw = (wheel.angleDelta.y !== 0)
                              ? (wheel.angleDelta.y / 120) * (grid.height * 0.45)
                              : wheel.pixelDelta.y * 1.6
                    var base = cGridScroll.running ? cGridScroll.to : grid.contentY
                    var tgt = Math.max(0, Math.min(base - raw, maxY))
                    cGridScroll.from = grid.contentY
                    cGridScroll.to = tgt
                    cGridScroll.restart()
                    wheel.accepted = true
                }
            }
        }
    }

    PdfExtractNameDialog {
        id: nameDlg
        onAccepted: (name) => root._submit(name)
    }
}
