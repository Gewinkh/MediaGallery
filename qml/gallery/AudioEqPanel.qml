pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  AudioEqPanel.qml - die zehn Regler, die Vorverstärkung und die
//  Voreinstellungen.
//
//  Bewusst ein schlichtes `Item`, KEIN Popup: dieselbe Datei dient beiden
//  Stellen - als Inhalt des Popups an der Player-Leiste und eingebettet in
//  Einstellungen ▸ Audio. Zwei Fassungen derselben Regler liefen sonst
//  auseinander.
//
//  Alle Werte kommen aus `Audio` und gehen sofort dorthin zurück; das Panel
//  hält keinen eigenen Zustand.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: panel

    //  ── Klick-Sperre ────────────────────────────────────────────────────────
    //  ZUERST im Baum, liegt damit UNTER allen Bedienelementen: die nehmen ihre
    //  Klicks weiterhin selbst, alles Uebrige schluckt diese Flaeche. Ohne sie
    //  fiel ein Klick ins Panel auf die Kachel dahinter durch und wechselte den
    //  Titel (vom Nutzer gemeldet: „klickt man automatisch durch den Button
    //  durch und wechselt auch die Audio").
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function(w) { w.accepted = true }
    }
    objectName: "audioEqPanel"        // Griff für tests/bench (Regel 31)

    //  VERWALTEN (löschen, zurücksetzen, Reihenfolge) gibt es nur in den
    //  EINSTELLUNGEN - Festlegung des Nutzers. Am Player wählt man eine
    //  Voreinstellung und sichert höchstens eine neue; alles Aufräumen wäre
    //  dort Ballast und im Eifer schnell versehentlich getroffen.
    //  Bewusst Pfeilknöpfe für die Reihenfolge und kein Ziehen wie bei den
    //  Lesezeichen: die Liste ist kurz, und ein Zug in einer 24-px-Zeile
    //  innerhalb eines scrollenden Einstellungs-Blatts trifft man schlecht.
    property bool manageable: false

    //  Die Breite kommt AUS dem Inhalt: die Zeile mit Namensfeld und den beiden
    //  Preset-Knöpfen ist breiter als die zehn Regler, und bei fester Breite
    //  stand der letzte Knopf außerhalb des Panels (Nutzerbefund).
    //  Untergrenze 300 px: unter der Breite bricht der Inhalt um (s. `Flow`),
    //  statt aus dem Fenster zu laufen - in einer schmalen Hälfte stand das
    //  Popup sonst über dem Rand (Nutzerbild `tests/miniEQ.png`).
    implicitWidth: Math.max(300, Math.min(520, presetRow.implicitWidth))
    implicitHeight: body.implicitHeight

    Column {
        id: body
        width: panel.width
        spacing: 10

        // ── Kopfzeile: an/aus, Voreinstellung, alles auf null ───────────────
        //  `Flow` statt `Row`: bei schmalem Panel rutschen die Knöpfe in die
        //  nächste Zeile, statt rechts hinauszulaufen.
        Flow {
            width: parent.width
            spacing: 10

            CheckBox {
                text: App.uiText(App.language, "AudioEqOn")
                checked: Audio.eqEnabled
                onToggled: Audio.eqEnabled = checked
                contentItem: Text {
                    text: parent.text; color: App.themeTextPrimary
                    leftPadding: parent.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: App.uiText(App.language, "AudioEqReset")
                onClicked: Audio.resetBands()
            }
        }

        // ── Die zehn Bänder ─────────────────────────────────────────────────
        Row {
            id: bandRow
            width: parent.width
            spacing: 2

            Repeater {
                model: Audio.eqFrequencies
                delegate: Column {
                    id: bandCol
                    required property int index
                    required property var modelData
                    width: (bandRow.width - 9 * bandRow.spacing) / 10
                    spacing: 4

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        //  31 … 16k - vierstellig aufwärts abgekürzt, sonst
                        //  passt die Beschriftung nicht unter den Regler.
                        text: {
                            const f = bandCol.modelData
                            return f >= 1000 ? (Math.round(f / 100) / 10) + "k"
                                             : String(Math.round(f))
                        }
                        color: App.themeTextMuted
                        font.pixelSize: 10
                    }

                    //  Eigene Optik statt des Stock-Reglers: der Standard bringt
                    //  ein fremdes Blau mit und passt in kein Theme.
                    Slider {
                        id: bandSlider
                        anchors.horizontalCenter: parent.horizontalCenter
                        orientation: Qt.Vertical
                        implicitWidth: 20
                        height: 120
                        from: -12; to: 12; stepSize: 0.5
                        value: Audio.eqGains[bandCol.index]
                        onMoved: Audio.setBandGain(bandCol.index, value)

                        background: Rectangle {
                            x: bandSlider.width / 2 - 2
                            width: 4; height: bandSlider.height; radius: 2
                            color: Qt.rgba(1, 1, 1, 0.14)
                            //  Der Ausschlag geht von der MITTE aus (0 dB) -
                            //  eine Füllung von unten läge bei jedem Regler an.
                            Rectangle {
                                width: parent.width; radius: parent.radius
                                color: App.themeAccent
                                y: Math.min(parent.height / 2,
                                            (1 - (bandSlider.value + 12) / 24) * parent.height)
                                height: Math.abs(bandSlider.value) / 24 * parent.height
                            }
                        }
                        handle: Rectangle {
                            x: bandSlider.width / 2 - width / 2
                            y: bandSlider.visualPosition * (bandSlider.height - height)
                            width: 14; height: 14; radius: 7
                            color: bandSlider.pressed ? App.themeAccent : App.themeTextPrimary
                            border.color: App.themeBorder
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (bandSlider.value > 0 ? "+" : "") + bandSlider.value.toFixed(1)
                        color: Math.abs(bandSlider.value) < 0.05 ? App.themeTextMuted
                                                                 : App.themeAccent
                        font.pixelSize: 10
                    }
                }
            }
        }

        // ── Vorverstärkung ──────────────────────────────────────────────────
        Flow {
            width: parent.width
            spacing: 8

            Text {
                height: 20                     // gleiche Höhe wie der Regler
                verticalAlignment: Text.AlignVCenter
                text: App.uiText(App.language, "AudioEqPreamp")
                color: App.themeTextPrimary
                font.pixelSize: 12
            }
            Slider {
                id: preampSlider
                width: 160
                height: 20
                //  Nach unten weiter als nach oben: die Gegenrechnung gegen das
                //  Übersteuern braucht bei mehreren angehobenen Bändern bis zu
                //  18 dB Luft (gemessen), +12 dB nach oben bleibt für alle, die
                //  bewusst übersteuern wollen.
                from: -24; to: 12; stepSize: 0.5
                value: Audio.eqPreamp
                onMoved: Audio.eqPreamp = value

                //  Null liegt jetzt bei zwei Dritteln, nicht in der Mitte.
                readonly property real _zeroX: 24 / 36

                background: Rectangle {
                    y: preampSlider.height / 2 - 2
                    width: preampSlider.width; height: 4; radius: 2
                    color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        height: parent.height; radius: parent.radius
                        color: App.themeAccent
                        x: Math.min(preampSlider._zeroX * parent.width,
                                    (preampSlider.value + 24) / 36 * parent.width)
                        width: Math.abs(preampSlider.value) / 36 * parent.width
                    }
                }
                handle: Rectangle {
                    x: preampSlider.visualPosition * (preampSlider.width - width)
                    y: preampSlider.height / 2 - height / 2
                    width: 14; height: 14; radius: 7
                    color: preampSlider.pressed ? App.themeAccent : App.themeTextPrimary
                    border.color: App.themeBorder
                }
            }
            Text {
                height: 20
                verticalAlignment: Text.AlignVCenter
                text: (preampSlider.value > 0 ? "+" : "") + preampSlider.value.toFixed(1) + " dB"
                color: App.themeTextMuted
                font.pixelSize: 11
            }
        }

        // ── Voreinstellungen als LISTE ──────────────────────────────────────
        //  Statt eines Auswahlfeldes: nur so sieht man auf einen Blick, was es
        //  gibt, welche gerade gilt und welche mitgelieferte verändert wurde.
        //  Ein Klick wählt, das Disketten-Zeichen sichert die aktuellen Regler
        //  AUF DIESE Zeile (überschreibt also auch eine mitgelieferte), das
        //  Rückgängig-Zeichen holt eine mitgelieferte Vorlage zurück, das X
        //  entfernt den Eintrag - der Klang bleibt dabei stehen.
        Text {
            text: App.uiText(App.language, "AudioEqPreset")
            color: App.themeTextMuted
            font.pixelSize: 11
        }

        Rectangle {
            width: parent.width
            //  Gedeckelt, damit das Popup an der Player-Leiste nicht wächst,
            //  bis es aus dem Fenster ragt; darüber wird gescrollt.
            height: Math.min(146, Math.max(24, presetList.contentHeight + 2))
            color: Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                           App.themeTextPrimary.b, 0.05)
            radius: 6
            border.color: App.themeBorder
            border.width: 1
            clip: true

            ListView {
                id: presetList
                objectName: "eqPresetList"      // Griff für den Prüfstand
                anchors.fill: parent
                anchors.margins: 1
                model: Audio.presetNames
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                //  Nur in den EINSTELLUNGEN: eine letzte Zeile mit „+", die aus
                //  den aktuellen Reglern eine neue Voreinstellung anlegt. Sie
                //  gehört ans Ende der Liste, weil man dort verwaltet - am
                //  Player führt der Weg weiter über „Sichern" (Festlegung des
                //  Nutzers: dort wird nur gewählt und gesichert).
                footer: Item {
                    width: presetList.width
                    height: panel.manageable ? 26 : 0
                    visible: panel.manageable

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 4
                        color: addHover.hovered
                               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                         App.themeAccent.b, 0.18)
                               : "transparent"
                        border.color: addHover.hovered ? App.themeAccent
                                                       : App.themeBorder
                        border.width: 1

                        DrawnIcon {
                            id: addIcon
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            name: "plus"
                            size: 12
                            color: addHover.hovered ? App.themeAccent
                                                    : App.themeTextMuted
                        }
                        Text {
                            anchors.left: addIcon.right
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: App.uiText(App.language, "AudioEqPresetNewTitle")
                            color: addHover.hovered ? App.themeAccent
                                                    : App.themeTextMuted
                            font.pixelSize: 11
                        }
                        HoverHandler { id: addHover }
                        TapHandler {
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: savePop.open()
                        }
                    }
                }

                delegate: Item {
                    id: prow
                    required property int index
                    required property string modelData
                    readonly property bool isActive:
                        Audio.activePreset === prow.modelData
                    readonly property bool isBuiltin:
                        Audio.presetIsBuiltin(prow.modelData)
                    //  Nur eine VERÄNDERTE mitgelieferte hat etwas
                    //  zurückzusetzen (`presetsChanged` treibt die Bindung).
                    readonly property bool canReset:
                        Audio.presetsModified !== undefined
                        && prow.isBuiltin
                        && Audio.presetIsModified(prow.modelData)

                    width: presetList.width
                    height: 24

                    Rectangle {
                        anchors.fill: parent
                        color: prow.isActive
                               ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                         App.themeAccent.b, 0.20)
                               : (rowHover.hovered
                                  ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                            App.themeTextPrimary.b, 0.08)
                                  : "transparent")
                    }
                    HoverHandler { id: rowHover }
                    TapHandler {
                        //  Exklusiv greifen - sonst reicht der Druck an das
                        //  Element dahinter weiter (Standardregel).
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: Audio.applyPreset(prow.modelData)
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: rowBtns.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: prow.modelData
                        elide: Text.ElideRight
                        color: prow.isActive ? App.themeAccent : App.themeTextPrimary
                        font.pixelSize: 12
                        font.bold: prow.isActive
                    }

                    //  Ein kleines Zeichen-Knöpfchen (Regel 28: gezeichnet).
                    //  EIGENE `id` statt `parent`: auf der Ebene der angehängten
                    //  Eigenschaften (`ToolTip.text`) meint `parent` den
                    //  VISUELLEN Elternteil - also die `Row`, nicht den Knopf.
                    //  Ohne die id blieb der Hilfetext `undefined` (QML meldete
                    //  „Unable to assign [undefined] to QString").
                    component RowBtn: Rectangle {
                        id: rb
                        property string icon: ""
                        property string tip: ""
                        signal triggered()
                        width: 20; height: 20; radius: 4
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        color: rbHover.hovered ? App.themeAccent : "transparent"
                        DrawnIcon {
                            anchors.centerIn: parent
                            name: rb.icon
                            size: 13
                            color: rbHover.hovered ? "#ffffff" : App.themeTextMuted
                        }
                        HoverHandler { id: rbHover }
                        TapHandler {
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: rb.triggered()
                        }
                        ToolTip.visible: rbHover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: rb.tip
                    }

                    Row {
                        id: rowBtns
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        RowBtn {
                            visible: panel.manageable && prow.index > 0
                            icon: "arrow-up"
                            tip: App.uiText(App.language, "AudioEqPresetUpTip")
                            onTriggered: Audio.movePreset(prow.index, prow.index - 1)
                        }
                        RowBtn {
                            visible: panel.manageable
                                     && prow.index < Audio.presetNames.length - 1
                            icon: "arrow-down"
                            tip: App.uiText(App.language, "AudioEqPresetDownTip")
                            onTriggered: Audio.movePreset(prow.index, prow.index + 1)
                        }
                        RowBtn {
                            visible: panel.manageable && prow.canReset
                            icon: "undo"
                            tip: App.uiText(App.language, "AudioEqPresetResetTip")
                            onTriggered: Audio.resetPreset(prow.modelData)
                        }
                        RowBtn {
                            visible: panel.manageable
                            icon: "close"
                            tip: App.uiText(App.language, "AudioEqPresetDeleteTip")
                            onTriggered: Audio.deletePreset(prow.modelData)
                        }
                    }
                }
            }
        }

        // ── Fusszeile ───────────────────────────────────────────────────────
        //  Am Player steht hier NUR das Sichern (mit dem Disketten-Zeichen); es
        //  öffnet ein kleines Fenster, in dem man entweder einen Namen vergibt
        //  ODER - darunter, durch eine Linie getrennt - eine vorhandene
        //  Voreinstellung überschreibt. In den EINSTELLUNGEN kommt der
        //  Sammel-Knopf „Mitgelieferte zurücksetzen" dazu.
        Flow {
            id: presetRow
            width: parent.width
            spacing: 8

            Button {
                id: saveBtn
                text: App.uiText(App.language, "AudioEqSavePreset")
                icon.name: ""
                onClicked: savePop.open()

                Popup {
                    id: savePop
                    y: -implicitHeight - 6
                    width: Math.min(280, Math.max(200, panel.width - 40))
                    padding: 10
                    modal: false
                    focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    background: Rectangle {
                        color: App.themeCard
                        radius: 8
                        border.color: App.themeBorder
                        border.width: 1
                    }
                    onOpened: newName.forceActiveFocus()

                    contentItem: Column {
                        spacing: 8

                        Text {
                            text: App.uiText(App.language, "AudioEqPresetNewTitle")
                            color: App.themeTextMuted
                            font.pixelSize: 11
                        }
                        Row {
                            spacing: 6
                            TextField {
                                id: newName
                                width: savePop.availableWidth - saveGo.width - 6
                                placeholderText: App.uiText(App.language, "AudioEqPresetName")
                                color: App.themeTextPrimary
                                onAccepted: if (text.trim().length > 0) {
                                    Audio.savePreset(text.trim()); text = ""; savePop.close()
                                }
                            }
                            Button {
                                id: saveGo
                                text: App.uiText(App.language, "AudioEqSavePreset")
                                enabled: newName.text.trim().length > 0
                                onClicked: {
                                    Audio.savePreset(newName.text.trim())
                                    newName.text = ""
                                    savePop.close()
                                }
                            }
                        }

                        //  Die Trennlinie ist die Aussage: darunter wird NICHTS
                        //  Neues angelegt, sondern etwas Vorhandenes ersetzt.
                        //  Sie sagt das allein - die Zeile darunter wiederholte
                        //  nur, was die Liste ohnehin zeigt (Nutzerwunsch).
                        Rectangle {
                            width: savePop.availableWidth
                            height: 1
                            color: App.themeBorder
                        }
                        Rectangle {
                            width: savePop.availableWidth
                            height: Math.min(120, Math.max(24, overList.contentHeight + 2))
                            color: "transparent"
                            clip: true
                            ListView {
                                id: overList
                                anchors.fill: parent
                                model: Audio.presetNames
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                delegate: Rectangle {
                                    id: orow
                                    required property string modelData
                                    width: overList.width
                                    height: 24
                                    color: oHover.hovered
                                           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                     App.themeAccent.b, 0.18)
                                           : "transparent"
                                    radius: 4
                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: orow.modelData
                                        color: App.themeTextPrimary
                                        font.pixelSize: 12
                                    }
                                    HoverHandler { id: oHover }
                                    TapHandler {
                                        onTapped: {
                                            Audio.savePreset(orow.modelData)
                                            savePop.close()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Button {
                //  Nur in den Einstellungen, und nur wenn es etwas zurückzusetzen gibt.
                visible: panel.manageable && Audio.presetsModified
                text: App.uiText(App.language, "AudioEqPresetResetAll")
                onClicked: Audio.resetAllPresets()
            }
        }
    }
}
