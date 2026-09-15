import QtQuick
import MediaGallery 1.0
import "../common"

// Tastenkürzel-Übersicht aus einem reinen JS-Modell; die Beschreibungen stehen bewusst zweisprachig inline,
// statt den Strings-Katalog um ~50 Einzweck-Labels aufzublähen. Diese Liste ist die EINZIGE Quelle der Kürzel -
// die Tabelle in FEATURES.md folgt ihr, und core.shortcuts prüft, dass jedes Shortcut{} der Oberfläche hier steht.
Item {
    id: rootCol
    implicitHeight: col.implicitHeight

    readonly property bool de: App.language === "de"

    //  Ein Kürzel-Block je Kontext: { headerKey, rows: [ {de,en,keys:[…]} ] }.
    //  keys mit mehreren Einträgen = gleichwertige Alternativen (F5 / R).
    readonly property var sections: [
        { headerKey: "ShortcutCtxGallery", rows: [
            { de: "Ordner öffnen",                 en: "Open folder",                 keys: ["Ctrl+O"] },
            { de: "Ins Suchfeld springen",         en: "Jump to the search field",    keys: ["Ctrl+F"] },
            { de: "Neu laden / Vorschau erneuern", en: "Reload / refresh thumbnails", keys: ["F5", "R"] },
            { de: "Alles wählen, was der Filter zeigt", en: "Select everything shown", keys: ["Ctrl+A"] },
            { de: "Auswahl aufheben",              en: "Clear the selection",         keys: ["Esc"] },
            { de: "Dateien kopieren / einfügen",   en: "Copy / paste files",          keys: ["Ctrl+C", "Ctrl+V"] },
            { de: "Auswahl löschen",               en: "Delete the selection",        keys: ["Entf"] },
            { de: "Aus dem Unterordner zurück",    en: "Back out of a subfolder",     keys: ["Alt+<-"] },
            { de: "Optionen-Modus umschalten",     en: "Toggle options mode",         keys: ["Alt+S"] },
            { de: "Audio-Player-Modus umschalten", en: "Toggle audio player mode",    keys: ["Alt+A"] },
            { de: "Abspielen / Pause (Player-Modus)", en: "Play / pause (player mode)", keys: ["Leertaste"] },
            { de: "Nächster / vorheriger Titel (Player-Modus)",
              en: "Next / previous track (player mode)",                              keys: ["->", "<-"] },
            { de: "Vollbild ein-/ausschalten",     en: "Toggle fullscreen",           keys: ["F"] },
            { de: "Vorschau-Sperre umschalten",    en: "Toggle cover mode",           keys: ["B"] },
            { de: "Kachelgröße größer / kleiner",  en: "Tile size larger / smaller",  keys: ["Ctrl++", "Ctrl+-"] },
            { de: "Vollbild öffnen",               en: "Open fullscreen",             keys: ["Doppelklick"] },
            { de: "Dateiaktion rückgängig / wiederholen",
              en: "Undo / redo a file operation", keys: ["Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+Y"] }
        ] },
        { headerKey: "ShortcutCtxViewer", rows: [
            { de: "Nächste / vorherige Datei",     en: "Next / previous item",        keys: ["->", "<-"] },
            { de: "Vollbild ein-/ausschalten",     en: "Toggle fullscreen",           keys: ["F"] },
            { de: "Video vor-/zurückspulen (Vollbild)",
              en: "Seek video forward / back (fullscreen)",                           keys: ["->", "<-"] },
            { de: "Zurück zur Galerie",            en: "Back to gallery",             keys: ["Esc", "Alt+<-"] },
            { de: "Optionen-Modus umschalten",     en: "Toggle options mode",         keys: ["Alt+S"] },
            { de: "Datum bearbeiten",              en: "Edit date",                   keys: ["D"] }
        ] },
        { headerKey: "ShortcutCtxPdf", rows: [
            { de: "Herein- / herauszoomen",        en: "Zoom in / out",               keys: ["+", "-"] },
            { de: "Im Dokument suchen",            en: "Search the document",         keys: ["Ctrl+F"] },
            { de: "Markierten Text kopieren",      en: "Copy selected text",          keys: ["Ctrl+C"] },
            { de: "Ganze Seite markieren",         en: "Select all text on page",     keys: ["Ctrl+A"] },
            { de: "Notizen ein-/ausblenden",       en: "Toggle note visibility",      keys: ["Alt+Q"] },
            { de: "Ausgewählte Notiz löschen",     en: "Delete selected annotation",  keys: ["Entf"] },
            { de: "Notiz kopieren / einfügen",     en: "Copy / paste annotation",     keys: ["Ctrl+C", "Ctrl+V"] },
            { de: "Rückgängig / Wiederholen",      en: "Undo / redo",                 keys: ["Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+Y"] },
        ] },
        { headerKey: "ShortcutCtxImage", rows: [
            { de: "Notizen ein-/ausblenden",       en: "Toggle note visibility",      keys: ["Alt+Q"] },
            { de: "Ausgewählte Notiz löschen",     en: "Delete selected annotation",  keys: ["Entf"] },
            { de: "Notiz kopieren / einfügen",     en: "Copy / paste annotation",     keys: ["Ctrl+C", "Ctrl+V"] },
            { de: "Rückgängig / Wiederholen",      en: "Undo / redo",                 keys: ["Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+Y"] },
        ] },
        { headerKey: "ShortcutCtxDocx", rows: [
            { de: "Speichern",                     en: "Save",                        keys: ["Ctrl+S"] },
            { de: "Fett / kursiv / unterstrichen", en: "Bold / italic / underline",   keys: ["Ctrl+B", "Ctrl+I", "Ctrl+U"] },
            { de: "Suchen & Ersetzen",             en: "Find & Replace",              keys: ["Ctrl+F"] },
            { de: "Alles / kopieren / ausschneiden / einfügen",
              en: "Select all / copy / cut / paste", keys: ["Ctrl+A", "Ctrl+C", "Ctrl+X", "Ctrl+V"] },
            { de: "Zeilenumbruch im Absatz",       en: "Line break inside paragraph", keys: ["Shift+↵"] },
            { de: "Rückgängig / Wiederholen",      en: "Undo / redo",                 keys: ["Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+Y"] },
        ] },
        { headerKey: "ShortcutCtxText", rows: [
            { de: "Speichern",                     en: "Save",                        keys: ["Ctrl+S"] },
            { de: "Suchen & Ersetzen",             en: "Find & replace",              keys: ["Ctrl+F"] },
            { de: "Nächster / vorheriger Treffer", en: "Next / previous match",       keys: ["↵", "Shift+↵"] },
            { de: "Suche schließen",               en: "Close search",                keys: ["Esc"] },
            { de: "Rückgängig / Wiederholen",      en: "Undo / redo",                 keys: ["Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+Y"] },
            { de: "Einrücken (schreibt Leerzeichen)", en: "Indent (writes spaces)",   keys: ["Tab"] },
            { de: "Anfang / Ende der Datei",        en: "Start / end of the file",     keys: ["Ctrl+Home", "Ctrl+End"] },
            { de: "Seite blättern (Cursor bleibt)", en: "Scroll a page (cursor stays)", keys: ["Page Up", "Page Down"] }
        ] },
        { headerKey: "ShortcutCtxTable", rows: [
            { de: "Zelle markieren",               en: "Mark a cell",                 keys: ["Klick"] },
            { de: "Zelle bearbeiten",              en: "Edit cell",                   keys: ["Doppelklick", "F2"] },
            { de: "Markierung bewegen",            en: "Move the mark",               keys: ["->", "<-", "Tab", "Shift+Tab"] },
            { de: "Zelle / Zeile kopieren",        en: "Copy cell / row",             keys: ["Ctrl+C", "Ctrl+Shift+C"] },
            { de: "Einfügen (auch mehrere Zellen)", en: "Paste (several cells too)",  keys: ["Ctrl+V"] },
            { de: "Rückgängig / Wiederholen",      en: "Undo / redo",                 keys: ["Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+Y"] },
            { de: "Speichern (mit Rückmeldung)",   en: "Save (with a check)",         keys: ["Ctrl+S"] },
            { de: "Suchen",                        en: "Find",                        keys: ["Ctrl+F"] },
            { de: "Markierung aufheben",           en: "Remove the mark",             keys: ["Esc"] }
        ] }
    ]

    //  Einzelnes Tasten-„Kbd"-Chip (gethemt, monospace-artig).
    component Kbd: Rectangle {
        id: kbd
        property string label: ""
        // Die Pfeiltasten tragen ihr Zeichen als Symbol - hier IST der Pfeil die Taste. Er kann allein stehen oder am
        // Ende eines zusammengesetzten Kürzels, deshalb wird die Beschriftung geteilt statt nur verglichen.
        readonly property string arrowPart:
            kbd.label.endsWith("->") ? "->" : (kbd.label.endsWith("<-") ? "<-" : "")
        readonly property string textPart:
            kbd.arrowPart.length > 0
            ? kbd.label.slice(0, kbd.label.length - 2) : kbd.label
        implicitWidth: kbdRow.implicitWidth + 14
        implicitHeight: 22
        radius: 5
        color: App.themeCard
        border.color: App.themeBorder
        border.width: 1
        Row {
            id: kbdRow
            anchors.centerIn: parent
            spacing: 0
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: kbd.textPart.length > 0
                text: kbd.textPart
                color: App.themeTextPrimary
                font.pixelSize: 11
                font.bold: true
            }
            DrawnIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: kbd.arrowPart.length > 0
                name: kbd.arrowPart === "->" ? "arrow-right" : "arrow-left"
                size: 13
                color: App.themeTextPrimary
            }
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
