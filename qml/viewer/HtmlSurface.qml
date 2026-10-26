pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtWebEngine
import MediaGallery 1.0

// Gerenderte HTML-Vorschau über Qt WebEngine. INVARIANTE: nur instanziieren, wenn `WebEngine.ready` - deshalb
// lädt der FullscreenViewer sie ausschliesslich über einen URL-Loader und NIE als Typ, sonst zöge schon das
// Übersetzen der referenzierenden Datei das QtWebEngine-Plugin herein. Ohne JavaScript und Netz: anzeigen statt Sandbox.
Item {
    id: root

    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0

    function release() {
        web.stop()
        web.url = "about:blank"
    }

    onSourceChanged: {
        crashNote.visible = false
        web.visible = true
        if (source.length > 0) web.url = App.fileUrl(source)
        else                   { web.stop(); web.url = "about:blank" }
    }

    //  Ein laufender Ladevorgang darf nicht in die Zerstörung durch den Loader hineinlaufen.
    Component.onDestruction: web.stop()

    Rectangle { anchors.fill: parent; color: App.themeBackground }

    WebEngineView {
        id: web
        anchors {
            left: parent.left;  right: parent.right
            top: parent.top;    topMargin: root.topInset
            bottom: parent.bottom; bottomMargin: root.bottomInset
        }
        backgroundColor: "white"

        settings.javascriptEnabled:             false
        settings.localContentCanAccessFileUrls: true    // lokale Bilder/CSS laden
        settings.localContentCanAccessRemoteUrls: false // Netzwerk AUS (keine Web-Fonts/Tracker)

        // Render-Prozess gestorben: nicht neu laden (sonst Crash-Schleife), sondern Hinweis zeigen.
        onRenderProcessTerminated: function(terminationStatus, exitCode) {
            web.visible = false
            crashNote.visible = true
        }

        // Sonst bliebe eine weiße Fläche, nicht unterscheidbar von "hängt"; `about:blank` ist der Ruhezustand.
        onLoadingChanged: function(info) {
            if (info.status === WebEngineView.LoadFailedStatus
                    && root.source.length > 0) {
                web.visible = false
                crashNote.visible = true
            }
        }

        // Ein lokales Dokument darf die Ansicht nicht wegnavigieren: Ziele außerhalb von `file://` werden verworfen,
        // statt die View in einen Ladezustand zu schicken, der nie endet; jede Art, auch Formular und meta refresh.
        onNavigationRequested: function(request) {
            const ziel = request.url.toString()
            if (ziel.indexOf("file:") !== 0 && ziel !== "about:blank")
                request.reject()
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: web.loading && !crashNote.visible
        visible: running
        z: 2
    }

    Rectangle {
        id: crashNote
        anchors.fill: parent
        color: App.themeBackground
        visible: false
        z: 3
        Text {
            anchors.centerIn: parent
            width: Math.min(parent.width - 64, 520)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: App.themeTextMuted
            font.pixelSize: 15
            text: App.uiText(App.language, "ViewerPreviewCrashed")
        }
    }
}
