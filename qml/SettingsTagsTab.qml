pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ── Tags: globale Tag-Verwaltung ─────────────────────────────────────────────
Item {
    id: root

    property var tagList: []
    function refresh() { tagList = App.allTags() }

    Component.onCompleted: refresh()
    Connections {
        target: Tags
        function onTagsChanged() { root.refresh() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Tags umbenennen, einfärben oder löschen. Änderungen wirken global.")
                color: App.themeTextMuted
                font.pixelSize: 11
            }
            Button {
                text: qsTr("+ Neuer Tag")
                highlighted: true
                onClicked: { newTagName.text = ""; newTagColor.selectedColor = App.themeAccent; newTagDialog.open() }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: root.width
                spacing: 4

                Repeater {
                    model: root.tagList
                    delegate: Rectangle {
                        id: tagRow
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 38
                        radius: 6
                        color: rowHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.02)

                        HoverHandler { id: rowHover }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            spacing: 8

                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: App.tagColor(tagRow.modelData)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: tagRow.modelData
                                color: App.tagColor(tagRow.modelData)
                                font.pixelSize: 13
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            ColorPicker {
                                width: 30; height: 22
                                title: qsTr("Tag-Farbe")
                                showAlpha: false
                                selectedColor: App.tagColor(tagRow.modelData)
                                onColorPicked: (c) => Tags.setTagColor(tagRow.modelData, c)
                            }
                            ToolButton {
                                text: "\u270E"   // Stift
                                ToolTip.text: qsTr("Umbenennen")
                                ToolTip.visible: hovered
                                onClicked: { root.renameTarget = tagRow.modelData; renameField.text = tagRow.modelData; renameDialog.open() }
                            }
                            ToolButton {
                                text: "\u2715"   // ✕
                                ToolTip.text: qsTr("Löschen")
                                ToolTip.visible: hovered
                                onClicked: { root.deleteTarget = tagRow.modelData; deleteDialog.open() }
                            }
                        }
                    }
                }

                Text {
                    visible: root.tagList.length === 0
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Noch keine Tags vorhanden.")
                    color: App.themeTextMuted
                    padding: 16
                }
            }
        }
    }

    // ── Dialoge ──────────────────────────────────────────────────────────────
    property string renameTarget: ""
    property string deleteTarget: ""

    Dialog {
        id: newTagDialog
        title: qsTr("Neuer Tag")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (newTagName.text.trim().length > 0)
                        Tags.createTag(newTagName.text.trim(), newTagColor.selectedColor)
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }

        contentItem: RowLayout {
            spacing: 10
            TextField {
                id: newTagName
                Layout.preferredWidth: 220
                placeholderText: qsTr("Tag-Name")
                color: App.themeTextPrimary
            }
            ColorPicker { id: newTagColor; width: 34; height: 24; showAlpha: false; selectedColor: App.themeAccent }
        }
    }

    Dialog {
        id: renameDialog
        title: qsTr("Tag umbenennen")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (renameField.text.trim().length > 0)
                        Tags.renameTag(root.renameTarget, renameField.text.trim())
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }

        contentItem: TextField {
            id: renameField
            implicitWidth: 240
            color: App.themeTextPrimary
        }
    }

    Dialog {
        id: deleteDialog
        title: qsTr("Tag löschen")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Tags.deleteTag(root.deleteTarget)
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }

        contentItem: Text {
            text: qsTr("Tag „%1\" wirklich löschen? Er wird aus allen Dateien entfernt.").arg(root.deleteTarget)
            color: App.themeTextPrimary
            wrapMode: Text.WordWrap
            width: 300
        }
    }
}
