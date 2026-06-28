import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ── Editor: Auto-Speichern ───────────────────────────────────────────────────
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
                title: App.uiText(App.language, "SettingsEditorAutoSaveGroup")
                Layout.fillWidth: true

                CheckBox {
                    id: autoChk
                    text: App.uiText(App.language, "EditorAutoSave")
                    checked: App.autoSaveEnabled
                    onToggled: App.autoSaveEnabled = checked
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                RowLayout {
                    Layout.leftMargin: 24
                    spacing: 10
                    enabled: autoChk.checked
                    opacity: enabled ? 1.0 : 0.5

                    Label {
                        text: App.uiText(App.language, "SettingsEditorIntervalLabel")
                        color: App.themeTextPrimary
                    }
                    SpinBox {
                        id: intervalSpin
                        from: 5; to: 3600; stepSize: 5
                        value: App.autoSaveInterval
                        editable: true
                        textFromValue: function(v) { return v + " s" }
                        valueFromText: function(t) { return parseInt(t) }
                        onValueModified: App.autoSaveInterval = value
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
