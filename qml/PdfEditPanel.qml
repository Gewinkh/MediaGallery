pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  PdfEditPanel.qml — Werkzeuge + Eigenschaften des PDF-Editors in ZWEI
//  Layouts:
//
//   • horizontal:false (Standard) → RECHTE SEITENLEISTE (Muster wie die
//     Audio-Leiste: Sidebar-Fläche, linke Trennlinie, Kopf mit ✕, Flickable).
//   • horizontal:true → OBERE LEISTE („wie Word"): kompaktes Ribbon mit
//     denselben Reglern in Mini-Gruppen (Label über Control), horizontal
//     scrollbar bei schmalen Fenstern, ✕ rechts.
//
//  WERKZEUG-PALETTE (Muster Bild-Editor): Auswählen · Textnotiz · Stift ·
//  Pfeil · Rechteck · Ellipse. Die Eigenschafts-Regler sind kontextsensitiv:
//   • Auswahl vorhanden → Regler der AUSGEWÄHLTEN Annotation (Ziel = Auswahl)
//   • keine Auswahl     → Vorlagen-Defaults des AKTIVEN Werkzeugs
//                         (Ziel = -1 → Controller setzt nur die Vorlage)
//
//  Die Position wählt der Nutzer in den Einstellungen (panel.ctl.panelOnTop);
//  PdfSurface instanziiert BEIDE Varianten und blendet genau eine ein. Das
//  Panel öffnet AUTOMATISCH beim Erstellen/Auswählen einer Annotation (kein
//  Toolbar-Button mehr) und schließt über sein ✕.
//
//  BINDUNGS-STRATEGIE: ComboBox/SpinBox/Slider SCHREIBEN ihre Werte bei
//  Nutzereingaben selbst → externe Bindungen würden dauerhaft reißen. Deshalb
//  werden sie IMPERATIV über refreshFromSelection() nachgeführt (getriggert
//  durch selectionRev/defaultRev/toolChanged; beide Layout-Varianten werden
//  gemeinsam synchronisiert). Der ColorPicker schreibt selectedColor beim OK
//  ebenfalls selbst; onColorPicked stellt die Bindung danach explizit über
//  Qt.binding() wieder her.
//
//  DECKKRAFT: Der Slider steuert das Alpha des Notiz-Papiers (Post-it-
//  Hintergrund). 0 = kein Papier (reiner Text, wie „Keine"); Ziehen erzeugt
//  dank mergeWith der Feld-Kommandos genau EINEN Undo-Schritt.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: panel

    property var  surface: null          // PdfSurface-Root (Commit + Export-Start)
    // Dezentraler PDF-Editor-Controller DIESER Kachel (von PdfSurface via
    // surface.editCtl gesetzt) — ersetzt den früheren globalen PdfEdit-Singleton.
    readonly property PdfEditController ctl: surface ? surface.editCtl : null
    property bool horizontal: false      // false = Seitenleiste, true = Ribbon

    // Eigenschaften der ausgewählten Box, rev-getrieben (Muster wie Toolbar);
    // OHNE Auswahl liefern die Vorlagen-Defaults die angezeigten Werte
    // (rev-getrieben über defaultRev — Muster Bild-Editor).
    readonly property var  selInfo: (panel.ctl.selectionRev, panel.ctl.boxInfo(panel.ctl.selectedId))
    readonly property bool hasSel: selInfo.exists === true
    readonly property var  info: hasSel ? selInfo
                                        : (panel.ctl.defaultRev, panel.ctl.defaultInfo())
    // Ziel der Stil-Setter: Auswahl — oder -1 (nur Vorlage, kein Kommando).
    readonly property int  targetId: hasSel ? panel.ctl.selectedId : -1

    // Kontextsensitive Regler-Sichtbarkeit: Art der Auswahl — oder ohne
    // Auswahl das aktive Werkzeug (Text-Regler beim Text-Werkzeug usw.).
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
    // Notiz-Papier (Farbe/„Keine"/Deckkraft) NUR für klassische Post-its —
    // die Deckfläche von „Text ersetzen" ist fix Weiß (keine Farbwahl-UI).
    readonly property bool showPaper:   selIsText || (!hasSel && panel.ctl.tool === 1)

    // Standard-Papierfarbe, wenn der Deckkraft-Slider aus „Keine" heraus
    // hochgezogen wird (klassisches Post-it-Gelb, s. PdfEditTypes).
    readonly property color defaultPaper: "#FEF39B"

    color: App.themeSidebarBg
    // Trennlinie: links (Seitenleiste) bzw. unten (Ribbon).
    Rectangle { anchors.left: parent.left; width: 1; height: parent.height
                color: App.themeBorder; visible: !panel.horizontal }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                color: App.themeBorder; visible: panel.horizontal }

    // Klicks/Wheel auf leeren Panel-Flächen abfangen (kein Durchgriff auf die
    // Seiten-Interaktion darunter) — gleiches Muster wie audioPanel.
    MouseArea { anchors.fill: parent; onWheel: (wheel) => { wheel.accepted = true } }

    // ── Wiederverwendete Mini-Buttons (Inline-Components: nur auf Root-Ebene
    //    einer QML-Datei zulässig; sichtbar in der gesamten Datei) ─────────────
    component StyleBtn: Rectangle {
        id: sb
        property string glyph: ""
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
               font.underline: sb.underlineGlyph }
        HoverHandler { id: sbHover }
        TapHandler { onTapped: sb.activated() }
    }
    component AlignBtn: Rectangle {
        id: ab
        property string glyph: ""
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
               color: App.themeTextPrimary; font.pixelSize: 12 }
        HoverHandler { id: abHover }
        TapHandler { onTapped: panel.ctl.setBoxAlignment(panel.targetId, ab.alignValue) }
    }
    //  Vertikale Ausrichtung (0 = oben wie Word-Textfeld, 1 = mittig) —
    //  gleiches Muster wie AlignBtn, schreibt über setBoxVAlign.
    component VAlignBtn: Rectangle {
        id: vb
        property string glyph: ""
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
               color: App.themeTextPrimary; font.pixelSize: 12 }
        HoverHandler { id: vbHover }
        TapHandler { onTapped: panel.ctl.setBoxVAlign(panel.targetId, vb.vAlignValue) }
    }
    //  Werkzeug-Button der Palette (Muster Bild-Editor): checked = aktives
    //  Werkzeug; Klick wechselt das Werkzeug (Controller committet Sessions).
    component ToolBtn: Rectangle {
        id: tb
        property string glyph: ""
        property int toolValue: 0
        property string tip: ""
        readonly property bool checked: panel.ctl.tool === toolValue
        width: 34; height: 30; radius: 6
        color: checked ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (tbHover.hovered
                ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07))
        border.color: checked ? App.themeAccent : App.themeBorder
        border.width: 1
        Text { anchors.centerIn: parent; text: tb.glyph
               color: App.themeTextPrimary; font.pixelSize: 13 }
        HoverHandler { id: tbHover }
        TapHandler {
            onTapped: {
                if (panel.surface) panel.surface.commitEditing()
                panel.ctl.tool = tb.toolValue
                //  ⇄ „Text ersetzen": eine bestehende Text-Markierung wird
                //  direkt zur Ersetzen-Box. Bewusst HIER (nicht über
                //  onToolChanged): war ⇄ bereits aktiv, feuert kein Signal —
                //  der Klick wäre wirkungslos (Nutzerbefund 2026-07-17).
                if (tb.toolValue === 6 && panel.surface
                        && panel.surface.replaceSelectionNow)
                    panel.surface.replaceSelectionNow()
            }
        }
        ToolTip.text: tb.tip
        ToolTip.visible: tbHover.hovered && tb.tip.length > 0
    }
    //  Ribbon-Gruppe: Mini-Label über dem Control (nur horizontal genutzt).
    component RibbonLabel: Text {
        color: App.themeTextMuted
        font.pixelSize: 9
    }

    // ── Imperative Synchronisation beider Layout-Varianten ────────────────────
    //  Läuft bei Auswahl-/Datenänderung, Vorlagen-Änderung UND Werkzeugwechsel
    //  — panel.info liefert dabei Auswahl ODER Vorlagen-Defaults.
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
    //  Deckkraft anwenden: Basisfarbe ist das aktuelle Papier — oder das
    //  Standard-Gelb, wenn gerade „Keine" aktiv ist (Slider reaktiviert so
    //  den Zettel). Alpha 0 == transparent == „Keine".
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

    // ═════════════════════════════════════════════════════════════════════════
    //  VARIANTE A — rechte Seitenleiste (vertikal)
    // ═════════════════════════════════════════════════════════════════════════
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

                // ═══ Sektion: Werkzeuge (Muster Bild-Editor) ══════════════════
                Text {
                    text: App.uiText(App.language, "ImageEditToolsLabel")
                    color: App.themeTextMuted; font.pixelSize: 11
                }
                Grid {
                    columns: 3
                    spacing: 4
                    ToolBtn { glyph: "\u2196"; toolValue: 0; tip: App.uiText(App.language, "ImageEditToolSelect") }
                    ToolBtn { glyph: "T";      toolValue: 1; tip: App.uiText(App.language, "ImageEditToolText") }
                    ToolBtn { glyph: "\u270E"; toolValue: 2; tip: App.uiText(App.language, "ImageEditToolPen") }
                    ToolBtn { glyph: "\u2197"; toolValue: 3; tip: App.uiText(App.language, "ImageEditToolArrow") }
                    ToolBtn { glyph: "\u25AD"; toolValue: 4; tip: App.uiText(App.language, "ImageEditToolRect") }
                    ToolBtn { glyph: "\u2B2D"; toolValue: 5; tip: App.uiText(App.language, "ImageEditToolEllipse") }
                    // „Text ersetzen" (PDF-exklusiv): weiße Deckfläche + Textbox.
                    ToolBtn { glyph: "\u21C4"; toolValue: 6; tip: App.uiText(App.language, "PdfEditToolReplace") }
                }

                Rectangle { width: parent.width; height: 1; color: App.themeBorder }

                // ═══ Sektion: Eigenschaften (Auswahl ODER Werkzeug-Vorlage) ═══
                Text {
                    visible: !panel.hasSel && panel.ctl.tool === 0
                    width: parent.width
                    text: App.uiText(App.language, "ImageEditPickHint")
                    color: App.themeTextMuted; font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                // Schriftart
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
                // TATSÄCHLICH gerendert wird — identisch in Anzeige und Export.
                Text {
                    visible: panel.showText
                             && panel.ctl.resolvedFont(fontBox.currentText) !== fontBox.currentText
                    width: parent.width
                    text: "\u2192 " + panel.ctl.resolvedFont(fontBox.currentText)
                    color: App.themeTextMuted; font.pixelSize: 10
                    elide: Text.ElideRight
                }

                // Größe + Stil in einer Zeile
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

                // Ausrichtung (horizontal)
                Text { visible: panel.showText
                       text: App.uiText(App.language, "PdfEditAlignLabel")
                       color: App.themeTextMuted; font.pixelSize: 11 }
                Row {
                    visible: panel.showText
                    spacing: 4
                    AlignBtn { glyph: "\u2B05"; alignValue: 0 }
                    AlignBtn { glyph: "\u2194"; alignValue: 1 }
                    AlignBtn { glyph: "\u2B95"; alignValue: 2 }
                }

                // Vertikale Ausrichtung: oben (Word-Textfeld, Standard) / mittig
                Text { visible: panel.showText
                       text: App.uiText(App.language, "PdfEditVAlignLabel")
                       color: App.themeTextMuted; font.pixelSize: 11 }
                Row {
                    visible: panel.showText
                    spacing: 4
                    VAlignBtn { glyph: "\u2912"; vAlignValue: 0 }
                    VAlignBtn { glyph: "\u2195"; vAlignValue: 1 }
                }

                // Farben (wiederverwendeter ColorPicker; Bindung nach Nutzer-
                // Commit explizit wiederherstellen — s. Kopfkommentar).
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
                        // Nur Post-its: die Deckfläche von „Text ersetzen" ist fix Weiß.
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
                            // „Keine": Papier entfernen (== Deckkraft 0).
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
                }

                // Deckkraft des Notiz-Papiers (Post-it-Transparenz) — nur
                // Post-its; die Deckfläche von „Text ersetzen" ist fix Weiß.
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

                // ═══ Zeichnen: Linienfarbe / -breite / Füllung ════════════════
                //  (Freihand/Pfeil: Farbe + Breite; Rechteck/Ellipse zusätzlich
                //  Füllung. Ohne Auswahl → Vorlage des aktiven Werkzeugs.)
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
                            // „Keine": Füllung entfernen (Alpha 0 → nur Kontur).
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

                // Anker-Chip (Box wurde per Zeilenfang auf einer PDF-Textzeile
                // erstellt).
                Rectangle {
                    visible: (panel.selIsText || panel.selIsReplace) && panel.info.anchored === true
                    width: chipLbl.implicitWidth + 18; height: 22; radius: 11
                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.15)
                    border.color: App.themeAccent; border.width: 1
                    Text { id: chipLbl; anchors.centerIn: parent
                           text: "\u2693 " + App.uiText(App.language, "PdfEditAnchoredChip")
                           color: App.themeTextPrimary; font.pixelSize: 10 }
                }

                // Kopieren/Einfügen (kachel-lokale Zwischenablage; Strg+C/V
                // funktionieren zusätzlich als Shortcuts der Surface).
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

                // Löschen (committet zuerst — die Box verschwindet mitsamt Session)
                Rectangle {
                    visible: panel.hasSel
                    width: parent.width; height: 30; radius: 6
                    color: delHover.hovered ? Qt.rgba(0.88, 0.35, 0.35, 0.22)
                                            : Qt.rgba(0.88, 0.35, 0.35, 0.10)
                    border.color: "#c25a5a"; border.width: 1
                    Text { anchors.centerIn: parent
                           text: "\u2715  " + App.uiText(App.language, "PdfEditDeleteBtn")
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

                // ═══ Sektion: Dokument ════════════════════════════════════════
                Row {
                    width: parent.width
                    spacing: 8
                    // Speichern → Sidecar (bleibt editierbar)
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
                    // Export → gerendertes PDF (Ziel je Überschreib-Option)
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
                            onTapped: if (panel.surface) panel.surface.startPdfExport()
                        }
                        ToolTip.text: App.uiText(App.language, "PdfEditExportTip")
                        ToolTip.visible: expHover.hovered
                    }
                }

                // Ziel des nächsten Exports (immer eine neue Kopie
                // „…_bearbeitet(.n).pdf" — das Original bleibt unangetastet,
                // die Notizen bleiben über das Sidecar reversibel).
                Text {
                    width: parent.width
                    text: panel.ctl.exportTargetPath()
                    color: App.themeTextMuted; font.pixelSize: 10
                    elide: Text.ElideMiddle
                    maximumLineCount: 1
                }
            }
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  VARIANTE B — obere Leiste („wie Word"; horizontal:true)
    //  Gleiche Regler in Mini-Gruppen; bei schmalen Fenstern horizontal
    //  scrollbar. Hinweis-/Zieltexte der Seitenleiste wandern in ToolTips.
    // ═════════════════════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: panel.horizontal

        Flickable {
            anchors { left: parent.left; right: closeBtnH.left; top: parent.top; bottom: parent.bottom }
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            contentWidth: ribbon.implicitWidth
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick

            Row {
                id: ribbon
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14

                // ── Werkzeug-Palette (immer sichtbar) ────────────────────────
                Column {
                    spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "ImageEditToolsLabel") }
                    Row {
                        spacing: 4
                        ToolBtn { glyph: "\u2196"; toolValue: 0; tip: App.uiText(App.language, "ImageEditToolSelect") }
                        ToolBtn { glyph: "T";      toolValue: 1; tip: App.uiText(App.language, "ImageEditToolText") }
                        ToolBtn { glyph: "\u270E"; toolValue: 2; tip: App.uiText(App.language, "ImageEditToolPen") }
                        ToolBtn { glyph: "\u2197"; toolValue: 3; tip: App.uiText(App.language, "ImageEditToolArrow") }
                        ToolBtn { glyph: "\u25AD"; toolValue: 4; tip: App.uiText(App.language, "ImageEditToolRect") }
                        ToolBtn { glyph: "\u2B2D"; toolValue: 5; tip: App.uiText(App.language, "ImageEditToolEllipse") }
                        // „Text ersetzen" (PDF-exklusiv): weiße Deckfläche + Textbox.
                        ToolBtn { glyph: "\u21C4"; toolValue: 6; tip: App.uiText(App.language, "PdfEditToolReplace") }
                    }
                }

                Rectangle { width: 1; height: 34; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                // Ohne Auswahl + Auswahl-Werkzeug: nur Hinweis + Dokument-Gruppe.
                Text {
                    visible: !panel.hasSel && panel.ctl.tool === 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.uiText(App.language, "ImageEditPickHint")
                    color: App.themeTextMuted; font.pixelSize: 11
                }

                Column {
                    visible: panel.showText
                    spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "PdfEditFontLabel") }
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
                    RibbonLabel { text: App.uiText(App.language, "PdfEditSizeLabel") }
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
                    RibbonLabel { text: App.uiText(App.language, "PdfEditStyleLabel") }
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
                    RibbonLabel { text: App.uiText(App.language, "PdfEditAlignLabel") }
                    Row {
                        spacing: 4
                        AlignBtn { glyph: "\u2B05"; alignValue: 0 }
                        AlignBtn { glyph: "\u2194"; alignValue: 1 }
                        AlignBtn { glyph: "\u2B95"; alignValue: 2 }
                    }
                }
                Column {
                    visible: panel.showText
                    spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "PdfEditVAlignLabel") }
                    Row {
                        spacing: 4
                        VAlignBtn { glyph: "\u2912"; vAlignValue: 0 }
                        VAlignBtn { glyph: "\u2195"; vAlignValue: 1 }
                    }
                }
                Column {
                    visible: panel.showText
                    spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "PdfEditColorLabel") }
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
                    // Nur Post-its: die Deckfläche von „Text ersetzen" ist fix Weiß.
                    visible: panel.showPaper
                    spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "PdfEditHighlightLabel")
                                  + " \u00B7 " + App.uiText(App.language, "PdfEditOpacityLabel") }
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

                // ── Zeichnen: Linienfarbe / -breite / Füllung ────────────────
                Column {
                    visible: panel.showStroke
                    spacing: 2
                    RibbonLabel { text: App.uiText(App.language, "ImageEditStrokeLabel") }
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
                    RibbonLabel { text: App.uiText(App.language, "ImageEditWidthLabel") }
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
                    RibbonLabel { text: App.uiText(App.language, "ImageEditFillLabel") }
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
                        // „Keine": Füllung entfernen (Alpha 0 → nur Kontur).
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

                // Anker-Chip
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

                // Kopieren / Einfügen (kachel-lokale Zwischenablage)
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

                // Löschen
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
                    ToolTip.text: App.uiText(App.language, "PdfEditDeleteBtn")
                    ToolTip.visible: delHoverH.hovered
                }

                Rectangle { width: 1; height: 34; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                // ── Dokument-Gruppe (immer sichtbar) ─────────────────────────
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
                        onTapped: if (panel.surface) panel.surface.startPdfExport()
                    }
                    // Ribbon hat keinen Platz für den Ziel-Pfad → ToolTip
                    // (immer eine neue Kopie „…_bearbeitet(.n).pdf").
                    ToolTip.text: App.uiText(App.language, "PdfEditExportTip") + "\n"
                                  + panel.ctl.exportTargetPath()
                    ToolTip.visible: expHoverH.hovered
                }
            }
        }

        // ✕ fest rechts (außerhalb des scrollenden Bereichs).
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
