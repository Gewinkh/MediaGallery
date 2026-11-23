import QtQuick
import MediaGallery 1.0

// Tafel aller Aufklapper: Rundung, Rahmen, vorgerechneter Schatten (kein Live-Weichzeichner, der Software-Zeichner
// blieb sonst bei jedem Oeffnen haengen). `implicitWidth` haelt Menues mit eigenem Hintergrund breit.
Item {
    id: panel
    property color color: App.themeCard
    property real radius: 8
    implicitWidth: 160

    ShadowItem {
        anchors.fill: parent
        radius: panel.radius
        spread: 24
        strength: 0.7
        offsetY: 8
    }
    Rectangle {
        anchors.fill: parent
        radius: panel.radius
        color: panel.color
        border.color: App.themeBorder
        border.width: 1
    }
}
