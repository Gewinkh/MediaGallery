import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// Namensdialog der Extraktion, geteilt von drei Aufrufstellen. Eingegeben wird nur der Basisname, ".pdf"
// ergänzt die App; ein leeres Feld übernimmt den Default, im Pflicht-Modus (`requireName`, global) bleibt
// der Knopf gesperrt. Kollisionen (" (1)", " (2)") löst der Controller beim Schreiben auf.
Item {
    id: root

    // Platzhalter/Default-Basisname (ohne „.pdf"); leer im globalen Modus.
    property string placeholderName: ""
    // true (global): OK erst bei nichtleerer Eingabe.
    property bool   requireName: false

    // name = eingegebener Basisname; "" = Default übernehmen (nur !requireName).
    signal accepted(string name)

    function openFor(placeholder, require) {
        placeholderName = placeholder !== undefined ? placeholder : ""
        requireName     = require === true
        nameField.text  = ""
        dlg.open()
        nameField.forceActiveFocus()
    }

    Popup {
        id: dlg
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 340
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        function confirm() {
            const t = nameField.text.trim()
            if (root.requireName && t.length === 0)
                return
            dlg.close()
            root.accepted(t)
        }

        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder; border.width: 1
            radius: 6
        }

        contentItem: Column {
            spacing: 10

            Text {
                text: App.uiText(App.language, "ExtractNameTitle")
                color: App.themeTextPrimary; font.pixelSize: 13; font.bold: true
            }
            Text {
                text: App.uiText(App.language, "ExtractNameLabel")
                color: App.themeTextMuted; font.pixelSize: 11
            }
            // Eingabefeld + statisches „.pdf"-Suffix (Endung ist sichtbar,
            // aber bewusst KEIN Eingabeschritt).
            Row {
                width: parent.width
                spacing: 6
                TextField {
                    id: nameField
                    width: parent.width - pdfSuffix.width - parent.spacing
                    font.pixelSize: 12
                    placeholderText: root.placeholderName
                    onAccepted: dlg.confirm()
                }
                Text {
                    id: pdfSuffix
                    anchors.verticalCenter: parent.verticalCenter
                    text: ".pdf"
                    color: App.themeTextMuted; font.pixelSize: 12
                }
            }

            Row {
                anchors.right: parent.right
                spacing: 8
                Button {
                    height: 28; font.pixelSize: 12
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: dlg.close()
                }
                Button {
                    height: 28; font.pixelSize: 12
                    enabled: !root.requireName || nameField.text.trim().length > 0
                    text: App.uiText(App.language, "ExtractCreateBtn")
                    palette.buttonText: enabled ? App.themeAccent : App.themeTextMuted
                    onClicked: dlg.confirm()
                }
            }
        }
    }
}
