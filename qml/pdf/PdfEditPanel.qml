pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

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

    //  „Als PDF exportieren (Zieldatei)" — der Name kommt erst beim Zeigen dazu,
    //  und wenn es (noch) keinen gibt, entfallen die Klammern ganz statt leer
    //  dazustehen.
    function _exportTip() {
        const tip = App.uiText(App.language, "PdfEditExportTip")
        const name = panel.ctl ? String(panel.ctl.exportTargetPath()).split("/").pop() : ""
        return name.length > 0 ? tip + " (" + name + ")" : tip
    }
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
    // Notiz-Papier (Farbe/„Keine"/Deckkraft) NUR für klassische Post-its.
    readonly property bool showPaper:   selIsText || (!hasSel && panel.ctl.tool === 1)
    // Deckfläche („Text ersetzen"): eigene Farbwahl OHNE „Keine"/Deckkraft —
    // die Cover-Farbe ist frei, bleibt aber immer deckend (Controller erzwingt).
    readonly property bool showCover:   selIsReplace || (!hasSel && panel.ctl.tool === 6)

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
        property url iconSource: ""
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
               visible: String(sb.iconSource).length === 0 }
        ThemedIcon { anchors.centerIn: parent; source: sb.iconSource; size: 16
                     visible: String(sb.iconSource).length > 0 }
        HoverHandler { id: sbHover }
        TapHandler { onTapped: sb.activated() }
    }
    component AlignBtn: Rectangle {
        id: ab
        property string glyph: ""
        property url iconSource: ""
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
               visible: String(ab.iconSource).length === 0 }
        ThemedIcon { anchors.centerIn: parent; source: ab.iconSource; size: 16
                     visible: String(ab.iconSource).length > 0 }
        HoverHandler { id: abHover }
        TapHandler { onTapped: panel.ctl.setBoxAlignment(panel.targetId, ab.alignValue) }
    }
    //  Vertikale Ausrichtung (0 = oben wie Word-Textfeld, 1 = mittig) —
    //  gleiches Muster wie AlignBtn, schreibt über setBoxVAlign.
    component VAlignBtn: Rectangle {
        id: vb
        property string glyph: ""
        property url iconSource: ""
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
               visible: String(vb.iconSource).length === 0 }
        ThemedIcon { anchors.centerIn: parent; source: vb.iconSource; size: 16
                     visible: String(vb.iconSource).length > 0 }
        HoverHandler { id: vbHover }
        TapHandler { onTapped: panel.ctl.setBoxVAlign(panel.targetId, vb.vAlignValue) }
    }
    //  Werkzeug-Button der Palette (Muster Bild-Editor): checked = aktives
    //  Werkzeug; Klick wechselt das Werkzeug (Controller committet Sessions).
    component ToolBtn: Rectangle {
        id: tb
        property string glyph: ""
        //  Symbol statt Glyphe (Regel 29). Eingefärbt wird in C++, s. ThemedIcon.
        property url iconSource: ""
        property int toolValue: 0
        //  Nur für die Markier-Knöpfe: zusätzlich der Stil (0/1/2). −1 = kein
        //  Stil, dann entscheidet allein das Werkzeug über „aktiv".
        property int styleValue: -1
        property string tip: ""
        //  Knöpfe, die KEIN Werkzeug setzen (toolValue < 0), sondern nur etwas
        //  auslösen — z. B. der Bild-Einfügen-Knopf.
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
               visible: String(tb.iconSource).length === 0
               color: App.themeTextPrimary; font.pixelSize: 13 }
        ThemedIcon { anchors.centerIn: parent; source: tb.iconSource; size: 16
                     visible: String(tb.iconSource).length > 0 }
        HoverHandler { id: tbHover }
        TapHandler {
            onTapped: {
                if (panel.surface) panel.surface.commitEditing()
                if (tb.styleValue >= 0)
                    panel.ctl.setMarkupStyle(tb.styleValue)
                if (tb.toolValue >= 0)
                    panel.ctl.tool = tb.toolValue
                tb.clicked()
                //  ⇄ „Text ersetzen": eine bestehende Text-Markierung wird
                //  direkt zur Ersetzen-Box. Bewusst HIER (nicht über
                //  onToolChanged): war ⇄ bereits aktiv, feuert kein Signal —
                //  der Klick wäre wirkungslos (Nutzerbefund 2026-07-17).
                if (tb.toolValue === 6 && panel.surface
                        && panel.surface.replaceSelectionNow)
                    panel.surface.replaceSelectionNow()
                //  ▮ „Schwärzen": genauso — eine bestehende Textauswahl wird
                //  direkt zur Schwärzung, ohne Auswahl bleibt der Ziehweg.
                if (tb.toolValue === 9 && panel.surface
                        && panel.surface.startRedact)
                    panel.surface.startRedact()
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
                    ToolBtn { iconSource: "qrc:/qml/icons/select.svg"; toolValue: 0; tip: App.uiText(App.language, "ImageEditToolSelect") }
                    ToolBtn { glyph: "T";      toolValue: 1; tip: App.uiText(App.language, "ImageEditToolText") }
                    ToolBtn { iconSource: "qrc:/qml/icons/pen.svg"; toolValue: 2; tip: App.uiText(App.language, "ImageEditToolPen") }
                    ToolBtn { iconSource: "qrc:/qml/icons/arrow.svg"; toolValue: 3; tip: App.uiText(App.language, "ImageEditToolArrow") }
                    ToolBtn { iconSource: "qrc:/qml/icons/rect.svg"; toolValue: 4; tip: App.uiText(App.language, "ImageEditToolRect") }
                    ToolBtn { iconSource: "qrc:/qml/icons/ellipse.svg"; toolValue: 5; tip: App.uiText(App.language, "ImageEditToolEllipse") }
                    // „Text ersetzen" (PDF-exklusiv): weiße Deckfläche + Textbox.
                    ToolBtn { iconSource: "qrc:/qml/icons/replace.svg"; toolValue: 6; tip: App.uiText(App.language, "PdfEditToolReplace") }
                    // „Text bearbeiten" (PDF-exklusiv): Caret DIREKT in der
                    // eingebetteten Textebene — kein Overlay, die Seite bleibt
                    // vektoriell.
                    ToolBtn { iconSource: "qrc:/qml/icons/caret.svg"; toolValue: 7; tip: App.uiText(App.language, "PdfEditToolCaret") }
                    // Textmarkierung (PDF-exklusiv): Ziehen über Text markiert,
                    // unterstreicht oder streicht durch — je nach Stil-Knopf.
                    ToolBtn { iconSource: "qrc:/qml/icons/markup-highlight.svg"; toolValue: 8; styleValue: 0
                              tip: App.uiText(App.language, "PdfMarkupHighlight") }
                    ToolBtn { iconSource: "qrc:/qml/icons/markup-underline.svg"; toolValue: 8; styleValue: 1
                              tip: App.uiText(App.language, "PdfMarkupUnderline") }
                    ToolBtn { iconSource: "qrc:/qml/icons/markup-strike.svg"; toolValue: 8; styleValue: 2
                              tip: App.uiText(App.language, "PdfMarkupStrike") }
                    // „Schwärzen": deckt ab UND entfernt den Text beim Export
                    // aus dem Content-Stream. Die Grenzen sagt der einmalige
                    // Hinweis beim ersten Schwärzen (PdfSurface).
                    ToolBtn { iconSource: "qrc:/qml/icons/redact.svg"; toolValue: 9
                              tip: App.uiText(App.language, "PdfEditToolRedact") }
                    // Signatur/Stempel: erst die Bilder im ORDNER anbieten (der
                    // häufige Fall, ganz ohne Dateidialog), „Durchsuchen…"
                    // fällt auf den Dateidialog der Kachel zurück.
                    ToolBtn { iconSource: "qrc:/qml/icons/signature.svg"; toolValue: -1
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

                // \u2500\u2500 OCR (gescannte PDFs): erkennt die Textzeilen der aktuellen
                //    Seite, danach greifen Zeilenfang + \u201EText ersetzen"-Vorbef\u00FCllung
                //    wie bei einer eingebetteten Textebene. Nur wenn Tesseract da ist.
                Rectangle {
                    id: ocrBtn
                    //  Fehlt Tesseract, bleibt der Knopf SICHTBAR, wird aber
                    //  ausgegraut \u2014 der Hover-Text sagt dann, was fehlt.
                    readonly property bool ocrOn: panel.surface && panel.surface.textCtl
                                                  && panel.surface.textCtl.ocrAvailable
                    width: parent.width; height: 30; radius: 6
                    visible: panel.surface && panel.surface.textCtl
                    opacity: ocrOn ? 1.0 : 0.45
                    color: (ocrOn && ocrHover.hovered) ? App.themeCard : "transparent"
                    border.color: App.themeBorder; border.width: 1
                    Row {
                        anchors.centerIn: parent; spacing: 6
                        BusyIndicator {
                            width: 16; height: 16
                            anchors.verticalCenter: parent.verticalCenter
                            running: panel.surface && panel.surface.textCtl
                                     && panel.surface.textCtl.ocrBusy
                            visible: running
                        }
                        Text {
                            text: App.uiText(App.language, "PdfOcrBtn")
                            color: ocrBtn.ocrOn ? App.themeTextPrimary : App.themeTextMuted
                            font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    HoverHandler { id: ocrHover }
                    TapHandler {
                        enabled: ocrBtn.ocrOn
                        onTapped: if (panel.surface) panel.surface.requestOcr()
                    }
                    ToolTip.visible: ocrHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: ocrBtn.ocrOn
                                  ? App.uiText(App.language, "PdfOcrTip")
                                  : App.uiText(App.language, "LibMissingTesseract")
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
                    AlignBtn { iconSource: "qrc:/qml/icons/align-left.svg"; alignValue: 0 }
                    AlignBtn { iconSource: "qrc:/qml/icons/align-center.svg"; alignValue: 1 }
                    AlignBtn { iconSource: "qrc:/qml/icons/align-right.svg"; alignValue: 2 }
                }

                // Vertikale Ausrichtung: oben (Word-Textfeld, Standard) / mittig
                Text { visible: panel.showText
                       text: App.uiText(App.language, "PdfEditVAlignLabel")
                       color: App.themeTextMuted; font.pixelSize: 11 }
                Row {
                    visible: panel.showText
                    spacing: 4
                    VAlignBtn { iconSource: "qrc:/qml/icons/valign-top.svg"; vAlignValue: 0 }
                    VAlignBtn { iconSource: "qrc:/qml/icons/valign-middle.svg"; vAlignValue: 1 }
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
                    // Deckfläche („Text ersetzen") — nur Farbe, immer deckend.
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

                // ── Reflow: Verketten/Lösen (nur textführende Auswahl) ────────
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
                           //  Bei einer Markierung ist „Textbox" schlicht falsch.
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
                    //  Export → neue PDF-Kopie. WELCHER Weg (verlustfrei oder
                    //  Raster) genommen wird, steht in den Einstellungen
                    //  (`PdfEdit.exportLossless`) — hier gibt es bewusst nur
                    //  noch EINEN Knopf, s. `PdfSurface.startExport`.
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
                        //  Ziel und Modus stehen im ToolTip. Früher standen hier
                        //  zusätzlich ein zweiter Knopf („→ PDF (Text im Stream)")
                        //  und eine eigene Pfad-Zeile — beides ist entfallen: der
                        //  Weg ist jetzt eine EINSTELLUNG, keine Entscheidung bei
                        //  jedem einzelnen Export.
                        //  Einzeilig, s. Ribbon-Fassung weiter unten.
                        //  Der Zielname kommt aus einer FUNKTION — eine Bindung
                        //  darauf wird nie neu ausgewertet und stand deshalb
                        //  ewig auf dem Stand beim Erzeugen des Panels: leere
                        //  Klammern „()" (Nutzerbefund). Über `expHover.hovered`
                        //  hängt sie an etwas, das sich ändert, und wird beim
                        //  Zeigen frisch gerechnet.
                        ToolTip.text: expHover.hovered ? panel._exportTip() : ""
                        ToolTip.visible: expHover.hovered
                    }
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
            id: ribbonFlick
            anchors { left: parent.left; right: closeBtnH.left; top: parent.top; bottom: parent.bottom }
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            contentWidth: ribbon.implicitWidth
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick

            //  STRG + Mausrad blättert die Leiste SEITLICH (Rad hoch = nach
            //  rechts). Nötig, weil das Ribbon bei schmalem Fenster — etwa
            //  einer halben Bildschirmbreite oder einer Split-View-Kachel —
            //  rechts abgeschnitten wird und die dortigen Regler sonst nur
            //  per Ziehen erreichbar waren.
            //
            //  OHNE Strg bleibt das Rad unverändert: es fällt an die
            //  Seitenansicht darunter durch (Scrollen bzw. Strg-loses
            //  Verhalten dort) — die Leiste kapert das Rad also nicht.
            //  Eine MouseArea ist zwingend, ein WheelHandler genügt NICHT:
            //  ein interaktives Flickable verarbeitet Radereignisse vorher
            //  selbst (s. „Bekannte Workarounds" in Structure.md).
            SmoothWheelArea {
                flickable: ribbonFlick
                horizontal: true
                requiredModifier: Qt.ControlModifier
            }

            //  Sichtbare Rückmeldung, dass seitlich mehr da ist (und zweiter,
            //  entdeckbarer Weg zum selben Ziel). Blendet sich aus, sobald die
            //  Leiste vollständig passt.
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

                // ── Werkzeug-Palette (immer sichtbar) ────────────────────────
                Column {
                    spacing: 2
                    Row {
                        spacing: 4
                        ToolBtn { iconSource: "qrc:/qml/icons/select.svg"; toolValue: 0; tip: App.uiText(App.language, "ImageEditToolSelect") }
                        ToolBtn { glyph: "T";      toolValue: 1; tip: App.uiText(App.language, "ImageEditToolText") }
                        ToolBtn { iconSource: "qrc:/qml/icons/pen.svg"; toolValue: 2; tip: App.uiText(App.language, "ImageEditToolPen") }
                        ToolBtn { iconSource: "qrc:/qml/icons/arrow.svg"; toolValue: 3; tip: App.uiText(App.language, "ImageEditToolArrow") }
                        ToolBtn { iconSource: "qrc:/qml/icons/rect.svg"; toolValue: 4; tip: App.uiText(App.language, "ImageEditToolRect") }
                        ToolBtn { iconSource: "qrc:/qml/icons/ellipse.svg"; toolValue: 5; tip: App.uiText(App.language, "ImageEditToolEllipse") }
                        // „Text ersetzen" (PDF-exklusiv): weiße Deckfläche + Textbox.
                        ToolBtn { iconSource: "qrc:/qml/icons/replace.svg"; toolValue: 6; tip: App.uiText(App.language, "PdfEditToolReplace") }
                        // „Text bearbeiten" (PDF-exklusiv): Caret DIREKT in der
                        // eingebetteten Textebene.
                        ToolBtn { iconSource: "qrc:/qml/icons/caret.svg"; toolValue: 7; tip: App.uiText(App.language, "PdfEditToolCaret") }
                        // Textmarkierung (PDF-exklusiv): Ziehen über Text markiert,
                        // unterstreicht oder streicht durch — je nach Stil-Knopf.
                        ToolBtn { iconSource: "qrc:/qml/icons/markup-highlight.svg"; toolValue: 8; styleValue: 0
                                  tip: App.uiText(App.language, "PdfMarkupHighlight") }
                        ToolBtn { iconSource: "qrc:/qml/icons/markup-underline.svg"; toolValue: 8; styleValue: 1
                                  tip: App.uiText(App.language, "PdfMarkupUnderline") }
                        ToolBtn { iconSource: "qrc:/qml/icons/markup-strike.svg"; toolValue: 8; styleValue: 2
                                  tip: App.uiText(App.language, "PdfMarkupStrike") }
                        // „Schwärzen": deckt ab UND entfernt den Text beim
                        // Export aus dem Content-Stream. Die Beschriftung
                        // bleibt kurz — die Grenzen sagt der einmalige Hinweis
                        // beim ersten Schwärzen (PdfSurface._redactHintOnce).
                        ToolBtn { iconSource: "qrc:/qml/icons/redact.svg"; toolValue: 9
                                  tip: App.uiText(App.language, "PdfEditToolRedact") }
                        // Signatur/Stempel: wie in der schmalen Leiste — erst
                        // die Bilder im Ordner, dann der Dateidialog.
                        ToolBtn { iconSource: "qrc:/qml/icons/signature.svg"; toolValue: -1
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

                // \u2500\u2500 OCR (gescannte PDFs). Ohne Tesseract ausgegraut statt weg \u2014
                //    der Hover-Text nennt dann die fehlende Bibliothek.
                //  Kein Beschriftungs-Ausgleich mehr nötig: seit die Gruppen im
                //  Ribbon ohne Überschriften auskommen, sitzt jeder Knopf direkt
                //  in der Zeile — und die zentriert senkrecht.
                Rectangle {
                    id: ocrRibBtn
                    readonly property bool ocrOn: panel.surface && panel.surface.textCtl
                                                  && panel.surface.textCtl.ocrAvailable
                    visible: panel.surface && panel.surface.textCtl
                    opacity: ocrOn ? 1.0 : 0.45
                    width: ocrRibRow.implicitWidth + 16; height: 30; radius: 6
                    color: (ocrOn && ocrRibHover.hovered) ? App.themeCard : "transparent"
                    border.color: App.themeBorder; border.width: 1
                    Row {
                        id: ocrRibRow
                        anchors.centerIn: parent; spacing: 6
                        //  Symbol statt Glyphe (Regel 29).
                        ThemedIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            source: "qrc:/qml/icons/search.svg"; size: 14
                            color: ocrRibBtn.ocrOn ? App.themeTextPrimary : App.themeTextMuted
                        }
                        BusyIndicator {
                            width: 16; height: 16
                            anchors.verticalCenter: parent.verticalCenter
                            running: panel.surface && panel.surface.textCtl
                                     && panel.surface.textCtl.ocrBusy
                            visible: running
                        }
                        Text {
                            text: App.uiText(App.language, "PdfOcrBtn")
                            color: ocrRibBtn.ocrOn ? App.themeTextPrimary : App.themeTextMuted
                            font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    HoverHandler { id: ocrRibHover }
                    TapHandler {
                        enabled: ocrRibBtn.ocrOn
                        onTapped: if (panel.surface) panel.surface.requestOcr()
                    }
                    ToolTip.visible: ocrRibHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: ocrRibBtn.ocrOn
                                  ? App.uiText(App.language, "PdfOcrTip")
                                  : App.uiText(App.language, "LibMissingTesseract")
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
                        AlignBtn { iconSource: "qrc:/qml/icons/align-left.svg"; alignValue: 0 }
                        AlignBtn { iconSource: "qrc:/qml/icons/align-center.svg"; alignValue: 1 }
                        AlignBtn { iconSource: "qrc:/qml/icons/align-right.svg"; alignValue: 2 }
                    }
                }
                Column {
                    visible: panel.showText
                    spacing: 2
                    Row {
                        spacing: 4
                        VAlignBtn { iconSource: "qrc:/qml/icons/valign-top.svg"; vAlignValue: 0 }
                        VAlignBtn { iconSource: "qrc:/qml/icons/valign-middle.svg"; vAlignValue: 1 }
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
                    // Nur Post-its: die Deckfläche von „Text ersetzen" ist fix Weiß.
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
                // Deckfläche („Text ersetzen") — nur Farbe, immer deckend.
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

                // ── Reflow: Verketten/Lösen (textführende Auswahl) ───────────
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

                // ── Zeichnen: Linienfarbe / -breite / Füllung ────────────────
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
                    ToolTip.text: App.uiText(App.language,
                                      panel.selInfo.kind === 6 ? "PdfEditDeleteMarkup"
                                                               : "PdfEditDeleteBtn")
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
                        onTapped: if (panel.surface) panel.surface.startExport()
                    }
                    // Ribbon hat keinen Platz für den Ziel-Pfad → ToolTip
                    // (immer eine neue Kopie „…_bearbeitet(.n).pdf").
                    // Der zweite Knopf für den verlustfreien Weg ist entfallen;
                    // welcher Weg läuft, steht in den Einstellungen
                    // (`PdfEdit.exportLossless`, s. `PdfSurface.startExport`).
                    //  EINZEILIG: der volle Pfad in einer zweiten Zeile machte den
                    //  Hinweis doppelt so hoch wie jeden anderen in der Leiste.
                    //  Der DATEINAME genügt — der Ordner ist der der Quelldatei.
                    //  s. Ribbon-Fassung oben: erst beim Zeigen rechnen.
                    ToolTip.text: expHoverH.hovered ? panel._exportTip() : ""
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
