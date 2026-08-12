pragma ComponentBehavior: Bound
import QtQuick
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  ThemedIcon.qml — eine SVG als Bedien-Symbol, eingefärbt auf die Theme-Farbe.
//
//  Regel 29 verbietet Emojis/Glyphen in der Oberfläche: sie sehen auf jedem
//  System anders aus, folgen nicht dem Theme und skalieren nicht mit dem Knopf.
//  Jedes Symbol ist deshalb eine einfarbige SVG unter `qml/icons/`.
//
//  EINGEFÄRBT WIRD IN C++ (`IconProvider`, `image://mgicon/…?c=#rrggbb`), NICHT
//  über `MultiEffect`: Qts SVG-Renderer kennt `currentColor` nicht, und der
//  Effekt-Weg ließ sich in keinem Prüflauf belegen — gemessen zeichnete er weder
//  offscreen noch in einer echten Grafiksitzung ein Pixel, während dieselbe
//  Datei als schlichtes `Image` sauber ankam. Der Provider braucht keinen
//  Shader und arbeitet deshalb auch unter Software-Rendering.
//
//  Nutzung in einem Knopf (Muster aller Leisten/Panels):
//      property url iconSource: ""          // "qrc:/qml/icons/pen.svg"
//      Text       { …; visible: String(btn.iconSource).length === 0 }
//      ThemedIcon { anchors.centerIn: parent; source: btn.iconSource
//                   visible: String(btn.iconSource).length > 0 }
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    //  Voller qrc-Pfad des Symbols, z. B. "qrc:/qml/icons/pen.svg".
    property url   source: ""
    property real  size: 16
    property color color: App.themeTextPrimary

    implicitWidth: root.size
    implicitHeight: root.size
    width: root.size
    height: root.size

    //  qrc:/qml/icons/pen.svg → qml/icons/pen.svg (der Provider hängt ":/" an).
    readonly property string _path: String(root.source).replace(/^qrc:\/?\/?/, "")

    Image {
        anchors.fill: parent
        //  Die Farbe steht IM Quell-String: ändert das Theme sie, lädt QML das
        //  Bild von selbst neu — ohne eigenes Signal.
        source: root._path.length > 0
                ? "image://mgicon/" + root._path + "?c=" + encodeURIComponent(root.color)
                : ""
        //  Rastern in der Zielgröße (der Provider zeichnet die SVG dafür neu).
        sourceSize: Qt.size(Math.round(root.size), Math.round(root.size))
        smooth: true
        cache: true
    }
}
