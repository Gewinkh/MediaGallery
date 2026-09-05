import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

Item {
    id: root

    component ViewSubHead: ColumnLayout {
        property alias text: subLabel.text
        Layout.fillWidth: true
        Layout.topMargin: 6
        spacing: 6
        Rectangle { Layout.fillWidth: true; height: 1; color: App.themeBorder }
        Text {
            id: subLabel
            Layout.fillWidth: true
            color: App.themeTextMuted
            font.pixelSize: 11
            font.bold: true
        }
    }

    readonly property int winW: Window.window ? Window.window.width
                                : (App.initialWindowWidth  > 0 ? App.initialWindowWidth  : 1280)
    readonly property int winH: Window.window ? Window.window.height
                                : (App.initialWindowHeight > 0 ? App.initialWindowHeight : 800)

    // Obergrenze = Breite des Bildschirms, auf dem das FENSTER liegt; die Galerie klemmt die Zone ohnehin auf die
    // Fensterbreite, vorher ging der Regler bis 8000 px und der obere Teil seines Wegs tat nichts. Gemeldet von der
    // ApplicationShell - das angehängte `Screen` dieses Reiters sitzt im Popup und meldete den PRIMÄREN Schirm.
    readonly property int maxAreaW: App.screenWidth > 0 ? App.screenWidth : 3840

    ScrollView {
        id: viewScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 14

            SettingsGroup {
                key: "view.files"
                title: App.uiText(App.language, "SettingsFilesGroup")
                Layout.fillWidth: true

                CheckBox {
                    text: App.uiText(App.language, "SettingsShowAllFiles")
                    checked: App.showAllFiles
                    onToggled: App.showAllFiles = checked
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "SettingsShowAllFilesHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                CheckBox {
                    text: App.uiText(App.language, "SettingsShowHidden")
                    checked: App.showHiddenFiles
                    onToggled: App.showHiddenFiles = checked
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "SettingsShowHiddenDesc")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            SettingsGroup {
                key: "view.arrangement"
                title: App.uiText(App.language, "SettingsViewGalleryLayout")
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: App.uiText(App.language, "SettingsViewGalleryLayoutHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                }

                // Eigene `ButtonGroup`, zwingend: `RadioButton` ist `autoExclusive` und gruppiert sonst über das ELTERNELEMENT
                // - `SettingsGroup` steckt alle Kinder in dieselbe innere ColumnLayout.
                ButtonGroup { id: galleryLayoutGroup }

                Repeater {
                    model: [
                        { label: App.uiText(App.language, "GalleryLayoutTiles"), value: false },
                        { label: App.uiText(App.language, "GalleryLayoutList"),  value: true }
                    ]
                    delegate: RadioButton {
                        required property var modelData
                        text: modelData.label
                        checked: App.galleryListLayout === modelData.value
                        ButtonGroup.group: galleryLayoutGroup
                        onToggled: if (checked) App.galleryListLayout = modelData.value
                        contentItem: Text {
                            text: parent.text; color: App.themeTextPrimary
                            leftPadding: parent.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: !App.galleryListLayout

                    ViewSubHead { text: App.uiText(App.language, "SettingsViewTileArrangement") }

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

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        spacing: 10
                        visible: App.tileArrangement === 3

                        // Vorschau originalgetreu zur Galerie: zentrierte Zone, an die Fensterbreite geklemmt (-2x12 px Rand), Zelle =
                        // Kachel + 8 px Abstand. Passt der Wunsch nicht ins Fenster, zeigt eine gestrichelte Kontur den geklemmten
                        // Wert - vorher wirkte der Breitenregler dadurch tot.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 340
                            radius: 6
                            color: Qt.darker(App.themeBackground, 1.2)
                            border.color: App.themeBorder
                            clip: true

                            Item {
                                id: previewArea
                                anchors.fill: parent
                                anchors.margins: 10

                                readonly property real sf: Math.min(width / root.winW, height / root.winH)
                                readonly property real frameW: root.winW * previewArea.sf
                                readonly property real frameH: root.winH * previewArea.sf

                                Rectangle {
                                    id: winFrame
                                    anchors.centerIn: parent
                                    width: previewArea.frameW
                                    height: previewArea.frameH
                                    color: Qt.rgba(1, 1, 1, 0.03)
                                    border.color: App.themeBorder
                                    radius: 3

                                    readonly property int  gMargin: 12
                                    readonly property int  gSpacing: 8
                                    readonly property int  tW: tileW.value
                                    readonly property int  tH: tileH.value
                                    readonly property int  cellW: tW + gSpacing
                                    readonly property int  cellH: tH + gSpacing
                                    readonly property int  areaW: Math.min(App.manualAreaWidth,
                                                                           root.winW - 2 * gMargin)
                                    readonly property int  columns: Math.max(1, Math.floor(areaW / cellW))
                                    readonly property int  gridW: columns * cellW
                                    readonly property real gridX: Math.max(gMargin,
                                                                           (root.winW - gridW) / 2)
                                    readonly property int  zoneHpx: root.winH
                                    readonly property int  rows: Math.max(1, Math.floor(zoneHpx / cellH))

                                    Rectangle {
                                        visible: App.manualAreaWidth > winFrame.areaW
                                        x: Math.max(0, (winFrame.width
                                                        - App.manualAreaWidth * previewArea.sf) / 2)
                                        y: 0
                                        width: Math.min(winFrame.width,
                                                        App.manualAreaWidth * previewArea.sf)
                                        height: zoneRect.height
                                        color: "transparent"
                                        border.color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                              App.themeAccent.b, 0.45)
                                        border.width: 1
                                        radius: 2
                                    }

                                    Rectangle {
                                        id: zoneRect
                                        readonly property real pad: 5
                                        x: winFrame.gridX * previewArea.sf - pad
                                        y: 0
                                        width: (winFrame.gridW - winFrame.gSpacing) * previewArea.sf
                                               + 2 * pad
                                        height: winFrame.zoneHpx * previewArea.sf
                                        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                       App.themeAccent.b, 0.14)
                                        border.color: App.themeAccent
                                        border.width: 1.5
                                        radius: 2

                                        Repeater {
                                            model: Math.min(400, winFrame.columns * winFrame.rows)
                                            delegate: Rectangle {
                                                required property int index
                                                readonly property int col: index % winFrame.columns
                                                readonly property int row: Math.floor(index / winFrame.columns)
                                                x: zoneRect.pad + col * winFrame.cellW * previewArea.sf
                                                y: zoneRect.pad + row * winFrame.cellH * previewArea.sf
                                                width: winFrame.tW * previewArea.sf
                                                height: winFrame.tH * previewArea.sf
                                                radius: 2
                                                color: App.themeBackground
                                                border.color: App.themeBorder
                                                opacity: 0.95
                                            }
                                        }

                                        Rectangle {
                                            id: zoneHandle
                                            width: 16; height: 16; radius: 8
                                            color: App.themeAccent
                                            border.color: "white"; border.width: 1.5
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.rightMargin: -8

                                            DragHandler {
                                                id: zoneDrag
                                                target: null
                                                xAxis.enabled: true
                                                yAxis.enabled: false
                                                property int startW: 0
                                                onActiveChanged: if (active) startW = App.manualAreaWidth
                                                onActiveTranslationChanged: {
                                                    if (!active) return
                                                    var dx = activeTranslation.x / previewArea.sf
                                                    App.setManualAreaWidth(
                                                        Math.min(root.maxAreaW,
                                                                 Math.max(80, Math.round(startW + dx))))
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label { text: App.uiText(App.language, "SettingsViewWidth"); color: App.themeTextPrimary; Layout.preferredWidth: 70 }
                            // `Math.min(..., maxAreaW)` ist Pflicht: fällt die Obergrenze unter den gespeicherten Wert, klemmt das
                            // Steuerelement seinen `value` SELBST und bricht damit die Bindung - steigt sie wieder, bliebe die Anzeige
                            // stehen. Ein eigenes `Binding`-Element tut es NICHT: es feuert, bevor `to` steht (beides gemessen).
                            Slider {
                                id: wSlider
                                objectName: "manualWidthSlider"   // Griff für tests/bench
                                Layout.fillWidth: true
                                from: 80; to: root.maxAreaW
                                value: Math.min(App.manualAreaWidth, root.maxAreaW)
                                onMoved: App.setManualAreaWidth(Math.round(value))
                            }
                            SpinBox {
                                from: 80; to: root.maxAreaW; stepSize: 10
                                value: Math.min(App.manualAreaWidth, root.maxAreaW)
                                editable: true
                                textFromValue: function(v){ return v + " px" }
                                valueFromText: function(t){ return parseInt(t) }
                                onValueModified: App.setManualAreaWidth(value)
                            }
                        }

                    }

                    ViewSubHead { text: App.uiText(App.language, "SettingsViewTileSize") }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Label { text: App.uiText(App.language, "SettingsViewWidth"); color: App.themeTextPrimary }
                        SpinBox {
                            id: tileW
                            from: 40; to: App.maxTileWidth; stepSize: 8
                            // `Math.min` aus demselben Grund wie oben: schrumpft die Grenze unter den Wert, klemmt das Feld selbst und bricht
                            // die Bindung - ohne den Bezug auf die Grenze bliebe es stehen, auch wenn wieder Platz da ist.
                            value: Math.min(App.tileWidth, App.maxTileWidth)
                            editable: true
                            textFromValue: function(v){ return v + " px" }
                            valueFromText: function(t){ return parseInt(t) }
                            onValueModified: App.setTileSize(tileW.value, tileH.value)
                        }
                        Label { text: App.uiText(App.language, "SettingsViewHeight"); color: App.themeTextPrimary }
                        SpinBox {
                            id: tileH
                            from: 40; to: App.maxTileHeight; stepSize: 8
                            value: Math.min(App.tileHeight, App.maxTileHeight)
                            editable: true
                            textFromValue: function(v){ return v + " px" }
                            valueFromText: function(t){ return parseInt(t) }
                            onValueModified: App.setTileSize(tileW.value, tileH.value)
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: App.galleryListLayout

                    ViewSubHead { text: App.uiText(App.language, "SettingsViewListSize") }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: App.uiText(App.language, "SettingsViewListSizeHint")
                        color: App.themeTextMuted
                        font.pixelSize: 11
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Label {
                            text: App.uiText(App.language, "SettingsViewListHeight")
                            color: App.themeTextPrimary
                        }
                        SpinBox {
                            id: listRowH
                            from: 28; to: 160; stepSize: 4
                            value: App.listRowHeight
                            editable: true
                            textFromValue: function(v){ return v + " px" }
                            valueFromText: function(t){ return parseInt(t) }
                            onValueModified: App.setListRowHeight(value)
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            SettingsGroup {
                key: "view.text-preview"
                title: App.uiText(App.language, "SettingsViewPreviewGroup")
                Layout.fillWidth: true
                visible: !App.galleryListLayout

                CheckBox {
                    text: App.uiText(App.language, "SettingsViewTextPreview")
                    checked: App.textPreviewContent
                    onToggled: App.textPreviewContent = checked
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "SettingsViewTextPreviewHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            SettingsGroup {
                key: "view.extract-style"
                title: App.uiText(App.language, "SettingsViewExtractStyle")
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: App.uiText(App.language, "SettingsViewExtractStyleHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                }

                ButtonGroup { id: extractStyleGroup }

                Repeater {
                    model: [
                        { label: App.uiText(App.language, "ExtractStyleFrame"),   value: "frame" },
                        { label: App.uiText(App.language, "ExtractStyleOverlay"), value: "overlay" }
                    ]
                    delegate: RadioButton {
                        required property var modelData
                        text: modelData.label
                        checked: App.extractSelectStyle === modelData.value
                        ButtonGroup.group: extractStyleGroup
                        onToggled: if (checked) App.setExtractSelectStyle(modelData.value)
                        contentItem: Text {
                            text: parent.text; color: App.themeTextPrimary
                            leftPadding: parent.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            SettingsGroup {
                key: "view.extract-layout"
                title: App.uiText(App.language, "SettingsViewExtractLayout")
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: App.uiText(App.language, "SettingsViewExtractLayoutHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                }

                ButtonGroup { id: extractLayoutGroup }

                Repeater {
                    model: [
                        { label: App.uiText(App.language, "ExtractLayoutWorkbench"), value: "workbench" },
                        { label: App.uiText(App.language, "ExtractLayoutCompact"),   value: "compact" }
                    ]
                    delegate: RadioButton {
                        required property var modelData
                        text: modelData.label
                        checked: App.extractLayout === modelData.value
                        ButtonGroup.group: extractLayoutGroup
                        onToggled: if (checked) App.setExtractLayout(modelData.value)
                        contentItem: Text {
                            text: parent.text; color: App.themeTextPrimary
                            leftPadding: parent.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

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

    SmoothWheelArea { flickable: viewScroll.contentItem }
}
