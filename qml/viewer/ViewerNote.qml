import QtQuick
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  ViewerNote.qml - die zwei Hinweistexte des Vollbilds („extern geöffnet",
//  „kein Betrachter dafür"), als EIGENE Datei.
//
//  Warum eine Datei und keine Inline-Komponente: Der `surface`-Loader des
//  `FullscreenViewer` wählt seine Fläche seit dem Start-Durchgang über eine
//  URL, nicht über einen Typnamen - ein Typname zwingt QML, die Datei schon
//  beim Übersetzen des Viewers zu laden (gemessen: alle Flächen zusammen rund
//  100 ms bei JEDEM Start, obwohl beim Start keine davon gebraucht wird). Ein
//  Loader kann `source` und `sourceComponent` nicht mischen, deshalb sind auch
//  diese beiden Kleinigkeiten Dateien.
//
//  `kind` wählt den Text: "external" oder "unsupported".
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: note
    //  Teil des Surface-Vertrags (der Viewer setzt sie an jeder Fläche).
    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0
    property string kind: "unsupported"
    function release() {}

    Text {
        anchors.centerIn: parent
        text: note.kind === "external"
              ? App.uiText(App.language, "ViewerOpenedExternal")
              : App.uiText(App.language, "ViewerNoRenderer")
        color: note.kind === "external" ? "#c8dbd5" : "#888"
        font.pixelSize: note.kind === "external" ? 16 : 15
    }
}
