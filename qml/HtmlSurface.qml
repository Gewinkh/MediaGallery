pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtWebEngine
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  HtmlSurface.qml — gerenderte HTML-Vorschau (Gegenstück zur Quelltext-Ansicht
//  in TextSurface). Wird vom FullscreenViewer für Typ 4 (Text) geladen, sobald
//  die Datei eine .html/.htm ist UND der Vorschau-Modus aktiv ist.
//
//  • Rendert die lokale Datei über Qt WebEngine (Chromium, Teil von Qt 6.11 —
//    keine externe Bibliothek). Pfad → kodierte file://-URL via App.fileUrl()
//    (CJK-/Leerzeichen-fest), analog zur Bild-Komponente.
//  • Policy „offline": JavaScript AN (interaktive Quizze/Suche/Shortcuts der
//    Lernzettel laufen), Netzwerk AUS — eine file://-Seite darf per Default
//    keine entfernten URLs laden; wir setzen das hier explizit, damit z. B.
//    Google-Fonts NICHT nachgeladen werden. Nichts verlässt den Rechner; bei
//    fehlenden Web-Fonts greift die CSS-Fallback-Kette (lokal installierte
//    Familien wie Noto Sans JP).
//  • Lokaler Dateizugriff bleibt erlaubt (eingebundene lokale Bilder/CSS).
//  • topInset/bottomInset werden vom FullscreenViewer reserviert → die globale
//    Leiste/Navigation überdeckt den Inhalt NICHT; die View füllt die Mitte.
//  • Beim Verlassen/Umschalten release() → Laden stoppen + Seite leeren; die
//    eigentliche RAM-/Render-Prozess-Freigabe erfolgt durch das Entladen der
//    Komponente im Loader (genau ein aktives Medium, RAM-Prio 1).
// ─────────────────────────────────────────────────────────────────────────────
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

    // Hintergrund hinter/neben der View (während des Ladens sichtbar).
    Rectangle { anchors.fill: parent; color: App.themeBackground }

    WebEngineView {
        id: web
        anchors {
            left: parent.left;  right: parent.right
            top: parent.top;    topMargin: root.topInset
            bottom: parent.bottom; bottomMargin: root.bottomInset
        }
        // Seitenweiß hinter dem Dokument-Body — passt zu den hellen Lernzetteln
        // und vermeidet ein dunkles Aufblitzen vor dem ersten Paint.
        backgroundColor: "white"

        // ── Offline-Policy (Option 2) ─────────────────────────────────────────
        settings.javascriptEnabled:             true    // Quizze/Suche/Shortcuts
        settings.localStorageEnabled:           true    // lokaler JS-Zustand
        settings.localContentCanAccessFileUrls: true    // lokale Bilder/CSS laden
        settings.localContentCanAccessRemoteUrls: false // Netzwerk AUS (keine Web-Fonts/Tracker)

        // Render-Prozess gestorben (Crash/OOM/vom System beendet): NICHT
        // automatisch neu laden (sonst Crash-Schleife). Stattdessen View leeren
        // und eine Hinweisfläche zeigen — der Nutzer kann oben auf den Quelltext
        // umschalten oder die Datei neu öffnen. Verhindert „weißer Hänger".
        onRenderProcessTerminated: function(terminationStatus, exitCode) {
            web.visible = false
            crashNote.visible = true
        }
    }

    // Dezenter Ladeindikator (größere Sheets brauchen einen Moment zum Rendern).
    BusyIndicator {
        anchors.centerIn: parent
        running: web.loading && !crashNote.visible
        visible: running
        z: 2
    }

    // Hinweisfläche, falls der WebEngine-Render-Prozess abstürzt (s. oben).
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
