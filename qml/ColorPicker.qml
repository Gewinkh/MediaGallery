import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  ColorPicker.qml — eigenständiger HSV+Alpha-Farbwähler (ersetzt
//  ColorPickerButton (QWidget) + QColorDialog).
//
//  Verwendung:
//      ColorPicker {
//          selectedColor: Settings.someColor        // Eingang (lesbar gebunden)
//          showAlpha: true
//          onColorPicked: (c) => App.setSomeColor(c) // Ausgang (bei OK)
//      }
//
//  Semantik wie das Widget-Original: Die Farbe wird erst bei „OK" committet
//  (Signal colorPicked + Aktualisierung von selectedColor). Externe Änderungen
//  an selectedColor synchronisieren den internen HSV-Zustand.
//
//  Performance: Das SV-Feld wird nur neu gezeichnet, wenn sich der Farbton
//  (Hue) ändert; Cursor/Handles sind reine Item-Overlays ohne Canvas-Repaint.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    // ── Öffentliche API ───────────────────────────────────────────────────────
    property color  selectedColor: "#00b4a0"
    property bool   showAlpha: true
    property string title: App.uiText(App.language, "ColorPickerTitle")
    signal colorPicked(color color)

    implicitWidth: 40
    implicitHeight: 24

    // ── Interner Arbeits-HSV-Zustand des Popups (0..1) ────────────────────────
    property real _wh: 0      // hue
    property real _ws: 0      // saturation
    property real _wv: 1      // value
    property real _wa: 1      // alpha
    property bool _editingHex: false

    function _loadFrom(c) {
        var hh = c.hsvHue
        _wh = hh < 0 ? _wh : hh          // hue bei achromatischen Farben halten
        _ws = c.hsvSaturation
        _wv = c.hsvValue
        _wa = c.a
    }
    function _working() {
        return Qt.hsva(_wh < 0 ? 0 : _wh, _ws, _wv, showAlpha ? _wa : 1)
    }
    function _pad2(n) { var s = Math.round(n).toString(16); return s.length < 2 ? "0" + s : s }
    function _hex(c, withAlpha) {
        var r = _pad2(c.r * 255), g = _pad2(c.g * 255), b = _pad2(c.b * 255)
        return withAlpha ? "#" + _pad2(c.a * 255) + r + g + b : "#" + r + g + b
    }

    // ── Swatch-Button (öffnet das Popup) ──────────────────────────────────────
    Rectangle {
        id: swatch
        anchors.fill: parent
        radius: 4
        border.width: 2
        border.color: swatchHover.hovered ? Qt.rgba(1, 1, 1, 0.7)
                                          : Qt.rgba(1, 1, 1, 0.3)

        // Schachbrett für Transparenz
        Canvas {
            anchors.fill: parent
            anchors.margins: 2
            onPaint: {
                var ctx = getContext("2d")
                var s = 5
                for (var y = 0; y < height; y += s)
                    for (var x = 0; x < width; x += s) {
                        var even = ((x / s) + (y / s)) % 2 === 0
                        ctx.fillStyle = even ? "#3a3a3a" : "#555555"
                        ctx.fillRect(x, y, s, s)
                    }
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: 2
            color: root.selectedColor
        }

        HoverHandler { id: swatchHover }
        TapHandler {
            onTapped: {
                root._loadFrom(root.selectedColor)
                popup.open()
            }
        }
    }

    // ── Popup mit Picker-UI ───────────────────────────────────────────────────
    Popup {
        id: popup
        width: 264
        // Genug Höhe für Titel + SV + Hue + (Alpha) + Hex + Buttons
        height: 250 + (root.showAlpha ? 26 : 0)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay

        background: Rectangle {
            color: App.themeCard
            border.color: App.themeBorder
            radius: 8
        }

        contentItem: Column {
            spacing: 10

            Text {
                text: root.title
                color: App.themeTextPrimary
                font.pixelSize: 13
                font.bold: true
            }

            // ── SV-Feld ────────────────────────────────────────────────────────
            Item {
                id: svArea
                width: 240
                height: 150

                Canvas {
                    id: svCanvas
                    anchors.fill: parent
                    // Nur bei Hue-Wechsel neu zeichnen
                    property real hue: root._wh
                    onHueChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        // Basis: voller Farbton
                        ctx.fillStyle = Qt.hsva(hue < 0 ? 0 : hue, 1, 1, 1)
                        ctx.fillRect(0, 0, width, height)
                        // Weiß (links) → transparent (rechts) = Sättigung
                        var gx = ctx.createLinearGradient(0, 0, width, 0)
                        gx.addColorStop(0, "rgba(255,255,255,1)")
                        gx.addColorStop(1, "rgba(255,255,255,0)")
                        ctx.fillStyle = gx
                        ctx.fillRect(0, 0, width, height)
                        // Transparent (oben) → schwarz (unten) = Helligkeit
                        var gy = ctx.createLinearGradient(0, 0, 0, height)
                        gy.addColorStop(0, "rgba(0,0,0,0)")
                        gy.addColorStop(1, "rgba(0,0,0,1)")
                        ctx.fillStyle = gy
                        ctx.fillRect(0, 0, width, height)
                    }
                }

                Rectangle {
                    id: svCursor
                    width: 12; height: 12; radius: 6
                    border.width: 2; border.color: "white"
                    color: "transparent"
                    x: root._ws * svArea.width - width / 2
                    y: (1 - root._wv) * svArea.height - height / 2
                    Rectangle {
                        anchors.centerIn: parent
                        width: 10; height: 10; radius: 5
                        border.width: 1; border.color: "black"
                        color: "transparent"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onPressed: (m) => update(m.x, m.y)
                    onPositionChanged: (m) => update(m.x, m.y)
                    function update(px, py) {
                        root._ws = Math.max(0, Math.min(1, px / svArea.width))
                        root._wv = Math.max(0, Math.min(1, 1 - py / svArea.height))
                    }
                }
            }

            // ── Hue-Slider ─────────────────────────────────────────────────────
            Item {
                width: 240; height: 16
                Canvas {
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        var g = ctx.createLinearGradient(0, 0, width, 0)
                        for (var i = 0; i <= 6; ++i)
                            g.addColorStop(i / 6, Qt.hsva(i / 6, 1, 1, 1))
                        ctx.fillStyle = g
                        ctx.fillRect(0, 0, width, height)
                    }
                }
                Rectangle {
                    width: 6; height: parent.height + 4
                    y: -2
                    x: root._wh * parent.width - width / 2
                    color: "transparent"
                    border.width: 2; border.color: "white"
                    radius: 2
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: (m) => set(m.x)
                    onPositionChanged: (m) => set(m.x)
                    function set(px) {
                        root._wh = Math.max(0, Math.min(1, px / width))
                    }
                }
            }

            // ── Alpha-Slider (optional) ────────────────────────────────────────
            Item {
                width: 240; height: 16
                visible: root.showAlpha
                Canvas {
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        var s = 4
                        for (var y = 0; y < height; y += s)
                            for (var x = 0; x < width; x += s) {
                                var even = ((x / s) + (y / s)) % 2 === 0
                                ctx.fillStyle = even ? "#3a3a3a" : "#555555"
                                ctx.fillRect(x, y, s, s)
                            }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Qt.hsva(root._wh < 0 ? 0 : root._wh, root._ws, root._wv, 0) }
                        GradientStop { position: 1.0; color: Qt.hsva(root._wh < 0 ? 0 : root._wh, root._ws, root._wv, 1) }
                    }
                }
                Rectangle {
                    width: 6; height: parent.height + 4
                    y: -2
                    x: root._wa * parent.width - width / 2
                    color: "transparent"
                    border.width: 2; border.color: "white"
                    radius: 2
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: (m) => set(m.x)
                    onPositionChanged: (m) => set(m.x)
                    function set(px) {
                        root._wa = Math.max(0, Math.min(1, px / width))
                    }
                }
            }

            // ── Vorschau + Hex-Eingabe ─────────────────────────────────────────
            Row {
                spacing: 8
                Rectangle {
                    width: 32; height: 28; radius: 4
                    border.width: 1; border.color: App.themeBorder
                    color: root._working()
                }
                TextField {
                    id: hexField
                    width: 200
                    text: root._hex(root._working(), root.showAlpha)
                    color: App.themeTextPrimary
                    selectByMouse: true
                    font.family: "monospace"
                    background: Rectangle {
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.color: App.themeBorder
                        radius: 4
                    }
                    onActiveFocusChanged: root._editingHex = activeFocus
                    onEditingFinished: {
                        var t = text.trim()
                        if (!t.startsWith("#")) t = "#" + t
                        if (/^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(t)) {
                            root._loadFrom(Qt.color(t))
                        } else {
                            text = root._hex(root._working(), root.showAlpha)
                        }
                    }
                }
            }

            // ── OK / Abbrechen ─────────────────────────────────────────────────
            Row {
                spacing: 8
                anchors.right: parent.right
                Button {
                    text: App.uiText(App.language, "SettingsCancel")
                    onClicked: popup.close()
                }
                Button {
                    text: App.uiText(App.language, "SettingsOk")
                    highlighted: true
                    onClicked: {
                        var c = root._working()
                        root.selectedColor = c
                        root.colorPicked(c)
                        popup.close()
                    }
                }
            }
        }
    }
}
