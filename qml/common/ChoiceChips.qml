pragma ComponentBehavior: Bound
import QtQuick
import MediaGallery 1.0

// ChoiceChips - eine Reihe Wahlknoepfe, von denen genau einer gilt.
Row {
    id: root

    property var labels: []
    property int current: 0
    signal picked(int index)

    spacing: 6

    Repeater {
        model: root.labels
        delegate: Rectangle {
            id: chip
            required property string modelData
            required property int index
            readonly property bool gewaehlt: root.current === chip.index
            height: 24
            width: beschriftung.implicitWidth + 18
            radius: 5
            color: chip.gewaehlt ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                 : (schwebe.hovered ? App.themeCard : "transparent")
            border.width: 1
            border.color: chip.gewaehlt ? App.themeAccent : App.themeBorder
            Text {
                id: beschriftung
                anchors.centerIn: parent
                text: chip.modelData
                color: chip.gewaehlt ? App.themeAccent : App.themeTextPrimary
                font.pixelSize: 11
            }
            HoverHandler { id: schwebe }
            TapHandler { onTapped: root.picked(chip.index) }
        }
    }
}
