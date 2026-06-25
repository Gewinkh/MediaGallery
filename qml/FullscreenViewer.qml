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

    function _maybeLoad() {
        if (root._loaded) return
        // In einem StackView erst bei Active laden; ohne StackView sofort.
        if (StackView.status === StackView.Active || StackView.view === null) {
            loadPath(startPath)       // setzt type/path …; surface noch inaktiv
            root._loaded = true       // aktiviert den Surface-Loader (onItemChanged setzt source)
            root.forceActiveFocus()
        }
    }

    Component.onCompleted: { root.forceActiveFocus(); _maybeLoad() }
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

    // Leichter Lade-Indikator während des Übergangs (vor _loaded). Kein schweres
    // Item → die Animation bleibt flüssig.
    BusyIndicator {
        anchors.centerIn: parent
        running: !root._loaded
        visible: running
        z: 50
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
            onSourceChanged: { img.scale = 1.0; img.x = 0; img.y = 0; img.source = source }

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
                text: "Im externen Player geöffnet."
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
                text: "Kein Vorschau-Renderer für diesen Typ."
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
                    tip: "Zufällig"
                    active: root.randomNext
                    onActivated: root.randomNext = !root.randomNext
                }
                ChromeBtn {
                    id: calBtn
                    anchors.right: diceBtn.left; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "calendar"
                    tip: "Datum bearbeiten"
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
