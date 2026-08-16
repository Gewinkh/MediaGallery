pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  ImageEditToolbar.qml — schwebende Kompakt-Toolbar des Bild-Editors.
//
//  Liegt EINMAL in der Bild-Ebene von ImageSurface (Kind der content-Ebene →
//  folgt Zoom/Pan positionsmäßig, bleibt aber selbst feste Chrome-Größe) und
//  erscheint über der ausgewählten Annotation. Inhalt passt sich der Art an:
//   • Text  → B/I/U, Größe, Ausrichtung, Vertikal, Textfarbe, Hervorhebung
//   • Strich/Form → Linienfarbe, Linienbreite, (Form:) Füllung
//  gemeinsam: Kopieren, Löschen.
//
//  DATENFLUSS wie PdfEditToolbar: `info` liest ctl.annInfo(selectedId)
//  REV-GETRIEBEN (selectionRev) neu; Buttons schreiben über die setAnn…-
//  Invokables zurück (aufeinanderfolgende Klicks verschmelzen im Undo-Stack).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: bar

    property real imgScale: 1.0
    property real contentW: 0                     // Bild-Anzeigebreite (Klemmen)
    property real contentH: 0
    property var  surface: null
    readonly property ImageEditController ctl: surface ? surface.editCtl : null

    readonly property var  info: (bar.ctl.selectionRev, bar.ctl.annInfo(bar.ctl.selectedId))
    readonly property bool active: bar.ctl.editMode && info.exists === true
                                   && (surface ? surface.notesVisible : true)
    readonly property bool isText:  active && info.isText === true
    readonly property bool isShape: active && info.isShape === true
    readonly property bool isStroke: active && info.isStroke === true

    visible: active
    width: content.implicitWidth + 12
    height: 32
    z: 6

    readonly property real boxX: active ? info.xPx * imgScale : 0
    readonly property real boxY: active ? info.yPx * imgScale : 0
    readonly property real boxW: active ? info.wPx * imgScale : 0
    readonly property real boxH: active ? info.hPx * imgScale : 0
    x: Math.max(2, Math.min(boxX + boxW / 2 - width / 2, contentW - width - 2))
    y: (boxY - height - 10 >= 0) ? boxY - height - 10
                                 : Math.min(boxY + boxH + 10, Math.max(2, contentH - height - 2))

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
            text: tb.glyph; color: tb.glyphColor; font.pixelSize: 12
            font.bold: tb.boldGlyph; font.italic: tb.italicGlyph; font.underline: tb.underlineGlyph
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
        anchors.fill: parent; radius: 8
        color: App.themeToolbarBg; border.color: App.themeBorder; border.width: 1
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 2

        // ── TEXT-Regler ───────────────────────────────────────────────────────
        TBtn { visible: bar.isText; glyph: "B"; boldGlyph: true; checked: bar.info.bold === true
               onActivated: bar.ctl.setAnnBold(bar.ctl.selectedId, !bar.info.bold) }
        TBtn { visible: bar.isText; glyph: "I"; italicGlyph: true; checked: bar.info.italic === true
               onActivated: bar.ctl.setAnnItalic(bar.ctl.selectedId, !bar.info.italic) }
        TBtn { visible: bar.isText; glyph: "U"; underlineGlyph: true; checked: bar.info.underline === true
               onActivated: bar.ctl.setAnnUnderline(bar.ctl.selectedId, !bar.info.underline) }
        Rectangle { visible: bar.isText; width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }
        TBtn { visible: bar.isText; iconName: "minus"
               onActivated: bar.ctl.setAnnFontSize(bar.ctl.selectedId,
                                                   Math.max(4, Math.round(bar.info.fontSizePx) - 2)) }
        Text {
            visible: bar.isText; anchors.verticalCenter: parent.verticalCenter
            width: 28; horizontalAlignment: Text.AlignHCenter
            text: bar.isText ? Math.round(bar.info.fontSizePx) : ""
            color: App.themeTextPrimary; font.pixelSize: 11
        }
        TBtn { visible: bar.isText; iconName: "plus"
               onActivated: bar.ctl.setAnnFontSize(bar.ctl.selectedId,
                                                   Math.min(800, Math.round(bar.info.fontSizePx) + 2)) }
        Rectangle { visible: bar.isText; width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }
        TBtn { visible: bar.isText; iconName: "align-left"; checked: bar.info.alignment === 0
               onActivated: bar.ctl.setAnnAlignment(bar.ctl.selectedId, 0) }
        TBtn { visible: bar.isText; iconName: "align-center"; checked: bar.info.alignment === 1
               onActivated: bar.ctl.setAnnAlignment(bar.ctl.selectedId, 1) }
        TBtn { visible: bar.isText; iconName: "align-right"; checked: bar.info.alignment === 2
               onActivated: bar.ctl.setAnnAlignment(bar.ctl.selectedId, 2) }
        TBtn { visible: bar.isText; iconName: "valign-top"; checked: bar.info.vAlign === 0
               tip: App.uiText(App.language, "PdfEditVAlignLabel")
               onActivated: bar.ctl.setAnnVAlign(bar.ctl.selectedId, 0) }
        TBtn { visible: bar.isText; iconName: "valign-middle"; checked: bar.info.vAlign === 1
               tip: App.uiText(App.language, "PdfEditVAlignLabel")
               onActivated: bar.ctl.setAnnVAlign(bar.ctl.selectedId, 1) }

        // Textfarbe
        Rectangle {
            visible: bar.isText; width: 26; height: 24; radius: 5
            color: tcHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16) : "transparent"
            Text { anchors.horizontalCenter: parent.horizontalCenter; y: 1; text: "A"; font.bold: true
                   font.pixelSize: 12; color: App.themeTextPrimary }
            Rectangle { anchors { bottom: parent.bottom; bottomMargin: 3; horizontalCenter: parent.horizontalCenter }
                        width: 16; height: 4; radius: 1
                        color: bar.isText ? bar.info.textColor : "#000000"
                        border.color: App.themeBorder; border.width: 1 }
            HoverHandler { id: tcHover }
            TapHandler { onTapped: { palette.mode = "text"; palette.open() } }
            ToolTip.text: App.uiText(App.language, "PdfEditColorLabel"); ToolTip.visible: tcHover.hovered
        }
        // Hervorhebung (Notiz-Papier)
        Rectangle {
            visible: bar.isText; width: 26; height: 24; radius: 5
            color: hiHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16) : "transparent"
            Rectangle {
                anchors.centerIn: parent; width: 16; height: 16; radius: 3
                color: (bar.isText && bar.info.hasHighlight) ? bar.info.highlightColor : "transparent"
                border.color: App.themeBorder; border.width: 1
                Text { anchors.centerIn: parent; visible: !(bar.isText && bar.info.hasHighlight)
                       text: "\u2215"; color: "#c25a5a"; font.pixelSize: 12 }
            }
            HoverHandler { id: hiHover }
            TapHandler { onTapped: { palette.mode = "highlight"; palette.open() } }
            ToolTip.text: App.uiText(App.language, "PdfEditHighlightLabel"); ToolTip.visible: hiHover.hovered
        }

        // ── STRICH-/FORM-Regler ───────────────────────────────────────────────
        // Linienfarbe
        Rectangle {
            visible: bar.isStroke || bar.isShape; width: 26; height: 24; radius: 5
            color: scHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16) : "transparent"
            Rectangle { anchors.centerIn: parent; width: 16; height: 16; radius: 8
                        color: "transparent"; border.width: 3
                        border.color: bar.active ? bar.info.strokeColor : "#e62c2c" }
            HoverHandler { id: scHover }
            TapHandler { onTapped: { palette.mode = "stroke"; palette.open() } }
            ToolTip.text: App.uiText(App.language, "ImageEditStrokeLabel"); ToolTip.visible: scHover.hovered
        }
        // Linienbreite
        TBtn { visible: bar.isStroke || bar.isShape; iconName: "minus"
               onActivated: bar.ctl.setAnnLineWidth(bar.ctl.selectedId, Math.max(1, Math.round(bar.info.lineWidth) - 1)) }
        Text {
            visible: bar.isStroke || bar.isShape; anchors.verticalCenter: parent.verticalCenter
            width: 24; horizontalAlignment: Text.AlignHCenter
            text: (bar.isStroke || bar.isShape) ? Math.round(bar.info.lineWidth) : ""
            color: App.themeTextPrimary; font.pixelSize: 11
        }
        TBtn { visible: bar.isStroke || bar.isShape; iconName: "plus"
               onActivated: bar.ctl.setAnnLineWidth(bar.ctl.selectedId, Math.min(200, Math.round(bar.info.lineWidth) + 1)) }
        // Füllung (nur Formen)
        Rectangle {
            visible: bar.isShape; width: 26; height: 24; radius: 5
            color: fcHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16) : "transparent"
            Rectangle {
                anchors.centerIn: parent; width: 16; height: 16; radius: 3
                color: (bar.isShape && bar.info.hasFill) ? bar.info.fillColor : "transparent"
                border.color: App.themeBorder; border.width: 1
                Text { anchors.centerIn: parent; visible: !(bar.isShape && bar.info.hasFill)
                       text: "\u2215"; color: "#c25a5a"; font.pixelSize: 12 }
            }
            HoverHandler { id: fcHover }
            TapHandler { onTapped: { palette.mode = "fill"; palette.open() } }
            ToolTip.text: App.uiText(App.language, "ImageEditFillLabel"); ToolTip.visible: fcHover.hovered
        }

        Rectangle { width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        // ── Kopieren + Löschen (gemeinsam) ────────────────────────────────────
        TBtn { iconName: "copy"; tip: App.uiText(App.language, "ImageEditCopyBtn")
               onActivated: { if (bar.surface) bar.surface.commitEditing(); bar.ctl.copySelected() } }
        TBtn { iconName: "close"; glyphColor: "#e05a5a"; tip: App.uiText(App.language, "PdfEditDeleteBtn")
               onActivated: { if (bar.surface) bar.surface.commitEditing(); bar.ctl.removeAnn(bar.ctl.selectedId) } }
    }

    // ── Farbpalette (Text / Hervorhebung / Linie / Füllung) ───────────────────
    Popup {
        id: palette
        property string mode: "text"             // text | highlight | stroke | fill
        readonly property bool allowNone: mode === "highlight" || mode === "fill"
        readonly property var solidColors: [
            "#000000", "#444444", "#888888", "#ffffff",
            "#d32f2f", "#f57c00", "#fbc02d", "#388e3c",
            "#00b4a0", "#1976d2", "#3f51b5", "#7b1fa2",
            "#c2185b", "#6d4c41" ]
        readonly property var pastelColors: [
            "#FFF59D", "#A5D6A7", "#90CAF9", "#F48FB1",
            "#FFE082", "#E1BEE7", "#FFCC80", "#CFD8DC" ]
        readonly property var swatches: (mode === "highlight") ? pastelColors : solidColors
        function pick(c) {
            const id = bar.ctl.selectedId
            if (palette.mode === "text")           bar.ctl.setAnnColor(id, c)
            else if (palette.mode === "highlight") bar.ctl.setAnnHighlight(id, c)
            else if (palette.mode === "stroke")    bar.ctl.setAnnStroke(id, c)
            else                                   bar.ctl.setAnnFill(id, c)
            palette.close()
        }
        x: Math.round((bar.width - width) / 2)
        y: bar.height + 4
        padding: 8
        background: Rectangle { color: App.themeToolbarBg; border.color: App.themeBorder; border.width: 1; radius: 8 }
        contentItem: Grid {
            columns: 5; spacing: 6
            Rectangle {
                visible: palette.allowNone
                width: 22; height: 22; radius: 4; color: "transparent"
                border.color: App.themeBorder; border.width: 1
                Text { anchors.centerIn: parent; text: "\u2215"; color: "#c25a5a"; font.pixelSize: 13 }
                HoverHandler { id: noneHover }
                TapHandler { onTapped: palette.pick("transparent") }
                ToolTip.text: App.uiText(App.language, "PdfEditNoHighlight"); ToolTip.visible: noneHover.hovered
            }
            Repeater {
                model: palette.swatches
                delegate: Rectangle {
                    id: sw
                    required property var modelData
                    width: 22; height: 22; radius: 4; color: modelData
                    border.color: swHover.hovered ? App.themeAccent : App.themeBorder
                    border.width: swHover.hovered ? 2 : 1
                    HoverHandler { id: swHover }
                    TapHandler { onTapped: palette.pick(sw.modelData) }
                }
            }
        }
    }
}
