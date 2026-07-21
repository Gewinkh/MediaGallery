import QtQuick
import QtQuick.Controls.Basic
import MediaGallery 1.0

//  DocxSurface.qml — die DOCX-Editor-Kachel des Viewers (FullscreenViewer
//  type 5). Erfüllt den Viewer-Vertrag der übrigen Surfaces: source,
//  topInset/bottomInset, save(), release() — und funktioniert dezentral in
//  bis zu vier Split-View-Kacheln (eigener DocxEditController je Instanz).
//
//  Optik (2026-07-17): ALLE Toolbar-Controls sind explizit im App-Theme
//  gestylt (die Basic-Defaults folgen der Systempalette und wirkten als
//  dunkle Fremdkörper); die Leiste scrollt horizontal, wenn die Kachel
//  schmaler ist als die Controls (Split-View). Die Editorfläche zeigt eine
//  WEISSE Word-Seite (durchlaufende Papierbahn) auf dem Theme-Grund
//  (DocxTextArea.surroundColor = App.themeBackground).
Item {
    id: root

    property string source: ""
    property real topInset: 0
    property real bottomInset: 0

    onSourceChanged: editCtl.source = root.source

    //  Auto-Speichern beim Verlassen (Vertrag wie TextSurface.release()).
    function save()    { editCtl.save() }
    function release() { editCtl.release() }

    DocxEditController {
        id: editCtl
        translit: Translit
        onSaveFinished: (ok, target, error) => {
            if (!ok)
                statusText.flash(error.length > 0
                                 ? error
                                 : App.uiText(App.language, "DocxSaveError"))
            else if (!Docx.saveDirect)
                statusText.flash(App.uiText(App.language, "DocxExportedTo")
                                 .replace("%1", target.split("/").pop()))
        }
        onPdfExportFinished: (ok, target, error) => {
            if (ok)
                statusText.flash(App.uiText(App.language, "DocxPdfExportedTo")
                                 .replace("%1", target.split("/").pop()))
            else
                statusText.flash(error.length > 0
                                 ? error
                                 : App.uiText(App.language, "DocxPdfError"))
        }
    }

    Rectangle { anchors.fill: parent; color: App.themeBackground }

    // ── Toolbar ───────────────────────────────────────────────────────────────
    Rectangle {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: root.topInset
        height: 42
        color: App.themeToolbarBg
        z: 2
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: App.themeBorder }

        //  Format am Cursor (rev-getrieben, Muster PdfEditToolbar).
        property var fmt: ({})
        function refresh() { fmt = editCtl.currentFormat() }
        Connections {
            target: editCtl
            function onFormatRevChanged() { bar.refresh() }
            function onReadyChanged()     { bar.refresh() }
        }
        Component.onCompleted: refresh()

        // ── Gethemte Bausteine ────────────────────────────────────────────────
        component DBtn: Rectangle {
            property string glyph: ""
            property bool   active: false
            property bool   enabledBtn: true
            property bool   boldGlyph: false
            property bool   italicGlyph: false
            property bool   underlineGlyph: false
            property string tip: ""
            signal clicked()
            width: 28; height: 26; radius: 6
            anchors.verticalCenter: parent.verticalCenter
            color: active ? App.themeAccent
                          : (bh.hovered ? App.themeCard : "transparent")
            opacity: enabledBtn ? 1.0 : 0.35
            Text {
                anchors.centerIn: parent
                text: parent.glyph
                color: parent.active ? "#ffffff" : App.themeTextPrimary
                font.pixelSize: 14
                font.bold: parent.boldGlyph
                font.italic: parent.italicGlyph
                font.underline: parent.underlineGlyph
            }
            HoverHandler { id: bh }
            TapHandler { enabled: parent.enabledBtn; onTapped: parent.clicked() }
            ToolTip.visible: bh.hovered && tip.length > 0
            ToolTip.delay: 600
            ToolTip.text: tip
        }

        //  Ausrichtungs-Button: gezeichnete Linien statt Pfeil-Glyphen —
        //  garantiert sichtbar in jeder Schrift/jedem Theme.
        //  Ausrichtungs-Button: gezeichnete Linien-Icons (explizite x/y —
        //  keine Anker in Positionern) statt Pfeil-Glyphen, die je nach
        //  Schrift/Theme unsichtbar blass ausfielen.
        component DAlignBtn: Rectangle {
            id: alignBtn
            property int  alignValue: 0     // 0 l · 1 z · 2 r · 3 Blocksatz
            property bool active: false
            property string tip: ""
            width: 28; height: 26; radius: 6
            anchors.verticalCenter: parent.verticalCenter
            color: active ? App.themeAccent
                          : (ah.hovered ? App.themeCard : "transparent")
            Item {
                width: 16; height: 17
                anchors.centerIn: parent
                Repeater {
                    model: 4
                    delegate: Rectangle {
                        required property int index
                        width: (alignBtn.alignValue === 3 || index % 2 === 0) ? 16 : 10
                        height: 2; radius: 1
                        y: index * 5
                        x: alignBtn.alignValue === 2 ? 16 - width
                           : alignBtn.alignValue === 1 ? (16 - width) / 2 : 0
                        color: alignBtn.active ? "#ffffff" : App.themeTextPrimary
                    }
                }
            }
            HoverHandler { id: ah }
            TapHandler { onTapped: editCtl.setAlignment(alignBtn.alignValue) }
            ToolTip.visible: ah.hovered && tip.length > 0
            ToolTip.delay: 600
            ToolTip.text: tip
        }

        //  Kompakter Zahlen-Steller [−|Wert|+] im App-Theme (Basic-SpinBox
        //  folgt der Systempalette und passte weder farblich noch in der
        //  Breite). Wert direkt editierbar, Commit bei Enter/Fokusverlust.
        component DSpin: Rectangle {
            property int  value: 0
            property int  from: 0
            property int  to: 200
            property string tip: ""
            signal committed(int v)
            width: 86; height: 26; radius: 6
            anchors.verticalCenter: parent.verticalCenter
            color: App.themeCard
            border.color: App.themeBorder
            Rectangle {                                   // −
                width: 22; height: parent.height; radius: 6
                color: mh.hovered ? Qt.darker(App.themeCard, 1.15) : "transparent"
                Text { anchors.centerIn: parent; text: "\u2212"
                       color: App.themeTextPrimary; font.pixelSize: 14 }
                HoverHandler { id: mh }
                TapHandler { onTapped: parent.parent.committed(
                                 Math.max(parent.parent.from, parent.parent.value - 1)) }
            }
            TextInput {
                anchors.centerIn: parent
                width: parent.width - 48
                horizontalAlignment: TextInput.AlignHCenter
                color: App.themeTextPrimary
                font.pixelSize: 12
                selectByMouse: true
                validator: IntValidator { bottom: 0; top: 999 }
                text: String(parent.value)
                onEditingFinished: {
                    const v = Math.max(parent.from, Math.min(parent.to, parseInt(text) || 0))
                    if (v !== parent.value) parent.committed(v)
                    text = Qt.binding(() => String(parent.value))
                }
            }
            Rectangle {                                   // +
                anchors.right: parent.right
                width: 22; height: parent.height; radius: 6
                color: ph.hovered ? Qt.darker(App.themeCard, 1.15) : "transparent"
                Text { anchors.centerIn: parent; text: "+"
                       color: App.themeTextPrimary; font.pixelSize: 14 }
                HoverHandler { id: ph }
                TapHandler { onTapped: parent.parent.committed(
                                 Math.min(parent.parent.to, parent.parent.value + 1)) }
            }
            ToolTip.visible: (mh.hovered || ph.hovered) && tip.length > 0
            ToolTip.delay: 600
            ToolTip.text: tip
        }

        //  Gethemte ComboBox (Hintergrund/Text/Pfeil/Popup im App-Theme).
        component DCombo: ComboBox {
            id: cb
            property string tip: ""
            height: 26
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: 12
            background: Rectangle {
                radius: 6
                color: App.themeCard
                border.color: cb.pressed || cb.popup.visible ? App.themeAccent
                                                             : App.themeBorder
            }
            contentItem: Text {
                leftPadding: 8; rightPadding: 22
                verticalAlignment: Text.AlignVCenter
                text: cb.displayText
                color: App.themeTextPrimary
                font: cb.font
                elide: Text.ElideRight
            }
            indicator: Text {
                x: cb.width - 16
                anchors.verticalCenter: parent.verticalCenter
                text: "\u25BE"
                color: App.themeTextMuted
                font.pixelSize: 10
            }
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: cb.width
                height: 26
                contentItem: Text {
                    text: modelData
                    color: App.themeTextPrimary
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    color: parent.highlighted ? App.themeCard : App.themeMenuBarBg
                }
                highlighted: cb.highlightedIndex === index
            }
            popup: Popup {
                y: cb.height + 2
                width: cb.width
                padding: 1
                background: Rectangle {
                    color: App.themeMenuBarBg
                    border.color: App.themeBorder
                    radius: 6
                }
                contentItem: ListView {
                    id: popList
                    clip: true
                    implicitHeight: Math.min(contentHeight, 280)
                    model: cb.popup.visible ? cb.delegateModel : null
                    currentIndex: cb.highlightedIndex
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    //  Rad-Scrollen wie überall in der App (halbe Sichthöhe,
                    //  180 ms OutCubic) — die Vorgabe der Basic-ListView
                    //  kroch zeilenweise durch die Schriftliste.
                    NumberAnimation {
                        id: popScroll
                        target: popList
                        property: "contentY"
                        duration: 180
                        easing.type: Easing.OutCubic
                    }
                    WheelHandler {
                        onWheel: (w) => {
                            const maxY = Math.max(0, popList.contentHeight - popList.height)
                            if (maxY <= 0) return
                            const base = popScroll.running ? popScroll.to : popList.contentY
                            popScroll.to = Math.max(0, Math.min(maxY,
                                              base + (w.angleDelta.y > 0 ? -1 : 1)
                                                     * popList.height / 2))
                            popScroll.restart()
                        }
                    }
                }
            }
            ToolTip.visible: hovered && tip.length > 0
            ToolTip.delay: 600
            ToolTip.text: tip
        }

        // ── Linke Controls (scrollbar, wenn die Kachel schmal ist) ────────────
        Flickable {
            id: leftFlick
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: rightRow.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            contentWidth: leftRow.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            //  Umschalt + Mausrad scrollt die Leiste waagerecht (hoch = nach
            //  rechts, runter = nach links) — bei schmalem Fenster bleibt so
            //  ALLES erreichbar. Ohne Umschalt scrollt das Rad ebenfalls
            //  waagerecht (die Leiste kennt keine Senkrechte), animiert wie
            //  überall in der App (halbe Sichtbreite, 180 ms OutCubic).
            NumberAnimation {
                id: barScrollAnim
                target: leftFlick
                property: "contentX"
                duration: 180
                easing.type: Easing.OutCubic
            }
            WheelHandler {
                acceptedModifiers: Qt.NoModifier | Qt.ShiftModifier
                onWheel: (w) => {
                    const maxX = Math.max(0, leftFlick.contentWidth - leftFlick.width)
                    if (maxX <= 0) return
                    const d = (w.angleDelta.y !== 0 ? w.angleDelta.y : w.angleDelta.x)
                    const base = barScrollAnim.running ? barScrollAnim.to : leftFlick.contentX
                    barScrollAnim.to = Math.max(0, Math.min(maxX,
                                          base + (d > 0 ? 1 : -1) * leftFlick.width / 2))
                    barScrollAnim.restart()
                }
            }

            Row {
                id: leftRow
                height: leftFlick.height
                spacing: 6

                //  Speichern (Modus folgt der globalen Einstellung) + Busy.
                Rectangle {
                    width: saveLbl.implicitWidth + 20; height: 26; radius: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: saveHover.hovered ? Qt.darker(App.themeAccent, 1.1)
                                             : App.themeAccent
                    opacity: (editCtl.ready && !editCtl.busy
                              && (editCtl.modified || !Docx.saveDirect)) ? 1.0 : 0.45
                    Text {
                        id: saveLbl
                        anchors.centerIn: parent
                        text: Docx.saveDirect
                              ? App.uiText(App.language, "EditorSave")
                              : App.uiText(App.language, "DocxSaveCopy")
                        color: "#ffffff"
                        font.pixelSize: 12
                        font.bold: true
                    }
                    HoverHandler { id: saveHover }
                    TapHandler { onTapped: if (editCtl.ready && !editCtl.busy) editCtl.save() }
                }
                BusyIndicator {
                    width: 20; height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    running: editCtl.busy
                    visible: editCtl.busy
                }

                //  DOCX → PDF exportieren (Aufgabe 2): Original bleibt erhalten.
                Rectangle {
                    width: pdfLbl.implicitWidth + 18; height: 26; radius: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: pdfHover.hovered ? App.themeCard : "transparent"
                    border.color: App.themeBorder
                    opacity: (editCtl.ready && !editCtl.busy) ? 1.0 : 0.45
                    Text {
                        id: pdfLbl
                        anchors.centerIn: parent
                        text: App.uiText(App.language, "DocxExportPdf")
                        color: App.themeTextPrimary
                        font.pixelSize: 12
                    }
                    HoverHandler { id: pdfHover }
                    TapHandler {
                        onTapped: if (editCtl.ready && !editCtl.busy)
                                      editCtl.exportToPdf(
                                          App.uiText(App.language, "DocxTablePlaceholder"),
                                          App.uiText(App.language, "DocxPageBreak"))
                    }
                    ToolTip.visible: pdfHover.hovered
                    ToolTip.delay: 600
                    ToolTip.text: App.uiText(App.language, "DocxExportPdfTip")
                }

                Rectangle { width: 1; height: 20; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                DBtn { glyph: "\u21A9"; enabledBtn: editCtl.canUndo
                       tip: App.uiText(App.language, "ImageEditUndo")
                       onClicked: editCtl.undo() }
                DBtn { glyph: "\u21AA"; enabledBtn: editCtl.canRedo
                       tip: App.uiText(App.language, "ImageEditRedo")
                       onClicked: editCtl.redo() }

                Rectangle { width: 1; height: 20; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                //  Schriftfamilie + Größe (pt).
                DCombo {
                    id: fontCombo
                    width: 150
                    model: Qt.fontFamilies()
                    currentIndex: Math.max(0, model.indexOf(bar.fmt.font || ""))
                    onActivated: (i) => editCtl.setFontFamily(model[i])
                }
                DSpin {
                    value: Math.round(bar.fmt.sizePt || 11)
                    from: 6; to: 96
                    onCommitted: (v) => editCtl.setFontSizePt(v)
                }

                DBtn { glyph: "B"; boldGlyph: true
                       active: bar.fmt.bold === true;      onClicked: editCtl.toggleBold() }
                DBtn { glyph: "I"; italicGlyph: true
                       active: bar.fmt.italic === true;    onClicked: editCtl.toggleItalic() }
                DBtn { glyph: "U"; underlineGlyph: true
                       active: bar.fmt.underline === true; onClicked: editCtl.toggleUnderline() }

                //  Textfarbe („A" + Farbbalken) mit Swatch-Popup.
                Rectangle {
                    width: 28; height: 26; radius: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: colHover.hovered ? App.themeCard : "transparent"
                    Column {
                        anchors.centerIn: parent
                        spacing: 1
                        Text { text: "A"; anchors.horizontalCenter: parent.horizontalCenter
                               color: App.themeTextPrimary; font.pixelSize: 12; font.bold: true }
                        Rectangle { width: 14; height: 3; radius: 1
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    color: bar.fmt.color !== undefined ? bar.fmt.color : "#000000" }
                    }
                    HoverHandler { id: colHover }
                    TapHandler { onTapped: palette.open() }

                    Popup {
                        id: palette
                        y: parent.height + 4
                        padding: 8
                        background: Rectangle {
                            color: App.themeMenuBarBg
                            border.color: App.themeBorder
                            radius: 8
                        }
                        readonly property var swatches: [
                            "#000000", "#444444", "#888888", "#ffffff",
                            "#d32f2f", "#f57c00", "#fbc02d", "#388e3c",
                            "#00b4a0", "#1976d2", "#3f51b5", "#7b1fa2",
                            "#c2185b", "#6d4c41" ]
                        contentItem: Grid {
                            columns: 7
                            spacing: 6
                            Repeater {
                                model: palette.swatches
                                delegate: Rectangle {
                                    required property string modelData
                                    width: 22; height: 22; radius: 5
                                    color: modelData
                                    border.color: App.themeBorder
                                    TapHandler {
                                        onTapped: {
                                            editCtl.setTextColor(modelData)
                                            palette.close()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle { width: 1; height: 20; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                //  Ausrichtung (gezeichnete Linien-Icons).
                DAlignBtn { alignValue: 0; active: bar.fmt.align === 0 }
                DAlignBtn { alignValue: 1; active: bar.fmt.align === 1 }
                DAlignBtn { alignValue: 2; active: bar.fmt.align === 2 }
                DAlignBtn { alignValue: 3; active: bar.fmt.align === 3
                            tip: App.uiText(App.language, "DocxAlignJustify") }

                //  ── Abstände: EIN Knopf (Zeilenabstand + davor + danach) ──
                //  Symbol wie Katakana エ: zwei waagerechte Striche, im
                //  mittleren Strich ein Doppelpfeil ↑↓ (gezeichnet, damit es
                //  in jeder Schrift gleich aussieht). Linksklick → Popup mit
                //  allen drei Einstellungen.
                Rectangle {
                    id: spacingBtn
                    width: 28; height: 26; radius: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: spacingPop.visible ? App.themeAccent
                           : (spHover.hovered ? App.themeCard : "transparent")
                    Item {
                        anchors.centerIn: parent
                        width: 16; height: 16
                        readonly property color ink: spacingPop.visible
                                                     ? "#ffffff" : App.themeTextPrimary
                        Rectangle { y: 0;  width: 16; height: 2; radius: 1; color: parent.ink }
                        Rectangle { y: 14; width: 16; height: 2; radius: 1; color: parent.ink }
                        Text {
                            anchors.centerIn: parent
                            text: "\u2195"                       // ↕ im Mittelstrich
                            color: parent.ink
                            font.pixelSize: 13
                        }
                    }
                    HoverHandler { id: spHover }
                    TapHandler { onTapped: spacingPop.open() }
                    ToolTip.visible: spHover.hovered && !spacingPop.visible
                    ToolTip.delay: 600
                    ToolTip.text: App.uiText(App.language, "DocxSpacingGroup")

                    Popup {
                        id: spacingPop
                        y: parent.height + 4
                        padding: 10
                        background: Rectangle {
                            color: App.themeMenuBarBg
                            border.color: App.themeBorder
                            radius: 8
                        }
                        contentItem: Column {
                            spacing: 8
                            Text {
                                text: App.uiText(App.language, "DocxLineSpacing")
                                color: App.themeTextMuted; font.pixelSize: 11
                            }
                            Row {
                                spacing: 4
                                Repeater {
                                    model: [["1,0", 1.0], ["1,15", 1.15],
                                            ["1,5", 1.5], ["2,0", 2.0]]
                                    delegate: Rectangle {
                                        required property var modelData
                                        readonly property bool sel:
                                            Math.abs((bar.fmt.lineSpacing || 1.0)
                                                     - modelData[1]) < 0.03
                                        width: 44; height: 26; radius: 6
                                        color: sel ? App.themeAccent : App.themeCard
                                        border.color: App.themeBorder
                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData[0]
                                            color: parent.sel ? "#ffffff" : App.themeTextPrimary
                                            font.pixelSize: 12
                                        }
                                        TapHandler {
                                            onTapped: editCtl.setLineSpacing(modelData[1])
                                        }
                                    }
                                }
                            }
                            Row {
                                spacing: 8
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 116
                                    text: App.uiText(App.language, "DocxSpaceBefore")
                                    color: App.themeTextPrimary; font.pixelSize: 12
                                }
                                DSpin {
                                    value: Math.round(bar.fmt.beforePt || 0)
                                    onCommitted: (v) => editCtl.setSpaceBeforePt(v)
                                }
                            }
                            Row {
                                spacing: 8
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 116
                                    text: App.uiText(App.language, "DocxSpaceAfter")
                                    color: App.themeTextPrimary; font.pixelSize: 12
                                }
                                DSpin {
                                    value: Math.round(bar.fmt.afterPt || 0)
                                    onCommitted: (v) => editCtl.setSpaceAfterPt(v)
                                }
                            }
                        }
                    }
                }

                //  ── Listen: EIN Knopf mit Auswahl (keine/•/1.) ────────────
                Rectangle {
                    id: listBtn
                    width: 40; height: 26; radius: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: bar.fmt.list > 0 ? App.themeAccent
                           : (liHover.hovered ? App.themeCard : "transparent")
                    Row {
                        anchors.centerIn: parent
                        spacing: 3
                        Text {
                            text: bar.fmt.list === 2 ? "1." : "\u2022"
                            color: bar.fmt.list > 0 ? "#ffffff" : App.themeTextPrimary
                            font.pixelSize: 13
                        }
                        Text {
                            text: "\u25BE"
                            color: bar.fmt.list > 0 ? "#ffffff" : App.themeTextMuted
                            font.pixelSize: 9
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    HoverHandler { id: liHover }
                    TapHandler { onTapped: listPop.open() }
                    ToolTip.visible: liHover.hovered && !listPop.visible
                    ToolTip.delay: 600
                    ToolTip.text: App.uiText(App.language, "DocxListType")

                    Popup {
                        id: listPop
                        y: parent.height + 4
                        padding: 4
                        background: Rectangle {
                            color: App.themeMenuBarBg
                            border.color: App.themeBorder
                            radius: 8
                        }
                        contentItem: Column {
                            spacing: 2
                            Repeater {
                                //  [Beschriftung, Symbol, Listenwert]
                                model: [[App.uiText(App.language, "DocxListNone"),  "\u2014", 0],
                                        [App.uiText(App.language, "DocxBullets"),   "\u2022", 1],
                                        [App.uiText(App.language, "DocxNumbered"),  "1.",      2]]
                                delegate: Rectangle {
                                    required property var modelData
                                    readonly property bool sel: (bar.fmt.list || 0) === modelData[2]
                                    width: 168; height: 28; radius: 5
                                    color: sel ? App.themeAccent
                                           : (rowHover.hovered ? App.themeCard : "transparent")
                                    Row {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8
                                        Text {
                                            width: 14
                                            text: modelData[1]
                                            color: parent.parent.sel ? "#ffffff" : App.themeTextPrimary
                                            font.pixelSize: 13
                                        }
                                        Text {
                                            text: modelData[0]
                                            color: parent.parent.sel ? "#ffffff" : App.themeTextPrimary
                                            font.pixelSize: 12
                                        }
                                    }
                                    HoverHandler { id: rowHover }
                                    TapHandler {
                                        onTapped: {
                                            const want = modelData[2]
                                            const cur  = bar.fmt.list || 0
                                            //  toggle* schaltet um — nur aufrufen,
                                            //  wenn sich der Zustand ändern soll.
                                            if (want === cur) { listPop.close(); return }
                                            if (want === 1)      editCtl.toggleBullets()
                                            else if (want === 2) editCtl.toggleNumbering()
                                            else if (cur === 1)  editCtl.toggleBullets()
                                            else                 editCtl.toggleNumbering()
                                            listPop.close()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        //  Rechts: Status + Dateiname (• = ungespeichert) + Transliteration.
        Row {
            id: rightRow
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Text {
                id: statusText
                anchors.verticalCenter: parent.verticalCenter
                color: App.themeTextMuted
                font.pixelSize: 11
                opacity: 0
                function flash(msg) { text = msg; opacity = 1; statusFade.restart() }
                NumberAnimation on opacity {
                    id: statusFade; running: false
                    from: 1; to: 0; duration: 4000; easing.type: Easing.InQuad
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 220)
                elide: Text.ElideMiddle
                text: (editCtl.modified ? "\u2022 " : "")
                      + root.source.split("/").pop()
                color: App.themeTextMuted
                font.pixelSize: 11
            }
            TranslitButton { anchors.verticalCenter: parent.verticalCenter }
        }
    }

    // ── Editorfläche ──────────────────────────────────────────────────────────
    Item {
        id: viewport
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        //  Kein bottomInset-Rand: die Datei-Navigation (Pfeile) schwebt ÜBER
        //  dem Dokument — wie in der PDF-Anzeige; sonst entstünde unten ein
        //  reservierter Streifen (wirkte als weiße Leiste).
        anchors.bottom: parent.bottom
        clip: true

        DocxTextArea {
            id: area
            anchors.fill: parent
            ctl: editCtl
            surroundColor: App.themeBackground
            tablePlaceholder: App.uiText(App.language, "DocxTablePlaceholder")
            pageBreakLabel: App.uiText(App.language, "DocxPageBreak")
            onSaveRequested: editCtl.save()
            //  Cursor sichtbar halten (Inhalts- → Viewport-Koordinaten).
            onCursorRectChanged: {
                if (cursorH <= 0) return
                if (cursorY < contentY + 8)
                    scrollAnim.to = Math.max(0, cursorY - 40)
                else if (cursorY + cursorH > contentY + viewport.height - 8)
                    scrollAnim.to = cursorY + cursorH - viewport.height + 40
                else
                    return
                scrollAnim.restart()
            }
        }

        //  Lade-/Fehlerzustand.
        Text {
            anchors.centerIn: parent
            visible: !editCtl.ready
            width: parent.width - 80
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text: editCtl.loadError.length > 0
                  ? App.uiText(App.language, "DocxLoadError") + "\n" + editCtl.loadError
                  : ""
            color: App.themeTextMuted
            font.pixelSize: 13
        }
        BusyIndicator {
            anchors.centerIn: parent
            running: !editCtl.ready && editCtl.loadError.length === 0
                     && root.source.length > 0
        }

        //  Animiertes Mausrad (Muster TextSurface: halbe Viewporthöhe je
        //  Klick, 180 ms OutCubic) — schreibt area.contentY.
        NumberAnimation {
            id: scrollAnim
            target: area
            property: "contentY"
            duration: 180
            easing.type: Easing.OutCubic
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel: (w) => {
                const step = viewport.height / 2
                const dir = w.angleDelta.y > 0 ? -1 : 1
                const base = scrollAnim.running ? scrollAnim.to : area.contentY
                scrollAnim.to = Math.max(0, Math.min(base + dir * step,
                                     Math.max(0, area.contentHeight - viewport.height)))
                scrollAnim.restart()
            }
        }

        //  Scrollbar.
        ScrollBar {
            id: vbar
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            orientation: Qt.Vertical
            policy: size < 1.0 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            size: area.contentHeight > 0
                  ? Math.min(1.0, viewport.height / area.contentHeight) : 1.0
            position: area.contentHeight > 0 ? area.contentY / area.contentHeight : 0
            onPositionChanged: {
                if (pressed)
                    area.contentY = position * area.contentHeight
            }
        }
    }
}
