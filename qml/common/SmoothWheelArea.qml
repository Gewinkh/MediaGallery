import QtQuick

// Qt scrollt ein Flickable fest mit wheelScrollLines * 20 px (gemessen 60), unabhängig von der Sichthöhe - auf
// den langen Einstellungsseiten über 20 Rastungen bis zum Ende. Ersetzt das durch ~45 % Sichthöhe je Rastung;
// MouseArea, weil ein interaktives Flickable Radereignisse vor jedem WheelHandler selbst verarbeitet.
MouseArea {
    id: wheelArea

    //  Das zu scrollende Flickable (bei ScrollView: dessen `contentItem`).
    property Flickable flickable: null

    //  Anteil der Sichthöhe je Rastung (Galerie-Muster).
    property real stepFactor: 0.45

    //  Achse: false = vertikal (`contentY`), true = HORIZONTAL (`contentX`).
    //  Horizontal gilt „Rad hoch = nach rechts" - dieselbe Richtung, die auch
    //  Browser und Editoren für seitliches Scrollen benutzen.
    property bool horizontal: false

    // Ist ein Modifikator gesetzt, laufen Radereignisse OHNE ihn unverändert nach unten durch - die bestehende
    // Bedeutung des Rades an dieser Stelle bleibt also erhalten.
    property int requiredModifier: Qt.NoModifier

    //  Direktes Kind des Flickable (NICHT dessen `contentItem`) -> bleibt beim
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

        //  Modifikator verlangt, aber nicht gedrückt -> Ereignis unangetastet
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
        //  Horizontal: Rad hoch = nach RECHTS (contentX größer) - so werden die
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
