import QtQuick
import QtQuick.Controls

// ─────────────────────────────────────────────────────────────────────────────
//  ScrollableBar.qml - eine waagerechte Leiste, die bei zu schmalem Fenster
//  BLÄTTERBAR wird statt ihre Knöpfe abzuschneiden.
//
//  Anlass (Nutzerbefund): in einer schmalen Hälfte lagen Menü- und Filterleiste
//  teils übereinander, und die rechten Knöpfe waren gar nicht mehr erreichbar.
//
//  Bedienung wie im DOCX-Editor: **jedes Mausrad** schwenkt die Leiste (mit und
//  ohne Strg), dazu eine dünne Bildlaufleiste als sichtbarer Hinweis. Wer Strg
//  aus dem PDF-Editor kennt, findet es hier genauso - beides führt zum Ziel.
//
//  NUTZUNG - Kinder werden in eine `Row` gelegt:
//      ScrollableBar { anchors.fill: parent; spacing: 6
//          Button { … }  Button { … }
//      }
// ─────────────────────────────────────────────────────────────────────────────
Flickable {
    id: bar

    default property alias content: inner.data
    property alias spacing: inner.spacing
    property alias leftPadding: inner.x
    //  Passt alles hinein? Dann verhält sich die Leiste wie ein gewöhnlicher Row.
    readonly property bool overflowing: contentWidth > width + 0.5

    contentWidth: inner.x + inner.implicitWidth
    contentHeight: height
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.HorizontalFlick
    //  Nur greifen, wenn es etwas zu blättern gibt - sonst schluckte die Leiste
    //  Züge, die der Fläche darunter gehören.
    interactive: bar.overflowing

    //  Eine MouseArea ist zwingend, ein WheelHandler genügt NICHT: ein
    //  interaktives Flickable verarbeitet Radereignisse vorher selbst
    //  (s. „Bekannte Workarounds" in Structure.md).
    //  `requiredModifier: Qt.NoModifier` heißt „jeder Modifikator ist recht" -
    //  die Komponente reicht nur durch, wenn einer VERLANGT und nicht gedrückt
    //  ist (s. `SmoothWheelArea`). Wer nur mit Strg schwenken will, setzt
    //  `wheelModifier: Qt.ControlModifier`.
    property int wheelModifier: Qt.NoModifier
    SmoothWheelArea {
        flickable: bar
        horizontal: true
        requiredModifier: bar.wheelModifier
    }

    ScrollBar.horizontal: ScrollBar {
        policy: bar.overflowing ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        height: 3
        opacity: 0.6
    }

    //  Die Reihe ist so hoch wie ihr GRÖSSTES Kind und sitzt mittig in der
    //  Leiste. Damit stehen beide Sorten Kinder richtig: solche mit
    //  `anchors.verticalCenter: parent.verticalCenter` (sie zentrieren sich in
    //  der Reihe) und solche ohne (sie sitzen an deren Oberkante - und die ist
    //  bereits mittig). Eine Reihe in voller Leistenhöhe hätte die zweite Sorte
    //  nach oben gedrückt (PDF-Werkzeugleiste).
    Row {
        id: inner
        y: Math.max(0, (bar.height - height) / 2)
        spacing: 6
    }
}
