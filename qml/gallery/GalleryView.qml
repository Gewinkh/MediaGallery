import QtQuick
import QtQuick.Controls
import QtQuick.Window
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

    //  ── Pfeiltasten scrollen die ANSICHT ────────────────────────────────────
    //  Bewusst NICHT der eingebaute Tastenfluss des GridView: der bewegt
    //  `currentIndex`, also eine Auswahl — die Galerie hat keine. Gescrollt wird
    //  über dieselbe Animation wie das Mausrad, damit sich beides gleich anfühlt.
    focus: true
    //  Schreibt der Nutzer gerade (Umbenennen-Feld, Filterleiste), gehören die
    //  Pfeiltasten IHM. Ein einzeiliges TextField lässt ↑/↓ sonst nach oben
    //  durch und die Galerie spränge beim Tippen. Muster aus `FullscreenViewer`.
    function _editableTextFocused() {
        var f = root.Window.activeFocusItem
        if (!f) return false
        return (f.cursorPosition !== undefined) && (f.readOnly !== true)
    }
    //  ── Scroll-Grenzen sind ORIGIN-BEWUSST ──────────────────────────────────
    //  Ein Flickable liegt NICHT zwingend bei 0: entfernt oder ergänzt das
    //  Modell Zeilen (Filter, Suche, Ordner-Watcher), verschiebt das GridView
    //  seinen `originY`. Wer trotzdem gegen 0 klemmt, schiebt die Ansicht ÜBER
    //  den Inhalt hinaus — oben steht ein leerer Streifen, und genau so weit
    //  sind die letzten Zeilen unten nicht mehr erreichbar. Genau dieser Befund
    //  („wie ein Shift im Scrolling-Fenster", schmales Fenster = mehr Zeilen).
    //  Dieselbe Lehre wie in `PdfSurface` (minContentY/maxContentY/clampContentY).
    readonly property real minContentY: grid.originY
    readonly property real maxContentY: Math.max(grid.originY,
                                                 grid.originY + grid.contentHeight - grid.height)
    function clampContentY(v) {
        return Math.max(root.minContentY, Math.min(v, root.maxContentY))
    }
    function scrollByPixels(dy) {
        if (root.maxContentY <= root.minContentY) return
        var base = gridScroll.running ? gridScroll.to : grid.contentY
        gridScroll.from = grid.contentY
        gridScroll.to = root.clampContentY(base + dy)
        gridScroll.restart()
    }
    //  Schrittweite: eine halbe Kachelreihe — ein ganzer Kachelsprung überspringt
    //  bei großen Kacheln fast den ganzen Sichtbereich.
    readonly property real _keyStep: Math.max(40, root.cellH * 0.5)
    Keys.onUpPressed: function(event) {
        if (root._editableTextFocused()) return
        root.scrollByPixels(-root._keyStep); event.accepted = true
    }
    Keys.onDownPressed: function(event) {
        if (root._editableTextFocused()) return
        root.scrollByPixels(root._keyStep); event.accepted = true
    }
    Keys.onPressed: function(event) {
        if (root._editableTextFocused()) return
        if (event.key === Qt.Key_PageDown) {
            root.scrollByPixels(grid.height * 0.9); event.accepted = true
        } else if (event.key === Qt.Key_PageUp) {
            root.scrollByPixels(-grid.height * 0.9); event.accepted = true
        } else if (event.key === Qt.Key_Home) {
            root.scrollByPixels(-grid.contentHeight); event.accepted = true
        } else if (event.key === Qt.Key_End) {
            root.scrollByPixels(grid.contentHeight); event.accepted = true
        }
    }

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

    // Kachelgrößen-Obergrenze = das, was diese Galeriefläche VOLLSTÄNDIG
    // darstellen kann (breiter: Seitenränder + Zellen-Padding; höher: eine
    // Zelle muss in die Höhe passen). Die Fläche selbst ist durchs Fenster
    // und dieses durch den Bildschirm begrenzt. App.setTileSize klemmt
    // dagegen; Dialog/Einstellungen binden ihre Maxima an App.maxTileWidth/-Height.
    function _reportTileLimit() {
        if (width > 0 && height > 0)
            App.setTileSizeLimit(width - 2 * margin - spacing, height - spacing)
    }
    onWidthChanged: _reportTileLimit()
    onHeightChanged: _reportTileLimit()
    Component.onCompleted: _reportTileLimit()

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
        //  KEIN Ziehen-zum-Scrollen: gescrollt wird per Rad, Bildlaufleiste und
        //  Pfeiltasten. Ein Zug mit der Maus gehört der KACHEL — sie zieht die
        //  Datei nach draußen (`Drag.Automatic`), und beides gleichzeitig ging
        //  nicht: das GridView riss den Griff an sich. Dasselbe Muster wie in
        //  `PdfSurface` (dort `pages`-ListView `interactive:false`).
        interactive: false

        //  ZURÜCK IN DEN GÜLTIGEN BEREICH, sobald sich Inhalt oder Geometrie
        //  ändern. `interactive: false` heißt: das Flickable holt sich NICHT
        //  selbst zurück (das täte sonst die Zug-Geste). Ohne diese Klemme blieb
        //  die Ansicht nach einer Suche, einem Ordnerwechsel oder einer
        //  Größenänderung außerhalb ihres Inhalts stehen — sichtbar als leerer
        //  Streifen mit unerreichbaren Kacheln.
        function _clampNow() {
            if (gridScroll.running) return          // die Animation klemmt selbst
            const v = root.clampContentY(contentY)
            if (Math.abs(v - contentY) > 0.5) contentY = v
        }
        onContentHeightChanged: _clampNow()
        onOriginYChanged:       _clampNow()
        onHeightChanged:        _clampNow()
        onCountChanged:         _clampNow()

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

            // Zielgrößen-Stufe der Thumbnails gewechselt (Kachelgröße): die
            // Anforderung dieses Delegates neu stellen — syncThumbs Guard
            // (requestedPath === filePath) würde sie sonst unterdrücken.
            Connections {
                target: mediaModel
                function onThumbnailsInvalidated() {
                    cell.requestedPath = ""
                    cell.syncThumb()
                }
            }

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
                onDeleteRequested: function(p, n) {
                    deleteDialog.targetPath = p
                    deleteDialog.targetName = n
                    deleteDialog.open()
                }
                onCompanionRemoveRequested: function(p, kind) {
                    companionDialog.targetPath = p
                    companionDialog.kind = kind
                    companionDialog.open()
                }
            }
        }
    }

    // ── Lösch-Bestätigung (EIN gemeinsamer Dialog für alle Kacheln) ──────────
    //  Verschiebt die Datei in den Papierkorb (mediaModel.deleteItem — räumt
    //  auch Sidecar + persistierte Metadaten ab). Themenkonform gestaltet.
    // ── Begleitdatei entfernen (Notizen/Zeichnungen bzw. Sicherungskopie) ────
    //  Eigener Dialog statt des Lösch-Dialogs: hier geht NICHT die Datei weg,
    //  und genau das muss der Text sagen. Rückholbar mit Strg+Z.
    Dialog {
        id: companionDialog
        property string targetPath: ""
        property int    kind: 1
        anchors.centerIn: parent
        modal: true
        padding: 18
        background: Rectangle {
            color: App.themeCard; radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            spacing: 10
            Text {
                text: companionDialog.kind === 1
                      ? App.uiText(App.language, "CtxRemoveEdits")
                      : App.uiText(App.language, "CtxRemoveBackup")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 320
                text: companionDialog.kind === 1
                      ? App.uiText(App.language, "CtxRemoveEditsAsk")
                      : App.uiText(App.language, "CtxRemoveBackupAsk")
                color: App.themeTextMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Rectangle {
                    width: compCancel.implicitWidth + 24; height: 30; radius: 6
                    color: "transparent"; border.color: App.themeBorder; border.width: 1
                    Text { id: compCancel; anchors.centerIn: parent
                           text: App.uiText(App.language, "SettingsCancel")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    TapHandler { onTapped: companionDialog.close() }
                }
                Rectangle {
                    width: compOk.implicitWidth + 24; height: 30; radius: 6
                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                   App.themeAccent.b, 0.28)
                    border.color: App.themeAccent; border.width: 1
                    Text { id: compOk; anchors.centerIn: parent
                           text: App.uiText(App.language, "SettingsOk")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    TapHandler {
                        onTapped: {
                            mediaModel.removeCompanion(companionDialog.targetPath,
                                                       companionDialog.kind)
                            companionDialog.close()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: deleteDialog
        property string targetPath: ""
        property string targetName: ""
        anchors.centerIn: parent
        modal: true
        padding: 18
        background: Rectangle {
            color: App.themeCard
            radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            spacing: 10
            Text {
                text: App.uiText(App.language, "DeleteMediaTitle")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 300
                text: App.uiText(App.language, "DeleteMediaText").arg(deleteDialog.targetName)
                color: App.themeTextMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Rectangle {
                    width: cancelLbl.implicitWidth + 24; height: 30; radius: 6
                    color: cancelHover.hovered
                           ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                           : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                    border.color: App.themeBorder; border.width: 1
                    Text { id: cancelLbl; anchors.centerIn: parent
                           text: App.uiText(App.language, "SettingsCancel")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: cancelHover }
                    TapHandler { onTapped: deleteDialog.close() }
                }
                Rectangle {
                    width: delLbl.implicitWidth + 24; height: 30; radius: 6
                    color: delHover.hovered ? Qt.rgba(0.88, 0.35, 0.35, 0.30)
                                            : Qt.rgba(0.88, 0.35, 0.35, 0.16)
                    border.color: "#c25a5a"; border.width: 1
                    Text { id: delLbl; anchors.centerIn: parent
                           text: App.uiText(App.language, "DeleteMediaConfirm")
                           color: "#e08080"; font.pixelSize: 12 }
                    HoverHandler { id: delHover }
                    TapHandler {
                        onTapped: {
                            mediaModel.deleteItem(deleteDialog.targetPath)
                            deleteDialog.close()
                        }
                    }
                }
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
            if (root.maxContentY <= root.minContentY) { wheel.accepted = true; return }
            var raw = (wheel.angleDelta.y !== 0)
                      ? (wheel.angleDelta.y / 120) * (grid.height * 0.45)
                      : wheel.pixelDelta.y * 1.6
            var base = gridScroll.running ? gridScroll.to : grid.contentY
            var tgt = root.clampContentY(base - raw)
            gridScroll.from = grid.contentY
            gridScroll.to = tgt
            gridScroll.restart()
            wheel.accepted = true
        }
    }
}
