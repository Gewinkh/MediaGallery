import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

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

            // Hier steht, was KEINE Farbe ist. Die Farben des Editors haben einen eigenen Block im Design-Reiter, mit
            // eigenen Profilen getrennt vom Oberflächen-Theme.
            SettingsGroup {
                key: "editor.view"
                title: App.uiText(App.language, "SettingsEditorViewGroup")
                Layout.fillWidth: true

                component EdCheck: CheckBox {
                    contentItem: Text {
                        text: parent.text; color: App.themeTextPrimary
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                EdCheck {
                    text: App.uiText(App.language, "SettingsEditorLineNumbers")
                    checked: Editor.lineNumbers
                    onToggled: Editor.lineNumbers = checked
                }
                EdCheck {
                    text: App.uiText(App.language, "SettingsEditorCurrentLine")
                    checked: Editor.highlightCurrentLine
                    onToggled: Editor.highlightCurrentLine = checked
                }
                EdCheck {
                    text: App.uiText(App.language, "EditorMinimap")
                    checked: Editor.minimap
                    onToggled: Editor.minimap = checked
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "EditorMinimapHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                EdCheck {
                    text: App.uiText(App.language, "EditorFolding")
                    checked: Editor.folding
                    onToggled: Editor.folding = checked
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "EditorFoldingHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                EdCheck {
                    text: App.uiText(App.language, "EditorIndentGuides")
                    checked: Editor.indentGuides
                    onToggled: Editor.indentGuides = checked
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "EditorIndentGuidesHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                EdCheck {
                    text: App.uiText(App.language, "EditorMatchBrackets")
                    checked: Editor.matchBrackets
                    onToggled: Editor.matchBrackets = checked
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "EditorMatchBracketsHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                EdCheck {
                    id: wrapChk
                    text: App.uiText(App.language, "SettingsEditorSoftWrap")
                    checked: Editor.softWrap
                    onToggled: Editor.softWrap = checked
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "SettingsEditorSoftWrapHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                //  Tabulator
                //  Zwei getrennte Fragen: die BREITE ist reine Anzeige, die
                //  TASTE ändert den Dateiinhalt.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 10
                    Label {
                        text: App.uiText(App.language, "SettingsEditorTabWidth")
                        color: App.themeTextPrimary
                    }
                    SpinBox {
                        from: 2; to: 8; stepSize: 1
                        value: Editor.tabWidth
                        editable: true
                        onValueModified: Editor.tabWidth = value
                    }
                    Item { Layout.fillWidth: true }
                }

                EdCheck {
                    text: App.uiText(App.language, "SettingsEditorTabSpaces")
                    checked: Editor.tabSpaces
                    onToggled: Editor.tabSpaces = checked
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    text: App.uiText(App.language, "SettingsEditorTabSpacesHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            SettingsGroup {
                key: "editor.auto-save"
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

                //  Vorgabe-Schriftfarbe für „Text -> PDF". Steht bewusst hier und
                //  nicht im Design-Tab: sie färbt ein DOKUMENT, nicht die
                //  Oberfläche, und darf einem Themenwechsel deshalb nicht folgen.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 10

                    ColumnLayout {
                        spacing: 2
                        Label {
                            text: App.uiText(App.language, "TextPdfColorSetting")
                            color: App.themeTextPrimary
                        }
                        Label {
                            text: App.uiText(App.language, "TextPdfColorSettingHint")
                            color: App.themeTextMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.maximumWidth: 420
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ColorPicker {
                        implicitWidth: 44; implicitHeight: 24
                        showAlpha: false
                        title: App.uiText(App.language, "TextPdfColorTitle")
                        selectedColor: App.textPdfColor
                        onColorPicked: function (c) {
                            App.textPdfColor = c
                            selectedColor = Qt.binding(function () { return App.textPdfColor })
                        }
                    }
                }
            }

            // Export erzeugt IMMER eine Kopie "..._bearbeitet(.n).pdf" - die frühere Überschreib-Option ist entfallen,
            // Original und Sidecar bleiben unangetastet. Hier steht nur noch die Panel-Position.
            SettingsGroup {
                key: "editor.pdf-edit"
                title: App.uiText(App.language, "SettingsPdfEditGroup")
                Layout.fillWidth: true

                // ZWEI getrennte ButtonGroups sind zwingend: `RadioButton` ist autoExclusive und gruppiert über das
                // ELTERNELEMENT, und SettingsGroup steckt alle Kinder in dieselbe innere ColumnLayout - die vier Knöpfe
                // bildeten dadurch EINE Auswahl, ein Klick auf den einen hob den anderen wieder auf.
                ButtonGroup { id: panelPosGroup }
                ButtonGroup { id: exportModeGroup }

                Label {
                    text: App.uiText(App.language, "PdfEditPanelPosLabel")
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                }
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

                Label {
                    text: App.uiText(App.language, "SettingsPdfPageEditLabel")
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                    topPadding: 6
                }
                Label {
                    text: App.uiText(App.language, "SettingsPdfPageEditHint")
                    color: App.themeTextMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
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
                // BEWUSST eine CheckBox und kein Radio: die Wahl ist unabhängig vom Export-Modus darüber - zwei Radio-Gruppen
                // in derselben SettingsGroup löschten einander.
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
            }

            // `Docx.saveDirect`: "Direkt speichern" schreibt auf die Originaldatei (einmalige .bak je Sitzung), "Kopie
            // exportieren" lässt das Original unangetastet und erzeugt <Name>_edited(.n).docx.
            SettingsGroup {
                key: "editor.docx"
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

            // Schema-Auswahl plus editierbare Zuordnungsliste; die Liste liest `Translit.mappings(scheme)` rev-getrieben
            // (`mappingsRev`) - kein eigenes Modell-Handling nötig.
            SettingsGroup {
                id: trGroup
                // OHNE `key` merkt sich die Gruppe ihren Zustand NICHT und stand nach jedem Öffnen wieder offen. Der
                // Schlüssel ist ein STABILER Bezeichner, nie die Überschrift.
                key: "editor.translit"
                title: App.uiText(App.language, "SettingsTranslitGroup")
                Layout.fillWidth: true

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
                        DrawnIcon { name: "arrow-right"; size: 13
                                    color: App.themeTextMuted
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
                    DrawnIcon { name: "arrow-right"; size: 13
                                color: App.themeTextMuted
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

    SmoothWheelArea { flickable: edScroll.contentItem }
}
