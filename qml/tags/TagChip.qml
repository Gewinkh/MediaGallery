pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

//  EIN Tag-Chip des Panels. Eigene Datei, damit ihn ein asynchroner `Loader`
//  einzeln erzeugen kann: vierhundert Chips auf einen Schlag blockierten den
//  GUI-Faden ueber eine halbe Sekunde.
Rectangle {
    id: pChip

    required property string name
    //  Das Panel liefert Farben, Aktivzustand und alle Aktionen.
    required property var panel

    readonly property string modelData: pChip.name

    readonly property color tc: panel.tagColorOf(pChip.modelData)
    readonly property bool active: panel.isTagActive(pChip.modelData)

    // Dieselben Nutzdaten wie die Chips unter einer Kategorie, damit die Ablegefläche des Kategorie-Kopfes beide
    // annimmt. `dragFromCat` bleibt leer: dieser Chip kommt aus der Liste, es wird nur HINZUGEFÜGT.
    property string dragTag: modelData
    property string dragFromCat: ""
    // Der Chip muss an seinen Platz zurück: ein `DragHandler` verschiebt sein Ziel, das `Flow` darüber setzt `x`/`y`
    // aber nur beim Auslegen neu. Ohne das Zurücksetzen blieb er liegen, wo man ihn fallen ließ.
    property real homeX: 0
    property real homeY: 0
    Drag.active: pDrag.active
    Drag.source: pChip
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2
    z: pDrag.active ? 10 : 0
    DragHandler {
        id: pDrag
        onActiveChanged: {
            if (active) {
                pChip.homeX = pChip.x; pChip.homeY = pChip.y
                return
            }
            pChip.Drag.drop()          // erst zustellen …
            pChip.x = pChip.homeX      // … dann zurück
            pChip.y = pChip.homeY
        }
    }

    height: 24; radius: 12
    width: pRow.implicitWidth + 16
    color: active ? Qt.rgba(tc.r, tc.g, tc.b, 0.42)
                  : Qt.rgba(tc.r, tc.g, tc.b, 0.10)
    border.color: active ? tc : App.themeBorder
    border.width: active ? 2 : 1

    Row {
        id: pRow
        anchors.centerIn: parent; spacing: 5
        Text {
            visible: pChip.active
            anchors.verticalCenter: parent.verticalCenter
            text: "\u2713"; color: App.themeTextPrimary
            font.pixelSize: 10; font.bold: true
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8; height: 8; radius: 4; color: pChip.tc
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: pChip.modelData
            color: pChip.active ? App.themeTextPrimary : App.themeTextMuted
            font.pixelSize: 11
        }
    }

    // Die Kachel zieht als PLATTFORM-Zug hinaus; landet er wieder im eigenen Fenster, kommt er hier als
    // gewöhnlicher Datei-Drop an - dieselbe Fläche nimmt deshalb auch Dateien von außen. Immer `addTag`, nie Toggle.
    DropArea {
        id: chipDrop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: function(drop) {
            if (!drop.hasUrls) { drop.accepted = false; return }
            panel.dropFilesOnTag(drop.urls, pChip.modelData)
            drop.acceptProposedAction()
        }
    }
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
        onTapped: panel.toggleTag(pChip.modelData)
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: pChipMenu.open()
    }
    ThemedMenu {
        id: pChipMenu
        MenuItem { text: App.uiText(App.language, "ModeAddToTag"); onTriggered: panel.requestAddToTagMode(pChip.modelData) }
        MenuItem { text: App.uiText(App.language, "ModeGroup");    onTriggered: panel.requestGroupMode(pChip.modelData) }
        MenuSeparator {}
        MenuItem { text: "+  " + App.uiText(App.language, "CatPanelNewTag")
                   onTriggered: panel.promptNewTag() }
        MenuItem { text: App.uiText(App.language, "SettingsTagDelete")
                   onTriggered: panel.promptDeleteTag(pChip.modelData) }
    }
}
