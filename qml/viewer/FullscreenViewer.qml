pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"
import "../docx"
import "../image"
import "../pdf"
import "../tags"

// Vollbild-Anzeige-Verbund: Dispatch nach Medientyp über EINEN Loader, also ist nur das aktive Medium
// dekodiert. Navigation ausschließlich über die Proxy-Reihenfolge - keine eigene Liste.
FocusScope {
    id: root
    focus: true

    signal backRequested()
    signal addFileRequested()

    signal paneDragStarted()
    signal paneDragMoved(real x, real y)
    signal paneDragEnded(real x, real y)
    signal paneDragCanceled()

    // `splitActive` = mehr als eine Datei offen: die Kopfleiste wird zur Ziehfläche fürs Docking, das immersive
    // Vollbild (F) entfällt. `canAddMore` steuert die Sichtbarkeit des "+"-Knopfes.
    property bool splitActive: false
    property bool canAddMore: true

    // Immersives Vollbild (F): keine Kachel-Chrome mehr, übrig bleibt allein die Steuerleiste der Surface. Der
    // Preis ist gewollt - die Kopfleiste ist zugleich die Ziehfläche fürs Docking, ohne sie lässt sich eine Kachel
    // nicht umsortieren; F/Esc holt sie zurück. `optionsVisible` gilt je HÄLFTE, nicht appweit.
    property bool optionsVisible: App.optionsVisible

    property bool immersive: false
    readonly property bool immersiveCapable: root.path.length > 0
    readonly property bool immersiveActive: root.immersive
    signal immersiveToggleRequested()

    function mediaPositionMs() {
        return (surface.item && surface.item.playbackPositionMs !== undefined)
               ? surface.item.playbackPositionMs : -1
    }
    function mediaRunning() {
        return !!(surface.item && surface.item.playbackRunning === true)
    }
    function pauseMedia() {
        if (surface.item && typeof surface.item.pausePlayback === "function")
            surface.item.pausePlayback()
    }

    // Alle fensterweiten Kürzel dieser Kachel hängen an `paneActive`: ohne die Bindung definieren bei 2-4 offenen
    // Kacheln ALLE dieselben `Qt.WindowShortcut`-Sequenzen - Qt meldet Mehrdeutigkeit und feuert KEINES.
    property bool paneActive: true
    signal paneActivated()

    property string startPath: ""

    property int    currentRow: -1
    property string path: ""
    //  Typtabelle wie `MediaItem.h` ▸ `MediaType`. Der frühere Kommentar
    //  nannte 5 „Unknown" - das stimmte seit dem DOCX-Editor nicht mehr
    //  und hätte beim nächsten Lesen in die Irre geführt.
    property int    type: 5          // 0 Image,1 Video,2 Audio,3 Pdf,4 Text/HTML,5 Docx
    property string displayName: ""
    property var    tags: []
    property var    dateTime
    property bool   randomNext: false

    property bool _htmlPreview: true
    function _isHtmlPath(p) {
        var dot = p.lastIndexOf(".")
        if (dot < 0) return false
        var ext = p.substring(dot + 1).toLowerCase()
        return ext === "html" || ext === "htm"
    }
    readonly property bool _isWebRenderable: root.type === 4 && root._isHtmlPath(root.path)

    //  DATEV-Buchungsstapel: erkannt am INHALT, nicht an der Endung - dieselbe
    //  Datei kommt als .csv UND als .txt. Einmal je Pfad gelesen, nicht je Bild.
    property bool _isDatev: false
    property bool _datevTable: true
    readonly property bool _showDatevTable: root._isDatev && root._datevTable

    //  Gewoehnliche Tabellendatei (.csv/.tsv). Dieselbe Mechanik, aber eine
    //  andere Flaeche: die DATEV-Ansicht DEUTET (Dateikopf, Summen, nie
    //  schreiben), die Tabellenansicht zeigt nur, was dasteht.
    property bool _isTable: false
    //  Zeilen- und Spaltennummern in der Tabelle. Gehoert dem Viewer, nicht der
    //  Flaeche: der Schalter sitzt in der oberen Leiste und muss den Wechsel
    //  DATEV <-> Tabelle ueberdauern.
    property bool _tableNumbers: false

    //  Eine Tabellendatei zeigt BEIDES: die Tabelle im `surface`-Loader und den
    //  Rohtext in einem eigenen daneben. Der Rohtext entsteht erst beim ersten
    //  Hinschauen und BLEIBT dann stehen - wuerde er beim Umschalten zerstoert,
    //  waere mit ihm die Undo-Historie weg (Strg+Z nach einem Blick in die
    //  Tabelle tat nichts mehr).
    property bool _rohtextGesehen: false
    //  An der Eigenschaft selbst, nicht in den Knopf-Handlern: `_datevTable`
    //  wird auch aus dem Menue und aus Pruefstaenden gesetzt.
    on_DatevTableChanged: if (!root._datevTable) root._rohtextGesehen = true
    readonly property bool _rohtextAktiv: (root._isDatev || root._isTable)
                                          && !root._datevTable
    readonly property bool _showTable: root._isTable && !root._isDatev && root._datevTable
    // WebEngine ist LAZY (WebEngineController): die Vorschau existiert nur,
    // wenn WebEngine.ready - vorher fällt HTML IMMER auf TextSurface zurück
    // und es wird garantiert keine WebEngineView instanziiert.
    readonly property bool _showWebPreview:  root._isWebRenderable && root._htmlPreview
                                             && WebEngine.ready

    // Lade-Gating: Die schwere Medien-/PDF-Last erst NACH dem StackView-Übergang
    // anstoßen (Status Active) -> die Öffnen-Animation läuft flüssig über einen
    // leichten Platzhalter statt gegen das synchrone PDF-Laden/Erstrendern.
    property bool   _loaded: false
    property int    _startType: -1   // Medientyp des Einstiegspfads (schon VOR _loaded bekannt)

    // PDF (3) und Bild (0) tragen beide einen Editor mit derselben Track-Changes-API - der Knopf ist deshalb für
    // beide derselbe.
    readonly property var _trackCtl: ((root.type === 3 || root.type === 0) && surface.item
                                      && surface.item.editCtl !== undefined)
                                     ? surface.item.editCtl : null
    readonly property var _pdfCtl: (root.type === 3 && surface.item
                                    && surface.item.editCtl !== undefined)
                                   ? surface.item.editCtl : null
    readonly property bool _pdfReady: root._loaded && surface.item !== null && surface.item.docReady === true
    readonly property bool pdfLoading: (root._loaded ? root.type : root._startType) === 3 && !root._pdfReady

    property bool _skelVisible: false
    Timer { id: skelDelay; interval: 300; repeat: false
            onTriggered: root._skelVisible = root.pdfLoading }
    onPdfLoadingChanged: {
        if (root.pdfLoading) { root._skelVisible = false; skelDelay.restart() }   // (neu) am Laden -> 300ms warten
        else { skelDelay.stop(); root._skelVisible = false }                       // fertig / kein PDF -> weg
    }

    function _maybeLoad() {
        if (root._loaded) return
        if (StackView.status === StackView.Active || StackView.view === null) {
            loadPath(startPath)       // setzt type/path …; surface noch inaktiv
            root._loaded = true       // aktiviert den Surface-Loader (onItemChanged setzt source)
            root.forceActiveFocus()
        }
    }

    Component.onCompleted: {
        var r = galleryModel.rowForPath(startPath)
        root._startType = (r >= 0) ? galleryModel.mediaTypeAt(r) : -1
        root.forceActiveFocus(); _maybeLoad()
    }
    Component.onDestruction: releaseCurrent()
    StackView.onStatusChanged: _maybeLoad()

    function loadPath(p) {
        var r = galleryModel.rowForPath(p)
        if (r < 0) r = galleryModel.count > 0 ? 0 : -1
        loadRow(r)
    }

    function loadRow(r) {
        if (r < 0 || r >= galleryModel.count) { root.backRequested(); return }
        releaseCurrent()
        currentRow  = r
        path        = galleryModel.filePathAt(r)
        type        = galleryModel.mediaTypeAt(r)
        displayName = galleryModel.displayNameAt(r)
        tags        = galleryModel.tagsAt(r)
        dateTime    = galleryModel.dateTimeAt(r)
        //  NACH `type`, nicht in `onPathChanged`: `path` wird oben zuerst
        //  gesetzt, dort staende noch der Typ der vorigen Datei.
        root._isDatev = (type === 4) && Viewer.isDatevFile(path)
        root._isTable = (type === 4) && !root._isDatev && Viewer.isTableFile(path)
        //  Neue Datei, neue Historie: der Rohtext wird erst wieder erzeugt,
        //  wenn jemand hinschaut.
        root._rohtextGesehen = false

        if (type === 1 && App.videoPlayback === "external") {
            Viewer.openExternally(path)
        }

        // Trigger der lazy WebEngine-Initialisierung; der Aufruf ist synchron und idempotent - danach ist
        // `WebEngine.ready` und der Loader wählt die gerenderte Vorschau statt des Quelltexts.
        if (root._isWebRenderable)
            WebEngine.ensureInitializedForHtml()

        // Inhalt explizit nachziehen: bei Navigation zwischen Medien GLEICHEN Typs bleibt das Loader-Item dasselbe
        // (`onItemChanged` feuert nicht), der alte Inhalt bliebe sonst stehen.
        if (surface.item && surface.item.hasOwnProperty("source"))
            surface.item.source = path
        if (rohtext.item) rohtext.item.source = path
    }

    readonly property var _textCtl: {
        if (rohtext.item && rohtext.visible) return rohtext.item
        return (surface.item && surface.item.exportPdf !== undefined) ? surface.item : null
    }

    function releaseCurrent() {
        if (surface.item && surface.item.release)
            surface.item.release()
        if (rohtext.item && rohtext.item.release)
            rohtext.item.release()
    }

    // Geblättert wird INNERHALB des Ordners, aus dem die offene Datei stammt: seit aufgeklappte Unterordner in
    // derselben Liste stehen, wäre "nächste Zeile" sonst ein Sprung über die Ordnergrenze. -1 = nichts anzusteuern.
    function nextRow() {
        if (galleryModel.count === 0) return
        if (randomNext) {
            var rnd = galleryModel.randomRow(currentRow)
            if (rnd >= 0) loadRow(rnd)
            return
        }
        var n = galleryModel.stepRow(currentRow, 1)
        if (n >= 0) loadRow(n)
    }
    function prevRow() {
        if (galleryModel.count === 0) return
        var p = galleryModel.stepRow(currentRow, -1)
        if (p >= 0) loadRow(p)
    }

    Rectangle { anchors.fill: parent; color: "#0a0a0a" }

    TapHandler {
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPressedChanged: if (pressed) root.paneActivated()
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: !root._loaded && root._startType !== 3
        visible: running
        z: 50
    }

    // Sofort eine leere weiße Seite statt Blackscreen; dauert das Laden länger als 300 ms, blenden Balken und
    // Shimmer darüber ein. Größe, Position und Seitenweiß spiegeln die spätere PDF-Seite.
    Item {
        id: pdfSkeleton
        anchors.fill: parent
        z: 50
        visible: opacity > 0.01
        opacity: root.pdfLoading ? 1 : 0          // leere Seite SOFORT beim Laden; weg, sobald fertig

        readonly property real _vpTop:    (topBar.visible ? topBar.height : 0) + 40
        readonly property real _vpBottom: root.height
        readonly property real _vpW:      root.width
        readonly property real _vpH:      Math.max(0, _vpBottom - _vpTop)
        readonly property real _fit:      Math.max(0, Math.min((_vpW - 24) / 595, (_vpH - 24) / 842))

        Rectangle {
            id: skelPage
            width:  595 * pdfSkeleton._fit
            height: 842 * pdfSkeleton._fit
            x: (parent.width - width) / 2
            y: pdfSkeleton._vpTop
            color: "#fbfbfa"
            clip: true

            Item {
                id: skelContent
                anchors.fill: parent
                opacity: root._skelVisible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutQuad } }

                Column {
                    id: skelLines
                    anchors {
                        left: parent.left;  leftMargin:  skelPage.width * 0.11
                        right: parent.right; rightMargin: skelPage.width * 0.11
                        top: parent.top;     topMargin:   skelPage.height * 0.11
                    }
                    spacing: skelPage.height * 0.0265

                    Rectangle { width: parent.width * 0.58; height: skelPage.height * 0.040
                                radius: 4; color: "#d9d9d4" }
                    Item { width: 1; height: skelPage.height * 0.028 }   // Absatzabstand

                    Repeater {
                        model: [0.99, 0.95, 0.98, 0.72,
                                0.97, 0.93, 0.99, 0.90, 0.58,
                                0.96, 0.99, 0.91, 0.66,
                                0.98, 0.94, 0.80]
                        Rectangle {
                            //  `pragma ComponentBehavior: Bound` (Zeile 1) verlangt die
                            //  ausdrückliche Deklaration - ohne sie blieb `modelData`
                            //  undefiniert (ReferenceError, Balkenbreite NaN).
                            required property real modelData
                            width: parent.width * modelData
                            height: skelPage.height * 0.0215
                            radius: 3
                            color: "#e5e5e1"
                        }
                    }
                }

                Rectangle {
                    id: skelSheen
                    height: skelPage.height * 1.5
                    width:  skelPage.width * 0.42
                    y: -skelPage.height * 0.25
                    rotation: 14
                    opacity: 0.9
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.55) }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                    SequentialAnimation on x {
                        running: root._skelVisible
                        loops: Animation.Infinite
                        NumberAnimation { from: -skelPage.width * 0.6; to: skelPage.width * 1.15
                                          duration: 1150; easing.type: Easing.InOutQuad }
                        PauseAnimation { duration: 360 }
                    }
                }
            }
        }
    }

    Loader {
        id: surface
        anchors.fill: parent
        active: root._loaded
        // URL statt Typname - der entscheidende Unterschied für den Start: ein Typname (`PdfSurface {}`) zwingt QML,
        // alle Flächen samt Kindern schon beim Übersetzen zu laden (gemessen rund 100 ms je Start).
        source: {
            switch (root.type) {
            case 0:  return "qrc:/qml/image/ImageSurface.qml"
            case 1:  return (App.videoPlayback === "external")
                            ? "qrc:/qml/viewer/ViewerNote.qml"
                            : "qrc:/qml/viewer/VideoSurface.qml"
            case 2:  return "qrc:/qml/viewer/VideoSurface.qml"   // Audio: VideoSurface mit Audio-Out
            case 3:  return "qrc:/qml/pdf/PdfSurface.qml"
            case 4:  return root._isDatev          ? "qrc:/qml/datev/DatevSurface.qml"
                     : root._isTable         ? "qrc:/qml/table/TableSurface.qml"
                     : root._showWebPreview  ? "qrc:/qml/viewer/HtmlHost.qml"
                                             : "qrc:/qml/viewer/TextSurface.qml"
            case 5:  return "qrc:/qml/docx/DocxSurface.qml"      // Word-Dokumente (DOCX-Editor)
            default: return "qrc:/qml/viewer/ViewerNote.qml"
            }
        }
        visible: !root._rohtextAktiv
        onItemChanged: {
            if (!item) return
            if (item.hasOwnProperty("kind"))
                item.kind = (root.type === 1) ? "external" : "unsupported"
            if (item.hasOwnProperty("source")) item.source = root.path
        }
    }

    //  Der Rohtext einer Tabellendatei. Lazy erzeugt (erst beim ersten
    //  Umschalten), danach dauerhaft - genau das haelt die Undo-Historie.
    Loader {
        id: rohtext
        anchors.fill: parent
        active: (root._isDatev || root._isTable) && root._rohtextGesehen
        visible: root._rohtextAktiv
        source: "qrc:/qml/viewer/TextSurface.qml"
        onItemChanged: { if (item) item.source = root.path }
    }

    // Reserviert die Höhe der oberen Leiste in der Surface, damit deren eigene Toolbar nicht mit der globalen
    // Chrome überlappt. Nur noch `topInset`; `bottomInset` bleibt Teil des Surface-Vertrags (0).
    Binding {
        target: surface.item
        property: "topInset"
        value: (topBar.visible && !root.immersiveActive) ? topBar.height : 0
        when: surface.item !== null && (root.type === 0 || root.type === 3 || root.type === 4
                                         || root.type === 5)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: rohtext.item
        property: "topInset"
        value: (topBar.visible && !root.immersiveActive) ? topBar.height : 0
        when: rohtext.item !== null
        restoreMode: Binding.RestoreNone
    }

    //  Nur die beiden Tabellen-Flaechen kennen `showNumbers` - `when` haelt die
    //  Bindung von allen anderen fern.
    Binding {
        target: surface.item
        property: "showNumbers"
        value: root._tableNumbers
        when: surface.item !== null && (root._showDatevTable || root._showTable)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: surface.item
        property: "paneActive"
        value: root.paneActive
        when: surface.item !== null && (root.type === 0 || root.type === 3 || root.type === 5)
        restoreMode: Binding.RestoreNone
    }


    Rectangle {
        id: topBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: root.optionsVisible ? 67 : 36
        color: Qt.rgba(0, 0, 0, 0.55)
        opacity: root.barOpacity
        visible: opacity > 0.01 && (!root.immersiveActive || root._barPeek)
        Behavior on opacity { NumberAnimation { duration: 180 } }

        // Erstes Kind der Leiste, also UNTER den Bedienelementen: Knöpfe und das aktive Namensfeld behalten ihre
        // Klicks, freie Bereiche starten den Kachel-Zug - erst nach einer Bewegungsschwelle, ein Klick bleibt folgenlos.
        MouseArea {
            id: paneDragArea
            anchors.fill: parent
            enabled: root.splitActive
            preventStealing: true
            cursorShape: dragging ? Qt.ClosedHandCursor
                       : (root.splitActive ? Qt.OpenHandCursor : Qt.ArrowCursor)
            property bool  dragging: false
            property point pressPos: Qt.point(0, 0)
            onPressed: (m) => { pressPos = Qt.point(m.x, m.y); dragging = false }
            onPositionChanged: (m) => {
                if (!pressed) return
                if (!dragging
                        && Math.abs(m.x - pressPos.x) + Math.abs(m.y - pressPos.y) > 10) {
                    dragging = true
                    root.paneDragStarted()
                }
                if (dragging) {
                    var p = mapToItem(root, m.x, m.y)
                    root.paneDragMoved(p.x, p.y)
                }
            }
            onReleased: (m) => {
                if (dragging) {
                    var p = mapToItem(root, m.x, m.y)
                    root.paneDragEnded(p.x, p.y)
                }
                dragging = false
            }
            onCanceled: { if (dragging) root.paneDragCanceled(); dragging = false }
        }

        Column {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.topMargin: 3
            anchors.bottomMargin: 3
            spacing: 6

            Item {
                width: parent.width
                height: 30

                ScrollableBar {
                    id: headRightBar
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    width: Math.min(contentWidth, parent.width * 0.5)
                    spacing: 6

                ChromeBtn {
                    id: addBtn
                    visible: root.canAddMore
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "addfile"
                    tip: App.uiText(App.language, "SplitAddFile")
                    onActivated: root.addFileRequested()
                }

                ChromeBtn {
                    id: mapBtn
                    visible: root._textCtl !== null
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "toc"
                    active: Editor.minimap
                    tip: App.uiText(App.language, "EditorMinimapTip")
                    onActivated: Editor.minimap = !Editor.minimap
                }
                TranslitButton {
                    id: translitBtn
                    visible: root._textCtl !== null
                    anchors.verticalCenter: parent.verticalCenter
                }

                ChromeBtn {
                    id: previewBtn
                    visible: root._isWebRenderable && WebEngine.ready
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "html"
                    active: root._htmlPreview
                    tip: root._htmlPreview ? App.uiText(App.language, "ViewerShowSource")
                                           : App.uiText(App.language, "ViewerShowPreview")
                    onActivated: {
                        root.releaseCurrent()
                        root._htmlPreview = !root._htmlPreview
                    }
                }
                ChromeBtn {
                    id: numbersBtn
                    //  Nur, solange wirklich eine Tabelle dasteht - im Rohtext
                    //  gaebe es nichts zu nummerieren.
                    visible: root._showDatevTable || root._showTable
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "gridhead"
                    active: root._tableNumbers
                    tip: App.uiText(App.language, "TableNumbersTip")
                    onActivated: root._tableNumbers = !root._tableNumbers
                }
                ChromeBtn {
                    id: diceBtn
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.type !== 4 && root.type !== 5
                    kind: "dice"
                    tip: App.uiText(App.language, "ViewerRandom")
                    active: root.randomNext
                    onActivated: root.randomNext = !root.randomNext
                }
                //  GANZ RECHTS und damit an derselben Stelle, egal welche
                //  Knoepfe daneben gerade sichtbar sind: er ist der eine,
                //  den man in JEDEM Zustand wiederfinden muss.
                ChromeBtn {
                    id: datevBtn
                    visible: root._isDatev || root._isTable
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "table"
                    active: root._datevTable
                    tip: root._datevTable ? App.uiText(App.language, "DatevShowSource")
                                          : App.uiText(App.language, "DatevShowTable")
                    //  KEIN `releaseCurrent()`: hier wird nichts ersetzt, beide
                    //  Flaechen bleiben stehen. Ein Release loeschte den Text und
                    //  mit ihm die Undo-Historie.
                    onActivated: root._datevTable = !root._datevTable
                }
                }   // Ende headRightBar

                // Alles Linke steht in EINER blätterbaren Leiste (Strg + Rad): sie endet vor der rechten Knopfgruppe und kann
                // nie darüber laufen. Vorher hingen die Knöpfe in einer Kette und das Namensfeld spannte bis zur rechten
                // Gruppe - im schmalen Fenster wurde seine Breite negativ und die Gruppen überlappten.
                ScrollableBar {
                    id: headLeftBar
                    anchors.left: parent.left
                    anchors.right: headRightBar.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    spacing: 6

                ToolButton {
                    id: backBtn
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: 32; implicitHeight: 26
                    padding: 0
                    onClicked: root.backRequested()
                    contentItem: Item {
                        DrawnIcon {
                            anchors.centerIn: parent
                            name: "chevron-left"
                            size: 16
                            color: "white"
                        }
                    }
                }

                Rectangle {
                    id: viewBtn
                    anchors.verticalCenter: parent.verticalCenter
                    width: viewLbl.implicitWidth + 22; height: 26; radius: 6
                    color: viewMenu.opened ? Qt.rgba(1, 1, 1, 0.18)
                         : (viewMenuHover.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                    border.width: 1
                    border.color: viewMenu.opened ? Qt.rgba(1, 1, 1, 0.35)
                                                  : Qt.rgba(1, 1, 1, 0.18)

                    Row {
                        id: viewLbl
                        anchors.centerIn: parent
                        spacing: 5
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: App.uiText(App.language, "MenuDocument")
                            color: "white"; font.pixelSize: 12
                        }
                        DrawnIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron-down"; size: 10; color: "white"
                        }
                    }
                    HoverHandler { id: viewMenuHover }
                    TapHandler {
                        onTapped: viewMenu.popup(viewBtn, 0, viewBtn.height + 2)
                    }

                    ThemedMenu {
                        id: viewMenu
                        objectName: "documentMenu"

                        MenuItem {
                            text: App.uiText(App.language, "MetaTitle")
                            onTriggered: dateEditor.openWith(root.dateTime)
                        }
                        MenuItem {
                            visible: root._isWebRenderable && WebEngine.ready
                            height: visible ? implicitHeight : 0
                            text: root._htmlPreview
                                  ? App.uiText(App.language, "ViewerShowSource")
                                  : App.uiText(App.language, "ViewerShowPreview")
                            onTriggered: {
                                root.releaseCurrent()
                                root._htmlPreview = !root._htmlPreview
                            }
                        }
                        MenuItem {
                            visible: root._isDatev || root._isTable
                            height: visible ? implicitHeight : 0
                            text: root._datevTable
                                  ? App.uiText(App.language, "DatevShowSource")
                                  : App.uiText(App.language, "DatevShowTable")
                            onTriggered: root._datevTable = !root._datevTable
                        }
                        MenuItem {
                            visible: root.type === 1 && Audio.canExtractAudio(root.path)
                            height: visible ? implicitHeight : 0
                            enabled: !Audio.extractBusy
                            text: App.uiText(App.language, "AudioExtractMenu")
                            onTriggered: Audio.extractAudio(root.path)
                        }
                        // KEINE `MenuItem`s: die schließen ihr Menü beim Klick von selbst, hier soll es stehen bleiben, während das
                        // kleine Fenster daneben aufgeht. Der frühere Weg (schließen und sofort wieder öffnen) flackerte sichtbar.
                        DocMenuRow {
                            visible: root.type === 5
                            label: App.uiText(App.language, "DocxPdfNumberMenu")
                            onActivated: root._openDocPopup(pageNumPopup, this)
                        }
                        DocMenuRow {
                            visible: root.type === 5
                            label: App.uiText(App.language, "DocxPdfNumberStyleHead")
                            onActivated: root._openDocPopup(pageStylePopup, this)
                        }

                        DocMenuRow {
                            visible: root._textCtl !== null
                            label: App.uiText(App.language, "TextPdfMenu")
                            onActivated: root._openDocPopup(textPdfPopup, this)
                        }

                        MenuItem {
                            visible: root._pdfCtl !== null && root._pdfCtl.ocrAvailable
                            height: visible ? implicitHeight : 0
                            enabled: root._pdfCtl !== null
                                     && !root._pdfCtl.searchableBusy
                                     && !root._pdfCtl.alreadySearchable
                            text: App.uiText(App.language, "PdfSearchableMenu")
                            onTriggered: root._pdfCtl.makeSearchable()
                        }

                        MenuSeparator { }
                        MenuItem {
                            visible: root._trackCtl !== null
                            height: visible ? implicitHeight : 0
                            text: App.uiText(App.language, "CtxRemoveEdits")
                            enabled: root._trackCtl
                                     && ((root._trackCtl.boxCount !== undefined
                                          ? root._trackCtl.boxCount
                                          : root._trackCtl.annCount) > 0)
                            onTriggered: root._trackCtl.discardAllAnnotations()
                        }
                    }
                }

                Rectangle {
                    id: trackBtn
                    visible: root._trackCtl !== null && root._trackCtl.editMode
                    anchors.verticalCenter: parent.verticalCenter
                    width: trackLbl.implicitWidth + 22; height: 26; radius: 6
                    color: trackMenu.opened ? Qt.rgba(1, 1, 1, 0.18)
                         : (trackHover.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                    border.width: 1
                    border.color: (root._trackCtl && root._trackCtl.recording)
                                  ? App.themeAccent
                                  : (trackMenu.opened ? Qt.rgba(1, 1, 1, 0.35)
                                                      : Qt.rgba(1, 1, 1, 0.18))

                    Row {
                        id: trackLbl
                        anchors.centerIn: parent
                        spacing: 5
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root._trackCtl && root._trackCtl.recording
                            width: 7; height: 7; radius: 3.5
                            color: App.themeAccent
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: App.uiText(App.language, "TrackMenuTitle")
                            color: "white"; font.pixelSize: 12
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root._trackCtl && root._trackCtl.trackedCount > 0
                            width: cntLbl.implicitWidth + 10; height: 16; radius: 8
                            color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                           App.themeAccent.b, 0.35)
                            Text {
                                id: cntLbl
                                anchors.centerIn: parent
                                text: root._trackCtl ? root._trackCtl.trackedCount : 0
                                color: "white"; font.pixelSize: 10
                            }
                        }
                        DrawnIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron-down"; size: 10; color: "white"
                        }
                    }
                    HoverHandler { id: trackHover }
                    TapHandler {
                        onTapped: trackMenu.popup(trackBtn, 0, trackBtn.height + 2)
                    }

                    ThemedMenu {
                        id: trackMenu

                        MenuItem {
                            text: App.uiText(App.language, "TrackRecord")
                            checkable: true
                            checked: root._trackCtl ? root._trackCtl.recording : false
                            onTriggered: if (root._trackCtl)
                                             root._trackCtl.recording = !root._trackCtl.recording
                        }
                        MenuSeparator { }
                        MenuItem {
                            enabled: false
                            visible: root._trackCtl && root._trackCtl.trackedCount > 0
                            height: visible ? implicitHeight : 0
                            text: App.uiText(App.language, "TrackOpenCount")
                                      .arg(root._trackCtl ? root._trackCtl.trackedCount : 0)
                        }
                        MenuItem {
                            text: App.uiText(App.language, "TrackAcceptAll")
                            enabled: root._trackCtl && root._trackCtl.trackedCount > 0
                            onTriggered: root._trackCtl.acceptAllChanges()
                        }
                        MenuItem {
                            text: App.uiText(App.language, "TrackRejectAll")
                            enabled: root._trackCtl && root._trackCtl.trackedCount > 0
                            onTriggered: root._trackCtl.rejectAllChanges()
                        }
                    }
                }

                TextField {
                    id: nameEdit
                    visible: root.optionsVisible
                    width: Math.min(320, Math.max(140, headLeftBar.width - 260))
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.displayName
                    color: "white"
                    font.pixelSize: 14; font.bold: true
                    readOnly: !root.optionsVisible
                    enabled: root.optionsVisible
                    background: Rectangle {
                        color: "transparent"
                        border.color: nameEdit.activeFocus ? App.themeAccent : "transparent"
                        border.width: 1; radius: 3
                    }
                    onAccepted: {
                        var t = text.trim()
                        if (t.length > 0 && t !== root.displayName)
                            mediaModel.renameItem(root.path, t)
                    }
                }
                }   // ScrollableBar (linke Gruppe der Kopfleiste)
            }

            Row {
                visible: root.optionsVisible
                width: parent.width
                spacing: 12

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.dateTime ? Qt.formatDateTime(root.dateTime, "yyyy-MM-dd hh:mm") : ""
                    color: Qt.rgba(1, 1, 1, 0.8); font.pixelSize: 11
                }

                TagBar {
                    width: parent.width - 200
                    filePath: root.path
                }
            }
        }
    }

    // Zwei verschiedene Grenzen, und das ist der Kern: im Vollbild beginnt die WERKZEUGleiste des Editors bei
    // y = 0, ein breiter Auslösestreifen läge komplett über ihr. AUSLÖSEN deshalb nur an der äussersten Kante
    // (`kPeekEdge`), HALTEN dagegen, solange der Zeiger über der eingeblendeten Leiste steht.
    readonly property int kPeekEdge: 3
    property bool _barPeek: false
    property real barOpacity: 1.0
    HoverHandler {
        id: viewHover
        onPointChanged: {
            root.barOpacity = 1.0
            barTimer.restart()
            if (!root.immersiveActive) { root._barPeek = false; return }
            const y = viewHover.point.position.y
            if (y <= root.kPeekEdge)                 root._barPeek = true
            else if (y > topBar.height + 8)          root._barPeek = false
        }
    }
    onImmersiveActiveChanged: if (!root.immersiveActive) root._barPeek = false
    Timer {
        id: barTimer
        interval: 2800
        running: true
        onTriggered: root.barOpacity = (root.type === 0) ? 0.0 : 1.0
    }

    MetadataDateEditor {
        id: dateEditor
        onAccepted: function(dt) {
            mediaModel.setCustomDate(root.path, dt)
            root.dateTime = dt
        }
        onCleared: mediaModel.clearCustomDate(root.path)
    }

    component DocMenuRow: Rectangle {
        id: docRow
        property string label: ""
        signal activated()

        implicitWidth: 200
        implicitHeight: visible ? 26 : 0
        height: implicitHeight
        width: docRow.ListView.view ? docRow.ListView.view.width : implicitWidth
        radius: 4
        color: docRowHover.hovered
               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20)
               : "transparent"

        Text {
            anchors { left: parent.left; leftMargin: 10; right: chevron.left
                      rightMargin: 6; verticalCenter: parent.verticalCenter }
            text: docRow.label
            color: App.themeTextPrimary
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        DrawnIcon {
            id: chevron
            anchors { right: parent.right; rightMargin: 8
                      verticalCenter: parent.verticalCenter }
            name: "chevron-right"; size: 10
            color: App.themeTextMuted
        }
        HoverHandler { id: docRowHover }
        TapHandler { onTapped: docRow.activated() }
    }

    component ChoicePopup: Popup {
        id: choice
        property var options: []
        property int current: -1
        signal picked(int value)

        modal: false
        dim: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 4
        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder
            radius: 8
        }
        contentItem: Column {
            spacing: 2
            Repeater {
                model: choice.options
                delegate: Rectangle {
                    id: optRow
                    required property var modelData
                    readonly property bool sel: choice.current === modelData.value
                    width: 176
                    height: 28
                    radius: 5
                    color: optRow.sel ? App.themeAccent
                                      : (optHover.hovered ? App.themeCard : "transparent")
                    Text {
                        anchors { left: parent.left; leftMargin: 10
                                  verticalCenter: parent.verticalCenter }
                        text: optRow.modelData.text
                        color: optRow.sel ? "#ffffff" : App.themeTextPrimary
                        font.pixelSize: 12
                    }
                    HoverHandler { id: optHover }
                    TapHandler {
                        onTapped: { choice.picked(optRow.modelData.value); choice.close() }
                    }
                }
            }
        }
    }

    function _openDocPopup(pop, item) {
        const at = viewBtn.mapToItem(root, 0, viewBtn.height + 2)
        const itemY = item ? item.mapToItem(root, 0, 0).y : at.y
        pop.x = Math.max(8, Math.min(at.x + viewMenu.width, root.width - pop.width - 8))
        pop.y = Math.max(8, Math.min(itemY, root.height - pop.height - 8))
        pop.open()
    }

    Popup {
        id: textPdfPopup
        objectName: "textPdfPopup"
        modal: false
        dim: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 10
        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder
            radius: 8
        }

        contentItem: Column {
            spacing: 10

            Text {
                text: App.uiText(App.language, "TextPdfColorTitle")
                color: App.themeTextPrimary
                font.pixelSize: 12
            }

            Row {
                spacing: 8
                ColorPicker {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34; height: 20
                    showAlpha: false
                    title: App.uiText(App.language, "TextPdfColorTitle")
                    selectedColor: root._textCtl ? root._textCtl.pdfInk : "black"
                    onColorPicked: function (c) {
                        if (root._textCtl) root._textCtl.pickPdfInk(c)
                    }
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root._textCtl !== null && root._textCtl.pdfInkOwn
                    width: 22; height: 22; radius: 4
                    color: inkResetHover.hovered ? App.themeCard : "transparent"
                    border.color: App.themeBorder; border.width: 1
                    DrawnIcon {
                        anchors.centerIn: parent
                        name: "undo"; size: 12
                        color: App.themeTextPrimary
                    }
                    HoverHandler { id: inkResetHover }
                    TapHandler { onTapped: { if (root._textCtl) root._textCtl.resetPdfInk() } }
                    ToolTip.visible: inkResetHover.hovered
                    ToolTip.delay: 600
                    ToolTip.text: App.uiText(App.language, "TextPdfColorResetTip")
                }
            }

            Rectangle {
                width: Math.max(140, konvRow.implicitWidth + 24); height: 28; radius: 6
                opacity: (root._textCtl && !root._textCtl.pdfBusy) ? 1.0 : 0.45
                color: konvHover.hovered ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                   App.themeAccent.b, 0.30)
                                         : Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                   App.themeAccent.b, 0.16)
                border.color: App.themeAccent; border.width: 1
                Row {
                    id: konvRow
                    anchors.centerIn: parent
                    spacing: 6
                    DrawnIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "arrow-right"; size: 13; color: App.themeAccent
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.uiText(App.language, "TextPdfConvert")
                        color: App.themeAccent; font.pixelSize: 12
                    }
                }
                HoverHandler { id: konvHover }
                TapHandler {
                    enabled: root._textCtl !== null && !root._textCtl.pdfBusy
                    onTapped: {
                        root._textCtl.exportPdf()
                        textPdfPopup.close()
                        viewMenu.close()
                    }
                }
            }
        }
    }

    ChoicePopup {
        id: pageNumPopup
        objectName: "docxPageNumberPopup"
        current: Docx.pdfPageNumberPos
        options: [
            { text: App.uiText(App.language, "DocxPdfNumberOff"),    value: 0 },
            { text: App.uiText(App.language, "DocxPdfNumberLeft"),   value: 1 },
            { text: App.uiText(App.language, "DocxPdfNumberCenter"), value: 2 },
            { text: App.uiText(App.language, "DocxPdfNumberRight"),  value: 3 },
        ]
        onPicked: function(v) { Docx.pdfPageNumberPos = v; viewMenu.close() }
    }

    ChoicePopup {
        id: pageStylePopup
        objectName: "docxPageStylePopup"
        current: Docx.pdfPageNumberStyle
        options: [
            { text: App.uiText(App.language, "DocxPdfNumberStylePlain"), value: 0 },
            { text: App.uiText(App.language, "DocxPdfNumberStyleTotal"), value: 1 },
        ]
        onPicked: function(v) { Docx.pdfPageNumberStyle = v; viewMenu.close() }
    }

    // Pfeiltasten-Guard: liegt der Fokus in einem editierbaren Textfeld, gehören <-/-> der Cursor-Bewegung, nicht
    // dem Dateiwechsel. Erkannt über die gemeinsame API der Text-Items (`cursorPosition` und nicht `readOnly`).
    function _editableTextFocused() {
        var f = root.Window.activeFocusItem
        if (!f) return false
        return (f.cursorPosition !== undefined) && (f.readOnly !== true)
    }
    function _seekSurface(sec) {
        if (root.type !== 1 && root.type !== 2) return false
        if (!surface.item || !surface.item.seekBy) return false
        surface.item.seekBy(sec * 1000)
        return true
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            if (root.immersiveActive) root.immersiveToggleRequested()
            else                root.backRequested()
            event.accepted = true
        }
        else if (event.key === Qt.Key_F && event.modifiers === Qt.NoModifier) {
            if (root._editableTextFocused()) return
            if (root.immersiveCapable || root.immersiveActive) {
                root.immersiveToggleRequested()
                event.accepted = true
            }
        }
        else if (event.key === Qt.Key_Left) {
            if (root._editableTextFocused()) return       // Pfeil bleibt im Textfeld
            if (root.immersiveActive && root._seekSurface(-App.videoSeekStep)) { event.accepted = true; return }
            root.prevRow(); event.accepted = true
        }
        else if (event.key === Qt.Key_Right) {
            if (root._editableTextFocused()) return       // Pfeil bleibt im Textfeld
            if (root.immersiveActive && root._seekSurface(App.videoSeekStep)) { event.accepted = true; return }
            root.nextRow(); event.accepted = true
        }
    }

    Shortcut {
        sequence: "Alt+S"
        enabled: root.paneActive
        onActivated: {
            App.toggleOptions()
            root.barOpacity = 1.0
            barTimer.restart()
        }
    }
    Shortcut {
        sequence: "Alt+Left"
        enabled: root.paneActive
        onActivated: root.backRequested()
    }
    Shortcut {
        sequence: "D"
        enabled: root.paneActive && root.path.length > 0 && root.optionsVisible
        onActivated: dateEditor.openWith(root.dateTime)
    }

    component ChromeBtn: Rectangle {
        id: cb
        property string kind: ""
        property bool active: false
        property string tip: ""
        signal activated()
        width: 32; height: 30; radius: 6
        color: active ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.28)
             : (cbHover.hovered ? Qt.rgba(1, 1, 1, 0.14) : "transparent")
        border.width: 1
        border.color: active ? App.themeAccent : Qt.rgba(1, 1, 1, 0.18)

        Item {
            anchors.centerIn: parent
            width: 18; height: 18

            Item {
                anchors.fill: parent
                visible: cb.kind === "dice"
                Rectangle { anchors.fill: parent; radius: 4; color: "transparent"
                            border.color: "#e8efed"; border.width: 1.4 }
                Rectangle { x: 3;   y: 3;   width: 3; height: 3; radius: 1.5; color: "#e8efed" }
                Rectangle { x: 12;  y: 3;   width: 3; height: 3; radius: 1.5; color: "#e8efed" }
                Rectangle { x: 7.5; y: 7.5; width: 3; height: 3; radius: 1.5; color: "#e8efed" }
                Rectangle { x: 3;   y: 12;  width: 3; height: 3; radius: 1.5; color: "#e8efed" }
                Rectangle { x: 12;  y: 12;  width: 3; height: 3; radius: 1.5; color: "#e8efed" }
            }

            Item {
                anchors.fill: parent
                visible: cb.kind === "html"
                Rectangle { anchors.fill: parent; radius: 2; color: "transparent"
                            border.color: "#e8efed"; border.width: 1.4 }
                Rectangle { anchors.top: parent.top; anchors.left: parent.left
                            anchors.right: parent.right; height: 5; radius: 1; color: "#e8efed" }
                Rectangle { x: 3; y: 9;  width: 12; height: 1.6; radius: 0.8; color: "#e8efed" }
                Rectangle { x: 3; y: 12; width: 8;  height: 1.6; radius: 0.8; color: "#e8efed" }
            }

            Item {
                anchors.fill: parent
                visible: cb.kind === "table"
                Rectangle { anchors.fill: parent; radius: 2; color: "transparent"
                            border.color: "#e8efed"; border.width: 1.4 }
                Rectangle { x: 0; y: 5.5;  width: 18; height: 1.3; color: "#e8efed" }
                Rectangle { x: 0; y: 11.5; width: 18; height: 1.3; color: "#e8efed" }
                Rectangle { x: 6;   y: 0; width: 1.3; height: 18; color: "#e8efed" }
                Rectangle { x: 12;  y: 0; width: 1.3; height: 18; color: "#e8efed" }
            }

            Item {
                anchors.fill: parent
                visible: cb.kind === "gridhead"
                //  Ein Raster, dessen erste Zeile und erste Spalte gefuellt
                //  sind - die Kopfleisten einer Tabellenkalkulation.
                Rectangle { anchors.fill: parent; radius: 2; color: "transparent"
                            border.color: "#e8efed"; border.width: 1.4 }
                Rectangle { x: 1.4; y: 1.4; width: 15.2; height: 4; color: "#e8efed" }
                Rectangle { x: 1.4; y: 1.4; width: 4;  height: 15.2; color: "#e8efed" }
                Rectangle { x: 10.5; y: 5.4;  width: 1.2; height: 11.2; color: "#e8efed" }
                Rectangle { x: 5.4;  y: 10.5; width: 11.2; height: 1.2; color: "#e8efed" }
            }

            Item {
                anchors.fill: parent
                visible: cb.kind === "toc"
                Rectangle { x: 0; y: 2;  width: 11; height: 1.6; radius: 0.8; color: "#e8efed" }
                Rectangle { x: 0; y: 8;  width: 11; height: 1.6; radius: 0.8; color: "#e8efed" }
                Rectangle { x: 0; y: 14; width: 11; height: 1.6; radius: 0.8; color: "#e8efed" }
                Rectangle { x: 13.5; y: 1; width: 4.5; height: 16; radius: 1.5
                            color: "transparent"; border.color: "#e8efed"; border.width: 1.3 }
            }

            Item {
                anchors.fill: parent
                visible: cb.kind === "addfile"
                Rectangle { x: 0; y: 1.5; width: 10; height: 14; radius: 1.5; color: "transparent"
                            border.color: "#e8efed"; border.width: 1.4 }
                Rectangle { x: 2.5; y: 5;    width: 5;   height: 1.3; radius: 0.6; color: "#e8efed" }
                Rectangle { x: 2.5; y: 8;    width: 5;   height: 1.3; radius: 0.6; color: "#e8efed" }
                Rectangle { x: 2.5; y: 11;   width: 3.4; height: 1.3; radius: 0.6; color: "#e8efed" }
                Rectangle { x: 10.5;  y: 7.25; width: 6.5; height: 1.8; radius: 0.9; color: "#e8efed" }
                Rectangle { x: 12.85; y: 4.9;  width: 1.8; height: 6.5; radius: 0.9; color: "#e8efed" }
            }
        }

        HoverHandler { id: cbHover }
        TapHandler { onTapped: cb.activated() }
        ToolTip.text: cb.tip
        ToolTip.visible: cbHover.hovered && cb.tip.length > 0
    }
}
