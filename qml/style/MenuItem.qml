import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  MenuItem.qml - gethemter Menüeintrag (Stil "style").
//
//  Ersetzt Fusions hellgraues Kästchen mit Bitmap-Haken durch dieselbe
//  abgerundete Akzent-Markierung wie `CheckBox.qml` und zeichnet den
//  Untermenü-Pfeil selbst (schriftart-/atlasfrei).
//
//  WICHTIG - `background.implicitWidth: 200`: `ThemedMenu` in
//  `ApplicationShell.qml` berechnet die Menübreite aus `max(item.implicitWidth)`
//  mit 200 px als Mindestmaß (Fusion-Verhalten, s. Runde 2026-07-25, 2). Ohne
//  diese implizite Breite kollabieren die Menü-Popups wieder zum „Strich".
// ─────────────────────────────────────────────────────────────────────────────
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
    spacing: 8
    font.pixelSize: 13

    //  ── Markierung loslassen, wenn der Zeiger weg ist ───────────────────────
    //  Qt setzt beim Überfahren `Menu.currentIndex` auf den Eintrag und lässt
    //  ihn dort stehen - fährt man vom Eintrag weg (aus dem Menü hinaus oder auf
    //  eine Zeile, die kein `MenuItem` ist), bleibt er markiert, obwohl die Maus
    //  längst woanders ist (Nutzerbefund am Dokument-Menü).
    //
    //  Aufgeräumt wird VERZÖGERT (`Qt.callLater`), und nur, wenn dieser Eintrag
    //  dann noch der markierte ist: beim Wechsel auf den Nachbarn setzt DIESER
    //  den Index zuerst auf sich - ein sofortiges Löschen nähme ihm die frische
    //  Markierung wieder weg.
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
        implicitWidth:  control.checkable ? 15 : 0
        implicitHeight: control.checkable ? 15 : 0
        visible: control.checkable
        radius: 4

        color: control.checked ? App.themeAccent : "transparent"
        border.width: 1
        border.color: control.checked ? App.themeAccent : App.themeBorder
        opacity: control.enabled ? 1.0 : 0.5

        Behavior on color { ColorAnimation { duration: 110 } }

        Item {
            anchors.centerIn: parent
            width: 11; height: 11
            visible: control.checked
            Rectangle {
                x: 1; y: 6; width: 4.5; height: 1.8; radius: 0.9
                color: App.themeBackground
                transformOrigin: Item.Left
                rotation: 45
            }
            Rectangle {
                x: 3.3; y: 7.8; width: 7.8; height: 1.8; radius: 0.9
                color: App.themeBackground
                transformOrigin: Item.Left
                rotation: -50
            }
        }
    }

    //  Untermenü-Pfeil (Dreieck nach rechts bzw. links bei RTL).
    arrow: Item {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth:  control.subMenu ? 12 : 0
        implicitHeight: control.subMenu ? 12 : 0
        visible: control.subMenu

        Canvas {
            anchors.centerIn: parent
            width: 6; height: 9
            readonly property color arrowColor: control.enabled ? App.themeTextMuted
                                                                : App.themeBorder
            readonly property bool flip: control.mirrored
            onArrowColorChanged: requestPaint()
            onFlipChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = arrowColor
                ctx.beginPath()
                if (flip) { ctx.moveTo(width, 0); ctx.lineTo(width, height); ctx.lineTo(0, height / 2) }
                else      { ctx.moveTo(0, 0);     ctx.lineTo(0, height);     ctx.lineTo(width, height / 2) }
                ctx.closePath(); ctx.fill()
            }
        }
    }

    contentItem: Text {
        //  Platz für Markierung bzw. Pfeil freihalten (wie Fusion), damit die
        //  Beschriftungen eines Menüs untereinander fluchten.
        readonly property real indicatorPad: control.checkable && control.indicator
                                             ? control.indicator.width + control.spacing : 0
        readonly property real arrowPad: control.subMenu && control.arrow
                                         ? control.arrow.width + control.spacing : 0
        leftPadding:  !control.mirrored ? indicatorPad : arrowPad
        rightPadding:  control.mirrored ? indicatorPad : arrowPad

        text: control.text
        font: control.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        color: control.enabled ? App.themeTextPrimary : App.themeTextMuted
    }

    background: Rectangle {
        //  s. Kopfkommentar - Mindestbreite für ThemedMenu.
        implicitWidth: 200
        implicitHeight: 26
        radius: 4
        color: control.highlighted || control.down
               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20)
               : "transparent"
    }
}
