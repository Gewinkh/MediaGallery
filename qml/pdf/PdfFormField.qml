pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// EIN ausfüllbares Formularfeld über einer PDF-Seite - und die EINZIGE Darstellung: Qt PDF zeichnet Widget-
// Annotationen nicht (PDFium malt sie nur über FPDF_FFLDraw), ohne dieses Overlay sähe ein Formular leer aus.
// Wert-Sync bewusst ohne Bindung: zum Modell live, zurück nur, solange das Feld NICHT den Fokus hat.
Item {
    id: ff

    required property var field
    property real pageScale: 1.0
    property real pageWPt: 612           // ANGEZEIGTE Seitengröße (inkl. Plan-Drehung)
    property real pageHPt: 792
    property PdfEditController ctl: null

    // `type`: 0 unbekannt, 1 Text, 2 Ankreuzfeld, 3 Optionsfeld, 4 Auswahl, 5 Druckknopf. Unterschriftenfelder
    // kommen gar nicht erst an - sie werden schon beim Lesen übergangen.
    readonly property string fieldName: ff.field.name
    readonly property int    fieldType: ff.field.type
    readonly property bool   readOnly:  ff.field.readOnly === true
    readonly property bool   isText:    ff.fieldType === 1
    readonly property bool   isCheck:   ff.fieldType === 2
    readonly property bool   isRadio:   ff.fieldType === 3
    readonly property bool   isChoice:  ff.fieldType === 4
    //  Druckknöpfe werden nur angedeutet: sie tragen keinen Wert, den wir
    //  schreiben könnten.
    readonly property bool   inert:     !ff.isText && !ff.isCheck && !ff.isRadio
                                        && !ff.isChoice
    readonly property bool   editable:  !ff.readOnly && !ff.inert

    //  Aktueller Wert - rev-getrieben (formValueRev), damit Optionsgruppen und
    //  mehrfach platzierte Felder sofort folgen, ohne dass die Feldliste (und
    //  damit dieses Delegate) neu erzeugt wird.
    readonly property string currentValue:
        ff.ctl ? (ff.ctl.formValueRev, ff.ctl.formValue(ff.fieldName)) : ""
    readonly property bool checkedState: ff.field.onState.length > 0
                                         && ff.currentValue === ff.field.onState

    readonly property int  rot: ((ff.field.rot % 360) + 360) % 360
    readonly property bool quarter: ff.rot === 90 || ff.rot === 270
    readonly property real srcWPt: ff.quarter ? ff.pageHPt : ff.pageWPt
    readonly property real srcHPt: ff.quarter ? ff.pageWPt : ff.pageHPt
    readonly property real rxPt: ff.rot === 90  ? (ff.srcHPt - (ff.field.yPt + ff.field.hPt))
                               : ff.rot === 180 ? (ff.srcWPt - (ff.field.xPt + ff.field.wPt))
                               : ff.rot === 270 ? ff.field.yPt
                                                : ff.field.xPt
    readonly property real ryPt: ff.rot === 90  ? ff.field.xPt
                               : ff.rot === 180 ? (ff.srcHPt - (ff.field.yPt + ff.field.hPt))
                               : ff.rot === 270 ? (ff.srcWPt - (ff.field.xPt + ff.field.wPt))
                                                : ff.field.yPt
    readonly property real rwPt: ff.quarter ? ff.field.hPt : ff.field.wPt
    readonly property real rhPt: ff.quarter ? ff.field.wPt : ff.field.hPt

    x: ff.rxPt * ff.pageScale
    y: ff.ryPt * ff.pageScale
    width:  Math.max(3, ff.rwPt * ff.pageScale)
    height: Math.max(3, ff.rhPt * ff.pageScale)
    z: 1

    //  Schrift wie im geschriebenen Erscheinungsbild: „automatisch" heißt
    //  mehrzeilig 10 pt, sonst 62 % der Feldhöhe (auf 4…12 pt geklemmt).
    readonly property real fontPt: ff.field.multiline === true
                                   ? 10.0
                                   : Math.max(4, Math.min(ff.rhPt * 0.62, 12))
    readonly property real padPx: 2.0 * ff.pageScale

    // Fläche (Acrobat-artige Tönung; die Seite darunter ist immer weiß, die
    //    Farben sind deshalb bewusst FEST und nicht themenabhängig)
    Rectangle {
        anchors.fill: parent
        radius: Math.min(3, parent.height / 4)
        color: ff.inert ? Qt.rgba(0.45, 0.45, 0.45, 0.10)
                        : (ff.readOnly ? Qt.rgba(0.55, 0.55, 0.55, 0.13)
                                       : Qt.rgba(0.20, 0.45, 0.95, 0.13))
        border.width: 1
        border.color: ff.activeFocus || textInput.activeFocus || textArea.activeFocus
                      ? Qt.rgba(0.10, 0.35, 0.90, 0.95)
                      : Qt.rgba(0.20, 0.45, 0.95, 0.45)
    }

    ToolTip.visible: hoverArea.hovered && ff.field.tooltip.length > 0
    ToolTip.text: ff.field.tooltip
    HoverHandler { id: hoverArea }

    TextInput {
        id: textInput
        visible: ff.isText && ff.field.multiline !== true
        enabled: ff.editable
        anchors.fill: parent
        anchors.leftMargin: ff.padPx
        anchors.rightMargin: ff.padPx
        clip: true
        color: "#111111"
        selectionColor: Qt.rgba(0.20, 0.45, 0.95, 0.45)
        selectedTextColor: "#111111"
        selectByMouse: true
        verticalAlignment: TextInput.AlignVCenter
        font.pixelSize: Math.max(4, ff.fontPt * ff.pageScale)
        maximumLength: ff.field.maxLen > 0 ? ff.field.maxLen : 32767
        echoMode: ff.field.password === true ? TextInput.Password : TextInput.Normal
        onTextEdited: if (ff.ctl) ff.ctl.setFormValue(ff.fieldName, text)
        Component.onCompleted: text = ff.currentValue
        Connections {
            target: ff
            function onCurrentValueChanged() {
                if (!textInput.activeFocus)
                    textInput.text = ff.currentValue
            }
        }
    }

    TextEdit {
        id: textArea
        visible: ff.isText && ff.field.multiline === true
        enabled: ff.editable
        anchors.fill: parent
        anchors.margins: ff.padPx
        clip: true
        color: "#111111"
        selectionColor: Qt.rgba(0.20, 0.45, 0.95, 0.45)
        selectedTextColor: "#111111"
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        font.pixelSize: Math.max(4, ff.fontPt * ff.pageScale)
        onTextChanged: if (activeFocus && ff.ctl) ff.ctl.setFormValue(ff.fieldName, text)
        Component.onCompleted: text = ff.currentValue
        Connections {
            target: ff
            function onCurrentValueChanged() {
                if (!textArea.activeFocus)
                    textArea.text = ff.currentValue
            }
        }
    }

    // Häkchen und Punkt selbst gezeichnet: die Zustände liegen zwar als fertige Erscheinungsbilder in der Datei,
    // sichtbar macht sie aber erst dieses Overlay - und es muss auf jeder Seitengröße mitskalieren.
    Item {
        anchors.fill: parent
        visible: ff.isCheck || ff.isRadio

        Rectangle {                                   // Optionsfeld: Punkt
            anchors.centerIn: parent
            visible: ff.isRadio && ff.checkedState
            width:  Math.max(3, Math.min(parent.width, parent.height) * 0.5)
            height: width
            radius: width / 2
            color: "#12327f"
        }
        Canvas {                                      // Ankreuzfeld: Häkchen
            id: tick
            anchors.fill: parent
            visible: ff.isCheck && ff.checkedState
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                if (!visible)
                    return
                var m = Math.min(width, height)
                ctx.strokeStyle = "#12327f"
                ctx.lineWidth = Math.max(1, m * 0.14)
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.moveTo(width * 0.24, height * 0.52)
                ctx.lineTo(width * 0.44, height * 0.74)
                ctx.lineTo(width * 0.78, height * 0.28)
                ctx.stroke()
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: ff.editable
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (!ff.ctl)
                    return
                //  Ankreuzfeld schaltet um, Optionsfeld wählt NUR aus (das
                //  Abwählen einer Gruppe ist in PDF-Formularen nicht üblich).
                if (ff.isRadio)
                    ff.ctl.setFormValue(ff.fieldName, ff.field.onState)
                else
                    ff.ctl.setFormValue(ff.fieldName,
                                        ff.checkedState ? "Off" : ff.field.onState)
            }
        }
    }

    Item {
        id: choiceBox
        anchors.fill: parent
        visible: ff.isChoice

        readonly property int valueIndex: ff.field.optionValues.indexOf(ff.currentValue)
        Text {
            anchors.fill: parent
            anchors.leftMargin: ff.padPx
            anchors.rightMargin: ff.padPx + choiceBox.height * 0.6
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            clip: true
            color: "#111111"
            font.pixelSize: Math.max(4, ff.fontPt * ff.pageScale)
            text: choiceBox.valueIndex >= 0 ? ff.field.options[choiceBox.valueIndex]
                                            : ff.currentValue
        }
        Canvas {                                      // Aufklapp-Dreieck
            anchors.right: parent.right
            anchors.rightMargin: ff.padPx
            anchors.verticalCenter: parent.verticalCenter
            width:  Math.max(4, Math.min(9, parent.height * 0.4))
            height: width * 0.66
            onWidthChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = "#12327f"
                ctx.beginPath()
                ctx.moveTo(0, 0); ctx.lineTo(width, 0); ctx.lineTo(width / 2, height)
                ctx.closePath(); ctx.fill()
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: ff.editable
            cursorShape: Qt.PointingHandCursor
            onClicked: choiceMenu.popup()
        }
        ThemedMenu {
            id: choiceMenu
            Repeater {
                model: ff.field.options
                MenuItem {
                    required property int index
                    required property string modelData
                    text: modelData
                    onTriggered: {
                        if (!ff.ctl)
                            return
                        //  Geschrieben wird der EXPORTWERT (/Opt-Paare); fehlt
                        //  er, ist der Anzeigetext zugleich der Wert.
                        const vals = ff.field.optionValues
                        ff.ctl.setFormValue(ff.fieldName,
                                            index < vals.length ? vals[index] : modelData)
                    }
                }
            }
        }
    }
}
