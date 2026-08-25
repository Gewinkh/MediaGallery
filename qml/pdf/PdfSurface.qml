pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Pdf
import QtMultimedia
import MediaGallery 1.0
import "../common"
import "../viewer"

// ─────────────────────────────────────────────────────────────────────────────
//  PdfSurface.qml - PDF-Anzeige in 100% QML (ersetzt PdfViewer(QWidget)).
//
//  Eigener vertikaler ListView aus PdfPageImage (kein PdfMultiPageView) für volle
//  Kontrolle über Seitengeometrie und das Annotations-Overlay (asynchron via
//  Viewer.requestPdfAnnotations).
//
//  Caching-Strategie (RAM-bewusst, beide Hebel deterministisch deckelbar):
//   • SCROLLEN: Die Seiten-ListView haelt ueber `pageCacheScreens` Viewporthoehen
//     Puffer je Richtung instanziiert+gerendert. Hoch-/Runterscrollen innerhalb
//     dieses Fensters laedt NICHT neu. Da QtQuick seinen internen Bild-Cache nicht
//     oeffentlich deckeln laesst, ist dieser delegate-basierte Puffer der einzige
//     wirklich kontrollierbare RAM-Deckel - wir nutzen bewusst IHN (cache:false).
//   • PDF-WECHSEL: Ein kleiner LRU-Pool (`pdfPoolSize`) haelt die zuletzt
//     geoeffneten PDFs GEPARST -> Hin-und-Her-Wechseln muss nicht neu parsen. Ein
//     warmes Dokument haelt v. a. die Seitenstruktur, NICHT die Seitenbitmaps.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    property string source: ""
    property var    annotations: []
    //  Nur die aktive Split-View-Kachel darf ihre fensterweiten Kürzel
    //  (Ctrl+C/A/V, Alt+Q, Entf, Ctrl+Z/Shift+Z) feuern - sonst sind sie bei
    //  mehreren offenen PDFs mehrdeutig und Qt feuert keines. Einzel-View=true.
    property bool   paneActive: true

    property real   zoom: 1.0
    property string fitMode: "page"          // "page" = ganze Seite, "width" = Breite
    property int    currentPage: 0
    property real   panX: 0                   // horizontaler Schwenk-Offset (Zoom-Pan)
    property int    _savePage: 0             // Resize: die stabile Seite sichern …
    property int    _stablePage: 0           // zuletzt SICHER erkannte Seite (Quelle für _savePage)
    property real   _saveFrac: 0             // … samt Innerseiten-Scrollanteil (0..1)
    property real   _stableFrac: 0           // zuletzt sicher erkannter Innerseiten-Anteil
    property bool   _resizing: false         // Resize-Phase aktiv -> updateCurrentPage gesperrt
    property bool   _restoring: false        // … und danach deterministisch wiederherstellen

    property real   topInset: 0
    property real   bottomInset: 0
    //  Höhe der oben liegenden Word-Leiste (Ribbon). Sie ist ein OVERLAY über
    //  den Seiten (z: 4) und verdeckte deren Oberkante - unerreichbar, weil
    //  contentY nicht über den Listenanfang hinausgeht. Die Seitenliste
    //  bekommt daher einen gleich hohen Kopfraum: die Seite lässt sich
    //  vollständig unter die Leiste scrollen und ist ganz bearbeitbar
    //  (2026-07-17). Der Seiten-Fit bleibt bewusst unangetastet - sonst würde
    //  jedes Ein-/Ausblenden der Leiste die Seite neu skalieren (teuer, s.
    //  bottomInset-Kommentar am contentArea).
    readonly property real ribbonInset:
        (root.editCtl.editMode && root.editPanelVisible && PdfEdit.panelOnTop) ? 62 : 0
    property bool   thumbsVisible: true
    property real   wheelPageFraction: 0.5   // Anteil der Viewporthöhe je Rad-Raststufe

    // ── Textauswahl-Zustand (recycling-sicher im Root gehalten) ───────────────
    //  Die Highlights leben hier (nicht im wiederverwendeten Delegate); nur die
    //  Seite mit selPage zeichnet selRects -> Recycling verliert nichts.
    property int    selPage: -1              // Seite mit aktiver Auswahl (-1 = keine)
    property var    selRects: []             // normalisierte Highlight-Rechtecke {x,y,w,h}
    property bool   _selecting: false        // gerade am Ziehen?
    property var    _lastSel: null           // letzte Drag-Brüche (für Re-Query bei Ready)
    property bool   _pendingSelectAll: false // Strg+A vor Abschluss des Lazy-Loads -> nachziehen
    property int    _linkFromId: 0           // Reflow-Verkettung: Ausgangsbox, wartet auf Zielbox-Klick

    // ── Audio (PdfAudio-Singleton) ────────────────────────────────────────────
    //  Eingebettete PDF-Audios in einer seitenbezogenen Leiste rechts + Mini-Player.
    //  audioClips = ALLE Clips des Dokuments [{id,page,x,y,w,h,label}]; die Leiste
    //  zeigt nur die der AKTUELLEN Seite, die Hotspots erscheinen je Seite.
    property bool   audioPanelVisible: false
    property var    audioClips: []
    property bool   documentHasAudio: false
    property var    _audioMeta: ({})         // id -> { url, durMs } (extrahiert/gecached)
    property var    _audioPos:  ({})         // id -> zuletzt gehoerte Position (ms, Resume)
    property int    activeClipId: -1         // aktuell geladener/spielender Clip
    property string _activeTitle: ""         // Anzeigetitel des Mini-Players
    property int    _audioRev: 0             // Counter -> erzwingt Binding-Refresh (Dauer/Resume)
    property int    _pendingSeekMs: -1       // Seek nach Medienladen anwenden
    property bool   _pendingPlay: false
    // Akzentfarbe der Audio-UI: Theme-Akzent oder Apple-Systemblau (Einstellung).
    readonly property color audioAccent: App.audioAccentApple ? "#0A84FF" : App.themeAccent

    function _clipsOnPage(p) {
        var r = []
        for (var i = 0; i < audioClips.length; i++)
            if (audioClips[i].page === p) r.push(audioClips[i])
        return r
    }
    function _audioLabel(clip, idxOnPage) { return App.uiText(App.language, "PdfAudioItemLabel").arg(idxOnPage + 1) }

    // ── Dezentrale Editor/Text/Audio-Controller (EIGENE Instanzen je Kachel) ──
    //  Früher globale Singletons (PdfEdit/PdfText/PdfAudio) -> in der geteilten
    //  Ansicht teilten sich mehrere PDF-Kacheln DENSELBEN Editmodus/Boxen/Text/
    //  Audio. Jetzt besitzt JEDE PdfSurface eigene Controller -> Boxen, Editmodus,
    //  Auswahl, Text-Selektion und Audio sind pro geöffneter Datei getrennt.
    //  editCtl ist als Property exponiert, damit die Kinder (PdfEditBox/-Toolbar/
    //  -Panel) über surface.editCtl darauf zugreifen. Die globale Einstellung
    //  panelOnTop kommt weiterhin vom PdfEdit-Singleton (Einstellungen ▸ Editor).
    property PdfEditController editCtl: PdfEditController {}
    PdfTextController  { id: pdfTextCtl }
    PdfAudioController { id: pdfAudioCtl }
    //  Für Kinder (Panel/Werkzeugleiste) - der Textcontroller dieser Kachel.
    property alias textCtl: pdfTextCtl

    // ── PDF-Editor (dezentraler Controller: root.editCtl) ─────────────────────
    //  Overlay-Textboxen über den Seiten: Modell/Undo/Sidecar/Export leben im
    //  Controller; hier nur UI-Zustand + drei Brücken-Mechanismen:
    //   • editCommitRev  - Zähler-Bump zwingt alle Box-Delegates, eine offene
    //     Textbearbeitung DETERMINISTISCH abzuschließen (vor Undo/Redo/Export/
    //     Moduswechsel/Löschen) - synchron, ohne Delegate-Registry.
    //   • _snapCache     - Zeilenrechtecke je Seite (pdfTextCtl.textLineRects) für
    //     den Zeilenfang; einmal je Seite geholt, bei Dokumentwechsel geleert.
    //   • _autoEditId    - frisch erstellte Box startet direkt die Bearbeitung
    //     (das Delegate prüft die ID in Component.onCompleted).
    property bool editPanelVisible: false
    property bool snapEnabled: true          // Zeilenfang (Toolbar-Toggle)
    // Sichtbarkeit der Notizen im ANSICHTS-Modus (Alt+Q-Toggle + Toolbar-Auge).
    // Im Editmodus sind Notizen immer sichtbar; Eintritt setzt zurück auf true.
    property bool notesVisible: true
    property int  editCommitRev: 0
    property var  _snapCache: ({})
    property int  _autoEditId: -1
    // Seite, von der aus gerade eine Notiz gezogen wird (−1 = keine): hebt das
    // Seiten-Delegate per z über seine Nachbarn, damit die Notiz beim Ziehen
    // ÜBER den Seitenrand hinaus sichtbar über die Lücke auf die Nachbarseite
    // gleitet (sonst malte die später erzeugte Nachbarseite sie zu).
    property int  editDragPage: -1
    // „Text bearbeiten" ist aktiv UND ein Caret steht - dann gehören Tasten
    // (auch „+"/„−") dem Text und nicht den Ansichts-Kürzeln.
    readonly property bool caretActive: editCtl.editMode && editCtl.tool === 7
                                        && editCtl.caretPage >= 0

    function commitEditing() { editCommitRev++ }

    //  Seitenübergreifendes Verschieben: übersetzt eine über den Seitenrand
    //  hinausgezogene Oberkante (yPt auf Seite page) in Zielseite + lokale
    //  y-Koordinate. Kriterium: Box-MITTE hat den Rand überschritten; die
    //  Lücke zwischen den Seiten (pages.spacing + 4 px Delegate-Puffer) wird
    //  über die aktuelle Skala in Punkte umgerechnet. Läuft iterativ - auch
    //  ein Drag über mehrere Seiten hinweg landet korrekt. Ergebnis ist in
    //  die Zielseite geklemmt.
    function resolveCrossPage(page, yPt, hPt, scale) {
        var p = Math.max(0, Math.min(page, root.pageCount - 1))
        var y = yPt
        if (!root.docReady || scale <= 0)
            return { page: p, y: Math.max(0, y) }
        var gapPt = (pages.spacing + 4) / scale
        // nach UNTEN über den Rand (Mitte unterhalb der Seitenunterkante)
        var ph = root.doc.pagePointSize(p).height
        while (p < root.pageCount - 1 && ph > 0 && y + hPt / 2 > ph) {
            y -= ph + gapPt
            p++
            ph = root.doc.pagePointSize(p).height
        }
        // nach OBEN über den Rand (Mitte oberhalb der Seitenoberkante)
        while (p > 0 && y + hPt / 2 < 0) {
            p--
            ph = root.doc.pagePointSize(p).height
            y += ph + gapPt
        }
        ph = root.doc.pagePointSize(p).height
        if (ph > 0)
            y = Math.max(0, Math.min(y, Math.max(0, ph - hPt)))
        else
            y = Math.max(0, y)
        return { page: p, y: y }
    }

    //  Oberkante yPt an die nächste erkannte Textzeile fangen (Toleranz: max aus
    //  7 pt und 75 % der Zeilenhöhe). Ohne Textebene/deaktiviert -> unverändert.
    function snapYPt(page, yPt, hPt) {
        if (!snapEnabled || !pdfTextCtl.ready || !root.docReady)
            return yPt
        var pts = root.doc.pagePointSize(page)
        if (!pts || pts.height <= 0)
            return yPt
        var lines = _snapLines(page)
        var best = yPt
        var bestD = 1e9
        for (var i = 0; i < lines.length; i++) {
            var ly  = lines[i].y * pts.height
            var lh  = lines[i].h * pts.height
            var tol = Math.max(7, lh * 0.75)
            var d   = Math.abs(yPt - ly)
            if (d < tol && d < bestD) { bestD = d; best = ly }
        }
        return best
    }

    //  Zeilenrechtecke einer Seite (normalisiert) - je Seite EINMAL geholt.
    function _snapLines(page) {
        var c = _snapCache[page]
        if (c !== undefined)
            return c
        var l = pdfTextCtl.ready ? pdfTextCtl.textLineRects(page) : []
        if (pdfTextCtl.ready)
            _snapCache[page] = l
        return l
    }

    //  Export starten. Ziel ist IMMER eine neue Kopie „…_bearbeitet(.n).pdf"
    //  neben dem Original - das Original (und damit die Anzeige-Handles)
    //  bleibt unangetastet, die Notizen bleiben über das Sidecar reversibel.
    function startPdfExport() {
        if (root.editCtl.busy || !root.docReady)
            return
        root.commitEditing()
        root.editCtl.exportPdf()
    }
    //  Verlustfreies Content-Stream-Editing (Text ersetzen direkt in der
    //  Textebene) - fällt im Controller automatisch auf den Raster-Export zurück.
    function startContentExport() {
        if (root.editCtl.busy || !root.docReady)
            return
        root.commitEditing()
        root.editCtl.exportContentEdited()
    }

    //  EIN Export-Weg für die Oberfläche: welcher der beiden oben tatsächlich
    //  läuft, entscheidet die Einstellung `PdfEdit.exportLossless`
    //  (Einstellungen -> Editor -> PDF-Editor -> Export). Vorher gab es dafür zwei
    //  Knöpfe nebeneinander, was die Wahl bei JEDEM Export erzwang, obwohl sie
    //  eine Grundsatzentscheidung ist. Der verlustfreie Weg fällt im Controller
    //  ohnehin selbsttätig auf den Raster-Export zurück, wo er nicht sicher
    //  anwendbar ist - die Einstellung kann also nichts unmöglich machen.
    function startExport() {
        if (PdfEdit.exportLossless) root.startContentExport()
        else                        root.startPdfExport()
    }

    // ── Tunbare Cache-Deckel ──────────────────────────────────────────────────
    //  Scroll-Vorhalte/-Cache in Viewporthoehen je Richtung. 1.5 ≈ eine Seite
    //  ober- und unterhalb bleibt gerendert (Scroll-zurueck ohne Reload). Hoeher
    //  = mehr RAM, ABER auch mehr KONKURRIERENDE Renderings: PDFium serialisiert
    //  alle render()-Aufrufe EINER Dokument-Instanz ueber einen Mutex, d. h. jede
    //  vorab gerenderte Nachbarseite verzoegert die gerade sichtbar werdende.
    //  1.5 ist die Balance aus „naechste Seite ist schon da“ und „sichtbare Seite
    //  rendert zuerst“. RAM ≈ (1 + 2·pageCacheScreens) sichtbare Seitenbitmaps.
    property real   pageCacheScreens: 1.5
    //  Anzahl GEPARSTER PDFs, die fuer schnelles Zurueckwechseln warm bleiben.
    property int    pdfPoolSize: 3

    // ── Gestaffeltes Laden (das Kern-Performance-Muster) ──────────────────────
    //  Problem: Wuerden Hauptseiten UND Thumbnail-Leiste beim Oeffnen gleichzeitig
    //  rendern, konkurrierten ~8 Thumbnails + mehrere Vorab-Hauptseiten mit der
    //  einen sichtbaren Seite um denselben PDFium-Render-Mutex -> spuerbare
    //  Oeffnen-Latenz, besonders bei schweren Seiten (Vektorgrafik/Bilder), wo
    //  jedes render() unabhaengig von der Zielgroesse teuer ist.
    //
    //  Loesung (wie die bewaehrte QPdfView-Version): zuerst NUR die sichtbare
    //  Seite rendern; Vorhalte-Puffer und Thumbnail-Leiste erst nach einer kurzen
    //  Verzoegerung freischalten - dann ist die erste Seite schon auf dem Schirm.
    property bool   _warm: false                 // false -> nur sichtbare Seite rendern
    property int    warmupDelayMs: 160           // Verzoegerung bis Puffer+Thumbnails

    // docId des aktiven PDFs im RAM-Thumbnail-Provider (0 = noch keine).
    // Baut die image://pdfthumb/<docId>/<page>-URLs der Seitenleiste.
    property int    _thumbDocId: 0

    //  Quelle der Vorschauleiste = die Datei, die die Ansicht rendert.
    function _thumbSource() {
        var p = root.editCtl.renderSourcePath()
        return (p && p.length > 0) ? p : root.source
    }

    // Warmlauf nach jedem (Neu-)Laden anstossen: erst sichtbare Seite, dann Rest.
    function _beginWarmup() {
        root._warm = false
        warmupTimer.restart()
    }
    Timer {
        id: warmupTimer
        interval: root.warmupDelayMs
        repeat: false
        onTriggered: {
            // Vorrendern der Seitenleiste anstossen, BEVOR die Delegates ueber
            // _warm entstehen -> sie binden sofort die korrekte docId. Sichtbare
            // Seite (currentPage) wird im Provider zuerst gerendert.
            //  Vorschauen der Datei, die auch die Seiten zeigt: Bei geändertem
            //  Seiten-Plan ist das die gebackene Arbeitsdatei - sonst zeigte die
            //  Leiste die Originalreihenfolge, während die Ansicht längst
            //  umsortiert ist.
            if (root.source.length > 0)
                root._thumbDocId = PdfThumbs.ensureDocument(root._thumbSource(),
                                                            root.currentPage)
            root._warm = true
        }
    }

    // ── Dokument-Pool (LRU) ───────────────────────────────────────────────────
    property var    doc: null                // aktuell aktives PdfDocument
    readonly property bool docReady: !!doc && doc.status === PdfDocument.Ready
    readonly property int  pageCount: docReady ? doc.pageCount : 0

    property var    _pool: ({})              // localPath -> PdfDocument
    property var    _poolOrder: []           // LRU-Reihenfolge (alt -> neu)

    Component { id: _pdfDocFactory; PdfDocument {} }

    // Aktiviert das PDF fuer `localPath` (aus Pool wiederverwenden oder neu laden).
    function _activateDoc(localPath) {
        var key = root._localPath(localPath)
        var url = localPath.indexOf("file:") === 0 ? localPath : "file://" + localPath
        var d = root._pool[key]
        if (!d) {
            d = _pdfDocFactory.createObject(root)
            if (!d) return
            d.source = url
            root._pool[key] = d
        }
        // LRU: Schluessel ans Ende (juengster).
        var i = root._poolOrder.indexOf(key)
        if (i >= 0) root._poolOrder.splice(i, 1)
        root._poolOrder.push(key)
        root.doc = d
        root._evictPool()
    }

    // Aelteste Dokumente verdraengen, sobald der Pool seinen Deckel ueberschreitet.
    function _evictPool() {
        while (root._poolOrder.length > Math.max(1, root.pdfPoolSize)) {
            var victim = root._poolOrder.shift()
            var vd = root._pool[victim]
            delete root._pool[victim]
            if (vd && vd !== root.doc) { vd.source = ""; vd.destroy() }
        }
    }

    function _clearPool() {
        for (var k in root._pool) {
            var d = root._pool[k]
            if (d) { d.source = ""; d.destroy() }
        }
        root._pool = ({})
        root._poolOrder = []
        root.doc = null
    }

    Component.onDestruction: { _clearPool(); pdfTextCtl.releaseDocument(); pdfAudioCtl.releaseDocument(); root.editCtl.releaseDocument() }

    function release() {
        // Leichtgewichtig: nur Overlays stoppen. Das RENDER-Dokument bleibt im Pool
        // warm (kein doc.source="" mehr) -> Zurueckwechseln muss nicht neu parsen.
        mediaLoader.active = false
        _saveActivePos()
        // Player-Instanz restlos zerstören - releaseDocument() (bzw. der nächste
        // prepare()) löscht die Temp-WAVs; eine noch lebende Instanz würde Handle
        // und Alt-Zustand behalten (Windows-Dateisperre, verworfenes play()).
        audioPlayer.reset()
        root.activeClipId = -1
        root._activeTitle = ""
        annotations = []
        // Das separate AUSWAHL-Dokument dagegen freigeben (RAM-Prio 1): es wird beim
        // naechsten Markieren ohnehin wieder lazy geladen.
        clearSelection()
        pdfTextCtl.releaseDocument()
        // Editor: ungespeicherte Overlay-Änderungen automatisch ins Sidecar
        // sichern und Modell + Undo-Stack leeren (kein stiller Verlust).
        root.editPanelVisible = false
        root.editCtl.releaseDocument()
    }

    onSourceChanged: {
        if (source.length > 0) {
            zoom = 1.0
            fitMode = "page"
            currentPage = 0
            panX = 0                         // Schwenk zurücksetzen (neues Dokument)
            _stablePage = 0                  // stabile Seite zurücksetzen
            _stableFrac = 0                  // … samt Innerseiten-Anteil
            _resizing = false; _restoring = false
            annotations = []                 // bis der asynchrone Scan zurueckkommt
            clearSelection()                 // evtl. Auswahl des vorherigen PDFs verwerfen
            // Audio-Zustand des vorherigen PDFs verwerfen + neuen Scan anstoßen (lazy).
            // WICHTIG: Player-Instanz vollständig zerstören, BEVOR pdfAudioCtl.prepare()
            // (-> releaseDocument) die Temp-WAVs des alten Dokuments löscht - sonst
            // hält der Player noch ein offenes Handle auf die letzte WAV.
            audioPlayer.reset()
            root.audioClips = []
            root.documentHasAudio = false
            root.activeClipId = -1
            root._activeTitle = ""
            root._audioMeta = ({})
            root._audioPos = ({})
            root.audioPanelVisible = false
            pdfAudioCtl.prepare(root.source)
            // Editor: Zeilenfang-Cache + Panel des vorherigen PDFs verwerfen;
            // setDocument() sichert dessen ungespeicherte Änderungen automatisch
            // ins Sidecar und lädt das Overlay des neuen Dokuments.
            root._snapCache = ({})
            root._autoEditId = -1
            root.editPanelVisible = false
            root.editCtl.setDocument(root.source)
            _activateDoc(root.source)
            // Bei einem bereits warmen (Ready) Dokument feuert kein statusChanged ->
            // Scrollposition hier zuruecksetzen und den Warmlauf direkt starten.
            if (root.docReady) {
                // Origin-bewusst an den Dokumentanfang (s. clampContentY).
                pages.positionViewAtBeginning(); root.currentPage = 0
                _beginWarmup()
                _ensurePlanInit()           // Aufgabe 3: Seiten-Plan initialisieren
            }
            // Annotationen NICHT blockierend holen -> der PDF-Wechsel laggt nicht
            // mehr. Die Badges erscheinen, sobald pdfAnnotationsReady feuert.
            Viewer.requestPdfAnnotations(root.source)
        } else {
            release()
        }
    }

    // Statuswechsel des AKTUELLEN Dokuments (Connections retargetet bei doc-Wechsel).
    Connections {
        target: root.doc
        function onStatusChanged() {
            if (root.doc && root.doc.status === PdfDocument.Ready) {
                // Origin-bewusst an den Dokumentanfang (s. clampContentY).
                pages.positionViewAtBeginning()
                root.currentPage = 0
                // Erst die sichtbare Seite rendern lassen, dann Puffer+Thumbnails.
                root._beginWarmup()
                root._ensurePlanInit()      // Aufgabe 3: Seiten-Plan initialisieren
            }
        }
    }

    // file://-Praefix abstreifen -> robuster Vergleich gegen den vom Viewer
    // emittierten lokalen Pfad (toLocalPath), plattformuebergreifend.
    function _localPath(s) {
        return s.indexOf("file://") === 0 ? s.substring(7) : s
    }

    // ── Aufgabe 3: Seiten-Plan ────────────────────────────────────────────────
    //  Sobald das PRISTINE Dokument bereit ist, die Quell-Seitenzahl an den
    //  Controller melden (initialisiert bzw. validiert den Plan). Nur solange
    //  noch kein Plan steht (viewPageCount==0) - nach dem Reload der gebackenen
    //  Arbeitsdatei ist er gesetzt und wird nicht überschrieben.
    function _ensurePlanInit() {
        if (root.docReady && root.editCtl.viewPageCount === 0)
            root.editCtl.setSourcePageCount(root.doc.pageCount)
    }

    //  Nach einer Seiten-Plan-Änderung rendert die Ansicht die gebackene
    //  Arbeitsdatei (renderSourcePath(): nicht-destruktiv .mgpreview.pdf,
    //  destruktiv die .pdf selbst; bei Identität wieder das Original). Die Datei
    //  kann sich in-place geändert haben -> gepooltes Dokument verwerfen und neu
    //  laden. So bleibt „Ansichts-Index == Seitenindex der Datei" erhalten und
    //  die gesamte Scroll-/Thumbnail-/Overlay-Logik gilt unverändert.
    function _reloadRenderDoc() {
        var p = root.editCtl.renderSourcePath()
        if (!p || p.length === 0) return
        var key = root._localPath(p)
        var old = root._pool[key]
        if (old) {
            delete root._pool[key]
            var i = root._poolOrder.indexOf(key)
            if (i >= 0) root._poolOrder.splice(i, 1)
        }
        _activateDoc(p)                     // frisch laden (Pool-Eintrag entfernt)
        if (old && old !== root.doc) { old.source = ""; old.destroy() }
    }

    // Ergebnis des asynchronen Scans entgegennehmen - nur uebernehmen, wenn es
    // zum aktuell angezeigten PDF gehoert (schnelles Vor/Zurueck ist sicher).
    Connections {
        target: Viewer
        function onPdfAnnotationsReady(path, anns) {
            if (root._localPath(path) === root._localPath(root.source))
                root.annotations = anns
        }
    }

    // Auswahl-Dokument wurde (asynchron) fertig geladen -> eine evtl. noch laufende
    // Drag-Auswahl nachholen, deren fruehe Abfragen mangels Dokument leer blieben
    // (relevant nur bei grossen PDFs, deren Laden laenger als der erste Drag dauert).
    Connections {
        target: pdfTextCtl
        function onReadyChanged() {
            if (!pdfTextCtl.ready)
                return
            // Editor: frisch geladenes Auswahl-Dokument -> Zeilenfang-Cache neu
            // aufbauen (der alte könnte leere Fallback-Listen enthalten).
            root._snapCache = ({})
            if (root._selecting && root._lastSel) {
                var s = root._lastSel
                root.selRects = pdfTextCtl.selectionBetween(s.page, s.a0, s.b0, s.a1, s.b1)
            } else if (root._pendingSelectAll) {
                root._pendingSelectAll = false
                root.selPage = root.currentPage
                root.selRects = pdfTextCtl.selectAllOnPage(root.currentPage)
            }
        }
        //  Suchlauf hat neue Treffer (er läuft stückweise) -> Anzeige nachziehen
        //  und beim ERSTEN Treffer gleich dorthin springen.
        function onSearchChanged() {
            root._searchRev++
            if (pdfTextCtl.searchCount > 0 && root.searchIndex < 0)
                root.goToHit(0)
            else if (pdfTextCtl.searchCount === 0)
                root.searchIndex = -1
        }
    }

    // ── PDF-Editor: Reaktionen auf Controller-Ereignisse ──────────────────────
    Connections {
        target: root.editCtl
        // Aufgabe 3: Seiten-Plan geändert -> gebackene Arbeitsdatei neu rendern.
        //  Die DATEI hat sich geändert (Seitenoperation, neue Textebene) -
        //  nicht nur die Anzeige: der Textcontroller hält sein eigenes
        //  Dokument und muss es neu lesen, sonst suchen und markieren wir
        //  weiter im alten Stand.
        function onDocumentRewritten() { root._reloadRenderDoc(); pdfTextCtl.reload() }
        //  Seitenstruktur geändert (umsortiert/gedreht/eingefügt/entfernt) ->
        //  Vorschauleiste neu rendern. BEWUSST nicht an documentRewritten
        //  gehängt: Ein Neubau der Textebene lässt die Struktur unberührt und
        //  würde die Vorschauen bei jedem Tippen verwerfen.
        function onPageStructureChanged() {
            if (root._warm && root.source.length > 0)
                root._thumbDocId = PdfThumbs.refreshDocument(root._thumbSource(),
                                                             root.currentPage)
        }
        function onEditModeChanged() {
            if (root.editCtl.editMode) {
                //  2026-07-17: Die Text-Auswahl BLEIBT beim Betreten des
                //  Editiermodus erhalten (vorher wurde sie verworfen - man
                //  markierte Text und verlor ihn beim Umschalten). Mit dem
                //  Auswahl-Werkzeug lässt sich auch IM Editiermodus markieren
                //  (createArea reicht Tool 0 an die Auswahl weiter); ein Klick
                //  auf ⇄ verwandelt die Markierung direkt in eine Ersetzen-Box.
                //  Auswahl-Dokument lazy laden - es liefert die Zeilen-
                //  rechtecke für den Zeilenfang.
                // Ausgeblendete Notizen (Alt+Q) wieder zeigen - man will sie
                // ja bearbeiten.
                root.notesVisible = true
                if (root.source.length > 0)
                    pdfTextCtl.prepare(root.source)
                // Word-Modus: die obere Leiste erscheint automatisch mit dem
                // Editmodus (wie das Ribbon in Word); ⚙ kann sie ausblenden.
                if (PdfEdit.panelOnTop)
                    root.editPanelVisible = true
            } else {
                root.editPanelVisible = false
            }
        }
        //  ── „Dokument durchsuchbar machen" ───────────────────────────────
        //  Der Lauf dauert je Seite rund eine Sekunde - ohne Rueckmeldung
        //  saehe es aus, als geschehe nichts.
        function onSearchableProgress(done, total) {
            if (done < total)
                root._toast(App.uiText(App.language, "PdfSearchableRunning")
                                .arg(done + 1).arg(total))
        }
        function onSearchableFinished(ok, pages, words, skipped, errorText) {
            if (!ok) {
                root._toast(errorText === "notext"
                            ? App.uiText(App.language, "PdfSearchableNoneToast")
                            : App.uiText(App.language, "PdfSearchableFailedToast")
                                  .arg(errorText))
                return
            }
            var msg = App.uiText(App.language, "PdfSearchableDoneToast")
                          .arg(pages).arg(words)
            if (skipped > 0)
                msg += App.uiText(App.language, "PdfSearchableSkippedNote").arg(skipped)
            root._toast(msg)
        }
        function onExportFinished(ok, targetPath, errorText) {
            if (ok)
                root._toast(App.uiText(App.language, "PdfEditExportDoneToast").arg(targetPath))
            else
                root._toast(App.uiText(App.language, "PdfEditExportFailedToast").arg(errorText))
        }
        //  Formular in eine Kopie geschrieben (bzw. gescheitert).
        function onFormSaved(ok, targetPath, errorText, flattened) {
            if (ok && flattened === true)
                //  Kopie trägt die geänderte Seitenfolge - dafür sind die
                //  Felder darin festgeschrieben. Das muss gesagt werden.
                root._toast(App.uiText(App.language, "PdfFormSavedFlattenedToast")
                                .arg(targetPath))
            else if (ok)
                root._toast(App.uiText(App.language, "PdfFormSavedToast").arg(targetPath))
            else
                root._toast(App.uiText(App.language, "PdfFormSaveFailedToast")
                                .arg(errorText.length > 0 ? errorText : "?"))
        }
        // Content-Stream-Editing nicht (vollständig) möglich -> Raster-Export.
        function onContentEditFellBack() {
            root._toast(App.uiText(App.language, "PdfContentFallbackToast"))
        }
        // Text-Eigenschaften öffnen sich AUTOMATISCH, sobald eine Notiz
        // erstellt oder ausgewählt wird (Erstellen setzt die Auswahl -> ein
        // Pfad genügt); der frühere ⚙-Toolbar-Button entfällt. Im
        // Seitenleisten-Modus verdrängt das Panel die Audio-Leiste
        // (Exklusivität wie zuvor); Abwählen lässt das Panel offen - es
        // schließt über sein ✕ und öffnet bei der nächsten Auswahl erneut.
        function onSelectedIdChanged() {
            if (root.editCtl.editMode && root.editCtl.selectedId >= 0) {
                root.editPanelVisible = true
                if (!PdfEdit.panelOnTop)
                    root.audioPanelVisible = false
            }
            // Reflow-Verkettung: wartet der Link-Modus auf ein Ziel und wird eine
            // ANDERE Box gewählt -> beide verketten und den Modus verlassen.
            if (root._linkFromId !== 0) {
                var to = root.editCtl.selectedId
                if (to >= 0 && to !== root._linkFromId) {
                    root.editCtl.linkChain(root._linkFromId, to)
                    root._linkFromId = 0
                    root._toast(App.uiText(App.language, "PdfChainDone"))
                }
            }
        }
        function onExportProgress(done, total) {
            root._toast(App.uiText(App.language, "PdfEditExportingToast").arg(done).arg(total))
        }
        function onOverlaySaved(ok) {
            root._toast(App.uiText(App.language,
                                   ok ? "PdfEditSavedToast" : "PdfEditSaveFailedToast"))
        }
        // „Text bearbeiten": Die Änderung ließ sich nicht in den Content-Stream
        // schreiben (z. B. Zeichen nicht in der Kodierung der Schrift) - der
        // Controller hat sie verworfen, der Nutzer erfährt WARUM.
        //  Der Absatz-Umbruch nach dem Tippen hat den Rest nicht mehr
        //  unterbringen können - das wird gesagt, statt ihn still über den
        //  Rand laufen zu lassen.
        function onReflowOverflow() {
            root._toast(App.uiText(App.language, "PdfReflowOverflow"))
        }
        function onTextEditFailed(reason) {
            root._toast(App.uiText(App.language, "PdfEditTextOpFailed")
                            .arg(reason.length > 0 ? reason : "?"))
        }
        // Zeichen-Layout der angeklickten Seite ist da - ist sie nicht
        // bearbeitbar, wird das gesagt statt still nichts zu tun.
        function onCaretReadyChanged() {
            if (root.editCtl.tool !== 7 || root.editCtl.caretReady)
                return
            //  Eingefügte/gedrehte Seite: Der Controller lehnt das Caret dort
            //  bewusst ab (die Ops adressieren die ungedrehte Quellseite) und
            //  meldet das über caretError - dann DIESEN Grund nennen.
            if (root.editCtl.caretError === "pagenotext") {
                root._toast(App.uiText(App.language, "PdfCaretPageNotEditable"))
                return
            }
            //  Seite gelesen, aber ohne Text: das ist kein Fehler, sondern eine
            //  andere Lage - und ein Klick, der gar nichts meldet, wirkt kaputt.
            if (root.editCtl.caretError === "pagenotext_empty") {
                root._toast(App.uiText(App.language, "PdfCaretPageNoText"))
                return
            }
            //  Sonst: den WIRKLICHEN Grund nennen. Ein Pauschalsatz macht aus
            //  einem behebbaren Fall ein Rätsel - auch für Fehlermeldungen.
            if (root.editCtl.caretPage >= 0 && root.editCtl.caretError.length > 0)
                root._toast(App.uiText(App.language, "PdfEditCaretUnavailable")
                                .arg(root.editCtl.caretError))
        }
    }

    // ── Audio-Wiedergabe (EIN Player; Mini-Player + seitenbezogene Leiste) ─────
    function playClip(id) {
        if (id < 0) return
        if (root.activeClipId === id) {                 // gleicher Clip -> Play/Pause
            if (audioPlayer.playbackState === MediaPlayer.PlayingState) audioPlayer.pause()
            else audioPlayer.play()
            return
        }
        _saveActivePos()
        root.activeClipId = id
        var clip = null
        for (var i = 0; i < audioClips.length; i++) if (audioClips[i].id === id) { clip = audioClips[i]; break }
        if (clip) {
            var onPage = _clipsOnPage(clip.page); var k = 0
            for (var j = 0; j < onPage.length; j++) if (onPage[j].id === id) { k = j; break }
            root._activeTitle = App.uiText(App.language, "PdfAudioActiveTitle").arg(k + 1).arg(clip.page + 1)
        }
        var meta = root._audioMeta[id]
        if (meta && meta.url) _startActive(meta.url)
        else { root._pendingPlay = true; pdfAudioCtl.requestClip(id) }   // async -> onClipReady startet
    }

    function _startActive(url) {
        // Resume-Position ZUERST sichern (vor stop(): stop() setzt position=0 und
        // loest onPositionChanged(0) aus, das _audioPos[activeClipId] sonst transient
        // auf 0 ueberschriebe).
        root._pendingSeekMs = root._audioPos[root.activeClipId] || 0
        root._pendingPlay = true
        // Vollständige Neu-Initialisierung: die neue Quelle wird in eine FRISCHE
        // MediaPlayer-Instanz geladen (loadFresh zerstört die alte restlos) -
        // kein wiederverwendeter Demuxer-/Handle-/Sink-Zustand, der die
        // Wiedergabe auf jedem zweiten Wechsel verwerfen könnte.
        audioPlayer.loadFresh(url)
        // FFmpeg-Backend (Linux): das erste play() direkt bei LoadedMedia wird auf
        // jedem zweiten Quellenwechsel VERWORFEN (Player bleibt Stopped, kein Ton) -
        // exakt das, was ein zweiter manueller Klick heilt. playRetry ruft play()
        // wiederholt auf, bis playbackState wirklich Playing ist -> automatisiert den
        // zweiten Klick und beseitigt das "jede zweite stumm"-Muster.
        playRetry.tries = 0
        playRetry.start()
    }

    function _saveActivePos() {
        if (root.activeClipId >= 0) {
            var m = root._audioPos; m[root.activeClipId] = audioPlayer.position; root._audioPos = m
            root._audioRev++
        }
    }

    // Bei offener Leiste die Clips der aktuellen Seite extrahieren (Dauer + bereit).
    function _ensurePageClipsExtracted() {
        if (!root.audioPanelVisible) return
        var cs = _clipsOnPage(root.currentPage)
        for (var i = 0; i < cs.length; i++)
            if (!root._audioMeta[cs[i].id]) pdfAudioCtl.requestClip(cs[i].id)
    }

    function _savedPos(id) { return root._audioPos[id] || 0 }
    function _clipDurMs(id) { var m = root._audioMeta[id]; return m ? m.durMs : 0 }
    function _fmtTime(ms) {
        if (!ms || ms < 0) ms = 0
        var s = Math.floor(ms / 1000); var mm = Math.floor(s / 60); var ss = s % 60
        return mm + ":" + (ss < 10 ? "0" + ss : ss)
    }

    onAudioPanelVisibleChanged: if (audioPanelVisible) _ensurePageClipsExtracted()

    // ── Mono-Play ──────────────────────────────────────────────────────────────
    //  Eindeutiges Token dieser Wiedergabestelle (PDF-Audio dieser Kachel; die
    //  Objekt-Identität macht es je Instanz verschieden). Der Play-Start meldet
    //  sich in playerComponent.onPlaybackStateChanged via App.announcePlayback;
    //  startet eine ANDERE Stelle (fremdes Token), pausiert die Wiedergabe hier
    //  - Position bleibt erhalten (Pause, kein Stop). App sendet playbackStarted
    //  NUR bei aktivierter Mono-Play-Option (Einstellungen ▸ Allgemein).
    readonly property string _playToken: "pdfaudio-" + root

    Connections {
        target: App
        function onPlaybackStarted(token) {
            if (token === root._playToken) return
            // Läuft hier gerade Audio -> pausieren (Position bleibt erhalten).
            if (audioPlayer.playbackState === MediaPlayer.PlayingState)
                audioPlayer.pause()
            // Wartet hier noch ein asynchroner Play (Clip-Extraktion/playRetry),
            // hat der FREMDE Start zuletzt gewonnen -> schwebenden Play abbrechen,
            // sonst „stiehlt" der verspätete Retry die Wiedergabe zurück.
            root._pendingPlay = false
        }
    }

    // ── Audio-Player-Fassade: JEDE Wiedergabe = frische MediaPlayer-Instanz ────
    //  Stabiler Zugriffspunkt (id: audioPlayer) für die gesamte UI (Slider/
    //  Buttons binden weiter an audioPlayer.position/duration/playbackState).
    //  Die eigentliche MediaPlayer+AudioOutput-Instanz wird für jede Wiedergabe
    //  KOMPLETT NEU erzeugt und die alte restlos zerstört - keine Wieder-
    //  verwendung von Demuxer, Datei-Handle oder Audio-Sink. Hintergrund: das
    //  FFmpeg-Backend behält nach einem Quellenwechsel AUF DERSELBEN Instanz
    //  internen Pipeline-Zustand und verwirft die Wiedergabe auf jedem zweiten
    //  Wechsel (Datei wird korrekt geprobt/geladen - „Input #0, wav, …" - aber
    //  es startet kein Ton). Mit einer frischen Instanz existiert dieser
    //  Alt-Zustand gar nicht erst; zusätzlich sind die Temp-WAVs nach reset()
    //  garantiert ungesperrt (löschbar).
    Item {
        id: audioPlayer
        visible: false

        // Aktive Instanz (playerComponent) - null, wenn nichts geladen ist.
        property var _inst: null

        // PERSISTENTER Audio-Sink: EINMAL geöffnet, für JEDE Wiedergabe
        // wiederverwendet. Nur der MediaPlayer (Demuxer/Pipeline) wird je
        // Wiedergabe neu erzeugt - der eigentliche Grund für die frische Instanz.
        // Vorher trug jeder MediaPlayer seinen EIGENEN AudioOutput; dessen
        // schnelles Öffnen/Schließen bei jedem Quellenwechsel ließ das
        // Audiogerät (PipeWire/Pulse) reproduzierbar bei JEDER ZWEITEN
        // Wiedergabe stumm bleiben (Player meldet Playing, aber kein Ton).
        // Ein dauerhaft offener Sink kennt diesen Öffnen/Schließen-Wechsel nicht.
        AudioOutput { id: sharedAudioOut }

        // Reaktive Spiegel-Properties (Ruhewerte, solange keine Instanz lebt).
        readonly property int  playbackState: _inst ? _inst.playbackState : MediaPlayer.StoppedState
        readonly property int  mediaStatus:   _inst ? _inst.mediaStatus   : MediaPlayer.NoMedia
        readonly property real position:      _inst ? _inst.position      : 0
        readonly property real duration:      _inst ? _inst.duration      : 0

        function play()   { if (_inst) _inst.play() }
        function pause()  { if (_inst) _inst.pause() }
        function stop()   { if (_inst) _inst.stop() }
        function seek(ms) { if (_inst) _inst.position = ms }

        // Neue Quelle in eine FRISCHE MediaPlayer-Instanz laden (alte vorher
        // restlos weg), die den PERSISTENTEN Sink bespielt.
        function loadFresh(url) {
            reset()
            _inst = playerComponent.createObject(audioPlayer,
                                                 { source: url, audioOutput: sharedAudioOut })
        }

        // Instanz vollständig zerstören: erst Fassade abkoppeln (Bindings fallen
        // auf Ruhewerte), dann stoppen, den gemeinsamen Sink lösen (er darf NICHT
        // mit dem Player sterben), Quelle leeren (Datei-Handle/Demuxer SOFORT
        // freigeben - destroy() ist verzögert) und Objekt zerstören.
        function reset() {
            var old = _inst
            _inst = null
            if (old) {
                // ZUERST abkoppeln: stop() einer SPIELENDEN Instanz durchläuft
                // mediaStatus -> LoadedMedia; deren Handler sah _pendingPlay
                // (vom bereits eingeleiteten Wechsel), startete die ALTE Quelle
                // erneut und konsumierte _pendingPlay - der NEUE Clip wurde
                // dann nie gestartet (deterministisch „jeder zweite Wechsel
                // stumm"). detached schaltet alle Handler der alten Instanz ab
                // (auch positionChanged(0), das sonst die Resume-Position des
                // neuen Clips überschriebe).
                old.detached = true
                old.stop()
                old.audioOutput = null
                old.source = ""
                old.destroy()
            }
        }

        Component {
            id: playerComponent
            MediaPlayer {
                // Wird in reset() gesetzt, BEVOR die alte Instanz gestoppt/zerstört
                // wird: ihre Handler dürfen dann weder _pendingPlay konsumieren noch
                // play() auf der alten Quelle auslösen noch Positionen schreiben.
                property bool detached: false
                // audioOutput wird bei createObject auf den persistenten Sink gesetzt.
                // Robust gegen das FFmpeg-Backend (Linux): play() wird bei LoadedMedia
                // teils VERWORFEN (zu früh). Erstversuch hier (schneller Pfad), die
                // eigentliche Absicherung uebernimmt playRetry (wiederholt play(), bis
                // Playing). Es wird NIE auf 0 gesucht (ein redundanter Seek-auf-0 ließ
                // die erste Wiedergabe hängen); ein echter Resume-Sprung (>0) erfolgt
                // erst, NACHDEM die Wiedergabe läuft.
                onMediaStatusChanged: {
                    if (detached) return
                    if ((mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)
                            && root._pendingPlay)
                        play()
                }
                onPlaybackStateChanged: {
                    if (detached) return
                    if (playbackState === MediaPlayer.PlayingState) {
                        root._pendingPlay = false
                        if (root._pendingSeekMs > 0) { position = root._pendingSeekMs; root._pendingSeekMs = -1 }
                        // Mono-Play: Start (auch Resume) melden - andere
                        // Wiedergabestellen pausieren sich daraufhin.
                        App.announcePlayback(root._playToken)
                    }
                }
                onErrorOccurred: function(err, errStr) {
                    if (err !== MediaPlayer.NoError) console.log("MediaGallery Audio-Fehler:", err, errStr)
                }
                // Nur in das einfache Objekt schreiben (Resume) - KEIN Reassign -> keine
                // Binding-Last je Tick. Der aktive Slider liest audioPlayer.position direkt.
                onPositionChanged: if (!detached && root.activeClipId >= 0) root._audioPos[root.activeClipId] = position
            }
        }
    }

    // Wiederholt play(), bis die Wiedergabe wirklich laeuft (Sicherheitsnetz für
    // den Fall, dass das erste play() direkt bei LoadedMedia verworfen wird).
    // Stoppt sich selbst, sobald Playing erreicht ist, _pendingPlay zurueckgesetzt
    // wurde, oder nach einer Sicherheitsgrenze (Schutz vor Endlos-Retry bei
    // InvalidMedia).
    Timer {
        id: playRetry
        interval: 80
        repeat: true
        property int tries: 0
        onTriggered: {
            if (!root._pendingPlay || audioPlayer.playbackState === MediaPlayer.PlayingState) {
                stop(); tries = 0; return
            }
            if (++tries > 25) { stop(); tries = 0; return }   // ~2 s, dann aufgeben
            var ms = audioPlayer.mediaStatus
            if (ms === MediaPlayer.LoadedMedia || ms === MediaPlayer.BufferedMedia)
                audioPlayer.play()
        }
    }

    Connections {
        target: pdfAudioCtl
        function onReadyChanged() {
            if (pdfAudioCtl.ready) {
                root.audioClips = pdfAudioCtl.clips()
                root.documentHasAudio = pdfAudioCtl.documentHasAudio
                root._ensurePageClipsExtracted()
            } else {
                root.audioClips = []
                root.documentHasAudio = false
            }
        }
        function onClipReady(id, url, durMs) {
            if (url.length > 0) {
                var m = root._audioMeta; m[id] = { url: url, durMs: durMs }; root._audioMeta = m
                root._audioRev++
                if (id === root.activeClipId && root._pendingPlay) root._startActive(url)
            }
        }
    }

    // ── Tastenkuerzel: Kopieren / Seite komplett markieren ─────────────────────
    //  WindowShortcut-Kontext (Standard) -> feuert, solange dieses PDF im Vollbild
    //  aktiv ist. Copy ist nur scharf, wenn wirklich Text markiert ist (kein
    //  mehrdeutiger Konflikt). Explizite Sequenzen statt StandardKey, damit keine
    //  Zweitbindung den Shortcut mehrdeutig macht.
    Shortcut {
        sequence: "Ctrl+C"
        enabled: root.paneActive && root.docReady && pdfTextCtl.selectedText.length > 0
        onActivated: pdfTextCtl.copyToClipboard()
    }
    // Notizen-Toggle (Alt+Q blendet die Post-its aus, erneutes Drücken wieder
    // ein). Wirkt in BEIDEN Modi - der Eintritt in den Editmodus erzwingt
    // notesVisible=true, danach kann auch dort ausgeblendet werden (das
    // Erstellen einer Notiz blendet wieder ein, s. createArea).
    //  Suchen wie überall: Strg+F (bzw. was die Plattform dafür vorsieht).
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: root.paneActive && root.docReady
        onActivated: root.toggleSearch()
    }
    Shortcut {
        sequence: "Alt+Q"
        enabled: root.paneActive && root.docReady
        onActivated: root.notesVisible = !root.notesVisible
    }
    // Notiz KOPIEREN (Edit-Modus, Auswahl vorhanden, nicht beim Tippen) - greift
    // vor dem Textebenen-Ctrl+C oben (dessen enabled verlangt markierten Text,
    // sich gegenseitig ausschließend -> keine Mehrdeutigkeit).
    Shortcut {
        sequence: "Ctrl+C"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode
                 && root.editCtl.selectedId >= 0 && !root.editCtl.textEditing
        onActivated: { root.commitEditing(); root.editCtl.copySelected() }
    }
    // Notiz EINFÜGEN (Edit-Modus, Zwischenablage gefüllt).
    Shortcut {
        sequence: "Ctrl+V"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode
                 && root.editCtl.hasClipboard && !root.editCtl.textEditing
        onActivated: root.editCtl.paste()
    }
    Shortcut {
        sequence: "Ctrl+A"
        enabled: root.paneActive && root.docReady
        onActivated: root.selectAllCurrentPage()
    }
    // Entf löscht die AUSGEWÄHLTE Notiz (nur im Editmodus). Gesperrt, solange
    // eine Inline-Textbearbeitung läuft - dort gehört Entf dem TextEdit
    // (Zeichen löschen), nicht der Box (root.editCtl.textEditing).
    Shortcut {
        sequence: "Delete"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode
                 && root.editCtl.selectedId >= 0 && !root.editCtl.textEditing
        onActivated: {
            root.commitEditing()
            root.editCtl.removeBox(root.editCtl.selectedId)
        }
    }
    // Undo/Redo des Editors (nur im Editmodus). Gesperrt während einer
    // Inline-Textbearbeitung - dort gehört Strg+Z dem TextEdit (zeichenweises
    // Undo beim Tippen); das Editor-Undo greift wieder nach dem Commit.
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode && !root.editCtl.textEditing
        onActivated: root.editCtl.undo()
    }
    Shortcut {
        sequence: "Ctrl+Shift+Z"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode && !root.editCtl.textEditing
        onActivated: root.editCtl.redo()
    }
    //  +/- = herein-/herauszoomen (ohne Modifikator; die Kachelgröße der Galerie
    //  liegt auf Ctrl++/- und ist ohnehin nur auf der Galerie-Seite scharf).
    //  „=" liegt auf derselben Taste wie „+" (ohne Shift) -> als Zweit-Sequenz.
    Shortcut {
        sequences: ["+", "="]
        enabled: root.paneActive && root.docReady && !root.caretActive
        onActivated: root.zoomIn()
    }
    Shortcut {
        sequence: "-"
        enabled: root.paneActive && root.docReady && !root.caretActive
        onActivated: root.zoomOut()
    }

    // ── Tastatur des Werkzeugs „Text bearbeiten" ──────────────────────────────
    //  Ein Shortcut kann das nicht leisten: Hier wird BELIEBIGER Text getippt,
    //  nicht eine feste Tastenfolge abgefangen. Deshalb ein eigener, unsichtbarer
    //  Fokus-Empfänger, der beim Setzen des Carets den Fokus holt (createArea).
    //  Er nimmt keine Fläche ein und fängt daher keine Mausereignisse ab.
    Item {
        id: caretInput
        width: 0; height: 0
        enabled: root.caretActive
        Keys.onPressed: (e) => {
            if (!root.caretActive)
                return
            const c = root.editCtl
            switch (e.key) {
            case Qt.Key_Left:      c.moveCaret(-1);      e.accepted = true; return
            case Qt.Key_Right:     c.moveCaret(1);       e.accepted = true; return
            case Qt.Key_Up:        c.moveCaretLine(-1);  e.accepted = true; return
            case Qt.Key_Down:      c.moveCaretLine(1);   e.accepted = true; return
            case Qt.Key_Home:      c.caretHome();        e.accepted = true; return
            case Qt.Key_End:       c.caretEnd();         e.accepted = true; return
            case Qt.Key_Backspace: c.deleteAtCaret(-1);  e.accepted = true; return
            case Qt.Key_Delete:    c.deleteAtCaret(1);   e.accepted = true; return
            case Qt.Key_Escape:    c.clearCaret();       e.accepted = true; return
            }
            // Steuerkürzel (Strg+Z/C/V …) bleiben den Shortcuts überlassen.
            if (e.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                return
            // Nur DRUCKBARE Zeichen einfügen - Rest (F-Tasten, Tab …) durchlassen.
            if (e.text.length > 0 && e.text.charCodeAt(0) >= 0x20
                    && e.text.charCodeAt(0) !== 0x7f) {
                c.insertAtCaret(e.text)
                e.accepted = true
            }
        }
    }

    onCurrentPageChanged: {
        if (thumbs.count > 0) thumbs.positionViewAtIndex(currentPage, ListView.Contain)
        _ensurePageClipsExtracted()
        root.clampPanX()
        // Stabile Seite nur außerhalb von Resize/Restore fortschreiben (Grundlage
        // für die Wiederherstellung nach dem Resize).
        if (!root._resizing && !root._restoring)
            root._stablePage = root.currentPage
    }

    // ── Zoom mit SEITEN-Verankerung ───────────────────────────────────────────
    //  Der Punkt in der Viewport-MITTE bleibt beim Zoomen stehen - verankert
    //  als Seite + Innerseiten-Anteil (dieselbe Mechanik wie die Resize-
    //  Wiederherstellung). Die frühere reine contentY-Verhältnisrechnung
    //  (`anchor·ratio`) war Relayout-anfällig: der Zoom ändert ALLE Delegate-
    //  Höhen, die ListView schätzt nicht instanziierte Seiten und korrigiert
    //  asynchron (originY-Verschiebung) -> die Verhältnisrechnung sprang durch
    //  die Seiten. Ablauf jetzt: Anker sichern -> Zoom setzen -> forceLayout
    //  (neue Höhen deterministisch) -> Seite anspringen -> Anteil anlegen.
    function setZoom(z) {
        var nz = Math.max(0.25, Math.min(4.0, z))
        if (Math.abs(nz - root.zoom) < 0.0001) return
        // Anker VOR dem Zoom bestimmen: Seite + Anteil unter der Viewport-Mitte.
        var midY = pages.contentY + pages.height / 2
        var page = pages.indexAt(pages.width / 2, midY)
        if (page < 0) page = root._stablePage
        var it0 = pages.itemAtIndex(page)
        var frac = (it0 && it0.height > 0)
                   ? Math.max(0, Math.min(1, (midY - it0.y) / it0.height)) : 0
        root._restoring = true                  // updateCurrentPage während des Relayouts sperren
        root.zoom = nz
        pages.forceLayout()                     // neue Delegate-Höhen deterministisch anwenden
        pages.positionViewAtIndex(page, ListView.Center)   // Delegate instanziieren
        var it = pages.itemAtIndex(page)
        if (it && it.height > 0)
            pages.contentY = root.clampContentY(it.y + frac * it.height - pages.height / 2)
        root.currentPage = page
        root._stablePage = page
        root.clampPanX()
        root._restoring = false
        root.updateCurrentPage()                // _stableFrac auf den neuen Stand
    }
    function zoomIn()  { setZoom(root.zoom + 0.15) }
    function zoomOut() { setZoom(root.zoom - 0.15) }

    // ── Horizontaler Schwenk (Zoom-Pan) ───────────────────────────────────────
    //  Anzeigebreite der aktuellen Seite (Pixel) - spiegelt die Delegate-Formel.
    function _curPageW() {
        if (!root.docReady) return 0
        var pts = root.doc.pagePointSize(root.currentPage)
        if (!pts || pts.width <= 0) return 0
        var wFit = (pages.width  - 24) / pts.width
        var hFit = (pages.height - 24) / pts.height
        var fit  = root.fitMode === "page" ? Math.min(wFit, hFit) : wFit
        return pts.width * fit * root.zoom
    }
    function clampPanX() {
        var overflow = Math.max(0, (_curPageW() - pages.width) / 2)
        root.panX = Math.max(-overflow, Math.min(root.panX, overflow))
    }
    // Schwenken kann, wer über die Seite hinaus hinein- (horizontal) oder
    // hinaus- (vertikal) gezoomt hat.
    function canPan() {
        return (_curPageW() - pages.width > 1) || (pages.contentHeight - pages.height > 1)
    }
    function panBy(dx, dy) {
        root.panX += dx
        root.clampPanX()
        pages.contentY = root.clampContentY(pages.contentY - dy)
    }
    // Liegt (nx,ny) [0..1] über einer erkannten Textzeile? (Pan vs. Markieren)
    function _overText(page, nx, ny) {
        if (!pdfTextCtl.ready) return false
        var lines = _snapLines(page)
        for (var i = 0; i < lines.length; i++) {
            var L = lines[i]
            if (nx >= L.x && nx <= L.x + L.w && ny >= L.y - 0.004 && ny <= L.y + L.h + 0.004)
                return true
        }
        return false
    }

    function goToPage(p) {
        if (pages.count <= 0) return
        var t = Math.max(0, Math.min(p, pages.count - 1))
        pages.positionViewAtIndex(t, ListView.Beginning)
        root.currentPage = t
    }
    // ── Origin-bewusste Scroll-Klemmen ────────────────────────────────────────
    //  ListView kann nach positionViewAtIndex auf eine MITTLERE Seite einen
    //  verschobenen Content-Ursprung haben (originY ≠ 0): oberhalb liegende,
    //  noch nicht instanziierte Seiten werden geschätzt und beim Nachladen
    //  korrigiert - der gültige contentY-Bereich ist dann
    //  [originY … originY + contentHeight − height]. Klemmen auf [0 … cH−h]
    //  blockieren das Hochscrollen (y < 0 unerreichbar) und stoppen das
    //  Runterscrollen zu früh. ALLE manuellen contentY-Zuweisungen laufen
    //  deshalb über clampContentY().
    function minContentY() { return pages.originY }
    function maxContentY() {
        return pages.originY + Math.max(0, pages.contentHeight - pages.height)
    }
    function clampContentY(y) {
        return Math.max(minContentY(), Math.min(y, maxContentY()))
    }
    function updateCurrentPage() {
        // Während Resize/Wiederherstellung NICHT überschreiben - sonst driftet die
        // Seite durch die transienten contentY-Änderungen des Relayouts.
        if (root._restoring || root._resizing) return
        if (pages.count <= 0) { root.currentPage = 0; return }
        var idx = pages.indexAt(pages.width / 2, pages.contentY + pages.height / 2)
        if (idx >= 0) root.currentPage = idx
        // Innerseiten-Scrollanteil (0..1) der stabilen Seite fortschreiben -
        // Grundlage, um nach einem Resize nicht nur die SEITE, sondern auch
        // die POSITION innerhalb der Seite wiederherzustellen (das Seiten-
        // Layout skaliert proportional -> der Anteil bleibt gültig).
        var it = pages.itemAtIndex(root._stablePage)
        if (it && it.height > 0)
            root._stableFrac = Math.max(0, Math.min(1,
                (pages.contentY - it.y) / it.height))
    }
    // Fenster-Resize (auch Split-View: Datei hinzufügen ändert die Kachelgröße):
    // die STABILE Seite (nur außerhalb von Resizes fortgeschrieben, s.
    // onCurrentPageChanged) samt Innerseiten-Anteil merken und nach dem Relayout
    // wiederherstellen -> KEIN Zurückspringen, KEIN Verlust der Scrollposition
    // innerhalb langer Seiten. `_resizing` sperrt updateCurrentPage für die
    // GANZE Resize-Phase; der Timer feuert erst, wenn die Größenänderung
    // ausläuft.
    function _preservePageAcrossResize() {
        if (!root.docReady) return
        if (!root._resizing) {
            root._resizing = true
            root._savePage = root._stablePage     // garantiert unverfälschte Seite
            root._saveFrac = root._stableFrac     // … samt Position in der Seite
        }
        restorePageTimer.restart()
    }
    // Solange die Kachel VERSTECKT ist (abgepoppte, persistente Split-Seite
    // während des Datei-Hinzufügens), ist die ListView-Geometrie nicht
    // verlässlich (kein Fenster, keine Polish-Läufe) - die Wiederherstellung
    // wartet dann auf das Wieder-Sichtbarwerden; `_resizing` bleibt gesetzt
    // und sperrt updateCurrentPage (kein Drift durch transiente Zustände).
    onVisibleChanged: if (visible && _resizing) restorePageTimer.restart()
    Timer {
        id: restorePageTimer
        interval: 90                              // erst nach Ende des Resize-Bursts
        onTriggered: {
            if (!root.visible || pages.width <= 0 || pages.height <= 0)
                return                            // versteckt -> onVisibleChanged holt nach
            root._restoring = true
            pages.forceLayout()                   // Geometrie deterministisch machen
            pages.positionViewAtIndex(root._savePage, ListView.Beginning)
            // Innerseiten-Anteil auf die NEUE Seitenhöhe anwenden (nach
            // positionViewAtIndex ist das Delegate der Seite instanziiert).
            // Origin-bewusst klemmen: nach dem Sprung auf eine mittlere Seite
            // kann originY ≠ 0 sein (s. clampContentY).
            var it = pages.itemAtIndex(root._savePage)
            if (it && it.height > 0)
                pages.contentY = root.clampContentY(it.y + root._saveFrac * it.height)
            root.currentPage = root._savePage
            root._stablePage = root._savePage
            root._stableFrac = root._saveFrac
            root.clampPanX()
            root._restoring = false
            root._resizing = false
        }
    }

    // ── Textauswahl-Steuerung ─────────────────────────────────────────────────
    //  beginSelection lädt das Auswahl-Dokument LAZY (erst bei echtem Markieren ->
    //  reines Ansehen kostet kein zusätzliches QPdfDocument). Bei großen PDFs ist
    //  der asynchrone Ladevorgang ggf. erst während des Ziehens fertig - dann holt
    //  der pdfTextCtl.onReadyChanged-Handler die Auswahl nach (Catch-up).
    function beginSelection(page) {
        root._selecting = true
        root.selPage = page
        root.selRects = []
        root._lastSel = null
        root._pendingSelectAll = false
        pdfTextCtl.clearSelection()
        pdfTextCtl.prepare(root.source)
    }
    //  Reflow-Verkettung starten: die aktuell gewählte Box wird zur Ausgangsbox;
    //  der nächste Klick auf eine ANDERE Textbox verkettet beide (onSelectedIdChanged).
    function startLink(fromId) {
        if (fromId < 0) return
        root._linkFromId = fromId
        root._toast(App.uiText(App.language, "PdfChainPick"))
    }
    // ── Suche im Dokument ────────────────────────────────────────────────────
    //  Der Controller hält Begriff und Treffer; hier liegt nur, was die
    //  ANZEIGE braucht: ob die Leiste offen ist und welcher Treffer gerade dran
    //  ist. `_searchRev` zieht die Treffer-Rechtecke der Seiten nach (die Suche
    //  läuft stückweise, also kommen sie nach und nach).
    property bool searchVisible: false
    property int  searchIndex: -1
    property int  _searchRev: 0

    //  Signatur/Stempel: Bild auswählen und einfügen (vom Panel-Knopf gerufen).
    //  Der Knopf bietet zuerst die Bilder im ORDNER an (s. FolderImagePicker);
    //  dies hier ist der Weg über den Dateidialog.
    function pickStampImage() {
        if (!root.docReady || !root.editCtl.editMode) return
        stampFileDlg.open()
    }

    //  Ein gewähltes Bild als Stempel setzen - EIN Weg für Dateidialog und
    //  Ordner-Wähler. Das Bild landet mittig auf der aktuellen Seite; Größe =
    //  ein Drittel der Seitenbreite, Höhe folgt dem Seitenverhältnis.
    function insertStampImage(fileUrl) {
        if (!root.docReady || !root.editCtl.editMode) return
        const page = root.currentPage
        const pts = root.doc.pagePointSize(page)
        if (pts.width <= 0) { root._toast(App.uiText(App.language, "PdfStampFailedToast")); return }
        const w = pts.width / 3
        const id = root.editCtl.addStamp(fileUrl, page,
                                        (pts.width - w) / 2, pts.height / 3, w)
        if (id < 0) root._toast(App.uiText(App.language, "PdfStampFailedToast"))
        else        root.notesVisible = true
    }

    function toggleSearch() {
        root.searchVisible = !root.searchVisible
        if (root.searchVisible) {
            if (root.source.length > 0) pdfTextCtl.prepare(root.source)
            searchField.forceActiveFocus()
            searchField.selectAll()
        } else {
            pdfTextCtl.clearSearch()
            root.searchIndex = -1
        }
    }
    //  Zum Treffer springen: Seite anzeigen und ihn als „aktuell" merken.
    function goToHit(i) {
        const n = pdfTextCtl.searchCount
        if (n <= 0) { root.searchIndex = -1; return }
        root.searchIndex = ((i % n) + n) % n          // umlaufend
        const h = pdfTextCtl.searchHit(root.searchIndex)
        if (h.page !== undefined) root.goToPage(h.page)
    }

    function updateSelection(page, a0, b0, a1, b1) {
        // Auf [0..1] klemmen (Ziehen über den Seitenrand hinaus).
        a0 = Math.max(0, Math.min(1, a0)); b0 = Math.max(0, Math.min(1, b0))
        a1 = Math.max(0, Math.min(1, a1)); b1 = Math.max(0, Math.min(1, b1))
        root._lastSel = { page: page, a0: a0, b0: b0, a1: a1, b1: b1 }
        root.selPage = page
        root.selRects = pdfTextCtl.selectionBetween(page, a0, b0, a1, b1)
    }
    function endSelection(wasDrag) {
        root._selecting = false
        if (!wasDrag) root.clearSelection()   // reiner Klick -> Auswahl aufheben
    }
    //  Auswahl -> TEXTMARKIERUNG. Die Zeilenrechtecke der Textauswahl
    //  (`selRects`, von PdfText geliefert) sind exakt das, was eine Markierung
    //  braucht - je Zeile ein Bereich, alle zusammen EIN Objekt. Ohne Textebene
    //  (gescannte Seite) gibt es keine Zeilen; dann markiert `fallback` genau
    //  den aufgezogenen Bereich.
    function markSelectionNow(style, fallback) {
        if (!root.docReady || !root.editCtl.editMode) return false
        const page = root.selPage >= 0 ? root.selPage : (fallback ? fallback.page : -1)
        if (page < 0) return false
        const pts = root.doc.pagePointSize(page)
        if (pts.width <= 0 || pts.height <= 0) return false
        let quads = []
        for (var i = 0; i < root.selRects.length; i++) {
            const r = root.selRects[i]
            quads.push({ x: r.x * pts.width,  y: r.y * pts.height,
                         w: r.w * pts.width,  h: r.h * pts.height })
        }
        if (quads.length === 0 && fallback && fallback.w > 0 && fallback.h > 0)
            quads = [ { x: fallback.x, y: fallback.y, w: fallback.w, h: fallback.h } ]
        if (quads.length === 0) return false
        const id = root.editCtl.addMarkup(page, style, quads)
        root.clearSelection()
        if (id >= 0) root.notesVisible = true
        return id >= 0
    }

    //  Markierung -> Ersetzen-Box (Arbeitsweise: Text auswählen ▸ ⇄ klicken).
    //  Bewusst auf ROOT-Ebene und über die Panel-Schaltfläche AUFGERUFEN statt
    //  über onToolChanged: war ⇄ bereits das aktive Werkzeug, feuert
    //  toolChanged nicht - der Klick blieb dann wirkungslos (Nutzerbefund
    //  2026-07-17). Seitenmaße kommen direkt aus root.doc, es braucht also
    //  kein instanziiertes Seiten-Delegate.
    function replaceSelectionNow() {
        if (!root.docReady || !root.editCtl.editMode) return false
        const page = root.selPage
        if (page < 0 || root.selRects.length === 0) return false
        const pts = root.doc.pagePointSize(page)
        if (!pts || pts.width <= 0 || pts.height <= 0) return false
        //  Wie beim Schwärzen: die Zeilen-Rechtecke der Auswahl aufziehen, nicht
        //  die Zugkoordinaten (Höhe 0 -> die Sonde fand nichts und die Box blieb
        //  unbefüllt).
        const u = root._selectionUnionPt(page)
        if (!u) return false
        const id = root.editCtl.beginDraw(5, page, u.x, u.y)
        if (id < 0) return false
        root.editCtl.updateDraw(id, u.x + u.w, u.y + u.h)

        const info = root.editCtl.boxInfo(id)
        let snapped = false
        let sx = 0, sy = 0, sw = 0, sh = 0, lh = 0, txt = ""
        if (info.exists === true && pdfTextCtl.ready) {
            const pr = pdfTextCtl.replaceProbe(page,
                           info.xPt / pts.width,
                           info.yPt / pts.height,
                           (info.xPt + info.wPt) / pts.width,
                           (info.yPt + info.hPt) / pts.height)
            if (pr.found === true) {
                snapped = true
                sx = pr.x * pts.width;  sy = pr.y * pts.height
                sw = pr.w * pts.width;  sh = pr.h * pts.height
                lh = pr.lineH * pts.height
                txt = pr.text
            }
        }
        const nid = root.editCtl.endReplaceDraw(id, snapped, sx, sy, sw, sh, lh, txt)
        root.clearSelection()
        if (nid >= 0) {
            root.notesVisible = true
            root._autoEditId = nid            // startet direkt in der Textbearbeitung
            return true
        }
        return false
    }

    //  Union der AUSWAHL-Rechtecke in PDF-Punkten. Sie ist die richtige Quelle
    //  für die Zeilen-Sonde: die Zugkoordinaten (`_lastSel`) beschreiben eine
    //  Bewegung ENTLANG einer Zeile und haben deshalb Höhe 0 - `replaceProbe`
    //  fand damit nichts, die Schwärzung bekam keinen Originaltext und beim
    //  Export blieb der Text unter dem Balken stehen (gemessen 2026-08-11).
    //  `selRects` dagegen sind die erkannten ZEILEN und haben echte Höhe.
    function _selectionUnionPt(page) {
        const pts = root.doc.pagePointSize(page)
        if (!pts || pts.width <= 0 || pts.height <= 0) return null
        if (root.selRects.length === 0) return null
        let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9
        for (let i = 0; i < root.selRects.length; ++i) {
            const r = root.selRects[i]
            x0 = Math.min(x0, r.x);          y0 = Math.min(y0, r.y)
            x1 = Math.max(x1, r.x + r.w);    y1 = Math.max(y1, r.y + r.h)
        }
        if (x1 <= x0 || y1 <= y0) return null
        return { x: x0 * pts.width,          y: y0 * pts.height,
                 w: (x1 - x0) * pts.width,   h: (y1 - y0) * pts.height }
    }

    //  Markierung -> SCHWÄRZUNG (Arbeitsweise: Text auswählen ▸ ▮ klicken).
    //  Derselbe Weg wie beim Ersetzen, nur endet er in `endRedactDraw`: die
    //  Fläche schnappt auf die erkannten Zeilen ein, und genau deren Text
    //  wandert als `origText` mit - Gedecktes und Entferntes bleiben damit
    //  deckungsgleich. Ohne Textebene gibt es weder Zeilen noch Text; dann
    //  bleibt es beim Aufziehen, und der Aufrufer sagt das dem Nutzer.
    //  Rückgabe: true = Schwärzung angelegt.
    function redactSelectionNow() {
        if (!root.docReady || !root.editCtl.editMode) return false
        const page = root.selPage
        if (page < 0 || root.selRects.length === 0) return false
        const pts = root.doc.pagePointSize(page)
        if (!pts || pts.width <= 0 || pts.height <= 0) return false
        //  Aufgezogen wird über die AUSWAHL-Rechtecke (echte Zeilenhöhe), nicht
        //  über die Zugkoordinaten - s. _selectionUnionPt.
        const u = root._selectionUnionPt(page)
        if (!u) return false
        const id = root.editCtl.beginDraw(5, page, u.x, u.y)
        if (id < 0) return false
        root.editCtl.updateDraw(id, u.x + u.w, u.y + u.h)

        //  Gedeckt wird GENAU die Auswahl - nicht die ganze Zeile. Früher
        //  schnappte die Fläche über `replaceProbe` auf die Zeilen-Bounds ein
        //  (Muster „Text ersetzen"); markierte man drei Wörter, lag der Balken
        //  über der kompletten Zeile. Entfernt wird entsprechend der WIRKLICH
        //  markierte Text (`selectedText`), nicht der Zeilentext.
        const txt = pdfTextCtl.selectedText;
        const nid = root.editCtl.endRedactDraw(id, true, u.x, u.y, u.w, u.h, txt)
        root.clearSelection()
        if (nid >= 0) {
            root.notesVisible = true
            root._redactHintOnce()
            return true
        }
        return false
    }

    //  Der Knopf ▮ - der Hauptweg. Liegt eine Textauswahl vor, wird sie sofort
    //  geschwärzt; sonst bleibt der Ziehweg, dann aber MIT Rückmeldung, wenn
    //  die Seite gar keine Textebene hat: dort startet die Geste nicht, und
    //  früher passierte wortlos nichts (Nutzerbefund „lässt sich nicht
    //  bedienen").
    function startRedact() {
        if (!root.docReady || !root.editCtl.editMode) return
        if (root.selPage >= 0 && root.selRects.length > 0) {
            if (!root.redactSelectionNow())
                root._toast(App.uiText(App.language, "PdfRedactNoTextToast"))
            return
        }
        //  Die Textebene wird lazy geladen; erst wenn sie DA ist, lässt sich
        //  sagen, dass die Seite keine hat.
        pdfTextCtl.prepare(root.source)
        if (pdfTextCtl.ready
                && pdfTextCtl.textLineRects(root.currentPage).length === 0)
            root._toast(App.uiText(App.language, "PdfRedactNoTextToast"))
    }

    //  Die Grenzen der Schwärzung sind wichtig, aber nichts für jeden Tooltip:
    //  einmal je Kachel-Sitzung als Hinweis, danach nie wieder.
    property bool _redactHintShown: false
    function _redactHintOnce() {
        if (root._redactHintShown) return
        root._redactHintShown = true
        root._toast(App.uiText(App.language, "PdfRedactLimitHint"))
    }

    function clearSelection() {
        root.selPage = -1
        root.selRects = []
        root._lastSel = null
        root._pendingSelectAll = false
        pdfTextCtl.clearSelection()
    }
    function selectAllCurrentPage() {
        if (!root.docReady) return
        root._lastSel = null
        pdfTextCtl.prepare(root.source)
        if (pdfTextCtl.ready) {
            root.selPage = root.currentPage
            root.selRects = pdfTextCtl.selectAllOnPage(root.currentPage)
            root._pendingSelectAll = false
        } else {
            // Auswahl-Dokument laedt noch (lazy) -> nach dem Laden nachholen.
            root._pendingSelectAll = true
        }
    }

    // Web-artiges, animiertes Scrollen der Seiten (von der Wheel-MouseArea genutzt).
    NumberAnimation {
        id: pagesScroll
        target: pages; property: "contentY"
        duration: 180; easing.type: Easing.OutCubic
    }
    function wheelPages(wheel) {
        if (wheel.modifiers & Qt.ControlModifier) {
            if (wheel.angleDelta.y > 0) root.zoomIn()
            else if (wheel.angleDelta.y < 0) root.zoomOut()
            wheel.accepted = true
            return
        }
        var scrollable = pages.contentHeight - pages.height
        if (scrollable <= 0) { wheel.accepted = true; return }
        var raw = (wheel.angleDelta.y !== 0)
                  ? (wheel.angleDelta.y / 120) * (pages.height * root.wheelPageFraction)
                  : wheel.pixelDelta.y * 1.6
        var base = pagesScroll.running ? pagesScroll.to : pages.contentY
        var tgt = root.clampContentY(base - raw)
        pagesScroll.from = pages.contentY
        pagesScroll.to = tgt
        pagesScroll.restart()
        wheel.accepted = true
    }

    Rectangle { anchors.fill: parent; color: App.themeBackground }

    Text {
        id: errLabel
        anchors.centerIn: parent
        // Reaktiv an den Status des aktuellen Dokuments gebunden.
        visible: !!root.doc && root.doc.status === PdfDocument.Error
        text: App.uiText(App.language, "PdfLoadError")
        color: "#ff8a80"; font.pixelSize: 14
        z: 5
    }

    // ── Toolbar (unter der globalen Leiste -> kein Overlap) ────────────────────
    Rectangle {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset }
        //  42 px wie im DOCX-Editor - die drei Editor-Leisten sind
        //  bewusst gleich hoch (Nutzerwunsch: überall konsistent).
        height: 42
        color: App.themeToolbarBg
        visible: root.docReady
        z: 6
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: App.themeBorder }

        //  Die linke Werkzeuggruppe endet VOR der rechten (2026-07-17): vorher
        //  waren beide Reihen nur verankert - bei schmaler (Split-)Kachel lagen
        //  die Knöpfe ÜBEREINANDER. Passt die Gruppe nicht, schwenkt das
        //  Mausrad sie waagerecht. Seit 2026-08-19 macht das die gemeinsame
        //  `ScrollableBar` (dieselbe Bedienung wie im DOCX-Editor und in den
        //  Menüleisten) statt einer eigenen Klemm-/Animationslösung.
        ScrollableBar {
            id: toolbarLeftClip
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: toolbarRight.left; anchors.rightMargin: 8
            anchors.top: parent.top; anchors.bottom: parent.bottom
            spacing: 6

            PdfToolButton {
                // Pfeil nach links = einklappen (Panel offen), nach rechts =
                // ausklappen (Panel zu). Bewusst PFEILE MIT SCHAFT und nicht die
                // Chevrons: die tragen direkt daneben die Seiten-Navigation, und
                // zwei Bedeutungen dürfen nicht dieselbe Form haben.
                iconName: root.thumbsVisible ? "arrow-left" : "arrow-right"
                active: root.thumbsVisible
                tip: root.thumbsVisible ? App.uiText(App.language, "PdfCollapsePreview")
                                        : App.uiText(App.language, "PdfExpandPreview")
                onActivated: root.thumbsVisible = !root.thumbsVisible
            }
            Item { width: 4; height: 1 }
            PdfToolButton {
                iconName: "chevron-left"
                enabled: root.currentPage > 0
                onActivated: root.goToPage(root.currentPage - 1)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: App.uiText(App.language, "PdfPageIndicator").arg(root.currentPage + 1).arg(root.pageCount)
                color: App.themeTextPrimary; font.pixelSize: 12
                width: 96; horizontalAlignment: Text.AlignHCenter
            }
            PdfToolButton {
                iconName: "chevron-right"
                enabled: root.currentPage < root.pageCount - 1
                onActivated: root.goToPage(root.currentPage + 1)
            }

            // ── PDF-Editor ────────────────────────────────────────────────────
            Item { width: 8; height: 1 }
            Rectangle { width: 1; height: 18; color: App.themeBorder
                        anchors.verticalCenter: parent.verticalCenter }
            Item { width: 8; height: 1 }
            PdfToolButton {
                iconName: "eye"
                visible: root.editCtl.boxCount > 0 || root.editCtl.editMode
                active: root.notesVisible
                tip: App.uiText(App.language, "PdfEditNotesToggleTip")
                onActivated: root.notesVisible = !root.notesVisible
            }
            PdfToolButton {
                iconName: "pen"
                active: root.editCtl.editMode
                tip: App.uiText(App.language, "PdfEditToggleTip")
                // commitEditing() VOR dem Umschalten: eine offene Text-Session
                // schließt sauber ab, bevor der Modus (und damit die Auswahl)
                // fällt - reiner Zustandswechsel, KEIN Dokument-Reload.
                onActivated: { root.commitEditing(); root.editCtl.editMode = !root.editCtl.editMode }
            }
            PdfToolButton {
                iconName: "undo"
                visible: root.editCtl.editMode
                enabled: root.editCtl.canUndo
                tip: App.uiText(App.language, "PdfEditUndoTip")
                onActivated: { root.commitEditing(); root.editCtl.undo() }
            }
            PdfToolButton {
                iconName: "redo"
                visible: root.editCtl.editMode
                enabled: root.editCtl.canRedo
                tip: App.uiText(App.language, "PdfEditRedoTip")
                onActivated: { root.commitEditing(); root.editCtl.redo() }
            }
            PdfToolButton {
                iconName: "search"
                active: root.searchVisible
                tip: App.uiText(App.language, "PdfSearchTip")
                onActivated: root.toggleSearch()
            }
            PdfToolButton {
                iconName: "snap"
                visible: root.editCtl.editMode
                active: root.snapEnabled
                tip: App.uiText(App.language, "PdfEditSnapTip")
                onActivated: root.snapEnabled = !root.snapEnabled
            }
            // \u2500\u2500 Formular \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
            //  Nur bei einem Dokument MIT ausf\u00fcllbaren Feldern; schreibt die
            //  eingetragenen Werte in eine Kopie \u201e\u2026_ausgefuellt.pdf".
            //  Unabh\u00e4ngig vom Editmodus: Formulare f\u00fcllt man beim Lesen aus.
            PdfToolButton {
                iconName: "save"
                visible: root.editCtl.hasForm
                enabled: root.editCtl.formDirty && !root.editCtl.busy
                tip: App.uiText(App.language, "PdfFormSaveTip")
                onActivated: root.editCtl.saveFormValues()
            }
            // Der frühere ⚙-Button (Text-Eigenschaften ein/aus) entfällt: das
            // Panel öffnet automatisch beim Erstellen/Auswählen einer Notiz
            // (s. Connections auf root.editCtl.onSelectedIdChanged) und schließt
            // über sein eigenes ✕.
        }   // Ende toolbarLeftClip (ScrollableBar)

        //  Auch die rechte Gruppe ist blätterbar UND gedeckelt (höchstens die
        //  halbe Leiste): sie ist nach links gewachsen, bis sie die linke Gruppe
        //  vollständig verdeckte - dann war dort nichts mehr erreichbar
        //  (Nutzerbefund). Jetzt stehen beide nebeneinander, und jede lässt sich
        //  für sich schwenken.
        ScrollableBar {
            id: toolbarRight
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.bottom: parent.bottom
            width: Math.min(contentWidth, toolbar.width * 0.55)
            spacing: 6
            // Live-Transliteration (oben rechts): nur im Editmodus sinnvoll,
            // da Notizen hier getippt werden. Zustand/Schema global (Translit).
            TranslitButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.editCtl.editMode
            }
            Item { width: root.editCtl.editMode ? 4 : 0; height: 1 }
            // Audio-Leiste umschalten - nur sichtbar, wenn das PDF Audio enthält.
            PdfToolButton {
                iconName: "audio"
                visible: pdfAudioCtl.documentHasAudio
                active: root.audioPanelVisible
                tip: root.audioPanelVisible ? App.uiText(App.language, "PdfHideAudioBar") : App.uiText(App.language, "PdfShowAudioBar")
                onActivated: {
                    root.audioPanelVisible = !root.audioPanelVisible
                    if (root.audioPanelVisible && !PdfEdit.panelOnTop)
                        root.editPanelVisible = false
                }
            }
            Item { width: pdfAudioCtl.documentHasAudio ? 4 : 0; height: 1 }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: fitLabel.implicitWidth + 22; height: 26; radius: 6
                color: fitHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                                        : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                border.color: App.themeBorder; border.width: 1
                Text { id: fitLabel; anchors.centerIn: parent
                       text: root.fitMode === "page" ? App.uiText(App.language, "PdfFitPage") : App.uiText(App.language, "PdfFitWidth")
                       color: App.themeTextPrimary; font.pixelSize: 11 }
                HoverHandler { id: fitHover }
                TapHandler {
                    onTapped: {
                        root.fitMode = (root.fitMode === "page") ? "width" : "page"
                        root.zoom = 1.0
                        root.updateCurrentPage()
                    }
                }
            }
            Item { width: 6; height: 1 }
            PdfToolButton { iconName: "minus"; enabled: root.zoom > 0.26; onActivated: root.zoomOut() }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(root.zoom * 100) + " %"
                color: App.themeTextPrimary; font.pixelSize: 12
                width: 48; horizontalAlignment: Text.AlignHCenter
            }
            PdfToolButton { iconName: "plus"; enabled: root.zoom < 3.99; onActivated: root.zoomIn() }
        }
    }

    // ── Suchleiste (Overlay unter der Toolbar; Strg+F) ───────────────────────
    Rectangle {
        id: searchBar
        visible: root.searchVisible && root.docReady
        z: 7
        anchors { left: parent.left; right: parent.right }
        y: toolbar.visible ? toolbar.y + toolbar.height : root.topInset
        height: 40
        color: App.themeToolbarBg
        Rectangle { anchors.bottom: parent.bottom; width: parent.width
                    height: 1; color: App.themeBorder }
        //  Klicks dürfen nicht auf die Seite darunter durchfallen.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            spacing: 8

            TextField {
                id: searchField
                objectName: "searchField"
                width: Math.min(320, searchBar.width - 260)
                anchors.verticalCenter: parent.verticalCenter
                placeholderText: App.uiText(App.language, "PdfSearchPlaceholder")
                //  Live suchen: der Controller sucht stückweise, das hält die
                //  Oberfläche auch bei langen Dokumenten flüssig.
                onTextChanged: {
                    root.searchIndex = -1
                    pdfTextCtl.search(text)
                }
                Keys.onReturnPressed: root.goToHit(root.searchIndex + 1)
                Keys.onEscapePressed: root.toggleSearch()
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: App.themeTextMuted
                font.pixelSize: 12
                text: pdfTextCtl.searchTerm.length === 0
                      ? ""
                      : (pdfTextCtl.searchCount === 0
                         ? (pdfTextCtl.searching ? "…"
                            : App.uiText(App.language, "PdfSearchNone"))
                         : App.uiText(App.language, "PdfSearchCount")
                               .arg(root.searchIndex + 1).arg(pdfTextCtl.searchCount)
                           + (pdfTextCtl.searching ? " …" : ""))
            }
            PdfToolButton {
                iconName: "chevron-up"
                enabled: pdfTextCtl.searchCount > 0
                onActivated: root.goToHit(root.searchIndex - 1)
            }
            PdfToolButton {
                iconName: "chevron-down"
                enabled: pdfTextCtl.searchCount > 0
                onActivated: root.goToHit(root.searchIndex + 1)
            }
            PdfToolButton {
                iconName: "close"
                onActivated: root.toggleSearch()
            }
        }
    }

    // ── Inhaltsbereich ────────────────────────────────────────────────────────
    Item {
        id: contentArea
        anchors {
            left: parent.left; right: parent.right
            top: toolbar.visible ? toolbar.bottom : parent.top
            bottom: parent.bottom
            // "Ganze Seite": Der Fit haengt von der Viewport-HOEHE ab - jede
            // bottomInset-Aenderung (Ein-/Ausblenden der unteren Datei-Navigation
            // per Hover) wuerde die Seite sonst neu skalieren und verschieben
            // (teures Re-Rendern + visuelles Springen). Daher wird das Inset im
            // Seiten-Fit-Modus IGNORIERT: Die Navigation liegt dann - wie im
            // "Breite"-Modus - als reines Overlay UEBER der statisch gerenderten
            // PDF-Seite. Im "Breite"-Modus bleibt das Inset erhalten (Fit haengt
            // nur von der Breite ab -> kein Re-Rendern, nur Viewport-Hoehe).
            bottomMargin: root.fitMode === "page" ? 0 : root.bottomInset
        }

        // ── Seiten (volle Breite; Thumbnail-Panel liegt als Overlay darüber) ──
        ListView {
            id: pages
            anchors.fill: parent
            clip: true
            // Browser-artige Textauswahl ist IMMER aktiv: Linksziehen markiert.
            // Damit der Flickable das Ziehen nicht als Schwenken stiehlt, ist das
            // eigene Dragging der Liste deaktiviert. Gescrollt wird ueber das
            // Mausrad (NoButton-Wheel-MouseArea) und die ScrollBar - beide
            // funktionieren bei interactive:false unveraendert (sie setzen contentY
            // direkt). Programmatische contentY/positionViewAtIndex bleiben gueltig.
            interactive: false
            model: root.docReady ? root.doc.pageCount : 0
            spacing: 10
            //  Kopfraum unter der Word-Leiste (s. root.ribbonInset): bewusst
            //  als HEADER statt topMargin - so bleibt der gültige contentY-
            //  Bereich [0 … contentHeight−height] und sämtliche vorhandene
            //  Scroll-/ScrollBar-/positionViewAtIndex-Logik gilt unverändert.
            header: Item { width: 1; height: root.ribbonInset }
            // Scroll-Cache: pageCacheScreens Viewporthoehen je Richtung bleiben
            // gerendert -> Hoch-/Runterscrollen innerhalb des Fensters laedt nicht
            // neu. cache:false an den PdfPageImage -> ausserhalb dieses Fensters
            // wird wieder freigegeben (deterministischer RAM-Deckel).
            //
            // GESTAFFELT: Bis der Warmlauf greift, ist der Puffer fast 0 -> beim
            // Oeffnen rendert NUR die sichtbare Seite (kein Vorab-Rendern von
            // Nachbarseiten, die sie sonst hinter dem Render-Mutex blockierten).
            // Danach klappt der volle Vorhalte-Puffer fuer fluessiges Scrollen auf.
            //  max(0, …): vor dem ersten Layout ist die Höhe negativ (Inset
            //  minus Null-Höhe) - ein negativer cacheBuffer ist ungültig und
            //  wurde beim Öffnen als Warnung gemeldet (wie bei der
            //  Vorschauleiste unten).
            cacheBuffer: Math.max(0, Math.round(pages.height *
                                    (root._warm ? root.pageCacheScreens : 0.1)))
            boundsBehavior: Flickable.StopAtBounds
            onContentYChanged: root.updateCurrentPage()
            onCountChanged: root.updateCurrentPage()
            // Breite/Höhe ändern sich beim Fenster-Resize UND beim Aufteilen der
            // Split-Ansicht (Datei hinzufügen). Aktuelle Seite/Position halten.
            onWidthChanged:  root._preservePageAcrossResize()
            onHeightChanged: root._preservePageAcrossResize()

            ScrollBar.vertical: ScrollBar {
                id: vbar
                policy: ScrollBar.AlwaysOn
                width: 12
                interactive: true
                contentItem: Rectangle {
                    implicitWidth: 8; radius: 4
                    color: vbar.pressed ? App.themeAccent
                         : vbar.hovered ? Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.9)
                                        : Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.55)
                }
                background: Rectangle { color: Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.05); radius: 4 }
            }

            delegate: Item {
                id: pageCell
                required property int index
                width: pages.width
                // Während eines Notiz-Drags von DIESER Seite über den Rand:
                // über die Geschwister-Delegates heben (s. root.editDragPage).
                z: index === root.editDragPage ? 1 : 0

                readonly property size pts: root.doc.pagePointSize(index)
                // Skalierung unabhaengig vom Overlay-Panel -> Umschalten loest KEIN
                // teures Neu-Rendern aus (Panel liegt nur ueber dem linken Rand).
                readonly property real wFit: pts.width  > 0 ? (pages.width  - 24) / pts.width  : 1.0
                readonly property real hFit: pts.height > 0 ? (pages.height - 24) / pts.height : 1.0
                readonly property real fitScale: root.fitMode === "page"
                                                 ? Math.min(wFit, hFit) : wFit
                readonly property real pageW: pts.width  * fitScale * root.zoom
                readonly property real pageH: pts.height * fitScale * root.zoom
                // Aufgabe 3: im Editmodus Platz für die „+"-Linie (Seite einfügen).
                readonly property bool showAddLine: root.editCtl.editMode
                height: pageH + 4 + (showAddLine ? 26 : 0)

                Rectangle {
                    id: pageBg
                    // Horizontal grundsätzlich zentriert; bei Zoom-Überlauf um
                    // root.panX verschiebbar (Links-Drag-Schwenk -> sonst nicht
                    // erreichbarer Inhalt am Rand wird sichtbar).
                    x: Math.round((pages.width - width) / 2 + root.panX)
                    width: pageCell.pageW; height: pageCell.pageH
                    color: "white"

                    // ── Textauswahl-Fänger (UNTERSTE Ebene der Seite) ──────────
                    //  Das PdfPageImage darueber faengt keine Maus -> ein Linkspress
                    //  faellt hierher durch. Ausnahme: ein Badge (eigene MouseArea,
                    //  liegt oben) verbraucht den Press -> Annotation-Klicks bleiben
                    //  erhalten und starten KEINE Markierung.
                    //  Ziehen markiert; reiner Klick hebt die Auswahl auf.
                    MouseArea {
                        id: selArea
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: selArea._panMode ? Qt.ClosedHandCursor : Qt.IBeamCursor
                        preventStealing: true
                        property real sx: 0
                        property real sy: 0
                        property real lastGX: 0
                        property real lastGY: 0
                        property bool dragging: false
                        property bool _panMode: false
                        onPressed: (m) => {
                            selArea.sx = m.x; selArea.sy = m.y
                            selArea.dragging = false
                            var g = mapToItem(null, m.x, m.y)
                            selArea.lastGX = g.x; selArea.lastGY = g.y
                            // Schwenken statt Markieren, wenn hineingezoomt UND der
                            // Druckpunkt NICHT über auswählbarem Text liegt.
                            var nx = m.x / pageImg.width, ny = m.y / pageImg.height
                            selArea._panMode = root.canPan() && !root._overText(pageCell.index, nx, ny)
                            if (!selArea._panMode)
                                root.beginSelection(pageCell.index)
                            else
                                pdfTextCtl.prepare(root.source)   // Zeilen für später laden
                        }
                        onPositionChanged: (m) => {
                            if (selArea._panMode) {
                                var g = mapToItem(null, m.x, m.y)
                                root.panBy(g.x - selArea.lastGX, g.y - selArea.lastGY)
                                selArea.lastGX = g.x; selArea.lastGY = g.y
                                return
                            }
                            if (!selArea.dragging) {
                                if (Math.abs(m.x - selArea.sx) + Math.abs(m.y - selArea.sy) < 3)
                                    return
                                selArea.dragging = true
                            }
                            root.updateSelection(pageCell.index,
                                selArea.sx / pageImg.width, selArea.sy / pageImg.height,
                                m.x        / pageImg.width, m.y        / pageImg.height)
                        }
                        onReleased: {
                            if (selArea._panMode) { selArea._panMode = false; return }
                            root.endSelection(selArea.dragging)
                        }
                    }

                    PdfPageImage {
                        id: pageImg
                        anchors.fill: parent
                        document: root.doc
                        currentFrame: pageCell.index
                        asynchronous: true
                        cache: false
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: pageCell.pageW * Screen.devicePixelRatio
                        sourceSize.height: pageCell.pageH * Screen.devicePixelRatio

                        // ── Auswahl-Highlights (nur auf der Seite mit aktiver
                        //    Auswahl; normalisierte Rechtecke vom PdfText-Singleton) ─
                        Repeater {
                            model: pageCell.index === root.selPage ? root.selRects : []
                            delegate: Rectangle {
                                required property var modelData
                                x: modelData.x * pageImg.width
                                y: modelData.y * pageImg.height
                                width:  Math.max(1, modelData.w * pageImg.width)
                                height: Math.max(1, modelData.h * pageImg.height)
                                color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                               App.themeAccent.b, 0.32)
                            }
                        }

                        // ── Treffer der Dokumentsuche ─────────────────────────
                        //  Rev-getrieben: die Suche liefert stückweise, die
                        //  Rechtecke kommen normalisiert wie die Auswahl.
                        Repeater {
                            model: root.searchVisible
                                   ? (root._searchRev,
                                      pdfTextCtl.searchHitsOnPage(pageCell.index))
                                   : []
                            delegate: Rectangle {
                                required property var modelData
                                x: modelData.x * pageImg.width
                                y: modelData.y * pageImg.height
                                width:  Math.max(2, modelData.w * pageImg.width)
                                height: Math.max(2, modelData.h * pageImg.height)
                                color: Qt.rgba(1.0, 0.85, 0.0, 0.42)
                                border.color: Qt.rgba(0.9, 0.55, 0.0, 0.9)
                                border.width: 1
                            }
                        }

                        // ── Caret des Werkzeugs „Text bearbeiten" ─────────────
                        //  Sitzt DIREKT in der eingebetteten Textebene (kein
                        //  Overlay): Position kommt aus PdfTextLayout über den
                        //  Controller, in PDF-Punkten der Seite - hier nur auf
                        //  die aktuelle Darstellungsgröße skaliert.
                        Rectangle {
                            id: caretBar
                            readonly property rect r: root.editCtl.caretRectPt
                            visible: root.editCtl.editMode
                                     && root.editCtl.tool === 7
                                     && root.editCtl.caretPage === pageCell.index
                                     && caretBar.r.height > 0
                                     && pageCell.pts.width > 0 && pageCell.pts.height > 0
                            x: caretBar.r.x / pageCell.pts.width  * pageImg.width
                            y: caretBar.r.y / pageCell.pts.height * pageImg.height
                            width: Math.max(1, Math.round(
                                       1.4 / pageCell.pts.width * pageImg.width))
                            height: Math.max(2, caretBar.r.height
                                                / pageCell.pts.height * pageImg.height)
                            color: App.themeAccent
                            // Blinken wie in jedem Texteditor; läuft nur, solange
                            // das Caret sichtbar ist (keine Animation im Leerlauf).
                            SequentialAnimation on opacity {
                                running: caretBar.visible
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.15; duration: 450 }
                                NumberAnimation { to: 1.0;  duration: 450 }
                            }
                        }

                        Repeater {
                            model: root.annotations
                            delegate: Rectangle {
                                id: badge
                                required property var modelData
                                // Audio (type 0) NICHT hier - das übernimmt PdfAudio
                                // (eigene Hotspots + Leiste). Hier nur Video/Link.
                                visible: modelData.page === pageCell.index && modelData.type !== 0
                                x: modelData.x * pageImg.width
                                y: modelData.y * pageImg.height
                                width:  Math.max(18, modelData.w * pageImg.width)
                                height: Math.max(18, modelData.h * pageImg.height)
                                radius: 4
                                color: badgeHover.hovered ? Qt.rgba(0.0, 0.78, 0.70, 0.35)
                                                          : Qt.rgba(0.0, 0.78, 0.70, 0.18)
                                border.color: "#00c8b4"; border.width: 1
                                DrawnIcon {
                                    anchors.centerIn: parent
                                    name: badge.modelData.type === 1 ? "play" : "arrow"
                                    size: 13
                                    color: "#e0fffb"
                                }
                                HoverHandler { id: badgeHover }
                                ToolTip.visible: badgeHover.hovered && badge.modelData.label.length > 0
                                ToolTip.text: badge.modelData.label
                                // MouseArea (statt TapHandler): verbraucht den Press,
                                // sodass der darunterliegende Auswahl-Fänger bei einem
                                // Badge-Klick KEINE Markierung startet. Annotation wird
                                // wie gehabt aktiviert.
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.activateAnnotation(badge.modelData)
                                }
                            }
                        }

                        // ── Audio-Hotspots (PdfAudio; je Seite) ──────────────────
                        //  Klickbare Marker über den Audio-Buttons: Klick spielt den
                        //  Clip ab und öffnet die Audioleiste. Aktiver Clip pulsiert.
                        Repeater {
                            model: root.audioClips
                            delegate: Rectangle {
                                id: aspot
                                required property var modelData
                                readonly property bool isActive: root.activeClipId === aspot.modelData.id
                                visible: aspot.modelData.page === pageCell.index
                                x: aspot.modelData.x * pageImg.width
                                y: aspot.modelData.y * pageImg.height
                                width:  Math.max(18, aspot.modelData.w * pageImg.width)
                                height: Math.max(18, aspot.modelData.h * pageImg.height)
                                radius: 4
                                color: aspot.isActive
                                       ? Qt.rgba(root.audioAccent.r, root.audioAccent.g, root.audioAccent.b, 0.30)
                                       : (aspotHover.hovered
                                          ? Qt.rgba(root.audioAccent.r, root.audioAccent.g, root.audioAccent.b, 0.26)
                                          : Qt.rgba(root.audioAccent.r, root.audioAccent.g, root.audioAccent.b, 0.12))
                                border.color: root.audioAccent
                                border.width: aspot.isActive ? 2 : 1
                                DrawnIcon {
                                    anchors.centerIn: parent
                                    name: (aspot.isActive && audioPlayer.playbackState === MediaPlayer.PlayingState)
                                          ? "pause" : "audio"
                                    size: 13
                                    color: "#ffffff"
                                }
                                HoverHandler { id: aspotHover }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: { root.audioPanelVisible = true; root.playClip(aspot.modelData.id) }
                                }
                            }
                        }

                        // ── PDF-Editor: Werkzeug-Fänger der Seite (nur Edit-
                        //    Modus). Liegt ÜBER Auswahl-Fänger, Badges und Audio-
                        //    Hotspots (Editmodus hat Vorrang - Annotationen sind
                        //    dann bewusst nicht klickbar), aber UNTER den Box-
                        //    Delegates: Klick auf eine Box trifft die Box (nur
                        //    Auswahl-Werkzeug - sonst haben deren MouseAreas
                        //    enabled:false und die Gesten fallen hierher durch).
                        //    Routing nach aktivem Werkzeug (Muster Bild-Editor):
                        //     • Auswählen  -> Klick auf freie Fläche wählt ab
                        //     • Textnotiz  -> Klick erstellt eine Notiz; Zeilen-
                        //       fang: Trifft der Klick das Fenster einer
                        //       erkannten Textzeile (−35 %…+135 % der Zeilenhöhe
                        //       um ihre Oberkante), entsteht eine VERANKERTE Box
                        //       exakt auf der Zeile (Größe/Schrift aus der
                        //       Zeile) - sonst eine freie Box am Klickpunkt
                        //     • Stift/Pfeil/Rechteck/Ellipse -> Ziehen zeichnet
                        //       live (Controller-Session; Koordinaten in
                        //       PDF-Punkten, an die Seite geklemmt)
                        //     • Text ersetzen -> Aufziehen erzeugt eine weiße
                        //       Deckfläche + Textbox als EIN Objekt; beim
                        //       Loslassen schnappt die Box auf die erkannten
                        //       Textzeilen (pdfTextCtl.replaceProbe) und über-
                        //       nimmt Schriftgröße + eingebetteten Text - ohne
                        //       Textebene bleibt sie STILL unbefüllt (Vorlage)
                        MouseArea {
                            id: createArea
                            anchors.fill: parent
                            enabled: root.editCtl.editMode
                            visible: root.editCtl.editMode
                            acceptedButtons: Qt.LeftButton
                            preventStealing: root.editCtl.tool >= 2
                            cursorShape: root.editCtl.tool === 0 ? Qt.ArrowCursor
                                         : (root.editCtl.tool === 6
                                            || root.editCtl.tool === 7
                                            || root.editCtl.tool === 8) ? Qt.IBeamCursor
                                                                   : Qt.CrossCursor
                            property int _drawId: -1
                            //  Zustand der beiden NICHT-zeichnenden Gesten dieses
                            //  Fängers. Diese Bezeichner waren nirgends deklariert:
                            //  QML wirft beim Lesen eines unbekannten Namens eine
                            //  ReferenceError und beim Schreiben „Invalid write to
                            //  global property" - beides bricht den Handler SOFORT
                            //  ab. Folge: (a) „Text ersetzen" komplett tot, (b)
                            //  Markieren im Editmodus unmöglich, (c) jedes Zeichnen
                            //  endete am Druckpunkt, weil schon die erste Zeile von
                            //  onPositionChanged/onReleased warf (updateDraw/endDraw
                            //  wurden nie erreicht -> nur der Stummel blieb stehen).
                            //   • _textSel/_textSelDrag/_selSx/_selSy -> Markieren
                            //     mit dem Auswahl-Werkzeug (wie in der Leseansicht)
                            //   • _repStart/_repLast -> aufgezogener Bereich des
                            //     „Text ersetzen"-Werkzeugs (seiten-normiert 0..1)
                            property bool _textSel: false
                            property bool _textSelDrag: false
                            property real _selSx: 0
                            property real _selSy: 0
                            property var  _repStart: null
                            property var  _repLast: null
                            //  Werkzeugwechsel auf ⇄ -> Textebene (nach)laden,
                            //  falls die editMode-Vorbereitung verworfen wurde
                            //  (Quellenwechsel/Race) - prepare() ist idempotent.
                            Connections {
                                target: root.editCtl
                                function onToolChanged() {
                                    //  Nur die Textebene vorbereiten; die
                                    //  Umwandlung einer bestehenden Markierung
                                    //  läuft über root.replaceSelectionNow()
                                    //  (vom ⇄-Knopf aufgerufen - feuert auch,
                                    //  wenn ⇄ schon aktiv war).
                                    if ((root.editCtl.tool === 6 || root.editCtl.tool === 8)
                                            && root.source.length > 0)
                                        pdfTextCtl.prepare(root.source)
                                }
                            }
                            // Mausposition -> PDF-Punkte, an die Seite geklemmt.
                            function _toPt(mx, my) {
                                var pts = pageCell.pts
                                if (pts.width <= 0 || pts.height <= 0)
                                    return null
                                return { x: Math.max(0, Math.min((mx / pageImg.width)  * pts.width,  pts.width)),
                                         y: Math.max(0, Math.min((my / pageImg.height) * pts.height, pts.height)) }
                            }
                            onPressed: (m) => {
                                //  Markieren wie in der Leseansicht. BEWUSST OHNE
                                //  `pdfTextCtl.ready`-Wache: beginSelection() lädt
                                //  die Textebene lazy und der onReadyChanged-
                                //  Catch-up (s. root._selecting/_lastSel) zieht die
                                //  Auswahl nach. Mit der Wache blieb der erste
                                //  Markierversuch im Editmodus wirkungslos, solange
                                //  die Ebene noch nie geladen war (selArea in der
                                //  Leseansicht kennt die Wache ebenfalls nicht -
                                //  daher „Markieren geht nur außerhalb des
                                //  Editmodus").
                                if (root.editCtl.tool === 0
                                        && pageImg.width > 0 && pageImg.height > 0) {
                                    _textSel = true
                                    _textSelDrag = false
                                    _selSx = m.x; _selSy = m.y
                                    root.beginSelection(pageCell.index)
                                    return
                                }
                                //  „Text bearbeiten": Klick setzt das Caret in die
                                //  eingebettete Textebene. KEINE Zeichen-Session
                                //  (Werkzeug 7 liegt über der Zeichen-Schwelle 2,
                                //  würde sonst in beginDraw laufen).
                                if (root.editCtl.tool === 7) {
                                    root.commitEditing()
                                    const cp = _toPt(m.x, m.y)
                                    if (cp) {
                                        const first = !root.editCtl.caretReady
                                                      || root.editCtl.caretPage !== pageCell.index
                                        root.editCtl.placeCaret(pageCell.index, cp.x, cp.y)
                                        caretInput.forceActiveFocus()
                                        // Beim ersten Klick auf eine Seite wird die
                                        // Textebene asynchron gelesen - das dauert
                                        // sichtbar, also sagen wir es.
                                        if (first && !root.editCtl.caretReady)
                                            root._toast(App.uiText(App.language,
                                                                   "PdfEditCaretLoading"))
                                    }
                                    return
                                }
                                if (root.editCtl.tool < 2)
                                    return                       // Auswahl/Text: s. onClicked
                                root.commitEditing()
                                const p = _toPt(m.x, m.y)
                                if (!p) return
                                root.notesVisible = true
                                const pts = pageCell.pts
                                //  Markieren (Werkzeug 8) zieht wie „Text
                                //  ersetzen" auf: Die Textauswahl darunter
                                //  liefert die Zeilenbereiche. Ohne Textebene
                                //  bleibt der aufgezogene Bereich selbst.
                                if (root.editCtl.tool === 8
                                        && pts.width > 0 && pts.height > 0) {
                                    _repStart = { x: p.x / pts.width, y: p.y / pts.height }
                                    _repLast  = _repStart
                                    if (pdfTextCtl.ready)
                                        root.updateSelection(pageCell.index,
                                            _repStart.x, _repStart.y, _repStart.x, _repStart.y)
                                    return
                                }
                                //  Schwärzen (Werkzeug 9) zieht wie „Text
                                //  ersetzen" auf: dieselbe Auswahl, anderer
                                //  Abschluss.
                                if ((root.editCtl.tool === 6 || root.editCtl.tool === 9)
                                        && pdfTextCtl.ready
                                        && pts.width > 0 && pts.height > 0) {
                                    // Auswahl-Modus: wie die normale Text-
                                    // Selektion, nur über das Werkzeug.
                                    _repStart = { x: p.x / pts.width, y: p.y / pts.height }
                                    _repLast  = _repStart
                                    root.updateSelection(pageCell.index,
                                        _repStart.x, _repStart.y, _repStart.x, _repStart.y)
                                    return
                                }
                                // Tool 2..6 -> PdfAnnKind 1..5 (Freihand…Text ersetzen).
                                _drawId = root.editCtl.beginDraw(root.editCtl.tool - 1,
                                                                 pageCell.index, p.x, p.y)
                            }
                            onPositionChanged: (m) => {
                                if (_textSel) {
                                    if (!_textSelDrag) {
                                        if (Math.abs(m.x - _selSx) + Math.abs(m.y - _selSy) < 3)
                                            return
                                        _textSelDrag = true
                                    }
                                    root.updateSelection(pageCell.index,
                                        _selSx / pageImg.width, _selSy / pageImg.height,
                                        m.x     / pageImg.width, m.y     / pageImg.height)
                                    return
                                }
                                if (_repStart) {
                                    const p = _toPt(m.x, m.y)
                                    const pts = pageCell.pts
                                    if (p && pts.width > 0) {
                                        _repLast = { x: p.x / pts.width, y: p.y / pts.height }
                                        root.updateSelection(pageCell.index,
                                            _repStart.x, _repStart.y, _repLast.x, _repLast.y)
                                    }
                                    return
                                }
                                if (_drawId < 0) return
                                const p = _toPt(m.x, m.y)
                                if (p) root.editCtl.updateDraw(_drawId, p.x, p.y)
                            }
                            onReleased: {
                                if (_textSel) {
                                    root.endSelection(_textSelDrag)
                                    if (!_textSelDrag) {         // reiner Klick: abwählen
                                        root.commitEditing()
                                        root.editCtl.selectedId = -1
                                    }
                                    _textSel = false
                                    return
                                }
                                if (_repStart) {
                                    const pts = pageCell.pts
                                    //  Markieren: keine Zeichen-Session, die
                                    //  Auswahl IST das Ergebnis.
                                    if (root.editCtl.tool === 8) {
                                        if (pts.width > 0 && _repLast) {
                                            const x0 = Math.min(_repStart.x, _repLast.x) * pts.width
                                            const y0 = Math.min(_repStart.y, _repLast.y) * pts.height
                                            const x1 = Math.max(_repStart.x, _repLast.x) * pts.width
                                            const y1 = Math.max(_repStart.y, _repLast.y) * pts.height
                                            root.markSelectionNow(
                                                root.editCtl.markupStyle(),
                                                { page: pageCell.index, x: x0, y: y0,
                                                  w: x1 - x0, h: y1 - y0 })
                                        }
                                        _repStart = null; _repLast = null
                                        return
                                    }
                                    if (pts.width > 0 && _repLast) {
                                        _drawId = root.editCtl.beginDraw(5, pageCell.index,
                                                      _repStart.x * pts.width,
                                                      _repStart.y * pts.height)
                                        root.editCtl.updateDraw(_drawId,
                                                      _repLast.x * pts.width,
                                                      _repLast.y * pts.height)
                                        if (root.editCtl.tool === 9) _finishRedact()
                                        else                         _finishReplace()
                                    }
                                    root.clearSelection()
                                    _repStart = null; _repLast = null
                                    _drawId = -1
                                    return
                                }
                                if (_drawId < 0) return
                                if (root.editCtl.tool === 6) _finishReplace()
                                else if (root.editCtl.tool === 9) _finishRedact()
                                else root.editCtl.endDraw(_drawId)
                                _drawId = -1
                            }
                            onCanceled: {
                                if (_textSel) {
                                    root.endSelection(_textSelDrag)
                                    _textSel = false
                                }
                                if (_repStart) {
                                    root.clearSelection()
                                    _repStart = null; _repLast = null
                                }
                                if (_drawId < 0) return
                                root.editCtl.endDraw(_drawId)
                                _drawId = -1
                            }
                            // „Text ersetzen"-Abschluss: die Textzeilen-Sonde
                            // (replaceProbe) liefert Zeilen-Bounds, Ø-Zeilen-
                            // höhe und den eingebetteten Text unter der auf-
                            // gezogenen Fläche; der Controller schnappt die
                            // Box darauf ein und befüllt sie vor. Ohne Text-
                            // ebene/Treffer (gescannte PDF) -> snapped=false,
                            // die Box bleibt STILL unbefüllt (Anforderung:
                            // kein Hinweis-Dialog/Toast). Die neue Box startet
                            // wie Notizen direkt in der Textbearbeitung.
                            //  Abschluss der Schwärzung: dieselbe Zeilen-Sonde
                            //  wie beim Ersetzen - nur wandert der erkannte
                            //  Text NICHT in die Box, sondern in `origText`,
                            //  damit der Export ihn aus dem Strom entfernt.
                            function _finishRedact() {
                                const info = root.editCtl.boxInfo(_drawId)
                                const pts = pageCell.pts
                                let snapped = false
                                let sx = 0, sy = 0, sw = 0, sh = 0, txt = ""
                                if (info.exists === true && pdfTextCtl.ready
                                        && pts.width > 0 && pts.height > 0) {
                                    const pr = pdfTextCtl.replaceProbe(pageCell.index,
                                                   info.xPt / pts.width,
                                                   info.yPt / pts.height,
                                                   (info.xPt + info.wPt) / pts.width,
                                                   (info.yPt + info.hPt) / pts.height)
                                    if (pr.found === true) {
                                        snapped = true
                                        sx = pr.x * pts.width;  sy = pr.y * pts.height
                                        sw = pr.w * pts.width;  sh = pr.h * pts.height
                                        txt = pr.text
                                    }
                                }
                                const nid = root.editCtl.endRedactDraw(
                                                _drawId, snapped, sx, sy, sw, sh, txt)
                                if (nid >= 0) {
                                    root.notesVisible = true
                                    root._redactHintOnce()
                                }
                            }

                            function _finishReplace() {
                                const info = root.editCtl.boxInfo(_drawId)
                                const pts = pageCell.pts
                                let snapped = false
                                let sx = 0, sy = 0, sw = 0, sh = 0, lh = 0, txt = ""
                                if (info.exists === true && pdfTextCtl.ready
                                        && pts.width > 0 && pts.height > 0) {
                                    const pr = pdfTextCtl.replaceProbe(pageCell.index,
                                                   info.xPt / pts.width,
                                                   info.yPt / pts.height,
                                                   (info.xPt + info.wPt) / pts.width,
                                                   (info.yPt + info.hPt) / pts.height)
                                    if (pr.found === true) {
                                        snapped = true
                                        sx = pr.x * pts.width;  sy = pr.y * pts.height
                                        sw = pr.w * pts.width;  sh = pr.h * pts.height
                                        lh = pr.lineH * pts.height
                                        txt = pr.text
                                    }
                                }
                                const nid = root.editCtl.endReplaceDraw(
                                                _drawId, snapped, sx, sy, sw, sh, lh, txt)
                                if (nid >= 0) {
                                    root.notesVisible = true
                                    root._autoEditId = nid
                                }
                            }
                            onClicked: (m) => {
                                if (root.editCtl.tool >= 2)
                                    return                       // Zeichnen lief über pressed/released
                                root.commitEditing()
                                if (root.editCtl.tool === 0) {   // Auswahl: nur abwählen
                                    root.editCtl.selectedId = -1
                                    return
                                }
                                var pts = pageCell.pts
                                if (pts.width <= 0 || pts.height <= 0)
                                    return
                                var xPt = (m.x / pageImg.width)  * pts.width
                                var yPt = (m.y / pageImg.height) * pts.height
                                var id = -1
                                if (root.snapEnabled && pdfTextCtl.ready) {
                                    var lines = root._snapLines(pageCell.index)
                                    for (var i = 0; i < lines.length; i++) {
                                        var ly = lines[i].y * pts.height
                                        var lh = lines[i].h * pts.height
                                        if (yPt >= ly - lh * 0.35 && yPt <= ly + lh * 1.35) {
                                            id = root.editCtl.addAnchoredTextBox(
                                                     pageCell.index,
                                                     lines[i].x * pts.width, ly,
                                                     Math.max(48, lines[i].w * pts.width), lh)
                                            break
                                        }
                                    }
                                }
                                if (id < 0)
                                    id = root.editCtl.addTextBox(pageCell.index, xPt, yPt,
                                                            pts.width, pts.height)
                                // Neue Box direkt in die Textbearbeitung schicken;
                                // evtl. per Toggle ausgeblendete Notizen wieder
                                // zeigen (die frische Notiz muss sichtbar sein).
                                root.notesVisible = true
                                root._autoEditId = id
                            }
                        }

                        // ── PDF-Editor: Overlay-Textboxen - sichtbar in beiden
                        //    Modi, solange der Notizen-Toggle (Alt+Q/◉) sie
                        //    nicht ausblendet; interaktiv nur im Editmodus.
                        //    Ein Repeater über ALLE Boxen je Seite; das
                        //    Delegate blendet sich über page===pageIndex selbst
                        //    ein (Boxzahl ist klein - kein Proxy-Filter nötig).
                        Repeater {
                            model: root.editCtl.boxModel
                            delegate: PdfEditBox {
                                //  Die Notiz findet ihre Seite über den STABILEN
                                //  Seiten-Key (viewPageKey), nicht über die
                                //  Position - deshalb bleibt sie beim
                                //  Umsortieren/Einfügen an ihrer Seite.
                                //  (viewPageCount als Reaktiv-Trigger bei Plan-Änd.)
                                pageIndex: (root.editCtl.viewPageCount,
                                            root.editCtl.viewPageKey(pageCell.index))
                                viewIndex: pageCell.index
                                pageScale: pageCell.pts.width > 0
                                           ? pageImg.width / pageCell.pts.width : 1
                                pageWPt: Math.max(1, pageCell.pts.width)
                                pageHPt: Math.max(1, pageCell.pts.height)
                                surface: root
                            }
                        }

                        // ── Formularfelder (AcroForm) dieser Seite ────────────
                        //  Qt PDF zeichnet Widget-Annotationen NICHT - dieses
                        //  Overlay ist die EINZIGE Darstellung der Felder und
                        //  deshalb in BEIDEN Modi aktiv (ein Formular gehört
                        //  dem Dokument, nicht dem Editor). Die Liste ändert
                        //  sich nur bei Dokument-/Plan-Wechsel; das Tippen
                        //  läuft rev-getrieben (formValueRev), damit die
                        //  Delegates nicht je Zeichen neu entstehen.
                        Repeater {
                            model: root.editCtl.formFields
                            delegate: PdfFormField {
                                required property var modelData
                                field: modelData
                                visible: modelData.page === pageCell.index
                                enabled: visible
                                ctl: root.editCtl
                                pageScale: pageCell.pts.width > 0
                                           ? pageImg.width / pageCell.pts.width : 1
                                pageWPt: Math.max(1, pageCell.pts.width)
                                pageHPt: Math.max(1, pageCell.pts.height)
                            }
                        }

                        // ── PDF-Editor: schwebende Format-Toolbar der Auswahl ─────
                        PdfEditToolbar {
                            pageIndex: (root.editCtl.viewPageCount,
                                        root.editCtl.viewPageKey(pageCell.index))
                            pageScale: pageCell.pts.width > 0
                                       ? pageImg.width / pageCell.pts.width : 1
                            pageW: pageImg.width
                            pageH: pageImg.height
                            surface: root
                        }

                        // ── Rechtsklick: Seiten-Kontextmenü (Extraktion) ──────────
                        //  Eigene Ebene NUR für die rechte Maustaste, ÜBER
                        //  selArea/createArea/Boxen: die Linksklick-Logik bleibt
                        //  unberührt (acceptedButtons filtert), Rechtsklick war
                        //  bisher ungenutzt. Das Menü lebt EINMAL im Root
                        //  (pageCtxMenu) - kein Menü je Delegate.
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            z: 60
                            onClicked: (m) => {
                                pageCtxMenu.ctxPage = pageCell.index
                                pageCtxMenu.popup()
                            }
                        }
                    }

                    // ── Aufgabe 3: „+"-Linie unter der Seite (nur Editmodus) ──────
                    //  Klick fügt eine leere A4-Seite NACH dieser Ansichts-Seite ein.
                    Item {
                        visible: pageCell.showAddLine
                        // Kind von pageBg -> in SEITEN-Koordinaten positionieren
                        // (nicht nochmal über pages.width zentrieren, sonst
                        //  doppelter Seiten-Offset -> nach rechts versetzt).
                        x: 0
                        width: parent.width          // = Seitenbreite
                        height: 26
                        y: parent.height + 4         // direkt unter der Seite
                        Rectangle {                                   // durchgehende Linie
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left; anchors.right: parent.right
                            height: 1
                            color: addBtnHover.hovered ? App.themeAccent : App.themeBorder
                        }
                        Rectangle {                                   // „+"-Knopf mittig
                            anchors.centerIn: parent
                            width: 22; height: 22; radius: 11
                            color: addBtnHover.hovered ? App.themeAccent : App.themeToolbarBg
                            border.color: App.themeBorder
                            Text {
                                anchors.centerIn: parent; text: "+"
                                font.pixelSize: 16; font.bold: true
                                color: addBtnHover.hovered ? "white" : App.themeTextPrimary
                            }
                            HoverHandler { id: addBtnHover }
                            TapHandler { onTapped: root.editCtl.addBlankPageAfter(pageCell.index) }
                            ToolTip.visible: addBtnHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: App.uiText(App.language, "PdfAddPageTip")
                        }
                    }
                }
            }
        }

        // ── Wheel-Fänger über den Seiten (NoButton -> Klicks/Badges bleiben aktiv) ──
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            z: 1
            onWheel: (wheel) => root.wheelPages(wheel)
        }

        // ── Thumbnail-Seitenleiste (OVERLAY links; kein Seiten-Reflow) ─────────
        Rectangle {
            id: thumbPanel
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: 152
            visible: root.thumbsVisible && root.pageCount > 0
            z: 3
            color: App.themeSidebarBg
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: App.themeBorder }

            ListView {
                id: thumbs
                objectName: "pdfThumbs"           // Prüfstand: MG_BENCH_WHEEL
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                // Erst NACH dem Warmlauf befuellen -> die Thumbnail-Renderings
                // konkurrieren nicht mehr mit der ersten sichtbaren Hauptseite um
                // den PDFium-Render-Mutex (entspricht dem 120-ms-Deferral der
                // alten QPdfView-Version).
                model: (root.docReady && root._warm) ? root.doc.pageCount : 0
                spacing: 10
                // Vorschauen kommen jetzt JPEG-komprimiert aus dem RAM-Provider -
                // Scrollen kostet nur einen winzigen Dekode, kein PDFium-Render.
                // Etwas mehr Vorhalt haelt die Leiste auch bei schnellem Scrollen
                // luecken­frei, ohne nennenswerten RAM (wenige KB je Vorschau).
                // max(0, …): vor dem ersten Layout ist die Höhe negativ
                // (Panel-Höhe 0 minus 2×8 px Rand) - ein negativer cacheBuffer
                // ist ungültig und wurde beim Start als Warnung gemeldet.
                cacheBuffer: Math.max(0, Math.round(thumbs.height * 1.5))
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                // ── Seiten umsortieren (Ziehen in der Vorschauleiste) ──────────
                //  Nur im Editmodus. Gezogen wird die VORSCHAU, umsortiert wird
                //  der Seiten-Plan im Controller (movePage -> EIN Undo-Schritt);
                //  die Notizen folgen ihrer Seite über den Seiten-Key.
                //  dragIndex = gezogene Seite, dropIndex = Zielposition.
                property int dragIndex: -1
                property int dropIndex: -1
                readonly property bool reorderable: root.editCtl.editMode
                                                    && root.editCtl.viewPageCount > 1

                //  Zielposition aus einer y-Koordinate IN der Liste (Inhalts-
                //  koordinaten). Zwischen zwei Kacheln (Lücke) liefert indexAt
                //  −1 - dann entscheidet die Nähe: oberhalb der ersten Kachel
                //  die 0, unterhalb der letzten die letzte Position.
                function indexForContentY(cy) {
                    var i = thumbs.indexAt(thumbs.width / 2, cy)
                    if (i >= 0)
                        return i
                    if (cy <= thumbs.originY)
                        return 0
                    if (cy >= thumbs.originY + thumbs.contentHeight)
                        return thumbs.count - 1
                    //  In der Lücke: die nächstgelegene Kachel oberhalb suchen.
                    for (var d = 4; d < 40; d += 4) {
                        var up = thumbs.indexAt(thumbs.width / 2, cy - d)
                        if (up >= 0)
                            return up
                        var dn = thumbs.indexAt(thumbs.width / 2, cy + d)
                        if (dn >= 0)
                            return dn
                    }
                    return -1
                }

                //  Randnähe beim Ziehen scrollt die Leiste weiter - sonst wäre
                //  nur innerhalb des sichtbaren Ausschnitts umsortierbar.
                Timer {
                    id: thumbAutoScroll
                    interval: 16; repeat: true
                    running: thumbs.dragIndex >= 0 && dir !== 0
                    property int dir: 0
                    onTriggered: {
                        var scrollable = thumbs.contentHeight - thumbs.height
                        if (scrollable <= 0)
                            return
                        var minY = thumbs.originY
                        var maxY = thumbs.originY + scrollable
                        thumbs.contentY = Math.max(minY, Math.min(thumbs.contentY + dir * 6, maxY))
                    }
                }

                delegate: Item {
                    id: thumbCell
                    required property int index
                    // Cache-Buster: hochzaehlen, sobald die Vorschau gerendert ist
                    // -> die Image-source wird neu angefordert und aus dem RAM-Store
                    //   geliefert.
                    property int rev: 0
                    readonly property size pts: root.doc.pagePointSize(index)
                    readonly property real thumbW: thumbs.width - 8
                    readonly property real thumbH: pts.width > 0 ? thumbW * (pts.height / pts.width) : thumbW * 1.414
                    width: thumbs.width
                    height: thumbH + 18

                    // Meldung des Providers: genau diese Seite liegt jetzt im Store.
                    Connections {
                        target: PdfThumbs
                        function onPageReady(docId, page) {
                            if (docId === root._thumbDocId && page === thumbCell.index)
                                thumbCell.rev++
                        }
                    }

                    Rectangle {
                        id: thumbFrame
                        width: thumbCell.thumbW; height: thumbCell.thumbH
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        color: "white"
                        border.color: thumbCell.index === root.currentPage ? App.themeAccent
                                                                           : App.themeBorder
                        border.width: thumbCell.index === root.currentPage ? 2 : 1
                        // Vorschau kommt JPEG-komprimiert aus dem RAM-Provider:
                        // KEIN PdfPageImage mehr -> kein PDFium-Render am Haupt-Mutex,
                        // Scrollen dekodiert nur winzige JPEGs (asynchron, < 1 ms).
                        Image {
                            id: thumbImg
                            anchors.fill: parent
                            anchors.margins: thumbFrame.border.width
                            asynchronous: true
                            cache: false
                            fillMode: Image.PreserveAspectFit
                            source: root._thumbDocId > 0
                                    ? "image://pdfthumb/" + root._thumbDocId + "/"
                                      + thumbCell.index + "?r=" + thumbCell.rev
                                    : ""
                            sourceSize.width: thumbCell.thumbW * Screen.devicePixelRatio
                        }
                        //  Gezogene Kachel blasser zeichnen (sie „liegt in der Hand").
                        opacity: thumbs.dragIndex === thumbCell.index ? 0.45 : 1.0
                        TapHandler { onTapped: root.goToPage(thumbCell.index) }

                        //  Ziehen sortiert um. TapHandler und DragHandler
                        //  vertragen sich: Wird aus dem Druck ein Zug, feuert der
                        //  Tap nicht mehr (kein Seitensprung beim Umsortieren).
                        DragHandler {
                            id: thumbDrag
                            enabled: thumbs.reorderable
                            target: null                      // wir bewegen nichts selbst
                            cursorShape: Qt.ClosedHandCursor
                            //  ENTSCHEIDEND in einer ListView: Ohne die
                            //  eingeschränkten Rechte reißt die Liste den Griff
                            //  an sich und scrollt, statt die Seite ziehen zu
                            //  lassen (senkrechter Zug in senkrechter Liste).
                            //  Kein „Approves…“ = die Liste darf nicht stehlen.
                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                            onActiveChanged: {
                                if (active) {
                                    thumbs.dragIndex = thumbCell.index
                                    thumbs.dropIndex = thumbCell.index
                                    return
                                }
                                //  Loslassen: umsortieren (EIN Undo-Schritt).
                                const from = thumbs.dragIndex
                                const to   = thumbs.dropIndex
                                thumbs.dragIndex = -1
                                thumbs.dropIndex = -1
                                thumbAutoScroll.dir = 0
                                if (from >= 0 && to >= 0 && from !== to) {
                                    root.commitEditing()
                                    root.editCtl.movePage(from, to)
                                    root.goToPage(to)
                                }
                            }
                            onCentroidChanged: {
                                if (!active)
                                    return
                                //  Randnähe -> Leiste mitscrollen (Sichtfenster-
                                //  Koordinaten).
                                const ly = thumbs.mapFromItem(null, centroid.scenePosition).y
                                thumbAutoScroll.dir = ly < 24 ? -1
                                                   : ly > thumbs.height - 24 ? 1 : 0
                                //  Zielposition: indexAt() erwartet INHALTS-
                                //  koordinaten -> direkt über das contentItem
                                //  abbilden (originY-sicher).
                                const cy = thumbs.contentItem.mapFromItem(
                                               null, centroid.scenePosition).y
                                const i = thumbs.indexForContentY(cy)
                                if (i >= 0)
                                    thumbs.dropIndex = i
                            }
                        }
                        ToolTip.visible: thumbDrag.active
                        ToolTip.text: App.uiText(App.language, "PdfMovePageTip")
                    }

                    //  Einfügemarke: zeigt, WO die gezogene Seite landet.
                    Rectangle {
                        visible: thumbs.dragIndex >= 0 && thumbs.dropIndex === thumbCell.index
                                 && thumbs.dragIndex !== thumbCell.index
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: thumbCell.thumbW; height: 3; radius: 1.5
                        color: App.themeAccent
                        //  Von oben gezogen -> Marke unter die Zielkachel, sonst darüber.
                        y: thumbs.dragIndex < thumbCell.index ? thumbFrame.height + 1 : -2
                        z: 5
                    }

                    Row {
                        anchors.top: thumbFrame.bottom; anchors.topMargin: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 4
                        Text {
                            text: (thumbCell.index + 1)
                            color: thumbCell.index === root.currentPage ? App.themeAccent
                                                                        : App.themeTextMuted
                            font.pixelSize: 10
                        }
                        //  Marke für eingefügte (fremde) Seiten - im Editmodus
                        //  sichtbar, damit erkennbar bleibt, was nicht aus dem
                        //  Originaldokument stammt (dort ist auch kein
                        //  zeichenweises Bearbeiten möglich).
                        Text {
                            visible: root.editCtl.editMode
                                     && (root.editCtl.viewPageCount,
                                         root.editCtl.pageInfo(thumbCell.index).imported === true)
                            text: App.uiText(App.language, "PdfPageImportedBadge")
                            color: App.themeAccent
                            font.pixelSize: 9
                        }
                    }
                }
            }

            // Web-artiges, animiertes Wheel-Scrollen der Seitenleiste (wie die
            // Hauptansicht). Liegt als NoButton-MouseArea ueber der Liste: faengt
            // nur das Mausrad, laesst Klicks (Thumbnail-Tap, ScrollBar) hindurch.
            NumberAnimation {
                id: thumbsScroll
                target: thumbs; property: "contentY"
                duration: 180; easing.type: Easing.OutCubic
            }
            MouseArea {
                anchors.fill: thumbs
                acceptedButtons: Qt.NoButton
                z: 1
                onWheel: (wheel) => {
                    // Origin-bewusst wie die Hauptansicht (s. clampContentY):
                    // positionViewAtIndex (Seitensprünge) kann originY ≠ 0 erzeugen.
                    var scrollable = thumbs.contentHeight - thumbs.height
                    if (scrollable <= 0) { wheel.accepted = true; return }
                    var minY = thumbs.originY
                    var maxY = thumbs.originY + scrollable
                    var raw = (wheel.angleDelta.y !== 0)
                              ? (wheel.angleDelta.y / 120) * (thumbs.height * 0.55)
                              : wheel.pixelDelta.y * 1.6
                    var base = thumbsScroll.running ? thumbsScroll.to : thumbs.contentY
                    var tgt = Math.max(minY, Math.min(base - raw, maxY))
                    thumbsScroll.from = thumbs.contentY
                    thumbsScroll.to = tgt
                    thumbsScroll.restart()
                    wheel.accepted = true
                }
            }
        }

        // ── Audio-Panel (rechts; Overlay über den Seiten, KEIN Reflow) ─────────
        //  Symmetrisch zur Thumbnail-Leiste links. 14px Lücke rechts lässt die
        //  Dokument-Scrollleiste sichtbar. Zeigt NUR die Audios der aktuellen Seite
        //  („Audios nur da, wo sie herkommen"); der Mini-Player unten spielt
        //  unabhängig vom gerade angezeigten Seitenausschnitt weiter.
        // ── PDF-Editor: Text-Eigenschaften - Position je Einstellung ──────────
        //  Rechts (Seitenleiste, Standard) ODER oben (Leiste wie Word). Beide
        //  Instanzen teilen sich dieselbe PdfEditPanel-Datei (horizontal-Flag);
        //  sichtbar ist je nach PdfEdit.panelOnTop genau eine.
        PdfEditPanel {
            anchors { right: parent.right; rightMargin: 14; top: parent.top; bottom: parent.bottom }
            width: 320
            visible: root.editPanelVisible && root.editCtl.editMode && !PdfEdit.panelOnTop
            z: 4
            surface: root
        }
        PdfEditPanel {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 62
            horizontal: true
            visible: root.editPanelVisible && root.editCtl.editMode && PdfEdit.panelOnTop
            z: 4
            surface: root
        }

        Rectangle {
            id: audioPanel
            anchors { right: parent.right; rightMargin: 14; top: parent.top; bottom: parent.bottom }
            width: 300
            visible: root.audioPanelVisible && pdfAudioCtl.documentHasAudio
            z: 4
            color: App.themeSidebarBg

            // Clips der aktuellen Seite (hängt an audioClips + currentPage, NICHT an
            // _audioRev -> kein Delegate-Neuaufbau bei Positions-/Dauer-Updates).
            readonly property var pageClips: root._clipsOnPage(root.currentPage)

            Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: App.themeBorder }

            // Klicks/Wheel auf leeren Panel-Flächen abfangen (sonst Durchgriff auf
            // die Seiten-Textauswahl darunter). Kinder darüber behandeln ihre Events.
            MouseArea { anchors.fill: parent; onWheel: (wheel) => { wheel.accepted = true } }

            // Kopf
            Item {
                id: audioHeader
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 44
                Text {
                    anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                    text: App.uiText(App.language, "PdfAudioPanelHeader").arg(root.currentPage + 1)
                    color: App.themeTextPrimary; font.pixelSize: 13; font.bold: true
                }
                Rectangle {
                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    width: 26; height: 26; radius: 13
                    color: closeHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.14) : "transparent"
                    Text { anchors.centerIn: parent; text: "\u2715"; color: App.themeTextMuted; font.pixelSize: 13 }
                    HoverHandler { id: closeHover }
                    TapHandler { onTapped: root.audioPanelVisible = false }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: App.themeBorder }
            }

            // Leerzustand (Seite ohne Audio)
            Text {
                anchors.centerIn: parent
                visible: audioPanel.pageClips.length === 0
                text: App.uiText(App.language, "PdfNoAudioOnPage")
                color: App.themeTextMuted; font.pixelSize: 12
            }

            // Liste der Clips dieser Seite
            ListView {
                id: audioList
                anchors {
                    left: parent.left; right: parent.right
                    top: audioHeader.bottom
                    bottom: miniPlayer.visible ? miniPlayer.top : parent.bottom
                    margins: 8
                }
                clip: true
                spacing: 6
                model: audioPanel.pageClips
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: arow
                    required property var modelData
                    required property int index
                    readonly property int  cid: arow.modelData.id
                    readonly property bool isActive: root.activeClipId === arow.cid
                    readonly property int  durMs: (root._audioRev, root._clipDurMs(arow.cid))
                    width: audioList.width
                    height: 56
                    radius: 10
                    color: arow.isActive ? Qt.rgba(root.audioAccent.r, root.audioAccent.g, root.audioAccent.b, 0.12)
                                         : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.04)
                    border.color: arow.isActive ? root.audioAccent : "transparent"
                    border.width: 1

                    // Runder Play/Pause-Knopf
                    Rectangle {
                        id: arowBtn
                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 36; height: 36; radius: 18
                        color: root.audioAccent
                        opacity: arowBtnHover.hovered ? 0.85 : 1.0
                        DrawnIcon {
                            anchors.centerIn: parent
                            name: (arow.isActive && audioPlayer.playbackState === MediaPlayer.PlayingState)
                                  ? "pause" : "play"
                            size: 15
                            color: "white"
                        }
                        HoverHandler { id: arowBtnHover }
                        TapHandler { onTapped: root.playClip(arow.cid) }
                    }

                    Text {
                        id: arowTitle
                        anchors { left: arowBtn.right; leftMargin: 10; top: parent.top; topMargin: 9 }
                        text: App.uiText(App.language, "PdfAudioItemLabel").arg(arow.index + 1)
                        color: App.themeTextPrimary; font.pixelSize: 13; font.bold: arow.isActive
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 12; verticalCenter: arowTitle.verticalCenter }
                        text: arow.durMs > 0 ? root._fmtTime(arow.durMs) : "\u2013:\u2013\u2013"
                        color: App.themeTextMuted; font.pixelSize: 11
                    }

                    // Fortschritts-/Seek-Slider (wie YouTube/Spotify): zeigt Gehörtes,
                    // an beliebige Stelle ziehbar -> ab dort weiter/starten.
                    Slider {
                        id: arowSlider
                        anchors { left: arowBtn.right; leftMargin: 10; right: parent.right; rightMargin: 12; bottom: parent.bottom; bottomMargin: 8 }
                        height: 16
                        from: 0
                        to: Math.max(1, arow.durMs)
                        // pressed?value:… hält die Bindung beim Ziehen intakt.
                        value: pressed ? value
                                       : (arow.isActive ? audioPlayer.position
                                                        : (root._audioRev, root._savedPos(arow.cid)))
                        onMoved: if (arow.isActive) audioPlayer.seek(value)
                        onPressedChanged: {
                            if (!pressed && !arow.isActive) {
                                var m = root._audioPos; m[arow.cid] = value; root._audioPos = m; root._audioRev++
                                root.playClip(arow.cid)     // ab gewählter Stelle starten
                            }
                        }
                        background: Rectangle {
                            x: arowSlider.leftPadding; y: arowSlider.topPadding + arowSlider.availableHeight / 2 - height / 2
                            width: arowSlider.availableWidth; height: 4; radius: 2
                            color: Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.35)
                            Rectangle { width: arowSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: root.audioAccent }
                        }
                        handle: Rectangle {
                            x: arowSlider.leftPadding + arowSlider.visualPosition * (arowSlider.availableWidth - width)
                            y: arowSlider.topPadding + arowSlider.availableHeight / 2 - height / 2
                            width: 12; height: 12; radius: 6
                            color: root.audioAccent; border.color: "white"; border.width: 1
                        }
                    }
                }
            }

            // ── Mini-Player (Now-Playing; unten angedockt) ─────────────────────
            Rectangle {
                id: miniPlayer
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 84
                visible: root.activeClipId >= 0
                color: App.themeToolbarBg
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: App.themeBorder }

                Rectangle {
                    id: miniBtn
                    anchors { left: parent.left; leftMargin: 14; top: parent.top; topMargin: 12 }
                    width: 44; height: 44; radius: 22
                    color: root.audioAccent
                    opacity: miniBtnHover.hovered ? 0.85 : 1.0
                    DrawnIcon {
                        anchors.centerIn: parent
                        name: audioPlayer.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                        size: 18
                        color: "white"
                    }
                    HoverHandler { id: miniBtnHover }
                    TapHandler { onTapped: root.playClip(root.activeClipId) }
                }
                Text {
                    id: miniTitle
                    anchors { left: miniBtn.right; leftMargin: 12; right: parent.right; rightMargin: 14; top: parent.top; topMargin: 13 }
                    text: root._activeTitle.length > 0 ? root._activeTitle : "Audio"
                    color: App.themeTextPrimary; font.pixelSize: 13; font.bold: true
                    elide: Text.ElideRight
                }
                Slider {
                    id: miniSlider
                    anchors { left: miniBtn.right; leftMargin: 12; right: parent.right; rightMargin: 14; top: miniTitle.bottom; topMargin: 6 }
                    height: 18
                    from: 0; to: Math.max(1, audioPlayer.duration)
                    value: pressed ? value : audioPlayer.position
                    onMoved: audioPlayer.seek(value)
                    background: Rectangle {
                        x: miniSlider.leftPadding; y: miniSlider.topPadding + miniSlider.availableHeight / 2 - height / 2
                        width: miniSlider.availableWidth; height: 4; radius: 2
                        color: Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.35)
                        Rectangle { width: miniSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: root.audioAccent }
                    }
                    handle: Rectangle {
                        x: miniSlider.leftPadding + miniSlider.visualPosition * (miniSlider.availableWidth - width)
                        y: miniSlider.topPadding + miniSlider.availableHeight / 2 - height / 2
                        width: 14; height: 14; radius: 7
                        color: root.audioAccent; border.color: "white"; border.width: 1
                    }
                }
                Text {
                    anchors { left: miniBtn.right; leftMargin: 12; bottom: parent.bottom; bottomMargin: 7 }
                    text: root._fmtTime(audioPlayer.position)
                    color: App.themeTextMuted; font.pixelSize: 10
                }
                Text {
                    anchors { right: parent.right; rightMargin: 14; bottom: parent.bottom; bottomMargin: 7 }
                    text: root._fmtTime(audioPlayer.duration)
                    color: App.themeTextMuted; font.pixelSize: 10
                }
            }
        }
    }

    function activateAnnotation(a) {
        if (a.type === 2 || a.uri.indexOf("http") === 0) {       // Link
            Viewer.openExternally(a.uri)
        } else if (a.type === 1) {                                // Video
            mediaLoader.uri = a.uri
            mediaLoader.active = true
        }
        // Audio (type 0) wird über PdfAudio/Audioleiste abgespielt (eigene Hotspots).
    }

    // ── Video-Overlay (Annotation) ─────────────────────────────────────────────
    Loader {
        id: mediaLoader
        anchors.fill: parent
        active: false
        property string uri: ""
        z: 7
        sourceComponent: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.92)
            VideoSurface { anchors.fill: parent; anchors.margins: 24; source: mediaLoader.uri }
            Rectangle {
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 16
                width: 40; height: 40; radius: 20; color: Qt.rgba(1,1,1,0.12)
                Text { anchors.centerIn: parent; text: "\u2715"; color: "white"; font.pixelSize: 18 }
                TapHandler { onTapped: mediaLoader.active = false }
            }
        }
    }

    // ── Seiten-Kontextmenü: PDF-Seiten extrahieren ─────────────────────────────
    //  „Seite extrahieren"           -> Namensdialog (Default „<Name> - Page N")
    //  „Mehrere Seiten extrahieren…" -> Auswahlraster (PdfPageSelectDialog);
    //  Ziel-Ordner = Ordner der Quelldatei (Controller), Ergebnis via Toast.
    ThemedMenu {
        id: pageCtxMenu
        property int ctxPage: 0

        //  ── Entscheidung über EINE nachverfolgte Änderung ─────────────────────
        //  Bezieht sich auf die AUSGEWÄHLTE Notiz und erscheint nur, solange die
        //  eine offene Änderung ist (`track` 1 = neu, 2 = gelöscht).
        readonly property int ctxTrack: root.editCtl.selectedId >= 0
            ? (root.editCtl.selectionRev,
               root.editCtl.boxInfo(root.editCtl.selectedId).track || 0)
            : 0
        MenuItem {
            visible: pageCtxMenu.ctxTrack > 0
            height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TrackAcceptOne")
            onTriggered: root.editCtl.acceptChange(root.editCtl.selectedId)
        }
        MenuItem {
            visible: pageCtxMenu.ctxTrack > 0
            height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "TrackRejectOne")
            onTriggered: root.editCtl.rejectChange(root.editCtl.selectedId)
        }
        MenuSeparator { visible: pageCtxMenu.ctxTrack > 0 }

        //  Alle Notizen/Zeichnungen dieser Datei verwerfen (Strg+Z holt sie
        //  zurück). Nur im Bearbeiten-Modus und nur, wenn es welche gibt.
        MenuItem {
            visible: root.editCtl.editMode && root.editCtl.boxCount > 0
            height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "CtxRemoveEdits")
            onTriggered: root.editCtl.discardAllAnnotations()
        }
        MenuSeparator { visible: root.editCtl.editMode && root.editCtl.boxCount > 0 }

        MenuItem {
            text: App.uiText(App.language, "CtxExtractPage")
            enabled: !PdfExtract.busy
            onTriggered: extractNameDlg.openFor(
                             PdfExtract.defaultSingleName(root.source,
                                                          pageCtxMenu.ctxPage),
                             false)
        }
        MenuItem {
            text: App.uiText(App.language, "CtxExtractPages")
            enabled: !PdfExtract.busy
            onTriggered: {
                extractSelectDlg.titleText   = App.uiText(App.language, "ExtractDialogTitle")
                extractSelectDlg.defaultName = PdfExtract.defaultMultiName(root.source)
                extractSelectDlg.openWith([{ path: root.source,
                                             pageCount: root.docReady ? root.doc.pageCount : 0 }])
            }
        }
        // ── Seitenverwaltung (nur im Editmodus; Strg+Z macht alles rückgängig) ─
        MenuSeparator { visible: root.editCtl.editMode }
        MenuItem {
            visible: root.editCtl.editMode
            height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "PdfRotatePageLeft")
            onTriggered: root._rotatePage(pageCtxMenu.ctxPage, -90)
        }
        MenuItem {
            visible: root.editCtl.editMode
            height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "PdfRotatePageRight")
            onTriggered: root._rotatePage(pageCtxMenu.ctxPage, 90)
        }
        MenuItem {
            visible: root.editCtl.editMode
            height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "PdfInsertPagesFrom")
            onTriggered: {
                root._insertAfter = pageCtxMenu.ctxPage
                insertFileDlg.open()
            }
        }
        MenuItem {
            visible: root.editCtl.editMode
            height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "PdfRemovePage")
            enabled: root.editCtl.viewPageCount > 1
            onTriggered: root.editCtl.removePage(pageCtxMenu.ctxPage)
        }
    }

    // ── Seiten aus einer FREMDEN PDF einfügen ──────────────────────────────────
    //  Erst die Datei wählen, dann IHRE Seiten im bekannten Auswahlraster
    //  (PdfPageSelectDialog, hier ohne Namensabfrage). Das Kopieren selbst läuft
    //  verlustfrei im Controller; die Rückmeldung kommt als Toast.
    property int _insertAfter: -1

    FileChooser {
        id: stampFileDlg
        title: App.uiText(App.language, "PdfStampFileTitle")
        fileMode: FileChooser.OpenFile
        nameFilters: ["Bilder (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"]
        onAccepted: root.insertStampImage(selectedFile)
    }

    FileChooser {
        id: insertFileDlg
        title: App.uiText(App.language, "PdfInsertPagesFileTitle")
        fileMode: FileChooser.OpenFile
        nameFilters: ["PDF (*.pdf)"]
        onAccepted: {
            var n = root.editCtl.probePageCount(selectedFile)
            if (n <= 0) {
                root._toast(App.uiText(App.language, "PdfPagesInsertFailedToast"))
                return
            }
            insertSelectDlg.titleText = App.uiText(App.language, "PdfInsertPagesDialogTitle")
            insertSelectDlg.openWith([{ path: String(selectedFile), pageCount: n }])
        }
    }

    PdfPageSelectDialog {
        id: insertSelectDlg
        anchors.fill: parent
        requireName: false
        askName: false                    // Einfügen braucht keinen Dateinamen
        confirmText: App.uiText(App.language, "PdfInsertPagesFrom")
        onExtractRequested: (items, name) => {
            if (!items || items.length === 0)
                return
            var pages = []
            for (var i = 0; i < items.length; i++)
                pages.push(items[i].page)
            root.editCtl.insertPagesFrom(items[0].path, pages, root._insertAfter)
        }
    }

    Connections {
        target: root.editCtl
        function onPagesInserted(count, errorText) {
            root._toast(count > 0
                        ? App.uiText(App.language, "PdfPagesInsertedToast").arg(count)
                        : App.uiText(App.language, "PdfPagesInsertFailedToast"))
        }
    }

    //  Drehen: Die Seitenmaße in Punkten kennt nur die Ansicht - der Controller
    //  dreht damit auch die Notizen der Seite mit (EIN Undo-Schritt).
    function _rotatePage(viewIndex, delta) {
        if (!root.docReady || viewIndex < 0)
            return
        root.commitEditing()
        var pts = root.doc.pagePointSize(viewIndex)
        root.editCtl.rotatePage(viewIndex, delta,
                                pts && pts.width  > 0 ? pts.width  : 595.276,
                                pts && pts.height > 0 ? pts.height : 841.890)
    }

    PdfExtractNameDialog {
        id: extractNameDlg
        onAccepted: (name) => {
            root._extractPending = true
            PdfExtract.extractSingle(root.source, pageCtxMenu.ctxPage, name)
        }
    }

    PdfPageSelectDialog {
        id: extractSelectDlg
        anchors.fill: parent
        requireName: false
        onExtractRequested: (items, name) => {
            root._extractPending = true
            // Leerer Ziel-Ordner -> extractOrdered nutzt den Ordner der Quelle
            // (das offene PDF); Reihenfolge = Auswahlreihenfolge/Original.
            PdfExtract.extractOrdered(items, "", name)
        }
    }

    //  PdfExtract ist ein Singleton (auch die Shell nutzt es global) -> nur die
    //  Surface, die den Auftrag GESTARTET hat, meldet das Ergebnis (Flag).
    property bool _extractPending: false
    Connections {
        target: PdfExtract
        function onExtractProgress(done, total) {
            if (root._extractPending)
                root._toast(App.uiText(App.language, "ExtractProgressToast")
                                .arg(done).arg(total))
        }
        function onExtractFinished(ok, targetPath, errorText) {
            if (!root._extractPending) return
            root._extractPending = false
            if (ok) {
                // Neue Datei sofort in der Galerie zeigen (deterministisch,
                // nicht nur über den Datei-Watcher - wie createEmptyFile).
                App.refreshCurrentFolder()
                root._toast(App.uiText(App.language, "ExtractOkToast")
                                .arg(String(targetPath).split("/").pop()))
            } else {
                root._toast(App.uiText(App.language, "ExtractFailToast"))
            }
        }
    }

    // ── Kompakter Toolbar-Button (wiederverwendbar, theme-konform) ────────────
    // ── Toast (unten mittig): Rückmeldungen des Editors - Speichern, Export-
    //    Fortschritt („Seite x/y") und -Ergebnis. Jede Meldung startet die
    //    Ausblend-Uhr neu; während des Exports wirkt das wie eine Live-Anzeige.
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

    component PdfToolButton: Rectangle {
        id: tb
        property string glyph: ""
        property string iconName: ""
        property string tip: ""
        property bool active: false
        signal activated()
        width: 30; height: 26; radius: 6
        opacity: enabled ? 1.0 : 0.35
        color: active ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
             : (tbHover.hovered && enabled
                ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07))
        border.color: active ? App.themeAccent : App.themeBorder; border.width: 1
        Text { anchors.centerIn: parent; text: tb.glyph; color: App.themeTextPrimary; font.pixelSize: 13
               visible: tb.iconName.length === 0 }
        DrawnIcon { anchors.centerIn: parent; name: tb.iconName; size: 16
                     visible: tb.iconName.length > 0 }
        HoverHandler { id: tbHover; enabled: tb.enabled }
        TapHandler { enabled: tb.enabled; onTapped: tb.activated() }
        ToolTip.text: tb.tip
        ToolTip.visible: tbHover.hovered && tb.tip.length > 0
    }
}
