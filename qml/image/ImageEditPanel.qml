pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  ImageEditPanel.qml — Werkzeug- & Eigenschaften-Panel des Bild-Editors, in
//  ZWEI Layouts in einer Datei (Analog PdfEditPanel):
//   • horizontal:false → RECHTE SEITENLEISTE (Standard)
//   • horizontal:true  → OBERE LEISTE („wie Word") — Ribbon
//  Die Dock-Position teilt sich der Bild-Editor mit dem PDF-Editor über die
//  globale Einstellung PdfEdit.panelOnTop (Einstellungen ▸ Editor).
//
//  ZIEL-ID: Regler wirken auf die AUSWAHL (ctl.selectedId) — oder, wenn nichts
//  ausgewählt ist, auf die VORLAGE für neue Annotationen (id −1). So setzt man
//  „erst Farbe/Breite, dann zeichnen" (Stil-Erben).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: panel

    property bool horizontal: false
    property var  surface: null
    readonly property ImageEditController ctl: surface ? surface.editCtl : null
    signal closeRequested()

    // Auswahl ODER Vorlagen-Defaults (rev-getrieben).
    readonly property var  sel: (ctl.selectionRev, ctl.annInfo(ctl.selectedId))
    readonly property bool hasSel: sel.exists === true
    readonly property var  def: (ctl.defaultRev, ctl.defaultInfo())
    readonly property var  info: hasSel ? sel : def
    readonly property int  targetId: hasSel ? ctl.selectedId : -1

    // Welche Regler zeigen? Auswahl → deren Art; sonst das aktive Werkzeug.
    readonly property int  tool: ctl.tool
    readonly property bool showText:   hasSel ? sel.isText  : tool === 1
    readonly property bool showStroke: hasSel ? (sel.isStroke === true || sel.isShape === true) : tool >= 2
    readonly property bool showFill:   hasSel ? sel.isShape : (tool === 4 || tool === 5)
    readonly property bool showAny:    showText || showStroke || showFill
    readonly property color defaultPaper: "#FEF39B"

    implicitWidth: horizontal ? 0 : 320
    implicitHeight: horizontal ? 62 : 0

    // ── Wiederverwendbare Buttons ─────────────────────────────────────────────
    component ToolBtn: Rectangle {
        id: tbt
        property string glyph: ""
        property url iconSource: ""
        property string tip: ""
        property int    toolValue: 0
        readonly property bool checked: panel.ctl.tool === toolValue
        width: 34; height: 30; radius: 6
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (tbtHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.14) : "transparent")
        border.color: checked ? App.themeAccent : App.themeBorder
        border.width: 1
        Text { anchors.centerIn: parent; text: tbt.glyph; color: App.themeTextPrimary; font.pixelSize: 15
               visible: String(tbt.iconSource).length === 0 }
        ThemedIcon { anchors.centerIn: parent; source: tbt.iconSource; size: 16
                     visible: String(tbt.iconSource).length > 0 }
        HoverHandler { id: tbtHover }
        TapHandler { onTapped: panel.ctl.tool = tbt.toolValue }
        ToolTip.text: tbt.tip; ToolTip.visible: tbtHover.hovered && tbt.tip.length > 0
    }
    component StyleBtn: Rectangle {
        id: sbt
        property string glyph: ""
        property url iconSource: ""
        property bool checked: false
        property bool boldGlyph: false
        property bool italicGlyph: false
        property bool underlineGlyph: false
        signal activated()
        width: 30; height: 28; radius: 5
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (sbtHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.14) : "transparent")
        border.color: checked ? App.themeAccent : App.themeBorder; border.width: 1
        Text { anchors.centerIn: parent; text: sbt.glyph; color: App.themeTextPrimary; font.pixelSize: 13
               font.bold: sbt.boldGlyph; font.italic: sbt.italicGlyph; font.underline: sbt.underlineGlyph
               visible: String(sbt.iconSource).length === 0 }
        ThemedIcon { anchors.centerIn: parent; source: sbt.iconSource; size: 16
                     visible: String(sbt.iconSource).length > 0 }
        HoverHandler { id: sbtHover }
        TapHandler { onTapped: sbt.activated() }
    }
    component AlignBtn: Rectangle {
        id: abt
        property string glyph: ""
        property url iconSource: ""
        property int alignValue: 0
        readonly property bool checked: panel.info.alignment === alignValue
        width: 30; height: 28; radius: 5
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (abtHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.14) : "transparent")
        border.color: checked ? App.themeAccent : App.themeBorder; border.width: 1
        Text { anchors.centerIn: parent; text: abt.glyph; color: App.themeTextPrimary; font.pixelSize: 13
               visible: String(abt.iconSource).length === 0 }
        ThemedIcon { anchors.centerIn: parent; source: abt.iconSource; size: 16
                     visible: String(abt.iconSource).length > 0 }
        HoverHandler { id: abtHover }
        TapHandler { onTapped: panel.ctl.setAnnAlignment(panel.targetId, abt.alignValue) }
    }
    component VAlignBtn: Rectangle {
        id: vbt
        property string glyph: ""
        property url iconSource: ""
        property int vAlignValue: 0
        readonly property bool checked: panel.info.vAlign === vAlignValue
        width: 30; height: 28; radius: 5
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (vbtHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.14) : "transparent")
        border.color: checked ? App.themeAccent : App.themeBorder; border.width: 1
        Text { anchors.centerIn: parent; text: vbt.glyph; color: App.themeTextPrimary; font.pixelSize: 13
               visible: String(vbt.iconSource).length === 0 }
        ThemedIcon { anchors.centerIn: parent; source: vbt.iconSource; size: 16
                     visible: String(vbt.iconSource).length > 0 }
        HoverHandler { id: vbtHover }
        TapHandler { onTapped: panel.ctl.setAnnVAlign(panel.targetId, vbt.vAlignValue) }
    }
    component RibbonLabel: Text {
        color: App.themeTextMuted; font.pixelSize: 9
        horizontalAlignment: Text.AlignHCenter
    }

    // Regler bei Auswahlwechsel imperativ nachführen (Nutzereingaben reißen sonst
    // externe Bindungen von ComboBox/SpinBox).
    function refreshFromSelection() {
        let idx = 0
        for (let i = 0; i < fontBox.count; i++)
            if (fontBox.textAt(i) === panel.info.fontFamily) { idx = i; break }
        fontBox.currentIndex = idx
        sizeBox.value  = Math.round(panel.info.fontSizePx)
        lwBox.value    = Math.round(panel.info.lineWidth)
        const a = panel.info.hasHighlight ? panel.info.highlightColor.a : 0
        opSlider.value = a
    }
    function applyOpacity(v) {
        const base = panel.info.hasHighlight ? panel.info.highlightColor : panel.defaultPaper
        panel.ctl.setAnnHighlight(panel.targetId, Qt.rgba(base.r, base.g, base.b, v))
    }
    Connections {
        target: panel.ctl
        function onSelectionRevChanged() { panel.refreshFromSelection() }
        function onDefaultRevChanged()   { if (!panel.hasSel) panel.refreshFromSelection() }
        function onToolChanged()         { if (!panel.hasSel) panel.refreshFromSelection() }
    }
    Component.onCompleted: refreshFromSelection()

    // ══════════════════════════════════════════════════════════════════════════
    //  VARIANTE A — rechte Seitenleiste (Standard)
    // ══════════════════════════════════════════════════════════════════════════
    Rectangle {
        anchors.fill: parent
        visible: !panel.horizontal
        color: App.themeSidebarBg
        Rectangle { anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 1; color: App.themeBorder }
        MouseArea { anchors.fill: parent; onWheel: (w) => { w.accepted = true } }  // Event-Schlucker

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12

            // Kopf
            Item {
                width: parent.width; height: 24
                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                       text: App.uiText(App.language, "ImageEditPanelTitle")
                       color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true }
                Rectangle {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    width: 24; height: 24; radius: 5
                    color: closeHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16) : "transparent"
                    Text { anchors.centerIn: parent; text: "\u2715"; color: App.themeTextPrimary; font.pixelSize: 13 }
                    HoverHandler { id: closeHover }
                    TapHandler { onTapped: panel.closeRequested() }
                }
            }

            // Werkzeuge
            Text { text: App.uiText(App.language, "ImageEditToolsLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
            Grid {
                columns: 6; spacing: 6
                ToolBtn { iconSource: "qrc:/qml/icons/select.svg"; toolValue: 0; tip: App.uiText(App.language, "ImageEditToolSelect") }
                ToolBtn { glyph: "T";       toolValue: 1; tip: App.uiText(App.language, "ImageEditToolText") }
                ToolBtn { iconSource: "qrc:/qml/icons/pen.svg"; toolValue: 2; tip: App.uiText(App.language, "ImageEditToolPen") }
                ToolBtn { iconSource: "qrc:/qml/icons/arrow.svg"; toolValue: 3; tip: App.uiText(App.language, "ImageEditToolArrow") }
                ToolBtn { iconSource: "qrc:/qml/icons/rect.svg"; toolValue: 4; tip: App.uiText(App.language, "ImageEditToolRect") }
                ToolBtn { iconSource: "qrc:/qml/icons/ellipse.svg"; toolValue: 5; tip: App.uiText(App.language, "ImageEditToolEllipse") }
            }

            Rectangle { width: parent.width; height: 1; color: App.themeBorder }

            // Hinweis, wenn nichts ausgewählt und Auswahl-Werkzeug aktiv
            Text {
                visible: !panel.showAny
                width: parent.width; wrapMode: Text.WordWrap
                text: App.uiText(App.language, "ImageEditPickHint")
                color: App.themeTextMuted; font.pixelSize: 11
            }

            // ── TEXT-Eigenschaften ────────────────────────────────────────────
            Column {
                visible: panel.showText
                width: parent.width; spacing: 8
                Text { text: App.uiText(App.language, "PdfEditFontLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                ComboBox {
                    id: fontBox
                    width: parent.width
                    model: panel.ctl.standardFonts()
                    onActivated: panel.ctl.setAnnFont(panel.targetId, currentText)
                }
                Text { visible: panel.ctl.resolvedFont(panel.info.fontFamily) !== panel.info.fontFamily
                       width: parent.width; wrapMode: Text.WordWrap
                       text: "\u2192 " + panel.ctl.resolvedFont(panel.info.fontFamily)
                       color: App.themeTextMuted; font.pixelSize: 10 }
                Row {
                    spacing: 10
                    Column { spacing: 4
                        Text { text: App.uiText(App.language, "PdfEditSizeLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                        SpinBox { id: sizeBox; from: 4; to: 800; stepSize: 2; value: 28
                                  onValueModified: panel.ctl.setAnnFontSize(panel.targetId, value) }
                    }
                    Column { spacing: 4
                        Text { text: App.uiText(App.language, "PdfEditStyleLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                        Row { spacing: 4
                            StyleBtn { glyph: "B"; boldGlyph: true;      checked: panel.info.bold === true
                                       onActivated: panel.ctl.setAnnBold(panel.targetId, !panel.info.bold) }
                            StyleBtn { glyph: "I"; italicGlyph: true;    checked: panel.info.italic === true
                                       onActivated: panel.ctl.setAnnItalic(panel.targetId, !panel.info.italic) }
                            StyleBtn { glyph: "U"; underlineGlyph: true; checked: panel.info.underline === true
                                       onActivated: panel.ctl.setAnnUnderline(panel.targetId, !panel.info.underline) }
                        }
                    }
                }
                Row {
                    spacing: 16
                    Column { spacing: 4
                        Text { text: App.uiText(App.language, "PdfEditAlignLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                        Row { spacing: 4
                            AlignBtn { iconSource: "qrc:/qml/icons/align-left.svg"; alignValue: 0 }
                            AlignBtn { iconSource: "qrc:/qml/icons/align-center.svg"; alignValue: 1 }
                            AlignBtn { iconSource: "qrc:/qml/icons/align-right.svg"; alignValue: 2 }
                        }
                    }
                    Column { spacing: 4
                        Text { text: App.uiText(App.language, "PdfEditVAlignLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                        Row { spacing: 4
                            VAlignBtn { iconSource: "qrc:/qml/icons/valign-top.svg"; vAlignValue: 0 }
                            VAlignBtn { iconSource: "qrc:/qml/icons/valign-middle.svg"; vAlignValue: 1 }
                        }
                    }
                }
                Row {
                    spacing: 16
                    Column { spacing: 4
                        Text { text: App.uiText(App.language, "PdfEditColorLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                        ColorPicker {
                            showAlpha: false
                            title: App.uiText(App.language, "PdfEditColorLabel")
                            selectedColor: panel.info.textColor
                            onColorPicked: (c) => {
                                panel.ctl.setAnnColor(panel.targetId, c)
                                selectedColor = Qt.binding(() => panel.info.textColor)
                            }
                        }
                    }
                    Column { spacing: 4
                        Text { text: App.uiText(App.language, "PdfEditHighlightLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                        Row { spacing: 6
                            ColorPicker {
                                showAlpha: true
                                title: App.uiText(App.language, "PdfEditHighlightLabel")
                                selectedColor: panel.info.hasHighlight ? panel.info.highlightColor : panel.defaultPaper
                                onColorPicked: (c) => {
                                    panel.ctl.setAnnHighlight(panel.targetId, c)
                                    selectedColor = Qt.binding(() => panel.info.hasHighlight ? panel.info.highlightColor : panel.defaultPaper)
                                }
                            }
                            Rectangle {
                                width: noneLbl.implicitWidth + 16; height: 26; radius: 6
                                anchors.verticalCenter: parent.verticalCenter
                                color: noneHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                                                         : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                                border.color: App.themeBorder; border.width: 1
                                opacity: panel.info.hasHighlight === true ? 1.0 : 0.4
                                Text { id: noneLbl; anchors.centerIn: parent
                                       text: App.uiText(App.language, "PdfEditNoHighlight"); color: App.themeTextPrimary; font.pixelSize: 11 }
                                HoverHandler { id: noneHover }
                                TapHandler { onTapped: panel.ctl.setAnnHighlight(panel.targetId, "transparent") }
                            }
                        }
                    }
                }
                Column {
                    width: parent.width; spacing: 4
                    Text { text: App.uiText(App.language, "PdfEditOpacityLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                    Slider { id: opSlider; width: parent.width; from: 0; to: 1; stepSize: 0.05
                             onMoved: panel.applyOpacity(value) }
                }
            }

            // ── STRICH-/FORM-Eigenschaften ────────────────────────────────────
            Column {
                visible: panel.showStroke || panel.showFill
                width: parent.width; spacing: 8
                Row {
                    spacing: 16
                    Column { spacing: 4
                        Text { text: App.uiText(App.language, "ImageEditStrokeLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                        ColorPicker {
                            showAlpha: false
                            title: App.uiText(App.language, "ImageEditStrokeLabel")
                            selectedColor: panel.info.strokeColor
                            onColorPicked: (c) => {
                                panel.ctl.setAnnStroke(panel.targetId, c)
                                selectedColor = Qt.binding(() => panel.info.strokeColor)
                            }
                        }
                    }
                    Column { spacing: 4
                        Text { text: App.uiText(App.language, "ImageEditWidthLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                        SpinBox { id: lwBox; from: 1; to: 200; stepSize: 1; value: 4
                                  onValueModified: panel.ctl.setAnnLineWidth(panel.targetId, value) }
                    }
                }
                Column {
                    visible: panel.showFill
                    spacing: 4
                    Text { text: App.uiText(App.language, "ImageEditFillLabel"); color: App.themeTextMuted; font.pixelSize: 11 }
                    Row { spacing: 6
                        ColorPicker {
                            showAlpha: true
                            title: App.uiText(App.language, "ImageEditFillLabel")
                            selectedColor: panel.info.hasFill ? panel.info.fillColor : "#3300b4a0"
                            onColorPicked: (c) => {
                                panel.ctl.setAnnFill(panel.targetId, c)
                                selectedColor = Qt.binding(() => panel.info.hasFill ? panel.info.fillColor : "#3300b4a0")
                            }
                        }
                        Rectangle {
                            width: noneFillLbl.implicitWidth + 16; height: 26; radius: 6
                            anchors.verticalCenter: parent.verticalCenter
                            color: noneFillHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                                                         : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                            border.color: App.themeBorder; border.width: 1
                            opacity: panel.info.hasFill === true ? 1.0 : 0.4
                            Text { id: noneFillLbl; anchors.centerIn: parent
                                   text: App.uiText(App.language, "PdfEditNoHighlight"); color: App.themeTextPrimary; font.pixelSize: 11 }
                            HoverHandler { id: noneFillHover }
                            TapHandler { onTapped: panel.ctl.setAnnFill(panel.targetId, "transparent") }
                        }
                    }
                }
            }

            // Auswahl-Aktionen
            Row {
                visible: panel.hasSel
                spacing: 8
                Rectangle {
                    width: cpLbl.implicitWidth + 20; height: 30; radius: 6
                    color: cpHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.14) : "transparent"
                    border.color: App.themeBorder; border.width: 1
                    Text { id: cpLbl; anchors.centerIn: parent; text: App.uiText(App.language, "ImageEditCopyBtn")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: cpHover }
                    TapHandler { onTapped: panel.ctl.copySelected() }
                }
                Rectangle {
                    width: delLbl.implicitWidth + 20; height: 30; radius: 6
                    color: delHover.hovered ? Qt.rgba(0.88, 0.35, 0.35, 0.18) : "transparent"
                    border.color: "#e05a5a"; border.width: 1
                    Text { id: delLbl; anchors.centerIn: parent; text: App.uiText(App.language, "PdfEditDeleteBtn")
                           color: "#e05a5a"; font.pixelSize: 12 }
                    HoverHandler { id: delHover }
                    TapHandler { onTapped: { if (panel.surface) panel.surface.commitEditing(); panel.ctl.removeAnn(panel.ctl.selectedId) } }
                }
            }

            Rectangle { width: parent.width; height: 1; color: App.themeBorder }

            // ── Dokument (Speichern / Export) ─────────────────────────────────
            Text { text: App.uiText(App.language, "ImageEditDocSection"); color: App.themeTextMuted; font.pixelSize: 11 }
            Row {
                spacing: 8
                Rectangle {
                    width: pasteLbl.implicitWidth + 20; height: 32; radius: 6
                    opacity: panel.ctl.hasClipboard ? 1.0 : 0.4
                    color: pasteHover.hovered && panel.ctl.hasClipboard ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.14) : "transparent"
                    border.color: App.themeBorder; border.width: 1
                    Text { id: pasteLbl; anchors.centerIn: parent; text: App.uiText(App.language, "ImageEditPasteBtn")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: pasteHover; enabled: panel.ctl.hasClipboard }
                    TapHandler { enabled: panel.ctl.hasClipboard; onTapped: panel.ctl.paste() }
                }
            }
            Row {
                spacing: 8
                Rectangle {
                    width: saveLbl.implicitWidth + 20; height: 32; radius: 6
                    opacity: panel.ctl.dirty ? 1.0 : 0.4
                    color: saveHover.hovered && panel.ctl.dirty ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20) : "transparent"
                    border.color: App.themeAccent; border.width: 1
                    Text { id: saveLbl; anchors.centerIn: parent; text: App.uiText(App.language, "PdfEditSaveBtn")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: saveHover; enabled: panel.ctl.dirty }
                    TapHandler { enabled: panel.ctl.dirty; onTapped: panel.ctl.saveOverlay() }
                }
                Rectangle {
                    readonly property bool usable: !panel.ctl.busy && panel.ctl.annCount > 0
                    width: expLbl.implicitWidth + 20; height: 32; radius: 6
                    opacity: usable ? 1.0 : 0.4
                    color: expHover.hovered && usable ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20) : "transparent"
                    border.color: App.themeAccent; border.width: 1
                    Text { id: expLbl; anchors.centerIn: parent; text: App.uiText(App.language, "PdfEditExportBtn")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: expHover; enabled: parent.usable }
                    TapHandler { enabled: parent.usable; onTapped: if (panel.surface) panel.surface.startImageExport() }
                }
            }
            Text {
                width: parent.width; wrapMode: Text.NoWrap; elide: Text.ElideMiddle
                text: panel.ctl.exportTargetPath()
                color: App.themeTextMuted; font.pixelSize: 10
            }
        }
    }

    // ══════════════════════════════════════════════════════════════════════════
    //  VARIANTE B — obere Leiste („wie Word"; horizontal:true)
    // ══════════════════════════════════════════════════════════════════════════
    Rectangle {
        anchors.fill: parent
        visible: panel.horizontal
        color: App.themeToolbarBg
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1; color: App.themeBorder }
        MouseArea { anchors.fill: parent; onWheel: (w) => { w.accepted = true } }

        Flickable {
            anchors.fill: parent
            anchors.leftMargin: 10; anchors.rightMargin: 40
            contentWidth: ribbon.implicitWidth; contentHeight: height
            clip: true
            Row {
                id: ribbon
                height: parent.height; spacing: 12
                // Werkzeuge
                Column { anchors.verticalCenter: parent.verticalCenter; spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "ImageEditToolsLabel"); width: parent.width }
                    Row { spacing: 4
                        ToolBtn { iconSource: "qrc:/qml/icons/select.svg"; toolValue: 0 }
                        ToolBtn { glyph: "T";       toolValue: 1 }
                        ToolBtn { iconSource: "qrc:/qml/icons/pen.svg"; toolValue: 2 }
                        ToolBtn { iconSource: "qrc:/qml/icons/arrow.svg"; toolValue: 3 }
                        ToolBtn { iconSource: "qrc:/qml/icons/rect.svg"; toolValue: 4 }
                        ToolBtn { iconSource: "qrc:/qml/icons/ellipse.svg"; toolValue: 5 }
                    }
                }
                Rectangle { width: 1; height: 40; color: App.themeBorder; anchors.verticalCenter: parent.verticalCenter }
                // Text-Größe / Stil (nur bei Text)
                Column { visible: panel.showText; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "PdfEditSizeLabel"); width: parent.width }
                    SpinBox { id: sizeBoxH; from: 4; to: 800; stepSize: 2; value: 28; width: 110
                              onValueModified: panel.ctl.setAnnFontSize(panel.targetId, value) }
                }
                Column { visible: panel.showText; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "PdfEditStyleLabel"); width: parent.width }
                    Row { spacing: 4
                        StyleBtn { glyph: "B"; boldGlyph: true;      checked: panel.info.bold === true
                                   onActivated: panel.ctl.setAnnBold(panel.targetId, !panel.info.bold) }
                        StyleBtn { glyph: "I"; italicGlyph: true;    checked: panel.info.italic === true
                                   onActivated: panel.ctl.setAnnItalic(panel.targetId, !panel.info.italic) }
                        StyleBtn { glyph: "U"; underlineGlyph: true; checked: panel.info.underline === true
                                   onActivated: panel.ctl.setAnnUnderline(panel.targetId, !panel.info.underline) }
                    }
                }
                // Linienbreite (Strich/Form)
                Column { visible: panel.showStroke || panel.showFill; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "ImageEditWidthLabel"); width: parent.width }
                    SpinBox { id: lwBoxH; from: 1; to: 200; stepSize: 1; value: 4; width: 100
                              onValueModified: panel.ctl.setAnnLineWidth(panel.targetId, value) }
                }
                Rectangle { width: 1; height: 40; color: App.themeBorder; anchors.verticalCenter: parent.verticalCenter }
                // Dokument
                Column { anchors.verticalCenter: parent.verticalCenter; spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "ImageEditDocSection"); width: parent.width }
                    Row { spacing: 6
                        Rectangle {
                            width: 30; height: 28; radius: 5; opacity: panel.ctl.dirty ? 1.0 : 0.4
                            color: "transparent"; border.color: App.themeAccent; border.width: 1
                            Text { anchors.centerIn: parent; text: "\u2B07"; color: App.themeTextPrimary; font.pixelSize: 13 }
                            ToolTip.text: App.uiText(App.language, "PdfEditSaveBtn"); ToolTip.visible: shHover.hovered
                            HoverHandler { id: shHover; enabled: panel.ctl.dirty }
                            TapHandler { enabled: panel.ctl.dirty; onTapped: panel.ctl.saveOverlay() }
                        }
                        Rectangle {
                            readonly property bool usable: !panel.ctl.busy && panel.ctl.annCount > 0
                            width: ehLbl.implicitWidth + 16; height: 28; radius: 5; opacity: usable ? 1.0 : 0.4
                            color: "transparent"; border.color: App.themeAccent; border.width: 1
                            Text { id: ehLbl; anchors.centerIn: parent; text: App.uiText(App.language, "PdfEditExportBtn")
                                   color: App.themeTextPrimary; font.pixelSize: 12 }
                            HoverHandler { id: ehHover; enabled: parent.usable }
                            TapHandler { enabled: parent.usable; onTapped: if (panel.surface) panel.surface.startImageExport() }
                        }
                    }
                }
            }
        }
        // Schließen fest rechts
        Rectangle {
            anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
            width: 26; height: 26; radius: 5
            color: xhHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16) : "transparent"
            Text { anchors.centerIn: parent; text: "\u2715"; color: App.themeTextPrimary; font.pixelSize: 13 }
            HoverHandler { id: xhHover }
            TapHandler { onTapped: panel.closeRequested() }
        }
    }
}
