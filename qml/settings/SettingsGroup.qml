import QtQuick
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  SettingsGroup.qml - einheitlicher Titel-Container (ersetzt QGroupBox-Stil).
//  Kinder werden in die innere ColumnLayout aufgenommen (default property alias).
//  implicitHeight/Width leiten sich aus dem Inhalt ab -> passt in ColumnLayouts.
//
//  AUF- UND ZUKLAPPEN: Jede Gruppe mit einer Überschrift lässt sich zuklappen;
//  der Zustand überlebt den Neustart, sofern die Gruppe einen `key` trägt.
//   • **`key` ist ein STABILER Bezeichner** ("view.tiles"), NICHT die
//     Überschrift - die ist übersetzt und hieße nach einem Sprachwechsel
//     anders, der gemerkte Zustand wäre verloren.
//   • **Ohne `key` klappt die Gruppe trotzdem**, merkt es sich aber nicht.
//   • Gelesen wird EINMAL beim Entstehen, geschrieben beim Umschalten - kein
//     Signal nötig, es gibt keinen zweiten Leser (s. `AppController`).
//
//  Die HÖHE muss mitgehen: `implicitHeight` kommt aus dem Inhalt. Würde nur der
//  Inhalt unsichtbar geschaltet, behielte die Gruppe ihre volle Höhe und es sähe
//  aus, als klemme das Klappen.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: group
    property string title: ""
    property int contentSpacing: 10
    default property alias content: inner.data

    //  Stabiler Schlüssel für den gemerkten Klappzustand; leer = nicht gemerkt.
    property string key: ""
    //  Eine Gruppe ohne Überschrift hat nichts, woran man klappen könnte -
    //  deshalb die Vorgabe. Ein Reiter kann es zusätzlich ABSCHALTEN, wenn seine
    //  einzige Gruppe der ganze Reiter ist: dort wäre ein Pfeil nur ein Weg,
    //  sich die Seite selbst wegzuklicken (Converter, Festlegung des Nutzers).
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
