pragma ComponentBehavior: Bound
import QtQuick
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  PdfEditBox.qml — EINE Overlay-Textbox des PDF-Editors (Delegate im
//  Boxen-Repeater jeder Seite von PdfSurface).
//
//  KOORDINATEN: Das Modell führt Position/Größe in PDF-PUNKTEN (Ursprung
//  oben-links); dieses Item rechnet über `pageScale` (Pixel je Punkt) in
//  Bildschirm-Pixel um. Zoom/Resize der Seite skaliert die Box damit
//  automatisch korrekt mit — WYSIWYG zum Export (72-dpi-Schreiber).
//
//  SICHTBARKEIT: Boxen sind in BEIDEN Modi sichtbar (Teil des Dokuments-
//  Eindrucks); Rahmen, Handles und Maus-Interaktion existieren nur im
//  Edit-Modus.
//
//  TEXT-SYNC (bewusst KEINE text-Bindung — Zweiweg-Konflikt beim Tippen):
//   • Modell → Anzeige: imperativ (Component.onCompleted + onBoxTextChanged),
//     aber NUR solange nicht editiert wird (Undo/Redo/Sidecar-Load).
//   • Anzeige → Modell: während der Bearbeitung fließt jede Änderung live über
//     box.ctl.updateText() (Session; genau EIN Undo-Kommando am Ende).
//  Externe Aktionen (Undo/Redo/Export/Löschen/Moduswechsel) schließen eine
//  offene Bearbeitung DETERMINISTISCH über surface.editCommitRev ab.
//
//  GESTEN (Word-artig):
//   • Klick        → auswählen (schwebende Format-Toolbar erscheint)
//   • Ziehen       → verschieben (optionaler Zeilenfang über surface.snapYPt);
//                    über den Seitenrand hinaus → Notiz wechselt beim
//                    Loslassen auf die Nachbarseite (surface.resolveCrossPage)
//   • Doppelklick  → Textbearbeitung (Cursor, Auswahl, Tippen)
//   • 8 Handles    → skalieren (Ecken + Kantenmitten), min. Größe aus PdfEdit
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: box

    // ── Modellrollen (PdfEditModel) ───────────────────────────────────────────
    required property int    boxId
    required property int    page
    required property real   xPt
    required property real   yPt
    required property real   wPt
    required property real   hPt
    required property string boxText
    required property string fontFamily
    required property real   fontSizePt
    required property bool   bold
    required property bool   italic
    required property bool   underline
    required property color  textColor
    required property color  highlightColor
    required property int    alignment
    required property int    vAlign
    required property bool   anchored

    // ── Vom Seiten-Delegate gesetzt ───────────────────────────────────────────
    property int  pageIndex: -1          // Seite, auf der dieses Delegate lebt
    property real pageScale: 1.0         // Pixel je PDF-Punkt (Zoom-abhängig)
    property real pageWPt: 612           // Seitengröße in Punkten (Klemm-Grenzen)
    property real pageHPt: 792
    property var  surface: null          // PdfSurface-Root (Commit, Snapping, AutoEdit)
    // Dezentraler PDF-Editor-Controller DIESER Kachel (von PdfSurface via
    // surface.editCtl gesetzt) — ersetzt den früheren globalen PdfEdit-Singleton.
    readonly property PdfEditController ctl: surface ? surface.editCtl : null

    readonly property bool onThisPage: page === pageIndex
    readonly property bool editMode: box.ctl.editMode
    readonly property bool selected: editMode && box.ctl.selectedId === boxId
    property bool editing: false         // Inline-Textbearbeitung aktiv

    // Sichtbar auf der eigenen Seite UND solange die Notizen nicht über den
    // Alt+Q-/◉-Toggle (surface.notesVisible) ausgeblendet sind. Der Toggle
    // wirkt bewusst in BEIDEN Modi — vorher übersteuerte „|| editMode" die
    // Ausblendung im Editmodus komplett (Toggle wirkungslos). Der Eintritt in
    // den Editmodus setzt notesVisible ohnehin auf true (PdfSurface).
    visible: onThisPage && (surface ? surface.notesVisible : true)
    x: xPt * pageScale
    y: yPt * pageScale
    width:  Math.max(2, wPt * pageScale)
    height: Math.max(2, hPt * pageScale)
    z: selected ? 3 : 2

    // Externe Commit-Anforderung: Zähler-Bump im Surface → alle Boxen schließen
    // eine evtl. offene Textbearbeitung ab, BEVOR die auslösende Aktion läuft.
    readonly property int commitRev: surface ? surface.editCommitRev : 0
    onCommitRevChanged: finishEditing()

    onEditModeChanged: if (!editMode) finishEditing()
    onSelectedChanged: if (!selected) finishEditing()

    // Frisch erstellte Box (Klick auf die Seite) direkt in die Textbearbeitung
    // schicken — callLater, damit Fokus erst NACH der Instanziierung greift.
    Component.onCompleted: {
        if (surface && surface._autoEditId === boxId) {
            surface._autoEditId = -1
            Qt.callLater(startEditing)
        }
    }

    function startEditing() {
        if (!box.editMode || box.editing)
            return
        box.ctl.selectedId = box.boxId
        box.ctl.beginTextEdit(box.boxId)       // Session: Alt-Text als Undo-Basis
        box.editing = true
        edit.forceActiveFocus()
        edit.cursorPosition = edit.length
    }
    function finishEditing() {
        if (!box.editing)
            return
        box.editing = false
        box.ctl.endTextEdit(box.boxId)         // erzeugt genau EIN Text-Kommando
        if (edit.activeFocus)
            edit.focus = false
    }

    // ── Post-it-Optik: Schatten → Papier → Eselsohr ───────────────────────────
    //  Alles hängt am Papier (highlightColor.a > 0; „Keine"/Deckkraft 0 = reiner
    //  Text ohne Zettel). Geometrie und Farben sind mit dem Export identisch:
    //  Schattenversatz/Eselsohr-Größe kommen aus PdfEdit-Konstanten (PDF-Punkte
    //  × pageScale), Schatten-Alpha 52/255, Flap = Papier ×1.18 dunkler,
    //  Faltlinie ×1.5 dunkler mit 0.7-pt-Strich → WYSIWYG.
    readonly property bool hasPaper: highlightColor.a > 0

    Rectangle {                                     // weicher Schattenwurf
        x: box.ctl.noteShadowDxPt * box.pageScale
        y: box.ctl.noteShadowDyPt * box.pageScale
        width: parent.width
        height: parent.height
        color: Qt.rgba(0, 0, 0, 52 / 255)
        visible: box.hasPaper
    }
    Rectangle {                                     // Papierfläche
        anchors.fill: parent
        color: box.highlightColor
        visible: box.hasPaper
    }
    Canvas {                                        // Eselsohr unten rechts
        id: fold
        readonly property real f: Math.min(box.ctl.noteFoldPt * box.pageScale,
                                           Math.min(box.width, box.height) / 3)
        readonly property color paper: box.highlightColor
        anchors { right: parent.right; bottom: parent.bottom }
        width: Math.max(1, f)
        height: Math.max(1, f)
        visible: box.hasPaper && f > 2
        onPaperChanged: requestPaint()
        onFChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            // Umgeklappter Zipfel: abgedunkeltes Dreieck über der Ecke …
            const flap = Qt.darker(paper, 1.18)
            ctx.fillStyle = Qt.rgba(flap.r, flap.g, flap.b, paper.a)
            ctx.beginPath()
            ctx.moveTo(0, height)
            ctx.lineTo(width, 0)
            ctx.lineTo(width, height)
            ctx.closePath()
            ctx.fill()
            // … plus feine Faltlinie entlang der Hypotenuse.
            const ln = Qt.darker(paper, 1.5)
            ctx.strokeStyle = Qt.rgba(ln.r, ln.g, ln.b, paper.a)
            ctx.lineWidth = Math.max(1, 0.7 * box.pageScale)
            ctx.beginPath()
            ctx.moveTo(0, height)
            ctx.lineTo(width, 0)
            ctx.stroke()
        }
    }

    // ── Rahmen (nur Edit-Modus; Akzent bei Auswahl) ───────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        visible: box.editMode
        border.color: box.selected ? App.themeAccent
                                   : Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                             App.themeAccent.b, 0.45)
        border.width: box.selected ? 2 : 1
    }

    // ── Text (Anzeige + Inline-Bearbeitung) ───────────────────────────────────
    TextEdit {
        id: edit
        anchors.fill: parent
        padding: box.ctl.boxPaddingPt * box.pageScale   // gleiche Konstante wie Export
        clip: false                        // Überlauf sichtbar — wie im Export
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        enabled: box.editing
        activeFocusOnPress: false          // Fokus vergibt AUSSCHLIESSLICH startEditing()
        selectByMouse: true
        persistentSelection: false
        // Gewählte Familie führt (Latein); arabische Glyphen fallen auf Naskh
        // zurück. QFont-Familienliste aus C++ (QML kennt in Qt 6.4 kein
        // font.families) — Größe/Stil werden mit übergeben und ersetzen die
        // einzelnen font.*-Bindings.
        font: App.fallbackFont(box.fontFamily,
                               Math.max(1, box.fontSizePt * box.pageScale),
                               box.bold, box.italic, box.underline)
        color: box.textColor
        horizontalAlignment: box.alignment === 1 ? TextEdit.AlignHCenter
                           : box.alignment === 2 ? TextEdit.AlignRight
                                                 : TextEdit.AlignLeft
        // Vertikal je Box-Einstellung: 0 = OBEN-links wie ein Word-Textfeld
        // (Standard), 1 = zentriert. Der Export rechnet denselben Offset —
        // bei zentriertem Überlauf beidseitig symmetrisch (clip:false).
        verticalAlignment: box.vAlign === 1 ? TextEdit.AlignVCenter
                                            : TextEdit.AlignTop

        Component.onCompleted: text = box.boxText
        // Live-Transliteration (falls aktiv): gezieltes remove()/insert() VOR
        // updateText, damit der finale Zielschrift-Text ins Modell wandert.
        // Der Guard verhindert Re-Entranz durch die eigene Edition; das
        // erneute onTextChanged nach der Umsetzung läuft mit Endstand weiter.
        property bool _trGuard: false
        function _applyTranslit() {
            if (edit._trGuard || !box.editing || !Translit.enabled)
                return
            const r = Translit.liveApply(edit.text, edit.cursorPosition)
            if (!r.changed)
                return
            edit._trGuard = true
            edit.remove(r.start, r.end)
            edit.insert(r.start, r.replacement)
            edit.cursorPosition = r.cursor
            edit._trGuard = false
        }
        onTextChanged: {
            if (box.editing) {
                edit._applyTranslit()
                box.ctl.updateText(box.boxId, text)
            }
        }
        onActiveFocusChanged: if (!activeFocus) box.finishEditing()
        Keys.onEscapePressed: (e) => { box.finishEditing(); e.accepted = true }
    }
    // Modell → Anzeige (Undo/Redo/Sidecar), nur außerhalb einer Bearbeitung.
    onBoxTextChanged: if (!editing && edit.text !== boxText) edit.text = boxText

    // ── Platzhalter in leerer Box (fester Grauton: lesbar auf weißer Seite) ───
    Text {
        anchors.fill: parent
        anchors.margins: box.ctl.boxPaddingPt * box.pageScale
        visible: box.editMode && !box.editing && box.boxText.length === 0
        text: App.uiText(App.language, "PdfEditEmptyHint")
        color: "#9aa0a6"
        font.pixelSize: Math.max(8, box.fontSizePt * box.pageScale * 0.85)
        font.italic: true
        elide: Text.ElideRight
        verticalAlignment: box.vAlign === 1 ? Text.AlignVCenter : Text.AlignTop
    }

    // ── Verschieben + Auswahl + Doppelklick (nur außerhalb der Bearbeitung) ───
    //  Live-Updates laufen über den Controller (Modell bleibt einzige Wahrheits-
    //  quelle; die x/y-Bindungen oben folgen automatisch). Der Cursor-Punkt wird
    //  je Move in ELTERN-Koordinaten aufgelöst — robust, obwohl sich die Box
    //  (und damit diese MouseArea) unter dem Zeiger mitbewegt.
    MouseArea {
        id: moveArea
        anchors.fill: parent
        enabled: box.editMode && !box.editing
        visible: enabled
        acceptedButtons: Qt.LeftButton
        cursorShape: box.selected ? Qt.SizeAllCursor : Qt.PointingHandCursor
        preventStealing: true              // ListView darf den Drag nicht klauen
        property real grabDx: 0
        property real grabDy: 0
        property bool moved: false
        onPressed: (m) => {
            if (box.surface) {
                box.surface.commitEditing()                // fremde Bearbeitung schließen
                box.surface.editDragPage = box.pageIndex   // Delegate über Nachbarn heben
            }
            box.ctl.selectedId = box.boxId
            const p = mapToItem(box.parent, m.x, m.y)
            grabDx = p.x - box.x
            grabDy = p.y - box.y
            moved = false
            box.ctl.beginGeometryEdit(box.boxId)
        }
        onPositionChanged: (m) => {
            const p = mapToItem(box.parent, m.x, m.y)
            let nx = (p.x - grabDx) / box.pageScale
            let ny = (p.y - grabDy) / box.pageScale
            // Kleine Zitter-Schwelle: reiner Klick bleibt reiner Klick.
            if (!moved && Math.abs(nx - box.xPt) + Math.abs(ny - box.yPt) < 1.5)
                return
            moved = true
            // Zeilenfang (falls aktiv): Oberkante an erkannte PDF-Textzeile.
            if (box.surface)
                ny = box.surface.snapYPt(box.pageIndex, ny, box.hPt)
            nx = Math.max(0, Math.min(nx, Math.max(0, box.pageWPt - box.wPt)))
            // y bewusst NICHT klemmen: über den unteren/oberen Seitenrand
            // hinausgezogene Notizen wandern beim Loslassen auf die
            // Nachbarseite (seitenübergreifendes Verschieben).
            box.ctl.updatePlacement(box.boxId, box.page, nx, ny, box.wPt, box.hPt)
        }
        onReleased: {
            // Über den Seitenrand gezogen? → Zielseite + lokale y-Koordinate
            // auflösen (PdfSurface kennt Seitenmaße + Lückenbreite) und die
            // Session mit der finalen Platzierung abschließen (EIN Undo-Schritt
            // inkl. Seitenwechsel).
            if (moved && box.surface) {
                const t = box.surface.resolveCrossPage(box.page, box.yPt,
                                                       box.hPt, box.pageScale)
                box.ctl.updatePlacement(box.boxId, t.page, box.xPt, t.y,
                                        box.wPt, box.hPt)
            }
            if (box.surface) box.surface.editDragPage = -1
            box.ctl.endGeometryEdit(box.boxId)     // EIN Undo-Kommando
        }
        onDoubleClicked: box.startEditing()
    }

    // ── Größen-Handles: 4 Ecken + 4 Kantenmitten (nur bei Auswahl) ────────────
    //  hx/hy ∈ {-1,0,1} beschreiben die Handle-Lage; daraus folgen Position,
    //  Cursorform und welche Kanten das Ziehen bewegt. Mindestgrößen kommen aus
    //  PdfEdit (dieselben Konstanten wie der Controller), Klemmen an die Seite.
    Repeater {
        model: box.selected && !box.editing
               ? [ { hx: -1, hy: -1 }, { hx: 0, hy: -1 }, { hx: 1, hy: -1 },
                   { hx: -1, hy:  0 },                    { hx: 1, hy:  0 },
                   { hx: -1, hy:  1 }, { hx: 0, hy:  1 }, { hx: 1, hy:  1 } ]
               : []
        delegate: Rectangle {
            id: handle
            required property var modelData
            readonly property int hx: modelData.hx
            readonly property int hy: modelData.hy
            width: 9; height: 9; radius: 2
            color: App.themeAccent
            border.color: "#ffffff"; border.width: 1
            x: (hx < 0 ? 0 : hx > 0 ? box.width  : box.width  / 2) - width / 2
            y: (hy < 0 ? 0 : hy > 0 ? box.height : box.height / 2) - height / 2
            z: 4

            MouseArea {
                anchors.fill: parent
                anchors.margins: -4        // größere Greiffläche als das 9-px-Quadrat
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                cursorShape: {
                    if (handle.hx !== 0 && handle.hy !== 0)
                        return (handle.hx === handle.hy) ? Qt.SizeFDiagCursor
                                                         : Qt.SizeBDiagCursor
                    return handle.hx !== 0 ? Qt.SizeHorCursor : Qt.SizeVerCursor
                }
                property rect startRect
                property real pressX: 0
                property real pressY: 0
                onPressed: (m) => {
                    if (box.surface) box.surface.commitEditing()
                    box.ctl.selectedId = box.boxId
                    const p = mapToItem(box.parent, m.x, m.y)
                    pressX = p.x
                    pressY = p.y
                    startRect = Qt.rect(box.xPt, box.yPt, box.wPt, box.hPt)
                    box.ctl.beginGeometryEdit(box.boxId)
                }
                onPositionChanged: (m) => {
                    const p = mapToItem(box.parent, m.x, m.y)
                    const dxPt = (p.x - pressX) / box.pageScale
                    const dyPt = (p.y - pressY) / box.pageScale
                    let x = startRect.x, y = startRect.y
                    let w = startRect.width, h = startRect.height
                    if (handle.hx < 0) { x = startRect.x + dxPt; w = startRect.width  - dxPt }
                    if (handle.hx > 0) {                          w = startRect.width  + dxPt }
                    if (handle.hy < 0) { y = startRect.y + dyPt; h = startRect.height - dyPt }
                    if (handle.hy > 0) {                          h = startRect.height + dyPt }
                    // Mindestgröße halten, OHNE die gegenüberliegende Kante zu bewegen.
                    const minW = box.ctl.minBoxWPt
                    const minH = box.ctl.minBoxHPt
                    if (w < minW) { if (handle.hx < 0) x = startRect.x + startRect.width  - minW; w = minW }
                    if (h < minH) { if (handle.hy < 0) y = startRect.y + startRect.height - minH; h = minH }
                    // In die Seite klemmen.
                    if (x < 0) { w += x; x = 0 }
                    if (y < 0) { h += y; y = 0 }
                    if (x + w > box.pageWPt) w = box.pageWPt - x
                    if (y + h > box.pageHPt) h = box.pageHPt - y
                    box.ctl.updateGeometry(box.boxId, x, y,
                                           Math.max(minW, w), Math.max(minH, h))
                }
                onReleased: box.ctl.endGeometryEdit(box.boxId)
            }
        }
    }
}
