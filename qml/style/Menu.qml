import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import MediaGallery 1.0

// Gethemtes Menü-Popup als STIL-Datei: `common/ThemedMenu.qml` greift nur bei Menüs, die die App selbst
// instanziiert - die von QT geöffneten (Bearbeiten-Kontextmenü einer TextArea) blieben eckig in Fusions
// Grundfarbe. `topPadding`/`bottomPadding` halten die Rundung frei, die Markierung füllte sie sonst aus.
T.Menu {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    margins: 0
    topPadding: 6
    bottomPadding: 6
    overlap: 1

    delegate: MenuItem { }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        //  Nur scrollbar, wenn das Menü höher als der Bildschirm wäre (wie die
        //  eingebauten Stile) - sonst schluckt das ListView Rad-Ereignisse.
        interactive: Window.window ? contentHeight + control.topPadding
                                     + control.bottomPadding > Window.window.height
                                   : false
        clip: true
        currentIndex: control.currentIndex
        keyNavigationEnabled: true
        keyNavigationWraps: true
        // BEWUSST ohne `ScrollIndicator`: eine Stil-Datei darf `QtQuick.Controls` nicht importieren (Ringschluss), und
        // ohne den Import ist der Typ unbekannt - das Menü ließe sich gar nicht mehr erzeugen.
    }

    background: Rectangle {
        //  Mindestbreite wie bei `MenuItem`: ohne implizite Breite kollabiert
        //  ein Popup mit eigenem Hintergrund zum senkrechten „Strich".
        implicitWidth: 200
        color: App.themeMenuBarBg
        border.color: App.themeBorder
        border.width: 1
        radius: 6
    }
}
