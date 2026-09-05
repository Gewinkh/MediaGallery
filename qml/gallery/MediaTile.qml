import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// Eine Galerie-Kachel. KEINE Pixmap im RAM: das Image lädt die kleine Cache-Datei asynchron, `sourceSize` ==
// Kachelgröße, damit nie in Vollauflösung dekodiert wird. Nicht sichtbare Kacheln recycelt der GridView.
Rectangle {
    id: tile

    property string filePath: ""
    property string displayName: ""
    property int    mediaType: 6      // 0 Image,1 Video,2 Audio,3 Pdf,4 Text,5 Docx,6 Unknown,7 Folder
    property bool   expanded: false
    property int    childCount: -1
    property string typeLabel: ""
    property var    tags: []
    property var    dateTime
    property string thumbUrl: ""
    property int    thumbState: 0     // 0 pending,1 ready,2 failed
    readonly property int thumbStatus: thumb.status

    property int    tagMode: 0        // 0 none,1 group,2 addToTag
    property string modeTag: ""

    property bool   covered: false

    // Der ZUSTAND kommt aus dem Modell, die ABSICHT geht als Signal an die Galerie: nur die kennt die Ansichts-
    // Reihenfolge, und ein Umschalt-Bereich meint "von dort bis hier" in genau dieser Ordnung.
    property bool selected: false
    property int  proxyRow: -1

    signal selectRequested(int proxyRow, int modifiers)
    signal selectionResetRequested()

    readonly property bool multi: tile.selected && mediaModel.selectionCount > 1
    readonly property int  multiCount: tile.multi ? mediaModel.selectionCount : 1

    signal activated(string filePath)
    signal fileClicked(string filePath)
    signal deleteRequested(string filePath, string displayName)
    signal copyRequested(string filePath)
    signal deleteSelectionRequested(int count)
    signal renameRequested(string filePath, string currentName)
    signal newTagRequested(string filePath)
    signal newCategoryRequested(string filePath)
    signal companionRemoveRequested(string filePath, int kind)
    signal audioExtractRequested(string filePath)
    signal folderOpenRequested(string folderPath)
    signal folderRenameRequested(string folderPath, string currentName)
    signal folderDeleteRequested(string folderPath, string displayName, int itemCount)
    property bool dropTarget: false

    readonly property bool tagged: modeTag.length > 0 && tags.indexOf(modeTag) >= 0
    readonly property bool dimmed: tagMode === 1 && modeTag.length > 0 && !tagged

    // DOCX braucht ZLIB - ohne sie lässt sich eine .docx nicht einmal LESEN (ZIP legt Einträge als rohes Deflate
    // ab). Die Kachel bleibt ausgegraut stehen; Verschlagworten, Umbenennen und Löschen gehen weiter.
    readonly property bool unavailable: mediaType === 5 && !App.docxAvailable

    readonly property bool isFolder: mediaType === 7


    property bool listMode: false
    property bool optionsVisible: App.optionsVisible

    readonly property bool playing: Audio.owner !== null && tile.filePath.length > 0
                                    && tile.filePath === Audio.currentPath

    radius: tile.listMode ? 6 : 10
    color: (tile.isFolder && tile.expanded)
           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.13)
           : App.themeCard
    clip: true
    opacity: (dimmed || unavailable) ? 0.45 : 1.0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    border.width: tile.selected ? 3
                  : (tagged || tile.playing || (tile.isFolder && tile.expanded)) ? 2 : 1
    border.color: (tile.selected || tagged || tile.playing
                   || (tile.isFolder && tile.expanded))
                  ? App.themeAccent : App.themeBorder

    Text {
        anchors.centerIn: parent
        visible: !tile.listMode && !tile.isFolder && tile.thumbState !== 1 && !tile.covered
        text: tile.thumbState === 2 ? "\u26A0" : "\u2026"
        color: App.themeTextMuted
        font.pixelSize: 22
    }

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

    Column {
        visible: tile.isFolder && !tile.listMode
        anchors.centerIn: parent
        width: parent.width - 16
        spacing: Math.max(4, tile.height * 0.04)

        DrawnIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: tile.expanded ? "folder-open" : "folder"
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
            visible: tile.childCount >= 0
            text: tile.childCount === 0
                  ? App.uiText(App.language, "FolderEmpty")
                  : App.uiText(App.language, "FolderMediaCount").arg(tile.childCount)
            color: App.themeTextMuted
            font.pixelSize: Math.max(9, Math.min(13, tile.height * 0.07))
            elide: Text.ElideRight
        }
    }

    readonly property real listScale: tile.height / 46
    Item {
        visible: tile.listMode
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 10

        Item {
            id: listArt
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: Math.max(24, tile.height - 12)
            height: width

            // Textdateien zeigen immer ihren Typ, nie den Inhalt - unabhängig von der Einstellung: bei rund 30 px ist von
            // fünf Zeilen Quelltext nichts zu erkennen, der Typ dagegen sofort.
            readonly property bool zeigeTyp: !tile.isFolder && tile.mediaType === 4
                                             && !tile.covered
                                             && tile.typeLabel.length > 0
                                             && !tile.covered

            Image {
                anchors.fill: parent
                visible: !tile.isFolder && !listArt.zeigeTyp && tile.thumbState === 1
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
            Text {
                anchors.centerIn: parent
                visible: listArt.zeigeTyp
                text: tile.typeLabel
                color: App.themeTextMuted
                font.bold: true
                font.pixelSize: Math.max(7, Math.min(parent.width * 0.42,
                                                     parent.width / (0.75 * Math.max(2, tile.typeLabel.length))))
                elide: Text.ElideRight
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }
            DrawnIcon {
                anchors.centerIn: parent
                visible: !listArt.zeigeTyp
                         && (tile.isFolder || tile.covered || tile.thumbState !== 1)
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
            font.pixelSize: Math.round(Math.max(10, Math.min(24, 13 * tile.listScale)))
            font.bold: tile.playing
            elide: Text.ElideMiddle
        }

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
                    width: Math.round(Math.max(7, Math.min(16, 9 * tile.listScale)))
                    height: width
                    radius: width / 2
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
            font.pixelSize: Math.round(Math.max(9, Math.min(18, 11 * tile.listScale)))
        }
    }

    // AUCH an `mediaType` hängen, nicht nur an `filePath`: beim Recyceln werden die Kacheldaten der Reihe nach
    // durchgereicht, und `filePath` steht VOR `mediaType`. Von Datei auf Ordner umgesetzt lief der Handler sonst
    // mit dem alten Typ, die Anfrage entfiel, und die Zeile blieb leer.
    function _wantFolderCount() {
        if (tile.mediaType === 7 && tile.filePath.length > 0)
            mediaModel.ensureFolderCount(tile.filePath)
    }
    onFilePathChanged:     tile._wantFolderCount()
    onMediaTypeChanged:    tile._wantFolderCount()
    Component.onCompleted: tile._wantFolderCount()

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

    // Der Zug-Träger liegt EINMAL in der GalleryView, nicht hier: `Drag.active = true` blockiert bis zum Loslassen,
    // und ein Randscrollen räumte dabei genau diese Kachel weg - die App stürzte ab. Die Kachel bittet nur noch.
    signal dragStartRequested(string filePath, string thumbUrl)

    Rectangle {
        visible: tile.playing
        anchors.fill: parent
        radius: tile.radius
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.14)
    }

    Rectangle {
        visible: tile.selected
        anchors.fill: parent
        radius: tile.radius
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
    }
    Rectangle {
        visible: tile.selected
        anchors { left: parent.left; top: parent.top; margins: 5 }
        width: 18; height: 18; radius: 9
        color: App.themeAccent
        DrawnIcon {
            anchors.centerIn: parent
            name: "check"
            size: 13
            color: App.themeCard
        }
    }

    Rectangle {
        visible: tile.dropTarget
        anchors.fill: parent
        radius: tile.radius
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
        border.width: 2
        border.color: App.themeAccent
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property point pressPos: Qt.point(0, 0)
        property bool  dragArmed: false
        onPressed: function(mouse) {
            pressPos = Qt.point(mouse.x, mouse.y)
            dragArmed = (mouse.button === Qt.LeftButton)
                        && !tile.unavailable && !tile.isFolder
        }
        onReleased: dragArmed = false
        onPositionChanged: function(mouse) {
            if (!dragArmed || tile.filePath.length === 0)
                return
            const dx = mouse.x - pressPos.x
            const dy = mouse.y - pressPos.y
            // Reine Bewegungsschwelle, keine Richtungsprüfung: seit die Galerie nicht mehr per Ziehen scrollt, gehört jeder
            // Zug auf einer Kachel dem Herausziehen. Die Schwelle bleibt - die ersten Pixel eines Klicks zittern.
            if (Math.abs(dx) < 12 && Math.abs(dy) < 12)
                return                            // noch unentschieden
            dragArmed = false
            // Den Zug startet `Drag.active = true`, nicht `startDrag()`: das verlangt umgekehrt ein bereits aktives
            // `active` und meldet sonst nur eine Warnung nach journald - daran ist das Ziehen lautlos gescheitert.
            // Die Zuweisung blockiert bis zum Ende des Zuges, deshalb lässt sich die Ablegeleiste davor einblenden.
            tile.dragStartRequested(tile.filePath, tile.thumbUrl)
        }
        onDoubleClicked: function(mouse) {
            if (tile.unavailable)
                return
            if (tile.isFolder) {
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
            if (mouse.button === Qt.LeftButton
                && (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                tile.selectRequested(tile.proxyRow, mouse.modifiers)
                return
            }
            if (mouse.button === Qt.LeftButton || !tile.selected)
                tile.selectionResetRequested()

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

    HoverHandler { id: tileHover }
    ToolTip.visible: tileHover.hovered && tile.unavailable
    ToolTip.delay: 500
    ToolTip.text: App.uiText(App.language, "LibMissingZlib")

    // Speist sich beim Öffnen frisch aus der Persistenz (`App.allTags` / `Tags.categoriesFlat`); Zugewiesenes ist
    // angehakt, erneutes Auswählen entfernt es wieder.
    readonly property string fileName: filePath.substring(
        Math.max(filePath.lastIndexOf("/"), filePath.lastIndexOf("\\")) + 1)

    // LAZY: als direktes Kind entstand das Menü bei JEDEM Delegate - bei ~40 sichtbaren Kacheln 120 Popup-Instanzen,
    // obwohl höchstens eines sichtbar ist. Das war der größte Einzelposten beim Aufbau der Delegates.
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
                text: tile.multi
                      ? App.uiText(App.language, "CtxDeleteSelection")
                            .replace("%1", tile.multiCount)
                      : App.uiText(App.language, "CtxFolderDelete")
                onTriggered: {
                    if (tile.multi) tile.deleteSelectionRequested(tile.multiCount)
                    else            tile.folderDeleteRequested(tile.filePath,
                                                               tile.displayName,
                                                               tile.childCount)
                }
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
            property int companions: 0
            property bool canExtractAudio: false
            onAboutToShow: {
                ctxMenu.companions = mediaModel.companionKinds(tile.filePath)
                ctxMenu.canExtractAudio = tile.mediaType === 1
                                          && Audio.canExtractAudio(tile.filePath)
                ctxTags    = Tags.allTags()
                ctxCats    = Tags.categoriesFlat()
                fileTags   = tile.multi ? mediaModel.tagsOfSelection()
                                        : mediaModel.tagsOfFile(tile.filePath)
                fileCatIds = tile.multi
                             ? Tags.categoryIdsForFiles(mediaModel.selectedFileNames())
                             : Tags.categoryIdsForFile(tile.fileName)
            }

            ThemedMenu {
                title: App.uiText(App.language, "CtxAddTag")
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
                        onTriggered: {
                            if (tile.multi)
                                mediaModel.setTagOnSelection(
                                    modelData, ctxMenu.fileTags.indexOf(modelData) < 0)
                            else
                                mediaModel.toggleTag(tile.filePath, modelData)
                        }
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
                        onTriggered: {
                            if (tile.multi)
                                Tags.setFilesInCategory(
                                    modelData.id, mediaModel.selectedFileNames(),
                                    ctxMenu.fileCatIds.indexOf(modelData.id) < 0)
                            else
                                Tags.toggleFileInCategory(modelData.id, tile.fileName)
                        }
                    }
                }
            }

            MenuSeparator {}
            MenuItem {
                text: App.uiText(App.language, "CtxRenameFile")
                onTriggered: tile.renameRequested(tile.filePath, tile.displayName)
            }
            MenuItem {
                text: App.uiText(App.language, "CtxCopyFiles")
                onTriggered: tile.copyRequested(tile.filePath)
            }
            MenuItem {
                text: tile.multi
                      ? App.uiText(App.language, "CtxDeleteSelection")
                            .replace("%1", tile.multiCount)
                      : App.uiText(App.language, "CtxDeleteFile")
                onTriggered: {
                    if (tile.multi) tile.deleteSelectionRequested(tile.multiCount)
                    else            tile.deleteRequested(tile.filePath, tile.displayName)
                }
            }

            MenuItem {
                visible: ctxMenu.canExtractAudio
                height: visible ? implicitHeight : 0
                enabled: !Audio.extractBusy
                text: App.uiText(App.language, "AudioExtractMenu")
                onTriggered: tile.audioExtractRequested(tile.filePath)
            }

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

    MediaOverlay {
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
