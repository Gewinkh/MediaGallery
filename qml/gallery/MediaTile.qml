import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  MediaTile.qml — eine Galerie-Kachel (Delegate-Inhalt).
//
//  Ersetzt MediaThumbnail(QWidget). KEINE Pixmap im RAM gehalten: das Image lädt
//  die kleine Disk-Cache-Datei (thumbUrl) asynchron; sourceSize == Kachelgröße,
//  damit nie in Vollauflösung dekodiert wird. Nicht sichtbare Kacheln werden vom
//  GridView recycelt → nur sichtbare Thumbnails belegen Speicher.
//
//  Interaktionen: Doppelklick (Bildbereich) → activated(); Inline-Rename
//  (Doppelklick auf Name, im Overlay); Tag-Toggle je nach View-Modus.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: tile

    // Model-Rollen (vom Delegate gesetzt)
    property string filePath: ""
    property string displayName: ""
    property int    mediaType: 6      // 0 Image,1 Video,2 Audio,3 Pdf,4 Text,5 Docx,6 Unknown
    property string typeLabel: ""
    property var    tags: []
    property var    dateTime
    property string thumbUrl: ""
    property int    thumbState: 0     // 0 pending,1 ready,2 failed

    // View-Modus (von GalleryView)
    property int    tagMode: 0        // 0 none,1 group,2 addToTag
    property string modeTag: ""

    // Vorschau-Sperre (Privatsphäre, Taste "B"): verdeckt das Thumbnail.
    property bool   covered: false

    signal activated(string filePath)
    // Kontextmenü „Datei löschen…" → GalleryView zeigt EINEN gemeinsamen
    // Bestätigungs-Dialog (kein Dialog je Kachel) und ruft mediaModel.deleteItem.
    signal deleteRequested(string filePath, string displayName)

    readonly property bool tagged: modeTag.length > 0 && tags.indexOf(modeTag) >= 0
    readonly property bool dimmed: tagMode === 1 && modeTag.length > 0 && !tagged

    //  DOCX braucht ZLIB (s. src/core/ZCodec.h) — ohne sie lässt sich eine .docx
    //  nicht einmal LESEN (ZIP legt Einträge als rohes Deflate ab). Die Kachel
    //  bleibt samt Namen stehen, ist aber ausgegraut und nicht zu öffnen; der
    //  Hover-Text nennt die fehlende Bibliothek (Muster wie FilterBar.qml).
    //  Verschlagworten, Umbenennen und Löschen funktionieren weiter.
    readonly property bool unavailable: mediaType === 5 && !App.docxAvailable

    // Rundum gleichmaessig abgerundet (oben wie unten).
    radius: 10
    color: App.themeCard
    clip: true
    opacity: (dimmed || unavailable) ? 0.45 : 1.0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    border.width: tagged ? 2 : 1
    border.color: tagged ? App.themeAccent : App.themeBorder

    // ── Platzhalter (während/!= ready) ──────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: tile.thumbState !== 1 && !tile.covered
        text: tile.thumbState === 2 ? "\u26A0" : "\u2026"
        color: App.themeTextMuted
        font.pixelSize: 22
    }

    // ── Thumbnail (Disk-Cache, asynchron) ───────────────────────────────────
    Image {
        id: thumb
        anchors.fill: parent
        anchors.margins: 1
        visible: tile.thumbState === 1 && status === Image.Ready && !tile.covered
        source: tile.thumbUrl
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: App.tileWidth
        sourceSize.height: App.tileHeight
        mipmap: true
    }

    // ── Video-Play-Glyph (zentral) ──────────────────────────────────────────
    Rectangle {
        visible: tile.mediaType === 1 && !tile.covered
        anchors.centerIn: parent
        width: 40; height: 40; radius: 20
        color: Qt.rgba(0, 0, 0, 0.45)
        Text {
            anchors.centerIn: parent
            text: "\u25B6"
            color: "white"
            font.pixelSize: 18
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

    // ── Datei aus der App HERAUSziehen (in Dateimanager, Mail, Chat, Upload) ─
    //  `Drag.Automatic` + `startDrag()` ist der EINZIGE Weg, der das Fenster
    //  verlässt: das sonst im Projekt benutzte `Drag.active` (mit `Drag.keys`
    //  und `DropArea`) bleibt app-intern. Übergeben wird `text/uri-list` — also
    //  die DATEI, wie aus einem Dateimanager; damit nimmt sie jedes Ziel an,
    //  das Dateien annimmt (Anhänge, Uploads, Chat-Fenster).
    //  **Nur kopieren:** mit `MoveAction` dürfte ein Zielprogramm die Datei aus
    //  dem Ordner des Nutzers ENTFERNEN.
    Item {
        id: dragPayload
        anchors.fill: parent
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction
        Drag.mimeData: { "text/uri-list": App.fileUrl(tile.filePath) }
        //  Am Zeiger hängt die Miniatur der Kachel — sonst zieht man ins Blaue.
        Drag.imageSource: tile.thumbUrl
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
            dragArmed = (mouse.button === Qt.LeftButton) && !tile.unavailable
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
            //  sonst meldet es nur „startDrag() drag must be active" — eine
            //  Warnung, die nach journald geht und deshalb unsichtbar blieb.
            //  Genau daran ist das Ziehen bisher lautlos gescheitert.
            //  Die Zuweisung BLOCKIERT bis zum Ende des Zuges und setzt `active`
            //  danach selbst wieder auf false. Genau deshalb kann die Leiste zum
            //  Ablegen (Lesezeichen) davor eingeblendet und danach wieder
            //  abgeräumt werden — dazwischen läuft die Ereignisschleife des Zuges.
            App.beginTileDrag()
            dragPayload.Drag.active = true
            App.endTileDrag()
        }
        onDoubleClicked: function(mouse) {
            // Ohne ZLIB öffnet die DOCX-Kachel nicht — der Editor bliebe leer.
            if (tile.unavailable)
                return
            if (mouse.button === Qt.LeftButton)
                tile.activated(tile.filePath)
        }
        onClicked: function(mouse) {
            // Ohne aktiven View-Modus öffnet Rechtsklick das Kontextmenü
            // (Tag/Kategorie hinzufügen). Die Modus-Interaktionen (Group/
            // Add-to-Tag) bleiben unverändert und haben Vorrang.
            if (tile.modeTag.length === 0) {
                if (mouse.button === Qt.RightButton)
                    tile.openContextMenu()
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
    //  Delegate — bei ~40 sichtbaren Kacheln also 120 Popup-Instanzen (Menu ist
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

    Component {
        id: ctxMenuComponent

        ThemedMenu {
            id: ctxMenu
            property var ctxTags: []       // alle Tags (JSON)
            property var ctxCats: []       // flacher Kategorienbaum [{id,name,color}]
            property var fileTags: []      // Tags der Datei
            property var fileCatIds: []    // Kategorie-IDs der Datei
            onAboutToShow: {
                ctxTags    = App.allTags()
                ctxCats    = Tags.categoriesFlat()
                fileTags   = App.tagsForFile(tile.fileName)
                fileCatIds = Tags.categoryIdsForFile(tile.fileName)
            }

            ThemedMenu {
                title: App.uiText(App.language, "CtxAddTag")
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
            // Datei löschen (in den Papierkorb) — Bestätigung übernimmt der
            // gemeinsame Dialog in GalleryView (deleteRequested-Signal).
            MenuItem {
                text: App.uiText(App.language, "CtxDeleteFile")
                onTriggered: tile.deleteRequested(tile.filePath, tile.displayName)
            }
        }
    }

    // ── Info-Overlay (Name/Datum/Tags, Inline-Rename) ───────────────────────
    MediaOverlay {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        filePath: tile.filePath
        displayName: tile.displayName
        tags: tile.tags
        dateTime: tile.dateTime
        compact: !App.optionsVisible
    }
}
