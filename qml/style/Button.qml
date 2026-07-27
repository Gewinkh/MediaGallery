import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  Button.qml — gethemter Standard-Button (Stil "style").
//
//  Ersetzt die eckige, palettengetriebene Fusion-Variante durch die Optik der
//  handgebauten Leisten-Knöpfe der App (ChromeBtn/MenuBtn/SettingsTab): Radius 6,
//  Karten-Hintergrund, Rahmen in Themenfarbe, weiche Hover-/Druck-Zustände.
//  Alle Farben kommen aus dem App-Singleton → das frei einstellbare Farbschema
//  wirkt auch auf die Standard-Controls, unabhängig vom Desktop-Palette-Theme.
// ─────────────────────────────────────────────────────────────────────────────
T.Button {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6
    leftPadding: 14
    rightPadding: 14
    spacing: 6
    font.pixelSize: 13

    //  Akzent-Zustand: „highlighted" (Bestätigen/Primäraktion) und „checked"
    //  (Umschalter) teilen sich dieselbe Akzent-Darstellung.
    readonly property bool _accent: control.highlighted || control.checked

    contentItem: Text {
        text: control.text
        font: control.font
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: !control.enabled ? App.themeTextMuted
                                : (control._accent ? App.themeAccent : App.themeTextPrimary)
        opacity: control.enabled ? 1.0 : 0.6
    }

    background: Rectangle {
        implicitWidth: 84
        implicitHeight: 28
        radius: 6

        color: {
            if (control._accent)
                return Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b,
                               control.down ? 0.34 : (control.hovered ? 0.26 : 0.18))
            if (control.flat && !control.down && !control.hovered)
                return "transparent"
            return control.down ? Qt.darker(App.themeCard, 1.25)
                                : (control.hovered ? Qt.lighter(App.themeCard, 1.35)
                                                   : App.themeCard)
        }
        border.width: 1
        border.color: !control.enabled
                      ? App.themeBorder
                      : (control._accent || control.visualFocus
                         ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.75)
                         : App.themeBorder)
        opacity: control.enabled ? 1.0 : 0.5

        Behavior on color { ColorAnimation { duration: 110 } }
    }
}
