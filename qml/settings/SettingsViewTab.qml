import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// ── Ansicht / Layout: Anordnung, manuelle Zone, Kachelgröße ──────────────────
Item {
    id: root

    //  Zwischenüberschrift INNERHALB einer gebündelten Gruppe: eine feine Linie
    //  plus der frühere Gruppentitel. Damit bleibt beim Zusammenlegen lesbar,
    //  wo ein Thema aufhört und das nächste anfängt.
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

    // Referenz-Fenstergröße für die maßstabsgetreue Zonen-Vorschau
    //  Bezugsfenster der Vorschau = das AKTUELLE Fenster (2026-07-17). Vorher
    //  stand hier die INITIALE Fenstergröße - bei maximiertem Fenster rechnete
    //  die Vorschau dadurch mit einer viel zu schmalen Fläche und zeigte
    //  dauerhaft nur ~2 Kacheln, während die Galerie 6 anzeigte.
    readonly property int winW: Window.window ? Window.window.width
                                : (App.initialWindowWidth  > 0 ? App.initialWindowWidth  : 1280)
    readonly property int winH: Window.window ? Window.window.height
                                : (App.initialWindowHeight > 0 ? App.initialWindowHeight : 800)

    //  Obergrenze der manuellen Zonenbreite = Breite des Bildschirms, auf dem
    //  das Fenster liegt. Mehr lässt sich gar nicht darstellen: Die Galerie
    //  klemmt die Zone ohnehin auf die Fensterbreite (GalleryView.areaW), ein
    //  größerer Wert wäre also folgenlos - vorher ging der Regler bis 8000 px
    //  und der obere Teil seines Wegs tat schlicht nichts.
    //  Obergrenze der manuellen Kachelbreite = Breite des Bildschirms, auf dem
    //  das FENSTER steht - gemeldet von der `ApplicationShell` (s. dort).
    //  Früher hing das am angehängten `Screen` dieses Reiters; der sitzt aber im
    //  Einstellungen-Dialog, also in einem Popup, und meldete dort den PRIMÄREN
    //  Bildschirm. Beim Wechsel auf einen größeren Monitor blieb die Grenze
    //  deshalb auf dem kleineren stehen (vom Nutzer gemeldet, nachgemessen).
    readonly property int maxAreaW: App.screenWidth > 0 ? App.screenWidth : 3840

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

                //  Steht bewusst NEBEN „Alle Dateien anzeigen": beide beantworten
                //  dieselbe Frage - welche Dateien sind ueberhaupt zu sehen -, und
                //  sie werden leicht verwechselt. „Alle Dateien" meint UNBEKANNTE
                //  Typen, dieser hier den Punkt am Namensanfang.
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

            // ── Ansicht: Anordnung, Kachel-Anordnung und Kachelgröße ─────────────────────
            //  EINE Gruppe statt dreier (Festlegung des Nutzers 2026-09-01): es ist
            //  dieselbe Frage in drei Stufen - wie die Galerie ihre Einträge
            //  anordnet, wie die Kacheln darin stehen und wie groß sie sind.
            //  Die früheren Gruppentitel leben als Zwischenüberschriften weiter,
            //  sonst wäre der Kasten eine Wand aus Reglern.
            //
            //  ZWEI `ButtonGroup`s in EINER Gruppe: `RadioButton` ist
            //  `autoExclusive` und gruppiert sich sonst über das ELTERNELEMENT -
            //  `SettingsGroup` steckt alle Kinder in dieselbe innere
            //  `ColumnLayout`. Ohne sie bildeten Kacheln/Liste UND die vier
            //  Anordnungs-Knöpfe EINE Auswahl (s. Structure.md ▸ Settings).
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

                //  Eigene `ButtonGroup`, zwingend: `RadioButton` ist
                //  `autoExclusive` und gruppiert sich sonst über das
                //  ELTERNELEMENT - `SettingsGroup` steckt alle Kinder in
                //  dieselbe innere `ColumnLayout` (s. Structure.md ▸ Settings).
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

                //  ── Was hier steht, hängt von der GEWÄHLTEN Anordnung ab ──
                //  Kachel-Anordnung und Kachelgröße gelten nur für Kacheln, die
                //  Zeilenhöhe nur für die Liste. Beides gleichzeitig zu zeigen
                //  hiesse, die Hälfte der Regler wirkungslos danebenzustellen -
                //  der Nutzer hat ausdrücklich das Umschalten gewollt.
                //  Massgeblich ist die EINSTELLUNG (`App.galleryListLayout`),
                //  nicht der Player-Modus einer Hälfte: der ist vorübergehend
                //  und gehört einer Hälfte, dieser Reiter gilt der ganzen App.
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

                    // ── Manuelles Unterpanel ──────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        spacing: 10
                        visible: App.tileArrangement === 3

                        //  Zonen-Vorschau - ORIGINALGETREU zur Galerie (2026-07-17):
                        //  Die Galerie zentriert die manuelle Zone, klemmt sie an
                        //  die Fensterbreite (−2×12 px Rand) und füllt sie mit
                        //  Kacheln (Zelle = Kachel + 8 px Abstand, Spalten =
                        //  ⌊Zone/Zelle⌋). Genau das zeigt die Vorschau jetzt im
                        //  Maßstab - inklusive der ECHTEN Kachelgröße (live an die
                        //  Werte der Kachelgröße-Gruppe unten gebunden). Wünscht
                        //  man mehr Breite, als ins Fenster passt, zeigt eine
                        //  gestrichelte Kontur den eingestellten (geklemmten)
                        //  Wunsch - vorher wirkte der Breitenregler dadurch „tot".
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
                                    //  skaliert) - exakt die Formeln aus
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
                                    //  begrenzung (s. GalleryView.areaW) - eine
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
                                        //  vorher direkt an der ersten Kachel - die
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
                            //  **`Math.min(…, maxAreaW)` ist Pflicht, nicht Zierde.**
                            //  Faellt die Obergrenze unter den gespeicherten Wert
                            //  (Wechsel 1920 -> 1536), klemmt das Steuerelement
                            //  seinen `value` SELBST - und bricht damit die
                            //  Bindung. Steigt die Grenze wieder (1536 -> 1920),
                            //  bliebe die Anzeige auf 1536 stehen, obwohl rechts
                            //  wieder Platz ist und der gespeicherte Wert nie
                            //  angeruehrt wurde (vom Nutzer gemeldet, am
                            //  Pruefstand nachgestellt). Haengt der Ausdruck
                            //  zusaetzlich an `maxAreaW`, wird er beim Wachsen
                            //  neu ausgewertet und holt den Wert zurueck.
                            //  Ein eigenes `Binding`-Element tut es NICHT - es
                            //  feuert, bevor `to` steht, und wird dann selbst
                            //  geklemmt (beides gemessen).
                            //  Der GESPEICHERTE Wert bleibt unberuehrt: geschrieben
                            //  wird nur in `onMoved`/`onValueModified`, also nur,
                            //  wenn der Nutzer wirklich zieht oder tippt.
                            Slider {
                                id: wSlider
                                objectName: "manualWidthSlider"   // Griff für tests/bench (Regel 31)
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

                        //  KEIN Höhenregler: Die Galerie klemmt in der manuellen
                        //  Anordnung ausschließlich die BREITE der Kachelzone
                        //  (GalleryView.areaW); die Höhe ergibt sich aus der
                        //  Kachelzahl. Der frühere Regler war folgenlos.
                    }

                    ViewSubHead { text: App.uiText(App.language, "SettingsViewTileSize") }

                    //  SOFORT wirksam, ohne „Anwenden" - wie die Zeilenhöhe
                    //  darunter (Festlegung des Nutzers 2026-09-01). Der Knopf
                    //  war ein zweiter Handgriff für etwas, das man ohnehin
                    //  sieht, während man es einstellt; `Strg` + Rad in der
                    //  Galerie tut seit jeher genau dasselbe ohne Bestätigung.
                    //  Beide Felder schicken BEIDE Werte: `setTileSize` nimmt
                    //  Breite und Höhe zusammen und meldet die Änderung einmal.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Label { text: App.uiText(App.language, "SettingsViewWidth"); color: App.themeTextPrimary }
                        SpinBox {
                            id: tileW
                            // Obergrenze = darstellbare Galeriefläche (App.setTileSize
                            // klemmt zusätzlich serverseitig dagegen).
                            from: 40; to: App.maxTileWidth; stepSize: 8
                            //  `Math.min` aus demselben Grund wie bei der
                            //  manuellen Breite oben: schrumpft die Grenze unter
                            //  den Wert (kleineres Fenster, kleinerer Monitor),
                            //  klemmt das Feld selbst und bricht die Bindung -
                            //  ohne den Bezug auf die Grenze bliebe es danach
                            //  stehen, auch wenn wieder Platz da ist.
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
                            //  Grenzen wie in `ISettings` - dort wird beim Lesen
                            //  UND Schreiben geklemmt, hier stehen sie nur, damit
                            //  das Feld gar nichts Unbrauchbares anbietet.
                            from: 28; to: 160; stepSize: 4
                            value: App.listRowHeight
                            editable: true
                            textFromValue: function(v){ return v + " px" }
                            valueFromText: function(t){ return parseInt(t) }
                            //  Sofort wirksam, ohne „Anwenden": anders als bei der
                            //  Kachelgröße gibt es hier nur EINEN Wert, und man
                            //  sieht das Ergebnis in derselben Sekunde.
                            onValueModified: App.setListRowHeight(value)
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ── PDF-Extraktion: Auswahl-Darstellung ───────────────────────────
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

            // ── PDF-Extraktion: Dialog-Layout (Werkbank / kompakt) ────────────
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
