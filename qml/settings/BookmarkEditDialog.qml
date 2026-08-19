import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// ── Wiederverwendbarer Dialog: gespeicherten Ordner (Lesezeichen) anlegen/ändern
//
//  Identisches Verhalten an beiden Aufrufstellen:
//    • Einstellungen ▸ Lesezeichen ▸ "+ Ordner hinzufügen"
//    • Hauptmenü ▸ Ordner ▸ "Ordner hinzufügen"
//
//  Die Komponente ist self-contained: Sie kapselt den modalen Eingabedialog
//  (Anzeigename + Pfad + Durchsuchen) samt Ordner-Wähler und schreibt direkt
//  über die globalen Singletons App.addBookmark / App.updateBookmark.
//
//  API:
//    openAdd(prefillPath[, group])   -> Formular im Hinzufügen-Modus; optionaler
//                                      vorbefüllter Pfad (bleibt frei änderbar)
//                                      und optionale Vorauswahl der Gruppe
//    openEdit(index, name, path, group) -> vorbefülltes Formular (Bearbeiten)
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    // -1 = Hinzufügen, >=0 = Bearbeiten (Index in App.savedFolders)
    property int editIndex: -1

    //  Auswahlliste der Gruppe: „ohne" + jede angelegte Gruppe (Reihenfolge wie
    //  im Menü). Der leere Eintrag steht bewusst an Position 0.
    readonly property var groupNames: {
        var out = [""]
        var tree = App.bookmarkTree
        for (var i = 0; i < tree.length; i++)
            if (tree[i].group.length > 0) out.push(tree[i].group)
        return out
    }
    function _groupIndex(g) {
        var i = root.groupNames.indexOf(g === undefined || g === null ? "" : g)
        return i < 0 ? 0 : i
    }

    function openAdd(prefillPath, group) {
        editIndex = -1
        groupBox.currentIndex = root._groupIndex(group)
        nameField.text = ""
        // Optionale Vorbefüllung (z. B. der aktuell geöffnete Ordner aus dem
        // Hauptmenü, sofern noch nicht gespeichert - s. ApplicationShell).
        // Ohne Argument (Einstellungen ▸ Lesezeichen) bleibt das Feld leer.
        pathField.text = (prefillPath !== undefined && prefillPath !== null)
                         ? prefillPath : ""
        editDialog.title = App.uiText(App.language, "SettingsBookAddTitle")
        editDialog.open()
    }
    function openEdit(index, name, path, group) {
        editIndex = index
        groupBox.currentIndex = root._groupIndex(group)
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
            if (p.length === 0) return                 // leerer Pfad -> kein Eintrag
            var g = root.groupNames[groupBox.currentIndex]
            if (root.editIndex < 0) App.addBookmark(nameField.text.trim(), p, g)
            else                    App.updateBookmark(root.editIndex, nameField.text.trim(), p, g)
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

            //  Gruppe - die Zeile erscheint erst, wenn es überhaupt eine gibt;
            //  ohne angelegte Gruppen gäbe es nichts zu wählen.
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                visible: root.groupNames.length > 1
                Label {
                    text: App.uiText(App.language, "BookmarkGroupLabel")
                    color: App.themeTextPrimary; Layout.preferredWidth: 60
                }
                ComboBox {
                    id: groupBox
                    Layout.fillWidth: true
                    model: root.groupNames
                    displayText: root.groupNames[currentIndex] === ""
                                 ? App.uiText(App.language, "BookmarkGroupNone")
                                 : root.groupNames[currentIndex]
                    delegate: ItemDelegate {
                        required property int index
                        required property var modelData
                        width: groupBox.width
                        text: modelData === "" ? App.uiText(App.language, "BookmarkGroupNone")
                                               : modelData
                        highlighted: groupBox.highlightedIndex === index
                    }
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
    FileChooser {
        id: folderDialog
        title: App.uiText(App.language, "SettingsBookChooseFolder")
        fileMode: FileChooser.Directory
        onAccepted: {
            var p = selectedFolder.toString()
            if (p.startsWith("file://")) p = decodeURIComponent(p.substring(7))
            pathField.text = p
        }
    }
}
