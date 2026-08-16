import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// ── Ansicht / Layout: Anordnung, manuelle Zone, Kachelgröße ──────────────────
Item {
    id: root

    // Referenz-Fenstergröße für die maßstabsgetreue Zonen-Vorschau
    //  Bezugsfenster der Vorschau = das AKTUELLE Fenster (2026-07-17). Vorher
    //  stand hier die INITIALE Fenstergröße — bei maximiertem Fenster rechnete
    //  die Vorschau dadurch mit einer viel zu schmalen Fläche und zeigte
    //  dauerhaft nur ~2 Kacheln, während die Galerie 6 anzeigte.
    readonly property int winW: Window.window ? Window.window.width
                                : (App.initialWindowWidth  > 0 ? App.initialWindowWidth  : 1280)
    readonly property int winH: Window.window ? Window.window.height
                                : (App.initialWindowHeight > 0 ? App.initialWindowHeight : 800)

    //  Obergrenze der manuellen Zonenbreite = Breite des Bildschirms, auf dem
    //  das Fenster liegt. Mehr lässt sich gar nicht darstellen: Die Galerie
    //  klemmt die Zone ohnehin auf die Fensterbreite (GalleryView.areaW), ein
    //  größerer Wert wäre also folgenlos — vorher ging der Regler bis 8000 px
    //  und der obere Teil seines Wegs tat schlicht nichts.
    readonly property int maxAreaW: Screen.width > 0 ? Screen.width : 3840

    ScrollView {
        id: viewScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 14

            // ── Sichtbarkeit der Begleitdateien ───────────────────────────────
            //  Standard AUS: die Ordner-Datei, die Editor-Notizen und die
            //  Sicherungskopien gehören zur Verwaltung, nicht zur Sammlung.
            SettingsGroup {
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
            }

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

                    //  Zonen-Vorschau — ORIGINALGETREU zur Galerie (2026-07-17):
                    //  Die Galerie zentriert die manuelle Zone, klemmt sie an
                    //  die Fensterbreite (−2×12 px Rand) und füllt sie mit
                    //  Kacheln (Zelle = Kachel + 8 px Abstand, Spalten =
                    //  ⌊Zone/Zelle⌋). Genau das zeigt die Vorschau jetzt im
                    //  Maßstab — inklusive der ECHTEN Kachelgröße (live an die
                    //  Werte der Kachelgröße-Gruppe unten gebunden). Wünscht
                    //  man mehr Breite, als ins Fenster passt, zeigt eine
                    //  gestrichelte Kontur den eingestellten (geklemmten)
                    //  Wunsch — vorher wirkte der Breitenregler dadurch „tot".
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

                            // Maßstab so, dass das Fenster komplett passt
                            readonly property real sf: Math.min(width / root.winW, height / root.winH)
                            readonly property real frameW: root.winW * previewArea.sf
                            readonly property real frameH: root.winH * previewArea.sf

                            // Fenster-Rahmen (zentriert im Vorschaufeld)
                            Rectangle {
                                id: winFrame
                                anchors.centerIn: parent
                                width: previewArea.frameW
                                height: previewArea.frameH
                                color: Qt.rgba(1, 1, 1, 0.03)
                                border.color: App.themeBorder
                                radius: 3

                                //  Galerie-Layoutmodell (Modell-Pixel, nicht
                                //  skaliert) — exakt die Formeln aus
                                //  GalleryView.qml.
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
                                //  Die Zone läuft IMMER über die volle Fenster-
                                //  höhe: Die Galerie kennt nur eine Breiten-
                                //  begrenzung (s. GalleryView.areaW) — eine
                                //  einstellbare Höhe hatte nie eine Wirkung und
                                //  ist daher entfallen (Nutzerbefund).
                                readonly property int  zoneHpx: root.winH
                                readonly property int  rows: Math.max(1, Math.floor(zoneHpx / cellH))

                                //  Gewünschte Zonenbreite als gestrichelte
                                //  Kontur, wenn sie über die Fensterklemme
                                //  hinausgeht (zentriert wie die Galerie).
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

                                //  Effektive Zone (zentriert, wie die Galerie
                                //  sie tatsächlich belegt) …
                                Rectangle {
                                    id: zoneRect
                                    //  Sichtabstand zwischen Zonenrahmen und
                                    //  Kacheln (NUR Darstellung): Der Rahmen lag
                                    //  vorher direkt an der ersten Kachel — die
                                    //  Zone war dadurch kaum als eigene Fläche
                                    //  zu erkennen. Rechts entsteht der Abstand
                                    //  ohnehin aus dem Kachel-Abstand (cellW−tW),
                                    //  deshalb wird die Breite hier auf den
                                    //  tatsächlich belegten Block gerechnet und
                                    //  beidseitig um `pad` erweitert.
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

                                    //  … gefüllt mit maßstabsgetreuen Kacheln
                                    //  (Zeilen×Spalten wie die Galerie; zur
                                    //  Sicherheit auf 400 Stück gedeckelt).
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
                                            //  Kachelfläche = Design ▸ Grundfarben ▸
                                            //  Hintergrund (Nutzer-Vorgabe).
                                            color: App.themeBackground
                                            border.color: App.themeBorder
                                            opacity: 0.95
                                        }
                                    }

                                    // Ost-Griff: zieht NUR die Breite (die Höhe
                                    // der Zone ist nicht einstellbar, s. o.).
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

                    // Breite
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label { text: App.uiText(App.language, "SettingsViewWidth"); color: App.themeTextPrimary; Layout.preferredWidth: 70 }
                        Slider {
                            id: wSlider
                            Layout.fillWidth: true
                            from: 80; to: root.maxAreaW
                            value: App.manualAreaWidth
                            onMoved: App.setManualAreaWidth(Math.round(value))
                        }
                        SpinBox {
                            from: 80; to: root.maxAreaW; stepSize: 10
                            value: App.manualAreaWidth
                            editable: true
                            textFromValue: function(v){ return v + " px" }
                            valueFromText: function(t){ return parseInt(t) }
                            onValueModified: App.setManualAreaWidth(value)
                        }
                    }

                    //  KEIN Höhenregler: Die Galerie klemmt in der manuellen
                    //  Anordnung ausschließlich die BREITE der Kachelzone
                    //  (GalleryView.areaW); die Höhe ergibt sich aus der
                    //  Kachelzahl. Der frühere Regler war folgenlos.
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
                        // Obergrenze = darstellbare Galeriefläche (App.setTileSize
                        // klemmt zusätzlich serverseitig dagegen).
                        from: 40; to: App.maxTileWidth; stepSize: 8
                        value: App.tileWidth
                        editable: true
                        textFromValue: function(v){ return v + " px" }
                        valueFromText: function(t){ return parseInt(t) }
                    }
                    Label { text: App.uiText(App.language, "SettingsViewHeight"); color: App.themeTextPrimary }
                    SpinBox {
                        id: tileH
                        from: 40; to: App.maxTileHeight; stepSize: 8
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

            // ── PDF-Extraktion: Auswahl-Darstellung ───────────────────────────
            SettingsGroup {
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

            // ── PDF-Extraktion: Dialog-Layout (Werkbank / kompakt) ────────────
            SettingsGroup {
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

    //  Weiches, schnelles Mausrad-Scrollen (Galerie-Muster) statt der festen
    //  60 px je Rastung von `Flickable`.
    SmoothWheelArea { flickable: viewScroll.contentItem }
}
