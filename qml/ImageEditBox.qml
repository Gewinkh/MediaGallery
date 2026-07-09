pragma ComponentBehavior: Bound
import QtQuick
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  ImageEditBox.qml — EINE Overlay-Annotation des Bild-Editors (Delegate im
//  Annotations-Repeater von ImageSurface). Deckt alle fünf Arten ab:
//  Text-Notiz (Post-it, volle Parität zum PDF-Editor), Freihand, Pfeil,
//  Rechteck, Ellipse.
//
//  KOORDINATEN: Das Modell führt Geometrie in BILD-PIXELN (Ursprung oben-links);
//  dieses Item rechnet über `imgScale` (angezeigte Pixel je Bild-Pixel) in
//  Bildschirm-Pixel um → WYSIWYG zum Export (1:1 in native Auflösung).
//
//  SICHTBARKEIT: in BEIDEN Modi sichtbar (Teil des Bild-Eindrucks); Rahmen,
//  Handles und Maus-Interaktion existieren nur im Edit-Modus mit dem
//  Auswahl-Werkzeug (tool === Select).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: box

    // ── Modellrollen (ImageEditModel) ─────────────────────────────────────────
    required property int    annId
    required property int    annKind          // 0 Text,1 Freihand,2 Pfeil,3 Rect,4 Ellipse
    required property real   xPx
    required property real   yPx
    required property real   wPx
    required property real   hPx
    required property var    annPoints        // Freihand/Pfeil: Liste von QPointF (Bild-px)
    required property color  strokeColor
    required property real   lineWidth
    required property color  fillColor
    required property string annText
    required property string fontFamily
    required property real   fontSizePx
    required property bool   bold
    required property bool   italic
    required property bool   underline
    required property color  textColor
    required property color  highlightColor
    required property int    alignment
    required property int    vAlign

    // ── Vom Surface gesetzt ───────────────────────────────────────────────────
    property real imgScale: 1.0                // angezeigte Pixel je Bild-Pixel
    property real imgWpx: 0                     // Bildgröße (Klemm-Grenzen)
    property real imgHpx: 0
    property var  surface: null
    readonly property ImageEditController ctl: surface ? surface.editCtl : null

    readonly property bool isText:   annKind === 0
    readonly property bool isStroke: annKind === 1 || annKind === 2
    readonly property bool editMode: box.ctl.editMode
    readonly property bool selectTool: box.ctl.tool === 0
    readonly property bool selected: editMode && box.ctl.selectedId === annId
    readonly property bool interactive: selected && selectTool
    property bool editing: false               // Inline-Textbearbeitung aktiv

    visible: surface ? surface.notesVisible : true
    x: xPx * imgScale
    y: yPx * imgScale
    width:  Math.max(1, wPx * imgScale)
    height: Math.max(1, hPx * imgScale)
    z: selected ? 3 : 2

    readonly property int commitRev: surface ? surface.editCommitRev : 0
    onCommitRevChanged: finishEditing()
    onEditModeChanged: if (!editMode) finishEditing()
    onSelectedChanged: if (!selected) finishEditing()

    Component.onCompleted: {
        if (surface && surface._autoEditId === annId) {
            surface._autoEditId = -1
            Qt.callLater(startEditing)
        }
    }

    function startEditing() {
        if (!box.isText || !box.editMode || box.editing)
            return
        box.ctl.selectedId = box.annId
        box.ctl.beginTextEdit(box.annId)
        box.editing = true
        edit.forceActiveFocus()
        edit.cursorPosition = edit.length
    }
    function finishEditing() {
        if (!box.editing)
            return
        box.editing = false
        box.ctl.endTextEdit(box.annId)
        if (edit.activeFocus)
            edit.focus = false
    }

    // ══ TEXT-NOTIZ (Post-it) ══════════════════════════════════════════════════
    readonly property bool hasPaper: isText && highlightColor.a > 0

    Rectangle {                                     // weicher Schattenwurf
        x: box.ctl.noteShadowDxPx * box.imgScale
        y: box.ctl.noteShadowDyPx * box.imgScale
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
        readonly property real f: Math.min(box.ctl.noteFoldPx * box.imgScale,
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
            const flap = Qt.darker(paper, 1.18)
            ctx.fillStyle = Qt.rgba(flap.r, flap.g, flap.b, paper.a)
            ctx.beginPath()
            ctx.moveTo(0, height); ctx.lineTo(width, 0); ctx.lineTo(width, height)
            ctx.closePath(); ctx.fill()
            const ln = Qt.darker(paper, 1.5)
            ctx.strokeStyle = Qt.rgba(ln.r, ln.g, ln.b, paper.a)
            ctx.lineWidth = Math.max(1, 1.0 * box.imgScale)
            ctx.beginPath(); ctx.moveTo(0, height); ctx.lineTo(width, 0); ctx.stroke()
        }
    }

    // ══ ZEICHNUNG (Freihand / Pfeil / Rechteck / Ellipse) ═════════════════════
    //  Canvas deckt die Bounding-Box PLUS Linienbreiten-Rand ab (die äußere
    //  Stifthälfte darf nicht abgeschnitten werden — Export zeichnet mittig).
    Canvas {
        id: shape
        visible: !box.isText
        readonly property real pad: Math.max(2, box.lineWidth * box.imgScale)
        anchors.fill: parent
        anchors.margins: -pad
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.lineCap = "round"; ctx.lineJoin = "round"
            const sc = box.imgScale
            const ox = pad, oy = pad     // Ursprung = Box-Ecke innerhalb des Canvas
            const lw = Math.max(1, box.lineWidth * sc)
            if (box.annKind === 1) {                 // Freihand
                if (box.annPoints && box.annPoints.length >= 2) {
                    ctx.strokeStyle = box.strokeColor; ctx.lineWidth = lw
                    ctx.beginPath()
                    for (var i = 0; i < box.annPoints.length; ++i) {
                        const px = ox + (box.annPoints[i].x - box.xPx) * sc
                        const py = oy + (box.annPoints[i].y - box.yPx) * sc
                        if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                    }
                    ctx.stroke()
                }
            } else if (box.annKind === 2) {          // Pfeil
                if (box.annPoints && box.annPoints.length >= 2) {
                    const fx = ox + (box.annPoints[0].x - box.xPx) * sc
                    const fy = oy + (box.annPoints[0].y - box.yPx) * sc
                    const tx = ox + (box.annPoints[1].x - box.xPx) * sc
                    const ty = oy + (box.annPoints[1].y - box.yPx) * sc
                    ctx.strokeStyle = box.strokeColor; ctx.lineWidth = lw
                    ctx.beginPath(); ctx.moveTo(fx, fy); ctx.lineTo(tx, ty); ctx.stroke()
                    // Pfeilspitze (identische Geometrie wie der Export).
                    const ang = Math.atan2(ty - fy, tx - fx)
                    const len = Math.max(14 * sc, box.lineWidth * 4 * sc)
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
                    if (box.annKind === 4) { _ellipsePath(ctx, rx, ry, rw, rh); ctx.fill() }
                    else ctx.fillRect(rx, ry, rw, rh)
                }
                ctx.strokeStyle = box.strokeColor; ctx.lineWidth = lw
                if (box.annKind === 4) { _ellipsePath(ctx, rx, ry, rw, rh); ctx.stroke() }
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
        // Neu zeichnen bei jeder relevanten Änderung.
        Connections {
            target: box
            function onWidthChanged()          { shape.requestPaint() }
            function onHeightChanged()         { shape.requestPaint() }
            function onAnnPointsChanged()      { shape.requestPaint() }
            function onStrokeColorChanged()    { shape.requestPaint() }
            function onLineWidthChanged()      { shape.requestPaint() }
            function onFillColorChanged()      { shape.requestPaint() }
            function onImgScaleChanged()       { shape.requestPaint() }
        }
        Component.onCompleted: requestPaint()
    }

    // ── Rahmen (nur Edit-Modus; Akzent bei Auswahl) ───────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        visible: box.editMode && box.selectTool
        border.color: box.selected ? App.themeAccent
                                   : Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                             App.themeAccent.b, 0.35)
        border.width: box.selected ? 2 : 1
    }

    // ── Text (Anzeige + Inline-Bearbeitung) ───────────────────────────────────
    TextEdit {
        id: edit
        visible: box.isText
        anchors.fill: parent
        padding: box.ctl.boxPaddingPx * box.imgScale
        clip: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        enabled: box.editing
        activeFocusOnPress: false
        selectByMouse: true
        persistentSelection: false
        font: App.fallbackFont(box.fontFamily,
                               Math.max(1, box.fontSizePx * box.imgScale),
                               box.bold, box.italic, box.underline)
        color: box.textColor
        horizontalAlignment: box.alignment === 1 ? TextEdit.AlignHCenter
                           : box.alignment === 2 ? TextEdit.AlignRight
                                                 : TextEdit.AlignLeft
        verticalAlignment: box.vAlign === 1 ? TextEdit.AlignVCenter : TextEdit.AlignTop

        Component.onCompleted: if (box.isText) text = box.annText
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
                box.ctl.updateText(box.annId, text)
            }
        }
        onActiveFocusChanged: if (!activeFocus) box.finishEditing()
        Keys.onEscapePressed: (e) => { box.finishEditing(); e.accepted = true }
    }
    onAnnTextChanged: if (isText && !editing && edit.text !== annText) edit.text = annText

    // ── Platzhalter in leerer Text-Notiz ──────────────────────────────────────
    Text {
        anchors.fill: parent
        anchors.margins: box.ctl.boxPaddingPx * box.imgScale
        visible: box.isText && box.editMode && !box.editing && box.annText.length === 0
        text: App.uiText(App.language, "ImageEditEmptyHint")
        color: "#9aa0a6"
        font.pixelSize: Math.max(8, box.fontSizePx * box.imgScale * 0.85)
        font.italic: true
        elide: Text.ElideRight
        verticalAlignment: box.vAlign === 1 ? Text.AlignVCenter : Text.AlignTop
    }

    // ── Verschieben + Auswahl + Doppelklick (nur tool === Select) ─────────────
    MouseArea {
        id: moveArea
        anchors.fill: parent
        enabled: box.editMode && box.selectTool && !box.editing
        visible: enabled
        acceptedButtons: Qt.LeftButton
        cursorShape: box.selected ? Qt.SizeAllCursor : Qt.PointingHandCursor
        preventStealing: true
        property real grabDx: 0
        property real grabDy: 0
        property bool moved: false
        onPressed: (m) => {
            if (box.surface) box.surface.commitEditing()
            box.ctl.selectedId = box.annId
            const p = mapToItem(box.parent, m.x, m.y)
            grabDx = p.x - box.x
            grabDy = p.y - box.y
            moved = false
            box.ctl.beginGeometryEdit(box.annId)
        }
        onPositionChanged: (m) => {
            const p = mapToItem(box.parent, m.x, m.y)
            let nx = (p.x - grabDx) / box.imgScale
            let ny = (p.y - grabDy) / box.imgScale
            if (!moved && Math.abs(nx - box.xPx) + Math.abs(ny - box.yPx) < 1.5)
                return
            moved = true
            // In das Bild klemmen.
            nx = Math.max(0, Math.min(nx, Math.max(0, box.imgWpx - box.wPx)))
            ny = Math.max(0, Math.min(ny, Math.max(0, box.imgHpx - box.hPx)))
            box.ctl.updateGeometry(box.annId, nx, ny, box.wPx, box.hPx)
        }
        onReleased: box.ctl.endGeometryEdit(box.annId)
        onDoubleClicked: box.startEditing()
    }

    // ── Größen-Handles: 4 Ecken + 4 Kantenmitten (nur bei Auswahl) ────────────
    Repeater {
        model: box.interactive && !box.editing
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
                anchors.margins: -4
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                cursorShape: {
                    if (handle.hx !== 0 && handle.hy !== 0)
                        return (handle.hx === handle.hy) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                    return handle.hx !== 0 ? Qt.SizeHorCursor : Qt.SizeVerCursor
                }
                property rect startRect
                property real pressX: 0
                property real pressY: 0
                onPressed: (m) => {
                    if (box.surface) box.surface.commitEditing()
                    box.ctl.selectedId = box.annId
                    const p = mapToItem(box.parent, m.x, m.y)
                    pressX = p.x; pressY = p.y
                    startRect = Qt.rect(box.xPx, box.yPx, box.wPx, box.hPx)
                    box.ctl.beginGeometryEdit(box.annId)
                }
                onPositionChanged: (m) => {
                    const p = mapToItem(box.parent, m.x, m.y)
                    const dx = (p.x - pressX) / box.imgScale
                    const dy = (p.y - pressY) / box.imgScale
                    let x = startRect.x, y = startRect.y
                    let w = startRect.width, h = startRect.height
                    if (handle.hx < 0) { x = startRect.x + dx; w = startRect.width  - dx }
                    if (handle.hx > 0) {                       w = startRect.width  + dx }
                    if (handle.hy < 0) { y = startRect.y + dy; h = startRect.height - dy }
                    if (handle.hy > 0) {                       h = startRect.height + dy }
                    const minW = box.ctl.minAnnPx, minH = box.ctl.minAnnPx
                    if (w < minW) { if (handle.hx < 0) x = startRect.x + startRect.width  - minW; w = minW }
                    if (h < minH) { if (handle.hy < 0) y = startRect.y + startRect.height - minH; h = minH }
                    if (x < 0) { w += x; x = 0 }
                    if (y < 0) { h += y; y = 0 }
                    if (x + w > box.imgWpx) w = box.imgWpx - x
                    if (y + h > box.imgHpx) h = box.imgHpx - y
                    box.ctl.updateGeometry(box.annId, x, y, Math.max(minW, w), Math.max(minH, h))
                }
                onReleased: box.ctl.endGeometryEdit(box.annId)
            }
        }
    }
}
