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

    Component.onDestruction: _clearPool()

    function release() {
        // Leichtgewichtig: nur Overlays stoppen. Das Dokument bleibt im Pool warm
        // (kein doc.source="" mehr) → Zurueckwechseln muss nicht neu parsen.
        mediaLoader.active = false
        audioPlayer.stop()
        annotations = []
    }

    onSourceChanged: {
        if (source.length > 0) {
            zoom = 1.0
            fitMode = "page"
            currentPage = 0
            annotations = []                 // bis der asynchrone Scan zurueckkommt
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

    onCurrentPageChanged: if (thumbs.count > 0) thumbs.positionViewAtIndex(currentPage, ListView.Contain)

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
                glyph: "\u2637"
                active: root.thumbsVisible
                tip: "Seitenvorschau ein-/ausblenden"
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
            bottomMargin: root.bottomInset + (audioBar.visible ? audioBar.height : 0)
        }

        // ── Seiten (volle Breite; Thumbnail-Panel liegt als Overlay darüber) ──
        ListView {
            id: pages
            anchors.fill: parent
            clip: true
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

                        Repeater {
                            model: root.annotations
                            delegate: Rectangle {
                                id: badge
                                required property var modelData
                                visible: modelData.page === pageCell.index
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
                                    text: badge.modelData.type === 0 ? "\u266A"
                                        : badge.modelData.type === 1 ? "\u25B6" : "\u2197"
                                    color: "#e0fffb"; font.pixelSize: 13
                                }
                                HoverHandler { id: badgeHover }
                                ToolTip.visible: badgeHover.hovered && badge.modelData.label.length > 0
                                ToolTip.text: badge.modelData.label
                                TapHandler { onTapped: root.activateAnnotation(badge.modelData) }
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
    }

    function activateAnnotation(a) {
        if (a.type === 2 || a.uri.indexOf("http") === 0) {       // Link
            Viewer.openExternally(a.uri)
        } else if (a.type === 1) {                                // Video
            mediaLoader.uri = a.uri
            mediaLoader.active = true
        } else if (a.type === 0) {                                // Audio
            audioPlayer.source = a.uri.indexOf("file:") === 0 ? a.uri : "file://" + a.uri
            audioBar.label = a.label.length > 0 ? a.label : "Audio"
            audioPlayer.play()
        }
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

    // ── Audio-Leiste (Annotation) ──────────────────────────────────────────────
    MediaPlayer { id: audioPlayer; audioOutput: AudioOutput { id: audioOut } }
    Rectangle {
        id: audioBar
        property string label: ""
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: root.bottomInset }
        height: 46; z: 5
        visible: audioPlayer.playbackState !== MediaPlayer.StoppedState
        color: App.themeToolbarBg
        Row {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                text: audioPlayer.playbackState === MediaPlayer.PlayingState ? "\u23F8" : "\u25B6"
                onClicked: audioPlayer.playbackState === MediaPlayer.PlayingState
                           ? audioPlayer.pause() : audioPlayer.play()
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: audioBar.label; color: App.themeTextPrimary; font.pixelSize: 12
                elide: Text.ElideRight; width: 160
            }
            Slider {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 280
                from: 0; to: Math.max(1, audioPlayer.duration)
                value: pressed ? value : audioPlayer.position
                onMoved: audioPlayer.position = value
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                text: "\u2715"; onClicked: audioPlayer.stop()
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
