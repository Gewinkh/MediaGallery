pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import QtQuick.Window
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  DrawnIcon.qml - jedes Bedien-Symbol als GEZEICHNETE Form (Regel 28).
//
//  Ersetzt den früheren Weg „SVG-Datei + IconProvider + ThemedIcon": keine
//  Textur, kein Bild-Cache, kein Rastern je Farbe/Größe - die Formen hängen als
//  Rechtecke (und wo nötig als `Shape`) im Szenengraph und werden vom Renderer
//  gebatcht. Die Farbe ist eine gewöhnliche Bindung: ein Themenwechsel färbt um,
//  ohne dass irgendetwas neu geladen wird.
//
//  RASTER: Alle Formen sind im 24×24-Raster beschrieben (dasselbe, in dem die
//  früheren Symbole gezeichnet waren) und werden über `scale` auf `size`
//  gebracht - die Zahlen unten sind damit direkt lesbar und bleiben bei jeder
//  Größe stimmig. Strichstärke 2, runde Enden.
//
//  TABELLE statt 36 Dateien: `_table` beschreibt jedes Symbol aus vier Sorten
//  von Grundformen, die vier `Repeater` unten zeichnen:
//      lines  [x1,y1,x2,y2,(w)]      Strich mit runden Enden (Rechteck + Drehung)
//      boxes  [x,y,w,h,(r)]          Rahmen (Rechteck mit Rand)
//      rings  [cx,cy,r]              Kreis-Rahmen
//      fills  [x,y,w,h,(r),(alpha)]  gefüllte Fläche
//  Neun Symbole brauchen zusätzlich eine echte KURVE oder ein Vieleck, das aus
//  Rechtecken nicht entsteht (Ellipse, Auge, U, Undo/Redo-Bogen, Unterschrift,
//  Stift, Diskette, Zeiger) - nur die tragen ein `Shape`, s. `_curve`.
//
//  Nutzung (Muster aller Leisten/Panels):
//      property string iconName: ""       // "pen"
//      Text      { …; visible: btn.iconName.length === 0 }
//      DrawnIcon { anchors.centerIn: parent; name: btn.iconName
//                  visible: btn.iconName.length > 0 }
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    //  Name des Symbols, z. B. "pen". Unbekannt/leer ⇒ es wird nichts gezeichnet.
    property string name: ""
    property real   size: 16
    property color  color: App.themeTextPrimary

    implicitWidth: root.size
    implicitHeight: root.size
    width: root.size
    height: root.size

    readonly property var _def: root._table[root.name] !== undefined
                                ? root._table[root.name] : ({})

    //  Ein Rasterschritt in Zielpixeln. Die Rechtecke werden damit GLEICH in der
    //  Endgröße aufgebaut statt über `scale` vergrößert: Qt legt seinen
    //  Antialiasing-Saum in ITEM-Koordinaten an, ein skaliertes Rechteck bekommt
    //  ihn deshalb mitvergrößert und sieht klotzig aus (am Prüfstand bei 110 px
    //  deutlich sichtbar). `Shape` ist davon nicht betroffen - dessen Kurven
    //  werden im Fragment aufgelöst, es darf skaliert werden.
    readonly property real _u: root.size / 24

    //  Einrasten geschieht auf GERÄTEpixel, nicht auf logische. Bei 125/150/200 %
    //  Skalierung ist ein logisches Pixel kein Rasterschritt mehr - auf ganze
    //  logische Pixel gerundet läge die Kante wieder mitten in einem Gerätepixel
    //  und das Symbol sähe grau und unruhig aus, genau wie ganz ohne Einrasten.
    readonly property real _dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    //  Auf das Gerätepixel-Raster gerundeter Wert (in logischen Pixeln).
    function _q(v) { return Math.round(v * root._dpr) / root._dpr }
    //  Kleinste sinnvolle Strichstärke: EIN Gerätepixel.
    function _qw(v) { return Math.max(1 / root._dpr, root._q(v)) }

    readonly property var _lines: root._def.lines !== undefined ? root._def.lines : []
    //  Achsenparallel ⇒ eingerastetes Rechteck (scharf). Schräg ⇒ `Shape` mit
    //  Kurven-Renderer: ein GEDREHTES Rechteck rastert seine Kanten nicht
    //  spiegelbildlich, weshalb die beiden Balken des „close"-X sichtbar
    //  unterschiedlich schwer wirkten.
    readonly property var _axisLines: root._lines.filter(function (l) {
        return l[0] === l[2] || l[1] === l[3]
    })
    readonly property var _diagLines: root._lines.filter(function (l) {
        return l[0] !== l[2] && l[1] !== l[3]
    })

    Item {
        anchors.fill: parent

        //  Striche mit runden Enden: ein Rechteck der Länge len + w (die runden
        //  Kappen verlängern eine Strecke um w/2 je Ende).
        //  ACHSENPARALLELE Striche rasten auf ganze Pixel ein und werden NICHT
        //  gedreht: bei 16 px ist ein 2-Einheiten-Strich sonst 1,33 px breit,
        //  liegt also zwischen zwei Pixelreihen und wirkt grau und unruhig.
        //  Schräge bleiben ungerastert - dort wäre Einrasten sinnlos.
        Repeater {
            model: root._axisLines
            delegate: Rectangle {
                required property var modelData
                readonly property bool horiz: modelData[1] === modelData[3]
                readonly property real sw: (modelData.length > 4 ? modelData[4] : 2) * root._u
                readonly property real len: Math.hypot(modelData[2] - modelData[0],
                                                       modelData[3] - modelData[1]) * root._u
                //  Eingerastete Strichstärke, mindestens ein Pixel.
                readonly property real swS: root._qw(sw)
                //  IMMER von der MITTE aus einrasten, nie von der Kante: sonst
                //  landen zwei Formen, die dieselbe Mittellinie teilen (Schaft
                //  und Balken des „caret"), einen halben Pixel auseinander -
                //  die Symmetrie zerfällt genau dort, wo sie auffällt.
                color: root.color
                width: horiz ? root._q(len) + swS : swS
                height: horiz ? swS : root._q(len) + swS
                radius: Math.min(width, height) / 2
                x: root._q((modelData[0] + modelData[2]) / 2 * root._u - width / 2)
                y: root._q((modelData[1] + modelData[3]) / 2 * root._u - height / 2)
            }
        }

        //  Schräge Striche: eine Strecke mit runden Enden im Kurven-Renderer.
        Repeater {
            model: root._diagLines
            delegate: Shape {
                id: seg
                required property var modelData
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: root.color
                    strokeWidth: (seg.modelData.length > 4 ? seg.modelData[4] : 2) * root._u
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    startX: seg.modelData[0] * root._u
                    startY: seg.modelData[1] * root._u
                    PathLine { x: seg.modelData[2] * root._u; y: seg.modelData[3] * root._u }
                }
            }
        }

        //  Rahmen. Qt zeichnet den Rand NACH INNEN, die Vorlage mittig auf der
        //  Kante - deshalb um einen halben Strich nach außen versetzt. Kanten und
        //  Randstärke rasten wie die Striche auf ganze Pixel ein.
        Repeater {
            model: root._def.boxes !== undefined ? root._def.boxes : []
            delegate: Rectangle {
                required property var modelData
                readonly property real bw: root._qw(root._u * 2)
                color: "transparent"
                border.color: root.color
                border.width: bw
                antialiasing: true
                width: root._q(modelData[2] * root._u) + bw
                height: root._q(modelData[3] * root._u) + bw
                x: root._q((modelData[0] + modelData[2] / 2) * root._u - width / 2)
                y: root._q((modelData[1] + modelData[3] / 2) * root._u - height / 2)
                radius: ((modelData.length > 4 ? modelData[4] : 0) + 1) * root._u
            }
        }

        //  Kreis-Rahmen (Rechteck mit halbem Radius ist exakt ein Kreis).
        //  Der Durchmesser rastet ein, damit der Kreis rund bleibt und nicht
        //  auf einer Seite einen halben Pixel breiter ausfällt.
        Repeater {
            model: root._def.rings !== undefined ? root._def.rings : []
            delegate: Rectangle {
                required property var modelData
                readonly property real bw: root._qw(root._u * 2)
                color: "transparent"
                border.color: root.color
                border.width: bw
                antialiasing: true
                width: root._q(2 * modelData[2] * root._u + bw)
                height: width
                x: root._q(modelData[0] * root._u - width / 2)
                y: root._q(modelData[1] * root._u - width / 2)
                radius: width / 2
            }
        }

        //  Gefüllte Flächen (die Balken des Schwärzens) - ebenfalls eingerastet.
        Repeater {
            model: root._def.fills !== undefined ? root._def.fills : []
            delegate: Rectangle {
                required property var modelData
                color: root.color
                antialiasing: true
                width: root._q(modelData[2] * root._u)
                height: root._qw(modelData[3] * root._u)
                x: root._q((modelData[0] + modelData[2] / 2) * root._u - width / 2)
                y: root._q((modelData[1] + modelData[3] / 2) * root._u - height / 2)
                radius: (modelData.length > 4 ? modelData[4] : 0) * root._u
                opacity: modelData.length > 5 ? modelData[5] : 1
            }
        }

        //  BUCHSTABE als Symbolkern (Regel 28 lässt reine Buchstaben zu). Nötig
        //  dort, wo der Sinn am Buchstaben hängt: „a" mit Strich heißt
        //  durchgestrichen, eine gezeichnete Ersatzform las sich als Bauteil.
        //  `letterY` verschiebt die Grundlinie, weil eine zentrierte Textzeile die
        //  ZEILENBOX zentriert, nicht die Glyphe - bei Kleinbuchstaben sitzt der
        //  sichtbare Körper sonst zu hoch.
        Text {
            visible: root._def.letter !== undefined
            text: root._def.letter !== undefined ? root._def.letter : ""
            color: root.color
            font.pixelSize: Math.max(6, Math.round(root.size *
                (root._def.letterSize !== undefined ? root._def.letterSize : 0.8)))
            font.bold: true
            x: (root.width - width) / 2
            y: (root.height - height) / 2
                + (root._def.letterY !== undefined ? root._def.letterY : 0) * root._u
        }

        //  Der krumme Anteil - nur bei den neun Symbolen, die ihn brauchen.
        //  Hier bleibt das 24er-Raster samt `scale`, s. Begründung bei `_u`.
        Item {
            width: 24
            height: 24
            scale: root._u
            transformOrigin: Item.TopLeft
            Loader {
                anchors.fill: parent
                sourceComponent: root._curve(root.name)
            }
        }
    }

    //  ── Kurven und Vielecke ──────────────────────────────────────────────────
    function _curve(n) {
        switch (n) {
        case "ellipse":           return cEllipse
        case "eye":               return cEye
        case "markup-underline":  return cUnderline
        case "undo":              return cUndo
        case "redo":              return cRedo
        case "signature":         return cSignature
        case "pen":               return cPen
        case "save":              return cSave
        case "select":            return cSelect
        case "play":              return cPlay
        case "loop":              return cLoop
        case "loop-one":          return cLoop
        case "rotate-right":      return cRotateRight
        case "rotate-left":       return cRotateLeft
        }
        return null
    }

    component Outline: ShapePath {
        strokeColor: root.color
        strokeWidth: 2
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
    }

    Component {
        id: cEllipse
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                PathAngleArc { centerX: 12; centerY: 12; radiusX: 8.5; radiusY: 6
                               startAngle: 0; sweepAngle: 360 }
            }
        }
    }

    Component {
        id: cEye
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                startX: 2.5; startY: 12
                PathCubic { control1X: 2.5;  control1Y: 12; control2X: 6;    control2Y: 6
                            x: 12;   y: 6 }
                PathCubic { control1X: 18;   control1Y: 6;  control2X: 21.5; control2Y: 12
                            x: 21.5; y: 12 }
                PathCubic { control1X: 21.5; control1Y: 12; control2X: 18;   control2Y: 18
                            x: 12;   y: 18 }
                PathCubic { control1X: 6;    control1Y: 18; control2X: 2.5;  control2Y: 12
                            x: 2.5;  y: 12 }
            }
        }
    }

    Component {
        id: cUnderline
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                startX: 6; startY: 4
                PathLine { x: 6; y: 11 }
                PathArc  { x: 18; y: 11; radiusX: 6; radiusY: 6
                           direction: PathArc.Counterclockwise }
                PathLine { x: 18; y: 4 }
            }
        }
    }

    Component {
        id: cUndo
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                startX: 4; startY: 10
                PathLine { x: 15; y: 10 }
                PathArc  { x: 15; y: 20; radiusX: 5; radiusY: 5
                           direction: PathArc.Clockwise }
                PathLine { x: 8; y: 20 }
            }
        }
    }

    Component {
        id: cRedo
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                startX: 20; startY: 10
                PathLine { x: 9; y: 10 }
                PathArc  { x: 9; y: 20; radiusX: 5; radiusY: 5
                           direction: PathArc.Counterclockwise }
                PathLine { x: 16; y: 20 }
            }
        }
    }

    Component {
        id: cSignature
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                startX: 3; startY: 17
                PathCubic { control1X: 6;  control1Y: 17; control2X: 7;  control2Y: 8
                            x: 10;   y: 8 }
                PathCubic { control1X: 13; control1Y: 8;  control2X: 12; control2Y: 15
                            x: 14.5; y: 15 }
                PathCubic { control1X: 17; control1Y: 15; control2X: 18; control2Y: 9
                            x: 21;   y: 9 }
            }
        }
    }

    Component {
        id: cPen
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            //  Breiterer Körper (4,8 statt 4,2 Einheiten quer) und mittig im
            //  24er-Kasten (x und y je 4,5…19,6): bei 16 px war der schmale
            //  Stift kaum noch als Stift zu erkennen.
            Outline {
                startX: 4.5; startY: 19.6
                PathLine { x: 5.9;  y: 14.7 }
                PathLine { x: 16.1; y: 4.4 }
                PathLine { x: 19.5; y: 7.8 }
                PathLine { x: 9.1;  y: 18.4 }
                PathLine { x: 4.5;  y: 19.6 }
            }
        }
    }

    Component {
        id: cSave
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                startX: 5; startY: 4
                PathLine { x: 16; y: 4 }
                PathLine { x: 19; y: 7 }
                PathLine { x: 19; y: 20 }
                PathLine { x: 5;  y: 20 }
                PathLine { x: 5;  y: 4 }
            }
        }
    }

    //  Wiedergabe-Dreieck: gefülltes Vieleck mit leicht gerundeten Ecken.
    Component {
        id: cPlay
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root.color
                strokeWidth: 1.5
                fillColor: root.color
                joinStyle: ShapePath.RoundJoin
                startX: 7; startY: 4.5
                PathLine { x: 19;  y: 12 }
                PathLine { x: 7;   y: 19.5 }
                PathLine { x: 7;   y: 4.5 }
            }
        }
    }

    //  Ring der Wiederholung: fast geschlossener Kreis + Pfeilspitze am Ende.
    Component {
        id: cLoop
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                PathAngleArc { centerX: 12; centerY: 12; radiusX: 7.5; radiusY: 7.5
                               startAngle: -50; sweepAngle: 305 }
            }
            //  Spitze am oberen Ende des Rings, nach rechts zeigend.
            ShapePath {
                strokeColor: "transparent"
                fillColor: root.color
                startX: 14.8; startY: 2.4
                PathLine { x: 19.4; y: 5.2 }
                PathLine { x: 14.8; y: 8.0 }
                PathLine { x: 14.8; y: 2.4 }
            }
        }
    }

    //  Seite drehen: derselbe Ring, aber als RICHTUNGSANZEIGE gedacht - im
    //  Menü steht nur noch „Drehen", die Richtung sagt allein das Symbol.
    //  Winkel: 0 Grad liegt auf 3 Uhr, positive Werte laufen im Uhrzeigersinn.
    Component {
        id: cRotateRight
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            //  Luecke von 90 Grad (statt 55 wie beim Wiederholungs-Ring): bei
            //  18-20 px im Menue ist die OFFENE SEITE das, was man zuerst
            //  sieht - erst danach die Spitze.
            Outline {
                PathAngleArc { centerX: 12; centerY: 12; radiusX: 6.4; radiusY: 6.4
                               startAngle: 220; sweepAngle: -260 }
            }
            //  Spitze wie beim Wiederholungs-Ring gebaut, nur groesser: SENKRECHTE
            //  Grundlinie, die den Bogen kreuzt, Spitze nach aussen. Eine sauber
            //  tangential gerechnete Spitze sah am Pruefstand LOSGELOEST aus - sie
            //  wird an der Kreuzungsstelle zu schmal, um den Strich zu decken.
            //  Groesser als beim Ring, weil im Menue nur noch „Drehen" steht und
            //  die RICHTUNG die ganze Aussage ist.
            //  **Welche der beiden gespiegelten Formen „rechts" heisst, ist eine
            //  Frage des LESENS, nicht der Geometrie.** Die Zuordnung folgt der
            //  Festlegung des Nutzers (2026-08-26); die AKTION dahinter ist
            //  unabhaengig davon geprueft: `rotatePage(v,-90)` dreht die Seite
            //  nachweislich nach links (`bench_pageorder`, schwarze Seitenecke).
            ShapePath {
                strokeColor: "transparent"
                fillColor: root.color
                startX: 10.4; startY: 0.8
                PathLine { x: 2.4; y: 5.4 }
                PathLine { x: 10.4; y: 10.0 }
                PathLine { x: 10.4; y: 0.8 }
            }
        }
    }

    //  Gespiegelt an x = 12 (Spiegeln per `scale: -1` würde den
    //  Antialiasing-Saum mitspiegeln, s. Regel 28 - deshalb eigene Zahlen).
    Component {
        id: cRotateLeft
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                PathAngleArc { centerX: 12; centerY: 12; radiusX: 6.4; radiusY: 6.4
                               startAngle: -40; sweepAngle: 260 }
            }
            ShapePath {
                strokeColor: "transparent"
                fillColor: root.color
                startX: 13.6; startY: 0.8
                PathLine { x: 21.6; y: 5.4 }
                PathLine { x: 13.6; y: 10.0 }
                PathLine { x: 13.6; y: 0.8 }
            }
        }
    }

    Component {
        id: cSelect
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root.color
                strokeWidth: 1.5
                fillColor: root.color
                joinStyle: ShapePath.RoundJoin
                startX: 5; startY: 3
                PathLine { x: 19;   y: 11 }
                PathLine { x: 13;   y: 12.6 }
                PathLine { x: 10.4; y: 18.6 }
                PathLine { x: 5;    y: 3 }
            }
        }
    }

    //  ── Die 44 Symbole ───────────────────────────────────────────────────────
    readonly property var _table: ({
        "align-center": { lines: [[4,6,20,6], [7,12,17,12], [6,18,18,18]] },
        "align-left":   { lines: [[4,6,20,6], [4,12,14,12], [4,18,17,18]] },
        "align-right":  { lines: [[4,6,20,6], [10,12,20,12], [7,18,20,18]] },
        "arrow":        { lines: [[5,19,19,5], [12,5,19,5], [19,5,19,12]] },
        //  Richtungspfeile mit Schaft - NICHT die Chevrons: die tragen im PDF
        //  die Seiten-Navigation, und zwei Bedeutungen dürfen nicht dieselbe
        //  Form haben. Schaft achsenparallel (rastet scharf ein), Spitze schräg
        //  (läuft über `Shape`, s. oben).
        "arrow-right":  { lines: [[5,12,19,12], [13.5,6.5,19,12], [19,12,13.5,17.5]] },
        "arrow-left":   { lines: [[5,12,19,12], [10.5,6.5,5,12], [5,12,10.5,17.5]] },
        "arrow-up":     { lines: [[12,5,12,19], [6.5,10.5,12,5], [12,5,17.5,10.5]] },
        "arrow-down":   { lines: [[12,5,12,19], [6.5,13.5,12,19], [12,19,17.5,13.5]] },
        "audio":        { lines: [[9,18,9,6], [9,6,19,4], [19,4,19,16]],
                          rings: [[6.5,18,2.5], [16.5,16,2.5]] },
        "caret":        { lines: [[9,4,15,4], [9,20,15,20], [12,4,12,20]] },
        "check":        { lines: [[4.5,12.5,9.5,17.5], [9.5,17.5,19.5,7]] },
        "chevron-down": { lines: [[5,9,12,16], [12,16,19,9]] },
        "chevron-left": { lines: [[15,5,8,12], [8,12,15,19]] },
        "chevron-right":{ lines: [[9,5,16,12], [16,12,9,19]] },
        "chevron-up":   { lines: [[5,15,12,8], [12,8,19,15]] },
        "close":        { lines: [[6,6,18,18], [18,6,6,18]] },
        "copy":         { lines: [[16,5,5.5,5], [4,6.5,4,17]],
                          boxes: [[8,8,12,12,1.5]] },
        "ellipse":      { },
        "eye":          { rings: [[12,12,2.5]] },
        //  Blatt mit drei Textzeilen - Gegenstueck zum "folder" im "+"-Menue
        //  der Filterleiste ("Datei erstellen" gegen "Ordner anlegen").
        "file":         { boxes: [[6,3,12,18,1]],
                          lines: [[9,9,15,9], [9,13,15,13], [9,17,13,17]] },
        "fit-window":   { lines: [[8,9,6,9], [6,9,6,7], [16,9,18,9], [18,9,18,7],
                                  [8,15,6,15], [6,15,6,17], [16,15,18,15], [18,15,18,17]],
                          boxes: [[3.5,4.5,17,15,1]] },
        //  Ordner: Reiter + Korpus als gefuellte Flaechen. Bewusst gefuellt und
        //  nicht als Rahmen - die Ordnerkachel zeigt das Symbol gross, und ein
        //  2-px-Rahmen wirkt in dieser Groesse duenn und leer.
        "folder":       { fills: [[3,5.5,8.5,3,1], [3,7,18,12.5,1.5]] },
        //  Offener Ordner: Rueckwand blasser (Alpha), Vorderblatt tiefer und
        //  breiter davor - die Stufe dazwischen liest sich als „aufgeklappt".
        //  Zwei Toene aus EINER Farbe, weil `fills` einen Alpha-Wert kennt.
        "folder-open":  { fills: [[3,5.5,8.5,3,1,0.5], [3,7,17,7,1.5,0.5],
                                  [4.5,11,18.5,8.5,1.5,1]] },
        "image":        { lines: [[4,17,9,12], [9,12,13,16], [13,16,16,14], [16,14,20,17]],
                          boxes: [[3.5,5,17,14,1.5]], rings: [[8.5,10,1.6]] },
        "markup-highlight": { lines: [[3.5,18,20.5,18,3]], boxes: [[3.5,6,17,8,1]] },
        //  Ein „a" mit Strich mittendurch - wie in Textverarbeitungen. Das frühere
        //  T las sich als Bauteil des Symbols, nicht als Buchstabe (Nutzerbefund);
        //  der Sinn „durchgestrichener TEXT" hängt aber am Buchstaben.
        //  „S" mit Strich - dieselbe Wahl wie in LibreOffice (Colibre, Breeze und
        //  Elementary zeigen dort alle drei ein S). Das frühere T las sich als
        //  Bauteil statt als Buchstabe; Words „abc" wäre bei 16 px matschig.
        //  `letterY` hebt die Glyphe an, bis der Strich durch ihre MITTE geht:
        //  eine zentrierte Textzeile zentriert die ZEILENBOX, nicht die Tinte
        //  (gemessen: ein halbes Rastermaß Versatz).
        "markup-strike":    { letter: "S", letterSize: 0.80, letterY: -0.5,
                              lines: [[4,12,20,12,2.5]] },
        "markup-underline": { lines: [[5,20,19,20]] },
        "minus":        { lines: [[5,12,19,12]] },
        //  Zwinge (der Metallring) quer zur Achse; der Körper steht in `_curve`.
        "pen":          { lines: [[13.24,7.28,16.59,10.77]] },
        //  Wiedergabe/Pause der Audio-Leiste. Die Pause sind zwei Balken (reine
        //  Rechtecke, s. Regel 28); das Dreieck der Wiedergabe ist ein Vieleck
        //  und steht deshalb in `_curve`.
        "pause":        { fills: [[7,5,4,14,1], [13,5,4,14,1]] },
        "play":         { },
        //  Wiederholung: ein geschlossener Ring mit Pfeilspitze (`_curve`).
        //  „loop-one" trägt zusätzlich eine kleine, dicke 1 in der Mitte - ohne
        //  ihren Fuß, sonst passt sie nicht in den Ring.
        "list":         { lines: [[9,6,20,6], [9,12,20,12], [9,18,20,18]],
                          fills: [[4,5,2,2,1], [4,11,2,2,1], [4,17,2,2,1]] },
        "loop":         { },
        "loop-one":     { fills: [[10.9,8.9,2.5,6.2,1.2]],
                          lines: [[9.6,10.7,11.0,9.1]] },
        "plus":         { lines: [[12,5,12,19], [5,12,19,12]] },
        "rect":         { boxes: [[3.5,6,17,12,1]] },
        //  Seite drehen - reine Kurven, s. `_curve`.
        "rotate-left":  { },
        "rotate-right": { },
        "redact":       { fills: [[3,4,18,2,1,0.45], [3,16,13,2,1,0.45], [2.5,9,19,5.5,1]] },
        "redo":         { lines: [[16,6,20,10], [20,10,16,14]] },
        "replace":      { lines: [[4,9,17,9], [14,6,17,9], [17,9,14,12],
                                  [20,15,7,15], [10,12,7,15], [7,15,10,18]] },
        "save":         { lines: [[8,4,8,10], [8,10,15,10], [15,10,15,4]],
                          boxes: [[8,13,8,7,0]] },
        "search":       { lines: [[15,15,20,20]], rings: [[10.5,10.5,6]] },
        "select":       { },
        "signature":    { lines: [[4,21,20,21]] },
        "snap":         { lines: [[4,7,20,7], [4,12,20,12], [4,17,20,17]] },
        //  Gitter in exakten Dritteln (die Vorlage teilte 6/5.5/5.5 bzw. 5/5/4).
        "table":        { lines: [[3.5,9.67,20.5,9.67], [3.5,14.33,20.5,14.33],
                                  [9.17,5,9.17,19], [14.83,5,14.83,19]],
                          boxes: [[3.5,5,17,14,1]] },
        "toc":          { lines: [[4,6,8,6], [11,6,20,6], [4,12,8,12],
                                  [11,12,20,12], [4,18,8,18], [11,18,20,18]] },
        "undo":         { lines: [[8,6,4,10], [4,10,8,14]] },
        "valign-middle":{ lines: [[4,12,20,12], [12,3,12,9], [9,6,12,3], [12,3,15,6],
                                  [12,21,12,15], [9,18,12,21], [12,21,15,18]] },
        "valign-top":   { lines: [[4,4,20,4], [12,20,12,9], [8,13,12,9], [12,9,16,13]] }
    })
}
