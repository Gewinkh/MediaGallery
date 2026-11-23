import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Tags, Kategorien und Konverter arbeiten alle am Sidecar desselben Ordners - daher ein Reiter mit drei Blöcken, die
// gemeinsam rollen; die Blöcke selbst haben keine eigene Rollfläche mehr.
Item {
    id: root

    ScrollView {
        id: labelScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            //  Rand rechts für die Rollleiste, wie im Design-Reiter.
            width: labelScroll.availableWidth - 14
            spacing: 30

            SettingsBlock {
                title: App.uiText(App.language, "FilterTags")
                groupKey: "labeling.block.tags"
                SettingsTagsTab { Layout.fillWidth: true }
            }
            SettingsBlock {
                title: App.uiText(App.language, "SettingsTabCategories")
                groupKey: "labeling.block.categories"
                SettingsCategoriesTab { Layout.fillWidth: true }
            }
            SettingsBlock {
                title: App.uiText(App.language, "SettingsTabConverter")
                groupKey: "labeling.block.converter"
                SettingsConverterTab { Layout.fillWidth: true }
            }

            Item { Layout.fillHeight: true }
        }
    }

    SmoothWheelArea { flickable: labelScroll.contentItem; backdrop: App.themeBackground }
}
