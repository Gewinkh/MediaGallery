pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  AudioPlayerBar.qml - die Leiste unter der Galerie im Player-Modus.
//
//  Sie erscheint erst, WENN ein Titel läuft, und nur in der Hälfte, die den
//  Player besitzt (`Audio.owner`). Alles geht über den `Audio`-Singleton; eigene
//  Zustände hat sie keine.
//
//  AUFBAU: drei Blöcke nebeneinander - links die Steuerung, rechts die
//  Schalter, dazwischen Titel und Fortschritt. Der mittlere Block liegt
//  ZWISCHEN den beiden äußeren (nicht in einer Reihe mit ihnen): so bleibt der
//  Fortschrittsbalken mittig, egal wie lang der Dateiname ist.
//
//  Ein Klick auf die Mitte öffnet die große Ansicht (`expandRequested`).
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: bar
    objectName: "audioPlayerBar"      // Griff für tests/bench (Regel 31)

    //  Öffnet die große Player-Ansicht mit Warteschlange.
    signal expandRequested()

    implicitHeight: 68
    color: App.themeMenuBarBg

    //  Schmale Hälfte: die drei Blöcke passen nicht mehr nebeneinander. Dann
    //  bleiben Steuerung und Fortschritt, die Schalter rücken in ein kleines
    //  Menü, und der Titel weicht dem Platz. Vorher überlappten die Blöcke
    //  einfach (Nutzerbild `tests/miniPlayer.png`).
    readonly property bool narrow: width < 560
    readonly property bool veryNarrow: width < 380

    //  Eine feine Akzentlinie oben - die Leiste soll sich als eigener Bereich
    //  zu erkennen geben, ohne laut zu werden.
    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: App.themeAccent }
            GradientStop { position: 1.0; color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                        App.themeAccent.b, 0.15) }
        }
    }

    // ── Bausteine ───────────────────────────────────────────────────────────
    //  Ein gezeichneter Knopf (Regel 28: keine Emoji, keine Glyphen).
    component BarBtn: Rectangle {
        id: bb
        property string iconName: ""
        property bool   on: false
        property int    iconSize: 15
        property string tip: ""
        signal clicked()
        width: 30; height: 30; radius: 15
        anchors.verticalCenter: parent.verticalCenter
        color: bb.on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20)
             : (bbHover.hovered ? App.themeCard : "transparent")
        DrawnIcon {
            anchors.centerIn: parent
            name: bb.iconName
            size: bb.iconSize
            color: bb.on ? App.themeAccent : App.themeTextPrimary
        }
        HoverHandler { id: bbHover }
        TapHandler { onTapped: bb.clicked() }
        ToolTip.visible: bbHover.hovered && bb.tip.length > 0
        ToolTip.delay: 600
        ToolTip.text: bb.tip
    }

    // ── Links: die Steuerung ────────────────────────────────────────────────
    Row {
        id: transport
        anchors { left: parent.left; leftMargin: bar.narrow ? 8 : 14
                  verticalCenter: parent.verticalCenter }
        spacing: bar.narrow ? 2 : 6

        BarBtn {
            iconName: "chevron-left"
            tip: App.uiText(App.language, "AudioPrevious")
            onClicked: Audio.previous()
        }
        //  Der größte Knopf der Leiste - er wird am häufigsten getroffen.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 40; height: 40; radius: 20
            color: playHover.hovered ? Qt.lighter(App.themeAccent, 1.15) : App.themeAccent
            DrawnIcon {
                anchors.centerIn: parent
                //  Dreieck (läuft nicht) bzw. zwei Balken (läuft) - gezeichnet.
                name: Audio.state === 1 ? "pause" : "play"
                size: 17
                color: App.themeBackground
            }
            HoverHandler { id: playHover }
            TapHandler { onTapped: Audio.togglePlay() }
        }
        BarBtn {
            iconName: "chevron-right"
            tip: App.uiText(App.language, "AudioNext")
            onClicked: Audio.next()
        }
    }

    // ── Rechts: Zufall, Wiederholung, Equalizer, Lautstärke ─────────────────
    Row {
        id: rightTools
        anchors { right: parent.right; rightMargin: bar.narrow ? 8 : 14
                  verticalCenter: parent.verticalCenter }
        spacing: 4

        //  Im schmalen Fall EIN Knopf, der die übrigen Schalter als Menü zeigt.
        BarBtn {
            visible: bar.narrow
            iconName: "snap"
            tip: App.uiText(App.language, "AudioEqTitle")
            onClicked: moreMenu.opened ? moreMenu.close()
                                       : moreMenu.popup(this, 0, -moreMenu.height - 6)
        }

        BarBtn {
            visible: !bar.narrow
            iconName: "replace"                 // zwei Pfeile über Kreuz
            on: Audio.shuffle
            tip: App.uiText(App.language, "AudioShuffle")
            onClicked: Audio.shuffle = !Audio.shuffle
        }
        BarBtn {
            visible: !bar.narrow
            //  Aus ⇄ Titel ⇄ Liste. „Eine wiederholen" trägt die kleine 1 im
            //  Ring, sonst steht dort der leere Ring.
            iconName: Audio.repeat === 1 ? "loop-one" : "loop"
            iconSize: 17
            on: Audio.repeat !== 0
            tip: App.uiText(App.language,
                            Audio.repeat === 0 ? "AudioRepeatOff"
                          : Audio.repeat === 1 ? "AudioRepeatOne" : "AudioRepeatAll")
            onClicked: Audio.repeat = (Audio.repeat + 1) % 3
        }
        BarBtn {
            visible: !bar.narrow
            iconName: "snap"                    // drei Striche = Regler
            on: Audio.eqEnabled
            tip: App.uiText(App.language, "AudioEqTitle")
            onClicked: eqPopup.opened ? eqPopup.close() : eqPopup.open()
        }

        //  Lautstärke als richtiger Regler mit Griff: der frühere 4-px-Streifen
        //  war kaum zu treffen und sprang bei jedem Klick auf fast 0.
        Item {
            visible: !bar.narrow
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? volIcon.width + volSlider.width + 6 : 0
            height: 30
            DrawnIcon {
                id: volIcon
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                name: "audio"
                size: 14
                color: App.themeTextMuted
            }
            Slider {
                id: volSlider
                anchors { left: volIcon.right; leftMargin: 6
                          verticalCenter: parent.verticalCenter }
                width: 86
                height: 20
                from: 0; to: 1
                value: Audio.volume
                //  `onMoved` statt einer Bindung: sonst schriebe der Regler beim
                //  Aufbau seinen eigenen Anfangswert zurück.
                onMoved: Audio.volume = value
                background: Rectangle {
                    x: 0; y: volSlider.height / 2 - 2
                    width: volSlider.width; height: 4; radius: 2
                    color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        width: volSlider.visualPosition * parent.width
                        height: parent.height; radius: parent.radius
                        color: App.themeTextMuted
                    }
                }
                handle: Rectangle {
                    x: volSlider.visualPosition * (volSlider.width - width)
                    y: volSlider.height / 2 - height / 2
                    width: 12; height: 12; radius: 6
                    color: volSlider.pressed ? App.themeAccent : App.themeTextPrimary
                }
            }
        }
    }

    //  Menü der Schalter für schmale Leisten (dieselben Funktionen, nur gefaltet).
    ThemedMenu {
        id: moreMenu
        objectName: "audioMoreMenu"
        MenuItem {
            text: App.uiText(App.language, "AudioShuffle")
                  + (Audio.shuffle ? "  ✓" : "")
            onTriggered: Audio.shuffle = !Audio.shuffle
        }
        MenuItem {
            text: App.uiText(App.language,
                             Audio.repeat === 0 ? "AudioRepeatOff"
                           : Audio.repeat === 1 ? "AudioRepeatOne" : "AudioRepeatAll")
            onTriggered: Audio.repeat = (Audio.repeat + 1) % 3
        }
        MenuSeparator {}
        MenuItem {
            text: App.uiText(App.language, "AudioEqTitle")
            onTriggered: eqPopup.opened ? eqPopup.close() : eqPopup.open()
        }
        MenuItem {
            text: App.uiText(App.language, "AudioOpenPlayer")
            onTriggered: bar.expandRequested()
        }
    }

    // ── Mitte: Titel + Fortschritt ──────────────────────────────────────────
    Item {
        id: center
        anchors { left: transport.right; leftMargin: bar.narrow ? 8 : 18
                  right: rightTools.left; rightMargin: bar.narrow ? 8 : 18
                  verticalCenter: parent.verticalCenter }
        height: bar.veryNarrow ? 20 : 44

        //  Klick auf die Mitte ⇒ große Ansicht. Der Fortschrittsbalken fängt
        //  seine eigenen Klicks vorher ab, hier bleibt der Titelbereich.
        TapHandler { onTapped: bar.expandRequested() }
        HoverHandler { id: centerHover }

        Text {
            id: titleText
            visible: !bar.veryNarrow
            anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 2 }
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            //  Titel aus den Datei-Tags, dahinter der Interpret; ohne Tags
            //  bleibt es beim Dateinamen (das entscheidet `Audio.trackTitle`).
            text: {
                if (!Audio.currentPath) return App.uiText(App.language, "AudioNoTrack")
                const t = Audio.trackTitle
                const a = Audio.trackArtist
                return a.length > 0 ? t + "  -  " + a : t
            }
            color: centerHover.hovered ? App.themeAccent : App.themeTextPrimary
            font.pixelSize: 13
            font.bold: true
        }
        // ── Fortschritt: tippen und ziehen ──────────────────────────────────
        Item {
            id: progress
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 16

            Text {
                id: posLbl
                visible: !bar.narrow
                width: visible ? implicitWidth : 0
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: Audio.formatTime(Audio.position)
                color: App.themeTextMuted
                font.pixelSize: 11
            }
            Text {
                id: durLbl
                visible: !bar.narrow
                width: visible ? implicitWidth : 0
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: Audio.formatTime(Audio.duration)
                color: App.themeTextMuted
                font.pixelSize: 11
            }

            Item {
                id: trackArea
                anchors { left: posLbl.right; leftMargin: 8; right: durLbl.left; rightMargin: 8
                          verticalCenter: parent.verticalCenter }
                height: 16                      // Trefferfläche, sichtbar sind 5 px

                Rectangle {
                    id: track
                    anchors { left: parent.left; right: parent.right
                              verticalCenter: parent.verticalCenter }
                    height: 5; radius: 2.5
                    color: App.themeCard
                    Rectangle {
                        id: played
                        width: Audio.duration > 0
                               ? parent.width * Math.min(1, Audio.position / Audio.duration) : 0
                        height: parent.height; radius: parent.radius
                        color: App.themeAccent
                    }
                }
                //  Griff nur beim Überfahren - sonst bleibt die Leiste ruhig.
                Rectangle {
                    visible: trackHover.hovered || seekDrag.active
                    x: played.width - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 11; height: 11; radius: 5.5
                    color: App.themeAccent
                }
                HoverHandler { id: trackHover }
                TapHandler {
                    onTapped: function(point) {
                        if (Audio.duration <= 0) return
                        Audio.seek(Audio.duration
                                   * Math.max(0, Math.min(1, point.position.x / Math.max(1, trackArea.width))))
                    }
                }
                DragHandler {
                    id: seekDrag
                    target: null
                    yAxis.enabled: false
                    onActiveChanged: if (!active && Audio.duration > 0) {
                        const f = centroid.position.x / Math.max(1, trackArea.width)
                        Audio.seek(Audio.duration * Math.max(0, Math.min(1, f)))
                    }
                }
            }
        }
    }

    // ── Equalizer-Panel ─────────────────────────────────────────────────────
    //  Dasselbe `AudioEqPanel` steht auch in Einstellungen ▸ Audio; hier bekommt
    //  es nur den Rahmen eines Popups.
    Popup {
        id: eqPopup
        objectName: "audioEqPopup"    // Griff für tests/bench (Regel 31)
        padding: 12
        //  Höhe/Breite AUS dem Inhalt: sonst standen die unteren Knöpfe (Preset
        //  sichern/löschen) außerhalb des Fensters.
        width: eqContent.implicitWidth + 2 * padding
        height: eqContent.implicitHeight + 2 * padding
        y: -height - 8
        x: Math.max(6, bar.width - width - 12)
        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder; border.width: 1
            radius: 8
        }
        contentItem: AudioEqPanel { id: eqContent }
    }
}
