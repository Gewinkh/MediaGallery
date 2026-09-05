pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Acht Reiter; alle Werte laufen über die Singletons `App` und `Tags`, AppSettings bleibt unverändertes Backend.
// Im Shell Loader-gated gehalten (RAM): erst beim Öffnen instanziiert, beim Schließen wieder freigegeben.
Dialog {
    id: dlg
    //  Griff fuer die Pruefstaende (Muster wie `documentMenu`) - QML-`id`s sind
    //  von aussen nicht auffindbar, `objectName` schon.
    objectName: "settingsDialog"
    title: App.uiText(App.language, "SettingsTitle")
    //  Beim Öffnen gilt die Hälfte, in der gerade gearbeitet wird; beim
    //  Schließen folgt die Fassade wieder dem Fokus (−1).
    onOpened: App.setSettingsPaneIndex(App.paneCount > 1 ? App.focusedPaneIndex : -1)
    onClosed: App.setSettingsPaneIndex(-1)
    modal: true
    //  Nie breiter/höher als das Fenster: sonst stehen Reiter und Inhalt über
    //  den Rand hinaus und sind nicht mehr erreichbar (Nutzerbild `settings.png`).
    width: Math.min(940, (Overlay.overlay ? Overlay.overlay.width : 940) - 24)
    height: Math.min(580, (Overlay.overlay ? Overlay.overlay.height : 580) - 24)
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.NoButton
    padding: 0

    background: Rectangle {
        color: App.themeBackground
        border.color: App.themeBorder
        radius: 8
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Tags, Kategorien und der Konverter arbeiten am SIDECAR eines Ordners; mit zwei Galerien nebeneinander muss
        // man sagen können, welcher gemeint ist. Ohne Teilung bleibt die Zeile weg.
        RowLayout {
            id: paneChooser
            //  Nur dort, wo es wirklich um EINEN Ordner geht: Tags (3),
            //  Kategorien (4), Konverter (5). Alles andere ist appweit - dort
            //  wäre die Zeile eine falsche Zusage.
            readonly property bool folderScoped: tabBar.currentIndex >= 3
                                                 && tabBar.currentIndex <= 5
            visible: App.paneCount > 1 && folderScoped
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: visible ? 10 : 0
            spacing: 8

            Text {
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
                    height: 24
                    width: chipText.implicitWidth + 22
                    radius: 12
                    color: paneChip.on
                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                           : (chipHover.hovered ? App.themeCard : "transparent")
                    border.color: paneChip.on ? App.themeAccent : App.themeBorder
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: {
                            var p = paneChip.modelData ? paneChip.modelData.currentFolder : ""
                            if (!p) return (paneChip.index === 0 ? "1" : "2")
                            var cut = Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"))
                            return (paneChip.index === 0 ? "1 · " : "2 · ")
                                   + (cut >= 0 ? p.slice(cut + 1) : p)
                        }
                        color: App.themeTextPrimary
                        font.pixelSize: 12
                    }
                    HoverHandler { id: chipHover }
                    TapHandler { onTapped: App.setSettingsPaneIndex(paneChip.index) }
                }
            }
        }

        TabBar {
            id: tabBar
            objectName: "settingsTabBar"
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.bottomMargin: 0

            // BESCHNEIDEN, sonst laufen die Reiter aus dem Dialog: die TabBar blättert intern über eine ListView, die ohne
            // `clip` auch links und rechts außerhalb malt. Beides setzen - an der Leiste und am `contentItem`.
            clip: true
            Component.onCompleted: if (tabBar.contentItem) tabBar.contentItem.clip = true

            background: Rectangle { color: "transparent" }

            // Die `TabBar` blättert intern über eine `ListView`, hört aber von sich aus nicht aufs Rad. Dieselbe
            // Komponente wie in den übrigen Leisten holt das nach.
            SmoothWheelArea {
                flickable: tabBar.contentItem
                horizontal: true
            }

            component SettingsTab: TabButton {
                // Mindestbreite je Reiter: sonst quetscht die `TabBar` alle auf dieselbe Breite und aus "Kategorien" wird
                // "Kate...". Passen sie zusammen nicht mehr hinein, blättert sie stattdessen.
                width: Math.max(tabLbl.implicitWidth + 26, 90)
                contentItem: Text {
                    id: tabLbl
                    text: parent.text
                    color: parent.checked ? App.themeAccent : App.themeTextMuted
                    font.pixelSize: 13
                    font.bold: parent.checked
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    color: parent.checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.15)
                                          : Qt.rgba(1, 1, 1, 0.04)
                    border.color: parent.checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.4)
                                                 : App.themeBorder
                    radius: 4
                }
            }

            SettingsTab { text: App.uiText(App.language, "SettingsTabGeneral") }
            SettingsTab { text: App.uiText(App.language, "SettingsTabEditorShort") }
            SettingsTab { text: App.uiText(App.language, "SettingsTabViewLayout") }
            SettingsTab { text: App.uiText(App.language, "FilterTags") }
            SettingsTab { text: App.uiText(App.language, "SettingsTabCategories") }
            SettingsTab { text: App.uiText(App.language, "SettingsTabConverter") }
            SettingsTab { text: App.uiText(App.language, "SettingsTabAudio") }
            SettingsTab { text: App.uiText(App.language, "SettingsTabDesign") }
            SettingsTab { text: App.uiText(App.language, "SettingsTabBookmarks") }
        }

        StackLayout {
            id: stack
            objectName: "settingsStack"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            currentIndex: tabBar.currentIndex

            SettingsGeneralTab    {
                id: generalTab
                onRhiSwitchSucceeded: dlg.accept()
            }
            SettingsEditorTab     {}
            SettingsViewTab       {}
            SettingsTagsTab       {}
            SettingsCategoriesTab {}
            SettingsConverterTab  {}
            SettingsAudioTab      {}
            SettingsDesignTab     {}
            SettingsBookmarksTab  {}
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0

            Item { Layout.fillWidth: true }
            Button {
                text: App.uiText(App.language, "SettingsClose")
                highlighted: true
                onClicked: dlg.accept()
            }
        }
    }

}

