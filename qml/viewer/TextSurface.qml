pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  TextSurface.qml — editierbarer Plain-Text-Editor (ersetzt TextViewer(QWidget)).
//  Inhalt via Viewer.readTextFile, Speichern via Viewer.writeTextFile (atomar).
//
//  • Editierbar (kein read-only mehr); Strg+S oder Speichern-Button schreibt.
//  • Ungespeicherte Aenderungen werden mit "•" markiert; beim Verlassen
//    (release) automatisch gespeichert, damit keine Eingaben verloren gehen.
//  • topInset/bottomInset werden vom FullscreenViewer reserviert → die globale
//    Leiste (Dateiname) ueberdeckt den Inhalt NICHT mehr.
//  • Weiches, web-aehnliches Mausrad-Scrollen (animiert, groesserer Schritt).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0

    property string currentPath: ""
    property bool   dirty: false
    property bool   _loading: false
    //  Läuft gerade ein PDF-Export? (Der Knopf bleibt so lange stumm — der
    //  Export selbst liegt im Worker, die Oberfläche bleibt bedienbar.)
    property bool   _pdfBusy: false

    //  Schriftfarbe des PDF-Exports. Der Zähler treibt die Neuauswertung: die
    //  Farbe kommt aus einer Invokable (Ordner-Sidecar), also gibt es kein
    //  Signal, an dem eine Bindung allein hängen könnte. `App.textPdfColor` steht
    //  bewusst MIT in der Bindung — ändert sich die globale Vorgabe, zieht eine
    //  Datei ohne eigene Farbe sofort nach.
    property int    _inkRev: 0
    readonly property color _pdfInk: {
        //  Die globale Vorgabe wird IMMER gelesen, auch wenn sie gleich verworfen
        //  wird: eine QML-Bindung hängt nur an dem, was sie tatsächlich anfasst —
        //  stünde sie im else-Zweig, bliebe eine Datei ohne eigene Farbe beim
        //  Ändern der Vorgabe auf dem alten Wert stehen.
        var vorgabe = App.textPdfColor
        return (root._inkRev, root.currentPath.length > 0)
               ? App.fileTextPdfColor(root.currentPath) : vorgabe
    }
    readonly property bool _pdfInkOwn:
        (root._inkRev, root.currentPath.length > 0)
        && App.hasFileTextPdfColor(root.currentPath)

    // HTML-Quelltext bekommt eine eigene Editor-Hintergrundfarbe (Design-Tab),
    // getrennt von TXT/Code — daher Endungserkennung aus dem aktuellen Pfad.
    readonly property bool _isHtml: {
        var p = root.currentPath.toLowerCase()
        return p.endsWith(".html") || p.endsWith(".htm")
    }

    function save() {
        if (!root.dirty || root.currentPath.length === 0) return
        if (Viewer.writeTextFile(root.currentPath, editor.text))
            root.dirty = false
    }

    function release() {
        root.save()                       // beim Verlassen sichern (kein Datenverlust)
        editor.text = ""
        root.currentPath = ""
        root.dirty = false
    }

    onSourceChanged: {
        root.save()                       // evtl. vorherige Datei sichern
        root._loading = true
        root.currentPath = source
        editor.text = source.length > 0 ? Viewer.readTextFile(source) : ""
        editor.cursorPosition = 0
        root.dirty = false
        root._loading = false
    }

    Rectangle { anchors.fill: parent; color: App.themeBackground }

    // ── Toolbar (unter der globalen Leiste) ───────────────────────────────────
    Rectangle {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset }
        height: 40
        color: App.themeToolbarBg
        z: 4
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: App.themeBorder }

        Row {
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: saveRow.implicitWidth + 22; height: 26; radius: 6
                opacity: root.dirty ? 1.0 : 0.5
                color: saveHover.hovered && root.dirty ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
                                                       : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.16)
                border.color: App.themeAccent; border.width: 1
                Row {
                    id: saveRow
                    anchors.centerIn: parent; spacing: 6
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "\u2913"
                           color: App.themeAccent; font.pixelSize: 13 }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: App.uiText(App.language, "EditorSave")
                           color: App.themeAccent; font.pixelSize: 12 }
                }
                HoverHandler { id: saveHover; enabled: root.dirty }
                TapHandler { enabled: root.dirty; onTapped: root.save() }
            }

            //  Schriftfarbe des PDF-Exports. Vorgabe kommt aus den Einstellungen
            //  (App.textPdfColor); wählt der Nutzer hier eine Farbe, gilt sie NUR
            //  für diese Datei und liegt im Ordner-Sidecar neben Tags und Datum.
            //  Farbfeld und Zurücksetzen stecken in EINEM Rahmen: allein stehend
            //  war dem Zurücksetzen-Knopf nicht anzusehen, worauf er sich bezieht.
            Rectangle {
                id: inkBox
                anchors.verticalCenter: parent.verticalCenter
                visible: root.currentPath.length > 0
                height: 26; radius: 6
                width: inkRow.width + 12
                color: "transparent"
                border.color: App.themeBorder
                border.width: 1

                Row {
                    id: inkRow
                    anchors.centerIn: parent
                    spacing: 6

                    ColorPicker {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26; height: 18
                        showAlpha: false
                        title: App.uiText(App.language, "TextPdfColorTitle")
                        selectedColor: root._pdfInk
                        onColorPicked: function (c) {
                            App.setFileTextPdfColor(root.currentPath, c)
                            root._inkRev++
                            selectedColor = Qt.binding(function () { return root._pdfInk })
                        }
                        ToolTip.visible: pdfInkHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: App.uiText(App.language, "TextPdfColorTip")
                        HoverHandler { id: pdfInkHover }
                    }

                    //  Trenner + Zurücksetzen erscheinen erst, wenn die Datei eine
                    //  eigene Farbe trägt — vorher gäbe es nichts zurückzusetzen.
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root._pdfInkOwn
                        width: 1; height: 16
                        color: App.themeBorder
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root._pdfInkOwn
                        width: 18; height: 18; radius: 4
                        color: inkResetHover.hovered ? App.themeCard : "transparent"
                        DrawnIcon {
                            anchors.centerIn: parent
                            name: "undo"; size: 12
                            color: inkResetHover.hovered ? App.themeTextPrimary : App.themeTextMuted
                        }
                        HoverHandler { id: inkResetHover }
                        TapHandler {
                            onTapped: {
                                App.clearFileTextPdfColor(root.currentPath)
                                root._inkRev++
                            }
                        }
                        ToolTip.visible: inkResetHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: App.uiText(App.language, "TextPdfColorResetTip")
                    }
                }
            }

            //  Text → PDF: schreibt <Name>.pdf NEBEN die Quelle, die Textdatei
            //  bleibt unangetastet. Gedruckt wird der STAND IM EDITOR (nicht der
            //  auf Platte) — ungespeicherte Änderungen sind also mit im PDF.
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: pdfLbl.implicitWidth + 18; height: 26; radius: 6
                color: pdfHover.hovered ? App.themeCard : "transparent"
                border.color: App.themeBorder
                opacity: (root.currentPath.length > 0 && !root._pdfBusy) ? 1.0 : 0.45
                Text {
                    id: pdfLbl
                    anchors.centerIn: parent
                    text: App.uiText(App.language, "TextExportPdf")
                    color: App.themeTextPrimary
                    font.pixelSize: 12
                }
                HoverHandler { id: pdfHover }
                TapHandler {
                    onTapped: {
                        if (root.currentPath.length === 0 || root._pdfBusy)
                            return
                        root._pdfBusy = true
                        Viewer.exportTextToPdf(root.currentPath, editor.text, root._pdfInk)
                    }
                }
                ToolTip.visible: pdfHover.hovered
                ToolTip.delay: 600
                ToolTip.text: App.uiText(App.language, "TextExportPdfTip")
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (root.dirty ? "\u2022 " : "") + root.currentPath
                color: root.dirty ? App.themeAccent : App.themeTextMuted
                font.pixelSize: 11; elide: Text.ElideLeft
                width: Math.min(implicitWidth, toolbar.width - 300)
            }
        }

        // Live-Transliteration (oben rechts): Latein → Arabisch/Kana beim
        // Tippen; Schema-Auswahl im Popup, Umsetzung s. editor.onTextChanged.
        TranslitButton {
            anchors { right: parent.right; rightMargin: 12
                      verticalCenter: parent.verticalCenter }
        }
    }

    // ── Editor (editierbar, eigene Flickable für sauberes Scrollen) ────────────
    Flickable {
        id: flick
        anchors {
            left: parent.left; right: parent.right
            top: toolbar.bottom; bottom: parent.bottom
            bottomMargin: root.bottomInset
            margins: 12
        }
        clip: true
        //  TXT bricht an der SICHTBAREN Breite um (kein waagerechtes Scrollen
        //  mehr — Nutzerwunsch 2026-07-17); HTML-Quelltext behält NoWrap, weil
        //  Code-Zeilen dort bündig bleiben sollen.
        contentWidth: root._isHtml ? editor.paintedWidth : width
        contentHeight: editor.paintedHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar {
            policy: root._isHtml ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        TextArea.flickable: TextArea {
            id: editor
            readOnly: false
            selectByMouse: true
            //  Breite an den Viewport binden, sobald umgebrochen wird — sonst
            //  bliebe die TextArea so breit wie ihre längste Zeile.
            width: root._isHtml ? Math.max(implicitWidth, flick.width) : flick.width
            wrapMode: root._isHtml ? TextEdit.NoWrap : TextEdit.Wrap
            color: App.themeTextPrimary
            selectionColor: App.themeAccent
            // Monospace für Latein (Code/HTML-Bündigkeit); arabische Glyphen fallen
            // pro Zeichen auf Naskh zurück (QFont-Familienliste aus C++, da QML in
            // Qt 6.4 kein font.families kennt).
            font: App.fallbackFont("monospace", 13)
            padding: 10
            background: Rectangle { color: root._isHtml ? App.themeEditorBgHtml : App.themeEditorBgText
                                    radius: 6; border.color: App.themeBorder }
            // Live-Transliteration: gezieltes remove()/insert() statt text-
            // Neuzuweisung (Undo-Stack + Performance großer Dateien bleiben
            // intakt); der Guard verhindert Re-Entranz durch die eigene Edition.
            //  ↓ in der LETZTEN (sichtbaren) Zeile springt ans Zeilenende,
            //  statt wirkungslos zu bleiben — einheitlich in allen Editoren
            //  der App (Vergleich der Cursor-Zeilen-y mit dem Textende deckt
            //  auch umgebrochene Zeilen ab).
            Keys.onDownPressed: (e) => {
                const yCur = editor.positionToRectangle(editor.cursorPosition).y
                const yEnd = editor.positionToRectangle(editor.length).y
                if (Math.abs(yCur - yEnd) < 0.5 && editor.cursorPosition < editor.length) {
                    if (e.modifiers & Qt.ShiftModifier)
                        editor.moveCursorSelection(editor.length)
                    else
                        editor.cursorPosition = editor.length
                    e.accepted = true
                } else {
                    e.accepted = false
                }
            }
            property bool _trGuard: false
            function _applyTranslit() {
                if (editor._trGuard || root._loading || !Translit.enabled)
                    return
                const r = Translit.liveApply(editor.text, editor.cursorPosition)
                if (!r.changed)
                    return
                editor._trGuard = true
                editor.remove(r.start, r.end)
                editor.insert(r.start, r.replacement)
                editor.cursorPosition = r.cursor
                editor._trGuard = false
            }
            onTextChanged: {
                if (!root._loading) root.dirty = true
                editor._applyTranslit()
            }
            Keys.onPressed: function(e) {
                if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_S) {
                    root.save(); e.accepted = true
                }
            }
        }
    }

    // ── Rückmeldung des PDF-Exports (Muster wie PdfSurface: kurzer Toast, kein
    //    Dialog — der Export ist eine Nebentätigkeit und soll nicht bestätigt
    //    werden müssen). Der Pfad wird auf den Dateinamen gekürzt.
    Connections {
        target: Viewer
        function onTextPdfExportFinished(ok, target, error) {
            root._pdfBusy = false
            if (ok)
                root._toast(App.uiText(App.language, "TextExportPdfOk")
                                .arg(target.split("/").pop()))
            else
                root._toast(App.uiText(App.language, "TextExportPdfFail")
                                .arg(error.length > 0 ? error : "?"))
        }
    }

    function _toast(msg) {
        toastLabel.text = msg
        toast.visible = true
        toastTimer.restart()
    }
    Rectangle {
        id: toast
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom
                  bottomMargin: root.bottomInset + 18 }
        width: toastLabel.implicitWidth + 28
        height: 32
        radius: 16
        visible: false
        z: 8
        color: Qt.rgba(0, 0, 0, 0.78)
        border.color: App.themeBorder; border.width: 1
        Text {
            id: toastLabel
            anchors.centerIn: parent
            color: "#ffffff"; font.pixelSize: 12
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, root.width - 80)
            horizontalAlignment: Text.AlignHCenter
        }
        Timer { id: toastTimer; interval: 3500; onTriggered: toast.visible = false }
    }

    // Weiches, web-aehnliches Mausrad-Scrollen — als Geschwister der Flickable,
    // damit der Fänger NICHT mit dem Inhalt mitscrollt. NoButton → Klicks/Markieren
    // erreichen den Editor.
    NumberAnimation {
        id: scrollAnim
        target: flick; property: "contentY"
        duration: 180; easing.type: Easing.OutCubic
    }
    MouseArea {
        anchors.fill: flick
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => {
            var maxY = Math.max(0, flick.contentHeight - flick.height)
            if (maxY <= 0) { wheel.accepted = true; return }
            var raw = (wheel.angleDelta.y !== 0)
                      ? (wheel.angleDelta.y / 120) * (flick.height * 0.5)
                      : wheel.pixelDelta.y * 1.6
            var base = scrollAnim.running ? scrollAnim.to : flick.contentY
            var tgt = Math.max(0, Math.min(base - raw, maxY))
            scrollAnim.from = flick.contentY
            scrollAnim.to = tgt
            scrollAnim.restart()
            wheel.accepted = true
        }
    }
}
