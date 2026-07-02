pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  SettingsConverterTab.qml — universeller Konverter zwischen Tag,
//  Unterkategorie und (Haupt-)Kategorie in JEDER Richtung.
//
//  Die Richtung wird über EIN Dropdown gewählt; die UI darunter passt sich der
//  gewählten Richtung an (Quelle, ggf. Ziel-Kategorie, ggf. neuer Name):
//    • Tag → Unterkategorie   Tags.convertTagToSubcategory(tag, parent, name)
//    • Tag → Kategorie        Tags.convertTagToRootCategory(tag, name)
//    • Unterkategorie → Tag   Tags.convertSubcategoryToTag(id)
//    • Kategorie → Tag        Tags.convertSubcategoryToTag(id)  (gleiche Logik)
//    • Unterkategorie → Kat.  Tags.moveCategory(id, "")         (→ Hauptebene)
//    • Kategorie → Unterkat.  Tags.moveCategory(id, parentId)
//
//  Die frühere JSON-Migration (altes tag-zentrisches Format → v2) wurde
//  entfernt — das Legacy-Format wird nicht mehr unterstützt und die JSONs
//  tragen keinen Versions-Marker mehr (siehe JsonStorage).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    property var tagModel:  []     // [{text,value}] — alle globalen Tags
    property var catModel:  []     // alle Kategorien (für Ziel-/Parent-Auswahl)
    property var subModel:  []     // nur Unterkategorien (depth > 0)
    property var rootModel: []     // nur Hauptkategorien (depth == 0)

    function flatten(nodes, depth, out, filter) {
        for (var i = 0; i < nodes.length; ++i) {
            var n = nodes[i]
            var take = filter === "all"
                    || (filter === "sub"  && depth > 0)
                    || (filter === "root" && depth === 0)
            if (take) {
                var indent = "    ".repeat(filter === "sub" ? Math.max(0, depth - 1) : depth)
                var prefix = (filter === "sub" && depth > 0) ? "\u21B3 " : ""
                out.push({ text: indent + prefix + n.name, value: n.id })
            }
            if (n.children && n.children.length > 0)
                root.flatten(n.children, depth + 1, out, filter)
        }
    }

    function refresh() {
        var tags = App.allTags()
        var tm = []
        for (var i = 0; i < tags.length; ++i) tm.push({ text: tags[i], value: tags[i] })
        tagModel = tm

        var tree = Tags.categoriesTree()
        var cats = [];  flatten(tree, 0, cats,  "all");  catModel  = cats
        var subs = [];  flatten(tree, 0, subs,  "sub");  subModel  = subs
        var roots = []; flatten(tree, 0, roots, "root"); rootModel = roots
    }

    Component.onCompleted: refresh()
    Connections {
        target: Tags
        function onTagsChanged()       { root.refresh() }
        function onCategoriesChanged() { root.refresh() }
    }

    // ── Richtungs-Definition (steuert die adaptive UI) ────────────────────────
    //  source:      "tag" | "sub" | "root"  → Quell-Dropdown + Beschriftung
    //  needsParent: Ziel-Kategorie-Dropdown sichtbar
    //  needsName:   Namensfeld sichtbar (neue Kategorie wird erstellt)
    readonly property var modes: [
        { label: App.uiText(App.language, "ConverterTagToSubcat"),  hint: App.uiText(App.language, "SettingsConvTagToSubHint"),
          source: "tag",  needsParent: true,  needsName: true,  op: "t2s" },
        { label: App.uiText(App.language, "ConverterTagToCat"),     hint: App.uiText(App.language, "SettingsConvTagToCatHint"),
          source: "tag",  needsParent: false, needsName: true,  op: "t2c" },
        { label: App.uiText(App.language, "ConverterSubcatToTag"),  hint: App.uiText(App.language, "SettingsConvSubToTagHint"),
          source: "sub",  needsParent: false, needsName: false, op: "s2t" },
        { label: App.uiText(App.language, "ConverterCatToTag"),     hint: App.uiText(App.language, "SettingsConvCatToTagHint"),
          source: "root", needsParent: false, needsName: false, op: "c2t" },
        { label: App.uiText(App.language, "ConverterSubcatToCat"),  hint: App.uiText(App.language, "SettingsConvSubToCatHint"),
          source: "sub",  needsParent: false, needsName: false, op: "s2c" },
        { label: App.uiText(App.language, "ConverterCatToSubcat"),  hint: App.uiText(App.language, "SettingsConvCatToSubHint"),
          source: "root", needsParent: true,  needsName: false, op: "c2s" }
    ]
    readonly property var mode: modes[modeBox.currentIndex]

    // Quell-Modell/-Beschriftung je Richtung.
    readonly property var sourceModel: mode.source === "tag" ? tagModel
                                     : mode.source === "sub" ? subModel
                                                             : rootModel
    readonly property string sourceLabel: mode.source === "tag"
            ? App.uiText(App.language, "SettingsConvTagLabel")
            : mode.source === "sub"
              ? App.uiText(App.language, "SettingsConvSubcatLabel")
              : App.uiText(App.language, "SettingsConvCatLabel")

    // Ziel-Kategorie-Modell: bei „Kategorie → Unterkategorie" ohne die Quelle
    // selbst (in die eigene/untergeordnete Kategorie kann nicht verschoben
    // werden — den Teilbaum-Fall fängt zusätzlich TagManager::moveCategory ab).
    readonly property var parentModel: {
        if (mode.op !== "c2s") return catModel
        var out = []
        for (var i = 0; i < catModel.length; ++i)
            if (catModel[i].value !== sourceBox.currentValue) out.push(catModel[i])
        return out
    }

    function convert() {
        var src = sourceBox.currentValue
        if (src === undefined || src === null || src === "") return
        var name = nameField.text.trim()
        if (name.length === 0) name = sourceBox.currentText.trim()

        switch (mode.op) {
        case "t2s": Tags.convertTagToSubcategory(src, parentBox.currentValue, name); break
        case "t2c": Tags.convertTagToRootCategory(src, name); break
        case "s2t":                                    // gleiche Logik für beide
        case "c2t": Tags.convertSubcategoryToTag(src); break
        case "s2c": Tags.moveCategory(src, ""); break  // → Hauptebene
        case "c2s": Tags.moveCategory(src, parentBox.currentValue); break
        }
        nameField.text = ""
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 14

            SettingsGroup {
                title: App.uiText(App.language, "SettingsTabConverter")
                Layout.fillWidth: true

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2; columnSpacing: 12; rowSpacing: 8

                    // ── Richtung ─────────────────────────────────────────────
                    Label { text: App.uiText(App.language, "SettingsConvModeLabel"); color: App.themeTextPrimary }
                    ComboBox {
                        id: modeBox
                        Layout.fillWidth: true
                        model: root.modes
                        textRole: "label"
                    }
                }

                // Hinweistext der gewählten Richtung.
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: root.mode.hint
                    color: App.themeTextMuted; font.pixelSize: 11
                }

                // ── Adaptive Eingaben je Richtung ─────────────────────────────
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2; columnSpacing: 12; rowSpacing: 8

                    Label { text: root.sourceLabel; color: App.themeTextPrimary }
                    ComboBox {
                        id: sourceBox
                        Layout.fillWidth: true
                        model: root.sourceModel
                        textRole: "text"; valueRole: "value"
                    }

                    Label {
                        visible: root.mode.needsParent
                        text: App.uiText(App.language, "SettingsConvTargetCat"); color: App.themeTextPrimary
                    }
                    ComboBox {
                        id: parentBox
                        visible: root.mode.needsParent
                        Layout.fillWidth: true
                        model: root.parentModel
                        textRole: "text"; valueRole: "value"
                    }

                    Label {
                        visible: root.mode.needsName
                        text: App.uiText(App.language, "FilterCatNewName"); color: App.themeTextPrimary
                    }
                    TextField {
                        id: nameField
                        visible: root.mode.needsName
                        Layout.fillWidth: true
                        color: App.themeTextPrimary
                        placeholderText: sourceBox.currentText
                    }
                }

                Button {
                    Layout.alignment: Qt.AlignRight
                    text: App.uiText(App.language, "SettingsConvConvertBtn")
                    highlighted: true
                    enabled: root.sourceModel.length > 0
                             && (!root.mode.needsParent || root.parentModel.length > 0)
                    onClicked: root.convert()
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
