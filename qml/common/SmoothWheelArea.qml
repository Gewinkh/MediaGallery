import QtQuick

// ─────────────────────────────────────────────────────────────────────────────
//  SmoothWheelArea.qml — weiches, schnelles Mausrad-Scrollen für ein `Flickable`
//  (z. B. das `contentItem` eines `ScrollView`).
//
//  WARUM: Qts `Flickable` scrollt je Rastung fest
//  `QStyleHints::wheelScrollLines() * 20 px` — gemessen **60 px**, unabhängig von
//  der Sichthöhe. Bei den langen Einstellungsseiten (Allgemein: ~1835 px Inhalt
//  auf ~448 px Sichtfläche) sind das über 20 Rastungen bis zum Ende; das Scrollen
//  wirkt zäh. Galerie, PDF-Seitenraster und PDF-Liste ersetzen dieses Verhalten
//  längst durch „~45 % Sichthöhe je Rastung + kurze Animation" — diese Komponente
//  bündelt genau dieses etablierte Muster zum Wiederverwenden.
//
//  WIE: Eine `MouseArea` mit `acceptedButtons: Qt.NoButton` über der Sichtfläche
//  fängt NUR Radereignisse ab; Klicks, Doppelklicks, Hover und Ziehen laufen
//  ungehindert an die Controls darunter. (Ein `WheelHandler` genügt nicht — ein
//  interaktives `Flickable` verarbeitet Radereignisse vorher selbst, s.
//  „Bekannte Workarounds" in Structure.md.)
//
//  NUTZUNG — irgendwo im selben Dokument deklarieren, die Komponente hängt sich
//  selbst als DIREKTES Kind des Flickable ein (nicht in dessen `contentItem`, sie
//  scrollt also nicht mit) und liegt über dem Inhalt:
//      ScrollView { id: sv; anchors.fill: parent }
//      SmoothWheelArea { flickable: sv.contentItem }
// ─────────────────────────────────────────────────────────────────────────────
MouseArea {
    id: wheelArea

    //  Das zu scrollende Flickable (bei ScrollView: dessen `contentItem`).
    property Flickable flickable: null

    //  Anteil der Sichthöhe je Rastung (Galerie-Muster).
    property real stepFactor: 0.45

    //  Achse: false = vertikal (`contentY`), true = HORIZONTAL (`contentX`).
    //  Horizontal gilt „Rad hoch = nach rechts" — dieselbe Richtung, die auch
    //  Browser und Editoren für seitliches Scrollen benutzen.
    property bool horizontal: false

    //  Erforderlicher Modifikator (`Qt.NoModifier` = keiner). Ist einer gesetzt,
    //  laufen Radereignisse OHNE ihn UNVERÄNDERT weiter nach unten durch — die
    //  bestehende Bedeutung des Rades an dieser Stelle bleibt also erhalten
    //  (im PDF-Editor z. B. Strg+Rad = Ribbon seitlich, Rad allein = Seiten).
    property int requiredModifier: Qt.NoModifier

    //  Direktes Kind des Flickable (NICHT dessen `contentItem`) → bleibt beim
    //  Scrollen an Ort und Stelle und deckt genau die Sichtfläche ab.
    parent: flickable
    anchors.fill: parent
    acceptedButtons: Qt.NoButton
    z: 2

    NumberAnimation {
        id: scrollAnim
        target: wheelArea.flickable
        property: wheelArea.horizontal ? "contentX" : "contentY"
        duration: 180
        easing.type: Easing.OutCubic
    }

    onWheel: function(wheel) {
        var fl = wheelArea.flickable
        if (!fl) { wheel.accepted = false; return }

        //  Modifikator verlangt, aber nicht gedrückt → Ereignis unangetastet
        //  weiterreichen (accepted=false propagiert es an die Items darunter).
        if (wheelArea.requiredModifier !== Qt.NoModifier
            && !(wheel.modifiers & wheelArea.requiredModifier)) {
            wheel.accepted = false
            return
        }

        var viewport = wheelArea.horizontal ? fl.width : fl.height
        var content  = wheelArea.horizontal ? fl.contentWidth : fl.contentHeight
        var maxPos   = Math.max(0, content - viewport)
        if (maxPos <= 0) { wheel.accepted = true; return }

        var raw = (wheel.angleDelta.y !== 0)
                  ? (wheel.angleDelta.y / 120) * (viewport * wheelArea.stepFactor)
                  : wheel.pixelDelta.y * 1.6
        //  Beim schnellen Weiterdrehen vom bereits laufenden Ziel aus rechnen,
        //  damit sich die Rastungen addieren statt sich gegenseitig zu verwerfen.
        var cur  = wheelArea.horizontal ? fl.contentX : fl.contentY
        var base = scrollAnim.running ? scrollAnim.to : cur
        //  Vertikal: Rad hoch = Inhalt nach oben (contentY kleiner).
        //  Horizontal: Rad hoch = nach RECHTS (contentX größer) — so werden die
        //  rechts abgeschnittenen Bedienelemente hereingeholt.
        var tgt  = wheelArea.horizontal
                   ? Math.max(0, Math.min(base + raw, maxPos))
                   : Math.max(0, Math.min(base - raw, maxPos))

        scrollAnim.from = cur
        scrollAnim.to   = tgt
        scrollAnim.restart()
        wheel.accepted = true
    }
}
