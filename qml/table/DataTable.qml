pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

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
                    required property var modelData
                    width: root._breite(modelData.chars)
                    height: nummernLeiste.height
                    Text {
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        verticalAlignment: Text.AlignVCenter
                        color: Editor.gutterText
                        font.pixelSize: 10
                        text: modelData.index + 1
                    }
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                                color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                               Editor.gutterText.b, 0.20) }
                }
            }
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
                    required property var modelData
                    width: root._breite(modelData.chars)
                    height: spaltenKopf.height
                    Text {
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        color: Editor.gutterTextActive
                        font.pixelSize: 11
                        font.bold: true
                        text: modelData.title
                    }
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                                color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                               Editor.gutterText.b, 0.20) }
                }
            }
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

        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

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
                    root.provider ? root.provider.rowEmpty(zeile.index) : false

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
                                text: root.provider ? root.provider.cell(zeile.index,
                                                                         modelData.index) : ""
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
    MouseArea {
        anchors.fill: flick
        acceptedButtons: Qt.NoButton
        z: 2
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
