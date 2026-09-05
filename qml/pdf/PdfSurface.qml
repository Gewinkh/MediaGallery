pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Pdf
import QtMultimedia
import MediaGallery 1.0
import "../common"
import "../viewer"

// PDF-Anzeige in reinem QML: eigener vertikaler ListView aus PdfPageImage statt PdfMultiPageView, für volle
// Kontrolle über Seitengeometrie und Overlay. RAM-Deckel über `pageCacheScreens` und `pdfPoolSize`.
Item {
    id: root

    property string source: ""
    property var    annotations: []
    // Nur die aktive Split-Kachel darf fensterweite Kuerzel feuern - sonst sind sie bei
    // mehreren offenen PDFs mehrdeutig und Qt feuert keines.
    property bool   paneActive: true

    property real   zoom: 1.0
    property string fitMode: "page"          // "page" = ganze Seite, "width" = Breite
    property int    currentPage: 0
    property real   panX: 0                   // horizontaler Schwenk-Offset (Zoom-Pan)
    property int    _savePage: 0             // Resize: die stabile Seite sichern …
    // Solange gesetzt, darf der Ready-Melder die Ansicht nicht an den Anfang setzen -
    // die Wiederherstellung bringt sie an die richtige Stelle.
    property bool   _reloading: false
    property int    _fileRev: 0
    property int    _stablePage: 0           // zuletzt SICHER erkannte Seite (Quelle für _savePage)
    property real   _saveFrac: 0             // … samt Innerseiten-Scrollanteil (0..1)
    property real   _stableFrac: 0           // zuletzt sicher erkannter Innerseiten-Anteil
    property bool   _resizing: false         // Resize-Phase aktiv -> updateCurrentPage gesperrt
    property bool   _restoring: false        // … und danach deterministisch wiederherstellen

    property real   topInset: 0
    property real   bottomInset: 0
    // Kopfraum in Hoehe der Ribbon-Leiste: sie liegt als Overlay ueber den Seiten und
    // verdeckte deren Oberkante, weil contentY nicht ueber den Listenanfang hinausgeht.
    // Der Seiten-Fit bleibt unangetastet - sonst skalierte jedes Ein-/Ausblenden neu.
    readonly property real ribbonInset:
        (root.editCtl.editMode && root.editPanelVisible && PdfEdit.panelOnTop) ? 62 : 0
    property bool   thumbsVisible: true
    property real   wheelPageFraction: 0.5   // Anteil der Viewporthöhe je Rad-Raststufe

    // Highlights leben im Root, nicht im wiederverwendeten Delegate; nur die Seite mit
    // selPage zeichnet selRects, damit Recycling nichts verliert.
    property int    selPage: -1              // Seite mit aktiver Auswahl (-1 = keine)
    property var    selRects: []             // normalisierte Highlight-Rechtecke {x,y,w,h}
    property bool   _selecting: false        // gerade am Ziehen?
    property var    _lastSel: null           // letzte Drag-Brüche (für Re-Query bei Ready)
    property bool   _pendingSelectAll: false // Strg+A vor Abschluss des Lazy-Loads -> nachziehen
    property int    _linkFromId: 0           // Reflow-Verkettung: Ausgangsbox, wartet auf Zielbox-Klick

    // audioClips = alle Clips des Dokuments; die Leiste zeigt nur die der aktuellen Seite.
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
    readonly property color audioAccent: App.audioAccentApple ? "#0A84FF" : App.themeAccent

    function _clipsOnPage(p) {
        var r = []
        for (var i = 0; i < audioClips.length; i++)
            if (audioClips[i].page === p) r.push(audioClips[i])
        return r
    }
    function _audioLabel(clip, idxOnPage) { return App.uiText(App.language, "PdfAudioItemLabel").arg(idxOnPage + 1) }

    // Eigene Controller je Kachel statt globaler Singletons: sonst teilten sich mehrere
    // PDF-Kacheln in der geteilten Ansicht Editmodus, Boxen, Auswahl und Audio.
    // editCtl ist exponiert, damit Kinder ueber surface.editCtl zugreifen.
    property PdfEditController editCtl: PdfEditController {}
    PdfTextController  { id: pdfTextCtl }
    PdfAudioController { id: pdfAudioCtl }
    property alias textCtl: pdfTextCtl

    // Modell, Undo, Sidecar und Export liegen im Controller; hier nur UI-Zustand und drei Brücken: `editCommitRev`
    // schließt offene Textbearbeitungen ab, `_snapCache` hält Zeilenrechtecke, `_autoEditId` startet eine neue Box.
    property bool editPanelVisible: false
    property bool snapEnabled: true          // Zeilenfang (Toolbar-Toggle)
    // Im Editmodus sind Notizen immer sichtbar; der Eintritt setzt auf true zurueck.
    property bool notesVisible: true
    property int  editCommitRev: 0
    property var  _snapCache: ({})
    property int  _autoEditId: -1
    // Hebt das Seiten-Delegate per z ueber seine Nachbarn, damit eine gezogene Notiz
    // sichtbar ueber die Luecke gleitet statt von der Nachbarseite zugemalt zu werden.
    property int  editDragPage: -1
    // Dann gehoeren Tasten - auch + und - - dem Text und nicht den Ansichts-Kuerzeln.
    readonly property bool caretActive: editCtl.editMode && editCtl.tool === 7
                                        && editCtl.caretPage >= 0

    function commitEditing() { editCommitRev++ }

    // Uebersetzt eine ueber den Seitenrand gezogene Oberkante in Zielseite und lokales y.
    // Kriterium ist die Box-Mitte; laeuft iterativ, also auch ueber mehrere Seiten.
    function resolveCrossPage(page, yPt, hPt, scale) {
        var p = Math.max(0, Math.min(page, root.pageCount - 1))
        var y = yPt
        if (!root.docReady || scale <= 0)
            return { page: p, y: Math.max(0, y) }
        var gapPt = (pages.spacing + 4) / scale
        var ph = root.doc.pagePointSize(p).height
        while (p < root.pageCount - 1 && ph > 0 && y + hPt / 2 > ph) {
            y -= ph + gapPt
            p++
            ph = root.doc.pagePointSize(p).height
        }
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

    // Toleranz: max aus 7 pt und 75 % der Zeilenhoehe. Ohne Textebene unveraendert.
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

    function _snapLines(page) {
        var c = _snapCache[page]
        if (c !== undefined)
            return c
        var l = pdfTextCtl.ready ? pdfTextCtl.textLineRects(page) : []
        if (pdfTextCtl.ready)
            _snapCache[page] = l
        return l
    }

    // Ziel ist immer eine neue Kopie neben dem Original; die Notizen bleiben ueber das
    // Sidecar reversibel.
    function startPdfExport() {
        if (root.editCtl.busy || !root.docReady)
            return
        root.commitEditing()
        root.editCtl.exportPdf()
    }
    // Faellt im Controller automatisch auf den Raster-Export zurueck.
    function startContentExport() {
        if (root.editCtl.busy || !root.docReady)
            return
        root.commitEditing()
        root.editCtl.exportContentEdited()
    }

    // Ein Export-Weg fuer die Oberflaeche; welcher laeuft, entscheidet
    // PdfEdit.exportLossless. Zwei Knoepfe erzwangen die Wahl bei jedem Export,
    // obwohl sie eine Grundsatzentscheidung ist.
    function startExport() {
        if (PdfEdit.exportLossless) root.startContentExport()
        else                        root.startPdfExport()
    }

    // Scroll-Vorhalt in Viewporthoehen je Richtung. Hoeher = mehr RAM UND mehr
    // konkurrierende Renderings: PDFium serialisiert alle render()-Aufrufe einer
    // Dokument-Instanz ueber einen Mutex. RAM ~ (1 + 2*pageCacheScreens) Seitenbitmaps.
    property real   pageCacheScreens: 1.5
    property int    pdfPoolSize: 3

    // Gestaffeltes Laden: zuerst nur die sichtbare Seite rendern, Vorhalte-Puffer und
    // Thumbnail-Leiste erst nach kurzer Verzoegerung freischalten. Sonst konkurrieren
    // ~8 Thumbnails und mehrere Vorabseiten um denselben PDFium-Render-Mutex.
    property bool   _warm: false                 // false -> nur sichtbare Seite rendern
    property int    warmupDelayMs: 160           // Verzoegerung bis Puffer+Thumbnails

    // docId des aktiven PDFs im RAM-Thumbnail-Provider; baut die image://pdfthumb-URLs.
    property int    _thumbDocId: 0

    function _thumbSource() {
        var p = root.editCtl.renderSourcePath()
        return (p && p.length > 0) ? p : root.source
    }

    function _beginWarmup() {
        root._warm = false
        warmupTimer.restart()
    }
    Timer {
        id: warmupTimer
        interval: root.warmupDelayMs
        repeat: false
        onTriggered: {
            // Vorrendern anstossen, BEVOR die Delegates ueber _warm entstehen - sie binden dann
            // sofort die richtige docId. Quelle ist die gebackene Arbeitsdatei, sonst zeigte die
            // Leiste die Originalreihenfolge.
            if (root.source.length > 0)
                root._thumbDocId = PdfThumbs.ensureDocument(root._thumbSource(),
                                                            root.currentPage)
            root._warm = true
        }
    }

    property var    doc: null                // aktuell aktives PdfDocument
    readonly property bool docReady: !!doc && doc.status === PdfDocument.Ready
    readonly property int  pageCount: docReady ? doc.pageCount : 0

    property var    _pool: ({})              // localPath -> PdfDocument
    property var    _poolOrder: []           // LRU-Reihenfolge (alt -> neu)

    Component { id: _pdfDocFactory; PdfDocument {} }

    function _activateDoc(localPath) {
        var key = root._localPath(localPath)
        var url = localPath.indexOf("file:") === 0 ? localPath : "file://" + localPath
        // Nach einer Seitenoperation liegt die neue Datei am selben Pfad; Qt erkennt am
        // gleichen URL nichts Neues. Der Zaehler macht die URL eindeutig, toLocalFile()
        // verwirft ihn wieder.
        if (root._fileRev > 0) url += "?mgrev=" + root._fileRev
        var d = root._pool[key]
        if (!d) {
            d = _pdfDocFactory.createObject(root)
            if (!d) return
            d.source = url
            root._pool[key] = d
        }
        var i = root._poolOrder.indexOf(key)
        if (i >= 0) root._poolOrder.splice(i, 1)
        root._poolOrder.push(key)
        root.doc = d
        root._evictPool()
    }

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
        // Nur Overlays stoppen - das Render-Dokument bleibt im Pool warm.
        mediaLoader.active = false
        _saveActivePos()
        // Player-Instanz restlos zerstoeren: eine lebende Instanz behielte Handle und
        // Alt-Zustand, obwohl releaseDocument() die Temp-WAVs loescht.
        audioPlayer.reset()
        root.activeClipId = -1
        root._activeTitle = ""
        annotations = []
        // Auswahl-Dokument freigeben; es wird beim naechsten Markieren lazy neu geladen.
        clearSelection()
        pdfTextCtl.releaseDocument()
        // Ungespeicherte Overlay-Aenderungen ins Sidecar sichern - kein stiller Verlust.
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
            // Player-Instanz zerstoeren, BEVOR prepare() die Temp-WAVs des alten Dokuments
            // loescht - sonst haelt der Player noch ein offenes Handle auf die letzte WAV.
            audioPlayer.reset()
            root.audioClips = []
            root.documentHasAudio = false
            root.activeClipId = -1
            root._activeTitle = ""
            root._audioMeta = ({})
            root._audioPos = ({})
            root.audioPanelVisible = false
            pdfAudioCtl.prepare(root.source)
            // setDocument() sichert die Aenderungen des vorherigen PDFs und laedt das neue.
            root._snapCache = ({})
            root._autoEditId = -1
            root.editPanelVisible = false
            root.editCtl.setDocument(root.source)
            _activateDoc(root.source)
            // Ein bereits warmes Dokument feuert kein statusChanged.
            if (root.docReady) {
                pages.positionViewAtBeginning(); root.currentPage = 0
                _beginWarmup()
                _ensurePlanInit()           // Aufgabe 3: Seiten-Plan initialisieren
            }
            // Nicht blockierend: die Badges erscheinen, sobald pdfAnnotationsReady feuert.
            Viewer.requestPdfAnnotations(root.source)
        } else {
            release()
        }
    }

    Connections {
        target: root.doc
        function onStatusChanged() {
            if (root.doc && root.doc.status === PdfDocument.Ready) {
                if (root._reloading) {
                    // Neuladen nach einer Seitenoperation - restorePageTimer bringt die Stelle zurueck.
                    root._beginWarmup()
                    root._ensurePlanInit()
                    return
                }
                pages.positionViewAtBeginning()
                root.currentPage = 0
                root._beginWarmup()
                root._ensurePlanInit()      // Aufgabe 3: Seiten-Plan initialisieren
            }
        }
    }

    function _localPath(s) {
        return s.indexOf("file://") === 0 ? s.substring(7) : s
    }

    // Nur solange noch kein Plan steht; nach dem Reload ist er gesetzt.
    function _ensurePlanInit() {
        if (root.docReady && root.editCtl.viewPageCount === 0)
            root.editCtl.setSourcePageCount(root.doc.pageCount)
    }

    // Nach einer Plan-Aenderung rendert die Ansicht die gebackene Arbeitsdatei. Die
    // Datei kann sich in-place geaendert haben, also gepooltes Dokument verwerfen.
    function _reloadRenderDoc() {
        var p = root.editCtl.renderSourcePath()
        if (!p || p.length === 0) return

        // Zielseite der Operation anspringen; ohne das sprang die Ansicht auf Seite 1.
        var focus = root.editCtl.takeStructureFocus()
        root._savePage = (focus >= 0) ? focus : root._stablePage
        root._saveFrac = (focus >= 0) ? 0     : root._stableFrac
        root._reloading = true
        root._resizing  = true              // sperrt updateCurrentPage bis zum Restore

        var key = root._localPath(p)
        var old = root._pool[key]
        if (old) {
            delete root._pool[key]
            var i = root._poolOrder.indexOf(key)
            if (i >= 0) root._poolOrder.splice(i, 1)
        }
        // Delegates jetzt wegwerfen: die Bild-URL aendert sich nicht, ein bestehendes
        // PdfPageImage laese die Datei sonst nicht neu.
        root._fileRev++                     // neue URL -> Qt liest die Datei neu
        _activateDoc(p)                     // frisch laden (Pool-Eintrag entfernt)
        if (old && old !== root.doc) { old.source = ""; old.destroy() }
        restorePageTimer.restart()
    }

    // Nur uebernehmen, wenn das Ergebnis zum aktuell angezeigten PDF gehoert.
    Connections {
        target: Viewer
        function onPdfAnnotationsReady(path, anns) {
            if (root._localPath(path) === root._localPath(root.source))
                root.annotations = anns
        }
    }

    // Auswahl-Dokument fertig geladen - eine laufende Drag-Auswahl nachholen.
    Connections {
        target: pdfTextCtl
        function onReadyChanged() {
            if (!pdfTextCtl.ready)
                return
            // Zeilenfang-Cache neu aufbauen; der alte koennte leere Fallback-Listen enthalten.
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
        // Der Suchlauf laeuft stueckweise; beim ersten Treffer gleich dorthin springen.
        function onSearchChanged() {
            root._searchRev++
            if (pdfTextCtl.searchCount > 0 && root.searchIndex < 0)
                root.goToHit(0)
            else if (pdfTextCtl.searchCount === 0)
                root.searchIndex = -1
        }
    }

    Connections {
        target: root.editCtl
        // Die Datei hat sich geaendert, nicht nur die Anzeige: der Textcontroller haelt
        // sein eigenes Dokument und muss neu lesen.
        function onDocumentRewritten() { root._reloadRenderDoc(); pdfTextCtl.reload() }
        // Bewusst nicht an documentRewritten: ein Neubau der Textebene laesst die Struktur
        // unberuehrt und wuerde die Vorschauen bei jedem Tippen verwerfen.
        function onPageStructureChanged() {
            if (root._warm && root.source.length > 0)
                root._thumbDocId = PdfThumbs.refreshDocument(root._thumbSource(),
                                                             root.currentPage)
        }
        function onEditModeChanged() {
            if (root.editCtl.editMode) {
                // Die Textauswahl bleibt beim Betreten des Editiermodus erhalten; das
                // Auswahl-Dokument wird lazy geladen (Zeilenrechtecke fuer den Zeilenfang).
                root.notesVisible = true
                if (root.source.length > 0)
                    pdfTextCtl.prepare(root.source)
                // Word-Modus: die obere Leiste erscheint automatisch mit dem Editmodus.
                if (PdfEdit.panelOnTop)
                    root.editPanelVisible = true
            } else {
                root.editPanelVisible = false
            }
        }
        // Der Lauf dauert je Seite rund eine Sekunde - ohne Rueckmeldung saehe es aus,
        // als geschehe nichts.
        function onSearchableProgress(done, total) {
            if (done < total)
                root._toast(App.uiText(App.language, "PdfSearchableRunning")
                                .arg(done + 1).arg(total))
        }
        function onSearchableFinished(ok, pages, words, skipped, errorText) {
            if (!ok) {
                // Drei Faelle: schon durchsuchbar ist kein Fehlschlag, sondern nichts zu tun.
                root._toast(errorText === "alreadytext"
                            ? App.uiText(App.language, "PdfSearchableAlreadyToast")
                            : errorText === "notext"
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
        function onFormSaved(ok, targetPath, errorText, flattened) {
            if (ok && flattened === true)
                // Die Kopie traegt die geaenderte Seitenfolge, dafuer sind ihre Felder
                // festgeschrieben - das muss gesagt werden.
                root._toast(App.uiText(App.language, "PdfFormSavedFlattenedToast")
                                .arg(targetPath))
            else if (ok)
                root._toast(App.uiText(App.language, "PdfFormSavedToast").arg(targetPath))
            else
                root._toast(App.uiText(App.language, "PdfFormSaveFailedToast")
                                .arg(errorText.length > 0 ? errorText : "?"))
        }
        function onContentEditFellBack() {
            root._toast(App.uiText(App.language, "PdfContentFallbackToast"))
        }
        // Text-Eigenschaften oeffnen automatisch bei Auswahl oder Erstellen. Abwaehlen
        // laesst das Panel offen; es schliesst ueber sein eigenes Kreuz.
        function onSelectedIdChanged() {
            if (root.editCtl.editMode && root.editCtl.selectedId >= 0) {
                root.editPanelVisible = true
                if (!PdfEdit.panelOnTop)
                    root.audioPanelVisible = false
            }
            // Wartet der Link-Modus auf ein Ziel: beide verketten und den Modus verlassen.
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
        // Die Aenderung liess sich nicht in den Content-Stream schreiben oder der
        // Absatz-Umbruch konnte den Rest nicht unterbringen - beides wird gesagt,
        // statt es still zu verwerfen.
        function onReflowOverflow() {
            root._toast(App.uiText(App.language, "PdfReflowOverflow"))
        }
        function onTextEditFailed(reason) {
            root._toast(App.uiText(App.language, "PdfEditTextOpFailed")
                            .arg(reason.length > 0 ? reason : "?"))
        }
        // Zeichen-Layout der Seite ist da; ist sie nicht bearbeitbar, wird das gesagt.
        function onCaretReadyChanged() {
            if (root.editCtl.tool !== 7 || root.editCtl.caretReady)
                return
            // Eingefuegte oder gedrehte Seite: der Controller lehnt das Caret dort ab, weil
            // die Ops die ungedrehte Quellseite adressieren.
            if (root.editCtl.caretError === "pagenotext") {
                root._toast(App.uiText(App.language, "PdfCaretPageNotEditable"))
                return
            }
            // Seite gelesen, aber ohne Text - kein Fehler, aber ein Klick ohne Meldung
            // wirkt kaputt.
            if (root.editCtl.caretError === "pagenotext_empty") {
                root._toast(App.uiText(App.language, "PdfCaretPageNoText"))
                return
            }
            // Sonst den wirklichen Grund nennen; ein Pauschalsatz macht daraus ein Raetsel.
            if (root.editCtl.caretPage >= 0 && root.editCtl.caretError.length > 0)
                root._toast(App.uiText(App.language, "PdfEditCaretUnavailable")
                                .arg(root.editCtl.caretError))
        }
    }

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
        // Resume-Position vor stop() sichern: stop() setzt position=0 und loeste
        // onPositionChanged(0) aus, das _audioPos transient ueberschriebe.
        root._pendingSeekMs = root._audioPos[root.activeClipId] || 0
        root._pendingPlay = true
        // Frische MediaPlayer-Instanz - kein wiederverwendeter Demuxer- oder Sink-Zustand.
        audioPlayer.loadFresh(url)
        // FFmpeg-Backend unter Linux verwirft das erste play() bei LoadedMedia auf jedem
        // zweiten Quellenwechsel. playRetry ruft play(), bis wirklich Playing erreicht ist.
        playRetry.tries = 0
        playRetry.start()
    }

    function _saveActivePos() {
        if (root.activeClipId >= 0) {
            var m = root._audioPos; m[root.activeClipId] = audioPlayer.position; root._audioPos = m
            root._audioRev++
        }
    }

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

    // Mono-Play: eindeutiges Token dieser Wiedergabestelle. Startet eine andere Stelle,
    // pausiert die Wiedergabe hier - Position bleibt erhalten.
    readonly property string _playToken: "pdfaudio-" + root

    Connections {
        target: App
        function onPlaybackStarted(token) {
            if (token === root._playToken) return
            if (audioPlayer.playbackState === MediaPlayer.PlayingState)
                audioPlayer.pause()
            // Wartet hier noch ein asynchroner Play, hat der fremde Start gewonnen - sonst
            // stiehlt der verspaetete Retry die Wiedergabe zurueck.
            root._pendingPlay = false
        }
    }

    // Audio-Fassade: stabiler Zugriffspunkt fuer die UI, waehrend die MediaPlayer-
    // Instanz je Wiedergabe neu erzeugt wird. Das FFmpeg-Backend behaelt nach einem
    // Quellenwechsel auf derselben Instanz Pipeline-Zustand und startet keinen Ton.
    Item {
        id: audioPlayer
        visible: false

        property var _inst: null

        // Persistenter Sink, einmal geoeffnet und wiederverwendet. Ein eigener AudioOutput
        // je Player liess das Audiogeraet durch das schnelle Oeffnen/Schliessen bei jeder
        // zweiten Wiedergabe stumm bleiben.
        AudioOutput { id: sharedAudioOut }

        readonly property int  playbackState: _inst ? _inst.playbackState : MediaPlayer.StoppedState
        readonly property int  mediaStatus:   _inst ? _inst.mediaStatus   : MediaPlayer.NoMedia
        readonly property real position:      _inst ? _inst.position      : 0
        readonly property real duration:      _inst ? _inst.duration      : 0

        function play()   { if (_inst) _inst.play() }
        function pause()  { if (_inst) _inst.pause() }
        function stop()   { if (_inst) _inst.stop() }
        function seek(ms) { if (_inst) _inst.position = ms }

        // Neue Quelle in eine frische Instanz laden, die den persistenten Sink bespielt.
        function loadFresh(url) {
            reset()
            _inst = playerComponent.createObject(audioPlayer,
                                                 { source: url, audioOutput: sharedAudioOut })
        }

        // Erst Fassade abkoppeln, dann stoppen, Sink loesen (er darf nicht mitsterben),
        // Quelle leeren (Handle sofort frei - destroy() ist verzoegert), dann zerstoeren.
        function reset() {
            var old = _inst
            _inst = null
            if (old) {
                // Zuerst abkoppeln: stop() einer spielenden Instanz laeuft ueber LoadedMedia, deren
                // Handler _pendingPlay konsumierte und die alte Quelle erneut startete - der neue
                // Clip wurde dann nie gestartet.
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
                // Schaltet alle Handler der alten Instanz ab, bevor sie gestoppt und zerstoert wird.
                property bool detached: false
                // Erstversuch als schneller Pfad, die Absicherung uebernimmt playRetry. Nie auf 0
                // suchen - ein redundanter Seek liess die erste Wiedergabe haengen; ein echter
                // Resume-Sprung erfolgt erst, nachdem die Wiedergabe laeuft.
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
                        // Mono-Play: Start melden, andere Wiedergabestellen pausieren sich daraufhin.
                        App.announcePlayback(root._playToken)
                    }
                }
                onErrorOccurred: function(err, errStr) {
                    if (err !== MediaPlayer.NoError) console.log("MediaGallery Audio-Fehler:", err, errStr)
                }
                // Nur ins einfache Objekt schreiben, kein Reassign - keine Binding-Last je Tick.
                onPositionChanged: if (!detached && root.activeClipId >= 0) root._audioPos[root.activeClipId] = position
            }
        }
    }

    // Sicherheitsnetz, falls das erste play() verworfen wird. Stoppt bei Playing oder
    // nach einer Grenze, damit InvalidMedia keinen Endlos-Retry ausloest.
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

    // WindowShortcut-Kontext. Copy ist nur scharf bei markiertem Text. Explizite
    // Sequenzen statt StandardKey, damit keine Zweitbindung sie mehrdeutig macht.
    Shortcut {
        sequence: "Ctrl+C"
        enabled: root.paneActive && root.docReady && pdfTextCtl.selectedText.length > 0
        onActivated: pdfTextCtl.copyToClipboard()
    }
    // Wirkt in beiden Modi; der Eintritt in den Editmodus erzwingt notesVisible=true.
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
    // Greift vor dem Textebenen-Strg+C; beide schliessen sich ueber enabled aus.
    Shortcut {
        sequence: "Ctrl+C"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode
                 && root.editCtl.selectedId >= 0 && !root.editCtl.textEditing
        onActivated: { root.commitEditing(); root.editCtl.copySelected() }
    }
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
    // Gesperrt waehrend einer Inline-Textbearbeitung - dort gehoert Entf dem TextEdit.
    Shortcut {
        sequence: "Delete"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode
                 && root.editCtl.selectedId >= 0 && !root.editCtl.textEditing
        onActivated: {
            root.commitEditing()
            root.editCtl.removeBox(root.editCtl.selectedId)
        }
    }
    // Gesperrt waehrend einer Inline-Textbearbeitung - dort gehoert Strg+Z dem TextEdit.
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: root.paneActive && root.docReady && root.editCtl.editMode && !root.editCtl.textEditing
        onActivated: root.editCtl.undo()
    }
    // Strg+Y als zweite Folge: es gibt Sitzungen, in denen das Plattform-Thema sie
    // nicht liefert und sie an nichts gebunden waere.
    Shortcut {
        sequences: [ "Ctrl+Shift+Z", "Ctrl+Y" ]
        enabled: root.paneActive && root.docReady && root.editCtl.editMode && !root.editCtl.textEditing
        onActivated: root.editCtl.redo()
    }
    // +/- zoomt ohne Modifikator; die Kachelgroesse der Galerie liegt auf Strg++/-.
    // = liegt auf derselben Taste wie + und kommt als Zweit-Sequenz.
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

    // Ein Shortcut kann das nicht leisten: hier wird beliebiger Text getippt, nicht
    // eine feste Folge abgefangen. Der Empfaenger nimmt keine Flaeche ein und faengt
    // daher keine Mausereignisse ab.
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
            if (e.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                return
            // Nur druckbare Zeichen einfuegen, Rest durchlassen.
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
        // Stabile Seite nur ausserhalb von Resize/Restore fortschreiben.
        if (!root._resizing && !root._restoring)
            root._stablePage = root.currentPage
    }

    // Der Punkt in der Viewport-Mitte bleibt beim Zoomen stehen, verankert als Seite plus Innerseiten-Anteil. Eine
    // reine contentY-Verhältnisrechnung war relayout-anfällig: der Zoom ändert alle Delegate-Höhen.
    function setZoom(z) {
        var nz = Math.max(0.25, Math.min(4.0, z))
        if (Math.abs(nz - root.zoom) < 0.0001) return
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

    // Anzeigebreite der aktuellen Seite - spiegelt die Delegate-Formel.
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
    // Schwenken kann, wer ueber die Seite hinaus hinein- oder hinausgezoomt hat.
    function canPan() {
        return (_curPageW() - pages.width > 1) || (pages.contentHeight - pages.height > 1)
    }
    function panBy(dx, dy) {
        root.panX += dx
        root.clampPanX()
        pages.contentY = root.clampContentY(pages.contentY - dy)
    }
    // Liegt der Punkt ueber einer erkannten Textzeile? Entscheidet Pan gegen Markieren.
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
    // Nach `positionViewAtIndex` kann der Ursprung verschoben sein (`originY != 0`), weil noch nicht instanziierte
    // Seiten geschätzt werden - auf [0 .. cH-h] geklemmt blockierte das Hochscrollen.
    function minContentY() { return pages.originY }
    function maxContentY() {
        return pages.originY + Math.max(0, pages.contentHeight - pages.height)
    }
    function clampContentY(y) {
        return Math.max(minContentY(), Math.min(y, maxContentY()))
    }
    function updateCurrentPage() {
        // Waehrend Resize/Wiederherstellung nicht ueberschreiben - sonst driftet die Seite.
        if (root._restoring || root._resizing) return
        if (pages.count <= 0) { root.currentPage = 0; return }
        var idx = pages.indexAt(pages.width / 2, pages.contentY + pages.height / 2)
        if (idx >= 0) root.currentPage = idx
        // Innerseiten-Anteil fortschreiben; das Seitenlayout skaliert proportional, der
        // Anteil bleibt also ueber einen Resize gueltig.
        var it = pages.itemAtIndex(root._stablePage)
        if (it && it.height > 0)
            root._stableFrac = Math.max(0, Math.min(1,
                (pages.contentY - it.y) / it.height))
    }
    // Stabile Seite samt Innerseiten-Anteil ueber einen Resize retten. _resizing sperrt
    // updateCurrentPage fuer die ganze Phase; der Timer feuert erst am Ende.
    function _preservePageAcrossResize() {
        if (!root.docReady) return
        if (!root._resizing) {
            root._resizing = true
            root._savePage = root._stablePage     // garantiert unverfälschte Seite
            root._saveFrac = root._stableFrac     // … samt Position in der Seite
        }
        restorePageTimer.restart()
    }
    // Solange die Kachel versteckt ist, ist die ListView-Geometrie nicht verlaesslich -
    // die Wiederherstellung wartet auf das Wieder-Sichtbarwerden.
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
            // Innerseiten-Anteil auf die neue Seitenhoehe anwenden und origin-bewusst klemmen.
            var it = pages.itemAtIndex(root._savePage)
            if (it && it.height > 0)
                pages.contentY = root.clampContentY(it.y + root._saveFrac * it.height)
            root.currentPage = root._savePage
            root._stablePage = root._savePage
            root._stableFrac = root._saveFrac
            root.clampPanX()
            root._restoring = false
            root._resizing = false
            root._reloading = false
        }
    }

    // Laedt das Auswahl-Dokument lazy. Bei grossen PDFs ist es womoeglich erst waehrend
    // des Ziehens fertig - onReadyChanged holt die Auswahl dann nach.
    function beginSelection(page) {
        root._selecting = true
        root.selPage = page
        root.selRects = []
        root._lastSel = null
        root._pendingSelectAll = false
        pdfTextCtl.clearSelection()
        pdfTextCtl.prepare(root.source)
    }
    // Der naechste Klick auf eine andere Textbox verkettet beide.
    function startLink(fromId) {
        if (fromId < 0) return
        root._linkFromId = fromId
        root._toast(App.uiText(App.language, "PdfChainPick"))
    }
    // Der Controller haelt Begriff und Treffer; hier nur, was die Anzeige braucht.
    property bool searchVisible: false
    property int  searchIndex: -1
    property int  _searchRev: 0

    // Weg ueber den Dateidialog; der Knopf bietet zuerst die Bilder im Ordner an.
    function pickStampImage() {
        if (!root.docReady || !root.editCtl.editMode) return
        stampFileDlg.open()
    }

    // Ein Weg fuer Dateidialog und Ordner-Waehler. Breite = ein Drittel der Seite.
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
    function goToHit(i) {
        const n = pdfTextCtl.searchCount
        if (n <= 0) { root.searchIndex = -1; return }
        root.searchIndex = ((i % n) + n) % n          // umlaufend
        const h = pdfTextCtl.searchHit(root.searchIndex)
        if (h.page !== undefined) root.goToPage(h.page)
    }

    function updateSelection(page, a0, b0, a1, b1) {
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
    // Die Zeilenrechtecke der Textauswahl sind genau das, was eine Markierung braucht.
    // Ohne Textebene gibt es keine Zeilen; dann markiert fallback den Zugbereich.
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

    // Bewusst auf Root-Ebene und ueber die Panel-Schaltflaeche gerufen statt ueber
    // onToolChanged: war das Werkzeug schon aktiv, feuert toolChanged nicht.
    function replaceSelectionNow() {
        if (!root.docReady || !root.editCtl.editMode) return false
        const page = root.selPage
        if (page < 0 || root.selRects.length === 0) return false
        const pts = root.doc.pagePointSize(page)
        if (!pts || pts.width <= 0 || pts.height <= 0) return false
        // Zeilen-Rechtecke aufziehen, nicht die Zugkoordinaten - s. _selectionUnionPt.
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

    // Union der Auswahl-Rechtecke in PDF-Punkten. Die Zugkoordinaten beschreiben eine
    // Bewegung entlang einer Zeile und haben Hoehe 0; die Sonde fand damit nichts und
    // der Text blieb beim Export unter dem Balken stehen.
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

    // Wie beim Ersetzen, endet aber in endRedactDraw: die Flaeche schnappt auf die
    // erkannten Zeilen ein, deren Text als origText mitwandert - Gedecktes und
    // Entferntes bleiben deckungsgleich.
    function redactSelectionNow() {
        if (!root.docReady || !root.editCtl.editMode) return false
        const page = root.selPage
        if (page < 0 || root.selRects.length === 0) return false
        const pts = root.doc.pagePointSize(page)
        if (!pts || pts.width <= 0 || pts.height <= 0) return false
        // Ueber die Auswahl-Rechtecke mit echter Zeilenhoehe - s. _selectionUnionPt.
        const u = root._selectionUnionPt(page)
        if (!u) return false
        const id = root.editCtl.beginDraw(5, page, u.x, u.y)
        if (id < 0) return false
        root.editCtl.updateDraw(id, u.x + u.w, u.y + u.h)

        // Gedeckt wird genau die Auswahl, nicht die ganze Zeile: ein Einschnappen auf die
        // Zeilen-Bounds legte den Balken bei drei markierten Woertern ueber die Zeile.
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

    // Liegt eine Auswahl vor, wird sofort geschwaerzt. Sonst der Ziehweg, aber mit
    // Rueckmeldung, wenn die Seite keine Textebene hat - dort startet die Geste nicht.
    function startRedact() {
        if (!root.docReady || !root.editCtl.editMode) return
        if (root.selPage >= 0 && root.selRects.length > 0) {
            if (!root.redactSelectionNow())
                root._toast(App.uiText(App.language, "PdfRedactNoTextToast"))
            return
        }
        // Erst wenn die lazy geladene Textebene da ist, laesst sich das sagen.
        pdfTextCtl.prepare(root.source)
        if (pdfTextCtl.ready
                && pdfTextCtl.textLineRects(root.currentPage).length === 0)
            root._toast(App.uiText(App.language, "PdfRedactNoTextToast"))
    }

    // Einmal je Kachel-Sitzung als Hinweis, danach nie wieder.
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
            root._pendingSelectAll = true
        }
    }

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
        visible: !!root.doc && root.doc.status === PdfDocument.Error
        text: App.uiText(App.language, "PdfLoadError")
        color: "#ff8a80"; font.pixelSize: 14
        z: 5
    }

    Rectangle {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset }
        // 42 px wie im DOCX-Editor - die drei Editor-Leisten sind bewusst gleich hoch.
        height: 42
        color: App.themeToolbarBg
        visible: root.docReady
        z: 6
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: App.themeBorder }

        // Die linke Werkzeuggruppe endet vor der rechten; bei schmaler Kachel lagen die
        // Knoepfe sonst uebereinander. Passt die Gruppe nicht, schwenkt das Mausrad sie.
        ScrollableBar {
            id: toolbarLeftClip
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: toolbarRight.left; anchors.rightMargin: 8
            anchors.top: parent.top; anchors.bottom: parent.bottom
            spacing: 6

            PdfToolButton {
                // Pfeile mit Schaft statt Chevrons: die Chevrons daneben tragen die
                // Seiten-Navigation, und zwei Bedeutungen duerfen nicht dieselbe Form haben.
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
                // commitEditing() vor dem Umschalten, damit eine offene Text-Session sauber
                // abschliesst, bevor der Modus und damit die Auswahl faellt.
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
            // Nur bei einem Dokument mit ausfuellbaren Feldern; schreibt die Werte in eine
            // Kopie. Unabhaengig vom Editmodus - Formulare fuellt man beim Lesen aus.
            PdfToolButton {
                iconName: "save"
                visible: root.editCtl.hasForm
                enabled: root.editCtl.formDirty && !root.editCtl.busy
                tip: App.uiText(App.language, "PdfFormSaveTip")
                onActivated: root.editCtl.saveFormValues()
            }
            // Der Gear-Knopf entfaellt: das Panel oeffnet automatisch bei Auswahl.
        }   // Ende toolbarLeftClip (ScrollableBar)

        // Auch die rechte Gruppe ist blaetterbar und auf die halbe Leiste gedeckelt: sie
        // wuchs nach links, bis die linke Gruppe vollstaendig verdeckt war.
        ScrollableBar {
            id: toolbarRight
            anchors.right: parent.right; anchors.rightMargin: 12
            anchors.top: parent.top; anchors.bottom: parent.bottom
            width: Math.min(contentWidth, toolbar.width * 0.55)
            spacing: 6
            // Nur im Editmodus sinnvoll; Zustand und Schema liegen global im Translit-Singleton.
            TranslitButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.editCtl.editMode
            }
            Item { width: root.editCtl.editMode ? 4 : 0; height: 1 }
            // Nur sichtbar, wenn das PDF Audio enthaelt.
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
        // Klicks duerfen nicht auf die Seite darunter durchfallen.
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
                // Der Controller sucht stueckweise - das haelt die Oberflaeche fluessig.
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

    Item {
        id: contentArea
        anchors {
            left: parent.left; right: parent.right
            top: toolbar.visible ? toolbar.bottom : parent.top
            bottom: parent.bottom
            // Im Seiten-Fit haengt die Skalierung von der Viewport-Hoehe ab, deshalb wird das
            // Inset dort ignoriert: sonst skalierte jedes Ein-/Ausblenden der Navigation die
            // Seite neu. Im Breite-Modus bleibt es erhalten - dort aendert es nichts.
            bottomMargin: root.fitMode === "page" ? 0 : root.bottomInset
        }

        ListView {
            id: pages
            anchors.fill: parent
            clip: true
            // Linksziehen markiert immer, deshalb ist das eigene Dragging der Liste aus.
            // Mausrad und ScrollBar setzen contentY direkt und funktionieren unveraendert.
            interactive: false
            model: root.docReady ? root.doc.pageCount : 0
            spacing: 10
            // Kopfraum als header statt topMargin: so bleibt der gueltige contentY-Bereich
            // [0 .. contentHeight-height] und die vorhandene Scroll-Logik gilt unveraendert.
            header: Item { width: 1; height: root.ribbonInset }
            // `cache:false` gibt außerhalb des Puffers wieder frei; bis der Warmlauf greift, ist der Puffer fast 0, damit
            // nur die sichtbare Seite rendert. `max(0,..)`: vor dem ersten Layout ist die Höhe negativ.
            cacheBuffer: Math.max(0, Math.round(pages.height *
                                    (root._warm ? root.pageCacheScreens : 0.1)))
            boundsBehavior: Flickable.StopAtBounds
            onContentYChanged: root.updateCurrentPage()
            onCountChanged: root.updateCurrentPage()
            // Aendert sich beim Fenster-Resize und beim Aufteilen der Split-Ansicht.
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
                // Waehrend eines Notiz-Drags ueber die Geschwister-Delegates heben.
                z: index === root.editDragPage ? 1 : 0

                readonly property size pts: root.doc.pagePointSize(index)
                // Skalierung unabhaengig vom Overlay-Panel - Umschalten loest kein Neu-Rendern aus.
                readonly property real wFit: pts.width  > 0 ? (pages.width  - 24) / pts.width  : 1.0
                readonly property real hFit: pts.height > 0 ? (pages.height - 24) / pts.height : 1.0
                readonly property real fitScale: root.fitMode === "page"
                                                 ? Math.min(wFit, hFit) : wFit
                readonly property real pageW: pts.width  * fitScale * root.zoom
                readonly property real pageH: pts.height * fitScale * root.zoom
                readonly property bool showAddLine: root.editCtl.editMode
                height: pageH + 4 + (showAddLine ? 26 : 0)

                Rectangle {
                    id: pageBg
                    // Zentriert; bei Zoom-Ueberlauf um panX verschiebbar.
                    x: Math.round((pages.width - width) / 2 + root.panX)
                    width: pageCell.pageW; height: pageCell.pageH
                    color: "white"

                    // Unterste Ebene: das PdfPageImage darueber faengt keine Maus. Ein Badge liegt
                    // hoeher und verbraucht den Press, damit Annotation-Klicks nicht markieren.
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
                            // Schwenken statt Markieren, wenn gezoomt und der Druckpunkt nicht ueber Text liegt.
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

                        // Nur auf der Seite mit aktiver Auswahl; Rechtecke normalisiert.
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

                        // Rev-getrieben: die Suche liefert stueckweise.
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

                        // Sitzt direkt in der eingebetteten Textebene, nicht als Overlay: die Position
                        // kommt in PDF-Punkten und wird hier nur auf die Darstellungsgroesse skaliert.
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
                            // Laeuft nur, solange das Caret sichtbar ist - keine Animation im Leerlauf.
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
                                // Audio (type 0) uebernimmt PdfAudio mit eigenen Hotspots; hier nur Video/Link.
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
                                // MouseArea statt TapHandler: verbraucht den Press, sonst markierte der Faenger.
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.activateAnnotation(badge.modelData)
                                }
                            }
                        }

                        // Klick spielt den Clip und oeffnet die Audioleiste; der aktive Clip pulsiert.
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

                        // Werkzeug-Fänger der Seite, nur im Editmodus: liegt über Auswahl-Fänger, Badges und Hotspots, aber unter den
                        // Box-Delegates. Das Routing entscheidet nach aktivem Werkzeug.
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
                            // Zustand der beiden nicht-zeichnenden Gesten. Die Bezeichner muessen deklariert
                            // sein: QML wirft sonst ReferenceError bzw. Invalid write to global property und
                            // bricht den Handler sofort ab - updateDraw/endDraw wurden nie erreicht.
                            property bool _textSel: false
                            property bool _textSelDrag: false
                            property real _selSx: 0
                            property real _selSy: 0
                            property var  _repStart: null
                            property var  _repLast: null
                            // Textebene nachladen, falls die editMode-Vorbereitung verworfen wurde.
                            Connections {
                                target: root.editCtl
                                function onToolChanged() {
                                    // Nur vorbereiten; die Umwandlung einer Markierung laeuft ueber replaceSelectionNow().
                                    if ((root.editCtl.tool === 6 || root.editCtl.tool === 8)
                                            && root.source.length > 0)
                                        pdfTextCtl.prepare(root.source)
                                }
                            }
                            function _toPt(mx, my) {
                                var pts = pageCell.pts
                                if (pts.width <= 0 || pts.height <= 0)
                                    return null
                                return { x: Math.max(0, Math.min((mx / pageImg.width)  * pts.width,  pts.width)),
                                         y: Math.max(0, Math.min((my / pageImg.height) * pts.height, pts.height)) }
                            }
                            onPressed: (m) => {
                                // Bewusst ohne ready-Wache: beginSelection() laedt die Textebene lazy und der
                                // onReadyChanged-Catch-up zieht die Auswahl nach. Mit Wache blieb der erste
                                // Markierversuch im Editmodus wirkungslos.
                                if (root.editCtl.tool === 0
                                        && pageImg.width > 0 && pageImg.height > 0) {
                                    _textSel = true
                                    _textSelDrag = false
                                    _selSx = m.x; _selSy = m.y
                                    root.beginSelection(pageCell.index)
                                    return
                                }
                                // Klick setzt das Caret. Keine Zeichen-Session - Werkzeug 7 liegt ueber der
                                // Zeichen-Schwelle und liefe sonst in beginDraw.
                                if (root.editCtl.tool === 7) {
                                    root.commitEditing()
                                    const cp = _toPt(m.x, m.y)
                                    if (cp) {
                                        const first = !root.editCtl.caretReady
                                                      || root.editCtl.caretPage !== pageCell.index
                                        root.editCtl.placeCaret(pageCell.index, cp.x, cp.y)
                                        caretInput.forceActiveFocus()
                                        // Die Textebene wird beim ersten Klick asynchron gelesen - das dauert sichtbar.
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
                                // Markieren zieht wie Text ersetzen auf; ohne Textebene bleibt der Zugbereich.
                                if (root.editCtl.tool === 8
                                        && pts.width > 0 && pts.height > 0) {
                                    _repStart = { x: p.x / pts.width, y: p.y / pts.height }
                                    _repLast  = _repStart
                                    if (pdfTextCtl.ready)
                                        root.updateSelection(pageCell.index,
                                            _repStart.x, _repStart.y, _repStart.x, _repStart.y)
                                    return
                                }
                                // Schwaerzen zieht wie Text ersetzen auf - dieselbe Auswahl, anderer Abschluss.
                                if ((root.editCtl.tool === 6 || root.editCtl.tool === 9)
                                        && pdfTextCtl.ready
                                        && pts.width > 0 && pts.height > 0) {
                                    _repStart = { x: p.x / pts.width, y: p.y / pts.height }
                                    _repLast  = _repStart
                                    root.updateSelection(pageCell.index,
                                        _repStart.x, _repStart.y, _repStart.x, _repStart.y)
                                    return
                                }
                                // Tool 2..6 entspricht PdfAnnKind 1..5.
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
                                    // Markieren hat keine Zeichen-Session - die Auswahl ist das Ergebnis.
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
                            // replaceProbe liefert Zeilen-Bounds, mittlere Zeilenhoehe und den eingebetteten
                            // Text; der Controller schnappt die Box darauf ein. Ohne Treffer bleibt sie still
                            // unbefuellt. Beim Schwaerzen wandert der Text nach origText statt in die Box.
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
                                // Neue Box direkt in die Textbearbeitung; ausgeblendete Notizen wieder zeigen.
                                root.notesVisible = true
                                root._autoEditId = id
                            }
                        }

                        // Ein Repeater ueber alle Boxen je Seite; das Delegate blendet sich selbst ein.
                        // Interaktiv nur im Editmodus, sichtbar auch beim Lesen.
                        Repeater {
                            model: root.editCtl.boxModel
                            delegate: PdfEditBox {
                                // Die Notiz findet ihre Seite ueber den stabilen Seiten-Key, nicht ueber die
                                // Position - deshalb bleibt sie beim Umsortieren an ihrer Seite.
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

                        // Qt PDF zeichnet Widget-Annotationen nicht - dieses Overlay ist die einzige
                        // Darstellung der Felder und deshalb in beiden Modi aktiv. Das Tippen laeuft
                        // rev-getrieben, damit die Delegates nicht je Zeichen neu entstehen.
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

                        PdfEditToolbar {
                            pageIndex: (root.editCtl.viewPageCount,
                                        root.editCtl.viewPageKey(pageCell.index))
                            pageScale: pageCell.pts.width > 0
                                       ? pageImg.width / pageCell.pts.width : 1
                            pageW: pageImg.width
                            pageH: pageImg.height
                            surface: root
                        }

                        // Eigene Ebene nur fuer die rechte Maustaste; die Linksklick-Logik bleibt unberuehrt.
                        // Das Menue lebt einmal im Root, nicht je Delegate.
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

                    // Klick fuegt eine leere A4-Seite nach dieser Ansichts-Seite ein.
                    Item {
                        visible: pageCell.showAddLine
                        // Kind von pageBg: in Seitenkoordinaten positionieren, sonst doppelter Offset.
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

        // Wheel-Faenger ueber den Seiten; NoButton laesst Klicks durch.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            z: 1
            onWheel: (wheel) => root.wheelPages(wheel)
        }

        // Thumbnail-Leiste als Overlay links - kein Seiten-Reflow.
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
                // Erst nach dem Warmlauf befuellen, damit die Thumbnails nicht mit der ersten
                // sichtbaren Hauptseite um den PDFium-Render-Mutex konkurrieren.
                model: (root.docReady && root._warm) ? root.doc.pageCount : 0
                spacing: 10
                // Vorschauen kommen JPEG-komprimiert aus dem RAM-Provider; mehr Vorhalt kostet
                // kaum RAM. max(0,..): vor dem ersten Layout ist die Hoehe negativ.
                cacheBuffer: Math.max(0, Math.round(thumbs.height * 1.5))
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                // Gezogen wird die Vorschau, umsortiert wird der Seiten-Plan im Controller
                // (ein Undo-Schritt); die Notizen folgen ihrer Seite ueber den Seiten-Key.
                property int dragIndex: -1
                property int dropIndex: -1
                readonly property bool reorderable: root.editCtl.editMode
                                                    && root.editCtl.viewPageCount > 1

                // Zwischen zwei Kacheln liefert indexAt -1; dann entscheidet die Naehe.
                function indexForContentY(cy) {
                    var i = thumbs.indexAt(thumbs.width / 2, cy)
                    if (i >= 0)
                        return i
                    if (cy <= thumbs.originY)
                        return 0
                    if (cy >= thumbs.originY + thumbs.contentHeight)
                        return thumbs.count - 1
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

                // Randnaehe scrollt die Leiste weiter - sonst waere nur der Ausschnitt sortierbar.
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
                    // Cache-Buster: hochzaehlen, sobald die Vorschau im RAM-Store liegt.
                    property int rev: 0
                    readonly property size pts: root.doc.pagePointSize(index)
                    readonly property real thumbW: thumbs.width - 8
                    readonly property real thumbH: pts.width > 0 ? thumbW * (pts.height / pts.width) : thumbW * 1.414
                    width: thumbs.width
                    height: thumbH + 18

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
                        // Aus dem RAM-Provider statt PdfPageImage: kein Render am PDFium-Mutex.
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
                        opacity: thumbs.dragIndex === thumbCell.index ? 0.45 : 1.0
                        TapHandler { onTapped: root.goToPage(thumbCell.index) }

                        // TapHandler und DragHandler vertragen sich: wird aus dem Druck ein Zug, feuert
                        // der Tap nicht mehr.
                        DragHandler {
                            id: thumbDrag
                            enabled: thumbs.reorderable
                            target: null                      // wir bewegen nichts selbst
                            cursorShape: Qt.ClosedHandCursor
                            // Entscheidend in einer ListView: ohne eingeschraenkte Rechte reisst die Liste den
                            // Griff an sich und scrollt, statt die Seite ziehen zu lassen.
                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                            onActiveChanged: {
                                if (active) {
                                    thumbs.dragIndex = thumbCell.index
                                    thumbs.dropIndex = thumbCell.index
                                    return
                                }
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
                                const ly = thumbs.mapFromItem(null, centroid.scenePosition).y
                                thumbAutoScroll.dir = ly < 24 ? -1
                                                   : ly > thumbs.height - 24 ? 1 : 0
                                // indexAt() erwartet Inhaltskoordinaten - ueber das contentItem abbilden.
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

                    Rectangle {
                        visible: thumbs.dragIndex >= 0 && thumbs.dropIndex === thumbCell.index
                                 && thumbs.dragIndex !== thumbCell.index
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: thumbCell.thumbW; height: 3; radius: 1.5
                        color: App.themeAccent
                        // Von oben gezogen: Marke unter die Zielkachel, sonst darueber.
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
                        // Marke fuer eingefuegte Fremdseiten - dort ist kein zeichenweises Bearbeiten moeglich.
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

            // Faengt als NoButton-MouseArea nur das Mausrad, laesst Klicks hindurch.
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
                    // Origin-bewusst wie die Hauptansicht: Seitenspruenge koennen originY != 0 erzeugen.
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

        // Audio-Panel rechts, symmetrisch zur Thumbnail-Leiste; zeigt nur die Audios der
        // aktuellen Seite, der Mini-Player laeuft unabhaengig weiter. Das Editor-Panel
        // teilt sich dieselbe Datei - je nach PdfEdit.panelOnTop ist genau eines sichtbar.
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

            // Haengt an audioClips und currentPage, nicht an _audioRev - kein Neuaufbau je Tick.
            readonly property var pageClips: root._clipsOnPage(root.currentPage)

            Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: App.themeBorder }

            // Klicks und Wheel auf leeren Panel-Flaechen abfangen, sonst Durchgriff auf die Seite.
            MouseArea { anchors.fill: parent; onWheel: (wheel) => { wheel.accepted = true } }

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

            Text {
                anchors.centerIn: parent
                visible: audioPanel.pageClips.length === 0
                text: App.uiText(App.language, "PdfNoAudioOnPage")
                color: App.themeTextMuted; font.pixelSize: 12
            }

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

                    // Zeigt Gehoertes und ist an beliebige Stelle ziehbar.
                    Slider {
                        id: arowSlider
                        anchors { left: arowBtn.right; leftMargin: 10; right: parent.right; rightMargin: 12; bottom: parent.bottom; bottomMargin: 8 }
                        height: 16
                        from: 0
                        to: Math.max(1, arow.durMs)
                        // pressed?value:.. haelt die Bindung beim Ziehen intakt.
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

            // Mini-Player, unten angedockt.
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
    }

    // Video-Overlay einer Annotation.
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

    // Seiten-Kontextmenue: einzelne Seite oder mehrere ueber das Auswahlraster.
    // Ziel-Ordner ist der Ordner der Quelldatei.
    ThemedMenu {
        id: pageCtxMenu
        objectName: "pageCtxMenu"      // Griff für tests/bench
        property int ctxPage: 0

        // Bezieht sich auf die ausgewaehlte Notiz und erscheint nur, solange sie eine
        // offene Aenderung ist (track 1 = neu, 2 = geloescht).
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

        // Nur im Bearbeiten-Modus und nur, wenn es welche gibt; Strg+Z holt sie zurueck.
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
        // Seitenverwaltung, nur im Editmodus.
        MenuSeparator { visible: root.editCtl.editMode }
        // Zweimal Drehen - die Richtung sagt allein das Symbol daneben.
        component RotateItem: MenuItem {
            id: rotItem
            property string iconName: ""
            visible: root.editCtl.editMode
            height: visible ? implicitHeight : 0
            text: App.uiText(App.language, "PdfRotatePage")
            // Kein Row und kein Bezug auf rotIcon.height: beides liess die Zeile leer. Die
            // Hoehe kommt aus dem Textmass und der festen Symbolgroesse.
            contentItem: Item {
                implicitWidth:  rotLbl.implicitWidth + 8 + 18
                implicitHeight: Math.max(18, rotLbl.implicitHeight)
                // Symbol hinter der Beschriftung: erst liest man Drehen, dann sieht man wohin.
                Text {
                    id: rotLbl
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: rotItem.text
                    font: rotItem.font
                    color: rotItem.enabled ? App.themeTextPrimary : App.themeTextMuted
                }
                DrawnIcon {
                    anchors.left: rotLbl.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    name: rotItem.iconName
                    size: 18
                    color: rotItem.enabled ? App.themeTextPrimary : App.themeTextMuted
                }
            }
        }
        RotateItem {
            iconName: "rotate-left"
            onTriggered: root._rotatePage(pageCtxMenu.ctxPage, -90)
        }
        RotateItem {
            iconName: "rotate-right"
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

    // Erst die Datei waehlen, dann ihre Seiten im Auswahlraster. Das Kopieren laeuft
    // verlustfrei im Controller.
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

    // Die Seitenmasse kennt nur die Ansicht; der Controller dreht die Notizen mit.
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
            // Leerer Ziel-Ordner: extractOrdered nutzt den Ordner der Quelle.
            PdfExtract.extractOrdered(items, "", name)
        }
    }

    // PdfExtract ist ein Singleton - nur die Surface, die den Auftrag startete, meldet.
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
                // Neue Datei sofort in der Galerie zeigen, nicht erst ueber den Watcher.
                App.refreshCurrentFolder()
                root._toast(App.uiText(App.language, "ExtractOkToast")
                                .arg(String(targetPath).split("/").pop()))
            } else {
                root._toast(App.uiText(App.language, "ExtractFailToast"))
            }
        }
    }

    // Toast unten mittig fuer Speichern, Export-Fortschritt und -Ergebnis. Jede
    // Meldung startet die Ausblend-Uhr neu.
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
