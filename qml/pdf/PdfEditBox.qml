pragma ComponentBehavior: Bound
import QtQuick
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  PdfEditBox.qml - EINE Overlay-Annotation des PDF-Editors (Delegate im
//  Boxen-Repeater jeder Seite von PdfSurface). Deckt alle sechs Arten ab:
//  Text-Notiz (Post-it), Freihand, Pfeil, Rechteck, Ellipse - analog zum
//  Bild-Editor (ImageEditBox), nur in PDF-Punkten statt Bild-Pixeln - sowie
//  „Text ersetzen" (PDF-exklusiv): deckende, fix weiße Fläche + Textbox als
//  EIN Objekt, ohne Post-it-Optik (kein Schatten, kein Eselsohr).
//
//  KOORDINATEN: Das Modell führt Position/Größe in PDF-PUNKTEN (Ursprung
//  oben-links); dieses Item rechnet über `pageScale` (Pixel je Punkt) in
//  Bildschirm-Pixel um. Zoom/Resize der Seite skaliert die Box damit
//  automatisch korrekt mit - WYSIWYG zum Export (72-dpi-Schreiber).
//
//  SICHTBARKEIT: Annotationen sind in BEIDEN Modi sichtbar (Teil des
//  Dokument-Eindrucks); Rahmen, Handles und Maus-Interaktion existieren nur
//  im Edit-Modus MIT dem Auswahl-Werkzeug (ctl.tool === Select) - während
//  des Zeichnens fängt die drawArea der Seite alle Gesten.
//
//  TEXT-SYNC (bewusst KEINE text-Bindung - Zweiweg-Konflikt beim Tippen):
//   • Modell -> Anzeige: imperativ (Component.onCompleted + onBoxTextChanged),
//     aber NUR solange nicht editiert wird (Undo/Redo/Sidecar-Load).
//   • Anzeige -> Modell: während der Bearbeitung fließt jede Änderung live über
//     box.ctl.updateText() (Session; genau EIN Undo-Kommando am Ende).
//  Externe Aktionen (Undo/Redo/Export/Löschen/Moduswechsel) schließen eine
//  offene Bearbeitung DETERMINISTISCH über surface.editCommitRev ab.
//
//  GESTEN (Word-artig, Auswahl-Werkzeug):
//   • Klick        -> auswählen (schwebende Format-Toolbar erscheint)
//   • Ziehen       -> verschieben (Text: optionaler Zeilenfang über
//                    surface.snapYPt); über den Seitenrand hinaus -> wechselt
//                    beim Loslassen auf die Nachbarseite (resolveCrossPage) -
//                    Strich-Punkte wandern über die Controller-Session mit
//   • Doppelklick  -> Textbearbeitung (nur Text-Notizen)
//   • 8 Handles    -> skalieren (Ecken + Kantenmitten; Striche transformieren
//                    ihre Punkte proportional - Controller-Session)
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: box

    // ── Modellrollen (PdfEditModel) ───────────────────────────────────────────
    required property int    boxId
    //  Nachverfolgte Änderung: 0 keine, 1 neu, 2 gelöscht (PdfEditModel).
    required property int    trackState
    required property int    page
    required property int    boxKind          // 0 Text,1 Freihand,2 Pfeil,3 Rect,4 Ellipse
    required property real   xPt
    required property real   yPt
    required property real   wPt
    required property real   hPt
    required property var    boxPoints        // Freihand/Pfeil: Liste von QPointF (PDF-Punkte)
    required property color  strokeColor
    required property real   lineWidth
    required property color  fillColor
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
    //  Nur Stempel: Pfad der Bilddatei.
    required property string imagePath
    //  Nur Markierungen: 0 Fläche, 1 unterstrichen, 2 durchgestrichen.
    required property int    markupStyle

    // ── Vom Seiten-Delegate gesetzt ───────────────────────────────────────────
    //  pageIndex = STABILER Seiten-Key (PdfEdit.viewPageKey): darüber findet die
    //  Notiz ihre Seite, auch nachdem Seiten umsortiert/eingefügt wurden.
    //  viewIndex = laufende Nummer derselben Seite in der Seitenliste; alles
    //  Geometrische (Nachbarseiten, Zeilenfang, Zielseite beim Ziehen) rechnet
    //  damit, weil nur die Ansicht Seitenmaße und Lücken kennt.
    property int  pageIndex: -1
    property int  viewIndex: -1
    property real pageScale: 1.0         // Pixel je PDF-Punkt (Zoom-abhängig)
    property real pageWPt: 612           // Seitengröße in Punkten (Klemm-Grenzen)
    property real pageHPt: 792
    property var  surface: null          // PdfSurface-Root (Commit, Snapping, AutoEdit)
    // Dezentraler PDF-Editor-Controller DIESER Kachel (von PdfSurface via
    // surface.editCtl gesetzt) - ersetzt den früheren globalen PdfEdit-Singleton.
    readonly property PdfEditController ctl: surface ? surface.editCtl : null

    readonly property bool onThisPage: page === pageIndex
    readonly property bool isText:    boxKind === 0
    // „Text ersetzen" (kind 5): weiße Deckfläche + Textbox als EIN Objekt -
    // volle Text-Pipeline, aber OHNE Post-it-Optik (kein Schatten/Eselsohr).
    readonly property bool isReplace: boxKind === 5
    // Textführende Arten (Notiz + Text ersetzen): TextEdit, Platzhalter,
    // Zeilenfang und Text-Sync teilen sich denselben Pfad.
    readonly property bool isTextual: isText || isReplace
    readonly property bool isStroke: boxKind === 1 || boxKind === 2
    //  Textmarkierung (Markieren/Unterstreichen/Durchstreichen): EIN Objekt mit
    //  MEHREREN Bereichen (paarweise in boxPoints). Sie hängt am Text darunter
    //  - deshalb bewusst NICHT verschieb- oder skalierbar: Auswählen, umfärben
    //  und löschen ja, herumziehen nein (so verhält sie sich in jedem
    //  PDF-Betrachter).
    readonly property bool isMarkup: boxKind === 6
    //  „Text schwärzen": deckende Fläche, kein eigener Text. Der darunter
    //  liegende Text verschwindet erst beim Export aus dem Content-Stream.
    readonly property bool isRedact: boxKind === 7
    //  Signatur/Stempel: eine Bilddatei im Dokument.
    readonly property bool isStamp:  boxKind === 8
    readonly property bool editMode: box.ctl.editMode
    //  Offene Änderung? Eine als GELÖSCHT markierte Notiz bleibt sichtbar,
    //  aber blass und durchgestrichen - sie ist noch da, bis jemand
    //  entscheidet.
    readonly property bool trackedNew: box.trackState === 1
    readonly property bool trackedDel: box.trackState === 2
    // Auswahl/Verschieben/Skalieren nur mit dem Auswahl-Werkzeug (wie im
    // Bild-Editor - während des Zeichnens fängt die drawArea der Seite).
    readonly property bool selectTool: box.ctl.tool === 0
    readonly property bool selected: editMode && box.ctl.selectedId === boxId
    // Kind-abhängige Mindestgrößen: Zeichnungen dürfen deutlich kleiner
    // werden als Textboxen (dünner Pfeil ist legitim; Werte wie Controller).
    readonly property real minWPt: isTextual ? box.ctl.minBoxWPt : 4
    readonly property real minHPt: isTextual ? box.ctl.minBoxHPt : 4
    property bool editing: false         // Inline-Textbearbeitung aktiv

    // Sichtbar auf der eigenen Seite UND solange die Notizen nicht über den
    // Alt+Q-/◉-Toggle (surface.notesVisible) ausgeblendet sind. Der Toggle
    // wirkt bewusst in BEIDEN Modi - vorher übersteuerte „|| editMode" die
    // Ausblendung im Editmodus komplett (Toggle wirkungslos). Der Eintritt in
    // den Editmodus setzt notesVisible ohnehin auf true (PdfSurface).
    visible: onThisPage && (surface ? surface.notesVisible : true)
    x: xPt * pageScale
    y: yPt * pageScale
    width:  Math.max(2, wPt * pageScale)
    height: Math.max(2, hPt * pageScale)
    z: selected ? 3 : 2

    //  ── Kennzeichnung offener Änderungen ─────────────────────────────────────
    //  Als GELÖSCHT markiert: blass - die Notiz ist noch da, aber auf dem Weg
    //  hinaus. Die Entscheidung fällt über das Track-Changes-Menü oder das
    //  Kontextmenü.
    opacity: box.trackedDel ? 0.45 : 1.0

    //  Rahmen in der Akzentfarbe zeigt „das ist eine offene Änderung"; beim
    //  Löschen zusätzlich ein Balken quer über die Fläche (wie durchgestrichener
    //  Text im DOCX-Streifen). Beides liegt ÜBER der Notiz, greift aber keine
    //  Mausereignisse ab.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        visible: box.trackedNew || box.trackedDel
        color: "transparent"
        border.width: 2
        border.color: App.themeAccent
        radius: 3
        z: 5
    }
    Rectangle {
        visible: box.trackedDel
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        color: App.themeAccent
        z: 5
    }

    // Externe Commit-Anforderung: Zähler-Bump im Surface -> alle Boxen schließen
    // eine evtl. offene Textbearbeitung ab, BEVOR die auslösende Aktion läuft.
    readonly property int commitRev: surface ? surface.editCommitRev : 0
    onCommitRevChanged: finishEditing()

    onEditModeChanged: if (!editMode) finishEditing()
    onSelectedChanged: if (!selected) finishEditing()

    // Frisch erstellte Box (Klick auf die Seite) direkt in die Textbearbeitung
    // schicken - callLater, damit Fokus erst NACH der Instanziierung greift.
    Component.onCompleted: {
        if (surface && surface._autoEditId === boxId) {
            surface._autoEditId = -1
            Qt.callLater(startEditing)
        }
    }

    function startEditing() {
        if (!box.isTextual || !box.editMode || box.editing)
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

    // ── Post-it-Optik: Schatten -> Papier -> Eselsohr ───────────────────────────
    //  Alles hängt am Papier (highlightColor.a > 0; „Keine"/Deckkraft 0 = reiner
    //  Text ohne Zettel). Geometrie und Farben sind mit dem Export identisch:
    //  Schattenversatz/Eselsohr-Größe kommen aus PdfEdit-Konstanten (PDF-Punkte
    //  × pageScale), Schatten-Alpha 52/255, Flap = Papier ×1.18 dunkler,
    //  Faltlinie ×1.5 dunkler mit 0.7-pt-Strich -> WYSIWYG.
    readonly property bool hasPaper: isText && highlightColor.a > 0

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
    Rectangle {                                     // „Text ersetzen": deckende
        anchors.fill: parent                        // weiße Fläche - bewusst
        color: box.highlightColor                   // OHNE Schatten/Eselsohr
        visible: box.isReplace                      // Cover-Farbe (Controller erzwingt Deckung)
    }
    Rectangle {                                     // „Text schwärzen": deckende
        anchors.fill: parent                        // Fläche (Standard Schwarz)
        color: box.highlightColor
        visible: box.isRedact
    }
    Image {                                         // Signatur/Stempel
        anchors.fill: parent
        visible: box.isStamp && box.imagePath.length > 0
        source: box.isStamp && box.imagePath.length > 0
                ? "file://" + box.imagePath : ""
        fillMode: Image.Stretch                     // Box FOLGT dem Seitenverhältnis
        smooth: true
        asynchronous: true
        //  Deckkraft steckt im Alpha der Füllfarbe (Panel-Regler).
        opacity: box.fillColor.a
    }
    // ── Textmarkierung: je Bereich eine Fläche bzw. eine Linie ───────────────
    //  Die Bereiche stehen in PDF-Punkten und BEZIEHEN SICH AUF DIE SEITE -
    //  hier also relativ zur Box-Ecke rechnen (box.xPt/yPt), nicht absolut.
    Repeater {
        model: box.isMarkup ? box.boxPoints.length / 2 : 0
        delegate: Rectangle {
            required property int index
            readonly property point p0: box.boxPoints[index * 2]
            readonly property point p1: box.boxPoints[index * 2 + 1]
            readonly property real qx: Math.min(p0.x, p1.x)
            readonly property real qy: Math.min(p0.y, p1.y)
            readonly property real qw: Math.abs(p1.x - p0.x)
            readonly property real qh: Math.abs(p1.y - p0.y)
            //  Strichstärke der Unter-/Durchstreichung wie im Export: 1/14 der
            //  Bereichshöhe, mindestens ein sichtbarer halber Punkt.
            readonly property real t: Math.max(0.5, qh / 14)
            x: (qx - box.xPt) * box.pageScale
            y: ((box.markupStyle === 0 ? qy
                 : box.markupStyle === 1 ? qy + qh - t
                                         : qy + qh / 2) - box.yPt) * box.pageScale
            width:  Math.max(1, qw * box.pageScale)
            height: Math.max(1, (box.markupStyle === 0 ? qh : t) * box.pageScale)
            color: box.strokeColor
        }
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

    // ══ ZEICHNUNG (Freihand / Pfeil / Rechteck / Ellipse) ═════════════════════
    //  Canvas deckt die Bounding-Box PLUS Linienbreiten-Rand ab (die äußere
    //  Stifthälfte darf nicht abgeschnitten werden - der Export zeichnet
    //  mittig). Geometrie in PDF-Punkten × pageScale - identisch zum Export
    //  (drawBox im PdfExportTask) -> WYSIWYG.
    Canvas {
        id: shape
        //  Nur die ZEICHNUNGEN (Strich/Pfeil/Rechteck/Ellipse). Textarten
        //  bringen ihre eigene Darstellung mit - und die neuen Arten
        //  Markierung/Schwärzung/Stempel ebenso; ohne diese Ausnahme malte
        //  der Formen-Canvas seine Füllung ÜBER das Stempelbild (im echten
        //  Fenster als schwarzes Rechteck aufgefallen).
        visible: !box.isTextual && !box.isMarkup && !box.isRedact && !box.isStamp
        readonly property real pad: Math.max(2, box.lineWidth * box.pageScale)
        anchors.fill: parent
        anchors.margins: -pad
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.lineCap = "round"; ctx.lineJoin = "round"
            const sc = box.pageScale
            const ox = pad, oy = pad     // Ursprung = Box-Ecke innerhalb des Canvas
            const lw = Math.max(1, box.lineWidth * sc)
            if (box.boxKind === 1) {                 // Freihand
                if (box.boxPoints && box.boxPoints.length >= 2) {
                    ctx.strokeStyle = box.strokeColor; ctx.lineWidth = lw
                    ctx.beginPath()
                    for (var i = 0; i < box.boxPoints.length; ++i) {
                        const px = ox + (box.boxPoints[i].x - box.xPt) * sc
                        const py = oy + (box.boxPoints[i].y - box.yPt) * sc
                        if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                    }
                    ctx.stroke()
                }
            } else if (box.boxKind === 2) {          // Pfeil
                if (box.boxPoints && box.boxPoints.length >= 2) {
                    const fx = ox + (box.boxPoints[0].x - box.xPt) * sc
                    const fy = oy + (box.boxPoints[0].y - box.yPt) * sc
                    const tx = ox + (box.boxPoints[1].x - box.xPt) * sc
                    const ty = oy + (box.boxPoints[1].y - box.yPt) * sc
                    ctx.strokeStyle = box.strokeColor; ctx.lineWidth = lw
                    ctx.beginPath(); ctx.moveTo(fx, fy); ctx.lineTo(tx, ty); ctx.stroke()
                    // Pfeilspitze (identische Geometrie wie der Export:
                    // Länge = max(10 pt, 4·Linienbreite), Spreizung π/7).
                    const ang = Math.atan2(ty - fy, tx - fx)
                    const len = Math.max(10 * sc, box.lineWidth * 4 * sc)
                    const spread = Math.PI / 7
                    ctx.beginPath()
                    ctx.moveTo(tx, ty)
                    ctx.lineTo(tx - len * Math.cos(ang - spread), ty - len * Math.sin(ang - spread))
                    ctx.moveTo(tx, ty)
                    ctx.lineTo(tx - len * Math.cos(ang + spread), ty - len * Math.sin(ang + spread))
                    ctx.stroke()
                }
            } else {                                 // Rechteck / Ellipse
                const rx = ox, ry = oy
                const rw = box.width, rh = box.height
                if (box.fillColor.a > 0) {
                    ctx.fillStyle = box.fillColor
                    if (box.boxKind === 4) { _ellipsePath(ctx, rx, ry, rw, rh); ctx.fill() }
                    else ctx.fillRect(rx, ry, rw, rh)
                }
                ctx.strokeStyle = box.strokeColor; ctx.lineWidth = lw
                if (box.boxKind === 4) { _ellipsePath(ctx, rx, ry, rw, rh); ctx.stroke() }
                else ctx.strokeRect(rx, ry, rw, rh)
            }
        }
        function _ellipsePath(ctx, x, y, w, h) {
            ctx.beginPath()
            const kappa = 0.5522847498
            const cx = w / 2, cy = h / 2
            const ex = w, ey = h
            ctx.moveTo(x, y + cy)
            ctx.bezierCurveTo(x, y + cy - cy * kappa, x + cx - cx * kappa, y, x + cx, y)
            ctx.bezierCurveTo(x + cx + cx * kappa, y, x + ex, y + cy - cy * kappa, x + ex, y + cy)
            ctx.bezierCurveTo(x + ex, y + cy + cy * kappa, x + cx + cx * kappa, y + ey, x + cx, y + ey)
            ctx.bezierCurveTo(x + cx - cx * kappa, y + ey, x, y + cy + cy * kappa, x, y + cy)
            ctx.closePath()
        }
        // Neu zeichnen bei jeder relevanten Änderung (Punkte/Farben/Zoom).
        Connections {
            target: box
            function onWidthChanged()          { shape.requestPaint() }
            function onHeightChanged()         { shape.requestPaint() }
            function onBoxPointsChanged()      { shape.requestPaint() }
            function onStrokeColorChanged()    { shape.requestPaint() }
            function onLineWidthChanged()      { shape.requestPaint() }
            function onFillColorChanged()      { shape.requestPaint() }
            function onPageScaleChanged()      { shape.requestPaint() }
        }
        Component.onCompleted: requestPaint()
    }

    // ── Rahmen (nur Edit-Modus mit Auswahl-Werkzeug; Akzent bei Auswahl) ──────
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        visible: box.editMode && box.selectTool
        border.color: box.selected ? App.themeAccent
                                   : Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                             App.themeAccent.b, 0.45)
        border.width: box.selected ? 2 : 1
    }

    // ── Text (Anzeige + Inline-Bearbeitung) ───────────────────────────────────
    TextEdit {
        id: edit
        visible: box.isTextual
        anchors.fill: parent
        padding: box.ctl.boxPaddingPt * box.pageScale   // gleiche Konstante wie Export
        clip: false                        // Überlauf sichtbar - wie im Export
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        enabled: box.editing
        activeFocusOnPress: false          // Fokus vergibt AUSSCHLIESSLICH startEditing()
        selectByMouse: true
        persistentSelection: false
        //  ↓ in der letzten Zeile springt ans Zeilenende (einheitlich in allen
        //  Editoren der App - 2026-07-17).
        Keys.onDownPressed: (e) => {
            const yCur = edit.positionToRectangle(edit.cursorPosition).y
            const yEnd = edit.positionToRectangle(edit.length).y
            if (Math.abs(yCur - yEnd) < 0.5 && edit.cursorPosition < edit.length) {
                if (e.modifiers & Qt.ShiftModifier)
                    edit.moveCursorSelection(edit.length)
                else
                    edit.cursorPosition = edit.length
                e.accepted = true
            } else {
                e.accepted = false
            }
        }
        // Gewählte Familie führt (Latein); arabische Glyphen fallen auf Naskh
        // zurück. QFont-Familienliste aus C++ (QML kennt in Qt 6.4 kein
        // font.families) - Größe/Stil werden mit übergeben und ersetzen die
        // einzelnen font.*-Bindings.
        font: App.fallbackFont(box.fontFamily,
                               Math.max(1, box.fontSizePt * box.pageScale),
                               box.bold, box.italic, box.underline)
        color: box.textColor
        horizontalAlignment: box.alignment === 1 ? TextEdit.AlignHCenter
                           : box.alignment === 2 ? TextEdit.AlignRight
                                                 : TextEdit.AlignLeft
        // Vertikal je Box-Einstellung: 0 = OBEN-links wie ein Word-Textfeld
        // (Standard), 1 = zentriert. Der Export rechnet denselben Offset -
        // bei zentriertem Überlauf beidseitig symmetrisch (clip:false).
        verticalAlignment: box.vAlign === 1 ? TextEdit.AlignVCenter
                                            : TextEdit.AlignTop

        Component.onCompleted: if (box.isTextual) text = box.boxText
        // Live-Transliteration (falls aktiv): gezieltes remove()/insert() VOR
        // updateText, damit der finale Zielschrift-Text ins Modell wandert.
        // Der Guard verhindert Re-Entranz durch die eigene Edition; das
        // erneute onTextChanged nach der Umsetzung läuft mit Endstand weiter.
        property bool _trGuard: false
        function _applyTranslit() {
            if (edit._trGuard || !box.editing || !Translit.enabled)
                return
            //  EIN Undo-Schritt statt zwei: `applyInDocument` klammert Loeschen
            //  und Einfuegen im Dokument zusammen. Der fruehere Weg
            //  (`remove()` + `insert()`) hinterliess je Tastendruck ZWEI
            //  Schritte, sodass Strg+Z durch halb umgesetzte Zwischenstaende
            //  lief („سَلam").
            edit._trGuard = true
            const r = Translit.applyInDocument(edit.textDocument, edit.cursorPosition)
            if (r.changed)
                edit.cursorPosition = r.cursor
            edit._trGuard = false
        }

        //  ── Strg+Z / Strg+Y: die Transliteration muss dabei STILLHALTEN ──
        //  Ein Undo stellt den lateinischen Stand wieder her, `onTextChanged`
        //  feuert, und die Transliteration schriebe ihn sofort wieder um - was
        //  selbst ein Undo-Schritt ist. Strg+Z pendelte dadurch endlos zwischen
        //  zwei Staenden (Nutzerbefund 2026-09-02, arabische Eingabe; belegt in
        //  `tests/bench/bench_translitundo.cpp`). Der Guard, der ohnehin gegen
        //  Re-Entranz schuetzt, deckt das mit ab.
        function _guardedUndo() {
            edit._trGuard = true
            edit.undo()
            edit._trGuard = false
        }
        function _guardedRedo() {
            edit._trGuard = true
            edit.redo()
            edit._trGuard = false
        }
        Keys.onPressed: function(e) {
            if (!(e.modifiers & Qt.ControlModifier))
                return
            if (e.key === Qt.Key_Z) {
                if (e.modifiers & Qt.ShiftModifier) edit._guardedRedo()
                else                                edit._guardedUndo()
                e.accepted = true
            } else if (e.key === Qt.Key_Y) {
                edit._guardedRedo(); e.accepted = true
            }
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
    // Modell -> Anzeige (Undo/Redo/Sidecar), nur außerhalb einer Bearbeitung.
    onBoxTextChanged: if (isTextual && !editing && edit.text !== boxText) edit.text = boxText

    // ── Platzhalter in leerer Box (fester Grauton: lesbar auf weißer Seite) ───
    Text {
        anchors.fill: parent
        anchors.margins: box.ctl.boxPaddingPt * box.pageScale
        visible: box.isTextual && box.editMode && !box.editing && box.boxText.length === 0
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
    //  je Move in ELTERN-Koordinaten aufgelöst - robust, obwohl sich die Box
    //  (und damit diese MouseArea) unter dem Zeiger mitbewegt.
    MouseArea {
        id: moveArea
        anchors.fill: parent
        enabled: box.editMode && box.selectTool && !box.editing
        visible: enabled
        acceptedButtons: Qt.LeftButton
        //  Markierungen hängen am Text: auswählen ja, ziehen nein.
        cursorShape: box.isMarkup ? Qt.PointingHandCursor
                                  : (box.selected ? Qt.SizeAllCursor : Qt.PointingHandCursor)
        preventStealing: true              // ListView darf den Drag nicht klauen
        property real grabDx: 0
        property real grabDy: 0
        property bool moved: false
        onPressed: (m) => {
            if (box.surface) {
                box.surface.commitEditing()                // fremde Bearbeitung schließen
                box.surface.editDragPage = box.viewIndex   // Delegate über Nachbarn heben
            }
            box.ctl.selectedId = box.boxId
            const p = mapToItem(box.parent, m.x, m.y)
            grabDx = p.x - box.x
            grabDy = p.y - box.y
            moved = false
            box.ctl.beginGeometryEdit(box.boxId)
        }
        onPositionChanged: (m) => {
            if (box.isMarkup)
                return                     // Markierungen bleiben an ihrem Text
            const p = mapToItem(box.parent, m.x, m.y)
            let nx = (p.x - grabDx) / box.pageScale
            let ny = (p.y - grabDy) / box.pageScale
            // Kleine Zitter-Schwelle: reiner Klick bleibt reiner Klick.
            if (!moved && Math.abs(nx - box.xPt) + Math.abs(ny - box.yPt) < 1.5)
                return
            moved = true
            // Zeilenfang (falls aktiv): Oberkante an erkannte PDF-Textzeile -
            // nur für TEXT-Notizen sinnvoll (Zeichnungen bewegen sich frei).
            if (box.surface && box.isTextual)
                ny = box.surface.snapYPt(box.viewIndex, ny, box.hPt)
            nx = Math.max(0, Math.min(nx, Math.max(0, box.pageWPt - box.wPt)))
            // y bewusst NICHT klemmen: über den unteren/oberen Seitenrand
            // hinausgezogene Notizen wandern beim Loslassen auf die
            // Nachbarseite (seitenübergreifendes Verschieben).
            box.ctl.updatePlacement(box.boxId, box.viewIndex, nx, ny, box.wPt, box.hPt)
        }
        onReleased: {
            // Über den Seitenrand gezogen? -> Zielseite + lokale y-Koordinate
            // auflösen (PdfSurface kennt Seitenmaße + Lückenbreite) und die
            // Session mit der finalen Platzierung abschließen (EIN Undo-Schritt
            // inkl. Seitenwechsel).
            if (moved && box.surface) {
                const t = box.surface.resolveCrossPage(box.viewIndex, box.yPt,
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
        model: box.selected && box.selectTool && !box.editing && !box.isMarkup
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
                    // Signatur/Stempel skaliert IMMER proportional - an allen
                    // acht Punkten, nicht nur an den Ecken: ein verzerrtes
                    // Bild sieht nie nach einer echten Unterschrift aus.
                    // Gerechnet wird über EINEN Faktor, in den auch die
                    // Seitengrenzen eingehen; das spätere Klemmen an die Seite
                    // würde das Verhältnis sonst wieder verziehen.
                    if (box.isStamp && startRect.width > 0 && startRect.height > 0) {
                        const cx = startRect.x + startRect.width  / 2
                        const cy = startRect.y + startRect.height / 2
                        const wMax = handle.hx < 0 ? startRect.x + startRect.width
                                   : handle.hx > 0 ? box.pageWPt - startRect.x
                                   : 2 * Math.min(cx, box.pageWPt - cx)
                        const hMax = handle.hy < 0 ? startRect.y + startRect.height
                                   : handle.hy > 0 ? box.pageHPt - startRect.y
                                   : 2 * Math.min(cy, box.pageHPt - cy)
                        let f = Math.max(handle.hx !== 0 ? w / startRect.width  : 0,
                                         handle.hy !== 0 ? h / startRect.height : 0)
                        f = Math.min(f, wMax / startRect.width, hMax / startRect.height)
                        f = Math.max(f, box.minWPt / startRect.width,
                                        box.minHPt / startRect.height)
                        const sw = startRect.width  * f
                        const sh = startRect.height * f
                        // Die gegenüberliegende Kante bleibt stehen; eine
                        // Kantenmitte hält die andere Achse mittig.
                        box.ctl.updateGeometry(
                            box.boxId,
                            handle.hx < 0 ? startRect.x + startRect.width - sw
                                          : handle.hx > 0 ? startRect.x : cx - sw / 2,
                            handle.hy < 0 ? startRect.y + startRect.height - sh
                                          : handle.hy > 0 ? startRect.y : cy - sh / 2,
                            sw, sh)
                        return
                    }
                    // Mindestgröße halten, OHNE die gegenüberliegende Kante zu
                    // bewegen - kind-abhängig (Zeichnungen dürfen kleiner werden).
                    const minW = box.minWPt
                    const minH = box.minHPt
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
