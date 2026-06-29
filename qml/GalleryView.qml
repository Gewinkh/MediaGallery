import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  GalleryView.qml — Model/View-Galerie (Phase 2/3, Kernkomponente).
//
//  Ersetzt GalleryView(QScrollArea+QGridLayout). KEIN 1:1-Port: statt 1 Widget je
//  Datei recycelt ein GridView seine Delegates (reuseItems) und hält nur sichtbare
//  Kacheln im Speicher → flacher RAM-Verbrauch auch bei 10–50k Medien.
//
//  Daten kommen aus galleryModel (MediaProxyModel → MediaModel). Mutationen/
//  Thumbnail-Anforderungen laufen über mediaModel (per Dateipfad).
//
//  Performance (Scrollen):
//   • Jede Kachel fordert ihr Thumbnail nur einmal an (Pfad-getaktet) und BRICHT
//     die Anforderung der zuvor angezeigten Datei AB, sobald sie recycelt wird
//     oder verschwindet (mediaModel.cancelThumbnail) → der Loader verschwendet
//     keinen Decode für weggescrollte Kacheln, sichtbare laufen mit Vorrang.
//   • cacheBuffer ≈ 2 Zeilen: glättet schnelles Scrollen (weniger Delegate-Auf-/
//     Abbau an den Rändern) bei weiterhin beschränktem RAM.
//
//  Erhaltene Features: dynamische Kachelgröße, Ctrl+Mausrad-Zoom, Anordnung
//  (Centered/Left/Right/Manual) inkl. Manual-Area-Breite, Doppelklick→Vollbild,
//  Inline-Rename (Overlay), Tag-Toggle, Group-/Add-to-Tag-Modus.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: root
    color: App.themeBackground

    // Vollbild-Anforderung an die Shell (Phase 3 füllt das Ziel).
    signal activated(string filePath)

    // Vorschau-Sperre ("B"): blendet alle Thumbnails hinter einer Abdeckung aus
    // (Privatsphäre). Entspricht GalleryView::setCovered(bool) im alten Projekt.
    property bool covered: false

    // Beim Ordnerwechsel/Neuladen die Vorschau-Sperre aufheben.
    Connections {
        target: App
        function onFolderOpened(path) { root.covered = false }
    }

    // ── View-Modi (Hook-Punkte für FilterBar/TagSystem in Phase 3) ──────────
    // 0 = none, 1 = group (Rechtsklick toggelt), 2 = addToTag (Linksklick toggelt)
    property int    tagMode: 0
    property string modeTag: ""

    function enterGroupMode(tag)     { modeTag = tag; tagMode = 1 }
    function enterAddToTagMode(tag)  { modeTag = tag; tagMode = 2 }
    function exitModes()             { modeTag = ""; tagMode = 0 }

    // ── Layout-Konstanten / abgeleitete Geometrie ───────────────────────────
    readonly property int margin: 12
    readonly property int spacing: 8
    readonly property int cellW: App.tileWidth + spacing
    readonly property int cellH: App.tileHeight + spacing

    readonly property int areaW: App.tileArrangement === 3   // Manual
                                 ? Math.min(App.manualAreaWidth, root.width - 2 * margin)
                                 : root.width - 2 * margin
    readonly property int columns: Math.max(1, Math.floor(areaW / cellW))
    readonly property int gridW: columns * cellW
    readonly property int gridX: {
        switch (App.tileArrangement) {
        case 1: return margin                              // Left
        case 2: return Math.max(margin, root.width - margin - gridW)  // Right
        default: return Math.max(margin, (root.width - gridW) / 2)    // Centered/Manual
        }
    }

    // ── Leerzustand ─────────────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: grid.count === 0
        width: Math.min(root.width - 48, 640)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: App.currentFolder.length > 0
              ? App.uiText(App.language, "GalleryNoMedia")
              : App.uiText(App.language, "GalleryNoFolder")
        color: App.themeTextMuted
        font.pixelSize: 14
    }

    // ── Gitter ──────────────────────────────────────────────────────────────
    GridView {
        id: grid
        y: 0
        x: root.gridX
        width: root.gridW
        height: root.height
        clip: true

        cellWidth: root.cellW
        cellHeight: root.cellH

        model: galleryModel

        // RAM-Priorität bei zugleich glattem Scrollen: ~2 Zeilen Vorhalt.
        reuseItems: true
        cacheBuffer: root.cellH * 2
        boundsBehavior: Flickable.StopAtBounds

        // Vertikale Scrollbar bündig an den rechten Rand der Galerie (root) statt
        // an den Rand des zentrierten Gitters. Sie bleibt funktional an den
        // GridView gebunden (Größe/Position), wird aber zu root umgehängt und dort
        // rechts verankert — Standardmuster für „Scrollbar am Container-Rand".
        ScrollBar.vertical: ScrollBar {
            id: vScroll
            parent: root
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            policy: ScrollBar.AsNeeded
        }

        delegate: Item {
            id: cell
            width: grid.cellWidth
            height: grid.cellHeight

            required property string filePath
            required property string displayName
            required property int    mediaType
            required property string typeLabel
            required property var    tags
            required property var    dateTime
            required property string thumbUrl
            required property int    thumbState

            // Pfad, für den aktuell ein Thumbnail angefordert ist (Abbruch-Tracking).
            property string requestedPath: ""

            // Sichtbarkeitsgesteuerte Thumbnail-Anforderung mit Abbruch der zuvor
            // angezeigten Datei (greift auch bei Delegate-Recycling).
            function syncThumb() {
                if (requestedPath === filePath)
                    return
                if (requestedPath.length > 0)
                    mediaModel.cancelThumbnail(requestedPath)   // weggescrollte Kachel
                requestedPath = filePath
                if (filePath.length > 0)
                    mediaModel.ensureThumbnail(filePath)
            }

            Component.onCompleted: syncThumb()
            onFilePathChanged: syncThumb()
            GridView.onReused: syncThumb()

            // Wird die Kachel zerstört (Ordner schrumpft o. Ä.), laufende
            // Anforderung abbrechen.
            Component.onDestruction: {
                if (requestedPath.length > 0)
                    mediaModel.cancelThumbnail(requestedPath)
            }

            MediaTile {
                anchors.centerIn: parent
                width: App.tileWidth
                height: App.tileHeight

                filePath: cell.filePath
                displayName: cell.displayName
                mediaType: cell.mediaType
                typeLabel: cell.typeLabel
                tags: cell.tags
                dateTime: cell.dateTime
                thumbUrl: cell.thumbUrl
                thumbState: cell.thumbState

                tagMode: root.tagMode
                modeTag: root.modeTag
                covered: root.covered

                onActivated: function(p) { root.activated(p) }
            }
        }
    }

    // ── Mausrad: Strg = Zoom (Kachelgröße), sonst weiches Scrollen ───────────
    //  Eine MouseArea(NoButton) fängt die Wheel-Events zuverlässig ab — ein
    //  WheelHandler/das GridView selbst verschluckt sie sonst. NoButton lässt
    //  Klicks, Doppelklicks und Hover ungehindert zu den Kacheln durch.
    NumberAnimation {
        id: gridScroll
        target: grid; property: "contentY"
        duration: 180; easing.type: Easing.OutCubic
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        z: 2
        onWheel: function(wheel) {
            if (wheel.modifiers & Qt.ControlModifier) {
                if (wheel.angleDelta.y > 0)      App.zoomIn(16)
                else if (wheel.angleDelta.y < 0) App.zoomOut(16)
                wheel.accepted = true
                return
            }
            var maxY = Math.max(0, grid.contentHeight - grid.height)
            if (maxY <= 0) { wheel.accepted = true; return }
            var raw = (wheel.angleDelta.y !== 0)
                      ? (wheel.angleDelta.y / 120) * (grid.height * 0.45)
                      : wheel.pixelDelta.y * 1.6
            var base = gridScroll.running ? gridScroll.to : grid.contentY
            var tgt = Math.max(0, Math.min(base - raw, maxY))
            gridScroll.from = grid.contentY
            gridScroll.to = tgt
            gridScroll.restart()
            wheel.accepted = true
        }
    }
}
