import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

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
    property int    mediaType: 5      // 0 Image,1 Video,2 Audio,3 Pdf,4 Text,5 Unknown
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

    readonly property bool tagged: modeTag.length > 0 && tags.indexOf(modeTag) >= 0
    readonly property bool dimmed: tagMode === 1 && modeTag.length > 0 && !tagged

    // Rundum gleichmaessig abgerundet (oben wie unten).
    radius: 10
    color: App.themeCard
    clip: true
    opacity: dimmed ? 0.45 : 1.0
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

    // ── Interaktion (Bildbereich): Aktivieren / Tag-Toggle / Kontextmenü ─────
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onDoubleClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton)
                tile.activated(tile.filePath)
        }
        onClicked: function(mouse) {
            // Ohne aktiven View-Modus öffnet Rechtsklick das Kontextmenü
            // (Tag/Kategorie hinzufügen). Die Modus-Interaktionen (Group/
            // Add-to-Tag) bleiben unverändert und haben Vorrang.
            if (tile.modeTag.length === 0) {
                if (mouse.button === Qt.RightButton)
                    ctxMenu.popup()
                return
            }
            if (tile.tagMode === 2 && mouse.button === Qt.LeftButton)
                mediaModel.toggleTag(tile.filePath, tile.modeTag)
            else if (tile.tagMode === 1 && mouse.button === Qt.RightButton)
                mediaModel.toggleTag(tile.filePath, tile.modeTag)
        }
    }

    // ── Kontextmenü: Tag / Kategorie hinzufügen ──────────────────────────────
    //  Speist sich beim Öffnen frisch aus der JSON-Persistenz (App.allTags /
    //  Tags.categoriesFlat). Bereits zugewiesene Werte sind angehakt; erneutes
    //  Auswählen entfernt sie wieder (Toggle). Mutationen laufen über
    //  mediaModel.toggleTag (Tags, aktualisiert TagsRole) bzw.
    //  Tags.toggleFileInCategory (direkte Datei↔Kategorie-Mitgliedschaft).
    readonly property string fileName: filePath.substring(
        Math.max(filePath.lastIndexOf("/"), filePath.lastIndexOf("\\")) + 1)

    Menu {
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

        Menu {
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
        Menu {
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
