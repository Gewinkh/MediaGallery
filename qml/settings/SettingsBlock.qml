import QtQuick
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Großer, klappbarer Abschnitt eines Reiters: nur Überschrift mit Linie, keine eigene Fläche - eine dunklere Platte
// hob Design und Verschlagwortung sichtbar von den übrigen Reitern ab.
Item {
    id: big
    default property alias content: innerCol.data
    property string title: ""
    property string groupKey: ""
    property bool collapsed: false

    Layout.fillWidth: true
    implicitHeight: big.collapsed ? head.height
                                  : (head.height + 18 + innerCol.implicitHeight)

    Component.onCompleted:
        if (big.groupKey.length > 0)
            big.collapsed = App.settingsGroupCollapsed(big.groupKey)
    onCollapsedChanged:
        if (big.groupKey.length > 0)
            App.setSettingsGroupCollapsed(big.groupKey, big.collapsed)

    Item {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 42

        DrawnIcon {
            id: chev
            anchors { left: parent.left; verticalCenter: titleText.verticalCenter }
            name: big.collapsed ? "chevron-right" : "chevron-down"
            size: 13
            color: headHover.hovered ? App.themeTextPrimary : App.themeTextMuted
        }
        Text {
            id: titleText
            anchors { left: chev.right; leftMargin: 10; verticalCenter: parent.verticalCenter
                      verticalCenterOffset: -2 }
            text: big.title
            color: App.themeTextPrimary
            font.pixelSize: 17
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: App.themeBorder
        }
        HoverHandler { id: headHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: big.collapsed = !big.collapsed }
    }

    ColumnLayout {
        id: innerCol
        anchors { left: parent.left; right: parent.right; top: head.bottom; topMargin: 18 }
        spacing: 22
        visible: !big.collapsed
    }
}
