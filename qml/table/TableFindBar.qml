pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

//  Suchbalken der Tabellen-Flaechen (CSV/TSV und DATEV). Dieselbe Form wie im
//  Texteditor und im DOCX-Editor, damit die App einheitlich bleibt; ersetzen
//  kann er nichts.
//  Der Balken haelt die Eingabe, der Controller die Treffer.
Rectangle {
    id: root

    //  TableController oder DatevController - beide tragen dieselbe Such-API.
    property var ctl: null
    //  Oberste sichtbare Zeile: von dort aus wird der erste Treffer gesucht,
    //  damit der Sprung nicht ans Ende der Datei zurueckfaellt.
    property int fromRow: 0

    //  Der Suchtext - nach aussen sichtbar, damit ein Pruefstand den ECHTEN Weg
    //  fahren kann (Feld fuellen statt den Controller anzustossen).
    property alias suchtext: feld.text

    property bool grossKlein: false
    property bool ganzeZelle: false

    signal geschlossen()

    function oeffnen() {
        feld.field.forceActiveFocus()
        feld.field.selectAll()
        if (feld.text.length > 0) root._suchen()
    }
    function schliessen() {
        if (root.ctl) root.ctl.clearSearch()
        root.geschlossen()
    }
    function _suchen() {
        if (!root.ctl) return
        root.ctl.search(feld.text, root.grossKlein, root.ganzeZelle, root.fromRow)
    }
    function _schritt(rueckwaerts) {
        if (!root.ctl) return
        if (root.ctl.matchCount === 0) { root._suchen(); return }
        root.ctl.stepMatch(rueckwaerts ? -1 : 1)
    }

    readonly property string _stand: {
        if (!root.ctl || feld.text.length === 0) return ""
        if (root.ctl.searching) return App.uiText(App.language, "TableFindSearching")
        if (root.ctl.matchCount === 0) {
            const rest = (root.ctl.otherBlockMatches !== undefined)
                         ? root.ctl.otherBlockMatches : 0
            return rest > 0
                   ? App.uiText(App.language, "TableFindOtherBlocks").arg(rest)
                   : App.uiText(App.language, "EditorFindNoMatch")
        }
        return App.uiText(App.language, "EditorFindCounter")
                  .arg(root.ctl.matchIndex + 1)
                  .arg(root.ctl.matchCount) + (root.ctl.matchOverflow ? "+" : "")
    }
    readonly property bool _ohneTreffer:
        root.ctl && !root.ctl.searching && feld.text.length > 0 && root.ctl.matchCount === 0

    //  `searchRevision` steht bewusst drin: die Liste kommt aus einem
    //  Funktionsaufruf, und darauf erzeugt QML keine Bindung.
    readonly property var _andere: {
        void root.ctl
        if (!root.ctl || feld.text.length === 0) return []
        void root.ctl.searchRevision
        if (!root.ctl.otherBlocksWithMatches) return []
        return root.ctl.otherBlocksWithMatches()
    }

    radius: 8
    color: App.themeMenuBarBg
    border.color: App.themeBorder
    implicitWidth: gitter.implicitWidth + 20
    implicitHeight: gitter.implicitHeight + 16

    //  Ein Suchlauf ueber eine grosse Datei kostet gemessen bis zu 119 ms; ihn
    //  je Tastendruck anzustossen hiesse, die meisten davon wegzuwerfen.
    Timer {
        id: entprellung
        interval: 150
        onTriggered: root._suchen()
    }

    component FField: Rectangle {
        id: ff
        property alias field: innerField
        property alias text: innerField.text
        property string ph: ""
        signal accepted()
        signal shiftAccepted()
        signal escaped()
        width: 200; height: 28; radius: 6
        color: App.themeCard
        border.color: innerField.activeFocus ? App.themeAccent : App.themeBorder
        TextField {
            id: innerField
            anchors.fill: parent
            anchors.leftMargin: 8; anchors.rightMargin: 8
            verticalAlignment: TextInput.AlignVCenter
            placeholderText: ff.ph
            color: App.themeTextPrimary
            placeholderTextColor: App.themeTextMuted
            font.pixelSize: 12
            background: null
            Keys.onPressed: function (e) {
                if (e.key === Qt.Key_Escape) {
                    ff.escaped(); e.accepted = true
                } else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                    if (e.modifiers & Qt.ShiftModifier) ff.shiftAccepted()
                    else                                ff.accepted()
                    e.accepted = true
                }
            }
        }
    }

    //  ALLES ueber die eigene `id`, nie ueber `parent`: an der Wurzel einer
    //  Inline-Komponente zeigt `parent` auf das umgebende Element.
    component FBtn: Rectangle {
        id: fb
        property string iconName: ""
        property string label: ""
        property string tip: ""
        property bool   an: false
        property bool   wide: false
        signal clicked()
        width: fb.wide ? (btnText.implicitWidth + 20) : 28
        height: 28; radius: 6
        color: fbHover.hovered ? App.themeCard : "transparent"
        border.color: fb.an ? App.themeAccent : App.themeBorder
        DrawnIcon {
            anchors.centerIn: parent
            visible: fb.iconName.length > 0
            name: fb.iconName
            size: 14
            color: fb.an ? App.themeAccent : App.themeTextPrimary
        }
        Text {
            id: btnText
            anchors.centerIn: parent
            visible: fb.iconName.length === 0
            text: fb.label
            color: fb.an ? App.themeAccent : App.themeTextPrimary
            font.pixelSize: 12
        }
        HoverHandler { id: fbHover }
        TapHandler { onTapped: fb.clicked() }
        ToolTip.visible: fbHover.hovered && fb.tip.length > 0
        ToolTip.delay: 500
        ToolTip.text: fb.tip
    }

    RowLayout {
        id: gitter
        anchors.centerIn: parent
        spacing: 8

        FField {
            id: feld
            ph: App.uiText(App.language, "EditorFindPlaceholder")
            onAccepted: root._schritt(false)
            onShiftAccepted: root._schritt(true)
            onEscaped: root.schliessen()
            field.onTextChanged: {
                if (feld.text.length === 0) {
                    entprellung.stop()
                    if (root.ctl) root.ctl.clearSearch()
                } else {
                    entprellung.restart()
                }
            }
        }
        FBtn { iconName: "chevron-up"
               tip: App.uiText(App.language, "EditorFindPrev")
               onClicked: root._schritt(true) }
        FBtn { iconName: "chevron-down"
               tip: App.uiText(App.language, "EditorFindNext")
               onClicked: root._schritt(false) }
        FBtn { label: "Aa"; tip: App.uiText(App.language, "EditorFindCase")
               an: root.grossKlein
               onClicked: { root.grossKlein = !root.grossKlein; root._suchen() } }
        FBtn { wide: true
               label: App.uiText(App.language, "TableFindCellLabel")
               tip: App.uiText(App.language, "TableFindWholeCell")
               an: root.ganzeZelle
               onClicked: { root.ganzeZelle = !root.ganzeZelle; root._suchen() } }
        Text {
            text: root._stand
            color: root._ohneTreffer ? Qt.rgba(1, 0.55, 0.35, 1) : App.themeTextMuted
            font.pixelSize: 11
            Layout.minimumWidth: 90
            Layout.alignment: Qt.AlignVCenter
        }
        //  Von hier aus fuehrt ein Klick in die Tabelle mit dem Treffer.
        FBtn {
            id: andereBtn
            visible: root._andere.length > 0
            wide: true
            label: App.uiText(App.language, "TableFindGoOther").arg(root._andere.length)
            tip: App.uiText(App.language, "TableFindGoOtherTip")
            onClicked: andereMenu.popup(andereBtn, 0, andereBtn.height + 2)
        }
        Menu {
            id: andereMenu
            //  Ohne Mindestbreite kollabiert ein Menue mit eigenem Hintergrund zum Strich.
            implicitWidth: Math.max(200, andereMenu.contentItem ? andereMenu.contentItem.childrenRect.width + 24 : 200)
            background: Rectangle {
                implicitWidth: 200
                color: App.themeMenuBarBg
                border.color: App.themeBorder
                radius: 6
            }
            Repeater {
                model: root._andere
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.title + "  (" + modelData.count + ")"
                    contentItem: Text {
                        text: parent.text
                        color: App.themeTextPrimary
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 8
                    }
                    onTriggered: {
                        if (root.ctl && root.ctl.jumpToBlock)
                            root.ctl.jumpToBlock(modelData.index)
                    }
                }
            }
        }
        FBtn { iconName: "close"
               tip: App.uiText(App.language, "EditorFindClose")
               onClicked: root.schliessen() }
    }
}
