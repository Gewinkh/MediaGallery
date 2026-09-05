import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// Ein Randlineal des DOCX-Editors. Es stellt die echten Seitenränder ein (`w:sectPr/w:pgMar`), nicht
// eine Anzeige-Zugabe - daran hängen Umbruch, Seitenzahl und PDF-Export. EINE Datei für beide
// Richtungen: getrennt geführt driften waagerecht und senkrecht später auseinander.
Item {
    id: ruler

    property bool vertical: false

    property real pageLenMm: 210
    property real pageStart: 0
    property real pagePx: 0

    property real startMm: 25
    property real endMm: 25
    property bool startEnabled: false
    property bool endEnabled: true

    property bool resettable: false

    signal marginsRequested(real startMm, real endMm)
    signal resetRequested()
    signal grabbed()

    implicitHeight: 18
    implicitWidth: 18

    readonly property real _mmPx: (ruler.pagePx > 1 && ruler.pageLenMm > 1)
                                  ? ruler.pagePx / ruler.pageLenMm : 0

    property real scrollY: 0

    // Maßstab des Zugs: `pageStart` springt beim Scrollen um eine ganze Seite, sobald der Blick auf die
    // nächste wechselt. Gegen eine feste Szenenstelle gerechnet lief der Griff dem Zeiger davon (369 px);
    // ganz eingefroren zählte der Bildlauf nicht mit. Jetzt wandert er um genau die gescrollte Strecke.
    property real _frozenStart: 0
    property real _frozenScrollY: 0
    property bool _frozen: false
    readonly property real _scaleStart:
        (ruler._frozen && ruler.vertical)
            ? ruler._frozenStart - (ruler.scrollY - ruler._frozenScrollY)
            : ruler.pageStart

    readonly property real _textFrom: ruler._scaleStart + ruler.startMm * ruler._mmPx
    readonly property real _textTo:   ruler._scaleStart + ruler.pagePx - ruler.endMm * ruler._mmPx

    readonly property int _grip: 9

    function _alongOf(scenePos) {
        const p = ruler.mapFromItem(null, scenePos.x, scenePos.y)
        return ruler.vertical ? p.y : p.x
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.darker(App.themeToolbarBg, 1.05)
    }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: ruler.vertical ? parent.height : 1
        width:  ruler.vertical ? 1 : parent.width
        x: ruler.vertical ? 0 : 0
        y: ruler.vertical ? 0 : parent.height - 1
        color: App.themeBorder
    }

    Rectangle {
        visible: ruler._mmPx > 0
        x: ruler.vertical ? 2 : ruler._textFrom
        y: ruler.vertical ? ruler._textFrom : 2
        width:  ruler.vertical ? Math.max(0, ruler.width - 4)
                               : Math.max(0, ruler._textTo - ruler._textFrom)
        height: ruler.vertical ? Math.max(0, ruler._textTo - ruler._textFrom)
                               : Math.max(0, ruler.height - 5)
        color: App.themeCard
        border.color: App.themeBorder
        border.width: 1
        radius: 2
    }

    Repeater {
        model: ruler._mmPx > 0 ? Math.floor(ruler.pageLenMm / 5) + 1 : 0
        delegate: Rectangle {
            required property int index
            readonly property real mm: index * 5
            readonly property bool isCm: (index % 2) === 0
            visible: mm <= ruler.pageLenMm
            color: App.themeTextMuted
            opacity: isCm ? 0.75 : 0.45
            width:  ruler.vertical ? (isCm ? 6 : 3) : 1
            height: ruler.vertical ? 1 : (isCm ? 6 : 3)
            x: ruler.vertical ? ruler.width - width - 3
                              : Math.round(ruler._scaleStart + mm * ruler._mmPx)
            y: ruler.vertical ? Math.round(ruler._scaleStart + mm * ruler._mmPx)
                              : ruler.height - height - 4
        }
    }

    readonly property real _len: ruler.vertical ? ruler.height : ruler.width

    component Grip: Item {
        id: g
        property bool atEnd: false
        property bool active: true
        readonly property real rawPos: g.atEnd ? ruler._textTo : ruler._textFrom
        // Ein Griff außerhalb der Sicht wird am Rand GEPARKT: eine A4-Seite ist höher als das Fenster, der untere
        // Rand wäre sonst gar nicht zu fassen. Der Zug rechnet ohnehin relativ.
        readonly property real posPx: Math.max(4, Math.min(g.rawPos, ruler._len - 4))
        readonly property bool parked: Math.abs(g.posPx - g.rawPos) > 0.5

        width:  ruler.vertical ? ruler.width : ruler._grip
        height: ruler.vertical ? ruler._grip : ruler.height
        x: ruler.vertical ? 0 : Math.round(g.posPx - ruler._grip / 2)
        y: ruler.vertical ? Math.round(g.posPx - ruler._grip / 2) : 0
        visible: ruler._mmPx > 0

        Rectangle {
            anchors.centerIn: parent
            width:  ruler.vertical ? Math.max(10, ruler.width - (g.parked ? 12 : 6)) : 3
            height: ruler.vertical ? 3 : Math.max(10, ruler.height - (g.parked ? 12 : 6))
            radius: 1.5
            color: !g.active ? App.themeBorder
                             : (gDrag.active || gHover.hovered) ? App.themeAccent
                                                                : App.themeTextMuted
        }

        property real _grabOffsetMm: 0
        function _mmUnder(along) {
            if (ruler._mmPx <= 0) return 0
            return g.atEnd ? (ruler._scaleStart + ruler.pagePx - along) / ruler._mmPx
                           : (along - ruler._scaleStart) / ruler._mmPx
        }

        function _rebase(along) {
            g._grabOffsetMm = (g.atEnd ? ruler.endMm : ruler.startMm) - g._mmUnder(along)
        }

        function _apply() {
            if (!gDrag.active || ruler._mmPx <= 0) return
            const along = ruler._alongOf(gDrag.centroid.scenePosition)
            let mm = Math.round((g._mmUnder(along) + g._grabOffsetMm) * 2) / 2
            const other = g.atEnd ? ruler.startMm : ruler.endMm
            mm = Math.max(0, Math.min(mm, ruler.pageLenMm - other - 10))
            // Nur melden, wenn sich wirklich etwas ändert: jede Meldung legt das Dokument neu aus (73 ms bei 4000
            // Absätzen), und `_apply` läuft auch aus dem Bildlauf - sonst koppelte es über das Nachziehen zurück.
            if (g.atEnd) { if (mm !== ruler.endMm)   ruler.marginsRequested(ruler.startMm, mm) }
            else         { if (mm !== ruler.startMm) ruler.marginsRequested(mm, ruler.endMm) }
        }


        HoverHandler {
            id: gHover
            enabled: g.active
            cursorShape: ruler.vertical ? Qt.SizeVerCursor : Qt.SizeHorCursor
        }

        DragHandler {
            id: gDrag
            enabled: g.active
            target: null
            // BEIDE Achsen freigeben, obwohl nur eine gemeint ist: mit `xAxis.enabled: false` wird der Handler nie
            // `active`, ein Zug am senkrechten Lineal blieb wirkungslos (`bench_docxruler`). `target: null`,
            // ausgewertet wird nur die eigene Achse - gegen den aktuellen Maßstab, mit dem Versatz vom Anfassen.
            onActiveChanged: {
                if (!gDrag.active) {
                    ruler._frozen = false      // Maßstab folgt wieder der Seite
                    return
                }
                ruler.grabbed()
                //  Maßstab einfrieren, BEVOR gerechnet wird - sonst nähme der
                //  Versatz schon den wandernden Wert auf.
                ruler._frozenStart   = ruler.pageStart
                ruler._frozenScrollY = ruler.scrollY
                ruler._frozen = true
                g._rebase(ruler._alongOf(gDrag.centroid.scenePosition))
            }
            onCentroidChanged: g._apply()
        }

        //  Der Maßstab wandert beim Scrollen - dann muss der Rand nachgeführt
        //  werden, auch wenn die Maus stillsteht. `onCentroidChanged` feuert
        //  dabei nicht (die Szenenstelle des Zeigers ändert sich ja nicht).
        Connections {
            target: ruler
            enabled: gDrag.active
            function on_ScaleStartChanged() { g._apply() }
        }

        ToolTip.visible: gDrag.active
        ToolTip.text: (g.atEnd ? ruler.endMm : ruler.startMm).toFixed(1) + " mm"
    }

    Grip { atEnd: false; active: ruler.startEnabled }
    Grip { atEnd: true;  active: ruler.endEnabled }

    Rectangle {
        id: resetBtn
        visible: ruler.resettable
        width: 16; height: 16; radius: 3
        x: ruler.vertical ? Math.round((ruler.width - width) / 2) : 2
        y: ruler.vertical ? 2 : Math.round((ruler.height - height) / 2)
        color: resetHover.hovered ? App.themeAccent : "transparent"
        border.color: App.themeBorder
        border.width: 1
        DrawnIcon {
            anchors.centerIn: parent
            name: "undo"
            size: 11
            color: resetHover.hovered ? "#ffffff" : App.themeTextMuted
        }
        HoverHandler { id: resetHover }
        TapHandler { onTapped: ruler.resetRequested() }
        ToolTip.visible: resetHover.hovered
        ToolTip.delay: 400
        ToolTip.text: App.uiText(App.language, "DocxRulerResetTip")
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onPressedChanged: if (pressed) ruler.grabbed()
    }

    HoverHandler { id: rulerHover }
    ToolTip.visible: rulerHover.hovered && !resetHover.hovered
    ToolTip.delay: 800
    ToolTip.text: App.uiText(App.language,
                             ruler.vertical ? "DocxRulerSideTip" : "DocxRulerTopTip")
}
