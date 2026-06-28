pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  SettingsCategoryNode.qml — rekursiver Editor-Knoten des Kategorie-Baums.
//  Instanziiert sich für Unterkategorien selbst (Loader + source-String, um den
//  M129-Fehler "Typ kann nicht rekursiv instanziiert werden" zu vermeiden).
//
//  Knoten-Schema: { id, name, color, uniform, inherit, tags, fileCount, children }
//  Mutationen laufen über Tags (TagController) bzw. tab.prompt*()-Callbacks.
// ─────────────────────────────────────────────────────────────────────────────
Column {
    id: nodeRoot

    required property var node
    required property int depth
    required property var tab     // SettingsCategoriesTab (Prompt-Callbacks)

    property bool collapsed: false

    spacing: 2
    width: parent ? parent.width : 0

    // ── Kopfzeile ─────────────────────────────────────────────────────────────
    Rectangle {
        width: parent.width
        height: 34
        radius: 5
        color: headerHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"

        HoverHandler { id: headerHover }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 6 + nodeRoot.depth * 16
            anchors.rightMargin: 6
            spacing: 6

            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 18; padding: 0
                visible: nodeRoot.node.children.length > 0 || nodeRoot.node.tags.length > 0
                text: nodeRoot.collapsed ? "\u25B8" : "\u25BE"
                font.pixelSize: 10
                onClicked: nodeRoot.collapsed = !nodeRoot.collapsed
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 12; height: 12; radius: 6
                color: nodeRoot.node.color
                border.color: Qt.rgba(1, 1, 1, 0.3); border.width: 1
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(40, nodeRoot.width - nodeRoot.depth * 16 - 230)
                text: nodeRoot.node.name
                color: App.themeTextPrimary
                font.pixelSize: 13
                elide: Text.ElideRight
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: nodeRoot.node.fileCount > 0
                width: cntText.implicitWidth + 12; height: 18; radius: 9
                color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.18)
                Text {
                    id: cntText
                    anchors.centerIn: parent
                    text: nodeRoot.node.fileCount
                    color: App.themeAccent; font.pixelSize: 10
                }
            }

            // Umfärben (nur bei Einheitsfarbe)
            ColorPicker {
                anchors.verticalCenter: parent.verticalCenter
                visible: nodeRoot.node.uniform
                width: 28; height: 20
                title: App.uiText(App.language, "SettingsCatNodeColorTitle")
                showAlpha: false
                selectedColor: nodeRoot.node.color
                onColorPicked: (c) => Tags.setCategoryUniformColor(nodeRoot.node.id, true, c, nodeRoot.node.inherit)
            }

            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 24; height: 24
                text: "\u22EE"
                onClicked: ctxMenu.open()
                Menu {
                    id: ctxMenu
                    MenuItem { text: App.uiText(App.language, "SettingsCatNodeAddSub"); onTriggered: nodeRoot.tab.promptAddSub(nodeRoot.node.id) }
                    MenuItem { text: App.uiText(App.language, "SettingsCatNodeRename");     onTriggered: nodeRoot.tab.promptRename(nodeRoot.node.id, nodeRoot.node.name) }
                    MenuSeparator {}
                    MenuItem {
                        text: App.uiText(App.language, "SettingsCatNodeSetUniform")
                        enabled: !nodeRoot.node.uniform
                        onTriggered: nodeRoot.tab.promptUniform(nodeRoot.node.id, nodeRoot.node.color)
                    }
                    MenuItem {
                        text: App.uiText(App.language, "SettingsCatNodeClearUniform")
                        enabled: nodeRoot.node.uniform
                        onTriggered: Tags.setCategoryUniformColor(nodeRoot.node.id, false, nodeRoot.node.color, false)
                    }
                    MenuSeparator {}
                    MenuItem { text: App.uiText(App.language, "BookmarkDelete"); onTriggered: nodeRoot.tab.promptDelete(nodeRoot.node.id, nodeRoot.node.name) }
                }
            }
        }
    }

    // ── Tag-Chips ─────────────────────────────────────────────────────────────
    Flow {
        width: parent.width - nodeRoot.depth * 16 - 28
        x: nodeRoot.depth * 16 + 28
        spacing: 4
        visible: !nodeRoot.collapsed && nodeRoot.node.tags.length > 0

        Repeater {
            model: nodeRoot.node.tags
            delegate: Rectangle {
                id: chip
                required property var modelData
                height: 22; radius: 11
                width: chipRow.implicitWidth + 14
                color: Qt.rgba(App.tagColor(chip.modelData).r, App.tagColor(chip.modelData).g,
                               App.tagColor(chip.modelData).b, 0.22)
                border.color: App.tagColor(chip.modelData); border.width: 1

                Row {
                    id: chipRow
                    anchors.centerIn: parent; spacing: 4
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: chip.modelData; color: App.themeTextPrimary; font.pixelSize: 11
                    }
                    ToolButton {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14; height: 14; padding: 0
                        text: "\u2715"; font.pixelSize: 9
                        onClicked: Tags.removeTagFromCategory(nodeRoot.node.id, chip.modelData)
                    }
                }
            }
        }
    }

    // ── Unterkategorien (rekursiv über Loader) ──────────────────────────────────
    Column {
        width: parent.width
        spacing: 2
        visible: !nodeRoot.collapsed
        Repeater {
            model: nodeRoot.node.children
            delegate: Loader {
                id: childLoader
                required property var modelData
                width: parent ? parent.width : 0
                source: Qt.resolvedUrl("SettingsCategoryNode.qml")
                onLoaded: {
                    item.node  = Qt.binding(function() { return childLoader.modelData })
                    item.depth = Qt.binding(function() { return nodeRoot.depth + 1 })
                    item.tab   = nodeRoot.tab
                }
            }
        }
    }
}
