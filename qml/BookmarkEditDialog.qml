import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import MediaGallery 1.0

// ── Wiederverwendbarer Dialog: gespeicherten Ordner (Lesezeichen) anlegen/ändern
//
//  Identisches Verhalten an beiden Aufrufstellen:
//    • Einstellungen ▸ Lesezeichen ▸ "+ Ordner hinzufügen"
//    • Hauptmenü ▸ Ordner ▸ "Ordner hinzufügen"
//
//  Die Komponente ist self-contained: Sie kapselt den modalen Eingabedialog
//  (Anzeigename + Pfad + Durchsuchen) samt FolderDialog und schreibt direkt
//  über die globalen Singletons App.addBookmark / App.updateBookmark.
//
//  API:
//    openAdd()                       → leeres Formular im Hinzufügen-Modus
//    openEdit(index, name, path)     → vorbefülltes Formular im Bearbeiten-Modus
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    // -1 = Hinzufügen, >=0 = Bearbeiten (Index in App.savedFolders)
    property int editIndex: -1

    function openAdd() {
        editIndex = -1
        nameField.text = ""
        pathField.text = ""
        editDialog.title = App.uiText(App.language, "SettingsBookAddTitle")
        editDialog.open()
    }
    function openEdit(index, name, path) {
        editIndex = index
        nameField.text = name
        pathField.text = path
        editDialog.title = App.uiText(App.language, "SettingsBookEditTitle")
        editDialog.open()
    }

    // ── Eingabedialog (Hinzufügen / Bearbeiten) ──────────────────────────────
    Dialog {
        id: editDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.NoButton
        width: 460
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        onAccepted: {
            var p = pathField.text.trim()
            if (p.length === 0) return                 // leerer Pfad → kein Eintrag
            if (root.editIndex < 0) App.addBookmark(nameField.text.trim(), p)
            else                    App.updateBookmark(root.editIndex, nameField.text.trim(), p)
        }

        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Label { text: App.uiText(App.language, "SettingsCatNewLabel"); color: App.themeTextPrimary; Layout.preferredWidth: 60 }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: App.uiText(App.language, "SettingsBookDisplayName")
                    color: App.themeTextPrimary
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Label { text: App.uiText(App.language, "BookmarkPathLabel"); color: App.themeTextPrimary; Layout.preferredWidth: 60 }
                TextField {
                    id: pathField
                    Layout.fillWidth: true
                    placeholderText: App.uiText(App.language, "SettingsBookFolderPath")
                    color: App.themeTextPrimary
                }
                Button {
                    text: App.uiText(App.language, "BookmarkBrowse")
                    onClicked: folderDialog.open()
                }
            }

            // ── Aktionsschaltflächen (eigener Footer statt standardButtons:
            //    folgt App.language statt der Qt-Systemlocale, mit Luft zum Rand)
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: editDialog.reject()
                }
                Button {
                    text: App.uiText(App.language, "SettingsOk")
                    highlighted: true
                    onClicked: editDialog.accept()
                }
            }
        }
    }

    // ── Ordner-Auswahl (füllt nur das Pfadfeld) ──────────────────────────────
    FolderDialog {
        id: folderDialog
        title: App.uiText(App.language, "SettingsBookChooseFolder")
        onAccepted: {
            var p = selectedFolder.toString()
            if (p.startsWith("file://")) p = decodeURIComponent(p.substring(7))
            pathField.text = p
        }
    }
}
