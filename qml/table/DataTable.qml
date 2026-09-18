pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

//  Der Tabellenkoerper: Spaltenkopf, Zeilen, beide Rollachsen. Kennt weder
//  DATEV noch CSV - er fragt seinen `provider` nach Spalten, Zeilenzahl und
//  Zelle. Kopf- und Fusszeile baut die jeweilige Flaeche selbst.
Item {
    id: root

    //  Erwartet: `columns` ({index, title, chars}), `rowCount`, `cell(zeile, spalte)`.
    property var provider: null

    property int rowHeight: 20
    property int headerHeight: 24
    //  Traegt die erste Zeile Spaltennamen? Ohne sie bleibt der Kopf leer.
    property bool showHeader: true
    //  Zeilen- und Spaltennummern wie in einer Tabellenkalkulation. Die
    //  Zeilenspalte ist FEST - sie rollt senkrecht mit, waagerecht nicht.
    property bool showNumbers: false
    //  Spalten als A, B, C - so, wie eine Formel sie nennt. Der Buchungsstapel
    //  bleibt bei den Nummern: dort IST die Feldnummer die Bezeichnung.
    property bool columnLabelsAlpha: false

    function _spaltenKopf(index) {
        if (!root.columnLabelsAlpha) return index + 1
        var n = index + 1
        var s = ""
        while (n > 0) {
            const rest = (n - 1) % 26
            s = String.fromCharCode(65 + rest) + s
            n = Math.floor((n - 1) / 26)
        }
        return s
    }

    //  Die Zell-Bindungen LESEN beide Revisionen - `cell()` ist eine Funktion, ohne
    //  gelesenen Wert wertet QML sie nie neu aus.
    property int searchRevision: 0
    property int currentRow: -1
    property int currentColumn: -1
    property int contentRevision: 0
    //  Als undurchsichtige Flaeche DARUEBER gezeichnet, nicht herausgenommen - sonst
    //  laegen Kopf, Suchmarken und Trefferansteuerung in zwei Koordinatensystemen.
    property bool frozenColumn: false

    //  Die gewaehlte Zelle - Grundlage fuer Kopieren, Bearbeiten und Tastatur.
    property int selRow: -1
    property int selColumn: -1
    //  Offen, solange das Eingabefeld ueber der Zelle steht.
    property bool bearbeitet: false

    //  Bearbeiten koennen nur Anbieter, die es selbst sagen - DATEV nie.
    readonly property bool _bearbeitbar: root.provider !== null && root.provider.editable === true
    readonly property bool _umbaubar: root._bearbeitbar && root.provider.structureEditable === true

    readonly property var _ersteSpalte: root._spalten.length > 0 ? root._spalten[0] : null
    readonly property bool _kopfSortiert:
        root.provider !== null && root._ersteSpalte !== null
        && root.provider.sortColumn === root._ersteSpalte.index
    readonly property real _frostBreite:
        (root.frozenColumn && root._ersteSpalte) ? root._breite(root._ersteSpalte.chars) : 0

    //  Welche Spalte liegt an dieser Stelle? Ueber die Breiten, nicht ueber die
    //  Position in der Liste - die Spaltenliste kennt Luecken.
    function _spalteBeiX(px) {
        if (root.frozenColumn && px < root._frostBreite && root._ersteSpalte)
            return root._ersteSpalte.index
        var x = -root.xOffset
        for (var i = 0; i < root._spalten.length; ++i) {
            const w = root._breite(root._spalten[i].chars)
            if (px >= x && px < x + w) return root._spalten[i].index
            x += w
        }
        return -1
    }

    function _zeileBeiY(py) {
        const z = Math.floor((py + liste.contentY) / root.rowHeight)
        return (z >= 0 && z < liste.count) ? z : -1
    }

    function entmarkiere() {
        if (root.bearbeitet) root.uebernehme()
        root.selRow = -1
        root.selColumn = -1
    }

    function waehle(zeile, spalte) {
        if (!root.provider || zeile < 0 || spalte < 0) return
        root.selRow = Math.max(0, Math.min(zeile, root.provider.rowCount - 1))
        root.selColumn = spalte
        root.zeigeZelle(root.selRow, root.selColumn)
        root.forceActiveFocus()
    }

    //  Um `dz` Zeilen und `ds` gezeigte Spalten weiter, am Rand angehalten.
    function _bewege(dz, ds) {
        if (!root.provider || root._spalten.length === 0) return
        var pos = 0
        for (var i = 0; i < root._spalten.length; ++i)
            if (root._spalten[i].index === root.selColumn) pos = i
        pos = Math.max(0, Math.min(pos + ds, root._spalten.length - 1))
        root.waehle(Math.max(0, root.selRow + dz), root._spalten[pos].index)
    }

    function bearbeiteZelle() {
        if (!root._bearbeitbar || root.selRow < 0 || root.selColumn < 0) return
        root.zeigeZelle(root.selRow, root.selColumn)
        //  Bearbeitet wird, was in der Datei STEHT - `=A1+B2`, nicht sein Ergebnis.
        zellEditor.text = (root.provider.cellRaw !== undefined)
                          ? root.provider.cellRaw(root.selRow, root.selColumn)
                          : root.provider.cell(root.selRow, root.selColumn)
        root.bearbeitet = true
        zellEditor.forceActiveFocus()
        zellEditor.selectAll()
    }
    function uebernehme() {
        if (!root.bearbeitet) return
        root.bearbeitet = false
        root.provider.setCell(root.selRow, root.selColumn, zellEditor.text)
        root.forceActiveFocus()
    }
    function verwerfe() {
        root.bearbeitet = false
        root.forceActiveFocus()
    }
    function _linksGetippt(szene, doppelt) {
        const p = zellSchicht.mapFromItem(null, szene)
        const z  = root._zeileBeiY(p.y)
        const sp = root._spalteBeiX(p.x)
        if (root.bearbeitet) root.uebernehme()
        if (z < 0 || sp < 0) { root.entmarkiere(); return }
        root.waehle(z, sp)
        if (doppelt) root.bearbeiteZelle()
    }
    function einfuegen() {
        if (!root._bearbeitbar || root.selRow < 0 || root.selColumn < 0) return
        const text = App.clipboardText()
        if (text.length > 0) root.provider.pasteText(root.selRow, root.selColumn, text)
    }

    //  Rueckmeldung an Ort und Stelle; die Statuszeile laege vier Ebenen entfernt.
    function melde(text) { root._melde(text) }
    function _melde(text) {
        hinweis.text = text
        hinweis.opacity = 1
        hinweisAus.restart()
    }

    function kopiereZelle() {
        if (!root.provider || root.selRow < 0 || root.selColumn < 0) return
        App.copyTextToClipboard(root.provider.cell(root.selRow, root.selColumn))
        root._melde(App.uiText(App.language, "TableCopiedCell"))
    }
    function kopiereZeile() {
        if (!root.provider || root.selRow < 0) return
        App.copyTextToClipboard(root.provider.rowText(root.selRow))
        root._melde(App.uiText(App.language, "TableCopiedRow"))
    }
    //  Linke Kante und Breite einer Spalte. Die Spaltenliste kennt Luecken
    //  (DATEV blendet leere Spalten aus), deshalb ueber `index`, nicht ueber
    //  die Position in der Liste.
    function _spalteX(spalte) {
        var x = 0
        for (var i = 0; i < root._spalten.length; ++i) {
            if (root._spalten[i].index === spalte) return x
            x += root._breite(root._spalten[i].chars)
        }
        return -1
    }
    //  Linke Kante im Sichtfenster; die festgestellte erste Spalte rollt nicht mit.
    function _zelleX(spalte) {
        if (root.frozenColumn && root._ersteSpalte && spalte === root._ersteSpalte.index)
            return 0
        return root._spalteX(spalte) - root.xOffset
    }
    function _spalteBreite(spalte) {
        for (var i = 0; i < root._spalten.length; ++i)
            if (root._spalten[i].index === spalte) return root._breite(root._spalten[i].chars)
        return 0
    }

    //  Eine Zelle ins Bild holen (Sprung zum Treffer). Eine bereits sichtbare
    //  Zeile bleibt, wo sie ist - sonst spraenge die Ansicht bei jedem Schritt.
    function zeigeZelle(zeile, spalte) {
        if (zeile < 0) return
        rollAnim.stop()
        rollAnimX.stop()
        const y = zeile * root.rowHeight
        if (y < liste.contentY || y + root.rowHeight > liste.contentY + liste.height) {
            const maxY = Math.max(0, liste.contentHeight - liste.height)
            liste.contentY = Math.max(0, Math.min(y - liste.height / 3, maxY))
        }
        if (spalte < 0) return
        const x = root._spalteX(spalte)
        const breite = root._spalteBreite(spalte)
        if (x < 0 || breite <= 0) return
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        if (x < flick.contentX)
            flick.contentX = Math.max(0, Math.min(x - 20, maxX))
        else if (x + breite > flick.contentX + flick.width)
            flick.contentX = Math.max(0, Math.min(x + breite - flick.width + 20, maxX))
    }

    readonly property alias rows: liste
    readonly property alias area: flick

    property font cellFont: App.fallbackFont("monospace", 12)
    FontMetrics { id: fm; font: root.cellFont }

    //  Die Zeichenzahl je Spalte steht im Modell; hier wird nur gerechnet. Sie
    //  je Zelle zu erfragen kostete beim Rollen je neuer Zeile einen Lauf ueber
    //  500 Datenzeilen mal Spalte.
    function _breite(zeichen) {
        return Math.max(70, Math.min(320, zeichen * fm.averageCharacterWidth + 16))
    }

    readonly property var _spalten: root.provider ? root.provider.columns : []
    readonly property bool _hatFormeln:
        root.provider !== null && root.provider.formulaCount !== undefined
        && root.provider.formulaCount > 0

    //  Breite der Zeilenspalte: so viel, wie die groesste Nummer braucht.
    readonly property real _nummernBreite:
        root.showNumbers
        ? Math.max(34, String(root.provider && root.provider.totalRows !== undefined
                              ? root.provider.totalRows : liste.count).length
                       * fm.averageCharacterWidth + 16)
        : 0
    readonly property real gesamtBreite: {
        var b = 0
        for (var i = 0; i < root._spalten.length; ++i) b += root._breite(root._spalten[i].chars)
        return b
    }

    //  Waagerecht wird EINMAL gerollt: Ueberschriftzeile und Zeilenliste haengen
    //  beide an `xOffset`. Zwei getrennte Flickables liefen sonst auseinander.
    property real xOffset: 0

    //  Spaltennummern in einer EIGENEN Leiste ueber den Namen, wie die Zeilennummern daneben.
    Rectangle {
        id: nummernLeiste
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: root._nummernBreite }
        //  So dick wie die Zeilenspalte breit ist wirkt klobig, halb so dick zu schmal.
        height: root.showNumbers ? 24 : 0
        visible: root.showNumbers
        color: Editor.gutterBackground
        clip: true
        z: 1

        Row {
            x: -root.xOffset
            height: parent.height
            Repeater {
                model: root._spalten
                delegate: Item {
                    id: nrZelle
                    required property var modelData
                    width: root._breite(modelData.chars)
                    height: nummernLeiste.height
                    Text {
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        verticalAlignment: Text.AlignVCenter
                        color: (root.provider && root.provider.sortColumn === modelData.index)
                               ? App.themeAccent : Editor.gutterText
                        font.pixelSize: 10
                        text: root._spaltenKopf(modelData.index)
                    }
                    //  Ohne Spaltennamen der einzige Kopf - also dieselben Griffe.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function (maus) {
                            if (maus.button === Qt.RightButton)
                                root._oeffneSpaltenMenue(nrZelle.modelData.index,
                                                         nrZelle, maus.x, maus.y)
                            else if (root.provider) {
                                root.entmarkiere()
                                root.provider.sortByColumn(nrZelle.modelData.index)
                            }
                        }
                    }
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                                color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                               Editor.gutterText.b, 0.20) }
                }
            }
        }
        Rectangle {
            visible: root.frozenColumn && root._frostBreite > 0
            width: root._frostBreite
            height: parent.height
            color: Editor.gutterBackground
            z: 1
            Text {
                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                verticalAlignment: Text.AlignVCenter
                color: root._kopfSortiert ? App.themeAccent : Editor.gutterText
                font.pixelSize: 10
                text: root._ersteSpalte ? root._spaltenKopf(root._ersteSpalte.index) : ""
            }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                        color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                       Editor.gutterText.b, 0.45) }
        }

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.20) }
    }

    Rectangle {
        id: spaltenKopf
        anchors { left: parent.left; right: parent.right; top: nummernLeiste.bottom
                  leftMargin: root._nummernBreite }
        height: root.showHeader ? root.headerHeight : 0
        visible: root.showHeader
        color: Editor.gutterBackground
        clip: true
        z: 1

        Row {
            x: -root.xOffset
            height: parent.height
            Repeater {
                model: root._spalten
                delegate: Item {
                    id: kopfZelle
                    required property var modelData
                    width: root._breite(modelData.chars)
                    height: spaltenKopf.height
                    readonly property bool sortiert:
                        root.provider && root.provider.sortColumn === modelData.index

                    Text {
                        anchors { fill: parent; leftMargin: 8
                                  rightMargin: kopfZelle.sortiert ? 20 : 8 }
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        color: kopfZelle.sortiert ? App.themeAccent : Editor.gutterTextActive
                        font.pixelSize: 11
                        font.bold: true
                        text: modelData.title
                    }
                    //  Die Richtung wird GEZEICHNET, nie als Pfeilzeichen gesetzt.
                    DrawnIcon {
                        anchors { right: parent.right; rightMargin: 5
                                  verticalCenter: parent.verticalCenter }
                        size: 12
                        visible: kopfZelle.sortiert
                        name: (root.provider && root.provider.sortAscending)
                              ? "arrow-up" : "arrow-down"
                        color: App.themeAccent
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function (maus) {
                            if (maus.button === Qt.RightButton)
                                root._oeffneSpaltenMenue(kopfZelle.modelData.index,
                                                         kopfZelle, maus.x, maus.y)
                            else if (root.provider) {
                                root.entmarkiere()
                                root.provider.sortByColumn(kopfZelle.modelData.index)
                            }
                        }
                        ToolTip.visible: containsMouse && ToolTip.text.length > 0
                        ToolTip.text: App.uiText(App.language, "TableSortTip")
                        hoverEnabled: true
                        ToolTip.delay: 700
                    }
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                                color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                               Editor.gutterText.b, 0.20) }
                }
            }
        }
        //  Muss MITfeststehen - sonst steht ueber den Werten der Name einer
        //  weggerollten Spalte.
        Rectangle {
            visible: root.frozenColumn && root._frostBreite > 0
            width: root._frostBreite
            height: parent.height
            color: Editor.gutterBackground
            z: 1
            Text {
                anchors { fill: parent; leftMargin: 8
                          rightMargin: root._kopfSortiert ? 20 : 8 }
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                color: root._kopfSortiert ? App.themeAccent : Editor.gutterTextActive
                font.pixelSize: 11
                font.bold: true
                text: root._ersteSpalte ? root._ersteSpalte.title : ""
            }
            DrawnIcon {
                anchors { right: parent.right; rightMargin: 5
                          verticalCenter: parent.verticalCenter }
                size: 12
                visible: root._kopfSortiert
                name: (root.provider && root.provider.sortAscending)
                      ? "arrow-up" : "arrow-down"
                color: App.themeAccent
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (maus) {
                    if (!root._ersteSpalte) return
                    if (maus.button === Qt.RightButton)
                        root._oeffneSpaltenMenue(root._ersteSpalte.index, parent, maus.x, maus.y)
                    else if (root.provider) {
                        root.entmarkiere()
                        root.provider.sortByColumn(root._ersteSpalte.index)
                    }
                }
            }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                        color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                       Editor.gutterText.b, 0.45) }
        }

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.35) }
    }

    //  Die Ecke links oben - sie fuellt die Flaeche, die Zeilenspalte und
    //  Kopfleisten gemeinsam frei lassen.
    Rectangle {
        visible: root.showNumbers
        anchors { left: parent.left; top: parent.top }
        width: root._nummernBreite
        height: nummernLeiste.height + spaltenKopf.height
        color: Editor.gutterBackground
        z: 1
    }

    //  Waagerecht rollt die AEUSSERE Flaeche, senkrecht die Liste darin. Beide
    //  senkrecht rollen zu lassen verdoppelte jeden Radschritt.
    Flickable {
        id: flick
        objectName: "datevTable"
        anchors { left: parent.left; right: parent.right
                  leftMargin: root._nummernBreite
                  top: spaltenKopf.bottom; bottom: parent.bottom }
        clip: true
        contentWidth: Math.max(width, root.gesamtBreite)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        onContentXChanged: root.xOffset = contentX

        //  Leisten ausdruecklich nach oben: als Geschwister des Inhalts hinge ihre
        //  Reihenfolge sonst am Quelltext (daran scheiterte das seitliche Rollen).
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; z: 10 }

        //  Rechts an der Flaeche, nicht an einer Schicht darueber: die Leisten bekommen
        //  den Druck so zuerst, und `DragThreshold` gibt ihn beim Ziehen wieder her.
        TapHandler {
            acceptedButtons: Qt.RightButton
            gesturePolicy: TapHandler.DragThreshold
            onTapped: function (punkt) {
                //  Ueber die Szene: der Helfer haengt am mitrollenden Inhalt.
                const p = zellSchicht.mapFromItem(null, punkt.scenePosition)
                const z  = root._zeileBeiY(p.y)
                const sp = root._spalteBeiX(p.x)
                if (z < 0 || sp < 0) return
                if (root.bearbeitet) root.uebernehme()
                root.waehle(z, sp)
                const r = root.mapFromItem(null, punkt.scenePosition)
                zellMenue.popup(r.x, r.y)
            }
        }



        //  Nur die sichtbaren Zeilen entstehen als Elemente, auch bei 10.000
        //  Datenzeilen.
        ListView {
            id: liste
            objectName: "datevRows"
            width: flick.contentWidth
            height: flick.height
            model: root.provider ? root.provider.rowCount : 0
            clip: false
            cacheBuffer: 400
            boundsBehavior: Flickable.StopAtBounds

            //  Linksklick unter die letzte Zeile beendet Auswahl und
            //  Bearbeitung. Der Helfer gehoert AN DIE LISTE - an der Flaeche
            //  darunter kaeme der linke Druck nie an, den nimmt die Liste fuer
            //  ihr Rollen.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.DragThreshold
                onTapped: function (punkt) {
                    const p = zellSchicht.mapFromItem(null, punkt.scenePosition)
                    if (root._zeileBeiY(p.y) >= 0 && root._spalteBeiX(p.x) >= 0) return
                    root.entmarkiere()
                }
            }

            //  An der Liste haengen Groesse und Ziehen von selbst; seitlich rollt er aber
            //  mit und wird deshalb an den Rand des SICHTBAREN Ausschnitts gerechnet.
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                z: 10
                x: flick.contentX + flick.width - width
            }

            delegate: Item {
                id: zeile
                required property int index
                width: liste.width
                height: root.rowHeight

                //  Eine leere Zeile (Leerzeile in der Datei) bekommt keinen
                //  Streifen - die Luecke soll als Luecke zu sehen sein.
                readonly property bool leer:
                    (root.contentRevision >= 0 && root.provider)
                    ? root.provider.rowEmpty(zeile.index) : false

                //  Links waehlt, doppelt bearbeitet. Der Helfer sitzt IN der Zeile:
                //  die Liste nimmt den linken Druck fuer ihr Rollen an, ein Helfer
                //  an der Flaeche darueber bekaeme ihn nie (gemessen).
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.DragThreshold
                    onTapped: function (punkt) { root._linksGetippt(punkt.scenePosition, false) }
                    onDoubleTapped: function (punkt) { root._linksGetippt(punkt.scenePosition, true) }
                }
                Rectangle {
                    anchors.fill: parent
                    color: (!zeile.leer && zeile.index % 2 === 1)
                           ? Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.05)
                           : "transparent"
                }
                //  Suchtreffer: je Zeile entstehen nur so viele Marken, wie
                //  die Zeile Treffer hat - ohne laufende Suche also keine. Je
                //  Zelle eine (unsichtbare) Marke kostete gemessen 3,78 -> 4,32 ms
                //  je Bild, auch wenn niemand sucht.
                Repeater {
                    model: (root.searchRevision > 0 && root.provider)
                           ? root.provider.rowMatches(zeile.index) : []
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool laufend:
                            zeile.index === root.currentRow && modelData === root.currentColumn
                        x: root._spalteX(modelData) + 1
                        width: Math.max(0, root._spalteBreite(modelData) - 2)
                        y: 1
                        height: zeile.height - 2
                        radius: 2
                        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                       App.themeAccent.b, laufend ? 0.55 : 0.20)
                        border.width: laufend ? 1 : 0
                        border.color: App.themeAccent
                    }
                }

                Row {
                    Repeater {
                        model: root._spalten
                        delegate: Item {
                            required property var modelData
                            width: root._breite(modelData.chars)
                            height: zeile.height

                            Text {
                                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                color: Editor.text
                                font: root.cellFont
                                text: (root.contentRevision >= 0 && root.provider)
                                      ? root.provider.cell(zeile.index, modelData.index) : ""
                            }
                            //  Die Ecke sagt, dass die Zahl gerechnet und nicht
                            //  getippt ist. Ohne eine einzige Formel in der Datei
                            //  faellt die Abfrage je Zelle weg.
                            Rectangle {
                                visible: root._hatFormeln && root.contentRevision >= 0
                                         && root.provider.cellIsFormula(zeile.index,
                                                                        modelData.index)
                                anchors { right: parent.right; top: parent.top
                                          rightMargin: 1; topMargin: 1 }
                                width: 5
                                height: 5
                                radius: 1
                                color: App.themeAccent
                                opacity: 0.55
                            }
                        }
                    }
                }
            }
        }
    }

    //  NEBEN der rollenden Flaeche, sonst wanderten die Nummern seitlich aus dem
    //  Bild. Nur die sichtbaren; bei fester Zeilenhoehe genuegt Rechnen.
    Rectangle {
        id: nummernSpalte
        visible: root.showNumbers
        anchors { left: parent.left; top: spaltenKopf.bottom; bottom: parent.bottom }
        width: root._nummernBreite
        color: Editor.gutterBackground
        clip: true
        z: 1

        readonly property int erste: Math.max(0, Math.floor(liste.contentY / root.rowHeight))
        readonly property int sichtbar: Math.ceil(height / root.rowHeight) + 1

        Repeater {
            model: nummernSpalte.sichtbar
            delegate: Text {
                required property int index
                readonly property int zeile: nummernSpalte.erste + index
                visible: zeile < liste.count
                y: zeile * root.rowHeight - liste.contentY
                width: nummernSpalte.width - 8
                height: root.rowHeight
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                color: Editor.gutterText
                font.pixelSize: 11
                //  Mit Filter die Nummer der Datei - die Luecken bleiben sichtbar.
                text: (root.contentRevision >= 0 && root.provider
                       && root.provider.rowNumber !== undefined)
                      ? root.provider.rowNumber(zeile) : zeile + 1
            }
        }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.25) }
    }

    //  Rad wie im Rest der App: halbe Sichthoehe je Rastung, weich ueber 180 ms (Qt gibt
    //  60 px vor, gemessen 72 statt 276). Eigene Flaeche, weil ZWEI Ziele rollen; eine
    //  MouseArea ist zwingend - ein Flickable nimmt Radereignisse vor jedem WheelHandler.
    NumberAnimation {
        id: rollAnim
        target: liste; property: "contentY"
        duration: 180; easing.type: Easing.OutCubic
    }
    NumberAnimation {
        id: rollAnimX
        target: flick; property: "contentX"
        duration: 180; easing.type: Easing.OutCubic
    }
    //  Auswahlrahmen und Eingabefeld: Geschwister der Flaeche (als Kind waeren die
    //  Koordinaten doppelt versetzt), ohne Zeigerhelfer - einer setzte am Element alle
    //  Tasten an und verdeckte die waagerechte Bildlaufleiste.
    Item {
        id: zellSchicht
        anchors.fill: flick
        clip: true
        z: 3

        //  Eigene Flaeche statt im Delegaten: steht auch ausserhalb des Delegat-Vorrats.
        Rectangle {
            visible: root.selRow >= 0 && root.selColumn >= 0
                     && root._spalteX(root.selColumn) >= 0
            x: root._zelleX(root.selColumn)
            y: root.selRow * root.rowHeight - liste.contentY
            width: root._spalteBreite(root.selColumn)
            height: root.rowHeight
            color: "transparent"
            border.width: 2
            border.color: App.themeAccent
            radius: 2
        }

        //  Nur waehrend des Bearbeitens sichtbar - unsichtbar nimmt es keine
        //  Taste und keinen Klick an, die Schicht bleibt sonst rein sichtbar.
        TextField {
            id: zellEditor
            objectName: "zellEditor"     // Griff fuer tests/bench
            visible: root.bearbeitet
            x: root._zelleX(root.selColumn)
            y: root.selRow * root.rowHeight - liste.contentY
            width: Math.max(root._spalteBreite(root.selColumn), 140)
            height: root.rowHeight
            topPadding: 0; bottomPadding: 0; leftPadding: 7; rightPadding: 7
            verticalAlignment: TextInput.AlignVCenter
            font: root.cellFont
            color: Editor.text
            background: Rectangle {
                color: Editor.background
                border.color: App.themeAccent
                border.width: 2
                radius: 2
            }
            Keys.onPressed: function (e) {
                switch (e.key) {
                case Qt.Key_Return: case Qt.Key_Enter:
                    root.uebernehme(); root._bewege(1, 0); e.accepted = true; return
                case Qt.Key_Tab:
                    root.uebernehme(); root._bewege(0, 1); e.accepted = true; return
                case Qt.Key_Backtab:
                    root.uebernehme(); root._bewege(0, -1); e.accepted = true; return
                case Qt.Key_Up: case Qt.Key_Down:
                    root.uebernehme(); root._bewege(e.key === Qt.Key_Up ? -1 : 1, 0)
                    e.accepted = true; return
                case Qt.Key_Escape:
                    root.verwerfe(); e.accepted = true; return
                }
            }
            onActiveFocusChanged: if (!activeFocus && root.bearbeitet) root.uebernehme()
        }
    }

    //  Tastatur, sobald eine Zelle gewaehlt ist. Ohne Auswahl bleiben die Pfeile
    //  beim Viewer (vorherige/naechste Datei).
    Keys.onPressed: function (e) {
        if (root.bearbeitet || root.selRow < 0 || root.selColumn < 0 || !root.provider) return
        const strg = (e.modifiers & Qt.ControlModifier) !== 0
        const seite = Math.max(1, Math.floor(liste.height / root.rowHeight) - 1)
        switch (e.key) {
        case Qt.Key_Up:       root._bewege(-1, 0); e.accepted = true; return
        case Qt.Key_Down:     root._bewege(1, 0);  e.accepted = true; return
        case Qt.Key_Left:     root._bewege(0, -1); e.accepted = true; return
        case Qt.Key_Right:    root._bewege(0, 1);  e.accepted = true; return
        case Qt.Key_Tab:      root._bewege(0, 1);  e.accepted = true; return
        case Qt.Key_Backtab:  root._bewege(0, -1); e.accepted = true; return
        case Qt.Key_PageUp:   root._bewege(-seite, 0); e.accepted = true; return
        case Qt.Key_PageDown: root._bewege(seite, 0);  e.accepted = true; return
        case Qt.Key_Home:
            root._bewege(strg ? -root.selRow : 0, -root._spalten.length); e.accepted = true; return
        case Qt.Key_End:
            root._bewege(strg ? root.provider.rowCount : 0, root._spalten.length); e.accepted = true; return
        case Qt.Key_Escape:   root.entmarkiere(); e.accepted = true; return
        }
        //  Eine Markierung zeigt nur - Tippen oder Entf aendern nichts. Bearbeitet wird
        //  per Doppelklick, F2 oder aus dem Menue.
        if (e.key === Qt.Key_F2 && root._bearbeitbar) { root.bearbeiteZelle(); e.accepted = true }
    }

    //  Nach dem Loeschen von Zeilen darf die Auswahl nicht hinter dem Ende stehen.
    Connections {
        target: root.provider
        ignoreUnknownSignals: true
        function onRowsChanged() {
            if (root.provider && root.selRow >= root.provider.rowCount)
                root.selRow = root.provider.rowCount - 1
        }
    }

    //  Rechtsklick auf einen Spaltenkopf.
    Menu {
        id: spaltenMenue
        property int spalte: -1
        MenuItem {
            text: App.uiText(App.language, "TableSortAsc")
            onTriggered: {
                if (!root.provider) return
                if (root.provider.sortColumn === spaltenMenue.spalte
                    && root.provider.sortAscending) return
                root.provider.clearSort()
                root.provider.sortByColumn(spaltenMenue.spalte)
            }
        }
        MenuItem {
            text: App.uiText(App.language, "TableSortDesc")
            onTriggered: {
                if (!root.provider) return
                root.provider.clearSort()
                root.provider.sortByColumn(spaltenMenue.spalte)
                root.provider.sortByColumn(spaltenMenue.spalte)
            }
        }
        MenuItem {
            text: App.uiText(App.language, "TableSortNone")
            enabled: root.provider && root.provider.sortColumn >= 0
            onTriggered: if (root.provider) root.provider.clearSort()
        }
        MenuSeparator {}
        MenuItem {
            text: App.uiText(App.language, "TableHideColumn")
            enabled: root._spalten.length > 1
            onTriggered: if (root.provider) root.provider.setColumnHidden(spaltenMenue.spalte, true)
        }
        MenuItem {
            text: App.uiText(App.language, "TableShowAllColumns")
            enabled: root.provider && root.provider.hiddenColumnCount > 0
            onTriggered: root._alleSpaltenZeigen()
        }
        MenuSeparator { visible: root._umbaubar; height: visible ? implicitHeight : 0 }
        MenuItem {
            visible: root._umbaubar; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TableInsertColLeft")
            onTriggered: root.provider.insertColumn(spaltenMenue.spalte)
        }
        MenuItem {
            visible: root._umbaubar; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TableInsertColRight")
            onTriggered: root.provider.insertColumn(spaltenMenue.spalte + 1)
        }
        MenuItem {
            visible: root._umbaubar; height: visible ? implicitHeight : 0
            enabled: root.provider && root.provider.columnCount > 1
            text: App.uiText(App.language, "TableRemoveCol")
            onTriggered: root.provider.removeColumn(spaltenMenue.spalte)
        }
        MenuItem {
            visible: root._bearbeitbar && root.showHeader; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TableRenameCol")
            onTriggered: umbenennen.oeffne(spaltenMenue.spalte)
        }
        MenuSeparator {}
        MenuItem {
            text: App.uiText(App.language, "TableFreezeColumn")
            checkable: true
            checked: root.frozenColumn
            onTriggered: root.frozenColumn = !root.frozenColumn
        }
    }

    //  Rechtsklick auf eine Zelle.
    Menu {
        id: zellMenue
        objectName: "zellMenue"     // Griff fuer tests/bench
        MenuItem {
            visible: root._bearbeitbar; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TableEditCell")
            onTriggered: root.bearbeiteZelle()
        }
        MenuItem {
            visible: root._bearbeitbar; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TablePaste")
            onTriggered: root.einfuegen()
        }
        MenuSeparator { visible: root._bearbeitbar; height: visible ? implicitHeight : 0 }
        MenuItem {
            text: App.uiText(App.language, "TableCopyCell")
            enabled: root.selRow >= 0 && root.selColumn >= 0
            onTriggered: root.kopiereZelle()
        }
        MenuItem {
            text: App.uiText(App.language, "TableCopyRow")
            enabled: root.selRow >= 0
            onTriggered: root.kopiereZeile()
        }
        MenuSeparator { visible: root._umbaubar; height: visible ? implicitHeight : 0 }
        MenuItem {
            visible: root._umbaubar; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TableInsertRowAbove")
            onTriggered: root.provider.insertRows(root.selRow, 1)
        }
        MenuItem {
            visible: root._umbaubar; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TableInsertRowBelow")
            onTriggered: root.provider.insertRows(root.selRow + 1, 1)
        }
        MenuItem {
            visible: root._umbaubar; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TableRemoveRow")
            onTriggered: root.provider.removeRows(root.selRow, 1)
        }
        MenuSeparator { visible: root._umbaubar; height: visible ? implicitHeight : 0 }
        MenuItem {
            visible: root._umbaubar; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TableInsertColLeft")
            onTriggered: root.provider.insertColumn(root.selColumn)
        }
        MenuItem {
            visible: root._umbaubar; height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TableInsertColRight")
            onTriggered: root.provider.insertColumn(root.selColumn + 1)
        }
        MenuItem {
            visible: root._umbaubar; height: visible ? implicitHeight : 0
            enabled: root.provider && root.provider.columnCount > 1
            text: App.uiText(App.language, "TableRemoveCol")
            onTriggered: root.provider.removeColumn(root.selColumn)
        }
    }

    //  Spalte umbenennen: ein kleines Feld unter dem Spaltenkopf.
    Popup {
        id: umbenennen
        property int spalte: -1
        function oeffne(sp) {
            umbenennen.spalte = sp
            for (var i = 0; i < root._spalten.length; ++i)
                if (root._spalten[i].index === sp) nameFeld.text = root._spalten[i].title
            umbenennen.x = Math.max(0, root._zelleX(sp) + root._nummernBreite)
            umbenennen.y = nummernLeiste.height + spaltenKopf.height
            umbenennen.open()
            nameFeld.forceActiveFocus()
            nameFeld.selectAll()
        }
        modal: false
        dim: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 8
        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder
            radius: 6
        }
        TextField {
            id: nameFeld
            width: 200
            onAccepted: {
                if (root.provider) root.provider.setColumnName(umbenennen.spalte, nameFeld.text)
                umbenennen.close()
            }
        }
    }

    function _oeffneSpaltenMenue(spalte, ueber, mx, my) {
        spaltenMenue.spalte = spalte
        spaltenMenue.popup(ueber, mx, my)
    }
    //  Der DATEV-Weg heisst anders: dort ist `showAllColumns` der grosse
    //  Schalter, nicht die Ruecknahme.
    function _alleSpaltenZeigen() {
        if (!root.provider) return
        if (root.provider.showAllHiddenColumns !== undefined) root.provider.showAllHiddenColumns()
        else if (root.provider.showAllColumns !== undefined) root.provider.showAllColumns()
    }

    //  Liegt UEBER der rollenden Flaeche; steht die waagerecht auf 0, deckt sie
    //  genau die Spalte darunter ab und ist damit unsichtbar.
    Rectangle {
        id: frostSpalte
        visible: root.frozenColumn && root._frostBreite > 0
        anchors { left: parent.left; top: spaltenKopf.bottom; bottom: parent.bottom
                  leftMargin: root._nummernBreite }
        width: root._frostBreite
        color: Editor.background
        clip: true
        z: 2

        readonly property int erste: Math.max(0, Math.floor(liste.contentY / root.rowHeight))
        readonly property int sichtbar: Math.ceil(height / root.rowHeight) + 1
        readonly property int spalte: root._ersteSpalte ? root._ersteSpalte.index : -1

        Repeater {
            model: frostSpalte.visible ? frostSpalte.sichtbar : 0
            delegate: Item {
                required property int index
                readonly property int zeile: frostSpalte.erste + index
                visible: zeile < liste.count
                y: zeile * root.rowHeight - liste.contentY
                width: frostSpalte.width
                height: root.rowHeight

                Rectangle {
                    anchors.fill: parent
                    color: (zeile % 2 === 1)
                           ? Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.05)
                           : "transparent"
                }
                Text {
                    anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    color: Editor.text
                    font: root.cellFont
                    text: (root.contentRevision >= 0 && root.provider
                           && frostSpalte.spalte >= 0)
                          ? root.provider.cell(parent.zeile, frostSpalte.spalte) : ""
                }
            }
        }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.45) }
    }

    Rectangle {
        id: hinweis
        objectName: "tabellenHinweis"
        property alias text: hinweisText.text
        anchors { right: parent.right; bottom: parent.bottom; margins: 14 }
        width: hinweisText.implicitWidth + 20
        height: 26
        radius: 4
        z: 6
        opacity: 0
        visible: opacity > 0
        color: Qt.rgba(App.themeCard.r, App.themeCard.g, App.themeCard.b, 0.95)
        border.width: 1
        border.color: App.themeBorder
        Text {
            id: hinweisText
            anchors.centerIn: parent
            color: App.themeTextPrimary
            font.pixelSize: 11
        }
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Timer { id: hinweisAus; interval: 1400; onTriggered: hinweis.opacity = 0 }
    }

    MouseArea {
        anchors.fill: flick
        //  KEINE Tasten: eine MouseArea, die sie annimmt, verschluckt sie fuer alles
        //  darunter - mit `LeftButton` waren die Bildlaufleisten tot.
        acceptedButtons: Qt.NoButton
        z: 4
        onWheel: function (wheel) {
            //  Waagerecht: Radneigung, Strg oder Umschalt. Strg ist hier frei -
            //  die Tabelle kennt keine Zoomstufe; in der Galerie stellt dasselbe
            //  Kuerzel die Kachelgroesse.
            const waagerecht = wheel.angleDelta.x !== 0
                               || (wheel.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
            if (waagerecht) {
                const maxX = Math.max(0, flick.contentWidth - flick.width)
                if (maxX <= 0) { wheel.accepted = true; return }
                const rohX = (wheel.angleDelta.x !== 0 ? wheel.angleDelta.x
                                                       : wheel.angleDelta.y) / 120
                const basisX = rollAnimX.running ? rollAnimX.to : flick.contentX
                rollAnimX.from = flick.contentX
                //  Rad HOCH holt den rechten Teil herein - dieselbe Richtung wie
                //  in `SmoothWheelArea` und wie in Browsern und Editoren.
                rollAnimX.to = Math.max(0, Math.min(basisX + rohX * flick.width * 0.5, maxX))
                rollAnimX.restart()
                wheel.accepted = true
                return
            }
            const maxY = Math.max(0, liste.contentHeight - liste.height)
            if (maxY <= 0) { wheel.accepted = true; return }
            const roh = (wheel.angleDelta.y !== 0)
                        ? (wheel.angleDelta.y / 120) * (liste.height * 0.5)
                        : wheel.pixelDelta.y * 1.6
            const basis = rollAnim.running ? rollAnim.to : liste.contentY
            rollAnim.from = liste.contentY
            rollAnim.to = Math.max(0, Math.min(basis - roh, maxY))
            rollAnim.restart()
            wheel.accepted = true
        }
    }
}
