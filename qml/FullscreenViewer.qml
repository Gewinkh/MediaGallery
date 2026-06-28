pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  FullscreenViewer.qml — Vollbild-Anzeige-Verbund (ersetzt FullscreenView/
//  ImageViewerWindow/ImageViewer.qml). StackView-Seite "fullscreen" (Phase 1).
//
//  • Dispatch nach Medientyp über EINEN Loader → nur das aktive Medium ist
//    dekodiert; beim Wechsel/Verlassen release() (RAM-Prio 1).
//  • Navigation prev/next/zufall ausschließlich über die Proxy-Reihenfolge
//    (galleryModel.*At / rowForPath / randomRow) — keine eigene Liste.
//  • Metadaten-Overlay (Name/Datum/Tags) + Inline-Edit via Bridge.
//  • Video-Mode "external": Medium wird im Systemplayer geöffnet (Viewer.bridge).
// ─────────────────────────────────────────────────────────────────────────────
FocusScope {
    id: root
    focus: true

    signal backRequested()

    // Vom Shell gesetzt (Einstiegspfad).
    property string startPath: ""

    // Aktueller Zustand (aus dem Proxy gelesen).
    property int    currentRow: -1
    property string path: ""
    property int    type: 5          // 0 Image,1 Video,2 Audio,3 Pdf,4 Text,5 Unknown
    property string displayName: ""
    property var    tags: []
    property var    dateTime
    property bool   randomNext: false

    // Lade-Gating: Die schwere Medien-/PDF-Last erst NACH dem StackView-Übergang
    // anstoßen (Status Active) → die Öffnen-Animation läuft flüssig über einen
    // leichten Platzhalter statt gegen das synchrone PDF-Laden/Erstrendern.
    property bool   _loaded: false
    property int    _startType: -1   // Medientyp des Einstiegspfads (schon VOR _loaded bekannt)

    // PDF gilt als geladen, sobald das Dokument „Ready" ist (erste Seite kann rendern).
    readonly property bool _pdfReady: root._loaded && surface.item !== null && surface.item.docReady === true
    // Ein PDF wird gerade geladen: deckt Öffnen-Animation (Typ aus _startType),
    // Dokument-Parsing (Typ aus root.type) UND Blättern zur nächsten PDF ab.
    readonly property bool pdfLoading: (root._loaded ? root.type : root._startType) === 3 && !root._pdfReady

    // Skeleton ERST nach 300 ms zeigen: lädt das PDF schneller, blitzt nichts auf.
    property bool _skelVisible: false
    Timer { id: skelDelay; interval: 300; repeat: false
            onTriggered: root._skelVisible = root.pdfLoading }
    onPdfLoadingChanged: {
        if (root.pdfLoading) { root._skelVisible = false; skelDelay.restart() }   // (neu) am Laden → 300ms warten
        else { skelDelay.stop(); root._skelVisible = false }                       // fertig / kein PDF → weg
    }

    function _maybeLoad() {
        if (root._loaded) return
        // In einem StackView erst bei Active laden; ohne StackView sofort.
        if (StackView.status === StackView.Active || StackView.view === null) {
            loadPath(startPath)       // setzt type/path …; surface noch inaktiv
            root._loaded = true       // aktiviert den Surface-Loader (onItemChanged setzt source)
            root.forceActiveFocus()
        }
    }

    Component.onCompleted: {
        var r = galleryModel.rowForPath(startPath)
        root._startType = (r >= 0) ? galleryModel.mediaTypeAt(r) : -1
        root.forceActiveFocus(); _maybeLoad()
    }
    Component.onDestruction: releaseCurrent()
    StackView.onStatusChanged: _maybeLoad()

    // ── Laden / Navigation ────────────────────────────────────────────────────
    function loadPath(p) {
        var r = galleryModel.rowForPath(p)
        if (r < 0) r = galleryModel.count > 0 ? 0 : -1
        loadRow(r)
    }

    function loadRow(r) {
        if (r < 0 || r >= galleryModel.count) { root.backRequested(); return }
        releaseCurrent()
        currentRow  = r
        path        = galleryModel.filePathAt(r)
        type        = galleryModel.mediaTypeAt(r)
        displayName = galleryModel.displayNameAt(r)
        tags        = galleryModel.tagsAt(r)
        dateTime    = galleryModel.dateTimeAt(r)

        // Video-Mode "external": im Systemplayer öffnen, Surface bleibt leer.
        if (type === 1 && App.videoPlayback === "external") {
            Viewer.openExternally(path)
        }

        // Inhalt explizit nachziehen: Bei Navigation zwischen Medien GLEICHEN Typs
        // bleibt das Loader-Item dasselbe (onItemChanged feuert nicht) — sonst
        // bliebe der alte Inhalt stehen. Der Loader ist synchron, d. h. nach dem
        // Setzen von 'type' ist surface.item bereits das passende Item.
        if (surface.item && surface.item.hasOwnProperty("source"))
            surface.item.source = path
    }

    function releaseCurrent() {
        if (surface.item && surface.item.release)
            surface.item.release()
    }

    function nextRow() {
        if (galleryModel.count === 0) return
        if (randomNext) { loadRow(galleryModel.randomRow(currentRow)); return }
        loadRow(currentRow + 1 < galleryModel.count ? currentRow + 1 : 0)
    }
    function prevRow() {
        if (galleryModel.count === 0) return
        loadRow(currentRow - 1 >= 0 ? currentRow - 1 : galleryModel.count - 1)
    }

    Rectangle { anchors.fill: parent; color: "#0a0a0a" }

    // Leichter Lade-Indikator (Nicht-PDF-Medien) während des Übergangs. Für PDFs
    // übernimmt das seitenförmige Skeleton (unten) die gesamte Ladephase.
    BusyIndicator {
        anchors.centerIn: parent
        running: !root._loaded && root._startType !== 3
        visible: running
        z: 50
    }

    // ── PDF-Ladeanzeige: zuerst leere (weiße) Seite, ab 300 ms das Skeleton ──────
    //  Beim Öffnen erscheint SOFORT eine leere PDF-Seite (kein Blackscreen). Dauert
    //  das Laden länger als 300 ms, blenden Inhalts-Balken + Shimmer darüber ein
    //  (Skeleton). Größe/Position UND das Seitenweiß spiegeln die spätere PDF-Seite →
    //  nahtloser Übergang. Leicht genug, dass die Öffnen-Animation flüssig bleibt.
    Item {
        id: pdfSkeleton
        anchors.fill: parent
        z: 50
        visible: opacity > 0.01
        opacity: root.pdfLoading ? 1 : 0          // leere Seite SOFORT beim Laden; weg, sobald fertig

        // Viewport, in dem die PDF-Seite NACH dem Laden erscheint — spiegelt die
        // PdfSurface-Geometrie: unter Metadaten-Leiste + PdfSurface-Toolbar (40 px,
        // erscheint mit docReady), oberhalb der unteren Navigation (74 px). Seiten-Fit
        // wie fitMode "page": min(wFit,hFit) gegen (Viewport − 24); Standardmaß A4
        // (595×842 pt) als Annahme, bis das echte Seitenmaß bekannt ist.
        readonly property real _vpTop:    (topBar.visible ? topBar.height : 0) + 40
        readonly property real _vpBottom: root.height - (bottomNav.visible ? 74 : 0)
        readonly property real _vpW:      root.width
        readonly property real _vpH:      Math.max(0, _vpBottom - _vpTop)
        readonly property real _fit:      Math.max(0, Math.min((_vpW - 24) / 595, (_vpH - 24) / 842))

        // Leere PDF-Seite (Seitenweiß) — Größe/Position wie die spätere A4-Seite.
        Rectangle {
            id: skelPage
            width:  595 * pdfSkeleton._fit
            height: 842 * pdfSkeleton._fit
            x: (parent.width - width) / 2
            y: pdfSkeleton._vpTop
            color: "#fbfbfa"
            clip: true

            // Skeleton-Schicht (Inhalts-Balken + Shimmer) — ERST nach 300 ms, weich ein.
            Item {
                id: skelContent
                anchors.fill: parent
                opacity: root._skelVisible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutQuad } }

                // Platzhalter-Inhalt: Titelbalken + Absatzzeilen mit variabler Breite.
                Column {
                    id: skelLines
                    anchors {
                        left: parent.left;  leftMargin:  skelPage.width * 0.11
                        right: parent.right; rightMargin: skelPage.width * 0.11
                        top: parent.top;     topMargin:   skelPage.height * 0.11
                    }
                    spacing: skelPage.height * 0.0265

                    // Titel (breiter, etwas dunkler)
                    Rectangle { width: parent.width * 0.58; height: skelPage.height * 0.040
                                radius: 4; color: "#d9d9d4" }
                    Item { width: 1; height: skelPage.height * 0.028 }   // Absatzabstand

                    Repeater {
                        model: [0.99, 0.95, 0.98, 0.72,
                                0.97, 0.93, 0.99, 0.90, 0.58,
                                0.96, 0.99, 0.91, 0.66,
                                0.98, 0.94, 0.80]
                        Rectangle {
                            width: parent.width * modelData
                            height: skelPage.height * 0.0215
                            radius: 3
                            color: "#e5e5e1"
                        }
                    }
                }

                // Shimmer: heller, leicht geneigter Streifen — sichtbar auf den Balken.
                Rectangle {
                    id: skelSheen
                    height: skelPage.height * 1.5
                    width:  skelPage.width * 0.42
                    y: -skelPage.height * 0.25
                    rotation: 14
                    opacity: 0.9
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.55) }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                    SequentialAnimation on x {
                        running: root._skelVisible
                        loops: Animation.Infinite
                        NumberAnimation { from: -skelPage.width * 0.6; to: skelPage.width * 1.15
                                          duration: 1150; easing.type: Easing.InOutQuad }
                        PauseAnimation { duration: 360 }
                    }
                }
            }
        }
    }

    // ── Medien-Loader (genau ein aktives Medium) ──────────────────────────────
    Loader {
        id: surface
        anchors.fill: parent
        active: root._loaded
        sourceComponent: {
            switch (root.type) {
            case 0:  return imageComponent
            case 1:  return (App.videoPlayback === "external") ? externalNote : videoComponent
            case 2:  return videoComponent      // Audio: VideoSurface mit Audio-Out
            case 3:  return pdfComponent
            case 4:  return textComponent
            default: return unsupportedNote
            }
        }
        onItemChanged: if (item && item.hasOwnProperty("source")) item.source = root.path
    }

    // ── PDF-Chrome unterhalb der globalen Leisten halten (kein Overlap) ────────
    //  Reserviert die obere (topBar) und untere (Datei-Navigation) Hoehe in der
    //  PdfSurface, sodass deren eigene Toolbar/Thumbnails NICHT mit der globalen
    //  FullscreenViewer-Chrome ueberlappen.
    Binding {
        target: surface.item
        property: "topInset"
        value: topBar.visible ? topBar.height : 0
        when: surface.item !== null && (root.type === 3 || root.type === 4)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: surface.item
        property: "bottomInset"
        value: bottomNav.visible ? 74 : 0
        when: surface.item !== null && (root.type === 3 || root.type === 4)
        restoreMode: Binding.RestoreNone
    }

    // ── Bild ──────────────────────────────────────────────────────────────────
    Component {
        id: imageComponent
        Item {
            property string source: ""
            function release() { img.source = "" }
            // Rohen Dateipfad in eine korrekt kodierte file://-URL wandeln.
            // Direkte Zuweisung des rohen Pfades an Image.source schlägt fehl, da
            // die Basis-URL der Komponente "qrc:" ist → relative Auflösung zu
            // "qrc:/home/…" ("Cannot open" → Blackscreen). Betrifft ALLE Bilder;
            // App.fileUrl() (QUrl::fromLocalFile) kodiert zudem Sonderzeichen
            // (Leerzeichen, CJK, …) korrekt.
            onSourceChanged: { img.scale = 1.0; img.x = 0; img.y = 0; img.source = source ? App.fileUrl(source) : "" }

            Image {
                id: img
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                smooth: true
                mipmap: true
                transformOrigin: Item.Center
            }
            PinchHandler {
                target: img
                minimumScale: 0.1
                maximumScale: 10.0
            }
            WheelHandler {
                target: null
                onWheel: function(ev) {
                    var f = ev.angleDelta.y > 0 ? 1.15 : 1/1.15
                    img.scale = Math.max(0.1, Math.min(img.scale * f, 10.0))
                    if (img.scale <= 1.0) { img.x = 0; img.y = 0 }
                }
            }
            DragHandler {
                target: img
                enabled: img.scale > 1.0
            }
        }
    }

    // ── Video / Audio ───────────────────────────────────────────────────────
    Component { id: videoComponent; VideoSurface {} }

    // ── PDF ───────────────────────────────────────────────────────────────────
    Component { id: pdfComponent; PdfSurface {} }

    // ── Text ──────────────────────────────────────────────────────────────────
    Component { id: textComponent; TextSurface {} }

    // ── Hinweise ────────────────────────────────────────────────────────────
    Component {
        id: externalNote
        Item {
            property string source: ""
            function release() {}
            Text {
                anchors.centerIn: parent
                text: App.uiText(App.language, "ViewerOpenedExternal")
                color: "#c8dbd5"; font.pixelSize: 16
            }
        }
    }
    Component {
        id: unsupportedNote
        Item {
            property string source: ""
            function release() {}
            Text {
                anchors.centerIn: parent
                text: App.uiText(App.language, "ViewerNoRenderer")
                color: "#888"; font.pixelSize: 15
            }
        }
    }

    // ── Obere Leiste: Zurück / Name / Datum / Tags ────────────────────────────
    Rectangle {
        id: topBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: App.optionsVisible ? 96 : 52
        color: Qt.rgba(0, 0, 0, 0.55)
        opacity: root.barOpacity
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6

            Item {
                width: parent.width
                height: 34

                ToolButton {
                    id: backBtn
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\u2190"
                    font.pixelSize: 18
                    onClicked: root.backRequested()
                }

                // Buttons bündig am rechten Rand
                ChromeBtn {
                    id: diceBtn
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "dice"
                    tip: App.uiText(App.language, "ViewerRandom")
                    active: root.randomNext
                    onActivated: root.randomNext = !root.randomNext
                }
                ChromeBtn {
                    id: calBtn
                    anchors.right: diceBtn.left; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "calendar"
                    tip: App.uiText(App.language, "MetaTitle")
                    onActivated: dateEditor.openWith(root.dateTime)
                }

                TextField {
                    id: nameEdit
                    anchors.left: backBtn.right; anchors.leftMargin: 8
                    anchors.right: calBtn.left; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.displayName
                    color: "white"
                    font.pixelSize: 14; font.bold: true
                    background: Rectangle {
                        color: "transparent"
                        border.color: nameEdit.activeFocus ? App.themeAccent : "transparent"
                        border.width: 1; radius: 3
                    }
                    onAccepted: {
                        var t = text.trim()
                        if (t.length > 0 && t !== root.displayName)
                            mediaModel.renameItem(root.path, t)
                    }
                }
            }

            Row {
                visible: App.optionsVisible
                width: parent.width
                spacing: 12

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.dateTime ? Qt.formatDateTime(root.dateTime, "yyyy-MM-dd hh:mm") : ""
                    color: Qt.rgba(1, 1, 1, 0.8); font.pixelSize: 11
                }

                TagBar {
                    width: parent.width - 200
                    fileName: root.path.length > 0 ? root.path.substring(
                                  Math.max(root.path.lastIndexOf("/"), root.path.lastIndexOf("\\")) + 1) : ""
                }
            }
        }
    }

    // ── Untere Leiste: prev / next ────────────────────────────────────────────
    Row {
        id: bottomNav
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 20
        spacing: 16
        opacity: root.barOpacity
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Rectangle {
            width: 56; height: 44; radius: 8
            color: prevHover.hovered ? "#1f4d47" : Qt.rgba(0,0,0,0.55)
            border.color: "#3a4a48"; border.width: 1
            Text { anchors.centerIn: parent; text: "\u25C0"; color: "#c8dbd5"; font.pixelSize: 18 }
            HoverHandler { id: prevHover }
            TapHandler { onTapped: root.prevRow() }
        }
        Rectangle {
            width: 80; height: 44; radius: 8
            color: Qt.rgba(0,0,0,0.55)
            Text {
                anchors.centerIn: parent
                text: (root.currentRow + 1) + " / " + galleryModel.count
                color: "#c8dbd5"; font.pixelSize: 12
            }
        }
        Rectangle {
            width: 56; height: 44; radius: 8
            color: nextHover.hovered ? "#1f4d47" : Qt.rgba(0,0,0,0.55)
            border.color: "#3a4a48"; border.width: 1
            Text { anchors.centerIn: parent; text: "\u25B6"; color: "#c8dbd5"; font.pixelSize: 18 }
            HoverHandler { id: nextHover }
            TapHandler { onTapped: root.nextRow() }
        }
    }

    // ── Auto-Hide der Leisten ──────────────────────────────────────────────────
    property real barOpacity: 1.0
    HoverHandler {
        id: viewHover
        onPointChanged: { root.barOpacity = 1.0; barTimer.restart() }
    }
    Timer {
        id: barTimer
        interval: 2800
        running: true
        // Nur Bilder blenden die Leisten automatisch aus; PDFs behalten ihre
        // Chrome dauerhaft (stabile, einheitliche Struktur ohne Layout-Springen).
        onTriggered: root.barOpacity = (root.type === 0) ? 0.0 : 1.0
    }

    // ── Datum-Editor ───────────────────────────────────────────────────────────
    MetadataDateEditor {
        id: dateEditor
        onAccepted: function(dt) {
            var fn = root.path.substring(Math.max(root.path.lastIndexOf("/"),
                                                  root.path.lastIndexOf("\\")) + 1)
            App.setCustomDate(fn, dt)
            root.dateTime = dt
        }
        onCleared: {
            var fn = root.path.substring(Math.max(root.path.lastIndexOf("/"),
                                                  root.path.lastIndexOf("\\")) + 1)
            App.clearCustomDate(fn)
        }
    }

    // ── Tastatur ────────────────────────────────────────────────────────────
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape)      { root.backRequested(); event.accepted = true }
        else if (event.key === Qt.Key_Left)   { root.prevRow();       event.accepted = true }
        else if (event.key === Qt.Key_Right)  { root.nextRow();       event.accepted = true }
    }

    // ── Minimalistischer Chrome-Button (flach, monochrom, theme-Akzent) ───────
    //  Zeichnet sein Icon intern (kind) — bewusst keine farbigen Emoji mehr und
    //  kein default-children-Alias (vermeidet Selbstreferenz).
    component ChromeBtn: Rectangle {
        id: cb
        property string kind: ""
        property bool active: false
        property string tip: ""
        signal activated()
        width: 32; height: 30; radius: 6
        color: active ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.28)
             : (cbHover.hovered ? Qt.rgba(1, 1, 1, 0.14) : "transparent")
        border.width: 1
        border.color: active ? App.themeAccent : Qt.rgba(1, 1, 1, 0.18)

        Item {
            anchors.centerIn: parent
            width: 18; height: 18

            // Kalender (Datum)
            Item {
                anchors.fill: parent
                visible: cb.kind === "calendar"
                Rectangle { anchors.fill: parent; radius: 2; color: "transparent"
                            border.color: "#e8efed"; border.width: 1.4 }
                Rectangle { anchors.top: parent.top; anchors.left: parent.left
                            anchors.right: parent.right; height: 5; radius: 1; color: "#e8efed" }
                Rectangle { x: 4;  y: -2; width: 2; height: 4; radius: 1; color: "#e8efed" }
                Rectangle { x: 12; y: -2; width: 2; height: 4; radius: 1; color: "#e8efed" }
            }

            // Würfel (Zufall, 5 Augen)
            Item {
                anchors.fill: parent
                visible: cb.kind === "dice"
                Rectangle { anchors.fill: parent; radius: 4; color: "transparent"
                            border.color: "#e8efed"; border.width: 1.4 }
                Rectangle { x: 3;   y: 3;   width: 3; height: 3; radius: 1.5; color: "#e8efed" }
                Rectangle { x: 12;  y: 3;   width: 3; height: 3; radius: 1.5; color: "#e8efed" }
                Rectangle { x: 7.5; y: 7.5; width: 3; height: 3; radius: 1.5; color: "#e8efed" }
                Rectangle { x: 3;   y: 12;  width: 3; height: 3; radius: 1.5; color: "#e8efed" }
                Rectangle { x: 12;  y: 12;  width: 3; height: 3; radius: 1.5; color: "#e8efed" }
            }
        }

        HoverHandler { id: cbHover }
        TapHandler { onTapped: cb.activated() }
        ToolTip.text: cb.tip
        ToolTip.visible: cbHover.hovered && cb.tip.length > 0
    }
}
