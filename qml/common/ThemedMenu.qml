import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// Eigener Hintergrund, weil Fusions weder Themenfarbe noch Rundung folgt; ohne `implicitWidth` kollabiert er zum
// 2-px-Strich (contentWidth liefert hier 0). `topPadding`/`bottomPadding` >= Radius halten die runden Ecken frei.
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
    // Echte Zeilenhöhen statt der beim Rollen nachgeschätzten contentHeight - jede Änderung ließ die Liste nach oben springen.
    implicitHeight: {
        var h = 0
        for (var i = 0; i < count; i++) {
            var it = itemAt(i)
            if (it) h += it.height
        }
        return h + topPadding + bottomPadding
    }

    background: Rectangle {
        implicitWidth: 200
        color: App.themeMenuBarBg
        border.color: App.themeBorder; border.width: 1
        radius: 6
    }
}
