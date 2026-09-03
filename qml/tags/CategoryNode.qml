pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  CategoryNode.qml - ein (rekursiver) Knoten des Kategorie-Baums (ersetzt
//  CategoryNode aus TagCategoryPanel.cpp). Instanziiert sich für Unterkategorien
//  selbst. Tags sind ziehbare Chips; eine DropArea nimmt auf einen anderen Knoten
//  gezogene Tags entgegen -> nodeRoot.panel.tagsCtl.moveTagToCategory.
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
        id: header
        width: parent.width
        height: 30
        color: dropArea.containsDrag ? Qt.rgba(0, 0.78, 0.70, 0.18)
                                     : (headerHover.hovered ? App.themeCard : "transparent")
        radius: 5

        //  ── Kategorie UMHÄNGEN (nur im Optionen-Modus, Alt+S) ─────────────
        //  Eine Kategorie auf eine andere ziehen macht sie zu deren
        //  Unterkategorie. **Nur im Optionen-Modus:** im Normalbetrieb ist die
        //  Kopfzeile zum Anklicken da (auf-/zuklappen, filtern), und ein
        //  versehentliches Umhängen des halben Baums wäre teuer. Der Zug ist
        //  DERSELBE Mechanismus wie beim Tag-Chip, nur mit `dragCat` statt
        //  `dragTag` - `catHeaderDrop` unterscheidet danach.
        property string dragCat: nodeRoot.node.id
        //  Zurück an den Platz - s. `TagCategoryPanel` ▸ Chip. Ohne das blieb
        //  die Kopfzeile liegen, wo man sie fallen ließ, und verdeckte den neu
        //  gezeichneten Baum: es sah eingefroren aus (Nutzerbefund 2026-09-03).
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
                //  Ins Leere gezogen: die Kategorie wird zur Hauptkategorie.
                if (wirkung === Qt.IgnoreAction)
                    nodeRoot.panel.dropCategoryOutside(header.dragCat)
            }
        }
        //  Im Optionen-Modus sichtbar machen, dass hier etwas zu greifen ist.
        opacity: catDrag.active ? 0.6 : 1.0

        //  Nimmt ZWEIERLEI an: einen app-intern gezogenen Tag-Chip (-> der Tag
        //  wechselt die Kategorie) und eine gezogene DATEI (-> sie wird Mitglied
        //  dieser Kategorie). Bewusst EINE Fläche für beides: zwei
        //  übereinanderliegende DropAreas würden einander den Zug wegnehmen -
        //  geliefert wird immer nur an die oberste.
        DropArea {
            id: dropArea
            objectName: "catHeaderDrop"      // Griff für tests/tags/tst_dropdelivery
            anchors.fill: parent
            onDropped: function(drop) {
                //  Eine Kategorie auf diese hier: sie wird deren Unterkategorie.
                //  `moveCategory` weist einen Zug in den EIGENEN Teilbaum von
                //  selbst ab - der Knoten ginge sonst verloren.
                //  **`drop.accept()` ist hier PFLICHT.** Ohne sie liefert
                //  `Drag.drop()` beim Ziehenden `Qt.IgnoreAction`, und der hält
                //  den Zug für „ins Leere gefallen" - eine auf eine andere
                //  Kategorie gezogene Kategorie wanderte deshalb anschliessend
                //  auf die Hauptebene, statt dorthin zu gehen, wo man sie
                //  hingezogen hat (Nutzerbefund 2026-09-04). Angenommen wird
                //  auch dann, wenn der Vorgang selbst abgelehnt wird: die
                //  Fläche HAT ihn bearbeitet.
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

            //  Auf-/Zuklappen. **Der Platz bleibt IMMER stehen**, auch wenn es
            //  nichts aufzuklappen gibt: sonst sprang die ganze Zeile nach
            //  rechts, sobald ein Tag oder eine Unterkategorie dazukam
            //  (Nutzerbefund 2026-09-04). Und das Zeichen ist GEZEICHNET, kein
            //  Dreieck aus der Schrift (Regel 28) - das sah je nach System
            //  anders aus und folgte dem Theme nicht.
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
                //  Was nach Klapp-Knopf, Farbpunkt und Häkchen übrig bleibt.
                width: Math.max(0, parent.width - 74)
            }
        }

        //  Die drei Punkte stehen FEST am rechten Rand - unabhängig von der
        //  Ebene. Vorher liefen sie im Zeilen-`Row` mit und rutschten mit jeder
        //  Einrückung weiter nach rechts (Nutzerbefund 2026-09-04).
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
            //  Gezeichnet, nicht als Zeichen aus der Schrift (Regel 28).
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

        //  Rechtsklick auf die Kopfzeile öffnet dasselbe Menü wie der „⋮"-Knopf
        //  (Unterkategorie/Tag anlegen, umbenennen, löschen) - man sucht die
        //  Funktion dort, wo man gerade steht.
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

                // Klarer Toggle-Zustand (einheitlich mit dem Tags-Abschnitt des
                // Panels): aktiv = gefüllt + Häkchen + kräftiger Rand.
                readonly property bool active: nodeRoot.panel.isTagActive(chip.modelData)

                // Effektive Farbe: Einheitsfarbe der Kategorie (bzw. vererbt),
                // sonst die Eigenfarbe des Tags - beim Deaktivieren automatisch zurück.
                //  Über `panel.tagColorOf`, nicht direkt über den Controller:
                //  nur so hängt die Bindung am Auffrisch-Zähler des Panels
                //  (s. dort - ein Funktionsaufruf allein bindet an nichts).
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
                    //  **Ins Leere gezogen = aus DIESER Kategorie heraus.**
                    //  `Drag.drop()` liefert `Qt.IgnoreAction`, wenn keine
                    //  Ablegefläche den Zug angenommen hat - die Sammelfläche
                    //  der Shell nimmt nur Dateien an, also bleibt ein Chip,
                    //  den man irgendwohin fallen lässt, wirklich unangenommen.
                    //  Entfernt wird NUR aus der Kategorie, aus der er kam
                    //  (Festlegung des Nutzers 2026-09-03); der Vorgang steht
                    //  danach in der Rückgängig-Leiste.
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

                //  ── Kachel auf den Tag ziehen ⇒ Datei bekommt ihn ──
                //  Gleiche Fläche wie im Tags-Abschnitt des Panels; ein Tag
                //  unter einer Kategorie soll sich nicht anders verhalten als
                //  derselbe Tag in der Liste darüber. `keys` grenzt sauber gegen
                //  den Chip-Zug ab (der trägt keine Schlüssel) - sonst nähme
                //  diese Fläche dem Kategorie-Kopf das Verschieben weg.
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

    // ── Unterkategorien (rekursiv) ─────────────────────────────────────────────
    // Hinweis: Direktes `CategoryNode { }` hier würde mit pragma ComponentBehavior: Bound
    // den Fehler M129 ("Typ kann nicht rekursiv instanziiert werden") auslösen.
    // Lösung: Loader mit source-String -> kein statischer Typ-Verweis zur Compile-Zeit.
    Column {
        width: parent.width
        spacing: 2
        visible: !nodeRoot.collapsed
        Repeater {
            model: nodeRoot.node.children
            //  setSource() statt `source:`: CategoryNode benutzt `required
            //  property`, die nur bei der Erzeugung belegt werden koennen.
            //  Mit `source:` brach Qt die Erzeugung ab („Required property …
            //  was not initialized"), onLoaded feuerte nie - UNTERkategorien
            //  waren dadurch auch im Hauptbildschirm unsichtbar.
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
