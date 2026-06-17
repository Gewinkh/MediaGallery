import QtQuick
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  FullscreenStub.qml — Platzhalter der Vollbild-Seite (Phase 1).
//
//  Leerer Stub bis Phase 3 (FullscreenViewer/VideoPlayer/PDF). Existiert nur,
//  damit die StackView-Navigation der Shell vollständig verdrahtet ist.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: root
    color: App.themeBackground

    signal backRequested()

    Text {
        anchors.centerIn: parent
        text: "Vollbild — Platzhalter (Phase 3)"
        color: App.themeTextMuted
        font.pixelSize: 16
    }

    Keys.onEscapePressed: root.backRequested()
}
