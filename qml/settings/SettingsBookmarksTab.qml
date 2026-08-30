pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// ── Lesezeichen: gespeicherte Ordner in Gruppen verwalten ────────────────────
//
//  Die Liste ist NACH GRUPPEN gegliedert (`App.bookmarkTree`): zuoberst die
//  Lesezeichen ohne Gruppe, darunter je Gruppe eine Kopfzeile mit ihren
//  Einträgen. Dieselbe Ordnung zeigt das Hauptmenü ▸ Ordner.
//
//  ZIEHEN UND ABLEGEN - der ganze Zweck dieses Reiters:
//   • Am **Griff** (drei Striche) links zieht man einen Eintrag oder eine ganze
//     Gruppe. Gezogen wird ein STELLVERTRETER (`dragGhost`) statt der Zeile
//     selbst: die Zeilen liegen in einem Layout, das eine verschobene Zeile
//     sofort zurückrückt - der Zug sähe aus, als hinge er fest.
//   • Ablegen auf einer ZEILE: der Eintrag wandert in deren Gruppe an deren
//     Platz. Ablegen auf einer KOPFZEILE: ans Ende dieser Gruppe. Eine Gruppe
//     auf eine Kopfzeile gelegt tauscht die Reihenfolge der Gruppen.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    // Nach Gruppen gegliedert; Abschnitt 0 ist immer „ohne Gruppe".
    readonly property var tree: App.bookmarkTree
    //  Gibt es überhaupt Gruppen? Ohne sie bleibt der Reiter die schlichte
    //  Liste, die er vorher war - inklusive der Kopfzeile „Ohne Gruppe", die
    //  dann nichts erklären würde.
    readonly property bool hasGroups: tree.length > 1

    // Index für den Lösch-Dialog (Hinzufügen/Bearbeiten liegt in BookmarkEditDialog).
    property int deleteIndex: -1
    // Gruppe, die gerade umbenannt oder gelöscht wird.
    property string groupTarget: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: App.uiText(App.language, "SettingsBookHint")
                    color: App.themeTextMuted; font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: App.uiText(App.language, "BookmarkDragHint")
                    color: App.themeTextMuted; font.pixelSize: 11
                }
            }
            Button {
                text: App.uiText(App.language, "BookmarkGroupAdd")
                onClicked: {
                    root.groupTarget = ""
                    groupNameField.text = ""
                    groupDialog.title = App.uiText(App.language, "BookmarkGroupNewTitle")
                    groupDialog.open()
                    groupNameField.forceActiveFocus()
                }
            }
            Button {
                text: App.uiText(App.language, "SettingsBookBtnAdd")
                highlighted: true
                onClicked: bookmarkEditDialog.openAdd()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: App.themeBorder }

        ScrollView {
            id: bmScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: root.width
                spacing: 6

                // ── Ein Abschnitt: „ohne Gruppe" oder eine benannte Gruppe ───
                Repeater {
                    model: root.tree
                    delegate: ColumnLayout {
                        id: sec
                        required property int index
                        required property var modelData

                        readonly property string groupName: sec.modelData.group
                        readonly property bool   isGroup:   sec.groupName.length > 0
                        //  Platz in der Gruppenliste (Abschnitt 0 = ohne Gruppe).
                        readonly property int    groupPos:  sec.index - 1
                        readonly property bool   collapsed: sec.isGroup && sec.modelData.collapsed

                        Layout.fillWidth: true
                        spacing: 4

                        // ── Kopfzeile ───────────────────────────────────────
                        Rectangle {
                            id: header
                            Layout.fillWidth: true
                            implicitHeight: 34
                            radius: 6
                            visible: sec.isGroup || root.hasGroups
                            color: headDrop.containsDrag
                                   ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                   : (sec.isGroup ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                            border.color: sec.isGroup ? App.themeBorder : "transparent"

                            //  Nimmt Einträge (ans Ende dieser Gruppe) UND ganze
                            //  Gruppen (Reihenfolge der Gruppen) an.
                            DropArea {
                                id: headDrop
                                anchors.fill: parent
                                onDropped: function(drop) {
                                    if (dragGhost.payloadKind === "bookmark") {
                                        App.moveBookmark(dragGhost.payloadIndex, sec.groupName, -1)
                                        drop.accept()
                                    } else if (dragGhost.payloadKind === "group" && sec.isGroup) {
                                        App.moveBookmarkGroup(dragGhost.payloadGroupPos, sec.groupPos)
                                        drop.accept()
                                    } else {
                                        drop.accepted = false
                                    }
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 8

                                // Griff - nur benannte Gruppen lassen sich ordnen.
                                Item {
                                    implicitWidth: 16; implicitHeight: 16
                                    visible: sec.isGroup
                                    DrawnIcon {
                                        anchors.centerIn: parent
                                        name: "snap"; size: 14
                                        color: App.themeTextMuted
                                    }
                                    DragHandler {
                                        target: null
                                        onActiveChanged: {
                                            if (active) dragGhost.begin("group", -1, sec.groupName, sec.groupPos)
                                            else        dragGhost.finish()
                                        }
                                        onCentroidChanged: dragGhost.follow(centroid.scenePosition)
                                    }
                                }

                                // Auf-/Zuklappen (nur benannte Gruppen).
                                Item {
                                    implicitWidth: sec.isGroup ? 16 : 0
                                    implicitHeight: 16
                                    visible: sec.isGroup
                                    DrawnIcon {
                                        anchors.centerIn: parent
                                        name: sec.collapsed ? "chevron-right" : "chevron-down"
                                        size: 12
                                        color: App.themeTextMuted
                                    }
                                    TapHandler {
                                        onTapped: App.setBookmarkGroupCollapsed(sec.groupName,
                                                                                !sec.collapsed)
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: sec.isGroup ? sec.groupName
                                                      : App.uiText(App.language, "BookmarkGroupUngrouped")
                                    color: sec.isGroup ? App.themeTextPrimary : App.themeTextMuted
                                    font.pixelSize: 13
                                    font.bold: sec.isGroup
                                }
                                Text {
                                    text: sec.modelData.items.length
                                    color: App.themeTextMuted; font.pixelSize: 11
                                }
                                Button {
                                    visible: sec.isGroup
                                    text: App.uiText(App.language, "BookmarkEdit")
                                    onClicked: {
                                        root.groupTarget = sec.groupName
                                        groupNameField.text = sec.groupName
                                        groupDialog.title = App.uiText(App.language, "BookmarkGroupRenameTitle")
                                        groupDialog.open()
                                        groupNameField.forceActiveFocus()
                                    }
                                }
                                Button {
                                    visible: sec.isGroup
                                    text: App.uiText(App.language, "BookmarkDelete")
                                    onClicked: {
                                        root.groupTarget = sec.groupName
                                        groupDeleteDialog.open()
                                    }
                                }
                            }
                        }

                        // ── Die Lesezeichen dieses Abschnitts ───────────────
                        Repeater {
                            model: sec.collapsed ? [] : sec.modelData.items
                            delegate: Rectangle {
                                id: bmRow
                                required property int index
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.leftMargin: sec.isGroup ? 18 : 0
                                implicitHeight: 56
                                radius: 6
                                color: rowDrop.containsDrag
                                       ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                       : Qt.rgba(1, 1, 1, 0.03)
                                border.color: App.themeBorder

                                //  Ablegen auf einer Zeile: in DEREN Gruppe an
                                //  DEREN Platz. Gruppen legt man nur auf
                                //  Kopfzeilen ab - sonst hinge die Reihenfolge
                                //  der Gruppen an einem beliebigen Eintrag.
                                DropArea {
                                    id: rowDrop
                                    anchors.fill: parent
                                    onDropped: function(drop) {
                                        if (dragGhost.payloadKind !== "bookmark"
                                            || dragGhost.payloadIndex === bmRow.modelData.index) {
                                            drop.accepted = false
                                            return
                                        }
                                        App.moveBookmark(dragGhost.payloadIndex,
                                                         sec.groupName, bmRow.index)
                                        drop.accept()
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 8

                                    Item {
                                        implicitWidth: 16; implicitHeight: 16
                                        DrawnIcon {
                                            anchors.centerIn: parent
                                            name: "snap"; size: 14
                                            color: App.themeTextMuted
                                        }
                                        DragHandler {
                                            target: null
                                            onActiveChanged: {
                                                if (active) dragGhost.begin("bookmark",
                                                                            bmRow.modelData.index,
                                                                            bmRow.modelData.name, -1)
                                                else        dragGhost.finish()
                                            }
                                            onCentroidChanged: dragGhost.follow(centroid.scenePosition)
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Text {
                                            text: bmRow.modelData.name
                                            color: App.themeTextPrimary
                                            font.pixelSize: 13; font.bold: true
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: bmRow.modelData.path
                                            color: App.themeTextMuted
                                            font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                            Layout.fillWidth: true
                                        }
                                    }

                                    Button {
                                        text: App.uiText(App.language, "BookmarkEdit")
                                        onClicked: bookmarkEditDialog.openEdit(bmRow.modelData.index,
                                                                               bmRow.modelData.name,
                                                                               bmRow.modelData.path,
                                                                               bmRow.modelData.group)
                                    }
                                    Button {
                                        text: App.uiText(App.language, "BookmarkDelete")
                                        onClicked: {
                                            root.deleteIndex = bmRow.modelData.index
                                            deleteDialog.open()
                                        }
                                    }
                                }
                            }
                        }

                        //  Leere, aufgeklappte Gruppe: eine Zeile, die sagt,
                        //  wofür sie da ist - und die man treffen kann.
                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: 18
                            visible: sec.isGroup && !sec.collapsed
                                     && sec.modelData.items.length === 0
                            text: App.uiText(App.language, "BookmarkGroupEmptyHint")
                            color: App.themeTextMuted; font.pixelSize: 11
                            padding: 6
                        }
                    }
                }

                Text {
                    visible: App.savedFolders.length === 0 && !root.hasGroups
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: App.uiText(App.language, "SettingsBookEmpty")
                    color: App.themeTextMuted
                    padding: 16
                }
            }
        }
    }

    // ── Stellvertreter des Zuges ─────────────────────────────────────────────
    //  Ein einziges Item für ALLE Züge dieses Reiters. Es hängt an `root` (kein
    //  Layout), lässt sich also frei bewegen, und trägt die Nutzlast, die die
    //  Ablegeflächen auslesen.
    Rectangle {
        id: dragGhost
        z: 100
        width: 200; height: 28; radius: 5
        visible: dragging
        opacity: 0.92
        color: App.themeCard
        border.color: App.themeAccent; border.width: 1

        property bool   dragging: false
        property string payloadKind: ""      // "bookmark" | "group"
        property int    payloadIndex: -1     // Platz in der gespeicherten Liste
        property string payloadLabel: ""
        property int    payloadGroupPos: -1

        Drag.active: dragGhost.dragging
        Drag.source: dragGhost
        Drag.hotSpot.x: 14
        Drag.hotSpot.y: 14

        Text {
            anchors.fill: parent
            anchors.leftMargin: 10; anchors.rightMargin: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: dragGhost.payloadLabel
            color: App.themeTextPrimary; font.pixelSize: 12
        }

        function begin(kind, index, label, groupPos) {
            dragGhost.payloadKind     = kind
            dragGhost.payloadIndex    = index
            dragGhost.payloadLabel    = label
            dragGhost.payloadGroupPos = groupPos
            dragGhost.dragging        = true
        }
        function follow(scenePos) {
            if (!dragGhost.dragging) return
            var p = root.mapFromItem(null, scenePos.x, scenePos.y)
            dragGhost.x = p.x - dragGhost.Drag.hotSpot.x
            dragGhost.y = p.y - dragGhost.Drag.hotSpot.y
        }
        function finish() {
            if (!dragGhost.dragging) return
            dragGhost.Drag.drop()
            dragGhost.dragging = false
            dragGhost.payloadKind = ""
            dragGhost.payloadIndex = -1
            dragGhost.payloadGroupPos = -1
        }
    }

    // ── Hinzufügen / Bearbeiten (geteilt mit ApplicationShell) ───────────────
    BookmarkEditDialog { id: bookmarkEditDialog }

    // ── Gruppe anlegen / umbenennen ─────────────────────────────────────────
    Dialog {
        id: groupDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.NoButton
        width: 380
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }

        function commit() {
            var n = groupNameField.text.trim()
            if (n.length === 0) return
            if (root.groupTarget.length === 0) App.addBookmarkGroup(n)
            else                               App.renameBookmarkGroup(root.groupTarget, n)
            groupDialog.close()
        }

        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Label {
                    text: App.uiText(App.language, "BookmarkGroupNameLabel")
                    color: App.themeTextPrimary
                }
                TextField {
                    id: groupNameField
                    Layout.fillWidth: true
                    color: App.themeTextPrimary
                    onAccepted: groupDialog.commit()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: groupDialog.close()
                }
                Button {
                    text: App.uiText(App.language, "SettingsOk")
                    highlighted: true
                    enabled: groupNameField.text.trim().length > 0
                    onClicked: groupDialog.commit()
                }
            }
        }
    }

    Dialog {
        id: groupDeleteDialog
        title: App.uiText(App.language, "BookmarkGroupDeleteTitle")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        onAccepted: App.removeBookmarkGroup(root.groupTarget)
        //  **Eingabetaste bestaetigt.** `focus: true` am Dialog UND ein
        //  `Keys`-Handler am `contentItem` - beides noetig: `standardButtons`
        //  allein wertet `Return` NICHT aus (der Fokus liegt dann auf der
        //  `DialogButtonBox`, und die hat keinen Vorgabe-Knopf), und ein
        //  `Keys`-Handler am Dialog selbst feuert gar nicht. Beides gemessen.
        focus: true
        contentItem: Item {
            focus: true
            Keys.onReturnPressed: function(e) { groupDeleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { groupDeleteDialog.accept(); e.accepted = true }
            implicitWidth: 300
            implicitHeight: groupDelText.implicitHeight
            Text {
                id: groupDelText
                width: parent.width
                text: App.uiText(App.language, "BookmarkGroupDeleteText")
                color: App.themeTextPrimary; wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: deleteDialog
        title: App.uiText(App.language, "SettingsBookDeleteTitle")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }
        onAccepted: App.removeBookmark(root.deleteIndex)
        //  **Eingabetaste bestaetigt.** `focus: true` am Dialog UND ein
        //  `Keys`-Handler am `contentItem` - beides noetig: `standardButtons`
        //  allein wertet `Return` NICHT aus (der Fokus liegt dann auf der
        //  `DialogButtonBox`, und die hat keinen Vorgabe-Knopf), und ein
        //  `Keys`-Handler am Dialog selbst feuert gar nicht. Beides gemessen.
        focus: true
        // Umbrechender Text in einem Item mit FESTER implicitWidth: Als
        // contentItem bestimmt dessen implicitWidth die Dialogbreite. Ein
        // umbrechender Text meldet dagegen eine implicitWidth, die von seiner
        // (vom Dialog gesetzten) Breite abhängt -> Rückkopplung
        // Dialog.implicitWidth ↔ Textumbruch ("Binding loop detected").
        contentItem: Item {
            focus: true
            Keys.onReturnPressed: function(e) { deleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { deleteDialog.accept(); e.accepted = true }
            implicitWidth: 280
            implicitHeight: delText.implicitHeight
            Text {
                id: delText
                width: parent.width
                text: App.uiText(App.language, "SettingsBookDeleteConfirm")
                color: App.themeTextPrimary; wrapMode: Text.WordWrap
            }
        }
    }

    //  Weiches, schnelles Mausrad-Scrollen (Galerie-Muster) statt der festen
    //  60 px je Rastung von `Flickable`.
    SmoothWheelArea { flickable: bmScroll.contentItem }
}
