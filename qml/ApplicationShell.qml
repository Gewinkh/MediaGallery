import QtQuick
import QtQuick.Window
import QtQuick.Controls
import MediaGallery 1.0
import "common"
import "gallery"
import "pdf"
import "settings"
import "tags"
import "viewer"

// ─────────────────────────────────────────────────────────────────────────────
//  ApplicationShell.qml - Wurzel-Fenster der QML-UI.
//
//  Phase 3: Die Galerie-Seite trägt jetzt die FilterBar (oben) und ein optional
//  einblendbares TagCategoryPanel (rechte Seitenleiste). Die Vollbild-Seite des
//  StackView ist der vollständige FullscreenViewer (Bild/Video/PDF/Text/Audio).
// ─────────────────────────────────────────────────────────────────────────────
ApplicationWindow {
    id: shell
    visible: true

    width:  App.initialWindowWidth  > 0 ? App.initialWindowWidth  : 1200
    height: App.initialWindowHeight > 0 ? App.initialWindowHeight : 800
    x: App.initialWindowX
    y: App.initialWindowY

    //  Fenstertitel: „MediaGallery - <Ordner> / <Datei>". Der Dateiname steht
    //  hier, weil die Kopfleiste der Kachel ihn nicht mehr trägt (dort sitzt jetzt
    //  das Ansichts-Menü). Ohne offene Datei bleibt es beim Ordner.
    title: {
        const basis = App.currentFolder.length > 0
                      ? ("MediaGallery - " + folderName(App.currentFolder))
                      : "MediaGallery"
        return (!shell.galleryVisible && shell.activeFilePath.length > 0)
               ? (basis + " / " + folderName(shell.activeFilePath))
               : basis
    }

    //  Pfad der Datei in der AKTIVEN Kachel; die Kacheln melden ihn über ein
    //  `Binding` mit `when: paneActive` (s. Repeater der Split-Seite).
    property string activeFilePath: ""

    color: App.themeBackground

    palette.window:     App.themeBackground
    palette.windowText: App.themeTextPrimary
    palette.base:       App.themeCard
    palette.text:       App.themeTextPrimary
    palette.button:     App.themeMenuBarBg
    palette.buttonText: App.themeTextPrimary
    palette.highlight:  App.themeAccent
    palette.highlightedText: App.themeBackground
    palette.mid:        App.themeBorder
    //  Schattierungs-Rollen: hier BEWUSST noch einmal, obwohl main.cpp sie
    //  bereits auf der QGuiApplication-Palette setzt. Datei-/Ordnerdialoge
    //  öffnen als EIGENES Fenster und erben die Palette ihres ELTERNfensters,
    //  nicht die der Anwendung (gemessen) - ohne diese Zeilen zeichnet Qts
    //  Dialog-Implementierung Rahmen, Trenner und Seitenleiste aus der
    //  Systempalette und wirkt als Fremdkörper im Theme.
    //  Werte identisch zu `applyThemePalette` in main.cpp halten.
    palette.light:      Qt.lighter(App.themeCard, 1.3)
    palette.midlight:   Qt.lighter(App.themeBorder, 1.2)
    palette.dark:       Qt.darker(App.themeBackground, 1.2)
    palette.shadow:     Qt.darker(App.themeBackground, 1.6)
    palette.alternateBase:   App.themeBackground
    palette.placeholderText: App.themeTextMuted

    property string statusText: ""

    //  Zeigt die fokussierte Hälfte gerade ihre Galerie? Daran hängen die
    //  Ablegeleisten (im Vollbild einer Datei gibt es keine Kacheln zu ziehen).
    //  Die Hälften melden es über ihre `galleryActive`-Eigenschaft.
    property bool galleryVisible: true
    //  Das QML-Element der ERSTEN Hälfte - für die Menüknöpfe oben, solange es
    //  nur eine gibt. Wird vom Wiederholer gesetzt.
    property Item firstPaneItem: null
    //  Während eine Hälfte an ihrer Leiste gezogen wird und über der anderen
    //  steht: Vorschau auf den Tausch.
    property bool swapPreview: false

    // ── Immersives Vollbild (Taste F im Media Viewer) ─────────────────────────
    //  Blendet die App-Chrome aus: Fenster auf Vollbild (Titelleiste/Dekoration
    //  weg), Menüleiste weg, Kachel-Chrome der Viewer weg (dort: obere Leiste +
    //  untere Vor/Zurück-Navigation, s. FullscreenViewer.immersive). Erneutes F
    //  (oder Esc) stellt alles wieder her.
    //  Fenstergeometrie/-zustand VOR dem Vollbild merken: `visibility` liefert im
    //  Vollbild nur noch FullScreen und width/height die Bildschirmmaße - ohne
    //  diese Sicherung würde ein Beenden im Vollbild die gespeicherte
    //  Fensterposition/-größe mit den Bildschirmmaßen überschreiben.
    property bool immersiveFullscreen: false
    property int  _preImmersiveVisibility: Window.Windowed
    property rect _preImmersiveGeometry: Qt.rect(0, 0, 0, 0)
    //  Haben WIR den Fensterzustand umgeschaltet? Nur dann dürfen wir ihn beim
    //  Verlassen wieder anfassen.
    property bool _windowWasSwitched: false

    function setImmersive(on) {
        if (shell.immersiveFullscreen === on) return
        if (on) {
            shell._preImmersiveVisibility = shell.visibility
            shell._preImmersiveGeometry = Qt.rect(shell.x, shell.y, shell.width, shell.height)
            shell.immersiveFullscreen = true
            //  **War das Fenster schon im Vollbild, wird es NICHT angefasst** -
            //  weder beim Betreten noch beim Verlassen. Wer die App per
            //  Fenstermanager auf Vollbild gestellt hat, bekommt durch F nur die
            //  Chrome weg und behält seinen Fensterzustand. Ein blindes Setzen
            //  und späteres „Zurücksetzen" warf das Fenster hier auf seine
            //  normale Größe zurück.
            shell._windowWasSwitched = (shell.visibility !== Window.FullScreen)
            if (shell._windowWasSwitched)
                shell.visibility = Window.FullScreen
        } else {
            shell.immersiveFullscreen = false
            if (!shell._windowWasSwitched)
                return                     // Fenster gehörte uns nie
            shell._windowWasSwitched = false
            //  Deterministisch zurück: ERST in den normalen Zustand samt
            //  gemerkter Geometrie, DANN ggf. maximieren. Der direkte Sprung
            //  Vollbild -> Maximiert ist eine reine Wertzuweisung, die manche
            //  Fenstermanager verschlucken; über den Zwischenschritt gibt es in
            //  jedem Fall einen echten Zustandswechsel, und die Geometrie sitzt
            //  danach auch als „normale" Fläche des Fensters richtig.
            shell.visibility = Window.Windowed
            const g = shell._preImmersiveGeometry
            if (g.width > 0) {
                shell.x = g.x; shell.y = g.y
                shell.width = g.width; shell.height = g.height
            }
            if (shell._preImmersiveVisibility === Window.Maximized)
                shell.visibility = Window.Maximized
        }
    }
    function toggleImmersive() { setImmersive(!shell.immersiveFullscreen) }

    // ── Globale PDF-Seiten-Extraktion (FilterBar ▸ „Extrahieren") ─────────────
    //  PdfExtract ist ein Singleton, das AUCH jede PdfSurface nutzt -> diese
    //  Flags markieren, ob der laufende Scan/Auftrag von HIER stammt; nur dann
    //  reagiert die Shell auf die Ergebnis-Signale.
    property bool _scanPending: false
    property bool _extractPending: false
    //  Ordner, in dem die laufende Seiten-Extraktion arbeitet. Leer = der
    //  geoeffnete Ordner; die Kopfzeile eines aufgeklappten Bereichs setzt
    //  hier ihren eigenen.
    property string _extractFolder: ""
    function _extractTarget() {
        return shell._extractFolder.length > 0 ? shell._extractFolder
                                               : App.currentFolder
    }
    property string _extractName: ""

    Component.onCompleted: {
        if (App.startMaximized)
            shell.visibility = Window.Maximized
        App.restoreLastFolder()
        //  Zwei-Fenster-Modus der letzten Sitzung wiederherstellen. Der Fokus
        //  geht danach zurück auf die erste Hälfte - dort hat man aufgehört.
        var second = App.secondFolder()
        if (second.length > 0) {
            var p = App.addPane()
            if (p) p.openFolder(second)
            App.focusPane(0)
        }
        //  Zuletzt gespielten Titel BEREITLEGEN (nicht abspielen) - er wartet
        //  auf den ersten Druck auf ⏯.
        Audio.restoreSession()
    }

    onClosing: function(close) {
        //  Titel und Stelle merken, solange die Wiedergabe noch steht.
        Audio.rememberSession()
        // Im immersiven Vollbild den GEMERKTEN Fensterzustand sichern - sonst
        // startet die App beim nächsten Mal in Bildschirmgröße an Position 0,0.
        if (shell.immersiveFullscreen) {
            const g = shell._preImmersiveGeometry
            App.saveWindowState(g.width, g.height, g.x, g.y,
                                shell._preImmersiveVisibility === Window.Maximized)
            return
        }
        App.saveWindowState(shell.width, shell.height, shell.x, shell.y,
                            shell.visibility === Window.Maximized)
    }

    function folderName(path) {
        var n = path.replace(/[\/\\]+$/, "")
        var i = Math.max(n.lastIndexOf("/"), n.lastIndexOf("\\"))
        return i >= 0 ? n.substring(i + 1) : n
    }

    // ── „Ordner hinzufügen": Vorbefüllung mit dem aktuellen Ordner ────────────
    //  Pfad für den Duplikat-Vergleich normalisieren: NUR abschließende
    //  Separatoren entfernen („C:\Test" ≡ „C:\Test\"), Vergleich bleibt
    //  case-SENSITIV. Wurzelpfade („/", „C:\") bleiben unangetastet (das
    //  Muster verlangt ein Nicht-Separator-Zeichen vor den Separatoren).
    function _normalizedFolderPath(p) {
        return p.replace(/([^\/\\])[\/\\]+$/, "$1")
    }
    //  Liefert den aktuell geöffneten Ordner, sofern er noch NICHT in den
    //  gespeicherten Ordnern steht - sonst "" (Dialog öffnet dann leer).
    //  (Wird nur noch von der Hälfte gebraucht - dort steht eine eigene Fassung.)
    function _bookmarkPrefillPath() {
        var cur = App.currentFolder
        if (cur.length === 0) return ""
        var norm = _normalizedFolderPath(cur)
        var list = App.savedFolders
        for (var i = 0; i < list.length; i++)
            if (_normalizedFolderPath(list[i].path) === norm) return ""
        return cur
    }

    // ── Menüleiste ───────────────────────────────────────────────────────────
    //  ThemedMenu: bisher folgten die Menü-POPUPS (Datei/Ansicht/Einstellungen/
    //  Ordner + deren Untermenüs) NICHT der in Einstellungen ▸ Design gewählten
    //  Menüleisten-Farbe (App.themeMenuBarBg) - nur die Leiste selbst (via
    //  palette.button) war korrekt eingefärbt, die aufklappenden Popups nutzten
    //  weiterhin die Fusion-Standardfarbe. Analog zum bereits korrekt
    //  eingefärbten Filter-Popup (FilterBar.qml) bekommt jedes Menu hier
    //  denselben expliziten Hintergrund.
    //  `ThemedMenu` liegt jetzt als eigene Datei unter `qml/common/` - die
    //  Kontextmenüs (Kachel, PDF-Seite, Tag-Chips) brauchen dieselbe Fassung,
    //  und eine Inline-Komponente ist außerhalb dieser Datei nicht erreichbar.

    //  EIGENE Menüleiste statt der nativen `MenuBar`: Die Fusion-`MenuBar`
    //  belegt (wie ihr Widgets-Pendant) die ALT-Taste für Menü-Navigation/
    //  Mnemoniks - sprachabhängig und per API nicht abschaltbar; das kollidierte
    //  mit den App-Kürzeln (Alt+S/Alt+Q/Alt+<-). Diese Leiste ist eine gethemte
    //  Button-Reihe, die die `Menu`-Popups per KLICK öffnet -> die Alt-Taste
    //  gehört ausschließlich den App-Shortcuts, sprachunabhängig. Die
    //  Menü-Inhalte (inkl. Lesezeichen-Logik) sind unverändert.
    menuBar: Rectangle {
        id: menuStrip
        //  Nur auf der Galerie-Seite sichtbar: eine geöffnete Datei bekommt die
        //  ungeteilte Fensterfläche (dedizierte Vollbild-Ansicht) - die Kachel
        //  bringt ihre eigene Kopfleiste inkl. Zurück-Knopf mit. ApplicationWindow
        //  nimmt eine unsichtbare `menuBar` aus dem Layout, es bleibt also kein
        //  Leerstreifen stehen. Im immersiven Vollbild (F) ist sie ebenfalls weg.
        visible: !shell.immersiveFullscreen
        implicitHeight: 32
        color: App.themeMenuBarBg
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: App.themeBorder }

        //  Ein Knopf, der das Menü der EINEN Hälfte aufklappt (nur ungeteilt).
        component PaneMenuBtn: Rectangle {
            id: pmb
            property string label: ""
            property bool   folder: false          // false = Datei, true = Ordner
            readonly property var paneItem: shell.firstPaneItem
            width: visible ? pmbLbl.implicitWidth + 22 : 0
            height: 24; radius: 5
            anchors.verticalCenter: parent.verticalCenter
            color: pmbHover.hovered ? App.themeCard : "transparent"
            Text { id: pmbLbl; anchors.centerIn: parent; text: pmb.label
                   color: App.themeTextPrimary; font.pixelSize: 13 }
            HoverHandler { id: pmbHover }
            TapHandler {
                onTapped: {
                    if (!pmb.paneItem) return
                    const open = pmb.folder ? pmb.paneItem.folderMenuOpen()
                                            : pmb.paneItem.fileMenuOpen()
                    if (open) { pmb.paneItem.closeMenus(); return }
                    if (pmb.folder) pmb.paneItem.popupFolderMenu(pmb)
                    else            pmb.paneItem.popupFileMenu(pmb)
                }
            }
        }

        component MenuBtn: Rectangle {
            property string label: ""
            property var menu: null
            width: mbLbl.implicitWidth + 22; height: 24; radius: 5
            anchors.verticalCenter: parent.verticalCenter
            color: (mbHover.hovered || (menu && menu.opened)) ? App.themeCard : "transparent"
            Text { id: mbLbl; anchors.centerIn: parent; text: parent.label
                   color: App.themeTextPrimary; font.pixelSize: 13 }
            HoverHandler { id: mbHover }
            TapHandler {
                onTapped: {
                    if (menu.opened) menu.close()
                    else menu.popup(parent, 0, parent.height + 3)
                }
            }
        }

        //  Blätterbar statt abgeschnitten: in einem schmalen Fenster passten die
        //  Menüknöpfe nicht mehr in die Zeile und die rechten waren gar nicht
        //  erreichbar. Strg + Mausrad blättert (Muster aus dem PDF-Editor).
        ScrollableBar {
            id: menuBtnRow
            anchors { left: parent.left; leftMargin: 6; right: parent.right; rightMargin: 6
                      verticalCenter: parent.verticalCenter }
            height: parent.height
            spacing: 2
            //  APPWEIT: „Ansicht" und „Einstellungen" gelten für beide Hälften
            //  und stehen immer hier oben. „Datei" und „Ordner" gehören der
            //  Hälfte: bei ZWEI Hälften stehen sie in deren eigener Leiste, bei
            //  EINER hier in derselben Zeile - dann sieht die Leiste aus wie vor
            //  dem Zwei-Fenster-Modus (Festlegung des Nutzers).
            PaneMenuBtn { label: App.menuFileText;  folder: false; visible: !paneArea.split }
            MenuBtn { label: App.menuViewText;      menu: viewMenu }
            MenuBtn { label: App.menuSettingsText;  menu: settingsMenu }
            PaneMenuBtn { label: App.menuBookmarksText; folder: true; visible: !paneArea.split }
        }

        // ── Menü-Popups (per Klick geöffnet - KEINE MenuBar/Alt-Navigation) ────
        ThemedMenu {
            id: viewMenu
            MenuItem {
                text: App.menuToggleOptionsText
                checkable: true
                checked: App.optionsVisible
                onTriggered: App.toggleOptions()
            }
            MenuSeparator {}
            MenuItem {
                text: App.uiText(App.language, "MenuTileSize")
                onTriggered: {
                    tileSizeLoader.active = true
                    tileSizeLoader.item.openDialog()
                }
            }
            MenuSeparator {}
            //  Immersives Vollbild (F): betrifft das FENSTER, nicht die Datei -
            //  deshalb hier oben und nicht mehr im Menü der Kachel. Gilt auch auf
            //  der Galerie, wo die zusätzliche Fläche am meisten bringt.
            MenuItem {
                text: App.uiText(App.language, "ViewMenuImmersive")
                checkable: true
                checked: shell.immersiveFullscreen
                onTriggered: shell.toggleImmersive()
            }
            MenuSeparator {}
            //  Zweite Galerie daneben - jede Hälfte hat ihren eigenen Ordner.
            MenuItem {
                text: App.paneCount > 1 ? App.uiText(App.language, "MenuUnsplitWindow")
                                        : App.uiText(App.language, "MenuSplitWindow")
                onTriggered: {
                    if (App.paneCount > 1) paneArea.unsplit()
                    else                   App.addPane()
                }
            }
            //  Hälften tauschen. Der GRIFF dafür ist die Leiste der Hälfte -
            //  die ist aber weg, sobald dort eine Datei offen ist (die Kachel
            //  bringt ihre eigene Kopfzeile mit). Ohne diesen Eintrag ließen
            //  sich zwei Hälften mit geöffneten Dateien überhaupt nicht mehr
            //  tauschen (Nutzerbefund).
            MenuItem {
                text: App.uiText(App.language, "MenuSwapPanes")
                enabled: App.paneCount > 1
                onTriggered: App.swapPanes()
            }
        }

        ThemedMenu {
            id: settingsMenu
            MenuItem {
                text: App.uiText(App.language, "MenuSettingsItem")
                onTriggered: shell.openSettings()
            }
            MenuSeparator {}
            ThemedMenu {
                title: App.menuLanguageText
                MenuItem { text: "Deutsch"; checkable: true; checked: App.language === "de"; onTriggered: App.setLanguage("de") }
                MenuItem { text: "English"; checkable: true; checked: App.language === "en"; onTriggered: App.setLanguage("en") }
            }
            ThemedMenu {
                title: App.menuVideoPlaybackText
                MenuItem { text: App.menuVideoNativeText;   checkable: true; checked: App.videoPlayback === "native";   onTriggered: App.setVideoPlayback("native") }
                MenuItem { text: App.menuVideoExternalText; checkable: true; checked: App.videoPlayback === "external"; onTriggered: App.setVideoPlayback("external") }
            }
        }

    }

    // ── Die Hälften des Hauptfensters ───────────────────────────────────────
    //  Eine oder zwei vollwertige Galerien nebeneinander. Jede bekommt ihren
    //  Teilbaum von `PaneHost` mit EIGENEM QML-Kontext (s. src/app/PaneHost.h) -
    //  darin zeigen `mediaModel`, `galleryModel` und `Tags` auf ihre Objekte.
    Item {
        id: paneArea
        objectName: "paneArea"      // Griff für tests/bench (Regel 31)
        anchors.fill: parent

        readonly property bool split: App.paneCount > 1

        //  Zeigt die Hälfte i gerade ihre Galerie, ist dort also KEINE Datei
        //  offen? (`itemAt` liefert den `PaneHost`, dessen `item` die Galerie.)
        function _paneEmpty(i) {
            const h = paneRepeater.itemAt(i)
            return !(h && h.item) || h.item.galleryActive
        }
        //  Teilung aufheben: eine Hälfte MUSS weichen - aber nie die, in der
        //  etwas offen ist, solange die andere leer ist. Vorher stand hier fest
        //  `closePane(1)`; damit verschwand die geöffnete Datei der rechten
        //  Hälfte, obwohl links nur die Galerie stand (Nutzerbefund), und wer
        //  rechts arbeitete, schloss mit dem Eintrag seine EIGENE Hälfte.
        function unsplit() {
            const focused = Math.max(0, App.focusedPaneIndex)
            const other   = (focused === 0) ? 1 : 0
            //  Regel: es geht die unfokussierte Hälfte - es sei denn, die
            //  fokussierte ist leer und die andere nicht. Dann geht die leere,
            //  und es geht nichts verloren.
            if (_paneEmpty(focused) && !_paneEmpty(other)) App.closePane(focused)
            else                                          App.closePane(other)
        }
        readonly property real dividerW: 6
        //  Breite der linken Hälfte aus dem gespeicherten Verhältnis.
        readonly property real leftW: split
            ? Math.max(120, Math.min(width - dividerW - 120,
                                     Math.round((width - dividerW) * App.paneSplit)))
            : width

        Repeater {
            id: paneRepeater
            //  **Modell, NICHT `App.panes`**: ueber eine Liste baut ein
            //  `Repeater` bei jeder Aenderung ALLE Delegates neu - die zweite
            //  Haelfte aufzumachen zerstoerte damit die erste samt der dort
            //  geoeffneten Datei, das Schliessen ebenso (gemessen). Das Modell
            //  meldet Einfuegen/Entfernen/Verschieben punktgenau.
            model: App.panesModel
            delegate: PaneHost {
                id: paneHost
                required property int index
                required property var paneObject

                pane: paneHost.paneObject
                source: "qrc:/qml/gallery/GalleryPane.qml"
                y: 0
                height: paneArea.height
                x: paneHost.index === 0 ? 0 : paneArea.leftW + paneArea.dividerW
                width: paneHost.index === 0
                       ? paneArea.leftW
                       : Math.max(0, paneArea.width - paneArea.leftW - paneArea.dividerW)

                onLoadFailed: function(err) { console.warn("Hälfte nicht ladbar:", err) }

                //  Das Element der ERSTEN Hälfte für die Menüknöpfe oben.
                //  **Muss eine Bindung sein, keine Zuweisung in `onItemChanged`:**
                //  Wird die linke Hälfte geschlossen, rutscht die rechte auf
                //  Platz 0, OHNE dass ihr `item` sich ändert - `onItemChanged`
                //  feuerte dann nie, und `firstPaneItem` blieb auf dem gerade
                //  zerstörten Element stehen (also null). Die Knöpfe „Datei"
                //  und „Ordner" oben waren damit für den Rest der Sitzung tot
                //  (gemessen, `tests/bench/bench_shell.cpp`).
                //  `RestoreNone`: die weichende Hälfte darf den Wert beim
                //  Abschalten nicht auf ihren alten Stand zurückdrehen.
                Binding {
                    target: shell
                    property: "firstPaneItem"
                    value: paneHost.item
                    when: paneHost.index === 0
                    restoreMode: Binding.RestoreNone
                }

                //  Zustand hinein …
                Binding { target: paneHost.item; property: "splitActive"; value: paneArea.split
                          when: paneHost.item !== null }
                Binding { target: paneHost.item; property: "showClose";   value: paneArea.split
                          when: paneHost.item !== null }
                Binding { target: paneHost.item; property: "paneIndex";   value: paneHost.index
                          when: paneHost.item !== null }
                Binding { target: paneHost.item; property: "paneFocused"
                          value: paneHost.index === App.focusedPaneIndex
                          when: paneHost.item !== null }
                Binding { target: paneHost.item; property: "immersive"
                          value: shell.immersiveFullscreen
                          when: paneHost.item !== null }

                //  Die fokussierte Hälfte sagt der Shell, ob ihre Galerie zu
                //  sehen ist und welche Datei oben liegt (Ablegeleisten, Titel).
                Binding {
                    target: shell
                    property: "galleryVisible"
                    value: paneHost.item ? paneHost.item.galleryActive : true
                    when: paneHost.item !== null && paneHost.index === App.focusedPaneIndex
                }
                Binding {
                    target: shell
                    property: "activeFilePath"
                    value: paneHost.item ? paneHost.item.activeFilePath : ""
                    when: paneHost.item !== null && paneHost.index === App.focusedPaneIndex
                    restoreMode: Binding.RestoreNone
                }

                //  … und Meldungen heraus.
                Connections {
                    target: paneHost.item
                    function onFocusRequested() { App.focusPane(paneHost.index) }
                    function onCloseRequested() { App.closePane(paneHost.index) }
                    function onStatusRequested(text) {
                        shell.statusText = text
                        statusClearTimer.restart()
                    }
                    function onExtractRequested(folder) {
                        App.focusPane(paneHost.index)
                        shell._extractFolder = folder
                        shell._scanPending = true
                        PdfExtract.scanFolder(shell._extractTarget())
                    }
                    function onFolderDropRequested(sourcePath, folderPath) {
                        App.focusPane(paneHost.index)
                        shell._dropIntoFolder(sourcePath, folderPath)
                    }
                    function onExternalDropRequested(urls, folderPath) {
                        App.focusPane(paneHost.index)
                        App.handleDroppedUrls(urls, folderPath)
                    }
                    function onImmersiveToggleRequested() { shell.toggleImmersive() }
                    //  Leiste gezogen: über der ANDEREN Hälfte losgelassen ⇒
                    //  tauschen. Während des Zugs zeigt ein Schatten, wohin es
                    //  geht - ohne Rückmeldung wäre nicht zu sehen, dass die
                    //  Leiste überhaupt ein Griff ist.
                    function onBarDragMoved(x) {
                        const p = paneArea.mapFromItem(null, x, 0)
                        shell.swapPreview = (paneHost.index === 0)
                                            ? (p.x > paneArea.leftW + paneArea.dividerW)
                                            : (p.x < paneArea.leftW)
                    }
                    function onBarDragReleased(x) {
                        const p = paneArea.mapFromItem(null, x, 0)
                        const over = (paneHost.index === 0)
                                     ? (p.x > paneArea.leftW + paneArea.dividerW)
                                     : (p.x < paneArea.leftW)
                        shell.swapPreview = false
                        if (over) App.swapPanes()
                    }
                }
            }
        }

        //  Tausch-Vorschau: die ganze Fläche bekommt einen Akzentrahmen, damit
        //  sichtbar ist, dass beim Loslassen getauscht wird.
        Rectangle {
            visible: shell.swapPreview
            anchors.fill: parent
            color: "transparent"
            border.color: App.themeAccent
            border.width: 3
            radius: 4
            z: 50
        }

        // ── Trenner: zieht das Verhältnis ───────────────────────────────────
        Rectangle {
            id: paneDivider
            visible: paneArea.split
            x: paneArea.leftW
            y: 0
            width: paneArea.dividerW
            height: paneArea.height
            color: (dividerHover.hovered || dividerDrag.active) ? App.themeAccent : App.themeBorder

            HoverHandler { id: dividerHover; cursorShape: Qt.SplitHCursor }
            DragHandler {
                id: dividerDrag
                target: null
                yAxis.enabled: false
                onCentroidChanged: {
                    if (!active || paneArea.width <= 0) return
                    var p = paneArea.mapFromItem(null, centroid.scenePosition.x, 0)
                    App.paneSplit = p.x / paneArea.width
                }
            }
        }
    }


    // ── Drag & Drop ───────────────────────────────────────────────────────────
    //  `z: -1` ist PFLICHT, nicht Kosmetik: Ein Zug wird immer nur an die
    //  OBERSTE annehmende DropArea unter dem Zeiger geliefert (gemessen -
    //  danach ist Schluss, tiefere Flächen sehen ihn nie). Diese Fläche hier
    //  füllt das ganze Fenster, steht ohne `keys` für JEDE Nutzlast offen und
    //  ist als spät deklariertes Kind der Wurzel automatisch obenauf - sie
    //  verschluckte damit die Ablegeflächen der Tag-/Kategorien-Seitenleiste
    //  vollständig. Nach unten gelegt greift sie nur noch dort, wo keine
    //  besondere Fläche zuständig ist.
    DropArea {
        z: -1
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls) {
                App.handleDroppedUrls(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    // ── Ablegeleiste für die ORDNER dieser Ansicht (nur während eines Zuges) ─
    //  Warum es sie gibt: während eines Zuges gehört der Zeiger dem Compositor,
    //  das Mausrad erreicht die Anwendung gar nicht (gemessen, s. `config.md` ▸
    //  „Ziehen"). Um einen Ordner zu erreichen, der aus dem Bild gescrollt ist,
    //  bliebe nur das Randscrollen. Diese Leiste bringt statt dessen die Ziele
    //  zum Zeiger: sie zeigt den geöffneten Ordner und jeden Ordner, der gerade
    //  in der Galerie steht - eingerückt nach Tiefe.
    //
    //  Dasselbe Muster wie die Lesezeichen-Leiste darunter; sie kostet keinen
    //  dauerhaften Platz.
    Rectangle {
        id: folderDropBar
        z: 91
        //  Nur auf der Galerie-Seite und nur, wenn es überhaupt Ordner gibt.
        visible: App.tileDragActive && shell.galleryVisible
                 && folderDropRepeater.count > 0
        anchors { left: parent.left; right: parent.right; bottom: bookmarkDropBar.top }
        anchors.bottomMargin: bookmarkDropBar.visible ? 6 : 0
        height: 70
        color: App.themeMenuBarBg
        border.color: App.themeAccent; border.width: 1

        //  Die Ziele werden EINMAL beim Beginn des Zuges eingesammelt - während
        //  des Zuges ändert sich die Galerie nicht, und eine Bindung über alle
        //  Zeilen liefe bei jedem Ereignis neu.
        //  NICHT an `visible` hängen: `visible` fragt `folderDropRepeater.count`
        //  ab, der wiederum an `targets` hängt - die Leiste käme nie hoch.
        property var targets: []
        Connections {
            target: App
            function onTileDragActiveChanged() {
                if (App.tileDragActive)
                    folderDropBar.targets = shell._visibleFolders()
            }
        }

        Text {
            anchors { left: parent.left; leftMargin: 14; top: parent.top; topMargin: 6 }
            text: App.uiText(App.language, "FolderDropBar")
            color: App.themeTextMuted; font.pixelSize: 11
        }
        Flickable {
            anchors { left: parent.left; leftMargin: 14; right: parent.right
                      rightMargin: 14; bottom: parent.bottom; bottomMargin: 8 }
            height: 30
            contentWidth: folderRow.width
            clip: true
            Row {
                id: folderRow
                spacing: 8
                Repeater {
                    id: folderDropRepeater
                    model: folderDropBar.targets
                    delegate: Rectangle {
                        id: fTarget
                        required property var modelData
                        width: fLabel.implicitWidth + 34 + fTarget.modelData.depth * 10
                        height: 30
                        radius: 6
                        color: fDrop.containsDrag
                               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                         App.themeAccent.b, 0.30)
                               : App.themeCard
                        border.color: fDrop.containsDrag ? App.themeAccent : App.themeBorder
                        border.width: fDrop.containsDrag ? 2 : 1

                        DrawnIcon {
                            id: fIcon
                            anchors { left: parent.left
                                      leftMargin: 8 + fTarget.modelData.depth * 10
                                      verticalCenter: parent.verticalCenter }
                            name: "folder"
                            size: 14
                            color: App.themeAccent
                        }
                        Text {
                            id: fLabel
                            anchors { left: fIcon.right; leftMargin: 6
                                      verticalCenter: parent.verticalCenter }
                            text: fTarget.modelData.name
                            color: App.themeTextPrimary; font.pixelSize: 12
                        }
                        DropArea {
                            id: fDrop
                            anchors.fill: parent
                            keys: ["text/uri-list"]
                            onDropped: function(drop) {
                                if (!drop.hasUrls) { drop.accepted = false; return }
                                shell._dropUrlsOnFolder(drop.urls,
                                                        fTarget.modelData.path)
                                drop.acceptProposedAction()
                            }
                        }
                    }
                }
            }
        }
    }

    //  Der geöffnete Ordner und jeder Ordner, der gerade in der Galerie steht -
    //  in der sichtbaren Reihenfolge, mit ihrer Tiefe.
    //  ── Welche Hälfte ist gemeint? ──────────────────────────────────────────
    //  Die Shell hat seit dem Zwei-Fenster-Modus KEIN eigenes Modell mehr. Für
    //  die Ablegeleisten zählt die Hälfte, in der der Zug BEGANN - der Zeiger
    //  wandert währenddessen über die andere, und der Fokus folgt ihm.
    property int _dragPaneIndex: 0
    Connections {
        target: App
        function onTileDragActiveChanged() {
            if (App.tileDragActive)
                shell._dragPaneIndex = Math.max(0, App.focusedPaneIndex)
        }
    }
    function _paneAt(i) {
        var list = App.panes
        return (i >= 0 && i < list.length) ? list[i] : (list.length > 0 ? list[0] : null)
    }
    //  Für eine DATEI zählt nicht der Fokus, sondern wer sie hat: nur das Modell
    //  ihres Ordners darf sie verschieben (`transferToFolder` weist alles
    //  Fremde ab). Deshalb wird die Hälfte über den Pfad gesucht.
    function _modelOwning(path) {
        var list = App.panes
        for (var i = 0; i < list.length; i++) {
            var m = list[i] ? list[i].mediaModel : null
            if (m && m.ownsFile(path)) return m
        }
        var p = shell._paneAt(shell._dragPaneIndex)
        return p ? p.mediaModel : null
    }

    function _visibleFolders() {
        var out = []
        var pane = shell._paneAt(shell._dragPaneIndex)
        if (!pane) return out
        var gm = pane.galleryModel
        if (pane.currentFolder.length > 0)
            out.push({ path: pane.currentFolder,
                       name: shell.folderName(pane.currentFolder),
                       depth: 0 })
        if (!gm) return out
        for (var i = 0; i < gm.count; ++i) {
            if (gm.mediaTypeAt(i) !== 7) continue
            out.push({ path: gm.filePathAt(i),
                       name: gm.displayNameAt(i),
                       depth: gm.depthAt(i) + 1 })
        }
        return out
    }

    //  Eine abgelegte Nutzlast in einen Ordner geben - app-intern verschieben
    //  bzw. kopieren, von aussen kopieren. Dieselbe Unterscheidung wie in der
    //  Galerie (`mediaModel.ownsFile`).
    function _dropUrlsOnFolder(urls, destFolder) {
        if (!urls || urls.length === 0 || destFolder.length === 0) return
        const src = App.localPath(urls[0])
        const owner = shell._modelOwning(src)
        if (owner && owner.ownsFile(src)) {
            //  In den EIGENEN Ordner abzulegen ist keine Bewegung.
            const cut = Math.max(src.lastIndexOf("/"), src.lastIndexOf("\\"))
            if (cut > 0 && src.substring(0, cut) === destFolder) return
            shell._dropIntoFolder(src, destFolder)
        } else {
            App.handleDroppedUrls(urls, destFolder)
        }
    }

    // ── Ablegeleiste für Lesezeichen (nur WÄHREND eines Kachel-Zuges) ────────
    //  Der Zug einer Kachel ist ein PLATTFORM-Zug; landet er im eigenen Fenster,
    //  kommt er hier als gewöhnlicher Datei-Drop an. Die Leiste erscheint nur,
    //  solange gezogen wird (`App.tileDragActive`) - ein Ziel, das keinen
    //  dauerhaften Platz kostet. Sie liegt über allem, weil ein Zug immer nur an
    //  die OBERSTE annehmende Fläche geht.
    Rectangle {
        id: bookmarkDropBar
        z: 90
        visible: App.tileDragActive && App.savedFolders.length > 0
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.bottomMargin: 28
        height: 70
        color: App.themeMenuBarBg
        border.color: App.themeAccent; border.width: 1

        Text {
            id: dropBarHint
            anchors { left: parent.left; leftMargin: 14; top: parent.top; topMargin: 6 }
            text: App.uiText(App.language,
                             App.fileDropMove ? "DropBarMove" : "DropBarCopy")
            color: App.themeTextMuted; font.pixelSize: 11
        }
        Row {
            anchors { left: parent.left; leftMargin: 14; bottom: parent.bottom; bottomMargin: 8 }
            spacing: 8
            Repeater {
                model: App.savedFolders
                delegate: Rectangle {
                    id: bmTarget
                    required property var modelData
                    width: bmLabel.implicitWidth + 24; height: 30; radius: 6
                    color: bmDrop.containsDrag
                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
                           : App.themeCard
                    border.color: bmDrop.containsDrag ? App.themeAccent : App.themeBorder
                    border.width: bmDrop.containsDrag ? 2 : 1
                    Text {
                        id: bmLabel
                        anchors.centerIn: parent
                        text: bmTarget.modelData.name
                        color: App.themeTextPrimary; font.pixelSize: 12
                    }
                    DropArea {
                        id: bmDrop
                        anchors.fill: parent
                        keys: ["text/uri-list"]
                        onDropped: function(drop) {
                            if (!drop.hasUrls) { drop.accepted = false; return }
                            shell._dropIntoFolder(App.localPath(drop.urls[0]),
                                                  bmTarget.modelData.path)
                            drop.acceptProposedAction()
                        }
                    }
                }
            }
        }
    }

    //  Eine gezogene Datei in einen Ordner ablegen - auf einem LESEZEICHEN oder
    //  auf einer ORDNERKACHEL der Galerie; beide Wege sind derselbe Vorgang.
    //  Die Regeln stehen im Modell (`transferToFolder`); hier bleibt nur die
    //  Rückfrage bei einem belegten Namen und die Meldung.
    function _dropIntoFolder(path, destFolder) {
        if (path.length === 0 || destFolder.length === 0) return
        const move = App.fileDropMove
        const owner = shell._modelOwning(path)
        if (!owner) return
        const r = owner.transferToFolder(path, destFolder, move, 0)
        if (r === 1) {
            collisionDialog.srcPath    = path
            collisionDialog.destFolder = destFolder
            collisionDialog.moveMode   = move
            collisionDialog.altName    = owner.transferTargetName(path, destFolder)
            collisionDialog.open()
            return
        }
        shell._reportTransfer(r, move, path)
    }
    function _reportTransfer(result, move, path) {
        const name = String(path).split("/").pop()
        if (result === 0)
            shell.statusText = App.uiText(App.language, move ? "DropMoved" : "DropCopied") + name
        else
            shell.statusText = App.uiText(App.language, "DropFailed") + name
        statusClearTimer.restart()
    }

    // ── Rückfrage bei belegtem Namen (Ersetzen / Umbenennen / Abbrechen) ─────
    Dialog {
        id: collisionDialog
        property string srcPath: ""
        property string destFolder: ""
        property bool   moveMode: true
        property string altName: ""
        anchors.centerIn: parent
        modal: true
        padding: 18
        background: Rectangle {
            color: App.themeCard; radius: 10
            border.color: App.themeBorder; border.width: 1
        }
        contentItem: Column {
            spacing: 10
            Text {
                text: App.uiText(App.language, "DropCollisionTitle")
                color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true
            }
            Text {
                width: 320
                text: App.uiText(App.language, "DropCollisionText") + collisionDialog.altName
                color: App.themeTextMuted; font.pixelSize: 12; wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                Repeater {
                    model: [
                        { key: "SettingsCancel",     mode: 0 },
                        { key: "DropCollisionRename", mode: 2 },
                        { key: "DropCollisionReplace", mode: 1 }
                    ]
                    delegate: Rectangle {
                        id: colBtn
                        required property var modelData
                        width: colLbl.implicitWidth + 24; height: 30; radius: 6
                        color: colHover.hovered
                               ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                         App.themeTextPrimary.b, 0.16)
                               : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                         App.themeTextPrimary.b, 0.07)
                        border.color: App.themeBorder; border.width: 1
                        Text {
                            id: colLbl
                            anchors.centerIn: parent
                            text: App.uiText(App.language, colBtn.modelData.key)
                            color: App.themeTextPrimary; font.pixelSize: 12
                        }
                        HoverHandler { id: colHover }
                        TapHandler {
                            onTapped: {
                                collisionDialog.close()
                                if (colBtn.modelData.mode === 0) return
                                const owner = shell._modelOwning(collisionDialog.srcPath)
                                if (!owner) return
                                const r = owner.transferToFolder(
                                              collisionDialog.srcPath,
                                              collisionDialog.destFolder,
                                              collisionDialog.moveMode,
                                              colBtn.modelData.mode)
                                shell._reportTransfer(r, collisionDialog.moveMode,
                                                      collisionDialog.srcPath)
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Globale PDF-Seiten-Extraktion: Auswahldialog + Rückmeldungen ─────────
    //  Dieselbe Komponente wie in der PdfSurface, nur mit allen PDFs des
    //  Ordners und Namenspflicht (requireName) - die Ausgabe landet im
    //  aktuellen Ordner, deshalb hostet die Shell den Dialog (sie kennt
    //  Overlay und Statuszeile).
    //  Erst beim Öffnen erzeugt (Muster `settingsLoader` unten): der Dialog ist
    //  mit Abstand die größte Komponente der Shell, wird aber nur gebraucht,
    //  wenn jemand wirklich Seiten aus den PDFs des Ordners zieht. Eager
    //  erzeugt kostete er gemessen einen guten Teil der Startzeit.
    Component {
        id: globalExtractComponent
        PdfPageSelectDialog {
            requireName: true
            titleText: App.uiText(App.language, "ExtractGlobalTitle")
            defaultName: ""
            onExtractRequested: (items, name) => {
                shell._extractPending = true
                shell._extractName    = name
                // items = [{path,page}] in Auswahlreihenfolge (Werkbank) bzw.
                // Originalreihenfolge (kompakt) -> extractOrdered erhält die Reihenfolge.
                PdfExtract.extractOrdered(items, shell._extractTarget(), name)
            }
        }
    }
    Loader {
        id: globalExtractLoader
        anchors.fill: parent
        active: false
        sourceComponent: globalExtractComponent
    }

    Connections {
        target: PdfExtract
        function onFolderPdfsReady(files) {
            if (!shell._scanPending) return          // Scan eines anderen Aufrufers
            shell._scanPending = false
            if (files.length === 0) {
                shell.statusText = App.uiText(App.language, "ExtractNoPdfs")
                statusClearTimer.restart()
                return
            }
            globalExtractLoader.active = true
            globalExtractLoader.item.openWith(files)
        }
        function onExtractProgress(done, total) {
            if (!shell._extractPending) return
            shell.statusText = App.uiText(App.language, "ExtractProgressToast")
                                   .arg(done).arg(total)
            statusClearTimer.restart()
        }
        function onExtractFinished(ok, targetPath, errorText) {
            if (!shell._extractPending) return
            shell._extractPending = false
            // ERST aktualisieren, DANN die eigene Meldung setzen: der Refresh
            // emittiert selbst eine statusMessage („Aktualisiert"), die sonst
            // unsere Erfolgsmeldung sofort wieder überschriebe.
            if (ok) App.refreshCurrentFolder()
            shell.statusText = ok
                ? App.uiText(App.language, "ExtractOkToast")
                      .arg(String(targetPath).split("/").pop())
                : App.uiText(App.language, "ExtractFailToast")
            statusClearTimer.restart()
        }
    }

    //  ── Meldungen: eingeblendeter TOAST statt Statusleiste ───────────────────
    //  Der frühere `footer` (24-px-Streifen) ist entfernt - er stand in JEDER
    //  Ansicht und kostete überall Fläche, im Vollbild lag er quer unter dem
    //  Video. `statusText` blieb als Sammelstelle bestehen; alle Melder
    //  (globale PDF-Extraktion, „max. 4 Dateien", Ordnerwechsel,
    //  `App.statusMessage`) schreiben unverändert dorthin. Angezeigt wird das
    //  jetzt wie in den Kacheln: ein Toast, der sich selbst ausblendet und
    //  KEINE Fläche kostet (Overlay, klickdurchlässig).
    Timer { id: statusClearTimer; interval: 4000; onTriggered: shell.statusText = "" }

    Rectangle {
        id: statusToast
        parent: Overlay.overlay
        z: 9999
        //  Unten mittig, mit Abstand zum Rand - im Vollbild wie in der Galerie.
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
        anchors.bottom: parent ? parent.bottom : undefined
        anchors.bottomMargin: 28
        width: Math.min(parent ? parent.width - 48 : 400, toastLabel.implicitWidth + 32)
        height: toastLabel.implicitHeight + 18
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.82)
        border.color: Qt.rgba(1, 1, 1, 0.18)
        visible: opacity > 0.01
        opacity: shell.statusText.length > 0 ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Text {
            id: toastLabel
            anchors.centerIn: parent
            width: Math.min(parent.width - 24, implicitWidth)
            text: shell.statusText
            color: "#ffffff"
            font.pixelSize: 12
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Connections {
        target: App
        function onStatusMessage(text) { shell.statusText = text; statusClearTimer.restart() }
        //  Offene Kacheln räumt die Hälfte selbst ab (sie kennt ihren Ordner).
        function onFolderOpened(path)  { shell.statusText = path; statusClearTimer.restart() }
    }

    //  Meldungen der Wiedergabe: Fehler der Kette und der Verlauf des
    //  Ton-Sicherns. Sie gehen hier auf denselben Toast wie alles andere -
    //  vorher fielen sie ins Leere, weil ihnen niemand zuhörte.
    Connections {
        target: Audio
        function onMessage(text) {
            if (!text || text.length === 0) return
            shell.statusText = text
            statusClearTimer.restart()
        }
    }


    // ── Lesezeichen anlegen/bearbeiten (geteilt mit SettingsBookmarksTab) ──────

    // ── Kachelgrößen-Dialog (Phase 4) ─────────────────────────────────────────
    //  Ebenfalls erst beim Öffnen - er erscheint über einen Menüeintrag.
    Component { id: tileSizeComponent; TileSizeDialog {} }
    Loader {
        id: tileSizeLoader
        active: false
        sourceComponent: tileSizeComponent
    }

    //  „Welche Tonspur?" - erscheint nur, wenn die Datei mehr als eine hat.
    //  Er gehört der SHELL: `Audio` ist ein Singleton, je Hälfte gehostet
    //  gingen bei zwei Hälften zwei Fenster gleichzeitig auf.
    //  Erscheint nur, wenn eine Datei mehr als eine Tonspur hat - also selten.
    Component { id: audioTrackComponent; AudioTrackDialog {} }
    Loader {
        id: audioTrackLoader
        active: false
        sourceComponent: audioTrackComponent
    }
    Connections {
        target: Audio
        function onTrackChoiceNeeded(source, tracks) {
            audioTrackLoader.active = true
            audioTrackLoader.item.openFor(source, tracks)
        }
    }

    // ── Einstellungs-Dialog (Phase 4) ─────────────────────────────────────────
    // Loader-gated: erst beim Öffnen instanziiert, beim Schließen wieder
    // freigegeben (RAM-Priorität - der Dialog mit acht Tabs lebt nicht dauerhaft).
    Component {
        id: settingsComponent
        SettingsDialog {}
    }
    Loader {
        id: settingsLoader
        active: false
        sourceComponent: settingsComponent
        onLoaded: item.open()
        Connections {
            target: settingsLoader.item
            ignoreUnknownSignals: true
            function onClosed() { settingsLoader.active = false }
        }
    }
    function openSettings() {
        if (settingsLoader.active && settingsLoader.item)
            settingsLoader.item.open()
        else
            settingsLoader.active = true   // -> onLoaded öffnet den Dialog
    }
}
