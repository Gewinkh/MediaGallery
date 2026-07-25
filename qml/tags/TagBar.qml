pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  TagBar.qml — Tags einer Datei anzeigen/bearbeiten (ersetzt TagBar/TagPill aus
//  TagWidget.cpp). Reine QML-Items; Mutationen via Bridge (App.addTagToFile/
//  removeTagFromFile). Tag-Auswahl-Dropdown speist sich aus Tags.allTags().
//
//  Reaktiv: lokale tagModel wird bei fileName-Wechsel und Tags.tagsChanged neu
//  aus App.tagsForFile() gezogen (App.* sind Funktionen, keine Bindings).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: bar

    property string fileName: ""
    property bool   editable: true
    property var    tagModel: []

    implicitHeight: flow.implicitHeight

    function refresh() { tagModel = fileName.length > 0 ? App.tagsForFile(fileName) : [] }

    onFileNameChanged: refresh()
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
                color: Qt.rgba(App.tagColor(pill.modelData).r, App.tagColor(pill.modelData).g,
                               App.tagColor(pill.modelData).b, 0.22)
                border.color: App.tagColor(pill.modelData)
                border.width: 1

                Row {
                    id: pillRow
                    anchors.centerIn: parent
                    spacing: 5
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4
                        color: App.tagColor(pill.modelData)
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
                            onTapped: { App.removeTagFromFile(bar.fileName, pill.modelData); bar.refresh() }
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
                    if (t.length > 0) { App.addTagToFile(bar.fileName, t); bar.refresh() }
                    text = ""
                }
            }

            ToolButton {
                text: "\u25BE"
                width: 26
                onClicked: tagMenu.open()

                Menu {
                    id: tagMenu
                    Repeater {
                        model: App.allTags()
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData
                            onTriggered: { App.addTagToFile(bar.fileName, modelData); bar.refresh() }
                        }
                    }
                }
            }
        }
    }
}
