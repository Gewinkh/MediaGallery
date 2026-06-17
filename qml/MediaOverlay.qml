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
    }
}
