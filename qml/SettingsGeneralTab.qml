import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ── Allgemein: Sprache + Video-Wiedergabe + Render-Backend ───────────────────
Item {
    id: root

    // Signal nach oben: Backend gespeichert → Dialog schließen
    signal rhiSwitchSucceeded()

    readonly property var backendOptions: {
        const p = Qt.platform.os
        if (p === "windows") return ["d3d11", "vulkan", "opengl", "software"]
        if (p === "osx")     return ["metal",  "opengl", "software"]
        return                        ["vulkan", "opengl", "software"]
    }

    function backendLabel(b) {
        switch (b) {
            case "vulkan":   return "Vulkan"
            case "d3d11":    return "Direct3D 11"
            case "metal":    return "Metal"
            case "opengl":   return "OpenGL"
            case "software": return qsTr("Software (kein GPU)")
            default:         return b !== "" ? b : "OpenGL"
        }
    }

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

            // ── Render-Backend ────────────────────────────────────────────────
            SettingsGroup {
                title: qsTr("Render-Backend")
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Label {
                            text: qsTr("Backend:")
                            color: App.themeTextPrimary
                        }

                        ComboBox {
                            id: rhiCombo
                            Layout.preferredWidth: 200
                            model: root.backendOptions.map(b => root.backendLabel(b))

                            Component.onCompleted: {
                                const active = Settings.rhiBackend.toLowerCase()
                                const idx = root.backendOptions.indexOf(active)
                                currentIndex = (idx >= 0) ? idx : 0
                            }
                        }

                        Button {
                            text: qsTr("Speichern & Schließen")
                            enabled: root.backendOptions[rhiCombo.currentIndex]
                                     !== Settings.rhiBackend.toLowerCase()

                            onClicked: {
                                const chosen = root.backendOptions[rhiCombo.currentIndex]
                                App.trySetRhiBackend(chosen)
                                root.rhiSwitchSucceeded()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        text: qsTr("Aktuell aktiv: %1  —  Änderungen werden beim nächsten Start wirksam.")
                              .arg(root.backendLabel(Settings.rhiBackend))
                        color: App.themeTextMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // Hinweis wenn Software-Fallback automatisch aktiviert wurde
                    Label {
                        visible: Settings.rhiBackend === "software"
                        text: qsTr("⚠  Software-Renderer aktiv. Möglicherweise wurde beim letzten Start ein inkompatibles Backend erkannt und automatisch zurückgesetzt.")
                        color: "#e8a000"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
