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
    title: App.uiText(App.language, "SettingsViewTileSize")
    modal: true
    width: 600
    height: 540
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.NoButton

    // Geometrie-Konstanten — identisch zu GalleryView
    readonly property int gMargin: 12
    readonly property int gSpacing: 8
    readonly property int minDim: 40
    readonly property int handleHit: 14
    // Dynamische Obergrenze = darstellbare Galeriefläche (meldet GalleryView
    // an den AppController; das Fenster ist seinerseits durch den Bildschirm
    // begrenzt) — größer als die App kann keine Kachel angezeigt werden.
    readonly property int maxW: App.maxTileWidth
    readonly property int maxH: App.maxTileHeight

    // Arbeits-Zustand
    property int workW: 160
    property int workH: 200

    function openDialog() {
        // setWork klemmt gespeicherte Werte in die AKTUELLEN Grenzen
        // (z. B. nach Fensterverkleinerung seit dem letzten Übernehmen).
        setWork(App.tileWidth  > 0 ? App.tileWidth  : 160,
                App.tileHeight > 0 ? App.tileHeight : 200)
        open()
    }

    function setWork(w, h) {
        workW = Math.max(minDim, Math.min(maxW, Math.round(w)))
        workH = Math.max(minDim, Math.min(maxH, Math.round(h)))
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
            text: App.uiText(App.language, "TileSizeHint")
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

                // ── Maßstab: Miniatur des ECHTEN Anwendungsfensters ────────────
                //  Die Vorschau zeichnet das komplette Fenster verkleinert, mit
                //  FESTEM Maßstab (nur von der Fenstergröße abhängig, NICHT von
                //  der Kachelgröße): Spalten/Zeilen entsprechen dadurch exakt
                //  der echten Galerie, und der Griff folgt der Maus auch bei
                //  großen Kacheln. (Früher wuchs die Kachel nur bis zu einem
                //  62-%-Deckel und „fror" dann sichtbar ein — ab dieser Größe
                //  war die Vorschau nicht mehr originalgetreu.)
                readonly property real winW: Overlay.overlay ? Overlay.overlay.width  : 1280
                readonly property real winH: Overlay.overlay ? Overlay.overlay.height : 800
                readonly property real availW: Math.max(1, width  - 8)
                readonly property real availH: Math.max(1, height - 8)
                readonly property real fitScale: Math.min(1, Math.min(availW / winW, availH / winH))
                //  Miniatur-Fensterrechteck (zentriert im Canvas).
                readonly property real offX: (width  - winW * fitScale) / 2
                readonly property real offY: (height - winH * fitScale) / 2
                // Gezeichnete (skalierte) Maße — Basis für ALLE Zeichenrechnungen.
                readonly property real stw: tw * fitScale
                readonly property real sth: th * fitScale
                readonly property real ssp: dlg.gSpacing * fitScale
                readonly property real smar: dlg.gMargin * fitScale

                onTwChanged: requestPaint()
                onThChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onDraggingChanged: requestPaint()
                onWinWChanged: requestPaint()
                onWinHChanged: requestPaint()

                //  Spalten/Zeilen in REALEN Einheiten (wie die echte Galerie).
                function cols() {
                    return Math.max(1, Math.floor((winW - 2 * dlg.gMargin + dlg.gSpacing)
                                                  / (tw + dlg.gSpacing)))
                }
                function rows() {
                    return Math.max(1, Math.ceil((winH - 2 * dlg.gMargin + dlg.gSpacing)
                                                 / (th + dlg.gSpacing)))
                }
                function handleX() { return offX + smar + stw }
                function handleY() { return offY + smar + sth }

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()

                    var accent = App.themeAccent
                    var card   = App.themeCard
                    var border = App.themeBorder
                    var textC  = App.themeTextPrimary
                    var muted  = App.themeTextMuted

                    var nCols = cols(), nRows = rows()

                    //  Miniatur-Fensterrahmen (die Vorschau IST das Fenster).
                    ctx.fillStyle = Qt.darker(App.themeBackground, 1.05)
                    ctx.strokeStyle = border
                    ctx.lineWidth = 1
                    roundRect(ctx, offX, offY, winW * fitScale, winH * fitScale, 4)
                    ctx.fill()
                    ctx.stroke()

                    for (var r = 0; r < nRows; ++r) {
                        for (var c = 0; c < nCols; ++c) {
                            var x = offX + smar + c * (stw + ssp)
                            var y = offY + smar + r * (sth + ssp)
                            if (x + stw > offX + winW * fitScale - smar) continue
                            if (y + sth > offY + winH * fitScale) continue

                            var isFirst = (r === 0 && c === 0)

                            // Kachel-Hintergrund
                            ctx.fillStyle = isFirst ? Qt.rgba(accent.r, accent.g, accent.b, 0.55)
                                                    : card
                            roundRect(ctx, x, y, stw, sth, 6)
                            ctx.fill()
                            ctx.lineWidth = 1.2
                            ctx.strokeStyle = isFirst ? accent : border
                            ctx.stroke()

                            // Thumbnail-Platzhalter
                            if (sth > 60) {
                                var ix = x + 6, iy = y + 6, iw = stw - 12, ih = sth - 38
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
                            if (sth > 40) {
                                ctx.fillStyle = isFirst ? textC : muted
                                ctx.font = Math.max(7, Math.floor(stw / 16)) + "px sans-serif"
                                ctx.textBaseline = "middle"
                                ctx.fillText(isFirst ? "example.jpg" : "media.jpg",
                                             x + 6, y + sth - 16, stw - 12)
                            }
                        }
                    }

                    // Größen-Anzeige auf erster Kachel — zeigt die REALE Pixelgröße
                    // (nicht die skalierte), plus den Maßstab, wenn verkleinert.
                    ctx.fillStyle = textC
                    ctx.font = "bold 11px sans-serif"
                    ctx.textBaseline = "top"
                    var szLabel = tw + " × " + th + " px"
                    if (fitScale < 0.999)
                        szLabel += "  (" + Math.round(fitScale * 100) + "%)"
                    ctx.fillText(szLabel, dlg.gMargin + 5, dlg.gMargin + 5)

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
                //  Maßstab beim Drag-Start festhalten: die Griff-Bewegung in
                //  Bildschirm-Pixeln wird darüber in REALE Kachel-Pixel
                //  zurückgerechnet (sonst würde der wachsende Maßstab-Nenner den
                //  Drag nahe der Fitgrenze durchdrehen lassen).
                property real scaleAtStart: 1.0

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
                            preview.scaleAtStart = preview.fitScale
                            m.accepted = true
                        }
                    }
                    onPositionChanged: (m) => {
                        if (preview.dragging) {
                            var s = preview.scaleAtStart > 0 ? preview.scaleAtStart : 1.0
                            dlg.setWork(preview.sizeAtStartW + (m.x - preview.dragStartX) / s,
                                        preview.sizeAtStartH + (m.y - preview.dragStartY) / s)
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

            Label { text: App.uiText(App.language, "SettingsViewWidth"); color: App.themeTextPrimary }
            SpinBox {
                id: wSpin
                from: dlg.minDim; to: dlg.maxW; stepSize: 8
                value: dlg.workW
                editable: true
                onValueModified: dlg.setWork(value, dlg.workH)
            }

            Label { text: App.uiText(App.language, "SettingsViewHeight"); color: App.themeTextPrimary }
            SpinBox {
                id: hSpin
                from: dlg.minDim; to: dlg.maxH; stepSize: 8
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
                text: App.uiText(App.language, "SettingsCancel")
                onClicked: dlg.reject()
            }
            Button {
                text: App.uiText(App.language, "SettingsDesignApplyBtn")
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
