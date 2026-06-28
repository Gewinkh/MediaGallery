import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0

// ── Ansicht / Layout: Anordnung, manuelle Zone, Kachelgröße ──────────────────
Item {
    id: root

    // Referenz-Fenstergröße für die maßstabsgetreue Zonen-Vorschau
    readonly property int winW: App.initialWindowWidth  > 0 ? App.initialWindowWidth  : 1280
    readonly property int winH: App.initialWindowHeight > 0 ? App.initialWindowHeight : 800

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 14

            // ── Kachel-Anordnung ──────────────────────────────────────────────
            SettingsGroup {
                title: App.uiText(App.language, "SettingsViewTileArrangement")
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: App.uiText(App.language, "SettingsViewArrangementHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                }

                ButtonGroup { id: arrGroup }

                Repeater {
                    model: [
                        { label: App.uiText(App.language, "SettingsViewAlignCenter"),               value: 0 },
                        { label: App.uiText(App.language, "SettingsViewAlignLeft"),             value: 1 },
                        { label: App.uiText(App.language, "SettingsViewAlignRight"),            value: 2 },
                        { label: App.uiText(App.language, "SettingsViewAlignManual"), value: 3 }
                    ]
                    delegate: RadioButton {
                        required property var modelData
                        text: modelData.label
                        checked: App.tileArrangement === modelData.value
                        ButtonGroup.group: arrGroup
                        onToggled: if (checked) App.setTileArrangement(modelData.value)
                        contentItem: Text {
                            text: parent.text; color: App.themeTextPrimary
                            leftPadding: parent.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // ── Manuelles Unterpanel ──────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    spacing: 10
                    visible: App.tileArrangement === 3

                    // Zonen-Vorschau (maßstabsgetreu zum Fenster)
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 200
                        radius: 6
                        color: Qt.darker(App.themeBackground, 1.2)
                        border.color: App.themeBorder
                        clip: true

                        Item {
                            id: previewArea
                            anchors.fill: parent
                            anchors.margins: 10

                            // Maßstab so, dass das Fenster komplett passt
                            readonly property real sf: Math.min(width / root.winW, height / root.winH)
                            readonly property real frameW: root.winW * previewArea.sf
                            readonly property real frameH: root.winH * previewArea.sf

                            // Fenster-Rahmen
                            Rectangle {
                                id: winFrame
                                width: previewArea.frameW
                                height: previewArea.frameH
                                color: Qt.rgba(1, 1, 1, 0.03)
                                border.color: App.themeBorder
                                radius: 3

                                // Zonen-Rechteck (links-oben verankert)
                                Rectangle {
                                    id: zoneRect
                                    x: 0; y: 0
                                    width: Math.min(parent.width, App.manualAreaWidth * previewArea.sf)
                                    height: App.manualAreaHeight === 0
                                            ? parent.height
                                            : Math.min(parent.height, App.manualAreaHeight * previewArea.sf)
                                    color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                    border.color: App.themeAccent
                                    border.width: 1.5
                                    radius: 2

                                    // SE-Resize-Griff
                                    Rectangle {
                                        id: zoneHandle
                                        width: 16; height: 16; radius: 8
                                        color: App.themeAccent
                                        border.color: "white"; border.width: 1.5
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.rightMargin: -8
                                        anchors.bottomMargin: -8

                                        DragHandler {
                                            id: zoneDrag
                                            target: null
                                            property int startW: 0
                                            property int startH: 0
                                            onActiveChanged: {
                                                if (active) {
                                                    startW = App.manualAreaWidth
                                                    startH = App.manualAreaHeight === 0
                                                             ? Math.round(winFrame.height / previewArea.sf)
                                                             : App.manualAreaHeight
                                                }
                                            }
                                            onActiveTranslationChanged: {
                                                if (!active) return
                                                var dx = activeTranslation.x / previewArea.sf
                                                var dy = activeTranslation.y / previewArea.sf
                                                App.setManualAreaWidth(Math.max(80, Math.round(startW + dx)))
                                                App.setManualAreaHeight(Math.max(0, Math.round(startH + dy)))
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Breite
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label { text: App.uiText(App.language, "SettingsViewWidth"); color: App.themeTextPrimary; Layout.preferredWidth: 70 }
                        Slider {
                            id: wSlider
                            Layout.fillWidth: true
                            from: 80; to: 8000
                            value: App.manualAreaWidth
                            onMoved: App.setManualAreaWidth(Math.round(value))
                        }
                        SpinBox {
                            from: 80; to: 8000; stepSize: 10
                            value: App.manualAreaWidth
                            editable: true
                            textFromValue: function(v){ return v + " px" }
                            valueFromText: function(t){ return parseInt(t) }
                            onValueModified: App.setManualAreaWidth(value)
                        }
                    }

                    // Höhe (0 = Auto)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label { text: App.uiText(App.language, "SettingsViewHeight"); color: App.themeTextPrimary; Layout.preferredWidth: 70 }
                        Slider {
                            id: hSlider
                            Layout.fillWidth: true
                            from: 0; to: 2000
                            value: App.manualAreaHeight
                            onMoved: App.setManualAreaHeight(Math.round(value))
                        }
                        SpinBox {
                            from: 0; to: 2000; stepSize: 10
                            value: App.manualAreaHeight
                            editable: true
                            textFromValue: function(v){ return v === 0 ? App.uiText(App.language, "SettingsGenBackendAuto") : (v + " px") }
                            valueFromText: function(t){ return t === App.uiText(App.language, "SettingsGenBackendAuto") ? 0 : parseInt(t) }
                            onValueModified: App.setManualAreaHeight(value)
                        }
                    }
                }
            }

            // ── Kachelgröße ───────────────────────────────────────────────────
            SettingsGroup {
                title: App.uiText(App.language, "SettingsViewTileSize")
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label { text: App.uiText(App.language, "SettingsViewWidth"); color: App.themeTextPrimary }
                    SpinBox {
                        id: tileW
                        from: 40; to: 4096; stepSize: 8
                        value: App.tileWidth
                        editable: true
                        textFromValue: function(v){ return v + " px" }
                        valueFromText: function(t){ return parseInt(t) }
                    }
                    Label { text: App.uiText(App.language, "SettingsViewHeight"); color: App.themeTextPrimary }
                    SpinBox {
                        id: tileH
                        from: 40; to: 4096; stepSize: 8
                        value: App.tileHeight
                        editable: true
                        textFromValue: function(v){ return v + " px" }
                        valueFromText: function(t){ return parseInt(t) }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: App.uiText(App.language, "SettingsDesignApplyBtn")
                        highlighted: true
                        onClicked: App.setTileSize(tileW.value, tileH.value)
                    }
                }
            }

            // ── Zoom-Hinweis ──────────────────────────────────────────────────
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.RichText
                color: App.themeTextMuted
                font.pixelSize: 11
                text: App.uiText(App.language, "SettingsViewZoomHint")
            }

            Item { Layout.fillHeight: true }
        }
    }
}
