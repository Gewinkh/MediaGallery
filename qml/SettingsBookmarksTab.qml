pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import MediaGallery 1.0

// ── Lesezeichen: gespeicherte Ordner verwalten ───────────────────────────────
Item {
    id: root

    // savedFolders ist ein reaktives Q_PROPERTY (NOTIFY savedFoldersChanged).
    readonly property var folders: App.savedFolders

    // -1 = Hinzufügen, >=0 = Bearbeiten
    property int editIndex: -1

    function openAdd() {
        editIndex = -1
        nameField.text = ""
        pathField.text = ""
        editDialog.title = qsTr("Lesezeichen hinzufügen")
        editDialog.open()
    }
    function openEdit(index, name, path) {
        editIndex = index
        nameField.text = name
        pathField.text = path
        editDialog.title = qsTr("Lesezeichen bearbeiten")
        editDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: qsTr("Schnellzugriff auf häufig genutzte Ordner.")
                color: App.themeTextMuted; font.pixelSize: 11
            }
            Button {
                text: qsTr("+ Ordner hinzufügen")
                highlighted: true
                onClicked: root.openAdd()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: App.themeBorder }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: root.width
                spacing: 6

                Repeater {
                    model: root.folders
                    delegate: Rectangle {
                        id: bmRow
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 56
                        radius: 6
                        color: Qt.rgba(1, 1, 1, 0.03)
                        border.color: App.themeBorder

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    text: bmRow.modelData.name
                                    color: App.themeTextPrimary
                                    font.pixelSize: 13; font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: bmRow.modelData.path
                                    color: App.themeTextMuted
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
                                text: qsTr("Bearbeiten")
                                onClicked: root.openEdit(bmRow.index, bmRow.modelData.name, bmRow.modelData.path)
                            }
                            Button {
                                text: qsTr("Löschen")
                                onClicked: { root.editIndex = bmRow.index; deleteDialog.open() }
                            }
                        }
                    }
                }

                Text {
                    visible: root.folders.length === 0
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Noch keine Lesezeichen vorhanden.")
                    color: App.themeTextMuted
                    padding: 16
                }
            }
        }
    }

    // ── Hinzufügen / Bearbeiten ──────────────────────────────────────────────
    Dialog {
        id: editDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 460
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        onAccepted: {
            var p = pathField.text.trim()
            if (p.length === 0) return
            if (root.editIndex < 0) App.addBookmark(nameField.text.trim(), p)
            else                    App.updateBookmark(root.editIndex, nameField.text.trim(), p)
        }

        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Label { text: qsTr("Name:"); color: App.themeTextPrimary; Layout.preferredWidth: 60 }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Anzeigename (optional)")
                    color: App.themeTextPrimary
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Label { text: qsTr("Pfad:"); color: App.themeTextPrimary; Layout.preferredWidth: 60 }
                TextField {
                    id: pathField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Ordnerpfad")
                    color: App.themeTextPrimary
                }
                Button {
                    text: qsTr("Durchsuchen…")
                    onClicked: folderDialog.open()
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Ordner wählen")
        onAccepted: {
            var p = selectedFolder.toString()
            if (p.startsWith("file://")) p = decodeURIComponent(p.substring(7))
            pathField.text = p
        }
    }

    Dialog {
        id: deleteDialog
        title: qsTr("Lesezeichen löschen")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        onAccepted: App.removeBookmark(root.editIndex)
        contentItem: Text {
            text: qsTr("Dieses Lesezeichen wirklich entfernen?")
            color: App.themeTextPrimary; wrapMode: Text.WordWrap; width: 280
        }
    }
}
