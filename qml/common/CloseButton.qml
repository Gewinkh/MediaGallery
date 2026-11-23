import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// Runder Schliessknopf oben rechts in Dialogen.
Rectangle {
    id: btn
    property string toolTipText: App.uiText(App.language, "SettingsClose")
    signal clicked()

    width: 34; height: 34; radius: 17
    color: Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b,
                   hover.hovered ? 0.14 : 0.0)
    border.color: App.themeBorder
    //  Zwei um DIESELBE Mitte gedrehte Balken: symmetrisch auf jeder Skalierung; das 12-px-Symbol zerfiel bei 1,25.
    Repeater {
        model: [45, -45]
        delegate: Rectangle {
            required property int modelData
            x: (btn.width - width) / 2
            y: (btn.height - height) / 2
            width: 14
            height: 1.6
            radius: 0.8
            rotation: modelData
            antialiasing: true
            color: hover.hovered ? App.themeTextPrimary : App.themeTextMuted
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: btn.clicked() }
    ToolTip.text: btn.toolTipText
    ToolTip.visible: hover.hovered
    ToolTip.delay: 500
}
