pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

Item {
    id: root
    implicitHeight: tagsCol.implicitHeight

    property var tagList: []
    function refresh() {
        tagList = Tags.allTags()
        zeilen.fill(tagList, tagList.join("\n"), t => ({ name: t }))
        farbStand++
    }
    //  Eine Farbaenderung meldet sich als tagsChanged; die stehenden Zeilen lesen ihre Farbe darum neu.
    property int farbStand: 0
    function farbe(t) { return farbStand >= 0 ? Tags.tagColor(t) : "" }
    //  Zeilen in Haeppchen: alle 403 am Stueck hielten das erste Oeffnen des Reiters auf.
    LazyRows { id: zeilen; viewHeight: root.Window.height; rowHeight: 42 }

    Component.onCompleted: refresh()
    Connections {
        target: Tags
        function onTagsChanged() { root.refresh() }
    }

    ColumnLayout {
        id: tagsCol
        width: parent.width
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

        // Jeder Ordner führt seine Verschlagwortung in einer eigenen Datei; ohne diesen Schalter blieb ein gelöschter
        // Tag in jedem Unterordner stehen. Standard ist AN.
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

        //  Keine eigene Rollfläche: gerollt wird der ganze Reiter.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: zeilen.model
                delegate: Rectangle {
                    id: tagRow
                    required property string name
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
                            color: root.farbe(tagRow.name)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tagRow.name
                            color: root.farbe(tagRow.name)
                            font.pixelSize: 13
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        ColorPicker {
                            width: 30; height: 22
                            title: App.uiText(App.language, "SettingsTagColorTitle")
                            showAlpha: false
                            selectedColor: root.farbe(tagRow.name)
                            onColorPicked: (c) => Tags.setTagColor(tagRow.name, c)
                        }
                        ToolButton {
                            text: "\u270E"   // Stift
                            ToolTip.text: App.uiText(App.language, "CatPanelRename")
                            ToolTip.visible: hovered
                            onClicked: { root.renameTarget = tagRow.name; renameField.text = tagRow.name; renameDialog.open() }
                        }
                        ToolButton {
                            text: "\u2715"   // ✕
                            ToolTip.text: App.uiText(App.language, "BookmarkDelete")
                            ToolTip.visible: hovered
                            onClicked: { root.deleteTarget = tagRow.name; deleteDialog.open() }
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
        // `focus: true` am Dialog UND ein `Keys`-Handler am `contentItem` - beides nötig: `standardButtons` allein
        // wertet `Return` nicht aus (Fokus auf der DialogButtonBox ohne Vorgabe-Knopf), und `Keys` am Dialog feuert nie.
        focus: true
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }

        // s. SettingsBookmarksTab: feste implicitWidth des contentItem bricht
        // die Rückkopplung Dialog.implicitWidth ↔ Textumbruch.
        contentItem: Item {
            focus: true
            Keys.onReturnPressed: function(e) { deleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { deleteDialog.accept(); e.accepted = true }
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
}
