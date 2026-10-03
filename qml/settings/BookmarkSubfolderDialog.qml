pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// BookmarkSubfolderDialog - welche Unterordner einer an einen Ordner gebundenen Gruppe im Lesezeichen-Menue
// stehen. Eine Ebene je Ansicht, ein Klick auf den Namen geht tiefer; jeder Haken wirkt sofort.
Item {
    id: root
    objectName: "bookmarkSubfolderPicker"

    property string groupPath: ""
    property string groupName: ""
    property string rootFolder: ""
    property string _ordner: ""
    property var    _gewaehlt: []

    function openFor(gruppe, name, ordner) {
        root.groupPath = gruppe
        root.groupName = name
        root.rootFolder = ordner
        root._ordner = ordner
        root._lies()
        dlg.open()
    }
    function _lies() { root._gewaehlt = App.bookmarkedSubfolders(root.groupPath) }

    Connections {
        target: App
        function onSavedFoldersChanged() { if (dlg.opened) root._lies() }
    }

    FileBrowseModel {
        id: ordnerListe
        folder: root._ordner
        dirsOnly: true
        showHidden: false
    }

    Dialog {
        id: dlg
        objectName: "bookmarkSubfolderDialog"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 460
        height: 480
        padding: 16
        background: Rectangle {
            color: App.themeCard; radius: 10
            border.color: App.themeBorder; border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                text: App.uiText(App.language, "BookmarkPickTitle").arg(root.groupName)
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: App.uiText(App.language, "BookmarkPickHint")
                color: App.themeTextMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            //  Wo man gerade steht, relativ zum Gruppenordner; der Pfeil geht eine Ebene zurueck, nie darueber.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle {
                    readonly property bool kann: root._ordner !== root.rootFolder
                    implicitWidth: 26; implicitHeight: 24; radius: 5
                    opacity: kann ? 1.0 : 0.35
                    color: zurueckHover.hovered && kann ? App.themeBorder : "transparent"
                    border.color: App.themeBorder
                    DrawnIcon { anchors.centerIn: parent; name: "arrow-left"; size: 14; color: App.themeTextPrimary }
                    HoverHandler { id: zurueckHover }
                    TapHandler {
                        onTapped: if (parent.kann) {
                            const i = root._ordner.lastIndexOf("/")
                            root._ordner = root._ordner.substring(0, i)
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideLeft
                    text: root.groupName + root._ordner.substring(root.rootFolder.length)
                    color: App.themeTextPrimary
                    font.pixelSize: 12
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"
                border.color: App.themeBorder
                radius: 6
                clip: true

                Text {
                    anchors.centerIn: parent
                    visible: ordnerListe.count === 0 && !ordnerListe.loading
                    text: App.uiText(App.language, "BookmarkPickEmpty")
                    color: App.themeTextMuted
                    font.pixelSize: 12
                }
                ListView {
                    anchors { fill: parent; margins: 4 }
                    model: ordnerListe
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Item {
                        id: zeile
                        required property string name
                        required property string path
                        readonly property bool angehakt: root._gewaehlt.indexOf(zeile.path) >= 0
                        width: ListView.view.width
                        height: 30
                        Rectangle {
                            anchors.fill: parent
                            radius: 4
                            color: zeileHover.hovered ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                                App.themeAccent.b, 0.10) : "transparent"
                        }
                        HoverHandler { id: zeileHover }
                        RowLayout {
                            anchors { fill: parent; leftMargin: 4; rightMargin: 8 }
                            spacing: 6
                            CheckBox {
                                checked: zeile.angehakt
                                onToggled: App.setBookmarkSubfolder(root.groupPath, zeile.path, checked)
                            }
                            DrawnIcon { name: "folder"; size: 14; color: App.themeTextMuted }
                            Text {
                                Layout.fillWidth: true
                                text: zeile.name
                                elide: Text.ElideRight
                                color: App.themeTextPrimary
                                font.pixelSize: 12
                                TapHandler { onTapped: root._ordner = zeile.path }
                            }
                            DrawnIcon { name: "chevron-right"; size: 12; color: App.themeTextMuted }
                        }
                    }
                }
            }

            Rectangle {
                Layout.alignment: Qt.AlignRight
                implicitWidth: fertigText.implicitWidth + 24; implicitHeight: 30; radius: 6
                color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.28)
                border.color: App.themeAccent; border.width: 1
                Text {
                    id: fertigText
                    anchors.centerIn: parent
                    text: App.uiText(App.language, "SettingsOk")
                    color: App.themeTextPrimary; font.pixelSize: 12
                }
                TapHandler { onTapped: dlg.close() }
            }
        }
    }
}
