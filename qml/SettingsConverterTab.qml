pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ── Converter: Tag ↔ Unterkategorie + JSON-Migration ─────────────────────────
Item {
    id: root

    property var tagModel: []      // [{text,value}]
    property var catModel: []      // alle Kategorien (für Parent-Auswahl)
    property var subModel: []      // nur Unterkategorien (depth > 0)

    function flatten(nodes, depth, out, onlySub) {
        for (var i = 0; i < nodes.length; ++i) {
            var n = nodes[i]
            if (!onlySub || depth > 0) {
                var indent = "    ".repeat(onlySub ? Math.max(0, depth - 1) : depth)
                var prefix = (onlySub && depth > 0) ? "\u21B3 " : ""
                out.push({ text: indent + prefix + n.name, value: n.id })
            }
            if (n.children && n.children.length > 0)
                root.flatten(n.children, depth + 1, out, onlySub)
        }
    }

    function refresh() {
        var tags = App.allTags()
        var tm = []
        for (var i = 0; i < tags.length; ++i) tm.push({ text: tags[i], value: tags[i] })
        tagModel = tm

        var tree = Tags.categoriesTree()
        var cats = []; flatten(tree, 0, cats, false); catModel = cats
        var subs = []; flatten(tree, 0, subs, true);  subModel = subs
    }

    Component.onCompleted: refresh()
    Connections {
        target: Tags
        function onTagsChanged()       { root.refresh() }
        function onCategoriesChanged() { root.refresh() }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 14

            // ── Tag → Unterkategorie ──────────────────────────────────────────
            SettingsGroup {
                title: App.uiText(App.language, "ConverterTagToSubcat")
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: App.uiText(App.language, "SettingsConvTagToSubHint")
                    color: App.themeTextMuted; font.pixelSize: 11
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2; columnSpacing: 12; rowSpacing: 8

                    Label { text: App.uiText(App.language, "SettingsConvTagLabel"); color: App.themeTextPrimary }
                    ComboBox {
                        id: t2sTag
                        Layout.fillWidth: true
                        model: root.tagModel
                        textRole: "text"; valueRole: "value"
                    }

                    Label { text: App.uiText(App.language, "SettingsConvTargetCat"); color: App.themeTextPrimary }
                    ComboBox {
                        id: t2sParent
                        Layout.fillWidth: true
                        model: root.catModel
                        textRole: "text"; valueRole: "value"
                    }

                    Label { text: App.uiText(App.language, "FilterCatNewName"); color: App.themeTextPrimary }
                    TextField {
                        id: t2sName
                        Layout.fillWidth: true
                        color: App.themeTextPrimary
                        placeholderText: t2sTag.currentText
                    }
                }

                Button {
                    Layout.alignment: Qt.AlignRight
                    text: App.uiText(App.language, "SettingsConvConvertBtn")
                    highlighted: true
                    enabled: root.tagModel.length > 0 && root.catModel.length > 0
                    onClicked: {
                        var name = t2sName.text.trim()
                        if (name.length === 0) name = t2sTag.currentText
                        Tags.convertTagToSubcategory(t2sTag.currentValue, t2sParent.currentValue, name)
                        t2sName.text = ""
                    }
                }
            }

            // ── Unterkategorie → Tag ──────────────────────────────────────────
            SettingsGroup {
                title: App.uiText(App.language, "ConverterSubcatToTag")
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: App.uiText(App.language, "SettingsConvSubToTagHint")
                    color: App.themeTextMuted; font.pixelSize: 11
                }

                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    Label { text: App.uiText(App.language, "SettingsConvSubcatLabel"); color: App.themeTextPrimary }
                    ComboBox {
                        id: s2tSub
                        Layout.fillWidth: true
                        model: root.subModel
                        textRole: "text"; valueRole: "value"
                    }
                }

                Button {
                    Layout.alignment: Qt.AlignRight
                    text: App.uiText(App.language, "SettingsConvConvertBtn")
                    highlighted: true
                    enabled: root.subModel.length > 0
                    onClicked: Tags.convertSubcategoryToTag(s2tSub.currentValue)
                }
            }

            // ── JSON-Migration ────────────────────────────────────────────────
            SettingsGroup {
                title: App.uiText(App.language, "SettingsConvJsonMigration")
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: App.uiText(App.language, "SettingsConvMigrateHint")
                    color: App.themeTextMuted; font.pixelSize: 11
                }

                Button {
                    text: App.uiText(App.language, "SettingsConvMigrateNow")
                    onClicked: migrateDialog.open()
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    Dialog {
        id: migrateDialog
        title: App.uiText(App.language, "SettingsConvJsonMigration")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: App.saveCurrentFolder()
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        contentItem: Text {
            text: App.uiText(App.language, "SettingsConvSaveV2Confirm")
            color: App.themeTextPrimary; wrapMode: Text.WordWrap; width: 300
        }
    }
}
