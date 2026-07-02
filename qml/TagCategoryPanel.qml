pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  TagCategoryPanel.qml — einheitliches Panel-System für Tags UND Kategorien
//  (rechte Seitenleiste; ersetzt TagCategoryPanel(QWidget)).
//
//  Aufbau (ein Panel, zwei strukturell gleichwertige Abschnitte, gemeinsamer
//  SectionHeader — keine UI-Duplikation):
//    • Abschnitt „Tags":       ALLE Tags als Chips mit klarem Aktiv-/Inaktiv-
//                              Zustand (Toggle gegen galleryModel.tagFilter).
//                              „+" im Kopf erstellt einen neuen Tag.
//    • Abschnitt „Kategorien": bestehender Baum aus Tags.categoriesTree() über
//                              rekursive CategoryNode-Knoten. „+" im Kopf
//                              erstellt eine neue Wurzelkategorie.
//
//  Filter-Konsistenz (referenzbasiert):
//    Referenzquelle für den Tag-Filter ist AUSSCHLIESSLICH der Proxy
//    (galleryModel.tagFilter); activeTagFilter ist nur ein reaktiver Spiegel.
//    Beim ABWÄHLEN einer Kategorie werden abhängige aktive Unterkategorien und
//    deren Tags mit deaktiviert — außer sie werden von einem anderen weiterhin
//    aktiven Filter referenziert (siehe toggleCategory).
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: panel
    color: App.themeSidebarBg
    border.color: App.themeBorder
    border.width: 1

    signal enterAddToTagMode(string tag)
    signal enterGroupMode(string tag)

    // Individuelle Abschnitts-Sichtbarkeit (gesteuert über Filter ▸ Tags &
    // Kategorien): das Panel selbst ist sichtbar, solange mindestens ein
    // Abschnitt aktiv ist (Bindung in ApplicationShell).
    property bool showTagsSection: true
    property bool showCategoriesSection: true

    property var tree: []
    property var allTagsModel: []
    property var activeCategories: []
    // Reaktiver Spiegel von galleryModel.tagFilter (NIE direkt mutieren —
    // Mutationen laufen immer über den Proxy, der Spiegel folgt via Connections).
    property var activeTagFilter: []

    function refresh() {
        tree = Tags.categoriesTree()
        allTagsModel = Tags.allTags()
    }
    Component.onCompleted: {
        refresh()
        activeTagFilter = galleryModel.tagFilter
    }
    Connections {
        target: Tags
        function onCategoriesChanged() { panel.refresh() }
        function onTagsChanged()       { panel.refresh() }
    }
    // Beim Ordnerwechsel/-start neu ziehen: JsonStorage lädt die Tags/Kategorien
    // eines Ordners OHNE tagsChanged/categoriesChanged zu emittieren — ohne
    // diesen Hook bliebe das Panel bis zur ersten Mutation leer.
    Connections {
        target: App
        function onFolderOpened(path) { panel.refresh() }
    }
    Connections {
        target: galleryModel
        function onFilterChanged() { panel.activeTagFilter = galleryModel.tagFilter }
    }

    // ── Zustands-Callbacks (auch für CategoryNode) ────────────────────────────
    function isCategoryActive(id) { return activeCategories.indexOf(id) >= 0 }
    function isTagActive(tag)     { return activeTagFilter.indexOf(tag) >= 0 }

    // ── Referenz-Helfer für die Kaskadenlogik ─────────────────────────────────
    //  Alle Prüfungen laufen über den aktuellen Baum (tree) — per ID, nicht per
    //  Name (Referenzbasis: TagCategory.id).
    function _findNode(nodes, id) {
        for (var i = 0; i < nodes.length; i++) {
            if (nodes[i].id === id) return nodes[i]
            var f = _findNode(nodes[i].children, id)
            if (f) return f
        }
        return null
    }
    // Alle Kategorie-IDs des Teilbaums INKLUSIVE des Knotens selbst.
    function _subtreeIds(node) {
        var out = [node.id]
        for (var i = 0; i < node.children.length; i++)
            out = out.concat(_subtreeIds(node.children[i]))
        return out
    }
    // Alle Tags des Teilbaums (Knoten + rekursiv alle Unterkategorien).
    function _subtreeTags(node) {
        var out = node.tags.slice()
        for (var i = 0; i < node.children.length; i++) {
            var sub = _subtreeTags(node.children[i])
            for (var j = 0; j < sub.length; j++)
                if (out.indexOf(sub[j]) < 0) out.push(sub[j])
        }
        return out
    }
    // Vorfahren-IDs (Wurzel → …) eines Knotens; null, wenn nicht gefunden.
    function _ancestorIds(nodes, id) {
        for (var i = 0; i < nodes.length; i++) {
            if (nodes[i].id === id) return []
            var sub = _ancestorIds(nodes[i].children, id)
            if (sub !== null) { sub.push(nodes[i].id); return sub }
        }
        return null
    }

    // ── Kategorie an-/abwählen (mit referenzbasierter Kaskade beim Abwählen) ──
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

            // 1) Abhängige aktive Unterkategorien deaktivieren.
            //    AUSNAHME: Eine Unterkategorie bleibt aktiv, wenn ein weiterhin
            //    aktiver Vorfahre AUSSERHALB des abgewählten Teilbaums sie noch
            //    referenziert (z. B. eine aktive übergeordnete Wurzelkategorie).
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

            // 2) Tag-Kaskade: Tags des abgewählten Teilbaums aus dem Tag-Filter
            //    entfernen — AUSSER ein verbleibender aktiver Kategorie-Teilbaum
            //    referenziert den Tag weiterhin (Referenzzählung über die
            //    Teilbaum-Tags aller noch aktiven Kategorien).
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
    function moveTag(tag, fromCat, toCat) { Tags.moveTagToCategory(tag, fromCat, toCat) }
    function requestAddToTagMode(tag) { panel.enterAddToTagMode(tag) }
    function requestGroupMode(tag)    { panel.enterGroupMode(tag) }

    function promptAddSubcategory(parentId) {
        namePrompt.title = App.uiText(App.language, "CatPanelNewSubcategory"); namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { Tags.addSubcategory(parentId, v, Qt.rgba(0,0.7,0.63,1), false) }
        namePrompt.open()
    }
    function promptAddTag(catId) {
        namePrompt.title = App.uiText(App.language, "TagBarDropdownHeader"); namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { Tags.addTagToCategory(catId, v) }
        namePrompt.open()
    }
    function promptRename(id, oldName) {
        namePrompt.title = App.uiText(App.language, "CatPanelRename"); namePrompt.value = oldName
        namePrompt.onAcceptFn = function(v) { Tags.renameCategory(id, v) }
        namePrompt.open()
    }
    function promptUniformColor(id) {
        colorDialog.targetCat = id
        colorDialog.selectedColor = Tags.categoryColor(id)
        colorDialog.open()
    }
    function promptDelete(id) { deleteCatId = id; confirmDelete.open() }
    property string deleteCatId: ""

    // ── Gemeinsamer Abschnittskopf (Titel + „+"-Button) ───────────────────────
    //  EIN Kopf-Baustein für beide Abschnitte → einheitliche Steuerung ohne
    //  UI-Duplikation.
    component SectionHeader: Rectangle {
        id: hdr
        property string title: ""
        property string addTip: ""
        signal addClicked()

        height: 34
        color: App.themeToolbarBg

        Text {
            anchors.left: parent.left; anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: hdr.title
            color: App.themeTextPrimary
            font.pixelSize: 13; font.bold: true
        }
        Rectangle {
            id: addBtn
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 22; height: 22; radius: 11
            color: addHover.hovered ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.32)
                                    : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.18)
            border.color: App.themeAccent; border.width: 1
            Text {
                anchors.centerIn: parent
                text: "+"
                color: App.themeAccent
                font.pixelSize: 14; font.bold: true
            }
            HoverHandler { id: addHover }
            TapHandler { onTapped: hdr.addClicked() }
            ToolTip.text: hdr.addTip
            ToolTip.visible: addHover.hovered && hdr.addTip.length > 0
        }
    }

    Column {
        anchors.fill: parent

        // ── Abschnitt 1: Tags ─────────────────────────────────────────────────
        SectionHeader {
            visible: panel.showTagsSection
            width: parent.width
            title: App.uiText(App.language, "PanelSectionTags")
            addTip: App.uiText(App.language, "PanelAddTagTip")
            onAddClicked: {
                namePrompt.title = App.uiText(App.language, "CatPanelNewTag"); namePrompt.value = ""
                namePrompt.onAcceptFn = function(v) { Tags.createTag(v, Qt.rgba(0, 0.7, 0.63, 1)) }
                namePrompt.open()
            }
        }

        ScrollView {
            id: tagsArea
            visible: panel.showTagsSection
            width: parent.width
            // Natürliche Höhe, gedeckelt auf ~35 % des Panels (bzw. volle Höhe,
            // wenn der Kategorien-Abschnitt ausgeblendet ist); darüber scrollbar.
            height: Math.min(tagsCol.implicitHeight,
                             panel.showCategoriesSection ? Math.floor(panel.height * 0.35)
                                                         : panel.height - 34)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                id: tagsCol
                width: panel.width - 12
                x: 6
                topPadding: 6; bottomPadding: 6

                Flow {
                    id: tagsFlow
                    width: parent.width
                    spacing: 4

                    Repeater {
                        model: panel.allTagsModel
                        delegate: Rectangle {
                            id: pChip
                            required property var modelData

                            readonly property color tc: App.tagColor(pChip.modelData)
                            // Klarer Toggle-Zustand: aktiv = gefüllt + Häkchen + kräftiger Rand.
                            readonly property bool active: panel.isTagActive(pChip.modelData)

                            height: 24; radius: 12
                            width: pRow.implicitWidth + 16
                            color: active ? Qt.rgba(tc.r, tc.g, tc.b, 0.42)
                                          : Qt.rgba(tc.r, tc.g, tc.b, 0.10)
                            border.color: active ? tc : App.themeBorder
                            border.width: active ? 2 : 1

                            Row {
                                id: pRow
                                anchors.centerIn: parent; spacing: 5
                                Text {
                                    visible: pChip.active
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "\u2713"; color: App.themeTextPrimary
                                    font.pixelSize: 10; font.bold: true
                                }
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 8; height: 8; radius: 4; color: pChip.tc
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: pChip.modelData
                                    color: pChip.active ? App.themeTextPrimary : App.themeTextMuted
                                    font.pixelSize: 11
                                }
                            }

                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: panel.toggleTag(pChip.modelData)
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: pChipMenu.open()
                            }
                            Menu {
                                id: pChipMenu
                                MenuItem { text: App.uiText(App.language, "ModeAddToTag"); onTriggered: panel.requestAddToTagMode(pChip.modelData) }
                                MenuItem { text: App.uiText(App.language, "ModeGroup");    onTriggered: panel.requestGroupMode(pChip.modelData) }
                            }
                        }
                    }
                }

                Text {
                    visible: panel.allTagsModel.length === 0
                    text: App.uiText(App.language, "PanelNoTags")
                    color: App.themeTextMuted; font.pixelSize: 12
                }
            }
        }

        // Visuelle Trennung der beiden Abschnitte.
        Rectangle {
            visible: panel.showTagsSection && panel.showCategoriesSection
            width: parent.width; height: 1; color: App.themeBorder
        }

        // ── Abschnitt 2: Kategorien ───────────────────────────────────────────
        SectionHeader {
            visible: panel.showCategoriesSection
            width: parent.width
            title: App.uiText(App.language, "SettingsTabCategories")
            addTip: App.uiText(App.language, "PanelAddCategoryTip")
            onAddClicked: {
                namePrompt.title = App.uiText(App.language, "CatPanelAddCategory"); namePrompt.value = ""
                namePrompt.onAcceptFn = function(v) { Tags.addRootCategory(v, Qt.rgba(0,0.7,0.63,1), false) }
                namePrompt.open()
            }
        }

        ScrollView {
            visible: panel.showCategoriesSection
            width: parent.width
            // Resthöhe unter dem (ggf. ausgeblendeten) Tags-Abschnitt.
            height: panel.height
                    - (panel.showTagsSection ? 34 + tagsArea.height : 0)
                    - (panel.showTagsSection && panel.showCategoriesSection ? 1 : 0)
                    - 34
            clip: true

            Column {
                id: treeColumn
                width: panel.width - 12
                x: 6
                spacing: 3

                Repeater {
                    model: panel.tree
                    delegate: CategoryNode {
                        required property var modelData
                        width: treeColumn.width
                        node: modelData
                        depth: 0
                        panel: panel
                    }
                }

                Text {
                    visible: panel.tree.length === 0
                    text: App.uiText(App.language, "TagPanelEmpty")
                    color: App.themeTextMuted; font.pixelSize: 12
                    topPadding: 12
                }
            }
        }
    }

    // ── Namens-Prompt ───────────────────────────────────────────────────────
    Popup {
        id: namePrompt
        modal: true; focus: true; anchors.centerIn: Overlay.overlay; padding: 16
        property string title: ""
        property string value: ""
        property var onAcceptFn: (function(v){})
        background: Rectangle { color: App.themeCard; radius: 10; border.color: App.themeBorder }
        contentItem: Column {
            spacing: 12
            Text { text: namePrompt.title; color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true }
            TextField {
                id: promptField
                width: 260
                text: namePrompt.value
                color: App.themeTextPrimary
                onAccepted: namePrompt.commit()
            }
            Row {
                spacing: 8
                Button { text: App.uiText(App.language, "SettingsOk"); onClicked: namePrompt.commit() }
                Button { text: App.uiText(App.language, "SettingsCancel"); onClicked: namePrompt.close() }
            }
        }
        onOpened: { promptField.text = value; promptField.forceActiveFocus(); promptField.selectAll() }
        function commit() {
            var v = promptField.text.trim()
            if (v.length > 0) onAcceptFn(v)
            close()
        }
    }

    // ── Farbwahl ────────────────────────────────────────────────────────────
    ColorDialog {
        id: colorDialog
        property string targetCat: ""
        onAccepted: Tags.setCategoryUniformColor(targetCat, true, selectedColor, false)
    }

    // ── Löschbestätigung ──────────────────────────────────────────────────────
    Popup {
        id: confirmDelete
        modal: true; focus: true; anchors.centerIn: Overlay.overlay; padding: 16
        background: Rectangle { color: App.themeCard; radius: 10; border.color: App.themeBorder }
        contentItem: Column {
            spacing: 12
            Text { text: App.uiText(App.language, "TagPanelDeleteTitle"); color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true }
            Row {
                spacing: 8
                Button { text: App.uiText(App.language, "BookmarkDelete"); onClicked: { Tags.deleteCategory(panel.deleteCatId); confirmDelete.close() } }
                Button { text: App.uiText(App.language, "SettingsCancel"); onClicked: confirmDelete.close() }
            }
        }
    }
}
