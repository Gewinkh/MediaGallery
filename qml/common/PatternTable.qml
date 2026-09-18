pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MediaGallery 1.0

//  Zwei Spalten statt einer Textwand: das Muster links in fester Schrift, die
//  Erklaerung rechts. Als Raster beginnen alle Erklaerungen an derselben Stelle,
//  egal wie lang das Muster daneben ist.
Rectangle {
    id: tab

    //  Zeilen der Form „Muster<Tab>Erklaerung".
    property string quelle: ""
    property color  textFarbe: App.themeTextPrimary

    readonly property var zeilen:
        tab.quelle.length > 0 ? tab.quelle.split("\n").map(z => z.split("\t"))
                              : []

    Layout.fillWidth: true
    color: App.themeBackground
    border.color: App.themeBorder
    border.width: 1
    radius: 6
    implicitHeight: raster.implicitHeight + 20

    GridLayout {
        id: raster
        anchors { left: parent.left; right: parent.right
                  top: parent.top; margins: 10 }
        columns: 3
        columnSpacing: 10
        rowSpacing: 4

        Repeater {
            model: tab.zeilen.length * 3
            delegate: Item {
                id: zelle
                required property int index
                readonly property int _zeile: Math.floor(zelle.index / 3)
                readonly property int _spalte: zelle.index % 3

                implicitWidth: zelle._spalte === 1 ? 1 : inhalt.implicitWidth
                implicitHeight: Math.max(18, inhalt.implicitHeight)
                Layout.fillWidth: zelle._spalte === 2
                Layout.fillHeight: zelle._spalte === 1
                Layout.alignment: Qt.AlignTop

                //  Mittlere Spalte: der Trennstrich zwischen dem Muster und
                //  seiner Erklaerung.
                Rectangle {
                    visible: zelle._spalte === 1
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 2
                    width: 1
                    height: Math.max(14, zelle.height - 4)
                    color: App.themeBorder
                }
                Text {
                    id: inhalt
                    visible: zelle._spalte !== 1
                    width: zelle._spalte === 2 ? zelle.width : implicitWidth
                    text: zelle._spalte === 0
                          ? tab.zeilen[zelle._zeile][0]
                          : (tab.zeilen[zelle._zeile][1] || "")
                    color: zelle._spalte === 0 ? App.themeAccent : tab.textFarbe
                    font.family: zelle._spalte === 0 ? "monospace" : ""
                    font.pixelSize: 12
                    font.bold: zelle._spalte === 0
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
