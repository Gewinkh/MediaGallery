pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  SettingsDialog.qml — vollständige QML-Migration des SettingsDialog (QWidget).
//
//  Acht Tabs (Allgemein, Editor, Ansicht/Layout, Tags, Kategorien, Converter,
//  Design, Lesezeichen). Alle Werte werden über das App-Singleton (AppController,
//  Settings-Bridge) bzw. Tags-Singleton (TagController) gelesen/geschrieben —
//  Lesen via Q_PROPERTY/Q_INVOKABLE, Schreiben via Q_INVOKABLE. AppSettings/
//  ISettings bleiben unverändertes Backend.
//
//  Im Shell als Loader-gated Instanz gehalten (RAM-Priorität): erst beim Öffnen
//  instanziiert, beim Schließen wieder freigegeben.
// ─────────────────────────────────────────────────────────────────────────────
Dialog {
    id: dlg
    title: qsTr("Einstellungen")
    modal: true
    width: 940
    height: 580
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

        // ── Tab-Leiste ───────────────────────────────────────────────────────
        TabBar {
            id: tabBar
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.bottomMargin: 0

            background: Rectangle { color: "transparent" }

            component SettingsTab: TabButton {
                contentItem: Text {
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

            SettingsTab { text: qsTr("Allgemein") }
            SettingsTab { text: qsTr("Editor") }
            SettingsTab { text: qsTr("Ansicht / Layout") }
            SettingsTab { text: qsTr("Tags") }
            SettingsTab { text: qsTr("Kategorien") }
            SettingsTab { text: qsTr("Converter") }
            SettingsTab { text: qsTr("Design") }
            SettingsTab { text: qsTr("Lesezeichen") }
        }

        // ── Tab-Inhalte ──────────────────────────────────────────────────────
        StackLayout {
            id: stack
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
            SettingsDesignTab     {}
            SettingsBookmarksTab  {}
        }

        // ── Fußzeile ─────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0

            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Schließen")
                highlighted: true
                onClicked: dlg.accept()
            }
        }
    }

}

