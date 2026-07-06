pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  PdfEditToolbar.qml — schwebende Kompakt-Toolbar des PDF-Editors (Word-artig).
//
//  Lebt EINMAL je Seiten-Delegate (innerhalb von pageImg) und erscheint nur,
//  wenn die AUSGEWÄHLTE Box auf genau dieser Seite liegt. Position: mittig
//  über der Box; fehlt oben der Platz, springt sie unter die Box — stets in
//  die Seite geklemmt.
//
//  DATENFLUSS: `info` liest bar.ctl.boxInfo(selectedId) REV-GETRIEBEN neu
//  (selectionRev bumpt bei Auswahl- UND Datenänderung der ausgewählten Box) —
//  dasselbe Muster wie _audioRev in PdfSurface. Die Buttons schreiben über die
//  setBox…-Invokables zurück; aufeinanderfolgende Klicks desselben Feldes
//  verschmelzen im Undo-Stack zu EINEM Schritt (FieldCommand::mergeWith).
//
//  BEWUSST: Stil-Klicks schließen eine offene TEXT-Bearbeitung NICHT ab —
//  der Nutzer kann beim Tippen fett/kursiv umschalten und weitertippen
//  (Stil-Kommandos sind von der Text-Session unabhängig). Nur LÖSCHEN
//  committet vorher (die Box verschwindet ja mitsamt Session).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: bar

    property int  pageIndex: -1
    property real pageScale: 1.0
    property real pageW: 0               // Seitenbreite in Pixel (Klemmen)
    property real pageH: 0
    property var  surface: null
    // Dezentraler PDF-Editor-Controller DIESER Kachel (von PdfSurface via
    // surface.editCtl gesetzt) — ersetzt den früheren globalen PdfEdit-Singleton.
    readonly property PdfEditController ctl: surface ? surface.editCtl : null

    // Eigenschaften der ausgewählten Box, rev-getrieben neu gelesen. Aktiv nur,
    // wenn die Notizen nicht gerade über den Toggle (Alt+Q/◉) ausgeblendet
    // sind — sonst schwebte die Leiste über einer unsichtbaren Box.
    readonly property var  info: (bar.ctl.selectionRev, bar.ctl.boxInfo(bar.ctl.selectedId))
    readonly property bool active: bar.ctl.editMode && info.exists === true
                                   && info.page === pageIndex
                                   && (surface ? surface.notesVisible : true)

    visible: active
    width: content.implicitWidth + 12
    height: 32
    z: 5

    // ── Position relativ zur Box (Pixel) ──────────────────────────────────────
    readonly property real boxX: active ? info.xPt * pageScale : 0
    readonly property real boxY: active ? info.yPt * pageScale : 0
    readonly property real boxW: active ? info.wPt * pageScale : 0
    readonly property real boxH: active ? info.hPt * pageScale : 0
    x: Math.max(2, Math.min(boxX + boxW / 2 - width / 2, pageW - width - 2))
    y: (boxY - height - 10 >= 0) ? boxY - height - 10
                                 : Math.min(boxY + boxH + 10,
                                            Math.max(2, pageH - height - 2))

    // ── Mini-Button (kompakter als PdfToolButton; Glyph darf B/I/U tragen) ────
    component TBtn: Rectangle {
        id: tb
        property string glyph: ""
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
        }
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

        // ── Stil ──────────────────────────────────────────────────────────────
        TBtn { glyph: "B"; boldGlyph: true;      checked: bar.info.bold === true
               onActivated: bar.ctl.setBoxBold(bar.ctl.selectedId, !bar.info.bold) }
        TBtn { glyph: "I"; italicGlyph: true;    checked: bar.info.italic === true
               onActivated: bar.ctl.setBoxItalic(bar.ctl.selectedId, !bar.info.italic) }
        TBtn { glyph: "U"; underlineGlyph: true; checked: bar.info.underline === true
               onActivated: bar.ctl.setBoxUnderline(bar.ctl.selectedId, !bar.info.underline) }

        Rectangle { width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        // ── Schriftgröße ──────────────────────────────────────────────────────
        TBtn { glyph: "\u2212"
               onActivated: bar.ctl.setBoxFontSize(bar.ctl.selectedId,
                                                   Math.max(4, Math.round(bar.info.fontSizePt) - 1)) }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 24; horizontalAlignment: Text.AlignHCenter
            text: bar.active ? Math.round(bar.info.fontSizePt) : ""
            color: App.themeTextPrimary; font.pixelSize: 11
        }
        TBtn { glyph: "+"
               onActivated: bar.ctl.setBoxFontSize(bar.ctl.selectedId,
                                                   Math.min(200, Math.round(bar.info.fontSizePt) + 1)) }

        Rectangle { width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        // ── Ausrichtung (0=links, 1=zentriert, 2=rechts) ──────────────────────
        TBtn { glyph: "\u2B05"; checked: bar.info.alignment === 0
               onActivated: bar.ctl.setBoxAlignment(bar.ctl.selectedId, 0) }
        TBtn { glyph: "\u2194"; checked: bar.info.alignment === 1
               onActivated: bar.ctl.setBoxAlignment(bar.ctl.selectedId, 1) }
        TBtn { glyph: "\u2B95"; checked: bar.info.alignment === 2
               onActivated: bar.ctl.setBoxAlignment(bar.ctl.selectedId, 2) }

        Rectangle { width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        // ── Vertikale Ausrichtung (0=oben wie Word-Textfeld, 1=mittig) ────────
        TBtn { glyph: "\u2912"; checked: bar.info.vAlign === 0
               tip: App.uiText(App.language, "PdfEditVAlignLabel")
               onActivated: bar.ctl.setBoxVAlign(bar.ctl.selectedId, 0) }
        TBtn { glyph: "\u2195"; checked: bar.info.vAlign === 1
               tip: App.uiText(App.language, "PdfEditVAlignLabel")
               onActivated: bar.ctl.setBoxVAlign(bar.ctl.selectedId, 1) }

        Rectangle { width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        // ── Textfarbe: „A" über aktuellem Farbbalken; öffnet die Palette ──────
        Rectangle {
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
                color: bar.active ? bar.info.textColor : "#000000"
                border.color: App.themeBorder; border.width: 1
            }
            HoverHandler { id: colHover }
            TapHandler { onTapped: { palette.forHighlight = false; palette.open() } }
            ToolTip.text: App.uiText(App.language, "PdfEditColorLabel")
            ToolTip.visible: colHover.hovered
        }

        // ── Hervorhebung: Farbfeld (Schrägstrich = keine); öffnet die Palette ─
        Rectangle {
            width: 26; height: 24; radius: 5
            color: hiHover.hovered
                   ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                   : "transparent"
            Rectangle {
                anchors.centerIn: parent
                width: 16; height: 16; radius: 3
                color: (bar.active && bar.info.hasHighlight) ? bar.info.highlightColor : "transparent"
                border.color: App.themeBorder; border.width: 1
                Text {
                    anchors.centerIn: parent
                    visible: !(bar.active && bar.info.hasHighlight)
                    text: "\u2215"; color: "#c25a5a"; font.pixelSize: 12
                }
            }
            HoverHandler { id: hiHover }
            TapHandler { onTapped: { palette.forHighlight = true; palette.open() } }
            ToolTip.text: App.uiText(App.language, "PdfEditHighlightLabel")
            ToolTip.visible: hiHover.hovered
        }

        Rectangle { width: 1; height: 16; color: App.themeBorder
                    anchors.verticalCenter: parent.verticalCenter }

        // ── Löschen (committet die offene Bearbeitung — Box verschwindet) ─────
        TBtn {
            glyph: "\u2715"; glyphColor: "#e05a5a"
            tip: App.uiText(App.language, "PdfEditDeleteBtn")
            onActivated: {
                if (bar.surface) bar.surface.commitEditing()
                bar.ctl.removeBox(bar.ctl.selectedId)
            }
        }
    }

    // ── Farbpalette (gemeinsames Popup für Textfarbe UND Hervorhebung) ────────
    //  Textfarben: kräftige Lese-Palette. Hervorhebungen: Pastelltöne (Marker-
    //  Charakter) + „Keine" als erste Kachel (transparent).
    Popup {
        id: palette
        property bool forHighlight: false
        readonly property var textColors: [
            "#000000", "#444444", "#888888", "#ffffff",
            "#d32f2f", "#f57c00", "#fbc02d", "#388e3c",
            "#00b4a0", "#1976d2", "#3f51b5", "#7b1fa2",
            "#c2185b", "#6d4c41" ]
        readonly property var highlightColors: [
            "#FFF59D", "#A5D6A7", "#90CAF9", "#F48FB1",
            "#FFE082", "#E1BEE7", "#FFCC80", "#CFD8DC" ]
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
            // „Keine"-Kachel nur im Hervorhebungs-Modus.
            Rectangle {
                visible: palette.forHighlight
                width: 22; height: 22; radius: 4
                color: "transparent"
                border.color: App.themeBorder; border.width: 1
                Text { anchors.centerIn: parent; text: "\u2215"
                       color: "#c25a5a"; font.pixelSize: 13 }
                HoverHandler { id: noneHover }
                TapHandler {
                    onTapped: {
                        bar.ctl.setBoxHighlight(bar.ctl.selectedId, "transparent")
                        palette.close()
                    }
                }
                ToolTip.text: App.uiText(App.language, "PdfEditNoHighlight")
                ToolTip.visible: noneHover.hovered
            }
            Repeater {
                model: palette.forHighlight ? palette.highlightColors : palette.textColors
                delegate: Rectangle {
                    id: swatch
                    required property var modelData
                    width: 22; height: 22; radius: 4
                    color: modelData
                    border.color: swHover.hovered ? App.themeAccent : App.themeBorder
                    border.width: swHover.hovered ? 2 : 1
                    HoverHandler { id: swHover }
                    TapHandler {
                        onTapped: {
                            if (palette.forHighlight)
                                bar.ctl.setBoxHighlight(bar.ctl.selectedId, swatch.modelData)
                            else
                                bar.ctl.setBoxColor(bar.ctl.selectedId, swatch.modelData)
                            palette.close()
                        }
                    }
                }
            }
        }
    }
}
