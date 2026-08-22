pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"
import "../settings"
import "../tags"
import "../viewer"

// ─────────────────────────────────────────────────────────────────────────────
//  GalleryPane.qml - EINE vollständige Hälfte des Hauptfensters: eigene Menüs
//  (Datei/Ordner), Filterleiste, Kachelraster, Tag-Panel UND eigene
//  Vollbild-Ansicht (bis zu zwei Dateien nebeneinander, bei nur einer Hälfte
//  bis zu vier).
//
//  ERZEUGT WIRD SIE VON `PaneHost` (src/app/PaneHost.h) mit einem EIGENEN
//  QML-Kontext: `mediaModel`, `galleryModel` und `Tags` zeigen darin auf die
//  Objekte DIESER Hälfte, `Pane` ist ihr `PaneController`. Deshalb steht in
//  dieser Datei - und in allem, was sie erzeugt - nirgends „Hälfte A oder B".
//
//  WAS HIER LIEGT: alles, was zu EINEM offenen Ordner gehört, samt den Menüs
//  „Datei" und „Ordner" (Festlegung des Nutzers: appweit sind nur „Ansicht" und
//  „Einstellungen", die stehen in der Leiste ganz oben).
//  WAS NICHT: Fenster-Vollbild, Ablegeleisten und die Dialoge, die das ganze
//  Fenster brauchen - die hostet die `ApplicationShell`.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: pane
    objectName: "galleryPane"      // Griff für tests/bench (Regel 31)

    // ── Von der Shell gesetzt ───────────────────────────────────────────────
    property bool showClose: false      // zwei Hälften ⇒ „Schließen" statt „Beenden"
    property bool paneFocused: true     // hat diese Hälfte den Fokus?
    property bool splitActive: false    // gibt es überhaupt zwei Hälften?
    property int  paneIndex: 0         // Platz in `App.panes` (Shell setzt ihn)
    property bool immersive: false      // Fenster-Vollbild (F) - gehört der Shell
    //  Pfad der Datei in der aktiven Kachel dieser Hälfte (für den Fenstertitel).
    property string activeFilePath: ""
    //  Die aktive Vollbild-Kachel selbst - für die Übergabe an den Player-Modus
    //  (Stelle der Wiedergabe, Anhalten). Nur gesetzt, solange eine offen ist.
    property var    activeViewer: null

    // ── Meldungen nach oben ─────────────────────────────────────────────────
    signal extractRequested(string folder)     // „PDF-Seiten…" (Dialog hostet die Shell)
    signal folderDropRequested(string sourcePath, string folderPath)
    signal externalDropRequested(var urls, string folderPath)
    signal statusRequested(string text)
    signal focusRequested()                    // diese Hälfte ist jetzt gemeint
    signal closeRequested()                    // Datei ▸ Schließen (nur bei zwei Hälften)
    signal immersiveToggleRequested()
    //  Zug an der eigenen Leiste: die Shell entscheidet, ob getauscht wird
    //  (losgelassen über der anderen Hälfte). `x` ist eine SZENEN-Koordinate.
    signal barDragMoved(real x)
    signal barDragReleased(real x)

    // ── Player-Modus (Alt+A) ────────────────────────────────────────────────
    //  Er filtert die Galerie auf Audio (und, wenn eingestellt, Video) und
    //  schaltet das Öffnen um: ein Doppelklick SPIELT, statt die Vollbild-
    //  Ansicht zu öffnen. Die Leiste erscheint erst, wenn wirklich etwas läuft.
    property bool playerMode: false
    //  Der Filterzustand VOR dem Umschalten - beim Verlassen kommt er zurück.
    property var  _savedFilter: null

    //  Besitzt DIESE Hälfte die laufende Wiedergabe? Nur dann gehören ihr die
    //  Leiste und die große Ansicht - sonst stünde der Player in beiden Galerien.
    readonly property bool playerMine: Audio.owner === PaneCtl

    function togglePlayerMode() { pane.setPlayerMode(!pane.playerMode) }
    function setPlayerMode(on) {
        if (pane.playerMode === on) return
        if (on) {
            pane._savedFilter = { images: galleryModel.showImages,
                                  videos: galleryModel.showVideos,
                                  audio:  galleryModel.showAudio,
                                  pdfs:   galleryModel.showPdfs,
                                  texts:  galleryModel.showTexts }
            //  Die WEISSE Liste entscheidet (`MediaProxyModel::isPlayableType`) -
            //  die Häkchen darunter bleiben trotzdem stimmig, damit die
            //  Medien-Rubrik der Filterleiste zeigt, was wirklich zu sehen ist.
            //  Ohne die weisse Liste lief alles Unbekannte (ZIP, XLSX, OTH …) an
            //  jedem Häkchen vorbei.
            galleryModel.onlyPlayable = true
            galleryModel.showImages = false
            galleryModel.showPdfs   = false
            galleryModel.showTexts  = false
            galleryModel.showAudio  = true
            galleryModel.showVideos = Audio.showVideos
            pane.playerMode = true
            PaneCtl.playerMode = true            // überlebt das Neubauen der Hälfte
            Audio.playerModeRemembered = true    // und den Programmstart
            //  Läuft in der ANDEREN Hälfte gerade etwas, wird es ihr nicht
            //  weggenommen. Liegt dagegen nur ein Titel bereit (wiederhergestellt
            //  oder pausiert), übernimmt diese Hälfte ihn - genau das erwartet
            //  man, wenn man mitten in der Wiedergabe Alt+A drückt.
            if (!Audio.owner || !Audio.active) Audio.owner = PaneCtl
            pane._pushQueue()
            return
        }

        //  Verlassen: die Wiedergabe dieser Hälfte endet mit dem Modus - sonst
        //  bliebe eine Kachel markiert und ein unsichtbarer Titel liefe weiter.
        if (pane.playerMine) {
            Audio.stop()
            Audio.owner = null
        }
        PaneCtl.playerViewOpen = false
        if (pane._playerPage && paneStack.depth > 1) paneStack.pop()
        //  Filter zurück. Ist nichts gemerkt (Modus überlebte einen Ordner-
        //  wechsel oder wurde von außen gesetzt), gilt der Grundzustand: ALLES
        //  an - ein Rest-Filter „nur Audio" wäre die schlimmere Überraschung.
        //  Gemerkt wird der Zustand VOR dem Modus. Ist keiner da - oder war er
        //  selbst schon „nur Ton" (etwa weil der Modus einen Neustart oder eine
        //  Teilung überlebt hat) -, gilt der Grundzustand ALLES AN: mit einem
        //  Rest-Filter „nur Audio" stünde man sonst vor einer halbleeren
        //  Galerie und wüsste nicht, warum.
        galleryModel.onlyPlayable = false
        const f = pane._savedFilter
        const meaningful = f && (f.images || f.pdfs || f.texts)
        galleryModel.showImages = meaningful ? f.images : true
        galleryModel.showVideos = meaningful ? f.videos : true
        galleryModel.showAudio  = meaningful ? f.audio  : true
        galleryModel.showPdfs   = meaningful ? f.pdfs   : true
        galleryModel.showTexts  = meaningful ? f.texts  : true
        pane._savedFilter = null
        pane.playerMode = false
        PaneCtl.playerMode = false
        Audio.playerModeRemembered = false
    }

    //  Beim Aufbau der Hälfte: War sie im Player-Modus (Teilung/Zusammenlegen
    //  baut die Hälften neu), oder war es beim letzten Beenden die erste Hälfte?
    //  Dann geht es dort weiter, wo aufgehört wurde.
    Component.onCompleted: {
        if (PaneCtl.playerMode) {
            //  Der Filter steht schon auf Ton (die Modelle sind C++-seitig und
            //  haben das Neubauen überlebt) - nur der QML-Zustand fehlt.
            pane.playerMode = true
            if (!Audio.owner) Audio.owner = PaneCtl
            pane._pushQueue()
            //  Lag die große Ansicht oben, kommt sie zurück - sonst stünde man
            //  nach dem Teilen unvermittelt in der Galerie.
            if (PaneCtl.playerViewOpen) pane.openPlayerView()
        } else if (Audio.takePlayerModeRestore()) {
            //  Gilt EINMAL je Programmlauf (s. AudioController): die erste
            //  Hälfte nimmt den Modus auf, eine später hinzugefügte nicht.
            pane.setPlayerMode(true)
        }
    }

    //  Alt+A aus einer OFFENEN DATEI heraus: zurück zur Galerie, in den
    //  Player-Modus - und war es eine Audiodatei, läuft sie dort weiter, an der
    //  Stelle, an der sie stand (Festlegung des Nutzers). Die Wiedergabe der
    //  Kachel läuft über `MediaPlayer`, die des Player-Modus über die eigene
    //  Kette; „weiterlaufen" heißt deshalb: dort anhalten, hier aufnehmen.
    function enterPlayerFromViewer() {
        const path = pane.activeFilePath
        var pos = -1
        var wasRunning = false
        if (pane.activeViewer) {
            pos = pane.activeViewer.mediaPositionMs()
            wasRunning = pane.activeViewer.mediaRunning()
            pane.activeViewer.pauseMedia()
        }
        //  Typ NOCH VOR dem Umschalten bestimmen: danach filtert die Galerie.
        const row = path.length > 0 ? galleryModel.rowForPath(path) : -1
        const type = row >= 0 ? galleryModel.mediaTypeAt(row) : -1
        const isSound = type === 2 || (Audio.showVideos && type === 1)

        pane.popFullscreen()
        pane.setPlayerMode(true)
        if (!isSound || path.length === 0) return

        pane.playHere(path)
        if (pos > 0) Audio.seek(pos)
        if (!wasRunning) Audio.togglePlay()      // stand still ⇒ still bleiben
    }

    //  Einen Titel dieser Hälfte anspielen (Klick, Doppelklick, große Ansicht).
    function playHere(filePath) {
        Audio.owner = PaneCtl
        if (Audio.currentPath !== filePath)
            Audio.playFile(filePath, pane._visibleAudioPaths())
    }

    // ── Große Player-Ansicht (Doppelklick oder Klick auf die Leiste) ────────
    property var _playerPage: null
    function openPlayerView() {
        if (!pane.playerMode) return
        if (paneStack.depth > 1) return              // Vollbild liegt schon oben
        if (!pane._playerPage)
            pane._playerPage = playerViewComponent.createObject(pane)
        if (pane._playerPage) {
            paneStack.push(pane._playerPage)
            PaneCtl.playerViewOpen = true      // überlebt das Neubauen der Hälfte
        }
    }

    //  Die SICHTBARE Liste IST die Warteschlange - Filter und Sortierung der
    //  Galerie bestimmen damit auch die Reihenfolge der Wiedergabe.
    function _visibleAudioPaths() {
        var out = []
        for (var i = 0; i < galleryModel.count; ++i) {
            var t = galleryModel.mediaTypeAt(i)     // 1 = Video, 2 = Audio
            if (t === 2 || (Audio.showVideos && t === 1))
                out.push(galleryModel.filePathAt(i))
        }
        return out
    }
    function _pushQueue() {
        //  Nur der Besitzer schreibt die Warteschlange - sonst risse die andere
        //  Hälfte mit ihrem Filter die laufende Liste an sich.
        if (!pane.playerMode || !(pane.playerMine || !Audio.active)) return
        const list = pane._visibleAudioPaths()
        //  Eine LEERE Liste wird nie eingespielt: beim Start ist der Ordner noch
        //  nicht gelesen, und der wiederhergestellte Titel stünde danach allein
        //  da (Nutzerbefund, zweimal). Zu spielen gäbe es aus einer leeren Liste
        //  ohnehin nichts.
        if (list.length === 0 && Audio.queue.length > 0) return
        Audio.setQueue(list)
    }

    //  Ordnerwechsel ⇒ die Liste dieses Ordners ist zu Ende (Festlegung des
    //  Nutzers). Läuft der Player in DIESER Hälfte, hört er auf.
    Connections {
        target: PaneCtl
        function onFolderOpened(path) {
            if (!pane.playerMode) return
            //  Der alte Titel gehört zum alten Ordner: anhalten UND wegräumen,
            //  sonst bliebe die Leiste mit einem Titel stehen, den man von hier
            //  aus gar nicht mehr bedienen kann.
            if (pane.playerMine) Audio.stopAndClear()
            //  Lag etwas ÜBER der Galerie (große Player-Ansicht oder eine offene
            //  Datei), gehört es zum alten Ordner: zurück zur Galerie. Der
            //  Player-MODUS bleibt dabei an (Festlegung des Nutzers) - nur der
            //  Titel ist weg, die neue Liste steht bereit.
            if (paneStack.depth > 1) {
                PaneCtl.playerViewOpen = false
                pane.popFullscreen()
            }
            pane._pushQueue()
        }
    }
    //  „Videos mitzeigen" umgestellt, WÄHREND der Modus läuft: der Filter zieht
    //  sofort nach, sonst müsste man Alt+A zweimal drücken.
    Connections {
        target: Audio
        function onOptionsChanged() {
            if (pane.playerMode) galleryModel.showVideos = Audio.showVideos
        }
    }
    //  Filter oder Sortierung geändert ⇒ neue Warteschlange; der laufende Titel
    //  bleibt dabei stehen (`PlayQueue::setItems`).
    Connections {
        target: galleryModel
        function onFilterChanged() { pane._pushQueue() }
        function onSortChanged()   { pane._pushQueue() }
        //  Beim Start steht der Ordner noch nicht, wenn die Hälfte den
        //  Player-Modus aufnimmt - die Warteschlange bestünde sonst nur aus dem
        //  wiederhergestellten Titel und wäre nach ihm zu Ende (Nutzerbefund).
        function onCountChanged()  { pane._pushQueue() }
    }

    //  Zeigt diese Hälfte gerade ihre Galerie (nicht die Vollbild-Ansicht)?
    readonly property bool galleryActive: paneStack.depth === 1
    //  Tastenkürzel greifen nur in der FOKUSSIERTEN Hälfte und nur auf der
    //  Galerie-Seite - sonst lüde ein „R" beide Ordner neu.
    readonly property bool _keysLive: pane.galleryActive && pane.paneFocused
    //  Liegt die große Player-Ansicht oben?
    readonly property bool playerPageActive: paneStack.depth > 1
                                             && pane._playerPage !== null
                                             && paneStack.currentItem === pane._playerPage
    //  Die Audio-Tasten (Leertaste, Pfeile) gelten AUCH dort - sie gehören zur
    //  Wiedergabe, nicht zur Galerie.
    readonly property bool _audioKeysLive: pane.paneFocused && pane.playerMode
                                           && pane.playerMine
                                           && (pane.galleryActive || pane.playerPageActive)

    //  Griffe für die Shell (sie kennt die Kinder dieser Datei nicht).
    function focusGallery()     { if (paneStack.currentItem) paneStack.currentItem.forceActiveFocus() }
    function openFolderDialog() { folderDialog.open() }
    //  Bei EINER Hälfte stehen „Datei" und „Ordner" oben in der großen Leiste
    //  (wie vor dem Zwei-Fenster-Modus) - sie klappt die Menüs DIESER Hälfte
    //  auf, statt sie ein zweites Mal zu bauen.
    function popupFileMenu(anchor)   { fileMenu.popup(anchor, 0, anchor.height + 3) }
    function popupFolderMenu(anchor) { bookmarksMenu.popup(anchor, 0, anchor.height + 3) }
    function fileMenuOpen()   { return fileMenu.opened }
    function folderMenuOpen() { return bookmarksMenu.opened }
    function closeMenus()     { fileMenu.close(); bookmarksMenu.close() }
    function _folderName(path) {
        if (!path) return ""
        var s = String(path)
        var cut = Math.max(s.lastIndexOf("/"), s.lastIndexOf("\\"))
        return cut >= 0 ? s.slice(cut + 1) : s
    }

    //  Der Zeiger bestimmt, welche Hälfte gemeint ist: wer über einer Hälfte
    //  steht, arbeitet in ihr. Ein Klick tut dasselbe (der Zeiger ist ja dort),
    //  ohne dass ein Fänger den Kindern ihre Ereignisse wegnimmt.
    HoverHandler {
        onHoveredChanged: if (hovered) pane.focusRequested()
    }

    //  ── Vorbefüllung für „Ordner hinzufügen" ────────────────────────────────
    //  Der geöffnete Ordner, sofern er noch NICHT gespeichert ist - sonst "".
    function _normalizedFolderPath(p) {
        if (!p) return ""
        var s = String(p)
        while (s.length > 1 && (s.endsWith("/") || s.endsWith("\\"))) s = s.slice(0, -1)
        return s
    }
    function _bookmarkPrefillPath() {
        var cur = pane._normalizedFolderPath(PaneCtl.currentFolder)
        if (cur.length === 0) return ""
        var list = App.savedFolders
        for (var i = 0; i < list.length; i++)
            if (pane._normalizedFolderPath(list[i].path) === cur) return ""
        return cur
    }

    // ── Menüleiste der Hälfte: Datei + Ordner ───────────────────────────────
    Rectangle {
        id: paneMenuStrip
        anchors { left: parent.left; right: parent.right; top: parent.top }
        //  Nur im ZWEI-Fenster-Modus: bei einer Hälfte stehen „Datei" und
        //  „Ordner" oben in der großen Leiste, alle vier Menüs in EINER Zeile
        //  wie vor dem Umbau. Im Vollbild einer Datei ist die Leiste ohnehin
        //  weg - die Kachel bringt ihre eigene Kopfzeile mit.
        visible: pane.splitActive && pane.galleryActive && !pane.immersive
        height: visible ? 28 : 0
        color: App.themeMenuBarBg
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                    color: App.themeBorder }
        //  Nur bei zwei Hälften: ein Strich in Akzentfarbe zeigt, welche gemeint
        //  ist. Bei einer Hälfte wäre die Markierung sinnlos.
        Rectangle {
            visible: pane.splitActive && pane.paneFocused
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 2
            color: App.themeAccent
        }

        component MenuBtn: Rectangle {
            property string label: ""
            property var menu: null
            width: mbLbl.implicitWidth + 20; height: 22; radius: 5
            anchors.verticalCenter: parent.verticalCenter
            color: (mbHover.hovered || (menu && menu.opened)) ? App.themeCard : "transparent"
            Text { id: mbLbl; anchors.centerIn: parent; text: parent.label
                   color: App.themeTextPrimary; font.pixelSize: 12 }
            HoverHandler { id: mbHover }
            TapHandler {
                onTapped: {
                    pane.focusRequested()
                    if (menu.opened) menu.close()
                    else menu.popup(parent, 0, parent.height + 3)
                }
            }
        }

        //  Auch hier blätterbar (Strg + Rad), damit in einer schmalen Hälfte
        //  kein Knopf unerreichbar wird.
        ScrollableBar {
            anchors { left: parent.left; leftMargin: 6; right: folderLbl.left; rightMargin: 8
                      verticalCenter: parent.verticalCenter }
            height: parent.height
            spacing: 2
            MenuBtn { label: App.menuFileText;      menu: fileMenu }
            MenuBtn { label: App.menuBookmarksText; menu: bookmarksMenu }
        }

        //  ── Die Leiste ist der GRIFF der Hälfte ─────────────────────────────
        //  Sie an die andere Seite ziehen tauscht die beiden Hälften. Der
        //  Fänger liegt hinter den Menüknöpfen (sie melden sich zuerst), zieht
        //  also nur dort, wo nichts anderes zuständig ist.
        DragHandler {
            id: barDrag
            target: null
            yAxis.enabled: false
            cursorShape: Qt.ClosedHandCursor
            enabled: pane.splitActive
            onCentroidChanged: if (active) pane.barDragMoved(centroid.scenePosition.x)
            onActiveChanged: if (!active) pane.barDragReleased(centroid.scenePosition.x)
        }

        //  Der Name des offenen Ordners rechts in der Leiste - bei zwei Hälften
        //  ist sonst nicht zu sehen, wo man ist.
        Text {
            id: folderLbl
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            width: Math.max(0, parent.width * 0.4)
            horizontalAlignment: Text.AlignRight
            text: pane._folderName(PaneCtl.currentFolder)
            color: App.themeTextMuted
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }

        // ── Datei ────────────────────────────────────────────────────────────
        //  „Beenden" wird bei zwei Hälften zu „Schließen" und schließt GENAU
        //  diese Hälfte (Festlegung des Nutzers) - so bleibt der Knopf beisammen,
        //  statt die Menüs zu zerstreuen.
        ThemedMenu {
            id: fileMenu
            MenuItem { text: App.menuOpenFolderText; onTriggered: folderDialog.open() }
            MenuItem {
                text: App.menuRefreshText
                enabled: PaneCtl.currentFolder.length > 0
                onTriggered: PaneCtl.refreshCurrentFolder()
            }
            MenuSeparator {}
            MenuItem {
                text: pane.showClose ? App.uiText(App.language, "MenuClosePane")
                                     : App.menuQuitText
                onTriggered: {
                    if (pane.showClose) pane.closeRequested()
                    else                Qt.quit()
                }
            }
        }

    ThemedMenu {
        id: bookmarksMenu
        objectName: "bookmarksMenu"      // Griff für tests/bench (Regel 31)

        MenuItem {
            text: App.bookmarkAddText
            // Öffnet denselben Hinzufügen-Dialog wie Einstellungen ▸ Lesezeichen
            // (Anzeigename + Pfad + Durchsuchen). Ist gerade ein Ordner geöffnet
            // und noch NICHT in den gespeicherten Ordnern, wird sein Pfad
            // vorbefüllt (case-sensitiver Vergleich, Trailing-Separatoren
            // normalisiert) - er bleibt vor dem Bestätigen frei änderbar.
            // Ohne offenen (oder mit bereits gespeichertem) Ordner öffnet der
            // Dialog wie bisher leer.
            onTriggered: bookmarkEditDialog.openAdd(pane._bookmarkPrefillPath())
        }
        MenuSeparator {
            id: bookmarksSeparator
            visible: App.savedFolders.length > 0
        }
        MenuItem {
            id: bookmarksEmpty
            text: App.menuBookmarksEmptyText
            enabled: false
            visible: App.savedFolders.length === 0
            //  Ein unsichtbarer `MenuItem` behält im Menü-ListView seine
            //  Höhe -> zwischen „Ordner hinzufügen" und dem ersten Lesezeichen
            //  klaffte eine leere Zeile. Höhe explizit auf 0 klemmen.
            height: visible ? implicitHeight : 0
        }

        // Vorlage für dynamisch erzeugte Lesezeichen-Einträge.
        // Instantiator + insertItem() ist in Qt6 defekt (QTBUG-69922) ->
        // Items werden manuell per rebuildBookmarks() erzeugt und verwaltet.
        Component {
            id: bookmarkItemComponent
            MenuItem {
                property string bookmarkPath: ""
                onTriggered: PaneCtl.openFolder(bookmarkPath)
            }
        }

        //  Kopfzeile einer Lesezeichen-Gruppe. BEWUSST KEIN `MenuItem`:
        //  ein Menüeintrag schließt beim Auslösen das Menü - Auf- und
        //  Zuklappen soll das Menü aber offen lassen. Ein gewöhnliches
        //  `Item` im Menü-ListView tut genau das.
        Component {
            id: bookmarkGroupComponent
            Item {
                id: groupRow
                property string groupName: ""
                property bool   collapsed: false
                property int    itemCount: 0

                implicitWidth: Math.max(200, groupLabel.implicitWidth + 64)
                implicitHeight: 28
                width: parent ? parent.width : implicitWidth

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    radius: 4
                    color: groupHover.hovered
                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.14)
                           : "transparent"
                }
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    spacing: 6
                    DrawnIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: groupRow.collapsed ? "chevron-right" : "chevron-down"
                        size: 12
                        color: App.themeTextMuted
                    }
                    Text {
                        id: groupLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: groupRow.groupName
                        color: App.themeTextPrimary
                        font.pixelSize: 13; font.bold: true
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: groupRow.itemCount
                        color: App.themeTextMuted
                        font.pixelSize: 11
                    }
                }
                HoverHandler { id: groupHover }
                TapHandler {
                    onTapped: App.setBookmarkGroupCollapsed(groupRow.groupName,
                                                            !groupRow.collapsed)
                }
            }
        }

        // Aktuell aktive dynamische Items (zum sauberen Entfernen beim Rebuild).
        property var dynamicBookmarkItems: []

        //  Baut aus `App.bookmarkTree` auf: erst die Lesezeichen ohne
        //  Gruppe, danach je Gruppe eine Kopfzeile und - solange sie
        //  aufgeklappt ist - ihre eingerückten Einträge.
        function rebuildBookmarks() {
            // Alte dynamische Items entfernen
            for (var i = 0; i < dynamicBookmarkItems.length; i++)
                bookmarksMenu.removeItem(dynamicBookmarkItems[i])
            dynamicBookmarkItems = []

            var tree = App.bookmarkTree
            for (var s = 0; s < tree.length; s++) {
                var sec = tree[s]
                var isGroup = sec.group.length > 0
                if (isGroup) {
                    var head = bookmarkGroupComponent.createObject(bookmarksMenu, {
                        groupName: sec.group,
                        collapsed: sec.collapsed,
                        itemCount: sec.items.length
                    })
                    bookmarksMenu.addItem(head)
                    dynamicBookmarkItems.push(head)
                    if (sec.collapsed) continue
                }
                for (var j = 0; j < sec.items.length; j++) {
                    var item = bookmarkItemComponent.createObject(bookmarksMenu, {
                        text:         sec.items[j].name,
                        bookmarkPath: sec.items[j].path,
                        //  Eingerückt, damit ein Eintrag sichtbar zu seiner
                        //  Gruppe gehört (Menüs kennen keine Einzüge).
                        leftPadding:  isGroup ? 30 : 10
                    })
                    bookmarksMenu.addItem(item)
                    dynamicBookmarkItems.push(item)
                }
            }
        }

        Component.onCompleted: rebuildBookmarks()

        Connections {
            target: App
            function onSavedFoldersChanged() { bookmarksMenu.rebuildBookmarks() }
        }
    }
    }

    // ── Galerie ⇄ Vollbild dieser Hälfte ────────────────────────────────────
    StackView {
        id: paneStack
        anchors { left: parent.left; right: parent.right
                  top: paneMenuStrip.bottom
                  bottom: playerBar.visible ? playerBar.top : parent.bottom }
        initialItem: galleryComponent

        //  Übergangsstil aus den Einstellungen; bewusst nur GPU-günstige
        //  Transforms (x/scale/opacity) - kein Relayout während der Animation.
        readonly property bool _txSlide: App.pageTransition === "slide"
        readonly property int  _txDur:   240

        pushEnter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: paneStack._txSlide ? 1 : 0;             to: 1; duration: paneStack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "x";       from: paneStack._txSlide ? paneStack.width : 0; to: 0; duration: paneStack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale";   from: paneStack._txSlide ? 1 : 0.97;           to: 1; duration: paneStack._txDur; easing.type: Easing.OutCubic }
            }
        }
        pushExit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x";       from: 0; to: paneStack._txSlide ? -paneStack.width * 0.22 : 0; duration: paneStack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "opacity"; from: 1; to: paneStack._txSlide ? 1 : 0;                        duration: paneStack._txDur; easing.type: Easing.InCubic }
            }
        }
        popEnter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x";       from: paneStack._txSlide ? -paneStack.width * 0.22 : 0; to: 0; duration: paneStack._txDur; easing.type: Easing.OutCubic }
                NumberAnimation { property: "opacity"; from: paneStack._txSlide ? 1 : 0;                        to: 1; duration: paneStack._txDur; easing.type: Easing.OutCubic }
            }
        }
        popExit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x";       from: 0; to: paneStack._txSlide ? paneStack.width : 0; duration: paneStack._txDur; easing.type: Easing.InCubic }
                NumberAnimation { property: "opacity"; from: 1; to: paneStack._txSlide ? 1 : 0;                duration: paneStack._txDur; easing.type: Easing.InCubic }
                NumberAnimation { property: "scale";   from: 1; to: paneStack._txSlide ? 1 : 0.97;             duration: paneStack._txDur; easing.type: Easing.InCubic }
            }
        }
    }

    Component {
        id: galleryComponent
        Item {
            id: galleryPage

            //  Tastatur-Fokus an die Galerie, sobald die Seite aktiv ist - sonst
            //  scrollen ihre Pfeiltasten nie. Ein blosses `focus: true` an der
            //  GalleryView reicht NICHT: gemessen bleibt sie damit auf
            //  `activeFocus = false`, der Fokus liegt beim Wurzelelement. Gleiche
            //  Lehre wie bei der geteilten Ansicht (`_focusActivePane`).
            StackView.onActivated: galleryView.forceActiveFocus()
            Component.onCompleted: if (StackView.status === StackView.Active)
                                       galleryView.forceActiveFocus()

            // ── Tastenkürzel (nur auf der Galerie-Seite, nicht im Vollbild) ──
            //  Alt+S = Optionen umschalten (einheitlich mit dem Media Viewer),
            //  R = Ordner/Vorschau neu laden, B = Vorschau-Sperre (blockieren ⇄
            //  neu laden). Einzeltasten werden von fokussierten Textfeldern via
            //  Shortcut-Override unterdrückt.
            Shortcut {
                sequence: "Alt+S"; enabled: pane._keysLive
                //  Nur DIESE Hälfte: mit zwei Galerien nebeneinander soll der
                //  Modus dort gelten, wo man ihn eingeschaltet hat.
                onActivated: PaneCtl.optionsVisible = !PaneCtl.optionsVisible
            }
            Shortcut {
                sequence: "R"; enabled: pane._keysLive
                onActivated: PaneCtl.refreshCurrentFolder()
            }
            //  F5 = neu laden (Standard-Alias zu „R"); Ctrl+O = Ordner öffnen.
            //  Beide nur auf der Galerie-Seite (nur wenn die Galerie sichtbar ist).
            Shortcut {
                sequence: "F5"; enabled: pane._keysLive
                onActivated: PaneCtl.refreshCurrentFolder()
            }
            Shortcut {
                sequence: "Ctrl+O"; enabled: pane._keysLive
                onActivated: folderDialog.open()
            }
            //  ── Alt+<- : zurueck aus einem Unterordner ───────────────────────
            //  Nur der Rueckweg, kein Vorwaerts: hinein fuehrt der Doppelklick
            //  auf die Ordnerkachel. Auf den Stapel kommt ausschliesslich ein
            //  Abstieg (App.openSubfolder) - ein Lesezeichen oder ein Drop
            //  verlaesst den Baum und leert ihn.
            Shortcut {
                sequence: "Alt+Left"
                enabled: pane._keysLive && PaneCtl.canNavigateBack
                onActivated: PaneCtl.navigateBack()
            }
            //  F schaltet das immersive Vollbild auch AUF DER GALERIE um - bis
            //  jetzt ging das nur mit einer offenen Datei, obwohl gerade die
            //  Galerie von der zusätzlichen Fläche profitiert.
            Shortcut {
                sequence: "F"; enabled: pane._keysLive
                onActivated: {
                    if (galleryView._editableTextFocused()) return
                    pane.immersiveToggleRequested()
                }
            }
            //  Strg+F springt ins Suchfeld der Filterleiste (dieselbe Taste wie
            //  die Suche in der PDF-Ansicht - dort gehört sie der Kachel).
            Shortcut {
                sequence: "Ctrl+F"; enabled: pane._keysLive
                onActivated: filterBar.focusSearch()
            }
            //  Alt+A schaltet den Player-Modus dieser Hälfte um - auch aus einer
            //  offenen Datei heraus (s. `enterPlayerFromViewer`).
            Shortcut {
                sequence: "Alt+A"
                enabled: pane.paneFocused
                onActivated: pane.galleryActive || pane.playerPageActive
                             ? pane.togglePlayerMode()
                             : pane.enterPlayerFromViewer()
            }
            //  Leertaste = Start/Pause, Pfeile = Titel wechseln - aber NUR im
            //  Player-Modus und nicht, während jemand tippt (dort gehört die
            //  Leertaste dem Text; gleiche Prüfung wie bei Strg+Z).
            Shortcut {
                sequence: "Space"
                enabled: pane._audioKeysLive
                onActivated: if (!galleryView._editableTextFocused()) Audio.togglePlay()
            }
            Shortcut {
                sequence: "Right"
                enabled: pane._audioKeysLive && Audio.hasTrack
                onActivated: if (!galleryView._editableTextFocused()) Audio.next()
            }
            Shortcut {
                sequence: "Left"
                enabled: pane._audioKeysLive && Audio.hasTrack
                onActivated: if (!galleryView._editableTextFocused()) Audio.previous()
            }
            //  ── Rückgängig / Wiederholen für DATEI-Vorgänge ─────────────────
            //  Gilt NUR auf der Galerie-Seite: in den Editoren gehört Strg+Z dem
            //  Dokument. Und nicht, während jemand in einem Feld tippt - dort
            //  gehört die Taste dem Text (dieselbe Prüfung wie die Pfeiltasten
            //  der Galerie, deshalb ihre Funktion und keine zweite Kopie).
            Shortcut {
                sequence: StandardKey.Undo; enabled: pane._keysLive
                onActivated: {
                    if (galleryView._editableTextFocused()) return
                    const name = mediaModel.undoFileOpName()
                    pane.statusRequested(mediaModel.undoFileOp()
                        ? App.uiText(App.language, "FileUndoRestored") + name
                        : App.uiText(App.language, "FileUndoNothing"))
                }
            }
            Shortcut {
                sequence: StandardKey.Redo; enabled: pane._keysLive
                onActivated: {
                    if (galleryView._editableTextFocused()) return
                    const name = mediaModel.redoFileOpName()
                    pane.statusRequested(mediaModel.redoFileOp()
                        ? App.uiText(App.language, "FileRedoDeleted") + name
                        : App.uiText(App.language, "FileRedoNothing"))
                }
            }
            Shortcut {
                sequence: "B"; enabled: pane._keysLive
                onActivated: {
                    if (galleryView.covered) {
                        galleryView.covered = false
                        PaneCtl.refreshCurrentFolder()
                    } else {
                        galleryView.covered = true
                    }
                }
            }
            // Strg + '+'/'-' (inkl. '='): Kachelgröße ändern. Nur eindeutige
            // Sequenzen - StandardKey.ZoomIn würde zusätzlich "Ctrl++" liefern und
            // den Shortcut mehrdeutig machen (feuert dann gar nicht).
            Shortcut {
                sequences: ["Ctrl++", "Ctrl+="]
                enabled: pane._keysLive
                onActivated: App.zoomIn(16)
            }
            Shortcut {
                sequence: "Ctrl+-"
                enabled: pane._keysLive
                onActivated: App.zoomOut(16)
            }

            FilterBar {
                id: filterBar
                //  Im Player-Modus stehen nur Audio und Video zur Wahl.
                audioOnly: pane.playerMode
                anchors { left: parent.left; right: parent.right; top: parent.top }
                onEnterAddToTagMode: function(tag) { galleryView.enterAddToTagMode(tag) }
                // Panel-Steuerung: Tag- und Kategorie-Abschnitt des Seitenpanels
                // INDIVIDUELL schaltbar; der Zustand lebt im TagCategoryPanel und
                // wird hier für die Aktiv-Anzeige der Toggle-Zeilen gespiegelt.
                tagPanelVisible: catPanel.showTagsSection
                categoryPanelVisible: catPanel.showCategoriesSection
                onTagPanelToggled:      catPanel.showTagsSection      = !catPanel.showTagsSection
                onCategoryPanelToggled: catPanel.showCategoriesSection = !catPanel.showCategoriesSection
                // „Extrahieren": Ordner asynchron nach PDFs durchsuchen; das
                // Ergebnis öffnet unten (onFolderPdfsReady) den Auswahldialog.
                // Das Flag grenzt uns gegen Scans anderer Aufrufer ab (Singleton).
                onNewFolderRequested: function(folder) {
                    galleryView.promptNewFolder(folder)
                }
                onExtractPagesRequested: function(folder) {
                    pane.extractRequested(folder)
                }
            }

            GalleryView {
                id: galleryView
                //  Im Player-Modus auf Wunsch als Liste statt als Kachelraster
                //  (Einstellungen ▸ Audio) - so sieht man auch auf den ersten
                //  Blick, in welchem Modus man ist.
                listMode: pane.playerMode && Audio.listLayout
                //  Optionen-Modus dieser Hälfte (Alt+S).
                optionsVisible: PaneCtl.optionsVisible
                anchors {
                    left: parent.left
                    right: catPanel.visible ? catPanel.left : parent.right
                    top: filterBar.bottom
                    bottom: addBanner.visible ? addBanner.top
                            : (modeBanner.visible ? modeBanner.top : parent.bottom)
                }
                onActivated: function(filePath) {
                    if (pane.pendingAddFile) { pane.addFileFromGallery(filePath); return }
                    //  Im Player-Modus öffnet der Doppelklick die GROSSE Ansicht
                    //  (Titel, Fortschritt, Warteschlange) - das Vollbild der
                    //  Datei bleibt zu, man geht ja weiter durch die Liste.
                    if (pane.playerMode) {
                        pane.playHere(filePath)
                        pane.openPlayerView()
                        return
                    }
                    pane.pushFullscreen(filePath)
                }
                //  Einfacher Klick: im Player-Modus spielt er sofort, ohne die
                //  Ansicht zu wechseln (Wunsch des Nutzers).
                onFileClicked: function(filePath) {
                    if (pane.playerMode && !pane.pendingAddFile) pane.playHere(filePath)
                }
                //  Doppelklick auf eine Ordnerkachel: hinein. Waehrend der Modus
                //  „Datei zur geteilten Ansicht waehlen" laeuft, wird NICHT der
                //  Ordner gewechselt - der Modus wartet auf eine DATEI.
                onFolderOpenRequested: function(folderPath) {
                    if (pane.pendingAddFile) return
                    PaneCtl.openSubfolder(folderPath)
                }
                //  Aktionen aus der Kopfzeile eines aufgeklappten Bereichs -
                //  sie zielen auf DESSEN Ordner.
                onCreateFileRequested: function(folderPath) {
                    filterBar.openCreateFor(folderPath)
                }
                onExtractPagesRequested: function(folderPath) {
                    pane.extractRequested(folderPath)
                }
                onFolderDropRequested: function(sourcePath, folderPath) {
                    pane.folderDropRequested(sourcePath, folderPath)
                }
                //  Dateien von AUSSEN landen in dem Ordner, ueber dem
                //  losgelassen wurde (geprueft in AppController).
                onExternalDropRequested: function(urls, folderPath) {
                    pane.externalDropRequested(urls, folderPath)
                }
                onStatusRequested: function(text) { pane.statusRequested(text) }
                //  „Ton als Audiodatei sichern": die Arbeit macht `Audio`
                //  asynchron, das Aufnehmen der neuen Datei danach `PaneCtl`
                //  (s. Connections weiter unten).
                onAudioExtractRequested: function(filePath) {
                    Audio.extractAudio(filePath)
                }
            }

            //  Ergebnis des Ton-Sicherns. `Audio` ist APPWEIT - deshalb setzen
            //  BEIDE Hälften denselben Ruf ab; `adoptSiblingFile` prüft selbst,
            //  ob die neue Datei im Ordner DIESER Hälfte liegt, und tut sonst
            //  nichts. Ein Merker je Hälfte wäre dafür die zweite Wahrheit.
            Connections {
                target: Audio
                function onExtractFinished(ok, source, target) {
                    if (!ok) return
                    PaneCtl.adoptSiblingFile(source, target, Audio.extractInheritTags)
                }
            }

            TagCategoryPanel {
                id: catPanel
                //  Die Tags/Kategorien DIESER Hälfte - `Tags` wäre appweit und
                //  folgte dem Mauszeiger (s. TagCategoryPanel ▸ tagsCtl).
                tagsCtl: PaneCtl.tags
                folderSource: PaneCtl
                // Beide Abschnitte starten ausgeblendet; das Panel erscheint,
                // sobald mindestens einer aktiviert wird (Filter ▸ Tags & Kategorien).
                showTagsSection: false
                showCategoriesSection: false
                visible: showTagsSection || showCategoriesSection
                width: Math.min(300, galleryPage.width * 0.45)
                anchors { right: parent.right; top: filterBar.bottom; bottom: parent.bottom }
                onEnterAddToTagMode: function(tag) { galleryView.enterAddToTagMode(tag) }
                onEnterGroupMode: function(tag) { galleryView.enterGroupMode(tag) }
            }

            // ── Hinzufügen-Modus-Banner (Datei zur geteilten Ansicht wählen) ─
            Rectangle {
                id: addBanner
                visible: pane.pendingAddFile
                anchors { left: parent.left; right: parent.right
                          bottom: modeBanner.visible ? modeBanner.top : parent.bottom }
                height: 34
                color: App.themeAccent
                Row {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.uiText(App.language, "SplitPickPrompt")
                        color: App.themeBackground; font.pixelSize: 12; font.bold: true
                    }
                    Item { width: parent.width - 260; height: 1 }
                    Button {
                        anchors.verticalCenter: parent.verticalCenter
                        height: 24; text: App.uiText(App.language, "SplitCancel"); font.pixelSize: 11
                        onClicked: pane.cancelAddFile()
                    }
                }
            }

            // ── Modus-Banner (Gruppen-/Add-to-Tag-Modus verlassen) ───────────
            Rectangle {
                id: modeBanner
                visible: galleryView.tagMode !== 0
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 34
                color: App.themeAccent
                Row {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: (galleryView.tagMode === 1 ? App.uiText(App.language, "ModeGroup") : App.uiText(App.language, "ModeAddToTag"))
                              + ": " + galleryView.modeTag
                        color: App.themeBackground; font.pixelSize: 12; font.bold: true
                    }
                    Item { width: parent.width - 260; height: 1 }
                    Button {
                        anchors.verticalCenter: parent.verticalCenter
                        height: 24; text: "Fertig"; font.pixelSize: 11
                        onClicked: galleryView.exitModes()
                    }
                }
            }
        }
    }

    // ── Geteilte Ansicht (Splitscreen): bis zu 4 Dateien NEBENEINANDER ────────
    //  Anders als klassische Tabs zeigt die Vollbild-Seite mehrere Dateien
    //  GLEICHZEITIG - analog zum 2-/3-/4-Spieler-Splitscreen: zwei Dateien
    //  nebeneinander, drei als 2 oben + 1 unten (volle Breite), vier als 2×2.
    //  So lässt sich bequem parallel arbeiten/vergleichen.
    //
    //  • Jede Kachel ist ein eigenständiger FullscreenViewer (eigene Kopfleiste,
    //    eigene <-/->-Navigation über das Galerie-Modell).
    //  • „+" (Kopfleiste jeder Kachel, direkt neben dem Datum-Button) öffnet
    //    einen Datei-Dialog und fügt die gewählte Datei als weitere Kachel hinzu.
    //  • Zurück/Esc einer Kachel SCHLIESST genau diese Datei (RAM sofort frei);
    //    war es die letzte Kachel, geht es zurück zur Galerie.
    //  • Bei mehr als einer Kachel entfällt die untere Hover-Navigation der
    //    Kacheln (Pfeile + Zähler) - sie kehrt zurück, sobald nur noch eine
    //    Datei offen ist (splitActive-Flag je Viewer).
    //  Bei ZWEI Hälften darf jede höchstens zwei Dateien zeigen - insgesamt
    //  bleibt es damit bei vier (Festlegung des Nutzers).
    readonly property int maxOpenFiles: pane.splitActive ? 2 : 4
    ListModel { id: openFilesModel }   // Rolle: path (aktueller Pfad der Kachel)

    // Geometrie einer Kachel je Index/Anzahl (kleine Lücke g als Trennfuge).
    // Einstellbare Split-Verhältnisse (per Divider-Drag; auf sinnvolle Grenzen
    // geklemmt). splitV = vertikale Trennung (Spalten), splitH = horizontale.
    property real splitV: 0.5
    property real splitH: 0.5
    readonly property real _splitMin: 0.15
    readonly property real _splitMax: 0.85

    // ── Docking-Layoutzustand (Session-lokal, KEIN Persistieren) ──────────────
    //  Das bestehende paneRect-System wurde zum Slot-System erweitert:
    //  layout2:   Anordnung bei 2 Dateien - "cols" (nebeneinander, Standard)
    //             oder "rows" (übereinander).
    //  layout3:   Seite der GROSSEN Kachel bei 3 Dateien - "bottom" (Standard:
    //             zwei kleine oben, eine große unten), "top", "left", "right".
    //  slotOrder: Zuordnung Layout-Slot -> Modell-Index (openFilesModel).
    //             Slots sind layoutfest definiert (s. _slotRect). Drag&Drop,
    //             Schließen und Hinzufügen ordnen NUR diese Liste bzw. die
    //             Layout-Variante um - das Modell selbst bleibt unangetastet,
    //             kein Viewer wird dadurch zerstört oder neu geladen (reine
    //             Geometrie-Bindings, performant identisch zum alten System).
    property string layout2: "cols"
    property string layout3: "bottom"
    property var    slotOrder: []

    // Reine Slot-Geometrie eines LAYOUTS (unabhängig vom Modell). Slots:
    //  count 2 "cols": 0 = links, 1 = rechts · "rows": 0 = oben, 1 = unten
    //  count 3: 0/1 = die zwei KLEINEN Kacheln (Lesereihenfolge), 2 = die GROSSE
    //  count 4: 0 = oben-links, 1 = oben-rechts, 2 = unten-links, 3 = unten-rechts
    function _slotRect(slot, count, l2, l3, W, H) {
        var g = 2
        var rV = Math.max(_splitMin, Math.min(pane.splitV, _splitMax))
        var rH = Math.max(_splitMin, Math.min(pane.splitH, _splitMax))
        if (count <= 1)
            return { x: 0, y: 0, w: W, h: H }
        if (count === 2) {
            if (l2 === "rows") {                             // zwei Zeilen übereinander
                var th2 = (H - g) * rH
                if (slot === 0) return { x: 0, y: 0,       w: W, h: th2 }
                return                 { x: 0, y: th2 + g, w: W, h: H - g - th2 }
            }
            var cwL = (W - g) * rV                           // zwei Spalten nebeneinander
            if (slot === 0) return { x: 0,       y: 0, w: cwL,           h: H }
            return                 { x: cwL + g, y: 0, w: W - g - cwL,   h: H }
        }
        if (count === 3) {
            var hs = (H - g) * rH                            // horizontale Trennlinie
            var vs = (W - g) * rV                            // vertikale Trennlinie
            if (l3 === "top") {                              // große Kachel oben
                if (slot === 2) return { x: 0,      y: 0,      w: W,           h: hs }
                if (slot === 0) return { x: 0,      y: hs + g, w: vs,          h: H - g - hs }
                return                 { x: vs + g, y: hs + g, w: W - g - vs,  h: H - g - hs }
            }
            if (l3 === "left") {                             // große Kachel links
                if (slot === 2) return { x: 0,      y: 0,      w: vs,          h: H }
                if (slot === 0) return { x: vs + g, y: 0,      w: W - g - vs,  h: hs }
                return                 { x: vs + g, y: hs + g, w: W - g - vs,  h: H - g - hs }
            }
            if (l3 === "right") {                            // große Kachel rechts
                if (slot === 2) return { x: vs + g, y: 0,      w: W - g - vs,  h: H }
                if (slot === 0) return { x: 0,      y: 0,      w: vs,          h: hs }
                return                 { x: 0,      y: hs + g, w: vs,          h: H - g - hs }
            }
            // "bottom" (Standard): zwei kleine oben, eine große unten
            if (slot === 0) return { x: 0,      y: 0,      w: vs,          h: hs }
            if (slot === 1) return { x: vs + g, y: 0,      w: W - g - vs,  h: hs }
            return                 { x: 0,      y: hs + g, w: W,           h: H - g - hs }
        }
        var topH = (H - g) * rH                              // 2×2
        var leftW = (W - g) * rV
        var col = slot % 2
        var rowi = Math.floor(slot / 2)
        return { x: col === 0 ? 0 : leftW + g,
                 y: rowi === 0 ? 0 : topH + g,
                 w: col === 0 ? leftW : W - g - leftW,
                 h: rowi === 0 ? topH : H - g - topH }
    }

    // Slot eines Modell-Index (defensive Identität, falls slotOrder gerade
    // nicht synchron ist - transient WÄHREND einer Modelländerung im selben
    // JS-Block; gerendert wird erst nach dessen Abschluss, also nie sichtbar).
    function _slotOfIndex(index, count) {
        if (slotOrder.length === count) {
            var s = slotOrder.indexOf(index)
            if (s >= 0) return s
        }
        return index
    }

    function paneRect(index, count, W, H) {
        return _slotRect(_slotOfIndex(index, count), count, layout2, layout3, W, H)
    }

    function _identity(n) { var a = []; for (var i = 0; i < n; i++) a.push(i); return a }

    // ── Slot-Übergänge bei Anzahl-Wechsel („sinngemäß erhalten") ──────────────
    //  Nach einem append (neuer Modell-Index = count-1) die Slot-Zuordnung so
    //  fortschreiben, dass die bestehenden Kacheln ihre relative Anordnung
    //  behalten und die neue Datei die frei werdende Fläche bekommt.
    function _paneAdded() {
        var n = openFilesModel.count
        var S = (slotOrder.length === n - 1) ? slotOrder.slice() : _identity(n - 1)
        var ni = n - 1
        if (n <= 1) { slotOrder = [0]; return }
        if (n === 2) { slotOrder = [S[0], ni]; return }
        if (n === 3) {
            // "cols" -> beide bisherigen bleiben nebeneinander (obere Hälfte),
            //          die Neue wird große Kachel unten (Standard-3er-Layout);
            // "rows" -> beide bisherigen bleiben übereinander (linke Hälfte),
            //          die Neue wird große Kachel rechts.
            layout3 = (layout2 === "rows") ? "right" : "bottom"
            slotOrder = [S[0], S[1], ni]
            return
        }
        // n === 4: Übergang ins 2×2 je bisherigem 3er-Layout - die große Kachel
        // behält den ersten Quadranten ihrer Hälfte, die Neue den frei werdenden.
        if (layout3 === "top")        slotOrder = [S[2], ni,   S[0], S[1]]
        else if (layout3 === "left")  slotOrder = [S[2], S[0], ni,   S[1]]
        else if (layout3 === "right") slotOrder = [S[0], S[2], S[1], ni]
        else /* bottom */             slotOrder = [S[0], S[1], S[2], ni]
    }

    //  Vor dem Entfernen von Modell-Index ri: neue Slot-Zuordnung + ggf. neues
    //  Layout bestimmen - die ANGRENZENDE Kachel übernimmt die frei werdende
    //  Fläche. Liefert die NEUE slotOrder (Indizes > ri bereits dekrementiert);
    //  der Aufrufer weist sie NACH dem Modell-remove zu.
    function _slotsAfterRemove(ri) {
        var n = openFilesModel.count
        var S = (slotOrder.length === n) ? slotOrder.slice() : _identity(n)
        var rs = S.indexOf(ri)
        var R = []
        if (n === 2) {
            R = [S[rs === 0 ? 1 : 0]]
        } else if (n === 3) {
            if (rs === 2) {
                // Große Kachel weg -> die zwei kleinen behalten ihre Achse:
                // oben/unten-Layout hatte sie nebeneinander -> Spalten;
                // links/rechts-Layout hatte sie übereinander -> Zeilen.
                layout2 = (layout3 === "left" || layout3 === "right") ? "rows" : "cols"
                R = [S[0], S[1]]
            } else {
                // Kleine Kachel weg -> verbleibende kleine + große teilen sich
                // die Fläche entlang der bisherigen Halbierungsachse; die
                // Reihenfolge spiegelt die bisherigen Positionen.
                var small = S[rs === 0 ? 1 : 0]
                var big   = S[2]
                if (layout3 === "bottom")    { layout2 = "rows"; R = [small, big] }
                else if (layout3 === "top")  { layout2 = "rows"; R = [big, small] }
                else if (layout3 === "left") { layout2 = "cols"; R = [big, small] }
                else /* right */             { layout2 = "cols"; R = [small, big] }
            }
        } else if (n === 4) {
            // 2×2 -> 3: der ZEILEN-Nachbar des entfernten Quadranten wird zur
            // großen Kachel seiner Zeile (übernimmt die frei werdende Fläche).
            if (rs === 0)      { layout3 = "top";    R = [S[2], S[3], S[1]] }
            else if (rs === 1) { layout3 = "top";    R = [S[2], S[3], S[0]] }
            else if (rs === 2) { layout3 = "bottom"; R = [S[0], S[1], S[3]] }
            else               { layout3 = "bottom"; R = [S[0], S[1], S[2]] }
        }
        for (var i = 0; i < R.length; i++)
            if (R[i] > ri) R[i]--
        return R
    }

    function indexOfOpenFile(p) {
        for (var i = 0; i < openFilesModel.count; i++)
            if (openFilesModel.get(i).path === p) return i
        return -1
    }

    // ── Docking-Drag (Kopfleisten-Drag der Kacheln) ───────────────────────────
    //  Zustand + Zonenmodell (VS-Code-artig). Getroffen wird ein sichtbarer
    //  INDIKATOR (Rand-Zonen bei 2/3 Dateien, Ecken bei 3); außerhalb der
    //  Indikatoren gilt die Kachel unter dem Cursor als TAUSCH-Ziel. Bei
    //  4 Dateien gibt es AUSSCHLIESSLICH den Positionstausch (kein Layout-
    //  Wechsel) - daher dort keine Indikatoren.
    property bool dragActive: false
    property int  dragIndex: -1
    property real dragX: 0
    property real dragY: 0
    property var  dragZone: ({ kind: "none" })   // {kind:"edge"|"corner"|"swap"|"none", side|corner|target}

    function beginPaneDrag(i) {
        if (openFilesModel.count < 2 || i < 0 || i >= openFilesModel.count) return
        dragIndex = i
        dragZone = { kind: "none" }
        dragActive = true
    }
    function updatePaneDrag(x, y) {
        if (!dragActive) return
        dragX = x; dragY = y
        dragZone = _zoneAt(x, y)
    }
    function endPaneDrag(x, y) {
        if (!dragActive) return
        updatePaneDrag(x, y)
        var z = dragZone
        if (z.kind === "swap")        _applySwap(dragIndex, z.target)
        else if (z.kind === "edge")   _applyEdgeDrop(dragIndex, z.side)
        else if (z.kind === "corner") _applyCornerDrop(dragIndex, z.corner)
        cancelPaneDrag()
    }
    function cancelPaneDrag() {
        dragActive = false
        dragIndex = -1
        dragZone = { kind: "none" }
    }

    //  Sichtbare Drop-Indikatoren je Anzahl (Position/Größe in Split-Seiten-
    //  Koordinaten). Rand-Mitten bei 2 UND 3 Dateien, Ecken zusätzlich bei 3.
    function _zoneIndicators(count, W, H) {
        var s = 46, m = 14
        var list = []
        if (count === 2 || count === 3) {
            list.push({ kind: "edge", side: "left",   x: m,           y: (H - s) / 2, w: s, h: s })
            list.push({ kind: "edge", side: "right",  x: W - m - s,   y: (H - s) / 2, w: s, h: s })
            list.push({ kind: "edge", side: "top",    x: (W - s) / 2, y: m,           w: s, h: s })
            list.push({ kind: "edge", side: "bottom", x: (W - s) / 2, y: H - m - s,   w: s, h: s })
        }
        if (count === 3) {
            list.push({ kind: "corner", corner: "tl", x: m,           y: m,           w: s, h: s })
            list.push({ kind: "corner", corner: "tr", x: W - m - s,   y: m,           w: s, h: s })
            list.push({ kind: "corner", corner: "bl", x: m,           y: H - m - s,   w: s, h: s })
            list.push({ kind: "corner", corner: "br", x: W - m - s,   y: H - m - s,   w: s, h: s })
        }
        return list
    }

    function _zoneAt(x, y) {
        var pg = _splitPageItem
        if (!pg) return { kind: "none" }
        var count = openFilesModel.count
        var W = pg.width, H = pg.height
        var inds = _zoneIndicators(count, W, H)
        for (var i = 0; i < inds.length; i++) {
            var z = inds[i]
            if (x >= z.x && x <= z.x + z.w && y >= z.y && y <= z.y + z.h) return z
        }
        for (var j = 0; j < count; j++) {                    // Tausch: Kachel unter dem Cursor
            if (j === dragIndex) continue
            var r = paneRect(j, count, W, H)
            if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h)
                return { kind: "swap", target: j }
        }
        return { kind: "none" }
    }

    //  Vorschau der Zielfläche der GEZOGENEN Datei für die getroffene Zone -
    //  zeigt das Layout-Ergebnis an, BEVOR gedroppt wird.
    function _zonePreviewRect(z, W, H) {
        var count = openFilesModel.count
        if (z.kind === "swap")
            return paneRect(z.target, count, W, H)
        if (z.kind === "edge") {
            if (count === 2) {
                var l2 = (z.side === "top" || z.side === "bottom") ? "rows" : "cols"
                return _slotRect((z.side === "left" || z.side === "top") ? 0 : 1,
                                 2, l2, layout3, W, H)
            }
            return _slotRect(2, 3, layout2, z.side, W, H)    // große Kachel auf dieser Seite
        }
        if (z.kind === "corner") {
            var l3 = (z.corner === "tl" || z.corner === "tr") ? "bottom" : "top"
            return _slotRect((z.corner === "tl" || z.corner === "bl") ? 0 : 1,
                             3, layout2, l3, W, H)
        }
        return { x: 0, y: 0, w: 0, h: 0 }
    }

    //  Positionstausch zweier Kacheln - NUR slotOrder wird umgestellt (kein
    //  Modell-Umbau: die Viewer bleiben samt Zustand am Leben, nur die
    //  Geometrie-Bindings wechseln).
    function _applySwap(a, b) {
        if (a === b || a < 0 || b < 0) return
        var n = openFilesModel.count
        var S = (slotOrder.length === n) ? slotOrder.slice() : _identity(n)
        var sa = S.indexOf(a), sb = S.indexOf(b)
        if (sa < 0 || sb < 0) return
        S[sa] = b; S[sb] = a
        slotOrder = S
    }

    //  Zwei verbleibende Kacheln auf zwei Ziel-Slots verteilen: die Zuordnung
    //  mit dem geringeren Gesamtabstand der Kachel-MITTEN (aktuell -> Ziel)
    //  gewinnt - jede Kachel bleibt so möglichst nah an ihrer bisherigen
    //  Position („sinngemäß"). Muss VOR dem Umstellen von layout*/slotOrder
    //  laufen (liest die aktuellen Positionen über paneRect).
    function _assignNearest(idxA, idxB, slotP, slotQ, l3) {
        var pg = _splitPageItem
        var W = pg ? pg.width : 1, H = pg ? pg.height : 1
        var n = openFilesModel.count
        function cx(r) { return r.x + r.w / 2 }
        function cy(r) { return r.y + r.h / 2 }
        function d2(r, t) { var dx = cx(r) - cx(t), dy = cy(r) - cy(t); return dx * dx + dy * dy }
        var ra = paneRect(idxA, n, W, H), rb = paneRect(idxB, n, W, H)
        var tp = _slotRect(slotP, 3, layout2, l3, W, H)
        var tq = _slotRect(slotQ, 3, layout2, l3, W, H)
        return (d2(ra, tp) + d2(rb, tq) <= d2(ra, tq) + d2(rb, tp))
               ? { p: idxA, q: idxB } : { p: idxB, q: idxA }
    }

    //  Rand-Zone: bei 2 Dateien Layout + Reihenfolge setzen (gezogene Datei
    //  bekommt die Zonen-Seite); bei 3 Dateien wird die gezogene Datei zur
    //  GROSSEN Kachel dieser Seite, die übrigen zwei teilen sich die andere
    //  Hälfte (Slot-Verteilung positionsnah).
    function _applyEdgeDrop(di, side) {
        var count = openFilesModel.count
        if (count === 2) {
            var other = (di === 0) ? 1 : 0
            layout2 = (side === "top" || side === "bottom") ? "rows" : "cols"
            slotOrder = (side === "left" || side === "top") ? [di, other] : [other, di]
            return
        }
        if (count !== 3) return
        var rest = []
        for (var j = 0; j < count; j++) if (j !== di) rest.push(j)
        var asg = _assignNearest(rest[0], rest[1], 0, 1, side)
        layout3 = side
        slotOrder = [asg.p, asg.q, di]
    }

    //  Ecken-Zone (nur 3 Dateien): die gezogene Datei wird KLEINE Kachel in
    //  diesem Quadranten; die frei werdende große Fläche geht vollständig an
    //  die angrenzende Kachel (positionsnahe Verteilung der übrigen zwei auf
    //  den zweiten kleinen Slot und den großen Slot der anderen Hälfte).
    function _applyCornerDrop(di, corner) {
        if (openFilesModel.count !== 3) return
        var l3 = (corner === "tl" || corner === "tr") ? "bottom" : "top"
        var dslot = (corner === "tl" || corner === "bl") ? 0 : 1
        var oslot = (dslot === 0) ? 1 : 0
        var rest = []
        for (var j = 0; j < 3; j++) if (j !== di) rest.push(j)
        var asg = _assignNearest(rest[0], rest[1], oslot, 2, l3)
        layout3 = l3
        var S = [0, 0, 0]
        S[dslot] = di; S[oslot] = asg.p; S[2] = asg.q
        slotOrder = S
    }

    // Kachel schließen (Zurück/Esc einer Datei). Letzte Kachel -> zurück zur
    // Galerie. Die Slot-Zuordnung wird VOR dem remove berechnet („sinngemäß
    // erhalten": die angrenzende Kachel übernimmt die frei werdende Fläche)
    // und NACH dem remove zugewiesen - dazwischen greift die Identitäts-
    // Absicherung in _slotOfIndex (nie gerendert, gleicher JS-Block).
    function closeFilePane(i) {
        if (i < 0 || i >= openFilesModel.count) return
        var R = _slotsAfterRemove(i)
        openFilesModel.remove(i)              // gibt den zugehörigen Viewer sofort frei
        slotOrder = R
        if (openFilesModel.count === 0)
            popFullscreen()
    }

    // „+": weitere Datei zur geteilten Ansicht hinzufügen. Statt eines
    // Datei-Dialogs kehrt die Ansicht zur GALERIE (Hauptfenster) zurück - die
    // offenen Kacheln bleiben im Modell erhalten. Ein Klick in der Galerie wählt
    // die nächste Datei; danach wird die geteilte Ansicht wiederhergestellt.
    //
    // WICHTIG (Issue-Fix): Die Split-Seite wird als PERSISTENTES Item genau
    // EINMAL erzeugt und dieses Item gepusht - nicht die Component. StackView
    // zerstört beim Pop nur selbst erzeugte Items; ein gepushtes Fremd-Item
    // überlebt den Pop (Qt blendet es aus und gibt die Ownership zurück).
    // Dadurch bleiben ALLE Viewer beim „+"-Rücksprung in die Galerie am Leben
    // - PDFs behalten Seite/Scrollposition, Bilder ihren Zoom, Texte ihre
    // Scrollstelle. Vorher zerstörte der Pop die per Component erzeugte Seite
    // samt Viewern; der erneute Push baute alles frisch -> Seite 1.
    // popFullscreen() leert weiterhin das Modell -> alle Viewer werden sofort
    // freigegeben (RAM-Priorität unverändert); nur der leere Seiten-Rahmen
    // bleibt für die Wiederverwendung bestehen.
    property Item _splitPageItem: null
    function _splitPage() {
        if (!_splitPageItem)
            _splitPageItem = fullscreenComponent.createObject(pane)
        return _splitPageItem
    }
    property bool pendingAddFile: false
    function requestAddFile() {
        if (openFilesModel.count >= pane.maxOpenFiles) {
            pane.statusRequested(App.uiText(App.language, "SplitMaxReached"))
            
            return
        }
        pane.pendingAddFile = true
        if (paneStack.depth > 1)
            paneStack.pop()                 // NICHT leeren - Kacheln bleiben im Modell UND am Leben
    }
    // Datei aus der Galerie zur geteilten Ansicht hinzufügen (Klick im Hinzufügen-Modus).
    function addFileFromGallery(p) {
        pane.pendingAddFile = false
        if (p !== undefined && p.length > 0
                && indexOfOpenFile(p) < 0
                && openFilesModel.count < pane.maxOpenFiles) {
            openFilesModel.append({ path: p })
            _paneAdded()                      // Slot-Zuordnung fortschreiben (Layout bleibt sinngemäß)
        }
        if (paneStack.depth < 2)
            paneStack.push(_splitPage())          // geteilte Ansicht wiederherstellen
    }
    // Hinzufügen abbrechen -> zurück zur (unveränderten) geteilten Ansicht.
    function cancelAddFile() {
        pane.pendingAddFile = false
        if (paneStack.depth < 2 && openFilesModel.count > 0)
            paneStack.push(_splitPage())
    }

    Component {
        id: fullscreenComponent

        Item {
            id: splitPage

            // Lade-Gating: die schwere Medienlast erst NACH dem StackView-Übergang
            // anstoßen - die Viewer sitzen in Loadern UNTER der Seite (StackView.view
            // wäre dort null), daher gated die Seite die Loader-Aktivierung.
            // Nur bei Active setzen: das persistente Item existiert bereits VOR
            // dem ersten Push (s. _splitPage()) - dort ist StackView.view noch
            // null, das darf das Gating nicht auslösen. Einmal ready, bleibt
            // ready (beim „+"-Rücksprung laufen die Viewer bewusst weiter).
            property bool pageReady: false
            function _checkReady() {
                if (StackView.status === StackView.Active)
                    pageReady = true
            }
            //  Tastatur-Fokus auf die aktive Kachel legen. NÖTIG bei JEDEM
            //  Sichtbarwerden der Seite, nicht nur beim ersten: Beim zweiten
            //  Öffnen ist `pageReady` schon true (die Seite ist persistent), der
            //  Kachel-Loader lädt also SOFORT beim Befüllen des Modells - also
            //  VOR dem StackView-Push. Das `forceActiveFocus()` im `onLoaded`
            //  läuft dann auf einer Seite, die noch gar nicht die aktive ist,
            //  und der anschließende Push nimmt den Fokus wieder weg
            //  (`activeFocusItem` = QQuickRootItem). Ergebnis: die Kachel sah
            //  richtig aus, reagierte aber auf KEINE Taste mehr - F, <-/-> und Esc
            //  hängen alle am `Keys.onPressed` des FullscreenViewer.
            function _focusActivePane() {
                const l = paneRepeater.itemAt(splitPage.activePaneIndex)
                if (l && l.item) l.item.forceActiveFocus()
            }
            StackView.onStatusChanged: {
                _checkReady()
                if (StackView.status === StackView.Active) _focusActivePane()
            }
            onActivePaneIndexChanged: _focusActivePane()

            // Trennfugen-Hintergrund (scheint in der 2 px-Lücke zwischen Kacheln durch).
            Rectangle { anchors.fill: parent; color: "#0a0a0a" }

            readonly property int paneCount: openFilesModel.count

            //  Aktive Kachel (Split-View): genau EINE trägt die fensterweiten
            //  Tastenkürzel scharf (paneActive) -> keine Mehrdeutigkeit bei 2–4
            //  offenen Dateien. Ein Klick in eine Kachel (paneActivated) oder das
            //  Laden einer neu hinzugefügten Kachel setzt den Index; beim
            //  Schließen wird er in den gültigen Bereich geklemmt.
            property int activePaneIndex: 0
            onPaneCountChanged: activePaneIndex =
                Math.max(0, Math.min(activePaneIndex, paneCount - 1))

            // ── Kacheln: ein FullscreenViewer je Datei, per Split-Layout platziert ─
            Repeater {
                id: paneRepeater
                model: openFilesModel
                delegate: Loader {
                    id: paneLoader
                    required property int index
                    required property string path

                    // Kachel-Geometrie nach Split-Layout (reagiert auf Anzahl/Größe).
                    readonly property var _r: pane.paneRect(index, splitPage.paneCount,
                                                             splitPage.width, splitPage.height)
                    x: _r.x; y: _r.y
                    width: _r.w; height: _r.h

                    // Alle sichtbaren Kacheln sind aktiv (Splitscreen zeigt sie parallel).
                    active: splitPage.pageReady

                    sourceComponent: FullscreenViewer {
                        id: paneViewer
                        startPath: paneLoader.path
                        // Mehr als eine Datei offen -> untere Hover-Navigation der Kachel
                        // (Pfeile + Zähler) ausblenden; bei genau einer Datei wieder an.
                        splitActive: splitPage.paneCount > 1
                        canAddMore:  splitPage.paneCount < pane.maxOpenFiles
                        // Nur die aktive Kachel trägt ihre fensterweiten Kürzel scharf.
                        paneActive:  paneLoader.index === splitPage.activePaneIndex
                        onPaneActivated: splitPage.activePaneIndex = paneLoader.index
                        // Immersives Vollbild (F): Zustand kommt vom Shell, der
                        // Wunsch geht dorthin zurück (Fenster + Menüleiste).
                        immersive: pane.immersive
                        optionsVisible: PaneCtl.optionsVisible
                        onImmersiveToggleRequested: pane.immersiveToggleRequested()
                        onBackRequested:    pane.closeFilePane(paneLoader.index)
                        onAddFileRequested: pane.requestAddFile()
                        // Docking: Kopfleisten-Drag dieser Kachel an die Shell
                        // durchreichen (Viewer-Koordinaten -> Split-Seite; das
                        // Loader-Item sitzt bei (0,0) im Loader -> identisch).
                        onPaneDragStarted:  pane.beginPaneDrag(paneLoader.index)
                        onPaneDragMoved: (x, y) => {
                            var p = paneLoader.mapToItem(splitPage, x, y)
                            pane.updatePaneDrag(p.x, p.y)
                        }
                        onPaneDragEnded: (x, y) => {
                            var p = paneLoader.mapToItem(splitPage, x, y)
                            pane.endPaneDrag(p.x, p.y)
                        }
                        onPaneDragCanceled: pane.cancelPaneDrag()

                        //  Fenstertitel: nur die AKTIVE Kachel schreibt ihren Pfad
                        //  in die Shell. `when` sorgt dafür, dass beim Wechsel der
                        //  aktiven Kachel genau eine Bindung gilt; `RestoreNone`
                        //  verhindert, dass eine inaktiv werdende Kachel den Wert
                        //  auf ihren alten Stand zurücksetzt.
                        Binding {
                            target: pane
                            property: "activeFilePath"
                            value: paneViewer.path
                            when: paneViewer.paneActive
                            restoreMode: Binding.RestoreNone
                        }
                        Binding {
                            target: pane
                            property: "activeViewer"
                            value: paneViewer
                            when: paneViewer.paneActive
                            restoreMode: Binding.RestoreNone
                        }
                    }
                    onLoaded: {
                        item.forceActiveFocus()
                        // Neu geladene (zuletzt hinzugefügte) Kachel wird aktiv.
                        splitPage.activePaneIndex = paneLoader.index
                    }

                    // Kachel-Pfad folgt der <-/->-Navigation IM Viewer (Modell nachziehen).
                    Connections {
                        target: paneLoader.item
                        ignoreUnknownSignals: true
                        function onPathChanged() {
                            if (paneLoader.item && paneLoader.index < openFilesModel.count)
                                openFilesModel.setProperty(paneLoader.index, "path",
                                                           paneLoader.item.path)
                        }
                    }
                }
            }

            // ── Ziehbare Trenner (Divider) ────────────────────────────────────
            //  Sichtbarkeit/Ausdehnung folgt der Layout-Variante:
            //  vertikal  - 2 Spalten ("cols"), alle 3er-Layouts (bei großer
            //              Kachel oben/unten nur in der Hälfte mit den kleinen
            //              Kacheln, bei links/rechts volle Höhe), 2×2;
            //  horizontal - 2 Zeilen ("rows"), alle 3er-Layouts (bei großer
            //              Kachel links/rechts nur in der kleinen Hälfte), 2×2.
            //  Ziehen setzt pane.splitV / pane.splitH (geklemmt) -> paneRect folgt.
            readonly property real _gap: 2
            readonly property real _vX: (splitPage.width  - _gap) * Math.max(pane._splitMin, Math.min(pane.splitV, pane._splitMax))
            readonly property real _hY: (splitPage.height - _gap) * Math.max(pane._splitMin, Math.min(pane.splitH, pane._splitMax))

            Rectangle {                                       // vertikaler Trenner
                z: 50
                visible: splitPage.paneCount >= 4
                         || (splitPage.paneCount === 2 && pane.layout2 === "cols")
                         || splitPage.paneCount === 3
                x: splitPage._vX - 3
                y: (splitPage.paneCount === 3 && pane.layout3 === "top")
                   ? splitPage._hY : 0
                width: 8
                height: splitPage.paneCount === 3
                        ? (pane.layout3 === "bottom" ? splitPage._hY
                           : pane.layout3 === "top"  ? splitPage.height - splitPage._hY
                           : splitPage.height)
                        : splitPage.height
                color: vDivMA.pressed ? App.themeAccent : "transparent"
                MouseArea {
                    id: vDivMA
                    anchors.fill: parent
                    cursorShape: Qt.SplitHCursor
                    onPositionChanged: (m) => {
                        var p = mapToItem(splitPage, m.x, m.y)
                        pane.splitV = Math.max(pane._splitMin,
                            Math.min(p.x / Math.max(1, splitPage.width - splitPage._gap), pane._splitMax))
                    }
                }
            }
            Rectangle {                                       // horizontaler Trenner
                z: 50
                visible: splitPage.paneCount >= 4
                         || (splitPage.paneCount === 2 && pane.layout2 === "rows")
                         || splitPage.paneCount === 3
                x: (splitPage.paneCount === 3 && pane.layout3 === "left")
                   ? splitPage._vX : 0
                y: splitPage._hY - 3
                width: splitPage.paneCount === 3
                       ? (pane.layout3 === "right" ? splitPage._vX
                          : pane.layout3 === "left" ? splitPage.width - splitPage._vX
                          : splitPage.width)
                       : splitPage.width
                height: 8
                color: hDivMA.pressed ? App.themeAccent : "transparent"
                MouseArea {
                    id: hDivMA
                    anchors.fill: parent
                    cursorShape: Qt.SplitVCursor
                    onPositionChanged: (m) => {
                        var p = mapToItem(splitPage, m.x, m.y)
                        pane.splitH = Math.max(pane._splitMin,
                            Math.min(p.y / Math.max(1, splitPage.height - splitPage._gap), pane._splitMax))
                    }
                }
            }

            // ── Docking-Overlay: Drop-Zonen + Layout-Vorschau + Drag-Geist ────
            //  Nur während eines Kopfleisten-Drags sichtbar (pane.dragActive).
            //  Rand-Indikatoren (2/3 Dateien) + Ecken (3 Dateien); außerhalb der
            //  Indikatoren markiert die Kachel unter dem Cursor den Positions-
            //  tausch. Die Vorschaufläche zeigt das Layout-Ergebnis des Drops.
            Item {
                id: dockOverlay
                anchors.fill: parent
                visible: pane.dragActive
                z: 100

                // Dezentes Abdunkeln - hebt Zonen und Vorschau vom Inhalt ab.
                Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.25) }

                // Vorschau der Zielfläche (Layout-Ergebnis der getroffenen Zone).
                Rectangle {
                    readonly property var _pr: pane.dragActive
                        ? pane._zonePreviewRect(pane.dragZone, dockOverlay.width, dockOverlay.height)
                        : ({ x: 0, y: 0, w: 0, h: 0 })
                    visible: pane.dragActive && pane.dragZone.kind !== "none"
                    x: _pr.x; y: _pr.y
                    width: _pr.w; height: _pr.h
                    radius: 4
                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                    border.color: App.themeAccent
                    border.width: 2
                }

                // Rand-/Ecken-Indikatoren (bei 4 Dateien bewusst leer - nur Tausch).
                Repeater {
                    model: pane.dragActive
                           ? pane._zoneIndicators(splitPage.paneCount,
                                                   dockOverlay.width, dockOverlay.height)
                           : []
                    delegate: Rectangle {
                        id: zoneInd
                        required property var modelData
                        readonly property bool hot:
                            pane.dragZone.kind === modelData.kind
                            && (modelData.kind === "edge"
                                ? pane.dragZone.side === modelData.side
                                : pane.dragZone.corner === modelData.corner)
                        x: modelData.x; y: modelData.y
                        width: modelData.w; height: modelData.h
                        radius: 8
                        color: hot ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.85)
                                   : Qt.rgba(0.08, 0.10, 0.11, 0.85)
                        border.color: hot ? App.themeAccent : Qt.rgba(1, 1, 1, 0.35)
                        border.width: 1

                        // Rand-Symbol: Richtungspfeil, GEZEICHNET (Regel 28).
                        // Vorher standen hier zwei ASCII-Zeichen und zwei
                        // Unicode-Pfeile nebeneinander - vier Richtungen in zwei
                        // verschiedenen Bauarten, entsprechend ungleich schwer.
                        DrawnIcon {
                            anchors.centerIn: parent
                            visible: zoneInd.modelData.kind === "edge"
                            size: 20
                            color: "#e8efed"
                            name: zoneInd.modelData.kind === "edge"
                                  ? ({ left: "arrow-left", right: "arrow-right",
                                       top: "arrow-up", bottom: "arrow-down" })[zoneInd.modelData.side]
                                  : ""
                        }
                        // Ecken-Glyphe: Quadrat-Umriss mit gefülltem Viertel -
                        // gezeichnet statt Sonderzeichen (fontunabhängig).
                        Item {
                            anchors.centerIn: parent
                            width: 20; height: 20
                            visible: zoneInd.modelData.kind === "corner"
                            Rectangle {
                                anchors.fill: parent; radius: 3; color: "transparent"
                                border.color: "#e8efed"; border.width: 1.4
                            }
                            Rectangle {
                                width: 8; height: 8; radius: 1; color: "#e8efed"
                                x: (zoneInd.modelData.corner === "tl"
                                    || zoneInd.modelData.corner === "bl") ? 3 : 9
                                y: (zoneInd.modelData.corner === "tl"
                                    || zoneInd.modelData.corner === "tr") ? 3 : 9
                            }
                        }
                    }
                }

                // Drag-Geist: Dateiname der gezogenen Kachel folgt dem Cursor.
                Rectangle {
                    visible: pane.dragActive && pane.dragIndex >= 0
                    x: pane.dragX + 14; y: pane.dragY + 10
                    width: ghostLabel.implicitWidth + 20; height: 28; radius: 6
                    color: Qt.rgba(0.08, 0.10, 0.11, 0.92)
                    border.color: App.themeAccent; border.width: 1
                    Text {
                        id: ghostLabel
                        anchors.centerIn: parent
                        color: "#e8efed"; font.pixelSize: 12; font.bold: true
                        text: (pane.dragIndex >= 0 && pane.dragIndex < openFilesModel.count)
                              ? pane._folderName(openFilesModel.get(pane.dragIndex).path) : ""
                    }
                }
            }
        }
    }

    // Navigations-API.
    //  Galerie-Doppelklick -> frische Einzel-Kachel (die vorherige Ansicht ist beim
    //  Verlassen ohnehin geschlossen). Weitere Kacheln kommen über den „+"-Button.
    function pushFullscreen(filePath) {
        var p = filePath !== undefined ? filePath : ""
        if (p.length === 0) return
        openFilesModel.clear()
        openFilesModel.append({ path: p })
        pane.slotOrder = [0]
        if (paneStack.depth < 2)
            paneStack.push(_splitPage())
    }
    function popFullscreen() {
        openFilesModel.clear()          // Verlassen schließt alle Dateien (RAM frei)
        pane.slotOrder = []
        if (paneStack.depth > 1)
            paneStack.pop()                 // die leere Split-Seite überlebt (persistentes Item)
    }

    // ── Player-Leiste ───────────────────────────────────────────────────────
    //  Erst sichtbar, wenn ein Titel gewählt ist (Festlegung des Nutzers) - und
    //  nur in der Hälfte, die die Wiedergabe BESITZT: in der anderen Galerie
    //  hätte sie nichts zu suchen.
    AudioPlayerBar {
        id: playerBar
        //  `hasTrack` statt `active`: nach dem Wiederherstellen liegt ein Titel
        //  bereit, ohne zu laufen - die Leiste muss ihn zeigen können.
        //  In der großen Ansicht ist sie weg, dort steht alles schon oben.
        visible: pane.playerMode && pane.playerMine && Audio.hasTrack
                 && !pane.playerPageActive
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        onExpandRequested: pane.openPlayerView()
    }

    //  Die große Ansicht entsteht erst beim ersten Öffnen und bleibt dann als
    //  Item bestehen (Muster wie die Vollbild-Seite: `StackView` zerstört
    //  Fremd-Items beim Pop nicht, der Zustand überlebt).
    Component {
        id: playerViewComponent
        AudioPlayerView {
            onBackRequested: {
                PaneCtl.playerViewOpen = false
                if (paneStack.depth > 1) paneStack.pop()
            }
        }
    }

    //  Das Datum sollte an der DATEI stehen; geht das nicht (schreibgeschützt,
    //  fremdes Dateisystem), bleibt es beim Sidecar - und der Nutzer erfährt es.
    Connections {
        target: mediaModel
        function onFileDateNotWritten(fileName) {
            pane.statusRequested(App.uiText(App.language, "DateNotWrittenToFile")
                                 .arg(fileName))
        }
    }

    // ── Dialoge dieser Hälfte ───────────────────────────────────────────────
    FileChooser {
        id: folderDialog
        title: App.menuOpenFolderText
        fileMode: FileChooser.Directory
        onAccepted: PaneCtl.openFolderUrl(folderDialog.selectedFolder)
    }
    BookmarkEditDialog { id: bookmarkEditDialog }
}
