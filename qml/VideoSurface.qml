import QtQuick
import QtQuick.Controls
import QtMultimedia

// ─────────────────────────────────────────────────────────────────────────────
//  VideoSurface.qml — interner Video-Player (ersetzt VideoPlayer(QWidget)+
//  MultimediaWidgets). MediaPlayer + VideoOutput + AudioOutput, vollständig in
//  QML. Steuerung (Play/Pause/Seek/Volume/Mute) als QML; Auto-Hide der Leiste.
//
//  RAM/Perf: Quelle wird nur gesetzt, solange diese Surface aktiv ist; beim
//  Verlassen ruft der Aufrufer release() → stop() + source = "" → Dekoder frei.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    property string source: ""
    property bool   active: true

    function release() {
        player.stop()
        player.source = ""
    }

    onSourceChanged: {
        player.source = source.length > 0 ? source : ""
        if (source.length > 0 && active)
            player.play()
    }

    Rectangle { anchors.fill: parent; color: "#000000" }

    MediaPlayer {
        id: player
        videoOutput: videoOut
        audioOutput: AudioOutput { id: audioOut; volume: 0.85 }
        onErrorOccurred: function(err, str) { errorLabel.text = str }
    }

    VideoOutput {
        id: videoOut
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }

    Text {
        id: errorLabel
        anchors.centerIn: parent
        color: "#ff8a80"
        font.pixelSize: 14
        visible: text.length > 0
        wrapMode: Text.WordWrap
        width: parent.width * 0.8
        horizontalAlignment: Text.AlignHCenter
    }

    // ── Klick auf Videofläche: Play/Pause ────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
        onPositionChanged: { controls.opacity = 1.0; hideTimer.restart() }
        hoverEnabled: true
    }

    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: if (player.playbackState === MediaPlayer.PlayingState) controls.opacity = 0.0
    }

    // ── Steuerleiste ─────────────────────────────────────────────────────────
    Rectangle {
        id: controls
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 52
        color: Qt.rgba(0, 0, 0, 0.6)
        opacity: 1.0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 36; height: 36
                text: player.playbackState === MediaPlayer.PlayingState ? "\u23F8" : "\u25B6"
                font.pixelSize: 16
                onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.formatTime(player.position)
                color: "white"
                font.pixelSize: 11
            }

            Slider {
                id: seek
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 320
                from: 0
                to: Math.max(1, player.duration)
                value: pressed ? value : player.position
                onMoved: player.position = value
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.formatTime(player.duration)
                color: "white"
                font.pixelSize: 11
            }

            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 32; height: 36
                text: audioOut.muted ? "\u{1F507}" : "\u{1F50A}"
                font.pixelSize: 14
                onClicked: audioOut.muted = !audioOut.muted
            }

            Slider {
                anchors.verticalCenter: parent.verticalCenter
                width: 90
                from: 0; to: 1.0
                value: audioOut.volume
                onMoved: { audioOut.volume = value; if (value > 0) audioOut.muted = false }
            }
        }
    }

    function formatTime(ms) {
        if (ms <= 0) return "0:00"
        var s = Math.floor(ms / 1000)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        var mm = (h > 0 && m < 10) ? "0" + m : "" + m
        var ss = sec < 10 ? "0" + sec : "" + sec
        return (h > 0 ? h + ":" : "") + mm + ":" + ss
    }
}
