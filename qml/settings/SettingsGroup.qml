import QtQuick
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Einheitlicher Titel-Container; Kinder kommen in die innere ColumnLayout. Der Klappzustand überlebt den
// Neustart, sofern die Gruppe einen `key` trägt - einen STABILEN Bezeichner ("view.tiles"), nicht die
// übersetzte Überschrift. Die HÖHE muss mitgehen, sonst behielte die Gruppe zugeklappt ihre volle Höhe.
// Ohne eigene Fläche: die kleine Versal-Überschrift gliedert, Platten in Platten lasen sich als Fenster im Fenster.
Item {
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

    readonly property int padTop: title.length > 0 ? 26 : 0
    //  Ohne Platten trennt nur der Abstand die Gruppen voneinander.
    readonly property int padBottom: 12

    implicitWidth:  inner.implicitWidth
    implicitHeight: group.collapsed ? group.padTop
                                    : inner.implicitHeight + padTop + padBottom

    //  Kopfzeile über die ganze Breite, damit man nicht auf den Pfeil zielen muss.
    Item {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: group.padTop
        visible: group.title.length > 0

        Text {
            id: caption
            anchors { left: parent.left; top: parent.top }
            width: Math.min(implicitWidth, head.width - 16)
            text: group.title
            elide: Text.ElideRight
            color: headHover.hovered ? App.themeTextPrimary : App.themeTextMuted
            font.pixelSize: 11
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1.2
        }
        //  Der Pfeil steht HINTER der Versalzeile, damit alle Überschriften bündig mit dem Inhalt beginnen.
        DrawnIcon {
            anchors { left: caption.right; leftMargin: 5; verticalCenter: caption.verticalCenter }
            visible: group.collapsible && (group.collapsed || headHover.hovered)
            name: group.collapsed ? "chevron-right" : "chevron-down"
            size: 10
            color: App.themeTextMuted
        }
        HoverHandler { id: headHover; enabled: group.collapsible }
        TapHandler  { enabled: group.collapsible; onTapped: group.toggle() }
    }

    ColumnLayout {
        id: inner
        y: group.padTop
        width: group.width
        spacing: group.contentSpacing
        visible: !group.collapsed
    }
}
