import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  ScrollBar.qml — schlanke, gethemte Bildlaufleiste (Stil "MediaGalleryStyle").
//
//  Ohne Pfeil-Schaltflächen und ohne Rinnen-Rahmen: nur ein abgerundeter Griff,
//  der bei Hover/Zug breiter und kräftiger wird. Die Leiste zeichnet dadurch pro
//  Bild ein einziges Rechteck statt der fünf Fusion-Elemente.
// ─────────────────────────────────────────────────────────────────────────────
T.ScrollBar {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 2
    visible: control.policy !== T.ScrollBar.AlwaysOff

    contentItem: Rectangle {
        implicitWidth:  control.interactive ? 8 : 4
        implicitHeight: control.interactive ? 8 : 4
        radius: width / 2
        color: control.pressed
               ? App.themeAccent
               : Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b,
                         control.hovered ? 0.75 : 0.45)

        opacity: 0.0
        states: State {
            name: "active"
            when: control.policy === T.ScrollBar.AlwaysOn
                  || (control.active && control.size < 1.0)
            PropertyChanges { control.contentItem.opacity: 1.0 }
        }
        transitions: Transition {
            from: "active"
            SequentialAnimation {
                PauseAnimation { duration: 500 }
                NumberAnimation { target: control.contentItem; property: "opacity"; to: 0.0; duration: 300 }
            }
        }

        Behavior on color { ColorAnimation { duration: 110 } }
    }

    background: Rectangle {
        color: Qt.rgba(App.themeBorder.r, App.themeBorder.g, App.themeBorder.b, 0.35)
        radius: width / 2
        visible: control.policy === T.ScrollBar.AlwaysOn
                 || (control.active && control.size < 1.0)
        opacity: control.hovered || control.pressed ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
}
