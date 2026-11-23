import QtQuick

// Ersetzt Qts feste 60 px je Rastung durch ~45 % Sichthöhe; bei ScrollView dessen `contentItem` angeben. MouseArea, weil
// ein interaktives Flickable Radereignisse vor jedem WheelHandler selbst verarbeitet.
MouseArea {
    id: wheelArea

    property Flickable flickable: null

    property real stepFactor: 0.45

    //  Horizontal gilt „Rad hoch = nach rechts", wie in Browsern und Editoren.
    property bool horizontal: false

    // Ist ein Modifikator gesetzt, laufen Radereignisse OHNE ihn unverändert nach unten durch.
    property int requiredModifier: Qt.NoModifier

    //  Deckende Flaeche, die mit dem Inhalt rollt: der Software-Zeichner meldet KWin dann ein Rechteck je Bild statt
    //  eines je bewegtem Element (Einstellungen: ~300, KWin verzoegerte die Bilder). Durchsichtig = keine.
    //  Neben dem Inhalt statt darin: ein zweites Kind im Inhalt nimmt `ScrollView` die eigene Inhaltshoehe.
    property color backdrop: "transparent"
    Loader {
        active: wheelArea.backdrop.a > 0 && wheelArea.flickable !== null
        parent: wheelArea.flickable ? wheelArea.flickable : wheelArea
        z: -1
        sourceComponent: Rectangle {
            readonly property Flickable fl: wheelArea.flickable
            x: -fl.contentX
            y: -fl.contentY
            width: Math.max(fl.contentWidth, fl.width)
            height: Math.max(fl.contentHeight, fl.height)
            color: wheelArea.backdrop
        }
    }

    //  Direktes Kind des Flickable, nicht von `contentItem`: bleibt beim Scrollen stehen.
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
        //  Vom laufenden Ziel aus, damit sich schnelle Rastungen addieren statt sich zu verwerfen.
        var cur  = wheelArea.horizontal ? fl.contentX : fl.contentY
        var base = scrollAnim.running ? scrollAnim.to : cur
        //  Rad hoch = Inhalt nach oben; horizontal nach RECHTS, das holt abgeschnittene Bedienelemente herein.
        var tgt  = wheelArea.horizontal
                   ? Math.max(0, Math.min(base + raw, maxPos))
                   : Math.max(0, Math.min(base - raw, maxPos))

        scrollAnim.from = cur
        scrollAnim.to   = tgt
        scrollAnim.restart()
        wheel.accepted = true
    }
}
