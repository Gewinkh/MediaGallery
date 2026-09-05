import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"
import "../gallery"

// Audio: Klangregelung und Verhalten des Player-Modus (Alt+A)
//  Die Regler sind DIESELBE Datei wie am Player (`AudioEqPanel`) - zwei
//  Fassungen liefen sonst auseinander.
Item {
    id: root

    ScrollView {
        id: audioScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 14

            SettingsGroup {
                key: "audio.audio-eq"
                title: App.uiText(App.language, "AudioEqTitle")
                Layout.fillWidth: true

                CheckBox {
                    text: App.uiText(App.language, "AudioEqAutoPreamp")
                    checked: Audio.eqAutoPreamp
                    onToggled: Audio.eqAutoPreamp = checked
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.bottomMargin: 6
                    text: App.uiText(App.language, "AudioEqAutoPreampHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                AudioEqPanel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    //  Hier - und nur hier - lassen sich Voreinstellungen
                    //  löschen, zurücksetzen und umsortieren (Wunsch des
                    //  Nutzers); am Player wählt und sichert man nur.
                    manageable: true
                }
            }

            SettingsGroup {
                key: "audio.audio-layout"
                title: App.uiText(App.language, "AudioLayoutTitle")
                Layout.fillWidth: true

                //  Eigene `ButtonGroup`: `RadioButton` gruppiert sich sonst über
                //  das Elternelement, und `SettingsGroup` steckt alle Kinder in
                //  DIESELBE innere ColumnLayout (s. `## Settings`).
                ButtonGroup { id: layoutGroup }

                RadioButton {
                    ButtonGroup.group: layoutGroup
                    text: App.uiText(App.language, "AudioLayoutTiles")
                    checked: !Audio.listLayout
                    onToggled: if (checked) Audio.listLayout = false
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    ButtonGroup.group: layoutGroup
                    text: App.uiText(App.language, "AudioLayoutList")
                    checked: Audio.listLayout
                    onToggled: if (checked) Audio.listLayout = true
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "AudioLayoutHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            SettingsGroup {
                key: "audio.audio-player-mode"
                title: App.uiText(App.language, "AudioPlayerMode")
                Layout.fillWidth: true

                CheckBox {
                    text: App.uiText(App.language, "AudioShowVideos")
                    checked: Audio.showVideos
                    onToggled: Audio.showVideos = checked
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                CheckBox {
                    text: App.uiText(App.language, "AudioRememberLast")
                    checked: Audio.rememberLast
                    onToggled: Audio.rememberLast = checked
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "AudioRememberHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: App.uiText(App.language, "AudioVolume")
                        color: App.themeTextPrimary
                        font.pixelSize: 12
                    }
                    Slider {
                        id: volSlider
                        Layout.preferredWidth: 200
                        from: 0; to: 1; stepSize: 0.01
                        value: Audio.volume
                        onMoved: Audio.volume = value
                    }
                    Text {
                        text: Math.round(volSlider.value * 100) + " %"
                        color: App.themeTextMuted
                        font.pixelSize: 11
                    }
                }
            }

            // Ton aus Videos sichern
            //  Ausgelöst wird es am Video (Rechtsklick auf die Kachel) oder in
            //  der Warteschlange des Players; hier stehen nur die Vorgaben.
            SettingsGroup {
                key: "audio.audio-extract"
                title: App.uiText(App.language, "AudioExtractGroup")
                Layout.fillWidth: true

                CheckBox {
                    text: App.uiText(App.language, "AudioExtractInheritTags")
                    checked: Audio.extractInheritTags
                    onToggled: Audio.extractInheritTags = checked
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "AudioExtractInheritHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                CheckBox {
                    text: App.uiText(App.language, "AudioExtractToQueue")
                    checked: Audio.extractToQueue
                    onToggled: Audio.extractToQueue = checked
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "AudioExtractToQueueHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    //  Weiches, schnelles Mausrad-Scrollen (Galerie-Muster) statt der festen
    //  60 px je Rastung von `Flickable`. Fehlte hier als einzigem Reiter -
    //  dadurch lief der Audio-Reiter spürbar zäher als die übrigen acht.
    SmoothWheelArea { flickable: audioScroll.contentItem }
}
