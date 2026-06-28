pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  TagCategoryPanel.qml — Kategorie-Baum-Panel (ersetzt TagCategoryPanel(QWidget)).
//
//  Rendert den Baum aus Tags.categoriesTree() über rekursive CategoryNode-Knoten,
//  verwaltet den aktiven Kategorie-Filter (→ galleryModel.categoryFilter), die
//  manuelle Tag-Auswahl (→ galleryModel.tagFilter, gemeinsame Quelle mit FilterBar)
//  und stellt CRUD per Bridge (Tags.*) sowie Gruppen-/Add-to-Tag-Modus bereit.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: panel
    color: App.themeSidebarBg
    border.color: App.themeBorder
    border.width: 1

    signal enterAddToTagMode(string tag)
    signal enterGroupMode(string tag)

    property var tree: []
    property var activeCategories: []

    function refresh() { tree = Tags.categoriesTree() }
    Component.onCompleted: refresh()
    Connections {
        target: Tags
        function onCategoriesChanged() { panel.refresh() }
        function onTagsChanged()       { panel.refresh() }
    }

    // ── Callbacks für CategoryNode ────────────────────────────────────────────
    function isCategoryActive(id) { return activeCategories.indexOf(id) >= 0 }
    function toggleCategory(id, on) {
        var a = activeCategories.slice()
        var i = a.indexOf(id)
        if (on && i < 0) a.push(id)
        else if (!on && i >= 0) a.splice(i, 1)
        activeCategories = a
        galleryModel.categoryFilter = a
    }
    function toggleTag(tag) {
        var a = galleryModel.tagFilter.slice()
        var i = a.indexOf(tag)
        if (i >= 0) a.splice(i, 1); else a.push(tag)
        galleryModel.tagFilter = a
    }
    function moveTag(tag, fromCat, toCat) { Tags.moveTagToCategory(tag, fromCat, toCat) }
    function requestAddToTagMode(tag) { panel.enterAddToTagMode(tag) }
    function requestGroupMode(tag)    { panel.enterGroupMode(tag) }

    function promptAddSubcategory(parentId) {
        namePrompt.title = "Unterkategorie"; namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { Tags.addSubcategory(parentId, v, Qt.rgba(0,0.7,0.63,1), false) }
        namePrompt.open()
    }
    function promptAddTag(catId) {
        namePrompt.title = App.uiText(App.language, "TagBarDropdownHeader"); namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { Tags.addTagToCategory(catId, v) }
        namePrompt.open()
    }
    function promptRename(id, oldName) {
        namePrompt.title = "Umbenennen"; namePrompt.value = oldName
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

    // ── Kopf ──────────────────────────────────────────────────────────────────
    Column {
        anchors.fill: parent

        Rectangle {
            width: parent.width; height: 38; color: App.themeToolbarBg
            Row {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.uiText(App.language, "SettingsTabCategories"); color: App.themeTextPrimary
                    font.pixelSize: 13; font.bold: true
                }
                Item { width: parent.width - 200; height: 1 }
                Button {
                    anchors.verticalCenter: parent.verticalCenter
                    height: 26; text: App.uiText(App.language, "SettingsCatAdd"); font.pixelSize: 11
                    onClicked: {
                        namePrompt.title = App.uiText(App.language, "CatPanelAddCategory"); namePrompt.value = ""
                        namePrompt.onAcceptFn = function(v) { Tags.addRootCategory(v, Qt.rgba(0,0.7,0.63,1), false) }
                        namePrompt.open()
                    }
                }
            }
        }

        // ── Baum ──────────────────────────────────────────────────────────────
        ScrollView {
            width: parent.width
            height: parent.height - 38
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
