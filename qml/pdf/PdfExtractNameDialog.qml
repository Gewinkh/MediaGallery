import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

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
        width: 400
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        function confirm() {
            if (!dlg.visible) return
            const t = nameField.text.trim()
            if (root.requireName && t.length === 0)
                return
            dlg.close()
            root.accepted(t)
        }

        background: Rectangle {
            color: App.themeBackground
            border.color: App.themeBorder
            radius: 10
        }

        contentItem: Column {
            spacing: 0
            //  Enter gilt im ganzen Fenster, auch wenn ein Klick dem Feld den Fokus genommen hat.
            Keys.onReturnPressed: (e) => { dlg.confirm(); e.accepted = true }
            Keys.onEnterPressed:  (e) => { dlg.confirm(); e.accepted = true }

            Item {
                width: parent.width
                height: 60
                AccentSlash {
                    id: headSlash
                    anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                }
                Text {
                    anchors { left: headSlash.right; leftMargin: 10; right: closeBtn.left; rightMargin: 12
                              verticalCenter: parent.verticalCenter }
                    text: App.uiText(App.language, "ExtractNameTitle")
                    color: App.themeTextPrimary
                    font.pixelSize: 17
                    elide: Text.ElideRight
                }
                CloseButton {
                    id: closeBtn
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    onClicked: dlg.close()
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: App.themeBorder
                }
            }

            Column {
                width: parent.width
                padding: 22
                topPadding: 16
                spacing: 10

                // Eingabefeld + statisches „.pdf"-Suffix (Endung ist sichtbar,
                // aber bewusst KEIN Eingabeschritt).
                Row {
                    width: parent.width - 44
                    spacing: 6
                    TextField {
                        id: nameField
                        width: parent.width - pdfSuffix.width - parent.spacing
                        font.pixelSize: 13
                        placeholderText: root.placeholderName
                        onAccepted: dlg.confirm()
                    }
                    Text {
                        id: pdfSuffix
                        anchors.verticalCenter: parent.verticalCenter
                        text: ".pdf"
                        color: App.themeTextMuted; font.pixelSize: 13
                    }
                }
                Item {
                    width: parent.width - 44
                    height: createBtn.height + 6
                    Button {
                        id: createBtn
                        anchors { right: parent.right; bottom: parent.bottom }
                        highlighted: true
                        enabled: !root.requireName || nameField.text.trim().length > 0
                        text: App.uiText(App.language, "ExtractCreateBtn")
                        onClicked: dlg.confirm()
                    }
                }
            }
        }
    }
}
