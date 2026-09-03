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
    //  „Ton als Audiodatei sichern" aus dem Kontextmenü einer Videokachel.
    signal audioExtractRequested(string filePath)
    //  Aktionen aus der Kopfzeile eines aufgeklappten Bereichs - sie zielen auf
    //  DIESEN Ordner, nicht auf den geoeffneten (Festlegung des Nutzers).
    signal createFileRequested(string folderPath)
    signal extractPagesRequested(string folderPath)
    //  Meldung an die Shell (sie führt die Statuszeile).
    signal statusRequested(string text)
    //  Eine oder MEHRERE Dateien wurden auf eine Ordnerkachel gezogen. Die
    //  Shell entscheidet (Verschieben/Kopieren, Namenskollision) - sie hat den
    //  Dialog dafür schon für die Lesezeichen-Leiste.
    signal folderDropRequested(var sourcePaths, string folderPath)
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

    // ── Mehrfachauswahl ─────────────────────────────────────────────────────
    //  Der ZUSTAND liegt im Modell (`mediaModel.selected*`); hier steht nur, was
    //  die ANSICHTS-Reihenfolge braucht - ein Umschalt-Bereich meint „von der
    //  zuletzt angefassten Kachel bis zu dieser", und das sind Proxy-Zeilen.
    //
    //  `selAnchor` ist die zuletzt mit Strg oder Umschalt angefasste Zeile. Ohne
    //  ihn haette ein Umschalt-Klick keinen Anfang; er verhaelt sich dann wie
    //  ein Strg-Klick und setzt den Anker selbst.
    property int selAnchor: -1

    function selectFromTile(proxyRow, mods) {
        if (proxyRow < 0) return
        //  Wer in die Galerie klickt, meint die Galerie - ab jetzt gehoeren ihr
        //  die Tasten. Ohne das behielt den Fokus, was ihn zuletzt hatte (das
        //  Suchfeld der Filterleiste, ein geschlossener Dialog, die andere
        //  Haelfte), und `Entf` lief dorthin statt in die Auswahl. Am
        //  Pruefstand war der Fehler nicht zu sehen: dort hatte die Ansicht den
        //  Fokus ohnehin (`bench_shell del2`). Kostet nichts, wenn er schon da
        //  ist, und ist das Verhalten jedes Dateimanagers.
        root.forceActiveFocus()
        const shift = (mods & Qt.ShiftModifier) !== 0
        const ctrl  = (mods & Qt.ControlModifier) !== 0
        if (shift && root.selAnchor >= 0) {
            //  Strg zusaetzlich = den Bereich HINZUnehmen, ohne das Bisherige
            //  wegzuwerfen (wie im Dateimanager).
            galleryModel.selectRange(root.selAnchor, proxyRow, ctrl)
            return
        }
        const path = galleryModel.filePathAt(proxyRow)
        if (path.length === 0) return
        mediaModel.toggleSelected(path)
        root.selAnchor = proxyRow
    }

    //  Strg+A / Esc - aus den Tastenkuerzeln der Hälfte (s. GalleryPane).
    function selectAll() {
        galleryModel.selectAllVisible()
        root.selAnchor = galleryModel.count > 0 ? 0 : -1
    }
    function clearSelection() {
        mediaModel.clearSelection()
        root.selAnchor = -1
    }

    //  In die Zwischenablage: die ganze Auswahl, wenn `path` dazugehoert und
    //  mehr als eines gewaehlt ist - sonst genau diese eine Datei. Ordner
    //  bleiben aussen vor (`filesOnly`), sie sind hier nicht kopierbar.
    function copyToClipboard(path) {
        const many = mediaModel.selectionCount > 1 && mediaModel.isSelected(path)
        const list = many ? galleryModel.selectedPaths(true)
                          : (path.length > 0 ? [path] : [])
        const n = App.copyFilesToClipboard(list)
        if (n > 0)
            root.statusRequested(App.uiText(App.language, "SelCopied")
                                 .replace("%1", n))
    }

    //  Der Ordner, in dem eine Datei liegt - rein textuell, ohne Dateisystem.
    //  Gehört die Datei IRGENDEINER Hälfte dieser App? Für `mediaModel` ist eine
    //  Datei aus der ANDEREN Hälfte fremd - app-intern ist der Zug trotzdem, und
    //  er soll deshalb der Einstellung „Verschieben statt Kopieren" folgen statt
    //  blind zu kopieren. Dieselbe Frage beantwortet die Shell mit
    //  `_modelOwning`; hier steht sie, weil hier die Entscheidung fällt.
    function _appOwnsFile(path) {
        if (mediaModel.ownsFile(path)) return true
        const list = App.panes
        for (var i = 0; i < list.length; ++i) {
            const m = list[i] ? list[i].mediaModel : null
            if (m && m.ownsFile(path)) return true
        }
        return false
    }

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
    //  Die Sammel-Löschung anstoßen. EIN Weg für beide Auslöser (Kontextmenü
    //  der Kachel und die Entf-Taste der Hälfte) - der Dialog gehört dieser
    //  Datei, und zwei Öffnungswege wären zwei Stellen, an denen die Rückfrage
    //  vergessen werden kann. Ohne Auswahl passiert NICHTS; gelöscht wird nie
    //  ohne Nachfrage.
    function requestDeleteSelection() {
        const n = mediaModel.selectionCount
        if (n <= 0) return false
        selDeleteDialog.count = n
        selDeleteDialog.open()
        return true
    }

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
    //  Esc hebt die Mehrfachauswahl auf. Bewusst ein TASTENereignis und kein
    //  `Shortcut`: ein Kuerzel wird vor den Tastenereignissen ausgewertet und
    //  haette einem offenen Dialog sein Escape weggenommen. Hier greift es nur,
    //  solange die Galerie selbst den Tastaturfokus hat.
    Keys.onEscapePressed: function(event) {
        if (root._editableTextFocused()) return
        if (mediaModel.selectionCount === 0) return
        root.clearSelection()
        event.accepted = true
    }
    Keys.onPressed: function(event) {
        if (root._editableTextFocused()) return
        //  Entf löscht die AUSWAHL - mit derselben Rückfrage wie das
        //  Kontextmenü; ohne Auswahl bleibt sie folgenlos.
        //
        //  BEWUSST HIER und nicht als `Shortcut` in der Hälfte: ein Kürzel
        //  erreicht die Landkarte nur über ein aktives Fenster und einen
        //  Fokus, der bei keinem Popup liegt. Am Prüfstand gemessen
        //  (`bench_shell del`): `Strg+A` kam an, `Entf` NICHT - auch nachdem
        //  der offene Dialog geschlossen war und der Fokus wieder auf der
        //  Hälfte lag. Die Tastenbehandlung der Ansicht ist der Weg, den die
        //  Galerie ohnehin nimmt (PageUp/PageDown/Pos1/Ende stehen daneben).
        if (event.key === Qt.Key_Delete) {
            if (root.requestDeleteSelection()) event.accepted = true
            return
        }
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

    //  WELCHER Tag-Controller? Der der EIGENEN Haelfte - `Tags` folgt dem
    //  Fokus und damit dem Mauszeiger (s. `TagCategoryPanel` ▸ `tagsCtl`).
    //  `GalleryPane` setzt ihn auf `PaneCtl.tags`.
    property var tagsCtl: Tags

    //  Ein Tag-Modus ist EINE Bedienung: der Nutzer klickt Kachel um Kachel
    //  an, bis er „Fertig" drueckt. Alles darin ist EIN Rueckgaengig-Schritt
    //  in der Tag-Seitenleiste (Nutzerbefund 2026-09-03: drei angeklickte
    //  Dateien brauchten drei Klicks auf „Rueckgaengig"). Die Gruppe ist
    //  LAZY - wer nur hineinschaut und wieder herausgeht, hinterlaesst nichts.
    function enterGroupMode(tag)     { root._startTagMode(tag, 1) }
    function enterAddToTagMode(tag)  { root._startTagMode(tag, 2) }
    function exitModes() {
        if (root.tagMode !== 0) root.tagsCtl.endUndoGroup()
        modeTag = ""; tagMode = 0
    }
    function _startTagMode(tag, mode) {
        root.exitModes()                 // ein laufender Modus wird sauber beendet
        root.tagsCtl.beginTagModeGroup(tag)
        modeTag = tag; tagMode = mode
    }

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
    //  Zeilenhöhe aus den Einstellungen statt fest 46 - einstellbar über
    //  `Strg` + `+`/`-`, `Strg` + Rad und den Reiter Ansicht.
    readonly property int tileH: root.listMode ? App.listRowHeight : App.tileHeight
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
    // ── Auswahlrahmen (Gummiband) ───────────────────────────────────────────
    //  Ein Zug auf LEERER Fläche zieht einen Rahmen; was er überdeckt, wird
    //  ausgewählt. Die Fläche liegt VOR der ListView im Baum und damit unter
    //  ihr: eine Kachel nimmt den Druck zuerst (sie zieht die Datei hinaus),
    //  freier Raum fällt hierher durch. Ein `z` bräuchte es dafür nicht - die
    //  Reihenfolge im Baum entscheidet.
    //
    //  ES WIRD NICHT MITGESCROLLT. Der Rahmen wirkt auf das, was zu sehen ist;
    //  ein Rand-Scrollen wie beim Ziehen einer Datei würde Kacheln unter dem
    //  Rahmen hindurchlaufen lassen, und was oben hinausrutscht, wäre weder
    //  sichtbar gewählt noch sichtbar abgewählt. Die Grenze steht in
    //  LIMITATIONS.md.
    property bool bandActive: false
    //  Inhalts-Koordinaten (also mitsamt `contentY`), damit ein Rahmen einen
    //  Scroll zwischendurch unbeschadet übersteht.
    property real bandAx: 0
    property real bandAy: 0
    property real bandBx: 0
    property real bandBy: 0
    readonly property real _bandL: Math.min(root.bandAx, root.bandBx)
    readonly property real _bandR: Math.max(root.bandAx, root.bandBx)
    readonly property real _bandT: Math.min(root.bandAy, root.bandBy)
    readonly property real _bandB: Math.max(root.bandAy, root.bandBy)

    //  Welche PROXY-Bereiche überdeckt der Rahmen? Ergebnis ist eine flache
    //  Liste [a0,b0,a1,b1,…] - das Proxy-Modell setzt daraus die Auswahl.
    //
    //  Gelaufen wird nur über die AUSGELEGTEN Zeilen (`itemAtIndex` liefert für
    //  alles andere null): mehr als das Sichtbare kann der Rahmen ohnehin nicht
    //  meinen, und ein Lauf über alle Zeilen wäre bei 78.000 Dateien je
    //  Mausbewegung ein paar tausend Schritte.
    function bandRanges() {
        const out = []
        if (rowModel.count === 0) return out
        let r = grid.indexAt(1, Math.max(0, grid.contentY + 1))
        if (r < 0) r = 0
        for (; r < rowModel.count; ++r) {
            const it = grid.itemAtIndex(r)
            if (!it) break                       // ab hier ist nichts ausgelegt
            if (it.y > root._bandB) break        // der Rahmen endet darüber
            if (it.y + it.height < root._bandT) continue
            const info = rowModel.rowInfo(r)
            if (!info || info.kind !== 0 || info.count <= 0) continue
            //  Innerhalb der Zeile liegen die Kacheln unter der Bandpolsterung.
            const padTop = (info.openMask !== 0) ? root.bandPad : 0
            const top    = it.y + padTop
            if (top > root._bandB || top + root.cellH < root._bandT) continue
            //  Waagerecht entscheidet die Spalte - dieselbe Rechnung wie beim
            //  Ablegen (`folderAtPoint`), damit beide dasselbe Raster sehen.
            const inset = info.depth * root.levelInset
            const relL  = root._bandL - inset
            const relR  = root._bandR - inset
            if (relR < 0 || relL > info.count * root.cellW) continue
            const c0 = Math.max(0, Math.floor(relL / root.cellW))
            const c1 = Math.min(info.count - 1, Math.floor(relR / root.cellW))
            if (c1 < c0) continue
            out.push(info.first + c0, info.first + c1)
        }
        return out
    }

    MouseArea {
        id: bandArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        //  Der Rahmen beginnt erst nach einer Schwelle - ein einfacher Klick auf
        //  freie Fläche soll die Auswahl aufheben, nicht einen 2-px-Rahmen
        //  ziehen (dieselbe Lehre wie beim Herausziehen einer Kachel).
        property real pressX: 0
        property real pressY: 0
        property bool armed: false
        property bool additive: false

        onPressed: function(mouse) {
            root.forceActiveFocus()          // s. selectFromTile
            bandArea.pressX  = mouse.x - grid.x
            bandArea.pressY  = mouse.y - grid.y + grid.contentY
            bandArea.additive = (mouse.modifiers & Qt.ControlModifier) !== 0
            bandArea.armed   = true
            root.bandActive  = false
        }
        onPositionChanged: function(mouse) {
            if (!bandArea.armed) return
            const cx = mouse.x - grid.x
            const cy = mouse.y - grid.y + grid.contentY
            if (!root.bandActive) {
                if (Math.abs(cx - bandArea.pressX) < 8
                    && Math.abs(cy - bandArea.pressY) < 8)
                    return                        // noch ein Klick, kein Zug
                root.bandActive = true
                root.bandAx = bandArea.pressX
                root.bandAy = bandArea.pressY
                galleryModel.beginBand(bandArea.additive)
            }
            root.bandBx = cx
            root.bandBy = cy
            galleryModel.updateBand(root.bandRanges())
        }
        onReleased: {
            bandArea.armed = false
            if (root.bandActive) {
                galleryModel.endBand()
                root.bandActive = false
                //  Der Anker fuer einen spaeteren Umschalt-Klick ist die Zeile,
                //  an der der Rahmen begann.
                root.selAnchor = -1
            } else {
                //  Ein Klick ins Leere hebt die Auswahl auf.
                root.clearSelection()
            }
        }
        onCanceled: {
            bandArea.armed = false
            if (root.bandActive) { galleryModel.endBand(); root.bandActive = false }
        }
    }

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

                    //  Eingerueckt je Ebene.
                    x: rowItem.depth * root.levelInset + cell.index * root.cellW
                    y: rowItem.padTop
                    //  Im LISTEN-Modus füllt EINE Kachel die Zeile - sie zieht
                    //  deshalb die Einrückung von ihrer Länge ab, sonst liefe
                    //  sie rechts aus dem Fenster (Nutzerbefund).
                    //  ABGEZOGEN WIRD NUR DIE LINKE Einrückung: die rechte Kante
                    //  bleibt damit auf ALLEN Ebenen dieselbe (Festlegung des
                    //  Nutzers - „rechts soll es einheitlich sein"). Zöge man
                    //  links UND rechts ab, würde jede Ebene zusätzlich kürzer,
                    //  und die rechten Kanten stünden treppenförmig versetzt.
                    //  Im Kachel-Modus ändert sich nichts: dort haben Kacheln
                    //  eine feste Größe, und die Einrückung steckt bereits in der
                    //  Spaltenzahl (`GalleryRowModel::columnsForDepth`).
                    width: root.listMode
                           ? Math.max(120, root.cellW - rowItem.depth * root.levelInset)
                           : root.cellW
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
                        //  Im LISTEN-Modus füllt die Kachel ihre Zelle - und die
                        //  ist bei einem aufgeklappten Unterordner schmaler
                        //  (s. `cell.width`). Die feste Zeilenbreite `root.tileW`
                        //  hob die Einrückung wieder auf: die Kachel wurde
                        //  mittig in die eingerückte Zelle gesetzt und landete
                        //  dadurch bei JEDER Ebene an derselben Stelle
                        //  (gemessen: x=16, rechts=984 für Ebene 0, 1 und 2).
                        width: root.listMode
                               ? Math.max(120, cell.width - root.spacing)
                               : root.tileW
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

                        proxyRow: cell.d && cell.d.row !== undefined ? cell.d.row : -1
                        //  Der Auswahlzustand kommt NICHT aus den Kacheldaten,
                        //  sondern direkt aus dem Modell: `selectionRevision`
                        //  ist die Abhaengigkeit, `isSelected` die Antwort.
                        //  Ueber die Kacheldaten kostete jede Mausbewegung des
                        //  Auswahlrahmens den vollen Umbau aller sichtbaren
                        //  Kacheln - gemessen 1145 statt 60 µs je Bewegung.
                        selected: mediaModel.selectionRevision >= 0
                                  && mediaModel.isSelected(cell.filePath)

                        onSelectRequested: function(row, mods) {
                            root.selectFromTile(row, mods)
                        }
                        onSelectionResetRequested: mediaModel.clearSelection()
                        onCopyRequested: function(p) { root.copyToClipboard(p) }
                        //  Die Zahl aus dem Signal wird nicht gebraucht - die
                        //  Auswahl kennt sie selbst, und EINE Quelle ist besser
                        //  als zwei, die auseinanderlaufen koennen.
                        onDeleteSelectionRequested: root.requestDeleteSelection()

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
                        onAudioExtractRequested: function(p) {
                            root.audioExtractRequested(p)
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
        //  **Eingabetaste bestaetigt** - s. `companionDialog`.
        focus: true
        onAccepted: {
            //  Ohne Papierkorb passiert NICHTS - ein Ordner kann beliebig viel
            //  enthalten, und ohne Rückweg wäre das die falsche Zusage
            //  (s. MediaModel::deleteFolder).
            if (!mediaModel.deleteFolder(folderDeleteDialog.targetPath))
                root.statusRequested(App.uiText(App.language, "DeleteFolderNoTrash"))
        }
        padding: 18
        background: Rectangle {
            color: App.themeCard; radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { folderDeleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { folderDeleteDialog.accept(); e.accepted = true }
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
                    onClicked: folderDeleteDialog.accept()
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
        //  **Eingabetaste bestaetigt.** Die Tat steht in `onAccepted`, die
        //  Knoepfe rufen nur `accept()` - so gibt es EINEN Weg zum Loeschen,
        //  gleich ob geklickt oder `Enter` gedrueckt wird (Wunsch des Nutzers:
        //  fuer JEDE Loeschrueckfrage, nicht nur die der Entf-Taste).
        //  `focus: true` ist Pflicht: ohne Tastaturfokus kommt `Return` beim
        //  Dialog gar nicht an. `Esc` schliesst ihn ohnehin (`closePolicy`).
        //  **`Return` haengt am `contentItem`, NICHT am Dialog** (s. unten):
        //  ein `Dialog` wertet die Taste nur mit `standardButtons` aus, und ein
        //  `Keys`-Handler am Popup selbst feuert nicht. Beides gemessen
        //  (`bench_shell del2`): `Esc` schloss den Dialog, `Enter` blieb
        //  wirkungslos - die Taste KAM an, nur niemand nahm sie.
        focus: true
        onAccepted: mediaModel.removeCompanion(companionDialog.targetPath,
                                               companionDialog.kind)
        padding: 18
        background: Rectangle {
            color: App.themeCard; radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { companionDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { companionDialog.accept(); e.accepted = true }
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
                    TapHandler { onTapped: companionDialog.accept() }
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
        //  **Eingabetaste bestaetigt.** Die Tat steht in `onAccepted`, die
        //  Knoepfe rufen nur `accept()` - so gibt es EINEN Weg zum Loeschen,
        //  gleich ob geklickt oder `Enter` gedrueckt wird (Wunsch des Nutzers:
        //  fuer JEDE Loeschrueckfrage, nicht nur die der Entf-Taste).
        //  `focus: true` ist Pflicht: ohne Tastaturfokus kommt `Return` beim
        //  Dialog gar nicht an. `Esc` schliesst ihn ohnehin (`closePolicy`).
        //  **`Return` haengt am `contentItem`, NICHT am Dialog** (s. unten):
        //  ein `Dialog` wertet die Taste nur mit `standardButtons` aus, und ein
        //  `Keys`-Handler am Popup selbst feuert nicht. Beides gemessen
        //  (`bench_shell del2`): `Esc` schloss den Dialog, `Enter` blieb
        //  wirkungslos - die Taste KAM an, nur niemand nahm sie.
        focus: true
        onAccepted: {
            mediaModel.deleteItem(deleteDialog.targetPath)
            root.forceActiveFocus()          // s. selDeleteDialog
        }
        padding: 18
        background: Rectangle {
            color: App.themeCard
            radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { deleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { deleteDialog.accept(); e.accepted = true }
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
                    TapHandler { onTapped: deleteDialog.accept() }
                }
            }
        }
    }

    //  ── Sammel-Löschung (Mehrfachauswahl) ──────────────────────────────────
    //  EINE Rückfrage für alles Gewählte, und der Vorgang landet als EIN Schritt
    //  auf dem Rückgängig-Stapel (s. `MediaModel::deleteSelected`) - `Strg+Z`
    //  holt die ganze Gruppe zurück, nicht Datei für Datei.
    Dialog {
        id: selDeleteDialog
        property int count: 0
        anchors.centerIn: parent
        modal: true
        //  **Eingabetaste bestaetigt.** Die Tat steht in `onAccepted`, die
        //  Knoepfe rufen nur `accept()` - so gibt es EINEN Weg zum Loeschen,
        //  gleich ob geklickt oder `Enter` gedrueckt wird (Wunsch des Nutzers:
        //  fuer JEDE Loeschrueckfrage, nicht nur die der Entf-Taste).
        //  `focus: true` ist Pflicht: ohne Tastaturfokus kommt `Return` beim
        //  Dialog gar nicht an. `Esc` schliesst ihn ohnehin (`closePolicy`).
        //  **`Return` haengt am `contentItem`, NICHT am Dialog** (s. unten):
        //  ein `Dialog` wertet die Taste nur mit `standardButtons` aus, und ein
        //  `Keys`-Handler am Popup selbst feuert nicht. Beides gemessen
        //  (`bench_shell del2`): `Esc` schloss den Dialog, `Enter` blieb
        //  wirkungslos - die Taste KAM an, nur niemand nahm sie.
        focus: true
        onAccepted: {
            const n = mediaModel.deleteSelected()
            root.selAnchor = -1
            //  Fokus zurueck an die Galerie: sonst liegt er nach dem Dialog
            //  anderswo, und `Strg+Z` kaeme nicht an (vom Nutzer gemeldet; am
            //  Pruefstand ohne Dialog liess sich der Fall nicht nachstellen).
            root.forceActiveFocus()
            if (n > 0)
                root.statusRequested(
                    App.uiText(App.language, "SelDeleted").replace("%1", n))
        }
        padding: 18
        background: Rectangle {
            color: App.themeCard
            radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { selDeleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { selDeleteDialog.accept(); e.accepted = true }
            spacing: 10
            Text {
                text: App.uiText(App.language, "SelDeleteTitle")
                color: App.themeTextPrimary
                font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 320
                text: App.uiText(App.language, "SelDeleteText")
                          .replace("%1", selDeleteDialog.count)
                color: App.themeTextMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Rectangle {
                    width: selCancelLbl.implicitWidth + 24; height: 30; radius: 6
                    color: selCancelHover.hovered
                           ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                           : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                    border.color: App.themeBorder; border.width: 1
                    Text { id: selCancelLbl; anchors.centerIn: parent
                           text: App.uiText(App.language, "SettingsCancel")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: selCancelHover }
                    TapHandler { onTapped: selDeleteDialog.close() }
                }
                Rectangle {
                    width: selDelLbl.implicitWidth + 24; height: 30; radius: 6
                    color: selDelHover.hovered ? Qt.rgba(0.88, 0.35, 0.35, 0.30)
                                               : Qt.rgba(0.88, 0.35, 0.35, 0.16)
                    border.color: "#c25a5a"; border.width: 1
                    Text { id: selDelLbl; anchors.centerIn: parent
                           text: App.uiText(App.language, "DeleteMediaConfirm")
                           color: "#e08080"; font.pixelSize: 12 }
                    HoverHandler { id: selDelHover }
                    TapHandler { onTapped: selDeleteDialog.accept() }
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
        //  Gehoert die angefasste Kachel zur Auswahl, wandert die GANZE Auswahl
        //  mit - sonst nur sie selbst (genau wie in einem Dateimanager). Ordner
        //  bleiben aussen vor: ein Ordner-Zug ins Fremdprogramm ist eine eigene
        //  Zusage, keine Nebenwirkung.
        const many = mediaModel.selectionCount > 1 && mediaModel.isSelected(filePath)
        const list = many ? galleryModel.selectedPaths(true) : [filePath]
        if (list.length === 0) return
        Qt.callLater(root._runFileDrag, list, thumbUrl)
    }
    //  `text/uri-list` traegt MEHRERE Adressen, je eine Zeile (RFC 2483: CRLF) -
    //  so liest es jeder Dateimanager, und so liest es auch `drop.urls` auf dem
    //  Rueckweg ins eigene Fenster. Eigene Funktion, damit der Prüfstand sie
    //  aufrufen kann, ohne einen echten Zug zu starten (der blockiert bis zum
    //  Loslassen) - s. tests/media/tst_bandselect.
    function dragUriList(paths) {
        const uris = []
        for (var i = 0; i < paths.length; ++i) {
            if (!paths[i] || paths[i].length === 0) continue
            uris.push(App.fileUrl(paths[i]))
        }
        return uris.join("\r\n")
    }
    function _runFileDrag(paths, thumbUrl) {
        const filePath = paths[0]
        dragPayload.Drag.mimeData = { "text/uri-list": root.dragUriList(paths) }
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

            //  Ein Zug kann MEHRERE Dateien tragen (Mehrfachauswahl). Ob er
            //  aus der App kommt, entscheidet die erste Adresse - gemischt
            //  kommt eine Nutzlast nicht vor.
            const src = App.localPath(drop.urls[0])
            if (root._appOwnsFile(src)) {
                //  Ein app-interner Zug: verschieben/kopieren. In den EIGENEN
                //  Ordner abzulegen ist keine Bewegung - das Modell meldete
                //  sonst „nicht möglich" und die Shell zeigte einen Fehler.
                const moves = []
                for (var i = 0; i < drop.urls.length; ++i) {
                    const p = App.localPath(drop.urls[i])
                    if (p.length > 0 && root._parentOf(p) !== target) moves.push(p)
                }
                if (moves.length > 0)
                    root.folderDropRequested(moves, target)
            } else {
                root.externalDropRequested(drop.urls, target)
            }
            drop.acceptProposedAction()
        }
    }

    //  Der sichtbare Rahmen. Reine Anzeige - er faengt nichts ab (kein
    //  MouseArea darin), sonst naehme er dem Zug seine eigenen Ereignisse.
    Rectangle {
        visible: root.bandActive
        z: 3
        x: grid.x + root._bandL
        y: grid.y + root._bandT - grid.contentY
        width:  Math.max(0, root._bandR - root._bandL)
        height: Math.max(0, root._bandB - root._bandT)
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.16)
        border.color: App.themeAccent
        border.width: 1
        radius: 2
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
                //  In der LISTEN-Darstellung wächst die ZEILENHÖHE, nicht die
                //  Kachel: sonst tat sich beim Zoomen sichtbar nichts, und die
                //  verstellte Kachelgröße schlug erst beim Umschalten durch
                //  (vom Nutzer gemeldet). Schrittweite 4 statt 16 - eine Zeile
                //  startet bei 46 px, eine Kachel bei 200.
                if (wheel.angleDelta.y > 0)
                    root.listMode ? App.zoomInList(4)  : App.zoomIn(16)
                else if (wheel.angleDelta.y < 0)
                    root.listMode ? App.zoomOutList(4) : App.zoomOut(16)
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
