pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  AudioEqPanel.qml - die zehn Regler, die Vorverstärkung und die
//  Voreinstellungen.
//
//  Bewusst ein schlichtes `Item`, KEIN Popup: dieselbe Datei dient beiden
//  Stellen - als Inhalt des Popups an der Player-Leiste und eingebettet in
//  Einstellungen ▸ Audio. Zwei Fassungen derselben Regler liefen sonst
//  auseinander.
//
//  Alle Werte kommen aus `Audio` und gehen sofort dorthin zurück; das Panel
//  hält keinen eigenen Zustand.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: panel
    objectName: "audioEqPanel"        // Griff für tests/bench (Regel 31)

    //  Die Breite kommt AUS dem Inhalt: die Zeile mit Namensfeld und den beiden
    //  Preset-Knöpfen ist breiter als die zehn Regler, und bei fester Breite
    //  stand der letzte Knopf außerhalb des Panels (Nutzerbefund).
    //  Untergrenze 300 px: unter der Breite bricht der Inhalt um (s. `Flow`),
    //  statt aus dem Fenster zu laufen - in einer schmalen Hälfte stand das
    //  Popup sonst über dem Rand (Nutzerbild `tests/miniEQ.png`).
    implicitWidth: Math.max(300, Math.min(520, presetRow.implicitWidth))
    implicitHeight: body.implicitHeight

    Column {
        id: body
        width: panel.width
        spacing: 10

        // ── Kopfzeile: an/aus, Voreinstellung, alles auf null ───────────────
        //  `Flow` statt `Row`: bei schmalem Panel rutschen die Knöpfe in die
        //  nächste Zeile, statt rechts hinauszulaufen.
        Flow {
            width: parent.width
            spacing: 10

            CheckBox {
                text: App.uiText(App.language, "AudioEqOn")
                checked: Audio.eqEnabled
                onToggled: Audio.eqEnabled = checked
                contentItem: Text {
                    text: parent.text; color: App.themeTextPrimary
                    leftPadding: parent.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
            }

            ComboBox {
                id: presetBox
                width: Math.min(160, Math.max(110, panel.width - 150))
                model: Audio.presetNames
                //  Die Auswahl WÄHLT - sie zeigt nicht den aktuellen Stand:
                //  nach einem Regler-Zug passt kein Name mehr.
                displayText: App.uiText(App.language, "AudioEqPreset")
                onActivated: function(i) { Audio.applyPreset(Audio.presetNames[i]) }
            }

            Button {
                text: App.uiText(App.language, "AudioEqReset")
                onClicked: Audio.resetBands()
            }
        }

        // ── Die zehn Bänder ─────────────────────────────────────────────────
        Row {
            id: bandRow
            width: parent.width
            spacing: 2

            Repeater {
                model: Audio.eqFrequencies
                delegate: Column {
                    id: bandCol
                    required property int index
                    required property var modelData
                    width: (bandRow.width - 9 * bandRow.spacing) / 10
                    spacing: 4

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        //  31 … 16k - vierstellig aufwärts abgekürzt, sonst
                        //  passt die Beschriftung nicht unter den Regler.
                        text: {
                            const f = bandCol.modelData
                            return f >= 1000 ? (Math.round(f / 100) / 10) + "k"
                                             : String(Math.round(f))
                        }
                        color: App.themeTextMuted
                        font.pixelSize: 10
                    }

                    //  Eigene Optik statt des Stock-Reglers: der Standard bringt
                    //  ein fremdes Blau mit und passt in kein Theme.
                    Slider {
                        id: bandSlider
                        anchors.horizontalCenter: parent.horizontalCenter
                        orientation: Qt.Vertical
                        implicitWidth: 20
                        height: 120
                        from: -12; to: 12; stepSize: 0.5
                        value: Audio.eqGains[bandCol.index]
                        onMoved: Audio.setBandGain(bandCol.index, value)

                        background: Rectangle {
                            x: bandSlider.width / 2 - 2
                            width: 4; height: bandSlider.height; radius: 2
                            color: Qt.rgba(1, 1, 1, 0.14)
                            //  Der Ausschlag geht von der MITTE aus (0 dB) -
                            //  eine Füllung von unten läge bei jedem Regler an.
                            Rectangle {
                                width: parent.width; radius: parent.radius
                                color: App.themeAccent
                                y: Math.min(parent.height / 2,
                                            (1 - (bandSlider.value + 12) / 24) * parent.height)
                                height: Math.abs(bandSlider.value) / 24 * parent.height
                            }
                        }
                        handle: Rectangle {
                            x: bandSlider.width / 2 - width / 2
                            y: bandSlider.visualPosition * (bandSlider.height - height)
                            width: 14; height: 14; radius: 7
                            color: bandSlider.pressed ? App.themeAccent : App.themeTextPrimary
                            border.color: App.themeBorder
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (bandSlider.value > 0 ? "+" : "") + bandSlider.value.toFixed(1)
                        color: Math.abs(bandSlider.value) < 0.05 ? App.themeTextMuted
                                                                 : App.themeAccent
                        font.pixelSize: 10
                    }
                }
            }
        }

        // ── Vorverstärkung ──────────────────────────────────────────────────
        Flow {
            width: parent.width
            spacing: 8

            Text {
                height: 20                     // gleiche Höhe wie der Regler
                verticalAlignment: Text.AlignVCenter
                text: App.uiText(App.language, "AudioEqPreamp")
                color: App.themeTextPrimary
                font.pixelSize: 12
            }
            Slider {
                id: preampSlider
                width: 160
                height: 20
                from: -12; to: 12; stepSize: 0.5
                value: Audio.eqPreamp
                onMoved: Audio.eqPreamp = value

                background: Rectangle {
                    y: preampSlider.height / 2 - 2
                    width: preampSlider.width; height: 4; radius: 2
                    color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        height: parent.height; radius: parent.radius
                        color: App.themeAccent
                        x: Math.min(parent.width / 2,
                                    (preampSlider.value + 12) / 24 * parent.width)
                        width: Math.abs(preampSlider.value) / 24 * parent.width
                    }
                }
                handle: Rectangle {
                    x: preampSlider.visualPosition * (preampSlider.width - width)
                    y: preampSlider.height / 2 - height / 2
                    width: 14; height: 14; radius: 7
                    color: preampSlider.pressed ? App.themeAccent : App.themeTextPrimary
                    border.color: App.themeBorder
                }
            }
            Text {
                height: 20
                verticalAlignment: Text.AlignVCenter
                text: (preampSlider.value > 0 ? "+" : "") + preampSlider.value.toFixed(1) + " dB"
                color: App.themeTextMuted
                font.pixelSize: 11
            }
        }

        // ── Eigene Voreinstellung sichern / löschen ─────────────────────────
        Flow {
            id: presetRow
            width: parent.width
            spacing: 8

            TextField {
                id: presetName
                width: Math.min(180, panel.width - 24)
                placeholderText: App.uiText(App.language, "AudioEqPresetName")
                color: App.themeTextPrimary
                onAccepted: if (text.trim().length > 0) { Audio.savePreset(text.trim()); text = "" }
            }
            Button {
                text: App.uiText(App.language, "AudioEqSavePreset")
                enabled: presetName.text.trim().length > 0
                onClicked: { Audio.savePreset(presetName.text.trim()); presetName.text = "" }
            }
            Button {
                text: App.uiText(App.language, "AudioEqDeletePreset")
                enabled: presetName.text.trim().length > 0
                onClicked: { Audio.deletePreset(presetName.text.trim()); presetName.text = "" }
            }
        }
    }
}
