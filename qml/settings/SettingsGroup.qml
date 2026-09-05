import QtQuick
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Einheitlicher Titel-Container; Kinder kommen in die innere ColumnLayout. Der Klappzustand überlebt den
// Neustart, sofern die Gruppe einen `key` trägt - einen STABILEN Bezeichner ("view.tiles"), nicht die
// übersetzte Überschrift. Die HÖHE muss mitgehen, sonst behielte die Gruppe zugeklappt ihre volle Höhe.
Rectangle {
    id: group
    property string title: ""
    property int contentSpacing: 10
    default property alias content: inner.data

    //  Stabiler Schlüssel für den gemerkten Klappzustand; leer = nicht gemerkt.
    property string key: ""
    // Eine Gruppe ohne Überschrift hat nichts, woran man klappen könnte - daher die Vorgabe. Abschalten kann es
    // ein Reiter, dessen einzige Gruppe der ganze Reiter ist (Converter).
    property bool collapsible: group.title.length > 0
    property bool collapsed: false

    Component.onCompleted: {
        if (group.collapsible && group.key.length > 0)
            group.collapsed = App.settingsGroupCollapsed(group.key)
    }
    onCollapsedChanged: {
        if (group.key.length > 0)
            App.setSettingsGroupCollapsed(group.key, group.collapsed)
    }
    function toggle() { if (group.collapsible) group.collapsed = !group.collapsed }

    readonly property int padH:      14
    readonly property int padTop:    title.length > 0 ? 28 : 14
    readonly property int padBottom: 14

    color: Qt.rgba(1, 1, 1, 0.02)
    border.color: App.themeBorder
    radius: 8

    implicitWidth:  inner.implicitWidth + padH * 2
    //  Zugeklappt bleibt nur die Kopfzeile stehen.
    implicitHeight: group.collapsed ? group.padTop + 2
                                    : inner.implicitHeight + padTop + padBottom

    //  Kopfzeile: Pfeil + Überschrift, beides ein Anfasser. Sie reicht über die
    //  ganze Breite, damit man nicht auf den Pfeil zielen muss.
    Item {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: group.padTop
        visible: group.title.length > 0

        DrawnIcon {
            id: chevron
            anchors { left: parent.left; leftMargin: 11; verticalCenter: parent.verticalCenter }
            visible: group.collapsible
            name: group.collapsed ? "chevron-right" : "chevron-down"
            size: 11
            color: headHover.hovered ? App.themeTextPrimary : App.themeTextMuted
        }
        Text {
            anchors { left: group.collapsible ? chevron.right : parent.left
                      leftMargin: group.collapsible ? 5 : 12
                      right: parent.right; rightMargin: 12
                      verticalCenter: parent.verticalCenter }
            text: group.title
            elide: Text.ElideRight
            color: headHover.hovered ? App.themeTextPrimary : App.themeTextMuted
            font.pixelSize: 11
            font.bold: true
        }
        HoverHandler { id: headHover; enabled: group.collapsible }
        TapHandler  { enabled: group.collapsible; onTapped: group.toggle() }
    }

    ColumnLayout {
        id: inner
        x: group.padH
        y: group.padTop
        width: group.width - group.padH * 2
        spacing: group.contentSpacing
        visible: !group.collapsed
    }
}
