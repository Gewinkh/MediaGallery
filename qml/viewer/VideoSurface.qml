import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  VideoSurface.qml - interner Video-Player (ersetzt VideoPlayer(QWidget)+
//  MultimediaWidgets). MediaPlayer + VideoOutput + AudioOutput, vollständig in
//  QML. Steuerung (Play/Pause/Seek/Volume/Mute) als QML; Auto-Hide der Leiste.
//
//  RAM/Perf: Quelle wird nur gesetzt, solange diese Surface aktiv ist; beim
//  Verlassen ruft der Aufrufer release() -> stop() + source = "" -> Dekoder frei.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    property string source: ""
    property bool   active: true
    // Von FullscreenViewer gesetzt (Höhe der globalen unteren Navigation
    // ◀ N/M ▶), damit die eigene Steuerleiste (unten) sie nicht überdeckt.
    // Weich animiert, damit die Leiste beim Ein-/Ausblenden der Navigation
    // mitgleitet statt zu springen (180 ms, passend zur bottomNav-Fade-Dauer).
    property real   bottomInset: 0
    Behavior on bottomInset { NumberAnimation { duration: 180 } }

    // ── Mono-Play ─────────────────────────────────────────────────────────────
    //  Eindeutiges Token dieser Wiedergabestelle (Objekt-Identität -> je Kachel/
    //  Instanz verschieden). Beim Play-Start meldet sich der Player über
    //  App.announcePlayback; startet eine ANDERE Stelle (fremdes Token),
    //  pausiert diese hier - Position bleibt erhalten (Pause, kein Stop).
    //  App sendet playbackStarted NUR bei aktivierter Mono-Play-Option.
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

    // Relatives Spulen (Millisekunden, negativ = zurück) - genutzt von den
    // Pfeiltasten im Vollbild (FullscreenViewer). Geklemmt auf [0, duration];
    // bei noch unbekannter Dauer (duration <= 0) passiert nichts.
    function seekBy(deltaMs) {
        if (player.duration <= 0 || !player.seekable) return
        player.position = Math.max(0, Math.min(player.position + deltaMs,
                                               player.duration))
        root.showControls()
    }

    // Steuerleiste einblenden und den Auto-Hide-Timer neu starten.
    function showControls() {
        root._controlsShown = true
        hideTimer.restart()
    }

    onSourceChanged: {
        // Rohen lokalen Pfad in eine korrekt kodierte file://-URL wandeln -
        // analog zu Image/HtmlSurface. MediaPlayer.source ist QUrl-typisiert;
        // ein roher String ohne Schema wird von QML relativ zur Basis-URL der
        // Komponente (qrc:/qml/…) aufgelöst -> ungültige qrc-Ressource, daher
        // die Fehlermeldung "Attempting to play invalid Qt resource" beim
        // Öffnen von Video-/Audio-Dateien. App.fileUrl() (QUrl::fromLocalFile)
        // kodiert zudem Sonderzeichen (Leerzeichen, CJK/Arabisch, …) korrekt.
        player.source = source.length > 0 ? App.fileUrl(source) : ""
        if (source.length > 0 && active)
            player.play()
    }

    Rectangle { anchors.fill: parent; color: "#000000" }

    //  Für die Übergabe an den Player-Modus (Alt+A aus einer offenen Datei):
    //  die Stelle, an der man gerade steht, und ob es läuft.
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

    // ── Klick auf Videofläche: Play/Pause ────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
        onPositionChanged: root.showControls()
        hoverEnabled: true
    }

    // ── Auto-Hide der Steuerleiste ───────────────────────────────────────────
    //  Die Leiste wird NIE ausgeblendet, solange der Zeiger auf ihr steht oder
    //  ein Regler gezogen wird. Zuvor lief der Timer weiter, sobald der Zeiger
    //  die Leiste erreichte: über ihr bekam die darunterliegende MouseArea keine
    //  Bewegungen mehr, der Timer feuerte nach 2,5 s und die Leiste blendete sich
    //  genau beim Zugreifen weg. Im ausgeblendeten Zustand ist sie zudem nicht
    //  mehr bedienbar (enabled/visible) - eine unsichtbare, aber weiterhin
    //  klickbare Leiste schluckte sonst Klicks auf die Videofläche.
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

    // ── Steuerleiste ─────────────────────────────────────────────────────────
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

        //  Hält die Leiste sichtbar, solange der Zeiger auf ihr steht, und holt
        //  sie beim Betreten sofort zurück.
        HoverHandler {
            id: controlsHover
            onHoveredChanged: if (hovered) root.showControls()
        }

        //  RowLayout statt Row: Der Fortschrittsregler bekam seine Breite früher
        //  als `parent.width - 320` - in schmalen Kacheln (geteilte Ansicht) wurde
        //  das negativ, der Regler schrumpfte auf 0 und war nicht mehr greifbar.
        //  Jetzt füllt er den Rest und behält eine Mindestbreite.
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            //  GEZEICHNET, nicht als Glyphe (Regel 28): ⏸ und ▶ kommen aus
            //  verschiedenen Unicode-Blöcken und werden von der Schrift
            //  unterschiedlich groß gerastert - der Pause-Knopf sah dadurch
            //  kleiner aus als der Play-Knopf (Nutzerbefund).
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
                text: root.formatTime(player.position)
                color: "white"
                font.pixelSize: 11
            }

            Slider {
                id: seek
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                from: 0
                to: Math.max(1, player.duration)
                //  Der Griff folgt dem Player - aber NICHT während des Ziehens
                //  (sonst zöge die laufende Wiedergabe ihn zurück). Als eigenes
                //  Binding statt `value: pressed ? value : …`, das sich selbst
                //  referenziert (Binding-Schleife).
                Binding on value {
                    when: !seek.pressed
                    value: player.position
                    restoreMode: Binding.RestoreNone
                }
                onMoved: player.position = value
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
