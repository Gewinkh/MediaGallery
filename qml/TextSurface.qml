pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  TextSurface.qml — editierbarer Plain-Text-Editor (ersetzt TextViewer(QWidget)).
//  Inhalt via Viewer.readTextFile, Speichern via Viewer.writeTextFile (atomar).
//
//  • Editierbar (kein read-only mehr); Strg+S oder Speichern-Button schreibt.
//  • Ungespeicherte Aenderungen werden mit "•" markiert; beim Verlassen
//    (release) automatisch gespeichert, damit keine Eingaben verloren gehen.
//  • topInset/bottomInset werden vom FullscreenViewer reserviert → die globale
//    Leiste (Dateiname) ueberdeckt den Inhalt NICHT mehr.
//  • Weiches, web-aehnliches Mausrad-Scrollen (animiert, groesserer Schritt).
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0

    property string currentPath: ""
    property bool   dirty: false
    property bool   _loading: false

    function save() {
        if (!root.dirty || root.currentPath.length === 0) return
        if (Viewer.writeTextFile(root.currentPath, editor.text))
            root.dirty = false
    }

    function release() {
        root.save()                       // beim Verlassen sichern (kein Datenverlust)
        editor.text = ""
        root.currentPath = ""
        root.dirty = false
    }

    onSourceChanged: {
        root.save()                       // evtl. vorherige Datei sichern
        root._loading = true
        root.currentPath = source
        editor.text = source.length > 0 ? Viewer.readTextFile(source) : ""
        editor.cursorPosition = 0
        root.dirty = false
        root._loading = false
    }

    Rectangle { anchors.fill: parent; color: App.themeBackground }

    // ── Toolbar (unter der globalen Leiste) ───────────────────────────────────
    Rectangle {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: root.topInset }
        height: 40
        color: App.themeToolbarBg
        z: 4
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: App.themeBorder }

        Row {
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: saveRow.implicitWidth + 22; height: 26; radius: 6
                opacity: root.dirty ? 1.0 : 0.5
                color: saveHover.hovered && root.dirty ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
                                                       : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.16)
                border.color: App.themeAccent; border.width: 1
                Row {
                    id: saveRow
                    anchors.centerIn: parent; spacing: 6
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "\u2913"
                           color: App.themeAccent; font.pixelSize: 13 }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "Speichern"
                           color: App.themeAccent; font.pixelSize: 12 }
                }
                HoverHandler { id: saveHover; enabled: root.dirty }
                TapHandler { enabled: root.dirty; onTapped: root.save() }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (root.dirty ? "\u2022 " : "") + root.currentPath
                color: root.dirty ? App.themeAccent : App.themeTextMuted
                font.pixelSize: 11; elide: Text.ElideLeft
                width: Math.min(implicitWidth, toolbar.width - 220)
            }
        }
    }

    // ── Editor (editierbar, eigene Flickable für sauberes Scrollen) ────────────
    Flickable {
        id: flick
        anchors {
            left: parent.left; right: parent.right
            top: toolbar.bottom; bottom: parent.bottom
            bottomMargin: root.bottomInset
            margins: 12
        }
        clip: true
        contentWidth: editor.paintedWidth
        contentHeight: editor.paintedHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        TextArea.flickable: TextArea {
            id: editor
            readOnly: false
            selectByMouse: true
            wrapMode: TextEdit.NoWrap
            color: App.themeTextPrimary
            selectionColor: App.themeAccent
            font.family: "monospace"
            font.pixelSize: 13
            padding: 10
            background: Rectangle { color: App.themeCard; radius: 6; border.color: App.themeBorder }
            onTextChanged: if (!root._loading) root.dirty = true
            Keys.onPressed: function(e) {
                if ((e.modifiers & Qt.ControlModifier) && e.key === Qt.Key_S) {
                    root.save(); e.accepted = true
                }
            }
        }
    }

    // Weiches, web-aehnliches Mausrad-Scrollen — als Geschwister der Flickable,
    // damit der Fänger NICHT mit dem Inhalt mitscrollt. NoButton → Klicks/Markieren
    // erreichen den Editor.
    NumberAnimation {
        id: scrollAnim
        target: flick; property: "contentY"
        duration: 180; easing.type: Easing.OutCubic
    }
    MouseArea {
        anchors.fill: flick
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => {
            var maxY = Math.max(0, flick.contentHeight - flick.height)
            if (maxY <= 0) { wheel.accepted = true; return }
            var raw = (wheel.angleDelta.y !== 0)
                      ? (wheel.angleDelta.y / 120) * (flick.height * 0.5)
                      : wheel.pixelDelta.y * 1.6
            var base = scrollAnim.running ? scrollAnim.to : flick.contentY
            var tgt = Math.max(0, Math.min(base - raw, maxY))
            scrollAnim.from = flick.contentY
            scrollAnim.to = tgt
            scrollAnim.restart()
            wheel.accepted = true
        }
    }
}
