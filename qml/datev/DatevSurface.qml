pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"
import "../table"

//  Ansicht eines DATEV-Buchungsstapels: Dateikopf, Buchungen als Tabelle,
//  Summen darunter. NUR LESEND - in eine Buchungsdatei schreibt die App nicht.
//  Der Rohtext steht weiter im Texteditor; umgeschaltet wird oben in der Leiste.
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0
    //  Zeilen- und Spaltennummern; der Schalter sitzt in der oberen Leiste.
    property bool   showNumbers: false
    //  Nur die aktive Haelfte darf auf Strg+F antworten.
    property bool   paneActive: true

    readonly property string currentPath: root.source
    //  Fuer die Kopfleiste des Viewers (Filter, Spaltensuche).
    readonly property alias controller: ctl

    property bool _findOpen: false
    //  Strg+F ist ein SCHALTER, wie in der PDF-Ansicht: der zweite Druck raeumt
    //  die Leiste samt Treffern wieder weg.
    function oeffneSuche() {
        if (root._findOpen) { suchBalken.schliessen(); return }
        root._findOpen = true
        suchBalken.oeffnen()
    }

    //  Zahlen in der Sprache der Oberflaeche - eine Buchhaltungsdatei ist
    //  deutsch geschrieben, die Oberflaeche muss es nicht sein.
    readonly property var _loc: Qt.locale(App.language === "en" ? "en_US" : "de_DE")
    function _geld(v) { return Number(v).toLocaleString(root._loc, 'f', 2) }

    DatevController {
        id: ctl
        source: root.source
        slashDateMonthFirst: App.tableDateMonthFirst
        groupDigits: App.tableGroupDigits
    }

    Rectangle { anchors.fill: parent; color: Editor.background }

    //  ── Kopf ────────────────────────────────────────────────────────────────
    Rectangle {
        id: kopf
        anchors { left: parent.left; right: parent.right; top: parent.top
                  topMargin: root.topInset }
        height: kopfSpalte.height + 16
        color: Editor.gutterBackground
        z: 2

        Column {
            id: kopfSpalte
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: 12; rightMargin: 12; topMargin: 8 }
            spacing: 4

            Text {
                width: parent.width
                elide: Text.ElideRight
                color: Editor.text
                font.pixelSize: 14
                font.bold: true
                text: ctl.ready
                      ? (ctl.identifier + " " + ctl.version
                         + (ctl.formatName.length > 0 ? "  -  " + ctl.formatName : ""))
                      : (ctl.busy ? App.uiText(App.language, "DatevBusy")
                                  : App.uiText(App.language, "DatevLoadError"))
            }
            Text {
                visible: ctl.ready && ctl.createdAt.length > 0
                color: Editor.gutterText
                font.pixelSize: 11
                text: App.uiText(App.language, "DatevFieldCreated") + ": " + ctl.createdAt
            }

            //  Alle Kopffelder - eingeklappt, weil die meisten leer sind.
            Row {
                spacing: 6
                visible: ctl.ready
                Rectangle {
                    width: 16; height: 16; radius: 3
                    color: kopfHover.hovered ? Qt.rgba(Editor.text.r, Editor.text.g,
                                                       Editor.text.b, 0.12) : "transparent"
                    DrawnIcon {
                        anchors.centerIn: parent
                        size: 14
                        name: root.kopfOffen ? "chevron-down" : "chevron-right"
                        color: Editor.gutterText
                    }
                    HoverHandler { id: kopfHover }
                    TapHandler { onTapped: root.kopfOffen = !root.kopfOffen }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Editor.gutterText
                    font.pixelSize: 11
                    text: App.uiText(App.language, "DatevHeaderTitle")
                    TapHandler { onTapped: root.kopfOffen = !root.kopfOffen }
                }
            }

            Flow {
                width: parent.width
                visible: root.kopfOffen && ctl.ready
                spacing: 14
                Repeater {
                    model: ctl.headerFields
                    delegate: Text {
                        required property var modelData
                        color: Editor.gutterText
                        font.pixelSize: 11
                        font.family: tabelle.cellFont.family
                        text: (modelData.name.length > 0
                               ? modelData.name
                               : (App.uiText(App.language, "DatevHeaderTitle")
                                  + " " + modelData.number))
                              + ": " + (modelData.value.length > 0 ? modelData.value : "-")
                    }
                }
            }
        }

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.25) }
    }
    property bool kopfOffen: false

    //  ── Tabelle ─────────────────────────────────────────────────────────────
    //  Der Koerper ist geteilt (`qml/table/DataTable.qml`) - Rollverhalten,
    //  Balken und Spaltenbreiten sind bei CSV dieselben. DATEV-eigen sind nur
    //  Kopf und Fuss.
    //  `root.visible` ist die WIRKSAME Sichtbarkeit (der Rohtext blendet die
    //  ganze Flaeche aus) - ohne sie faenge auch die unsichtbare Tabelle das
    //  Strg+F des Texteditors ab.
    Shortcut {
        sequence: "Ctrl+F"
        enabled: root.visible && root.paneActive && ctl.ready
        onActivated: root.oeffneSuche()
    }
    //  Kopieren bezieht sich auf die ANGEKLICKTE Zelle.
    Shortcut {
        //  `sequences`, nicht `sequence`: Qt kennt zu StandardKey.Copy mehrere
        //  Folgen (Strg+C und Strg+Einfg), und `sequence` nimmt nur die erste.
        sequences: [ StandardKey.Copy ]
        enabled: root.visible && root.paneActive && ctl.ready
        onActivated: tabelle.kopiereZelle()
    }
    Shortcut {
        sequence: "Ctrl+Shift+C"
        enabled: root.visible && root.paneActive && ctl.ready
        onActivated: tabelle.kopiereZeile()
    }

    //  Der Sprung zum Treffer gehoert der Flaeche - nur sie kennt den Koerper.
    Connections {
        target: ctl
        function onSearchChanged() {
            if (!ctl.searching && ctl.matchRow >= 0)
                tabelle.zeigeZelle(ctl.matchRow, ctl.matchColumn)
        }
    }

    TableFindBar {
        id: suchBalken
        visible: root._findOpen
        z: 6
        anchors { top: kopf.bottom; topMargin: 8
                  right: parent.right; rightMargin: 18 }
        ctl: ctl
        fromRow: Math.floor(tabelle.rows.contentY / tabelle.rowHeight)
        onGeschlossen: root._findOpen = false
    }

    DataTable {
        id: tabelle
        anchors { left: parent.left; right: parent.right
                  top: kopf.bottom; bottom: fuss.top }
        provider: ctl
        showNumbers: root.showNumbers
        searchRevision: ctl.searchRevision
        contentRevision: ctl.contentRevision
        currentRow: ctl.matchRow
        currentColumn: ctl.matchColumn
    }

    //  ── Fuss ────────────────────────────────────────────────────────────────
    Rectangle {
        id: fuss
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  bottomMargin: root.bottomInset }
        height: fussSpalte.height + 14
        color: Editor.gutterBackground
        z: 2

        Rectangle { anchors.top: parent.top; width: parent.width; height: 1
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.25) }

        Column {
            id: fussSpalte
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: 12; rightMargin: 12; topMargin: 5 }
            spacing: 3

            component Feld: Text {
                color: Editor.gutterText
                font.pixelSize: 11
                height: 16
                verticalAlignment: Text.AlignVCenter
            }

            Row {
                spacing: 16
                height: 16
                //  Mit Filter gelten die Summen daneben nur fuer die gezeigten Buchungen.
                Feld {
                    text: App.uiText(App.language, "DatevBookings") + ": "
                          + (ctl.filterActive
                             ? App.uiText(App.language, "TableFiltered").arg(ctl.rowCount).arg(ctl.totalRows)
                             : ctl.rowCount)
                    color: ctl.filterActive ? App.themeAccent : Editor.gutterText
                }
                Feld { text: App.uiText(App.language, "DatevSumDebit") + ": " + root._geld(ctl.sumDebit) }
                Feld { text: App.uiText(App.language, "DatevSumCredit") + ": " + root._geld(ctl.sumCredit) }
                Feld {
                    text: App.uiText(App.language, "DatevSumDiff") + ": " + root._geld(ctl.sumDiff)
                    color: Math.abs(ctl.sumDiff) < 0.005 ? Editor.gutterText : "#d24f4f"
                }
                Feld {
                    text: Math.abs(ctl.sumDiff) < 0.005
                          ? App.uiText(App.language, "DatevBalanced")
                          : App.uiText(App.language, "DatevUnbalanced")
                    color: Math.abs(ctl.sumDiff) < 0.005 ? Editor.gutterText : "#d24f4f"
                }
            }

            Row {
                spacing: 16
                height: 18
                //  Umschalter, kein Fliesstext: 125 Spalten sind nicht lesbar,
                //  die Vorgabe zeigt deshalb nur die gefuellten.
                Rectangle {
                    height: 18
                    width: spaltenText.implicitWidth + 14
                    radius: 4
                    color: spaltenHover.hovered
                           ? Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.14)
                           : "transparent"
                    border.width: 1
                    border.color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                          Editor.gutterText.b, 0.45)
                    Text {
                        id: spaltenText
                        anchors.centerIn: parent
                        color: Editor.gutterText
                        font.pixelSize: 11
                        text: ctl.columns.length + "/" + ctl.columnCount + " "
                              + App.uiText(App.language, ctl.showAllColumns ? "DatevAllColumns"
                                                                            : "DatevUsedColumns")
                    }
                    HoverHandler { id: spaltenHover }
                    TapHandler { onTapped: ctl.showAllColumns = !ctl.showAllColumns }
                }
                //  Einzeln ausgeblendete Spalten muessen SICHTBAR bleiben, sonst
                //  sucht man spaeter eine Spalte, die man selbst weggeklickt hat.
                Feld {
                    visible: ctl.hiddenColumnCount > 0
                    color: App.themeAccent
                    text: App.uiText(App.language, "TableHiddenColumns").arg(ctl.hiddenColumnCount)
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: ctl.showAllHiddenColumns()
                        ToolTip.visible: containsMouse
                        ToolTip.text: App.uiText(App.language, "TableShowAllColumns")
                    }
                }
                Feld { visible: ctl.sorting; text: App.uiText(App.language, "TableSorting") }
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
                Feld { text: App.uiText(App.language, "DatevReadOnly") }
            }
        }
    }
}
