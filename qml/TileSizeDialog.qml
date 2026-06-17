import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  TileSizeDialog.qml — Kachelgröße per Live-Drag-Vorschau einstellen
//  (ersetzt TileSizeDialog/DragResizePreview (QWidget)).
//
//  Verwendung im Shell:
//      TileSizeDialog { id: tileSizeDialog }
//      ... onTriggered: tileSizeDialog.openDialog()
//
//  - Liest Startwerte aus App.tileWidth / App.tileHeight beim Öffnen.
//  - Schreibt bei „Übernehmen" via App.setTileSize(w, h); die Galerie reagiert
//    über das bestehende tileSizeChanged-Binding.
//  - Grid-Geometrie (margin = 12, spacing = 8) spiegelt GalleryView, damit die
//    Vorschau maßstabsgetreu ist.
// ─────────────────────────────────────────────────────────────────────────────
Dialog {
    id: dlg
    title: "Kachelgröße"
    modal: true
    width: 600
    height: 540
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.NoButton

    // Geometrie-Konstanten — identisch zu GalleryView
    readonly property int gMargin: 12
    readonly property int gSpacing: 8
    readonly property int minDim: 40
    readonly property int maxDim: 4096
    readonly property int handleHit: 14

    // Arbeits-Zustand
    property int workW: 160
    property int workH: 200

    function openDialog() {
        workW = App.tileWidth  > 0 ? App.tileWidth  : 160
        workH = App.tileHeight > 0 ? App.tileHeight : 200
        open()
    }

    function setWork(w, h) {
        workW = Math.max(minDim, Math.min(maxDim, Math.round(w)))
        workH = Math.max(minDim, Math.min(maxDim, Math.round(h)))
    }

    background: Rectangle {
        color: App.themeBackground
        border.color: App.themeBorder
        radius: 8
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Ziehe den Eck-Griff der ersten Kachel, um die bevorzugte Kachelgröße einzustellen."
            color: App.themeTextMuted
            font.pixelSize: 12
        }

        // ── Live-Vorschau ──────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 6
            color: Qt.darker(App.themeBackground, 1.2)
            border.color: App.themeBorder
            clip: true

            Canvas {
                id: preview
                anchors.fill: parent
                anchors.margins: 1

                property int tw: dlg.workW
                property int th: dlg.workH
                property bool dragging: false

                onTwChanged: requestPaint()
                onThChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onDraggingChanged: requestPaint()

                function cols() {
                    var avail = width - 2 * dlg.gMargin
                    return Math.max(1, Math.floor((avail + dlg.gSpacing) / (tw + dlg.gSpacing)))
                }
                function rows() {
                    return Math.max(1, Math.floor((height - 2 * dlg.gMargin + dlg.gSpacing) / (th + dlg.gSpacing)))
                }
                function handleX() { return dlg.gMargin + tw }
                function handleY() { return dlg.gMargin + th }

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()

                    var accent = App.themeAccent
                    var card   = App.themeCard
                    var border = App.themeBorder
                    var textC  = App.themeTextPrimary
                    var muted  = App.themeTextMuted

                    var nCols = cols(), nRows = rows()

                    for (var r = 0; r <= nRows; ++r) {
                        for (var c = 0; c < nCols; ++c) {
                            var x = dlg.gMargin + c * (tw + dlg.gSpacing)
                            var y = dlg.gMargin + r * (th + dlg.gSpacing)
                            if (x + tw > width  - dlg.gMargin) continue
                            if (y + th > height - dlg.gMargin) continue

                            var isFirst = (r === 0 && c === 0)

                            // Kachel-Hintergrund
                            ctx.fillStyle = isFirst ? Qt.rgba(accent.r, accent.g, accent.b, 0.55)
                                                    : card
                            roundRect(ctx, x, y, tw, th, 6)
                            ctx.fill()
                            ctx.lineWidth = 1.2
                            ctx.strokeStyle = isFirst ? accent : border
                            ctx.stroke()

                            // Thumbnail-Platzhalter
                            if (th > 60) {
                                var ix = x + 6, iy = y + 6, iw = tw - 12, ih = th - 38
                                if (iw > 0 && ih > 0) {
                                    var g = ctx.createLinearGradient(ix, iy, ix + iw, iy + ih)
                                    if (isFirst) {
                                        g.addColorStop(0, Qt.rgba(accent.r, accent.g, accent.b, 0.45))
                                        g.addColorStop(1, Qt.rgba(accent.r, accent.g, accent.b, 0.20))
                                    } else {
                                        g.addColorStop(0, Qt.lighter(card, 1.15))
                                        g.addColorStop(1, Qt.darker(card, 1.1))
                                    }
                                    ctx.fillStyle = g
                                    roundRect(ctx, ix, iy, iw, ih, 4)
                                    ctx.fill()
                                }
                            }

                            // Label-Leiste
                            if (th > 40) {
                                ctx.fillStyle = isFirst ? textC : muted
                                ctx.font = Math.max(7, Math.floor(tw / 16)) + "px sans-serif"
                                ctx.textBaseline = "middle"
                                ctx.fillText(isFirst ? "example.jpg" : "media.jpg",
                                             x + 6, y + th - 16, tw - 12)
                            }
                        }
                    }

                    // Größen-Anzeige auf erster Kachel
                    ctx.fillStyle = textC
                    ctx.font = "bold 11px sans-serif"
                    ctx.textBaseline = "top"
                    ctx.fillText(tw + " × " + th + " px", dlg.gMargin + 5, dlg.gMargin + 5)

                    // Resize-Griff
                    var hx = handleX(), hy = handleY(), rad = dlg.handleHit
                    ctx.beginPath()
                    ctx.arc(hx, hy, rad, 0, 2 * Math.PI)
                    var hg = ctx.createRadialGradient(hx, hy, 1, hx, hy, rad)
                    hg.addColorStop(0, dragging ? "#00ffd7" : "#00c8aa")
                    hg.addColorStop(1, dragging ? "#00b496" : "#008c73")
                    ctx.fillStyle = hg
                    ctx.fill()
                    ctx.lineWidth = 1.5
                    ctx.strokeStyle = "rgba(0,255,220,0.65)"
                    ctx.stroke()

                    // Pfeil-Andeutung
                    ctx.strokeStyle = "rgba(255,255,255,0.9)"
                    ctx.lineWidth = 1.5
                    ctx.beginPath()
                    ctx.moveTo(hx - 3, hy);     ctx.lineTo(hx + 3, hy)
                    ctx.moveTo(hx, hy - 3);     ctx.lineTo(hx, hy + 3)
                    ctx.moveTo(hx + 1, hy + 1); ctx.lineTo(hx + 4, hy + 4)
                    ctx.stroke()
                }

                function roundRect(ctx, x, y, w, h, r) {
                    r = Math.min(r, w / 2, h / 2)
                    ctx.beginPath()
                    ctx.moveTo(x + r, y)
                    ctx.arcTo(x + w, y,     x + w, y + h, r)
                    ctx.arcTo(x + w, y + h, x,     y + h, r)
                    ctx.arcTo(x,     y + h, x,     y,     r)
                    ctx.arcTo(x,     y,     x + w, y,     r)
                    ctx.closePath()
                }

                // ── Drag-Interaktion ────────────────────────────────────────────
                property real dragStartX: 0
                property real dragStartY: 0
                property int  sizeAtStartW: 0
                property int  sizeAtStartH: 0

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: nearHandle(mouseX, mouseY) ? Qt.SizeFDiagCursor
                                                            : Qt.ArrowCursor

                    function nearHandle(px, py) {
                        var dx = px - preview.handleX()
                        var dy = py - preview.handleY()
                        return Math.sqrt(dx * dx + dy * dy) <= dlg.handleHit + 4
                    }

                    onPressed: (m) => {
                        if (nearHandle(m.x, m.y)) {
                            preview.dragging = true
                            preview.dragStartX = m.x
                            preview.dragStartY = m.y
                            preview.sizeAtStartW = dlg.workW
                            preview.sizeAtStartH = dlg.workH
                            m.accepted = true
                        }
                    }
                    onPositionChanged: (m) => {
                        if (preview.dragging) {
                            dlg.setWork(preview.sizeAtStartW + (m.x - preview.dragStartX),
                                        preview.sizeAtStartH + (m.y - preview.dragStartY))
                        }
                    }
                    onReleased: { preview.dragging = false }
                }
            }
        }

        // ── Numerische Steuerung ─────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label { text: "Breite:"; color: App.themeTextPrimary }
            SpinBox {
                id: wSpin
                from: dlg.minDim; to: dlg.maxDim; stepSize: 8
                value: dlg.workW
                editable: true
                onValueModified: dlg.setWork(value, dlg.workH)
            }

            Label { text: "Höhe:"; color: App.themeTextPrimary }
            SpinBox {
                id: hSpin
                from: dlg.minDim; to: dlg.maxDim; stepSize: 8
                value: dlg.workH
                editable: true
                onValueModified: dlg.setWork(dlg.workW, value)
            }

            Item { Layout.fillWidth: true }

            Label {
                text: dlg.workW + " × " + dlg.workH
                color: App.themeAccent
                font.bold: true
            }
        }

        // ── Aktionsschaltflächen ─────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "Abbrechen"
                onClicked: dlg.reject()
            }
            Button {
                text: "Übernehmen"
                highlighted: true
                onClicked: {
                    App.setTileSize(dlg.workW, dlg.workH)
                    dlg.accept()
                }
            }
        }
    }

    // SpinBoxen folgen dem Arbeits-Zustand bereits über value: dlg.workW /
    // dlg.workH (deklarative Bindung) — Drag aktualisiert sie automatisch.
}
