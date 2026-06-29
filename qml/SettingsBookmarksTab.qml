pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ── Lesezeichen: gespeicherte Ordner verwalten ───────────────────────────────
Item {
    id: root

    // savedFolders ist ein reaktives Q_PROPERTY (NOTIFY savedFoldersChanged).
    readonly property var folders: App.savedFolders

    // Index für den Lösch-Dialog (Hinzufügen/Bearbeiten liegt in BookmarkEditDialog).
    property int deleteIndex: -1

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: App.uiText(App.language, "SettingsBookHint")
                color: App.themeTextMuted; font.pixelSize: 11
            }
            Button {
                text: App.uiText(App.language, "SettingsBookBtnAdd")
                highlighted: true
                onClicked: bookmarkEditDialog.openAdd()
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
                                text: App.uiText(App.language, "BookmarkEdit")
                                onClicked: bookmarkEditDialog.openEdit(bmRow.index, bmRow.modelData.name, bmRow.modelData.path)
                            }
                            Button {
                                text: App.uiText(App.language, "BookmarkDelete")
                                onClicked: { root.deleteIndex = bmRow.index; deleteDialog.open() }
                            }
                        }
                    }
                }

                Text {
                    visible: root.folders.length === 0
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: App.uiText(App.language, "SettingsBookEmpty")
                    color: App.themeTextMuted
                    padding: 16
                }
            }
        }
    }

    // ── Hinzufügen / Bearbeiten (geteilt mit ApplicationShell) ───────────────
    BookmarkEditDialog { id: bookmarkEditDialog }

    Dialog {
        id: deleteDialog
        title: App.uiText(App.language, "SettingsBookDeleteTitle")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        onAccepted: App.removeBookmark(root.deleteIndex)
        contentItem: Text {
            text: App.uiText(App.language, "SettingsBookDeleteConfirm")
            color: App.themeTextPrimary; wrapMode: Text.WordWrap; width: 280
        }
    }
}
