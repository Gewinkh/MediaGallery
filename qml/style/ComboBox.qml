pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Templates as T
import MediaGallery 1.0

// Gethemtes Auswahlfeld wie Button und TextField, mit selbst gezeichnetem Pfeil und einem Popup, das den
// aktuellen Eintrag mit der Akzentfarbe hinterlegt.
T.ComboBox {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    leftPadding:  padding + (!control.mirrored || !indicator || !indicator.visible ? 3 : indicator.width + spacing)
    rightPadding: padding + ( control.mirrored || !indicator || !indicator.visible ? 3 : indicator.width + spacing)
    padding: 6
    spacing: 6
    font.pixelSize: 13

    delegate: T.ItemDelegate {
        id: cbItem
        required property var model
        required property int index

        width: ListView.view.width
        implicitHeight: 28
        padding: 0
        text: cbItem.model[control.textRole]
        highlighted: control.highlightedIndex === cbItem.index
        hoverEnabled: control.hoverEnabled

        contentItem: Text {
            leftPadding: 10
            rightPadding: 10
            text: cbItem.text
            font: control.font
            color: cbItem.index === control.currentIndex ? App.themeAccent : App.themeTextPrimary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: (cbItem.highlighted || cbItem.hovered)
                   ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.18)
                   : (cbItem.index === control.currentIndex
                      ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.10)
                      : "transparent")
        }
    }

    //  Aufklapp-Pfeil (Dreieck, selbst gezeichnet).
    indicator: Item {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 12
        implicitHeight: 12
        opacity: control.enabled ? 1.0 : 0.5
        Canvas {
            anchors.centerIn: parent
            width: 9; height: 6
            //  Neuzeichnen, wenn sich die Themenfarbe ändert.
            readonly property color arrowColor: App.themeTextMuted
            onArrowColorChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = arrowColor
                ctx.beginPath()
                ctx.moveTo(0, 0); ctx.lineTo(width, 0); ctx.lineTo(width / 2, height)
                ctx.closePath(); ctx.fill()
            }
        }
    }

    contentItem: T.TextField {
        leftPadding:  control.mirrored ? 3 : control.leftPadding
        rightPadding: control.mirrored ? control.rightPadding : 3
        topPadding: 0; bottomPadding: 0

        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse

        font: control.font
        color: control.enabled ? App.themeTextPrimary : App.themeTextMuted
        selectionColor: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.45)
        selectedTextColor: App.themeTextPrimary
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 28
        radius: 6
        color: control.down ? Qt.darker(App.themeCard, 1.2)
                            : (control.hovered ? Qt.lighter(App.themeCard, 1.3) : App.themeCard)
        border.width: 1
        border.color: control.activeFocus || control.visualFocus
                      ? App.themeAccent : App.themeBorder
        opacity: control.enabled ? 1.0 : 0.5

        Behavior on color { ColorAnimation { duration: 110 } }
    }

    popup: T.Popup {
        y: control.height + 2
        width: control.width
        height: Math.min(contentItem.implicitHeight + 2, control.Window.height - topMargin - bottomMargin)
        topMargin: 6
        bottomMargin: 6
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder
            border.width: 1
            radius: 6
        }
    }
}
