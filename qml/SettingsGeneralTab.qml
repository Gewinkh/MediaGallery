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
            case "software": return App.uiText(App.language, "SettingsGenBackendSoftware")
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
                title: App.uiText(App.language, "MenuLanguage")
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: App.uiText(App.language, "SettingsLanguageLabel")
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
                title: App.uiText(App.language, "MenuVideoPlayback")
                Layout.fillWidth: true

                RadioButton {
                    text: App.uiText(App.language, "SettingsGenVideoInternal")
                    checked: App.videoPlayback === "native"
                    onToggled: if (checked) App.setVideoPlayback("native")
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    text: App.uiText(App.language, "SettingsGenVideoExternal")
                    checked: App.videoPlayback === "external"
                    onToggled: if (checked) App.setVideoPlayback("external")
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // ── Vollbild-Animation (Öffnen/Schließen) ─────────────────────────
            SettingsGroup {
                title: App.uiText(App.language, "SettingsGenFullscreenAnim")
                Layout.fillWidth: true

                RadioButton {
                    text: App.uiText(App.language, "SettingsGenAnimSlide")
                    checked: App.pageTransition === "slide"
                    onToggled: if (checked) App.setPageTransition("slide")
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    text: App.uiText(App.language, "SettingsGenAnimFade")
                    checked: App.pageTransition === "fade"
                    onToggled: if (checked) App.setPageTransition("fade")
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // ── Audio-Player-Akzent (PDF-Audioleiste) ─────────────────────────
            SettingsGroup {
                title: App.uiText(App.language, "SettingsGenAudioPlayer")
                Layout.fillWidth: true

                RadioButton {
                    text: App.uiText(App.language, "SettingsGenAudioAccentTheme")
                    checked: !App.audioAccentApple
                    onToggled: if (checked) App.setAudioAccentApple(false)
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    text: App.uiText(App.language, "SettingsGenAudioAccentApple")
                    checked: App.audioAccentApple
                    onToggled: if (checked) App.setAudioAccentApple(true)
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // ── Render-Backend ────────────────────────────────────────────────
            SettingsGroup {
                title: App.uiText(App.language, "SettingsGenRenderBackend")
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Label {
                            text: App.uiText(App.language, "SettingsGenBackendLabel")
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
                            text: App.uiText(App.language, "SettingsGenSaveClose")
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
                        text: App.uiText(App.language, "SettingsGenActiveBackend")
                              .arg(root.backendLabel(Settings.rhiBackend))
                        color: App.themeTextMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // Hinweis wenn Software-Fallback automatisch aktiviert wurde
                    Label {
                        visible: Settings.rhiBackend === "software"
                        text: App.uiText(App.language, "SettingsGenSoftwareWarning")
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
