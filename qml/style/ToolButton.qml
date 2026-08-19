import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  ToolButton.qml - flacher Werkzeug-Knopf (Stil "style").
//
//  Fusion zeichnete hier einen vollflächigen Knopf-Block mit Verlauf und Rahmen -
//  in den Leisten (Zurück-Pfeil des Viewers, Sortier-Richtung, Baum-Klapppfeile,
//  Video-Steuerung) wirkte das als „Kasten" um ein reines Glyph. Diese Variante
//  folgt dem Muster der handgebauten `ChromeBtn` der Vollbild-Leiste: standardmäßig
//  KEINE Fläche, erst bei Hover/Druck eine abgerundete, dezente Hinterlegung;
//  „checked" trägt die Akzentfarbe.
// ─────────────────────────────────────────────────────────────────────────────
T.ToolButton {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 5
    spacing: 6

    contentItem: Text {
        text: control.text
        font: control.font
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: !control.enabled ? App.themeTextMuted
                                : (control.checked ? App.themeAccent : App.themeTextPrimary)
        opacity: control.enabled ? 1.0 : 0.6
    }

    background: Rectangle {
        implicitWidth: 28
        implicitHeight: 28
        radius: 6
        color: control.checked
               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b,
                         control.down ? 0.38 : 0.26)
               : (control.down ? Qt.rgba(1, 1, 1, 0.20)
                               : (control.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent"))
        border.width: control.checked || control.visualFocus ? 1 : 0
        border.color: App.themeAccent

        Behavior on color { ColorAnimation { duration: 110 } }
    }
}
