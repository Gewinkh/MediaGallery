pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  TextSurface.qml - editierbarer Plain-Text-Editor (ersetzt TextViewer(QWidget)).
//  Inhalt via Viewer.readTextFile, Speichern via Viewer.writeTextFile (atomar).
//
//  • Editierbar (kein read-only mehr); Strg+S oder Speichern-Button schreibt.
//  • Ungespeicherte Aenderungen werden mit "•" markiert; beim Verlassen
//    (release) automatisch gespeichert, damit keine Eingaben verloren gehen.
//  • topInset/bottomInset werden vom FullscreenViewer reserviert -> die globale
//    Leiste (Dateiname) ueberdeckt den Inhalt NICHT mehr.
//  • Weiches, web-aehnliches Mausrad-Scrollen (animiert, groesserer Schritt).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0

    property string currentPath: ""
    property bool   dirty: false
    property bool   _loading: false
    //  Datei groesser als der Lesedeckel des ViewerControllers? Dann liegt nur
    //  ihr Anfang im Editor, und Schreiben wuerde den Rest loeschen. Der Editor
    //  geht deshalb auf nur lesen; die C++-Seite sperrt zusaetzlich (letzte
    //  Instanz, s. ViewerController::writeTextFile).
    property bool   _readOnly: false
    //  Läuft gerade ein PDF-Export? (Der Knopf bleibt so lange stumm - der
    //  Export selbst liegt im Worker, die Oberfläche bleibt bedienbar.)
    property bool   _pdfBusy: false

    //  Schriftfarbe des PDF-Exports. Der Zähler treibt die Neuauswertung: die
    //  Farbe kommt aus einer Invokable (Ordner-Sidecar), also gibt es kein
    //  Signal, an dem eine Bindung allein hängen könnte. `App.textPdfColor` steht
    //  bewusst MIT in der Bindung - ändert sich die globale Vorgabe, zieht eine
    //  Datei ohne eigene Farbe sofort nach.
    property int    _inkRev: 0
    readonly property color _pdfInk: {
        //  Die globale Vorgabe wird IMMER gelesen, auch wenn sie gleich verworfen
        //  wird: eine QML-Bindung hängt nur an dem, was sie tatsächlich anfasst -
        //  stünde sie im else-Zweig, bliebe eine Datei ohne eigene Farbe beim
        //  Ändern der Vorgabe auf dem alten Wert stehen.
        //  Über den PFAD ans Modell: die Ausnahme je Datei liegt im Sidecar des
        //  Ordners, dem die Datei gehört. Kennt das Modell sie nicht (oder hat
        //  sie keine eigene Wahl), gilt die globale Vorgabe.
        var vorgabe = App.textPdfColor
        if (!(root._inkRev, root.currentPath.length > 0)) return vorgabe
        var own = mediaModel.fileTextPdfColor(root.currentPath)
        return (own && own.a > 0) ? own : vorgabe
    }
    readonly property bool _pdfInkOwn:
        (root._inkRev, root.currentPath.length > 0)
        && mediaModel.hasFileTextPdfColor(root.currentPath)

    // HTML-Quelltext wird weiterhin gesondert behandelt (s. _wrap).
    readonly property bool _isHtml: {
        var p = root.currentPath.toLowerCase()
        return p.endsWith(".html") || p.endsWith(".htm")
    }

    //  Bricht die Anzeige um? Der Schalter steht in den Einstellungen
    //  (`Editor.softWrap`); HTML-Quelltext bleibt davon ausgenommen und läuft
    //  weiter waagerecht, damit die Verschachtelung ablesbar bleibt.
    readonly property bool _wrap: Editor.softWrap && !root._isHtml

    //  ── Suchen und Ersetzen (Strg+F) ────────────────────────────────────────
    //  Dieselbe Form wie im DOCX-Editor (`DocxSurface` ▸ findBar), damit die App
    //  einheitlich bleibt: Leiste oben rechts, Strg+F auf, Esc zu.
    property bool   _findOpen: false
    property bool   _findCase: false
    property bool   _findWords: false
    property bool   _findHighlight: true
    property string _findStatus: ""

    function _openFind() {
        //  Steht Text unter dem Cursor bereit, wird er übernommen - so muss man
        //  das gesuchte Wort nicht abtippen (Verhalten jeder IDE).
        if (editor.selectedText.length > 0 && editor.selectedText.indexOf("\n") < 0)
            findField.text = editor.selectedText
        root._findOpen = true
        //  An das FELD, nicht an den Rahmen: `FField` ist ein `Rectangle` und
        //  kennt weder `forceActiveFocus` mit Textwirkung noch `selectAll`.
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
    //  Sucht ab `von` und markiert den Treffer. Die Markierung IST die Anzeige:
    //  der angesprungene Treffer ist die Auswahl, alle übrigen sind hinterlegt.
    function _findFrom(von, rueckwaerts) {
        if (findField.text.length === 0) { root._findStatus = ""; return }
        const r = syntax.findNext(findField.text, von, root._findCase,
                                  root._findWords, rueckwaerts)
        if (!r.found) {
            root._findStatus = App.uiText(App.language, "EditorFindNoMatch")
            return
        }
        //  Liegt der Treffer in einem ZUGEKLAPPTEN Bereich, wird dieser
        //  aufgeklappt und bleibt es auch danach (Festlegung des Nutzers):
        //  man springt den Treffer an, also will man ihn sehen. Die uebrigen,
        //  nicht angesprungenen Treffer bleiben verborgen - ihre Faltmarke
        //  wird stattdessen hervorgehoben (s. `TextDecorations`).
        foldBar.ensureVisible(r.start)
        editor.select(r.start, r.start + r.length)
        root._findStatus = App.uiText(App.language, "EditorFindCounter")
                              .arg(r.index).arg(r.total)
    }
    //  Weitersuchen ab dem ENDE der aktuellen Auswahl - sonst fände man
    //  denselben Treffer wieder.
    function _findNext(rueckwaerts) {
        root._findFrom(rueckwaerts ? editor.selectionStart : editor.selectionEnd,
                       rueckwaerts)
    }
    //  Beim Tippen im Suchfeld: ab dem Anfang der aktuellen Auswahl, damit die
    //  Suche nicht wegspringt, während man den Begriff verlängert.
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

    //  Syntaxfärbung. Hängt sich an das `textDocument` der TextArea; die Sprache
    //  kommt aus der Dateiendung (Tabelle in C++). Bei unbekannter Endung färbt
    //  sie nichts und kostet nichts. Steht auf WURZELebene, weil die Statuszeile
    //  ihren Sprachnamen braucht.
    CodeHighlighter {
        id: syntax
        document: editor.textDocument
        path: root.currentPath
        //  Breite eines Tabulators in ZEICHEN. Umgerechnet wird in C++, an der
        //  Schrift des Dokuments - QMLs `FontMetrics` lag um ein Pixel daneben,
        //  und dann steht ein Tabulator nicht dort, wo vier Leerzeichen enden.
        tabWidth: Editor.tabWidth
    }

    //  ── KEINE eigene Werkzeugleiste mehr ────────────────────────────────────
    //  Sie ist ersatzlos entfallen (Festlegung des Nutzers 2026-09-03):
    //   • Speichern: passiert von selbst - beim Verlassen der Datei, beim
    //     Wechsel auf eine andere und auf dem Auto-Speichern-Takt; `Strg+S`
    //     bleibt. Die Statuszeile zeigt „geändert", solange etwas offen ist.
    //   • „-> PDF" samt Farbwahl: steht im Menü „Dokument" des Viewers.
    //   • Übersicht und Transliteration: stehen in der Leiste darüber, rechts
    //     neben „Datei hinzufügen".
    //  Damit bleibt EINE Leiste am oberen Rand statt zweier.

    //  Öffentliche Fläche für den Viewer: das Menü „Dokument" bedient den
    //  PDF-Weg von dort aus, der Text lebt aber hier.
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
    //  Gedruckt wird der STAND IM EDITOR (nicht der auf Platte) - ungespeicherte
    //  Änderungen sind also mit im PDF. Die Tabulatorweite kommt aus den
    //  Editor-Einstellungen, damit gedruckt genauso eingerückt ist wie am Schirm.
    function exportPdf() {
        if (root.currentPath.length === 0 || root._pdfBusy) return
        root._pdfBusy = true
        Viewer.exportTextToPdf(root.currentPath, editor.text,
                               root._pdfInk, Editor.tabWidth)
    }

    //  Auto-Speichern wie im DOCX-Editor: ohne Speichern-Knopf darf das Sichern
    //  nicht allein am Verlassen hängen (ein Absturz nähme sonst alles mit).
    Timer {
        interval: Math.max(5, App.autoSaveInterval) * 1000
        repeat: true
        running: App.autoSaveEnabled && root.dirty && !root._readOnly
                 && root.currentPath.length > 0
        onTriggered: root.save()
    }

    // ── Editor (editierbar, eigene Flickable für sauberes Scrollen) ────────────
    // ── Zeilennummern-Spalte (gezeichnet in C++, kein Item je Zeile) ─────────
    //  Sie liegt NEBEN dem Text, nicht darüber: sie nimmt keine Klicks an und
    //  stört das Markieren nicht. Breite bestimmt sie selbst - nur sie kennt
    //  die Schriftmaße und die höchste Nummer.
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

    //  Höhe des Textes: beim Falten weiß `paintedHeight` nichts von den
    //  verborgenen Zeilen (gemessen: Dokument 68017 px, `paintedHeight` blieb
    //  bei 85017), deshalb kommt sie bei faltbaren Dateien aus dem Dokument.
    readonly property real _textHoehe:
        foldBar.hasRegions && foldBar.documentHeight > 0
            ? foldBar.documentHeight + editor.topPadding + editor.bottomPadding
            : editor.paintedHeight

    //  Wie weit steht der Zeiger beim Ziehen ÜBER oder UNTER dem Fenster?
    //  0 = drinnen (dann rollt nichts).
    //  Gerechnet wird über die SZENEN-Position: `point.position` steht in den
    //  Koordinaten des Textfeldes, und das wandert beim Rollen unter dem
    //  stillstehenden Zeiger weg - der Abstand wäre nach dem ersten Schritt
    //  falsch. Die Szene bleibt liegen.
    //  Zeitpunkt des letzten Rollschrittes (s. `ziehRoller`).
    property real _ziehTick: 0

    readonly property real _ziehAbstand: {
        if (!zieher.active) return 0
        const y = flick.mapFromItem(null, zieher.point.scenePosition).y
        if (y < 0) return y
        if (y > flick.height) return y - flick.height
        return 0
    }

    //  Eine Fensterhöhe weiter - NUR die Ansicht. Die Schreibmarke bleibt, wo
    //  sie ist (Festlegung des Nutzers): man blättert, um etwas nachzusehen,
    //  nicht um die Eingabestelle zu verlieren. Beim nächsten Tastendruck holt
    //  `_cursorInsSichtfeld` die Ansicht ohnehin zur Marke zurück.
    function _blaettern(runter) {
        const schritt = flick.height * (runter ? 1 : -1)
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height,
                                              flick.contentY + schritt))
    }

    //  Die Ansicht dem Cursor nachführen - von Hand, weil `TextArea.flickable`
    //  dafür nicht mehr zuständig ist (s. dort). Sie wandert NUR, wenn der
    //  Cursor sonst aus dem Fenster liefe.
    function _cursorInsSichtfeld() {
        const r = editor.positionToRectangle(editor.cursorPosition)
        if (r.height <= 0) return
        //  Immer geklemmt: `positionToRectangle` kann bei einem gerade
        //  gefalteten oder noch nicht neu vermessenen Dokument auch Werte
        //  hinter dem Ende liefern - dann stünde das Fenster im Leeren.
        const max = Math.max(0, flick.contentHeight - flick.height)
        if (r.y < flick.contentY)
            flick.contentY = Math.min(max, Math.max(0, r.y))
        else if (r.y + r.height > flick.contentY + flick.height)
            flick.contentY = Math.min(max, r.y + r.height - flick.height)
    }

    // ── Faltungsleiste (Qt-Creator-Muster) ───────────────────────────────────
    //  Schmale Spalte RECHTS von den Nummern, in der Bloecke zu- und
    //  aufgeklappt werden. Sie erscheint nur, wenn die Datei ueberhaupt
    //  faltbare Bloecke hat (`hasRegions`, Festlegung des Nutzers) - eine
    //  `.txt` bekommt sie nie zu sehen.
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

    //  Rollt weiter, solange beim Markieren außerhalb des Fensters gezogen
    //  wird. Die Geschwindigkeit wächst mit dem Abstand zum Rand - nah dran
    //  kriecht es zeilenweise, weit draußen fliegt es, genau wie in einer IDE.
    Timer {
        id: ziehRoller
        interval: 16                       // rund ein Bild
        repeat: true
        running: zieher.active && root._ziehAbstand !== 0
        //  Gerollt wird nach VERGANGENER ZEIT, nicht je Auslösung: ein
        //  Zeitgeber feuert je nach Last unterschiedlich oft, und die
        //  Geschwindigkeit soll davon nicht abhängen (gemessen: 38 Schritte in
        //  0,60 s, Summe der Zeitschritte 0,608 s - die Rechnung geht auf).
        //  KEIN Zuruecksetzen bei `running` - die Bedingung flackert waehrend
        //  des Rollens (der Abstand wird neu bewertet), und jedes Flackern
        //  setzte die Zeitrechnung zurueck: `dt` blieb dann immer beim
        //  Ersatzwert, und das Tempo hing doch wieder an der Auslösungszahl
        //  (gemessen: dt=0.016 in JEDEM Schritt). Ein veralteter Zeitpunkt ist
        //  ungefährlich, weil `dt` ohnehin gedeckelt ist.
        onTriggered: {
            const jetzt = Date.now()
            const vorher = root._ziehTick
            const dt = vorher > 0 ? Math.min(0.05, (jetzt - vorher) / 1000) : 0.016
            root._ziehTick = jetzt

            const weg = root._ziehAbstand
            //  Nah am Rand kriecht es zeilenweise, weit draußen läuft es
            //  zügig - aber gedeckelt, sonst schießt eine kleine Handbewegung
            //  durch die halbe Datei.
            const proSekunde = Math.min(1800, 150 + Math.abs(weg) * 12)
            const tempo = proSekunde * dt * (weg < 0 ? -1 : 1)
            const ziel = Math.max(0, Math.min(flick.contentHeight - flick.height,
                                              flick.contentY + tempo))
            if (ziel === flick.contentY) return
            flick.contentY = ziel
            //  Die Auswahl mitziehen: ohne das rollte nur die Ansicht, und
            //  markiert bliebe der Stand von vorhin. Auch hier über die Szene,
            //  weil das Textfeld gerade unter dem Zeiger weggerollt ist.
            //
            //  Die Auswahl endet dabei an der letzten SICHTBAREN Zeile, nicht
            //  beim Zeiger: sonst steht der Cursor außerhalb des Fensters, die
            //  Nachführung (`_cursorInsSichtfeld`) rollt zusätzlich hinterher,
            //  und die Geschwindigkeit hängt nicht mehr an dieser Rechnung
            //  (gemessen: 1500 px/s auch dort, wo 250 gewollt waren).
            const p = editor.mapFromItem(null, zieher.point.scenePosition)
            const yInnen = Math.max(flick.contentY + 1,
                                    Math.min(flick.contentY + flick.height - 1, p.y))
            editor.moveCursorSelection(editor.positionAt(p.x, yInnen),
                                       TextEdit.SelectCharacters)
            //  Und den Stand wieder auf den berechneten Wert setzen: das
            //  Verschieben der Auswahl führt die Ansicht nach, und die Zeile
            //  am Rand ragt um ihre Höhe hinaus - jeder Schritt bekam dadurch
            //  eine Zeile geschenkt (gemessen: 1816 statt 967 px in 0,6 s).
            //  So gehört der Scrollstand allein dieser Rechnung.
            flick.contentY = ziel
        }
    }

    // ── Übersichtsspalte rechts (Kates „Minimap") ────────────────────────────
    //  Zeigt die GANZE Datei verkleinert und lässt sich darin scrollen.
    //  Auf- und zuklappbar über den Knopf in der Werkzeugleiste bzw. die
    //  Einstellung (`Editor.minimap`) - sie kostet Breite, deshalb ist sie
    //  standardmäßig aus.
    TextMinimap {
        id: minimap
        //  Griff fuer die Pruefstaende (s. `bench_shell t`).
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
        //  Das Sichtfenster ist ein AUFHELLENDER Schleier, kein Rahmen: bei
        //  drei Pixel je Zeile wäre ein Rahmen dicker als das, was er umfasst.
        viewportColor: Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.13)
        borderColor: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                             Editor.gutterText.b, 0.25)

        //  Die Spalte scrollt NICHT selbst - sie bittet darum. So bleibt der
        //  Scrollstand an einer Stelle (der `Flickable`).
        onScrollRequested: function (ziel) { flick.contentY = ziel }
    }

    Flickable {
        id: flick
        anchors {
            left: foldBar.right; right: minimap.left
            top: parent.top; topMargin: root.topInset; bottom: statusBar.top
        }
        clip: true
        //  RANDLOS (Wunsch T): kein `margins` mehr und kein eigenes Feld in der
        //  Fläche - der Text füllt die Kachel von Rand zu Rand, wie in einer IDE.
        //  Der frühere 12-px-Rand plus das gerundete Hintergrund-Rechteck war
        //  genau das „extra Feld", das weg sollte.
        //
        //  UMBRUCH: der sichtbare („weiche") Umbruch aus den Einstellungen
        //  (`Editor.softWrap`). Er ist rein optisch - die Datei bekommt davon
        //  nichts mit, echter Umbruch bleibt Enter. Aus = die Zeile läuft
        //  waagerecht weiter (wie in VS Code).
        contentWidth: root._wrap ? width : editor.paintedWidth
        //  Beim Falten weiß `paintedHeight` nichts von den verborgenen Zeilen
        //  (gemessen: Dokument 68017 px, `paintedHeight` blieb bei 85017). Sie
        //  dazu zu bewegen kostete eine volle Neuberechnung samt Sprung an den
        //  Dateianfang - deshalb kommt die Höhe bei faltbaren Dateien direkt
        //  aus dem Dokument (s. `TextFoldBar::documentHeight`).
        contentHeight: editor.height
        //  Schrumpft der Inhalt (ein zugeklappter Block, eine kürzere Datei),
        //  kann der Scrollstand HINTER dem Ende liegen - dann steht das Fenster
        //  im Leeren. `boundsBehavior` greift dort nicht: es bändigt nur das
        //  Werfen mit der Maus, nicht ein von Hand gesetztes `contentY`.
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

        //  ── Hervorhebung der aktuellen Zeile ────────────────────────────────
        //  Ein Streifen HINTER dem Text (z: -1), über die volle Inhaltsbreite.
        //  y und Höhe kommen aus `positionToRectangle` - das deckt auch eine
        //  weich umgebrochene Zeile ab, weil dort die Bildschirmzeile gemeint
        //  ist, in der der Cursor tatsächlich steht.
        //
        //  KEIN `+ topPadding` hier: `positionToRectangle` rechnet die Polsterung
        //  bereits ein (gemessen: bei `padding: 10` liefert Position 0 y = 10).
        //  Ein zweites Addieren schob den Streifen um genau diese 10 px nach
        //  unten - der gemeldete Versatz. Die NUMMERNSPALTE muss das Padding
        //  dagegen sehr wohl addieren: sie rechnet mit `blockBoundingRect`, und
        //  das beginnt bei y = 0 (ebenfalls gemessen).
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

        //  ── Einrueckungshilfen, Faltmarken, Klammernpaare ───────────────────
        //  Ein gezeichnetes Element HINTER dem Text, wie der Streifen der
        //  aktuellen Zeile. Es malt nur, was gerade im Fenster steht.
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

            //  Kräftig genug, um die Stufe zu sehen, blass genug, um beim
            //  Lesen nicht zu stören (gemessen: bei 0.22 hob sich die Linie
            //  auf dunklem Grund um drei Helligkeitsstufen ab - zu wenig).
            guideColor: Qt.rgba(Editor.gutterText.r, Editor.gutterText.g,
                                Editor.gutterText.b, 0.45)
            markerColor: Editor.gutterText
            bracketColor: Qt.rgba(Editor.text.r, Editor.text.g, Editor.text.b, 0.20)
            errorColor: Qt.rgba(0.86, 0.31, 0.31, 0.38)
            matchColor: Qt.rgba(Editor.selection.r, Editor.selection.g,
                                Editor.selection.b, 0.55)
        }

        //  ── Das Textfeld ────────────────────────────────────────────────────
        //  BEWUSST ohne `TextArea.flickable`: diese Anbindung installiert Qts
        //  „Cursor ins Bild rollen", und das feuert bei JEDER Neuberechnung des
        //  Layouts - nicht nur, wenn der Cursor sich bewegt. Beim Zuklappen
        //  eines Blocks oder beim Umlegen eines Schalters in den Einstellungen
        //  (die Faltungsleiste ändert dabei die Breite) sprang die Ansicht
        //  deshalb an die Stelle des Cursors, meist an den Dateianfang
        //  (gemessen: Scrollstand 4000 -> 17). Als gewöhnliches Kind der
        //  Flickable passiert das nicht mehr; dem Cursor folgt die Ansicht
        //  weiterhin - aber nur, wenn er sich WIRKLICH bewegt, s.
        //  `onCursorPositionChanged` und `root._cursorInsSichtfeld()`.
        TextArea {
            id: editor
            //  Griff fuer die Pruefstaende (s. `bench_shell t`).
            objectName: "textEditorArea"
            readOnly: root._readOnly
            selectByMouse: true
            //  Breite an den Viewport binden, sobald umgebrochen wird - sonst
            //  bliebe die TextArea so breit wie ihre längste Zeile.
            width: root._wrap ? flick.width : Math.max(implicitWidth, flick.width)
            //  Höhe: mindestens der Viewport (damit man auch in eine kurze
            //  Datei klicken kann), sonst die Höhe des Dokuments OHNE die
            //  zugeklappten Zeilen (s. `flick.contentHeight`).
            height: Math.max(root._textHoehe, flick.height)
            onCursorPositionChanged: root._cursorInsSichtfeld()

            //  ── Klick auf die drei Punkte klappt wieder auf ──────────────
            //  Die Punkte hinter einer zugeklappten Zeile SIND der Knopf, nicht
            //  nur eine Marke (Festlegung des Nutzers). Ein `TapHandler` nimmt
            //  den Zeiger nur passiv - das Textfeld behält seine eigene
            //  Mausbehandlung, Markieren und Cursor setzen gehen weiter.
            //  Gerechnet wird in den Koordinaten der Zusatzzeichnung, denn dort
            //  liegen die Punkte.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: function (punkt) {
                    const p = decorations.mapFromItem(editor, punkt.position)
                    const start = decorations.foldMarkerAt(p.x, p.y)
                    if (start >= 0) foldBar.toggleFold(start)
                }
            }

            //  ── Markieren über den Rand hinaus ──────────────────────────
            //  Zieht man mit gedrückter Maustaste aus dem Fenster heraus,
            //  bekommt das Textfeld keine neuen Mausereignisse mehr - die
            //  Auswahl bliebe an der letzten sichtbaren Zeile stehen. Der
            //  `PointHandler` verfolgt den Zeiger, ohne ihn zu greifen (das
            //  Textfeld behält seine eigene Mausbehandlung), und der Zeitgeber
            //  darunter rollt weiter, solange der Zeiger draußen steht.
            PointHandler {
                id: zieher
                objectName: "textZieher"
                acceptedButtons: Qt.LeftButton
                target: null
            }
            wrapMode: root._wrap ? TextEdit.Wrap : TextEdit.NoWrap
            //  Die Farben kommen aus der EDITOR-Palette, nicht aus dem App-Theme:
            //  der Texteditor hat eigene Profile (s. `Editor`-Singleton).
            color: Editor.text
            selectionColor: Editor.selection
            // Monospace für Latein (Code/HTML-Bündigkeit); arabische Glyphen fallen
            // pro Zeichen auf Naskh zurück (QFont-Familienliste aus C++, da QML in
            // Qt 6.4 kein font.families kennt).
            font: App.fallbackFont("monospace", 13)
            //  Polsterung statt Rand: der Text klebt nicht an der Kante, die
            //  FLÄCHE reicht aber bis dorthin - das ist der Unterschied zwischen
            //  „randlos" und „gequetscht".
            padding: 10
            //  Kein eigenes Feld mehr (kein `radius`, kein `border`) - die
            //  Fläche IST der Editor. DURCHSICHTIG, weil die Fläche schon vom
            //  Rechteck der Wurzel gemalt wird: ein deckender Hintergrund hier
            //  läge ÜBER den Einrückungshilfen und dem Streifen der aktuellen
            //  Zeile (beide `z: -1` in der Flickable) und machte sie unsichtbar.
            background: null
            // Live-Transliteration: gezieltes remove()/insert() statt text-
            // Neuzuweisung (Undo-Stack + Performance großer Dateien bleiben
            // intakt); der Guard verhindert Re-Entranz durch die eigene Edition.
            //  ↓ in der LETZTEN (sichtbaren) Zeile springt ans Zeilenende,
            //  statt wirkungslos zu bleiben - einheitlich in allen Editoren
            //  der App (Vergleich der Cursor-Zeilen-y mit dem Textende deckt
            //  auch umgebrochene Zeilen ab).
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
                //  EIN Undo-Schritt statt zwei: `applyInDocument` klammert Loeschen
                //  und Einfuegen im Dokument zusammen. Der fruehere Weg
                //  (`remove()` + `insert()`) hinterliess je Tastendruck ZWEI
                //  Schritte, sodass Strg+Z durch halb umgesetzte Zwischenstaende
                //  lief („سَلam").
                editor._trGuard = true
                const r = Translit.applyInDocument(editor.textDocument,
                                                   editor.cursorPosition)
                if (r.changed)
                    editor.cursorPosition = r.cursor
                editor._trGuard = false
            }

            //  ── Strg+Z / Strg+Y: die Transliteration muss dabei STILLHALTEN ──
            //  Ein Undo stellt den lateinischen Stand wieder her, `onTextChanged`
            //  feuert, und die Transliteration schriebe ihn sofort wieder um - was
            //  selbst ein Undo-Schritt ist. Strg+Z pendelte dadurch endlos zwischen
            //  zwei Staenden (Nutzerbefund 2026-09-02, arabische Eingabe; belegt in
            //  `tests/bench/bench_translitundo.cpp`). Der Guard, der ohnehin gegen
            //  Re-Entranz schuetzt, deckt das mit ab.
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
                //  ── Bild auf / Bild ab ──────────────────────────────────
                //  Qt rechnet eine „Seite" aus der HÖHE DES FELDES - und die
                //  ist hier die des ganzen Dokuments, seit das Feld nicht mehr
                //  an der Flickable hängt (s. dort). Eine Seite wäre damit die
                //  ganze Datei, und die Tasten taten sichtbar nichts.
                //  Geblättert wird deshalb selbst, um genau EINE Fensterhöhe.
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
                //  Undo/Redo selbst ausfuehren statt der TextArea zu ueberlassen -
                //  nur so laesst sich die Transliteration dabei stilllegen.
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

            //  ── Tabulatortaste ──────────────────────────────────────────────
            //  Mit `Editor.tabSpaces` (Vorgabe AN, wie Kate) schreibt sie
            //  LEERZEICHEN bis zum nächsten Halt statt eines `\t`. Das ist der
            //  Unterschied, der beim Vergleich zweier Editoren auffällt: Kate
            //  legt gar keinen Tabulator in die Datei, MediaGallery tat es -
            //  dieselbe Taste, verschiedener Dateiinhalt (Nutzerbefund
            //  2026-09-02, `tests/Test.txt`).
            //  Aufgefüllt wird bis zum NÄCHSTEN Halt, nicht um eine feste Zahl:
            //  in Spalte 2 sind das bei Breite 4 genau zwei Leerzeichen, nicht
            //  vier - sonst stünden die Spalten nicht untereinander.
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

    //  Hervorheben aller Treffer kostet ein Neufärben des ganzen Dokuments -
    //  bei 20 000 Zeilen rund 100 ms. Beim Tippen im Suchfeld wird deshalb
    //  entprellt: das SUCHEN läuft sofort, nur das Hinterlegen wartet kurz.
    Timer {
        id: highlightTimer
        interval: 180
        onTriggered: {
            syntax.highlightMatches(
                root._findHighlight ? findField.text : "", root._findCase)
            //  Damit eine zugeklappte Stelle mit Treffer erkennbar wird.
            decorations.setSearchTerm(findField.text, root._findCase)
        }
    }

    // ── Suchleiste (Strg+F) ──────────────────────────────────────────────────
    //  Aufbau wie im DOCX-Editor: Overlay oben rechts, damit sie nichts
    //  wegschiebt und der Text an derselben Stelle stehen bleibt.
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

        //  Gemeinsamer Feld-Baustein. `text` MUSS als Alias herausstehen -
        //  ohne das wäre `findField.text` undefined und die ganze Leiste
        //  funktionslos (dieselbe Falle wie im DOCX-Editor).
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

        //  Kleiner Knopf. `an` schaltet die Akzentfarbe für die drei Umschalter.
        //  ALLES über die eigene `id`, nie über `parent`: an der WURZEL einer
        //  Inline-Komponente zeigt `parent` auf das umgebende Element (hier den
        //  RowLayout), und in einem `TapHandler` sogar auf die Wurzel der
        //  ganzen Datei. Beide Fallen stehen in `Structure.md` ▸ Workarounds;
        //  ohne die id meldete Qt „Unable to assign [undefined] to QString"
        //  und kein Knopf trug eine Beschriftung.
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
            //  Symbole werden GEZEICHNET (Regel 28) - `▴ ▾ ✕ ≡` als Unicode
            //  sähen auf jedem System anders aus und folgten nicht dem Theme.
            //  Reine BUCHSTABEN sind erlaubt und hier auch richtig: „Aa" für
            //  Groß-/Kleinschreibung und „W" für ganze Wörter sagen mehr als
            //  jede erfundene Form.
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

            //  Zeile 1: Suchfeld + Navigation + Schalter.
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

            //  Zeile 2: Ersetzen. Ausgegraut, solange die Datei nur lesbar ist -
            //  dort wäre jedes Ersetzen sinnlos.
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

    // ── Statuszeile (Kate-Vorbild) ───────────────────────────────────────────
    //  Zeile/Spalte, Sprache, Kodierung, Zeichenzahl - und der ausführliche
    //  Platz für „nur lesen": in der Werkzeugleiste steht nur die Marke, hier
    //  der Grund. Sie sitzt ÜBER dem reservierten `bottomInset`, damit der
    //  Surface-Vertrag mit dem FullscreenViewer gilt.
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

            //  Inline-Komponente statt fünfmal derselbe Text-Block.
            component Feld: Text {
                color: Editor.gutterText
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            Feld {
                //  Zeile und Spalte rechnet die SPALTE mit - dieselbe
                //  Blockrechnung liefert sie ohnehin. In QML müsste man dafür
                //  den ganzen Text vor dem Cursor durchzählen.
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

        //  „Geändert" - der einzige Ort, an dem ungespeicherte Arbeit noch
        //  sichtbar ist, seit der Speichern-Knopf weg ist. Gespeichert wird von
        //  selbst (s. oben), aber sehen können muss man es.
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

    // ── Rückmeldung des PDF-Exports (Muster wie PdfSurface: kurzer Toast, kein
    //    Dialog - der Export ist eine Nebentätigkeit und soll nicht bestätigt
    //    werden müssen). Der Pfad wird auf den Dateinamen gekürzt.
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

    // Weiches, web-aehnliches Mausrad-Scrollen - als Geschwister der Flickable,
    // damit der Fänger NICHT mit dem Inhalt mitscrollt. NoButton -> Klicks/Markieren
    // erreichen den Editor.
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
