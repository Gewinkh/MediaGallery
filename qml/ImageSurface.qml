pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  ImageSurface.qml — Bild-Anzeige mit PDF-artigem Zoom/Pan UND dezentralem
//  Bild-Editor (nicht-destruktive Annotationen). Ersetzt die frühere simple
//  img.scale-Komponente im FullscreenViewer.
//
//  DEZENTRAL: jede Kachel (Split-View) hat ihre EIGENE ImageEditController-
//  Instanz (via qmlRegisterType) → unabhängige Auswahl/Undo/Export je Bild.
//
//  ANSICHT: Ein Flickable trägt das Bild in Anzeige-Pixeln (natürliche Größe ×
//  dispScale). Pan = Ziehen (Ansichtsmodus oder Auswahl-Werkzeug); Zoom =
//  Mausrad (mittig verankert, wie PdfSurface). „An Fenster anpassen" / „100 %".
//
//  EDIT: Werkzeuge (Auswahl/Text/Stift/Pfeil/Rechteck/Ellipse) im andockbaren
//  Panel; die schwebende Toolbar erscheint über der Auswahl. Sidecar/Export
//  übernimmt der Controller (WYSIWYG, Quellformat).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    // ── Vom FullscreenViewer gesetzt ──────────────────────────────────────────
    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0

    // Dezentraler Editor-Controller DIESER Kachel.
    property ImageEditController editCtl: ImageEditController {}

    // ── Ansichtszustand ───────────────────────────────────────────────────────
    property real dispScale: 1.0                 // angezeigte Pixel je Bild-Pixel
    property bool _fitMode: true                 // an Fenster angepasst?
    readonly property real minScale: 0.05
    readonly property real maxScale: 16.0

    // ── Editor-Zustand (von Delegates/Toolbar/Panel gelesen) ──────────────────
    property bool notesVisible: true
    property int  editCommitRev: 0               // Bump → offene Textbearbeitungen schließen
    property int  _autoEditId: -1                // frisch erzeugte Text-Notiz → sofort editieren
    property bool editPanelVisible: false

    readonly property bool docReady: img.status === Image.Ready
    readonly property int  natW: editCtl.imageWidth  > 0 ? editCtl.imageWidth  : img.implicitWidth
    readonly property int  natH: editCtl.imageHeight > 0 ? editCtl.imageHeight : img.implicitHeight
    readonly property real dispW: Math.max(1, natW * dispScale)
    readonly property real dispH: Math.max(1, natH * dispScale)
    readonly property real fitScale: (natW > 0 && natH > 0 && view.width > 0 && view.height > 0)
                                     ? Math.min(view.width / natW, view.height / natH) : 1.0

    function commitEditing() { root.editCommitRev++ }
    function startImageExport() { root.editCtl.exportImage() }
    function release() {
        root.commitEditing()
        root.editCtl.releaseDocument()
        img.source = ""
    }

    // ── Zoom / Fit ────────────────────────────────────────────────────────────
    function _clampScale(s) { return Math.max(root.minScale, Math.min(s, root.maxScale)) }
    function setDispScale(s) {
        const ns = _clampScale(s)
        if (Math.abs(ns - root.dispScale) < 1e-6) return
        const ratio = ns / root.dispScale
        const cx = view.contentX + view.width / 2
        const cy = view.contentY + view.height / 2
        root._fitMode = false
        root.dispScale = ns
        view.contentX = Math.max(0, Math.min(cx * ratio - view.width / 2,  Math.max(0, view.contentWidth  - view.width)))
        view.contentY = Math.max(0, Math.min(cy * ratio - view.height / 2, Math.max(0, view.contentHeight - view.height)))
    }
    function zoomIn()  { setDispScale(root.dispScale * 1.15) }
    function zoomOut() { setDispScale(root.dispScale / 1.15) }
    function fitToWindow() { root._fitMode = true; root.dispScale = root.fitScale; view.contentX = 0; view.contentY = 0 }
    function actualSize()  { setDispScale(1.0) }

    // Fenster-Resize: im Fit-Modus neu einpassen; sonst dispScale HALTEN (kein
    // Springen — der Flickable klemmt die Position selbst neu). Behebt das
    // Zurückspringen beim Größenändern/Hinzufügen einer Datei in der Split-Ansicht.
    onFitScaleChanged: if (root._fitMode) root.dispScale = root.fitScale

    onSourceChanged: {
        root.editCtl.setDocument(source)
        img.source = source ? App.fileUrl(source) : ""
        root._fitMode = true
        root.editPanelVisible = false
        // dispScale nach dem Laden einpassen (natW/natH stehen dann sicher).
        Qt.callLater(root.fitToWindow)
    }

    Rectangle { anchors.fill: parent; color: App.themeBackground }

    // ══ Ansichtsfläche (unter der Toolbar, über der unteren Navigation) ═══════
    Flickable {
        id: view
        anchors { left: parent.left; right: parent.right
                  top: parent.top
                  topMargin: root.topInset + (root.docReady ? 40 : 0)
                             + ((root.editCtl.editMode && root.editPanelVisible && PdfEdit.panelOnTop)
                                ? topPanel.implicitHeight : 0)
                  bottom: parent.bottom; bottomMargin: root.bottomInset }
        clip: true
        contentWidth:  Math.max(width,  root.dispW)
        contentHeight: Math.max(height, root.dispH)
        boundsBehavior: Flickable.StopAtBounds
        // Ziehen schwenkt im Ansichtsmodus ODER mit dem Auswahl-Werkzeug; beim
        // Zeichnen greift die drawArea (preventStealing) statt des Flickable.
        interactive: !root.editCtl.editMode || root.editCtl.tool === 0

        ScrollBar.vertical:   ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            id: imgWrap
            width: root.dispW; height: root.dispH
            x: Math.max(0, (view.contentWidth  - root.dispW) / 2)
            y: Math.max(0, (view.contentHeight - root.dispH) / 2)

            Image {
                id: img
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                smooth: true
                mipmap: true
                autoTransform: false               // gespeicherte Orientierung = Export = WYSIWYG
                sourceSize: (root.natW > 0 && root.natH > 0) ? Qt.size(root.natW, root.natH) : undefined
                // Fallback, falls QImageReader das Format nicht messen konnte.
                onStatusChanged: if (status === Image.Ready)
                                     root.editCtl.setImageSize(implicitWidth, implicitHeight)
            }

            // Leeren Bereich antippen hebt die Auswahl auf (nur Auswahl-Werkzeug;
            // Ziehen bleibt dem Flickable = Schwenken).
            TapHandler {
                enabled: root.editCtl.editMode && root.editCtl.tool === 0
                onTapped: root.editCtl.selectedId = -1
            }

            // ── Annotationen ──────────────────────────────────────────────────
            Repeater {
                model: root.editCtl.annModel
                delegate: ImageEditBox {
                    imgScale: root.dispScale
                    imgWpx: root.natW
                    imgHpx: root.natH
                    surface: root
                }
            }

            // ── Zeichnen / Text erstellen (Edit-Modus, Werkzeug ≠ Auswahl) ────
            MouseArea {
                id: drawArea
                anchors.fill: parent
                enabled: root.editCtl.editMode && root.editCtl.tool !== 0
                visible: enabled
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                cursorShape: Qt.CrossCursor
                property int _drawId: -1
                onPressed: (m) => {
                    root.commitEditing()
                    root.notesVisible = true
                    const ix = m.x / root.dispScale
                    const iy = m.y / root.dispScale
                    if (root.editCtl.tool === 1) {          // Text
                        root._autoEditId = root.editCtl.addText(ix, iy)
                        _drawId = -1
                    } else {                                // Stift/Pfeil/Rechteck/Ellipse
                        _drawId = root.editCtl.beginDraw(root.editCtl.tool - 1, ix, iy)
                    }
                }
                onPositionChanged: (m) => {
                    if (_drawId >= 0)
                        root.editCtl.updateDraw(_drawId, m.x / root.dispScale, m.y / root.dispScale)
                }
                onReleased: {
                    if (_drawId >= 0) { root.editCtl.endDraw(_drawId); _drawId = -1 }
                }
            }

            // ── Schwebende Format-Toolbar über der Auswahl ────────────────────
            ImageEditToolbar {
                imgScale: root.dispScale
                contentW: imgWrap.width
                contentH: imgWrap.height
                surface: root
            }
        }

        // Mausrad zoomt (Bild hat keinen Scrollinhalt) — mittig verankert.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel: (wheel) => {
                if (wheel.angleDelta.y > 0) root.zoomIn()
                else if (wheel.angleDelta.y < 0) root.zoomOut()
                wheel.accepted = true
            }
        }
    }

    // ── Ladefehler-Hinweis ────────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: img.status === Image.Error
        text: App.uiText(App.language, "ImageLoadError")
        color: "#ff8a80"; font.pixelSize: 14; z: 5
    }

    // ══ Toolbar (unter der globalen Leiste) ═══════════════════════════════════
    Rectangle {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset }
        height: 40
        color: App.themeToolbarBg
        visible: root.docReady
        z: 10
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1; color: App.themeBorder }

        component TBtn: Rectangle {
            id: tb
            property string glyph: ""
            property string tip: ""
            property bool checked: false
            property bool danger: false
            property bool disabledLook: false
            signal activated()
            width: 30; height: 28; radius: 6
            opacity: disabledLook ? 0.4 : 1.0
            color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.28)
                 : (tbHover.hovered && !disabledLook ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16) : "transparent")
            border.color: checked ? App.themeAccent : "transparent"; border.width: 1
            Text { anchors.centerIn: parent; text: tb.glyph
                   color: tb.danger ? "#e05a5a" : App.themeTextPrimary; font.pixelSize: 14 }
            HoverHandler { id: tbHover; enabled: !tb.disabledLook }
            TapHandler { enabled: !tb.disabledLook; onTapped: tb.activated() }
            ToolTip.text: tb.tip; ToolTip.visible: tbHover.hovered && tb.tip.length > 0
        }

        Row {
            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
            spacing: 4
            TBtn { glyph: "\u2212"; tip: App.uiText(App.language, "ImageZoomOut"); onActivated: root.zoomOut() }
            TBtn { glyph: "\u2317"; tip: App.uiText(App.language, "ImageFitWindow"); checked: root._fitMode
                   onActivated: root.fitToWindow() }
            TBtn { glyph: "1:1"; tip: App.uiText(App.language, "ImageActualSize")
                   onActivated: root.actualSize() }
            TBtn { glyph: "+"; tip: App.uiText(App.language, "ImageZoomIn"); onActivated: root.zoomIn() }
            Text { anchors.verticalCenter: parent.verticalCenter; width: 46; horizontalAlignment: Text.AlignHCenter
                   text: Math.round(root.dispScale * 100) + "%"; color: App.themeTextMuted; font.pixelSize: 11 }
        }

        Row {
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            spacing: 4
            // Editor-Regler (nur im Edit-Modus)
            TranslitButton { visible: root.editCtl.editMode; anchors.verticalCenter: parent.verticalCenter }
            TBtn { visible: root.editCtl.editMode; glyph: "\u25C9"; checked: !root.notesVisible
                   tip: App.uiText(App.language, "PdfEditNotesToggleTip")
                   onActivated: root.notesVisible = !root.notesVisible }
            TBtn { visible: root.editCtl.editMode; glyph: "\u21B6"; tip: App.uiText(App.language, "PdfEditUndoTip")
                   disabledLook: !root.editCtl.canUndo; onActivated: root.editCtl.undo() }
            TBtn { visible: root.editCtl.editMode; glyph: "\u21B7"; tip: App.uiText(App.language, "PdfEditRedoTip")
                   disabledLook: !root.editCtl.canRedo; onActivated: root.editCtl.redo() }
            TBtn { visible: root.editCtl.editMode; glyph: "\u2398"; tip: App.uiText(App.language, "ImageEditPasteBtn")
                   disabledLook: !root.editCtl.hasClipboard; onActivated: root.editCtl.paste() }
            Rectangle { visible: root.editCtl.editMode; width: 1; height: 18; color: App.themeBorder
                        anchors.verticalCenter: parent.verticalCenter }
            // Edit-Modus umschalten
            TBtn { glyph: "\u270E"; checked: root.editCtl.editMode
                   tip: App.uiText(App.language, "ImageEditToggle")
                   onActivated: {
                       root.editCtl.editMode = !root.editCtl.editMode
                       if (!root.editCtl.editMode) root.editPanelVisible = false
                   } }
        }
    }

    // ══ Andockbares Panel (rechts ODER oben; teilt PdfEdit.panelOnTop) ════════
    //  Rechte Seitenleiste
    ImageEditPanel {
        id: sidePanel
        visible: root.editCtl.editMode && root.editPanelVisible && !PdfEdit.panelOnTop
        horizontal: false
        surface: root
        width: implicitWidth
        anchors { right: parent.right; top: parent.top; topMargin: root.topInset + 40
                  bottom: parent.bottom; bottomMargin: root.bottomInset }
        z: 11
        onCloseRequested: root.editPanelVisible = false
    }
    //  Obere Leiste (Ribbon)
    ImageEditPanel {
        id: topPanel
        visible: root.editCtl.editMode && root.editPanelVisible && PdfEdit.panelOnTop
        horizontal: true
        surface: root
        height: implicitHeight
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset + 40 }
        z: 11
        onCloseRequested: root.editPanelVisible = false
    }
    // Edit-Modus/Auswahl öffnet das Panel (wie PDF-Editor).
    Connections {
        target: root.editCtl
        function onEditModeChanged() {
            if (root.editCtl.editMode) {
                root.notesVisible = true
                root.editPanelVisible = true
            } else {
                root.editPanelVisible = false
            }
        }
        function onSelectedIdChanged() {
            if (root.editCtl.selectedId >= 0) root.editPanelVisible = true
        }
        function onExportFinished(ok, targetPath, errorText) {
            exportToast.show(ok, targetPath, errorText)
        }
    }

    // ══ Export-Toast ══════════════════════════════════════════════════════════
    Rectangle {
        id: exportToast
        function show(ok, targetPath, errorText) {
            exportToast._ok = ok
            exportToast._msg = ok ? (App.uiText(App.language, "PdfEditExportDoneToast") + "  "
                                     + targetPath.substring(targetPath.lastIndexOf('/') + 1))
                                  : (App.uiText(App.language, "PdfEditExportFailedToast") + " " + errorText)
            exportToast.opacity = 1.0
            toastTimer.restart()
        }
        property bool _ok: true
        property string _msg: ""
        anchors { bottom: parent.bottom; bottomMargin: root.bottomInset + 16; horizontalCenter: parent.horizontalCenter }
        width: Math.min(root.width - 40, toastText.implicitWidth + 28)
        height: 34; radius: 8; z: 20
        opacity: 0.0
        visible: opacity > 0
        color: App.themeToolbarBg
        border.color: _ok ? App.themeAccent : "#e05a5a"; border.width: 1
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Text { id: toastText; anchors.centerIn: parent; text: exportToast._msg
               color: App.themeTextPrimary; font.pixelSize: 12; elide: Text.ElideMiddle
               width: parent.width - 20; horizontalAlignment: Text.AlignHCenter }
        Timer { id: toastTimer; interval: 3800; onTriggered: exportToast.opacity = 0.0 }
        MouseArea { anchors.fill: parent; onClicked: exportToast.opacity = 0.0 }
    }

    // Busy-Anzeige während des Exports.
    Rectangle {
        anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.25); visible: root.editCtl.busy; z: 19
        Text { anchors.centerIn: parent; text: App.uiText(App.language, "PdfEditExportingToast")
               color: "#ffffff"; font.pixelSize: 14 }
    }

    // ══ Tastenkürzel ══════════════════════════════════════════════════════════
    //  Notizen-Toggle (Alt+Q, beide Modi).
    Shortcut {
        sequence: "Alt+Q"
        enabled: root.docReady
        onActivated: root.notesVisible = !root.notesVisible
    }
    //  Entf löscht die ausgewählte Annotation (nur Edit-Modus, nicht beim Tippen).
    Shortcut {
        sequence: "Delete"
        enabled: root.docReady && root.editCtl.editMode
                 && root.editCtl.selectedId >= 0 && !root.editCtl.textEditing
        onActivated: { root.commitEditing(); root.editCtl.removeAnn(root.editCtl.selectedId) }
    }
    //  Kopieren/Einfügen der Annotation (Edit-Modus).
    Shortcut {
        sequence: "Ctrl+C"
        enabled: root.docReady && root.editCtl.editMode
                 && root.editCtl.selectedId >= 0 && !root.editCtl.textEditing
        onActivated: { root.commitEditing(); root.editCtl.copySelected() }
    }
    Shortcut {
        sequence: "Ctrl+V"
        enabled: root.docReady && root.editCtl.editMode && root.editCtl.hasClipboard
                 && !root.editCtl.textEditing
        onActivated: root.editCtl.paste()
    }
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: root.docReady && root.editCtl.editMode && root.editCtl.canUndo
        onActivated: root.editCtl.undo()
    }
    Shortcut {
        sequence: "Ctrl+Shift+Z"
        enabled: root.docReady && root.editCtl.editMode && root.editCtl.canRedo
        onActivated: root.editCtl.redo()
    }
}
