import QtQuick
import QtQuick.Shapes

// AppLogo - das Zeichen der App: ein grosses G, auf seinem waagerechten Strich ein kleines M. Aus dieser Form
// entstehen beim Bau auch die Icons der Installation (tools/mg_logogen.cpp); eine Bilddatei gibt es nicht.
Item {
    id: root

    property real  size: 256
    property color background: "#10232a"
    property color letterG: "#00b4a0"
    property color letterM: "#e8f4f2"

    width: root.size
    height: root.size
    //  Alles im 256er-Raster, in Zielpixeln umgerechnet - ein ueber `scale` vergroessertes Bild bekaeme seinen
    //  Kantensaum mitvergroessert.
    readonly property real u: root.size / 256

    Rectangle {
        anchors.fill: parent
        radius: 56 * root.u
        color: root.background
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        //  Der Bogen des G: oben rechts offen, gegen den Uhrzeiger bis zur Mitte rechts, dort der Querstrich.
        ShapePath {
            strokeColor: root.letterG
            strokeWidth: 30 * root.u
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathAngleArc {
                centerX: 128 * root.u; centerY: 132 * root.u
                radiusX: 82 * root.u; radiusY: 82 * root.u
                startAngle: -42; sweepAngle: -318
            }
            PathLine { x: 140 * root.u; y: 132 * root.u }
        }
        //  Das M sitzt AUF dem Querstrich, innerhalb des G.
        ShapePath {
            strokeColor: root.letterM
            strokeWidth: 11 * root.u
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            startX: 144 * root.u; startY: 108 * root.u
            PathLine { x: 144 * root.u; y: 82 * root.u }
            PathLine { x: 162 * root.u; y: 100 * root.u }
            PathLine { x: 180 * root.u; y: 82 * root.u }
            PathLine { x: 180 * root.u; y: 108 * root.u }
        }
    }
}
