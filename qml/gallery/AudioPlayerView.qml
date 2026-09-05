pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// Die große Player-Ansicht einer Hälfte: Titel groß, Fortschritt breit und vor allem die WARTESCHLANGE. Kein
// Cover - das bräuchte ID3-Auswertung; stattdessen ein gezeichneter Verlauf mit Notensymbol.
Item {
    id: view
    objectName: "audioPlayerView"     // Griff für tests/bench

    signal backRequested()

    function _name(p) {
        if (!p) return ""
        const cut = Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"))
        const n = cut >= 0 ? p.slice(cut + 1) : p
        const dot = n.lastIndexOf(".")
        return dot > 0 ? n.slice(0, dot) : n
    }

    Rectangle {
        anchors.fill: parent
        color: App.themeBackground
    }

    // Schmale Hälfte: für zwei Spalten ist kein Platz, die Warteschlange tritt an die Stelle des laufenden Titels
    // und wird über die Kopfzeile umgeschaltet. Gemessener Anlass: bei ~400 px überlappten Steuerung und Liste.
    readonly property bool narrow: width < 720
    property bool showQueue: false
    property bool queueOpen: true
    readonly property bool queueVisible: view.narrow ? view.showQueue : view.queueOpen

    Rectangle {
        id: head
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 42
        color: "transparent"

        Rectangle {
            id: backBtn
            readonly property real avail: headTools.x - 10 - 8
            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
            width: Math.max(30, Math.min(backRow.width + 18, backBtn.avail))
            height: 28; radius: 14
            clip: true
            color: backHover.hovered ? App.themeCard : "transparent"
            Row {
                id: backRow
                anchors.centerIn: parent
                spacing: 6
                DrawnIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-left"; size: 14; color: App.themeTextPrimary
                }
                Text {
                    //  Der Text erscheint nur, wenn er VOLLSTÄNDIG hineinpasst
                    //  - sonst bleibt der Pfeil allein, er ist auch so eindeutig.
                    visible: backBtn.avail > implicitWidth + 46
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.uiText(App.language, "AudioBackToGallery")
                    color: App.themeTextPrimary
                    font.pixelSize: 12
                }
            }
            HoverHandler { id: backHover }
            TapHandler { onTapped: view.backRequested() }
        }

        Row {
            id: headTools
            anchors { right: parent.right; rightMargin: 12
                      verticalCenter: parent.verticalCenter }
            spacing: 4

            component HeadBtn: Rectangle {
                id: hb
                property string iconName: ""
                property bool   on: false
                property int    iconSize: 15
                property string tip: ""
                signal clicked()
                width: 30; height: 30; radius: 15
                anchors.verticalCenter: parent.verticalCenter
                color: hb.on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                       App.themeAccent.b, 0.20)
                     : (hbHover.hovered ? App.themeCard : "transparent")
                DrawnIcon {
                    anchors.centerIn: parent
                    name: hb.iconName; size: hb.iconSize
                    color: hb.on ? App.themeAccent : App.themeTextPrimary
                }
                HoverHandler { id: hbHover }
                TapHandler { onTapped: hb.clicked() }
                ToolTip.visible: hbHover.hovered && hb.tip.length > 0
                ToolTip.delay: 600
                ToolTip.text: hb.tip
            }

            HeadBtn {
                iconName: "list"
                on: view.queueVisible
                tip: App.uiText(App.language, "AudioQueueHeader")
                onClicked: {
                    if (view.narrow) view.showQueue = !view.showQueue
                    else             view.queueOpen = !view.queueOpen
                }
            }
            HeadBtn {
                iconName: "snap"
                on: Audio.eqEnabled
                tip: App.uiText(App.language, "AudioEqTitle")
                onClicked: viewEqPopup.opened ? viewEqPopup.close() : viewEqPopup.open()
            }

            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: view.narrow ? 0 : (headVolIcon.width + headVol.width + 6)
                height: 30
                visible: !view.narrow
                DrawnIcon {
                    id: headVolIcon
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    name: "audio"; size: 14; color: App.themeTextMuted
                }
                Slider {
                    id: headVol
                    anchors { left: headVolIcon.right; leftMargin: 6
                              verticalCenter: parent.verticalCenter }
                    width: 90; height: 20
                    from: 0; to: 1
                    value: Audio.volume
                    onMoved: Audio.volume = value
                    background: Rectangle {
                        y: headVol.height / 2 - 2
                        width: headVol.width; height: 4; radius: 2
                        color: Qt.rgba(1, 1, 1, 0.14)
                        Rectangle {
                            width: headVol.visualPosition * parent.width
                            height: parent.height; radius: parent.radius
                            color: App.themeTextMuted
                        }
                    }
                    handle: Rectangle {
                        x: headVol.visualPosition * (headVol.width - width)
                        y: headVol.height / 2 - height / 2
                        width: 12; height: 12; radius: 6
                        color: headVol.pressed ? App.themeAccent : App.themeTextPrimary
                    }
                }
            }
        }
    }

    Popup {
        id: viewEqPopup
        objectName: "audioViewEqPopup"
        padding: 12
        width: Math.min(view.width - 24, viewEqContent.implicitWidth + 2 * padding)
        height: viewEqContent.implicitHeight + 2 * padding
        x: Math.max(12, view.width - width - 12)
        y: head.height + 4
        background: Rectangle {
            color: App.themeMenuBarBg
            border.color: App.themeBorder; border.width: 1
            radius: 8
        }
        contentItem: AudioEqPanel { id: viewEqContent }
    }

    Item {
        id: nowPane
        visible: !view.narrow || !view.showQueue
        anchors { left: parent.left; top: head.bottom; bottom: parent.bottom }
        anchors.margins: 24
        width: (view.narrow || !view.queueOpen ? view.width
                                               : view.width - queuePane.width) - 48

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width, 420)
            spacing: 18

            //  Das Titelbild aus der Datei - und solange es keines gibt (oder
            //  es noch geladen wird), die gezeichnete Fläche darunter.
            Rectangle {
                id: coverBox
                width: Math.min(parent.width, view.narrow ? 150 : 260)
                height: width
                anchors.horizontalCenter: parent.horizontalCenter
                radius: 16
                clip: true
                gradient: Gradient {
                    GradientStop { position: 0.0
                                   color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                  App.themeAccent.b, 0.30) }
                    GradientStop { position: 1.0; color: App.themeCard }
                }
                DrawnIcon {
                    anchors.centerIn: parent
                    visible: cover.status !== Image.Ready
                    name: "audio"
                    size: parent.width * 0.42
                    color: Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                   App.themeTextPrimary.b, 0.75)
                }
                Image {
                    id: cover
                    objectName: "playerCover"
                    anchors.fill: parent
                    sourceSize.width: Math.round(coverBox.width)
                    sourceSize.height: Math.round(coverBox.height)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    source: Audio.coverSource
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: App.uiText(App.language, "AudioNowPlaying")
                color: App.themeTextMuted
                font.pixelSize: 11
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                text: Audio.trackTitle || App.uiText(App.language, "AudioNoTrack")
                color: App.themeTextPrimary
                font.pixelSize: 20
                font.bold: true
            }
            Text {
                width: parent.width
                visible: Audio.trackSubtitle.length > 0
                height: visible ? implicitHeight : 0
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: Audio.trackSubtitle
                color: App.themeTextMuted
                font.pixelSize: 13
            }

            Item {
                width: parent.width
                height: 26

                Rectangle {
                    id: bigTrack
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    height: 6; radius: 3
                    color: App.themeCard
                    Rectangle {
                        id: bigPlayed
                        width: Audio.duration > 0
                               ? parent.width * Math.min(1, Audio.position / Audio.duration) : 0
                        height: parent.height; radius: parent.radius
                        color: App.themeAccent
                    }
                }
                Rectangle {
                    x: bigPlayed.width - width / 2
                    y: bigTrack.y + bigTrack.height / 2 - height / 2
                    width: 13; height: 13; radius: 6.5
                    color: App.themeAccent
                    visible: Audio.duration > 0
                }
                // Trefferfläche über dem dünnen Balken. Die Breite über die `id` ansprechen: ein nacktes `width` löst im
                // Handler nicht auf das umgebende Element auf, sondern auf die Wurzel der Datei - der Sprung landete falsch.
                Item {
                    id: seekArea
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    height: 18
                    function _seekTo(x) {
                        if (Audio.duration <= 0 || seekArea.width <= 0) return
                        const f = Math.max(0, Math.min(1, x / seekArea.width))
                        Audio.seek(Audio.duration * f)
                    }
                    TapHandler {
                        onTapped: function(point) { seekArea._seekTo(point.position.x) }
                    }
                    DragHandler {
                        target: null; yAxis.enabled: false
                        onActiveChanged: if (!active) seekArea._seekTo(centroid.position.x)
                    }
                }
                Text {
                    anchors { left: parent.left; bottom: parent.bottom }
                    text: Audio.formatTime(Audio.position)
                    color: App.themeTextMuted; font.pixelSize: 11
                }
                Text {
                    anchors { right: parent.right; bottom: parent.bottom }
                    text: Audio.formatTime(Audio.duration)
                    color: App.themeTextMuted; font.pixelSize: 11
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: view.narrow ? 6 : 14

                component RoundBtn: Rectangle {
                    id: rb
                    property string iconName: ""
                    property bool   on: false
                    property int    iconSize: 16
                    signal clicked()
                    width: view.narrow ? 32 : 38
                    height: width; radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: rb.on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                           App.themeAccent.b, 0.20)
                         : (rbHover.hovered ? App.themeCard : "transparent")
                    DrawnIcon {
                        anchors.centerIn: parent
                        name: rb.iconName; size: rb.iconSize
                        color: rb.on ? App.themeAccent : App.themeTextPrimary
                    }
                    HoverHandler { id: rbHover }
                    TapHandler { onTapped: rb.clicked() }
                }

                RoundBtn {
                    iconName: "replace"; on: Audio.shuffle
                    onClicked: Audio.shuffle = !Audio.shuffle
                }
                RoundBtn { iconName: "chevron-left"; onClicked: Audio.previous() }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: view.narrow ? 46 : 54
                    height: width; radius: width / 2
                    color: bigPlayHover.hovered ? Qt.lighter(App.themeAccent, 1.15)
                                                : App.themeAccent
                    DrawnIcon {
                        anchors.centerIn: parent
                        name: Audio.state === 1 ? "pause" : "play"
                        size: 22
                        color: App.themeBackground
                    }
                    HoverHandler { id: bigPlayHover }
                    TapHandler { onTapped: Audio.togglePlay() }
                }
                RoundBtn { iconName: "chevron-right"; onClicked: Audio.next() }
                RoundBtn {
                    iconName: Audio.repeat === 1 ? "loop-one" : "loop"
                    iconSize: 18
                    on: Audio.repeat !== 0
                    onClicked: Audio.repeat = (Audio.repeat + 1) % 3
                }
            }
        }
    }

    Rectangle {
        id: queuePane
        visible: view.queueVisible
        anchors { right: parent.right; top: head.bottom; bottom: parent.bottom }
        width: view.narrow ? view.width : Math.max(240, Math.min(360, view.width * 0.34))
        color: Qt.rgba(1, 1, 1, 0.03)

        Rectangle {
            visible: !view.narrow
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: 1; color: App.themeBorder
        }

        Text {
            id: queueHead
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
            text: App.uiText(App.language, "AudioQueueHeader")
                  + "  (" + Audio.queue.length + ")"
            color: App.themeTextMuted
            font.pixelSize: 11
            font.bold: true
        }

        ListView {
            id: queueList
            objectName: "audioQueueList"
            anchors { left: parent.left; right: parent.right; top: queueHead.bottom
                      bottom: parent.bottom; topMargin: 8 }
            clip: true
            model: Audio.queue
            currentIndex: Audio.queueIndex
            highlightFollowsCurrentItem: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: row
                required property int index
                required property string modelData
                width: queueList.width
                height: 40
                color: row.index === Audio.queueIndex
                       ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.16)
                       : (rowHover.hovered ? App.themeCard : "transparent")

                Rectangle {
                    visible: row.index === Audio.queueIndex
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 3
                    color: App.themeAccent
                }
                Text {
                    anchors { left: parent.left; leftMargin: 14
                              verticalCenter: parent.verticalCenter }
                    width: 22
                    text: row.index + 1
                    color: App.themeTextMuted
                    font.pixelSize: 11
                }
                Text {
                    anchors { left: parent.left; leftMargin: 40
                              right: saveSound.visible ? saveSound.left : parent.right
                              rightMargin: 12; verticalCenter: parent.verticalCenter }
                    elide: Text.ElideMiddle
                    text: Audio.titleOf(row.modelData)
                    color: row.index === Audio.queueIndex ? App.themeAccent
                                                          : App.themeTextPrimary
                    font.pixelSize: 12
                    font.bold: row.index === Audio.queueIndex
                }

                // Bei einem Video lässt sich die Tonspur hier als Datei sichern. BLEIBEND sichtbar, nicht erst bei
                // Zeigerkontakt: die Zeile zeigt den Namen ohne Endung, man sähe ihr sonst nicht an, dass sie ein Video ist.
                Rectangle {
                    id: saveSound
                    objectName: "queueSaveSound"
                    anchors { right: parent.right; rightMargin: 8
                              verticalCenter: parent.verticalCenter }
                    width: 28; height: 28; radius: 14
                    visible: Audio.canExtractAudio(row.modelData)
                    opacity: Audio.extractBusy ? 0.4 : 1.0
                    color: saveHover.hovered ? App.themeCard : "transparent"
                    DrawnIcon {
                        anchors.centerIn: parent
                        name: "save"; size: 15
                        color: saveHover.hovered ? App.themeAccent : App.themeTextMuted
                    }
                    ToolTip.visible: saveHover.hovered
                    ToolTip.text: App.uiText(App.language, "AudioExtractMenu")
                    HoverHandler { id: saveHover }
                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: if (!Audio.extractBusy) Audio.extractAudio(row.modelData)
                    }
                }

                HoverHandler { id: rowHover }
                TapHandler { onTapped: Audio.playAt(row.index) }
            }
        }
    }
}
