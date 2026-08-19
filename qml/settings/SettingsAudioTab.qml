import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"
import "../gallery"

// ── Audio: Klangregelung und Verhalten des Player-Modus (Alt+A) ──────────────
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

            // ── Equalizer ─────────────────────────────────────────────────────
            SettingsGroup {
                title: App.uiText(App.language, "AudioEqTitle")
                Layout.fillWidth: true

                AudioEqPanel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                }
            }

            // ── Darstellung ───────────────────────────────────────────────────
            SettingsGroup {
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

            // ── Player-Modus ──────────────────────────────────────────────────
            SettingsGroup {
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

                // ── Lautstärke ────────────────────────────────────────────────
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
        }
    }
}
