pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"
import "../table"

// HTML ueber den eigenen Renderer, ohne Chromium und ohne Skripte. Die Flickable traegt nur das Rollen,
// gezeichnet und markiert wird in der HtmlView darunter.
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0
    property bool   paneActive: true

    property bool _findOpen: false

    //  Verweis auf eine andere Datei: der Viewer blaettert hin, wenn sie in der Galerie liegt (wie im Markdown).
    signal openFileRequested(string path)

    //  Ueber root, nicht view.source: die Zuweisung loeste die Bindung, die naechste Datei kam nie an.
    function release() { root.source = "" }

    //  PDF-Vertrag des Viewers (s. TextSurface); „Original" legt fuer den Bildschirm, der Druckstil mit @media print.
    readonly property string pdfKind: "html"
    readonly property bool pdfBusy: view.exporting
    property int pdfPages: -1
    property string pdfPreviewPath: ""
    signal pdfFinished(bool ok, string target, string error)
    function pdfCount(nativ, quer, alle) {
        root.pdfPages = -1
        view.countPdfPages(!nativ, quer)
    }
    function pdfPreview(nativ, quer, alle) {
        root.pdfPreviewPath = ""
        root.pdfPages = -1
        view.previewPdf(!nativ, quer)
    }
    function pdfExport(nativ, quer, alle, von, bis, seiten) {
        view.exportPdf(view.pdfTarget(), !nativ, quer, von, bis, seiten || [])
    }
    Connections {
        target: view
        function onPdfPagesCounted(n) { root.pdfPages = n }
        function onPdfPreviewReady(pfad, n) { root.pdfPreviewPath = pfad; root.pdfPages = n }
        function onPdfExportFinished(ok, target, error) { root.pdfFinished(ok, target, error) }
    }
    //  Strg+F ist ein Schalter wie in Tabelle und PDF.
    function oeffneSuche() {
        if (root._findOpen) { suchBalken.schliessen(); return }
        root._findOpen = true
        suchBalken.oeffnen()
    }

    HtmlView {
        id: view
        objectName: "htmlView"
        anchors { fill: parent; topMargin: root.topInset + hinweis.height; bottomMargin: root.bottomInset }
        source: root.source
        contentY: flick.contentY
        selectionColor: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.35)
        onScrollRequested: (y) => flick.contentY = Math.max(0, Math.min(y, flick.contentHeight - flick.height))
        onLinkActivated: (url) => Qt.openUrlExternally(url)
        onOpenFileRequested: (p) => root.openFileRequested(p)
    }

    //  Nicht interaktiv: Druecken und Ziehen gehen an die HtmlView darunter (Markieren).
    Flickable {
        id: flick
        anchors.fill: view
        contentWidth: width
        contentHeight: Math.max(height, view.contentHeight)
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    }

    SmoothWheelArea { flickable: flick }

    //  Nur die rechte Taste; Markieren mit links bleibt bei der HtmlView.
    MouseArea {
        anchors.fill: view
        acceptedButtons: Qt.RightButton
        onClicked: kontext.popup()
    }
    ThemedMenu {
        id: kontext
        objectName: "htmlCtxMenu"
        MenuItem {
            text: App.uiText(App.language, "ViewerHtmlCopy")
            enabled: view.hasSelection
            onTriggered: view.copy()
        }
        MenuItem {
            text: App.uiText(App.language, "ViewerHtmlSelectAll")
            onTriggered: view.selectAll()
        }
    }

    Shortcut {
        sequence: "Ctrl+F"
        enabled: root.visible && root.paneActive
        onActivated: root.oeffneSuche()
    }
    //  `sequences`: StandardKey.Copy hat mehrere Folgen (Strg+C, Strg+Einfg).
    Shortcut {
        sequences: [ StandardKey.Copy ]
        enabled: root.visible && root.paneActive && view.hasSelection
        onActivated: view.copy()
    }
    Shortcut {
        sequence: "Ctrl+A"
        enabled: root.visible && root.paneActive
        onActivated: view.selectAll()
    }

    TableFindBar {
        id: suchBalken
        visible: root._findOpen
        z: 6
        anchors { top: view.top; topMargin: 8; right: parent.right; rightMargin: 18 }
        ctl: view
        woerter: true
        fromRow: Math.floor(flick.contentY)
        onGeschlossen: root._findOpen = false
    }

    Rectangle {
        id: hinweis
        visible: view.hasScript
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset }
        height: visible ? 36 : 0
        color: App.themeBackground
        Label {
            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
            text: App.uiText(App.language, "ViewerHtmlScripts")
        }
        Button {
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            text: App.uiText(App.language, "ViewerOpenInBrowser")
            onClicked: Viewer.openExternally(root.source)
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: view.busy && view.contentHeight === 0
        visible: running
    }
}
