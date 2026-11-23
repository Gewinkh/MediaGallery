pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import MediaGallery 1.0
import "../common"

// Ein Panel, zwei gleichwertige Abschnitte (Tags als Chips, Kategorien als rekursiver CategoryNode-Baum) mit
// gemeinsamem SectionHeader. Referenzquelle des Tag-Filters ist AUSSCHLIESSLICH der Proxy, `activeTagFilter`
// nur ein Spiegel. Beim Abwählen gehen abhängige Unterkategorien mit - ausser ein anderer Filter hält sie.
Rectangle {
    id: panel
    color: App.themeSidebarBg
    border.color: App.themeBorder
    border.width: 1

    signal enterAddToTagMode(string tag)
    signal enterGroupMode(string tag)

    property bool showTagsSection: true
    property bool showCategoriesSection: true

    // Die Tags des EIGENEN Ordners, nicht die der fokussierten Hälfte: `Tags` ist appweit und folgt dem Fokus, der
    // dem Mauszeiger folgt - der Inhalt wechselte, sobald die Maus über die andere Hälfte fuhr.
    property var tagsCtl: Tags
    property var folderSource: App

    // EINE Rückgängig-Leiste am Fuß des Panels. Zwei getrennte (Tags/Kategorien) waren überlegt und verworfen -
    // viele Vorgänge fassen beides an.
    readonly property bool hasUndo: (panel.tagsCtl && (panel.tagsCtl.canUndo
                                                       || panel.tagsCtl.canRedo)) ? true : false
    readonly property int  undoBarHeight: panel.hasUndo ? 44 : 0

    property var tree: []
    property var allTagsModel: []
    property var activeCategories: []
    property var activeTagFilter: []

    // Zähler, der bei jeder Tag-/Kategorie-Änderung hochgeht - die Abhängigkeit, die den Farb-Bindungen fehlte:
    // `tagColor(..)` ist ein Funktionsaufruf. `allTags` liefert bei reiner Farbänderung dieselbe Liste.
    property int tagRev: 0
    function tagColorOf(tag) {
        void panel.tagRev                 // s. oben - die Bindung braucht sie
        return panel.tagsCtl.tagColor(tag)
    }

    function refresh() {
        tree = panel.tagsCtl.categoriesTree()
        allTagsModel = panel.tagsCtl.allTags()
        panel.tagRev++
    }
    Component.onCompleted: {
        refresh()
        activeTagFilter = galleryModel.tagFilter
    }
    Connections {
        target: panel.tagsCtl
        function onCategoriesChanged() { panel.refresh() }
        function onTagsChanged()       { panel.refresh() }
    }
    Connections {
        target: panel.folderSource
        function onFolderOpened(path) { panel.refresh() }
    }
    Connections {
        target: galleryModel
        function onFilterChanged() { panel.activeTagFilter = galleryModel.tagFilter }
    }

    function isCategoryActive(id) { return activeCategories.indexOf(id) >= 0 }
    function isTagActive(tag)     { return activeTagFilter.indexOf(tag) >= 0 }

    function filterList(names, needle) {
        const n = (needle || "").trim().toLowerCase()
        if (n.length === 0) return names
        var out = []
        for (var i = 0; i < names.length; ++i)
            if (String(names[i]).toLowerCase().indexOf(n) >= 0) out.push(names[i])
        return out
    }
    function filterCats(needle) {
        const all = panel.tagsCtl.categoriesFlat()
        const n = (needle || "").trim().toLowerCase()
        var out = []
        for (var i = 0; i < all.length; ++i)
            if (n.length === 0 || String(all[i].name).toLowerCase().indexOf(n) >= 0)
                out.push(all[i])
        return out
    }

    // Referenz-Helfer für die Kaskadenlogik
    //  Alle Prüfungen laufen über den aktuellen Baum (tree) - per ID, nicht per
    //  Name (Referenzbasis: TagCategory.id).
    function _findNode(nodes, id) {
        for (var i = 0; i < nodes.length; i++) {
            if (nodes[i].id === id) return nodes[i]
            var f = _findNode(nodes[i].children, id)
            if (f) return f
        }
        return null
    }
    function _subtreeIds(node) {
        var out = [node.id]
        for (var i = 0; i < node.children.length; i++)
            out = out.concat(_subtreeIds(node.children[i]))
        return out
    }
    function _subtreeTags(node) {
        var out = node.tags.slice()
        for (var i = 0; i < node.children.length; i++) {
            var sub = _subtreeTags(node.children[i])
            for (var j = 0; j < sub.length; j++)
                if (out.indexOf(sub[j]) < 0) out.push(sub[j])
        }
        return out
    }
    function _ancestorIds(nodes, id) {
        for (var i = 0; i < nodes.length; i++) {
            if (nodes[i].id === id) return []
            var sub = _ancestorIds(nodes[i].children, id)
            if (sub !== null) { sub.push(nodes[i].id); return sub }
        }
        return null
    }

    function toggleCategory(id, on) {
        var a = activeCategories.slice()
        var i = a.indexOf(id)

        if (on) {
            if (i < 0) a.push(id)
            activeCategories = a
            galleryModel.categoryFilter = a
            return
        }
        if (i < 0) return
        a.splice(i, 1)

        var node = _findNode(tree, id)
        if (node) {
            var subIds = _subtreeIds(node)

            var kept = []
            for (var k = 0; k < a.length; k++) {
                var cid = a[k]
                if (subIds.indexOf(cid) < 0) { kept.push(cid); continue }   // unabhängig
                var anc = _ancestorIds(tree, cid)
                var referenced = false
                for (var j = 0; anc !== null && j < anc.length; j++) {
                    if (subIds.indexOf(anc[j]) >= 0) continue   // Referenz im entfernten Teilbaum zählt nicht
                    if (a.indexOf(anc[j]) >= 0) { referenced = true; break }
                }
                if (referenced) kept.push(cid)
            }
            a = kept

            var removedTags = _subtreeTags(node)
            if (removedTags.length > 0) {
                var stillRef = ({})
                for (k = 0; k < a.length; k++) {
                    var n2 = _findNode(tree, a[k])
                    if (!n2) continue
                    var ts = _subtreeTags(n2)
                    for (j = 0; j < ts.length; j++) stillRef[ts[j]] = true
                }
                var tf = galleryModel.tagFilter.slice()
                var out = []
                var changed = false
                for (k = 0; k < tf.length; k++) {
                    if (removedTags.indexOf(tf[k]) >= 0 && stillRef[tf[k]] !== true) {
                        changed = true
                        continue
                    }
                    out.push(tf[k])
                }
                if (changed) galleryModel.tagFilter = out
            }
        }

        activeCategories = a
        galleryModel.categoryFilter = a
    }

    function toggleTag(tag) {
        var a = galleryModel.tagFilter.slice()
        var i = a.indexOf(tag)
        if (i >= 0) a.splice(i, 1); else a.push(tag)
        galleryModel.tagFilter = a          // Spiegel folgt via onFilterChanged
    }
    function moveTag(tag, fromCat, toCat) { panel.tagsCtl.moveTagToCategory(tag, fromCat, toCat) }

    // Aufgerufen von den Chips dieses Panels UND von CategoryNode, deshalb liegen die Regeln an EINER Stelle.
    // Zugewiesen wird immer nur hinzufügend - ein Zug ist eine Zuweisung, kein Umschalter.
    function dropFilesOnTag(urls, tag) {
        for (var i = 0; i < urls.length; i++)
            mediaModel.addTag(App.localPath(urls[i]), tag)
    }
    function dropFilesOnCategory(urls, catId) {
        for (var i = 0; i < urls.length; i++) {
            var p = App.localPath(urls[i])
            if (!mediaModel.hasFile(p)) continue
            var name = String(p).split("/").pop()
            if (!panel.tagsCtl.fileInCategory(catId, name))
                panel.tagsCtl.toggleFileInCategory(catId, name)
        }
    }
    // Ein Tag-Chip, der ins Leere fällt, wird aus der Kategorie genommen, aus der er kam - die Regel gehört ins
    // Panel wie alle Ablege-Regeln, `CategoryNode` stellt nur fest, dass niemand den Zug annahm.
    readonly property bool editMode: (panel.folderSource
                                      && panel.folderSource.optionsVisible === true)

    function moveCategoryInto(catId, newParentId) {
        if (!catId || !newParentId || catId === newParentId) return
        // Nicht in den EIGENEN Teilbaum: `moveCategory` weist das ab, aber stillschweigend - auf dem Bildschirm sah es
        // eingefroren aus. Hier TAUSCHEN die beiden stattdessen ihre Plätze; ein Verschieben hinge den Ast in sich selbst.
        const node = panel._findNode(panel.tree, catId)
        if (node && panel._subtreeIds(node).indexOf(newParentId) >= 0) {
            panel.tagsCtl.swapCategories(catId, newParentId)
            return
        }
        panel.tagsCtl.moveCategory(catId, newParentId)
    }

    function dropCategoryOutside(catId) {
        if (!catId) return
        const oben = panel._ancestorIds(panel.tree, catId)
        if (!oben || oben.length === 0) return
        panel.tagsCtl.moveCategory(catId, "")
    }

    function dropTagOutside(tag, fromCat) {
        if (!fromCat || String(fromCat).length === 0) return
        panel.tagsCtl.removeTagFromCategory(fromCat, tag)
    }

    function requestAddToTagMode(tag) { panel.enterAddToTagMode(tag) }
    function requestGroupMode(tag)    { panel.enterGroupMode(tag) }

    function promptAddSubcategory(parentId) {
        namePrompt.title = App.uiText(App.language, "CatPanelNewSubcategory"); namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { panel.tagsCtl.addSubcategory(parentId, v, Qt.rgba(0,0.7,0.63,1), false) }
        namePrompt.open()
    }
    function promptAddTag(catId) {
        tagPick.targetCat = catId
        tagPick.open()
    }
    function promptRename(id, oldName) {
        namePrompt.title = App.uiText(App.language, "CatPanelRename"); namePrompt.value = oldName
        namePrompt.onAcceptFn = function(v) { panel.tagsCtl.renameCategory(id, v) }
        namePrompt.open()
    }
    function promptUniformColor(id) {
        colorDialog.targetCat = id
        colorDialog.selectedColor = panel.tagsCtl.categoryColor(id)
        colorDialog.open()
    }
    function promptDelete(id) { deleteCatId = id; confirmDelete.open() }
    property string deleteCatId: ""
    function promptDeleteTag(name) { deleteTagName = name; confirmDeleteTag.open() }
    property string deleteTagName: ""

    function promptNewTag() {
        namePrompt.title = App.uiText(App.language, "CatPanelNewTag")
        namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { panel.tagsCtl.createTag(v, Qt.rgba(0, 0.7, 0.63, 1)) }
        namePrompt.open()
    }
    function promptNewCategory() {
        namePrompt.title = App.uiText(App.language, "CatPanelAddCategory")
        namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { panel.tagsCtl.addRootCategory(v, Qt.rgba(0, 0.7, 0.63, 1), false) }
        namePrompt.open()
    }

    component SectionSearch: Rectangle {
        id: sf
        property alias text: sfInput.text
        property string placeholder: ""
        height: 26; radius: 6
        color: App.themeCard
        border.color: sfInput.activeFocus ? App.themeAccent : App.themeBorder
        border.width: 1

        DrawnIcon {
            id: sfIcon
            anchors { left: parent.left; leftMargin: 7; verticalCenter: parent.verticalCenter }
            name: "search"; size: 12
            color: sfInput.activeFocus ? App.themeAccent : App.themeTextMuted
        }
        TextField {
            id: sfInput
            anchors { left: sfIcon.right; leftMargin: 5; right: sfClear.left; rightMargin: 4
                      verticalCenter: parent.verticalCenter }
            height: parent.height - 2
            padding: 0
            font.pixelSize: 11
            color: App.themeTextPrimary
            placeholderText: sf.placeholder
            background: null
        }
        Rectangle {
            id: sfClear
            anchors { right: parent.right; rightMargin: 5; verticalCenter: parent.verticalCenter }
            visible: sfInput.text.length > 0
            width: 16; height: 16; radius: 8
            color: sfClearHover.hovered ? Qt.rgba(1, 1, 1, 0.16) : "transparent"
            DrawnIcon { anchors.centerIn: parent; name: "close"; size: 9
                        color: App.themeTextMuted }
            HoverHandler { id: sfClearHover }
            TapHandler { onTapped: sfInput.text = "" }
        }
    }

    // Rückgängig für TAG-Vorgänge, bewusst getrennt vom Datei-Stapel der Galerie: dieser trägt nur Tag- und
    // Kategorie-Vorgänge des eigenen Ordners und hat deshalb kein Tastenkürzel - ein zweites Strg+Z wäre nicht
    // vorhersagbar. Eine leere Marke bekommt keine Breite, die andere Seite den Platz.
    component MarkRow: Row {
        id: mr
        property var    marks: []
        property string iconName: ""
        property int    maxW: 0
        spacing: 3
        clip: true
        visible: mr.marks.length > 0

        DrawnIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: mr.iconName.length > 0
            width: visible ? 14 : 0
            name: mr.iconName.length > 0 ? mr.iconName : "trash"
            size: 14
            color: mr.marks.length > 0 && mr.marks[0].color ? mr.marks[0].color
                                                            : App.themeTextMuted
        }
        Repeater {
            model: mr.marks
            delegate: Text {
                required property var modelData
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.text
                color: modelData.color !== undefined && modelData.color !== null
                       ? modelData.color : App.themeTextPrimary
                font.pixelSize: 12
                font.italic: modelData.italic === true
                font.bold: true
                width: Math.min(implicitWidth, Math.max(24, mr.maxW - 20))
                elide: Text.ElideRight
            }
        }
        HoverHandler { id: mrHover }
        ToolTip.visible: mrHover.hovered && mr.fullText.length > 0
        ToolTip.delay: 400
        ToolTip.text: mr.fullText
        readonly property string fullText: {
            var out = ""
            for (var i = 0; i < mr.marks.length; ++i) {
                var p = mr.marks[i]
                out += (p.full && p.full.length > 0) ? p.full : p.text
            }
            return out
        }
    }

    component ArrowBtn: Rectangle {
        id: ab
        property string iconName: ""
        property bool   on: false
        property string tip: ""
        signal clicked()
        width: 30; height: 30; radius: 6
        color: !ab.on ? "transparent"
             : (abHover.hovered ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                          App.themeAccent.b, 0.34)
                                : Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                          App.themeAccent.b, 0.18))
        border.color: ab.on ? App.themeAccent : "transparent"
        border.width: 1
        DrawnIcon {
            anchors.centerIn: parent
            name: ab.iconName; size: 15
            color: ab.on ? App.themeAccent : App.themeTextMuted
        }
        HoverHandler { id: abHover; cursorShape: ab.on ? Qt.PointingHandCursor
                                                       : Qt.ArrowCursor }
        TapHandler { enabled: ab.on; onTapped: ab.clicked() }
        ToolTip.visible: abHover.hovered && ab.tip.length > 0
        ToolTip.delay: 500
        ToolTip.text: ab.tip
    }

    // Auf jeder Seite steht, was ihr EIGENER Knopf bewirken würde, in der Farbe dieser Richtung (Zurück eines
    // Löschens ist grün). Vorher stand dort die vergangene Tat, während der Knopf daneben das Gegenteil tat.
    component UndoBar: Rectangle {
        id: ub
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.10)

        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1
            color: App.themeBorder
        }

        readonly property bool hasL: panel.tagsCtl ? panel.tagsCtl.canUndo : false
        readonly property bool hasR: panel.tagsCtl ? panel.tagsCtl.canRedo : false
        readonly property int freeW: Math.max(0, ub.width - 78)
        readonly property int sideW: (ub.hasL && ub.hasR) ? Math.floor(ub.freeW / 2)
                                                          : ub.freeW

        Row {
            anchors.centerIn: parent
            width: Math.min(parent.width - 12, implicitWidth)
            spacing: 6

            MarkRow {
                anchors.verticalCenter: parent.verticalCenter
                marks:  panel.tagsCtl ? panel.tagsCtl.undoMark : []
                iconName: panel.tagsCtl ? panel.tagsCtl.undoIcon : ""
                maxW: ub.sideW
            }
            ArrowBtn {
                objectName: "undoBtn"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "undo"
                on: ub.hasL
                tip: App.uiText(App.language, "TagUndoTip")
                onClicked: panel.tagsCtl.undoLast()
            }
            ArrowBtn {
                objectName: "redoBtn"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "redo"
                on: ub.hasR
                tip: App.uiText(App.language, "TagUndoTip2")
                onClicked: panel.tagsCtl.redoLast()
            }
            MarkRow {
                anchors.verticalCenter: parent.verticalCenter
                marks:  panel.tagsCtl ? panel.tagsCtl.redoMark : []
                iconName: panel.tagsCtl ? panel.tagsCtl.redoIcon : ""
                maxW: ub.sideW
            }
        }
    }

    readonly property int headH: 44

    //  Kopf wie in den Einstellungen: Strich, Titel, Linie darunter, runder Knopf wie das X der Dialoge.
    component SectionHeader: Item {
        id: hdr
        property string title: ""
        property string addTip: ""
        signal addClicked()

        height: panel.headH

        AccentSlash {
            id: hdrSlash
            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
        }
        Text {
            anchors { left: hdrSlash.right; leftMargin: 8; right: addBtn.left; rightMargin: 8
                      verticalCenter: parent.verticalCenter }
            text: hdr.title
            color: App.themeTextPrimary
            font.pixelSize: 15
            elide: Text.ElideRight
        }
        Rectangle {
            id: addBtn
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            width: 26; height: 26; radius: 13
            color: Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b,
                           addHover.hovered ? 0.14 : 0.0)
            border.color: App.themeBorder
            Repeater {
                model: [0, 90]
                delegate: Rectangle {
                    required property int modelData
                    x: (addBtn.width - width) / 2
                    y: (addBtn.height - height) / 2
                    width: 12; height: 1.6; radius: 0.8
                    rotation: modelData
                    antialiasing: true
                    color: addHover.hovered ? App.themeAccent : App.themeTextMuted
                }
            }
            HoverHandler { id: addHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: hdr.addClicked() }
            ToolTip.text: hdr.addTip
            ToolTip.visible: addHover.hovered && hdr.addTip.length > 0
            ToolTip.delay: 500
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                      leftMargin: 10; rightMargin: 10 }
            height: 1
            color: App.themeBorder
        }
    }

    //  Rahmen der Fenster des Panels, wie der Namensdialog der Extraktion: Kopf mit Strich, Titel und X, darunter der Inhalt.
    component PanelDialog: Popup {
        id: pd
        property string title: ""
        property int bodyW: 300
        //  Eingabetaste bestätigt dort, wo kein Feld sie selbst nimmt (Löschfragen).
        property bool enterConfirms: false
        signal confirmed()
        default property alias body: bodyCol.data

        modal: true; focus: true; anchors.centerIn: Overlay.overlay
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: App.themeBackground; border.color: App.themeBorder; radius: 10 }

        contentItem: Column {
            focus: true
            Keys.onReturnPressed: (e) => { if (pd.enterConfirms) { pd.confirmed(); e.accepted = true } }
            Keys.onEnterPressed:  (e) => { if (pd.enterConfirms) { pd.confirmed(); e.accepted = true } }
            Item {
                width: pd.bodyW + 44
                height: 56
                AccentSlash {
                    id: pdSlash
                    anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                }
                Text {
                    anchors { left: pdSlash.right; leftMargin: 10; right: pdClose.left; rightMargin: 12
                              verticalCenter: parent.verticalCenter }
                    text: pd.title
                    color: App.themeTextPrimary
                    font.pixelSize: 16
                    elide: Text.ElideRight
                }
                CloseButton {
                    id: pdClose
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    onClicked: pd.close()
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: App.themeBorder
                }
            }
            Column {
                id: bodyCol
                padding: 22
                topPadding: 16
                spacing: 10
            }
        }
    }

    Column {
        anchors.fill: parent

        SectionHeader {
            visible: panel.showTagsSection
            width: parent.width
            title: App.uiText(App.language, "PanelSectionTags")
            addTip: App.uiText(App.language, "PanelAddTagTip")
            onAddClicked: panel.promptNewTag()
        }

        ScrollView {
            id: tagsArea
            objectName: "panelTagsArea"     // Griff fuer tests/bench
            visible: panel.showTagsSection
            width: parent.width
            height: Math.min(tagsCol.implicitHeight,
                             panel.showCategoriesSection ? Math.floor(panel.height * 0.35)
                                                         : panel.height - panel.headH - panel.undoBarHeight)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                id: tagsCol
                width: panel.width - 12
                x: 6
                topPadding: 6; bottomPadding: 6
                spacing: 6

                SectionSearch {
                    id: tagSearch
                    width: parent.width
                    placeholder: App.uiText(App.language, "PanelSearchTag")
                }

                Flow {
                    id: tagsFlow
                    width: parent.width
                    spacing: 4

                    //  Ein MODELL statt eines JS-Feldes: ein Repeater ueber ein
                    //  Feld wirft bei jeder Aenderung alle Chips weg und baut sie
                    //  neu - bei 400 Tags rund 600 ms je Vorgang. Das Modell
                    //  meldet nur den Unterschied. `source` haengt am Schalter:
                    //  `visible` allein verhindert das Erzeugen nicht.
                    TagListModel {
                        id: chipModel
                        source: panel.showTagsSection ? panel.allTagsModel : []
                        filter: tagSearch.text
                    }

                    Repeater {
                        model: chipModel
                    //  Jeder Chip in einem eigenen, ASYNCHRONEN Loader: 400 auf
                    //  einen Schlag blockierten den Faden ueber eine halbe
                    //  Sekunde. KEINE eigene Hoehe/Breite - ein Loader mit
                    //  gesetzter Groesse zwingt sie seinem Kind auf.
                    delegate: Loader {
                        id: chipLader
                        required property string name
                        asynchronous: true
                        Component.onCompleted: chipLader.setSource(
                            "qrc:/qml/tags/TagChip.qml",
                            { name: chipLader.name, panel: panel })
                    }
                    }
                }

                Text {
                    visible: panel.allTagsModel.length === 0
                             || (tagSearch.text.length > 0
                                 && chipModel.count === 0)
                    text: tagSearch.text.length > 0
                          ? App.uiText(App.language, "PanelSearchNoHit")
                          : App.uiText(App.language, "PanelNoTags")
                    color: App.themeTextMuted; font.pixelSize: 12
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) {
                        const p = tagsCol.mapToItem(tagsFlow, point.position.x,
                                                    point.position.y)
                        if (tagsFlow.childAt(p.x, p.y)) return
                        tagAreaMenu.open()
                    }
                }
                ThemedMenu {
                    id: tagAreaMenu
                    MenuItem { text: "+  " + App.uiText(App.language, "CatPanelNewTag")
                               onTriggered: panel.promptNewTag() }
                }
            }
        }

        SectionHeader {
            visible: panel.showCategoriesSection
            width: parent.width
            title: App.uiText(App.language, "SettingsTabCategories")
            addTip: App.uiText(App.language, "PanelAddCategoryTip")
            onAddClicked: panel.promptNewCategory()
        }

        ScrollView {
            id: catsArea
            objectName: "panelCatsArea"     // Griff fuer tests/bench
            visible: panel.showCategoriesSection
            width: parent.width
            height: panel.height
                    - (panel.showTagsSection ? panel.headH + tagsArea.height : 0)
                    - panel.headH - panel.undoBarHeight
            clip: true

            Column {
                id: treeColumn
                width: panel.width - 12
                x: 6
                topPadding: 6; bottomPadding: 6
                spacing: 6

                SectionSearch {
                    id: catSearch
                    width: parent.width
                    placeholder: App.uiText(App.language, "PanelSearchCategory")
                }

                //  WÄHREND der Suche eine flache Trefferliste statt des Baums:
                //  wer sucht, will den Treffer anklicken und nicht erst den Pfad
                //  aufklappen. Ein Klick wählt die Kategorie wie im Baum.
                Repeater {
                    model: (panel.showCategoriesSection && catSearch.text.length > 0)
                           ? panel.filterCats(catSearch.text) : []
                    delegate: Rectangle {
                        id: hitRow
                        required property var modelData
                        width: treeColumn.width
                        height: 26
                        radius: 6
                        readonly property bool on: panel.activeCategories.indexOf(hitRow.modelData.id) >= 0
                        color: hitRow.on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                   App.themeAccent.b, 0.28)
                             : (hitHover.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
                        border.color: hitRow.on ? App.themeAccent : App.themeBorder
                        border.width: 1
                        Text {
                            anchors { left: parent.left; leftMargin: 8; right: parent.right
                                      rightMargin: 8; verticalCenter: parent.verticalCenter }
                            text: hitRow.modelData.name
                            color: App.themeTextPrimary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        HoverHandler { id: hitHover }
                        TapHandler {
                            onTapped: panel.toggleCategory(hitRow.modelData.id, !hitRow.on)
                        }
                    }
                }

                Text {
                    visible: catSearch.text.length > 0
                             && panel.filterCats(catSearch.text).length === 0
                    text: App.uiText(App.language, "PanelSearchNoHit")
                    color: App.themeTextMuted; font.pixelSize: 12
                    topPadding: 8
                }

                //  Jeder Knoten entsteht in einem eigenen, ASYNCHRONEN Loader.
                //  Ein Knoten kostet rund 10 ms; 120 auf einen Schlag blockierten
                //  den Faden ueber eine Sekunde. Die Incubation verteilt das auf
                //  Bilder - der Baum fuellt sich sichtbar, statt zu stehen.
                Repeater {
                    model: (!panel.showCategoriesSection || catSearch.text.length > 0)
                           ? [] : panel.tree
                    delegate: Loader {
                        id: knotenLader
                        required property var modelData
                        asynchronous: true
                        //  KEINE eigene Hoehe: ein Loader mit gesetzter Hoehe zwingt
                        //  sie seinem Kind auf, und `height: item.height` liefe im
                        //  Kreis - der Knoten blieb dann unsichtbar. Die Hoehe kommt
                        //  ueber `implicitHeight` aus dem Knoten selbst.
                        width: treeColumn.width
                        Component.onCompleted: knotenLader.setSource(
                            "qrc:/qml/tags/CategoryNode.qml",
                            { node: knotenLader.modelData, depth: 0, panel: panel })
                    }
                }

                Text {
                    visible: panel.tree.length === 0 && catSearch.text.length === 0
                    text: App.uiText(App.language, "TagPanelEmpty")
                    color: App.themeTextMuted; font.pixelSize: 12
                    topPadding: 12
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) {
                        if (treeColumn.childAt(point.position.x, point.position.y)) return
                        catAreaMenu.open()
                    }
                }
                ThemedMenu {
                    id: catAreaMenu
                    MenuItem { text: "+  " + App.uiText(App.language, "CatPanelAddCategory")
                               onTriggered: panel.promptNewCategory() }
                }
            }
        }
    }

    PanelDialog {
        id: namePrompt
        property string value: ""
        property var onAcceptFn: (function(v){})
        TextField {
            id: promptField
            width: namePrompt.bodyW
            text: namePrompt.value
            color: App.themeTextPrimary
            onAccepted: namePrompt.commit()
        }
        Item {
            width: namePrompt.bodyW
            height: promptOk.height + 6
            Button {
                id: promptOk
                anchors { right: parent.right; bottom: parent.bottom }
                highlighted: true
                enabled: promptField.text.trim().length > 0
                text: App.uiText(App.language, "SettingsOk")
                onClicked: namePrompt.commit()
            }
        }
        onOpened: { promptField.text = value; promptField.forceActiveFocus(); promptField.selectAll() }
        function commit() {
            var v = promptField.text.trim()
            if (v.length > 0) onAcceptFn(v)
            close()
        }
    }

    // Zwei Wege in einem Fenster: oben ein Feld, dessen Eingabe anlegt UND zuweist; unten die Tags des Ordners,
    // die dieser Kategorie fehlen. Das Feld filtert die Liste zugleich - tippen und klicken statt abschreiben.
    PanelDialog {
        id: tagPick
        objectName: "tagPickPopup"
        title: App.uiText(App.language, "TagBarDropdownHeader")
        property string targetCat: ""

        function candidates() {
            const node = panel._findNode(panel.tree, tagPick.targetCat)
            const drin = node ? node.tags : []
            const alle = panel.filterList(panel.allTagsModel, pickField.text)
            var out = []
            for (var i = 0; i < alle.length; ++i)
                if (drin.indexOf(alle[i]) < 0) out.push(alle[i])
            return out
        }
        function assign(tag) {
            const t = String(tag).trim()
            if (t.length === 0) return
            panel.tagsCtl.addTagToCategory(tagPick.targetCat, t)
            tagPick.close()
        }

        onOpened: { pickField.text = ""; pickField.forceActiveFocus() }

            TextField {
                id: pickField
                width: tagPick.bodyW
                color: App.themeTextPrimary
                placeholderText: App.uiText(App.language, "CatPanelNewTag")
                onAccepted: tagPick.assign(pickField.text)
            }
            Item {
                width: tagPick.bodyW
                height: pickCreate.height
                Button {
                    id: pickCreate
                    anchors.right: parent.right
                    highlighted: true
                    enabled: pickField.text.trim().length > 0
                    text: App.uiText(App.language, "TagPickCreate")
                    onClicked: tagPick.assign(pickField.text)
                }
            }

            Text {
                topPadding: 6
                text: App.uiText(App.language, "TagPickExisting")
                color: App.themeTextMuted
                font.pixelSize: 11; font.capitalization: Font.AllUppercase; font.letterSpacing: 1.2
            }
            ScrollView {
                width: tagPick.bodyW
                height: Math.min(180, Math.max(28, pickList.count * 28))
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ListView {
                    id: pickList
                    objectName: "tagPickList"
                    model: tagPick.candidates()
                    spacing: 2
                    delegate: Rectangle {
                        required property var modelData
                        width: tagPick.bodyW - 16; height: 26; radius: 5
                        color: rowHover.hovered
                               ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                         App.themeTextPrimary.b, 0.10)
                               : "transparent"
                        Row {
                            anchors { left: parent.left; leftMargin: 8
                                      verticalCenter: parent.verticalCenter }
                            spacing: 7
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 10; height: 10; radius: 5
                                color: panel.tagColorOf(parent.parent.modelData)
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: parent.parent.modelData
                                color: App.themeTextPrimary; font.pixelSize: 12
                            }
                        }
                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: tagPick.assign(parent.modelData) }
                    }
                }
            }
            Text {
                visible: pickList.count === 0
                width: tagPick.bodyW
                text: App.uiText(App.language, "TagPickNone")
                color: App.themeTextMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
            }
    }

    ColorDialog {
        id: colorDialog
        property string targetCat: ""
        onAccepted: panel.tagsCtl.setCategoryUniformColor(targetCat, true, selectedColor, true)
    }

    UndoBar {
        objectName: "undoBar"          // fuer `bench_tagpanel`
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  leftMargin: 1; rightMargin: 1; bottomMargin: 1 }
        height: panel.undoBarHeight
        visible: height > 0
    }

    // Eingabetaste bestätigt - ein `Popup` bringt das anders als ein `Dialog` nicht mit; `PanelDialog` hört am `contentItem`.
    PanelDialog {
        id: confirmDeleteTag
        title: App.uiText(App.language, "SettingsTagDelete") + ": " + panel.deleteTagName
        bodyW: 280
        enterConfirms: true
        onConfirmed: confirmDeleteTag.confirm()
        function confirm() {
            panel.tagsCtl.deleteTag(panel.deleteTagName)
            confirmDeleteTag.close()
        }
        Item {
            width: confirmDeleteTag.bodyW
            height: delTagBtn.height
            Button {
                id: delTagBtn
                anchors.right: parent.right
                highlighted: true
                text: App.uiText(App.language, "BookmarkDelete")
                onClicked: confirmDeleteTag.confirm()
            }
        }
    }

    PanelDialog {
        id: confirmDelete
        title: App.uiText(App.language, "TagPanelDeleteTitle")
        bodyW: 280
        enterConfirms: true
        onConfirmed: confirmDelete.confirm()
        function confirm() {
            panel.tagsCtl.deleteCategory(panel.deleteCatId)
            confirmDelete.close()
        }
        Item {
            width: confirmDelete.bodyW
            height: delCatBtn.height
            Button {
                id: delCatBtn
                anchors.right: parent.right
                highlighted: true
                text: App.uiText(App.language, "BookmarkDelete")
                onClicked: confirmDelete.confirm()
            }
        }
    }
    //  Weiches, schnelles Mausrad wie im Rest der App - sonst gilt Qts fester
    //  Schritt. Deklariert am WURZELELEMENT, nicht in der ScrollView: dort
    //  landete die Flaeche im INHALT und rollte mit ihm mit.
    SmoothWheelArea { flickable: tagsArea.contentItem }
    SmoothWheelArea { flickable: catsArea.contentItem }

}
