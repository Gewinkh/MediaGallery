import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// Allgemein: Sprache + Video-Wiedergabe + Render-Backend
Item {
    id: root

    //  Zwischenüberschrift INNERHALB einer gebündelten Gruppe - dieselbe Form
    //  wie in `SettingsViewTab`.
    component GenSubHead: ColumnLayout {
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

    // Signal nach oben: Backend gespeichert -> Dialog schließen
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
        //  Griff fuer die Pruefstaende (s. `bench_shell de`).
        objectName: "generalScroll"
        id: genScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 16

            SettingsGroup {
                key: "general.menu-language"
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
                key: "general.menu-video-playback"
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

            SettingsGroup {
                key: "general.fullscreen-anim"
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

            SettingsGroup {
                key: "general.audio-player"
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

            // Mono-Play und Spulschritt in EINER Gruppe: beides betrifft dieselbe Sache - wie sich Ton und Video beim
            // Abspielen verhalten. Die früheren Gruppentitel leben als Zwischenüberschriften weiter.
            SettingsGroup {
                key: "general.playback"
                title: App.uiText(App.language, "SettingsGenMonoPlay")
                Layout.fillWidth: true

                CheckBox {
                    checked: App.monoPlay
                    onToggled: App.setMonoPlay(checked)
                    text: App.uiText(App.language, "SettingsGenMonoPlayLabel")
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Label {
                    text: App.uiText(App.language, "SettingsGenMonoPlayHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                GenSubHead { text: App.uiText(App.language, "SettingsGenSeekStep") }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        text: App.uiText(App.language, "SettingsGenSeekStepLabel")
                        color: App.themeTextPrimary
                    }
                    SpinBox {
                        id: seekStepSpin
                        from: 1; to: 600; stepSize: 5
                        value: App.videoSeekStep
                        editable: true
                        textFromValue: function(v) { return v + " s" }
                        valueFromText: function(t) { return parseInt(t) }
                        onValueModified: App.setVideoSeekStep(value)
                    }
                    Item { Layout.fillWidth: true }
                }
                Label {
                    text: App.uiText(App.language, "SettingsGenSeekStepHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // Ziehen auf einen Ordner: verschieben oder kopieren
            SettingsGroup {
                key: "general.drop-move"
                title: App.uiText(App.language, "SettingsDropMoveLabel")
                Layout.fillWidth: true

                CheckBox {
                    checked: App.fileDropMove
                    onToggled: App.fileDropMove = checked
                    text: App.uiText(App.language, "SettingsDropMoveLabel")
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Label {
                    text: App.uiText(App.language, "SettingsDropMoveDesc")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // Rechtschreibung ist PRÜFUNG, nicht Korrektur: markiert wird, ersetzt wird nur auf ausdrückliche Wahl im
            // Kontextmenü.
            SettingsGroup {
                key: "general.spell"
                title: App.uiText(App.language, "SettingsGenSpell")
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Switch {
                        id: spellSwitch
                        checked: App.spellCheck
                        enabled: App.spellLanguages().length > 0
                        text: App.uiText(App.language, "SettingsGenSpellLabel")
                        onToggled: App.setSpellCheck(checked)
                    }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    visible: App.spellLanguages().length > 0
                    Label {
                        text: App.uiText(App.language, "SettingsGenSpellLang")
                        color: App.themeTextPrimary
                    }
                    ComboBox {
                        id: spellLangBox
                        model: App.spellLanguages()
                        enabled: App.spellCheck
                        Component.onCompleted: {
                            const i = model.indexOf(App.spellLanguage)
                            currentIndex = i >= 0 ? i : 0
                        }
                        onActivated: App.setSpellLanguage(currentText)
                    }
                    Item { Layout.fillWidth: true }
                }
                Label {
                    //  Ohne Wörterbuch ist der Schalter wirkungslos - dann steht
                    //  hier, warum (dasselbe Muster wie bei fehlendem ZLIB).
                    text: App.spellLanguages().length > 0
                          ? App.uiText(App.language, "SettingsGenSpellHint")
                          : App.uiText(App.language, "SettingsGenSpellNone")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // Steht im ALLGEMEINEN Reiter, weil die Regel für JEDE Suche der App gilt, nicht nur für den Editor. Der
            // Abschnitt erklärt nur, er schaltet nichts - einen Regex-Schalter gibt es bewusst nicht.
            SettingsGroup {
                key: "general.search-regex"
                title: App.uiText(App.language, "SettingsSearchGroup")
                Layout.fillWidth: true

                // Zwei Spalten statt einer Textwand: das Muster links in fester Schrift, die Erklärung rechts. Als Raster
                // beginnen alle Erklärungen an derselben Stelle, egal wie lang das Muster daneben ist.
                component MusterTabelle: Rectangle {
                    id: tab
                    //  Zeilen der Form „Muster<Tab>Erklärung".
                    property string quelle: ""
                    property color textFarbe: App.themeTextPrimary
                    readonly property var zeilen:
                        tab.quelle.length > 0 ? tab.quelle.split("\n").map(z => z.split("\t"))
                                              : []

                    Layout.fillWidth: true
                    color: App.themeBackground
                    border.color: App.themeBorder
                    border.width: 1
                    radius: 6
                    implicitHeight: raster.implicitHeight + 20

                    GridLayout {
                        id: raster
                        anchors { left: parent.left; right: parent.right
                                  top: parent.top; margins: 10 }
                        columns: 3
                        columnSpacing: 10
                        rowSpacing: 4

                        Repeater {
                            model: tab.zeilen.length * 3
                            delegate: Item {
                                id: zelle
                                required property int index
                                readonly property int _zeile: Math.floor(zelle.index / 3)
                                readonly property int _spalte: zelle.index % 3

                                implicitWidth: zelle._spalte === 1 ? 1 : inhalt.implicitWidth
                                implicitHeight: Math.max(18, inhalt.implicitHeight)
                                Layout.fillWidth: zelle._spalte === 2
                                Layout.fillHeight: zelle._spalte === 1
                                Layout.alignment: Qt.AlignTop

                                //  Mittlere Spalte: der Trennstrich zwischen
                                //  dem Befehl und seiner Erklärung.
                                Rectangle {
                                    visible: zelle._spalte === 1
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 2
                                    width: 1
                                    height: Math.max(14, zelle.height - 4)
                                    color: App.themeBorder
                                }
                                Text {
                                    id: inhalt
                                    visible: zelle._spalte !== 1
                                    width: zelle._spalte === 2 ? zelle.width : implicitWidth
                                    text: zelle._spalte === 0
                                          ? tab.zeilen[zelle._zeile][0]
                                          : (tab.zeilen[zelle._zeile][1] || "")
                                    color: zelle._spalte === 0 ? App.themeAccent : tab.textFarbe
                                    font.family: zelle._spalte === 0 ? "monospace" : ""
                                    font.pixelSize: 12
                                    font.bold: zelle._spalte === 0
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }

                MusterTabelle {
                    Layout.topMargin: 4
                    quelle: App.uiText(App.language, "SettingsSearchTable")
                }
                MusterTabelle {
                    Layout.topMargin: 6
                    quelle: App.uiText(App.language, "SettingsSearchExamples")
                    textFarbe: App.themeTextMuted
                }
            }

            SettingsGroup {
                key: "general.render-backend"
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

            SettingsGroup {
                key: "general.shortcuts"
                title: App.uiText(App.language, "SettingsGenShortcuts")
                Layout.fillWidth: true

                //  Kein zusätzliches `width:` - die Breite kommt allein von der
                //  ColumnLayout (Layout.fillWidth); eine eigene Bindung würde mit
                //  ihr um dieselbe Eigenschaft konkurrieren.
                SettingsShortcutsView {
                    Layout.fillWidth: true
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    //  Weiches, schnelles Mausrad-Scrollen (Galerie-Muster) statt der festen
    //  60 px je Rastung von `Flickable`.
    SmoothWheelArea { flickable: genScroll.contentItem }
}
