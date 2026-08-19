import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  ThemedMenu.qml - ein Menü-Popup im Thema der App.
//
//  WARUM ES DAS GIBT: Fusions Popup-Hintergrund folgt weder der in
//  Einstellungen ▸ Design gewählten Menüleisten-Farbe noch der Rundung aller
//  anderen Popups (Filter, Erstellen, Dialoge) - Kontextmenüs sahen deshalb wie
//  Fremdkörper aus: eckig und in der Fusion-Standardfarbe. Jedes Menü der App
//  benutzt deshalb DIESE Fassung; sie lag zuvor als Inline-Komponente in
//  `ApplicationShell` und war damit für die Kontextmenüs (Kachel, PDF-Seite,
//  Tag-Chips …) nicht erreichbar.
//
//  ZWEI FALLEN, die hier gelöst sind:
//   • **Breite:** Fusion leitet die Menübreite aus der `implicitWidth` des
//     Hintergrunds (Standard 200) UND der aggregierten `contentWidth` ab. Ein
//     eigener Hintergrund hat keine `implicitWidth`, und die Auto-`contentWidth`
//     von QQuickMenu liefert hier 0 -> das Menü kollabierte zum 2-px-„Strich".
//     Deshalb wird die Inhaltsbreite reaktiv aus den Items gerechnet (jedes
//     `MenuItem` bringt ≥ 200 px mit und wächst bei langem Text).
//   • **Runde Ecken:** Die Markierung eines Eintrags ist ein Rechteck mit
//     kleinem Radius. Ohne Polster liegt der erste/letzte Eintrag genau auf der
//     Rundung des Hintergrunds und füllt sie aus - das Popup sah unten wieder
//     eckig aus, sobald man den letzten Eintrag berührte (Nutzerbefund an
//     Einstellungen ▸ Videowiedergabe). `topPadding`/`bottomPadding` ≥ Radius
//     halten die Ecken frei.
// ─────────────────────────────────────────────────────────────────────────────
Menu {
    id: control

    topPadding: 6
    bottomPadding: 6

    implicitWidth: {
        var w = 0
        for (var i = 0; i < count; i++) {
            var it = itemAt(i)
            if (it && it.implicitWidth > w) w = it.implicitWidth
        }
        return Math.max(w, 200) + leftPadding + rightPadding
    }

    background: Rectangle {
        implicitWidth: 200
        color: App.themeMenuBarBg
        border.color: App.themeBorder; border.width: 1
        radius: 6
    }
}
