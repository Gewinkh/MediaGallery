pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// ── Tags: globale Tag-Verwaltung ─────────────────────────────────────────────
Item {
    id: root

    property var tagList: []
    function refresh() { tagList = Tags.allTags() }

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
                text: App.uiText(App.language, "SettingsTagsHintNew")
                color: App.themeTextMuted
                font.pixelSize: 11
            }
            Button {
                text: App.uiText(App.language, "SettingsTagsBtnNew")
                highlighted: true
                onClicked: { newTagName.text = ""; newTagColor.selectedColor = App.themeAccent; newTagDialog.open() }
            }
        }

        //  ── Wie weit reicht „Tag löschen"? ──────────────────────────────────
        //  Jeder Ordner führt seine Verschlagwortung in einer eigenen Datei;
        //  ohne diesen Schalter blieb ein gelöschter Tag in jedem Unterordner
        //  stehen. Standard ist AN.
        CheckBox {
            id: subfolderBox
            Layout.fillWidth: true
            text: App.uiText(App.language, "SettingsTagDeleteSubfolders")
            checked: App.deleteTagsInSubfolders
            onToggled: App.deleteTagsInSubfolders = checked
            contentItem: Text {
                text: subfolderBox.text; color: App.themeTextPrimary
                leftPadding: subfolderBox.indicator.width + 6
                verticalAlignment: Text.AlignVCenter
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            text: App.uiText(App.language, "SettingsTagDeleteSubfoldersHint")
            color: App.themeTextMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        ScrollView {
            id: tagsScroll
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
                                color: Tags.tagColor(tagRow.modelData)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: tagRow.modelData
                                color: Tags.tagColor(tagRow.modelData)
                                font.pixelSize: 13
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            ColorPicker {
                                width: 30; height: 22
                                title: App.uiText(App.language, "SettingsTagColorTitle")
                                showAlpha: false
                                selectedColor: Tags.tagColor(tagRow.modelData)
                                onColorPicked: (c) => Tags.setTagColor(tagRow.modelData, c)
                            }
                            ToolButton {
                                text: "\u270E"   // Stift
                                ToolTip.text: App.uiText(App.language, "CatPanelRename")
                                ToolTip.visible: hovered
                                onClicked: { root.renameTarget = tagRow.modelData; renameField.text = tagRow.modelData; renameDialog.open() }
                            }
                            ToolButton {
                                text: "\u2715"   // ✕
                                ToolTip.text: App.uiText(App.language, "BookmarkDelete")
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
                    text: App.uiText(App.language, "SettingsTagsEmpty")
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
        title: App.uiText(App.language, "CatPanelNewTag")
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
                placeholderText: App.uiText(App.language, "SettingsTagsName")
                color: App.themeTextPrimary
            }
            ColorPicker { id: newTagColor; width: 34; height: 24; showAlpha: false; selectedColor: App.themeAccent }
        }
    }

    Dialog {
        id: renameDialog
        title: App.uiText(App.language, "FilterTagRenamePrompt")
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
        title: App.uiText(App.language, "SettingsTagDelete")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Tags.deleteTag(root.deleteTarget)
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }

        // s. SettingsBookmarksTab: feste implicitWidth des contentItem bricht
        // die Rückkopplung Dialog.implicitWidth ↔ Textumbruch.
        contentItem: Item {
            implicitWidth: 300
            implicitHeight: tagDelText.implicitHeight
            Text {
                id: tagDelText
                width: parent.width
                text: App.uiText(App.language, "SettingsTagsDeleteConfirm").arg(root.deleteTarget)
                color: App.themeTextPrimary
                wrapMode: Text.WordWrap
            }
        }
    }

    //  Weiches, schnelles Mausrad-Scrollen (Galerie-Muster) statt der festen
    //  60 px je Rastung von `Flickable`.
    SmoothWheelArea { flickable: tagsScroll.contentItem }
}
