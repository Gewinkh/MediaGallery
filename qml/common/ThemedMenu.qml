import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// Menü-Popup im Thema der App - Fusions Hintergrund folgt weder der gewählten Farbe noch der Rundung.
// Ein eigener Hintergrund ohne `implicitWidth` kollabiert zum 2-px-Strich (QQuickMenus contentWidth
// liefert hier 0); `topPadding`/`bottomPadding` >= Radius halten die runden Ecken frei.
Menu {
    id: control

    topPadding: 6
    bottomPadding: 6

    implicitWidth: {
        var w = 0
        for (var i = 0; i < count; i++) {
            var it = itemAt(i)
            if (it && it.implicitWidth > w) w = it.implicitWidth
        }
        return Math.max(w, 200) + leftPadding + rightPadding
    }

    background: Rectangle {
        implicitWidth: 200
        color: App.themeMenuBarBg
        border.color: App.themeBorder; border.width: 1
        radius: 6
    }
}
