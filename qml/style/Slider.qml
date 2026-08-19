import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  Slider.qml - gethemter Schieberegler (Stil "style").
//
//  Rinne in Themen-Rahmenfarbe, gefüllter Anteil und Griff in der Akzentfarbe.
//  Unterstützt beide Ausrichtungen (Farbwähler/Editor nutzen waagerecht).
// ─────────────────────────────────────────────────────────────────────────────
T.Slider {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)

    padding: 6

    handle: Rectangle {
        x: control.leftPadding + (control.horizontal
                                  ? control.visualPosition * (control.availableWidth - width)
                                  : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal
                                 ? (control.availableHeight - height) / 2
                                 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 16
        implicitHeight: 16
        radius: width / 2
        color: control.pressed ? Qt.darker(App.themeAccent, 1.2) : App.themeAccent
        border.width: 1
        border.color: Qt.darker(App.themeAccent, 1.4)
        opacity: control.enabled ? 1.0 : 0.5

        Behavior on color { ColorAnimation { duration: 110 } }
    }

    background: Rectangle {
        x: control.leftPadding + (control.horizontal ? 0 : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : 0)
        implicitWidth:  control.horizontal ? 150 : 5
        implicitHeight: control.horizontal ? 5 : 150
        width:  control.horizontal ? control.availableWidth : implicitWidth
        height: control.horizontal ? implicitHeight : control.availableHeight
        radius: 3
        color: App.themeBorder
        opacity: control.enabled ? 1.0 : 0.5

        Rectangle {
            x: control.horizontal ? 0 : (parent.width - width) / 2
            y: control.horizontal ? (parent.height - height) / 2
                                  : control.visualPosition * parent.height
            width:  control.horizontal ? control.position * parent.width : parent.width
            height: control.horizontal ? parent.height : control.position * parent.height
            radius: 3
            color: App.themeAccent
        }
    }
}
