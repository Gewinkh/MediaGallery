pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// Ein Unterdruecker in einer Zeile: Schalter, Staerkeregler, Prozentzahl - und
// darunter, wenn gesetzt, ein Satz Erklaerung. Die App hat zwei davon
// (Uebersteuern, Rauschen); ohne gemeinsame Form stuenden sie nebeneinander und
// saehen doch verschieden aus.
// Der Regler bleibt bedienbar, wenn der Schalter aus ist - die eingestellte
// Staerke ginge sonst beim Umlegen verloren; er wird nur abgeblendet.
Column {
    id: zeile

    property string text: ""
    property string hinweis: ""
    // Was die Stufe GERADE bewirkt, in ihrer eigenen Einheit. Ohne diese Zahl
    // waere am Regler nur ein Prozentwert zu sehen und nirgends, worauf er sich
    // auswirkt - beim Übersteuern bewegt sich sonst sichtbar gar nichts.
    property string wirkung: ""
    property bool   an: false
    property real   pegel: 1.0
    property alias  reglerName: regler.objectName

    signal umgelegt(bool wert)
    signal verschoben(real wert)

    spacing: 4

    Flow {
        width: zeile.width
        spacing: 8

        CheckBox {
            id: schalter
            text: zeile.text
            checked: zeile.an
            onToggled: zeile.umgelegt(checked)
            contentItem: Text {
                text: parent.text
                color: App.themeTextPrimary
                leftPadding: parent.indicator.width + 6
                verticalAlignment: Text.AlignVCenter
            }
        }
        Text {
            height: 20
            verticalAlignment: Text.AlignVCenter
            text: App.uiText(App.language, "AudioSuppressLevel")
            color: App.themeTextPrimary
            font.pixelSize: 12
            opacity: zeile.an ? 1.0 : 0.45
        }
        Slider {
            id: regler
            width: 140
            height: 20
            from: 0; to: 1; stepSize: 0.01
            value: zeile.pegel
            onMoved: zeile.verschoben(value)
            opacity: zeile.an ? 1.0 : 0.45

            background: Rectangle {
                y: regler.height / 2 - 2
                width: regler.width; height: 4; radius: 2
                color: Qt.rgba(1, 1, 1, 0.14)
                Rectangle {
                    height: parent.height; radius: parent.radius
                    width: regler.visualPosition * parent.width
                    color: App.themeAccent
                }
            }
            handle: Rectangle {
                x: regler.visualPosition * (regler.width - width)
                y: regler.height / 2 - height / 2
                width: 14; height: 14; radius: 7
                color: regler.pressed ? App.themeAccent : App.themeTextPrimary
                border.color: App.themeBorder
            }
        }
        Text {
            height: 20
            verticalAlignment: Text.AlignVCenter
            text: Math.round(regler.value * 100) + " %"
                  + (zeile.wirkung.length > 0 ? "  ·  " + zeile.wirkung : "")
            color: App.themeTextMuted
            font.pixelSize: 11
            opacity: zeile.an ? 1.0 : 0.45
        }
    }

    Text {
        width: zeile.width
        leftPadding: 24
        bottomPadding: 4
        visible: zeile.hinweis.length > 0
        text: zeile.hinweis
        color: App.themeTextMuted
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
}
