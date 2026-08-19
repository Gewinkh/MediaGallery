import QtQuick
import QtQuick.Controls
import QtQuick.Window
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  GalleryView.qml - Model/View-Galerie (Phase 2/3, Kernkomponente).
//
//  Ersetzt GalleryView(QScrollArea+QGridLayout). KEIN 1:1-Port: statt 1 Widget je
//  Datei recycelt ein GridView seine Delegates (reuseItems) und hält nur sichtbare
//  Kacheln im Speicher -> flacher RAM-Verbrauch auch bei 10–50k Medien.
//
//  Daten kommen aus galleryModel (MediaProxyModel -> MediaModel). Mutationen/
//  Thumbnail-Anforderungen laufen über mediaModel (per Dateipfad).
//
//  Performance (Scrollen):
//   • Jede Kachel fordert ihr Thumbnail nur einmal an (Pfad-getaktet) und BRICHT
//     die Anforderung der zuvor angezeigten Datei AB, sobald sie recycelt wird
//     oder verschwindet (mediaModel.cancelThumbnail) -> der Loader verschwendet
//     keinen Decode für weggescrollte Kacheln, sichtbare laufen mit Vorrang.
//   • cacheBuffer ≈ 2 Zeilen: glättet schnelles Scrollen (weniger Delegate-Auf-/
//     Abbau an den Rändern) bei weiterhin beschränktem RAM.
//
//  Erhaltene Features: dynamische Kachelgröße, Ctrl+Mausrad-Zoom, Anordnung
//  (Centered/Left/Right/Manual) inkl. Manual-Area-Breite, Doppelklick->Vollbild,
//  Inline-Rename (Overlay), Tag-Toggle, Group-/Add-to-Tag-Modus.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: root
    color: App.themeBackground

    // Vollbild-Anforderung an die Shell (Phase 3 füllt das Ziel).
    signal activated(string filePath)
    //  Einfacher Klick auf eine Datei (s. MediaTile) - im Player-Modus spielt er.
    signal fileClicked(string filePath)
    //  Doppelklick auf eine Ordnerkachel: den Ordner als neue Hauptebene
    //  oeffnen. Die Shell haengt das an App.openSubfolder (Rueckweg Alt+<-).
    signal folderOpenRequested(string folderPath)
    //  Aktionen aus der Kopfzeile eines aufgeklappten Bereichs - sie zielen auf
    //  DIESEN Ordner, nicht auf den geoeffneten (Festlegung des Nutzers).
    signal createFileRequested(string folderPath)
    signal extractPagesRequested(string folderPath)
    //  Meldung an die Shell (sie führt die Statuszeile).
    signal statusRequested(string text)
    //  Eine Datei wurde auf eine Ordnerkachel gezogen. Die Shell entscheidet
    //  (Verschieben/Kopieren, Namenskollision) - sie hat den Dialog dafür schon
    //  für die Lesezeichen-Leiste.
    signal folderDropRequested(string sourcePath, string folderPath)
    //  Dateien von AUSSEN (Dateimanager, Browser) - sie werden kopiert.
    signal externalDropRequested(var urls, string folderPath)

    // ── Ablegen: EINE Fläche, ausgewertet über die Zeilengeometrie ───────────
    //  Gemessen (`tests/bench/bench_dnd.cpp`): eine Fläche mit Treffersuche
    //  kostet je Mausbewegung ~2,5 µs und bleibt bei wachsender Kachelzahl
    //  praktisch flach; EINE Fläche JE KACHEL kostet bei 200 Kacheln 17 µs und
    //  wächst linear - auch abgeschaltet noch 10 µs. Der JavaScript-Handler
    //  selbst ist dabei Rundungsrauschen (2,67 -> 3,08 µs). Deshalb: eine
    //  Fläche, und die Hervorhebung läuft über `hoverFolder`.
    property string hoverFolder: ""

    //  Welcher Ordner liegt unter diesem Punkt (Koordinaten von `root`)?
    //  Über einer ORDNERKACHEL gewinnt deren eigener Pfad - dorthin zu ziehen
    //  ist eindeutiger gemeint als „in den Bereich". Trifft der Punkt keine
    //  Zeile (leerer Raum, Rand), bleibt es beim geöffneten Ordner: ein Drop
    //  landet nie im Nichts.
    function folderAtPoint(px, py) {
        //  Rückfall ist der Ordner DES MODELLS, nicht `App.currentFolder`:
        //  die Zeilen kommen von dort, und damit gibt es für „welcher Ordner
        //  ist gemeint" nur eine Quelle.
        const fallback = mediaModel.folder
        if (galleryModel.count === 0) return fallback
        const cx = px - grid.x
        const cy = py - grid.y + grid.contentY
        if (cx < 0 || cx > grid.width) return fallback
        const r = grid.indexAt(cx, cy)
        if (r < 0) return fallback
        const info = rowModel.rowInfo(r)
        if (!info || info.ownerFolder === undefined) return fallback
        if (info.kind === 1) return info.ownerFolder        // Kopfzeile

        //  Innerhalb der Zeile entscheidet allein x - eine Zeile trägt genau
        //  eine Kachelreihe.
        const rel = cx - info.depth * root.levelInset
        const col = Math.floor(rel / root.cellW)
        if (rel >= 0 && col >= 0 && col < info.count) {
            const proxyRow = info.first + col
            if (galleryModel.mediaTypeAt(proxyRow) === 7)
                return galleryModel.filePathAt(proxyRow)
        }
        return info.ownerFolder
    }

    //  Der Ordner, in dem eine Datei liegt - rein textuell, ohne Dateisystem.
    function _parentOf(path) {
        const cut = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return cut > 0 ? path.substring(0, cut) : ""
    }

    readonly property int headerHeight: 30

    //  Von der Filterleiste aufgerufen: „+ Ordner" für den geöffneten Ordner.
    //  Leerer Pfad = der geöffnete Ordner (das Modell setzt ihn selbst ein).
    function promptNewFolder(folderPath) {
        newFolderDialog.openFor(folderPath.length > 0 ? folderPath : mediaModel.folder)
    }

    //  Ein flacher Knopf für die Kopfzeile. Bewusst kein `Button` aus dem
    //  Stil-Modul: die Kopfzeile ist 30 px hoch und soll leicht wirken, nicht
    //  wie eine zweite Werkzeugleiste.
    component HeaderAction: Rectangle {
        id: ha
        property string label: ""
        signal triggered()
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        width: haText.implicitWidth + 16
        height: 22
        radius: 5
        color: haHover.hovered
               ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                         App.themeTextPrimary.b, 0.14)
               : "transparent"
        border.width: 1
        border.color: App.themeBorder
        Text {
            id: haText
            anchors.centerIn: parent
            text: ha.label
            color: App.themeTextMuted
            font.pixelSize: 11
        }
        HoverHandler { id: haHover }
        TapHandler { onTapped: ha.triggered() }
    }

    //  ── Pfeiltasten scrollen die ANSICHT ────────────────────────────────────
    //  Bewusst NICHT der eingebaute Tastenfluss des GridView: der bewegt
    //  `currentIndex`, also eine Auswahl - die Galerie hat keine. Gescrollt wird
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
    //  den Inhalt hinaus - oben steht ein leerer Streifen, und genau so weit
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
    //  Schrittweite: eine halbe Kachelreihe - ein ganzer Kachelsprung überspringt
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
    //  Listen-Darstellung (Player-Modus, s. GalleryPane): EINE Kachel je Zeile.
    //  Umgesetzt über die Zellengröße statt über ein zweites Raster - das
    //  Zeilenmodell rechnet seine Spaltenzahl ohnehin aus `contentWidth` und
    //  `cellWidth`, also ergibt eine zeilenbreite Zelle genau eine Liste.
    property bool listMode: false
    //  Optionen-Modus (Alt+S) der HÄLFTE - nicht appweit (s. GalleryPane).
    property bool optionsVisible: App.optionsVisible
    readonly property int tileW: root.listMode ? Math.max(200, root.areaW - spacing)
                                               : App.tileWidth
    readonly property int tileH: root.listMode ? 46 : App.tileHeight
    readonly property int cellW: tileW + spacing
    readonly property int cellH: tileH + spacing

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

    // ── Bänder (die helleren Flächen aufgeklappter Unterordner) ─────────────
    //  Ein Band liegt UNTER den Kacheln seines Ordners und deckt genau dessen
    //  Zeilen ab - kommt eine Datei dazu oder fällt weg, ändert sich einfach
    //  die Zeilenliste, und das Band wächst oder schrumpft mit.
    //
    //  Die Ebenen unterscheiden sich NUR über die Farbe, nicht über eine
    //  Einrückung: eingerückte Bänder müssten die Kachelzahl je Tiefe
    //  verringern, sonst ragte die letzte Kachel über den Rand hinaus.
    //  Sichtbar ist deshalb immer nur das INNERSTE Band einer Zeile - die
    //  äußeren liegen deckungsgleich darunter.
    //
    //  Richtung der Aufhellung hängt am Theme: auf dunklem Grund wird das Band
    //  heller, auf hellem Grund eine Spur dunkler. „Noch heller als weiß" gäbe
    //  es sonst nicht, und der Unterordner wäre im hellen Theme unsichtbar.
    readonly property bool darkTheme: (0.299 * App.themeBackground.r
                                     + 0.587 * App.themeBackground.g
                                     + 0.114 * App.themeBackground.b) < 0.5
    readonly property int  bandPad: 12       // Luft INNERHALB des Bandes
    readonly property int  bandRadius: 12
    //  Luft UNTERHALB eines endenden Bandes - sie gehört nicht mehr zum Band.
    //  Ohne sie klebte die nächste Kachelreihe des Elternordners direkt am
    //  Unterordner-Bereich (Nutzerbefund).
    readonly property int  bandGap: 14
    //  Einrueckung je Ebene. Sie zeigt auf den ersten Blick, dass der Inhalt zu
    //  einem Unterordner gehoert - und sie kostet die Zeile Platz, deshalb
    //  rechnet das Zeilenmodell die Kachelzahl je TIEFE (columnsForDepth).
    readonly property int  levelInset: 26
    //  DECKEND, nicht halbtransparent. Mit Alpha addieren sich der eckige
    //  Deckstreifen und die Fläche darunter zu einem sichtbar HELLEREN Streifen,
    //  und die abgedeckte Rundung blitzt trotzdem durch - beides war am
    //  Prüfstand deutlich zu sehen. Der Ton wird deshalb EINMAL über den
    //  Hintergrund gerechnet und dann deckend gemalt.
    function bandColor(level) {
        //  Ab der vierten Ebene bleibt es konstant - sonst liefe die Reihe in
        //  Weiß bzw. Schwarz aus und die Tiefen wären nicht mehr zu trennen.
        var l = Math.min(Math.max(level, 1), 4)
        var a = root.darkTheme ? 0.05 * l : 0.035 * l
        var t = root.darkTheme ? 1.0 : 0.0          // Ziel: Weiß bzw. Schwarz
        var bg = App.themeBackground
        return Qt.rgba(bg.r * (1 - a) + t * a,
                       bg.g * (1 - a) + t * a,
                       bg.b * (1 - a) + t * a, 1)
    }

    // ── Leerzustand ─────────────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: galleryModel.count === 0
        width: Math.min(root.width - 48, 640)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: App.currentFolder.length > 0
              ? App.uiText(App.language, "GalleryNoMedia")
              : App.uiText(App.language, "GalleryNoFolder")
        color: App.themeTextMuted
        font.pixelSize: 14
    }

    // ── Zeilenmodell ────────────────────────────────────────────────────────
    //  Rechnet aus Proxy-Reihenfolge, Spaltenzahl und Aufklapp-Zustand die
    //  sichtbaren Zeilen (s. src/media/GalleryRowModel.h).
    GalleryRowModel {
        id: rowModel
        source: galleryModel
        contentWidth: root.gridW
        cellWidth: root.cellW
        levelInset: root.levelInset
    }

    // ── Raster ──────────────────────────────────────────────────────────────
    //  Eine ListView über ZEILEN, kein GridView über Kacheln: ein GridView hat
    //  gleich hohe Zellen und kann eine Zeile nicht umbrechen - aufgeklappte
    //  Unterordner brauchen aber genau das. Das Delegate-Recycling bleibt
    //  erhalten (jetzt je Zeile), die RAM-Obergrenze also auch.
    ListView {
        id: grid
        //  Von aussen auffindbar (Pruefstand/Testtreiber): QML-`id`s sind aus
        //  C++ nicht erreichbar, ein Objektname schon.
        objectName: "galleryRows"
        y: 0
        x: root.gridX
        width: root.gridW
        height: root.height
        clip: true

        model: rowModel

        // RAM-Priorität bei zugleich glattem Scrollen: ~2 Zeilen Vorhalt.
        reuseItems: true
        cacheBuffer: root.cellH * 2
        boundsBehavior: Flickable.StopAtBounds
        //  KEIN Ziehen-zum-Scrollen: gescrollt wird per Rad, Bildlaufleiste und
        //  Pfeiltasten. Ein Zug mit der Maus gehört der KACHEL - sie zieht die
        //  Datei nach draußen (`Drag.Automatic`), und beides gleichzeitig ging
        //  nicht: die Ansicht riss den Griff an sich. Dasselbe Muster wie in
        //  `PdfSurface` (dort `pages`-ListView `interactive:false`).
        interactive: false

        //  ZURÜCK IN DEN GÜLTIGEN BEREICH, sobald sich Inhalt oder Geometrie
        //  ändern. `interactive: false` heißt: das Flickable holt sich NICHT
        //  selbst zurück (das täte sonst die Zug-Geste). Ohne diese Klemme blieb
        //  die Ansicht nach einer Suche, einem Ordnerwechsel oder einer
        //  Größenänderung außerhalb ihres Inhalts stehen - sichtbar als leerer
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
        // an den Rand des zentrierten Rasters. Sie bleibt funktional an die
        // ListView gebunden (Größe/Position), wird aber zu root umgehängt und dort
        // rechts verankert - Standardmuster für „Scrollbar am Container-Rand".
        ScrollBar.vertical: ScrollBar {
            id: vScroll
            parent: root
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            policy: ScrollBar.AsNeeded
        }

        delegate: Item {
            id: rowItem
            width: grid.width

            required property int    kind        // 0 = Kacheln, 1 = Kopfzeile
            required property string ownerName
            required property string ownerFolder
            required property int depth
            required property int firstIndex
            required property int tileCount
            required property int openMask
            required property int closeMask
            required property var tiles

            //  Luft am Anfang und Ende eines Bandes. Es genügt „irgendein Band
            //  beginnt/endet hier": beginnt ein äußeres, beginnt das innere
            //  zwangsläufig mit.
            readonly property int padTop:    (rowItem.openMask  !== 0) ? root.bandPad : 0
            readonly property int padBottom: (rowItem.closeMask !== 0) ? root.bandPad : 0
            //  Endet hier ein Band, kommt darunter ein Abstand - AUSSERHALB der
            //  Fläche, damit die nächste Reihe sichtbar zu einer anderen Ebene
            //  gehört.
            readonly property int gapBottom: (rowItem.closeMask !== 0) ? root.bandGap : 0

            height: (rowItem.kind === 1 ? root.headerHeight : root.cellH)
                    + rowItem.padTop + rowItem.padBottom + rowItem.gapBottom

            //  Die Flächen: je Ebene eine, von außen nach innen übereinander.
            //  Die äußeren sind vom innersten fast vollständig verdeckt - sie
            //  füllen genau die ABGERUNDETEN ECKEN, an denen sonst der
            //  Seitenhintergrund durchblitzte.
            //
            //  Gerundet ist jede Fläche immer; wo ihr Band weiterläuft, deckt
            //  ein eckiger Streifen die Rundung ab. (Einzelne Ecken zu runden
            //  gibt es erst ab Qt 6.7 - das Projekt baut ab 6.4.)
            Repeater {
                model: rowItem.depth

                delegate: Item {
                    id: bandItem
                    required property int index
                    readonly property int   level: bandItem.index + 1
                    readonly property bool  opens:  (rowItem.openMask  & (1 << bandItem.level)) !== 0
                    readonly property bool  closes: (rowItem.closeMask & (1 << bandItem.level)) !== 0
                    readonly property color tone:  root.bandColor(bandItem.level)
                    x: 0
                    y: 0
                    width: rowItem.width
                    //  Der Abstand unter einem endenden Band bleibt FREI.
                    height: rowItem.height - rowItem.gapBottom

                    Rectangle {
                        anchors.fill: parent
                        radius: root.bandRadius
                        color: bandItem.tone
                    }
                    Rectangle {
                        visible: !bandItem.opens
                        x: 0; y: 0
                        width: parent.width; height: root.bandRadius
                        color: bandItem.tone
                    }
                    Rectangle {
                        visible: !bandItem.closes
                        x: 0; y: parent.height - root.bandRadius
                        width: parent.width; height: root.bandRadius
                        color: bandItem.tone
                    }
                }
            }

            // ── Kopfzeile eines aufgeklappten Ordners ───────────────────────
            //  Name + eigene Aktionen. Was hier angelegt oder extrahiert wird,
            //  landet in DIESEM Ordner - man soll sehen und treffen können, wo
            //  man gerade arbeitet.
            Row {
                visible: rowItem.kind === 1
                x: rowItem.depth * root.levelInset + 4
                y: rowItem.padTop
                height: root.headerHeight
                spacing: 8

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.ownerName
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                    font.bold: true
                }
                //  Der Name ist zugleich der Weg hinein.
                HeaderAction {
                    label: App.uiText(App.language, "CtxFolderOpen")
                    onTriggered: root.folderOpenRequested(rowItem.ownerFolder)
                }
                HeaderAction {
                    label: "+ " + App.uiText(App.language, "CreateFileBtn")
                    onTriggered: root.createFileRequested(rowItem.ownerFolder)
                }
                HeaderAction {
                    label: App.uiText(App.language, "FolderNew")
                    onTriggered: newFolderDialog.openFor(rowItem.ownerFolder)
                }
                HeaderAction {
                    label: App.uiText(App.language, "FilterExtractBtn")
                    onTriggered: root.extractPagesRequested(rowItem.ownerFolder)
                }
            }

            // ── Die Kacheln dieser Zeile ────────────────────────────────────
            Repeater {
                model: rowItem.tileCount

                delegate: Item {
                    id: cell
                    required property int index

                    //  Die Kacheldaten kommen als Liste aus dem Zeilenmodell.
                    //  Bewusst über einen INDEX statt über `model: rowItem.tiles`:
                    //  ein Repeater baut bei jedem Modellwechsel alle Elemente
                    //  neu - jedes eintreffende Thumbnail zerstörte dann die
                    //  Kacheln seiner Zeile samt Hover-Zustand.
                    readonly property var d: (rowItem.tiles && cell.index < rowItem.tiles.length)
                                             ? rowItem.tiles[cell.index] : null
                    readonly property string filePath: cell.d ? cell.d.filePath : ""
                    readonly property int    mediaType: cell.d ? cell.d.mediaType : 6

                    //  Eingerueckt je Ebene - die Kachelzahl dieser Zeile hat
                    //  das Zeilenmodell bereits entsprechend verringert.
                    x: rowItem.depth * root.levelInset + cell.index * root.cellW
                    y: rowItem.padTop
                    width: root.cellW
                    height: root.cellH

                    // Pfad, für den aktuell ein Thumbnail angefordert ist.
                    property string requestedPath: ""

                    // Sichtbarkeitsgesteuerte Thumbnail-Anforderung mit Abbruch der
                    // zuvor angezeigten Datei (greift auch bei Delegate-Recycling).
                    function syncThumb() {
                        //  Ordner haben kein Thumbnail (sie zeichnen sich selbst).
                        if (cell.mediaType === 7) return
                        if (cell.requestedPath === cell.filePath) return
                        if (cell.requestedPath.length > 0)
                            mediaModel.cancelThumbnail(cell.requestedPath)
                        cell.requestedPath = cell.filePath
                        if (cell.filePath.length > 0)
                            mediaModel.ensureThumbnail(cell.filePath)
                    }

                    Component.onCompleted: syncThumb()
                    onFilePathChanged: syncThumb()

                    // Zielgrößen-Stufe der Thumbnails gewechselt (Kachelgröße): die
                    // Anforderung dieses Delegates neu stellen - syncThumbs Guard
                    // (requestedPath === filePath) würde sie sonst unterdrücken.
                    Connections {
                        target: mediaModel
                        function onThumbnailsInvalidated() {
                            cell.requestedPath = ""
                            cell.syncThumb()
                        }
                    }

                    //  ── Nachfassen, wenn eine Vorschau ausbleibt ──────────
                    //  Sicherheitsnetz, kein Hauptweg: die Kachel merkt sich
                    //  ihre Anforderung und fragt von sich aus NIE wieder. Geht
                    //  eine Anforderung unterwegs verloren - abbestellt von der
                    //  Kachel, die die Datei abgibt; gestellt, während das
                    //  Modell gerade neu aufbaut - bliebe die Kachel dauerhaft
                    //  ohne Bild, bis irgendetwas die Kacheln neu erzeugt. Der
                    //  Timer läuft NUR, solange diese Kachel noch keine Vorschau
                    //  hat, und hört von selbst auf; ein zweiter Ruf kostet im
                    //  Modell einen Hash-Zugriff (schon geliefert / schon in
                    //  Arbeit). Bei `thumbState === 2` (fehlgeschlagen) läuft er
                    //  bewusst nicht - dort gibt es nichts zu holen.
                    Timer {
                        interval: 1500
                        repeat: true
                        running: cell.filePath.length > 0 && cell.mediaType !== 7
                                 && cell.d !== null && cell.d.thumbState === 0
                        onTriggered: {
                            cell.requestedPath = ""
                            cell.syncThumb()
                        }
                    }

                    // Wird die Kachel zerstört (Ordner schrumpft o. Ä.), laufende
                    // Anforderung abbrechen.
                    Component.onDestruction: {
                        if (cell.requestedPath.length > 0)
                            mediaModel.cancelThumbnail(cell.requestedPath)
                    }

                    MediaTile {
                        anchors.centerIn: parent
                        width: root.tileW
                        height: root.tileH
                        listMode: root.listMode
                        optionsVisible: root.optionsVisible

                        filePath:    cell.filePath
                        displayName: cell.d ? cell.d.displayName : ""
                        mediaType:   cell.mediaType
                        typeLabel:   cell.d ? cell.d.typeLabel : ""
                        tags:        cell.d ? cell.d.tags : []
                        dateTime:    cell.d ? cell.d.dateTime : undefined
                        thumbUrl:    cell.d ? cell.d.thumbUrl : ""
                        thumbState:  cell.d ? cell.d.thumbState : 0
                        expanded:    cell.d ? cell.d.expanded : false
                        childCount:  cell.d && cell.d.childCount !== undefined
                                     ? cell.d.childCount : -1

                        tagMode: root.tagMode
                        modeTag: root.modeTag
                        covered: root.covered

                        onActivated: function(p) { root.activated(p) }
                        onFileClicked: function(p) { root.fileClicked(p) }
                        onFolderOpenRequested: function(p) { root.folderOpenRequested(p) }
                        onDragStartRequested: function(p, url) {
                            root.requestFileDrag(p, url)
                        }
                        onFolderRenameRequested: function(p, n) {
                            newFolderDialog.openRename(p, n)
                        }
                        onFolderDeleteRequested: function(p, n, c) {
                            folderDeleteDialog.askFor(p, n, c)
                        }
                        dropTarget: cell.mediaType === 7
                                    && root.hoverFolder.length > 0
                                    && root.hoverFolder === cell.filePath
                        onRenameRequested: function(p, n) {
                            fileRenameDialog.openFor(p, n)
                        }
                        //  „+ Neu…" aus den Untermenüs: anlegen UND dieser Datei
                        //  gleich zuweisen (derselbe Weg wie im Overlay-„+").
                        onNewTagRequested: function(p) {
                            newForFileDialog.openFor(p, true)
                        }
                        onNewCategoryRequested: function(p) {
                            newForFileDialog.openFor(p, false)
                        }
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
        }
    }

    // ── Neuer Tag / neue Kategorie FÜR EINE DATEI ───────────────────────────
    //  Aus dem Kontextmenü einer Kachel: den Namen abfragen, anlegen und die
    //  Datei sofort zuordnen - sonst müsste man erst im Panel erstellen und die
    //  Datei danach dort wiederfinden.
    Dialog {
        id: newForFileDialog
        property string filePath: ""
        property bool   forTag: true
        function openFor(path, tag) {
            filePath = path
            forTag = tag
            newForFileField.text = ""
            open(); newForFileField.forceActiveFocus()
        }
        function apply() {
            const v = newForFileField.text.trim()
            if (v.length > 0 && newForFileDialog.filePath.length > 0) {
                if (newForFileDialog.forTag) {
                    //  `addTag` registriert den Tag im Ordner und weist ihn zu.
                    mediaModel.addTag(newForFileDialog.filePath, v)
                } else {
                    const fn = newForFileDialog.filePath.substring(
                        Math.max(newForFileDialog.filePath.lastIndexOf("/"),
                                 newForFileDialog.filePath.lastIndexOf("\\")) + 1)
                    const id = Tags.addRootCategory(v, Qt.rgba(0, 0.7, 0.63, 1), false)
                    if (id && id.length > 0) Tags.toggleFileInCategory(id, fn)
                }
            }
            close()
        }
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
                text: newForFileDialog.forTag
                      ? App.uiText(App.language, "CatPanelNewTag")
                      : App.uiText(App.language, "CatPanelAddCategory")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            TextField {
                id: newForFileField
                width: 300
                color: App.themeTextPrimary
                onAccepted: newForFileDialog.apply()
            }
            Row {
                spacing: 8
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: newForFileDialog.close()
                }
                Button {
                    text: App.uiText(App.language, "SettingsOk")
                    onClicked: newForFileDialog.apply()
                }
            }
        }
    }

    // ── Datei umbenennen ────────────────────────────────────────────────────
    //  Bewusst ein eigener, winziger Dialog: der Ordner-Dialog spricht mit
    //  `createFolder`/`renameFolder` und hat dessen Fehlercodes; eine Datei
    //  geht über `renameItem` und kennt sie nicht.
    Dialog {
        id: fileRenameDialog
        property string filePath: ""
        function openFor(path, currentName) {
            filePath = path
            fileNameField.text = currentName
            open(); fileNameField.forceActiveFocus(); fileNameField.selectAll()
        }
        function apply() {
            const t = fileNameField.text.trim()
            if (t.length > 0 && fileRenameDialog.filePath.length > 0)
                mediaModel.renameItem(fileRenameDialog.filePath, t)
            close()
        }
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
                text: App.uiText(App.language, "CtxRenameFile")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            TextField {
                id: fileNameField
                width: 300
                color: App.themeTextPrimary
                onAccepted: fileRenameDialog.apply()
            }
            Row {
                spacing: 8
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: fileRenameDialog.close()
                }
                Button {
                    text: App.uiText(App.language, "SettingsOk")
                    onClicked: fileRenameDialog.apply()
                }
            }
        }
    }

    // ── Ordner anlegen / umbenennen (EIN Dialog für beides) ─────────────────
    //  Beides ist dieselbe Eingabe mit demselben Fehlerbild; ein zweiter Dialog
    //  wäre dieselbe Datei zweimal.
    Dialog {
        id: newFolderDialog
        property string parentFolder: ""     // anlegen
        property string renamePath: ""       // umbenennen (leer = anlegen)
        property string errorText: ""

        function openFor(folder) {
            parentFolder = folder; renamePath = ""; errorText = ""
            nameField.text = ""
            open(); nameField.forceActiveFocus()
        }
        function openRename(path, currentName) {
            parentFolder = ""; renamePath = path; errorText = ""
            nameField.text = currentName
            open(); nameField.forceActiveFocus(); nameField.selectAll()
        }
        function apply() {
            if (nameField.text.trim().length === 0) return
            var code = renamePath.length > 0
                       ? mediaModel.renameFolder(renamePath, nameField.text)
                       : mediaModel.createFolder(parentFolder, nameField.text)
            if (code === 0) { close(); return }
            errorText = code === 1 ? App.uiText(App.language, "FolderNameInvalid")
                      : code === 2 ? App.uiText(App.language, "FolderExists")
                                   : App.uiText(App.language, "FolderCreateFailed")
        }

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
                text: newFolderDialog.renamePath.length > 0
                      ? App.uiText(App.language, "FolderRenameTitle")
                      : App.uiText(App.language, "FolderNewTitle")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            TextField {
                id: nameField
                width: 300
                placeholderText: App.uiText(App.language, "FolderNamePlaceholder")
                onAccepted: newFolderDialog.apply()
                onTextChanged: newFolderDialog.errorText = ""
            }
            Text {
                width: 300
                visible: newFolderDialog.errorText.length > 0
                text: newFolderDialog.errorText
                color: "#e08080"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: newFolderDialog.close()
                }
                Button {
                    text: App.uiText(App.language, "SettingsOk")
                    enabled: nameField.text.trim().length > 0
                    onClicked: newFolderDialog.apply()
                }
            }
        }
    }

    // ── Ordner löschen (Papierkorb, mit Anzahl im Text) ─────────────────────
    Dialog {
        id: folderDeleteDialog
        property string targetPath: ""
        property string targetName: ""
        property int    targetCount: -1
        function askFor(path, name, count) {
            targetPath = path; targetName = name; targetCount = count
            open()
        }
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
                text: App.uiText(App.language, "DeleteFolderTitle")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 340
                text: App.uiText(App.language, "DeleteFolderText")
                          .arg(folderDeleteDialog.targetName)
                          .arg(Math.max(0, folderDeleteDialog.targetCount))
                color: App.themeTextMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: folderDeleteDialog.close()
                }
                Button {
                    text: App.uiText(App.language, "DeleteMediaConfirm")
                    onClicked: {
                        //  Ohne Papierkorb passiert NICHTS - ein Ordner kann
                        //  beliebig viel enthalten, und ohne Rückweg wäre das
                        //  die falsche Zusage (s. MediaModel::deleteFolder).
                        if (!mediaModel.deleteFolder(folderDeleteDialog.targetPath))
                            root.statusRequested(App.uiText(App.language, "DeleteFolderNoTrash"))
                        folderDeleteDialog.close()
                    }
                }
            }
        }
    }

    // ── Lösch-Bestätigung (EIN gemeinsamer Dialog für alle Kacheln) ──────────
    //  Verschiebt die Datei in den Papierkorb (mediaModel.deleteItem - räumt
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

    //  ── Mausrad WÄHREND eines Zuges ────────────────────────────────────────
    //  In QML ist da nichts zu holen: die Galerie hat einen Rad-Handler, der
    //  immer aktiv ist - während eines `QDrag` reicht Qt die Ereignisse aber
    //  nicht mehr an die Elemente durch. `App` sieht sie über einen Filter auf
    //  der Anwendung und meldet sie hier weiter (s. AppController::eventFilter).
    Connections {
        target: App
        function onDragWheel(angleDeltaY) {
            if (root.maxContentY <= root.minContentY) return
            //  Dieselbe Schrittweite wie beim gewöhnlichen Scrollen, aber OHNE
            //  Animation: während eines Zuges soll die Ansicht dem Rad direkt
            //  folgen und nicht nachschwingen.
            const step = (angleDeltaY / 120) * (grid.height * 0.45)
            grid.contentY = root.clampContentY(grid.contentY - step)
            const f = root.folderAtPoint(galleryDrop.lastX, galleryDrop.lastY)
            if (f !== root.hoverFolder) root.hoverFolder = f
        }
    }

    // ── Der Zug-Träger: EINER für die ganze Galerie ─────────────────────────
    //  `Drag.Automatic` + `Drag.active = true` ist der einzige Weg, der das
    //  Fenster verlässt (Dateimanager, Mail, Chat, Upload). Übergeben wird
    //  `text/uri-list` - die DATEI, wie aus einem Dateimanager.
    //  **Nur kopieren:** mit `MoveAction` dürfte ein Zielprogramm die Datei aus
    //  dem Ordner des Nutzers ENTFERNEN.
    //
    //  Er liegt HIER und nicht in der Kachel, weil `Drag.active = true` bis zum
    //  Loslassen blockiert: in der Kachel hinge deren JavaScript-Rahmen die
    //  ganze Zeit auf dem Stapel, und ein Scrollen während des Zuges räumte
    //  genau diese Kachel weg - die App stürzte ab.
    Item {
        id: dragPayload
        width: 1; height: 1
        visible: false
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction
    }

    //  Zwei Schritte: erst den Handler der Kachel verlassen (`Qt.callLater`),
    //  dann den blockierenden Zug starten. Sonst stünde die Kachel weiterhin
    //  auf dem Stapel und dürfte nicht weggeräumt werden.
    function requestFileDrag(filePath, thumbUrl) {
        if (filePath.length === 0) return
        Qt.callLater(root._runFileDrag, filePath, thumbUrl)
    }
    function _runFileDrag(filePath, thumbUrl) {
        dragPayload.Drag.mimeData = { "text/uri-list": App.fileUrl(filePath) }
        //  Am Zeiger hängt die Miniatur - sonst zieht man ins Blaue.
        dragPayload.Drag.imageSource = thumbUrl
        App.beginTileDrag()
        //  BLOCKIERT bis zum Ende des Zuges und setzt `active` danach selbst
        //  zurück; ein deklaratives `Drag.active: false` gehört NICHT daneben.
        dragPayload.Drag.active = true
        App.endTileDrag()
        galleryDrop.autoDir = 0
        autoScroll.stop()
        root.hoverFolder = ""
    }

    // ── Die Ablegefläche ────────────────────────────────────────────────────
    DropArea {
        id: galleryDrop
        anchors.fill: parent
        keys: ["text/uri-list"]

        //  NUR eine Property setzen, und nur wenn sie sich ändert - die
        //  Kacheln hängen ihre Hervorhebung daran. Kein Objekt je Kachel.
        //  ── Scrollen WÄHREND eines Zuges ────────────────────────────────
        //  Das Mausrad hilft hier nicht: solange ein Zug läuft, hält er den
        //  Griff, und Qt liefert keine Radereignisse an die Ansicht. Deshalb
        //  der übliche Weg - kommt der Zeiger an den oberen oder unteren Rand,
        //  scrollt die Ansicht von selbst weiter, damit man auch die Ordner
        //  darüber erreicht (Nutzerbefund).
        //  ── Randscrollen ────────────────────────────────────────────────
        //  Das ist der EINZIGE Weg, waehrend eines Zuges zu scrollen: unter
        //  Wayland gehoert der Zeiger dem Compositor, und Rad- wie
        //  Mausbewegungs-Ereignisse erreichen die Anwendung gar nicht.
        //  GEMESSEN (MG_DRAGLOG, echter Zug): waehrend des Zuges kamen 892
        //  `DragMove` an - aber KEIN einziges `Wheel` und kein `MouseMove`.
        //
        //  Die Zone ist grosszuegig, und das Tempo waechst mit der Naehe zum
        //  Rand: am aeussersten Rand rutscht die Ansicht spuerbar, kurz
        //  innerhalb der Zone kriecht sie. Mit fester Geschwindigkeit war der
        //  Effekt zu leicht zu uebersehen.
        readonly property int edgeZone: 72
        readonly property int minStep: 4
        readonly property int maxStep: 34
        property int  autoDir: 0         // −1 hoch · 0 aus · +1 runter
        property real autoStep: 0        // Pixel je Takt
        //  Letzte bekannte Zeigerposition: waehrend die Ansicht von selbst
        //  scrollt, bewegt sich der Zeiger nicht - das Ziel muss trotzdem
        //  laufend neu bestimmt werden, und zwar an SEINER Stelle.
        property real lastX: 0
        property real lastY: 0

        //  DIAGNOSE (nur mit MG_DRAGLOG=1, s. AppController): kommt der Zug hier
        //  ueberhaupt an, und laeuft das Randscrollen?
        readonly property bool logging: App.dragLogging
        property int moves: 0
        property int ticks: 0

        onPositionChanged: function(d) {
            galleryDrop.lastX = d.x
            galleryDrop.lastY = d.y
            if (galleryDrop.logging) {
                galleryDrop.moves++
                if (galleryDrop.moves % 20 === 1)
                    console.log("[MG_DRAGLOG] DropArea positionChanged #"
                                + galleryDrop.moves + " y=" + Math.round(d.y)
                                + " Hoehe=" + Math.round(root.height))
            }
            const f = root.folderAtPoint(d.x, d.y)
            if (f !== root.hoverFolder) root.hoverFolder = f

            //  Wie tief steckt der Zeiger in der Randzone? 0 = gerade eben
            //  drin, 1 = ganz am Rand.
            const near = galleryDrop.edgeZone
            let depth = 0
            if (d.y < near) {
                galleryDrop.autoDir = -1
                depth = (near - d.y) / near
            } else if (d.y > root.height - near) {
                galleryDrop.autoDir = +1
                depth = (d.y - (root.height - near)) / near
            } else {
                galleryDrop.autoDir = 0
            }
            galleryDrop.autoStep = galleryDrop.minStep
                                 + (galleryDrop.maxStep - galleryDrop.minStep)
                                   * Math.max(0, Math.min(1, depth))
            if (galleryDrop.logging && galleryDrop.autoDir !== 0
                && galleryDrop.moves % 20 === 1)
                console.log("[MG_DRAGLOG] Randzone: Richtung "
                            + galleryDrop.autoDir + ", Schritt "
                            + Math.round(galleryDrop.autoStep))
        }
        //  Der Takt laeuft waehrend des GANZEN Zuges. Ihn bei jedem Wackeln
        //  ueber die Zonengrenze zu stoppen und neu zu starten (so war es
        //  zuerst) machte das Scrollen ruckelig; ein 16-ms-Timer, der meistens
        //  nichts tut, kostet dagegen nichts.
        onEntered: {
            galleryDrop.autoDir = 0
            autoScroll.start()
        }
        onExited: {
            root.hoverFolder = ""
            galleryDrop.autoDir = 0
            autoScroll.stop()
        }

        //  Ein Takt statt einer Animation: die Richtung kann sich bei jeder
        //  Bewegung ändern, und die Ansicht soll dabei nicht nachschwingen.
        Timer {
            id: autoScroll
            interval: 16
            repeat: true
            onTriggered: {
                if (galleryDrop.autoDir === 0) return        // laeuft weiter, tut nichts
                const v = root.clampContentY(
                    grid.contentY + galleryDrop.autoDir * galleryDrop.autoStep)
                if (galleryDrop.logging) {
                    galleryDrop.ticks++
                    if (galleryDrop.ticks % 20 === 1)
                        console.log("[MG_DRAGLOG] Scroll-Takt #" + galleryDrop.ticks
                                    + " contentY " + Math.round(grid.contentY)
                                    + " -> " + Math.round(v)
                                    + " (Grenzen " + Math.round(root.minContentY)
                                    + ".." + Math.round(root.maxContentY) + ")")
                }
                if (Math.abs(v - grid.contentY) < 0.5) return   // am Anschlag
                grid.contentY = v
                //  Unter dem Zeiger liegt jetzt ein anderer Ordner - bestimmt
                //  an seiner ZULETZT BEKANNTEN Stelle, nicht in der Mitte.
                const f = root.folderAtPoint(galleryDrop.lastX, galleryDrop.lastY)
                if (f !== root.hoverFolder) root.hoverFolder = f
            }
        }

        onDropped: function(drop) {
            autoScroll.stop()
            galleryDrop.autoDir = 0
            const target = root.folderAtPoint(drop.x, drop.y)
            root.hoverFolder = ""
            if (!drop.hasUrls || target.length === 0) { drop.accepted = false; return }

            const src = App.localPath(drop.urls[0])
            if (mediaModel.ownsFile(src)) {
                //  Ein app-interner Zug: verschieben/kopieren. In den EIGENEN
                //  Ordner abzulegen ist keine Bewegung - das Modell meldete
                //  sonst „nicht möglich" und die Shell zeigte einen Fehler.
                if (root._parentOf(src) !== target)
                    root.folderDropRequested(src, target)
            } else {
                root.externalDropRequested(drop.urls, target)
            }
            drop.acceptProposedAction()
        }
    }

    // ── Mausrad: Strg = Zoom (Kachelgröße), sonst weiches Scrollen ───────────
    //  Eine MouseArea(NoButton) fängt die Wheel-Events zuverlässig ab - ein
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
