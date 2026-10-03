pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

//  Ansicht einer gewoehnlichen Tabellendatei (CSV/TSV): Spalten, Zeilen, und in
//  der Fusszeile das, was beim Lesen erkannt wurde, dazu der Stand des Speicherns.
//  Geschrieben wird wie im Texteditor: beim Verlassen, nach dem Intervall, auf Strg+S.
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0
    //  Zeilen- und Spaltennummern; der Schalter dafuer sitzt in der oberen
    //  Leiste neben dem Umschalter Tabelle/Rohtext.
    property bool   showNumbers: false
    //  Nur die aktive Haelfte darf auf Strg+F antworten.
    property bool   paneActive: true

    readonly property string currentPath: root.source
    //  Fuer die Kopfleiste des Viewers (Filter, Spaltensuche, Umschalten).
    readonly property alias controller: ctl

    property bool _findOpen: false
    //  Strg+F ist ein SCHALTER, wie in der PDF-Ansicht: der zweite Druck raeumt
    //  die Leiste samt Treffern wieder weg.
    function oeffneSuche() {
        if (root._findOpen) { suchBalken.schliessen(); return }
        root._findOpen = true
        suchBalken.oeffnen()
    }

    TableController {
        id: ctl
        source: root.source
        slashDateMonthFirst: App.tableDateMonthFirst
        groupDigits: App.tableGroupDigits
    }

    //  Ein offenes Eingabefeld zaehlt mit - sonst ginge der letzte Wert verloren.
    function uebernehme() { tabelle.uebernehme() }
    function release() { tabelle.uebernehme(); ctl.flush() }

    //  PDF-Vertrag des Viewers (s. TextSurface). Die Farben der Ansicht reisen mit, der Controller kennt sie nicht.
    readonly property string pdfKind: "table"
    readonly property bool pdfBusy: ctl.pdfBusy
    readonly property int pdfBlocks: ctl.blockCount
    property int pdfPages: -1
    property string pdfPreviewPath: ""
    signal pdfFinished(bool ok, string target, string error)
    function _pdfOpt(nativ, quer, alle, von, bis, seiten) {
        return { print: !nativ, landscape: quer, all: alle, grid: App.tableGridLines, first: von, last: bis,
                 pages: seiten || [],
                 font: tabelle.cellFont, background: Editor.background, text: Editor.text,
                 headerBackground: Editor.gutterBackground, headerText: Editor.gutterTextActive }
    }
    function pdfCount(nativ, quer, alle) {
        root.pdfPages = -1
        ctl.countPdfPages(root._pdfOpt(nativ, quer, alle, 1, 0))
    }
    function pdfPreview(nativ, quer, alle) {
        root.pdfPreviewPath = ""
        root.pdfPages = -1
        tabelle.uebernehme()
        ctl.previewPdf(root._pdfOpt(nativ, quer, alle, 1, 0))
    }
    function pdfExport(nativ, quer, alle, von, bis, seiten) {
        tabelle.uebernehme()
        ctl.exportPdf(ctl.pdfTarget(), root._pdfOpt(nativ, quer, alle, von, bis, seiten))
    }
    Connections {
        target: ctl
        function onPdfPagesCounted(n) { root.pdfPages = n }
        function onPdfPreviewReady(pfad, n) { root.pdfPreviewPath = pfad; root.pdfPages = n }
        function onPdfExportFinished(ok, target, error) { root.pdfFinished(ok, target, error) }
    }

    Timer {
        interval: Math.max(5, App.autoSaveInterval) * 1000
        repeat: true
        running: App.autoSaveEnabled && ctl.modified
        onTriggered: ctl.save()
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

    //  `root.visible` ist die WIRKSAME Sichtbarkeit - sonst finge die ausgeblendete
    //  Tabelle das Strg+F des Rohtexts ab.
    Shortcut {
        sequence: "Ctrl+F"
        enabled: root.visible && root.paneActive && ctl.ready
        onActivated: root.oeffneSuche()
    }
    //  Waehrend eine Zelle bearbeitet wird, gehoeren alle diese Tasten dem Feld.
    readonly property bool _tastenFrei: root.visible && root.paneActive && ctl.ready
                                        && !tabelle.bearbeitet
    Shortcut {
        //  `sequences`, nicht `sequence`: Qt kennt zu StandardKey.Copy mehrere
        //  Folgen (Strg+C und Strg+Einfg), und `sequence` nimmt nur die erste.
        sequences: [ StandardKey.Copy ]
        enabled: root._tastenFrei
        onActivated: tabelle.kopiereZelle()
    }
    Shortcut {
        sequence: "Ctrl+Shift+C"
        enabled: root._tastenFrei
        onActivated: tabelle.kopiereZeile()
    }
    Shortcut {
        sequence: "Ctrl+V"
        enabled: root._tastenFrei && ctl.editable
        onActivated: tabelle.einfuegen()
    }
    //  Ausgeschrieben: StandardKey.Redo liefert je nach Plattform-Thema andere Folgen.
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: root._tastenFrei && ctl.canUndo
        onActivated: ctl.undo()
    }
    Shortcut {
        sequences: [ "Ctrl+Shift+Z", "Ctrl+Y" ]
        enabled: root._tastenFrei && ctl.canRedo
        onActivated: ctl.redo()
    }
    //  Gespeichert wird ohnehin von selbst; Strg+S ist der sichtbare Check.
    property bool _checkOffen: false
    Shortcut {
        sequence: "Ctrl+S"
        enabled: root.visible && root.paneActive && ctl.editable
        onActivated: {
            tabelle.uebernehme()
            if (ctl.modified || ctl.saving) {
                root._checkOffen = true
                ctl.save()
            } else {
                tabelle.melde(App.uiText(App.language, "TableSaved"))
            }
        }
    }
    Connections {
        target: ctl
        function onSaved(ok) {
            if (root._checkOffen && ok) tabelle.melde(App.uiText(App.language, "TableSaved"))
            root._checkOffen = false
        }
    }

    //  Der Sprung zum Treffer gehoert der Flaeche, nicht dem Balken: nur sie
    //  kennt den Tabellenkoerper.
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
        anchors { top: reiter.bottom; topMargin: 8
                  right: parent.right; rightMargin: 18 }
        ctl: ctl
        fromRow: Math.floor(tabelle.rows.contentY / tabelle.rowHeight)
        onGeschlossen: root._findOpen = false
    }

    DataTable {
        id: tabelle
        anchors { left: parent.left; right: parent.right
                  top: reiter.bottom; bottom: fuss.top }
        provider: ctl
        showNumbers: root.showNumbers
        //  Die Spalten heissen A, B, C - so, wie eine Formel sie nennt.
        columnLabelsAlpha: true
        searchRevision: ctl.searchRevision
        contentRevision: ctl.contentRevision
        currentRow: ctl.matchRow
        currentColumn: ctl.matchColumn
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

        //  Fuellen die Zeilen das Bild, ist die Fussleiste die einzige freie
        //  Flaeche, die noch bleibt - ein Klick hierher beendet deshalb Auswahl
        //  und Bearbeitung. Die Knoepfe darueber behalten ihren Klick.
        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.DragThreshold
            onTapped: tabelle.entmarkiere()
        }

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

            Feld {
                text: App.uiText(App.language, "TableRows") + ": "
                      + (ctl.filterActive
                         ? App.uiText(App.language, "TableFiltered").arg(ctl.rowCount).arg(ctl.totalRows)
                         : ctl.rowCount)
                color: ctl.filterActive ? App.themeAccent : Editor.gutterText
            }
            Feld {
                visible: ctl.modified || ctl.saving
                color: App.themeAccent
                text: App.uiText(App.language, ctl.saving ? "TableSaving" : "TableModified")
            }
            Feld {
                visible: ctl.ready && !ctl.editable
                color: "#d2a04f"
                text: App.uiText(App.language, "TableReadOnly")
            }
            Feld {
                visible: ctl.savedCopy.length > 0
                text: App.uiText(App.language, "TableSavedCopy").arg(ctl.savedCopy)
            }
            //  Nicht gespeichert: sagen warum, und die beiden Wege anbieten.
            Feld { visible: ctl.saveError.length > 0; color: "#d24f4f"; text: ctl.saveError }
            component Aktion: Feld {
                id: ak
                signal ausgeloest()
                color: App.themeAccent
                font.underline: akMaus.containsMouse
                MouseArea {
                    id: akMaus
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ak.ausgeloest()
                }
            }
            Aktion {
                visible: ctl.saveError.length > 0
                text: App.uiText(App.language, "TableSaveAsCopy")
                onAusgeloest: ctl.saveCopy()
            }
            Aktion {
                visible: ctl.saveError.length > 0
                text: App.uiText(App.language, "TableReloadDiscard")
                onAusgeloest: ctl.reload()
            }
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
            //  Ausgeblendete Spalten muessen SICHTBAR bleiben, sonst sucht man
            //  spaeter eine Spalte, die man selbst weggeklickt hat. Ein Klick
            //  holt alle zurueck.
            Feld {
                visible: ctl.hiddenColumnCount > 0
                color: App.themeAccent
                text: App.uiText(App.language, "TableHiddenColumns").arg(ctl.hiddenColumnCount)
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ctl.showAllColumns()
                    ToolTip.visible: containsMouse
                    ToolTip.text: App.uiText(App.language, "TableShowAllColumns")
                    hoverEnabled: true
                }
            }
            Feld {
                visible: ctl.sorting
                text: App.uiText(App.language, "TableSorting")
            }
            //  Ohne die Zahl waere nicht zu sehen, dass eine gezeigte Zahl
            //  gerechnet und nicht getippt ist.
            Feld {
                visible: ctl.formulaCount > 0
                color: App.themeAccent
                text: App.uiText(App.language, "TableFormulas").arg(ctl.formulaCount)
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    ToolTip.visible: containsMouse
                    ToolTip.text: App.uiText(App.language, "TableFormulaHint")
                }
            }

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
