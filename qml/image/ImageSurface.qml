pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// Bild-Anzeige mit Zoom/Pan und dezentralem Editor: jede Kachel hat ihre EIGENE ImageEditController-Instanz,
// also unabhängige Auswahl, Undo und Export je Bild. Ein Flickable trägt das Bild in Anzeige-Pixeln, Zoom
// ist mittig verankert wie in PdfSurface. Sidecar und Export übernimmt der Controller.
Item {
    id: root
    objectName: "imageSurface"        // Prüfstand: MG_BENCH_INVOKE/MG_BENCH_WHEEL

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0
    //  Nur in der aktiven Split-View-Kachel dürfen die fensterweiten Kürzel
    //  feuern (sonst Mehrdeutigkeit bei mehreren offenen Bildern). Einzel-View
    //  = immer true (Vorgabe).
    property bool   paneActive: true

    property ImageEditController editCtl: ImageEditController {}

    property real dispScale: 1.0                 // angezeigte Pixel je Bild-Pixel
    property bool _fitMode: true                 // an Fenster angepasst?
    readonly property real minScale: 0.05
    readonly property real maxScale: 16.0

    property bool notesVisible: true
    property int  editCommitRev: 0               // Bump -> offene Textbearbeitungen schließen
    property int  _autoEditId: -1                // frisch erzeugte Text-Notiz -> sofort editieren
    property bool editPanelVisible: false

    // "Ein Bild ist DA" - nicht "gerade wird nicht geladen": beim Stufenwechsel bleibt das alte Bild
    // sichtbar; ginge `docReady` auf false, verschwände die Leiste und der Inhalt spränge 40 px.
    readonly property bool docReady: img.status === TiledImage.Ready || _everReady
    property bool _everReady: false
    // Beim Bildwechsel steht das alte Bild unbewegt, bis das neue gezeigt wird (`img.current`): Mass, Massstab und
    // Leiste bleiben solange die des alten.
    property bool holdingOld: false
    property int  _heldW: 0
    property int  _heldH: 0
    readonly property int  srcW: editCtl.imageWidth  > 0 ? editCtl.imageWidth  : img.implicitWidth
    readonly property int  srcH: editCtl.imageHeight > 0 ? editCtl.imageHeight : img.implicitHeight
    readonly property int  natW: holdingOld ? _heldW : srcW
    readonly property int  natH: holdingOld ? _heldH : srcH
    readonly property real dispW: Math.max(1, natW * dispScale)
    readonly property real dispH: Math.max(1, natH * dispScale)
    readonly property real fitScale: (natW > 0 && natH > 0 && view.width > 0 && view.height > 0)
                                     ? Math.min(view.width / natW, view.height / natH) : 1.0
    // Ein 27-MP-Foto sind 108 MB im Speicher, davon zeigt ein 1400 px breites Fenster nicht ein Zwanzigstel.
    // Dekodiert wird in STUFEN: Zoomen innerhalb einer Stufe dekodiert nicht neu, bei 100 % ist es das volle Bild.
    readonly property real decodeWant: {
        if (editCtl.imageWidth <= 0 || editCtl.imageHeight <= 0) return 0
        if (srcW <= 0 || srcH <= 0) return 0
        if (view.width <= 0 || view.height <= 0) return Math.min(1024, Math.max(srcW, srcH))
        const nat  = Math.max(srcW, srcH)
        const fit  = Math.min(view.width / srcW, view.height / srcH)
        //  Waehrend das alte Bild steht, wird das neue eingepasst geladen.
        const s    = holdingOld ? fit : dispScale
        const need = nat * s
        if (need >= nat) return nat
        //  Liegt das ganze Bild in der Ansicht, genau in Anzeigegroesse (wie dispW gerechnet): es wird 1:1 gezeigt.
        //  Eine Stufe knapp darueber wurde ein zweites Mal abgetastet und sichtbar weicher.
        if (s <= fit) return need
        let stage = 1024
        while (stage < need) stage *= 2
        return Math.min(stage, nat)
    }

    // Was WIRKLICH dekodiert wird - gegen "beim Ziehen am Fensterrand ist das Bild kurz weg" (gemessen: 2
    // Stufenwechsel und 24 % der Messpunkte ohne fertiges Bild in EINEM Zug). Wächst nur, folgt der Rechnung erst
    // nach Ruhe. ZUGEWIESEN statt gebunden, sonst meldet Qt den Ring decodeDim -> sourceSize -> natW als Schleife.
    property real decodeDim: 0

    onDecodeWantChanged: {
        if (root.decodeDim <= 0) { root.decodeDim = root.decodeWant; return }
        if (root.decodeWant > root.decodeDim) { decodeShrink.stop(); decodeGrow.restart() }
        else if (root.decodeWant < root.decodeDim) decodeShrink.restart()
        else decodeShrink.stop()
    }
    Timer {
        id: decodeGrow
        interval: 150
        onTriggered: if (root.decodeWant > root.decodeDim)
                         root.decodeDim = root.decodeWant
    }
    // Zurueck auf die kleinere Stufe erst nach laengerer Ruhe: nach einem Zoom zurueck auf Einpassen hielt die volle
    // Stufe sonst 80 MB bis zum Schliessen; kurzes Hin und Her dekodiert so nicht jedes Mal neu.
    Timer {
        id: decodeShrink
        interval: 1000
        onTriggered: if (root.decodeWant > 0 && root.decodeWant < root.decodeDim)
                         root.decodeDim = root.decodeWant
    }

    // Der Software-Rasterer filtert jedes Bild bilinear ueber die ganze Flaeche (beim Schwenken die halbe CPU eines
    // Kerns je Bild); in Bewegung ungefiltert, in Ruhe wieder glatt.
    readonly property bool _rough: GraphicsInfo.api === GraphicsInfo.Software && moveQuiet.running
    Timer { id: moveQuiet; interval: 150 }
    // Zoomschritte zaehlen nicht: jeder zeigt ein stehendes Bild, ungefiltert sah es 150 ms lang "frittiert" aus.
    property bool _zoomStep: false
    function _moved() { if (GraphicsInfo.api === GraphicsInfo.Software && !_zoomStep) moveQuiet.restart() }

    function commitEditing() { root.editCommitRev++ }
    function startImageExport() { root.editCtl.exportImage() }
    function release() {
        root.commitEditing()
        //  Beim Blaettern folgt gleich die naechste Quelle: das alte Bild haelt sein Mass. Sonst galt kurz die Stufe
        //  als Bildgroesse, die Flaeche sprang hin und zurueck, und Software zeichnete es ungefiltert ("frittiert").
        root._heldW = root.natW
        root._heldH = root.natH
        root.holdingOld = root.docReady && root.natW > 0 && root.natH > 0
        root.editCtl.releaseDocument()
        img.source = ""
    }

    function _clampScale(s) { return Math.max(root.minScale, Math.min(s, root.maxScale)) }
    function setDispScale(s) {
        const ns = _clampScale(s)
        if (Math.abs(ns - root.dispScale) < 1e-6) return
        const ratio = ns / root.dispScale
        const cx = view.contentX + view.width / 2
        const cy = view.contentY + view.height / 2
        root._fitMode = false
        root._zoomStep = true
        root.dispScale = ns
        view.contentX = Math.max(0, Math.min(cx * ratio - view.width / 2,  Math.max(0, view.contentWidth  - view.width)))
        view.contentY = Math.max(0, Math.min(cy * ratio - view.height / 2, Math.max(0, view.contentHeight - view.height)))
        root._zoomStep = false
    }
    function zoomIn()  { setDispScale(root.dispScale * 1.15) }
    function zoomOut() { setDispScale(root.dispScale / 1.15) }
    function fitToWindow() {
        root._fitMode = true
        root._zoomStep = true
        root.dispScale = root.fitScale
        view.contentX = 0
        view.contentY = 0
        root._zoomStep = false
    }
    function actualSize()  { setDispScale(1.0) }

    // Folgt dem Fenster beim Ziehen: das ist Bewegung, anders als ein Zoomschritt.
    onFitScaleChanged: if (root._fitMode) { root.dispScale = root.fitScale; root._moved() }

    onSourceChanged: {
        root._heldW = root.natW
        root._heldH = root.natH
        root.holdingOld = root.docReady && root.natW > 0 && root.natH > 0
        root.editCtl.setDocument(source)
        decodeGrow.stop()
        decodeShrink.stop()
        if (!root.holdingOld) {
            root._everReady = false
            //  Erst einpassen, dann die Stufe waehlen: sonst rechnet sie mit dem Massstab des vorigen Bildes (bis
            //  zum vollen Bild) und dekodiert nach dem Einpassen ein zweites Mal.
            root._fitMode = true
            root.dispScale = root.fitScale
        }
        root.decodeDim = root.decodeWant
        img.source = source
        root.editPanelVisible = false
        if (!root.holdingOld) Qt.callLater(root.fitToWindow)
    }
    function _releaseOld() {
        if (!root.holdingOld) return
        root._zoomStep = true                       // der Tausch ist ein Sprung, keine Bewegung
        root._fitMode = true
        root.holdingOld = false
        root._zoomStep = false
        root.fitToWindow()
    }

    Rectangle { anchors.fill: parent; color: App.themeBackground }

    Flickable {
        id: view
        anchors { left: parent.left; right: parent.right
                  top: parent.top
                  topMargin: root.topInset + (root.docReady ? 40 : 0)
                             + ((root.editCtl.editMode && root.editPanelVisible && PdfEdit.panelOnTop)
                                && topPanel.item ? topPanel.item.implicitHeight : 0)
                  bottom: parent.bottom; bottomMargin: root.bottomInset }
        clip: true
        contentWidth:  Math.max(width,  root.dispW)
        contentHeight: Math.max(height, root.dispH)
        boundsBehavior: Flickable.StopAtBounds
        onContentXChanged: root._moved()
        onContentYChanged: root._moved()
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

            TiledImage {
                id: img
                anchors.fill: parent
                smooth: !root._rough
                mipmap: true
                decodeSize: root.decodeDim
                viewRect: Qt.rect(view.contentX - imgWrap.x, view.contentY - imgWrap.y, view.width, view.height)
                //  Beim Stufenwechsel bleibt das alte Bild stehen, bis das neue fertig ist - sonst ist die Flaeche
                //  waehrenddessen leer ("Bild ist kurz weg").
                onCurrentChanged: if (current) root._releaseOld()
                onStatusChanged: {
                    if (status === TiledImage.Error) { root._releaseOld(); root._everReady = false }
                    if (status !== TiledImage.Ready) return
                    root._everReady = true
                    if (root.editCtl.imageWidth <= 0)
                        root.editCtl.setImageSize(implicitWidth, implicitHeight)
                }
            }

            TapHandler {
                enabled: root.editCtl.editMode && root.editCtl.tool === 0
                onTapped: root.editCtl.selectedId = -1
            }

            Repeater {
                model: root.editCtl.annModel
                delegate: ImageEditBox {
                    imgScale: root.dispScale
                    imgWpx: root.natW
                    imgHpx: root.natH
                    surface: root
                }
            }

            MouseArea {
                id: drawArea
                anchors.fill: parent
                enabled: root.editCtl.editMode && root.editCtl.tool !== 0
                visible: enabled
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                cursorShape: Qt.CrossCursor
                property int _drawId: -1
                property bool _textAdded: false
                onPressed: (m) => {
                    root.commitEditing()
                    root.notesVisible = true
                    const ix = m.x / root.dispScale
                    const iy = m.y / root.dispScale
                    if (root.editCtl.tool === 1) {          // Text
                        root._autoEditId = root.editCtl.addText(ix, iy)
                        _textAdded = root._autoEditId >= 0
                        _drawId = -1
                    } else {                                // Stift/Pfeil/Rechteck/Ellipse
                        _drawId = root.editCtl.beginDraw(root.editCtl.tool - 1, ix, iy)
                    }
                }
                onPositionChanged: (m) => {
                    if (_drawId >= 0)
                        root.editCtl.updateDraw(_drawId, m.x / root.dispScale, m.y / root.dispScale)
                }
                // Ohne den Schalter im Text-Knopf zurueck zur Auswahl; erst beim Loslassen, sonst endete der Druck
                // in der dann abgeschalteten Flaeche.
                onReleased: {
                    if (_drawId >= 0) { root.editCtl.endDraw(_drawId); _drawId = -1 }
                    if (_textAdded && !PdfEdit.textToolSticky) root.editCtl.tool = 0
                    _textAdded = false
                }
                onCanceled: _textAdded = false
            }

            ImageEditToolbar {
                imgScale: root.dispScale
                contentW: imgWrap.width
                contentH: imgWrap.height
                surface: root
            }
        }

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

    Text {
        anchors.centerIn: parent
        visible: img.status === TiledImage.Error
        text: App.uiText(App.language, "ImageLoadError")
        color: "#ff8a80"; font.pixelSize: 14; z: 5
    }

    Rectangle {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset }
        height: 42
        color: App.themeToolbarBg
        visible: root.docReady
        z: 10
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1; color: App.themeBorder }

        component TBtn: Rectangle {
            id: tb
            property string glyph: ""
        property string iconName: ""
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
                   color: tb.danger ? "#e05a5a" : App.themeTextPrimary; font.pixelSize: 14
                   visible: tb.iconName.length === 0 }
            DrawnIcon { anchors.centerIn: parent; name: tb.iconName; size: 16
                         visible: tb.iconName.length > 0 }
            HoverHandler { id: tbHover; enabled: !tb.disabledLook }
            TapHandler { enabled: !tb.disabledLook; onTapped: tb.activated() }
            ToolTip.text: tb.tip; ToolTip.visible: tbHover.hovered && tb.tip.length > 0
        }

        // LINKS die Werkzeuge wie im PDF- und DOCX-Editor, rechts die Ansicht (Zoom). Die Transliteration bleibt
        // rechts - sie gehört zur Eingabe, nicht zum Werkzeugkasten.
        ScrollableBar {
            anchors { left: parent.left; leftMargin: 10; right: imgToolsRight.left
                      rightMargin: 8; top: parent.top; bottom: parent.bottom }
            spacing: 4
            TBtn { visible: root.editCtl.editMode; iconName: "eye"; checked: !root.notesVisible
                   tip: App.uiText(App.language, "PdfEditNotesToggleTip")
                   onActivated: root.notesVisible = !root.notesVisible }
            TBtn { visible: root.editCtl.editMode; iconName: "undo"; tip: App.uiText(App.language, "PdfEditUndoTip")
                   disabledLook: !root.editCtl.canUndo; onActivated: root.editCtl.undo() }
            TBtn { visible: root.editCtl.editMode; iconName: "redo"; tip: App.uiText(App.language, "PdfEditRedoTip")
                   disabledLook: !root.editCtl.canRedo; onActivated: root.editCtl.redo() }
            TBtn { visible: root.editCtl.editMode; iconName: "copy"; tip: App.uiText(App.language, "ImageEditPasteBtn")
                   disabledLook: !root.editCtl.hasClipboard; onActivated: root.editCtl.paste() }
            Rectangle { visible: root.editCtl.editMode; width: 1; height: 18; color: App.themeBorder
                        anchors.verticalCenter: parent.verticalCenter }
            TBtn { iconName: "pen"; checked: root.editCtl.editMode
                   tip: App.uiText(App.language, "ImageEditToggle")
                   onActivated: {
                       root.editCtl.editMode = !root.editCtl.editMode
                       if (!root.editCtl.editMode) root.editPanelVisible = false
                   } }
        }

        ScrollableBar {
            id: imgToolsRight
            anchors { right: parent.right; rightMargin: 10
                      top: parent.top; bottom: parent.bottom }
            width: Math.min(contentWidth, toolbar.width * 0.55)
            spacing: 4
            TBtn { iconName: "minus"; tip: App.uiText(App.language, "ImageZoomOut"); onActivated: root.zoomOut() }
            TBtn { iconName: "fit-window"; tip: App.uiText(App.language, "ImageFitWindow"); checked: root._fitMode
                   onActivated: root.fitToWindow() }
            TBtn { glyph: "1:1"; tip: App.uiText(App.language, "ImageActualSize")
                   onActivated: root.actualSize() }
            TBtn { iconName: "plus"; tip: App.uiText(App.language, "ImageZoomIn"); onActivated: root.zoomIn() }
            Text { anchors.verticalCenter: parent.verticalCenter; width: 46; horizontalAlignment: Text.AlignHCenter
                   text: Math.round(root.dispScale * 100) + "%"; color: App.themeTextMuted; font.pixelSize: 11 }
            //  Der Trennstrich gehoert zum Knopf - ohne ihn haenge er allein am
            //  Ende der Leiste.
            Rectangle { visible: translitBtn.visible; width: 1; height: 18; color: App.themeBorder
                        anchors.verticalCenter: parent.verticalCenter }
            TranslitButton { id: translitBtn; erlaubt: root.editCtl.editMode
                             anchors.verticalCenter: parent.verticalCenter }
        }
    }

    // Andockbares Panel (rechts ODER oben; teilt PdfEdit.panelOnTop). Erst beim ersten Bearbeiten gebaut: versteckt
    // waren die zwei Panels die Haelfte aller Objekte der Ansicht und ein Drittel der Oeffnen-Blockade.
    readonly property bool _panelShown: root.editCtl.editMode && root.editPanelVisible
    Loader {
        id: sidePanel
        active: false
        readonly property bool _wanted: root._panelShown && !PdfEdit.panelOnTop
        on_WantedChanged: if (_wanted) active = true
        visible: _wanted
        width: item ? item.implicitWidth : 0
        anchors { right: parent.right; top: parent.top; topMargin: root.topInset + toolbar.height
                  bottom: parent.bottom; bottomMargin: root.bottomInset }
        z: 11
        sourceComponent: ImageEditPanel {
            horizontal: false
            surface: root
            onCloseRequested: root.editPanelVisible = false
        }
    }
    Loader {
        id: topPanel
        active: false
        readonly property bool _wanted: root._panelShown && PdfEdit.panelOnTop
        on_WantedChanged: if (_wanted) active = true
        visible: _wanted
        height: item ? item.implicitHeight : 0
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset + toolbar.height }
        z: 11
        sourceComponent: ImageEditPanel {
            horizontal: true
            surface: root
            onCloseRequested: root.editPanelVisible = false
        }
    }
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

    Rectangle {
        anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.25); visible: root.editCtl.busy; z: 19
        Text { anchors.centerIn: parent; text: App.uiText(App.language, "PdfEditExportingToast")
               color: "#ffffff"; font.pixelSize: 14 }
    }

    Shortcut {
        sequence: "Alt+Q"
        enabled: root.paneActive && root.docReady
        onActivated: root.notesVisible = !root.notesVisible
    }
    Shortcut {
        sequence: "Delete"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode
                 && root.editCtl.selectedId >= 0 && !root.editCtl.textEditing
        onActivated: { root.commitEditing(); root.editCtl.removeAnn(root.editCtl.selectedId) }
    }
    Shortcut {
        sequence: "Ctrl+C"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode
                 && root.editCtl.selectedId >= 0 && !root.editCtl.textEditing
        onActivated: { root.commitEditing(); root.editCtl.copySelected() }
    }
    Shortcut {
        sequence: "Ctrl+V"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode && root.editCtl.hasClipboard
                 && !root.editCtl.textEditing
        onActivated: root.editCtl.paste()
    }
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode && root.editCtl.canUndo
        onActivated: root.editCtl.undo()
    }
    //  Strg+Y als zweite Folge (s. GalleryPane: das Plattform-Thema liefert sie
    //  nicht in jeder Sitzung).
    Shortcut {
        sequences: [ "Ctrl+Shift+Z", "Ctrl+Y" ]
        enabled: root.paneActive && root.docReady && root.editCtl.editMode && root.editCtl.canRedo
        onActivated: root.editCtl.redo()
    }
}
