pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  MetadataDateEditor.qml — Inline-Editor für das benutzerdefinierte Datum eines
//  Mediums (ersetzt MetadataEditor(QDialog)). Speichern/Löschen laufen als
//  Bridge-Calls (App.setCustomDate / App.clearCustomDate) über den Aufrufer.
// ─────────────────────────────────────────────────────────────────────────────
Popup {
    id: editor
    modal: true
    focus: true
    padding: 16
    anchors.centerIn: Overlay.overlay

    property date value: new Date()
    signal accepted(date dt)
    signal cleared()

    function openWith(dt) {
        var d = dt && !isNaN(dt.getTime ? dt.getTime() : NaN) ? dt : new Date()
        ySpin.value = d.getFullYear()
        moSpin.value = d.getMonth() + 1
        dSpin.value = d.getDate()
        hSpin.value = d.getHours()
        miSpin.value = d.getMinutes()
        open()
    }

    background: Rectangle {
        color: App.themeCard
        radius: 10
        border.color: App.themeBorder
    }

    contentItem: Column {
        spacing: 12

        Text {
            text: App.uiText(App.language, "MetaTitle")
            color: App.themeTextPrimary
            font.pixelSize: 15; font.bold: true
        }

        Grid {
            columns: 2
            rowSpacing: 8
            columnSpacing: 10

            Text { text: App.uiText(App.language, "DateYear");   color: App.themeTextMuted }
            SpinBox { id: ySpin;  from: 1970; to: 2100; editable: true }
            Text { text: App.uiText(App.language, "DateMonth");  color: App.themeTextMuted }
            SpinBox { id: moSpin; from: 1; to: 12; editable: true }
            Text { text: App.uiText(App.language, "DateDay");    color: App.themeTextMuted }
            SpinBox { id: dSpin;  from: 1; to: 31; editable: true }
            Text { text: App.uiText(App.language, "DateHour"); color: App.themeTextMuted }
            SpinBox { id: hSpin;  from: 0; to: 23; editable: true }
            Text { text: App.uiText(App.language, "DateMinute"); color: App.themeTextMuted }
            SpinBox { id: miSpin; from: 0; to: 59; editable: true }
        }

        Row {
            spacing: 8
            Button {
                text: App.uiText(App.language, "EditorSave")
                onClicked: {
                    var dt = new Date(ySpin.value, moSpin.value - 1, dSpin.value,
                                      hSpin.value, miSpin.value, 0)
                    editor.accepted(dt)
                    editor.close()
                }
            }
            Button {
                text: App.uiText(App.language, "MetaReset")
                onClicked: { editor.cleared(); editor.close() }
            }
            Button {
                text: App.uiText(App.language, "SettingsCancel")
                onClicked: editor.close()
            }
        }
    }
}
