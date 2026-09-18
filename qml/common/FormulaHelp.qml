pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MediaGallery 1.0

//  Die Formel-Hilfe: oben die Beispieltabelle, darunter je Formel ihr Ergebnis
//  AUF DIESER Tabelle. Dass die genannten Ergebnisse stimmen, rechnet
//  `table.formulahelp` an der echten Maschine nach.
Rectangle {
    id: hilfe

    //  Zeilen „Zelle<Tab>Zelle<Tab>…", erste Zeile die Spaltennamen.
    property string demo: ""
    //  Zeilen „Formel<Tab>Ergebnis<Tab>Erklaerung"; leeres Ergebnis = nichts zu rechnen.
    property string zeilen: ""

    readonly property var _demo:
        hilfe.demo.length > 0 ? hilfe.demo.split("\n").map(z => z.split("\t")) : []
    readonly property var _hilfe:
        hilfe.zeilen.length > 0 ? hilfe.zeilen.split("\n").map(z => z.split("\t")) : []
    readonly property int _spalten: hilfe._demo.length > 0 ? hilfe._demo[0].length : 0

    Layout.fillWidth: true
    color: App.themeBackground
    border.color: App.themeBorder
    border.width: 1
    radius: 6
    implicitHeight: inhalt.implicitHeight + 20

    ColumnLayout {
        id: inhalt
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 10

        //  Gezeichnet wie die echte: die Namen oben und links sind genau die,
        //  die eine Formel nennt.
        Rectangle {
            Layout.fillWidth: false
            implicitWidth: raster.implicitWidth + 2
            implicitHeight: raster.implicitHeight + 2
            color: "transparent"
            border.color: App.themeBorder
            border.width: 1
            radius: 4

            GridLayout {
                id: raster
                anchors.centerIn: parent
                columns: hilfe._spalten + 1
                columnSpacing: 0
                rowSpacing: 0

                Repeater {
                    model: (hilfe._demo.length + 1) * (hilfe._spalten + 1)
                    delegate: Rectangle {
                        id: feld
                        required property int index
                        readonly property int _z: Math.floor(feld.index / (hilfe._spalten + 1))
                        readonly property int _s: feld.index % (hilfe._spalten + 1)
                        readonly property bool _kopf: feld._z === 0 || feld._s === 0
                        //  Die Spaltennamen der DATEI stehen ueber Zeile 1 und
                        //  bekommen keine Nummer: eine Formel zaehlt Datenzeilen.
                        readonly property string _text:
                            feld._z === 0 && feld._s === 0 ? ""
                            : feld._z === 0 ? String.fromCharCode(64 + feld._s)
                            : feld._s === 0 ? (feld._z === 1 ? "" : String(feld._z - 1))
                            : (hilfe._demo[feld._z - 1][feld._s - 1] || "")

                        implicitWidth: Math.max(feld._s === 0 ? 24 : 62, zellText.implicitWidth + 14)
                        implicitHeight: 21
                        Layout.fillWidth: feld._s > 0
                        color: feld._kopf ? Editor.gutterBackground : "transparent"

                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                                    visible: feld._s < hilfe._spalten
                                    color: Qt.rgba(App.themeBorder.r, App.themeBorder.g,
                                                   App.themeBorder.b, 0.6) }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                                    visible: feld._z < hilfe._demo.length
                                    color: Qt.rgba(App.themeBorder.r, App.themeBorder.g,
                                                   App.themeBorder.b, 0.6) }
                        Text {
                            id: zellText
                            anchors.centerIn: parent
                            text: feld._text
                            color: feld._kopf || feld._z === 1 ? App.themeTextMuted
                                                               : App.themeTextPrimary
                            font.family: "monospace"
                            font.pixelSize: 11
                            font.bold: feld._kopf
                        }
                    }
                }
            }
        }

        GridLayout {
            id: liste
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 10
            rowSpacing: 5

            Repeater {
                model: hilfe._hilfe.length * 3
                delegate: Item {
                    id: zelle
                    required property int index
                    readonly property int _z: Math.floor(zelle.index / 3)
                    readonly property int _s: zelle.index % 3
                    readonly property string _wert: hilfe._hilfe[zelle._z][1] || ""

                    implicitWidth: zelle._s === 1 ? ergebnis.width : eintrag.implicitWidth
                    implicitHeight: Math.max(20, eintrag.implicitHeight)
                    Layout.fillWidth: zelle._s === 2
                    Layout.alignment: Qt.AlignTop

                    //  Das Ergebnis in eigener Flaeche - es ist das, was die Zelle
                    //  ZEIGT. Kein gezeichneter Pfeil davor: ein `DrawnIcon` mit
                    //  schraegen Strichen zeichnete hier ueber den Rand des
                    //  rollenden Bereichs hinaus, bis unter das Fenster.
                    Rectangle {
                        id: ergebnis
                        visible: zelle._s === 1 && zelle._wert.length > 0
                        anchors.top: parent.top
                        width: wertText.implicitWidth + 12
                        height: 19
                        radius: 4
                        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                       App.themeAccent.b, 0.12)
                        border.color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                              App.themeAccent.b, 0.35)
                        border.width: 1
                        Text {
                            id: wertText
                            anchors.centerIn: parent
                            text: zelle._wert
                            color: App.themeTextPrimary
                            font.family: "monospace"
                            font.pixelSize: 12
                        }
                    }

                    Text {
                        id: eintrag
                        visible: zelle._s !== 1
                        width: zelle._s === 2 ? zelle.width : implicitWidth
                        text: zelle._s === 0 ? hilfe._hilfe[zelle._z][0]
                                             : (hilfe._hilfe[zelle._z][2] || "")
                        color: zelle._s === 0 ? App.themeAccent : App.themeTextMuted
                        font.family: zelle._s === 0 ? "monospace" : ""
                        font.pixelSize: 12
                        font.bold: zelle._s === 0
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
