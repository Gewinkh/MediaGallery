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

            // ── PDF-Editor ────────────────────────────────────────────────────
            //  Export erzeugt IMMER eine Kopie „…_bearbeitet(.n).pdf" — die
            //  frühere Überschreib-Option wurde entfernt (Original + Sidecar
            //  bleiben unangetastet, Notizen bleiben reversibel). Hier nur
            //  noch die Panel-Position.
            SettingsGroup {
                title: App.uiText(App.language, "SettingsPdfEditGroup")
                Layout.fillWidth: true

                // Position der Text-Eigenschaften: rechte Seitenleiste (Standard)
                // oder obere Leiste im Word-Stil (PdfEdit.panelOnTop, persistiert).
                Label {
                    text: App.uiText(App.language, "PdfEditPanelPosLabel")
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                }
                RadioButton {
                    id: posRight
                    checked: !PdfEdit.panelOnTop
                    onToggled: if (checked) PdfEdit.panelOnTop = false
                    contentItem: Text {
                        text: App.uiText(App.language, "PdfEditPanelPosRight")
                        color: App.themeTextPrimary
                        leftPadding: posRight.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    id: posTop
                    checked: PdfEdit.panelOnTop
                    onToggled: if (checked) PdfEdit.panelOnTop = true
                    contentItem: Text {
                        text: App.uiText(App.language, "PdfEditPanelPosTop")
                        color: App.themeTextPrimary
                        leftPadding: posTop.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // ── Live-Transliteration (Latein → Arabisch/Kana) ─────────────────
            //  Schema-Auswahl + editierbare Zuordnungsliste. Die Liste liest
            //  Translit.mappings(scheme) rev-getrieben (mappingsRev bumpt bei
            //  jeder Änderung) — kein manuelles Modell-Handling nötig.
            SettingsGroup {
                id: trGroup
                title: App.uiText(App.language, "SettingsTranslitGroup")
                Layout.fillWidth: true

                // Welches Schema wird bearbeitet — initial das aktive, danach
                // frei wählbar (Änderung im Editor überschreibt die Auswahl
                // hier NICHT). Component.onCompleted entkoppelt die Bindung.
                property string editScheme: "ar"
                Component.onCompleted: editScheme = Translit.scheme

                Label {
                    text: App.uiText(App.language, "TranslitHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label {
                        text: App.uiText(App.language, "TranslitSchemeLabel")
                        color: App.themeTextPrimary
                    }
                    ComboBox {
                        id: schemeCombo
                        Layout.preferredWidth: 220
                        model: [
                            App.uiText(App.language, "TranslitArabic"),
                            App.uiText(App.language, "TranslitHiragana"),
                            App.uiText(App.language, "TranslitKatakana")
                        ]
                        readonly property var ids: ["ar", "ja-hira", "ja-kata"]
                        currentIndex: Math.max(0, ids.indexOf(trGroup.editScheme))
                        onActivated: trGroup.editScheme = ids[currentIndex]
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        Layout.preferredWidth: resetLbl.implicitWidth + 22
                        Layout.preferredHeight: 30
                        radius: 6
                        color: resetHover.hovered
                               ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
                               : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07)
                        border.color: App.themeBorder; border.width: 1
                        Text { id: resetLbl; anchors.centerIn: parent
                               text: App.uiText(App.language, "TranslitResetBtn")
                               color: App.themeTextPrimary; font.pixelSize: 11 }
                        HoverHandler { id: resetHover }
                        TapHandler { onTapped: Translit.resetScheme(trGroup.editScheme) }
                    }
                }

                // Zuordnungsliste (rev-getrieben). Jede Zeile: Key + Wert
                // editierbar (updateMapping) und ✕ (removeMapping).
                Repeater {
                    model: (Translit.mappingsRev,
                            Translit.mappings(trGroup.editScheme))
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8

                        readonly property string curScheme: trGroup.editScheme

                        TextField {
                            id: keyField
                            Layout.preferredWidth: 150
                            text: modelData.key
                            color: App.themeTextPrimary
                            font.family: "monospace"
                            background: Rectangle {
                                color: App.themeCard; radius: 5
                                border.color: App.themeBorder
                            }
                            onEditingFinished: {
                                if (text !== modelData.key || valField.text !== modelData.value)
                                    Translit.updateMapping(curScheme, modelData.key,
                                                           text, valField.text)
                            }
                        }
                        Text { text: "\u2192"; color: App.themeTextMuted
                               Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: valField
                            Layout.fillWidth: true
                            text: modelData.value
                            color: App.themeTextPrimary
                            background: Rectangle {
                                color: App.themeCard; radius: 5
                                border.color: App.themeBorder
                            }
                            onEditingFinished: {
                                if (text !== modelData.value || keyField.text !== modelData.key)
                                    Translit.updateMapping(curScheme, modelData.key,
                                                           keyField.text, text)
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 28; Layout.preferredHeight: 28
                            radius: 6
                            color: rmHover.hovered ? Qt.rgba(0.88, 0.35, 0.35, 0.28)
                                                   : Qt.rgba(0.88, 0.35, 0.35, 0.12)
                            border.color: "#c25a5a"; border.width: 1
                            Text { anchors.centerIn: parent; text: "\u2715"
                                   color: "#e08080"; font.pixelSize: 12 }
                            HoverHandler { id: rmHover }
                            TapHandler {
                                onTapped: Translit.removeMapping(curScheme, modelData.key)
                            }
                        }
                    }
                }

                // Neue Zuordnung hinzufügen.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    TextField {
                        id: newKey
                        Layout.preferredWidth: 150
                        placeholderText: App.uiText(App.language, "TranslitKeyPlaceholder")
                        color: App.themeTextPrimary
                        font.family: "monospace"
                        background: Rectangle {
                            color: App.themeCard; radius: 5
                            border.color: App.themeBorder
                        }
                    }
                    Text { text: "\u2192"; color: App.themeTextMuted
                           Layout.alignment: Qt.AlignVCenter }
                    TextField {
                        id: newVal
                        Layout.fillWidth: true
                        placeholderText: App.uiText(App.language, "TranslitValuePlaceholder")
                        color: App.themeTextPrimary
                        background: Rectangle {
                            color: App.themeCard; radius: 5
                            border.color: App.themeBorder
                        }
                    }
                    Rectangle {
                        Layout.preferredWidth: addLbl.implicitWidth + 20
                        Layout.preferredHeight: 30
                        radius: 6
                        enabled: newKey.text.length > 0 && newVal.text.length > 0
                        opacity: enabled ? 1.0 : 0.5
                        color: addHover.hovered
                               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
                               : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.16)
                        border.color: App.themeAccent; border.width: 1
                        Text { id: addLbl; anchors.centerIn: parent
                               text: App.uiText(App.language, "TranslitAddBtn")
                               color: App.themeAccent; font.pixelSize: 11 }
                        HoverHandler { id: addHover }
                        TapHandler {
                            enabled: newKey.text.length > 0 && newVal.text.length > 0
                            onTapped: {
                                if (Translit.addMapping(trGroup.editScheme,
                                                        newKey.text, newVal.text)) {
                                    newKey.text = ""
                                    newVal.text = ""
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
