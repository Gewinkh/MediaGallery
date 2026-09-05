pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// Werkzeuge und Eigenschaften des PDF-Editors in zwei Layouts (rechte Leiste oder oberes Ribbon); PdfSurface
// instanziiert beide und blendet eine ein. Mit Auswahl gelten die Regler der Annotation, ohne Auswahl die
// Vorlage des Werkzeugs. Die Controls schreiben selbst und rissen Bindungen - daher `refreshFromSelection()`.
Rectangle {
    id: panel

    property var  surface: null          // PdfSurface-Root (Commit + Export-Start)

    function _exportTip() {
        const tip = App.uiText(App.language, "PdfEditExportTip")
        const name = panel.ctl ? String(panel.ctl.exportTargetPath()).split("/").pop() : ""
        return name.length > 0 ? tip + " (" + name + ")" : tip
    }
    readonly property PdfEditController ctl: surface ? surface.editCtl : null
    property bool horizontal: false      // false = Seitenleiste, true = Ribbon

    // Eigenschaften der ausgewählten Box, rev-getrieben (Muster wie Toolbar);
    // OHNE Auswahl liefern die Vorlagen-Defaults die angezeigten Werte
    // (rev-getrieben über defaultRev - Muster Bild-Editor).
    readonly property var  selInfo: (panel.ctl.selectionRev, panel.ctl.boxInfo(panel.ctl.selectedId))
    readonly property bool hasSel: selInfo.exists === true
    readonly property var  info: hasSel ? selInfo
                                        : (panel.ctl.defaultRev, panel.ctl.defaultInfo())
    readonly property int  targetId: hasSel ? panel.ctl.selectedId : -1

    readonly property bool selIsText:    hasSel && selInfo.isText === true
    readonly property bool selIsReplace: hasSel && selInfo.isReplace === true
    readonly property bool selIsStroke:  hasSel && selInfo.isStroke === true
    readonly property bool selIsShape:   hasSel && selInfo.isShape === true
    readonly property bool showText:    selIsText || selIsReplace
                                        || (!hasSel && (panel.ctl.tool === 1 || panel.ctl.tool === 6))
    readonly property bool showStroke:  selIsStroke || selIsShape
                                        || (!hasSel && panel.ctl.tool >= 2 && panel.ctl.tool <= 5)
    readonly property bool showFill:    selIsShape
                                        || (!hasSel && (panel.ctl.tool === 4 || panel.ctl.tool === 5))
    readonly property bool showPaper:   selIsText || (!hasSel && panel.ctl.tool === 1)
    // Deckfläche („Text ersetzen"): eigene Farbwahl OHNE „Keine"/Deckkraft -
    // die Cover-Farbe ist frei, bleibt aber immer deckend (Controller erzwingt).
    readonly property bool showCover:   selIsReplace || (!hasSel && panel.ctl.tool === 6)

    readonly property color defaultPaper: "#FEF39B"

    color: App.themeSidebarBg
    Rectangle { anchors.left: parent.left; width: 1; height: parent.height
                color: App.themeBorder; visible: !panel.horizontal }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                color: App.themeBorder; visible: panel.horizontal }

    MouseArea { anchors.fill: parent; onWheel: (wheel) => { wheel.accepted = true } }

    // Wiederverwendete Mini-Buttons (Inline-Components: nur auf Root-Ebene
    //    einer QML-Datei zulässig; sichtbar in der gesamten Datei)
    component StyleBtn: Rectangle {
        id: sb
        property string glyph: ""
        property string iconName: ""
        property bool checked: false
        property bool boldGlyph: false
        property bool italicGlyph: false
        property bool underlineGlyph: false
        signal activated()
        width: 30; height: 28; radius: 6
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (sbHover.hovered
                ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07))
        border.color: checked ? App.themeAccent : App.themeBorder
        border.width: 1
        Text { anchors.centerIn: parent; text: sb.glyph
               color: App.themeTextPrimary; font.pixelSize: 12
               font.bold: sb.boldGlyph; font.italic: sb.italicGlyph
               font.underline: sb.underlineGlyph
               visible: sb.iconName.length === 0 }
        DrawnIcon { anchors.centerIn: parent; name: sb.iconName; size: 16
                     visible: sb.iconName.length > 0 }
        HoverHandler { id: sbHover }
        TapHandler { onTapped: sb.activated() }
    }
    component AlignBtn: Rectangle {
        id: ab
        property string glyph: ""
        property string iconName: ""
        property int alignValue: 0
        readonly property bool checked: panel.info.alignment === alignValue
        width: 30; height: 28; radius: 6
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (abHover.hovered
                ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07))
        border.color: checked ? App.themeAccent : App.themeBorder
        border.width: 1
        Text { anchors.centerIn: parent; text: ab.glyph
               color: App.themeTextPrimary; font.pixelSize: 12
               visible: ab.iconName.length === 0 }
        DrawnIcon { anchors.centerIn: parent; name: ab.iconName; size: 16
                     visible: ab.iconName.length > 0 }
        HoverHandler { id: abHover }
        TapHandler { onTapped: panel.ctl.setBoxAlignment(panel.targetId, ab.alignValue) }
    }
    component VAlignBtn: Rectangle {
        id: vb
        property string glyph: ""
        property string iconName: ""
        property int vAlignValue: 0
        readonly property bool checked: panel.info.vAlign === vAlignValue
        width: 30; height: 28; radius: 6
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (vbHover.hovered
                ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07))
        border.color: checked ? App.themeAccent : App.themeBorder
        border.width: 1
        Text { anchors.centerIn: parent; text: vb.glyph
               color: App.themeTextPrimary; font.pixelSize: 12
               visible: vb.iconName.length === 0 }
        DrawnIcon { anchors.centerIn: parent; name: vb.iconName; size: 16
                     visible: vb.iconName.length > 0 }
        HoverHandler { id: vbHover }
        TapHandler { onTapped: panel.ctl.setBoxVAlign(panel.targetId, vb.vAlignValue) }
    }
    component ToolBtn: Rectangle {
        id: tb
        property string glyph: ""
        property string iconName: ""
        property int toolValue: 0
        property int styleValue: -1
        property string tip: ""
        //  Knöpfe, die KEIN Werkzeug setzen (toolValue < 0), sondern nur etwas
        //  auslösen - z. B. der Bild-Einfügen-Knopf.
        signal clicked()
        readonly property bool checked: tb.toolValue >= 0 && panel.ctl.tool === toolValue
                 && (tb.styleValue < 0
                     || (panel.ctl.defaultRev, panel.ctl.markupStyle()) === tb.styleValue)
        width: 34; height: 30; radius: 6
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (tbHover.hovered
                ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07))
        border.color: checked ? App.themeAccent : App.themeBorder
        border.width: 1
        Text { anchors.centerIn: parent; text: tb.glyph
               visible: tb.iconName.length === 0
               color: App.themeTextPrimary; font.pixelSize: 13 }
        DrawnIcon { anchors.centerIn: parent; name: tb.iconName; size: 16
                     visible: tb.iconName.length > 0 }
        HoverHandler { id: tbHover }
        TapHandler {
            onTapped: {
                if (panel.surface) panel.surface.commitEditing()
                if (tb.styleValue >= 0)
                    panel.ctl.setMarkupStyle(tb.styleValue)
                if (tb.toolValue >= 0)
                    panel.ctl.tool = tb.toolValue
                tb.clicked()
                if (tb.toolValue === 6 && panel.surface
                        && panel.surface.replaceSelectionNow)
                    panel.surface.replaceSelectionNow()
                if (tb.toolValue === 9 && panel.surface
                        && panel.surface.startRedact)
                    panel.surface.startRedact()
            }
        }
        ToolTip.text: tb.tip
        ToolTip.visible: tbHover.hovered && tb.tip.length > 0
    }
    component RibbonLabel: Text {
        color: App.themeTextMuted
        font.pixelSize: 9
    }

    // Imperative Synchronisation beider Layout-Varianten
    //  Läuft bei Auswahl-/Datenänderung, Vorlagen-Änderung UND Werkzeugwechsel
    //  - panel.info liefert dabei Auswahl ODER Vorlagen-Defaults.
    function refreshFromSelection() {
        let idx = 0
        for (let i = 0; i < fontBox.count; i++)
            if (fontBox.textAt(i) === panel.info.fontFamily) { idx = i; break }
        fontBox.currentIndex  = idx
        fontBoxH.currentIndex = idx
        const s = Math.round(panel.info.fontSizePt)
        sizeBox.value  = s
        sizeBoxH.value = s
        const a = panel.info.hasHighlight ? panel.info.highlightColor.a : 0
        opSlider.value  = a
        opSliderH.value = a
        const lw = Math.round(panel.info.lineWidth)
        lwBox.value  = lw
        lwBoxH.value = lw
    }
    function applyOpacity(v) {
        const base = panel.info.hasHighlight ? panel.info.highlightColor
                                             : panel.defaultPaper
        panel.ctl.setBoxHighlight(panel.targetId,
                                Qt.rgba(base.r, base.g, base.b, v))
    }
    Connections {
        target: panel.ctl
        function onSelectionRevChanged() { panel.refreshFromSelection() }
        function onDefaultRevChanged()   { if (!panel.hasSel) panel.refreshFromSelection() }
        function onToolChanged()         { if (!panel.hasSel) panel.refreshFromSelection() }
    }
    Component.onCompleted: refreshFromSelection()

    Item {
        anchors.fill: parent
        visible: !panel.horizontal

        Item {
            id: header
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 44
            Text {
                anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                text: App.uiText(App.language, "PdfEditPanelHeader")
                color: App.themeTextPrimary; font.pixelSize: 13; font.bold: true
            }
            Rectangle {
                anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                width: 26; height: 26; radius: 13
                color: closeHover.hovered
                       ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                       : "transparent"
                Text { anchors.centerIn: parent; text: "\u2715"
                       color: App.themeTextPrimary; font.pixelSize: 12 }
                HoverHandler { id: closeHover }
                TapHandler { onTapped: if (panel.surface) panel.surface.editPanelVisible = false }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: App.themeBorder }
        }

        Flickable {
            anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }
            anchors.margins: 14
            contentWidth: width
            contentHeight: col.implicitHeight + 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: col
                width: parent.width
                spacing: 10

                Grid {
                    columns: 3
                    spacing: 4
                    ToolBtn { iconName: "select"; toolValue: 0; tip: App.uiText(App.language, "ImageEditToolSelect") }
                    ToolBtn { glyph: "T";      toolValue: 1; tip: App.uiText(App.language, "ImageEditToolText") }
                    ToolBtn { iconName: "pen"; toolValue: 2; tip: App.uiText(App.language, "ImageEditToolPen") }
                    ToolBtn { iconName: "arrow"; toolValue: 3; tip: App.uiText(App.language, "ImageEditToolArrow") }
                    ToolBtn { iconName: "rect"; toolValue: 4; tip: App.uiText(App.language, "ImageEditToolRect") }
                    ToolBtn { iconName: "ellipse"; toolValue: 5; tip: App.uiText(App.language, "ImageEditToolEllipse") }
                    ToolBtn { iconName: "replace"; toolValue: 6; tip: App.uiText(App.language, "PdfEditToolReplace") }
                    ToolBtn { iconName: "caret"; toolValue: 7; tip: App.uiText(App.language, "PdfEditToolCaret") }
                    ToolBtn { iconName: "markup-highlight"; toolValue: 8; styleValue: 0
                              tip: App.uiText(App.language, "PdfMarkupHighlight") }
                    ToolBtn { iconName: "markup-underline"; toolValue: 8; styleValue: 1
                              tip: App.uiText(App.language, "PdfMarkupUnderline") }
                    ToolBtn { iconName: "markup-strike"; toolValue: 8; styleValue: 2
                              tip: App.uiText(App.language, "PdfMarkupStrike") }
                    ToolBtn { iconName: "redact"; toolValue: 9
                              tip: App.uiText(App.language, "PdfEditToolRedact") }
                    ToolBtn { iconName: "signature"; toolValue: -1
                              tip: App.uiText(App.language, "PdfEditToolStamp")
                              onClicked: {
                                  stampPickA.entries = panel.ctl.folderImages()
                                  stampPickA.open()
                              }
                              FolderImagePicker {
                                  id: stampPickA
                                  hostWidth: panel.width
                                  onPicked: function(u) {
                                      if (panel.surface) panel.surface.insertStampImage(u)
                                  }
                                  onBrowseRequested: if (panel.surface) panel.surface.pickStampImage()
                              } }
                }


                Rectangle { width: parent.width; height: 1; color: App.themeBorder }

                Text {
                    visible: !panel.hasSel && panel.ctl.tool === 0
                    width: parent.width
                    text: App.uiText(App.language, "ImageEditPickHint")
                    color: App.themeTextMuted; font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Text { visible: panel.showText
                       text: App.uiText(App.language, "PdfEditFontLabel")
                       color: App.themeTextMuted; font.pixelSize: 11 }
                ComboBox {
                    id: fontBox
                    visible: panel.showText
                    width: parent.width
                    model: panel.ctl.standardFonts()
                    font.pixelSize: 12
                    onActivated: (idx) => panel.ctl.setBoxFont(panel.targetId, fontBox.textAt(idx))
                }
                // Substitutions-Hinweis: Ist die Familie nicht installiert (auf
                // Linux z. B. Calibri/Helvetica), zeigt Qt-Fontauflösung, was
                // TATSÄCHLICH gerendert wird - identisch in Anzeige und Export.
                Row {
                    visible: panel.showText
                             && panel.ctl.resolvedFont(fontBox.currentText) !== fontBox.currentText
                    width: parent.width; spacing: 5
                    DrawnIcon { name: "arrow-right"; size: 11; color: App.themeTextMuted
                                y: 2 }
                    Text {
                        width: parent.width - 16
                        text: panel.ctl.resolvedFont(fontBox.currentText)
                        color: App.themeTextMuted; font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

                Row {
                    visible: panel.showText
                    width: parent.width
                    spacing: 8
                    Column {
                        spacing: 4
                        Text { text: App.uiText(App.language, "PdfEditSizeLabel")
                               color: App.themeTextMuted; font.pixelSize: 11 }
                        SpinBox {
                            id: sizeBox
                            from: 4; to: 200
                            editable: true
                            font.pixelSize: 12
                            onValueModified: panel.ctl.setBoxFontSize(panel.targetId, value)
                        }
                    }
                    Column {
                        spacing: 4
                        Text { text: App.uiText(App.language, "PdfEditStyleLabel")
                               color: App.themeTextMuted; font.pixelSize: 11 }
                        Row {
                            spacing: 4
                            StyleBtn { glyph: "B"; boldGlyph: true;      checked: panel.info.bold === true
                                       onActivated: panel.ctl.setBoxBold(panel.targetId, !panel.info.bold) }
                            StyleBtn { glyph: "I"; italicGlyph: true;    checked: panel.info.italic === true
                                       onActivated: panel.ctl.setBoxItalic(panel.targetId, !panel.info.italic) }
                            StyleBtn { glyph: "U"; underlineGlyph: true; checked: panel.info.underline === true
                                       onActivated: panel.ctl.setBoxUnderline(panel.targetId, !panel.info.underline) }
                        }
                    }
                }

                Text { visible: panel.showText
                       text: App.uiText(App.language, "PdfEditAlignLabel")
                       color: App.themeTextMuted; font.pixelSize: 11 }
                Row {
                    visible: panel.showText
                    spacing: 4
                    AlignBtn { iconName: "align-left"; alignValue: 0 }
                    AlignBtn { iconName: "align-center"; alignValue: 1 }
                    AlignBtn { iconName: "align-right"; alignValue: 2 }
                }

                Text { visible: panel.showText
                       text: App.uiText(App.language, "PdfEditVAlignLabel")
                       color: App.themeTextMuted; font.pixelSize: 11 }
                Row {
                    visible: panel.showText
                    spacing: 4
                    VAlignBtn { iconName: "valign-top"; vAlignValue: 0 }
                    VAlignBtn { iconName: "valign-middle"; vAlignValue: 1 }
                }

                Row {
                    visible: panel.showText
                    width: parent.width
                    spacing: 16
                    Column {
                        spacing: 4
                        Text { text: App.uiText(App.language, "PdfEditColorLabel")
                               color: App.themeTextMuted; font.pixelSize: 11 }
                        ColorPicker {
                            showAlpha: false
                            title: App.uiText(App.language, "PdfEditColorLabel")
                            selectedColor: panel.info.textColor !== undefined ? panel.info.textColor : "#000000"
                            onColorPicked: (c) => {
                                panel.ctl.setBoxColor(panel.targetId, c)
                                selectedColor = Qt.binding(() => panel.info.textColor !== undefined
                                                                 ? panel.info.textColor : "#000000")
                            }
                        }
                    }
                    Column {
                        spacing: 4
                        visible: panel.showPaper
                        Text { text: App.uiText(App.language, "PdfEditHighlightLabel")
                               color: App.themeTextMuted; font.pixelSize: 11 }
                        Row {
                            spacing: 6
                            ColorPicker {
                                showAlpha: true
                                title: App.uiText(App.language, "PdfEditHighlightLabel")
                                selectedColor: panel.info.hasHighlight === true
                                               ? panel.info.highlightColor : panel.defaultPaper
                                onColorPicked: (c) => {
                                    panel.ctl.setBoxHighlight(panel.targetId, c)
                                    selectedColor = Qt.binding(() => panel.info.hasHighlight === true
                                                                     ? panel.info.highlightColor : panel.defaultPaper)
                                }
                            }
                            Rectangle {
                                width: noneLbl.implicitWidth + 16; height: 26; radius: 6
                                anchors.verticalCenter: parent.verticalCenter
                                color: noneHover.hovered
                                       ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                                       : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                                border.color: App.themeBorder; border.width: 1
                                opacity: panel.info.hasHighlight === true ? 1.0 : 0.4
                                Text { id: noneLbl; anchors.centerIn: parent
                                       text: App.uiText(App.language, "PdfEditNoHighlight")
                                       color: App.themeTextPrimary; font.pixelSize: 11 }
                                HoverHandler { id: noneHover }
                                TapHandler { onTapped: panel.ctl.setBoxHighlight(panel.targetId, "transparent") }
                            }
                        }
                    }
                    Column {
                        spacing: 4
                        visible: panel.showCover
                        Text { text: App.uiText(App.language, "PdfEditCoverLabel")
                               color: App.themeTextMuted; font.pixelSize: 11 }
                        ColorPicker {
                            showAlpha: false
                            title: App.uiText(App.language, "PdfEditCoverLabel")
                            selectedColor: panel.info.highlightColor !== undefined
                                           ? panel.info.highlightColor : "#ffffff"
                            onColorPicked: (c) => {
                                panel.ctl.setBoxHighlight(panel.targetId, c)
                                selectedColor = Qt.binding(() => panel.info.highlightColor !== undefined
                                                                 ? panel.info.highlightColor : "#ffffff")
                            }
                        }
                    }
                }

                Text { visible: panel.showPaper
                       text: App.uiText(App.language, "PdfEditOpacityLabel")
                       color: App.themeTextMuted; font.pixelSize: 11 }
                Slider {
                    id: opSlider
                    visible: panel.showPaper
                    width: parent.width
                    from: 0; to: 1; stepSize: 0.05
                    onMoved: panel.applyOpacity(value)
                }

                Rectangle {
                    width: parent.width; height: 30; radius: 6
                    visible: panel.hasSel && (panel.selIsText || panel.selIsReplace)
                    readonly property bool chained: panel.info.chainNext !== undefined
                                                    && panel.info.chainNext > 0
                    color: chHover.hovered ? App.themeCard : "transparent"
                    border.color: App.themeBorder; border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: (parent.chained ? "⛓ " : "🔗 ")
                              + App.uiText(App.language,
                                    parent.chained ? "PdfChainUnlink" : "PdfChainLink")
                        color: App.themeTextPrimary; font.pixelSize: 12
                    }
                    HoverHandler { id: chHover }
                    TapHandler {
                        onTapped: {
                            if (parent.chained) panel.ctl.unlinkChain(panel.targetId)
                            else if (panel.surface) panel.surface.startLink(panel.targetId)
                        }
                    }
                }

                Row {
                    visible: panel.showStroke
                    width: parent.width
                    spacing: 16
                    Column {
                        spacing: 4
                        Text { text: App.uiText(App.language, "ImageEditStrokeLabel")
                               color: App.themeTextMuted; font.pixelSize: 11 }
                        ColorPicker {
                            showAlpha: false
                            title: App.uiText(App.language, "ImageEditStrokeLabel")
                            selectedColor: panel.info.strokeColor !== undefined ? panel.info.strokeColor : "#e62c2c"
                            onColorPicked: (c) => {
                                panel.ctl.setBoxStroke(panel.targetId, c)
                                selectedColor = Qt.binding(() => panel.info.strokeColor !== undefined
                                                                 ? panel.info.strokeColor : "#e62c2c")
                            }
                        }
                    }
                    Column {
                        spacing: 4
                        Text { text: App.uiText(App.language, "ImageEditWidthLabel")
                               color: App.themeTextMuted; font.pixelSize: 11 }
                        SpinBox {
                            id: lwBox
                            from: 1; to: 72
                            editable: true
                            font.pixelSize: 12
                            onValueModified: panel.ctl.setBoxLineWidth(panel.targetId, value)
                        }
                    }
                }
                Row {
                    visible: panel.showFill
                    width: parent.width
                    spacing: 6
                    Column {
                        spacing: 4
                        Text { text: App.uiText(App.language, "ImageEditFillLabel")
                               color: App.themeTextMuted; font.pixelSize: 11 }
                        Row {
                            spacing: 6
                            ColorPicker {
                                showAlpha: true
                                title: App.uiText(App.language, "ImageEditFillLabel")
                                selectedColor: panel.info.hasFill === true
                                               ? panel.info.fillColor : "#4de62c2c"
                                onColorPicked: (c) => {
                                    panel.ctl.setBoxFill(panel.targetId, c)
                                    selectedColor = Qt.binding(() => panel.info.hasFill === true
                                                                     ? panel.info.fillColor : "#4de62c2c")
                                }
                            }
                            Rectangle {
                                width: noFillLbl.implicitWidth + 16; height: 26; radius: 6
                                anchors.verticalCenter: parent.verticalCenter
                                color: noFillHover.hovered
                                       ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                                       : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                                border.color: App.themeBorder; border.width: 1
                                opacity: panel.info.hasFill === true ? 1.0 : 0.4
                                Text { id: noFillLbl; anchors.centerIn: parent
                                       text: App.uiText(App.language, "PdfEditNoHighlight")
                                       color: App.themeTextPrimary; font.pixelSize: 11 }
                                HoverHandler { id: noFillHover }
                                TapHandler { onTapped: panel.ctl.setBoxFill(panel.targetId, "transparent") }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: (panel.selIsText || panel.selIsReplace) && panel.info.anchored === true
                    width: chipLbl.implicitWidth + 18; height: 22; radius: 11
                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.15)
                    border.color: App.themeAccent; border.width: 1
                    Text { id: chipLbl; anchors.centerIn: parent
                           text: "\u2693 " + App.uiText(App.language, "PdfEditAnchoredChip")
                           color: App.themeTextPrimary; font.pixelSize: 10 }
                }

                Row {
                    width: parent.width
                    spacing: 8
                    visible: panel.hasSel || panel.ctl.hasClipboard
                    Rectangle {
                        visible: panel.hasSel
                        width: panel.ctl.hasClipboard ? (parent.width - 8) / 2 : parent.width
                        height: 30; radius: 6
                        color: copyHover.hovered
                               ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                               : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                        border.color: App.themeBorder; border.width: 1
                        Text { anchors.centerIn: parent
                               text: "\u2398  " + App.uiText(App.language, "ImageEditCopyBtn")
                               color: App.themeTextPrimary; font.pixelSize: 12 }
                        HoverHandler { id: copyHover }
                        TapHandler {
                            onTapped: {
                                if (panel.surface) panel.surface.commitEditing()
                                panel.ctl.copySelected()
                            }
                        }
                    }
                    Rectangle {
                        visible: panel.ctl.hasClipboard
                        width: panel.hasSel ? (parent.width - 8) / 2 : parent.width
                        height: 30; radius: 6
                        color: pasteHover.hovered
                               ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                               : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                        border.color: App.themeBorder; border.width: 1
                        Text { anchors.centerIn: parent
                               text: "\u2399  " + App.uiText(App.language, "ImageEditPasteBtn")
                               color: App.themeTextPrimary; font.pixelSize: 12 }
                        HoverHandler { id: pasteHover }
                        TapHandler {
                            onTapped: {
                                if (panel.surface) panel.surface.commitEditing()
                                panel.ctl.paste()
                            }
                        }
                    }
                }

                Rectangle {
                    visible: panel.hasSel
                    width: parent.width; height: 30; radius: 6
                    color: delHover.hovered ? Qt.rgba(0.88, 0.35, 0.35, 0.22)
                                            : Qt.rgba(0.88, 0.35, 0.35, 0.10)
                    border.color: "#c25a5a"; border.width: 1
                    Text { anchors.centerIn: parent
                           text: "\u2715  " + App.uiText(App.language,
                                     panel.selInfo.kind === 6 ? "PdfEditDeleteMarkup"
                                                              : "PdfEditDeleteBtn")
                           color: "#e08080"; font.pixelSize: 12 }
                    HoverHandler { id: delHover }
                    TapHandler {
                        onTapped: {
                            if (panel.surface) panel.surface.commitEditing()
                            panel.ctl.removeBox(panel.ctl.selectedId)
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: App.themeBorder }

                Row {
                    width: parent.width
                    spacing: 8
                    Rectangle {
                        width: (parent.width - 8) / 2; height: 32; radius: 6
                        opacity: panel.ctl.dirty ? 1.0 : 0.4
                        color: saveHover.hovered && panel.ctl.dirty
                               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.35)
                               : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20)
                        border.color: App.themeAccent; border.width: 1
                        Text { anchors.centerIn: parent
                               text: App.uiText(App.language, "PdfEditSaveBtn")
                               color: App.themeTextPrimary; font.pixelSize: 12 }
                        HoverHandler { id: saveHover }
                        TapHandler {
                            enabled: panel.ctl.dirty
                            onTapped: {
                                if (panel.surface) panel.surface.commitEditing()
                                panel.ctl.saveOverlay()
                            }
                        }
                        ToolTip.text: App.uiText(App.language, "PdfEditSaveTip")
                        ToolTip.visible: saveHover.hovered
                    }
                    Rectangle {
                        width: (parent.width - 8) / 2; height: 32; radius: 6
                        readonly property bool usable: !panel.ctl.busy && panel.ctl.boxCount > 0
                        opacity: usable ? 1.0 : 0.4
                        color: expHover.hovered && usable
                               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.35)
                               : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20)
                        border.color: App.themeAccent; border.width: 1
                        Text { anchors.centerIn: parent
                               text: App.uiText(App.language, "PdfEditExportBtn")
                               color: App.themeTextPrimary; font.pixelSize: 12 }
                        HoverHandler { id: expHover }
                        TapHandler {
                            enabled: parent.usable
                            onTapped: if (panel.surface) panel.surface.startExport()
                        }
                        // Der Zielname kommt aus einer FUNKTION - eine Bindung darauf wird nie neu ausgewertet und stand ewig auf
                        // dem Stand beim Erzeugen des Panels (leere Klammern). Über `expHover.hovered` hängt sie an etwas, das sich
                        // ändert, und wird beim Zeigen frisch gerechnet.
                        ToolTip.text: expHover.hovered ? panel._exportTip() : ""
                        ToolTip.visible: expHover.hovered
                    }
                }
            }
        }
    }

    Item {
        anchors.fill: parent
        visible: panel.horizontal

        Flickable {
            id: ribbonFlick
            anchors { left: parent.left; right: closeBtnH.left; top: parent.top; bottom: parent.bottom }
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            contentWidth: ribbon.implicitWidth
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick

            // Strg + Rad blättert die Leiste seitlich; bei schmalem Fenster wird das Ribbon rechts abgeschnitten und die
            // dortigen Regler waren sonst nur per Ziehen erreichbar. Ohne Strg fällt das Rad an die Seitenansicht durch.
            // MouseArea ist zwingend - ein interaktives Flickable verarbeitet Radereignisse vor jedem WheelHandler selbst.
            SmoothWheelArea {
                flickable: ribbonFlick
                horizontal: true
                requiredModifier: Qt.ControlModifier
            }

            ScrollBar.horizontal: ScrollBar {
                policy: ribbonFlick.contentWidth > ribbonFlick.width
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                height: 4
                opacity: 0.6
            }

            Row {
                id: ribbon
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14

                Column {
                    spacing: 2
                    Row {
                        spacing: 4
                        ToolBtn { iconName: "select"; toolValue: 0; tip: App.uiText(App.language, "ImageEditToolSelect") }
                        ToolBtn { glyph: "T";      toolValue: 1; tip: App.uiText(App.language, "ImageEditToolText") }
                        ToolBtn { iconName: "pen"; toolValue: 2; tip: App.uiText(App.language, "ImageEditToolPen") }
                        ToolBtn { iconName: "arrow"; toolValue: 3; tip: App.uiText(App.language, "ImageEditToolArrow") }
                        ToolBtn { iconName: "rect"; toolValue: 4; tip: App.uiText(App.language, "ImageEditToolRect") }
                        ToolBtn { iconName: "ellipse"; toolValue: 5; tip: App.uiText(App.language, "ImageEditToolEllipse") }
                        ToolBtn { iconName: "replace"; toolValue: 6; tip: App.uiText(App.language, "PdfEditToolReplace") }
                        ToolBtn { iconName: "caret"; toolValue: 7; tip: App.uiText(App.language, "PdfEditToolCaret") }
                        ToolBtn { iconName: "markup-highlight"; toolValue: 8; styleValue: 0
                                  tip: App.uiText(App.language, "PdfMarkupHighlight") }
                        ToolBtn { iconName: "markup-underline"; toolValue: 8; styleValue: 1
                                  tip: App.uiText(App.language, "PdfMarkupUnderline") }
                        ToolBtn { iconName: "markup-strike"; toolValue: 8; styleValue: 2
                                  tip: App.uiText(App.language, "PdfMarkupStrike") }
                        ToolBtn { iconName: "redact"; toolValue: 9
                                  tip: App.uiText(App.language, "PdfEditToolRedact") }
                        ToolBtn { iconName: "signature"; toolValue: -1
                                  tip: App.uiText(App.language, "PdfEditToolStamp")
                                  onClicked: {
                                      stampPickB.entries = panel.ctl.folderImages()
                                      stampPickB.open()
                                  }
                                  FolderImagePicker {
                                      id: stampPickB
                                      hostWidth: panel.width
                                      onPicked: function(u) {
                                          if (panel.surface) panel.surface.insertStampImage(u)
                                      }
                                      onBrowseRequested: if (panel.surface) panel.surface.pickStampImage()
                                  } }
                    }
                }

                Rectangle { width: 1; height: 34; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                Text {
                    visible: !panel.hasSel && panel.ctl.tool === 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.uiText(App.language, "ImageEditPickHint")
                    color: App.themeTextMuted; font.pixelSize: 11
                }

                Column {
                    visible: panel.showText
                    spacing: 2
                    ComboBox {
                        id: fontBoxH
                        width: 150
                        model: panel.ctl.standardFonts()
                        font.pixelSize: 11
                        onActivated: (idx) => panel.ctl.setBoxFont(panel.targetId, fontBoxH.textAt(idx))
                    }
                }
                Column {
                    visible: panel.showText
                    spacing: 2
                    SpinBox {
                        id: sizeBoxH
                        from: 4; to: 200
                        editable: true
                        font.pixelSize: 11
                        onValueModified: panel.ctl.setBoxFontSize(panel.targetId, value)
                    }
                }
                Column {
                    visible: panel.showText
                    spacing: 2
                    Row {
                        spacing: 4
                        StyleBtn { glyph: "B"; boldGlyph: true;      checked: panel.info.bold === true
                                   onActivated: panel.ctl.setBoxBold(panel.targetId, !panel.info.bold) }
                        StyleBtn { glyph: "I"; italicGlyph: true;    checked: panel.info.italic === true
                                   onActivated: panel.ctl.setBoxItalic(panel.targetId, !panel.info.italic) }
                        StyleBtn { glyph: "U"; underlineGlyph: true; checked: panel.info.underline === true
                                   onActivated: panel.ctl.setBoxUnderline(panel.targetId, !panel.info.underline) }
                    }
                }
                Column {
                    visible: panel.showText
                    spacing: 2
                    Row {
                        spacing: 4
                        AlignBtn { iconName: "align-left"; alignValue: 0 }
                        AlignBtn { iconName: "align-center"; alignValue: 1 }
                        AlignBtn { iconName: "align-right"; alignValue: 2 }
                    }
                }
                Column {
                    visible: panel.showText
                    spacing: 2
                    Row {
                        spacing: 4
                        VAlignBtn { iconName: "valign-top"; vAlignValue: 0 }
                        VAlignBtn { iconName: "valign-middle"; vAlignValue: 1 }
                    }
                }
                Column {
                    visible: panel.showText
                    spacing: 2
                    ColorPicker {
                        showAlpha: false
                        title: App.uiText(App.language, "PdfEditColorLabel")
                        selectedColor: panel.info.textColor !== undefined ? panel.info.textColor : "#000000"
                        onColorPicked: (c) => {
                            panel.ctl.setBoxColor(panel.targetId, c)
                            selectedColor = Qt.binding(() => panel.info.textColor !== undefined
                                                             ? panel.info.textColor : "#000000")
                        }
                    }
                }
                Column {
                    visible: panel.showPaper
                    spacing: 2
                    Row {
                        spacing: 6
                        ColorPicker {
                            showAlpha: true
                            title: App.uiText(App.language, "PdfEditHighlightLabel")
                            selectedColor: panel.info.hasHighlight === true
                                           ? panel.info.highlightColor : panel.defaultPaper
                            onColorPicked: (c) => {
                                panel.ctl.setBoxHighlight(panel.targetId, c)
                                selectedColor = Qt.binding(() => panel.info.hasHighlight === true
                                                                 ? panel.info.highlightColor : panel.defaultPaper)
                            }
                        }
                        Slider {
                            id: opSliderH
                            width: 110
                            anchors.verticalCenter: parent.verticalCenter
                            from: 0; to: 1; stepSize: 0.05
                            onMoved: panel.applyOpacity(value)
                        }
                    }
                }
                Column {
                    visible: panel.showCover
                    spacing: 2
                    ColorPicker {
                        showAlpha: false
                        title: App.uiText(App.language, "PdfEditCoverLabel")
                        selectedColor: panel.info.highlightColor !== undefined
                                       ? panel.info.highlightColor : "#ffffff"
                        onColorPicked: (c) => {
                            panel.ctl.setBoxHighlight(panel.targetId, c)
                            selectedColor = Qt.binding(() => panel.info.highlightColor !== undefined
                                                             ? panel.info.highlightColor : "#ffffff")
                        }
                    }
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: panel.hasSel && (panel.selIsText || panel.selIsReplace)
                    readonly property bool chained: panel.info.chainNext !== undefined
                                                    && panel.info.chainNext > 0
                    width: chRibText.implicitWidth + 16; height: 30; radius: 6
                    color: chRibHover.hovered ? App.themeCard : "transparent"
                    border.color: App.themeBorder; border.width: 1
                    Text {
                        id: chRibText
                        anchors.centerIn: parent
                        text: (parent.chained ? "⛓ " : "🔗 ")
                              + App.uiText(App.language,
                                    parent.chained ? "PdfChainUnlink" : "PdfChainLink")
                        color: App.themeTextPrimary; font.pixelSize: 12
                    }
                    HoverHandler { id: chRibHover }
                    TapHandler {
                        onTapped: {
                            if (parent.chained) panel.ctl.unlinkChain(panel.targetId)
                            else if (panel.surface) panel.surface.startLink(panel.targetId)
                        }
                    }
                }

                Column {
                    visible: panel.showStroke
                    spacing: 2
                    ColorPicker {
                        showAlpha: false
                        title: App.uiText(App.language, "ImageEditStrokeLabel")
                        selectedColor: panel.info.strokeColor !== undefined ? panel.info.strokeColor : "#e62c2c"
                        onColorPicked: (c) => {
                            panel.ctl.setBoxStroke(panel.targetId, c)
                            selectedColor = Qt.binding(() => panel.info.strokeColor !== undefined
                                                             ? panel.info.strokeColor : "#e62c2c")
                        }
                    }
                }
                Column {
                    visible: panel.showStroke
                    spacing: 2
                    SpinBox {
                        id: lwBoxH
                        from: 1; to: 72
                        editable: true
                        font.pixelSize: 11
                        onValueModified: panel.ctl.setBoxLineWidth(panel.targetId, value)
                    }
                }
                Column {
                    visible: panel.showFill
                    spacing: 2
                    Row {
                        spacing: 4
                        ColorPicker {
                            showAlpha: true
                            title: App.uiText(App.language, "ImageEditFillLabel")
                            selectedColor: panel.info.hasFill === true
                                           ? panel.info.fillColor : "#4de62c2c"
                            onColorPicked: (c) => {
                                panel.ctl.setBoxFill(panel.targetId, c)
                                selectedColor = Qt.binding(() => panel.info.hasFill === true
                                                                 ? panel.info.fillColor : "#4de62c2c")
                            }
                        }
                        Rectangle {
                            width: 26; height: 26; radius: 6
                            anchors.verticalCenter: parent.verticalCenter
                            color: noFillHoverH.hovered
                                   ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                                   : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                            border.color: App.themeBorder; border.width: 1
                            opacity: panel.info.hasFill === true ? 1.0 : 0.4
                            Text { anchors.centerIn: parent; text: "\u2215"
                                   color: "#c25a5a"; font.pixelSize: 12 }
                            HoverHandler { id: noFillHoverH }
                            TapHandler { onTapped: panel.ctl.setBoxFill(panel.targetId, "transparent") }
                            ToolTip.text: App.uiText(App.language, "PdfEditNoHighlight")
                            ToolTip.visible: noFillHoverH.hovered
                        }
                    }
                }

                Rectangle {
                    visible: (panel.selIsText || panel.selIsReplace) && panel.info.anchored === true
                    anchors.verticalCenter: parent.verticalCenter
                    width: chipLblH.implicitWidth + 16; height: 20; radius: 10
                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.15)
                    border.color: App.themeAccent; border.width: 1
                    Text { id: chipLblH; anchors.centerIn: parent
                           text: "\u2693 " + App.uiText(App.language, "PdfEditAnchoredChip")
                           color: App.themeTextPrimary; font.pixelSize: 9 }
                }

                Rectangle {
                    visible: panel.hasSel
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30; height: 28; radius: 6
                    color: copyHoverH.hovered
                           ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                           : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                    border.color: App.themeBorder; border.width: 1
                    Text { anchors.centerIn: parent; text: "\u2398"
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: copyHoverH }
                    TapHandler {
                        onTapped: {
                            if (panel.surface) panel.surface.commitEditing()
                            panel.ctl.copySelected()
                        }
                    }
                    ToolTip.text: App.uiText(App.language, "ImageEditCopyBtn")
                    ToolTip.visible: copyHoverH.hovered
                }
                Rectangle {
                    visible: panel.ctl.hasClipboard
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30; height: 28; radius: 6
                    color: pasteHoverH.hovered
                           ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                           : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                    border.color: App.themeBorder; border.width: 1
                    Text { anchors.centerIn: parent; text: "\u2399"
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: pasteHoverH }
                    TapHandler {
                        onTapped: {
                            if (panel.surface) panel.surface.commitEditing()
                            panel.ctl.paste()
                        }
                    }
                    ToolTip.text: App.uiText(App.language, "ImageEditPasteBtn")
                    ToolTip.visible: pasteHoverH.hovered
                }

                Rectangle {
                    visible: panel.hasSel
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30; height: 28; radius: 6
                    color: delHoverH.hovered ? Qt.rgba(0.88, 0.35, 0.35, 0.22)
                                             : Qt.rgba(0.88, 0.35, 0.35, 0.10)
                    border.color: "#c25a5a"; border.width: 1
                    Text { anchors.centerIn: parent; text: "\u2715"
                           color: "#e08080"; font.pixelSize: 12 }
                    HoverHandler { id: delHoverH }
                    TapHandler {
                        onTapped: {
                            if (panel.surface) panel.surface.commitEditing()
                            panel.ctl.removeBox(panel.ctl.selectedId)
                        }
                    }
                    ToolTip.text: App.uiText(App.language,
                                      panel.selInfo.kind === 6 ? "PdfEditDeleteMarkup"
                                                               : "PdfEditDeleteBtn")
                    ToolTip.visible: delHoverH.hovered
                }

                Rectangle { width: 1; height: 34; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 92; height: 30; radius: 6
                    opacity: panel.ctl.dirty ? 1.0 : 0.4
                    color: saveHoverH.hovered && panel.ctl.dirty
                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.35)
                           : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20)
                    border.color: App.themeAccent; border.width: 1
                    Text { anchors.centerIn: parent
                           text: App.uiText(App.language, "PdfEditSaveBtn")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: saveHoverH }
                    TapHandler {
                        enabled: panel.ctl.dirty
                        onTapped: {
                            if (panel.surface) panel.surface.commitEditing()
                            panel.ctl.saveOverlay()
                        }
                    }
                    ToolTip.text: App.uiText(App.language, "PdfEditSaveTip")
                    ToolTip.visible: saveHoverH.hovered
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 92; height: 30; radius: 6
                    readonly property bool usable: !panel.ctl.busy && panel.ctl.boxCount > 0
                    opacity: usable ? 1.0 : 0.4
                    color: expHoverH.hovered && usable
                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.35)
                           : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20)
                    border.color: App.themeAccent; border.width: 1
                    Text { anchors.centerIn: parent
                           text: App.uiText(App.language, "PdfEditExportBtn")
                           color: App.themeTextPrimary; font.pixelSize: 12 }
                    HoverHandler { id: expHoverH }
                    TapHandler {
                        enabled: parent.usable
                        onTapped: if (panel.surface) panel.surface.startExport()
                    }
                    // Das Ribbon hat keinen Platz für den Ziel-Pfad, also ToolTip - und EINZEILIG: der volle Pfad machte den Hinweis
                    // doppelt so hoch wie jeden anderen, der Dateiname genügt. Erst beim Zeigen rechnen (s. Fassung oben).
                    ToolTip.text: expHoverH.hovered ? panel._exportTip() : ""
                    ToolTip.visible: expHoverH.hovered
                }
            }
        }

        Rectangle {
            id: closeBtnH
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            width: 26; height: 26; radius: 13
            color: closeHoverH.hovered
                   ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                   : "transparent"
            Text { anchors.centerIn: parent; text: "\u2715"
                   color: App.themeTextPrimary; font.pixelSize: 12 }
            HoverHandler { id: closeHoverH }
            TapHandler { onTapped: if (panel.surface) panel.surface.editPanelVisible = false }
        }
    }
}
