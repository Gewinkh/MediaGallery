// Bilder aus dem Dokumentordner als Miniaturen; beide Editoren bieten dieselbe Abkürzung an, deshalb
// liegt sie EINMAL hier. `hostWidth` ist die Breite der KACHEL, nicht des Bildschirms - feste 396 px
// ragten in der geteilten Ansicht hinaus. Miniaturen laden asynchron, `sourceSize` deckelt sie.
import QtQuick
import QtQuick.Controls
//  Ohne diesen Import ist `App` in einer AUSGELAGERTEN Komponente unbekannt
//  (im eingebetteten Zustand kam er vom umgebenden Surface) - alle Farben und
//  Texte blieben dann leer.
import MediaGallery 1.0

Popup {
    id: pick

    property var  entries: []
    //  NICHT `availableWidth` nennen: das ist bei `Popup` eine FINAL-
    //  Eigenschaft (Breite minus Innenabstand). Ein Überschreiben lässt die
    //  ganze Komponente nicht mehr laden - „Cannot override FINAL property".
    property real hostWidth: 420
    //  Kantenlänge einer Zelle; die Miniatur sitzt darin.
    readonly property int cellW: 98
    readonly property int cellH: 96
    //  Wie viele Spalten passen? Mindestens eine, höchstens vier (breiter
    //  wirkte das Popup wie ein Dateibrowser), begrenzt durch die Kachel.
    readonly property int columns: Math.max(1, Math.min(4,
        Math.floor((Math.max(120, hostWidth) - 2 * padding - 18) / cellW)))
    readonly property int rows: Math.max(1, Math.ceil(entries.length / columns))

    signal picked(string url)
    signal browseRequested()

    y: parent ? parent.height + 4 : 0
    padding: 8

    background: Rectangle {
        color: App.themeMenuBarBg
        border.color: App.themeBorder
        radius: 8
    }

    contentItem: Column {
        spacing: 6
        Text {
            text: App.uiText(App.language, "DocxImageFromFolder")
            color: App.themeTextMuted
            font.pixelSize: 11
        }
        Text {
            visible: pick.entries.length === 0
            text: App.uiText(App.language, "DocxNoImagesInFolder")
            color: App.themeTextPrimary
            font.pixelSize: 12
        }
        GridView {
            id: grid
            visible: pick.entries.length > 0
            //  Breite folgt der Spaltenzahl (+ Platz für den Rollbalken).
            width: pick.columns * pick.cellW + 18
            height: Math.min(300, pick.rows * pick.cellH)
            cellWidth: pick.cellW
            cellHeight: pick.cellH
            clip: true
            model: pick.entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            // `flickable` MUSS über die id gesetzt werden, nicht über `parent`: SmoothWheelArea setzt selbst
            // `parent: flickable` - mit `flickable: parent` entsteht eine Bindungsschleife und nichts wirkt.
            SmoothWheelArea { flickable: grid }
            delegate: Rectangle {
                required property var modelData
                width: pick.cellW - 4
                height: pick.cellH - 4
                radius: 5
                color: fiHover.hovered ? App.themeCard : "transparent"
                border.color: fiHover.hovered ? App.themeAccent : App.themeBorder
                Image {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.topMargin: 4
                    width: 82; height: 58
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    sourceSize.width: 96
                    sourceSize.height: 96
                    source: parent.modelData.url
                }
                Text {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 3
                    x: 3
                    width: parent.width - 6
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    text: parent.modelData.name
                    color: App.themeTextPrimary
                    font.pixelSize: 10
                }
                HoverHandler { id: fiHover }
                TapHandler {
                    onTapped: {
                        const u = parent.modelData.url
                        pick.close()
                        pick.picked(u)
                    }
                }
            }
        }
        Rectangle {
            width: 150; height: 26; radius: 6
            color: brHover.hovered ? App.themeCard : "transparent"
            border.color: App.themeBorder
            Text {
                anchors.centerIn: parent
                text: App.uiText(App.language, "DocxImageBrowse")
                color: App.themeTextPrimary
                font.pixelSize: 12
            }
            HoverHandler { id: brHover }
            TapHandler {
                onTapped: { pick.close(); pick.browseRequested() }
            }
        }
    }
}
