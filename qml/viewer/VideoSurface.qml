import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import MediaGallery 1.0
import "../common"

// Interner Video-Player, vollständig in QML. Die Quelle wird nur gesetzt, solange die Surface aktiv ist; beim
// Verlassen ruft der Aufrufer `release()` und gibt den Dekoder frei.
Item {
    id: root

    property string source: ""
    property bool   active: true
    property real   bottomInset: 0
    Behavior on bottomInset { NumberAnimation { duration: 180 } }

    // Mono-Play: eindeutiges Token je Wiedergabestelle. Startet eine andere Stelle, PAUSIERT diese hier - kein
    // Stop, damit die Position erhalten bleibt. `App` sendet `playbackStarted` nur bei aktivierter Option.
    readonly property string _playToken: "video-" + root

    Connections {
        target: App
        function onPlaybackStarted(token) {
            if (token !== root._playToken
                    && player.playbackState === MediaPlayer.PlayingState)
                player.pause()
        }
    }

    function release() {
        player.stop()
        player.source = ""
    }

    function seekBy(deltaMs) {
        if (player.duration <= 0 || !player.seekable) return
        player.position = Math.max(0, Math.min(player.position + deltaMs,
                                               player.duration))
        root.showControls()
    }

    function showControls() {
        root._controlsShown = true
        hideTimer.restart()
    }

    onSourceChanged: {
        // `MediaPlayer.source` ist QUrl-typisiert: ein roher String ohne Schema wird relativ zur Basis-URL der
        // Komponente aufgelöst - daher "Attempting to play invalid Qt resource". `App.fileUrl()` kodiert auch Sonderzeichen.
        player.source = source.length > 0 ? App.fileUrl(source) : ""
        if (source.length > 0 && active)
            player.play()
    }

    Rectangle { anchors.fill: parent; color: "#000000" }

    function pausePlayback() { if (player.playbackState === MediaPlayer.PlayingState) player.pause() }
    readonly property int  playbackPositionMs: player.position
    readonly property bool playbackRunning: player.playbackState === MediaPlayer.PlayingState

    MediaPlayer {
        id: player
        videoOutput: videoOut
        audioOutput: AudioOutput { id: audioOut; volume: 0.85 }
        onErrorOccurred: function(err, str) { errorLabel.text = str }
        // Mono-Play: JEDEN Übergang nach Playing melden (auch Resume nach
        // Pause) - andere Wiedergabestellen pausieren sich daraufhin.
        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState)
                App.announcePlayback(root._playToken)
        }
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

    MouseArea {
        anchors.fill: parent
        onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
        onPositionChanged: root.showControls()
        hoverEnabled: true
    }

    // Die Leiste wird nie ausgeblendet, solange der Zeiger auf ihr steht: zuvor bekam die MouseArea darunter keine
    // Bewegungen mehr und die Leiste verschwand genau beim Zugreifen. Ausgeblendet ist sie zudem nicht bedienbar.
    property bool _controlsShown: true
    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: {
            if (controlsHover.hovered || seek.pressed || volumeSlider.pressed) {
                restart()                     // Zeiger/Griff auf der Leiste -> sichtbar lassen
                return
            }
            if (player.playbackState === MediaPlayer.PlayingState)
                root._controlsShown = false
        }
    }

    Rectangle {
        id: controls
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.bottomMargin: root.bottomInset
        height: 52
        color: Qt.rgba(0, 0, 0, 0.6)
        opacity: root._controlsShown ? 1.0 : 0.0
        visible: opacity > 0.01
        enabled: root._controlsShown
        Behavior on opacity { NumberAnimation { duration: 200 } }

        HoverHandler {
            id: controlsHover
            onHoveredChanged: if (hovered) root.showControls()
        }

        // RowLayout statt Row: der Fortschrittsregler bekam seine Breite früher als `parent.width - 320` - in schmalen
        // Kacheln wurde das negativ, er schrumpfte auf 0 und war nicht mehr greifbar.
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            // GEZEICHNET, nicht als Glyphe: Pause und Play kommen aus verschiedenen Unicode-Blöcken und werden
            // unterschiedlich groß gerastert - der Pause-Knopf sah dadurch kleiner aus.
            ToolButton {
                Layout.preferredWidth: 36; Layout.preferredHeight: 36
                onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                contentItem: DrawnIcon {
                    name: player.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                    size: 18
                    color: "white"
                }
            }

            Text {
                text: root.formatTime(seek.pressed ? seek.value : player.position)
                color: "white"
                font.pixelSize: 11
            }

            Slider {
                id: seek
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                from: 0
                to: Math.max(1, player.duration)
                // Der Griff folgt dem Player, aber NICHT während des Ziehens - sonst zöge die laufende Wiedergabe ihn zurück.
                // Als eigenes Binding, weil `value: pressed ? value : ...` sich selbst referenzierte.
                Binding on value {
                    when: !seek.pressed
                    value: player.position
                    restoreMode: Binding.RestoreNone
                }

                // Gesprungen wird beim LOSLASSEN: `onMoved` feuert während des Zuges laufend, und jeder Sprung zwingt den
                // Player auf ein Schlüsselbild - ein einzelner kostet dagegen 65 ms auf 45 min in einer 1,5-GB-Datei.
                property real pendingSeek: -1
                onMoved: {
                    if (seek.pressed) seek.pendingSeek = seek.value
                    else              player.position = seek.value
                }
                onPressedChanged: {
                    if (!seek.pressed && seek.pendingSeek >= 0) {
                        player.position = seek.pendingSeek
                        seek.pendingSeek = -1
                    }
                }
            }

            Text {
                text: root.formatTime(player.duration)
                color: "white"
                font.pixelSize: 11
            }

            ToolButton {
                Layout.preferredWidth: 32; Layout.preferredHeight: 36
                text: audioOut.muted ? "\u{1F507}" : "\u{1F50A}"
                font.pixelSize: 14
                onClicked: audioOut.muted = !audioOut.muted
            }

            Slider {
                id: volumeSlider
                Layout.preferredWidth: 90
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
