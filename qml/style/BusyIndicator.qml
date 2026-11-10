import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.Fusion.impl

//  Fusions Kreisel ohne dessen Einblenden per OpacityAnimator: kippt `running` in einem nicht gezeichneten Element
//  (geschlossener Dateiwaehler), endet der Auftrag im Renderfaden nie und das Fenster zeichnet in Ruhe weiter.
T.BusyIndicator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6

    contentItem: BusyIndicatorImpl {
        implicitWidth: 28
        implicitHeight: 28
        color: control.palette.text

        running: control.running
        visible: control.running

        RotationAnimator on rotation {
            running: control.running && control.contentItem.visible
            from: 0
            to: 360
            duration: 1000
            loops: Animation.Infinite
        }
    }
}
