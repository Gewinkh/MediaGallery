import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ── Allgemein: Sprache + Video-Wiedergabe ────────────────────────────────────
Item {
    id: root

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 16

            SettingsGroup {
                title: qsTr("Sprache")
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: qsTr("Sprache:")
                        color: App.themeTextPrimary
                    }
                    ComboBox {
                        id: langBox
                        Layout.preferredWidth: 180
                        model: ["Deutsch", "English"]
                        currentIndex: App.language === "en" ? 1 : 0
                        onActivated: App.setLanguage(currentIndex === 1 ? "en" : "de")
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            SettingsGroup {
                title: qsTr("Video-Wiedergabe")
                Layout.fillWidth: true

                RadioButton {
                    text: qsTr("Intern (eingebauter Player)")
                    checked: App.videoPlayback === "native"
                    onToggled: if (checked) App.setVideoPlayback("native")
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    text: qsTr("Extern (System-Player)")
                    checked: App.videoPlayback === "external"
                    onToggled: if (checked) App.setVideoPlayback("external")
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
