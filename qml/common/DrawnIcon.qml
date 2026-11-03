pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import QtQuick.Window
import MediaGallery 1.0
import "DrawnIconTable.js" as Symbole

// Jedes Bedien-Symbol als gezeichnete Form statt SVG + Cache: keine Textur, kein Rastern je Farbe
// oder Größe, ein Themenwechsel ist eine gewöhnliche Bindung. Alle Formen im 24x24-Raster, per
// `scale` auf `size`; `DrawnIconTable.js` beschreibt sie aus lines/boxes/rings/fills, echte Kurven aus `_curve`.
Item {
    id: root

    property string name: ""
    property real   size: 16
    property color  color: App.themeTextPrimary

    implicitWidth: root.size
    implicitHeight: root.size
    width: root.size
    height: root.size

    readonly property var _def: Symbole.tabelle[root.name] !== undefined
                                ? Symbole.tabelle[root.name] : ({})

    // Rechtecke gleich in Zielpixeln aufbauen statt über `scale`: Qt legt den Antialiasing-Saum in ITEM-
    // Koordinaten an, ein skaliertes Rechteck bekommt ihn mitvergrößert. `Shape` ist davon nicht betroffen.
    readonly property real _u: root.size / 24

    // Eingerastet wird auf GERÄTEpixel: bei 125/150/200 % ist ein logisches Pixel kein Rasterschritt mehr - darauf
    // gerundet läge die Kante wieder mitten in einem Gerätepixel und das Symbol wirkte grau.
    readonly property real _dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    function _q(v) { return Math.round(v * root._dpr) / root._dpr }
    function _qw(v) { return Math.max(1 / root._dpr, root._q(v)) }

    readonly property var _lines: root._def.lines !== undefined ? root._def.lines : []
    // Achsenparallel = eingerastetes Rechteck (scharf), schräg = `Shape`: ein gedrehtes Rechteck rastert seine
    // Kanten nicht spiegelbildlich, die beiden Balken des "close"-X wirkten unterschiedlich schwer.
    readonly property var _axisLines: root._lines.filter(function (l) {
        return l[0] === l[2] || l[1] === l[3]
    })
    readonly property var _diagLines: root._lines.filter(function (l) {
        return l[0] !== l[2] && l[1] !== l[3]
    })

    Item {
        anchors.fill: parent

        // Ein Strich ist ein Rechteck der Länge len + w (runde Kappen verlängern um w/2 je Ende). Achsenparallele
        // rasten auf ganze Pixel: bei 16 px läge ein 1,33 px breiter Strich zwischen zwei Pixelreihen und wirkte grau.
        Repeater {
            model: root._axisLines
            delegate: Rectangle {
                required property var modelData
                readonly property bool horiz: modelData[1] === modelData[3]
                readonly property real sw: (modelData.length > 4 ? modelData[4] : 2) * root._u
                readonly property real len: Math.hypot(modelData[2] - modelData[0],
                                                       modelData[3] - modelData[1]) * root._u
                readonly property real swS: root._qw(sw)
                // IMMER von der MITTE aus einrasten, nie von der Kante: sonst landen zwei Formen mit derselben Mittellinie
                // (Schaft und Balken des "caret") einen halben Pixel auseinander.
                color: root.color
                width: horiz ? root._q(len) + swS : swS
                height: horiz ? swS : root._q(len) + swS
                radius: Math.min(width, height) / 2
                x: root._q((modelData[0] + modelData[2]) / 2 * root._u - width / 2)
                y: root._q((modelData[1] + modelData[3]) / 2 * root._u - height / 2)
            }
        }

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

        // Buchstabe als Symbolkern, wo der Sinn am Buchstaben hängt. `letterY` verschiebt die Grundlinie, weil eine
        // zentrierte Textzeile die ZEILENBOX zentriert, nicht die Glyphe.
        // Nur bei Bedarf gebaut: ein leeres Text beobachtet in Qt den Viewport, jeder Teiler-Schritt lief dann den Baum ab.
        Loader {
            active: root._def.letter !== undefined
            sourceComponent: Text {
                text: root._def.letter
                color: root.color
                font.pixelSize: Math.max(6, Math.round(root.size *
                    (root._def.letterSize !== undefined ? root._def.letterSize : 0.8)))
                font.bold: true
                x: (root.width - width) / 2
                y: (root.height - height) / 2
                    + (root._def.letterY !== undefined ? root._def.letterY : 0) * root._u
            }
        }

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

    function _curve(n) {
        switch (n) {
        case "ellipse":           return cEllipse
        case "eye":               return cEye
        case "markup-underline":  return cUnderline
        case "gear":              return cGear
        case "trash":             return cTrash
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

    function _polar(r, deg) {
        const t = deg * Math.PI / 180
        return Qt.point(12 + r * Math.cos(t), 12 + r * Math.sin(t))
    }

    function _gearOutline() {
        const teeth = 8, rIn = 7.0, rOut = 10.2
        const step = 360 / teeth
        const halfOut = 13, halfIn = step / 2 - 5
        let pts = []
        for (let i = 0; i < teeth; ++i) {
            const a = i * step
            pts.push(root._polar(rIn,  a - halfIn))
            pts.push(root._polar(rOut, a - halfOut))
            pts.push(root._polar(rOut, a + halfOut))
            pts.push(root._polar(rIn,  a + halfIn))
        }
        pts.push(pts[0])                       // Ring schliessen
        return pts
    }

    component Outline: ShapePath {
        strokeColor: root.color
        strokeWidth: 2
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
    }

    Component {
        id: cGear
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline { PathPolyline { path: root._gearOutline() } }
        }
    }

    Component {
        id: cTrash
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                startX: 6.2; startY: 7.5
                PathLine { x: 7.2;  y: 20.5 }
                PathLine { x: 16.8; y: 20.5 }
                PathLine { x: 17.8; y: 7.5 }
            }
        }
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

    Component {
        id: cLoop
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            Outline {
                PathAngleArc { centerX: 12; centerY: 12; radiusX: 7.5; radiusY: 7.5
                               startAngle: -50; sweepAngle: 305 }
            }
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
            // Spitze auf SENKRECHTER Grundlinie, die den Bogen kreuzt: tangential gerechnet wird sie an der
            // Kreuzung zu schmal, um den Strich zu decken, und sah losgelöst aus. Welche der gespiegelten Formen
            // "rechts" heisst, ist eine Frage des Lesens - die Aktion dahinter ist mit `bench_pageorder` geprüft.
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

}
