import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  Menu.qml — gethemtes Menü-Popup (Stil "style").
//
//  WARUM DAS HIERHER GEHÖRT: Der Stil brachte bisher nur `MenuItem.qml` mit —
//  den RAHMEN eines Menüs malte also weiterhin Fusion: eckig und in der
//  Fusion-Grundfarbe. Solange die App ihre Menüs selbst instanziiert, ließ sich
//  das mit einem eigenen `background` überschreiben (`common/ThemedMenu.qml`).
//  Menüs, die QT SELBST aufmacht, erreicht man so aber nicht — allen voran das
//  Bearbeiten-Kontextmenü einer `TextArea` (Text-, HTML-, Quelltext-Ansicht).
//  Genau die blieben eckig (Nutzerbefund). Als Stil-Datei gilt die Fassung für
//  JEDES Menü der Anwendung, auch für die, die wir nie anfassen.
//
//  `topPadding`/`bottomPadding` halten die Rundung frei: die Markierung eines
//  Eintrags ist ein Rechteck und füllte die Ecke sonst wieder aus.
// ─────────────────────────────────────────────────────────────────────────────
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
        //  eingebauten Stile) — sonst schluckt das ListView Rad-Ereignisse.
        interactive: Window.window ? contentHeight + control.topPadding
                                     + control.bottomPadding > Window.window.height
                                   : false
        clip: true
        currentIndex: control.currentIndex
        keyNavigationEnabled: true
        keyNavigationWraps: true
        //  BEWUSST OHNE `ScrollIndicator`: eine Stil-Datei darf
        //  `QtQuick.Controls` nicht importieren (Ringschluss), und ohne diesen
        //  Import ist der Typ nicht bekannt — das Menü ließe sich gar nicht mehr
        //  erzeugen („Type Menu unavailable", am Prüfstand gemessen).
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
