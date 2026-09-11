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

    //  Erwartet: `columns` (Liste aus {index, title, chars}), `rowCount`,
    //  `cell(zeile, spalte)`.
    property var provider: null

    property int rowHeight: 20
    property int headerHeight: 24
    //  Traegt die erste Zeile Spaltennamen? Ohne sie bleibt der Kopf leer.
    property bool showHeader: true
    //  Zeilen- und Spaltennummern wie in einer Tabellenkalkulation. Die
    //  Zeilenspalte ist FEST - sie rollt senkrecht mit, waagerecht nicht.
    property bool showNumbers: false

    //  Suche: `searchRevision` steigt bei jeder Aenderung im Controller. Die
    //  Zell-Bindung LIEST sie - ohne einen gelesenen Wert wertet QML sie nie
    //  neu aus, und die Markierung bliebe auf dem Stand des ersten Bildes.
    property int searchRevision: 0
    property int currentRow: -1
    property int currentColumn: -1
    //  Steigt, wenn sich Reihenfolge oder Spaltenauswahl aendern. Die Zellen
    //  LESEN ihn - `cell()` ist eine Funktion, ohne einen gelesenen Wert wertet
    //  QML die Bindung nie neu aus und die Tabelle bliebe nach dem Sortieren
    //  Zeile fuer Zeile auf dem alten Stand.
    property int contentRevision: 0
    //  Die erste gezeigte Spalte bleibt beim seitlichen Rollen stehen. Sie wird
    //  als undurchsichtige Flaeche DARUEBER gezeichnet, statt sie aus der
    //  rollenden Flaeche herauszunehmen: das haette Spaltenkopf, Suchmarken und
    //  Trefferansteuerung auf zwei Koordinatensysteme aufgeteilt.
    property bool frozenColumn: false

    //  Die angeklickte Zelle. Sie ist die Grundlage fuers Kopieren; ein Rahmen
    //  zeigt sie an. Bearbeiten ist damit NICHT gemeint.
    property int selRow: -1
    property int selColumn: -1

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

    //  Eine kurze Rueckmeldung an Ort und Stelle. Der Weg ueber die Statuszeile
    //  der Haelfte ginge durch vier Ebenen QML, fuer einen Satz, der nach zwei
    //  Sekunden wieder weg ist.
    //  Die Markierung gehoert zum Rechtsklick; jeder Linksklick raeumt sie weg.
    function entmarkiere() {
        root.selRow = -1
        root.selColumn = -1
    }

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

    //  Breite der Zeilenspalte: so viel, wie die groesste Nummer braucht.
    readonly property real _nummernBreite:
        root.showNumbers
        ? Math.max(34, String(liste.count).length * fm.averageCharacterWidth + 16)
        : 0
    readonly property real gesamtBreite: {
        var b = 0
        for (var i = 0; i < root._spalten.length; ++i) b += root._breite(root._spalten[i].chars)
        return b
    }

    //  Waagerecht wird EINMAL gerollt: Ueberschriftzeile und Zeilenliste haengen
    //  beide an `xOffset`. Zwei getrennte Flickables liefen sonst auseinander.
    property real xOffset: 0

    //  Die Spaltennummern stehen in einer EIGENEN Leiste ueber den Namen - so
    //  wie die Zeilennummern in einer eigenen Spalte NEBEN der Tabelle stehen
    //  und nicht in deren erster Spalte.
    Rectangle {
        id: nummernLeiste
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: root._nummernBreite }
        //  Halb so dick wie die Zeilenspalte breit ist waere zu schmal, genau
        //  so dick wirkt klobig - deshalb 50 % mehr als die urspruenglichen 16.
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
                        text: modelData.index + 1
                    }
                    //  Ohne Spaltennamen ist diese Leiste der einzige Kopf, den
                    //  die Tabelle hat - sie muss deshalb dieselben zwei Griffe
                    //  tragen wie die Namensleiste.
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
                text: root._ersteSpalte ? (root._ersteSpalte.index + 1) : ""
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

        //  Die Leisten ausdruecklich nach oben: sie sind Geschwister des
        //  mitrollenden Inhalts, und deren Reihenfolge haengt sonst an der
        //  Reihenfolge im Quelltext. Genau daran ist das seitliche Rollen
        //  schon einmal gescheitert.
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; z: 10 }

        //  Eine Zelle mit RECHTS markieren und ihr Menue oeffnen. Der Helfer
        //  haengt an der FLAECHE, nicht an einer Schicht darueber: die
        //  Bildlaufleisten sind Kinder der Flaeche und bekommen den Druck
        //  dadurch zuerst. `DragThreshold` gibt den Griff wieder her, sobald
        //  gezogen wird - sonst waere das Rollen weg.
        TapHandler {
            acceptedButtons: Qt.RightButton
            gesturePolicy: TapHandler.DragThreshold
            onTapped: function (punkt) {
                //  Der Helfer haengt am MITROLLENDEN Inhalt der Flaeche, die
                //  Rechenwege unten erwarten Sichtfenster-Koordinaten. Der Weg
                //  ueber die Szene ist unabhaengig davon, wo er landet.
                const p = zellSchicht.mapFromItem(null, punkt.scenePosition)
                const z  = root._zeileBeiY(p.y)
                const sp = root._spalteBeiX(p.x)
                if (z < 0 || sp < 0) return
                root.selRow = z
                root.selColumn = sp
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

            //  Der Balken haengt an der Liste (dann stimmen Groesse, Stand und
            //  das Ziehen von selbst), wandert als deren Kind aber mit der
            //  waagerecht rollenden Flaeche mit - deshalb wird er an den
            //  rechten Rand des SICHTBAREN Ausschnitts gerechnet.
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
                        }
                    }
                }
            }
        }
    }

    //  Die Zeilennummern stehen NEBEN der waagerecht rollenden Flaeche, nicht
    //  darin - sonst wanderten sie beim seitlichen Rollen aus dem Bild. Gemalt
    //  werden nur die sichtbaren: bei fester Zeilenhoehe genuegt dafuer
    //  Rechnen, kein zweites Modell.
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
                text: zeile + 1
            }
        }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.25) }
    }

    //  Das Mausrad auf dasselbe Mass wie der Rest der App: rund die halbe
    //  Sichthoehe je Rastung, weich ueber 180 ms. Qts Vorgabe fuer ein
    //  Flickable sind feste 60 px - gemessen kamen 72 px an, wo 276 gewollt
    //  waren. Eine eigene Flaeche statt `SmoothWheelArea`, weil hier ZWEI
    //  Ziele bedient werden: senkrecht die Liste, waagerecht die Flaeche
    //  darunter. Eine MouseArea ist zwingend - ein interaktives Flickable
    //  verarbeitet Radereignisse vor jedem WheelHandler selbst.
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
    //  Der Rahmen um die angeklickte Zelle. Eine eigene Flaeche statt eines
    //  Rechtecks im Zeilen-Delegat: er soll auch dann stehen, wenn die Zeile
    //  ausserhalb des Delegat-Vorrats liegt.
    //  Geschwister der rollenden Flaeche, NICHT ihr Kind: eine `Flickable`
    //  haengt ihre Kinder an den mitrollenden `contentItem`, und dann waeren
    //  alle Koordinaten doppelt versetzt.
    //  REIN SICHTBAR, ohne jeden Zeigerhelfer: ein `TapHandler` setzt am Element
    //  `acceptedMouseButtons` auf ALLE Tasten, und die Schicht liegt ueber der
    //  waagerechten Bildlaufleiste - die war damit nicht mehr zu treffen.
    //  Das Anklicken einer Zelle haengt deshalb an der Flaeche selbst.
    Item {
        id: zellSchicht
        anchors.fill: flick
        clip: true
        z: 3

        //  Der Rahmen um die angeklickte Zelle. Eine eigene Flaeche statt eines
        //  Rechtecks im Zeilen-Delegat: er soll auch dann stehen, wenn die Zeile
        //  ausserhalb des Delegat-Vorrats liegt.
        Rectangle {
            visible: root.selRow >= 0 && root.selColumn >= 0
                     && root._spalteX(root.selColumn) >= 0
            //  `_spalteX` rechnet in INHALTS-Koordinaten, die Schicht liegt im
            //  Sichtfenster - der Versatz muss also abgezogen werden.
            x: root._spalteX(root.selColumn) - root.xOffset
            y: root.selRow * root.rowHeight - liste.contentY
            width: root._spalteBreite(root.selColumn)
            height: root.rowHeight
            color: "transparent"
            border.width: 2
            border.color: App.themeAccent
            radius: 2
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
        //  Die Markierung gehoert zum Rechtsklick und verschwindet mit dem
        //  naechsten Linksklick - EGAL WO. Genau das ist das Schliessen dieses
        //  Menues: ein Klick daneben laesst es zugehen. Ein eigener Zeigerhelfer
        //  bekaeme den Druck gar nicht, weil die rollende Flaeche die linke
        //  Taste fuer sich beansprucht (gemessen: die Markierung blieb stehen).
        //  Ein Eintrag im Menue feuert vorher, die Auswahl steht ihm also noch
        //  zur Verfuegung.
        onClosed: root.entmarkiere()
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
        //  KEINE Tasten: eine `MouseArea`, die Tasten annimmt, verschluckt sie
        //  fuer alles darunter - mit `LeftButton` hier waren die Bildlaufleisten
        //  nicht mehr zu bedienen. Das Anklicken einer Zelle macht deshalb ein
        //  `TapHandler` (s. u.), der nur passiv greift.
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
