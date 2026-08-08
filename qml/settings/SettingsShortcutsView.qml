import QtQuick
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  SettingsShortcutsView.qml — gethemte Tastenkürzel-Übersicht (Einstellungen ▸
//  Allgemein). Zeigt je Kontext (Galerie, Medienansicht, PDF-/Bild-/DOCX-/
//  Texteditor) die WIRKLICH implementierten Kürzel mit Funktion + Tasten.
//
//  Datenquelle ist ein reines JS-Modell (unten). Die Funktionsbeschreibungen
//  sind hier BEWUSST zweisprachig inline (de/en) statt über eigene String-Keys:
//  eine Kürzel-Referenz ist EINE zusammenhängende Tabelle — Funktion + Tasten
//  stehen so an einer Stelle beisammen, ohne den (gezählten) Strings-Katalog um
//  ~35 Einzweck-Labels aufzublähen. Die Kontext-Überschriften laufen dagegen
//  über String-Keys (wiederverwendbar, s. ShortcutCtx*).
//
//  Wichtig: Diese Liste ist die nutzersichtbare Wahrheit über die Kürzel und
//  MUSS mit den tatsächlichen Shortcut{}-Definitionen (ApplicationShell,
//  FullscreenViewer, PdfSurface, ImageSurface, DocxSurface, TextSurface)
//  übereinstimmen. Bei Änderungen dort hier UND in der README-Tabelle nachziehen.
// ─────────────────────────────────────────────────────────────────────────────
//  Wurzel ist bewusst ein Item mit einer inneren Column: Eine Column als
//  Layout-Kind würde ihre implicitWidth aus der Breite ihrer Kinder ableiten,
//  die ihrerseits an die von der ColumnLayout gesetzte Breite gebunden ist —
//  eine Rückkopplung, die bei jeder Geometrieänderung eine komplette
//  Neuberechnung der Einstellungsseite anstößt. Das Item meldet implicitWidth 0
//  (Layout.fillWidth bestimmt die Breite) und nur die Höhe nach oben.
Item {
    id: rootCol
    implicitHeight: col.implicitHeight

    readonly property bool de: App.language === "de"

    //  Ein Kürzel-Block je Kontext: { headerKey, rows: [ {de,en,keys:[…]} ] }.
    //  keys mit mehreren Einträgen = gleichwertige Alternativen (F5 / R).
    readonly property var sections: [
        { headerKey: "ShortcutCtxGallery", rows: [
            { de: "Ordner öffnen",                 en: "Open folder",                 keys: ["Ctrl+O"] },
            { de: "Neu laden / Vorschau erneuern", en: "Reload / refresh thumbnails", keys: ["F5", "R"] },
            { de: "Optionen-Modus umschalten",     en: "Toggle options mode",         keys: ["Alt+S"] },
            { de: "Vorschau-Sperre umschalten",    en: "Toggle cover mode",           keys: ["B"] },
            { de: "Kachelgröße größer / kleiner",  en: "Tile size larger / smaller",  keys: ["Ctrl++", "Ctrl+-"] },
            { de: "Vollbild öffnen",               en: "Open fullscreen",             keys: ["Doppelklick"] }
        ] },
        { headerKey: "ShortcutCtxViewer", rows: [
            { de: "Nächste / vorherige Datei",     en: "Next / previous item",        keys: ["→", "←"] },
            { de: "Vollbild ein-/ausschalten",     en: "Toggle fullscreen",           keys: ["F"] },
            { de: "Video vor-/zurückspulen (Vollbild)",
              en: "Seek video forward / back (fullscreen)",                           keys: ["→", "←"] },
            { de: "Zurück zur Galerie",            en: "Back to gallery",             keys: ["Esc", "Alt+←"] },
            { de: "Optionen-Modus umschalten",     en: "Toggle options mode",         keys: ["Alt+S"] },
            { de: "Datum bearbeiten",              en: "Edit date",                   keys: ["D"] }
        ] },
        { headerKey: "ShortcutCtxPdf", rows: [
            { de: "Herein- / herauszoomen",        en: "Zoom in / out",               keys: ["+", "-"] },
            { de: "Markierten Text kopieren",      en: "Copy selected text",          keys: ["Ctrl+C"] },
            { de: "Ganze Seite markieren",         en: "Select all text on page",     keys: ["Ctrl+A"] },
            { de: "Notizen ein-/ausblenden",       en: "Toggle note visibility",      keys: ["Alt+Q"] },
            { de: "Ausgewählte Notiz löschen",     en: "Delete selected annotation",  keys: ["Entf"] },
            { de: "Notiz kopieren / einfügen",     en: "Copy / paste annotation",     keys: ["Ctrl+C", "Ctrl+V"] },
            { de: "Rückgängig / Wiederholen",      en: "Undo / redo",                 keys: ["Ctrl+Z", "Ctrl+Shift+Z"] },
            { de: "Kopf-/Fußzeile verlassen",      en: "Leave header/footer editing", keys: ["Esc"] }
        ] },
        { headerKey: "ShortcutCtxImage", rows: [
            { de: "Notizen ein-/ausblenden",       en: "Toggle note visibility",      keys: ["Alt+Q"] },
            { de: "Ausgewählte Notiz löschen",     en: "Delete selected annotation",  keys: ["Entf"] },
            { de: "Notiz kopieren / einfügen",     en: "Copy / paste annotation",     keys: ["Ctrl+C", "Ctrl+V"] },
            { de: "Rückgängig / Wiederholen",      en: "Undo / redo",                 keys: ["Ctrl+Z", "Ctrl+Shift+Z"] },
            { de: "Kopf-/Fußzeile verlassen",      en: "Leave header/footer editing", keys: ["Esc"] }
        ] },
        { headerKey: "ShortcutCtxDocx", rows: [
            { de: "Speichern",                     en: "Save",                        keys: ["Ctrl+S"] },
            { de: "Fett / kursiv / unterstrichen", en: "Bold / italic / underline",   keys: ["Ctrl+B", "Ctrl+I", "Ctrl+U"] },
            { de: "Suchen & Ersetzen",             en: "Find & Replace",              keys: ["Ctrl+F"] },
            { de: "Alles / kopieren / ausschneiden / einfügen",
              en: "Select all / copy / cut / paste", keys: ["Ctrl+A", "Ctrl+C", "Ctrl+X", "Ctrl+V"] },
            { de: "Zeilenumbruch im Absatz",       en: "Line break inside paragraph", keys: ["Shift+↵"] },
            { de: "Rückgängig / Wiederholen",      en: "Undo / redo",                 keys: ["Ctrl+Z", "Ctrl+Shift+Z"] },
            { de: "Kopf-/Fußzeile verlassen",      en: "Leave header/footer editing", keys: ["Esc"] }
        ] },
        { headerKey: "ShortcutCtxText", rows: [
            { de: "Speichern",                     en: "Save",                        keys: ["Ctrl+S"] }
        ] }
    ]

    //  Einzelnes Tasten-„Kbd"-Chip (gethemt, monospace-artig).
    component Kbd: Rectangle {
        property string label: ""
        implicitWidth: kbdTxt.implicitWidth + 14
        implicitHeight: 22
        radius: 5
        color: App.themeCard
        border.color: App.themeBorder
        border.width: 1
        Text {
            id: kbdTxt
            anchors.centerIn: parent
            text: parent.label
            color: App.themeTextPrimary
            font.pixelSize: 11
            font.bold: true
        }
    }

    Column {
        id: col
        width: rootCol.width
        spacing: 14

        Repeater {
            model: rootCol.sections
            delegate: Column {
                required property var modelData
                width: col.width
                spacing: 6

                //  Kontext-Überschrift (Akzentfarbe, kleine Linie darunter).
                Row {
                    spacing: 8
                    width: parent.width
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3; height: 13; radius: 1.5
                        color: App.themeAccent
                    }
                    Text {
                        text: App.uiText(App.language, modelData.headerKey)
                        color: App.themeTextPrimary
                        font.pixelSize: 12
                        font.bold: true
                    }
                }

                //  Zeilen dieses Kontexts.
                Repeater {
                    model: modelData.rows
                    delegate: Item {
                        id: rowItem
                        required property var modelData
                        width: parent.width
                        height: Math.max(24, keysFlow.implicitHeight + 4)

                        Text {
                            id: descTxt
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width * 0.52
                            text: rootCol.de ? rowItem.modelData.de : rowItem.modelData.en
                            color: App.themeTextMuted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }

                        //  Tasten-Chips rechts; mehrere Alternativen mit „/"-Trenner.
                        Flow {
                            id: keysFlow
                            anchors.left: descTxt.right
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 10
                            spacing: 5

                            Repeater {
                                model: rowItem.modelData.keys
                                delegate: Row {
                                    id: keyRow
                                    required property int index
                                    required property string modelData
                                    spacing: 5
                                    Text {
                                        visible: keyRow.index > 0
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "/"
                                        color: App.themeTextMuted
                                        font.pixelSize: 11
                                    }
                                    Kbd { label: keyRow.modelData }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
