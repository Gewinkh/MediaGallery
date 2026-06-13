import QtQuick

// ─────────────────────────────────────────────────────────────────────────────
//  ImageViewer.qml – fullscreen image viewer with pinch-zoom, wheel-zoom and pan.
//
//  C++ side (ImageViewerWindow) sets `imageSource` and listens for the signals
//  below.  No JavaScript-heavy logic and no Controls dependency.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root
    focus: true

    // Set from C++ via rootObject()->setProperty("imageSource", ...)
    property string imageSource: ""

    signal closeRequested()
    signal previousRequested()
    signal nextRequested()

    // Reset zoom/pan whenever a new image is shown
    onImageSourceChanged: {
        image.scale = 1.0
        image.x = 0
        image.y = 0
    }

    Rectangle {
        anchors.fill: parent
        color: "#0a0a0a"
    }

    PinchArea {
        id: pinch
        anchors.fill: parent

        property real startScale: 1.0

        onPinchStarted: startScale = image.scale
        onPinchUpdated: {
            var s = startScale * pinch.scale
            image.scale = Math.max(0.1, Math.min(s, 10.0))
        }

        MouseArea {
            id: dragArea
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            drag.target: image
            drag.axis: Drag.XAndYAxis
            cursorShape: image.scale > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor

            onWheel: function(wheel) {
                var factor = wheel.angleDelta.y > 0 ? 1.15 : 1.0 / 1.15
                var s = image.scale * factor
                image.scale = Math.max(0.1, Math.min(s, 10.0))
                if (image.scale <= 1.0) {
                    image.x = 0
                    image.y = 0
                }
                wheel.accepted = true
            }
        }
    }

    Image {
        id: image
        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        source: root.imageSource
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: true
        cache: false
        transformOrigin: Item.Center
    }

    // ── On-screen controls ────────────────────────────────────────────────────
    Row {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 16
        spacing: 12

        Rectangle {
            width: 44; height: 44; radius: 8
            color: backMa.containsMouse ? "#1f4d47" : "#1a1a1a"
            border.color: "#3a4a48"; border.width: 1
            Text {
                anchors.centerIn: parent
                text: "\u2190"; color: "#c8dbd5"; font.pixelSize: 20
            }
            MouseArea {
                id: backMa
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.closeRequested()
            }
        }
    }

    Row {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.margins: 20
        spacing: 16

        Rectangle {
            width: 54; height: 44; radius: 8
            color: prevMa.containsMouse ? "#1f4d47" : "#1a1a1a"
            border.color: "#3a4a48"; border.width: 1
            Text { anchors.centerIn: parent; text: "\u25C0"; color: "#c8dbd5"; font.pixelSize: 18 }
            MouseArea {
                id: prevMa
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.previousRequested()
            }
        }

        Rectangle {
            width: 54; height: 44; radius: 8
            color: nextMa.containsMouse ? "#1f4d47" : "#1a1a1a"
            border.color: "#3a4a48"; border.width: 1
            Text { anchors.centerIn: parent; text: "\u25B6"; color: "#c8dbd5"; font.pixelSize: 18 }
            MouseArea {
                id: nextMa
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.nextRequested()
            }
        }
    }

    // ── Keyboard navigation ────────────────────────────────────────────────────
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            root.closeRequested(); event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            root.previousRequested(); event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            root.nextRequested(); event.accepted = true
        } else if ((event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)
                   && (event.modifiers & Qt.ControlModifier)) {
            image.scale = Math.min(image.scale * 1.15, 10.0); event.accepted = true
        } else if (event.key === Qt.Key_Minus && (event.modifiers & Qt.ControlModifier)) {
            image.scale = Math.max(image.scale / 1.15, 0.1)
            if (image.scale <= 1.0) { image.x = 0; image.y = 0 }
            event.accepted = true
        }
    }
}
