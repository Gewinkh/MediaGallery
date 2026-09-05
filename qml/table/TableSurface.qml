pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

//  Ansicht einer gewoehnlichen Tabellendatei (CSV/TSV): Spalten, Zeilen, und in
//  der Fusszeile das, was beim Lesen ENTSCHIEDEN wurde - Trennzeichen und
//  Kopfzeile. Beides ist umstellbar, weil Raten manchmal danebenliegt.
//  Zeigt nur an; Bearbeiten ist noch nicht gebaut.
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0
    //  Zeilen- und Spaltennummern; der Schalter dafuer sitzt in der oberen
    //  Leiste neben dem Umschalter Tabelle/Rohtext.
    property bool   showNumbers: false

    readonly property string currentPath: root.source

    TableController {
        id: ctl
        source: root.source
    }

    Rectangle { anchors.fill: parent; color: Editor.background }

    //  Mehrere Tabellen in EINER Datei: je Block ein Reiter, dazu „Alles" als
    //  Rueckfallweg. Getrennt wird an Leerzeilen - steht die Trennung nicht in
    //  der Datei, gibt es nur einen Block und die Leiste bleibt weg.
    Rectangle {
        id: reiter
        anchors { left: parent.left; right: parent.right
                  top: parent.top; topMargin: root.topInset }
        height: ctl.blockCount > 1 ? 26 : 0
        visible: ctl.blockCount > 1
        color: Editor.gutterBackground
        clip: true
        z: 2

        Row {
            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
            spacing: 4

            component Reiter: Rectangle {
                id: rt
                property string beschriftung: ""
                property bool   an: false
                signal geklickt()
                height: 20
                width: rtText.implicitWidth + 18
                radius: 4
                color: rt.an ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                       App.themeAccent.b, 0.30)
                     : (rtHover.hovered ? Qt.rgba(Editor.text.r, Editor.text.g,
                                                  Editor.text.b, 0.14) : "transparent")
                border.width: 1
                border.color: rt.an ? App.themeAccent
                                    : Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                              Editor.gutterText.b, 0.35)
                Text {
                    id: rtText
                    anchors.centerIn: parent
                    color: rt.an ? Editor.gutterTextActive : Editor.gutterText
                    font.pixelSize: 11
                    font.bold: rt.an
                    text: rt.beschriftung
                }
                HoverHandler { id: rtHover }
                TapHandler { onTapped: rt.geklickt() }
            }

            Repeater {
                model: ctl.blocks
                delegate: Reiter {
                    required property var modelData
                    beschriftung: modelData.title + "  (" + modelData.rows + ")"
                    an: ctl.currentBlock === modelData.index
                    onGeklickt: ctl.currentBlock = modelData.index
                }
            }
            Reiter {
                beschriftung: App.uiText(App.language, "TableAllBlocks")
                an: ctl.currentBlock < 0
                onGeklickt: ctl.currentBlock = -1
            }
        }

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.35) }
    }

    DataTable {
        id: tabelle
        anchors { left: parent.left; right: parent.right
                  top: reiter.bottom; bottom: fuss.top }
        provider: ctl
        showNumbers: root.showNumbers
        //  Die Namensleiste erscheint nur, wenn die Datei Spaltennamen traegt -
        //  sonst waere es ein leerer Streifen. Die Nummern haben ihre eigene.
        showHeader: ctl.headerRow
    }

    Text {
        anchors.centerIn: parent
        visible: !ctl.ready
        color: Editor.gutterText
        font.pixelSize: 13
        text: ctl.busy ? App.uiText(App.language, "DatevBusy")
                       : App.uiText(App.language, "TableLoadError")
    }

    Rectangle {
        id: fuss
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  bottomMargin: root.bottomInset }
        height: fussZeile.height + 14
        color: Editor.gutterBackground
        z: 2

        Rectangle { anchors.top: parent.top; width: parent.width; height: 1
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.25) }

        Row {
            id: fussZeile
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: 12; rightMargin: 12; topMargin: 5 }
            spacing: 14
            height: 18

            component Feld: Text {
                color: Editor.gutterText
                font.pixelSize: 11
                height: 18
                verticalAlignment: Text.AlignVCenter
            }

            Feld { text: App.uiText(App.language, "TableRows") + ": " + ctl.rowCount }
            Feld {
                visible: ctl.blockCount > 1
                //  Im Rueckfallweg steht kein Block zur Auswahl - „0/5" laese
                //  sich als „der nullte von fuenf" missverstehen.
                text: App.uiText(App.language, "TableBlock") + ": "
                      + (ctl.currentBlock < 0
                         ? App.uiText(App.language, "TableAllBlocks")
                         : (ctl.currentBlock + 1) + "/" + ctl.blockCount)
            }
            Feld { text: App.uiText(App.language, "TableColumns") + ": " + ctl.columnCount }

            //  Was beim Lesen ERKANNT wurde - als Angabe, nicht als Schalter:
            //  das Raten trifft, und vier Knoepfe fuer den Ausnahmefall standen
            //  dauerhaft im Weg.
            Feld {
                text: App.uiText(App.language, "TableSeparator") + ": "
                      + (ctl.separator === "\t" ? App.uiText(App.language, "TableTabSep")
                                                : ctl.separator)
            }
            Feld {
                visible: ctl.headerRow
                text: App.uiText(App.language, "TableHeaderRow")
            }
            Feld { visible: ctl.cp1252; text: App.uiText(App.language, "DatevCp1252") }
            Feld { visible: ctl.truncated; color: "#d2a04f"
                   text: App.uiText(App.language, "DatevTruncated") }
            Feld {
                visible: ctl.warnings.length > 0
                color: "#d2a04f"
                text: App.uiText(App.language, "DatevWarnings") + ": "
                      + ctl.warnings.slice(0, 3).join(" · ")
                      + (ctl.warnings.length > 3 ? " …" : "")
            }
        }
    }
}
