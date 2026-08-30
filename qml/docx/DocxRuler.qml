import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  DocxRuler.qml - EIN Randlineal des DOCX-Editors (waagerecht oder senkrecht).
//
//  Es stellt die echten SEITENRÄNDER des Dokuments ein (`w:sectPr/w:pgMar`),
//  nicht eine Anzeige-Zugabe: davon hängen Umbruch, Seitenzahl UND der
//  PDF-Export ab (der malt die Bildschirm-Auslegung). Word sieht dieselben
//  Ränder, weil sie im Dokument stehen.
//
//  EINE Datei für beide Richtungen: waagerecht und senkrecht unterscheiden sich
//  nur darin, welche Achse die Länge trägt - zwei Dateien hätten dieselbe
//  Rechnung zweimal geführt, und genau daraus entstehen später Abweichungen
//  zwischen oben und rechts.
//
//  Die Skala ist in ZENTIMETERN beschriftet (wie in Word und LibreOffice) und
//  in Millimetern geteilt; gezogen wird auf halbe Millimeter gerundet.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: ruler

    //  false = waagerecht (oben, stellt links/rechts) · true = senkrecht
    //  (rechts, stellt oben/unten).
    property bool vertical: false

    //  Die Seite in Millimetern und ihr Platz IN DIESEM Lineal (Pixel).
    //  `pageLen` ist die Seitenlänge in der Richtung des Lineals, `pageStart`
    //  der Anfang der Seite in Lineal-Koordinaten, `pagePx` ihre Länge in
    //  Pixeln - so muss das Lineal die Maßstabsrechnung der Textfläche nicht
    //  nachbauen (und kann nicht davon abweichen).
    property real pageLenMm: 210
    property real pageStart: 0
    property real pagePx: 0

    //  Die beiden Ränder in Millimetern: `startMm` = links bzw. oben,
    //  `endMm` = rechts bzw. unten.
    property real startMm: 25
    property real endMm: 25
    //  Darf der jeweilige Griff gezogen werden? Im einfachen Modus (Vorgabe)
    //  nur der, den der Nutzer wirklich braucht: rechts bzw. oben.
    property bool startEnabled: false
    property bool endEnabled: true

    //  Zurücksetzen-Knopf zeigen (nur, wenn es etwas zurückzusetzen gibt).
    property bool resettable: false

    //  Neue Werte in Millimetern - der Aufrufer schreibt sie ins Dokument.
    signal marginsRequested(real startMm, real endMm)
    signal resetRequested()
    //  Dieses Lineal wurde ANGEFASST - danach meint `Strg+Z` seine Kette
    //  (Festlegung des Nutzers: jedes Lineal hat seine eigene Historie, und
    //  die des Dokuments bleibt davon unberührt).
    signal grabbed()

    implicitHeight: 18
    implicitWidth: 18

    readonly property real _mmPx: (ruler.pagePx > 1 && ruler.pageLenMm > 1)
                                  ? ruler.pagePx / ruler.pageLenMm : 0

    //  Die Bildlaufstelle der Textfläche. Sie wird NUR während eines Zuges
    //  gebraucht (s. `_scaleStart`) - das Lineal zeichnet sonst aus `pageStart`.
    property real scrollY: 0

    //  ── Der Maßstab, gegen den gerechnet UND gezeichnet wird ────────────────
    //  Normalerweise `pageStart`. **Solange ein Griff gehalten wird, folgt er
    //  dem Bildlauf STETIG**, statt der Seite unter dem Blick zu folgen.
    //
    //  Warum überhaupt anders: `pageStart` des senkrechten Lineals ist die
    //  Seite, die man gerade VOR SICH hat - beim Scrollen springt dieser Wert
    //  um eine ganze Seite, sobald der Blick auf die nächste wechselt. Beide
    //  bisherigen Fassungen scheiterten daran:
    //   · gegen eine feste Szenenstelle rechnen -> der Griff lief dem Zeiger
    //     davon (gemessen 369 px);
    //   · den Maßstab ganz einfrieren -> der Griff blieb zwar am Zeiger, aber
    //     der Bildlauf zählte NICHT mit, und beim Loslassen sprang der Griff
    //     auf die Wahrheit zurück (beides vom Nutzer gemeldet).
    //  **Jetzt wandert der Maßstab genau um die gescrollte Strecke.** Damit
    //  zählt ein Bildlauf verhältnisgleich mit (`_mmUnder` rechnet dagegen),
    //  der Griff bleibt unter dem Zeiger, und der Seitensprung entfällt - der
    //  Bezug ist die Seite, auf der der Zug begann.
    //  **Warum das nötig ist und nicht Geschmack:** eine A4-Seite ist höher als
    //  das Fenster. Den UNTEREN Rand erreicht man ohne Scrollen gar nicht.
    property real _frozenStart: 0
    property real _frozenScrollY: 0
    property bool _frozen: false
    //  **Nur das SENKRECHTE Lineal fuehrt nach.** Das waagerechte haengt an
    //  `area.pageOffsetX` und hat mit dem Bildlauf nichts zu tun - liesse man
    //  seinen Massstab mitwandern, verstellte ein senkrechter Bildlauf den
    //  LINKEN/RECHTEN Rand.
    readonly property real _scaleStart:
        (ruler._frozen && ruler.vertical)
            ? ruler._frozenStart - (ruler.scrollY - ruler._frozenScrollY)
            : ruler.pageStart

    //  Der Schreibbereich in Lineal-Koordinaten.
    readonly property real _textFrom: ruler._scaleStart + ruler.startMm * ruler._mmPx
    readonly property real _textTo:   ruler._scaleStart + ruler.pagePx - ruler.endMm * ruler._mmPx

    //  Griff-Breite: großzügig genug, um ihn zu treffen, ohne die Skala zu
    //  verdecken.
    readonly property int _grip: 9

    //  Eine Szenenstelle in die Achse DIESES Lineals umrechnen. Der Zug muss in
    //  Lineal-Koordinaten rechnen, nicht in Szenen-Koordinaten: der Maßstab des
    //  senkrechten Lineals ist die Seite, die man gerade vor sich hat, und der
    //  wandert beim Scrollen (`pageStart`).
    function _alongOf(scenePos) {
        const p = ruler.mapFromItem(null, scenePos.x, scenePos.y)
        return ruler.vertical ? p.y : p.x
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.darker(App.themeToolbarBg, 1.05)
    }
    Rectangle {
        //  Trennlinie zum Dokument.
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: ruler.vertical ? parent.height : 1
        width:  ruler.vertical ? 1 : parent.width
        x: ruler.vertical ? 0 : 0
        y: ruler.vertical ? 0 : parent.height - 1
        color: App.themeBorder
    }

    //  ── Der Schreibbereich hebt sich vom Rand ab ────────────────────────────
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

    //  ── Skala: je Zentimeter ein langer Strich mit Zahl, dazwischen kurze ───
    //  Gezeichnet aus Rechtecken (Regel 28) und nur über der Seite - links und
    //  rechts daneben liegt kein Papier, dort gäbe es nichts zu messen.
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

    //  ── Ein Griff ───────────────────────────────────────────────────────────
    //  Zwei Instanzen: Anfang und Ende. Der Zug rechnet Pixel in Millimeter
    //  zurück und meldet BEIDE Werte - das Dokument kennt nur ganze Randsätze.
    //  Laenge des Lineals in seiner eigenen Richtung.
    readonly property real _len: ruler.vertical ? ruler.height : ruler.width

    component Grip: Item {
        id: g
        //  false = Anfang (links/oben) · true = Ende (rechts/unten)
        property bool atEnd: false
        property bool active: true
        //  Wo der Griff RECHNERISCH sitzt …
        readonly property real rawPos: g.atEnd ? ruler._textTo : ruler._textFrom
        //  … und wo er zu SEHEN ist. Eine A4-Seite ist hoeher als das Fenster;
        //  der untere Rand laege dann weit unterhalb des Lineals und waere gar
        //  nicht zu fassen (vom Nutzer gemeldet: „den unteren Teil kann ich
        //  nicht einstellen"). Ein Griff ausserhalb wird deshalb am Rand
        //  GEPARKT - er bleibt greifbar, und der Zug rechnet ohnehin relativ.
        readonly property real posPx: Math.max(4, Math.min(g.rawPos, ruler._len - 4))
        readonly property bool parked: Math.abs(g.posPx - g.rawPos) > 0.5

        width:  ruler.vertical ? ruler.width : ruler._grip
        height: ruler.vertical ? ruler._grip : ruler.height
        x: ruler.vertical ? 0 : Math.round(g.posPx - ruler._grip / 2)
        y: ruler.vertical ? Math.round(g.posPx - ruler._grip / 2) : 0
        visible: ruler._mmPx > 0

        //  Der Griff selbst: ein Balken quer zur Leserichtung. Kein Symbol -
        //  ein Rand IST eine Linie.
        Rectangle {
            anchors.centerIn: parent
            width:  ruler.vertical ? Math.max(10, ruler.width - (g.parked ? 12 : 6)) : 3
            height: ruler.vertical ? 3 : Math.max(10, ruler.height - (g.parked ? 12 : 6))
            radius: 1.5
            color: !g.active ? App.themeBorder
                             : (gDrag.active || gHover.hovered) ? App.themeAccent
                                                                : App.themeTextMuted
        }

        //  Ausgangswerte des laufenden Zuges (s. `onActiveChanged`).
        //  `_grabOffsetMm` ist der Abstand zwischen dem Wert UNTER dem Zeiger
        //  und dem tatsächlichen Rand im Moment des Anfassens. Damit springt
        //  der Rand beim Anfassen nicht auf die Zeigerstelle, und ein am Rand
        //  GEPARKTER Griff bleibt greifbar - beides war der Grund für die
        //  frühere Rechnung „Weg seit dem Druckpunkt".
        property real _grabOffsetMm: 0
        //  Welchen Rand-Wert markiert die Stelle `along` des Lineals gerade?
        function _mmUnder(along) {
            if (ruler._mmPx <= 0) return 0
            return g.atEnd ? (ruler._scaleStart + ruler.pagePx - along) / ruler._mmPx
                           : (along - ruler._scaleStart) / ruler._mmPx
        }

        function _rebase(along) {
            g._grabOffsetMm = (g.atEnd ? ruler.endMm : ruler.startMm) - g._mmUnder(along)
        }

        //  Den Rand auf die aktuelle Zeigerstelle nachführen. Gerechnet wird in
        //  LINEAL-Koordinaten gegen `_scaleStart`, der dem Bildlauf folgt -
        //  damit bleibt der Griff unter dem Zeiger UND ein Bildlauf zählt mit.
        //  Läuft aus ZWEI Quellen: aus der Mausbewegung und aus `_scaleStart` -
        //  beim Scrollen mit stehender Maus meldet der Zeiger nichts.
        function _apply() {
            if (!gDrag.active || ruler._mmPx <= 0) return
            const along = ruler._alongOf(gDrag.centroid.scenePosition)
            //  Auf halbe Millimeter gerundet: feiner ist am Bildschirm nicht zu
            //  treffen, und ein krummer Twip-Wert sieht in Word nach Zufall aus.
            let mm = Math.round((g._mmUnder(along) + g._grabOffsetMm) * 2) / 2
            //  Mindestens 10 mm Schreibbereich stehen lassen - das Dokument
            //  klemmt selbst noch einmal, aber ein Griff, der sich über den
            //  anderen schieben lässt, fühlt sich kaputt an.
            const other = g.atEnd ? ruler.startMm : ruler.endMm
            mm = Math.max(0, Math.min(mm, ruler.pageLenMm - other - 10))
            //  Nur melden, wenn sich WIRKLICH etwas ändert: jede Meldung legt
            //  das ganze Dokument neu aus (gemessen 73 ms bei 4000 Absätzen),
            //  und `_apply` läuft jetzt auch aus dem Bildlauf - eine Meldung
            //  mit unverändertem Wert wäre reine Arbeit für nichts und könnte
            //  über das Nachziehen der Ansicht zurückkoppeln.
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
            //  BEIDE Achsen freigeben, obwohl nur eine gemeint ist: ein
            //  `xAxis.enabled: false` verhindert die AKTIVIERUNG des Handlers
            //  ganz - der senkrechte Griff bekam dann zwar jede Bewegung
            //  gemeldet, `active` blieb aber falsch, und ein Zug am senkrechten
            //  Lineal war wirkungslos (am Prüfstand `bench_docxruler`
            //  gemessen: waagerecht `active=true` ab dem zweiten Schritt,
            //  senkrecht nie). Da `target: null` ist, bewegt der Handler
            //  ohnehin nichts; ausgewertet wird unten NUR die Achse des
            //  Lineals, ein Zug quer dazu bleibt also folgenlos.
            //  Der Zug rechnet in LINEAL-Koordinaten gegen den aktuellen
            //  Maßstab, mit einem beim Anfassen genommenen Versatz (s.
            //  `_grabOffsetMm`) - nicht als Weg gegen eine feste SZENEN-Stelle.
            //  Der Versatz hält beides zusammen: der Rand springt beim Anfassen
            //  nicht auf den Zeiger, ein geparkter Griff bleibt greifbar, UND
            //  der Griff bleibt unter dem Zeiger, wenn der Maßstab beim Scrollen
            //  wandert.
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

    //  ── Zurücksetzen ────────────────────────────────────────────────────────
    //  Am ANFANG des Lineals (dort, wo kein Papier liegt) - es verdeckt damit
    //  nie die Skala.
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

    //  Auch ein blosser Klick auf das Lineal zählt als „angefasst" - sonst
    //  müsste man erst ziehen, bevor `Strg+Z` das richtige meint.
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
