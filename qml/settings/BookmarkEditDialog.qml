import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Lesezeichen anlegen oder ändern, identisch an beiden Aufrufstellen (Einstellungen und Hauptmenü ▸ Ordner).
// Self-contained: kapselt Eingabedialog samt Ordner-Wähler und schreibt direkt über App.addBookmark /
// App.updateBookmark. Einstieg über `openAdd(pfad[, gruppe])` bzw. `openEdit(index, name, pfad, gruppe)`.
Item {
    id: root

    // -1 = Hinzufügen, >=0 = Bearbeiten (Index in App.savedFolders)
    property int editIndex: -1

    // Auswahlliste der Gruppe: "ohne" an Position 0 plus jede angelegte Gruppe in Menü-Reihenfolge. `path` ist die
    // Identität, `label` das Gelesene - nach Tiefe eingerückt, sonst ist die Verschachtelung nicht zu erkennen.
    readonly property var groupItems: {
        var out = [{ path: "", label: App.uiText(App.language, "BookmarkGroupNone") }]
        var rows = App.bookmarkTree
        for (var i = 0; i < rows.length; i++) {
            if (rows[i].kind !== "group") continue
            var pad = ""
            for (var d = 0; d < rows[i].depth; d++) pad += "    "
            out.push({ path: rows[i].group, label: pad + rows[i].name })
        }
        return out
    }
    function _groupIndex(g) {
        var want = (g === undefined || g === null) ? "" : g
        for (var i = 0; i < root.groupItems.length; i++)
            if (root.groupItems[i].path === want) return i
        return 0
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
            var gi = root.groupItems[groupBox.currentIndex]
            var g = gi ? gi.path : ""
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
                visible: root.groupItems.length > 1
                Label {
                    text: App.uiText(App.language, "BookmarkGroupLabel")
                    color: App.themeTextPrimary; Layout.preferredWidth: 60
                }
                ComboBox {
                    id: groupBox
                    Layout.fillWidth: true
                    model: root.groupItems
                    textRole: "label"
                    //  Im geschlossenen Zustand der VOLLE Pfad - eingerückt
                    //  wäre er dort ohne seine Nachbarn nicht zu deuten.
                    displayText: {
                        var gi = root.groupItems[groupBox.currentIndex]
                        if (!gi) return ""
                        return gi.path.length > 0
                               ? gi.path
                               : App.uiText(App.language, "BookmarkGroupNone")
                    }
                    delegate: ItemDelegate {
                        required property int index
                        required property var modelData
                        width: groupBox.width
                        text: modelData.label
                        highlighted: groupBox.highlightedIndex === index
                    }
                }
            }

            // Aktionsschaltflächen (eigener Footer statt standardButtons:
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
