pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// Gerenderte Markdown-Ansicht, nur lesen. Bearbeitet wird im Rohtext, zwischen beiden schaltet der Viewer um.
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0

    signal openFileRequested(string path)

    readonly property real _pad: 18
    // Lesbare Zeilenlaenge: breiter als rund 110 Zeichen wird Fliesstext muehsam.
    readonly property real _maxWidth: 980
    property real _keepY: -1
    // Code-Bloecke als Terminal: dunkler als die Seite, bei hellem Profil nur leicht abgesetzt.
    readonly property bool _hell: Editor.background.hslLightness > 0.5
    readonly property color _termBg: root._hell ? Qt.darker(Editor.background, 1.05)
                                                : Qt.darker(Editor.background, 1.5)
    readonly property color _termHead: Qt.tint(root._termBg, Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.07))
    readonly property color _termLine: Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.13)
    property string _shownSource: ""

    function release() { view.source = "" }
    function reload() {
        root._keepY = flick.contentY
        view.reload()
    }

    function _openLink(link) {
        const r = view.resolveLink(link)
        if (r.kind === "details") {
            root._keepY = flick.contentY
            view.toggleDetails(r.id)
        } else if (r.kind === "anchor") {
            root._scrollTo(r.position)
        } else if (r.kind === "file") {
            root.openFileRequested(r.path)
        } else if (r.kind === "url") {
            Qt.openUrlExternally(r.url)
        }
    }
    function _scrollTo(pos) {
        if (pos < 0) return
        const y = doc.y + doc.positionToRectangle(pos).y - 8
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, y))
    }
    function _scrollBy(dy) {
        flick.contentY = Math.max(0, Math.min(Math.max(0, flick.contentHeight - flick.height),
                                              flick.contentY + dy))
    }

    Rectangle { anchors.fill: parent; color: Editor.background }

    MarkdownView {
        id: view
        objectName: "markdownView"
        source: root.source
        document: doc.textDocument
        // Nach jedem neuen Dokument: bei derselben Datei die Stelle halten, bei einer anderen nach oben.
        onRevisionChanged: {
            const gleich = root._shownSource === view.source
            root._shownSource = view.source
            const ziel = gleich ? (root._keepY >= 0 ? root._keepY : flick.contentY) : 0
            root._keepY = -1
            Qt.callLater(function () {
                flick.contentY = Math.max(0, Math.min(Math.max(0, flick.contentHeight - flick.height), ziel))
            })
        }
    }

    Flickable {
        id: flick
        objectName: "markdownFlick"
        anchors { fill: parent; topMargin: root.topInset; bottomMargin: root.bottomInset }
        clip: true
        contentWidth: width
        contentHeight: doc.height + 2 * root._pad
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        //  Hinter der Textflaeche: die Rahmen im Dokument sind durchsichtig und halten nur den Platz.
        Rectangle {
            id: karte
            readonly property var box: view.frontMatterBox
            visible: karte.box.width !== undefined
            x: doc.x + (karte.visible ? karte.box.x : 0)
            y: doc.y + (karte.visible ? karte.box.y : 0)
            width: karte.visible ? karte.box.width : 0
            height: karte.visible ? karte.box.height : 0
            radius: 8
            color: Qt.tint(Editor.background, Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.035))
            border.width: 1
            border.color: root._termLine
            Rectangle {
                x: 12; y: 12
                width: 3
                height: karte.height - 24
                radius: 1.5
                color: Editor.colorFor("link")
            }
        }

        Repeater {
            model: view.codeBlocks
            delegate: Rectangle {
                id: kasten
                required property var modelData
                x: doc.x + kasten.modelData.x
                y: doc.y + kasten.modelData.y
                width: kasten.modelData.width
                height: kasten.modelData.height
                radius: 8
                color: root._termBg
                border.width: 1
                border.color: root._termLine

                // Oben rund, unten gerade: das zweite Rechteck deckt die unteren Rundungen der Leiste ab.
                Rectangle {
                    id: kopf
                    x: 1; y: 1
                    width: kasten.width - 2
                    height: kasten.modelData.header - 1
                    radius: 7
                    color: root._termHead
                }
                Rectangle { x: 1; y: kopf.height - 7; width: kopf.width; height: 8; color: root._termHead }
                Rectangle { x: 1; y: kasten.modelData.header; width: kopf.width; height: 1; color: root._termLine }
                // Feste, auf ganze Pixel gerundete Lage: in einer `Row` liefen die Punkte oben buendig mit der
                // hoeheren Beschriftung.
                Repeater {
                    model: ["#ff5f57", "#febc2e", "#28c840"]
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        x: 12 + index * 16
                        y: Math.round(kopf.y + (kopf.height - 10) / 2)
                        width: 10; height: 10; radius: 5
                        color: modelData
                        opacity: 0.85
                    }
                }
                Label {
                    x: 12 + 3 * 16 + 6
                    y: Math.round(kopf.y + (kopf.height - implicitHeight) / 2)
                    text: kasten.modelData.language
                    color: Editor.gutterText
                    font: App.fallbackFont("monospace", 11)
                }
            }
        }

        TextEdit {
            id: doc
            objectName: "markdownText"
            x: Math.max(root._pad, (flick.width - width) / 2)
            y: root._pad
            width: Math.max(40, Math.min(flick.width - 2 * root._pad, root._maxWidth))
            readOnly: true
            selectByMouse: true
            persistentSelection: true
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.RichText
            color: Editor.text
            selectionColor: Editor.selection
            selectedTextColor: Editor.text
            onLinkActivated: function (link) { root._openLink(link) }

            HoverHandler {
                cursorShape: doc.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
            }

            // Pfeile links/rechts bleiben beim Viewer (Dateiwechsel); alles Senkrechte rollt die Ansicht.
            Keys.onPressed: function (e) {
                const zeile = 40
                switch (e.key) {
                case Qt.Key_Down:     root._scrollBy(zeile); break
                case Qt.Key_Up:       root._scrollBy(-zeile); break
                case Qt.Key_PageDown: root._scrollBy(flick.height * 0.9); break
                case Qt.Key_PageUp:   root._scrollBy(-flick.height * 0.9); break
                case Qt.Key_Space:
                    root._scrollBy((e.modifiers & Qt.ShiftModifier ? -1 : 1) * flick.height * 0.9)
                    break
                case Qt.Key_Home:     flick.contentY = 0; break
                case Qt.Key_End:      root._scrollBy(flick.contentHeight); break
                default:              return
                }
                e.accepted = true
            }
        }

        //  Ueber der Textflaeche. Eine `MouseArea`, kein `TapHandler`: der liesse den Druck an die TextEdit durch,
        //  und die verwuerfe ihre Markierung.
        Repeater {
            model: view.codeBlocks
            delegate: Rectangle {
                id: knopf
                required property var modelData
                required property int index
                property bool kopiert: false
                objectName: "markdownCopy"
                x: doc.x + knopf.modelData.x + knopf.modelData.width - width - 6
                y: doc.y + knopf.modelData.y + (knopf.modelData.header - height) / 2
                width: 26; height: 22; radius: 5
                color: knopfMaus.containsMouse ? root._termLine : "transparent"

                DrawnIcon {
                    anchors.centerIn: parent
                    size: 15
                    name: knopf.kopiert ? "check" : "copy"
                    color: knopf.kopiert ? Editor.colorFor("string") : Editor.gutterText
                }
                MouseArea {
                    id: knopfMaus
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (view.copyCode(knopf.index)) {
                            knopf.kopiert = true
                            zurueck.restart()
                        }
                    }
                }
                Timer { id: zurueck; interval: 1400; onTriggered: knopf.kopiert = false }
                ToolTip.visible: knopfMaus.containsMouse
                ToolTip.delay: 500
                ToolTip.text: knopf.kopiert ? App.uiText(App.language, "MarkdownCopied")
                                            : App.uiText(App.language, "MarkdownCopyCode")
            }
        }
    }

    SmoothWheelArea { flickable: flick }

    BusyIndicator {
        anchors.centerIn: parent
        running: view.busy && !view.ready
        visible: running
    }

    Label {
        anchors.centerIn: parent
        visible: view.failed
        text: App.uiText(App.language, "MarkdownReadError")
        color: Editor.text
    }

    Rectangle {
        visible: view.truncated
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: root.bottomInset }
        height: truncLabel.implicitHeight + 10
        color: Editor.gutterBackground
        Label {
            id: truncLabel
            anchors.centerIn: parent
            text: App.uiText(App.language, "MarkdownTruncated")
            color: Editor.gutterText
            font.pixelSize: 12
        }
    }
}
