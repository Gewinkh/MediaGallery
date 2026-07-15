import QtQuick
import QtQuick.Window
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  PdfPageSelectDialog.qml — Seitenauswahl der PDF-Extraktion (Rasteransicht).
//
//  EINE Komponente für BEIDE Modi (Anforderung „gleiches Verhalten"):
//   • in-PDF   (PdfSurface):        files = [ { path, pageCount } ]
//   • global   (ApplicationShell):  files = alle PDFs des aktuellen Ordners
//     (aus PdfExtract.scanFolder, in Ordnerreihenfolge)
//
//  Bedienung laut Anforderung:
//   • EINFACHER LINKSKLICK wählt eine Seite aus bzw. ab — ohne Zusatztaste.
//   • Strg+Hover zeigt eine große Vorschau (~80 % des Dialogs) der Seite;
//     sie verschwindet beim Loslassen von Strg. Gerendert wird asynchron über
//     PdfThumbs.requestLargePreview (Einzelslot, RAM-gedeckelt).
//   • Kachelgröße = Galerie-Kacheln (App.tileWidth/tileHeight).
//   • Hervorhebung je Einstellung App.extractSelectStyle:
//     „frame" = Akzent-Rahmen (Standard) · „overlay" = Akzent-Überlagerung.
//   • Die erzeugte PDF folgt IMMER der Originalreihenfolge: je Datei werden
//     die gewählten Indizes aufsteigend übergeben; die Dateireihenfolge ist
//     die übergebene (Ordner-)Reihenfolge.
//
//  Thumbnails: bestehende PdfThumbs-Pipeline (ensureDocument ist idempotent,
//  LRU deckelt den RAM; image://pdfthumb/<docId>/<page>?r=<rev> mit
//  Cache-Buster über pageReady) — kein zweiter Render-Pfad.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    // [{ path, pageCount, name? }] — Reihenfolge = Ausgabereihenfolge der Dateien.
    property var    files: []
    property bool   requireName: false     // global: Name ist Pflicht
    property string titleText: ""
    property string defaultName: ""        // Platzhalter des Namensdialogs

    // jobs = [{ path, pages:[aufsteigend] }] nur Dateien mit Auswahl.
    signal extractRequested(var jobs, string baseName)

    // ── interner Zustand ──────────────────────────────────────────────────────
    property var  _model: []               // flach: {path, page, fileName, fileIdx}
    property var  _selected: ({})          // "fileIdx:page" → true
    property int  _selCount: 0
    property int  _selRev: 0               // Binding-Refresh der Kachel-Optik
    property bool _ctrlDown: false
    property string _hoverPath: ""
    property int    _hoverPage: -1
    property bool   _previewOk: false      // Großvorschau der Hover-Seite liegt vor
    property int    _prevRev: 0            // Cache-Buster der Vorschau-source

    function openWith(fileList) {
        files = fileList || []
        var m = []
        for (var i = 0; i < files.length; i++) {
            var name = files[i].name !== undefined
                     ? files[i].name
                     : String(files[i].path).split("/").pop()
            for (var p = 0; p < files[i].pageCount; p++)
                m.push({ path: files[i].path, page: p,
                         fileName: name, fileIdx: i })
        }
        _model = m
        _selected = ({})
        _selCount = 0
        _selRev++
        _clearHover()
        dlg.open()
    }

    function _key(fileIdx, page) { return fileIdx + ":" + page }
    function _isSelected(fileIdx, page) {
        void _selRev                       // Abhängigkeit fürs Re-Binding
        return _selected[_key(fileIdx, page)] === true
    }
    function _toggle(fileIdx, page) {
        var k = _key(fileIdx, page)
        if (_selected[k]) { delete _selected[k]; _selCount-- }
        else              { _selected[k] = true;  _selCount++ }
        _selRev++
    }
    function _clearHover() {
        _hoverPath = ""
        _hoverPage = -1
        _previewOk = false
    }
    function _setHover(path, page) {
        if (_hoverPath === path && _hoverPage === page) return
        _hoverPath = path
        _hoverPage = page
        _previewOk = false
        _maybePreview()
    }
    // Vorschau nur anfordern, wenn Strg gehalten wird UND eine Seite gehovert
    // ist — sonst bliebe der Preview-Worker grundlos beschäftigt.
    function _maybePreview() {
        if (!_ctrlDown || _hoverPath === "" || _hoverPage < 0) return
        var px = Math.round(Math.max(dlg.width, dlg.height) * 0.8
                            * Screen.devicePixelRatio)
        PdfThumbs.requestLargePreview(_hoverPath, _hoverPage, px)
    }

    Connections {
        target: PdfThumbs
        function onLargePreviewReady(path, page) {
            if (path === root._hoverPath && page === root._hoverPage) {
                root._prevRev++
                root._previewOk = true
            }
        }
    }

    Popup {
        id: dlg
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width:  Math.min(root.width  - 40, Math.max(560, root.width  * 0.92))
        height: Math.min(root.height - 40, Math.max(420, root.height * 0.92))
        padding: 14
        closePolicy: Popup.CloseOnEscape
        onClosed: { root._ctrlDown = false; root._clearHover() }

        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder; border.width: 1
            radius: 6
        }

        // Strg-Zustand über die Tastatur des fokussierten Popups verfolgen
        // (deterministischer als Modifier aus Hover-Events).
        contentItem: FocusScope {
            focus: true
            Keys.onPressed: (e) => {
                if (e.key === Qt.Key_Control) {
                    root._ctrlDown = true
                    root._maybePreview()
                }
            }
            Keys.onReleased: (e) => {
                if (e.key === Qt.Key_Control && !e.isAutoRepeat)
                    root._ctrlDown = false   // Vorschau verschwindet (s. visible)
            }

            Column {
                anchors.fill: parent
                spacing: 10

                // ── Kopf: Titel + Hinweis ────────────────────────────────────
                Text {
                    text: root.titleText
                    color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true
                }
                Text {
                    width: parent.width
                    text: App.uiText(App.language, "ExtractHintCtrl")
                    color: App.themeTextMuted; font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                // ── Seitenraster (Kachelgröße wie das MainWindow) ────────────
                Item {
                    id: gridWrap
                    width: parent.width
                    height: parent.height - y - footer.height - 10

                    GridView {
                        id: grid
                        anchors.fill: parent
                        clip: true
                        model: root._model
                        cellWidth:  App.tileWidth + 14
                        cellHeight: App.tileHeight + 34
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Item {
                            id: cell
                            required property var modelData
                            width: grid.cellWidth
                            height: grid.cellHeight

                            // Cache-Buster je Kachel (Muster der Thumbnail-Leiste).
                            property int docId: 0
                            property int rev: 0
                            readonly property bool selected:
                                root._isSelected(cell.modelData.fileIdx,
                                                 cell.modelData.page)

                            // Idempotent: nur der ERSTE Aufruf je Dokument startet
                            // den Render-Task; startPage priorisiert das Sichtbare.
                            Component.onCompleted:
                                docId = PdfThumbs.ensureDocument(cell.modelData.path,
                                                                 cell.modelData.page)
                            Connections {
                                target: PdfThumbs
                                function onPageReady(d, p) {
                                    if (d === cell.docId && p === cell.modelData.page)
                                        cell.rev++
                                }
                            }

                            // Weiße Seitenkachel + Vorschau.
                            Rectangle {
                                id: tile
                                anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
                                width: App.tileWidth; height: App.tileHeight
                                color: "white"
                                border.color: cell.selected && App.extractSelectStyle === "frame"
                                              ? App.themeAccent : App.themeBorder
                                border.width: cell.selected && App.extractSelectStyle === "frame"
                                              ? 3 : 1
                                radius: 3

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    asynchronous: true
                                    cache: false
                                    fillMode: Image.PreserveAspectFit
                                    source: cell.docId > 0
                                            ? "image://pdfthumb/" + cell.docId + "/"
                                              + cell.modelData.page + "?r=" + cell.rev
                                            : ""
                                }

                                // Hervorhebung „Überlagerung" (Einstellung).
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 3
                                    visible: cell.selected && App.extractSelectStyle === "overlay"
                                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                   App.themeAccent.b, 0.35)
                                    Text {
                                        anchors { top: parent.top; right: parent.right; margins: 4 }
                                        text: "\u2713"
                                        color: "white"; font.pixelSize: 15; font.bold: true
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    // Linksklick wählt aus/ab (Standard von
                                    // `acceptedButtons`) — OHNE Zusatztaste.
                                    // Modifier werden bewusst NICHT geprüft: Strg
                                    // hält evtl. gerade die Großvorschau offen,
                                    // ein Klick dabei soll trotzdem normal wählen.
                                    onClicked: root._toggle(cell.modelData.fileIdx,
                                                            cell.modelData.page)
                                    onEntered: root._setHover(cell.modelData.path,
                                                              cell.modelData.page)
                                    onExited: {
                                        if (root._hoverPath === cell.modelData.path
                                            && root._hoverPage === cell.modelData.page)
                                            root._clearHover()
                                    }
                                }
                            }

                            // Beschriftung: global mit Dateiname, in-PDF nur „S. N".
                            Text {
                                anchors { top: tile.bottom; topMargin: 4
                                          horizontalCenter: parent.horizontalCenter }
                                width: grid.cellWidth - 10
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideMiddle
                                color: cell.selected ? App.themeAccent : App.themeTextMuted
                                font.pixelSize: 10
                                text: (root.files.length > 1
                                       ? cell.modelData.fileName + " \u00B7 " : "")
                                      + App.uiText(App.language, "ExtractPageShort")
                                            .arg(cell.modelData.page + 1)
                            }
                        }
                    }

                    // ── Mausrad: identisches Verhalten wie die Galerie ────────────
                    //  Das Flickable-Standardverhalten scrollt in winzigen Schritten
                    //  ohne Nachlauf (zäh). Die GalleryView löst das bereits: EINE
                    //  `MouseArea(NoButton)` über dem Raster fängt die Wheel-Events
                    //  zuverlässig ab (ein WheelHandler bzw. das GridView selbst
                    //  verschluckt sie sonst) und animiert `contentY` — NoButton
                    //  lässt Klicks und Hover ungehindert zu den Kacheln durch.
                    //  Ein Rad-Klick = 45 % Viewporthöhe; laufende Animationen
                    //  rechnen ab ihrem ZIEL weiter (`gridScroll.to`), damit
                    //  schnelles Weiterdrehen sich addiert statt zu stocken.
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
                            // Strg gehört hier der Großvorschau (Strg+Hover) — anders
                            // als in der Galerie NICHT die Kachelgröße zoomen: das
                            // wäre eine globale Einstellung aus einem transienten
                            // Dialog heraus. Strg+Rad scrollt also einfach normal.
                            var maxY = Math.max(0, grid.contentHeight - grid.height)
                            if (maxY <= 0) { wheel.accepted = true; return }
                            var raw = (wheel.angleDelta.y !== 0)
                                      ? (wheel.angleDelta.y / 120) * (grid.height * 0.45)
                                      : wheel.pixelDelta.y * 1.6
                            var base = gridScroll.running ? gridScroll.to : grid.contentY
                            var tgt = Math.max(0, Math.min(base - raw, maxY))
                            gridScroll.from = grid.contentY
                            gridScroll.to = tgt
                            gridScroll.restart()
                            wheel.accepted = true
                        }
                    }
                }

                // ── Fußzeile: Zähler + Aktionen ──────────────────────────────
                Item {
                    id: footer
                    width: parent.width
                    height: 32

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.uiText(App.language, "ExtractSelectedCount")
                                  .arg(root._selCount)
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
                            enabled: root._selCount > 0 && !PdfExtract.busy
                            text: App.uiText(App.language, "ExtractCreateBtn")
                            palette.buttonText: enabled ? App.themeAccent
                                                        : App.themeTextMuted
                            onClicked: nameDlg.openFor(root.defaultName,
                                                       root.requireName)
                        }
                    }
                }
            }

            // ── Große Vorschau (Strg+Hover, ~80 % des Dialogs) ───────────────
            Rectangle {
                anchors.centerIn: parent
                width:  parent.width  * 0.8
                height: parent.height * 0.8
                visible: root._ctrlDown && root._previewOk
                color: Qt.rgba(0, 0, 0, 0.85)
                border.color: App.themeBorder; border.width: 1
                radius: 8
                z: 10
                Image {
                    anchors.fill: parent
                    anchors.margins: 10
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    // source erst NACH largePreviewReady der gehoverten Seite —
                    // der Einzelslot des Providers enthält dann genau diese.
                    source: parent.visible
                            ? "image://pdfthumb/preview?r=" + root._prevRev : ""
                }
            }
        }
    }

    // Namensdialog (geteilt): leerer Name = Default (nur wenn nicht Pflicht).
    PdfExtractNameDialog {
        id: nameDlg
        onAccepted: (name) => {
            var jobs = []
            for (var i = 0; i < root.files.length; i++) {
                var pages = []
                for (var p = 0; p < root.files[i].pageCount; p++)
                    if (root._selected[root._key(i, p)] === true)
                        pages.push(p)          // aufsteigend = Originalreihenfolge
                if (pages.length > 0)
                    jobs.push({ path: root.files[i].path, pages: pages })
            }
            dlg.close()
            if (jobs.length > 0)
                root.extractRequested(jobs, name)
        }
    }
}
