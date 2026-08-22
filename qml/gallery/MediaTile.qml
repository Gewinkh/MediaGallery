import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  MediaTile.qml - eine Galerie-Kachel (Delegate-Inhalt).
//
//  Ersetzt MediaThumbnail(QWidget). KEINE Pixmap im RAM gehalten: das Image lädt
//  die kleine Disk-Cache-Datei (thumbUrl) asynchron; sourceSize == Kachelgröße,
//  damit nie in Vollauflösung dekodiert wird. Nicht sichtbare Kacheln werden vom
//  GridView recycelt -> nur sichtbare Thumbnails belegen Speicher.
//
//  Interaktionen: Doppelklick (Bildbereich) -> activated(); Inline-Rename
//  (Doppelklick auf Name, im Overlay); Tag-Toggle je nach View-Modus.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: tile

    // Model-Rollen (vom Delegate gesetzt)
    property string filePath: ""
    property string displayName: ""
    property int    mediaType: 6      // 0 Image,1 Video,2 Audio,3 Pdf,4 Text,5 Docx,6 Unknown,7 Folder
    //  Nur Ordnerkacheln: aufgeklappt? (Modellrolle `expanded`)
    property bool   expanded: false
    //  Nur Ordnerkacheln: Medien darin. −1 = noch nicht gezaehlt (die Zeile
    //  bleibt dann leer, statt kurz „0 Medien" zu behaupten).
    property int    childCount: -1
    property string typeLabel: ""
    property var    tags: []
    property var    dateTime
    property string thumbUrl: ""
    property int    thumbState: 0     // 0 pending,1 ready,2 failed
    //  Ladezustand des BILDES (nicht des Modells) - Griff für den Prüfstand:
    //  „Modell sagt fertig, Bild aber nicht" ist ein eigener Fehlerfall.
    readonly property int thumbStatus: thumb.status

    // View-Modus (von GalleryView)
    property int    tagMode: 0        // 0 none,1 group,2 addToTag
    property string modeTag: ""

    // Vorschau-Sperre (Privatsphäre, Taste "B"): verdeckt das Thumbnail.
    property bool   covered: false

    signal activated(string filePath)
    //  Einfacher Linksklick auf eine DATEI (ohne Tag-Modus). Die Galerie nutzt
    //  ihn nur im Player-Modus: dort spielt ein Klick, ohne etwas zu öffnen.
    signal fileClicked(string filePath)
    // Kontextmenü „Datei löschen…" -> GalleryView zeigt EINEN gemeinsamen
    // Bestätigungs-Dialog (kein Dialog je Kachel) und ruft mediaModel.deleteItem.
    signal deleteRequested(string filePath, string displayName)
    //  „Umbenennen…" aus dem Kontextmenü - den Dialog stellt die GalleryView.
    signal renameRequested(string filePath, string currentName)
    //  „+ Neu…" in den Untermenüs: anlegen UND dieser Datei gleich zuweisen.
    //  Den Namensdialog stellt die GalleryView (eine Stelle für alle Kacheln).
    signal newTagRequested(string filePath)
    signal newCategoryRequested(string filePath)
    //  Begleitdatei entfernen: kind 1 = Notizen/Zeichnungen (Sidecar),
    //  2 = Sicherungskopie (.bak). Die Rückfrage führt GalleryView.
    signal companionRemoveRequested(string filePath, int kind)
    //  „Ton als Audiodatei sichern" (nur Videokacheln): die Tonspur wird ohne
    //  Neukodierung neben das Video gelegt. Ausgeführt wird es in GalleryPane -
    //  dort hängt auch das Erben der Tags.
    signal audioExtractRequested(string filePath)
    //  Ordnerkachel: Doppelklick oeffnet den Ordner als neue Hauptebene.
    signal folderOpenRequested(string folderPath)
    signal folderRenameRequested(string folderPath, string currentName)
    signal folderDeleteRequested(string folderPath, string displayName, int itemCount)
    //  Zielhinweis beim Ablegen - gesetzt von der EINEN Ablegefläche der
    //  Galerie (s. GalleryView ▸ hoverFolder). Eine eigene `DropArea` je Kachel
    //  war messbar teurer und wuchs mit der Kachelzahl (bench_dnd).
    property bool dropTarget: false

    readonly property bool tagged: modeTag.length > 0 && tags.indexOf(modeTag) >= 0
    readonly property bool dimmed: tagMode === 1 && modeTag.length > 0 && !tagged

    //  DOCX braucht ZLIB (s. src/core/ZCodec.h) - ohne sie lässt sich eine .docx
    //  nicht einmal LESEN (ZIP legt Einträge als rohes Deflate ab). Die Kachel
    //  bleibt samt Namen stehen, ist aber ausgegraut und nicht zu öffnen; der
    //  Hover-Text nennt die fehlende Bibliothek (Muster wie FilterBar.qml).
    //  Verschlagworten, Umbenennen und Löschen funktionieren weiter.
    readonly property bool unavailable: mediaType === 5 && !App.docxAvailable

    //  Eine Ordnerkachel ist KEIN Medium: kein Thumbnail, kein Play-Glyph, kein
    //  Herausziehen. Sie zeichnet sich selbst (Regel 28) und traegt ihren Namen
    //  dauerhaft - ein Ordner ohne sichtbaren Namen waere nicht unterscheidbar.
    readonly property bool isFolder: mediaType === 7

    //  LISTEN-Darstellung (Player-Modus, Einstellung „Liste"): dieselbe Kachel,
    //  aber flach und breit - Bild klein links, Name, Tags, Angabe rechts. Die
    //  Bedienung bleibt vollständig: Kontextmenü (Tags, Kategorien, Umbenennen,
    //  Löschen), Ziehen, Klick und Doppelklick verhalten sich wie sonst.
    property bool listMode: false
    //  Optionen-Modus der Hälfte (Alt+S), von der Galerie gesetzt.
    property bool optionsVisible: App.optionsVisible

    //  Der LAUFENDE Titel des Player-Modus. `Audio` ist appweit, die Kachel
    //  braucht dafür also keine Durchreichung durch die Galerie.
    //  Markiert wird der GEWÄHLTE Titel, solange eine Hälfte den Player besitzt
    //  - auch pausiert oder gerade erst wiederhergestellt. Verlässt man den
    //  Player-Modus, fällt der Besitzer weg und damit die Markierung.
    readonly property bool playing: Audio.owner !== null && tile.filePath.length > 0
                                    && tile.filePath === Audio.currentPath

    // Rundum gleichmaessig abgerundet (oben wie unten).
    radius: tile.listMode ? 6 : 10
    color: (tile.isFolder && tile.expanded)
           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.13)
           : App.themeCard
    clip: true
    opacity: (dimmed || unavailable) ? 0.45 : 1.0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    //  Ein AUFGEKLAPPTER Ordner hebt sich ab: Akzentrahmen, angehobener
    //  Kachelgrund und ein anderes Symbol (offener Ordner). Ohne das war nicht
    //  zu sehen, welcher Ordner die Flaeche darunter geoeffnet hat.
    border.width: (tagged || tile.playing || (tile.isFolder && tile.expanded)) ? 2 : 1
    border.color: (tagged || tile.playing || (tile.isFolder && tile.expanded))
                  ? App.themeAccent : App.themeBorder

    // ── Platzhalter (während/!= ready) ──────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: !tile.listMode && !tile.isFolder && tile.thumbState !== 1 && !tile.covered
        text: tile.thumbState === 2 ? "\u26A0" : "\u2026"
        color: App.themeTextMuted
        font.pixelSize: 22
    }

    // ── Thumbnail (Disk-Cache, asynchron) ───────────────────────────────────
    Image {
        id: thumb
        anchors.fill: parent
        anchors.margins: 1
        visible: !tile.listMode && !tile.isFolder && tile.thumbState === 1
                 && status === Image.Ready && !tile.covered
        source: tile.thumbUrl
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: App.tileWidth
        sourceSize.height: App.tileHeight
        mipmap: true
    }

    // ── Ordnerkachel ────────────────────────────────────────────────────────
    Column {
        visible: tile.isFolder && !tile.listMode
        anchors.centerIn: parent
        width: parent.width - 16
        spacing: Math.max(4, tile.height * 0.04)

        DrawnIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: tile.expanded ? "folder-open" : "folder"
            //  Etwas kleiner als die Kachel - Rand, Name und Anzahl brauchen
            //  Platz; darunter darf das Symbol aber ruhig gross sein.
            size: Math.max(28, Math.min(tile.width, tile.height) * 0.56)
            color: App.themeAccent
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: tile.displayName
            color: App.themeTextPrimary
            font.pixelSize: Math.max(11, Math.min(16, tile.height * 0.09))
            elide: Text.ElideMiddle
            maximumLineCount: 2
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            //  Solange nicht gezaehlt wurde (−1), bleibt die Zeile leer.
            visible: tile.childCount >= 0
            text: tile.childCount === 0
                  ? App.uiText(App.language, "FolderEmpty")
                  : App.uiText(App.language, "FolderMediaCount").arg(tile.childCount)
            color: App.themeTextMuted
            font.pixelSize: Math.max(9, Math.min(13, tile.height * 0.07))
            elide: Text.ElideRight
        }
    }

    // ── Zeilen-Darstellung (listMode) ───────────────────────────────────────
    Item {
        visible: tile.listMode
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 10

        //  Bild bzw. Symbol, klein und quadratisch.
        Item {
            id: listArt
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: Math.max(24, tile.height - 12)
            height: width

            Image {
                anchors.fill: parent
                visible: !tile.isFolder && tile.thumbState === 1
                         && status === Image.Ready && !tile.covered
                source: tile.thumbUrl
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 96
                sourceSize.height: 96
                mipmap: true
                clip: true
            }
            DrawnIcon {
                anchors.centerIn: parent
                //  Ordner, gesperrte Vorschau oder (noch) kein Bild: dann sagt
                //  das Symbol, worum es geht.
                visible: tile.isFolder || tile.covered || tile.thumbState !== 1
                name: tile.isFolder ? (tile.expanded ? "folder-open" : "folder")
                    : tile.mediaType === 2 ? "audio"
                    : tile.mediaType === 1 ? "play" : "file"
                size: Math.max(14, parent.width * 0.6)
                color: tile.isFolder ? App.themeAccent : App.themeTextMuted
            }
        }

        Text {
            id: listName
            anchors { left: listArt.right; leftMargin: 10
                      right: listTags.left; rightMargin: 10
                      verticalCenter: parent.verticalCenter }
            text: tile.displayName
            color: tile.playing ? App.themeAccent : App.themeTextPrimary
            font.pixelSize: 13
            font.bold: tile.playing
            elide: Text.ElideMiddle
        }

        //  Tag-Punkte wie im Overlay - und wie dort NUR im Optionen-Modus
        //  (Alt+S). Sie hingen vorher unabhängig davon in der Zeile, so dass der
        //  Modus in der Listendarstellung nichts bewirkte (Nutzerbefund).
        Row {
            id: listTags
            visible: tile.optionsVisible
            anchors { right: listInfo.left; rightMargin: 10
                      verticalCenter: parent.verticalCenter }
            spacing: 4
            Repeater {
                model: tile.tags
                delegate: Rectangle {
                    required property var modelData
                    width: 9; height: 9; radius: 4.5
                    color: Tags.tagColor(modelData)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.35)
                    ToolTip.visible: listDotHover.hovered
                    ToolTip.text: modelData
                    HoverHandler { id: listDotHover }
                }
            }
        }

        Text {
            id: listInfo
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: tile.isFolder
                  ? (tile.childCount < 0 ? ""
                     : (tile.childCount === 0 ? App.uiText(App.language, "FolderEmpty")
                        : App.uiText(App.language, "FolderMediaCount").arg(tile.childCount)))
                  : tile.typeLabel
            color: App.themeTextMuted
            font.pixelSize: 11
        }
    }

    //  Die Anzahl wird sichtbarkeitsgesteuert geholt - wie ein Thumbnail, und
    //  wie dort asynchron (s. MediaModel::ensureFolderCount).
    onFilePathChanged: if (tile.isFolder) mediaModel.ensureFolderCount(tile.filePath)
    Component.onCompleted: if (tile.isFolder) mediaModel.ensureFolderCount(tile.filePath)

    // ── Typ-Marke, wenn es KEIN Bild gibt ───────────────────────────────────
    //  Die Endung steckt sonst nur im erzeugten Thumbnail (dort malt sie der
    //  ThumbnailLoader hinein). Eine `.bak` oder ein Archiv hat aber gar keins:
    //  die Kachel blieb leer und war von nichts zu unterscheiden.
    Rectangle {
        visible: !tile.listMode && !tile.isFolder && !tile.covered
                 && tile.thumbState !== 1 && tile.typeLabel.length > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        width: badgeText.implicitWidth + 10
        height: badgeText.implicitHeight + 4
        radius: 4
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.85)
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: tile.typeLabel
            color: "#0b0f10"
            font.pixelSize: 10
            font.bold: true
        }
    }

    // ── Video-Play-Glyph (zentral) ──────────────────────────────────────────
    Rectangle {
        visible: !tile.listMode && tile.mediaType === 1 && !tile.covered
        anchors.centerIn: parent
        width: 40; height: 40; radius: 20
        color: Qt.rgba(0, 0, 0, 0.45)
        DrawnIcon {
            anchors.centerIn: parent
            name: "play"
            size: 18
            color: "white"
        }
    }

    // ── Vorschau-Sperre (Privatsphäre): verdeckt Bild + Glyph, Name bleibt ──
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 9
        visible: tile.covered
        color: "#0f1419"
        Text {
            anchors.centerIn: parent
            text: "\u{1F512}"
            color: "#3c5055"
            font.pixelSize: 26
        }
    }

    // ── Datei aus der App HERAUSziehen ──────────────────────────────────────
    //  Der Zug-Träger liegt NICHT mehr hier, sondern EINMAL in der GalleryView.
    //  Grund: `Drag.active = true` blockiert bis zum Loslassen. Stand dieser
    //  Aufruf im Handler der Kachel, lag deren JavaScript-Rahmen die ganze Zeit
    //  auf dem Stapel - und sobald die Ansicht während des Zuges scrollte
    //  (Randscrollen), räumte die ListView genau diese Kachel weg. Die App
    //  stürzte ab (vom Nutzer gemeldet). Die Kachel bittet deshalb nur noch.
    signal dragStartRequested(string filePath, string thumbUrl)

    //  Der laufende Titel bekommt zusätzlich einen schwachen Schleier - der
    //  Rahmen allein geht zwischen verschlagworteten Kacheln unter.
    Rectangle {
        visible: tile.playing
        anchors.fill: parent
        radius: tile.radius
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.14)
    }

    //  Waehrend ein Zug ueber diesem Ordner steht, hebt er sich hervor.
    Rectangle {
        visible: tile.dropTarget
        anchors.fill: parent
        radius: tile.radius
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
        border.width: 2
        border.color: App.themeAccent
    }

    // ── Interaktion (Bildbereich): Aktivieren / Tag-Toggle / Kontextmenü ─────
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        //  Ziehen beginnt erst nach einer Schwelle (Muster: der Docking-Drag im
        //  FullscreenViewer). Ohne sie würde jeder zittrige Klick zum Zug und
        //  Öffnen/Tag-Umschalten/Kontextmenü gingen verloren.
        property point pressPos: Qt.point(0, 0)
        property bool  dragArmed: false
        onPressed: function(mouse) {
            pressPos = Qt.point(mouse.x, mouse.y)
            //  Ordner werden (noch) nicht herausgezogen - ein Ordner-Zug ins
            //  Fremdprogramm ist eine eigene Zusage, keine Nebenwirkung.
            dragArmed = (mouse.button === Qt.LeftButton)
                        && !tile.unavailable && !tile.isFolder
        }
        onReleased: dragArmed = false
        onPositionChanged: function(mouse) {
            if (!dragArmed || tile.filePath.length === 0)
                return
            const dx = mouse.x - pressPos.x
            const dy = mouse.y - pressPos.y
            //  REINE BEWEGUNGSSCHWELLE, keine Richtungsprüfung mehr: seit die
            //  Galerie nicht mehr per Ziehen scrollt (`interactive: false`),
            //  gehört JEDER Zug auf einer Kachel dem Herausziehen der Datei.
            //  Vorher musste ein senkrechter Zug dem Scrollen überlassen werden,
            //  und genau daran scheiterte das Ziehen in der Praxis.
            //  Die Schwelle bleibt: die ersten Pixel eines Klicks zittern.
            if (Math.abs(dx) < 12 && Math.abs(dy) < 12)
                return                            // noch unentschieden
            dragArmed = false
            //  DEN ZUG STARTET `Drag.active = true`, NICHT `startDrag()`.
            //  Bei `Drag.Automatic` setzt Qt daraufhin selbst einen `QDrag` auf.
            //  `startDrag()` verlangt umgekehrt, dass `active` BEREITS true ist;
            //  sonst meldet es nur „startDrag() drag must be active" - eine
            //  Warnung, die nach journald geht und deshalb unsichtbar blieb.
            //  Genau daran ist das Ziehen bisher lautlos gescheitert.
            //  Die Zuweisung BLOCKIERT bis zum Ende des Zuges und setzt `active`
            //  danach selbst wieder auf false. Genau deshalb kann die Leiste zum
            //  Ablegen (Lesezeichen) davor eingeblendet und danach wieder
            //  abgeräumt werden - dazwischen läuft die Ereignisschleife des Zuges.
            //  Werte HERAUSLÖSEN und die Kachel verlassen: der Zug läuft dann
            //  in der GalleryView, nicht in diesem Handler.
            tile.dragStartRequested(tile.filePath, tile.thumbUrl)
        }
        onDoubleClicked: function(mouse) {
            // Ohne ZLIB öffnet die DOCX-Kachel nicht - der Editor bliebe leer.
            if (tile.unavailable)
                return
            //  Ein Ordner ist keine Datei: `activated` fuehrt in den Vollbild-
            //  Betrachter. Der Doppelklick oeffnet ihn stattdessen als neue
            //  Hauptebene - und nimmt dem einfachen Klick sein Aufklappen
            //  wieder ab (der ist beim Doppelklick zwangslaeufig schon
            //  gelaufen; deshalb wartet er auf `folderClickTimer`).
            if (tile.isFolder) {
                //  Der einfache Klick ist schon gelaufen und hat umgeschaltet -
                //  hier wieder zurueck, damit der Zustand stimmt, falls das
                //  Oeffnen nicht zustande kommt.
                if (mouse.button === Qt.LeftButton) {
                    mediaModel.toggleFolder(tile.filePath)
                    tile.folderOpenRequested(tile.filePath)
                }
                return
            }
            if (mouse.button === Qt.LeftButton)
                tile.activated(tile.filePath)
        }
        onClicked: function(mouse) {
            // Ohne aktiven View-Modus öffnet Rechtsklick das Kontextmenü
            // (Tag/Kategorie hinzufügen). Die Modus-Interaktionen (Group/
            // Add-to-Tag) bleiben unverändert und haben Vorrang.
            //  Linksklick klappt SOFORT auf bzw. zu. Auf die Doppelklick-Frist
            //  zu warten war zwar sauber, fuehlte sich aber wie eine halbe
            //  Sekunde Haenger an (Nutzerbefund) - der Doppelklick nimmt das
            //  Aufklappen stattdessen zurueck, bevor er den Ordner oeffnet.
            if (tile.isFolder) {
                if (mouse.button === Qt.LeftButton)
                    mediaModel.toggleFolder(tile.filePath)
                else if (mouse.button === Qt.RightButton)
                    tile.openFolderMenu()
                return
            }
            if (tile.modeTag.length === 0) {
                if (mouse.button === Qt.RightButton)
                    tile.openContextMenu()
                else if (mouse.button === Qt.LeftButton && !tile.unavailable)
                    tile.fileClicked(tile.filePath)
                return
            }
            if (tile.tagMode === 2 && mouse.button === Qt.LeftButton)
                mediaModel.toggleTag(tile.filePath, tile.modeTag)
            else if (tile.tagMode === 1 && mouse.button === Qt.RightButton)
                mediaModel.toggleTag(tile.filePath, tile.modeTag)
        }
    }

    // ── Hover-Hinweis, wenn der Typ mangels Bibliothek nicht öffenbar ist ────
    //  HoverHandler statt hoverEnabled an der MouseArea: der Handler läuft
    //  parallel und nimmt der MouseArea keine Klicks weg.
    HoverHandler { id: tileHover }
    ToolTip.visible: tileHover.hovered && tile.unavailable
    ToolTip.delay: 500
    ToolTip.text: App.uiText(App.language, "LibMissingZlib")

    // ── Kontextmenü: Tag / Kategorie hinzufügen ──────────────────────────────
    //  Speist sich beim Öffnen frisch aus der JSON-Persistenz (App.allTags /
    //  Tags.categoriesFlat). Bereits zugewiesene Werte sind angehakt; erneutes
    //  Auswählen entfernt sie wieder (Toggle). Mutationen laufen über
    //  mediaModel.toggleTag (Tags, aktualisiert TagsRole) bzw.
    //  Tags.toggleFileInCategory (direkte Datei↔Kategorie-Mitgliedschaft).
    readonly property string fileName: filePath.substring(
        Math.max(filePath.lastIndexOf("/"), filePath.lastIndexOf("\\")) + 1)

    //  LAZY: Das Menü (samt zwei Untermenüs und deren Repeatern) wird erst beim
    //  ersten Rechtsklick erzeugt. Als direktes Kind entstand es bei JEDEM
    //  Delegate - bei ~40 sichtbaren Kacheln also 120 Popup-Instanzen (Menu ist
    //  ein Popup mit eigenem Hintergrund, contentItem und ListView), obwohl
    //  höchstens eines je gleichzeitig sichtbar ist. Das war der grösste
    //  Einzelposten beim Aufbau und Recyceln der Galerie-Delegates.
    Loader {
        id: ctxLoader
        active: false
        sourceComponent: ctxMenuComponent
    }
    function openContextMenu() {
        ctxLoader.active = true
        if (ctxLoader.item)
            ctxLoader.item.popup()
    }

    //  Ordner haben ihr eigenes, kurzes Menü - Tags und Kategorien gehören
    //  Dateien, ein Ordner will geöffnet, umbenannt oder gelöscht werden.
    //  Ebenfalls lazy: sonst entstünde je Ordnerkachel ein Popup.
    Loader {
        id: folderCtxLoader
        active: false
        sourceComponent: folderMenuComponent
    }
    function openFolderMenu() {
        folderCtxLoader.active = true
        if (folderCtxLoader.item)
            folderCtxLoader.item.popup()
    }

    Component {
        id: folderMenuComponent
        ThemedMenu {
            MenuItem {
                text: App.uiText(App.language, "CtxFolderOpen")
                onTriggered: tile.folderOpenRequested(tile.filePath)
            }
            MenuItem {
                text: App.uiText(App.language, "CtxFolderRename")
                onTriggered: tile.folderRenameRequested(tile.filePath, tile.displayName)
            }
            MenuSeparator {}
            MenuItem {
                text: App.uiText(App.language, "CtxFolderDelete")
                onTriggered: tile.folderDeleteRequested(tile.filePath, tile.displayName,
                                                        tile.childCount)
            }
        }
    }

    Component {
        id: ctxMenuComponent

        ThemedMenu {
            id: ctxMenu
            property var ctxTags: []       // alle Tags (JSON)
            property var ctxCats: []       // flacher Kategorienbaum [{id,name,color}]
            property var fileTags: []      // Tags der Datei
            property var fileCatIds: []    // Kategorie-IDs der Datei
            //  Bitmaske der vorhandenen Begleitdateien (1 Sidecar, 2 .bak).
            property int companions: 0
            //  Lohnt „Ton sichern"? Reine Endungs-Prüfung in C++ - das Öffnen
            //  eines Menüs soll keine Datei anfassen.
            property bool canExtractAudio: false
            onAboutToShow: {
                ctxMenu.companions = mediaModel.companionKinds(tile.filePath)
                ctxMenu.canExtractAudio = tile.mediaType === 1
                                          && Audio.canExtractAudio(tile.filePath)
                ctxTags    = Tags.allTags()
                ctxCats    = Tags.categoriesFlat()
                fileTags   = mediaModel.tagsOfFile(tile.filePath)
                fileCatIds = Tags.categoryIdsForFile(tile.fileName)
            }

            ThemedMenu {
                title: App.uiText(App.language, "CtxAddTag")
                //  Anlegen und in einem Zug zuweisen - sonst müsste man den Tag
                //  erst im Panel erstellen und dann hier suchen.
                MenuItem {
                    text: "+  " + App.uiText(App.language, "CatPanelNewTag")
                    onTriggered: tile.newTagRequested(tile.filePath)
                }
                MenuSeparator {}
                MenuItem {
                    visible: ctxMenu.ctxTags.length === 0
                    height: visible ? implicitHeight : 0
                    enabled: false
                    text: App.uiText(App.language, "FilterNoTagsShort")
                }
                Repeater {
                    model: ctxMenu.ctxTags
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData
                        checkable: true
                        checked: ctxMenu.fileTags.indexOf(modelData) >= 0
                        onTriggered: mediaModel.toggleTag(tile.filePath, modelData)
                    }
                }
            }
            ThemedMenu {
                title: App.uiText(App.language, "CtxAddCategory")
                MenuItem {
                    text: "+  " + App.uiText(App.language, "CatPanelAddCategory")
                    onTriggered: tile.newCategoryRequested(tile.filePath)
                }
                MenuSeparator {}
                MenuItem {
                    visible: ctxMenu.ctxCats.length === 0
                    height: visible ? implicitHeight : 0
                    enabled: false
                    text: App.uiText(App.language, "CtxNoCategories")
                }
                Repeater {
                    model: ctxMenu.ctxCats
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name
                        checkable: true
                        checked: ctxMenu.fileCatIds.indexOf(modelData.id) >= 0
                        onTriggered: Tags.toggleFileInCategory(modelData.id, tile.fileName)
                    }
                }
            }

            MenuSeparator {}
            //  Umbenennen. Im Kachel-Modus geht es auch über den Doppelklick auf
            //  den Namen im Overlay; in der Liste gibt es kein Overlay.
            MenuItem {
                text: App.uiText(App.language, "CtxRenameFile")
                onTriggered: tile.renameRequested(tile.filePath, tile.displayName)
            }
            // Datei löschen (in den Papierkorb) - Bestätigung übernimmt der
            // gemeinsame Dialog in GalleryView (deleteRequested-Signal).
            MenuItem {
                text: App.uiText(App.language, "CtxDeleteFile")
                onTriggered: tile.deleteRequested(tile.filePath, tile.displayName)
            }

            //  ── Ton aus dem Video sichern ────────────────────────────────────
            //  Nur bei Videokacheln und nur für Hüllen, die der Leser kennt
            //  (MP4/M4V/MOV). Das Video bleibt unangetastet.
            MenuItem {
                visible: ctxMenu.canExtractAudio
                height: visible ? implicitHeight : 0
                enabled: !Audio.extractBusy
                text: App.uiText(App.language, "AudioExtractMenu")
                onTriggered: tile.audioExtractRequested(tile.filePath)
            }

            //  ── Begleitdateien dieser Datei ──────────────────────────────────
            //  Erscheinen nur, wenn es sie gibt (beim Öffnen des Menüs geprüft).
            //  Die Datei selbst bleibt unangetastet.
            MenuItem {
                visible: (ctxMenu.companions & 1) !== 0
                height: visible ? implicitHeight : 0
                text: App.uiText(App.language, "CtxRemoveEdits")
                onTriggered: tile.companionRemoveRequested(tile.filePath, 1)
            }
            MenuItem {
                visible: (ctxMenu.companions & 2) !== 0
                height: visible ? implicitHeight : 0
                text: App.uiText(App.language, "CtxRemoveBackup")
                onTriggered: tile.companionRemoveRequested(tile.filePath, 2)
            }
        }
    }

    // ── Info-Overlay (Name/Datum/Tags, Inline-Rename) ───────────────────────
    MediaOverlay {
        //  Ordner tragen ihren Namen selbst; Umbenennen, Tags und Datum
        //  gehoeren zu den Ordner-Operationen, nicht hierher.
        //  In der Liste hätte der Streifen die ganze Zeile verdeckt - dort
        //  stehen Name und Tags ohnehin sichtbar, der Rest im Kontextmenü.
        visible: !tile.isFolder && !tile.listMode
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        filePath: tile.filePath
        displayName: tile.displayName
        tags: tile.tags
        dateTime: tile.dateTime
        compact: !tile.optionsVisible
    }
}
