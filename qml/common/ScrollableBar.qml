import QtQuick
import QtQuick.Controls

// Waagerechte Leiste, die bei schmalem Fenster BLÄTTERBAR wird statt ihre Knöpfe abzuschneiden -
// zuvor lagen Menü- und Filterleiste übereinander und die rechten Knöpfe waren unerreichbar.
// Jedes Mausrad schwenkt sie, mit und ohne Strg (derselbe Griff wie im DOCX-Editor).
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

    // `requiredModifier: Qt.NoModifier` heißt "jeder Modifikator ist recht" - durchgereicht wird nur, wenn einer
    // VERLANGT und nicht gedrückt ist. Wer nur mit Strg schwenken will, setzt `wheelModifier`.
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

    // Die Reihe ist so hoch wie ihr größtes Kind und sitzt mittig: so stehen zentrierte wie nicht zentrierte
    // Kinder richtig. In voller Leistenhöhe würde die zweite Sorte nach oben gedrückt (PDF-Werkzeugleiste).
    Row {
        id: inner
        y: Math.max(0, (bar.height - height) / 2)
        spacing: 6
    }
}
