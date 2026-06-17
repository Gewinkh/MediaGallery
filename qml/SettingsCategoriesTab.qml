pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ── Kategorien: rekursiver Baum-Editor ───────────────────────────────────────
Item {
    id: tab

    property var treeModel: []
    function refresh() { treeModel = Tags.categoriesTree() }

    Component.onCompleted: refresh()
    Connections {
        target: Tags
        function onCategoriesChanged() { tab.refresh() }
        function onTagsChanged()       { tab.refresh() }
    }

    // ── Prompt-API (von den Knoten aufgerufen) ───────────────────────────────
    property string pId: ""
    property string pName: ""

    function promptNewRoot()            { pId = "";  pName = ""; npName.text = ""; npColor.selectedColor = App.themeAccent; newCatDialog.open() }
    function promptAddSub(id)           { pId = id;  pName = ""; spName.text = ""; spColor.selectedColor = App.themeAccent; subCatDialog.open() }
    function promptRename(id, name)     { pId = id;  pName = name; rnField.text = name; renameDialog.open() }
    function promptDelete(id, name)     { pId = id;  pName = name; deleteDialog.open() }
    function promptUniform(id, color)   { pId = id;  uColor.selectedColor = color; uInherit.checked = false; uniformDialog.open() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Kategorien und Unterkategorien verwalten. Tags lassen sich Kategorien zuordnen.")
                color: App.themeTextMuted
                font.pixelSize: 11
            }
            Button {
                text: qsTr("+ Neue Kategorie")
                highlighted: true
                onClicked: tab.promptNewRoot()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: App.themeBorder }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            Column {
                width: tab.width
                spacing: 4

                Repeater {
                    model: tab.treeModel
                    delegate: Loader {
                        id: rootLoader
                        required property var modelData
                        width: parent ? parent.width : 0
                        source: Qt.resolvedUrl("SettingsCategoryNode.qml")
                        onLoaded: {
                            item.node  = Qt.binding(function() { return rootLoader.modelData })
                            item.depth = 0
                            item.tab   = tab
                        }
                    }
                }

                Text {
                    visible: tab.treeModel.length === 0
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Noch keine Kategorien vorhanden.")
                    color: App.themeTextMuted
                    padding: 16
                }
            }
        }
    }

    // ── Dialoge ──────────────────────────────────────────────────────────────
    Dialog {
        id: newCatDialog
        title: qsTr("Neue Kategorie")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (npName.text.trim().length > 0)
                        Tags.addRootCategory(npName.text.trim(), npColor.selectedColor, false)
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        contentItem: RowLayout {
            spacing: 10
            TextField { id: npName; Layout.preferredWidth: 220; placeholderText: qsTr("Name"); color: App.themeTextPrimary }
            ColorPicker { id: npColor; width: 34; height: 24; showAlpha: false; selectedColor: App.themeAccent }
        }
    }

    Dialog {
        id: subCatDialog
        title: qsTr("Neue Unterkategorie")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (spName.text.trim().length > 0)
                        Tags.addSubcategory(tab.pId, spName.text.trim(), spColor.selectedColor, false)
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        contentItem: RowLayout {
            spacing: 10
            TextField { id: spName; Layout.preferredWidth: 220; placeholderText: qsTr("Name"); color: App.themeTextPrimary }
            ColorPicker { id: spColor; width: 34; height: 24; showAlpha: false; selectedColor: App.themeAccent }
        }
    }

    Dialog {
        id: renameDialog
        title: qsTr("Kategorie umbenennen")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: if (rnField.text.trim().length > 0)
                        Tags.renameCategory(tab.pId, rnField.text.trim())
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        contentItem: TextField { id: rnField; implicitWidth: 240; color: App.themeTextPrimary }
    }

    Dialog {
        id: deleteDialog
        title: qsTr("Kategorie löschen")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Tags.deleteCategory(tab.pId)
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        contentItem: Text {
            text: qsTr("Kategorie „%1\" und alle Unterkategorien löschen?").arg(tab.pName)
            color: App.themeTextPrimary; wrapMode: Text.WordWrap; width: 300
        }
    }

    Dialog {
        id: uniformDialog
        title: qsTr("Einheitsfarbe setzen")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: Tags.setCategoryUniformColor(tab.pId, true, uColor.selectedColor, uInherit.checked)
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                spacing: 10
                Label { text: qsTr("Farbe:"); color: App.themeTextPrimary }
                ColorPicker { id: uColor; width: 34; height: 24; showAlpha: false; selectedColor: App.themeAccent }
            }
            CheckBox {
                id: uInherit
                text: qsTr("An Unterkategorien vererben")
                contentItem: Text {
                    text: parent.text; color: App.themeTextPrimary
                    leftPadding: parent.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
