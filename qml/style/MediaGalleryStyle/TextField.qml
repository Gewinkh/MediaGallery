import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  TextField.qml — gethemtes Eingabefeld (Stil "MediaGalleryStyle").
//
//  Radius 6, Kartenhintergrund, Akzentrahmen bei Fokus. Farbe/Auswahlfarben aus
//  dem Themenschema; `color` bleibt überschreibbar (viele Aufrufstellen setzen
//  sie bereits explizit auf App.themeTextPrimary — das gewinnt weiterhin).
// ─────────────────────────────────────────────────────────────────────────────
T.TextField {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding,
                             28)

    padding: 6
    leftPadding: 9
    rightPadding: 9
    font.pixelSize: 13

    color: App.themeTextPrimary
    selectionColor: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.45)
    selectedTextColor: App.themeTextPrimary
    placeholderTextColor: App.themeTextMuted
    verticalAlignment: TextInput.AlignVCenter

    Text {
        //  Platzhalter (die Templates-Basis liefert keinen eigenen).
        x: control.leftPadding
        y: control.topPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height - control.topPadding - control.bottomPadding
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        elide: Text.ElideRight
        visible: !control.length && !control.preeditText
                 && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 28
        radius: 6
        color: control.enabled ? App.themeCard : Qt.darker(App.themeCard, 1.15)
        border.width: 1
        border.color: control.activeFocus
                      ? App.themeAccent
                      : (control.hovered ? Qt.lighter(App.themeBorder, 1.4) : App.themeBorder)
        opacity: control.enabled ? 1.0 : 0.55

        Behavior on border.color { ColorAnimation { duration: 110 } }
    }
}
