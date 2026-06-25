pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Pdf
import QtMultimedia
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  PdfSurface.qml — PDF-Anzeige in 100% QML (ersetzt PdfViewer(QWidget)).
//
//  Eigener vertikaler ListView aus PdfPageImage (kein PdfMultiPageView) für volle
//  Kontrolle über Seitengeometrie und das Annotations-Overlay (asynchron via
//  Viewer.requestPdfAnnotations).
//
//  Caching-Strategie (RAM-bewusst, beide Hebel deterministisch deckelbar):
//   • SCROLLEN: Die Seiten-ListView haelt ueber `pageCacheScreens` Viewporthoehen
//     Puffer je Richtung instanziiert+gerendert. Hoch-/Runterscrollen innerhalb
//     dieses Fensters laedt NICHT neu. Da QtQuick seinen internen Bild-Cache nicht
//     oeffentlich deckeln laesst, ist dieser delegate-basierte Puffer der einzige
//     wirklich kontrollierbare RAM-Deckel — wir nutzen bewusst IHN (cache:false).
//   • PDF-WECHSEL: Ein kleiner LRU-Pool (`pdfPoolSize`) haelt die zuletzt
//     geoeffneten PDFs GEPARST → Hin-und-Her-Wechseln muss nicht neu parsen. Ein
//     warmes Dokument haelt v. a. die Seitenstruktur, NICHT die Seitenbitmaps.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    property string source: ""
    property var    annotations: []

    property real   zoom: 1.0
    property string fitMode: "page"          // "page" = ganze Seite, "width" = Breite
    property int    currentPage: 0

    property real   topInset: 0
    property real   bottomInset: 0
    property bool   thumbsVisible: true
    property real   wheelPageFraction: 0.5   // Anteil der Viewporthöhe je Rad-Raststufe

    // ── Textauswahl-Zustand (recycling-sicher im Root gehalten) ───────────────
    //  Die Highlights leben hier (nicht im wiederverwendeten Delegate); nur die
    //  Seite mit selPage zeichnet selRects → Recycling verliert nichts.
    property int    selPage: -1              // Seite mit aktiver Auswahl (-1 = keine)
    property var    selRects: []             // normalisierte Highlight-Rechtecke {x,y,w,h}
    property bool   _selecting: false        // gerade am Ziehen?
    property var    _lastSel: null           // letzte Drag-Brüche (für Re-Query bei Ready)
    property bool   _pendingSelectAll: false // Strg+A vor Abschluss des Lazy-Loads → nachziehen

    // ── Audio (PdfAudio-Singleton) ────────────────────────────────────────────
    //  Eingebettete PDF-Audios in einer seitenbezogenen Leiste rechts + Mini-Player.
    //  audioClips = ALLE Clips des Dokuments [{id,page,x,y,w,h,label}]; die Leiste
    //  zeigt nur die der AKTUELLEN Seite, die Hotspots erscheinen je Seite.
    property bool   audioPanelVisible: false
    property var    audioClips: []
    property bool   documentHasAudio: false
    property var    _audioMeta: ({})         // id -> { url, durMs } (extrahiert/gecached)
    property var    _audioPos:  ({})         // id -> zuletzt gehoerte Position (ms, Resume)
    property int    activeClipId: -1         // aktuell geladener/spielender Clip
    property string _activeTitle: ""         // Anzeigetitel des Mini-Players
    property int    _audioRev: 0             // Counter → erzwingt Binding-Refresh (Dauer/Resume)
    property int    _pendingSeekMs: -1       // Seek nach Medienladen anwenden
    property bool   _pendingPlay: false
    // Akzentfarbe der Audio-UI: Theme-Akzent oder Apple-Systemblau (Einstellung).
    readonly property color audioAccent: App.audioAccentApple ? "#0A84FF" : App.themeAccent

    function _clipsOnPage(p) {
        var r = []
        for (var i = 0; i < audioClips.length; i++)
            if (audioClips[i].page === p) r.push(audioClips[i])
        return r
    }
    function _audioLabel(clip, idxOnPage) { return "Audio " + (idxOnPage + 1) }

    // ── Tunbare Cache-Deckel ──────────────────────────────────────────────────
    //  Scroll-Vorhalte/-Cache in Viewporthoehen je Richtung. 1.5 ≈ eine Seite
    //  ober- und unterhalb bleibt gerendert (Scroll-zurueck ohne Reload). Hoeher
    //  = mehr RAM, ABER auch mehr KONKURRIERENDE Renderings: PDFium serialisiert
    //  alle render()-Aufrufe EINER Dokument-Instanz ueber einen Mutex, d. h. jede
    //  vorab gerenderte Nachbarseite verzoegert die gerade sichtbar werdende.
    //  1.5 ist die Balance aus „naechste Seite ist schon da“ und „sichtbare Seite
    //  rendert zuerst“. RAM ≈ (1 + 2·pageCacheScreens) sichtbare Seitenbitmaps.
    property real   pageCacheScreens: 1.5
    //  Anzahl GEPARSTER PDFs, die fuer schnelles Zurueckwechseln warm bleiben.
    property int    pdfPoolSize: 3

    // ── Gestaffeltes Laden (das Kern-Performance-Muster) ──────────────────────
    //  Problem: Wuerden Hauptseiten UND Thumbnail-Leiste beim Oeffnen gleichzeitig
    //  rendern, konkurrierten ~8 Thumbnails + mehrere Vorab-Hauptseiten mit der
    //  einen sichtbaren Seite um denselben PDFium-Render-Mutex → spuerbare
    //  Oeffnen-Latenz, besonders bei schweren Seiten (Vektorgrafik/Bilder), wo
    //  jedes render() unabhaengig von der Zielgroesse teuer ist.
    //
    //  Loesung (wie die bewaehrte QPdfView-Version): zuerst NUR die sichtbare
    //  Seite rendern; Vorhalte-Puffer und Thumbnail-Leiste erst nach einer kurzen
    //  Verzoegerung freischalten — dann ist die erste Seite schon auf dem Schirm.
    property bool   _warm: false                 // false → nur sichtbare Seite rendern
    property int    warmupDelayMs: 160           // Verzoegerung bis Puffer+Thumbnails

    // docId des aktiven PDFs im RAM-Thumbnail-Provider (0 = noch keine).
    // Baut die image://pdfthumb/<docId>/<page>-URLs der Seitenleiste.
    property int    _thumbDocId: 0

    // Warmlauf nach jedem (Neu-)Laden anstossen: erst sichtbare Seite, dann Rest.
    function _beginWarmup() {
        root._warm = false
        warmupTimer.restart()
    }
    Timer {
        id: warmupTimer
        interval: root.warmupDelayMs
        repeat: false
        onTriggered: {
            // Vorrendern der Seitenleiste anstossen, BEVOR die Delegates ueber
            // _warm entstehen → sie binden sofort die korrekte docId. Sichtbare
            // Seite (currentPage) wird im Provider zuerst gerendert.
            if (root.source.length > 0)
                root._thumbDocId = PdfThumbs.ensureDocument(root.source, root.currentPage)
            root._warm = true
        }
    }

    // ── Dokument-Pool (LRU) ───────────────────────────────────────────────────
    property var    doc: null                // aktuell aktives PdfDocument
    readonly property bool docReady: !!doc && doc.status === PdfDocument.Ready
    readonly property int  pageCount: docReady ? doc.pageCount : 0

    property var    _pool: ({})              // localPath -> PdfDocument
    property var    _poolOrder: []           // LRU-Reihenfolge (alt -> neu)

    Component { id: _pdfDocFactory; PdfDocument {} }

    // Aktiviert das PDF fuer `localPath` (aus Pool wiederverwenden oder neu laden).
    function _activateDoc(localPath) {
        var key = root._localPath(localPath)
        var url = localPath.indexOf("file:") === 0 ? localPath : "file://" + localPath
        var d = root._pool[key]
        if (!d) {
            d = _pdfDocFactory.createObject(root)
            if (!d) return
            d.source = url
            root._pool[key] = d
        }
        // LRU: Schluessel ans Ende (juengster).
        var i = root._poolOrder.indexOf(key)
        if (i >= 0) root._poolOrder.splice(i, 1)
        root._poolOrder.push(key)
        root.doc = d
        root._evictPool()
    }

    // Aelteste Dokumente verdraengen, sobald der Pool seinen Deckel ueberschreitet.
    function _evictPool() {
        while (root._poolOrder.length > Math.max(1, root.pdfPoolSize)) {
            var victim = root._poolOrder.shift()
            var vd = root._pool[victim]
            delete root._pool[victim]
            if (vd && vd !== root.doc) { vd.source = ""; vd.destroy() }
        }
    }

    function _clearPool() {
        for (var k in root._pool) {
            var d = root._pool[k]
            if (d) { d.source = ""; d.destroy() }
        }
        root._pool = ({})
        root._poolOrder = []
        root.doc = null
    }

    Component.onDestruction: { _clearPool(); PdfText.releaseDocument(); PdfAudio.releaseDocument() }

    function release() {
        // Leichtgewichtig: nur Overlays stoppen. Das RENDER-Dokument bleibt im Pool
        // warm (kein doc.source="" mehr) → Zurueckwechseln muss nicht neu parsen.
        mediaLoader.active = false
        _saveActivePos()
        audioPlayer.stop()
        root.activeClipId = -1
        root._activeTitle = ""
        annotations = []
        // Das separate AUSWAHL-Dokument dagegen freigeben (RAM-Prio 1): es wird beim
        // naechsten Markieren ohnehin wieder lazy geladen.
        clearSelection()
        PdfText.releaseDocument()
    }

    onSourceChanged: {
        if (source.length > 0) {
            zoom = 1.0
            fitMode = "page"
            currentPage = 0
            annotations = []                 // bis der asynchrone Scan zurueckkommt
            clearSelection()                 // evtl. Auswahl des vorherigen PDFs verwerfen
            // Audio-Zustand des vorherigen PDFs verwerfen + neuen Scan anstoßen (lazy).
            audioPlayer.stop()
            root.audioClips = []
            root.documentHasAudio = false
            root.activeClipId = -1
            root._activeTitle = ""
            root._audioMeta = ({})
            root._audioPos = ({})
            root.audioPanelVisible = false
            PdfAudio.prepare(root.source)
            _activateDoc(root.source)
            // Bei einem bereits warmen (Ready) Dokument feuert kein statusChanged →
            // Scrollposition hier zuruecksetzen und den Warmlauf direkt starten.
            if (root.docReady) {
                pages.contentY = 0; root.currentPage = 0
                _beginWarmup()
            }
            // Annotationen NICHT blockierend holen → der PDF-Wechsel laggt nicht
            // mehr. Die Badges erscheinen, sobald pdfAnnotationsReady feuert.
            Viewer.requestPdfAnnotations(root.source)
        } else {
            release()
        }
    }

    // Statuswechsel des AKTUELLEN Dokuments (Connections retargetet bei doc-Wechsel).
    Connections {
        target: root.doc
        function onStatusChanged() {
            if (root.doc && root.doc.status === PdfDocument.Ready) {
                pages.contentY = 0
                root.currentPage = 0
                // Erst die sichtbare Seite rendern lassen, dann Puffer+Thumbnails.
                root._beginWarmup()
            }
        }
    }

    // file://-Praefix abstreifen → robuster Vergleich gegen den vom Viewer
    // emittierten lokalen Pfad (toLocalPath), plattformuebergreifend.
    function _localPath(s) {
        return s.indexOf("file://") === 0 ? s.substring(7) : s
    }

    // Ergebnis des asynchronen Scans entgegennehmen — nur uebernehmen, wenn es
    // zum aktuell angezeigten PDF gehoert (schnelles Vor/Zurueck ist sicher).
    Connections {
        target: Viewer
        function onPdfAnnotationsReady(path, anns) {
            if (root._localPath(path) === root._localPath(root.source))
                root.annotations = anns
        }
    }

    // Auswahl-Dokument wurde (asynchron) fertig geladen → eine evtl. noch laufende
    // Drag-Auswahl nachholen, deren fruehe Abfragen mangels Dokument leer blieben
    // (relevant nur bei grossen PDFs, deren Laden laenger als der erste Drag dauert).
    Connections {
        target: PdfText
        function onReadyChanged() {
            if (!PdfText.ready)
                return
            if (root._selecting && root._lastSel) {
                var s = root._lastSel
                root.selRects = PdfText.selectionBetween(s.page, s.a0, s.b0, s.a1, s.b1)
            } else if (root._pendingSelectAll) {
                root._pendingSelectAll = false
                root.selPage = root.currentPage
                root.selRects = PdfText.selectAllOnPage(root.currentPage)
            }
        }
    }

    // ── Audio-Wiedergabe (EIN Player; Mini-Player + seitenbezogene Leiste) ─────
    function playClip(id) {
        if (id < 0) return
        if (root.activeClipId === id) {                 // gleicher Clip → Play/Pause
            if (audioPlayer.playbackState === MediaPlayer.PlayingState) audioPlayer.pause()
            else audioPlayer.play()
            return
        }
        _saveActivePos()
        root.activeClipId = id
        var clip = null
        for (var i = 0; i < audioClips.length; i++) if (audioClips[i].id === id) { clip = audioClips[i]; break }
        if (clip) {
            var onPage = _clipsOnPage(clip.page); var k = 0
            for (var j = 0; j < onPage.length; j++) if (onPage[j].id === id) { k = j; break }
            root._activeTitle = "Audio " + (k + 1) + " \u00B7 S. " + (clip.page + 1)
        }
        var meta = root._audioMeta[id]
        if (meta && meta.url) _startActive(meta.url)
        else { root._pendingPlay = true; PdfAudio.requestClip(id) }   // async → onClipReady startet
    }

    function _startActive(url) {
        root._pendingSeekMs = root._audioPos[root.activeClipId] || 0
        root._pendingPlay = true
        audioPlayer.source = url           // Seek+Play erst bei LoadedMedia
    }

    function _saveActivePos() {
        if (root.activeClipId >= 0) {
            var m = root._audioPos; m[root.activeClipId] = audioPlayer.position; root._audioPos = m
            root._audioRev++
        }
    }

    // Bei offener Leiste die Clips der aktuellen Seite extrahieren (Dauer + bereit).
    function _ensurePageClipsExtracted() {
        if (!root.audioPanelVisible) return
        var cs = _clipsOnPage(root.currentPage)
        for (var i = 0; i < cs.length; i++)
            if (!root._audioMeta[cs[i].id]) PdfAudio.requestClip(cs[i].id)
    }

    function _savedPos(id) { return root._audioPos[id] || 0 }
    function _clipDurMs(id) { var m = root._audioMeta[id]; return m ? m.durMs : 0 }
    function _fmtTime(ms) {
        if (!ms || ms < 0) ms = 0
        var s = Math.floor(ms / 1000); var mm = Math.floor(s / 60); var ss = s % 60
        return mm + ":" + (ss < 10 ? "0" + ss : ss)
    }

    onAudioPanelVisibleChanged: if (audioPanelVisible) _ensurePageClipsExtracted()

    MediaPlayer {
        id: audioPlayer
        audioOutput: AudioOutput { id: audioOut }
        // Robust gegen das FFmpeg-Backend (Linux): play() wird bei LoadedMedia teils
        // VERWORFEN (zu früh) → nicht "verbrauchen", sondern bei LoadedMedia UND
        // BufferedMedia erneut versuchen, bis tatsächlich gespielt wird. Es wird NIE
        // auf 0 gesucht (ein redundanter Seek-auf-0 ließ die erste Wiedergabe hängen);
        // ein echter Resume-Sprung (>0) erfolgt erst, NACHDEM die Wiedergabe läuft.
        onMediaStatusChanged: {
            if ((mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)
                    && root._pendingPlay && playbackState !== MediaPlayer.PlayingState)
                play()
        }
        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState) {
                root._pendingPlay = false
                if (root._pendingSeekMs > 0) { position = root._pendingSeekMs; root._pendingSeekMs = -1 }
            }
        }
        // Nur in das einfache Objekt schreiben (Resume) — KEIN Reassign → keine
        // Binding-Last je Tick. Der aktive Slider liest audioPlayer.position direkt.
        onPositionChanged: if (root.activeClipId >= 0) root._audioPos[root.activeClipId] = position
    }

    Connections {
        target: PdfAudio
        function onReadyChanged() {
            if (PdfAudio.ready) {
                root.audioClips = PdfAudio.clips()
                root.documentHasAudio = PdfAudio.documentHasAudio
                root._ensurePageClipsExtracted()
            } else {
                root.audioClips = []
                root.documentHasAudio = false
            }
        }
        function onClipReady(id, url, durMs) {
            if (url.length > 0) {
                var m = root._audioMeta; m[id] = { url: url, durMs: durMs }; root._audioMeta = m
                root._audioRev++
                if (id === root.activeClipId && root._pendingPlay) root._startActive(url)
            }
        }
    }

    // ── Tastenkuerzel: Kopieren / Seite komplett markieren ─────────────────────
    //  WindowShortcut-Kontext (Standard) → feuert, solange dieses PDF im Vollbild
    //  aktiv ist. Copy ist nur scharf, wenn wirklich Text markiert ist (kein
    //  mehrdeutiger Konflikt). Explizite Sequenzen statt StandardKey, damit keine
    //  Zweitbindung den Shortcut mehrdeutig macht.
    Shortcut {
        sequence: "Ctrl+C"
        enabled: root.docReady && PdfText.selectedText.length > 0
        onActivated: PdfText.copyToClipboard()
    }
    Shortcut {
        sequence: "Ctrl+A"
        enabled: root.docReady
        onActivated: root.selectAllCurrentPage()
    }

    onCurrentPageChanged: {
        if (thumbs.count > 0) thumbs.positionViewAtIndex(currentPage, ListView.Contain)
        _ensurePageClipsExtracted()
    }

    // ── Zoom mit Erhaltung der relativen Scrollposition ───────────────────────
    function setZoom(z) {
        var nz = Math.max(0.25, Math.min(4.0, z))
        if (Math.abs(nz - root.zoom) < 0.0001) return
        var anchor = pages.contentY + pages.height / 2
        var ratio = nz / root.zoom
        root.zoom = nz
        pages.contentY = Math.max(0, Math.min(anchor * ratio - pages.height / 2,
                                              Math.max(0, pages.contentHeight - pages.height)))
    }
    function zoomIn()  { setZoom(root.zoom + 0.15) }
    function zoomOut() { setZoom(root.zoom - 0.15) }

    function goToPage(p) {
        if (pages.count <= 0) return
        var t = Math.max(0, Math.min(p, pages.count - 1))
        pages.positionViewAtIndex(t, ListView.Beginning)
        root.currentPage = t
    }
    function updateCurrentPage() {
        if (pages.count <= 0) { root.currentPage = 0; return }
        var idx = pages.indexAt(pages.width / 2, pages.contentY + pages.height / 2)
        if (idx >= 0) root.currentPage = idx
    }

    // ── Textauswahl-Steuerung ─────────────────────────────────────────────────
    //  beginSelection lädt das Auswahl-Dokument LAZY (erst bei echtem Markieren →
    //  reines Ansehen kostet kein zusätzliches QPdfDocument). Bei großen PDFs ist
    //  der asynchrone Ladevorgang ggf. erst während des Ziehens fertig — dann holt
    //  der PdfText.onReadyChanged-Handler die Auswahl nach (Catch-up).
    function beginSelection(page) {
        root._selecting = true
        root.selPage = page
        root.selRects = []
        root._lastSel = null
        root._pendingSelectAll = false
        PdfText.clearSelection()
        PdfText.prepare(root.source)
    }
    function updateSelection(page, a0, b0, a1, b1) {
        // Auf [0..1] klemmen (Ziehen über den Seitenrand hinaus).
        a0 = Math.max(0, Math.min(1, a0)); b0 = Math.max(0, Math.min(1, b0))
        a1 = Math.max(0, Math.min(1, a1)); b1 = Math.max(0, Math.min(1, b1))
        root._lastSel = { page: page, a0: a0, b0: b0, a1: a1, b1: b1 }
        root.selPage = page
        root.selRects = PdfText.selectionBetween(page, a0, b0, a1, b1)
    }
    function endSelection(wasDrag) {
        root._selecting = false
        if (!wasDrag) root.clearSelection()   // reiner Klick → Auswahl aufheben
    }
    function clearSelection() {
        root.selPage = -1
        root.selRects = []
        root._lastSel = null
        root._pendingSelectAll = false
        PdfText.clearSelection()
    }
    function selectAllCurrentPage() {
        if (!root.docReady) return
        root._lastSel = null
        PdfText.prepare(root.source)
        if (PdfText.ready) {
            root.selPage = root.currentPage
            root.selRects = PdfText.selectAllOnPage(root.currentPage)
            root._pendingSelectAll = false
        } else {
            // Auswahl-Dokument laedt noch (lazy) → nach dem Laden nachholen.
            root._pendingSelectAll = true
        }
    }

    // Web-artiges, animiertes Scrollen der Seiten (von der Wheel-MouseArea genutzt).
    NumberAnimation {
        id: pagesScroll
        target: pages; property: "contentY"
        duration: 180; easing.type: Easing.OutCubic
    }
    function wheelPages(wheel) {
        if (wheel.modifiers & Qt.ControlModifier) {
            if (wheel.angleDelta.y > 0) root.zoomIn()
            else if (wheel.angleDelta.y < 0) root.zoomOut()
            wheel.accepted = true
            return
        }
        var maxY = Math.max(0, pages.contentHeight - pages.height)
        if (maxY <= 0) { wheel.accepted = true; return }
        var raw = (wheel.angleDelta.y !== 0)
                  ? (wheel.angleDelta.y / 120) * (pages.height * root.wheelPageFraction)
                  : wheel.pixelDelta.y * 1.6
        var base = pagesScroll.running ? pagesScroll.to : pages.contentY
        var tgt = Math.max(0, Math.min(base - raw, maxY))
        pagesScroll.from = pages.contentY
        pagesScroll.to = tgt
        pagesScroll.restart()
        wheel.accepted = true
    }

    Rectangle { anchors.fill: parent; color: App.themeBackground }

    Text {
        id: errLabel
        anchors.centerIn: parent
        // Reaktiv an den Status des aktuellen Dokuments gebunden.
        visible: !!root.doc && root.doc.status === PdfDocument.Error
        text: "PDF konnte nicht geladen werden."
        color: "#ff8a80"; font.pixelSize: 14
        z: 5
    }

    // ── Toolbar (unter der globalen Leiste → kein Overlap) ────────────────────
    Rectangle {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset }
        height: 40
        color: App.themeToolbarBg
        visible: root.docReady
        z: 6
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: App.themeBorder }

        Row {
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            PdfToolButton {
                // ← = einklappen (Panel offen), → = ausklappen (Panel zu).
                // Bewusst Pfeile (\u2190/\u2192) statt der soliden Seiten-Nav-
                // Dreiecke (\u25C0/\u25B6) → optisch klar unterscheidbar.
                glyph: root.thumbsVisible ? "\u2190" : "\u2192"
                active: root.thumbsVisible
                tip: root.thumbsVisible ? "Seitenvorschau einklappen"
                                        : "Seitenvorschau ausklappen"
                onActivated: root.thumbsVisible = !root.thumbsVisible
            }
            Item { width: 4; height: 1 }
            PdfToolButton {
                glyph: "\u25C0"
                enabled: root.currentPage > 0
                onActivated: root.goToPage(root.currentPage - 1)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Seite " + (root.currentPage + 1) + " / " + root.pageCount
                color: App.themeTextPrimary; font.pixelSize: 12
                width: 96; horizontalAlignment: Text.AlignHCenter
            }
            PdfToolButton {
                glyph: "\u25B6"
                enabled: root.currentPage < root.pageCount - 1
                onActivated: root.goToPage(root.currentPage + 1)
            }
        }

        Row {
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            // Audio-Leiste umschalten — nur sichtbar, wenn das PDF Audio enthält.
            PdfToolButton {
                glyph: "\u266A"
                visible: PdfAudio.documentHasAudio
                active: root.audioPanelVisible
                tip: root.audioPanelVisible ? "Audioleiste ausblenden" : "Audioleiste einblenden"
                onActivated: root.audioPanelVisible = !root.audioPanelVisible
            }
            Item { width: PdfAudio.documentHasAudio ? 4 : 0; height: 1 }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: fitLabel.implicitWidth + 22; height: 26; radius: 6
                color: fitHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                                        : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                border.color: App.themeBorder; border.width: 1
                Text { id: fitLabel; anchors.centerIn: parent
                       text: root.fitMode === "page" ? "Ganze Seite" : "Breite"
                       color: App.themeTextPrimary; font.pixelSize: 11 }
                HoverHandler { id: fitHover }
                TapHandler {
                    onTapped: {
                        root.fitMode = (root.fitMode === "page") ? "width" : "page"
                        root.zoom = 1.0
                        root.updateCurrentPage()
                    }
                }
            }
            Item { width: 6; height: 1 }
            PdfToolButton { glyph: "\u2212"; enabled: root.zoom > 0.26; onActivated: root.zoomOut() }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(root.zoom * 100) + " %"
                color: App.themeTextPrimary; font.pixelSize: 12
                width: 48; horizontalAlignment: Text.AlignHCenter
            }
            PdfToolButton { glyph: "+"; enabled: root.zoom < 3.99; onActivated: root.zoomIn() }
        }
    }

    // ── Inhaltsbereich ────────────────────────────────────────────────────────
    Item {
        id: contentArea
        anchors {
            left: parent.left; right: parent.right
            top: toolbar.visible ? toolbar.bottom : parent.top
            bottom: parent.bottom
            bottomMargin: root.bottomInset
        }

        // ── Seiten (volle Breite; Thumbnail-Panel liegt als Overlay darüber) ──
        ListView {
            id: pages
            anchors.fill: parent
            clip: true
            // Browser-artige Textauswahl ist IMMER aktiv: Linksziehen markiert.
            // Damit der Flickable das Ziehen nicht als Schwenken stiehlt, ist das
            // eigene Dragging der Liste deaktiviert. Gescrollt wird ueber das
            // Mausrad (NoButton-Wheel-MouseArea) und die ScrollBar — beide
            // funktionieren bei interactive:false unveraendert (sie setzen contentY
            // direkt). Programmatische contentY/positionViewAtIndex bleiben gueltig.
            interactive: false
            model: root.docReady ? root.doc.pageCount : 0
            spacing: 10
            // Scroll-Cache: pageCacheScreens Viewporthoehen je Richtung bleiben
            // gerendert → Hoch-/Runterscrollen innerhalb des Fensters laedt nicht
            // neu. cache:false an den PdfPageImage → ausserhalb dieses Fensters
            // wird wieder freigegeben (deterministischer RAM-Deckel).
            //
            // GESTAFFELT: Bis der Warmlauf greift, ist der Puffer fast 0 → beim
            // Oeffnen rendert NUR die sichtbare Seite (kein Vorab-Rendern von
            // Nachbarseiten, die sie sonst hinter dem Render-Mutex blockierten).
            // Danach klappt der volle Vorhalte-Puffer fuer fluessiges Scrollen auf.
            cacheBuffer: Math.round(pages.height *
                                    (root._warm ? root.pageCacheScreens : 0.1))
            boundsBehavior: Flickable.StopAtBounds
            onContentYChanged: root.updateCurrentPage()
            onCountChanged: root.updateCurrentPage()

            ScrollBar.vertical: ScrollBar {
                id: vbar
                policy: ScrollBar.AlwaysOn
                width: 12
                interactive: true
                contentItem: Rectangle {
                    implicitWidth: 8; radius: 4
                    color: vbar.pressed ? App.themeAccent
                         : vbar.hovered ? Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.9)
                                        : Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.55)
                }
                background: Rectangle { color: Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.05); radius: 4 }
            }

            delegate: Item {
                id: pageCell
                required property int index
                width: pages.width

                readonly property size pts: root.doc.pagePointSize(index)
                // Skalierung unabhaengig vom Overlay-Panel → Umschalten loest KEIN
                // teures Neu-Rendern aus (Panel liegt nur ueber dem linken Rand).
                readonly property real wFit: pts.width  > 0 ? (pages.width  - 24) / pts.width  : 1.0
                readonly property real hFit: pts.height > 0 ? (pages.height - 24) / pts.height : 1.0
                readonly property real fitScale: root.fitMode === "page"
                                                 ? Math.min(wFit, hFit) : wFit
                readonly property real pageW: pts.width  * fitScale * root.zoom
                readonly property real pageH: pts.height * fitScale * root.zoom
                height: pageH + 4

                Rectangle {
                    id: pageBg
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: pageCell.pageW; height: pageCell.pageH
                    color: "white"

                    // ── Textauswahl-Fänger (UNTERSTE Ebene der Seite) ──────────
                    //  Das PdfPageImage darueber faengt keine Maus → ein Linkspress
                    //  faellt hierher durch. Ausnahme: ein Badge (eigene MouseArea,
                    //  liegt oben) verbraucht den Press → Annotation-Klicks bleiben
                    //  erhalten und starten KEINE Markierung.
                    //  Ziehen markiert; reiner Klick hebt die Auswahl auf.
                    MouseArea {
                        id: selArea
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.IBeamCursor
                        preventStealing: true
                        property real sx: 0
                        property real sy: 0
                        property bool dragging: false
                        onPressed: (m) => {
                            selArea.sx = m.x; selArea.sy = m.y
                            selArea.dragging = false
                            root.beginSelection(pageCell.index)
                        }
                        onPositionChanged: (m) => {
                            // Erst ab kleiner Schwelle als Ziehen werten (verhindert
                            // Mikro-Drags, die einen Klick als Auswahl missdeuten).
                            if (!selArea.dragging) {
                                if (Math.abs(m.x - selArea.sx) + Math.abs(m.y - selArea.sy) < 3)
                                    return
                                selArea.dragging = true
                            }
                            root.updateSelection(pageCell.index,
                                selArea.sx / pageImg.width, selArea.sy / pageImg.height,
                                m.x        / pageImg.width, m.y        / pageImg.height)
                        }
                        onReleased: root.endSelection(selArea.dragging)
                    }

                    PdfPageImage {
                        id: pageImg
                        anchors.fill: parent
                        document: root.doc
                        currentFrame: pageCell.index
                        asynchronous: true
                        cache: false
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: pageCell.pageW * Screen.devicePixelRatio
                        sourceSize.height: pageCell.pageH * Screen.devicePixelRatio

                        // ── Auswahl-Highlights (nur auf der Seite mit aktiver
                        //    Auswahl; normalisierte Rechtecke vom PdfText-Singleton) ─
                        Repeater {
                            model: pageCell.index === root.selPage ? root.selRects : []
                            delegate: Rectangle {
                                required property var modelData
                                x: modelData.x * pageImg.width
                                y: modelData.y * pageImg.height
                                width:  Math.max(1, modelData.w * pageImg.width)
                                height: Math.max(1, modelData.h * pageImg.height)
                                color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                               App.themeAccent.b, 0.32)
                            }
                        }

                        Repeater {
                            model: root.annotations
                            delegate: Rectangle {
                                id: badge
                                required property var modelData
                                // Audio (type 0) NICHT hier — das übernimmt PdfAudio
                                // (eigene Hotspots + Leiste). Hier nur Video/Link.
                                visible: modelData.page === pageCell.index && modelData.type !== 0
                                x: modelData.x * pageImg.width
                                y: modelData.y * pageImg.height
                                width:  Math.max(18, modelData.w * pageImg.width)
                                height: Math.max(18, modelData.h * pageImg.height)
                                radius: 4
                                color: badgeHover.hovered ? Qt.rgba(0.0, 0.78, 0.70, 0.35)
                                                          : Qt.rgba(0.0, 0.78, 0.70, 0.18)
                                border.color: "#00c8b4"; border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: badge.modelData.type === 1 ? "\u25B6" : "\u2197"
                                    color: "#e0fffb"; font.pixelSize: 13
                                }
                                HoverHandler { id: badgeHover }
                                ToolTip.visible: badgeHover.hovered && badge.modelData.label.length > 0
                                ToolTip.text: badge.modelData.label
                                // MouseArea (statt TapHandler): verbraucht den Press,
                                // sodass der darunterliegende Auswahl-Fänger bei einem
                                // Badge-Klick KEINE Markierung startet. Annotation wird
                                // wie gehabt aktiviert.
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.activateAnnotation(badge.modelData)
                                }
                            }
                        }

                        // ── Audio-Hotspots (PdfAudio; je Seite) ──────────────────
                        //  Klickbare Marker über den Audio-Buttons: Klick spielt den
                        //  Clip ab und öffnet die Audioleiste. Aktiver Clip pulsiert.
                        Repeater {
                            model: root.audioClips
                            delegate: Rectangle {
                                id: aspot
                                required property var modelData
                                readonly property bool isActive: root.activeClipId === aspot.modelData.id
                                visible: aspot.modelData.page === pageCell.index
                                x: aspot.modelData.x * pageImg.width
                                y: aspot.modelData.y * pageImg.height
                                width:  Math.max(18, aspot.modelData.w * pageImg.width)
                                height: Math.max(18, aspot.modelData.h * pageImg.height)
                                radius: 4
                                color: aspot.isActive
                                       ? Qt.rgba(root.audioAccent.r, root.audioAccent.g, root.audioAccent.b, 0.30)
                                       : (aspotHover.hovered
                                          ? Qt.rgba(root.audioAccent.r, root.audioAccent.g, root.audioAccent.b, 0.26)
                                          : Qt.rgba(root.audioAccent.r, root.audioAccent.g, root.audioAccent.b, 0.12))
                                border.color: root.audioAccent
                                border.width: aspot.isActive ? 2 : 1
                                Text {
                                    anchors.centerIn: parent
                                    text: (aspot.isActive && audioPlayer.playbackState === MediaPlayer.PlayingState)
                                          ? "\u23F8" : "\u266A"
                                    color: "#ffffff"; font.pixelSize: 12
                                }
                                HoverHandler { id: aspotHover }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: { root.audioPanelVisible = true; root.playClip(aspot.modelData.id) }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Wheel-Fänger über den Seiten (NoButton → Klicks/Badges bleiben aktiv) ──
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            z: 1
            onWheel: (wheel) => root.wheelPages(wheel)
        }

        // ── Thumbnail-Seitenleiste (OVERLAY links; kein Seiten-Reflow) ─────────
        Rectangle {
            id: thumbPanel
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: 152
            visible: root.thumbsVisible && root.pageCount > 0
            z: 3
            color: App.themeSidebarBg
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: App.themeBorder }

            ListView {
                id: thumbs
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                // Erst NACH dem Warmlauf befuellen → die Thumbnail-Renderings
                // konkurrieren nicht mehr mit der ersten sichtbaren Hauptseite um
                // den PDFium-Render-Mutex (entspricht dem 120-ms-Deferral der
                // alten QPdfView-Version).
                model: (root.docReady && root._warm) ? root.doc.pageCount : 0
                spacing: 10
                // Vorschauen kommen jetzt JPEG-komprimiert aus dem RAM-Provider —
                // Scrollen kostet nur einen winzigen Dekode, kein PDFium-Render.
                // Etwas mehr Vorhalt haelt die Leiste auch bei schnellem Scrollen
                // luecken­frei, ohne nennenswerten RAM (wenige KB je Vorschau).
                cacheBuffer: Math.round(thumbs.height * 1.5)
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: thumbCell
                    required property int index
                    // Cache-Buster: hochzaehlen, sobald die Vorschau gerendert ist
                    // → die Image-source wird neu angefordert und aus dem RAM-Store
                    //   geliefert.
                    property int rev: 0
                    readonly property size pts: root.doc.pagePointSize(index)
                    readonly property real thumbW: thumbs.width - 8
                    readonly property real thumbH: pts.width > 0 ? thumbW * (pts.height / pts.width) : thumbW * 1.414
                    width: thumbs.width
                    height: thumbH + 18

                    // Meldung des Providers: genau diese Seite liegt jetzt im Store.
                    Connections {
                        target: PdfThumbs
                        function onPageReady(docId, page) {
                            if (docId === root._thumbDocId && page === thumbCell.index)
                                thumbCell.rev++
                        }
                    }

                    Rectangle {
                        id: thumbFrame
                        width: thumbCell.thumbW; height: thumbCell.thumbH
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        color: "white"
                        border.color: thumbCell.index === root.currentPage ? App.themeAccent
                                                                           : App.themeBorder
                        border.width: thumbCell.index === root.currentPage ? 2 : 1
                        // Vorschau kommt JPEG-komprimiert aus dem RAM-Provider:
                        // KEIN PdfPageImage mehr → kein PDFium-Render am Haupt-Mutex,
                        // Scrollen dekodiert nur winzige JPEGs (asynchron, < 1 ms).
                        Image {
                            id: thumbImg
                            anchors.fill: parent
                            anchors.margins: thumbFrame.border.width
                            asynchronous: true
                            cache: false
                            fillMode: Image.PreserveAspectFit
                            source: root._thumbDocId > 0
                                    ? "image://pdfthumb/" + root._thumbDocId + "/"
                                      + thumbCell.index + "?r=" + thumbCell.rev
                                    : ""
                            sourceSize.width: thumbCell.thumbW * Screen.devicePixelRatio
                        }
                        TapHandler { onTapped: root.goToPage(thumbCell.index) }
                    }
                    Text {
                        anchors.top: thumbFrame.bottom; anchors.topMargin: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (thumbCell.index + 1)
                        color: thumbCell.index === root.currentPage ? App.themeAccent : App.themeTextMuted
                        font.pixelSize: 10
                    }
                }
            }

            // Web-artiges, animiertes Wheel-Scrollen der Seitenleiste (wie die
            // Hauptansicht). Liegt als NoButton-MouseArea ueber der Liste: faengt
            // nur das Mausrad, laesst Klicks (Thumbnail-Tap, ScrollBar) hindurch.
            NumberAnimation {
                id: thumbsScroll
                target: thumbs; property: "contentY"
                duration: 180; easing.type: Easing.OutCubic
            }
            MouseArea {
                anchors.fill: thumbs
                acceptedButtons: Qt.NoButton
                z: 1
                onWheel: (wheel) => {
                    var maxY = Math.max(0, thumbs.contentHeight - thumbs.height)
                    if (maxY <= 0) { wheel.accepted = true; return }
                    var raw = (wheel.angleDelta.y !== 0)
                              ? (wheel.angleDelta.y / 120) * (thumbs.height * 0.55)
                              : wheel.pixelDelta.y * 1.6
                    var base = thumbsScroll.running ? thumbsScroll.to : thumbs.contentY
                    var tgt = Math.max(0, Math.min(base - raw, maxY))
                    thumbsScroll.from = thumbs.contentY
                    thumbsScroll.to = tgt
                    thumbsScroll.restart()
                    wheel.accepted = true
                }
            }
        }

        // ── Audio-Panel (rechts; Overlay über den Seiten, KEIN Reflow) ─────────
        //  Symmetrisch zur Thumbnail-Leiste links. 14px Lücke rechts lässt die
        //  Dokument-Scrollleiste sichtbar. Zeigt NUR die Audios der aktuellen Seite
        //  („Audios nur da, wo sie herkommen"); der Mini-Player unten spielt
        //  unabhängig vom gerade angezeigten Seitenausschnitt weiter.
        Rectangle {
            id: audioPanel
            anchors { right: parent.right; rightMargin: 14; top: parent.top; bottom: parent.bottom }
            width: 300
            visible: root.audioPanelVisible && PdfAudio.documentHasAudio
            z: 4
            color: App.themeSidebarBg

            // Clips der aktuellen Seite (hängt an audioClips + currentPage, NICHT an
            // _audioRev → kein Delegate-Neuaufbau bei Positions-/Dauer-Updates).
            readonly property var pageClips: root._clipsOnPage(root.currentPage)

            Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: App.themeBorder }

            // Klicks/Wheel auf leeren Panel-Flächen abfangen (sonst Durchgriff auf
            // die Seiten-Textauswahl darunter). Kinder darüber behandeln ihre Events.
            MouseArea { anchors.fill: parent; onWheel: (wheel) => { wheel.accepted = true } }

            // Kopf
            Item {
                id: audioHeader
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 44
                Text {
                    anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                    text: "Audio \u00B7 Seite " + (root.currentPage + 1)
                    color: App.themeTextPrimary; font.pixelSize: 13; font.bold: true
                }
                Rectangle {
                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    width: 26; height: 26; radius: 13
                    color: closeHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.14) : "transparent"
                    Text { anchors.centerIn: parent; text: "\u2715"; color: App.themeTextMuted; font.pixelSize: 13 }
                    HoverHandler { id: closeHover }
                    TapHandler { onTapped: root.audioPanelVisible = false }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: App.themeBorder }
            }

            // Leerzustand (Seite ohne Audio)
            Text {
                anchors.centerIn: parent
                visible: audioPanel.pageClips.length === 0
                text: "Keine Audios auf dieser Seite."
                color: App.themeTextMuted; font.pixelSize: 12
            }

            // Liste der Clips dieser Seite
            ListView {
                id: audioList
                anchors {
                    left: parent.left; right: parent.right
                    top: audioHeader.bottom
                    bottom: miniPlayer.visible ? miniPlayer.top : parent.bottom
                    margins: 8
                }
                clip: true
                spacing: 6
                model: audioPanel.pageClips
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: arow
                    required property var modelData
                    required property int index
                    readonly property int  cid: arow.modelData.id
                    readonly property bool isActive: root.activeClipId === arow.cid
                    readonly property int  durMs: (root._audioRev, root._clipDurMs(arow.cid))
                    width: audioList.width
                    height: 56
                    radius: 10
                    color: arow.isActive ? Qt.rgba(root.audioAccent.r, root.audioAccent.g, root.audioAccent.b, 0.12)
                                         : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.04)
                    border.color: arow.isActive ? root.audioAccent : "transparent"
                    border.width: 1

                    // Runder Play/Pause-Knopf
                    Rectangle {
                        id: arowBtn
                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 36; height: 36; radius: 18
                        color: root.audioAccent
                        opacity: arowBtnHover.hovered ? 0.85 : 1.0
                        Text {
                            anchors.centerIn: parent
                            text: (arow.isActive && audioPlayer.playbackState === MediaPlayer.PlayingState) ? "\u23F8" : "\u25B6"
                            color: "white"; font.pixelSize: 14
                        }
                        HoverHandler { id: arowBtnHover }
                        TapHandler { onTapped: root.playClip(arow.cid) }
                    }

                    Text {
                        id: arowTitle
                        anchors { left: arowBtn.right; leftMargin: 10; top: parent.top; topMargin: 9 }
                        text: "Audio " + (arow.index + 1)
                        color: App.themeTextPrimary; font.pixelSize: 13; font.bold: arow.isActive
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 12; verticalCenter: arowTitle.verticalCenter }
                        text: arow.durMs > 0 ? root._fmtTime(arow.durMs) : "\u2013:\u2013\u2013"
                        color: App.themeTextMuted; font.pixelSize: 11
                    }

                    // Fortschritts-/Seek-Slider (wie YouTube/Spotify): zeigt Gehörtes,
                    // an beliebige Stelle ziehbar → ab dort weiter/starten.
                    Slider {
                        id: arowSlider
                        anchors { left: arowBtn.right; leftMargin: 10; right: parent.right; rightMargin: 12; bottom: parent.bottom; bottomMargin: 8 }
                        height: 16
                        from: 0
                        to: Math.max(1, arow.durMs)
                        // pressed?value:… hält die Bindung beim Ziehen intakt.
                        value: pressed ? value
                                       : (arow.isActive ? audioPlayer.position
                                                        : (root._audioRev, root._savedPos(arow.cid)))
                        onMoved: if (arow.isActive) audioPlayer.position = value
                        onPressedChanged: {
                            if (!pressed && !arow.isActive) {
                                var m = root._audioPos; m[arow.cid] = value; root._audioPos = m; root._audioRev++
                                root.playClip(arow.cid)     // ab gewählter Stelle starten
                            }
                        }
                        background: Rectangle {
                            x: arowSlider.leftPadding; y: arowSlider.topPadding + arowSlider.availableHeight / 2 - height / 2
                            width: arowSlider.availableWidth; height: 4; radius: 2
                            color: Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.35)
                            Rectangle { width: arowSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: root.audioAccent }
                        }
                        handle: Rectangle {
                            x: arowSlider.leftPadding + arowSlider.visualPosition * (arowSlider.availableWidth - width)
                            y: arowSlider.topPadding + arowSlider.availableHeight / 2 - height / 2
                            width: 12; height: 12; radius: 6
                            color: root.audioAccent; border.color: "white"; border.width: 1
                        }
                    }
                }
            }

            // ── Mini-Player (Now-Playing; unten angedockt) ─────────────────────
            Rectangle {
                id: miniPlayer
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 84
                visible: root.activeClipId >= 0
                color: App.themeToolbarBg
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: App.themeBorder }

                Rectangle {
                    id: miniBtn
                    anchors { left: parent.left; leftMargin: 14; top: parent.top; topMargin: 12 }
                    width: 44; height: 44; radius: 22
                    color: root.audioAccent
                    opacity: miniBtnHover.hovered ? 0.85 : 1.0
                    Text {
                        anchors.centerIn: parent
                        text: audioPlayer.playbackState === MediaPlayer.PlayingState ? "\u23F8" : "\u25B6"
                        color: "white"; font.pixelSize: 17
                    }
                    HoverHandler { id: miniBtnHover }
                    TapHandler { onTapped: root.playClip(root.activeClipId) }
                }
                Text {
                    id: miniTitle
                    anchors { left: miniBtn.right; leftMargin: 12; right: parent.right; rightMargin: 14; top: parent.top; topMargin: 13 }
                    text: root._activeTitle.length > 0 ? root._activeTitle : "Audio"
                    color: App.themeTextPrimary; font.pixelSize: 13; font.bold: true
                    elide: Text.ElideRight
                }
                Slider {
                    id: miniSlider
                    anchors { left: miniBtn.right; leftMargin: 12; right: parent.right; rightMargin: 14; top: miniTitle.bottom; topMargin: 6 }
                    height: 18
                    from: 0; to: Math.max(1, audioPlayer.duration)
                    value: pressed ? value : audioPlayer.position
                    onMoved: audioPlayer.position = value
                    background: Rectangle {
                        x: miniSlider.leftPadding; y: miniSlider.topPadding + miniSlider.availableHeight / 2 - height / 2
                        width: miniSlider.availableWidth; height: 4; radius: 2
                        color: Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.35)
                        Rectangle { width: miniSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: root.audioAccent }
                    }
                    handle: Rectangle {
                        x: miniSlider.leftPadding + miniSlider.visualPosition * (miniSlider.availableWidth - width)
                        y: miniSlider.topPadding + miniSlider.availableHeight / 2 - height / 2
                        width: 14; height: 14; radius: 7
                        color: root.audioAccent; border.color: "white"; border.width: 1
                    }
                }
                Text {
                    anchors { left: miniBtn.right; leftMargin: 12; bottom: parent.bottom; bottomMargin: 7 }
                    text: root._fmtTime(audioPlayer.position)
                    color: App.themeTextMuted; font.pixelSize: 10
                }
                Text {
                    anchors { right: parent.right; rightMargin: 14; bottom: parent.bottom; bottomMargin: 7 }
                    text: root._fmtTime(audioPlayer.duration)
                    color: App.themeTextMuted; font.pixelSize: 10
                }
            }
        }
    }

    function activateAnnotation(a) {
        if (a.type === 2 || a.uri.indexOf("http") === 0) {       // Link
            Viewer.openExternally(a.uri)
        } else if (a.type === 1) {                                // Video
            mediaLoader.uri = a.uri
            mediaLoader.active = true
        }
        // Audio (type 0) wird über PdfAudio/Audioleiste abgespielt (eigene Hotspots).
    }

    // ── Video-Overlay (Annotation) ─────────────────────────────────────────────
    Loader {
        id: mediaLoader
        anchors.fill: parent
        active: false
        property string uri: ""
        z: 7
        sourceComponent: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.92)
            VideoSurface { anchors.fill: parent; anchors.margins: 24; source: mediaLoader.uri }
            Rectangle {
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 16
                width: 40; height: 40; radius: 20; color: Qt.rgba(1,1,1,0.12)
                Text { anchors.centerIn: parent; text: "\u2715"; color: "white"; font.pixelSize: 18 }
                TapHandler { onTapped: mediaLoader.active = false }
            }
        }
    }

    // ── Kompakter Toolbar-Button (wiederverwendbar, theme-konform) ────────────
    component PdfToolButton: Rectangle {
        id: tb
        property string glyph: ""
        property string tip: ""
        property bool active: false
        signal activated()
        width: 30; height: 26; radius: 6
        opacity: enabled ? 1.0 : 0.35
        color: active ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (tbHover.hovered && enabled
                ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07))
        border.color: active ? App.themeAccent : App.themeBorder; border.width: 1
        Text { anchors.centerIn: parent; text: tb.glyph; color: App.themeTextPrimary; font.pixelSize: 13 }
        HoverHandler { id: tbHover; enabled: tb.enabled }
        TapHandler { enabled: tb.enabled; onTapped: tb.activated() }
        ToolTip.text: tb.tip
        ToolTip.visible: tbHover.hovered && tb.tip.length > 0
    }
}
