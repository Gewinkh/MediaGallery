import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  SpinBox.qml - gethemtes Zahlenfeld (Stil "style").
//
//  Feld wie TextField (Radius 6, Kartenfarbe, Akzentrahmen bei Fokus); die
//  beiden Schaltflächen sitzen rechts als abgerundete Halbfelder mit selbst
//  gezeichnetem +/− (schriftartunabhängig, kein Bild/Atlas).
// ─────────────────────────────────────────────────────────────────────────────
T.SpinBox {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             contentItem.implicitWidth + 2 * padding +
                             up.implicitIndicatorWidth + down.implicitIndicatorWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             up.implicitIndicatorHeight, down.implicitIndicatorHeight)

    padding: 4
    leftPadding: 9
    rightPadding: 9 + up.implicitIndicatorWidth + down.implicitIndicatorWidth
    font.pixelSize: 13

    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top:    Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: control.enabled ? App.themeTextPrimary : App.themeTextMuted
        selectionColor: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.45)
        selectedTextColor: App.themeTextPrimary
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        selectByMouse: true
    }

    //  Gemeinsame Optik beider Schaltflächen; `plus` wählt das Zeichen.
    component StepBtn: Rectangle {
        property bool plus: true
        property bool pressed: false
        property bool active: true
        implicitWidth: 22
        implicitHeight: 28
        radius: 4
        color: pressed ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.28)
                       : "transparent"
        opacity: active ? 1.0 : 0.35

        Rectangle {                                   // waagerechter Balken (+ und −)
            anchors.centerIn: parent
            width: 9; height: 1.6; radius: 0.8
            color: App.themeTextPrimary
        }
        Rectangle {                                   // senkrechter Balken (nur +)
            anchors.centerIn: parent
            width: 1.6; height: 9; radius: 0.8
            color: App.themeTextPrimary
            visible: parent.plus
        }
    }

    up.indicator: StepBtn {
        x: control.width - width - 4
        y: (control.height - height) / 2
        plus: true
        pressed: control.up.pressed
        active: control.up.indicator ? control.enabled && control.value < control.to : true
    }

    down.indicator: StepBtn {
        x: control.width - control.up.indicator.width - width - 5
        y: (control.height - height) / 2
        plus: false
        pressed: control.down.pressed
        active: control.enabled && control.value > control.from
    }

    background: Rectangle {
        implicitWidth: 130
        implicitHeight: 28
        radius: 6
        color: control.enabled ? App.themeCard : Qt.darker(App.themeCard, 1.15)
        border.width: 1
        border.color: control.activeFocus ? App.themeAccent
                                          : (control.hovered ? Qt.lighter(App.themeBorder, 1.4)
                                                             : App.themeBorder)
        opacity: control.enabled ? 1.0 : 0.55

        Behavior on border.color { ColorAnimation { duration: 110 } }
    }
}
