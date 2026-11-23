import QtQuick
import MediaGallery 1.0

// Schräger Akzentstrich vor Dialogtiteln; geschert wie die Profilkarten, nicht als Shape.
Item {
    id: sl
    property color tint: App.themeAccent
    readonly property real neig: height * 0.3
    width: 3 + neig
    height: 15
    Rectangle {
        width: 3
        height: parent.height
        color: sl.tint
        antialiasing: true
        transform: Matrix4x4 { matrix: Qt.matrix4x4(1, -0.3, 0, sl.neig, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) }
    }
}
