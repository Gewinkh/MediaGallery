import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// Abgerundeter, gethemter Haken statt des eckigen Fusion-Kästchens: im Zustand "an" in der Akzentfarbe gefüllt,
// der Haken aus zwei gedrehten Balken - schriftartunabhängig und ohne Textur.
T.CheckBox {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 4
    spacing: 8
    font.pixelSize: 13

    indicator: Rectangle {
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding
                                            : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 18
        implicitHeight: 18
        radius: 5

        readonly property bool on: control.checkState !== Qt.Unchecked

        color: on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b,
                            control.down ? 1.0 : 0.9)
                  : (control.down ? Qt.darker(App.themeCard, 1.2)
                                  : (control.hovered ? Qt.lighter(App.themeCard, 1.35)
                                                     : App.themeCard))
        border.width: 1
        border.color: on || control.visualFocus
                      ? App.themeAccent
                      : (control.hovered ? Qt.lighter(App.themeBorder, 1.4) : App.themeBorder)
        opacity: control.enabled ? 1.0 : 0.5

        Behavior on color { ColorAnimation { duration: 110 } }

        // Haken (zwei gedrehte Balken) - nur im Zustand „an".
        Item {
            anchors.centerIn: parent
            width: 12; height: 12
            visible: parent.on && control.checkState === Qt.Checked
            Rectangle {
                x: 1.5; y: 6.5; width: 5; height: 2; radius: 1
                color: App.themeBackground
                transformOrigin: Item.Left
                rotation: 45
            }
            Rectangle {
                x: 4; y: 8.5; width: 8.5; height: 2; radius: 1
                color: App.themeBackground
                transformOrigin: Item.Left
                rotation: -50
            }
        }

        // Teilweise ausgewählt: waagerechter Balken.
        Rectangle {
            anchors.centerIn: parent
            width: 9; height: 2; radius: 1
            color: App.themeBackground
            visible: control.checkState === Qt.PartiallyChecked
        }
    }

    contentItem: Text {
        leftPadding:  control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator &&  control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? App.themeTextPrimary : App.themeTextMuted
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
