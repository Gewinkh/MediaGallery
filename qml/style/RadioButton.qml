import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  RadioButton.qml - gethemter Auswahlknopf (Stil "style").
//
//  Ring in Themenfarbe, im Zustand „an" Ring + Punkt in der Akzentfarbe (statt
//  des grauen Fusion-Punkts). Geometrie und Abstände wie CheckBox.qml, damit
//  Auswahl- und Häkchen-Gruppen in den Einstellungen exakt fluchten.
// ─────────────────────────────────────────────────────────────────────────────
T.RadioButton {
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
        radius: width / 2

        color: control.down ? Qt.darker(App.themeCard, 1.2)
                            : (control.hovered ? Qt.lighter(App.themeCard, 1.35) : App.themeCard)
        border.width: control.checked ? 2 : 1
        border.color: control.checked || control.visualFocus
                      ? App.themeAccent
                      : (control.hovered ? Qt.lighter(App.themeBorder, 1.4) : App.themeBorder)
        opacity: control.enabled ? 1.0 : 0.5

        Behavior on color { ColorAnimation { duration: 110 } }

        Rectangle {
            anchors.centerIn: parent
            width: 8; height: 8; radius: 4
            color: App.themeAccent
            visible: control.checked
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
