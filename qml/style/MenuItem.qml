import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// Gethemter Menüeintrag mit abgerundeter Akzent-Markierung und selbst gezeichnetem Untermenü-Pfeil.
// `background.implicitWidth` ist Pflicht - ThemedMenu rechnet die Menübreite daraus, sonst kollabiert das Popup.
T.MenuItem {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 6
    leftPadding: 10
    rightPadding: 10
    spacing: 9
    font.pixelSize: 13

    //  Tastenkuerzel rechtsbuendig in Festbreite; steht nicht mehr im Text, damit die Spalten fluchten.
    property string shortcut: ""

    // Qt setzt `Menu.currentIndex` beim Überfahren und lässt ihn stehen - fährt man aus dem Menü hinaus, bleibt der
    // Eintrag markiert. Verzögert aufräumen und nur, wenn er dann noch markiert ist: der Nachbar setzt den Index zuerst.
    onHoveredChanged: if (!control.hovered) Qt.callLater(control._dropStaleHighlight)
    function _dropStaleHighlight() {
        if (control.hovered || !control.menu) return
        const i = control.menu.currentIndex
        if (i >= 0 && control.menu.itemAt(i) === control) control.menu.currentIndex = -1
    }

    //  Häkchen-Markierung (nur bei `checkable`) - Geometrie wie CheckBox.qml,
    //  nur eine Spur kleiner, damit die Zeilenhöhe eines Menüs erhalten bleibt.
    indicator: Rectangle {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth:  control.checkable ? 14 : 0
        implicitHeight: control.checkable ? 14 : 0
        visible: control.checkable
        radius: 3.5

        color: control.checked ? App.themeAccent : "transparent"
        border.width: control.checked ? 0 : 1
        border.color: Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.8)
        opacity: control.enabled ? 1.0 : 0.5

        Behavior on color { ColorAnimation { duration: 110 } }

        DrawnIconItem {
            anchors.centerIn: parent
            visible: control.checked
            name: "check"
            size: 10
            color: App.themeBackground
        }
    }

    //  Untermenü-Pfeil, gezeichnet wie alle Symbole (bei RTL nach links).
    arrow: DrawnIconItem {
        x: control.mirrored ? control.padding : control.width - width - control.rightPadding + 2
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.subMenu
        name: control.mirrored ? "chevron-left" : "chevron-right"
        size: control.subMenu ? 12 : 0
        color: control.enabled ? App.themeTextMuted : App.themeBorder
    }

    contentItem: Item {
        //  Platz für Markierung bzw. Pfeil freihalten (wie Fusion), damit die
        //  Beschriftungen eines Menüs untereinander fluchten.
        readonly property real indicatorPad: control.checkable && control.indicator
                                             ? control.indicator.width + control.spacing : 0
        readonly property real arrowPad: control.subMenu && control.arrow
                                         ? control.arrow.width + control.spacing : 0
        readonly property real hintPad: control.shortcut.length > 0 ? hint.implicitWidth + 24 : 0
        implicitWidth: beschriftung.implicitWidth + indicatorPad + arrowPad + hintPad
        implicitHeight: beschriftung.implicitHeight

        Text {
            id: beschriftung
            anchors.fill: parent
            leftPadding:  !control.mirrored ? parent.indicatorPad : parent.arrowPad + parent.hintPad
            rightPadding:  control.mirrored ? parent.indicatorPad : parent.arrowPad + parent.hintPad
            text: control.text
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            color: control.enabled ? App.themeTextPrimary : App.themeTextMuted
        }
        Text {
            id: hint
            visible: control.shortcut.length > 0
            anchors.verticalCenter: parent.verticalCenter
            x: control.mirrored ? parent.arrowPad : parent.width - width - parent.arrowPad
            text: control.shortcut
            font: App.fallbackFont("monospace", 11)
            color: App.themeTextMuted
            opacity: control.enabled ? 1.0 : 0.6
        }
    }

    background: Rectangle {
        //  s. Kopfkommentar - Mindestbreite für ThemedMenu.
        implicitWidth: 160
        implicitHeight: 28
        radius: 5
        color: control.highlighted || control.down
               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20)
               : "transparent"
    }
}
