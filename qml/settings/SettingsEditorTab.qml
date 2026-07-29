import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// ── Editor: Auto-Speichern ───────────────────────────────────────────────────
Item {
    id: root

    ScrollView {
        id: edScroll
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

                //  ZWEI GETRENNTE AUSWAHLGRUPPEN — zwingend nötig:
                //  `RadioButton` ist `autoExclusive` und gruppiert sich dann
                //  über das ELTERNELEMENT. SettingsGroup steckt aber ALLE
                //  Kinder in dieselbe innere ColumnLayout (`default property
                //  alias content: inner.data`), weshalb die vier Knöpfe dieser
                //  Gruppe eine einzige Auswahl bildeten: ein Klick auf
                //  „Seiten hinzufügen/entfernen" hob die Panel-Position wieder
                //  auf (und umgekehrt) — es ließ sich immer nur EINE der beiden
                //  Einstellungen zeigen. Eine explizite ButtonGroup je
                //  Sachbereich stellt die Exklusivität wieder korrekt her.
                //  (Die übrigen SettingsGroups haben je nur ein Auswahlpaar
                //  und brauchen das daher nicht.)
                ButtonGroup { id: panelPosGroup }
                ButtonGroup { id: pageEditGroup }
                ButtonGroup { id: exportModeGroup }

                // Position der Text-Eigenschaften: rechte Seitenleiste (Standard)
                // oder obere Leiste im Word-Stil (PdfEdit.panelOnTop, persistiert).
                Label {
                    text: App.uiText(App.language, "PdfEditPanelPosLabel")
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                }
                //  `text` MUSS gesetzt sein: der Stil richtet den Ring nur bei
                //  nicht-leerem `control.text` links aus, sonst zentriert er ihn
                //  in `availableWidth` (s. style/RadioButton.qml).
                RadioButton {
                    id: posRight
                    ButtonGroup.group: panelPosGroup
                    text: App.uiText(App.language, "PdfEditPanelPosRight")
                    checked: !PdfEdit.panelOnTop
                    onToggled: if (checked) PdfEdit.panelOnTop = false
                    contentItem: Text {
                        text: posRight.text
                        color: App.themeTextPrimary
                        leftPadding: posRight.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                RadioButton {
                    id: posTop
                    ButtonGroup.group: panelPosGroup
                    text: App.uiText(App.language, "PdfEditPanelPosTop")
                    checked: PdfEdit.panelOnTop
                    onToggled: if (checked) PdfEdit.panelOnTop = true
                    contentItem: Text {
                        text: posTop.text
                        color: App.themeTextPrimary
                        leftPadding: posTop.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Seiten hinzufügen/entfernen (Aufgabe 3): nicht-destruktiv
                // (Änderungen wirken beim Export, Original bleibt) vs. destruktiv
                // (Original-PDF wird sofort neu geschrieben; einmalige .mgorig-
                // Sicherung). PdfEdit.pageEditDestructive ist persistiert.
                Label {
                    text: App.uiText(App.language, "SettingsPdfPageEditLabel")
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                    topPadding: 6
                }
                RadioButton {
                    id: pageEditSafe
                    ButtonGroup.group: pageEditGroup
                    text: App.uiText(App.language, "PdfPageEditNonDestructive")
                    checked: !PdfEdit.pageEditDestructive
                    onToggled: if (checked) PdfEdit.pageEditDestructive = false
                    contentItem: Text {
                        text: pageEditSafe.text
                        color: App.themeTextPrimary
                        leftPadding: pageEditSafe.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                }
                RadioButton {
                    id: pageEditDestr
                    ButtonGroup.group: pageEditGroup
                    text: App.uiText(App.language, "PdfPageEditDestructiveMode")
                    checked: PdfEdit.pageEditDestructive
                    onToggled: if (checked) PdfEdit.pageEditDestructive = true
                    contentItem: Text {
                        text: pageEditDestr.text
                        color: App.themeTextPrimary
                        leftPadding: pageEditDestr.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                }
                // Export-Modus: steuert den EINEN „Export"-Knopf des PDF-Editors.
                // Früher gab es dafür zwei Knöpfe nebeneinander — die Wahl ist
                // aber eine Grundsatzentscheidung, keine je Export.
                Label {
                    text: App.uiText(App.language, "SettingsPdfExportLabel")
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                    topPadding: 6
                }
                RadioButton {
                    id: expLossless
                    ButtonGroup.group: exportModeGroup
                    text: App.uiText(App.language, "PdfExportLosslessMode")
                    checked: PdfEdit.exportLossless
                    onToggled: if (checked) PdfEdit.exportLossless = true
                    contentItem: Text {
                        text: expLossless.text
                        color: App.themeTextPrimary
                        leftPadding: expLossless.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                }
                RadioButton {
                    id: expRaster
                    ButtonGroup.group: exportModeGroup
                    text: App.uiText(App.language, "PdfExportRasterMode")
                    checked: !PdfEdit.exportLossless
                    onToggled: if (checked) PdfEdit.exportLossless = false
                    contentItem: Text {
                        text: expRaster.text
                        color: App.themeTextPrimary
                        leftPadding: expRaster.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                }
                //  Interchange: eigene Notizen als echte PDF-Annotationen
                //  schreiben. BEWUSST eine CheckBox und KEIN Radio: die Wahl
                //  ist unabhängig vom Export-Modus darüber (s. auch der Fehler,
                //  bei dem zwei Radio-Gruppen einander gelöscht haben).
                CheckBox {
                    id: expAsAnnots
                    text: App.uiText(App.language, "PdfExportAsAnnotationsOption")
                    checked: PdfEdit.exportAsAnnotations
                    onToggled: PdfEdit.exportAsAnnotations = checked
                    topPadding: 6
                    contentItem: Text {
                        text: expAsAnnots.text
                        color: App.themeTextPrimary
                        leftPadding: expAsAnnots.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                }
                Label {
                    text: App.uiText(App.language, "PdfExportAsAnnotationsHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: App.uiText(App.language, "SettingsPdfExportHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    leftPadding: 26
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Label {
                    text: App.uiText(App.language, "SettingsPdfPageEditHint")
                    //  themeTextMuted — die Hinweisfarbe aller übrigen
                    //  Erklärtexte (s. DOCX-Gruppe unten). Hier stand
                    //  `themeTextSecondary`: die Property gibt es in
                    //  AppController NICHT, die Bindung lieferte `undefined`
                    //  und der Text wurde schwarz gerendert.
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    leftPadding: 26
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // ── DOCX-Editor ───────────────────────────────────────────────────
            //  Speicherverhalten (Docx.saveDirect, persistiert): „Direkt
            //  speichern" schreibt auf die Originaldatei (einmalige .bak je
            //  Sitzung); „Kopie exportieren" lässt das Original unangetastet
            //  und erzeugt <Name>_edited(.n).docx.
            SettingsGroup {
                title: App.uiText(App.language, "SettingsDocxGroup")
                Layout.fillWidth: true

                Label {
                    text: App.uiText(App.language, "DocxSaveModeLabel")
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                }
                RadioButton {
                    id: docxDirect
                    text: App.uiText(App.language, "DocxSaveDirect")
                    checked: Docx.saveDirect
                    onToggled: if (checked) Docx.saveDirect = true
                    contentItem: Text {
                        text: docxDirect.text
                        color: App.themeTextPrimary
                        leftPadding: docxDirect.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Label {
                    text: App.uiText(App.language, "DocxSaveDirectHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    leftPadding: 26
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RadioButton {
                    id: docxCopy
                    text: App.uiText(App.language, "DocxSaveCopy")
                    checked: !Docx.saveDirect
                    onToggled: if (checked) Docx.saveDirect = false
                    contentItem: Text {
                        text: docxCopy.text
                        color: App.themeTextPrimary
                        leftPadding: docxCopy.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Label {
                    text: App.uiText(App.language, "DocxSaveCopyHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    leftPadding: 26
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
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

    //  Weiches, schnelles Mausrad-Scrollen (Galerie-Muster) statt der festen
    //  60 px je Rastung von `Flickable`.
    SmoothWheelArea { flickable: edScroll.contentItem }
}
