pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// Schwebende Kompakt-Toolbar des PDF-Editors, einmal je Seiten-Delegate; erscheint nur, wenn die ausgewählte
// Box auf dieser Seite liegt, und bleibt in die Seite geklemmt. `info` liest `ctl.boxInfo(selectedId)`
// REV-getrieben neu; Klickfolgen auf dasselbe Feld verschmelzen im Undo-Stack (FieldCommand::mergeWith).
Item {
    id: bar

    property int  pageIndex: -1
    property real pageScale: 1.0
    property real pageW: 0               // Seitenbreite in Pixel (Klemmen)
    property real pageH: 0
    property var  surface: null
    // Dezentraler PDF-Editor-Controller DIESER Kachel (von PdfSurface via
    // surface.editCtl gesetzt) - ersetzt den früheren globalen PdfEdit-Singleton.
    readonly property PdfEditController ctl: surface ? surface.editCtl : null

    // Eigenschaften der ausgewählten Box, rev-getrieben neu gelesen. Aktiv nur,
    // wenn die Notizen nicht gerade über den Toggle (Alt+Q/◉) ausgeblendet
    // sind - sonst schwebte die Leiste über einer unsichtbaren Box.
    readonly property var  info: (bar.ctl.selectionRev, bar.ctl.boxInfo(bar.ctl.selectedId))
    readonly property bool active: bar.ctl.editMode && info.exists === true
                                   && info.page === pageIndex
                                   && (surface ? surface.notesVisible : true)
    readonly property bool isText:    active && info.isText === true
    // „Text ersetzen": volle Text-Regler, aber KEIN Papier-Button (Deckfläche
    // fix Weiß - keine Farbwahl-UI in dieser Phase).
    readonly property bool isReplace: active && info.isReplace === true
    readonly property bool isTextual: isText || isReplace
    readonly property bool isShape:  active && info.isShape === true
    readonly property bool isStroke: active && info.isStroke === true

    visible: active
    width: content.implicitWidth + 12
    height: 32
    z: 5

    readonly property real boxX: active ? info.xPt * pageScale : 0
    readonly property real boxY: active ? info.yPt * pageScale : 0
    readonly property real boxW: active ? info.wPt * pageScale : 0
    readonly property real boxH: active ? info.hPt * pageScale : 0
    x: Math.max(2, Math.min(boxX + boxW / 2 - width / 2, pageW - width - 2))
    y: (boxY - height - 10 >= 0) ? boxY - height - 10
                                 : Math.min(boxY + boxH + 10,
                                            Math.max(2, pageH - height - 2))

    component TBtn: Rectangle {
        id: tb
        property string glyph: ""
        property string iconName: ""
        property string tip: ""
        property bool checked: false
        property bool boldGlyph: false
        property bool italicGlyph: false
        property bool underlineGlyph: false
        property color glyphColor: App.themeTextPrimary
        signal activated()
        width: 26; height: 24; radius: 5
        opacity: enabled ? 1.0 : 0.35
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (tbHover.hovered && enabled
                ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                : "transparent")
        border.color: checked ? App.themeAccent : "transparent"
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: tb.glyph
            color: tb.glyphColor
            font.pixelSize: 12
            font.bold: tb.boldGlyph
            font.italic: tb.italicGlyph
            font.underline: tb.underlineGlyph
               visible: tb.iconName.length === 0 }
        DrawnIcon { anchors.centerIn: parent; name: tb.iconName; size: 16
                     color: tb.glyphColor
                     visible: tb.iconName.length > 0 }
        HoverHandler { id: tbHover; enabled: tb.enabled }
        TapHandler { enabled: tb.enabled; onTapped: tb.activated() }
        ToolTip.text: tb.tip
        ToolTip.visible: tbHover.hovered && tb.tip.length > 0
    }

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: App.themeToolbarBg
        border.color: App.themeBorder
        border.width: 1
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 2

        TBtn { visible: bar.isTextual; glyph: "B"; boldGlyph: true;      checked: bar.info.bold === true
               onActivated: bar.ctl.setBoxBold(bar.ctl.selectedId, !bar.info.bold) }
        TBtn { visible: bar.isTextual; glyph: "I"; italicGlyph: true;    checked: bar.info.italic === true
               onActivated: bar.ctl.setBoxItalic(bar.ctl.selectedId, !bar.info.italic) }
        TBtn { visible: bar.isTextual; glyph: "U"; underlineGlyph: true; checked: bar.info.underline === true
               onActivated: bar.ctl.setBoxUnderline(bar.ctl.selectedId, !bar.info.underline) }

        Rectangle { visible: bar.isTextual; width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        TBtn { visible: bar.isTextual; iconName: "minus"
               onActivated: bar.ctl.setBoxFontSize(bar.ctl.selectedId,
                                                   Math.max(4, Math.round(bar.info.fontSizePt) - 1)) }
        Text {
            visible: bar.isTextual
            anchors.verticalCenter: parent.verticalCenter
            width: 24; horizontalAlignment: Text.AlignHCenter
            text: bar.isTextual ? Math.round(bar.info.fontSizePt) : ""
            color: App.themeTextPrimary; font.pixelSize: 11
        }
        TBtn { visible: bar.isTextual; iconName: "plus"
               onActivated: bar.ctl.setBoxFontSize(bar.ctl.selectedId,
                                                   Math.min(200, Math.round(bar.info.fontSizePt) + 1)) }

        Rectangle { visible: bar.isTextual; width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        TBtn { visible: bar.isTextual; iconName: "align-left"; checked: bar.info.alignment === 0
               onActivated: bar.ctl.setBoxAlignment(bar.ctl.selectedId, 0) }
        TBtn { visible: bar.isTextual; iconName: "align-center"; checked: bar.info.alignment === 1
               onActivated: bar.ctl.setBoxAlignment(bar.ctl.selectedId, 1) }
        TBtn { visible: bar.isTextual; iconName: "align-right"; checked: bar.info.alignment === 2
               onActivated: bar.ctl.setBoxAlignment(bar.ctl.selectedId, 2) }

        Rectangle { visible: bar.isTextual; width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        TBtn { visible: bar.isTextual; iconName: "valign-top"; checked: bar.info.vAlign === 0
               tip: App.uiText(App.language, "PdfEditVAlignLabel")
               onActivated: bar.ctl.setBoxVAlign(bar.ctl.selectedId, 0) }
        TBtn { visible: bar.isTextual; iconName: "valign-middle"; checked: bar.info.vAlign === 1
               tip: App.uiText(App.language, "PdfEditVAlignLabel")
               onActivated: bar.ctl.setBoxVAlign(bar.ctl.selectedId, 1) }

        Rectangle { visible: bar.isTextual; width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        Rectangle {
            visible: bar.isTextual
            width: 26; height: 24; radius: 5
            color: colHover.hovered
                   ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                   : "transparent"
            Text { anchors.horizontalCenter: parent.horizontalCenter; y: 1
                   text: "A"; font.bold: true; font.pixelSize: 12
                   color: App.themeTextPrimary }
            Rectangle {
                anchors { bottom: parent.bottom; bottomMargin: 3
                          horizontalCenter: parent.horizontalCenter }
                width: 16; height: 4; radius: 1
                color: bar.isTextual ? bar.info.textColor : "#000000"
                border.color: App.themeBorder; border.width: 1
            }
            HoverHandler { id: colHover }
            TapHandler { onTapped: { palette.mode = "text"; palette.open() } }
            ToolTip.text: App.uiText(App.language, "PdfEditColorLabel")
            ToolTip.visible: colHover.hovered
        }

        Rectangle {
            visible: bar.isTextual
            width: 26; height: 24; radius: 5
            color: hiHover.hovered
                   ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                   : "transparent"
            Rectangle {
                anchors.centerIn: parent
                width: 16; height: 16; radius: 3
                color: (bar.isTextual && bar.info.hasHighlight) ? bar.info.highlightColor : "transparent"
                border.color: App.themeBorder; border.width: 1
                Text {
                    anchors.centerIn: parent
                    visible: !(bar.isTextual && bar.info.hasHighlight)
                    text: "\u2215"; color: "#c25a5a"; font.pixelSize: 12
                }
            }
            HoverHandler { id: hiHover }
            TapHandler { onTapped: { palette.mode = "highlight"; palette.open() } }
            ToolTip.text: App.uiText(App.language, bar.isReplace ? "PdfEditCoverLabel" : "PdfEditHighlightLabel")
            ToolTip.visible: hiHover.hovered
        }

        Rectangle {
            visible: bar.isStroke || bar.isShape
            width: 26; height: 24; radius: 5
            color: scHover.hovered
                   ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                   : "transparent"
            Rectangle { anchors.centerIn: parent; width: 16; height: 16; radius: 8
                        color: "transparent"; border.width: 3
                        border.color: bar.active ? bar.info.strokeColor : "#e62c2c" }
            HoverHandler { id: scHover }
            TapHandler { onTapped: { palette.mode = "stroke"; palette.open() } }
            ToolTip.text: App.uiText(App.language, "ImageEditStrokeLabel")
            ToolTip.visible: scHover.hovered
        }
        TBtn { visible: bar.isStroke || bar.isShape; iconName: "minus"
               onActivated: bar.ctl.setBoxLineWidth(bar.ctl.selectedId,
                                                    Math.max(1, Math.round(bar.info.lineWidth) - 1)) }
        Text {
            visible: bar.isStroke || bar.isShape
            anchors.verticalCenter: parent.verticalCenter
            width: 24; horizontalAlignment: Text.AlignHCenter
            text: (bar.isStroke || bar.isShape) ? Math.round(bar.info.lineWidth) : ""
            color: App.themeTextPrimary; font.pixelSize: 11
        }
        TBtn { visible: bar.isStroke || bar.isShape; iconName: "plus"
               onActivated: bar.ctl.setBoxLineWidth(bar.ctl.selectedId,
                                                    Math.min(72, Math.round(bar.info.lineWidth) + 1)) }
        Rectangle {
            visible: bar.isShape
            width: 26; height: 24; radius: 5
            color: fcHover.hovered
                   ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                   : "transparent"
            Rectangle {
                anchors.centerIn: parent; width: 16; height: 16; radius: 3
                color: (bar.isShape && bar.info.hasFill) ? bar.info.fillColor : "transparent"
                border.color: App.themeBorder; border.width: 1
                Text { anchors.centerIn: parent; visible: !(bar.isShape && bar.info.hasFill)
                       text: "\u2215"; color: "#c25a5a"; font.pixelSize: 12 }
            }
            HoverHandler { id: fcHover }
            TapHandler { onTapped: { palette.mode = "fill"; palette.open() } }
            ToolTip.text: App.uiText(App.language, "ImageEditFillLabel")
            ToolTip.visible: fcHover.hovered
        }

        Rectangle { width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        TBtn {
            iconName: "copy"
            tip: App.uiText(App.language, "ImageEditCopyBtn")
            onActivated: { if (bar.surface) bar.surface.commitEditing(); bar.ctl.copySelected() }
        }
        TBtn {
            iconName: "close"; glyphColor: "#e05a5a"
            tip: App.uiText(App.language,
                     bar.info.kind === 6 ? "PdfEditDeleteMarkup" : "PdfEditDeleteBtn")
            onActivated: {
                if (bar.surface) bar.surface.commitEditing()
                bar.ctl.removeBox(bar.ctl.selectedId)
            }
        }
    }

    Popup {
        id: palette
        property string mode: "text"             // text | highlight | stroke | fill
        // „Keine" bei Hervorhebung/Füllung - NICHT bei „Text ersetzen" (die
        // Deckfläche muss deckend bleiben).
        readonly property bool allowNone: mode === "fill"
                                          || (mode === "highlight" && !bar.isReplace)
        readonly property var textColors: [
            "#000000", "#444444", "#888888", "#ffffff",
            "#d32f2f", "#f57c00", "#fbc02d", "#388e3c",
            "#00b4a0", "#1976d2", "#3f51b5", "#7b1fa2",
            "#c2185b", "#6d4c41" ]
        readonly property var highlightColors: [
            "#FFF59D", "#A5D6A7", "#90CAF9", "#F48FB1",
            "#FFE082", "#E1BEE7", "#FFCC80", "#CFD8DC" ]
        readonly property var swatches: (mode === "highlight") ? highlightColors : textColors
        function pick(c) {
            const id = bar.ctl.selectedId
            if (palette.mode === "text")           bar.ctl.setBoxColor(id, c)
            else if (palette.mode === "highlight") bar.ctl.setBoxHighlight(id, c)
            else if (palette.mode === "stroke")    bar.ctl.setBoxStroke(id, c)
            else                                   bar.ctl.setBoxFill(id, c)
            palette.close()
        }
        x: Math.round((bar.width - width) / 2)
        y: bar.height + 4
        padding: 8
        background: Rectangle {
            color: App.themeToolbarBg
            border.color: App.themeBorder; border.width: 1
            radius: 8
        }
        contentItem: Grid {
            columns: 5
            spacing: 6
            Rectangle {
                visible: palette.allowNone
                width: 22; height: 22; radius: 4
                color: "transparent"
                border.color: App.themeBorder; border.width: 1
                Text { anchors.centerIn: parent; text: "\u2215"
                       color: "#c25a5a"; font.pixelSize: 13 }
                HoverHandler { id: noneHover }
                TapHandler { onTapped: palette.pick("transparent") }
                ToolTip.text: App.uiText(App.language, "PdfEditNoHighlight")
                ToolTip.visible: noneHover.hovered
            }
            Repeater {
                model: palette.swatches
                delegate: Rectangle {
                    id: swatch
                    required property var modelData
                    width: 22; height: 22; radius: 4
                    color: modelData
                    border.color: swHover.hovered ? App.themeAccent : App.themeBorder
                    border.width: swHover.hovered ? 2 : 1
                    HoverHandler { id: swHover }
                    TapHandler { onTapped: palette.pick(swatch.modelData) }
                }
            }
        }
    }
}
