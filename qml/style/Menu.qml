import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import MediaGallery 1.0
import "../common"

// Gethemtes Menü-Popup als STIL-Datei: `ThemedMenu.qml` greift nur bei Menüs der App, die von QT geöffneten blieben eckig.
// `padding` haelt die Rundung frei; `PopupPanel` bringt die implizite Breite mit, sonst kollabiert das Popup.
T.Menu {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    margins: 0
    padding: 6
    overlap: 1

    delegate: MenuItem { }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        //  Nur scrollbar, wenn das Menü höher als der Bildschirm wäre - sonst schluckt das ListView Rad-Ereignisse.
        interactive: Window.window ? contentHeight + control.topPadding
                                     + control.bottomPadding > Window.window.height
                                   : false
        clip: true
        currentIndex: control.currentIndex
        keyNavigationEnabled: true
        keyNavigationWraps: true
        // Ohne `ScrollIndicator`: der Typ braucht `QtQuick.Controls`, das eine Stil-Datei nicht importieren darf (Ringschluss).
    }

    background: PopupPanel { }
}
