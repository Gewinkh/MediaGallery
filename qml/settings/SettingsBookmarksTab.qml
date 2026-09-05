pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Die Liste ist EINE FLACHE Zeilenliste (`App.bookmarkTree`), eingerückt nach `depth`: über die Grenze zweier
// ineinander liegender Repeater hinweg liesse sich nichts ziehen - genau das ist hier der Zweck. Gezogen wird
// ein STELLVERTRETER, weil das Layout eine verschobene Zeile sofort zurückrückt.
Item {
    id: root

    readonly property var rows: {
        var all = App.bookmarkTree
        var out = []
        for (var i = 0; i < all.length; i++)
            if (!all[i].hidden) out.push(all[i])
        return out
    }
    readonly property bool hasRows: rows.length > 0

    property int deleteIndex: -1
    property string groupTarget: ""
    property string groupParent: ""

    component IconBtn: Rectangle {
        id: ib
        property string iconName: ""
        property string tip: ""
        signal activated()

        implicitWidth: 28
        implicitHeight: 26
        radius: 5
        color: ibHover.hovered ? Qt.lighter(App.themeCard, 1.35) : "transparent"
        border.width: 1
        border.color: ibHover.hovered ? App.themeBorder : "transparent"

        DrawnIcon {
            anchors.centerIn: parent
            name: ib.iconName
            size: 15
            color: ibHover.hovered ? App.themeTextPrimary : App.themeTextMuted
        }
        HoverHandler { id: ibHover }
        TapHandler  { onTapped: ib.activated() }

        ToolTip.visible: ibHover.hovered && ib.tip.length > 0
        ToolTip.text: ib.tip
        ToolTip.delay: 500
    }

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
                onClicked: groupDialog.openAdd("")
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
                spacing: 4

                // „Ohne Gruppe" - die oberste Ebene als Ablegeziel
                //  Ohne diese Zeile käme nichts mehr aus einer Gruppe heraus.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    radius: 6
                    visible: root.hasRows
                    color: rootDrop.containsDrag
                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                           : "transparent"
                    border.color: rootDrop.containsDrag ? App.themeAccent : "transparent"
                    border.width: 1

                    DropArea {
                        id: rootDrop
                        anchors.fill: parent
                        onDropped: function(drop) {
                            if (dragGhost.payloadKind === "bookmark")
                                App.moveBookmark(dragGhost.payloadIndex, "", -1)
                            else if (dragGhost.payloadKind === "group")
                                App.moveBookmarkGroup(dragGhost.payloadGroup, "", -1)
                            else { drop.accepted = false; return }
                            drop.accept()
                        }
                    }
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: App.uiText(App.language, "BookmarkGroupUngrouped")
                        color: App.themeTextMuted; font.pixelSize: 12
                    }
                }

                Repeater {
                    model: root.rows
                    delegate: ColumnLayout {
                        id: line
                        required property int index
                        required property var modelData

                        readonly property bool   isGroup: line.modelData.kind === "group"
                        readonly property string groupPath: line.modelData.group
                        readonly property int    depth: line.modelData.depth

                        Layout.fillWidth: true
                        spacing: 0

                        // Einfügestreifen "davor, auf DERSELBEN Ebene" - ein Ablegen AUF einer Gruppe verschachtelt. Der Platz wird
                        // IMMER reserviert, sichtbar wird nur die Farbe: sonst sprängen beim Zug alle Zeilen um 10 px nach unten.
                        Rectangle {
                            id: beforeStrip
                            readonly property bool groupDrag: dragGhost.payloadKind === "group"

                            Layout.fillWidth: true
                            Layout.leftMargin: line.depth * 18
                            implicitHeight: line.isGroup ? 10 : 0
                            visible: line.isGroup
                            radius: 3
                            color: beforeDrop.containsDrag ? App.themeAccent : "transparent"
                            opacity: !beforeStrip.groupDrag ? 0
                                     : (beforeDrop.containsDrag ? 0.75 : 0.25)
                            border.color: App.themeAccent
                            border.width: (beforeStrip.groupDrag && !beforeDrop.containsDrag) ? 1 : 0

                            DropArea {
                                id: beforeDrop
                                anchors.fill: parent
                                //  Ausserhalb eines Gruppen-Zuges kein Ziel -
                                //  sonst finge der Streifen Einträge ab, für die
                                //  er nichts tun kann.
                                enabled: beforeStrip.groupDrag
                                onDropped: function(drop) {
                                    if (dragGhost.payloadKind !== "group") {
                                        drop.accepted = false
                                        return
                                    }
                                    App.moveBookmarkGroup(dragGhost.payloadGroup,
                                                          line.modelData.parent,
                                                          line.modelData.pos)
                                    drop.accept()
                                }
                            }
                        }

                        Rectangle {
                            id: rowBox
                            Layout.fillWidth: true
                            Layout.leftMargin: line.depth * 18
                            implicitHeight: line.isGroup ? 34 : 52
                            radius: 6
                            color: rowDrop.containsDrag
                                   ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                   : (line.isGroup ? Qt.rgba(1, 1, 1, 0.05)
                                                   : Qt.rgba(1, 1, 1, 0.03))
                            border.color: App.themeBorder
                            border.width: 1

                            DropArea {
                                id: rowDrop
                                anchors.fill: parent
                                onDropped: function(drop) {
                                    if (dragGhost.payloadKind === "bookmark") {
                                        if (!line.isGroup
                                            && dragGhost.payloadIndex === line.modelData.index) {
                                            drop.accepted = false
                                            return
                                        }
                                        App.moveBookmark(dragGhost.payloadIndex,
                                                         line.groupPath,
                                                         line.isGroup ? -1 : line.modelData.pos)
                                        drop.accept()
                                    } else if (dragGhost.payloadKind === "group") {
                                        App.moveBookmarkGroup(dragGhost.payloadGroup,
                                                              line.groupPath, -1)
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
                                spacing: 6

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
                                            if (active) {
                                                if (line.isGroup)
                                                    dragGhost.begin("group", -1,
                                                                    line.modelData.name,
                                                                    line.groupPath)
                                                else
                                                    dragGhost.begin("bookmark",
                                                                    line.modelData.index,
                                                                    line.modelData.name, "")
                                            } else {
                                                dragGhost.finish()
                                            }
                                        }
                                        onCentroidChanged: dragGhost.follow(centroid.scenePosition)
                                    }
                                }

                                Item {
                                    implicitWidth: line.isGroup ? 16 : 0
                                    implicitHeight: 16
                                    visible: line.isGroup
                                    DrawnIcon {
                                        anchors.centerIn: parent
                                        name: line.modelData.collapsed ? "chevron-right"
                                                                       : "chevron-down"
                                        size: 12
                                        color: App.themeTextMuted
                                    }
                                    TapHandler {
                                        onTapped: App.setBookmarkGroupCollapsed(
                                                      line.groupPath,
                                                      !line.modelData.collapsed)
                                    }
                                }

                                Text {
                                    visible: line.isGroup
                                    Layout.fillWidth: line.isGroup
                                    elide: Text.ElideRight
                                    text: line.modelData.name
                                    color: App.themeTextPrimary
                                    font.pixelSize: 13; font.bold: true
                                }
                                Text {
                                    visible: line.isGroup
                                    //  Eine Eintragszeile hat kein `count` -
                                    //  die Bindung wird trotz `visible: false`
                                    //  ausgewertet und meldete `undefined`.
                                    text: line.isGroup ? line.modelData.count : ""
                                    color: App.themeTextMuted; font.pixelSize: 11
                                }

                                ColumnLayout {
                                    visible: !line.isGroup
                                    Layout.fillWidth: !line.isGroup
                                    spacing: 1
                                    Text {
                                        text: line.modelData.name
                                        color: App.themeTextPrimary
                                        font.pixelSize: 13; font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: line.modelData.path
                                        color: App.themeTextMuted
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                }

                                IconBtn {
                                    visible: line.isGroup
                                    iconName: "plus"
                                    tip: App.uiText(App.language, "BookmarkGroupSubAdd")
                                    onActivated: groupDialog.openAdd(line.groupPath)
                                }
                                IconBtn {
                                    iconName: "gear"
                                    tip: App.uiText(App.language, "BookmarkEdit")
                                    onActivated: {
                                        if (line.isGroup)
                                            groupDialog.openRename(line.groupPath,
                                                                   line.modelData.name)
                                        else
                                            bookmarkEditDialog.openEdit(line.modelData.index,
                                                                        line.modelData.name,
                                                                        line.modelData.path,
                                                                        line.modelData.group)
                                    }
                                }
                                IconBtn {
                                    iconName: "trash"
                                    tip: App.uiText(App.language, "BookmarkDelete")
                                    onActivated: {
                                        if (line.isGroup) {
                                            root.groupTarget = line.groupPath
                                            groupDeleteDialog.open()
                                        } else {
                                            root.deleteIndex = line.modelData.index
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
                            Layout.leftMargin: (line.depth + 1) * 18
                            Layout.topMargin: 2
                            visible: line.isGroup && !line.modelData.collapsed
                                     && line.modelData.count === 0
                            text: App.uiText(App.language, "BookmarkGroupEmptyHint")
                            color: App.themeTextMuted; font.pixelSize: 11
                        }
                    }
                }

                Text {
                    visible: !root.hasRows
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: App.uiText(App.language, "SettingsBookEmpty")
                    color: App.themeTextMuted
                    padding: 16
                }
            }
        }
    }

    // Ein einziges Stellvertreter-Item für ALLE Züge dieses Reiters. Es hängt an `root` statt an einem Layout,
    // lässt sich also frei bewegen, und trägt die Nutzlast für die Ablegeflächen.
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
        property string payloadGroup: ""     // voller Pfad der gezogenen Gruppe

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

        function begin(kind, index, label, groupPath) {
            dragGhost.payloadKind  = kind
            dragGhost.payloadIndex = index
            dragGhost.payloadLabel = label
            dragGhost.payloadGroup = groupPath
            dragGhost.dragging     = true
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
            dragGhost.payloadGroup = ""
        }
    }

    BookmarkEditDialog { id: bookmarkEditDialog }

    Dialog {
        id: groupDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.NoButton
        width: 420
        background: Rectangle { color: App.themeCard; border.color: App.themeBorder; radius: 8 }

        function openAdd(parentPath) {
            root.groupTarget = ""
            root.groupParent = parentPath
            groupNameField.text = ""
            groupDialog.title = App.uiText(App.language, "BookmarkGroupNewTitle")
            groupDialog.open()
            groupNameField.forceActiveFocus()
        }
        function openRename(path, leafName) {
            root.groupTarget = path
            root.groupParent = ""
            groupNameField.text = leafName
            groupDialog.title = App.uiText(App.language, "BookmarkGroupRenameTitle")
            groupDialog.open()
            groupNameField.forceActiveFocus()
        }

        readonly property bool nameOk: App.isUsableGroupName(groupNameField.text)

        function commit() {
            if (!groupDialog.nameOk) return
            var n = groupNameField.text.trim()
            if (root.groupTarget.length === 0) App.addBookmarkGroup(n, root.groupParent)
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
            //  Wo die neue Gruppe landet - ohne diese Zeile wäre beim Anlegen
            //  einer Untergruppe nicht zu sehen, unter WEM sie entsteht.
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                visible: root.groupTarget.length === 0
                Label {
                    text: App.uiText(App.language, "BookmarkGroupParentLabel")
                    color: App.themeTextMuted
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    text: root.groupParent.length > 0
                          ? root.groupParent
                          : App.uiText(App.language, "BookmarkGroupParentRoot")
                    color: App.themeTextPrimary
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: groupNameField.text.length > 0 && !groupDialog.nameOk
                text: App.uiText(App.language, "BookmarkGroupNameInvalid")
                color: App.themeTextMuted; font.pixelSize: 11
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
                    enabled: groupDialog.nameOk
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
        // `focus: true` am Dialog UND ein `Keys`-Handler am `contentItem` - beides nötig: `standardButtons` allein
        // wertet `Return` nicht aus (Fokus auf der DialogButtonBox ohne Vorgabe-Knopf), und `Keys` am Dialog feuert nie.
        focus: true
        contentItem: Item {
            focus: true
            Keys.onReturnPressed: function(e) { groupDeleteDialog.accept(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { groupDeleteDialog.accept(); e.accepted = true }
            implicitWidth: 320
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
        focus: true
        // Umbrechender Text in einem Item mit FESTER `implicitWidth`: als contentItem bestimmt dessen implicitWidth die
        // Dialogbreite, ein umbrechender Text meldet dagegen eine, die von seiner gesetzten Breite abhängt.
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

    SmoothWheelArea { flickable: bmScroll.contentItem }
}
