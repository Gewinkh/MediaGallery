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
    //  Duenne Linien zwischen allen Zellen statt Zeilenstreifen, wie in einer Tabellenkalkulation.
    property bool gridLines: App.tableGridLines
    readonly property int _pad: 6

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

    //  Die gewaehlte Zelle - Grundlage fuer Kopieren, Bearbeiten und Tastatur,
    //  zugleich der ANKER eines Bereichs; -1 heisst „keine zweite Ecke".
    property int selRow: -1
    property int selColumn: -1
    property int bisRow: -1
    property int bisColumn: -1

    readonly property bool hatBereich:
        root.bisRow >= 0 && root.bisColumn >= 0
        && (root.bisRow !== root.selRow || root.bisColumn !== root.selColumn)
    readonly property int _zeileVon:
        Math.min(root.selRow, root.bisRow >= 0 ? root.bisRow : root.selRow)
    readonly property int _zeileBis:
        Math.max(root.selRow, root.bisRow >= 0 ? root.bisRow : root.selRow)
    //  Ueber STELLEN in der gezeigten Liste: dazwischen koennen ausgeblendete
    //  Spalten liegen.
    readonly property int _stelleVon: {
        const a = root._geometrie.stelle[root.selColumn]
        const b = root.bisColumn >= 0 ? root._geometrie.stelle[root.bisColumn] : a
        return (a === undefined || b === undefined) ? -1 : Math.min(a, b)
    }
    readonly property int _stelleBis: {
        const a = root._geometrie.stelle[root.selColumn]
        const b = root.bisColumn >= 0 ? root._geometrie.stelle[root.bisColumn] : a
        return (a === undefined || b === undefined) ? -1 : Math.max(a, b)
    }
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
        (root.frozenColumn && root._ersteSpalte) ? root._breite(root._ersteSpalte) : 0

    //  Welche Spalte liegt an dieser Stelle? `px` misst im Sichtfenster, die
    //  Kanten messen im Inhalt - der Versatz kommt also dazu.
    function _spalteBeiX(px) {
        if (root.frozenColumn && px < root._frostBreite && root._ersteSpalte)
            return root._ersteSpalte.index
        const ziel = px + root.xOffset
        if (ziel < 0 || ziel >= root.gesamtBreite) return -1
        return root._spalten[root._stelleBeiX(ziel)].index
    }

    function _zeileBeiY(py) {
        const z = Math.floor((py + liste.contentY) / root.rowHeight)
        return (z >= 0 && z < liste.count) ? z : -1
    }

    function entmarkiere() {
        if (root.bearbeitet) root.uebernehme()
        root.selRow = -1
        root.selColumn = -1
        root.bisRow = -1
        root.bisColumn = -1
    }

    function waehle(zeile, spalte) {
        if (!root.provider || zeile < 0 || spalte < 0) return
        root.bisRow = -1
        root.bisColumn = -1
        root.selRow = Math.max(0, Math.min(zeile, root.provider.rowCount - 1))
        root.selColumn = spalte
        root.zeigeZelle(root.selRow, root.selColumn)
        root.forceActiveFocus()
    }

    function erweitere(zeile, spalte) {
        if (!root.provider || root.selRow < 0 || root.selColumn < 0) return
        if (zeile < 0 || spalte < 0) return
        root.bisRow = Math.max(0, Math.min(zeile, root.provider.rowCount - 1))
        root.bisColumn = spalte
        root.zeigeZelle(root.bisRow, root.bisColumn)
        root.forceActiveFocus()
    }

    //  Um `dz` Zeilen und `ds` gezeigte Spalten weiter, am Rand angehalten;
    //  mit `erweitern` wandert die zweite Ecke statt des Ankers.
    function _bewege(dz, ds, erweitern) {
        if (!root.provider || root._spalten.length === 0) return
        const zAlt = (erweitern && root.bisRow >= 0) ? root.bisRow : root.selRow
        const sAlt = (erweitern && root.bisColumn >= 0) ? root.bisColumn : root.selColumn
        var pos = root._geometrie.stelle[sAlt]
        if (pos === undefined) pos = 0
        pos = Math.max(0, Math.min(pos + ds, root._spalten.length - 1))
        const spalte = root._spalten[pos].index
        if (erweitern) root.erweitere(Math.max(0, zAlt + dz), spalte)
        else root.waehle(Math.max(0, zAlt + dz), spalte)
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
    //  Ohne Anker waehlt Umschalt+Klick schlicht - sonst waere der erste wirkungslos.
    function _umschaltGetippt(szene) {
        const p = zellSchicht.mapFromItem(null, szene)
        const z  = root._zeileBeiY(p.y)
        const sp = root._spalteBeiX(p.x)
        if (z < 0 || sp < 0) return
        if (root.bearbeitet) root.uebernehme()
        if (root.selRow < 0 || root.selColumn < 0) root.waehle(z, sp)
        else root.erweitere(z, sp)
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
        if (!root.hatBereich) {
            App.copyTextToClipboard(root.provider.cell(root.selRow, root.selColumn))
            root._melde(App.uiText(App.language, "TableCopiedCell"))
            return
        }
        //  Aus dem Anbieter: er kennt Ordnung und Filter und hat einen Deckel.
        const text = (root.provider.rangeText !== undefined)
                     ? root.provider.rangeText(root._zeileVon, root._stelleVon,
                                               root._zeileBis, root._stelleBis) : ""
        if (text.length === 0) {
            root._melde(App.uiText(App.language, "TableRangeTooBig"))
            return
        }
        App.copyTextToClipboard(text)
        const zellen = (root._zeileBis - root._zeileVon + 1)
                       * (root._stelleBis - root._stelleVon + 1)
        root._melde(App.uiText(App.language, "TableCopiedRange").arg(zellen))
    }
    function kopiereZeile() {
        if (!root.provider || root.selRow < 0) return
        App.copyTextToClipboard(root.provider.rowText(root.selRow))
        root._melde(App.uiText(App.language, "TableCopiedRow"))
    }
    //  Linke Kante und Breite einer Spalte, beides aus `_geometrie`.
    function _spalteX(spalte) {
        const i = root._geometrie.stelle[spalte]
        return i === undefined ? -1 : root._geometrie.kanten[i]
    }
    //  Linke Kante im Sichtfenster; die festgestellte erste Spalte rollt nicht mit.
    function _zelleX(spalte) {
        if (root.frozenColumn && root._ersteSpalte && spalte === root._ersteSpalte.index)
            return 0
        return root._spalteX(spalte) - root.xOffset
    }
    function _spalteBreite(spalte) {
        const i = root._geometrie.stelle[spalte]
        if (i === undefined) return 0
        return root._geometrie.kanten[i + 1] - root._geometrie.kanten[i]
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
    FontMetrics { id: fmFett; font: root.cellFontFett }
    //  EINMAL gebaut, nicht je Zelle: ein `Qt.font({...})` in der Bindung legt
    //  je sichtbarer Zelle ein neues Schriftobjekt an.
    readonly property font cellFontFett: Qt.font({
        family: root.cellFont.family, pixelSize: root.cellFont.pixelSize, bold: true })

    //  Die Spalten MIT Hintergrund, vorab gesammelt: je Zeile entsteht dann nur
    //  fuer sie eine Flaeche. Eine (durchsichtige) Flaeche je Zelle kostete an
    //  derselben Stelle schon einmal 3,78 -> 4,32 ms je Bild.
    readonly property var _bgSpalten: {
        var aus = []
        for (var i = 0; i < root._spalten.length; ++i)
            if (root._spalten[i].bg !== undefined)
                aus.push({ index: root._spalten[i].index, farbe: root._spalten[i].bg })
        return aus
    }

    //  Gilt waehrend des Zugs; der Anbieter erfaehrt die Breite erst beim
    //  Loslassen - jede Meldung von dort baut die Zellen neu.
    //  Waehrend des Zugs aendert sich an der Tabelle NICHTS - gezeigt wird nur
    //  eine Hilfslinie, geordnet wird beim Loslassen (1,6 -> 0,11 ms je Schritt).
    property int _ziehSpalte: -1
    property real _ziehBreite: 0
    property real _ziehLinie: -1
    property real _ziehAb: 0
    //  Solange gezogen wird, schrumpft das Spaltenfenster nicht - sonst naehme
    //  der Repeater Kacheln weg, waehrend an ihnen haengt, was gerade laeuft.
    property int _ziehAnz: 0

    //  Uebernommen wird ERST nach der Ereignisbehandlung des Helfers - sonst
    //  zerstoert der Repeater seine Kachel unter ihm (Absturz, reproduziert).
    function _ziehUebernehmen() {
        const sp = root._ziehSpalte
        const px = Math.round(root._ziehBreite)
        root._ziehSpalte = -1
        if (sp >= 0 && root.provider) root.provider.setColumnWidth(sp, px)
    }

    //  Die Zeichenzahl steht im Modell; je Zelle erfragt kostete sie beim Rollen
    //  500 Datenzeilen mal Spalte. `px` von Hand sticht die gerechnete Breite.
    //  Gemessen an den laengsten Eintraegen (`widest`), einmal je Spaltenliste; der Kopf haelt Platz fuer den
    //  Sortierpfeil. Ohne `widest` (DATEV) bleibt die Schaetzung ueber die mittlere Zeichenbreite.
    FontMetrics { id: fmKopf; font.pixelSize: 11; font.bold: true }
    readonly property var _gemessen: {
        var m = ({})
        for (var i = 0; i < root._spalten.length; ++i) {
            const sp = root._spalten[i]
            if (sp.widest === undefined) continue
            const mass = sp.fett === true ? fmFett : fm
            var zelle = 0
            for (var k = 0; k < sp.widest.length; ++k) zelle = Math.max(zelle, mass.advanceWidth(sp.widest[k]))
            zelle += 2 * root._pad
            const kopf = sp.title ? fmKopf.advanceWidth(sp.title) + root._pad + 20 : 0
            m[sp.index] = Math.max(36, Math.min(320, Math.ceil(Math.max(zelle, kopf)) + 1))
        }
        return m
    }
    function _breite(sp) {
        if (!sp) return 70
        if (sp.px > 0) return sp.px
        const g = root._gemessen[sp.index]
        if (g !== undefined) return g
        return Math.max(70, Math.min(320, sp.chars * fm.averageCharacterWidth + 16))
    }

    readonly property var _spalten: root.provider ? root.provider.columns : []
    readonly property color _gitterFarbe: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g, Editor.gutterText.b, 0.30)

    //  Linke Kanten und Stelle je Spaltennummer in EINEM Durchlauf; die Liste
    //  kennt Luecken, deshalb ueber `index`.
    readonly property var _geometrie: {
        var kanten = [0]
        var stelle = ({})
        var x = 0
        for (var i = 0; i < root._spalten.length; ++i) {
            stelle[root._spalten[i].index] = i
            x += root._breite(root._spalten[i])
            kanten.push(x)
        }
        return { kanten: kanten, stelle: stelle }
    }

    //  Haelt die Bindungen einer Zelle gueltig, die beim Umbau kurz hinter dem
    //  Listenende steht; `index` trifft keine Spalte.
    readonly property var _keineSpalte: ({ index: -2, title: "", chars: 0 })

    //  Welche Stelle liegt an der Inhalts-X? Binaer, weil die Kanten steigen.
    function _stelleBeiX(x) {
        const n = root._spalten.length
        if (n === 0) return 0
        const k = root._geometrie.kanten
        var lo = 0, hi = n - 1
        while (lo < hi) {
            const m = (lo + hi + 1) >> 1
            if (k[m] <= x) lo = m
            else hi = m - 1
        }
        return lo
    }

    //  Nur die Spalten im Bild bauen, je eine mehr links und rechts; alle 125
    //  je Zeile kosteten beim Umschalten 237 ms im GUI-Faden.
    readonly property int _vonSpalte:
        Math.max(0, root._stelleBeiX(root.xOffset) - 1)
    readonly property int _spaltenAnz: {
        const n = root._spalten.length
        if (n === 0) return 0
        const bis = Math.min(n, root._stelleBeiX(root.xOffset + flick.width) + 2)
        const anz = Math.max(0, bis - root._vonSpalte)
        return root._ziehSpalte >= 0 ? Math.max(anz, Math.min(n, root._ziehAnz)) : anz
    }
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
    readonly property real gesamtBreite:
        root._geometrie.kanten[root._spalten.length]

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

        Item {
            x: -root.xOffset
            height: parent.height
            Repeater {
                model: root._spaltenAnz
                delegate: Item {
                    id: nrZelle
                    required property int index
                    readonly property var modelData:
                        root._spalten[root._vonSpalte + index] || root._keineSpalte
                    x: root._geometrie.kanten[root._vonSpalte + index]
                    width: root._breite(modelData)
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

        Item {
            x: -root.xOffset
            height: parent.height
            Repeater {
                model: root._spaltenAnz
                delegate: Item {
                    id: kopfZelle
                    required property int index
                    readonly property var modelData:
                        root._spalten[root._vonSpalte + index] || root._keineSpalte
                    x: root._geometrie.kanten[root._vonSpalte + index]
                    width: root._breite(modelData)
                    height: spaltenKopf.height
                    readonly property bool sortiert:
                        root.provider && root.provider.sortColumn === modelData.index

                    Text {
                        anchors { fill: parent; leftMargin: root._pad
                                  rightMargin: kopfZelle.sortiert ? 20 : root._pad }
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
        //  Muss MITfeststehen - sonst steht ueber den Werten ein weggerollter Name.
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

    //  EIN Ziehgriff fuer alle Spalten, ueber beiden Kopfleisten. In der
    //  Kopfkachel darf er NICHT sitzen: wird die Spalte breiter, nimmt der
    //  Repeater die Kachel weg und der Helfer stuerzt mit ihr ab (reproduziert).
    Item {
        id: kopfZug
        objectName: "spaltenZiehgriff"      // Griff fuer tests/bench
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: root._nummernBreite }
        height: nummernLeiste.height + spaltenKopf.height
        z: 3
        visible: height > 0 && root.provider
                 && root.provider.setColumnWidth !== undefined

        //  Die Spalte, deren RECHTE Kante gerade unter dem Zeiger liegt.
        property int spalte: -1
        property real kante: -1

        function suche(px) {
            if (root._ziehSpalte >= 0) return        // waehrend des Zugs steht sie
            const n = root._spalten.length
            if (n === 0) { kopfZug.spalte = -1; return }
            const ziel = px + root.xOffset
            const k = root._geometrie.kanten
            const i = root._stelleBeiX(ziel)
            for (var j = i; j >= i - 1; --j) {
                if (j < 0 || j >= n) continue
                if (Math.abs(k[j + 1] - ziel) <= 4) {
                    kopfZug.spalte = root._spalten[j].index
                    kopfZug.kante = k[j + 1] - root.xOffset
                    return
                }
            }
            kopfZug.spalte = -1
        }

        HoverHandler {
            id: kantenFuehler
            onPointChanged: kopfZug.suche(point.position.x)
            onHoveredChanged: if (!hovered && root._ziehSpalte < 0) kopfZug.spalte = -1
        }

        Item {
            id: griff
            x: kopfZug.kante - 4
            width: 8
            height: parent.height
            visible: kopfZug.spalte >= 0
            HoverHandler { cursorShape: Qt.SizeHorCursor }
            DragHandler {
                target: null
                yAxis.enabled: false
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeHorCursor
                //  Die Systemschwelle (10 px) liess den Anfang tot und die Kante
                //  dann springen; zwei Pixel lassen dem Doppelklick Raum.
                dragThreshold: 2
                onActiveChanged: {
                    if (active) {
                        root._ziehBreite = root._spalteBreite(kopfZug.spalte)
                        root._ziehLinie = kopfZug.kante + root._nummernBreite
                        root._ziehAb = root._ziehLinie
                        root._ziehAnz = root._spaltenAnz
                        root._ziehSpalte = kopfZug.spalte
                        return
                    }
                    root._ziehLinie = -1
                    Qt.callLater(root._ziehUebernehmen)
                }
                //  Erst die Breite klemmen, dann die Linie daraus - sonst zeigte sie
                //  am Anschlag etwas anderes an, als gesetzt wird.
                onTranslationChanged: {
                    if (!active) return
                    const basis = root._spalteBreite(root._ziehSpalte)
                    root._ziehBreite = Math.max(40, Math.round(basis + translation.x))
                    root._ziehLinie = root._ziehAb + (root._ziehBreite - basis)
                }
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.DragThreshold
                onDoubleTapped: if (root.provider && kopfZug.spalte >= 0)
                    root.provider.setColumnWidth(kopfZug.spalte, 0)
            }
        }
    }

    //  Die Ecke links oben, die Zeilenspalte und Kopfleisten gemeinsam frei lassen.
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

        //  Leisten nach oben: als Geschwister des Inhalts hinge ihre Reihenfolge
        //  sonst am Quelltext (daran scheiterte das seitliche Rollen).
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; z: 10 }

        //  Rechts an der Flaeche: die Leisten bekommen den Druck so zuerst, und
        //  `DragThreshold` gibt ihn beim Ziehen wieder her.
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



        //  Senkrechte Gitterlinien EINMAL ueber die sichtbaren Spalten, nicht je Zelle; sie enden an der letzten Zeile.
        Repeater {
            model: root.gridLines ? root._spaltenAnz : 0
            delegate: Rectangle {
                required property int index
                z: 1
                x: root._geometrie.kanten[root._vonSpalte + index + 1] - 1
                width: 1
                height: Math.max(0, Math.min(liste.height, liste.contentHeight - liste.contentY))
                color: root._gitterFarbe
            }
        }

        //  Nur die sichtbaren Zeilen entstehen als Elemente, auch bei 10.000.
        ListView {
            id: liste
            objectName: "datevRows"
            width: flick.contentWidth
            height: flick.height
            model: root.provider ? root.provider.rowCount : 0
            clip: false
            cacheBuffer: 400
            boundsBehavior: Flickable.StopAtBounds

            //  Linksklick unter die letzte Zeile beendet Auswahl und Bearbeitung.
            //  Der Helfer gehoert AN DIE LISTE - an der Flaeche darunter kaeme der
            //  linke Druck nie an, den nimmt die Liste fuer ihr Rollen.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.DragThreshold
                onTapped: function (punkt) {
                    const p = zellSchicht.mapFromItem(null, punkt.scenePosition)
                    if (root._zeileBeiY(p.y) >= 0 && root._spalteBeiX(p.x) >= 0) return
                    root.entmarkiere()
                }
            }

            //  Seitlich rollt er mit und wird deshalb an den Rand des SICHTBAREN
            //  Ausschnitts gerechnet.
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

                //  Eine Leerzeile bekommt keinen Streifen - die Luecke soll zu sehen sein.
                readonly property bool leer:
                    (root.contentRevision >= 0 && root.provider)
                    ? root.provider.rowEmpty(zeile.index) : false

                //  Links waehlt, doppelt bearbeitet. Die Helfer sitzen IN der Zeile:
                //  die Liste nimmt den linken Druck fuer ihr Rollen an, ein Helfer
                //  an der Flaeche darueber bekaeme ihn nie (gemessen).
                //  DREI statt einem: ein Tipp-Helfer meldet die gedrueckten Tasten
                //  nicht, und ohne die Trennung setzte der schlichte bei
                //  Umschalt+Klick den Anker zuletzt wieder um.
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    acceptedModifiers: Qt.NoModifier
                    gesturePolicy: TapHandler.DragThreshold
                    onTapped: function (punkt) { root._linksGetippt(punkt.scenePosition, false) }
                    onDoubleTapped: function (punkt) { root._linksGetippt(punkt.scenePosition, true) }
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    acceptedModifiers: Qt.ControlModifier
                    gesturePolicy: TapHandler.DragThreshold
                    onTapped: function (punkt) { root._linksGetippt(punkt.scenePosition, false) }
                    onDoubleTapped: function (punkt) { root._linksGetippt(punkt.scenePosition, true) }
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    acceptedModifiers: Qt.ShiftModifier
                    gesturePolicy: TapHandler.DragThreshold
                    onTapped: function (punkt) { root._umschaltGetippt(punkt.scenePosition) }
                }
                Rectangle {
                    anchors.fill: parent
                    color: (!root.gridLines && !zeile.leer && zeile.index % 2 === 1)
                           ? Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.05)
                           : "transparent"
                }
                Rectangle {
                    visible: root.gridLines
                    anchors.bottom: parent.bottom
                    width: root._geometrie.kanten[root._spalten.length]
                    height: 1
                    color: root._gitterFarbe
                }
                //  Spaltenhintergruende - unter den Suchmarken, damit ein
                //  Treffer sichtbar bleibt.
                Repeater {
                    model: zeile.leer ? [] : root._bgSpalten
                    delegate: Rectangle {
                        required property var modelData
                        x: root._spalteX(modelData.index)
                        width: root._spalteBreite(modelData.index)
                        height: zeile.height
                        color: modelData.farbe
                    }
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

                Item {
                    anchors.fill: parent
                    Repeater {
                        model: root._spaltenAnz
                        delegate: Item {
                            required property int index
                            readonly property var modelData:
                                root._spalten[root._vonSpalte + index] || root._keineSpalte
                            x: root._geometrie.kanten[root._vonSpalte + index]
                            width: root._breite(modelData)
                            height: zeile.height

                            Text {
                                anchors { fill: parent; leftMargin: root._pad; rightMargin: root._pad }
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                color: modelData.fg !== undefined ? modelData.fg : Editor.text
                                font: modelData.fett === true ? root.cellFontFett : root.cellFont
                                text: (root.contentRevision >= 0 && root.provider)
                                      ? root.provider.cell(zeile.index, modelData.index) : ""
                            }
                            //  Die Ecke sagt, dass die Zahl gerechnet ist; ohne
                            //  Formel faellt die Abfrage je Zelle weg.
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

    //  NEBEN der rollenden Flaeche, sonst wanderten die Nummern aus dem Bild.
    //  Nur die sichtbaren; bei fester Zeilenhoehe genuegt Rechnen.
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

    //  Rad wie im Rest der App: halbe Sichthoehe je Rastung, weich ueber 180 ms
    //  (Qt gibt 60 px vor). Eine MouseArea ist zwingend - ein Flickable nimmt
    //  Radereignisse vor jedem WheelHandler.
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
    //  Koordinaten doppelt versetzt), ohne Zeigerhelfer - einer verdeckte die
    //  waagerechte Bildlaufleiste.
    Item {
        id: zellSchicht
        anchors.fill: flick
        clip: true
        z: 3

        //  UNTER dem Rahmen der Ankerzelle: so bleibt sichtbar, von wo aus gezogen wurde.
        Rectangle {
            visible: root.hatBereich && root._stelleVon >= 0
            x: root._geometrie.kanten[root._stelleVon] - root.xOffset
            y: root._zeileVon * root.rowHeight - liste.contentY
            width: root._geometrie.kanten[root._stelleBis + 1]
                   - root._geometrie.kanten[root._stelleVon]
            height: (root._zeileBis - root._zeileVon + 1) * root.rowHeight
            color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.16)
            border.width: 1
            border.color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                  App.themeAccent.b, 0.55)
            radius: 2
        }

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

        //  Nur beim Bearbeiten sichtbar - unsichtbar nimmt es weder Taste noch Klick.
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

    //  Tastatur nur mit gewaehlter Zelle - sonst gehoeren die Pfeile dem Viewer.
    Keys.onPressed: function (e) {
        if (root.bearbeitet || root.selRow < 0 || root.selColumn < 0 || !root.provider) return
        const strg = (e.modifiers & Qt.ControlModifier) !== 0
        const um = (e.modifiers & Qt.ShiftModifier) !== 0
        const seite = Math.max(1, Math.floor(liste.height / root.rowHeight) - 1)
        switch (e.key) {
        case Qt.Key_Up:       root._bewege(-1, 0, um); e.accepted = true; return
        case Qt.Key_Down:     root._bewege(1, 0, um);  e.accepted = true; return
        case Qt.Key_Left:     root._bewege(0, -1, um); e.accepted = true; return
        case Qt.Key_Right:    root._bewege(0, 1, um);  e.accepted = true; return
        case Qt.Key_Tab:      root._bewege(0, 1, false);  e.accepted = true; return
        case Qt.Key_Backtab:  root._bewege(0, -1, false); e.accepted = true; return
        case Qt.Key_PageUp:   root._bewege(-seite, 0, um); e.accepted = true; return
        case Qt.Key_PageDown: root._bewege(seite, 0, um);  e.accepted = true; return
        case Qt.Key_Home:
            root._bewege(strg ? -root.provider.rowCount : 0,
                         -root._spalten.length, um); e.accepted = true; return
        case Qt.Key_End:
            root._bewege(strg ? root.provider.rowCount : 0,
                         root._spalten.length, um); e.accepted = true; return
        case Qt.Key_Escape:   root.entmarkiere(); e.accepted = true; return
        }
        //  Eine Markierung zeigt nur; bearbeitet wird per Doppelklick, F2 oder Menue.
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
            text: App.uiText(App.language, "TableColBold")
            checkable: true
            checked: root._spalteFett(spaltenMenue.spalte)
            onTriggered: root.provider.setColumnBold(spaltenMenue.spalte,
                                                     !root._spalteFett(spaltenMenue.spalte))
        }
        //  Der Tupfer rechts zeigt die gesetzte Farbe; ohne ihn muesste man
        //  jede Zeile aufklappen, um zu sehen, wo ueberhaupt etwas steht.
        component FarbZeile: MenuItem {
            property bool flaeche: false
            readonly property string farbe:
                root._spalteFarbe(spaltenMenue.spalte, flaeche)
            onTriggered: farbWahl.oeffne(spaltenMenue.spalte, flaeche)
            Rectangle {
                anchors { right: parent.right; rightMargin: 10
                          verticalCenter: parent.verticalCenter }
                width: 12; height: 12; radius: 3
                visible: parent.farbe !== ""
                color: parent.farbe === "" ? "transparent" : parent.farbe
                border.color: App.themeBorder
                border.width: 1
            }
        }
        FarbZeile {
            text: App.uiText(App.language, "TableColTextColor")
            flaeche: false
        }
        FarbZeile {
            text: App.uiText(App.language, "TableColFillColor")
            flaeche: true
        }
        MenuItem {
            text: App.uiText(App.language, "TableColClearFormat")
            enabled: root.provider && root.provider.columnHasFormat !== undefined
                     && root.provider.columnHasFormat(spaltenMenue.spalte)
            onTriggered: root.provider.clearColumnFormat(spaltenMenue.spalte)
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
            text: App.uiText(App.language,
                             root.hatBereich ? "TableCopyRange" : "TableCopyCell")
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

    //  Nicht jede Spalte ist im Bild - gesucht wird ueber `index`.
    function _spalteKarte(sp) {
        for (var i = 0; i < root._spalten.length; ++i)
            if (root._spalten[i].index === sp) return root._spalten[i]
        return null
    }
    function _spalteFett(sp) {
        const k = root._spalteKarte(sp)
        return k !== null && k.fett === true
    }
    //  Gesetzte Farbe der Spalte, sonst leer - die Menuezeile zeigt sie als
    //  Tupfer, damit man OHNE Aufklappen sieht, wo etwas gesetzt ist.
    function _spalteFarbe(sp, flaeche) {
        const k = root._spalteKarte(sp)
        if (k === null) return ""
        const w = flaeche ? k.bg : k.fg
        return w === undefined ? "" : w
    }

    //  Farbwahl je Spalte: eine kleine Palette und ein Weg zum vollen Waehler.
    //  Zwei Paletten, weil eine Textfarbe kraeftig und eine Flaeche blass sein
    //  muss; eine gemeinsame taugte fuer keines von beiden.
    readonly property var _textFarben: [
        "#d13438", "#ca5010", "#986f0b", "#0f7b0f", "#038387",
        "#0078d4", "#8764b8", "#c239b3", "#4f5b62", "#1a1a1a"]
    readonly property var _flaechenFarben: [
        "#fde7e9", "#fdf0e3", "#fdf6e3", "#e7f6e7", "#e3f6f6",
        "#e5f1fb", "#f0eaf8", "#fbe9f7", "#e8eaed", "#c9ccd1"]

    Popup {
        id: farbWahl
        property int  spalte: -1
        property bool flaeche: false
        function oeffne(sp, fuerFlaeche) {
            farbWahl.spalte = sp
            farbWahl.flaeche = fuerFlaeche
            farbWahl.x = Math.max(0, Math.min(root._zelleX(sp) + root._nummernBreite,
                                              root.width - farbWahl.implicitWidth))
            farbWahl.y = nummernLeiste.height + spaltenKopf.height
            farbWahl.open()
        }
        function setze(farbe) {
            if (!root.provider) return
            if (farbWahl.flaeche) root.provider.setColumnBackground(farbWahl.spalte, farbe)
            else                  root.provider.setColumnColor(farbWahl.spalte, farbe)
            farbWahl.close()
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
        contentItem: Column {
            spacing: 8
            Grid {
                columns: 5
                spacing: 4
                Repeater {
                    model: farbWahl.flaeche ? root._flaechenFarben : root._textFarben
                    delegate: Rectangle {
                        required property string modelData
                        width: 24; height: 20; radius: 3
                        color: modelData
                        border.width: feldHover.hovered ? 2 : 1
                        border.color: feldHover.hovered ? App.themeAccent : App.themeBorder
                        HoverHandler { id: feldHover }
                        TapHandler { onTapped: farbWahl.setze(modelData) }
                    }
                }
            }
            Row {
                spacing: 8
                //  Leere Farbe = nicht gesetzt: Thema bzw. kein Hintergrund.
                Rectangle {
                    width: 90; height: 24; radius: 4
                    color: ohneHover.hovered ? App.themeCard : "transparent"
                    border.color: App.themeBorder
                    Text {
                        anchors.centerIn: parent
                        text: App.uiText(App.language, "TableColNoColor")
                        color: App.themeTextPrimary
                        font.pixelSize: 11
                    }
                    HoverHandler { id: ohneHover }
                    TapHandler { onTapped: farbWahl.setze("") }
                }
                ColorPicker {
                    width: 40; height: 24
                    showAlpha: false
                    title: App.uiText(App.language, "TableColOwnColor")
                    onColorPicked: function (c) { farbWahl.setze(c.toString()) }
                }
            }
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

    //  Zeigt beim Ziehen, wo die Kante landet.
    Rectangle {
        objectName: "spaltenZiehlinie"      // Griff fuer tests/bench
        visible: root._ziehLinie >= 0
        x: root._ziehLinie - 1
        width: 2
        y: 0
        height: root.height
        color: App.themeAccent
        z: 5
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
