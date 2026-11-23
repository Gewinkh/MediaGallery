pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Sieben Seiten; alle Werte laufen über die Singletons `App` und `Tags`, AppSettings bleibt unverändertes Backend.
// Im Shell Loader-gated gehalten (RAM): erst beim Öffnen instanziiert, beim Schließen wieder freigegeben.
Dialog {
    id: dlg
    //  Griff fuer die Pruefstaende - eine QML-`id` ist von aussen nicht auffindbar.
    objectName: "settingsDialog"
    title: App.uiText(App.language, "SettingsTitle")
    //  Beim Öffnen gilt die Hälfte, in der gerade gearbeitet wird; beim Schließen wieder der Fokus (−1).
    onOpened: App.setSettingsPaneIndex(App.paneCount > 1 ? App.focusedPaneIndex : -1)
    onClosed: App.setSettingsPaneIndex(-1)
    modal: true
    //  Kein Schließen per Klick daneben: der zweite Klick eines Doppelklicks traf sonst die Kachel dahinter und öffnete sie.
    closePolicy: Popup.CloseOnEscape
    //  Nie breiter/höher als das Fenster: sonst stehen Seitenleiste und Inhalt unerreichbar über den Rand hinaus.
    width: Math.min(1040, (Overlay.overlay ? Overlay.overlay.width : 1040) - 24)
    height: Math.min(740, (Overlay.overlay ? Overlay.overlay.height : 740) - 24)
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.NoButton
    padding: 0
    header: null

    readonly property var pageKeys: ["SettingsTabGeneral", "SettingsTabEditorShort", "SettingsTabViewLayout",
                                     "SettingsTabLabeling", "SettingsTabAudio", "SettingsTabDesign",
                                     "SettingsTabBookmarks"]

    background: Rectangle {
        color: App.themeBackground
        border.color: App.themeBorder
        radius: 10
    }

    contentItem: RowLayout {
        spacing: 0

        Item {
            id: side
            Layout.fillHeight: true
            Layout.preferredWidth: Math.min(222, dlg.width * 0.26)

            AccentSlash {
                id: titleSlash
                anchors { left: parent.left; leftMargin: 26; verticalCenter: titleText.verticalCenter }
            }
            Text {
                id: titleText
                anchors { left: titleSlash.right; leftMargin: 8; right: parent.right; rightMargin: 12
                          top: parent.top; topMargin: 26 }
                text: dlg.title
                color: App.themeTextPrimary
                font.pixelSize: 16
                elide: Text.ElideRight
            }

            //  Name und Methode bleiben die der früheren Reiterleiste: `bench_shell de` schaltet darüber die Seite.
            Column {
                id: nav
                objectName: "settingsTabBar"
                property int currentIndex: 0
                function setCurrentIndex(i: int) { nav.currentIndex = i }
                anchors { left: parent.left; right: parent.right; top: titleText.bottom
                          leftMargin: 14; rightMargin: 14; topMargin: 26 }
                spacing: 3

                Repeater {
                    model: dlg.pageKeys
                    delegate: Rectangle {
                        id: navItem
                        required property int index
                        required property string modelData
                        readonly property bool on: nav.currentIndex === navItem.index
                        width: nav.width
                        height: 40
                        radius: 7
                        color: navItem.on ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                                    App.themeTextPrimary.b, 0.07)
                                          : (navHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                                                        App.themeTextPrimary.b, 0.035)
                                                              : "transparent")
                        AccentSlash {
                            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                            visible: navItem.on
                        }
                        Text {
                            anchors { left: parent.left; leftMargin: 31; right: parent.right; rightMargin: 8
                                      verticalCenter: parent.verticalCenter }
                            text: App.uiText(App.language, navItem.modelData)
                            color: navItem.on || navHover.hovered ? App.themeTextPrimary : App.themeTextMuted
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                        HoverHandler { id: navHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: nav.currentIndex = navItem.index }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: App.themeBorder
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 68

                Text {
                    id: pageTitle
                    anchors { left: parent.left; leftMargin: 34; verticalCenter: parent.verticalCenter }
                    width: Math.min(implicitWidth, parent.width - 34 - closeBtn.width - 30
                                    - (paneChooser.visible ? paneChooser.width + 16 : 0))
                    text: App.uiText(App.language, dlg.pageKeys[nav.currentIndex])
                    color: App.themeTextPrimary
                    font.pixelSize: 22
                    elide: Text.ElideRight
                }

                // Tags, Kategorien und der Konverter arbeiten am SIDECAR eines Ordners; mit zwei Galerien nebeneinander
                // muss man sagen können, welcher gemeint ist. Ohne Teilung bleibt die Wahl weg.
                Row {
                    id: paneChooser
                    //  Nur die Verschlagwortung (3) gilt je Ordner; alles andere ist appweit.
                    visible: App.paneCount > 1 && nav.currentIndex === 3
                    anchors { right: closeBtn.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
                    spacing: 8

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.uiText(App.language, "SettingsPaneLabel")
                        color: App.themeTextMuted
                        font.pixelSize: 12
                    }
                    Repeater {
                        model: App.panes
                        delegate: Rectangle {
                            id: paneChip
                            required property int index
                            required property var modelData
                            readonly property bool on: App.settingsPaneIndex === paneChip.index
                            height: 26
                            width: Math.min(chipText.implicitWidth, 160) + 22
                            radius: 13
                            color: paneChip.on
                                   ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                   : (chipHover.hovered ? App.themeCard : "transparent")
                            border.color: paneChip.on ? App.themeAccent : App.themeBorder
                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, 160)
                                text: {
                                    var p = paneChip.modelData ? paneChip.modelData.currentFolder : ""
                                    if (!p) return (paneChip.index === 0 ? "1" : "2")
                                    var cut = Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"))
                                    return (paneChip.index === 0 ? "1 · " : "2 · ")
                                           + (cut >= 0 ? p.slice(cut + 1) : p)
                                }
                                color: App.themeTextPrimary
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }
                            HoverHandler { id: chipHover }
                            TapHandler { onTapped: App.setSettingsPaneIndex(paneChip.index) }
                        }
                    }
                }

                CloseButton {
                    id: closeBtn
                    objectName: "settingsCloseButton"
                    anchors { right: parent.right; rightMargin: 22; verticalCenter: parent.verticalCenter }
                    onClicked: dlg.accept()
                }

                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: App.themeBorder
                }
            }

            StackLayout {
                id: stack
                objectName: "settingsStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 34
                Layout.rightMargin: 20
                Layout.topMargin: 20
                Layout.bottomMargin: 12
                currentIndex: nav.currentIndex

                //  Seiten erst beim ersten Besuch bauen, danach behalten: alle sieben auf einmal hielten das Oeffnen
                //  mit 403 Tags rund 4 s auf.
                component Seite: Loader {
                    active: false
                    readonly property bool jetzt: StackLayout.isCurrentItem
                    onJetztChanged: if (jetzt) active = true
                    Component.onCompleted: if (jetzt) active = true
                }

                Seite {
                    sourceComponent: SettingsGeneralTab { onRhiSwitchSucceeded: dlg.accept() }
                }
                Seite { sourceComponent: SettingsEditorTab {} }
                Seite { sourceComponent: SettingsViewTab {} }
                Seite { sourceComponent: SettingsLabelingTab {} }
                Seite { sourceComponent: SettingsAudioTab {} }
                Seite { sourceComponent: SettingsDesignTab {} }
                Seite { sourceComponent: SettingsBookmarksTab {} }
            }
        }
    }
}
