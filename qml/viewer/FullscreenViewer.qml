pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"
import "../docx"
import "../image"
import "../pdf"
import "../tags"

// ─────────────────────────────────────────────────────────────────────────────
//  FullscreenViewer.qml - Vollbild-Anzeige-Verbund (ersetzt FullscreenView/
//  ImageViewerWindow/ImageViewer.qml). StackView-Seite "fullscreen" (Phase 1).
//
//  • Dispatch nach Medientyp über EINEN Loader -> nur das aktive Medium ist
//    dekodiert; beim Wechsel/Verlassen release() (RAM-Prio 1).
//  • Navigation prev/next/zufall ausschließlich über die Proxy-Reihenfolge
//    (galleryModel.*At / rowForPath / randomRow) - keine eigene Liste.
//  • Metadaten-Overlay (Name/Datum/Tags) + Inline-Edit via Bridge.
//  • Video-Mode "external": Medium wird im Systemplayer geöffnet (Viewer.bridge).
// ─────────────────────────────────────────────────────────────────────────────
FocusScope {
    id: root
    focus: true

    signal backRequested()
    signal addFileRequested()

    // ── Docking-Drag (Kopfleiste, geteilte Ansicht) ───────────────────────────
    //  Bei mehr als einer offenen Datei (splitActive) lässt sich diese Kachel
    //  über ihre obere Leiste per Klick+Ziehen verschieben; die Drop-Zonen und
    //  das Anwenden des Layouts übernimmt die Shell (ApplicationShell). Die
    //  Koordinaten sind Viewer-lokal - die Shell mappt sie auf die Split-Seite.
    signal paneDragStarted()
    signal paneDragMoved(real x, real y)
    signal paneDragEnded(real x, real y)
    signal paneDragCanceled()

    // ── Geteilte Ansicht (vom Shell gesetzt) ──────────────────────────────────
    //  splitActive = mehr als eine Datei gleichzeitig offen -> die Kopfleiste
    //    dieser Kachel wird zur Ziehfläche fürs Docking und das immersive
    //    Vollbild (F) entfällt; bei genau einer Datei ist es wieder verfügbar.
    //  canAddMore  = es lassen sich noch weitere Dateien hinzufügen (< 4) ->
    //    steuert die Sichtbarkeit des „+"-Buttons in der Kopfleiste.
    property bool splitActive: false
    property bool canAddMore: true

    // ── Immersives Vollbild (Taste F) ─────────────────────────────────────────
    //  Vom Shell gesetzt (er schaltet zusätzlich das Fenster auf Vollbild und
    //  blendet die Menüleiste aus). Hier bedeutet es: KEINE Kachel-Chrome mehr -
    //  die obere Leiste (Zurück/Name/Datum/Tags) verschwindet, übrig bleibt
    //  allein die Steuerleiste der Surface
    //  (bei Video/Audio der Fortschrittsregler).
    //  Gilt AUCH in der geteilten Ansicht (Nutzerbefund: dort war `F` tot).
    //  Der Preis ist bekannt und gewollt: die Kopfleiste ist zugleich die
    //  Ziehfläche fürs Docking - solange die Chrome weg ist, lässt sich eine
    //  Kachel nicht umsortieren. Ein `F`/`Esc` holt sie zurück.
    //  Optionen-Modus (Alt+S) der HÄLFTE, zu der diese Kachel gehört - nicht
    //  appweit (s. PaneController::optionsVisible).
    property bool optionsVisible: App.optionsVisible

    property bool immersive: false
    readonly property bool immersiveCapable: root.path.length > 0
    readonly property bool immersiveActive: root.immersive
    signal immersiveToggleRequested()

    //  Wo steht die Wiedergabe dieser Kachel? (Nur Video/Audio-Flächen können
    //  das beantworten; sonst −1.) Der Player-Modus übernimmt damit die Stelle,
    //  statt den Titel von vorn zu beginnen.
    function mediaPositionMs() {
        return (surface.item && surface.item.playbackPositionMs !== undefined)
               ? surface.item.playbackPositionMs : -1
    }
    function mediaRunning() {
        return !!(surface.item && surface.item.playbackRunning === true)
    }
    //  Wiedergabe dieser Kachel anhalten (Übergabe an den Player-Modus).
    function pauseMedia() {
        if (surface.item && typeof surface.item.pausePlayback === "function")
            surface.item.pausePlayback()
    }

    //  paneActive = diese Kachel ist die AKTIVE (fokussierte) im Split-View.
    //  Im Einzel-View immer true. Alle fensterweiten Tastenkürzel dieser Kachel
    //  (Alt+S hier, plus die der geladenen Surface) sind an paneActive gebunden:
    //  ohne diese Bindung definieren bei 2–4 offenen Kacheln ALLE dieselben
    //  Qt.WindowShortcut-Sequenzen (Ctrl+C, Alt+Q, Entf …) -> Qt meldet
    //  Mehrdeutigkeit und feuert KEINES. Der Shell setzt paneActive über den
    //  activePaneIndex; paneActivated meldet einen Klick/Fokuswechsel zurück.
    property bool paneActive: true
    signal paneActivated()

    // Vom Shell gesetzt (Einstiegspfad).
    property string startPath: ""

    // Aktueller Zustand (aus dem Proxy gelesen).
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

    // ── HTML-Vorschau (nur Typ 4 / Text mit Endung .html/.htm) ────────────────
    //  „Gerendert zuerst": Web-Dateien öffnen direkt in der Vorschau (HtmlSurface);
    //  der Umschalt-Button oben rechts wechselt zur Quelltext-Ansicht (TextSurface)
    //  und zurück. Der Zustand bleibt über die Navigation hinweg erhalten;
    //  nicht-renderbare Dateien (z. B. .css, .txt) zeigen immer den Quelltext.
    property bool _htmlPreview: true
    function _isHtmlPath(p) {
        var dot = p.lastIndexOf(".")
        if (dot < 0) return false
        var ext = p.substring(dot + 1).toLowerCase()
        return ext === "html" || ext === "htm"
    }
    readonly property bool _isWebRenderable: root.type === 4 && root._isHtmlPath(root.path)
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

    // PDF gilt als geladen, sobald das Dokument „Ready" ist (erste Seite kann rendern).
    //  Editor-Controller der offenen Kachel, sofern es einen gibt (PDF).
    //  Träger des Track-Changes-Knopfes; der Bild-Editor folgt, sobald er
    //  dieselbe API trägt.
    //  PDF (3) und Bild (0) tragen beide einen Editor mit derselben
    //  Track-Changes-API - der Knopf ist deshalb für beide derselbe.
    readonly property var _trackCtl: ((root.type === 3 || root.type === 0) && surface.item
                                      && surface.item.editCtl !== undefined)
                                     ? surface.item.editCtl : null
    //  NUR der PDF-Editor - `_trackCtl` gilt auch fuer Bilder, und die kennen
    //  „durchsuchbar machen" nicht.
    readonly property var _pdfCtl: (root.type === 3 && surface.item
                                    && surface.item.editCtl !== undefined)
                                   ? surface.item.editCtl : null
    readonly property bool _pdfReady: root._loaded && surface.item !== null && surface.item.docReady === true
    // Ein PDF wird gerade geladen: deckt Öffnen-Animation (Typ aus _startType),
    // Dokument-Parsing (Typ aus root.type) UND Blättern zur nächsten PDF ab.
    readonly property bool pdfLoading: (root._loaded ? root.type : root._startType) === 3 && !root._pdfReady

    // Skeleton ERST nach 300 ms zeigen: lädt das PDF schneller, blitzt nichts auf.
    property bool _skelVisible: false
    Timer { id: skelDelay; interval: 300; repeat: false
            onTriggered: root._skelVisible = root.pdfLoading }
    onPdfLoadingChanged: {
        if (root.pdfLoading) { root._skelVisible = false; skelDelay.restart() }   // (neu) am Laden -> 300ms warten
        else { skelDelay.stop(); root._skelVisible = false }                       // fertig / kein PDF -> weg
    }

    function _maybeLoad() {
        if (root._loaded) return
        // In einem StackView erst bei Active laden; ohne StackView sofort.
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

    // ── Laden / Navigation ────────────────────────────────────────────────────
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

        // Video-Mode "external": im Systemplayer öffnen, Surface bleibt leer.
        if (type === 1 && App.videoPlayback === "external") {
            Viewer.openExternally(path)
        }

        // Trigger der lazy WebEngine-Initialisierung: das Öffnen einer
        // .html/.htm-Datei fordert HTML-Rendering an. Der Aufruf ist synchron
        // und idempotent - danach ist WebEngine.ready und der Loader unten
        // wählt die gerenderte Vorschau (sonst Quelltext-Fallback).
        if (root._isWebRenderable)
            WebEngine.ensureInitializedForHtml()

        // Inhalt explizit nachziehen: Bei Navigation zwischen Medien GLEICHEN Typs
        // bleibt das Loader-Item dasselbe (onItemChanged feuert nicht) - sonst
        // bliebe der alte Inhalt stehen. Der Loader ist synchron, d. h. nach dem
        // Setzen von 'type' ist surface.item bereits das passende Item.
        if (surface.item && surface.item.hasOwnProperty("source"))
            surface.item.source = path
    }

    //  Die offene TEXTFLÄCHE, falls es eine ist - daran hängen die Farbwahl und
    //  der PDF-Weg, die seit 2026-09-03 im Menü „Dokument" stehen statt in einer
    //  eigenen Leiste. Bei HTML-Vorschau und allen anderen Typen: null.
    readonly property var _textCtl:
        (surface.item && surface.item.exportPdf !== undefined) ? surface.item : null

    function releaseCurrent() {
        if (surface.item && surface.item.release)
            surface.item.release()
    }

    //  Geblaettert wird INNERHALB des Ordners, aus dem die offene Datei stammt:
    //  seit aufgeklappte Unterordner in derselben Liste stehen, waere „naechste
    //  Zeile" sonst ein Sprung ueber die Ordnergrenze. Ordnerkacheln werden
    //  uebersprungen - sie sind keine Datei. Beides entscheidet der Proxy
    //  (galleryModel.stepRow); −1 heisst „hier ist nichts anzusteuern".
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

    //  Klick/Antippen irgendwo in dieser Kachel meldet sie als aktive Kachel
    //  (Split-View). Passiver Grab (TapHandler) -> nimmt den darunterliegenden
    //  MouseAreas/Handlern NICHTS weg; reagiert nur auf den Druck-Beginn.
    TapHandler {
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPressedChanged: if (pressed) root.paneActivated()
    }

    // Leichter Lade-Indikator (Nicht-PDF-Medien) während des Übergangs. Für PDFs
    // übernimmt das seitenförmige Skeleton (unten) die gesamte Ladephase.
    BusyIndicator {
        anchors.centerIn: parent
        running: !root._loaded && root._startType !== 3
        visible: running
        z: 50
    }

    // ── PDF-Ladeanzeige: zuerst leere (weiße) Seite, ab 300 ms das Skeleton ──────
    //  Beim Öffnen erscheint SOFORT eine leere PDF-Seite (kein Blackscreen). Dauert
    //  das Laden länger als 300 ms, blenden Inhalts-Balken + Shimmer darüber ein
    //  (Skeleton). Größe/Position UND das Seitenweiß spiegeln die spätere PDF-Seite ->
    //  nahtloser Übergang. Leicht genug, dass die Öffnen-Animation flüssig bleibt.
    Item {
        id: pdfSkeleton
        anchors.fill: parent
        z: 50
        visible: opacity > 0.01
        opacity: root.pdfLoading ? 1 : 0          // leere Seite SOFORT beim Laden; weg, sobald fertig

        // Viewport, in dem die PDF-Seite NACH dem Laden erscheint - spiegelt die
        // PdfSurface-Geometrie: unter Metadaten-Leiste + PdfSurface-Toolbar (40 px,
        // erscheint mit docReady) bis zum unteren Rand. Seiten-Fit
        // wie fitMode "page": min(wFit,hFit) gegen (Viewport − 24); Standardmaß A4
        // (595×842 pt) als Annahme, bis das echte Seitenmaß bekannt ist.
        readonly property real _vpTop:    (topBar.visible ? topBar.height : 0) + 40
        readonly property real _vpBottom: root.height
        readonly property real _vpW:      root.width
        readonly property real _vpH:      Math.max(0, _vpBottom - _vpTop)
        readonly property real _fit:      Math.max(0, Math.min((_vpW - 24) / 595, (_vpH - 24) / 842))

        // Leere PDF-Seite (Seitenweiß) - Größe/Position wie die spätere A4-Seite.
        Rectangle {
            id: skelPage
            width:  595 * pdfSkeleton._fit
            height: 842 * pdfSkeleton._fit
            x: (parent.width - width) / 2
            y: pdfSkeleton._vpTop
            color: "#fbfbfa"
            clip: true

            // Skeleton-Schicht (Inhalts-Balken + Shimmer) - ERST nach 300 ms, weich ein.
            Item {
                id: skelContent
                anchors.fill: parent
                opacity: root._skelVisible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutQuad } }

                // Platzhalter-Inhalt: Titelbalken + Absatzzeilen mit variabler Breite.
                Column {
                    id: skelLines
                    anchors {
                        left: parent.left;  leftMargin:  skelPage.width * 0.11
                        right: parent.right; rightMargin: skelPage.width * 0.11
                        top: parent.top;     topMargin:   skelPage.height * 0.11
                    }
                    spacing: skelPage.height * 0.0265

                    // Titel (breiter, etwas dunkler)
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

                // Shimmer: heller, leicht geneigter Streifen - sichtbar auf den Balken.
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

    // ── Medien-Loader (genau ein aktives Medium) ──────────────────────────────
    Loader {
        id: surface
        anchors.fill: parent
        active: root._loaded
        //  **URL statt Typname** - der entscheidende Unterschied fuer den Start:
        //  ein Typname (`PdfSurface {}`) zwingt QML, die Datei schon beim
        //  UEBERSETZEN dieser Datei zu laden, und damit bei jedem App-Start alle
        //  Flaechen samt ihren Kindern. Gemessen kostete das rund 100 ms, obwohl
        //  beim Start keine einzige Flaeche gebraucht wird. Ueber die URL wird
        //  genau die eine Datei geladen, die der Nutzer wirklich oeffnet.
        //  (Derselbe Kunstgriff wie bisher schon bei HtmlSurface - jetzt fuer
        //  alle. Deshalb sind auch die zwei Hinweistexte eigene Dateien:
        //  ein Loader kann `source` und `sourceComponent` nicht mischen.)
        source: {
            switch (root.type) {
            case 0:  return "qrc:/qml/image/ImageSurface.qml"
            case 1:  return (App.videoPlayback === "external")
                            ? "qrc:/qml/viewer/ViewerNote.qml"
                            : "qrc:/qml/viewer/VideoSurface.qml"
            case 2:  return "qrc:/qml/viewer/VideoSurface.qml"   // Audio: VideoSurface mit Audio-Out
            case 3:  return "qrc:/qml/pdf/PdfSurface.qml"
            case 4:  return root._showWebPreview ? "qrc:/qml/viewer/HtmlHost.qml"
                                                 : "qrc:/qml/viewer/TextSurface.qml"
            case 5:  return "qrc:/qml/docx/DocxSurface.qml"      // Word-Dokumente (DOCX-Editor)
            default: return "qrc:/qml/viewer/ViewerNote.qml"
            }
        }
        onItemChanged: {
            if (!item) return
            //  Der Hinweistext braucht seine Art (extern geoeffnet vs. kein
            //  Betrachter) - beide Faelle teilen sich EINE Datei.
            if (item.hasOwnProperty("kind"))
                item.kind = (root.type === 1) ? "external" : "unsupported"
            if (item.hasOwnProperty("source")) item.source = root.path
        }
    }

    // ── Surface-Chrome unterhalb der globalen Leisten halten (kein Overlap) ────
    //  Reserviert die Hoehe der oberen Leiste (topBar) in der
    //  jeweiligen Surface, sodass deren eigene Toolbar/Steuerleiste NICHT mit
    //  der globalen FullscreenViewer-Chrome ueberlappen. Betrifft nur noch
    //  topInset (topBar): eine globale UNTERE Chrome gibt es nicht mehr - die
    //  Vor/Zurueck-Schaltflaechen sind entfallen, die Navigation laeuft ueber
    //  die Pfeiltasten. `bottomInset` bleibt Teil des Surface-Vertrags (0).
    Binding {
        target: surface.item
        property: "topInset"
        value: (topBar.visible && !root.immersiveActive) ? topBar.height : 0
        when: surface.item !== null && (root.type === 0 || root.type === 3 || root.type === 4
                                         || root.type === 5)
        restoreMode: Binding.RestoreNone
    }
    //  Aktiv-Zustand dieser Kachel an die Surfaces mit fensterweiten Kürzeln
    //  (Bild/PDF/DOCX) durchreichen -> deren Shortcuts feuern nur in der
    //  fokussierten Kachel (keine Split-View-Mehrdeutigkeit).
    Binding {
        target: surface.item
        property: "paneActive"
        value: root.paneActive
        when: surface.item !== null && (root.type === 0 || root.type === 3 || root.type === 5)
        restoreMode: Binding.RestoreNone
    }

    //  Die Flaechen selbst stehen NICHT mehr als Typnamen hier: der
    //  `surface`-Loader oben waehlt sie ueber eine URL (s. dort). Frueher
    //  standen an dieser Stelle sechs `Component { XxxSurface {} }` - genau die
    //  zwangen QML, samtliche Flaechen bei jedem Start mitzuuebersetzen.

    // ── Obere Leiste: Zurück / Name / Datum / Tags ────────────────────────────
    Rectangle {
        id: topBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        //  ~30 % niedriger als früher (52 -> 36 bzw. 96 -> 67): der Dateiname ist
        //  in den Fenstertitel gewandert, die Leiste trägt nur noch Bedienung.
        height: root.optionsVisible ? 67 : 36
        color: Qt.rgba(0, 0, 0, 0.55)
        opacity: root.barOpacity
        // Im immersiven Vollbild ist die Leiste weg - sie kommt aber zurück,
        // sobald der Zeiger an den OBEREN RAND fährt (Muster: Videoplayer,
        // Browser). Ohne das wäre im Vollbild weder das Ansichts-Menü noch der
        // Zurück-Knopf erreichbar (Nutzerbefund). Sie legt sich dabei ÜBER den
        // Inhalt: der `topInset` bleibt im Vollbild 0, sonst spränge die Seite
        // bei jeder Mausbewegung am oberen Rand.
        visible: opacity > 0.01 && (!root.immersiveActive || root._barPeek)
        Behavior on opacity { NumberAnimation { duration: 180 } }

        // ── Docking-Drag-Fläche (UNTER den Bedienelementen) ───────────────────
        //  Erstes Kind der Leiste -> Buttons und (im Alt+S-Modus) das aktive
        //  Namensfeld liegen darüber und behalten ihre Klicks; Press+Ziehen auf
        //  freien Leistenbereichen (und auf dem außerhalb des Alt+S-Modus
        //  deaktivierten Namensfeld) startet den Kachel-Drag. Ein Drag beginnt
        //  erst nach einer kleinen Bewegungsschwelle - ein einfacher Klick
        //  bleibt folgenlos und stört den Edit-Modus nicht.
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

                //  Rechte Knopfgruppe: eigene, gedeckelte Leiste (höchstens die
                //  halbe Zeile). Vorher hingen die Knöpfe einzeln am Rand und
                //  wuchsen nach links, bis sie die linke Gruppe verdeckten.
                ScrollableBar {
                    id: headRightBar
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    width: Math.min(contentWidth, parent.width * 0.5)
                    spacing: 6

                // Datei zur geteilten Ansicht hinzufügen - kleiner „Datei +"-Button
                // direkt neben dem Datum-Button (gleicher Stil wie Datum/Zufall).
                // Nur sichtbar, solange < 4 Dateien offen sind (canAddMore); im
                // 4er-Splitscreen entfällt er.
                ChromeBtn {
                    id: addBtn
                    visible: root.canAddMore
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "addfile"
                    tip: App.uiText(App.language, "SplitAddFile")
                    onActivated: root.addFileRequested()
                }

                //  ── Übersichtsspalte und Transliteration ──────────────
                //  Standen bis 2026-09-03 in der Werkzeugleiste der Textfläche.
                //  Sie gehören hierher, weil sie ANSICHT und EINGABE betreffen,
                //  nicht die Datei - und weil die zweite Leiste damit ganz
                //  entfallen konnte (Festlegung des Nutzers).
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

                // Quelltext ⇄ gerenderte HTML-Vorschau - nur sichtbar bei .html/.htm.
                ChromeBtn {
                    id: previewBtn
                    // Nur anzeigen, wenn HTML-Rendering TATSÄCHLICH verfügbar
                    // ist (WebEngine bereit) - sonst gibt es nichts umzuschalten.
                    visible: root._isWebRenderable && WebEngine.ready
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "html"
                    active: root._htmlPreview
                    tip: root._htmlPreview ? App.uiText(App.language, "ViewerShowSource")
                                           : App.uiText(App.language, "ViewerShowPreview")
                    onActivated: {
                        // Vor dem Komponentenwechsel die aktuelle Surface freigeben:
                        // TextSurface sichert dabei ungespeicherte Änderungen, HtmlSurface
                        // stoppt das Laden. Danach kippt der Modus -> Loader tauscht die Surface.
                        root.releaseCurrent()
                        root._htmlPreview = !root._htmlPreview
                    }
                }
                ChromeBtn {
                    id: diceBtn
                    anchors.verticalCenter: parent.verticalCenter
                    //  NICHT bei textbasierten Dateien (Typ 4 = Text/HTML,
                    //  5 = DOCX): dort liest man ein Dokument, man springt
                    //  nicht zufällig zum nächsten. Der Knopf stand in der
                    //  Leiste, ohne je zu etwas gut zu sein (Nutzerbefund
                    //  2026-09-02).
                    visible: root.type !== 4 && root.type !== 5
                    kind: "dice"
                    tip: App.uiText(App.language, "ViewerRandom")
                    active: root.randomNext
                    onActivated: root.randomNext = !root.randomNext
                }
                }   // Ende headRightBar

                //  ── (früher „Datei") ─────────────────────────────────────────
                //  Der Knopf ist ENTFALLEN: sein einziger Eintrag („Bearbeitungen
                //  entfernen") steht jetzt im Menü „Dokument". Zwei Menüs desselben
                //  Namens in einem Fenster - oben appweit, hier dateibezogen -
                //  waren nicht auseinanderzuhalten (Nutzerbefund).
                //  Alles Linke steht in EINER blätterbaren Leiste (Strg + Rad):
                //  sie endet vor der rechten Knopfgruppe, kann also nie darüber
                //  laufen, und bei zu wenig Platz bleibt jeder Knopf über das
                //  Rad erreichbar. Vorher hingen die Knöpfe in einer Kette und
                //  das Namensfeld spannte bis zur rechten Gruppe: im schmalen
                //  Fenster wurde seine Breite negativ und die Gruppen
                //  überlappten (Nutzerbild `tests/Viewer.png`).
                ScrollableBar {
                    id: headLeftBar
                    anchors.left: parent.left
                    anchors.right: headRightBar.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    spacing: 6

                //  Feste Größe wie die Menüknöpfe daneben (26 px hoch): ein
                //  `ToolButton` ohne Maße bringt ~40 px mit und war damit HÖHER
                //  als die 30-px-Zeile - alles daneben rutschte dadurch nach
                //  oben (Nutzerbild `Dokument.png`).
                ToolButton {
                    id: backBtn
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: 32; implicitHeight: 26
                    padding: 0
                    onClicked: root.backRequested()
                    //  Der Inhalt eines `ToolButton` füllt seine Fläche - ein
                    //  Symbol mit fester Größe säße darin links. Deshalb ein
                    //  füllendes `Item` mit zentriertem Symbol.
                    contentItem: Item {
                        DrawnIcon {
                            anchors.centerIn: parent
                            name: "chevron-left"
                            size: 16
                            color: "white"
                        }
                    }
                }

                //  ── Ansichts-Menü ────────────────────────────────────────────
                //  Steht dort, wo früher der Dateiname stand (der sitzt jetzt im
                //  Fenstertitel). Bewusst ein MENÜ und kein weiteres Symbol: die
                //  Leiste soll wachsen können, ohne in einer Symbolreihe zu enden,
                //  und jeder Eintrag trägt hier seinen Namen statt nur ein Bild.
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
                            //  „Dokument", nicht „Ansicht": das Menü betrifft DIESE
                            //  Datei. Was Fenster und App angeht (Optionen-Modus,
                            //  Kachelgröße, Teilen, Vollbild), steht oben.
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
                        //  Griff für den Prüfstand (`bench_qmlscene … documentMenu`).
                        objectName: "documentMenu"

                        //  Was mit DIESER Datei zu tun hat - nach Bedeutung
                        //  gruppiert statt nach Gewohnheit. Zufallsmodus und
                        //  „Datei danebenlegen" stehen nicht mehr hier: dafür gibt
                        //  es die beiden Knöpfe rechts in derselben Leiste, und
                        //  zweimal derselbe Befehl in Sichtweite hilft niemandem.
                        MenuItem {
                            text: App.uiText(App.language, "MetaTitle")
                            onTriggered: dateEditor.openWith(root.dateTime)
                        }
                        MenuItem {
                            //  Nur bei HTML und nur, wenn die Vorschau überhaupt
                            //  verfügbar ist - sonst gibt es nichts umzuschalten.
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
                        //  ── Ton aus DIESEM Video sichern ──────────────────
                        //  Derselbe Weg wie im Kontextmenü der Kachel, nur ohne
                        //  Umweg über die Galerie: wer das Video gerade ansieht,
                        //  will es nicht erst wieder schließen. Sichtbar nur bei
                        //  Videos und nur für Hüllen, die der Leser kennt.
                        //  Die neue Datei nimmt die HÄLFTE auf (GalleryPane hört
                        //  auf `Audio.extractFinished`) - die Ansicht bleibt
                        //  stehen, sie hat damit nichts zu tun.
                        MenuItem {
                            visible: root.type === 1 && Audio.canExtractAudio(root.path)
                            height: visible ? implicitHeight : 0
                            enabled: !Audio.extractBusy
                            text: App.uiText(App.language, "AudioExtractMenu")
                            onTriggered: Audio.extractAudio(root.path)
                        }
                        //  ── Nur DOCX: was beim Weg „-> PDF" gelten soll ───
                        //  KEINE `MenuItem`s: die schließen ihr Menü beim Klick
                        //  von selbst. Hier soll es stehen bleiben, während das
                        //  kleine Fenster daneben aufgeht - deshalb eigene
                        //  Zeilen (`DocMenuRow`), die den Klick selbst nehmen.
                        //  Der frühere Weg (schließen lassen und sofort wieder
                        //  öffnen) flackerte sichtbar.
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

                        //  ── Nur TEXT: als PDF sichern ─────────────────────
                        //  Farbe und Knopf stehen im Popup daneben - wie bei
                        //  DOCX. Eine `DocMenuRow`, damit das Menü offen bleibt,
                        //  während man die Farbe wählt.
                        DocMenuRow {
                            visible: root._textCtl !== null
                            label: App.uiText(App.language, "TextPdfMenu")
                            onActivated: root._openDocPopup(textPdfPopup, this)
                        }

                        //  ── Gescannte PDF dauerhaft durchsuchbar machen ───
                        //  Erkennt die Seiten OHNE Textebene und schreibt die
                        //  Wörter unsichtbar IN die Datei. Danach findet sie
                        //  jeder Leser - deshalb steht das hier und nicht als
                        //  Knopf in der Werkzeugpalette: es ist eine Sache des
                        //  DOKUMENTS, nicht des gerade gewählten Werkzeugs.
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
                        //  Bearbeitungen dieser Datei verwerfen (war das ganze
                        //  frühere „Datei"-Menü). Erscheint nur, wo es einen
                        //  Editor gibt (PDF, Bild).
                        MenuItem {
                            visible: root._trackCtl !== null
                            height: visible ? implicitHeight : 0
                            text: App.uiText(App.language, "CtxRemoveEdits")
                            //  Die beiden Editoren zählen ihre Objekte unter
                            //  verschiedenen Namen (`boxCount` bzw. `annCount`) -
                            //  hier zählt nur, ob es überhaupt welche gibt.
                            enabled: root._trackCtl
                                     && ((root._trackCtl.boxCount !== undefined
                                          ? root._trackCtl.boxCount
                                          : root._trackCtl.annCount) > 0)
                            onTriggered: root._trackCtl.discardAllAnnotations()
                        }
                    }
                }

                //  ── Änderungen verfolgen ─────────────────────────────────────
                //  Nur im Bearbeiten-Modus: außerhalb gibt es nichts aufzuzeichnen.
                //  Bewusst ein MENÜ, kein Schalter - es soll später mehr tragen
                //  als das Aufzeichnen.
                Rectangle {
                    id: trackBtn
                    visible: root._trackCtl !== null && root._trackCtl.editMode
                    anchors.verticalCenter: parent.verticalCenter
                    width: trackLbl.implicitWidth + 22; height: 26; radius: 6
                    color: trackMenu.opened ? Qt.rgba(1, 1, 1, 0.18)
                         : (trackHover.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                    border.width: 1
                    //  Läuft die Aufzeichnung, trägt der Knopf die Akzentfarbe -
                    //  sonst wäre am Bildschirm nicht zu sehen, dass mitgeschrieben
                    //  wird, und man wundert sich über markierte Notizen.
                    border.color: (root._trackCtl && root._trackCtl.recording)
                                  ? App.themeAccent
                                  : (trackMenu.opened ? Qt.rgba(1, 1, 1, 0.35)
                                                      : Qt.rgba(1, 1, 1, 0.18))

                    Row {
                        id: trackLbl
                        anchors.centerIn: parent
                        spacing: 5
                        //  Punkt in der Akzentfarbe = es wird aufgezeichnet.
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
                        //  Zahl der offenen Änderungen - nur wenn es welche gibt.
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
                            //  Reine Anzeige: wie viele Entscheidungen offen sind.
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
                    //  NUR im Optionen-Modus (Alt+S) - dort ist das Feld das
                    //  Umbenennen-Werkzeug. Als reine Anzeige wird es nicht mehr
                    //  gebraucht: der Name steht im Fenstertitel.
                    visible: root.optionsVisible
                    //  Feste Breite statt „bis zur rechten Gruppe": in der Reihe
                    //  gibt es kein Dehnen, dafür kann nichts mehr überlappen.
                    width: Math.min(320, Math.max(140, headLeftBar.width - 260))
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.displayName
                    color: "white"
                    font.pixelSize: 14; font.bold: true
                    // Umbenennen NUR im Optionen-/Edit-Modus (Alt+S): außerhalb
                    // ist das Feld deaktiviert (readOnly + keine Mauseingabe) -
                    // damit bleibt die Kopfleiste dort eine freie Drag-Fläche
                    // fürs Docking und versehentliche Fokus-Klicks entfallen.
                    // Die Textfarbe ist explizit gesetzt (color) -> kein
                    // Ausgrauen im deaktivierten Zustand.
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

    // ── Auto-Hide der oberen Leiste ─────────────────────────────────────────
    //  Betrifft ausschließlich topBar (Zurück/Name/Datum/Tags).
    //  ── Kopfleiste im Vollbild hervorholen ───────────────────────────────────
    //  ZWEI verschiedene Grenzen, und das ist der Kern der Sache: Im Vollbild
    //  beginnt die WERKZEUGleiste des Editors bei y = 0. Ein breiter Auslöse-
    //  streifen läge komplett über ihr - sie wäre nicht mehr bedienbar, weil sich
    //  die Kopfleiste beim Hinsteuern davorlegt (Nutzerbefund).
    //    • AUSLÖSEN nur an der äußersten Kante (`kPeekEdge`): dorthin fährt man
    //      absichtlich, für einen Knopf zielt man auf dessen Mitte.
    //    • HALTEN, solange der Zeiger über der eingeblendeten Leiste steht -
    //      sonst verschwände sie, bevor man ihren Knopf treffen kann.
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
    //  Vollbild verlassen ⇒ der Zustand gehört zurückgesetzt, sonst bliebe die
    //  Leiste beim nächsten Eintritt sofort sichtbar.
    onImmersiveActiveChanged: if (!root.immersiveActive) root._barPeek = false
    Timer {
        id: barTimer
        interval: 2800
        running: true
        // Nur Bilder blenden die obere Leiste automatisch aus; alle anderen
        // Medientypen behalten topBar dauerhaft sichtbar.
        onTriggered: root.barOpacity = (root.type === 0) ? 0.0 : 1.0
    }

    // ── Datum-Editor ───────────────────────────────────────────────────────────
    MetadataDateEditor {
        id: dateEditor
        //  Über den PFAD: das Datum gehört ins Sidecar des Ordners, dem die
        //  Datei gehört. Das Modell zieht dabei auch die Kachel nach (Anzeige
        //  und Sortierung), was der frühere Weg über den Namen nicht tat.
        onAccepted: function(dt) {
            mediaModel.setCustomDate(root.path, dt)
            root.dateTime = dt
        }
        onCleared: mediaModel.clearCustomDate(root.path)
    }

    // ── Zwei kleine Fenster für den DOCX-PDF-Export ──────────────────────────
    //  Aufgerufen aus dem Dokument-Menü. Bewusst EIGENE Popups und keine
    //  aufgeklappten Menülisten: im Menü sollen für ein Dokument nur drei Zeilen
    //  stehen (Datum, Seitenzahl, Form).
    //
    //  Inline-Komponente statt zweimal derselbe Aufbau - die beiden
    //  unterscheiden sich nur in Überschrift und Auswahl.
    //  Eine Zeile im Dokument-Menü, die das Menü NICHT schließt. Sieht aus wie
    //  ein `MenuItem` (s. `qml/style/MenuItem.qml`: Höhe 26, Radius 4, 10 px
    //  links, Schrift 13, Hervorhebung in 20 % Akzent) und trägt rechts einen
    //  Chevron - er sagt: hier geht etwas auf.
    component DocMenuRow: Rectangle {
        id: docRow
        property string label: ""
        signal activated()

        //  200 px ist das Mindestmaß der Menüs im Projekt (s. MenuItem.qml).
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
        //  Die Markierung eines zuvor überfahrenen `MenuItem` räumt der Stil
        //  selbst weg (s. `qml/style/MenuItem.qml`) - hier ist nichts zu tun.
        HoverHandler { id: docRowHover }
        TapHandler { onTapped: docRow.activated() }
    }

    component ChoicePopup: Popup {
        id: choice
        //  [{ text, value }] und der gerade gültige Wert.
        property var options: []
        property int current: -1
        signal picked(int value)

        //  AUSSEHEN UND VERHALTEN wie die Popups der Werkzeugleiste (Tabelle,
        //  Aufzählung in `DocxSurface`): Menüleisten-Ton, Radius 8, schmale
        //  Polsterung, die gültige Zeile in der Akzentfarbe GEFÜLLT. Kein
        //  `modal`, kein `dim` - es ist ein kleines Fenster am Bedienelement,
        //  kein Dialog. (Der Datums-Editor ist etwas anderes und bleibt es.)
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

    //  Ein Auswahl-Fenster BÜNDIG rechts neben dem Dokument-Menü aufmachen, auf
    //  Höhe des angeklickten Eintrags: die linke Kante des Fensters liegt an der
    //  rechten Kante des Menüs, nichts blitzt dazwischen durch (Festlegung des
    //  Nutzers; die Werkzeugleisten-Popups haben dort 4 px Luft). An den
    //  Fensterrändern wird geklemmt, damit es nie hinausragt.
    function _openDocPopup(pop, item) {
        const at = viewBtn.mapToItem(root, 0, viewBtn.height + 2)
        const itemY = item ? item.mapToItem(root, 0, 0).y : at.y
        pop.x = Math.max(8, Math.min(at.x + viewMenu.width, root.width - pop.width - 8))
        pop.y = Math.max(8, Math.min(itemY, root.height - pop.height - 8))
        pop.open()
        //  Das Menü BLEIBT offen: die beiden Zeilen sind keine `MenuItem`s,
        //  also schließt es sich gar nicht erst (s. `DocMenuRow`).
    }

    //  ── Text -> PDF ──────────────────────────────────────────────────────────
    //  Schriftfarbe DIESER Datei plus der Knopf, der die PDF neben die Quelle
    //  schreibt. Steht bewusst in einem Popup und nicht in einer Leiste: es ist
    //  ein seltener Vorgang mit einer Einstellung daran.
    Popup {
        id: textPdfPopup
        objectName: "textPdfPopup"
        //  Aussehen wie die übrigen Dokument-Popups (s. `ChoicePopup`).
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
                //  Zurücksetzen erscheint erst, wenn die Datei eine EIGENE Farbe
                //  trägt - vorher gäbe es nichts zurückzusetzen.
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
        //  Wahl getroffen -> auch das Menü darf zugehen.
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

    // ── Tastatur ────────────────────────────────────────────────────────────
    //  Pfeiltasten-Guard: Liegt der Fokus in einem EDITIERBAREN Textfeld
    //  (TextInput/TextEdit-basiert: TextField, TextArea, Editor, PDF-Notizen,
    //  Namensfeld), gehören <-/-> ausschließlich der Cursor-Bewegung im Feld -
    //  der Dateiwechsel darf dann nicht ausgelöst werden. Erkennung über die
    //  gemeinsame API der Text-Items (cursorPosition + nicht readOnly).
    function _editableTextFocused() {
        var f = root.Window.activeFocusItem
        if (!f) return false
        return (f.cursorPosition !== undefined) && (f.readOnly !== true)
    }
    //  Video/Audio: die geladene Surface um `sec` Sekunden spulen (negativ =
    //  zurück). Gibt true zurück, wenn tatsächlich gespult wurde.
    function _seekSurface(sec) {
        if (root.type !== 1 && root.type !== 2) return false
        if (!surface.item || !surface.item.seekBy) return false
        surface.item.seekBy(sec * 1000)
        return true
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            // Im immersiven Vollbild verlässt Esc zuerst das Vollbild -
            // erst der zweite Druck schließt die Datei.
            if (root.immersiveActive) root.immersiveToggleRequested()
            else                root.backRequested()
            event.accepted = true
        }
        //  F = immersives Vollbild an/aus. Ein fokussiertes Textfeld (Editor,
        //  Namensfeld, PDF-Notiz) verbraucht den Tastendruck selbst und erreicht
        //  diesen Handler gar nicht erst; der Guard sichert die Randfälle ab.
        else if (event.key === Qt.Key_F && event.modifiers === Qt.NoModifier) {
            if (root._editableTextFocused()) return
            if (root.immersiveCapable || root.immersiveActive) {
                root.immersiveToggleRequested()
                event.accepted = true
            }
        }
        else if (event.key === Qt.Key_Left) {
            if (root._editableTextFocused()) return       // Pfeil bleibt im Textfeld
            // Vollbild + Video/Audio: zurückspulen statt Dateiwechsel.
            if (root.immersiveActive && root._seekSurface(-App.videoSeekStep)) { event.accepted = true; return }
            root.prevRow(); event.accepted = true
        }
        else if (event.key === Qt.Key_Right) {
            if (root._editableTextFocused()) return       // Pfeil bleibt im Textfeld
            if (root.immersiveActive && root._seekSurface(App.videoSeekStep)) { event.accepted = true; return }
            root.nextRow(); event.accepted = true
        }
    }

    // ── S-Modus im Viewer (Alt+S) ─────────────────────────────────────────────
    //  Schaltet den Optionen-/S-Modus (App.optionsVisible) auch im geöffneten
    //  Media Viewer um - sichtbar an der erweiterten Metadaten-Zeile der oberen
    //  Leiste (Datum + Tags) und an den Kachel-Overlays nach der Rückkehr.
    //  Einheitliches Kürzel mit der Galerie-Seite: deren Alt+S-Shortcut ist per
    //  stack.depth-Guard hier inaktiv - keine Kollision; ebenso wenig mit
    //  Ctrl+S (Text speichern, TextSurface) oder den übrigen Viewer-Kürzeln
    //  (Esc/<-/->/+/-/Ctrl+C/Ctrl+A). Beim Umschalten wird
    //  die obere Leiste kurz eingeblendet, damit die Wirkung sofort sichtbar ist.
    Shortcut {
        sequence: "Alt+S"
        enabled: root.paneActive
        onActivated: {
            //  Schaltet die HÄLFTE um (die Kachel gehört zu einer): `App`
            //  reicht es an die fokussierte Hälfte weiter - und die ist beim
            //  Tastendruck genau diese.
            App.toggleOptions()
            root.barOpacity = 1.0
            barTimer.restart()
        }
    }
    //  Alt+<- = zurück zur Galerie (Standard-Rücksprung, zusätzlich zu Esc);
    //  nur in der aktiven Kachel. Kein Konflikt mit dem reinen <- (Dateiwechsel),
    //  da dieses ohne Alt-Modifikator läuft.
    Shortcut {
        sequence: "Alt+Left"
        enabled: root.paneActive
        onActivated: root.backRequested()
    }
    //  D = Datum-Editor dieser Datei öffnen - NUR im Optionen-Modus (Alt+S).
    //  Ohne diese Bedingung kaperte die Einzeltaste in jedem Editor die
    //  Texteingabe; der reguläre Weg ist und bleibt der Kalender-Knopf oben
    //  rechts. (Zusätzlich unterdrücken fokussierte Eingabeflächen Einzeltasten
    //  via ShortcutOverride - QML-Textfelder von selbst, DocxTextArea explizit.)
    Shortcut {
        sequence: "D"
        enabled: root.paneActive && root.path.length > 0 && root.optionsVisible
        onActivated: dateEditor.openWith(root.dateTime)
    }

    // ── Minimalistischer Chrome-Button (flach, monochrom, theme-Akzent) ───────
    //  Zeichnet sein Icon intern (kind) - bewusst keine farbigen Emoji mehr und
    //  kein default-children-Alias (vermeidet Selbstreferenz).
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

            // Würfel (Zufall, 5 Augen)
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

            // Web-Seite (HTML-Vorschau): Fensterrahmen + Titelleiste + Inhaltszeilen
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

            //  Übersichtsspalte: drei Zeilen mit einem Rahmen rechts - dasselbe
            //  Bild wie in der Symboltabelle (`DrawnIcon "toc"`), hier aber in
            //  der hellen Farbe der oberen Leiste.
            Item {
                anchors.fill: parent
                visible: cb.kind === "toc"
                Rectangle { x: 0; y: 2;  width: 11; height: 1.6; radius: 0.8; color: "#e8efed" }
                Rectangle { x: 0; y: 8;  width: 11; height: 1.6; radius: 0.8; color: "#e8efed" }
                Rectangle { x: 0; y: 14; width: 11; height: 1.6; radius: 0.8; color: "#e8efed" }
                Rectangle { x: 13.5; y: 1; width: 4.5; height: 16; radius: 1.5
                            color: "transparent"; border.color: "#e8efed"; border.width: 1.3 }
            }

            // Datei hinzufügen: minimalistisches Datei-Blatt (Umriss + Inhaltszeilen)
            // mit einem kleinen Plus daneben (rechts).
            Item {
                anchors.fill: parent
                visible: cb.kind === "addfile"
                // Blatt-Umriss (linke Seite)
                Rectangle { x: 0; y: 1.5; width: 10; height: 14; radius: 1.5; color: "transparent"
                            border.color: "#e8efed"; border.width: 1.4 }
                // Inhaltszeilen im Blatt
                Rectangle { x: 2.5; y: 5;    width: 5;   height: 1.3; radius: 0.6; color: "#e8efed" }
                Rectangle { x: 2.5; y: 8;    width: 5;   height: 1.3; radius: 0.6; color: "#e8efed" }
                Rectangle { x: 2.5; y: 11;   width: 3.4; height: 1.3; radius: 0.6; color: "#e8efed" }
                // Plus daneben (rechts, mittig)
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
