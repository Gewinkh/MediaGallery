import QtQuick
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  GalleryStub.qml — Platzhalter der Galerie-Seite (Phase 1).
//
//  Die echte GridView-basierte Galerie + MediaModel folgt in Phase 2. Hier nur
//  ein Rechteck mit Statuslabel, das den aktuell geladenen Ordner spiegelt und
//  damit Bridge + Theme + Ordner-Restore sichtbar verifiziert.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: root
    color: App.themeBackground

    Column {
        anchors.centerIn: parent
        spacing: 10
        width: Math.min(root.width - 48, 640)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Galerie — Platzhalter (Phase 2)"
            color: App.themeAccent
            font.pixelSize: 20
            font.bold: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            text: App.currentFolder.length > 0
                  ? App.currentFolder
                  : "Kein Ordner geöffnet — Datei ▸ Ordner öffnen oder hierher ziehen"
            color: App.themeTextMuted
            font.pixelSize: 13
        }
    }
}
