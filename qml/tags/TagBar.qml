pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  TagBar.qml - Tags einer Datei anzeigen/bearbeiten (ersetzt TagBar/TagPill aus
//  TagWidget.cpp). Reine QML-Items; Mutationen via Modell (mediaModel.addTag/
//  removeTagFromFile). Tag-Auswahl-Dropdown speist sich aus Tags.allTags().
//
//  Reaktiv: lokale tagModel wird bei fileName-Wechsel und Tags.tagsChanged neu
//  aus mediaModel.tagsOfFile() gezogen (Funktionen, keine Bindings).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: bar

    //  Der PFAD, nicht der Name: jede Datei führt ihre Zuordnungen im Sidecar
    //  IHRES Ordners. Der blanke Name traf immer den geöffneten Ordner - für
    //  eine Datei aus einem aufgeklappten Unterordner also das falsche.
    property string filePath: ""
    property bool   editable: true
    property var    tagModel: []

    implicitHeight: flow.implicitHeight

    function refresh() {
        tagModel = filePath.length > 0 ? mediaModel.tagsOfFile(filePath) : []
    }

    onFilePathChanged: refresh()
    Component.onCompleted: refresh()

    Connections {
        target: Tags
        function onTagsChanged() { bar.refresh() }
    }

    Flow {
        id: flow
        width: parent.width
        spacing: 6

        Repeater {
            model: bar.tagModel
            delegate: Rectangle {
                id: pill
                required property var modelData
                height: 22
                radius: 11
                width: pillRow.implicitWidth + 16
                //  Farbe erst aus dem offenen Ordner, dann aus den aufgeklappten
                //  Unterordnern: eine Datei von dort trägt ihre eigenen Tags,
                //  und deren Definition liegt in IHREM Sidecar.
                readonly property color pillCol: {
                    var c = mediaModel.visibleTagColor(pill.modelData)
                    return (c && c.a > 0) ? c : Tags.tagColor(pill.modelData)
                }
                color: Qt.rgba(pillCol.r, pillCol.g, pillCol.b, 0.22)
                border.color: pillCol
                border.width: 1

                Row {
                    id: pillRow
                    anchors.centerIn: parent
                    spacing: 5
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4
                        color: pill.pillCol
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: pill.modelData
                        color: App.themeTextPrimary
                        font.pixelSize: 11
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: bar.editable
                        text: "\u2715"
                        color: App.themeTextMuted
                        font.pixelSize: 10
                        TapHandler {
                            onTapped: { mediaModel.removeTag(bar.filePath, pill.modelData); bar.refresh() }
                        }
                    }
                }
            }
        }

        // ── Hinzufügen ────────────────────────────────────────────────────────
        Row {
            visible: bar.editable
            spacing: 4

            TextField {
                id: input
                width: 120
                placeholderText: "+ Tag"
                font.pixelSize: 11
                color: App.themeTextPrimary
                background: Rectangle {
                    color: App.themeCard; radius: 11
                    border.color: App.themeBorder; border.width: 1
                }
                onAccepted: {
                    var t = text.trim()
                    if (t.length > 0) { mediaModel.addTag(bar.filePath, t); bar.refresh() }
                    text = ""
                }
            }

            ToolButton {
                text: "\u25BE"
                width: 26
                onClicked: tagMenu.open()

                ThemedMenu {
                    id: tagMenu
                    Repeater {
                        model: Tags.allTags()
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData
                            onTriggered: { mediaModel.addTag(bar.filePath, modelData); bar.refresh() }
                        }
                    }
                }
            }
        }
    }
}
