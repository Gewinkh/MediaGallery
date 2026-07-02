import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  MediaOverlay.qml — leichtgewichtige Info-/Interaktionsschicht über einer
//  Kachel (ersetzt MediaOverlayWidget/Tag-Overlay aus dem Widget-Pfad; KEIN
//  QWidget, kein eigenes QObject pro Tag — reine QML-Items).
//
//  Zeigt: Datei-/Anzeigename (inline umbenennbar), Datum, Tag-Punkte. Im Compact-
//  Modus (App.optionsVisible == false) nur eine schmale Namenszeile.
//  Umbenennen/Tag-Toggle delegieren an mediaModel (per Dateipfad).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: overlay

    property string filePath: ""
    property string displayName: ""
    property var    tags: []
    property var    dateTime
    property bool   compact: false

    // Wird true, solange das Namensfeld editiert wird → Tile unterdrückt Klicks.
    readonly property bool editing: nameLoader.item ? nameLoader.item.visible : false

    implicitHeight: infoColumn.implicitHeight + 12

    // Lesbarkeits-Verlauf am unteren Rand. Unterkanten runden (== Tile-Radius),
    // sonst stuende das gefuellte Rechteck eckig ueber die runden Kachelecken.
    Rectangle {
        anchors.fill: parent
        topLeftRadius: 0
        topRightRadius: 0
        bottomLeftRadius: 10
        bottomRightRadius: 10
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.72) }
        }
    }

    Column {
        id: infoColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        spacing: 3

        // ── Name (Anzeige ⇄ Inline-Edit) ────────────────────────────────────
        Item {
            width: parent.width
            height: 18

            Text {
                id: nameText
                anchors.fill: parent
                visible: !(nameLoader.item && nameLoader.item.visible)
                text: overlay.displayName
                color: "white"
                font.pixelSize: 12
                font.bold: true
                elide: Text.ElideMiddle
                verticalAlignment: Text.AlignVCenter

                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: nameLoader.beginEdit()
                }
            }

            Loader {
                id: nameLoader
                anchors.fill: parent
                active: true
                function beginEdit() {
                    if (item) { item.visible = true; item.text = overlay.displayName; item.selectAll(); item.forceActiveFocus() }
                }
                sourceComponent: TextField {
                    visible: false
                    text: overlay.displayName
                    font.pixelSize: 12
                    padding: 0
                    background: Rectangle { color: App.themeCard; border.color: App.themeAccent; border.width: 1; radius: 2 }
                    color: App.themeTextPrimary
                    onAccepted: { commit(); visible = false }
                    onActiveFocusChanged: if (!activeFocus) visible = false
                    Keys.onEscapePressed: visible = false
                    function commit() {
                        var t = text.trim()
                        if (t.length > 0 && t !== overlay.displayName)
                            mediaModel.renameItem(overlay.filePath, t)
                    }
                }
            }
        }

        // ── Datum ───────────────────────────────────────────────────────────
        Text {
            visible: !overlay.compact
            width: parent.width
            text: overlay.dateTime ? Qt.formatDateTime(overlay.dateTime, "yyyy-MM-dd hh:mm") : ""
            color: Qt.rgba(1, 1, 1, 0.75)
            font.pixelSize: 10
            elide: Text.ElideRight
        }

        // ── Tag-Punkte ──────────────────────────────────────────────────────
        Row {
            visible: !overlay.compact && overlay.tags.length > 0
            spacing: 4
            Repeater {
                model: overlay.tags
                delegate: Rectangle {
                    required property var modelData
                    width: 10; height: 10; radius: 5
                    color: App.tagColor(modelData)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.4)

                    ToolTip.visible: dotHover.hovered
                    ToolTip.text: modelData
                    HoverHandler { id: dotHover }
                }
            }
        }

        // ── Optionen-Modus (S): „Tags anzeigen" / „Kategorien anzeigen" ─────
        //  Zwei Buttons je Kachel; Klick zeigt die Liste der jeweiligen Werte
        //  DIESES Mediums (Tags via App.tagsForFile, Kategorien via
        //  Tags.categoriesForFile — beides frisch aus der JSON-Persistenz).
        Row {
            id: optRow
            visible: !overlay.compact
            spacing: 4

            readonly property string fileName: overlay.filePath.substring(
                Math.max(overlay.filePath.lastIndexOf("/"),
                         overlay.filePath.lastIndexOf("\\")) + 1)

            // ── „+"-Button (S-Modus): neuen Tag erstellen — LINKS vom Tags-Button ──
            //  Bindet an die bestehende Daten-Logik an: App.addTagToFile registriert
            //  den Tag (falls neu) und weist ihn diesem Medium zu.
            Rectangle {
                width: 18; height: 18; radius: 9
                color: addTagHover.hovered ? Qt.rgba(1, 1, 1, 0.28) : Qt.rgba(1, 1, 1, 0.12)
                border.color: Qt.rgba(1, 1, 1, 0.35); border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "+"; color: "white"; font.pixelSize: 11; font.bold: true
                }
                HoverHandler { id: addTagHover }
                TapHandler { onTapped: addPopup.openFor("tag") }
                ToolTip.text: App.uiText(App.language, "PanelAddTagTip")
                ToolTip.visible: addTagHover.hovered
            }
            Rectangle {
                width: tagsBtnLbl.implicitWidth + 12; height: 18; radius: 9
                color: tagsBtnHover.hovered ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(1, 1, 1, 0.12)
                border.color: Qt.rgba(1, 1, 1, 0.35); border.width: 1
                Text {
                    id: tagsBtnLbl
                    anchors.centerIn: parent
                    text: App.uiText(App.language, "OverlayShowTags")
                    color: "white"; font.pixelSize: 9
                }
                HoverHandler { id: tagsBtnHover }
                TapHandler {
                    onTapped: {
                        valuesPopup.title  = App.uiText(App.language, "OverlayShowTags")
                        valuesPopup.values = App.tagsForFile(optRow.fileName)
                        valuesPopup.open()
                    }
                }
            }
            // ── „+"-Button (S-Modus): neue Kategorie erstellen — LINKS vom
            //  Kategorien-Button. Tags.addRootCategory liefert die neue
            //  Kategorie-ID; anschließend wird das Medium via
            //  Tags.toggleFileInCategory direkt zugeordnet.
            Rectangle {
                width: 18; height: 18; radius: 9
                color: addCatHover.hovered ? Qt.rgba(1, 1, 1, 0.28) : Qt.rgba(1, 1, 1, 0.12)
                border.color: Qt.rgba(1, 1, 1, 0.35); border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "+"; color: App.themeAccent; font.pixelSize: 11; font.bold: true
                }
                HoverHandler { id: addCatHover }
                TapHandler { onTapped: addPopup.openFor("category") }
                ToolTip.text: App.uiText(App.language, "PanelAddCategoryTip")
                ToolTip.visible: addCatHover.hovered
            }
            Rectangle {
                width: catsBtnLbl.implicitWidth + 12; height: 18; radius: 9
                color: catsBtnHover.hovered ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(1, 1, 1, 0.12)
                border.color: Qt.rgba(1, 1, 1, 0.35); border.width: 1
                Text {
                    id: catsBtnLbl
                    anchors.centerIn: parent
                    text: App.uiText(App.language, "OverlayShowCategories")
                    color: "white"; font.pixelSize: 9
                }
                HoverHandler { id: catsBtnHover }
                TapHandler {
                    onTapped: {
                        valuesPopup.title  = App.uiText(App.language, "OverlayShowCategories")
                        valuesPopup.values = Tags.categoriesForFile(optRow.fileName)
                        valuesPopup.open()
                    }
                }
            }
        }
    }

    // ── Eingabe-Popup: neuen Tag / neue Kategorie erstellen (S-Modus) ────────
    Popup {
        id: addPopup
        property string mode: "tag"          // "tag" | "category"
        x: 6
        y: overlay.height - height - 6
        padding: 10
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder; border.width: 1
            radius: 6
        }

        function openFor(m) { mode = m; open() }
        onOpened: { addField.text = ""; addField.forceActiveFocus() }

        function commit() {
            var v = addField.text.trim()
            if (v.length > 0) {
                var fn = overlay.filePath.substring(
                    Math.max(overlay.filePath.lastIndexOf("/"),
                             overlay.filePath.lastIndexOf("\\")) + 1)
                if (addPopup.mode === "tag") {
                    App.addTagToFile(fn, v)
                } else {
                    var newId = Tags.addRootCategory(v, Qt.rgba(0, 0.7, 0.63, 1), false)
                    if (newId && newId.length > 0)
                        Tags.toggleFileInCategory(newId, fn)
                }
            }
            close()
        }

        contentItem: Column {
            spacing: 6
            Text {
                text: addPopup.mode === "tag"
                      ? App.uiText(App.language, "CatPanelNewTag")
                      : App.uiText(App.language, "CatPanelAddCategory")
                color: App.themeAccent; font.pixelSize: 12; font.bold: true
            }
            Row {
                spacing: 4
                TextField {
                    id: addField
                    width: 120
                    font.pixelSize: 11
                    color: App.themeTextPrimary
                    background: Rectangle {
                        color: App.themeCard; radius: 4
                        border.color: addField.activeFocus ? App.themeAccent : App.themeBorder
                        border.width: 1
                    }
                    onAccepted: addPopup.commit()
                }
                Button {
                    height: addField.height
                    text: App.uiText(App.language, "SettingsOk")
                    font.pixelSize: 10
                    onClicked: addPopup.commit()
                }
            }
        }
    }

    // ── Werte-Popup (Tags bzw. Kategorien des Mediums) ──────────────────────
    Popup {
        id: valuesPopup
        property string title: ""
        property var values: []
        x: 6
        y: overlay.height - height - 6
        padding: 10
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder; border.width: 1
            radius: 6
        }
        contentItem: Column {
            spacing: 4
            Text {
                text: valuesPopup.title
                color: App.themeAccent; font.pixelSize: 12; font.bold: true
            }
            Text {
                visible: valuesPopup.values.length === 0
                text: App.uiText(App.language, "OverlayNoValues")
                color: App.themeTextMuted; font.pixelSize: 11
            }
            Repeater {
                model: valuesPopup.values
                delegate: Text {
                    required property var modelData
                    text: "\u2022 " + modelData
                    color: App.themeTextPrimary; font.pixelSize: 11
                }
            }
        }
    }
}
