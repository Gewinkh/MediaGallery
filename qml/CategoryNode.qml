pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  CategoryNode.qml — ein (rekursiver) Knoten des Kategorie-Baums (ersetzt
//  CategoryNode aus TagCategoryPanel.cpp). Instanziiert sich für Unterkategorien
//  selbst. Tags sind ziehbare Chips; eine DropArea nimmt auf einen anderen Knoten
//  gezogene Tags entgegen → Tags.moveTagToCategory.
// ─────────────────────────────────────────────────────────────────────────────
Column {
    id: nodeRoot

    // Knoten: { id, name, color, uniform, inherit, tags, fileCount, children }
    required property var node
    required property int depth
    required property var panel       // TagCategoryPanel (Callbacks/aktiver Filter)

    property bool collapsed: false

    spacing: 2
    width: parent ? parent.width : 0

    // ── Kopfzeile ─────────────────────────────────────────────────────────────
    Rectangle {
        width: parent.width
        height: 30
        color: dropArea.containsDrag ? Qt.rgba(0, 0.78, 0.70, 0.18)
                                     : (headerHover.hovered ? App.themeCard : "transparent")
        radius: 5

        DropArea {
            id: dropArea
            anchors.fill: parent
            onDropped: function(drop) {
                if (drop.source && drop.source.dragTag !== undefined)
                    nodeRoot.panel.moveTag(drop.source.dragTag, drop.source.dragFromCat, nodeRoot.node.id)
            }
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 6 + nodeRoot.depth * 14
            anchors.rightMargin: 6
            spacing: 6

            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 18
                visible: nodeRoot.node.children.length > 0 || nodeRoot.node.tags.length > 0
                text: nodeRoot.collapsed ? "\u25B8" : "\u25BE"
                font.pixelSize: 10
                padding: 0
                onClicked: nodeRoot.collapsed = !nodeRoot.collapsed
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 12; height: 12; radius: 6
                color: nodeRoot.node.color
                border.color: Qt.rgba(1, 1, 1, 0.3); border.width: 1
            }

            CheckBox {
                anchors.verticalCenter: parent.verticalCenter
                checked: nodeRoot.panel.isCategoryActive(nodeRoot.node.id)
                onToggled: nodeRoot.panel.toggleCategory(nodeRoot.node.id, checked)
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: nodeRoot.node.name + (nodeRoot.node.fileCount > 0 ? "  (" + nodeRoot.node.fileCount + ")" : "")
                color: App.themeTextPrimary
                font.pixelSize: 12
                elide: Text.ElideRight
                width: parent.width - 160
            }

            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 22; height: 22
                text: "\u22EE"
                onClicked: ctxMenu.open()
                Menu {
                    id: ctxMenu
                    MenuItem { text: "Unterkategorie…"; onTriggered: nodeRoot.panel.promptAddSubcategory(nodeRoot.node.id) }
                    MenuItem { text: "Tag hinzufügen…"; onTriggered: nodeRoot.panel.promptAddTag(nodeRoot.node.id) }
                    MenuItem { text: "Umbenennen…";     onTriggered: nodeRoot.panel.promptRename(nodeRoot.node.id, nodeRoot.node.name) }
                    MenuSeparator {}
                    MenuItem { text: "Einheitsfarbe setzen…"; onTriggered: nodeRoot.panel.promptUniformColor(nodeRoot.node.id) }
                    MenuItem {
                        text: "Einheitsfarbe entfernen"
                        enabled: nodeRoot.node.uniform
                        onTriggered: Tags.setCategoryUniformColor(nodeRoot.node.id, false, nodeRoot.node.color, false)
                    }
                    MenuSeparator {}
                    MenuItem { text: "Löschen"; onTriggered: nodeRoot.panel.promptDelete(nodeRoot.node.id) }
                }
            }
        }

        HoverHandler { id: headerHover }
    }

    // ── Tag-Chips ─────────────────────────────────────────────────────────────
    Flow {
        width: parent.width - nodeRoot.depth * 14 - 12
        x: nodeRoot.depth * 14 + 24
        spacing: 4
        visible: !nodeRoot.collapsed && nodeRoot.node.tags.length > 0

        Repeater {
            model: nodeRoot.node.tags
            delegate: Rectangle {
                id: chip
                required property var modelData

                // Drag-Nutzdaten (von der DropArea ausgelesen).
                property string dragTag: modelData
                property string dragFromCat: nodeRoot.node.id

                height: 24; radius: 12
                width: chipRow.implicitWidth + 16
                color: Qt.rgba(App.tagColor(chip.modelData).r, App.tagColor(chip.modelData).g,
                               App.tagColor(chip.modelData).b, 0.25)
                border.color: App.tagColor(chip.modelData); border.width: 1

                Drag.active: dragHandler.active
                Drag.source: chip
                Drag.hotSpot.x: width / 2
                Drag.hotSpot.y: height / 2

                Row {
                    id: chipRow
                    anchors.centerIn: parent; spacing: 5
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4; color: App.tagColor(chip.modelData)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: chip.modelData; color: App.themeTextPrimary; font.pixelSize: 11
                    }
                }

                DragHandler {
                    id: dragHandler
                    onActiveChanged: if (!active) chip.Drag.drop()
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: nodeRoot.panel.toggleTag(chip.modelData)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: chipMenu.open()
                }
                Menu {
                    id: chipMenu
                    MenuItem { text: "Add-to-Tag-Modus"; onTriggered: nodeRoot.panel.requestAddToTagMode(chip.modelData) }
                    MenuItem { text: "Gruppen-Modus";    onTriggered: nodeRoot.panel.requestGroupMode(chip.modelData) }
                    MenuSeparator {}
                    MenuItem {
                        text: "Aus Kategorie entfernen"
                        onTriggered: Tags.removeTagFromCategory(nodeRoot.node.id, chip.modelData)
                    }
                }
            }
        }
    }

    // ── Unterkategorien (rekursiv) ─────────────────────────────────────────────
    // Hinweis: Direktes `CategoryNode { }` hier würde mit pragma ComponentBehavior: Bound
    // den Fehler M129 ("Typ kann nicht rekursiv instanziiert werden") auslösen.
    // Lösung: Loader mit source-String → kein statischer Typ-Verweis zur Compile-Zeit.
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
                source: Qt.resolvedUrl("CategoryNode.qml")
                onLoaded: {
                    childLoader.item.node  = Qt.binding(function() { return childLoader.modelData })
                    childLoader.item.depth = Qt.binding(function() { return nodeRoot.depth + 1 })
                    childLoader.item.panel = Qt.binding(function() { return nodeRoot.panel })
                }
            }
        }
    }
}
