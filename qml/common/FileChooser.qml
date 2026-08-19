import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  FileChooser.qml - der EIGENE Datei-/Ordnerwähler der App.
//
//  WARUM: Qts `FileDialog`/`FolderDialog` öffnen ein eigenes Fenster, dessen
//  Liste in Qts eigener `FileDialog.qml` steckt. Dort ist `SmoothWheelArea`
//  nicht einhängbar - die Liste sprang deshalb in Stufen, während alles andere
//  in der App animiert scrollt; auch das Aussehen war nur über die Palette
//  steuerbar. Dieser Wähler ist ein `Popup` IM Fenster: gleiche Farben, gleiches
//  Scrollen, gleiche Tastaturbedienung wie der Rest der App.
//
//  VERTRAG (bewusst wie bei Qts Dialog, damit die Aufrufstellen sich kaum
//  ändern): `title`, `nameFilters`, `fileMode`, `defaultSuffix`, `currentFolder`,
//  Ergebnis in `selectedFile`/`selectedFolder`, Signal `accepted`, `open()`.
//
//      FileChooser {
//          id: dlg
//          title: "Bild wählen"
//          fileMode: FileChooser.OpenFile
//          nameFilters: [ "Bilder (*.png *.jpg)" ]
//          onAccepted: benutze(dlg.selectedFile)
//      }
// ─────────────────────────────────────────────────────────────────────────────
Popup {
    id: root

    enum FileMode { OpenFile, SaveFile, Directory }

    property string title: ""
    property var    nameFilters: []
    property int    fileMode: FileChooser.OpenFile
    property string defaultSuffix: ""
    //  Startverzeichnis (URL wie bei Qts Dialog) - leer = zuletzt benutztes
    //  bzw. das Heimatverzeichnis.
    property url    currentFolder
    //  Ergebnis. `selectedFiles` trägt bei MEHRFACHAUSWAHL alle gewählten
    //  Dateien (sonst genau eine); `selectedFile` bleibt die erste - die
    //  bestehenden Aufrufstellen ändern sich dadurch nicht.
    property url    selectedFile
    property url    selectedFolder
    property var    selectedFiles: []
    //  Mehrfachauswahl zulassen (nur beim ÖFFNEN sinnvoll - beim Speichern und
    //  bei der Ordnerwahl gibt es genau ein Ziel).
    property bool   multiSelect: false
    readonly property bool _multi: multiSelect && fileMode === FileChooser.OpenFile

    signal accepted()
    signal rejected()

    //  Der Wähler gehört über ALLES (Menüs, Leisten) und lebt so lange wie das
    //  Fenster - deshalb das Overlay als Elternteil und nicht der Aufrufer.
    parent: Overlay.overlay
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    width:  Math.min(920, (parent ? parent.width  : 900) - 60)
    height: Math.min(620, (parent ? parent.height : 600) - 60)
    x: parent ? (parent.width  - width)  / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0

    readonly property bool _saveMode: fileMode === FileChooser.SaveFile
    readonly property bool _dirMode:  fileMode === FileChooser.Directory
    //  Der gewählte Name (Datei ODER Unterordner) - leer heißt „der Ordner selbst".
    property string _chosenName: ""
    property int    _filterIndex: 0
    //  „Neuer Ordner": Eingabe läuft IN der Pfadleiste (s. dort), damit kein
    //  zweites Popup über dem Wähler liegt.
    property bool   _dirEditActive: false
    property string _dirError: ""
    //  Zeilen der Mehrfachauswahl (Indizes im Modell). `currentIndex` bleibt
    //  daneben bestehen - er führt die Tastatur.
    //  Orte der Seitenleiste: Standard-Orte PLUS die Lesezeichen der App - wer
    //  einen Ordner gespeichert hat, will ihn auch hier mit einem Klick
    //  erreichen. Doppelte und verschwundene Pfade fallen heraus.
    readonly property var _places: {
        App.savedFolders                       // Bindung an die Lesezeichen
        const out = fs.places()
        const seen = {}
        for (let i = 0; i < out.length; ++i) seen[out[i].path] = true
        const marks = App.savedFolders
        for (let k = 0; k < marks.length; ++k) {
            const m = marks[k]
            if (!m.path || seen[m.path]) continue
            if (!fs.dirExists(m.path)) continue
            out.push({ name: (m.name && m.name.length > 0)
                             ? m.name : fs.baseName(m.path),
                       path: m.path, bookmark: true })
            seen[m.path] = true
        }
        return out
    }

    property var    _picked: []
    property int    _anchorRow: -1
    function _isPicked(i) { return _picked.indexOf(i) >= 0 }
    function _clearPicked() { _picked = []; _anchorRow = -1 }
    //  Klick mit Umschalt/Strg - die Regeln, die jeder Dateiverwalter hat.
    function _pickRow(i, ctrl, shift) {
        if (!_multi || fs.entryIsDir(i)) { _clearPicked(); return }
        let out = _picked.slice()
        if (shift && _anchorRow >= 0) {
            out = []
            const a = Math.min(_anchorRow, i), b = Math.max(_anchorRow, i)
            for (let k = a; k <= b; ++k)
                if (!fs.entryIsDir(k)) out.push(k)
        } else if (ctrl) {
            const at = out.indexOf(i)
            if (at >= 0) out.splice(at, 1)
            else         out.push(i)
            _anchorRow = i
        } else {
            out = [ i ]
            _anchorRow = i
        }
        _picked = out
    }

    //  Anlegen und Ergebnis melden. Die Rückgabewerte kommen aus
    //  `FileBrowseModel::createFolder` (0 ok · 1 Name unbrauchbar · 2 gibt es
    //  schon · 3 fehlgeschlagen) - die Sprache wählt hier die Oberfläche.
    function _createFolder(name) {
        const rc = fs.createFolder(name)
        if (rc === 0) {
            _dirEditActive = false
            _dirError = ""
            //  Frisch angelegten Ordner gleich auswählen - man legt ihn an, um
            //  hineinzugehen oder darin zu speichern.
            _chosenName = name.trim()
            for (let i = 0; i < fs.count; ++i)
                if (fs.baseName(fs.entryPath(i)) === _chosenName) { list.currentIndex = i; break }
            return
        }
        _dirError = (rc === 2) ? App.uiText(App.language, "ChooserFolderExists")
                               : App.uiText(App.language, "ChooserFolderFailed")
    }

    FileBrowseModel {
        id: fs
        dirsOnly: root._dirMode
        //  Begleitdateien der App nur zeigen, wenn der Nutzer es will - dieselbe
        //  Einstellung wie in der Galerie, damit dieselbe Datei nicht hier
        //  auftaucht und dort fehlt.
        showAllFiles: App.showAllFiles
        nameFilters: (root._dirMode || root.nameFilters.length === 0)
                     ? [] : [ root.nameFilters[Math.min(root._filterIndex,
                                                        root.nameFilters.length - 1)] ]
    }

    //  Zwischen zwei Aufrufen soll der Wähler dort stehen, wo er zuletzt war -
    //  sonst fängt jedes Öffnen wieder im Heimatverzeichnis an.
    property string _lastFolder: ""

    onAboutToShow: {
        const want = currentFolder.toString().length > 0
                     ? fs.fromUrl(currentFolder)
                     : (_lastFolder.length > 0 ? _lastFolder : "")
        if (want.length > 0) fs.folder = want
        root._chosenName = ""
        nameField.text = ""
        list.currentIndex = -1
        root._clearPicked()
        //  Der Ordner-Modus braucht kein Namensfeld; sonst gehört der Fokus
        //  dorthin, damit man sofort tippen kann.
        if (!root._dirMode && root._saveMode) nameField.forceActiveFocus()
        else                                  list.forceActiveFocus()
    }
    onClosed: root._lastFolder = fs.folder

    // ── Ergebnis zusammensetzen und melden ───────────────────────────────────
    function _finish() {
        if (root._dirMode) {
            //  Im Ordner-Modus zählt der markierte UNTERordner, sonst der
            //  Ordner, in dem man gerade steht.
            const sel = (list.currentIndex >= 0)
                        ? fs.entryPath(list.currentIndex) : fs.folder
            root.selectedFolder = fs.toUrl(sel)
            root.selectedFile   = root.selectedFolder
            root.selectedFiles  = [ root.selectedFile ]
        } else if (root._multi && root._picked.length > 0) {
            //  MEHRFACHAUSWAHL: es zählen die markierten Zeilen, nicht das
            //  Namensfeld. Sonst käme eine gerade mit Strg abgewählte Datei
            //  über ihren stehengebliebenen Namen doch wieder ins Ergebnis.
            const rows = root._picked.slice().sort((a, b) => a - b)
            const urls = []
            for (let i = 0; i < rows.length; ++i)
                urls.push(fs.toUrl(fs.entryPath(rows[i])))
            if (urls.length === 0) return
            root.selectedFiles  = urls
            root.selectedFile   = urls[0]
            root.selectedFolder = fs.toUrl(fs.folder)
        } else {
            let name = nameField.text.trim()
            if (name.length === 0) name = root._chosenName
            if (name.length === 0) return                  // nichts gewählt
            if (root._saveMode) name = fs.withSuffix(name, root.defaultSuffix)
            const full = fs.join(fs.folder, name)
            if (fs.dirExists(full)) { fs.folder = full; nameField.text = ""; return }
            root.selectedFile   = fs.toUrl(full)
            root.selectedFolder = fs.toUrl(fs.folder)
            root.selectedFiles  = [ root.selectedFile ]
        }
        root._lastFolder = fs.folder
        root.close()
        root.accepted()
    }
    //  Eine Zeile aktivieren: Ordner betreten, Datei übernehmen.
    function _activate(row) {
        if (row < 0) return
        if (fs.entryIsDir(row)) {
            if (root._dirMode && list.currentIndex === row && false) return
            fs.folder = fs.entryPath(row)
            list.currentIndex = -1
            root._chosenName = ""
            root._clearPicked()
            nameField.text = ""
            return
        }
        root._chosenName = fs.baseName(fs.entryPath(row))
        nameField.text = root._chosenName
        root._finish()
    }

    //  Existiert die Zieldatei schon? Dann heißt der Knopf „Überschreiben".
    readonly property bool _willOverwrite: {
        if (!_saveMode) return false
        const n = nameField.text.trim()
        if (n.length === 0) return false
        return fs.fileExists(fs.join(fs.folder, fs.withSuffix(n, defaultSuffix)))
    }

    background: Rectangle {
        color: App.themeBackground
        border.color: App.themeBorder
        radius: 10
    }

    contentItem: Item {
        // ── Kopfzeile ────────────────────────────────────────────────────────
        Rectangle {
            id: head
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 44
            color: App.themeToolbarBg
            radius: 10
            //  Nur oben runden: ein zweites Rechteck deckt die untere Rundung.
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 12
                color: parent.color
            }
            Text {
                anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                text: root.title
                color: App.themeTextPrimary
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                width: parent.width - 60
            }
            //  Schließen.
            Rectangle {
                id: closeBtn
                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                width: 26; height: 26; radius: 5
                color: closeHover.hovered ? App.themeCard : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "✕"; color: App.themeTextMuted; font.pixelSize: 13
                }
                HoverHandler { id: closeHover }
                TapHandler { onTapped: { root.close(); root.rejected() } }
            }
        }

        // ── Orte-Leiste ──────────────────────────────────────────────────────
        Rectangle {
            id: places
            anchors { top: head.bottom; left: parent.left; bottom: foot.top }
            width: 176
            color: App.themeSidebarBg

            Text {
                id: placesTitle
                anchors { top: parent.top; left: parent.left; leftMargin: 12; topMargin: 10 }
                text: App.uiText(App.language, "ChooserPlaces")
                color: App.themeTextMuted
                font.pixelSize: 11
            }
            ListView {
                id: placesList
                anchors { top: placesTitle.bottom; topMargin: 6; left: parent.left
                          right: parent.right; bottom: parent.bottom; bottomMargin: 8 }
                clip: true
                //  Die Liste steht als benannte Eigenschaft am Wähler (s.
                //  `_places`) - sonst wäre sie nur ein Ausdruck an dieser Stelle
                //  und weder lesbar noch prüfbar.
                model: root._places
                interactive: false          // das Rad übernimmt SmoothWheelArea
                delegate: Rectangle {
                    required property var modelData
                    width: placesList.width
                    height: 30
                    color: placeHover.hovered ? App.themeCard : "transparent"
                    Text {
                        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                        width: parent.width - 20
                        elide: Text.ElideMiddle
                        text: modelData.name
                        color: App.themeTextPrimary
                        font.pixelSize: 12
                    }
                    HoverHandler { id: placeHover }
                    TapHandler {
                        onTapped: {
                            fs.folder = modelData.path
                            list.currentIndex = -1
                            root._chosenName = ""
                        }
                    }
                }
            }
            SmoothWheelArea { flickable: placesList }
        }

        // ── Pfadzeile ────────────────────────────────────────────────────────
        Rectangle {
            id: pathBar
            anchors { top: head.bottom; left: places.right; right: parent.right }
            height: 38
            color: App.themeFilterBarBg

            Rectangle {
                id: upBtn
                anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                width: 26; height: 26; radius: 5
                color: upHover.hovered && fs.canGoUp ? App.themeCard : "transparent"
                opacity: fs.canGoUp ? 1.0 : 0.35
                Text {
                    anchors.centerIn: parent
                    text: "↑"; font.pixelSize: 14
                    color: App.themeTextPrimary
                }
                HoverHandler { id: upHover }
                TapHandler {
                    onTapped: {
                        if (fs.canGoUp) {
                            fs.cdUp(); list.currentIndex = -1; root._clearPicked()
                        }
                    }
                }
                ToolTip.visible: upHover.hovered
                ToolTip.text: App.uiText(App.language, "ChooserUp")
            }
            //  ── Neuer Ordner ────────────────────────────────────────────
            //  Angelegt wird IM aktuellen Verzeichnis; die Eingabe ersetzt so
            //  lange die Brotkrumen, statt ein zweites Popup über das erste zu
            //  legen (verschachtelte Popups fangen sich gegenseitig den Fokus).
            Rectangle {
                id: newDirBtn
                anchors { right: parent.right; rightMargin: 8
                          verticalCenter: parent.verticalCenter }
                width: 26; height: 26; radius: 5
                visible: !root._dirEditActive
                color: newDirHover.hovered ? App.themeCard : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "\uFF0B"                       // Vollbreites Plus
                    font.pixelSize: 13
                    color: App.themeTextPrimary
                }
                HoverHandler { id: newDirHover }
                TapHandler {
                    onTapped: {
                        root._dirError = ""
                        root._dirEditActive = true
                        newDirField.text = ""
                        newDirField.forceActiveFocus()
                    }
                }
                ToolTip.visible: newDirHover.hovered
                ToolTip.delay: 500
                ToolTip.text: App.uiText(App.language, "ChooserNewFolder")
            }
            Row {
                id: newDirRow
                anchors { left: upBtn.right; leftMargin: 6; right: parent.right
                          rightMargin: 8; verticalCenter: parent.verticalCenter }
                visible: root._dirEditActive
                spacing: 6
                TextField {
                    id: newDirField
                    width: Math.max(140, newDirRow.width - 150)
                    height: 26
                    font.pixelSize: 12
                    placeholderText: App.uiText(App.language, "ChooserNewFolder")
                    onAccepted: root._createFolder(text)
                    Keys.onEscapePressed: root._dirEditActive = false
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root._dirError
                    color: App.themeAccent
                    font.pixelSize: 11
                    visible: root._dirError.length > 0
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26; height: 26; radius: 5
                    color: okHover.hovered ? App.themeCard : "transparent"
                    Text { anchors.centerIn: parent; text: "\u2713"
                           font.pixelSize: 13; color: App.themeTextPrimary }
                    HoverHandler { id: okHover }
                    TapHandler { onTapped: root._createFolder(newDirField.text) }
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26; height: 26; radius: 5
                    color: cancelHover.hovered ? App.themeCard : "transparent"
                    Text { anchors.centerIn: parent; text: "\u2715"
                           font.pixelSize: 12; color: App.themeTextMuted }
                    HoverHandler { id: cancelHover }
                    TapHandler { onTapped: root._dirEditActive = false }
                }
            }
            //  Brotkrumen: jedes Segment ist anklickbar.
            Flickable {
                id: crumbFlick
                visible: !root._dirEditActive
                anchors { left: upBtn.right; leftMargin: 6; right: newDirBtn.left
                          rightMargin: 6; verticalCenter: parent.verticalCenter }
                height: 26
                contentWidth: crumbRow.width
                clip: true
                interactive: true
                //  Immer das ENDE zeigen - dort steht der aktuelle Ordner.
                onContentWidthChanged: contentX = Math.max(0, contentWidth - width)
                Row {
                    id: crumbRow
                    spacing: 2
                    Repeater {
                        model: { fs.folder; return fs.crumbs() }
                        delegate: Row {
                            required property var modelData
                            required property int index
                            spacing: 2
                            Text {
                                visible: index > 0
                                anchors.verticalCenter: parent.verticalCenter
                                text: "›"; color: App.themeTextMuted; font.pixelSize: 12
                            }
                            Rectangle {
                                height: 24
                                width: crumbText.implicitWidth + 12
                                radius: 4
                                color: crumbHover.hovered ? App.themeCard : "transparent"
                                Text {
                                    id: crumbText
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    color: App.themeTextPrimary
                                    font.pixelSize: 12
                                }
                                HoverHandler { id: crumbHover }
                                TapHandler {
                                    onTapped: {
                                        fs.folder = modelData.path
                                        list.currentIndex = -1
                                        root._chosenName = ""
                                        root._clearPicked()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Dateiliste ───────────────────────────────────────────────────────
        Rectangle {
            id: listBox
            anchors { top: pathBar.bottom; left: places.right; right: parent.right
                      bottom: foot.top }
            color: App.themeBackground

            //  ── Spaltenköpfe: klicken sortiert, nochmal klicken dreht um ──
            //  Ordner bleiben in jeder Richtung vorn (das macht das Modell) -
            //  sonst müsste man zum Hochgehen erst durch alle Dateien scrollen.
            Rectangle {
                id: colHead
                anchors { top: parent.top; left: parent.left; right: parent.right
                          margins: 1 }
                height: 24
                color: App.themeFilterBarBg
                Repeater {
                    model: [ { key: 0, label: "ChooserColName" },
                             { key: 1, label: "ChooserColSize" },
                             { key: 2, label: "ChooserColDate" } ]
                    delegate: Item {
                        required property var modelData
                        readonly property bool active: fs.sortKey === modelData.key
                        //  Gleiche Spaltenkanten wie die Zeilen darunter.
                        x: modelData.key === 0 ? 10
                           : (modelData.key === 1 ? colHead.width - 224
                                                  : colHead.width - 120)
                        width: modelData.key === 0 ? colHead.width - 240 : 108
                        height: colHead.height
                        Text {
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                            text: App.uiText(App.language, modelData.label)
                                  + (parent.active ? (fs.sortDescending ? "  ▾" : "  ▴") : "")
                            color: parent.active ? App.themeTextPrimary : App.themeTextMuted
                            font.pixelSize: 11
                            font.bold: parent.active
                        }
                        HoverHandler { id: colHover }
                        TapHandler {
                            onTapped: {
                                if (fs.sortKey === modelData.key)
                                    fs.sortDescending = !fs.sortDescending
                                else {
                                    fs.sortKey = modelData.key
                                    fs.sortDescending = false
                                }
                                list.currentIndex = -1
                            }
                        }
                    }
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: App.themeBorder
                }
            }

            ListView {
                id: list
                anchors { top: colHead.bottom; left: parent.left; right: parent.right
                          bottom: parent.bottom; margins: 1 }
                clip: true
                model: fs
                interactive: false          // s. SmoothWheelArea
                currentIndex: -1
                highlightMoveDuration: 0
                keyNavigationEnabled: true

                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string name
                    required property bool isDir
                    required property string sizeText
                    required property string dateText
                    width: list.width
                    height: 28
                    color: (list.currentIndex === index || root._isPicked(index))
                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                     App.themeAccent.b, 0.28)
                           : (rowHover.hovered ? App.themeCard : "transparent")

                    Text {
                        id: icon
                        anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                        text: row.isDir ? "🗀" : "🗎"
                        font.pixelSize: 13
                        color: row.isDir ? App.themeAccent : App.themeTextMuted
                    }
                    Text {
                        anchors { left: icon.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        width: parent.width - 230
                        elide: Text.ElideMiddle
                        text: row.name
                        color: App.themeTextPrimary
                        font.pixelSize: 12
                    }
                    Text {
                        anchors { right: dateT.left; rightMargin: 14; verticalCenter: parent.verticalCenter }
                        text: row.sizeText
                        color: App.themeTextMuted
                        font.pixelSize: 11
                    }
                    Text {
                        id: dateT
                        anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        text: row.dateText
                        color: App.themeTextMuted
                        font.pixelSize: 11
                    }
                    HoverHandler { id: rowHover }
                    TapHandler {
                        onTapped: (eventPoint, button) => {
                            list.currentIndex = row.index
                            root._pickRow(row.index,
                                          (point.modifiers & Qt.ControlModifier) !== 0,
                                          (point.modifiers & Qt.ShiftModifier) !== 0)
                            if (!row.isDir) {
                                root._chosenName = row.name
                                nameField.text = row.name
                            }
                        }
                        onDoubleTapped: root._activate(row.index)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: fs.count === 0 && !fs.loading
                    text: App.uiText(App.language, "ChooserEmpty")
                    color: App.themeTextMuted
                    font.pixelSize: 12
                }
                BusyIndicator {
                    anchors.centerIn: parent
                    running: fs.loading && fs.count === 0
                }
                ScrollBar.vertical: ScrollBar { }

                //  Tastatur: ↑/↓ wählen, Eingabe aktiviert, Rücktaste geht hoch.
                Keys.onReturnPressed: root._activate(list.currentIndex)
                Keys.onEnterPressed:  root._activate(list.currentIndex)
                Keys.onBackPressed:   if (fs.canGoUp) fs.cdUp()
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && !fs.entryIsDir(currentIndex)) {
                        root._chosenName = fs.baseName(fs.entryPath(currentIndex))
                        if (!root._saveMode) nameField.text = root._chosenName
                    }
                }
            }
            SmoothWheelArea { flickable: list }
        }

        // ── Fußzeile: Name, Filter, Knöpfe ───────────────────────────────────
        Rectangle {
            id: foot
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: root._dirMode ? 52 : 88
            color: App.themeToolbarBg
            radius: 10
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 12
                color: parent.color
            }

            Text {
                id: nameLbl
                visible: !root._dirMode
                anchors { left: parent.left; leftMargin: 14; top: parent.top; topMargin: 14 }
                text: App.uiText(App.language, "ChooserName")
                color: App.themeTextMuted
                font.pixelSize: 11
            }
            TextField {
                id: nameField
                visible: !root._dirMode
                anchors { left: nameLbl.right; leftMargin: 10; right: filterBox.left
                          rightMargin: 10; verticalCenter: nameLbl.verticalCenter }
                height: 28
                font.pixelSize: 12
                onAccepted: root._finish()
            }
            ComboBox {
                id: filterBox
                visible: !root._dirMode && root.nameFilters.length > 1
                anchors { right: parent.right; rightMargin: 14
                          verticalCenter: nameLbl.verticalCenter }
                width: 230
                height: 28
                font.pixelSize: 12
                model: root.nameFilters
                currentIndex: root._filterIndex
                onActivated: root._filterIndex = currentIndex
            }

            CheckBox {
                id: hiddenBox
                anchors { left: parent.left; leftMargin: 10; bottom: parent.bottom
                          bottomMargin: 8 }
                text: App.uiText(App.language, "ChooserShowHidden")
                font.pixelSize: 11
                checked: fs.showHidden
                onToggled: fs.showHidden = checked
            }

            Button {
                id: cancelBtn
                anchors { right: okBtn.left; rightMargin: 8; bottom: parent.bottom
                          bottomMargin: 8 }
                text: App.uiText(App.language, "SettingsCancel")
                onClicked: { root.close(); root.rejected() }
            }
            Button {
                id: okBtn
                anchors { right: parent.right; rightMargin: 14; bottom: parent.bottom
                          bottomMargin: 8 }
                text: root._dirMode ? App.uiText(App.language, "ChooserChoose")
                     : root._saveMode ? (root._willOverwrite
                                         ? App.uiText(App.language, "ChooserOverwrite")
                                         : App.uiText(App.language, "ChooserSave"))
                                      : App.uiText(App.language, "ChooserOpen")
                enabled: root._dirMode || nameField.text.trim().length > 0
                         || root._chosenName.length > 0
                onClicked: root._finish()
            }
        }
    }
}
