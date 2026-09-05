pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Editierbarer Plain-Text-Editor; Lesen und atomares Schreiben über `Viewer`. Ungespeicherte Änderungen tragen
// "•" und werden beim Verlassen automatisch gesichert, damit keine Eingaben verloren gehen.
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0

    property string currentPath: ""
    property bool   dirty: false
    property bool   _loading: false
    // Datei größer als der Lesedeckel? Dann liegt nur ihr Anfang im Editor, und Schreiben löschte den Rest - der
    // Editor geht auf nur lesen, die C++-Seite sperrt zusätzlich.
    property bool   _readOnly: false
    property bool   _pdfBusy: false

    // Der Zähler treibt die Neuauswertung: die Farbe kommt aus einer Invokable, es gibt also kein Signal, an dem
    // eine Bindung hängen könnte. `App.textPdfColor` steht mit in der Bindung, damit eine Datei ohne eigene nachzieht.
    property int    _inkRev: 0
    readonly property color _pdfInk: {
        // Die globale Vorgabe wird IMMER gelesen, auch wenn sie gleich verworfen wird: eine Bindung hängt nur an dem,
        // was sie anfasst - im else-Zweig bliebe eine Datei ohne eigene Farbe auf dem alten Wert stehen.
        var vorgabe = App.textPdfColor
        if (!(root._inkRev, root.currentPath.length > 0)) return vorgabe
        var own = mediaModel.fileTextPdfColor(root.currentPath)
        return (own && own.a > 0) ? own : vorgabe
    }
    readonly property bool _pdfInkOwn:
        (root._inkRev, root.currentPath.length > 0)
        && mediaModel.hasFileTextPdfColor(root.currentPath)

    readonly property bool _isHtml: {
        var p = root.currentPath.toLowerCase()
        return p.endsWith(".html") || p.endsWith(".htm")
    }

    //  Bricht die Anzeige um? Der Schalter steht in den Einstellungen
    //  (`Editor.softWrap`); HTML-Quelltext bleibt davon ausgenommen und läuft
    //  weiter waagerecht, damit die Verschachtelung ablesbar bleibt.
    readonly property bool _wrap: Editor.softWrap && !root._isHtml

    //  Suchen und Ersetzen (Strg+F)
    //  Dieselbe Form wie im DOCX-Editor (`DocxSurface` ▸ findBar), damit die App
    //  einheitlich bleibt: Leiste oben rechts, Strg+F auf, Esc zu.
    property bool   _findOpen: false
    property bool   _findCase: false
    property bool   _findWords: false
    property bool   _findHighlight: true
    property string _findStatus: ""

    function _openFind() {
        if (editor.selectedText.length > 0 && editor.selectedText.indexOf("\n") < 0)
            findField.text = editor.selectedText
        root._findOpen = true
        findField.field.forceActiveFocus()
        findField.field.selectAll()
        root._findRefresh()
    }
    function _closeFind() {
        root._findOpen = false
        root._findStatus = ""
        syntax.highlightMatches("", false)
        decorations.setSearchTerm("", false)
        editor.forceActiveFocus()
    }
    function _findFrom(von, rueckwaerts) {
        if (findField.text.length === 0) { root._findStatus = ""; return }
        const r = syntax.findNext(findField.text, von, root._findCase,
                                  root._findWords, rueckwaerts)
        if (!r.found) {
            root._findStatus = App.uiText(App.language, "EditorFindNoMatch")
            return
        }
        // Liegt der Treffer in einem zugeklappten Bereich, wird dieser aufgeklappt und bleibt es: man springt den
        // Treffer an, also will man ihn sehen. Die übrigen bleiben verborgen, ihre Faltmarke wird hervorgehoben.
        foldBar.ensureVisible(r.start)
        editor.select(r.start, r.start + r.length)
        root._findStatus = App.uiText(App.language, "EditorFindCounter")
                              .arg(r.index).arg(r.total)
    }
    function _findNext(rueckwaerts) {
        root._findFrom(rueckwaerts ? editor.selectionStart : editor.selectionEnd,
                       rueckwaerts)
    }
    function _findRefresh() {
        root._findFrom(editor.selectionStart, false)
        highlightTimer.restart()
    }

    function _doReplace() {
        if (root._readOnly || findField.text.length === 0) return
        const r = syntax.replaceAndFind(findField.text, replaceField.text,
                                        editor.selectionStart,
                                        root._findCase, root._findWords)
        if (r.found) {
            editor.select(r.start, r.start + r.length)
            root._findStatus = App.uiText(App.language, "EditorFindCounter")
                                  .arg(r.index).arg(r.total)
        } else {
            root._findStatus = App.uiText(App.language, "EditorFindNoMatch")
        }
        highlightTimer.restart()
    }
    function _doReplaceAll() {
        if (root._readOnly || findField.text.length === 0) return
        const n = syntax.replaceAll(findField.text, replaceField.text,
                                    root._findCase, root._findWords)
        root._findStatus = n > 0
            ? App.uiText(App.language, "EditorFindReplaced").arg(n)
            : App.uiText(App.language, "EditorFindNoMatch")
        highlightTimer.restart()
    }

    function save() {
        if (root._readOnly) return
        if (!root.dirty || root.currentPath.length === 0) return
        if (Viewer.writeTextFile(root.currentPath, editor.text))
            root.dirty = false
    }

    function release() {
        root.save()                       // beim Verlassen sichern (kein Datenverlust)
        editor.text = ""
        root.currentPath = ""
        root.dirty = false
        root._readOnly = false
    }

    onSourceChanged: {
        root.save()                       // evtl. vorherige Datei sichern
        root._loading = true
        root.currentPath = source
        root._readOnly = source.length > 0 && Viewer.textFileTruncated(source)
        editor.text = source.length > 0 ? Viewer.readTextFile(source) : ""
        editor.cursorPosition = 0
        root.dirty = false
        root._loading = false
    }

    Rectangle { anchors.fill: parent; color: Editor.background }

    CodeHighlighter {
        id: syntax
        document: editor.textDocument
        path: root.currentPath
        tabWidth: Editor.tabWidth
    }

    // KEINE eigene Werkzeugleiste mehr: Speichern passiert von selbst (Strg+S bleibt), "-> PDF" steht im Menü
    // "Dokument", Übersicht und Transliteration in der Leiste darüber. Damit bleibt EINE Leiste statt zweier.

    readonly property color pdfInk: root._pdfInk
    readonly property bool  pdfInkOwn: root._pdfInkOwn
    readonly property bool  pdfBusy: root._pdfBusy
    function pickPdfInk(c) {
        if (root.currentPath.length === 0) return
        mediaModel.setFileTextPdfColor(root.currentPath, c)
        root._inkRev++
    }
    function resetPdfInk() {
        if (root.currentPath.length === 0) return
        mediaModel.clearFileTextPdfColor(root.currentPath)
        root._inkRev++
    }
    function exportPdf() {
        if (root.currentPath.length === 0 || root._pdfBusy) return
        root._pdfBusy = true
        Viewer.exportTextToPdf(root.currentPath, editor.text,
                               root._pdfInk, Editor.tabWidth)
    }

    Timer {
        interval: Math.max(5, App.autoSaveInterval) * 1000
        repeat: true
        running: App.autoSaveEnabled && root.dirty && !root._readOnly
                 && root.currentPath.length > 0
        onTriggered: root.save()
    }

    TextGutter {
        id: gutter
        visible: Editor.lineNumbers
        anchors { left: parent.left; top: parent.top; topMargin: root.topInset
                  bottom: statusBar.top }
        width: visible ? requiredWidth : 0
        z: 2

        document: editor.textDocument
        contentY: flick.contentY
        topPadding: editor.topPadding
        cursorPosition: editor.cursorPosition
        font: editor.font
        backgroundColor: Editor.gutterBackground
        textColor: Editor.gutterText
        activeColor: Editor.gutterTextActive
        borderColor: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                             Editor.gutterText.b, 0.25)
    }

    readonly property real _textHoehe:
        foldBar.hasRegions && foldBar.documentHeight > 0
            ? foldBar.documentHeight + editor.topPadding + editor.bottomPadding
            : editor.paintedHeight

    // Gerechnet wird über die SZENEN-Position: `point.position` steht in Koordinaten des Textfeldes, und das
    // wandert beim Rollen unter dem stillstehenden Zeiger weg - der Abstand wäre nach dem ersten Schritt falsch.
    property real _ziehTick: 0

    readonly property real _ziehAbstand: {
        if (!zieher.active) return 0
        const y = flick.mapFromItem(null, zieher.point.scenePosition).y
        if (y < 0) return y
        if (y > flick.height) return y - flick.height
        return 0
    }

    function _blaettern(runter) {
        const schritt = flick.height * (runter ? 1 : -1)
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height,
                                              flick.contentY + schritt))
    }

    function _cursorInsSichtfeld() {
        const r = editor.positionToRectangle(editor.cursorPosition)
        if (r.height <= 0) return
        const max = Math.max(0, flick.contentHeight - flick.height)
        if (r.y < flick.contentY)
            flick.contentY = Math.min(max, Math.max(0, r.y))
        else if (r.y + r.height > flick.contentY + flick.height)
            flick.contentY = Math.min(max, r.y + r.height - flick.height)
    }

    // Faltungsleiste rechts von den Nummern; sie erscheint nur, wenn die Datei überhaupt faltbare Blöcke hat
    // (`hasRegions`) - eine .txt bekommt sie nie zu sehen.
    TextFoldBar {
        id: foldBar
        objectName: "textFoldBar"
        visible: Editor.folding && Editor.lineNumbers && hasRegions
        anchors { left: gutter.right; top: parent.top; topMargin: root.topInset
                  bottom: statusBar.top }
        width: visible ? requiredWidth : 0
        z: 2

        document: editor.textDocument
        textArea: editor
        flickable: flick
        path: root.currentPath
        contentY: flick.contentY
        topPadding: editor.topPadding
        font: editor.font
        tabWidth: Editor.tabWidth
        backgroundColor: Editor.gutterBackground
        markerColor: Editor.gutterText
    }

    Timer {
        id: ziehRoller
        interval: 16                       // rund ein Bild
        repeat: true
        running: zieher.active && root._ziehAbstand !== 0
        // Gerollt wird nach VERGANGENER ZEIT, nicht je Auslösung: ein Zeitgeber feuert je nach Last unterschiedlich
        // oft (gemessen: 38 Schritte in 0,60 s, Summe der Zeitschritte 0,608 s). KEIN Zurücksetzen bei `running` - die
        // Bedingung flackert während des Rollens, und jedes Flackern liess `dt` beim Ersatzwert (gemessen 0.016).
        onTriggered: {
            const jetzt = Date.now()
            const vorher = root._ziehTick
            const dt = vorher > 0 ? Math.min(0.05, (jetzt - vorher) / 1000) : 0.016
            root._ziehTick = jetzt

            const weg = root._ziehAbstand
            const proSekunde = Math.min(1800, 150 + Math.abs(weg) * 12)
            const tempo = proSekunde * dt * (weg < 0 ? -1 : 1)
            const ziel = Math.max(0, Math.min(flick.contentHeight - flick.height,
                                              flick.contentY + tempo))
            if (ziel === flick.contentY) return
            flick.contentY = ziel
            // Die Auswahl muss mitziehen, sonst rollt nur die Ansicht. Sie endet an der letzten SICHTBAREN Zeile, nicht
            // beim Zeiger: sonst rollt `_cursorInsSichtfeld` hinterher (gemessen 1500 px/s, wo 250 gewollt waren).
            const p = editor.mapFromItem(null, zieher.point.scenePosition)
            const yInnen = Math.max(flick.contentY + 1,
                                    Math.min(flick.contentY + flick.height - 1, p.y))
            editor.moveCursorSelection(editor.positionAt(p.x, yInnen),
                                       TextEdit.SelectCharacters)
            flick.contentY = ziel
        }
    }

    // Übersichtsspalte ("Minimap") zeigt die ganze Datei verkleinert und lässt sich darin scrollen. Sie kostet
    // Breite und ist deshalb standardmäßig aus (`Editor.minimap`).
    TextMinimap {
        id: minimap
        objectName: "textMinimap"
        visible: Editor.minimap
        anchors { right: parent.right; top: parent.top; topMargin: root.topInset
                  bottom: statusBar.top }
        width: visible ? 110 : 0
        z: 2

        document: editor.textDocument
        contentY: flick.contentY
        viewportHeight: flick.height
        contentHeight: flick.contentHeight
        backgroundColor: Editor.gutterBackground
        textColor: Editor.text
        viewportColor: Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.13)
        borderColor: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                             Editor.gutterText.b, 0.25)

        onScrollRequested: function (ziel) { flick.contentY = ziel }
    }

    Flickable {
        id: flick
        anchors {
            left: foldBar.right; right: minimap.left
            top: parent.top; topMargin: root.topInset; bottom: statusBar.top
        }
        clip: true
        // RANDLOS: der Text füllt die Kachel von Rand zu Rand wie in einer IDE - der frühere 12-px-Rand mit gerundetem
        // Rechteck war das "extra Feld", das weg sollte. Der weiche Umbruch ist rein optisch, die Datei merkt nichts.
        contentWidth: root._wrap ? width : editor.paintedWidth
        // Beim Falten weiß `paintedHeight` nichts von den verborgenen Zeilen (gemessen: Dokument 68017 px,
        // `paintedHeight` blieb bei 85017), und sie dazu zu bewegen kostete einen Sprung an den Dateianfang.
        contentHeight: editor.height
        onContentHeightChanged: {
            const max = Math.max(0, contentHeight - height)
            if (contentY > max) contentY = max
        }
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar {
            policy: root._wrap ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
        }

        // y und Höhe kommen aus `positionToRectangle`, das auch eine weich umgebrochene Zeile trifft. KEIN
        // `+ topPadding`: die Funktion rechnet die Polsterung schon ein (bei `padding: 10` liefert Position 0 y = 10),
        // ein zweites Addieren schob den Streifen um 10 px. Die Nummernspalte muss es addieren - `blockBoundingRect` beginnt bei 0.
        Rectangle {
            visible: Editor.highlightCurrentLine && !root._readOnly
                     && editor.activeFocus
            z: -1
            x: 0
            width: Math.max(flick.width, flick.contentWidth)
            y: editor.positionToRectangle(editor.cursorPosition).y
            height: editor.positionToRectangle(editor.cursorPosition).height
            color: Editor.currentLine
        }

        TextDecorations {
            id: decorations
            objectName: "textDecorations"
            z: -1
            x: 0
            y: flick.contentY
            width: Math.max(flick.width, flick.contentWidth)
            height: flick.height

            document: editor.textDocument
            path: root.currentPath
            foldBar: foldBar
            contentY: flick.contentY
            viewportHeight: flick.height
            cursorPosition: editor.cursorPosition
            leftPadding: editor.leftPadding
            topPadding: editor.topPadding
            font: editor.font
            tabWidth: Editor.tabWidth
            showGuides: Editor.indentGuides
            showBrackets: Editor.matchBrackets

            guideColor: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                Editor.gutterText.b, 0.45)
            markerColor: Editor.gutterText
            bracketColor: Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.20)
            errorColor: Qt.rgba(0.86, 0.31, 0.31, 0.38)
            matchColor: Qt.rgba(Editor.selection.r, Editor.selection.g,
                                Editor.selection.b, 0.55)
        }

        // BEWUSST ohne `TextArea.flickable`: diese Anbindung installiert Qts "Cursor ins Bild rollen", und das feuert
        // bei JEDER Layout-Neuberechnung - beim Zuklappen eines Blocks sprang die Ansicht an den Dateianfang (gemessen:
        // Scrollstand 4000 -> 17). Als gewöhnliches Kind folgt sie dem Cursor nur, wenn er sich wirklich bewegt.
        TextArea {
            id: editor
            objectName: "textEditorArea"
            readOnly: root._readOnly
            selectByMouse: true
            width: root._wrap ? flick.width : Math.max(implicitWidth, flick.width)
            height: Math.max(root._textHoehe, flick.height)
            onCursorPositionChanged: root._cursorInsSichtfeld()

            // Die drei Punkte SIND der Knopf, nicht nur eine Marke. Ein `TapHandler` nimmt den Zeiger nur passiv - das
            // Textfeld behält Markieren und Cursor setzen. Gerechnet wird in den Koordinaten der Zusatzzeichnung.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: function (punkt) {
                    const p = decorations.mapFromItem(editor, punkt.position)
                    const start = decorations.foldMarkerAt(p.x, p.y)
                    if (start >= 0) foldBar.toggleFold(start)
                }
            }

            // Zieht man aus dem Fenster heraus, bekommt das Textfeld keine Mausereignisse mehr und die Auswahl bliebe an
            // der letzten sichtbaren Zeile stehen. Der `PointHandler` verfolgt den Zeiger, ohne ihn zu greifen.
            PointHandler {
                id: zieher
                objectName: "textZieher"
                acceptedButtons: Qt.LeftButton
                target: null
            }
            wrapMode: root._wrap ? TextEdit.Wrap : TextEdit.NoWrap
            color: Editor.text
            selectionColor: Editor.selection
            font: App.fallbackFont("monospace", 13)
            padding: 10
            background: null
            // Live-Transliteration über gezieltes `remove()`/`insert()` statt Neuzuweisung von `text` - so bleiben
            // Undo-Stack und Tempo großer Dateien intakt. Pfeil-runter in der letzten Zeile springt ans Zeilenende.
            Keys.onDownPressed: (e) => {
                const yCur = editor.positionToRectangle(editor.cursorPosition).y
                const yEnd = editor.positionToRectangle(editor.length).y
                if (Math.abs(yCur - yEnd) < 0.5 && editor.cursorPosition < editor.length) {
                    if (e.modifiers & Qt.ShiftModifier)
                        editor.moveCursorSelection(editor.length)
                    else
                        editor.cursorPosition = editor.length
                    e.accepted = true
                } else {
                    e.accepted = false
                }
            }
            property bool _trGuard: false
            function _applyTranslit() {
                if (editor._trGuard || root._loading || !Translit.enabled)
                    return
                editor._trGuard = true
                const r = Translit.applyInDocument(editor.textDocument,
                                                   editor.cursorPosition)
                if (r.changed)
                    editor.cursorPosition = r.cursor
                editor._trGuard = false
            }

            // Strg+Z / Strg+Y: die Transliteration muss STILLHALTEN. Ein Undo stellt den lateinischen Stand her,
            // `onTextChanged` feuert, und sie schriebe ihn sofort um - Strg+Z pendelte endlos (`bench_translitundo`).
            function _guardedUndo() {
                editor._trGuard = true
                editor.undo()
                editor._trGuard = false
            }
            function _guardedRedo() {
                editor._trGuard = true
                editor.redo()
                editor._trGuard = false
            }
            onTextChanged: {
                if (!root._loading) root.dirty = true
                editor._applyTranslit()
            }
            Keys.onPressed: function(e) {
                if (e.key === Qt.Key_PageDown || e.key === Qt.Key_PageUp) {
                    root._blaettern(e.key === Qt.Key_PageDown)
                    e.accepted = true
                    return
                }
                if (!(e.modifiers & Qt.ControlModifier))
                    return
                if (e.key === Qt.Key_S) {
                    root.save(); e.accepted = true; return
                }
                if (e.key === Qt.Key_Z) {
                    if (e.modifiers & Qt.ShiftModifier) editor._guardedRedo()
                    else                                editor._guardedUndo()
                    e.accepted = true; return
                }
                if (e.key === Qt.Key_Y) {
                    editor._guardedRedo(); e.accepted = true
                    return
                }
                if (e.key === Qt.Key_F) {
                    root._openFind(); e.accepted = true
                }
            }

            // Mit `Editor.tabSpaces` (Vorgabe AN, wie Kate) schreibt die Taste LEERZEICHEN statt eines Tabulators - Kate
            // legt gar keinen in die Datei. Aufgefüllt wird bis zum NÄCHSTEN Halt, sonst stünden die Spalten versetzt.
            Keys.onTabPressed: function(e) {
                if (root._readOnly) { e.accepted = true; return }
                if (!Editor.tabSpaces) { e.accepted = false; return }
                const spalte = syntax.columnAt(editor.cursorPosition)
                const rest = Editor.tabWidth - (spalte % Editor.tabWidth)
                editor.insert(editor.cursorPosition, " ".repeat(rest))
                e.accepted = true
            }
        }
    }

    Timer {
        id: highlightTimer
        interval: 180
        onTriggered: {
            syntax.highlightMatches(
                root._findHighlight ? findField.text : "", root._findCase)
            decorations.setSearchTerm(findField.text, root._findCase)
        }
    }

    Rectangle {
        id: findBar
        visible: root._findOpen
        anchors { top: parent.top; topMargin: root.topInset + 8
                  right: parent.right; rightMargin: 18 }
        z: 6
        radius: 8
        color: App.themeMenuBarBg
        border.color: App.themeBorder
        width: findGrid.implicitWidth + 20
        height: findGrid.implicitHeight + 16

        component FField: Rectangle {
            id: ff
            property alias field: innerField
            property alias text: innerField.text
            property string ph: ""
            signal accepted()
            signal shiftAccepted()
            signal escaped()
            width: 200; height: 28; radius: 6
            color: App.themeCard
            border.color: innerField.activeFocus ? App.themeAccent : App.themeBorder
            TextField {
                id: innerField
                anchors.fill: parent
                anchors.leftMargin: 8; anchors.rightMargin: 8
                verticalAlignment: TextInput.AlignVCenter
                placeholderText: ff.ph
                color: App.themeTextPrimary
                placeholderTextColor: App.themeTextMuted
                font.pixelSize: 12
                background: null
                Keys.onPressed: function (e) {
                    if (e.key === Qt.Key_Escape) {
                        ff.escaped(); e.accepted = true
                    } else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                        if (e.modifiers & Qt.ShiftModifier) ff.shiftAccepted()
                        else                                ff.accepted()
                        e.accepted = true
                    }
                }
            }
        }

        // ALLES über die eigene `id`, nie über `parent`: an der Wurzel einer Inline-Komponente zeigt `parent` auf das
        // umgebende Element, in einem `TapHandler` sogar auf die Wurzel der Datei - ohne id blieb jede Beschriftung leer.
        component FBtn: Rectangle {
            id: fb
            property string iconName: ""
            property string label: ""
            property string tip: ""
            property bool   an: false
            property bool   wide: false
            signal clicked()
            width: fb.wide ? (btnText.implicitWidth + 20) : 28
            height: 28; radius: 6
            color: fbHover.hovered ? App.themeCard : "transparent"
            border.color: fb.an ? App.themeAccent : App.themeBorder
            DrawnIcon {
                anchors.centerIn: parent
                visible: fb.iconName.length > 0
                name: fb.iconName
                size: 14
                color: fb.an ? App.themeAccent : App.themeTextPrimary
            }
            Text {
                id: btnText
                anchors.centerIn: parent
                visible: fb.iconName.length === 0
                text: fb.label
                color: fb.an ? App.themeAccent : App.themeTextPrimary
                font.pixelSize: 12
            }
            HoverHandler { id: fbHover }
            TapHandler { onTapped: fb.clicked() }
            ToolTip.visible: fbHover.hovered && fb.tip.length > 0
            ToolTip.delay: 500
            ToolTip.text: fb.tip
        }

        GridLayout {
            id: findGrid
            anchors.centerIn: parent
            columns: 2
            rowSpacing: 6
            columnSpacing: 8

            FField {
                id: findField
                ph: App.uiText(App.language, "EditorFindPlaceholder")
                onAccepted: root._findNext(false)
                onShiftAccepted: root._findNext(true)
                onEscaped: root._closeFind()
                field.onTextChanged: root._findRefresh()
            }
            RowLayout {
                spacing: 4
                FBtn { iconName: "chevron-up"
                       tip: App.uiText(App.language, "EditorFindPrev")
                       onClicked: root._findNext(true) }
                FBtn { iconName: "chevron-down"
                       tip: App.uiText(App.language, "EditorFindNext")
                       onClicked: root._findNext(false) }
                FBtn { label: "Aa"; tip: App.uiText(App.language, "EditorFindCase")
                       an: root._findCase
                       onClicked: { root._findCase = !root._findCase; root._findRefresh() } }
                FBtn { label: "W"; tip: App.uiText(App.language, "EditorFindWords")
                       an: root._findWords
                       onClicked: { root._findWords = !root._findWords; root._findRefresh() } }
                FBtn { iconName: "list"
                       tip: App.uiText(App.language, "EditorFindHighlight")
                       an: root._findHighlight
                       onClicked: { root._findHighlight = !root._findHighlight
                                    highlightTimer.restart() } }
                FBtn { iconName: "close"
                       tip: App.uiText(App.language, "EditorFindClose")
                       onClicked: root._closeFind() }
            }

            FField {
                id: replaceField
                enabled: !root._readOnly
                opacity: enabled ? 1.0 : 0.45
                ph: App.uiText(App.language, "EditorReplacePlaceholder")
                onAccepted: root._doReplace()
                onEscaped: root._closeFind()
            }
            RowLayout {
                spacing: 4
                enabled: !root._readOnly
                opacity: enabled ? 1.0 : 0.45
                FBtn { wide: true; label: App.uiText(App.language, "EditorFindReplaceOne")
                       onClicked: root._doReplace() }
                FBtn { wide: true; label: App.uiText(App.language, "EditorFindReplaceAll")
                       onClicked: root._doReplaceAll() }
                Item { Layout.fillWidth: true }
                Text {
                    text: root._findStatus
                    color: root._findStatus === App.uiText(App.language, "EditorFindNoMatch")
                           ? Qt.rgba(1, 0.55, 0.35, 1) : App.themeTextMuted
                    font.pixelSize: 11
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }

    Rectangle {
        id: statusBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  bottomMargin: root.bottomInset }
        height: 22
        color: Editor.gutterBackground
        z: 3
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1
                    color: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                   Editor.gutterText.b, 0.25) }

        Row {
            anchors { left: parent.left; leftMargin: 10
                      verticalCenter: parent.verticalCenter }
            spacing: 14

            component Feld: Text {
                color: Editor.gutterText
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            Feld {
                text: App.uiText(App.language, "EditorStatusLineCol")
                          .arg(gutter.cursorLine).arg(gutter.cursorColumn)
            }
            Feld { text: gutter.lineCount + " " + App.uiText(App.language, "EditorStatusLines") }
            Feld { text: syntax.active ? syntax.languageLabel
                                       : App.uiText(App.language, "EditorStatusPlain") }
            Feld { text: "UTF-8" }
            Feld {
                text: App.uiText(App.language, "EditorStatusWrapOn")
                visible: root._wrap
            }
        }

        Row {
            anchors { right: parent.right; rightMargin: root._readOnly ? 140 : 10
                      verticalCenter: parent.verticalCenter }
            visible: root.dirty && !root._readOnly
            spacing: 5
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 6; height: 6; radius: 3
                color: App.themeAccent
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: App.uiText(App.language, "EditorStatusModified")
                color: App.themeAccent
                font.pixelSize: 11
            }
        }

        Text {
            anchors { right: parent.right; rightMargin: 10
                      verticalCenter: parent.verticalCenter }
            visible: root._readOnly
            text: App.uiText(App.language, "TextReadOnlyTip")
            color: Qt.rgba(1, 0.72, 0.45, 1)
            font.pixelSize: 11
            elide: Text.ElideRight
            width: Math.min(implicitWidth, statusBar.width * 0.6)
        }
    }

    Connections {
        target: Viewer
        function onTextPdfExportFinished(ok, target, error) {
            root._pdfBusy = false
            if (ok)
                root._toast(App.uiText(App.language, "TextExportPdfOk")
                                .arg(target.split("/").pop()))
            else
                root._toast(App.uiText(App.language, "TextExportPdfFail")
                                .arg(error.length > 0 ? error : "?"))
        }
    }

    function _toast(msg) {
        toastLabel.text = msg
        toast.visible = true
        toastTimer.restart()
    }
    Rectangle {
        id: toast
        anchors { horizontalCenter: parent.horizontalCenter; bottom: statusBar.top
                  bottomMargin: 18 }
        width: toastLabel.implicitWidth + 28
        height: 32
        radius: 16
        visible: false
        z: 8
        color: Qt.rgba(0, 0, 0, 0.78)
        border.color: App.themeBorder; border.width: 1
        Text {
            id: toastLabel
            anchors.centerIn: parent
            color: "#ffffff"; font.pixelSize: 12
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, root.width - 80)
            horizontalAlignment: Text.AlignHCenter
        }
        Timer { id: toastTimer; interval: 3500; onTriggered: toast.visible = false }
    }

    NumberAnimation {
        id: scrollAnim
        target: flick; property: "contentY"
        duration: 180; easing.type: Easing.OutCubic
    }
    MouseArea {
        anchors.fill: flick
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => {
            var maxY = Math.max(0, flick.contentHeight - flick.height)
            if (maxY <= 0) { wheel.accepted = true; return }
            var raw = (wheel.angleDelta.y !== 0)
                      ? (wheel.angleDelta.y / 120) * (flick.height * 0.5)
                      : wheel.pixelDelta.y * 1.6
            var base = scrollAnim.running ? scrollAnim.to : flick.contentY
            var tgt = Math.max(0, Math.min(base - raw, maxY))
            scrollAnim.from = flick.contentY
            scrollAnim.to = tgt
            scrollAnim.restart()
            wheel.accepted = true
        }
    }
}
