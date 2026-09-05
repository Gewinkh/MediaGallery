pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// Ein rekursiver Knoten des Kategorie-Baums, der sich für Unterkategorien selbst instanziiert. Tags sind
// ziehbare Chips; eine DropArea nimmt auf den Knoten gezogene Tags entgegen (`moveTagToCategory`).
Column {
    id: nodeRoot

    required property var node
    required property int depth
    required property var panel       // TagCategoryPanel (Callbacks/aktiver Filter)

    property bool collapsed: false

    spacing: 2
    width: parent ? parent.width : 0

    Rectangle {
        id: header
        width: parent.width
        height: 30
        color: dropArea.containsDrag ? Qt.rgba(0, 0.78, 0.70, 0.18)
                                     : (headerHover.hovered ? App.themeCard : "transparent")
        radius: 5

        // Kategorie umhängen nur im Optionen-Modus (Alt+S): im Normalbetrieb ist die Kopfzeile zum Anklicken da, und ein
        // versehentliches Umhängen des halben Baums wäre teuer. Derselbe Mechanismus wie beim Tag-Chip, nur mit `dragCat`.
        property string dragCat: nodeRoot.node.id
        //  Zurück an den Platz - s. `TagCategoryPanel` ▸ Chip. Ohne das blieb
        //  die Kopfzeile liegen, wo man sie fallen ließ, und verdeckte den neu
        //  gezeichneten Baum: es sah eingefroren aus.
        property real homeX: 0
        property real homeY: 0
        Drag.active: catDrag.active
        Drag.source: header
        Drag.hotSpot.x: 20
        Drag.hotSpot.y: 15
        z: catDrag.active ? 10 : 0
        DragHandler {
            id: catDrag
            enabled: nodeRoot.panel.editMode
            onActiveChanged: {
                if (active) { header.homeX = header.x; header.homeY = header.y; return }
                const wirkung = header.Drag.drop()
                header.x = header.homeX
                header.y = header.homeY
                if (wirkung === Qt.IgnoreAction)
                    nodeRoot.panel.dropCategoryOutside(header.dragCat)
            }
        }
        opacity: catDrag.active ? 0.6 : 1.0

        // Nimmt ZWEIERLEI an: einen Tag-Chip (der Tag wechselt die Kategorie) und eine gezogene Datei (sie wird
        // Mitglied). Bewusst EINE Fläche - zwei übereinander nähmen einander den Zug weg, geliefert wird nur an die oberste.
        DropArea {
            id: dropArea
            objectName: "catHeaderDrop"      // Griff für tests/tags/tst_dropdelivery
            anchors.fill: parent
            onDropped: function(drop) {
                // `drop.accept` ist hier PFLICHT: ohne sie liefert `Drag.drop` beim Ziehenden `Qt.IgnoreAction`, der hält den
                // Zug für ins Leere gefallen, und die Kategorie wanderte auf die Hauptebene statt dorthin, wo man sie hinzog.
                // Angenommen wird auch, wenn `moveCategory` ablehnt (Zug in den eigenen Teilbaum) - die Fläche HAT ihn bearbeitet.
                if (drop.source && drop.source.dragCat !== undefined) {
                    nodeRoot.panel.moveCategoryInto(drop.source.dragCat, nodeRoot.node.id)
                    drop.accept()
                    return
                }
                if (drop.source && drop.source.dragTag !== undefined) {
                    nodeRoot.panel.moveTag(drop.source.dragTag, drop.source.dragFromCat, nodeRoot.node.id)
                    drop.accept()
                    return
                }
                if (drop.hasUrls) {
                    nodeRoot.panel.dropFilesOnCategory(drop.urls, nodeRoot.node.id)
                    drop.acceptProposedAction()
                    return
                }
                drop.accepted = false
            }
        }

        Row {
            anchors { left: parent.left; right: menuBtn.left; top: parent.top
                      bottom: parent.bottom }
            anchors.leftMargin: 6 + nodeRoot.depth * 14
            anchors.rightMargin: 4
            spacing: 6

            Item {
                id: expander
                anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 18
                readonly property bool hasKids: nodeRoot.node.children.length > 0
                                                || nodeRoot.node.tags.length > 0
                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    visible: expander.hasKids && expHover.hovered
                    color: Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                   App.themeTextPrimary.b, 0.12)
                }
                DrawnIcon {
                    anchors.centerIn: parent
                    visible: expander.hasKids
                    name: nodeRoot.collapsed ? "chevron-right" : "chevron-down"
                    size: 11
                    color: App.themeTextMuted
                }
                HoverHandler { id: expHover; enabled: expander.hasKids
                               cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: expander.hasKids
                             onTapped: nodeRoot.collapsed = !nodeRoot.collapsed }
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
                width: Math.max(0, parent.width - 74)
            }
        }

        //  Die drei Punkte stehen FEST am rechten Rand - unabhängig von der
        //  Ebene. Vorher liefen sie im Zeilen-`Row` mit und rutschten mit
        //  jeder Einrückung weiter nach rechts.
        Item {
            id: menuBtn
            anchors { right: parent.right; rightMargin: 6
                      verticalCenter: parent.verticalCenter }
            width: 22; height: 22

            Rectangle {
                anchors.fill: parent
                radius: 4
                visible: menuHover.hovered
                color: Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                               App.themeTextPrimary.b, 0.12)
            }
            Column {
                anchors.centerIn: parent
                spacing: 2
                Repeater {
                    model: 3
                    delegate: Rectangle {
                        width: 3; height: 3; radius: 1.5
                        color: App.themeTextMuted
                    }
                }
            }
            HoverHandler { id: menuHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: ctxMenu.open() }
            ThemedMenu {
                id: ctxMenu
                MenuItem { text: App.uiText(App.language, "SettingsCatNodeAddSub"); onTriggered: nodeRoot.panel.promptAddSubcategory(nodeRoot.node.id) }
                MenuItem { text: App.uiText(App.language, "TagBarPlaceholder"); onTriggered: nodeRoot.panel.promptAddTag(nodeRoot.node.id) }
                MenuItem { text: App.uiText(App.language, "SettingsCatNodeRename");     onTriggered: nodeRoot.panel.promptRename(nodeRoot.node.id, nodeRoot.node.name) }
                MenuSeparator {}
                MenuItem { text: App.uiText(App.language, "SettingsCatNodeSetUniform"); onTriggered: nodeRoot.panel.promptUniformColor(nodeRoot.node.id) }
                MenuItem {
                    text: App.uiText(App.language, "SettingsCatNodeClearUniform")
                    enabled: nodeRoot.node.uniform
                    onTriggered: nodeRoot.panel.tagsCtl.setCategoryUniformColor(nodeRoot.node.id, false, nodeRoot.node.color, false)
                }
                MenuSeparator {}
                MenuItem { text: App.uiText(App.language, "BookmarkDelete"); onTriggered: nodeRoot.panel.promptDelete(nodeRoot.node.id) }
            }
        }

        HoverHandler { id: headerHover }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: nodeCtxMenu.open()
        }
        ThemedMenu {
            id: nodeCtxMenu
            MenuItem { text: "+  " + App.uiText(App.language, "SettingsCatNodeAddSub")
                       onTriggered: nodeRoot.panel.promptAddSubcategory(nodeRoot.node.id) }
            MenuItem { text: "+  " + App.uiText(App.language, "TagBarPlaceholder")
                       onTriggered: nodeRoot.panel.promptAddTag(nodeRoot.node.id) }
            MenuSeparator {}
            MenuItem { text: App.uiText(App.language, "SettingsCatNodeRename")
                       onTriggered: nodeRoot.panel.promptRename(nodeRoot.node.id, nodeRoot.node.name) }
            MenuItem { text: App.uiText(App.language, "BookmarkDelete")
                       onTriggered: nodeRoot.panel.promptDelete(nodeRoot.node.id) }
        }
    }

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

                property string dragTag: modelData
                property string dragFromCat: nodeRoot.node.id

                // Klarer Toggle-Zustand (einheitlich mit dem Tags-Abschnitt des
                // Panels): aktiv = gefüllt + Häkchen + kräftiger Rand.
                readonly property bool active: nodeRoot.panel.isTagActive(chip.modelData)

                // Über `panel.tagColorOf`, nicht direkt über den Controller: nur so hängt die Bindung am Auffrisch-Zähler des
                // Panels - ein Funktionsaufruf allein bindet an nichts.
                readonly property color effColor: nodeRoot.node.tagUniform
                                                  ? nodeRoot.node.tagColor
                                                  : nodeRoot.panel.tagColorOf(chip.modelData)

                height: 24; radius: 12
                width: chipRow.implicitWidth + 16
                color: Qt.rgba(chip.effColor.r, chip.effColor.g,
                               chip.effColor.b, chip.active ? 0.42 : 0.12)
                border.color: chip.active ? chip.effColor : App.themeBorder
                border.width: chip.active ? 2 : 1

                property real homeX: 0
                property real homeY: 0
                z: dragHandler.active ? 10 : 0
                Drag.active: dragHandler.active
                Drag.source: chip
                Drag.hotSpot.x: width / 2
                Drag.hotSpot.y: height / 2

                Row {
                    id: chipRow
                    anchors.centerIn: parent; spacing: 5
                    Text {
                        visible: chip.active
                        anchors.verticalCenter: parent.verticalCenter
                        text: "\u2713"; color: App.themeTextPrimary
                        font.pixelSize: 10; font.bold: true
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4; color: chip.effColor
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: chip.modelData; color: App.themeTextPrimary; font.pixelSize: 11
                    }
                }

                DragHandler {
                    id: dragHandler
                    // Ins Leere gezogen = aus DIESER Kategorie heraus: `Drag.drop` liefert `Qt.IgnoreAction`, wenn keine Fläche
                    // angenommen hat, und die Sammelfläche der Shell nimmt nur Dateien. Entfernt wird nur aus der Herkunftskategorie.
                    onActiveChanged: {
                        if (active) {
                            chip.homeX = chip.x; chip.homeY = chip.y
                            return
                        }
                        const wirkung = chip.Drag.drop()
                        //  Zurück an den Platz - s. `TagCategoryPanel` ▸ Chip.
                        chip.x = chip.homeX
                        chip.y = chip.homeY
                        if (wirkung === Qt.IgnoreAction)
                            nodeRoot.panel.dropTagOutside(chip.modelData, chip.dragFromCat)
                    }
                }

                // Gleiche Ablegefläche wie im Tags-Abschnitt - ein Tag unter einer Kategorie soll sich nicht anders verhalten.
                // `keys` grenzt gegen den Chip-Zug ab (der trägt keine), sonst nähme sie dem Kopf das Verschieben weg.
                DropArea {
                    id: chipDrop
                    objectName: "catTagChipDrop"   // Griff für tests/tags/tst_dropdelivery
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: function(drop) {
                        if (!drop.hasUrls) { drop.accepted = false; return }
                        nodeRoot.panel.dropFilesOnTag(drop.urls, chip.modelData)
                        drop.acceptProposedAction()
                    }
                }
                //  Rückmeldung beim Ziehen darüber - sonst rät man, ob der Chip
                //  den Zug überhaupt annimmt.
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    visible: chipDrop.containsDrag
                    color: "transparent"
                    border.color: App.themeAccent
                    border.width: 2
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: nodeRoot.panel.toggleTag(chip.modelData)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: chipMenu.open()
                }
                ThemedMenu {
                    id: chipMenu
                    MenuItem { text: App.uiText(App.language, "ModeAddToTag"); onTriggered: nodeRoot.panel.requestAddToTagMode(chip.modelData) }
                    MenuItem { text: App.uiText(App.language, "ModeGroup");    onTriggered: nodeRoot.panel.requestGroupMode(chip.modelData) }
                    MenuSeparator {}
                    MenuItem {
                        text: App.uiText(App.language, "CatNodeRemoveFromCat")
                        onTriggered: nodeRoot.panel.tagsCtl.removeTagFromCategory(nodeRoot.node.id, chip.modelData)
                    }
                }
            }
        }
    }

    // Direktes `CategoryNode { }` löst mit `pragma ComponentBehavior: Bound` den Fehler M129 aus ("Typ kann nicht
    // rekursiv instanziiert werden") - deshalb ein Loader mit source-String, kein statischer Typ-Verweis.
    Column {
        width: parent.width
        spacing: 2
        visible: !nodeRoot.collapsed
        Repeater {
            model: nodeRoot.node.children
            // `setSource()` statt `source:`: CategoryNode benutzt `required property`, die sich nur bei der Erzeugung
            // belegen lassen - mit `source:` brach Qt ab, `onLoaded` feuerte nie und Unterkategorien blieben unsichtbar.
            delegate: Loader {
                id: childLoader
                required property var modelData
                width: parent ? parent.width : 0
                Component.onCompleted: setSource(
                    Qt.resolvedUrl("CategoryNode.qml"),
                    { node:  childLoader.modelData,
                      depth: nodeRoot.depth + 1,
                      panel: nodeRoot.panel })
            }
        }
    }
}
