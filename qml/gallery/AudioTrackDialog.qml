import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  AudioTrackDialog.qml - „Welche Tonspur?"
//
//  Er erscheint NUR, wenn eine Datei mehr als eine Tonspur hat (Mehrsprachig-
//  keit, Audiokommentar). Der Controller meldet das über `trackChoiceNeeded`
//  und liefert dabei fertige Zeilen mit - hier wird nichts mehr zusammengebaut,
//  nur angezeigt (Regel 7: Logik in C++).
//
//  Gehostet wird er von der SHELL, nicht von einer Hälfte: `Audio` ist ein
//  Singleton für das ganze Fenster - läge der Dialog je Hälfte vor, gingen bei
//  zwei Hälften zwei Fenster gleichzeitig auf.
//
//  Verwendung:
//      AudioTrackDialog { id: trackDlg }
//      Connections { target: Audio
//          function onTrackChoiceNeeded(src, tracks) { trackDlg.openFor(src, tracks) } }
// ─────────────────────────────────────────────────────────────────────────────
Dialog {
    id: dlg
    modal: true
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.NoButton
    title: App.uiText(App.language, "AudioTrackChooseTitle")
    objectName: "audioTrackDialog"          // Griff für tests/bench (Regel 31)

    property string source: ""
    property var    tracks: []

    function openFor(src, list) {
        dlg.source = src
        dlg.tracks = list
        open()
    }

    width: Math.min(520, Overlay.overlay ? Overlay.overlay.width - 40 : 520)

    background: Rectangle {
        color: App.themeBackground
        border.color: App.themeBorder
        radius: 8
    }
    header: Item {
        implicitHeight: 44
        Text {
            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            text: dlg.title
            color: App.themeTextPrimary
            font.pixelSize: 15
            font.bold: true
        }
    }

    contentItem: Column {
        spacing: 8

        Text {
            width: parent.width
            text: App.uiText(App.language, "AudioTrackChooseHint")
                      .arg(dlg.source.split("/").pop())
            color: App.themeTextMuted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: dlg.tracks
            delegate: Rectangle {
                id: row
                required property var modelData
                width: parent.width
                height: 38
                radius: 6
                //  Eine Spur, die sich nicht herauskopieren lässt, bleibt
                //  SICHTBAR - sonst wirkt die Datei, als hätte sie weniger
                //  Spuren -, ist aber nicht wählbar.
                readonly property bool usable: row.modelData.supported === true
                color: !row.usable ? "transparent"
                                   : (hover.hovered
                                      ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                App.themeAccent.b, 0.30)
                                      : App.themeCard)
                border.color: row.usable ? App.themeBorder : "transparent"
                border.width: 1
                opacity: row.usable ? 1.0 : 0.45

                Text {
                    anchors { left: parent.left; leftMargin: 12; right: parent.right
                              rightMargin: 12; verticalCenter: parent.verticalCenter }
                    text: row.modelData.label
                    color: App.themeTextPrimary
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                HoverHandler { id: hover; enabled: row.usable }
                TapHandler {
                    enabled: row.usable
                    onTapped: {
                        Audio.extractAudio(dlg.source, row.modelData.index)
                        dlg.close()
                    }
                }
            }
        }
    }

    footer: Item {
        implicitHeight: 48
        Button {
            anchors { right: parent.right; rightMargin: 16
                      verticalCenter: parent.verticalCenter }
            text: App.uiText(App.language, "SettingsCancel")
            onClicked: dlg.close()
        }
    }
}
