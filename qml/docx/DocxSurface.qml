import QtQuick
import QtQuick.Controls.Basic
import MediaGallery 1.0
import "../common"
import "../pdf"

//  DocxSurface.qml - die DOCX-Editor-Kachel des Viewers (FullscreenViewer
//  type 5). Erfüllt den Viewer-Vertrag der übrigen Surfaces: source,
//  topInset/bottomInset, save(), release() - und funktioniert dezentral in
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
    //  Nur die aktive Split-View-Kachel darf Ctrl+F auslösen (sonst mehrdeutig
    //  bei mehreren offenen DOCX). Einzel-View = immer true.
    property bool paneActive: true

    //  Suchen&Ersetzen-Leiste (Ctrl+F). Sichtbar-Zustand + Kurzstatus.
    property bool findVisible: false

    onSourceChanged: editCtl.source = root.source

    //  Kleiner Textknopf für den Änderungs-Streifen (die Toolbar-`DBtn` ist ein
    //  Glyphen-Quadrat und liegt in einem anderen Bereich).
    component RevBtn: Rectangle {
        property string label: ""
        signal clicked()
        width: rbText.implicitWidth + 16
        height: 22
        radius: 5
        anchors.verticalCenter: parent.verticalCenter
        color: rbHover.hovered ? App.themeAccent : "transparent"
        border.color: App.themeBorder
        Text {
            id: rbText
            anchors.centerIn: parent
            text: parent.label
            color: rbHover.hovered ? "#ffffff" : App.themeTextPrimary
            font.pixelSize: 11
        }
        HoverHandler { id: rbHover }
        TapHandler { onTapped: parent.clicked() }
    }

    function openFind() {
        root.findVisible = true
        findField.field.forceActiveFocus()
        findField.field.selectAll()
    }
    function closeFind() {
        root.findVisible = false
        area.forceActiveFocus()
    }

    //  Ctrl+F öffnet die Leiste; nur in der aktiven Kachel (Split-View).
    Shortcut {
        sequence: "Ctrl+F"
        enabled: root.paneActive && editCtl.ready
        onActivated: root.openFind()
    }

    //  Auto-Speichern beim Verlassen (Vertrag wie TextSurface.release()).
    function save()    { editCtl.save() }
    function release() { editCtl.release() }

    //  Rechtschreibprüfung folgt der globalen Einstellung - der Controller
    //  gehört zur KACHEL und kennt die Einstellungen bewusst nicht, deshalb
    //  reicht die Kachel sie herein (beim Start und bei jeder Änderung).
    //  `Connections` gehört an ein ITEM: ein QObject-Typ wie der Controller hat
    //  kein Default-Property und nimmt keine Kinder auf.
    function _applySpellSettings() {
        editCtl.setSpellLanguage(App.spellLanguage)
        editCtl.setSpellCheckEnabled(App.spellCheck)
    }
    Component.onCompleted: root._applySpellSettings()
    Connections {
        target: App
        function onSpellCheckChanged() { root._applySpellSettings() }
    }

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
        onImageInsertFailed: (error) => {
            statusText.flash(error.length > 0
                             ? error
                             : App.uiText(App.language, "DocxImageError"))
        }
    }

    //  ── PDF-Seiten als Bild einfügen ─────────────────────────────────────
    //  Wird im Bild-Popup eine PDF gewählt, kommt NICHT stillschweigend deren
    //  Cover ins Dokument: es öffnet DERSELBE große Auswahlbildschirm wie die
    //  Extraktion - links alle PDFs des Ordners, rechts das Seitenraster der
    //  angeklickten, unten die Werkbank-Leiste für die Reihenfolge.
    //  Die Komponente ist unverändert wiederverwendet; sie kennt den Modus
    //  „ohne Namensabfrage" schon vom PDF-Editor („Seiten einfügen").
    //
    //  Der Ordner-Scan läuft über das Singleton `PdfExtract` (async, ermittelt
    //  je PDF die Seitenzahl). Weil ALLE Flächen und die Shell dasselbe
    //  Singleton hören, markiert `_pdfScanPending`, dass der laufende Scan von
    //  HIER stammt - Muster wie `_extractPending` in PdfSurface/ApplicationShell.
    property bool _pdfScanPending: false
    property string _pdfScanPick: ""      // angeklickte PDF (Basisname)

    function _baseName(s) {
        const t = decodeURIComponent(String(s))
        const i = Math.max(t.lastIndexOf("/"), t.lastIndexOf("\\"))
        return i >= 0 ? t.substring(i + 1) : t
    }

    function openPdfPagePicker(fileUrl) {
        const n = editCtl.pdfPageCount(fileUrl)
        if (n <= 0) {
            statusText.flash(App.uiText(App.language, "DocxPdfPageError"))
            return
        }
        const dir = editCtl.folderPath()
        if (dir.length > 0) {
            //  Ergebnis öffnet den Dialog in onFolderPdfsReady; scheitert der
            //  Scan, greift dort der Rückfall auf die eine angeklickte Datei.
            root._pdfScanPick    = root._baseName(fileUrl)
            root._pdfScanPending = true
            PdfExtract.scanFolder(dir)
            return
        }
        pdfPagesDlg.openWith([ { path: fileUrl, pageCount: n } ], 0)
    }

    Connections {
        target: PdfExtract
        function onFolderPdfsReady(files) {
            if (!root._pdfScanPending) return    // Scan eines anderen Aufrufers
            root._pdfScanPending = false
            if (files.length === 0) {
                statusText.flash(App.uiText(App.language, "DocxPdfPageError"))
                return
            }
            let idx = 0
            for (let i = 0; i < files.length; ++i)
                if (root._baseName(files[i].path) === root._pdfScanPick) { idx = i; break }
            pdfPagesDlg.openWith(files, idx)
        }
    }

    PdfPageSelectDialog {
        id: pdfPagesDlg
        anchors.fill: parent
        z: 20
        requireName: false
        askName: false
        titleText: App.uiText(App.language, "DocxInsertPdfPage")
        confirmText: App.uiText(App.language, "DocxInsertPdfPageBtn")
        //  Reihenfolge = Auswahlreihenfolge; jede Seite wird ein eigener
        //  Bild-Absatz (und damit ein eigener Undo-Schritt).
        onExtractRequested: (items, name) => {
            for (let i = 0; i < items.length; ++i)
                editCtl.insertPdfPage(items[i].path, items[i].page)
            area.forceActiveFocus()
        }
    }

    //  Dateiauswahl fürs Bild-Einfügen - eigener gethemter Wähler (qml/common).
    FileChooser {
        id: imgDialog
        fileMode: FileChooser.OpenFile
        title: App.uiText(App.language, "DocxInsertImage")
        nameFilters: [ App.uiText(App.language, "DocxImageFilter") ]
        onAccepted: editCtl.insertImage(selectedFile)
    }

    //  Dateidialog des Unterschrift-Knopfes (eigener, damit „Durchsuchen …"
    //  aus beiden Popups das Richtige einfügt).
    FileChooser {
        id: signDialog
        fileMode: FileChooser.OpenFile
        title: App.uiText(App.language, "DocxInsertSignature")
        nameFilters: [ App.uiText(App.language, "DocxImageFilter") ]
        onAccepted: editCtl.insertSignatureImage(selectedFile)
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

        //  Absatzvorlagen des Dokuments - hängen am DOKUMENT, nicht am Cursor:
        //  einmal beim Laden holen, NICHT in refresh() (das läuft bei jedem
        //  Formatwechsel, also praktisch bei jedem Tastendruck).
        property var styleList: []          // [{ id, name, isDefault }]
        property var styleNames: []         // dieselben Namen für das Modell
        function reloadStyles() {
            const l = editCtl.ready ? editCtl.paragraphStyles() : []
            const n = []
            for (let i = 0; i < l.length; ++i) {
                //  Überschriften heißen im OOXML immer „heading N" (nur an
                //  diesem eingebauten Namen erkennt Word sie als Überschrift) -
                //  angezeigt wird die übersetzte Fassung.
                const m = /^Heading([1-9])$/.exec(l[i].id)
                n.push(m ? App.uiText(App.language, "DocxHeadingLevel").arg(m[1])
                         : l[i].name)
            }
            bar.styleList  = l
            bar.styleNames = n
        }
        //  Index der Vorlage am Cursor; ohne w:pStyle gilt die Standardvorlage.
        function styleIndex() {
            const sid = bar.fmt.styleId || ""
            for (let i = 0; i < bar.styleList.length; ++i)
                if (bar.styleList[i].id === sid) return i
            for (let j = 0; j < bar.styleList.length; ++j)
                if (bar.styleList[j].isDefault) return j
            return -1
        }

        Connections {
            target: editCtl
            function onFormatRevChanged() { bar.refresh() }
            function onReadyChanged()     { bar.refresh(); bar.reloadStyles() }
        }
        Component.onCompleted: { refresh(); reloadStyles() }

        // ── Gethemte Bausteine ────────────────────────────────────────────────
        component DBtn: Rectangle {
            property string glyph: ""
        property string iconName: ""
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
                   visible: parent.iconName.length === 0 }
            DrawnIcon { anchors.centerIn: parent; name: parent.iconName; size: 16
                         visible: parent.iconName.length > 0 }
            HoverHandler { id: bh }
            TapHandler { enabled: parent.enabledBtn; onTapped: parent.clicked() }
            ToolTip.visible: bh.hovered && tip.length > 0
            ToolTip.delay: 600
            ToolTip.text: tip
        }

        //  Ausrichtungs-Button: gezeichnete Linien statt Pfeil-Glyphen -
        //  garantiert sichtbar in jeder Schrift/jedem Theme.
        //  Ausrichtungs-Button: gezeichnete Linien-Icons (explizite x/y -
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
                    //  180 ms OutCubic) - die Vorgabe der Basic-ListView
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

            //  Mausrad scrollt die Leiste waagerecht (hoch = nach rechts,
            //  runter = nach links) - bei schmalem Fenster bleibt so ALLES
            //  erreichbar. Gilt OHNE Modifikator, mit Umschalt UND mit STRG:
            //  Strg+Rad ist der Griff, den der PDF-Editor für sein Ribbon
            //  schon anbietet (s. PdfEditPanel), und wer ihn dort gelernt
            //  hat, erwartet ihn hier genauso. Animiert wie überall in der
            //  App (halbe Sichtbreite, 180 ms OutCubic).
            //  Mausrad scrollt die Leiste waagerecht (hoch = nach rechts) -
            //  bei schmalem Fenster bleibt so ALLES erreichbar.
            //
            //  ES MUSS `SmoothWheelArea` SEIN, kein `WheelHandler`: dieses
            //  Flickable ist interaktiv und verarbeitet Radereignisse SELBST,
            //  bevor ein Handler darunter sie sieht - die Leiste scrollte
            //  deshalb in Qts Vorgabeschritten von ~60 px statt in halben
            //  Sichtbreiten (Nutzerbefund „scrollt langsam"). Genau dieselbe
            //  Falle ist in `PdfEditPanel` schon dokumentiert und dort mit
            //  derselben Komponente gelöst (Structure.md ▸ Workarounds).
            //
            //  `requiredModifier: Qt.NoModifier` heißt hier „jeder": die
            //  Komponente reicht nur dann durch, wenn ein Modifikator
            //  VERLANGT und nicht gedrückt ist. Rad, Umschalt+Rad, Strg+Rad
            //  und Alt+Rad schwenken die Leiste also alle.
            SmoothWheelArea {
                flickable: leftFlick
                horizontal: true
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

                //  DOCX -> PDF exportieren (Aufgabe 2): Original bleibt erhalten.
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
                        //  Gemalt wird aus DER Auslegung, die hier auf dem Schirm
                        //  steht (`area.exportPagesToPdf`) - dadurch ist das PDF
                        //  seitengleich. Der frühere Weg baute ein zweites Layout
                        //  im Worker und lief auseinander (gemessen: 4 Seiten am
                        //  Schirm gegen 3 im PDF). Das Zeichnen selbst ist kurz,
                        //  die Auslegung liegt ja schon vor.
                        onTapped: {
                            if (!editCtl.ready || editCtl.busy)
                                return
                            const tgt = editCtl.pdfExportTargetPath()
                            const e = area.exportPagesToPdf(tgt)
                            if (e.length === 0)
                                statusText.flash(App.uiText(App.language, "DocxPdfExportedTo")
                                                 .replace("%1", tgt.split("/").pop()))
                            else
                                statusText.flash(e)
                        }
                    }
                    ToolTip.visible: pdfHover.hovered
                    ToolTip.delay: 600
                    ToolTip.text: App.uiText(App.language, "DocxExportPdfTip")
                }

                Rectangle { width: 1; height: 20; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                //  Schlüssel bewusst die des PDF-Editors: der Text ist
                //  „Rückgängig"/„Wiederholen" und damit editorneutral. Die
                //  früheren Namen ImageEditUndo/-Redo gab es im String-Katalog
                //  GAR NICHT - App.uiText gibt bei unbekanntem Namen den Namen
                //  zurück, im Tooltip stand also wörtlich „ImageEditUndo".
                DBtn { iconName: "undo"; enabledBtn: editCtl.canUndo
                       tip: App.uiText(App.language, "PdfEditUndoTip")
                       onClicked: editCtl.undo() }
                DBtn { iconName: "redo"; enabledBtn: editCtl.canRedo
                       tip: App.uiText(App.language, "PdfEditRedoTip")
                       onClicked: editCtl.redo() }

                Rectangle { width: 1; height: 20; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                //  Formatvorlage des Absatzes (Standardvorlage = erster
                //  Eintrag; Anwenden entfernt dort das w:pStyle wieder).
                //  Ohne styles.xml im Dokument bleibt die Liste leer und die
                //  Auswahl verschwindet, statt leer dazustehen.
                DCombo {
                    id: styleCombo
                    width: 150
                    visible: bar.styleNames.length > 0
                    model: bar.styleNames
                    currentIndex: bar.styleIndex()
                    tip: App.uiText(App.language, "DocxParagraphStyle")
                    onActivated: (i) => {
                        if (i >= 0 && i < bar.styleList.length)
                            editCtl.setParagraphStyle(bar.styleList[i].id)
                    }
                }

                Rectangle { width: 1; height: 20; color: App.themeBorder
                            visible: styleCombo.visible
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
                //  in jeder Schrift gleich aussieht). Linksklick -> Popup mit
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
                                            //  toggle* schaltet um - nur aufrufen,
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

                //  EIN Trennstrich, nicht zwei: der zweite gehörte zur entfernten
                //  Kopf-/Fußzeilen-Gruppe und stand seitdem doppelt da.
                Rectangle { width: 1; height: 20; color: App.themeBorder
                            anchors.verticalCenter: parent.verticalCenter }

                //  Bild einfügen: Dateidialog -> eigener Absatz an der Cursorstelle.
                DBtn {
                    iconName: "image"          // ❏ - BMP, monochrom wie die übrigen
                    enabledBtn: editCtl.ready
                    tip: App.uiText(App.language, "DocxInsertImage")
                    //  Erst die Bilder im ORDNER der Datei anbieten (der häufige
                    //  Fall, ganz ohne Dateidialog), sonst der Dateidialog.
                    //  Strg+V fügt zusätzlich Bilder aus der Zwischenablage ein.
                    onClicked: {
                        imgPopup.entries = editCtl.folderImages()
                        imgPopup.open()
                    }

                    //  Ordner-Bildwähler (gemeinsam mit dem PDF-Editor).
                    FolderImagePicker {
                        id: imgPopup
                        hostWidth: root.width
                        onPicked: function(u) {
                            //  PDF: NICHT stillschweigend das Cover nehmen,
                            //  sondern dieselbe Seitenauswahl anbieten wie die
                            //  Extraktion (Nutzerbefund).
                            if (u.toLowerCase().endsWith(".pdf"))
                                root.openPdfPagePicker(u)
                            else
                                editCtl.insertImage(u)
                            area.forceActiveFocus()
                        }
                        onBrowseRequested: imgDialog.open()
                    }
                }

                //  Inhaltsverzeichnis einfügen: das Feld bleibt deklarativ,
                //  die Seitenzahlen kommen aus unserer eigenen Paginierung.
                DBtn {
                    iconName: "toc"          // ≡
                    enabledBtn: editCtl.ready
                    tip: App.uiText(App.language, "DocxInsertToc")
                    onClicked: {
                        editCtl.insertTableOfContents()
                        area.forceActiveFocus()
                    }
                }

                //  Unterschrift/Stempel: dasselbe Ordner-Popup wie beim Bild,
                //  aber das Bild wird SOFORT verankert eingesetzt (frei auf der
                //  Seite verschiebbar) und ausgewählt - genau wie im PDF-Editor.
                DBtn {
                    iconName: "signature"          // ✍
                    enabledBtn: editCtl.ready
                    tip: App.uiText(App.language, "DocxInsertSignature")
                    onClicked: {
                        signPopup.entries = editCtl.folderImages()
                        signPopup.open()
                    }
                    FolderImagePicker {
                        id: signPopup
                        hostWidth: root.width
                        onPicked: function(u) {
                            //  Eine PDF ist keine Unterschrift - dafür gibt es
                            //  den Bild-Knopf mit der Seitenauswahl.
                            if (!u.toLowerCase().endsWith(".pdf"))
                                editCtl.insertSignatureImage(u)
                            area.forceActiveFocus()
                        }
                        onBrowseRequested: signDialog.open()
                    }
                }

                //  Tabelle einfügen: kleiner Knopf mit Zeilen/Spalten-Popup.
                //  Eingefügt wird HINTER dem Cursor-Absatz; steht der Cursor in
                //  einer Zelle, hinter der ganzen Tabelle (keine Verschachtelung).
                DBtn {
                    id: tblBtn
                    iconName: "table"
                    enabledBtn: editCtl.ready
                    tip: App.uiText(App.language, "DocxInsertTable")
                    onClicked: tblPopup.open()

                    Popup {
                        id: tblPopup
                        y: parent.height + 4
                        padding: 10
                        background: Rectangle {
                            color: App.themeMenuBarBg
                            border.color: App.themeBorder
                            radius: 8
                        }
                        contentItem: Column {
                            spacing: 8
                            Row {
                                spacing: 6
                                Text { text: App.uiText(App.language, "DocxTableRows")
                                       color: App.themeTextPrimary; font.pixelSize: 12
                                       anchors.verticalCenter: parent.verticalCenter }
                                //  `DSpin` MELDET nur (`committed`) - den Wert setzt
                                //  der Aufrufer. Die anderen Steller schicken ihn an
                                //  den Controller und bekommen ihn über die Bindung
                                //  zurück; hier gibt es keinen Controller, also hält
                                //  der Dialog den Wert selbst. Ohne diese Zeile taten
                                //  „+"/„−" gar nichts (Nutzerbefund 2026-08-12).
                                DSpin { id: rowSpin; value: 3; from: 1; to: 100
                                        onCommitted: (v) => value = v }
                            }
                            Row {
                                spacing: 6
                                Text { text: App.uiText(App.language, "DocxTableCols")
                                       color: App.themeTextPrimary; font.pixelSize: 12
                                       anchors.verticalCenter: parent.verticalCenter }
                                DSpin { id: colSpin; value: 3; from: 1; to: 32
                                        onCommitted: (v) => value = v }
                            }
                            Rectangle {
                                width: 120; height: 26; radius: 6
                                color: insHover.hovered ? Qt.darker(App.themeAccent, 1.1)
                                                        : App.themeAccent
                                Text {
                                    anchors.centerIn: parent
                                    text: App.uiText(App.language, "DocxInsert")
                                    color: "#ffffff"; font.pixelSize: 12; font.bold: true
                                }
                                HoverHandler { id: insHover }
                                TapHandler {
                                    onTapped: {
                                        editCtl.insertTable(rowSpin.value, colSpin.value)
                                        tblPopup.close()
                                        area.forceActiveFocus()
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
        //  dem Dokument - wie in der PDF-Anzeige; sonst entstünde unten ein
        //  reservierter Streifen (wirkte als weiße Leiste).
        anchors.bottom: parent.bottom
        clip: true

        //  ── Seiten-Miniaturen ──────────────────────────────────────────────
        //  Links angedockte Leiste, nur bei mehr als einer Seite und nur, wenn
        //  die Kachel breit genug ist (in einer schmalen Split-Kachel hätte das
        //  Dokument selbst keinen Platz mehr). Die Delegates malen über
        //  DocxTextArea::paintPageInto - es gibt keinen Bild-Cache.
        Rectangle {
            id: thumbBar
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 132
            visible: editCtl.ready && area.pageCount > 1 && viewport.width > 520
            color: Qt.darker(App.themeBackground, 1.15)
            z: 1

            Rectangle { anchors.right: parent.right; width: 1; height: parent.height
                        color: App.themeBorder }

            ListView {
                id: thumbList
                anchors.fill: parent
                anchors.margins: 8
                spacing: 10
                clip: true
                model: area.pageCount
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Column {
                    id: thumbItem
                    required property int index
                    readonly property bool isCurrent: thumbItem.index === area.currentPage
                    //  MUSS über diesen Umweg gehen: `area: area` würde INNERHALB
                    //  von DocxPageThumb auf dessen EIGENE `area`-Property
                    //  auflösen (Selbst-Beschattung) und bliebe null - die
                    //  Miniaturen blieben leer. Hier im Column-Scope meint
                    //  `area` eindeutig die Textfläche.
                    readonly property var docArea: area
                    width: thumbList.width - 12
                    spacing: 3

                    Rectangle {
                        width: parent.width
                        //  A4-Verhältnis als Rahmenmaß; die Miniatur selbst folgt
                        //  der echten Seitengeometrie (paintPageInto rechnet mit
                        //  dem Seitenmaß des Dokuments und passt sich ein).
                        height: Math.round(parent.width * 1.414)
                        color: "transparent"
                        border.color: thumbItem.isCurrent ? App.themeAccent
                                                          : App.themeBorder
                        border.width: thumbItem.isCurrent ? 2 : 1

                        DocxPageThumb {
                            anchors.fill: parent
                            anchors.margins: 2
                            area: thumbItem.docArea
                            page: thumbItem.index
                        }
                        TapHandler {
                            onTapped: {
                                scrollAnim.to = area.pageTop(thumbItem.index)
                                scrollAnim.restart()
                            }
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: thumbItem.index + 1
                        color: thumbItem.isCurrent ? App.themeAccent
                                                   : App.themeTextMuted
                        font.pixelSize: 11
                    }
                }
            }
        }

        DocxTextArea {
            id: area
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: thumbBar.visible ? thumbBar.right : parent.left
            anchors.right: parent.right
            ctl: editCtl
            surroundColor: App.themeBackground
            tablePlaceholder: App.uiText(App.language, "DocxTablePlaceholder")
            pageBreakLabel: App.uiText(App.language, "DocxPageBreak")
            tocEmptyLabel: App.uiText(App.language, "DocxTocEmpty")
            onSaveRequested: editCtl.save()
            //  Rechtsklick: Menü nur dort anbieten, wo es etwas zu tun gibt
            //  (Tabelle oder Bild) - sonst bleibt der Klick wie bisher folgenlos.
            onContextMenuRequested: (mx, my, block) => ctxMenu.openFor(mx, my, block)
            //  Cursor sichtbar halten (Inhalts- -> Viewport-Koordinaten).
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

        //  ── Änderungsverfolgung: Streifen mit Zahl, Autoren und Erklärung ──
        //  Ohne ihn sah ein Dokument MIT nachverfolgten Änderungen aus wie eines
        //  ohne: die Markierungen waren da, aber nichts sagte, was sie bedeuten
        //  oder was man damit tun kann (Nutzerbefund). Aufgezeichnet wird
        //  weiterhin NICHT - genau das steht im Hinweis.
        Rectangle {
            id: revBar
            visible: editCtl.ready && editCtl.revisionCount > 0
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 8
            z: 6
            radius: 6
            color: App.themeCard
            border.color: App.themeBorder
            width: Math.min(viewport.width - 24, revRow.implicitWidth + 20)
            height: 30

            Row {
                id: revRow
                anchors.centerIn: parent
                spacing: 10

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.uiText(App.language, "DocxRevisionsBanner")
                              .arg(editCtl.revisionCount)
                              .arg(editCtl.revisionAuthorsText.length > 0
                                   ? editCtl.revisionAuthorsText : "–")
                    color: App.themeTextPrimary
                    font.pixelSize: 11
                    //  Die Erklärung steht im Tooltip - der Streifen soll die
                    //  Seite nicht zuwachsen.
                    ToolTip.text: App.uiText(App.language, "DocxRevisionsHint")
                    ToolTip.visible: revHint.hovered
                    HoverHandler { id: revHint }
                }
                RevBtn {
                    label: App.uiText(App.language, "DocxRevAcceptAll")
                    onClicked: { editCtl.acceptAllRevisions(); area.forceActiveFocus() }
                }
                RevBtn {
                    label: App.uiText(App.language, "DocxRevRejectAll")
                    onClicked: { editCtl.rejectAllRevisions(); area.forceActiveFocus() }
                }
            }
        }

        //  ── Ziehpunkte des ausgewählten Bildes ─────────────────────────────
        //  Auswahl = der Cursor steht in einem reinen Bild-Absatz (area.
        //  selImageBlock). Die Fläche liefert das Rechteck bereits in
        //  ITEM-Pixeln; gezogen wird nur eine VORSCHAU - erst beim Loslassen
        //  geht EIN setImageSizeMm an den Controller. Sonst entstünde je
        //  Mausbewegung ein Undo-Schritt und eine neue Zeichnung im Anhang-Pool.
        Item {
            id: imgBox
            visible: area.selImageBlock >= 0 && editCtl.ready
            z: 4
            x: area.x + area.selImageX + (moving ? previewDx : 0)
            y: area.y + area.selImageY + (moving ? previewDy : 0)
            width:  dragging ? previewW : Math.max(1, area.selImageW)
            height: dragging ? previewH : Math.max(1, area.selImageH)

            property bool dragging: false
            property real previewW: 0
            property real previewH: 0
            //  Verschieben (nur umfließende Bilder - ein Zeilen-Bild hat keine
            //  Lage, es steht dort, wo sein Zeichen im Text steht).
            property bool moving: false
            property real previewDx: 0
            property real previewDy: 0
            //  Auskunft zum ausgewählten Bild; hängt an denselben Bindungen wie
            //  das Rechteck, wird also mit jeder Auswahl/Änderung neu geholt.
            readonly property var selInfo: (area.selImageBlock >= 0 && editCtl.ready
                                            && area.selImageW >= 0 && editCtl.formatRev >= 0)
                                           ? editCtl.imageInfoAt(area.selImageBlock)
                                           : ({ image: false })
            readonly property bool floatingSel: selInfo.image === true
                                                && selInfo.floating === true
            //  Umrechnung Item-Pixel ↔ Millimeter: das Modell führt die Größe
            //  in mm, die Anzeige in Pixeln - der Faktor ergibt sich aus dem
            //  aktuellen Rechteck (er trägt Zoom und Einpass-Maßstab bereits).
            readonly property real mmPerPx: (selInfo.image && area.selImageW > 1)
                                            ? selInfo.widthMm / area.selImageW : 0.26458

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: App.themeAccent
                border.width: (imgBox.dragging || imgBox.moving) ? 2 : 1
            }

            //  Ziehen im Bild selbst VERSCHIEBT es - aber nur, wenn es
            //  umfließend ist; sonst müsste die Fläche Klicks schlucken, die
            //  in Wahrheit den Cursor setzen sollen. Wie beim Ändern der Größe
            //  wandert nur eine VORSCHAU, erst das Loslassen schreibt EINEN
            //  Undo-Schritt.
            MouseArea {
                anchors.fill: parent
                z: -1
                enabled: imgBox.floatingSel && !imgBox.dragging
                visible: enabled
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                cursorShape: Qt.SizeAllCursor
                property real pressX: 0
                property real pressY: 0
                onPressed: (m) => {
                    const p = mapToItem(viewport, m.x, m.y)
                    pressX = p.x; pressY = p.y
                    imgBox.previewDx = 0
                    imgBox.previewDy = 0
                    imgBox.moving = true
                }
                onPositionChanged: (m) => {
                    if (!imgBox.moving) return
                    const p = mapToItem(viewport, m.x, m.y)
                    imgBox.previewDx = p.x - pressX
                    imgBox.previewDy = p.y - pressY
                }
                onReleased: {
                    if (!imgBox.moving) return
                    imgBox.moving = false
                    if (Math.abs(imgBox.previewDx) < 2 && Math.abs(imgBox.previewDy) < 2)
                        return                       // Klick, keine Geste
                    const info = imgBox.selInfo
                    if (!info.image) return
                    //  Über die ANZEIGE ablegen, nicht direkt über den
                    //  Controller: liegt das Bild nach der Geste über einem
                    //  anderen Absatz, hängt sie den Anker dorthin um (wie
                    //  Word) - erst dann umfließt dessen Text es. Ohne
                    //  Absatzwechsel ist es dasselbe wie bisher.
                    area.dropSelectedImage(
                        area.selImageBlock,
                        info.xMm + imgBox.previewDx * imgBox.mmPerPx,
                        info.yMm + imgBox.previewDy * imgBox.mmPerPx)
                }
                onCanceled: imgBox.moving = false
            }

            //  4 Ecken + 4 Kantenmitten - gleiche Interaktion wie im Bild-Editor
            //  (ImageEditBox): „nach außen ziehen vergrößert". Das Bild bleibt
            //  am linken Textrand verankert (Inline-Absatz), es wandert also nie.
            Repeater {
                model: [ { hx: -1, hy: -1 }, { hx: 0, hy: -1 }, { hx: 1, hy: -1 },
                         { hx: -1, hy:  0 },                    { hx: 1, hy:  0 },
                         { hx: -1, hy:  1 }, { hx: 0, hy:  1 }, { hx: 1, hy:  1 } ]
                delegate: Rectangle {
                    id: handle
                    required property var modelData
                    readonly property int hx: modelData.hx
                    readonly property int hy: modelData.hy
                    width: 9; height: 9; radius: 2
                    color: App.themeAccent
                    border.color: "#ffffff"; border.width: 1
                    x: (hx < 0 ? 0 : hx > 0 ? imgBox.width  : imgBox.width  / 2) - width / 2
                    y: (hy < 0 ? 0 : hy > 0 ? imgBox.height : imgBox.height / 2) - height / 2

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        acceptedButtons: Qt.LeftButton
                        preventStealing: true
                        cursorShape: {
                            if (handle.hx !== 0 && handle.hy !== 0)
                                return (handle.hx === handle.hy) ? Qt.SizeFDiagCursor
                                                                 : Qt.SizeBDiagCursor
                            return handle.hx !== 0 ? Qt.SizeHorCursor : Qt.SizeVerCursor
                        }
                        property real pressX: 0
                        property real pressY: 0
                        property real startW: 0
                        property real startH: 0
                        onPressed: (m) => {
                            const p = mapToItem(viewport, m.x, m.y)
                            pressX = p.x; pressY = p.y
                            startW = Math.max(1, area.selImageW)
                            startH = Math.max(1, area.selImageH)
                            imgBox.previewW = startW
                            imgBox.previewH = startH
                            imgBox.dragging = true
                        }
                        onPositionChanged: (m) => {
                            if (!imgBox.dragging) return
                            const p = mapToItem(viewport, m.x, m.y)
                            //  Nach außen ziehen vergrößert - unabhängig davon,
                            //  an welchem der acht Punkte gezogen wird.
                            const dx = (p.x - pressX) * (handle.hx < 0 ? -1 : 1)
                            const dy = (p.y - pressY) * (handle.hy < 0 ? -1 : 1)
                            let w = startW + (handle.hx !== 0 ? dx : 0)
                            let h = startH + (handle.hy !== 0 ? dy : 0)
                            //  Ecken halten das Seitenverhältnis (wie in Word);
                            //  Kantenmitten dürfen bewusst verzerren.
                            if (handle.hx !== 0 && handle.hy !== 0) {
                                const f = Math.max(w / startW, h / startH)
                                w = startW * f
                                h = startH * f
                            }
                            imgBox.previewW = Math.max(12, w)
                            imgBox.previewH = Math.max(12, h)
                        }
                        onReleased: {
                            if (!imgBox.dragging) return
                            imgBox.dragging = false
                            editCtl.setImageSizeMm(area.selImageBlock,
                                                   imgBox.previewW * imgBox.mmPerPx,
                                                   imgBox.previewH * imgBox.mmPerPx)
                        }
                    }
                }
            }
        }

        //  ── Rahmen + Ziehpunkte der ausgewählten Tabelle ───────────────────
        //  Auswahl = der Cursor steht in einer Tabelle (kein zweiter Zustand,
        //  wie beim Bild). Gezogen wird eine VORSCHAU; beim Loslassen geht EIN
        //  Aufruf an den Controller, der ALLE Spalten mit demselben Faktor
        //  skaliert - die Zellen behalten so ihr Verhältnis zueinander.
        //  Der Rahmen umfasst die GANZE Tabelle: eine über Seiten getrennte
        //  bekommt je Stück einen Rahmenteil (`area.selTableRects`), weil
        //  zwischen den Stücken der Seitenrand liegt. Die Ziehpunkte sitzen am
        //  Stück des Cursors (`selTableX/Y/W/H`).
        Item {
            id: tblBox
            visible: area.selTableId >= 0 && editCtl.ready && !imgBox.visible
            z: 3
            anchors.fill: parent

            property bool dragging: false
            property real previewW: 0

            //  Stück am Cursor - Bezug der Ziehpunkte.
            readonly property real primX: area.x + area.selTableX
            readonly property real primY: area.y + area.selTableY
            readonly property real primW: dragging ? previewW
                                                   : Math.max(1, area.selTableW)
            readonly property real primH: Math.max(1, area.selTableH)

            //  Ein Rahmenteil je Seitenstück. Beim Ziehen zeigen ALLE die neue
            //  Breite: skaliert wird die Tabelle als Ganzes.
            Repeater {
                model: area.selTableRects
                delegate: Rectangle {
                    required property var modelData
                    x: area.x + modelData.x - 2
                    y: area.y + modelData.y - 2
                    width:  (tblBox.dragging ? tblBox.previewW
                                             : Math.max(1, modelData.w)) + 4
                    height: Math.max(1, modelData.h) + 4
                    color: "transparent"
                    border.color: App.themeAccent
                    border.width: tblBox.dragging ? 2 : 1
                    opacity: tblBox.dragging ? 1.0 : 0.55

                    //  Klick auf den RAHMEN wählt die ganze Tabelle aus - danach
                    //  löscht `Entf`/`Rücktaste` sie (`deleteSelectedTable`).
                    //  VIER Randstreifen statt einer Fläche mit `containmentMask`:
                    //  eine Maske aus einem `QtObject` wird von Qt VERWORFEN
                    //  („does not have an invokable contains method") - die Fläche
                    //  hätte dann die ganze Tabelle abgedeckt und jeden Klick in
                    //  eine Zelle verschluckt. Die Ziehpunkte liegen darüber und
                    //  behalten Vorrang.
                    Repeater {
                        model: [ "top", "bottom", "left", "right" ]
                        delegate: MouseArea {
                            required property string modelData
                            readonly property bool horiz: modelData === "top"
                                                          || modelData === "bottom"
                            width:  horiz ? parent.width : 6
                            height: horiz ? 6 : parent.height
                            x: modelData === "right" ? parent.width - 6 : 0
                            y: modelData === "bottom" ? parent.height - 6 : 0
                            acceptedButtons: Qt.LeftButton
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                editCtl.selectTable(area.selTableId)
                                area.forceActiveFocus()
                            }
                        }
                    }
                }
            }

            //  Nur WAAGERECHT wirksame Ziehpunkte: die Höhe einer Tabelle
            //  ergibt sich aus dem Inhalt ihrer Zellen. Punkte, die nichts
            //  bewirken (Mitte oben/unten), werden deshalb gar nicht erst
            //  angeboten - sie sahen wie eine Höhenverstellung aus und ließen
            //  beim Loslassen alles, wie es war.
            Repeater {
                model: [ { hx: -1, hy: -1 }, { hx: 1, hy: -1 },
                         { hx: -1, hy:  0 }, { hx: 1, hy:  0 },
                         { hx: -1, hy:  1 }, { hx: 1, hy:  1 } ]
                delegate: Rectangle {
                    id: th
                    required property var modelData
                    readonly property int hx: modelData.hx
                    readonly property int hy: modelData.hy
                    width: 8; height: 8; radius: 2
                    color: App.themeAccent
                    border.color: "#ffffff"; border.width: 1
                    x: tblBox.primX + (hx < 0 ? 0 : tblBox.primW) - width / 2
                    y: tblBox.primY + (hy < 0 ? 0 : hy > 0 ? tblBox.primH
                                                           : tblBox.primH / 2) - height / 2

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        acceptedButtons: Qt.LeftButton
                        preventStealing: true
                        cursorShape: Qt.SizeHorCursor
                        property real pressX: 0
                        property real startW: 0
                        onPressed: (m) => {
                            pressX = mapToItem(viewport, m.x, m.y).x
                            startW = Math.max(1, area.selTableW)
                            tblBox.previewW = startW
                            tblBox.dragging = true
                        }
                        onPositionChanged: (m) => {
                            if (!tblBox.dragging) return
                            //  Nach außen ziehen vergrößert, wie beim Bild.
                            const dx = (mapToItem(viewport, m.x, m.y).x - pressX)
                                       * (th.hx < 0 ? -1 : 1)
                            tblBox.previewW = Math.max(40, startW + dx)
                        }
                        onReleased: {
                            if (!tblBox.dragging) return
                            tblBox.dragging = false
                            const f = tblBox.previewW / Math.max(1, startW)
                            if (Math.abs(f - 1.0) > 0.01)
                                editCtl.scaleTableWidths(area.selTableId, f)
                            area.forceActiveFocus()
                        }
                    }
                }
            }
        }

        //  ── Kontextmenü (Rechtsklick): Tabelle & Bild ──────────────────────
        //  Bewusst ein gethemtes Popup statt eines Controls-`Menu`: die App malt
        //  alle Bedienelemente selbst, und ein eigener Menu-Background braucht
        //  eine berechnete implicitWidth, sonst kollabiert das Popup.
        Popup {
            id: ctxMenu
            parent: area
            width: 268
            padding: 5
            modal: false
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                         | Popup.CloseOnReleaseOutside
            //  [{ text, act, enabled, sep }] - von openFor() gefüllt.
            property var entries: []

            function openFor(mx, my, block) {
                const t = editCtl.tableInfoAt(block)
                const im = editCtl.imageInfoAt(block)
                let list = []
                if (t.table) {
                    if (t.editable) {
                        list.push({ text: App.uiText(App.language, "DocxRowInsertAbove"),
                                    act: () => editCtl.tableInsertRow(t.tableId, t.row) })
                        list.push({ text: App.uiText(App.language, "DocxRowInsertBelow"),
                                    act: () => editCtl.tableInsertRow(t.tableId, t.row + 1) })
                        list.push({ text: App.uiText(App.language, "DocxRowDelete"),
                                    enabled: t.rows > 1,
                                    act: () => editCtl.tableDeleteRow(t.tableId, t.row) })
                        list.push({ sep: true })
                        list.push({ text: App.uiText(App.language, "DocxColInsertLeft"),
                                    act: () => editCtl.tableInsertColumn(t.tableId, t.col) })
                        list.push({ text: App.uiText(App.language, "DocxColInsertRight"),
                                    act: () => editCtl.tableInsertColumn(t.tableId, t.col + 1) })
                        list.push({ text: App.uiText(App.language, "DocxColDelete"),
                                    enabled: t.cols > 1,
                                    act: () => editCtl.tableDeleteColumn(t.tableId, t.col) })
                        list.push({ sep: true })
                        list.push({ text: App.uiText(App.language, "DocxColWidths"),
                                    act: () => widthPop.openFor(t) })
                    } else {
                        list.push({ text: App.uiText(App.language, "DocxTableLocked"),
                                    enabled: false })
                    }
                    list.push({ sep: true })
                    //  Ganze Tabelle in die Zwischenablage - dieselben Wege wie
                    //  Strg+C/X/V, hier nur sichtbar gemacht. Verschieben =
                    //  Ausschneiden + an der Zielstelle Einfügen.
                    list.push({ text: App.uiText(App.language, "DocxTableCopy"),
                                enabled: t.editable,
                                act: () => editCtl.copyTableAtCursor() })
                    list.push({ text: App.uiText(App.language, "DocxTableCut"),
                                enabled: t.editable,
                                act: () => editCtl.cut() })
                    list.push({ text: App.uiText(App.language, "DocxTableDelete"),
                                act: () => editCtl.deleteTable(t.tableId) })
                }
                //  „Weiter unter der Tabelle": setzt Words Textumbruch mit
                //  `w:clear="all"` an die Cursorstelle. Neben einer gleitenden
                //  Tabelle ist das der einzige Weg, bewusst wieder UNTER sie zu
                //  kommen - angeboten deshalb genau dann, wenn der Cursor NICHT
                //  in der Tabelle steht (dort gäbe es nichts zu unterlaufen).
                if (editCtl.ready && !t.table) {
                    if (list.length > 0) list.push({ sep: true })
                    list.push({ text: App.uiText(App.language, "DocxClearBreak"),
                                act: () => editCtl.insertClearBreak() })
                }
                //  Einfügen steht auch AUSSERHALB einer Tabelle bereit - sonst
                //  ließe sich eine ausgeschnittene Tabelle nirgends absetzen.
                //  Nur, wenn wirklich eine in der Ablage liegt: sonst erschiene
                //  bei jedem Rechtsklick im Fließtext ein Menü mit einem
                //  einzigen, wirkungslosen Eintrag.
                if (editCtl.ready && editCtl.clipboardHasTable()) {
                    if (list.length > 0) list.push({ sep: true })
                    list.push({ text: App.uiText(App.language, "DocxTablePaste"),
                                act: () => editCtl.paste() })
                }
                if (im.image) {
                    if (list.length > 0) list.push({ sep: true })
                    list.push({ text: App.uiText(App.language, "DocxImageSize"),
                                act: () => imgSizePop.openFor(im) })
                    //  Umbruchart: in der Zeile (wp:inline) oder umfließend
                    //  (wp:anchor + w:wrapSquare) - gezeigt wird immer das,
                    //  was man WÄHLEN kann, nicht der aktuelle Zustand.
                    list.push({ text: App.uiText(App.language,
                                    im.floating ? "DocxImageInline"
                                                : "DocxImageFloating"),
                                act: () => editCtl.setImageFloating(block,
                                                                    !im.floating) })
                    //  Umbruchseite - nur ein umfließendes Bild hat eine. Der
                    //  laufende Wert bekommt einen Haken, damit man nicht raten
                    //  muss, wie es gerade steht.
                    if (im.floating) {
                        //  Alle vier Werte, die ECMA-376 kennt - „beide Seiten"
                        //  ist Words Vorgabe und die der Anzeige: sie teilt ein
                        //  Zeilenband dafür in ein Stück links und eines rechts
                        //  des Bildes. Ist eine Seite zu schmal, weicht sie von
                        //  selbst auf die breitere aus.
                        const side = (v) => (im.wrapSide === v ? "✓  " : "     ")
                        list.push({ text: side(0) + App.uiText(App.language, "DocxWrapBoth"),
                                    act: () => editCtl.setImageWrapSide(block, 0) })
                        list.push({ text: side(3) + App.uiText(App.language, "DocxWrapLargest"),
                                    act: () => editCtl.setImageWrapSide(block, 3) })
                        list.push({ text: side(1) + App.uiText(App.language, "DocxWrapLeft"),
                                    act: () => editCtl.setImageWrapSide(block, 1) })
                        list.push({ text: side(2) + App.uiText(App.language, "DocxWrapRight"),
                                    act: () => editCtl.setImageWrapSide(block, 2) })
                    }
                    list.push({ text: App.uiText(App.language, "DocxImageCopy"),
                                act: () => editCtl.copyImageAtCursor() })
                    list.push({ text: App.uiText(App.language, "DocxImageCut"),
                                act: () => editCtl.cut() })
                    list.push({ text: App.uiText(App.language, "DocxImageDelete"),
                                act: () => editCtl.deleteImageAtCursor() })
                }
                //  ── Änderungsverfolgung: annehmen/verwerfen ───────────────
                //  Nur, wenn an der Cursorstelle wirklich eine Änderung steht.
                {
                    const rev = editCtl.revisionAt(editCtl.cursorBlock(),
                                                   editCtl.cursorPos())
                    if (rev > 0) {
                        const who = editCtl.revisionAuthorAt(editCtl.cursorBlock(),
                                                             editCtl.cursorPos())
                        const suffix = who.length > 0 ? "  (" + who + ")" : ""
                        const head = []
                        head.push({ text: App.uiText(App.language, "DocxRevAccept") + suffix,
                                    act: () => editCtl.acceptRevisionAt(
                                             editCtl.cursorBlock(), editCtl.cursorPos()) })
                        head.push({ text: App.uiText(App.language, "DocxRevReject") + suffix,
                                    act: () => editCtl.rejectRevisionAt(
                                             editCtl.cursorBlock(), editCtl.cursorPos()) })
                        if (list.length > 0) head.push({ sep: true })
                        list = head.concat(list)
                    }
                }

                //  ── Rechtschreibung: Vorschläge ganz OBEN ─────────────────
                //  Der Rechtsklick setzt den Cursor an die Stelle (s. area),
                //  also fragt das Menü genau dort nach. Ersetzt wird nur auf
                //  Wahl - die Prüfung fasst den Text nie von selbst an.
                if (editCtl.spellAvailable) {
                    const sug = editCtl.spellSuggestions(editCtl.cursorBlock(),
                                                         editCtl.cursorPos())
                    if (sug.length > 0 || editCtl.spellHasIssueAt(editCtl.cursorBlock(),
                                                                  editCtl.cursorPos())) {
                        const head = []
                        for (let i = 0; i < Math.min(5, sug.length); ++i) {
                            const w = sug[i]
                            head.push({ text: w,
                                        act: () => editCtl.spellReplaceAt(
                                                 editCtl.cursorBlock(),
                                                 editCtl.cursorPos(), w) })
                        }
                        if (sug.length === 0)
                            head.push({ text: App.uiText(App.language, "DocxSpellNoSuggestion"),
                                        enabled: false, act: () => {} })
                        head.push({ text: App.uiText(App.language, "DocxSpellIgnore"),
                                    act: () => editCtl.spellIgnoreAt(editCtl.cursorBlock(),
                                                                     editCtl.cursorPos()) })
                        if (list.length > 0) head.push({ sep: true })
                        list = head.concat(list)
                    }
                }
                if (list.length === 0) return
                ctxMenu.entries = list
                ctxMenu.x = Math.max(0, Math.min(mx, area.width - ctxMenu.width))
                ctxMenu.y = Math.max(0, Math.min(my, area.height - ctxMenu.implicitHeight))
                ctxMenu.open()
            }

            background: Rectangle {
                color: App.themeMenuBarBg
                border.color: App.themeBorder
                radius: 8
            }
            contentItem: Column {
                spacing: 1
                Repeater {
                    model: ctxMenu.entries
                    delegate: Item {
                        required property var modelData
                        readonly property bool isSep: modelData.sep === true
                        readonly property bool on: !isSep && modelData.enabled !== false
                        width: ctxMenu.width - 2 * ctxMenu.padding
                        height: isSep ? 7 : 26

                        Rectangle {
                            visible: parent.isSep
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width; height: 1
                            color: App.themeBorder
                        }
                        Rectangle {
                            visible: !parent.isSep
                            anchors.fill: parent
                            radius: 5
                            color: (ctxHover.hovered && parent.on) ? App.themeCard
                                                                   : "transparent"
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 8
                                width: parent.width - 16
                                elide: Text.ElideRight
                                //  Trenner-Einträge tragen keinen Text.
                                text: modelData.text !== undefined ? modelData.text : ""
                                font.pixelSize: 12
                                color: parent.parent.on ? App.themeTextPrimary
                                                        : App.themeTextMuted
                            }
                            HoverHandler { id: ctxHover; enabled: parent.parent.on }
                            TapHandler {
                                enabled: parent.parent.on
                                onTapped: {
                                    ctxMenu.close()
                                    modelData.act()
                                    area.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }

        //  ── Spaltenbreiten (aus dem Kontextmenü) ───────────────────────────
        Popup {
            id: widthPop
            parent: area
            padding: 10
            modal: false
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            property int tblId: -1
            property var mmValues: []          // je Spalte ein ganzzahliger mm-Wert

            function openFor(t) {
                widthPop.tblId = t.tableId
                let v = []
                for (let i = 0; i < t.widths.length; ++i)
                    v.push(Math.max(4, Math.round(t.widths[i])))
                widthPop.mmValues = v
                widthPop.x = Math.max(0, (area.width - widthPop.implicitWidth) / 2)
                widthPop.y = 60
                widthPop.open()
            }
            function setAt(i, v) {
                let c = widthPop.mmValues.slice()
                c[i] = v
                widthPop.mmValues = c
            }

            background: Rectangle {
                color: App.themeMenuBarBg
                border.color: App.themeBorder
                radius: 8
            }
            contentItem: Column {
                spacing: 8
                Text {
                    text: App.uiText(App.language, "DocxColWidths")
                    color: App.themeTextMuted; font.pixelSize: 11
                }
                Repeater {
                    model: widthPop.mmValues.length
                    delegate: Row {
                        required property int index
                        spacing: 8
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 84
                            text: (parent.index + 1) + ". (mm)"
                            color: App.themeTextPrimary; font.pixelSize: 12
                        }
                        DSpin {
                            value: widthPop.mmValues[parent.index]
                            from: 4; to: 400
                            onCommitted: (v) => widthPop.setAt(parent.index, v)
                        }
                    }
                }
                Rectangle {
                    width: 130; height: 26; radius: 6
                    color: wapHover.hovered ? Qt.darker(App.themeAccent, 1.1)
                                            : App.themeAccent
                    Text {
                        anchors.centerIn: parent
                        text: App.uiText(App.language, "DocxApply")
                        color: "#ffffff"; font.pixelSize: 12; font.bold: true
                    }
                    HoverHandler { id: wapHover }
                    TapHandler {
                        onTapped: {
                            editCtl.tableSetColumnWidthsMm(widthPop.tblId,
                                                           widthPop.mmValues)
                            widthPop.close()
                            area.forceActiveFocus()
                        }
                    }
                }
            }
        }

        //  ── Bildgröße numerisch (aus dem Kontextmenü) ──────────────────────
        Popup {
            id: imgSizePop
            parent: area
            padding: 10
            modal: false
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            property int blk: -1
            property int wMm: 0
            property int hMm: 0
            property real ratio: 1.0
            property bool keepAspect: true

            function openFor(im) {
                imgSizePop.blk = im.block
                imgSizePop.wMm = Math.max(1, Math.round(im.widthMm))
                imgSizePop.hMm = Math.max(1, Math.round(im.heightMm))
                imgSizePop.ratio = imgSizePop.hMm / Math.max(1, imgSizePop.wMm)
                imgSizePop.x = Math.max(0, (area.width - imgSizePop.implicitWidth) / 2)
                imgSizePop.y = 60
                imgSizePop.open()
            }

            background: Rectangle {
                color: App.themeMenuBarBg
                border.color: App.themeBorder
                radius: 8
            }
            contentItem: Column {
                spacing: 8
                Row {
                    spacing: 8
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 96
                        text: App.uiText(App.language, "DocxImageWidth")
                        color: App.themeTextPrimary; font.pixelSize: 12
                    }
                    DSpin {
                        value: imgSizePop.wMm
                        from: 1; to: 400
                        onCommitted: (v) => {
                            imgSizePop.wMm = v
                            if (imgSizePop.keepAspect)
                                imgSizePop.hMm = Math.max(1, Math.round(v * imgSizePop.ratio))
                        }
                    }
                }
                Row {
                    spacing: 8
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 96
                        text: App.uiText(App.language, "DocxImageHeight")
                        color: App.themeTextPrimary; font.pixelSize: 12
                    }
                    DSpin {
                        value: imgSizePop.hMm
                        from: 1; to: 400
                        onCommitted: (v) => {
                            imgSizePop.hMm = v
                            if (imgSizePop.keepAspect)
                                imgSizePop.wMm = Math.max(1, Math.round(
                                                     v / Math.max(0.01, imgSizePop.ratio)))
                        }
                    }
                }
                Row {
                    spacing: 6
                    Rectangle {
                        width: 16; height: 16; radius: 3
                        anchors.verticalCenter: parent.verticalCenter
                        color: imgSizePop.keepAspect ? App.themeAccent : App.themeCard
                        border.color: App.themeBorder
                        Text {
                            anchors.centerIn: parent
                            text: imgSizePop.keepAspect ? "✓" : ""
                            color: "#ffffff"; font.pixelSize: 11
                        }
                        TapHandler {
                            onTapped: imgSizePop.keepAspect = !imgSizePop.keepAspect
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.uiText(App.language, "DocxKeepAspect")
                        color: App.themeTextPrimary; font.pixelSize: 12
                    }
                }
                Rectangle {
                    width: 130; height: 26; radius: 6
                    color: iapHover.hovered ? Qt.darker(App.themeAccent, 1.1)
                                            : App.themeAccent
                    Text {
                        anchors.centerIn: parent
                        text: App.uiText(App.language, "DocxApply")
                        color: "#ffffff"; font.pixelSize: 12; font.bold: true
                    }
                    HoverHandler { id: iapHover }
                    TapHandler {
                        onTapped: {
                            editCtl.setImageSizeMm(imgSizePop.blk, imgSizePop.wMm,
                                                   imgSizePop.hMm)
                            imgSizePop.close()
                            area.forceActiveFocus()
                        }
                    }
                }
            }
        }

        //  ── Suchen & Ersetzen (Ctrl+F) ─────────────────────────────────────
        Rectangle {
            id: findBar
            visible: root.findVisible
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 8
            anchors.rightMargin: 18
            z: 5
            radius: 8
            color: App.themeMenuBarBg
            border.color: App.themeBorder
            width: findGrid.implicitWidth + 20
            height: findGrid.implicitHeight + 16

            property bool matchCase: false

            function doFind(backward) {
                if (findField.text.length === 0) return
                const r = editCtl.findNext(findField.text, findBar.matchCase, backward)
                if (!r.found)
                    statusText.flash(App.uiText(App.language, "DocxFindNoMatch"))
            }
            function doReplace() {
                if (findField.text.length === 0) return
                const r = editCtl.replaceAndFind(findField.text, replaceField.text,
                                                 findBar.matchCase)
                if (!r.found)
                    statusText.flash(App.uiText(App.language, "DocxFindNoMatch"))
            }
            function doReplaceAll() {
                if (findField.text.length === 0) return
                const n = editCtl.replaceAll(findField.text, replaceField.text,
                                             findBar.matchCase)
                statusText.flash(n > 0
                    ? App.uiText(App.language, "DocxReplacedCount").replace("%1", n)
                    : App.uiText(App.language, "DocxFindNoMatch"))
            }

            //  Gemeinsamer Feld-Baustein (gethemt).
            component FField: Rectangle {
                property alias field: innerField
                //  Ohne dieses Alias wäre findField.text/replaceField.text
                //  undefined -> doFind/doReplace/doReplaceAll brächen mit
                //  TypeError ab (Suchen&Ersetzen komplett funktionslos).
                property alias text: innerField.text
                property string ph: ""
                signal accepted()
                signal shiftAccepted()
                width: 190; height: 28; radius: 6
                color: App.themeCard
                border.color: innerField.activeFocus ? App.themeAccent : App.themeBorder
                TextField {
                    id: innerField
                    anchors.fill: parent
                    anchors.leftMargin: 8; anchors.rightMargin: 8
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: parent.ph
                    color: App.themeTextPrimary
                    placeholderTextColor: App.themeTextMuted
                    font.pixelSize: 12
                    background: null
                    selectByMouse: true
                    Keys.onReturnPressed: (e) => {
                        if (e.modifiers & Qt.ShiftModifier) parent.shiftAccepted()
                        else parent.accepted()
                        e.accepted = true
                    }
                    Keys.onEscapePressed: (e) => { root.closeFind(); e.accepted = true }
                }
            }

            //  Gethemter kleiner Knopf.
            //  `parent` meint hier den ELTERN-Item der Komponente, nicht die
            //  Komponente selbst - `parent.tip`/`parent.label` waren deshalb
            //  undefiniert („Unable to assign [undefined] to QString", sechsmal
            //  je Suchleiste). Über die eigene id ist es eindeutig.
            component FBtn: Rectangle {
                id: fbtn
                property string label: ""
                property string tip: ""
                property bool wide: false
                signal clicked()
                width: wide ? (fbLbl.implicitWidth + 20) : 28
                height: 28; radius: 6
                color: fbHover.hovered ? App.themeAccent : App.themeCard
                border.color: App.themeBorder
                Text { id: fbLbl; anchors.centerIn: parent; text: fbtn.label
                       color: fbHover.hovered ? "#ffffff" : App.themeTextPrimary
                       font.pixelSize: 12 }
                HoverHandler { id: fbHover }
                TapHandler { onTapped: fbtn.clicked() }
                ToolTip.visible: fbHover.hovered && fbtn.tip.length > 0
                ToolTip.delay: 500
                ToolTip.text: fbtn.tip
            }

            Grid {
                id: findGrid
                anchors.centerIn: parent
                columns: 2
                rowSpacing: 6
                columnSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter

                //  Zeile 1: Suchfeld + Navigations-/Optionsknöpfe.
                FField {
                    id: findField
                    ph: App.uiText(App.language, "DocxFindPlaceholder")
                    onAccepted: findBar.doFind(false)
                    onShiftAccepted: findBar.doFind(true)
                }
                Row {
                    spacing: 6
                    FBtn { label: "▴"; tip: App.uiText(App.language, "DocxFindPrev")
                           onClicked: findBar.doFind(true) }
                    FBtn { label: "▾"; tip: App.uiText(App.language, "DocxFindNext")
                           onClicked: findBar.doFind(false) }
                    FBtn { label: "Aa"; tip: App.uiText(App.language, "DocxMatchCase")
                           onClicked: findBar.matchCase = !findBar.matchCase
                           color: findBar.matchCase ? App.themeAccent
                                  : (aaHover.hovered ? App.themeCard : "transparent")
                           HoverHandler { id: aaHover } }
                    FBtn { label: "✕"; tip: ""
                           onClicked: root.closeFind() }
                }

                //  Zeile 2: Ersetzungsfeld + Ersetzen-/Alle-Knöpfe.
                FField {
                    id: replaceField
                    ph: App.uiText(App.language, "DocxReplacePlaceholder")
                    onAccepted: findBar.doReplace()
                    onShiftAccepted: findBar.doReplace()
                }
                Row {
                    spacing: 6
                    FBtn { wide: true; label: App.uiText(App.language, "DocxReplaceOne")
                           onClicked: findBar.doReplace() }
                    FBtn { wide: true; label: App.uiText(App.language, "DocxReplaceAll")
                           onClicked: findBar.doReplaceAll() }
                }
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

        //  Animiertes Mausrad (180 ms OutCubic) - schreibt area.contentY.
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
            //  ZEILEN je Rastung, wie in jedem Texteditor - nicht ein Anteil
            //  der Fensterhöhe. Der Anteil war zuletzt ein Viertel Viewport und
            //  damit auf einem 900-px-Fenster ~225 px je Rastung, also das
            //  Vierfache eines gewöhnlichen Editors; außerdem hing die
            //  Geschwindigkeit an der Fenstergröße (Nutzerbefund „immer noch zu
            //  schnell"). Die Zeilenzahl kommt aus derselben Systemeinstellung,
            //  die auch die Qt-Dialoge benutzen.
            //  `Application.styleHints`, NICHT `Qt.styleHints` - Letzteres gibt
            //  es in QML nicht, die Bindung wäre still NaN und das Rad tot.
            readonly property int wheelLines:
                Math.max(1, Application.styleHints.wheelScrollLines)
            onWheel: (w) => {
                //  Touchpad/hochauflösendes Rad liefern `pixelDelta` - das ist
                //  bereits die gewünschte Strecke und wird PIXELGENAU
                //  übernommen; alles andere zählt Rastungen (120 = eine).
                let dy = 0
                if (w.pixelDelta.y !== 0)
                    dy = w.pixelDelta.y
                else if (w.angleDelta.y !== 0)
                    dy = (w.angleDelta.y / 120) * wheelLines * area.lineStep
                if (dy === 0) return
                const base = scrollAnim.running ? scrollAnim.to : area.contentY
                scrollAnim.to = Math.max(0,
                    Math.min(base - dy,
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
