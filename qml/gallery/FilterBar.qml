pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  FilterBar.qml - Filter-/Sortierleiste (ersetzt FilterBar(QWidget)).
//
//  Bindet ausschließlich serverseitig an den Proxy (galleryModel). Keine
//  Filterlogik in QML.
//
//  Layout:
//    • Links steht NUR der "Filter"-Button. Er oeffnet ein MASTER-DETAIL-Popup:
//      links eine Spalte mit Rubriken (Sortieren, Medien, Tag-Filtermodus, Tags
//      & Kategorien), rechts daneben die Optionen der gewaehlten Rubrik. Das
//      haelt jede Rubrik uebersichtlich getrennt.
//    • Die SORTIERUNG (Feld + Richtung) ist eine Rubrik darin - frueher standen
//      Sortierfeld und ein Richtungspfeil-Knopf inline in der Leiste.
//    • Aktive Tag-Chips + "Leeren" bleiben INLINE rechts.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: bar
    implicitHeight: 46
    color: App.themeFilterBarBg

    signal enterAddToTagMode(string tag)
    // Panel-Steuerung (Seitenpanel rechts): beide Abschnitte INDIVIDUELL
    // schaltbar. Der Zustand lebt im TagCategoryPanel (showTagsSection/
    // showCategoriesSection) und wird von der Shell in diese Properties
    // gespiegelt -> die Toggle-Zeilen im Filter-Popup zeigen den Aktiv-Zustand.
    signal tagPanelToggled()
    signal categoryPanelToggled()
    // „Extrahieren": globale Seiten-Extraktion anstoßen (Dialog hostet die Shell).
    //  Farbe eines Tags - erst der offene Ordner, dann die aufgeklappten
    //  Unterordner (s. MediaModel::visibleTagColor). Kennt ihn niemand, bleibt
    //  es bei der Vorgabe des Themes.
    function tagColorOf(tag) {
        var c = mediaModel.visibleTagColor(tag)
        return (c && c.a > 0) ? c : Tags.tagColor(tag)
    }

    //  Ordner, in dem „+ Erstellen" und „PDF-Seiten…" arbeiten. Leer = der
    //  geoeffnete Ordner; die Kopfzeile eines aufgeklappten Bereichs setzt hier
    //  ihren eigenen Ordner, bevor sie das Popup oeffnet.
    property string actionFolder: ""
    signal extractPagesRequested(string folder)
    //  „+ Ordner" für den GEÖFFNETEN Ordner - die Kopfzeile eines aufgeklappten
    //  Bereichs hat ihren eigenen Knopf für ihren Ordner.
    signal newFolderRequested(string folder)

    //  Von der Kopfzeile eines aufgeklappten Bereichs aufgerufen.
    function openCreateFor(folder) {
        bar.actionFolder = folder
        createNameField.text = ""
        createPopup.kind = "pdf"
        createPopup.open()
        createNameField.forceActiveFocus()
    }
    property bool tagPanelVisible: false
    property bool categoryPanelVisible: false

    property var activeTags: []

    // ── Referenzbasierte Konsistenz ──────────────────────────────────────────
    //  Referenzquelle des Tag-Filters ist der Proxy (galleryModel.tagFilter).
    //  Externe Änderungen - Tag-Toggle im Kategorie-Panel, Kaskaden-
    //  Deaktivierung beim Abwählen von Kategorien - spiegeln sich hier, damit
    //  die Inline-Chips und das Filter-Badge nie veralten.
    Component.onCompleted: activeTags = galleryModel.tagFilter
    Connections {
        target: galleryModel
        function onFilterChanged() {
            if (bar.activeTags.join("\u001f") !== galleryModel.tagFilter.join("\u001f"))
                bar.activeTags = galleryModel.tagFilter
        }
    }

    readonly property var modeNames:  [App.uiText(App.language, "FilterTagModeOr"), App.uiText(App.language, "FilterTagModeAnd"), App.uiText(App.language, "FilterTagModeNur"), App.uiText(App.language, "FilterTagModeInklusiv")]
    readonly property var modeTips: [
        App.uiText(App.language, "FilterModeAnyDesc"),
        App.uiText(App.language, "FilterModeAllDesc"),
        App.uiText(App.language, "FilterModeExclusiveDesc"),
        App.uiText(App.language, "FilterModeInclusiveDesc")
    ]

    //  Im Player-Modus (Alt+A) gibt es nur Ton: Bilder, PDF und Text stehen dort
    //  nicht zur Wahl - sie wären in dieser Ansicht ohnehin ausgefiltert.
    property bool audioOnly: false
    readonly property var mediaTypes: bar.audioOnly
        ? [
            { label: "Videos", key: 1 },
            { label: App.uiText(App.language, "FilterAudio"), key: 2 }
          ]
        : [
            { label: "Bilder", key: 0 },
            { label: "Videos", key: 1 },
            { label: App.uiText(App.language, "FilterAudio"),  key: 2 },
            { label: "PDF",    key: 3 },
            { label: "Text",   key: 4 }
          ]
    function mediaShown(key) {
        switch (key) {
        case 0: return galleryModel.showImages
        case 1: return galleryModel.showVideos
        case 2: return galleryModel.showAudio
        case 3: return galleryModel.showPdfs
        default: return galleryModel.showTexts
        }
    }
    function setMediaShown(key, v) {
        switch (key) {
        case 0: galleryModel.showImages = v; break
        case 1: galleryModel.showVideos = v; break
        case 2: galleryModel.showAudio  = v; break
        case 3: galleryModel.showPdfs   = v; break
        default: galleryModel.showTexts = v
        }
    }
    readonly property int mediaActiveCount: bar.audioOnly
        ? ((galleryModel.showVideos ? 1 : 0) + (galleryModel.showAudio ? 1 : 0))
        : ((galleryModel.showImages ? 1 : 0) + (galleryModel.showVideos ? 1 : 0)
           + (galleryModel.showAudio ? 1 : 0) + (galleryModel.showPdfs ? 1 : 0)
           + (galleryModel.showTexts ? 1 : 0))

    //  Sortierfelder in der Reihenfolge von MediaProxyModel::sortRole (0..3).
    readonly property var sortFields: [
        App.uiText(App.language, "FilterDate"),
        App.uiText(App.language, "FilterName"),
        App.uiText(App.language, "FilterTags"),
        App.uiText(App.language, "FilterFileSize")
    ]

    //  Eine Sortierung ist KEIN Filter: sie zählt bewusst weder in den Zähler am
    //  Knopf noch in den Aktiv-Rahmen - sonst sähe der Knopf dauerhaft „aktiv"
    //  aus, denn sortiert wird immer.
    readonly property bool anyFilterActive:
        mediaActiveCount < mediaTypes.length || activeTags.length > 0
    readonly property int filterBadge:
        (mediaActiveCount < mediaTypes.length ? 1 : 0) + activeTags.length

    //  Auswahlzeile (Radio-Optik) der Sortier-Rubrik - vier Felder und zwei
    //  Richtungen sähen sonst sechsmal identisch aus.
    component SortRow: Rectangle {
        id: sortRow
        property string label: ""
        property bool   selected: false
        signal picked()

        width: 236; height: 34; radius: 6
        color: selected ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.18)
                        : (srHover.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
        border.width: 1
        border.color: selected ? App.themeAccent : App.themeBorder

        Text {
            id: srDot
            anchors.left: parent.left; anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            text: sortRow.selected ? "\u25CF" : "\u25CB"   // wie die Modus-Zeilen
            color: sortRow.selected ? App.themeAccent : App.themeTextMuted
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            anchors.left: srDot.right; anchors.leftMargin: 9
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: sortRow.label
            color: sortRow.selected ? App.themeAccent : App.themeTextPrimary
            font.pixelSize: 14
            font.weight: sortRow.selected ? Font.Bold : Font.Normal
            elide: Text.ElideRight
        }
        HoverHandler { id: srHover }
        TapHandler { onTapped: sortRow.picked() }
    }

    function toggleTag(tag) {
        var a = activeTags.slice()
        var i = a.indexOf(tag)
        if (i >= 0) a.splice(i, 1); else a.push(tag)
        activeTags = a
        galleryModel.tagFilter = a
    }
    function clearTags() { activeTags = []; galleryModel.tagFilter = [] }
    //  Strg+F aus der Shell: Fokus ins Suchfeld, vorhandener Text markiert -
    //  ein zweites Strg+F beginnt damit sofort eine neue Suche.
    function focusSearch() { searchInput.forceActiveFocus(); searchInput.selectAll() }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: 1
        color: App.themeBorder
    }

    //  Blätterbar statt abgeschnitten (Strg + Rad, Muster aus dem PDF-Editor):
    //  in einer schmalen Hälfte passten Filter, Suche, „+" und „Extrahieren"
    //  nicht mehr nebeneinander, und die rechten waren unerreichbar.
    ScrollableBar {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 8

        // ── Sammel-Button "Filter" (Master-Detail-Popup) ─────────────────────
        //  EINZIGER Knopf links: Sortierung UND Filter sitzen darin. Vorher
        //  standen Sortierfeld und Richtungspfeil daneben in der Leiste.
        //  Gezeichneter Rahmen (kein nackter Fusion-`Button`), der Pfeil als
        //  SVG rechts (Regel 29) - er rutschte sonst mit der Textbreite.
        Button {
            id: filterBtn
            anchors.verticalCenter: parent.verticalCenter
            height: 30
            padding: 0
            onClicked: filterPopup.opened ? filterPopup.close() : filterPopup.open()

            background: Rectangle {
                implicitWidth: filterRow.implicitWidth + 20
                color: filterBtn.down ? App.themeCard
                     : (filterBtn.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                                    App.themeTextPrimary.b, 0.08)
                                          : App.themeMenuBarBg)
                border.color: bar.anyFilterActive ? App.themeAccent : App.themeBorder
                border.width: 1
                radius: 4
            }
            contentItem: Row {
                id: filterRow
                spacing: 6
                leftPadding: 10
                rightPadding: 10
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: bar.filterBadge > 0
                          ? App.uiText(App.language, "FilterBtn") + " (" + bar.filterBadge + ")"
                          : App.uiText(App.language, "FilterBtn")
                    color: bar.anyFilterActive ? App.themeAccent : App.themeTextPrimary
                    font.pixelSize: 13
                }
                DrawnIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-down"
                    size: 12
                    color: bar.anyFilterActive ? App.themeAccent : App.themeTextMuted
                }
            }

            Popup {
                id: filterPopup
                y: filterBtn.height + 4
                padding: 0
                modal: false
                focus: true

                // Aktuell gewaehlte Kategorie (Master-Spalte): 0..3
                property int selectedCat: 0
                //  Tag-Liste beim Öffnen auffrischen. `mediaModel.visibleTags()`
                //  statt `App.allTags()`: jeder Ordner führt seine eigene
                //  Liste - angeboten wird, was gerade zu SEHEN ist, also auch
                //  die Tags aufgeklappter Unterordner. Sonst gäbe es dort
                //  einen Tag, nach dem man nicht filtern kann.
                property var popupTags: []
                onAboutToShow: popupTags = mediaModel.visibleTags()

                // "Tags" und "Kategorien" sind zu EINEM Eintrag zusammengelegt:
                // Panel-Steuerung (Tag-/Kategorie-Panel individuell) + Tag-Schnellfilter.
                readonly property var cats: [
                    { label: App.uiText(App.language, "FilterSortLabel"),
                      hint: bar.sortFields[galleryModel.sortRole] + " · "
                            + App.uiText(App.language, galleryModel.sortDescending ? "FilterSortDesc" : "FilterSortAsc") },
                    { label: App.uiText(App.language, "FilterMedia"), hint: bar.mediaActiveCount + "/" + bar.mediaTypes.length },
                    { label: App.uiText(App.language, "FilterTagModeLabel"), hint: bar.modeNames[galleryModel.tagFilterMode] },
                    { label: App.uiText(App.language, "FilterTagsCatsLabel"),
                      hint: bar.activeTags.length > 0 ? bar.activeTags.length + App.uiText(App.language, "FilterActiveSuffix") : "-" }
                ]

                background: Rectangle {
                    color: App.themeMenuBarBg
                    border.color: App.themeBorder; border.width: 1
                    radius: 6
                }

                contentItem: Row {
                    id: popupRow
                    spacing: 0

                    // Maximalhöhe des Popups; darüber wird im Detail gescrollt.
                    readonly property int maxBodyH: 400
                    // Natürliche Höhe der Master-Spalte (Anzahl Kategorien).
                    readonly property real navH:
                        filterPopup.cats.length * 44 + (filterPopup.cats.length - 1) * 4 + 16
                    // Natürliche Höhe des aktuell geladenen Detail-Inhalts (+ Padding).
                    readonly property real detailH:
                        detailLoader.item ? detailLoader.item.implicitHeight + 20 : 0
                    // Höhe = größerer der beiden Inhalte, gedeckelt -> kein Leerraum,
                    // aber scrollbar sobald es zu groß wird. (Als Property statt
                    // height der Row, damit das Popup-Sizing nicht kollidiert.)
                    readonly property real bodyH: Math.min(maxBodyH, Math.max(navH, detailH))

                    // ── Master: Kategorie-Buttons ─────────────────────────────
                    //  200 statt 184: der Hinweis der Sortier-Rubrik trägt Feld
                    //  UND Richtung („Dateigröße · Absteigend") - schmaler
                    //  schnitt er ihn ab.
                    Item {
                        width: 200
                        height: popupRow.bodyH
                        Column {
                            id: masterCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4
                            Repeater {
                                model: filterPopup.cats
                                delegate: Rectangle {
                                    id: catBtn
                                    required property int index
                                    required property var modelData
                                    readonly property bool sel: filterPopup.selectedCat === index
                                    width: masterCol.width
                                    height: 44; radius: 6
                                    color: sel ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.18)
                                               : (catHover.hovered ? Qt.rgba(1,1,1,0.06) : "transparent")
                                    border.width: 1
                                    border.color: sel ? App.themeAccent : "transparent"
                                    Row {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10; anchors.rightMargin: 10
                                        spacing: 8
                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 1
                                            Text {
                                                text: catBtn.modelData.label
                                                color: catBtn.sel ? App.themeAccent : App.themeTextPrimary
                                                font.pixelSize: 14
                                                font.weight: catBtn.sel ? Font.DemiBold : Font.Normal
                                            }
                                            Text {
                                                text: catBtn.modelData.hint
                                                color: App.themeTextMuted; font.pixelSize: 12
                                                width: masterCol.width - 34; elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    Text {
                                        anchors.right: parent.right; anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "\u25B8"
                                        color: catBtn.sel ? App.themeAccent : App.themeTextMuted
                                        font.pixelSize: 13
                                    }
                                    HoverHandler { id: catHover }
                                    TapHandler { onTapped: filterPopup.selectedCat = catBtn.index }
                                }
                            }
                        }
                    }

                    Rectangle { width: 1; height: popupRow.bodyH; color: App.themeBorder }

                    // ── Detail: Optionen der gewaehlten Kategorie (scrollbar) ──
                    Item {
                        width: 260
                        height: popupRow.bodyH
                        ScrollView {
                            anchors.fill: parent
                            clip: true
                            leftPadding: 10; rightPadding: 10
                            topPadding: 10; bottomPadding: 10
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            Loader {
                                id: detailLoader
                                width: 236
                                sourceComponent: filterPopup.selectedCat === 0 ? sortComp
                                               : filterPopup.selectedCat === 1 ? medienComp
                                               : filterPopup.selectedCat === 2 ? modeComp
                                               : tagsCatsComp
                            }
                        }
                    }
                }

                // ── Detail-Komponenten ────────────────────────────────────────

                //  Sortieren: Feld UND Richtung als sichtbare Optionen. Die
                //  Richtung war vorher ein kleiner Pfeil-Knopf neben der Leiste -
                //  man sah ihr nicht an, was sie gerade bedeutet.
                Component {
                    id: sortComp
                    Column {
                        spacing: 4
                        Text {
                            text: App.uiText(App.language, "FilterSortField"); color: App.themeAccent
                            font.pixelSize: 13; font.bold: true; bottomPadding: 4
                        }
                        Repeater {
                            model: bar.sortFields
                            delegate: SortRow {
                                required property int index
                                required property var modelData
                                label: modelData
                                selected: galleryModel.sortRole === index
                                onPicked: galleryModel.sortRole = index
                            }
                        }
                        Text {
                            text: App.uiText(App.language, "FilterSortDirection"); color: App.themeAccent
                            font.pixelSize: 13; font.bold: true
                            topPadding: 8; bottomPadding: 4
                        }
                        Repeater {
                            model: [
                                { key: "FilterSortAsc",  desc: false },
                                { key: "FilterSortDesc", desc: true }
                            ]
                            delegate: SortRow {
                                required property var modelData
                                label: App.uiText(App.language, modelData.key)
                                selected: galleryModel.sortDescending === modelData.desc
                                onPicked: galleryModel.sortDescending = modelData.desc
                            }
                        }
                    }
                }

                Component {
                    id: medienComp
                    Column {
                        spacing: 4
                        Text {
                            text: App.uiText(App.language, "FilterShowMediaTypes"); color: App.themeAccent
                            font.pixelSize: 13; font.bold: true; bottomPadding: 4
                        }
                        Repeater {
                            model: bar.mediaTypes
                            delegate: Rectangle {
                                id: mediaRow
                                required property var modelData
                                readonly property bool on: bar.mediaShown(modelData.key)
                                width: 236; height: 34; radius: 6
                                color: on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                          : (mHover.hovered ? Qt.rgba(1,1,1,0.06) : "transparent")
                                border.width: 1
                                border.color: on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.55) : App.themeBorder
                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10; anchors.rightMargin: 10
                                    spacing: 8
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        text: mediaRow.on ? "\u2713" : "\u2715"
                                        color: mediaRow.on ? App.themeAccent : App.themeTextMuted
                                        font.pixelSize: 14; font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: mediaRow.modelData.label
                                        color: mediaRow.on ? App.themeTextPrimary : App.themeTextMuted
                                        font.pixelSize: 14
                                        font.weight: mediaRow.on ? Font.DemiBold : Font.Normal
                                    }
                                }
                                HoverHandler { id: mHover }
                                TapHandler { onTapped: bar.setMediaShown(mediaRow.modelData.key, !mediaRow.on) }
                            }
                        }
                    }
                }

                Component {
                    id: modeComp
                    Column {
                        spacing: 4
                        Text {
                            text: App.uiText(App.language, "FilterTagModeLabel"); color: App.themeAccent
                            font.pixelSize: 13; font.bold: true; bottomPadding: 4
                        }
                        Repeater {
                            model: bar.modeNames
                            delegate: Rectangle {
                                id: modeRow
                                required property int index
                                required property var modelData
                                readonly property bool selm: galleryModel.tagFilterMode === index
                                width: 236
                                // Höhe folgt dem (ggf. zweizeiligen) Beschreibungstext.
                                height: modeCol.implicitHeight + 16
                                radius: 6
                                color: selm ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.18)
                                            : (mdHover.hovered ? Qt.rgba(1,1,1,0.06) : "transparent")
                                border.width: 1
                                border.color: selm ? App.themeAccent : App.themeBorder
                                Text {
                                    id: modeDot
                                    anchors.left: parent.left; anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 14
                                    text: modeRow.selm ? "\u25CF" : "\u25CB"
                                    color: modeRow.selm ? App.themeAccent : App.themeTextMuted
                                    font.pixelSize: 15
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Column {
                                    id: modeCol
                                    anchors.left: modeDot.right; anchors.leftMargin: 9
                                    anchors.right: parent.right; anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1
                                    Text {
                                        text: modeRow.modelData
                                        color: modeRow.selm ? App.themeAccent : App.themeTextPrimary
                                        font.pixelSize: 14
                                        font.weight: modeRow.selm ? Font.Bold : Font.DemiBold
                                    }
                                    Text {
                                        width: parent.width
                                        text: bar.modeTips[modeRow.index]
                                        color: App.themeTextMuted; font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                HoverHandler { id: mdHover }
                                TapHandler { onTapped: galleryModel.tagFilterMode = modeRow.index }
                            }
                        }
                    }
                }

                Component {
                    id: tagsCatsComp
                    Column {
                        spacing: 4

                        // ── Panel-Steuerung: Tag- und Kategorie-Panel INDIVIDUELL ────
                        //  Toggle-Zeilen mit klar sichtbarem Aktiv-/Inaktiv-Zustand
                        //  (✓/✕ + Akzentfüllung, identische Optik wie die Medien-Zeilen).
                        Text {
                            text: App.uiText(App.language, "FilterPanelHeader"); color: App.themeAccent
                            font.pixelSize: 13; font.bold: true; bottomPadding: 4
                        }
                        Repeater {
                            model: [
                                { label: App.uiText(App.language, "FilterTagPanelRow"), tag: true },
                                { label: App.uiText(App.language, "FilterCatPanelRow"), tag: false }
                            ]
                            delegate: Rectangle {
                                id: panelRow
                                required property var modelData
                                readonly property bool on: modelData.tag ? bar.tagPanelVisible
                                                                         : bar.categoryPanelVisible
                                width: 236; height: 34; radius: 6
                                color: on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                          : (pHover.hovered ? Qt.rgba(1,1,1,0.06) : "transparent")
                                border.width: 1
                                border.color: on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.55) : App.themeBorder
                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10; anchors.rightMargin: 10
                                    spacing: 8
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        text: panelRow.on ? "\u2713" : "\u2715"
                                        color: panelRow.on ? App.themeAccent : App.themeTextMuted
                                        font.pixelSize: 14; font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: panelRow.modelData.label
                                        color: panelRow.on ? App.themeTextPrimary : App.themeTextMuted
                                        font.pixelSize: 14
                                        font.weight: panelRow.on ? Font.DemiBold : Font.Normal
                                    }
                                }
                                HoverHandler { id: pHover }
                                TapHandler {
                                    onTapped: panelRow.modelData.tag ? bar.tagPanelToggled()
                                                                     : bar.categoryPanelToggled()
                                }
                            }
                        }

                        Item { width: 1; height: 6 }   // Abstand Panel-Block ⇄ Tag-Schnellfilter

                        Item {
                            width: 236; height: tagsHdr.implicitHeight + 4
                            Text {
                                id: tagsHdr
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: App.uiText(App.language, "FilterTagsToFilter"); color: App.themeAccent
                                font.pixelSize: 13; font.bold: true
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                visible: bar.activeTags.length > 0
                                text: "Leeren"; color: App.themeAccent; font.pixelSize: 12
                                TapHandler { onTapped: bar.clearTags() }
                            }
                        }
                        // ── Tag manuell zur Filterliste hinzufügen ────────────
                        //  Eingabe + "Hinzufügen": fügt den eingegebenen Tag der
                        //  bestehenden Filterliste (bar.activeTags -> Proxy) hinzu.
                        Row {
                            spacing: 4
                            function addTypedTag() {
                                var t = addTagInput.text.trim()
                                if (t.length === 0) return
                                if (bar.activeTags.indexOf(t) < 0)
                                    bar.toggleTag(t)          // fügt hinzu (nicht vorhanden)
                                addTagInput.text = ""
                            }
                            TextField {
                                id: addTagInput
                                width: 236 - addTagBtn.width - 4
                                height: 30
                                font.pixelSize: 12
                                color: App.themeTextPrimary
                                placeholderText: App.uiText(App.language, "FilterAddTagPlaceholder")
                                background: Rectangle {
                                    color: App.themeCard; radius: 6
                                    border.color: addTagInput.activeFocus ? App.themeAccent : App.themeBorder
                                    border.width: 1
                                }
                                onAccepted: parent.addTypedTag()
                            }
                            Rectangle {
                                id: addTagBtn
                                width: addTagLbl.implicitWidth + 18; height: 30; radius: 6
                                color: addHover.hovered ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
                                                        : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.18)
                                border.color: App.themeAccent; border.width: 1
                                Text {
                                    id: addTagLbl
                                    anchors.centerIn: parent
                                    text: App.uiText(App.language, "FilterAddTagBtn")
                                    color: App.themeAccent; font.pixelSize: 12; font.bold: true
                                }
                                HoverHandler { id: addHover }
                                TapHandler { onTapped: addTagBtn.parent.addTypedTag() }
                            }
                        }
                        Text {
                            visible: filterPopup.popupTags.length === 0
                            text: App.uiText(App.language, "FilterNoTagsShort")
                            color: App.themeTextMuted; font.pixelSize: 13
                        }
                        Repeater {
                            model: filterPopup.popupTags
                            delegate: Rectangle {
                                id: tagRow
                                required property var modelData
                                readonly property bool on: bar.activeTags.indexOf(modelData) >= 0
                                readonly property color tagCol: bar.tagColorOf(modelData)
                                width: 236; height: 30; radius: 6
                                color: on ? Qt.rgba(tagCol.r, tagCol.g, tagCol.b, 0.22)
                                          : (tHover.hovered ? Qt.rgba(1,1,1,0.06) : "transparent")
                                border.width: 1
                                border.color: on ? tagCol : App.themeBorder
                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10; anchors.rightMargin: 10
                                    spacing: 8
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        text: tagRow.on ? "\u2713" : "\u2715"
                                        color: tagRow.on ? tagRow.tagCol : App.themeTextMuted
                                        font.pixelSize: 14; font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 9; height: 9; radius: 4.5
                                        color: tagRow.tagCol
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: tagRow.modelData
                                        color: tagRow.on ? App.themeTextPrimary : App.themeTextMuted
                                        font.pixelSize: 14
                                        font.weight: tagRow.on ? Font.DemiBold : Font.Normal
                                    }
                                }
                                HoverHandler { id: tHover }
                                TapHandler { onTapped: bar.toggleTag(tagRow.modelData) }
                            }
                        }
                    }
                }

            }
        }

        // ── Suchfeld (INLINE, direkt hinter dem Filter-Knopf) ────────────────
        //  Sucht LIVE bei jeder Änderung - kein Enter, kein Knopf. Gefiltert
        //  wird im Proxy (`galleryModel.searchText`), nicht in QML; gesucht wird
        //  in Anzeigename, Dateiname und Tags des OFFENEN Ordners, nicht im
        //  Dateiinhalt. Die Suche ist UND-verknüpft mit allen anderen Filtern.
        Rectangle {
            id: searchBox
            anchors.verticalCenter: parent.verticalCenter
            width: 220; height: 30; radius: 6
            color: App.themeCard
            border.color: searchInput.activeFocus ? App.themeAccent : App.themeBorder
            border.width: 1

            DrawnIcon {
                id: searchIcon
                anchors.left: parent.left; anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 14
                color: searchInput.activeFocus ? App.themeAccent : App.themeTextMuted
            }
            TextField {
                id: searchInput
                anchors.left: searchIcon.right; anchors.leftMargin: 6
                anchors.right: clearSearch.left; anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height - 2
                padding: 0
                font.pixelSize: 12
                color: App.themeTextPrimary
                placeholderText: App.uiText(App.language, "FilterSearchPlaceholder")
                //  Der Rahmen gehört dem umgebenden Rechteck - sonst säßen zwei
                //  Rahmen ineinander.
                background: null
                verticalAlignment: TextInput.AlignVCenter
                //  Live: JEDE Änderung filtert sofort. `onTextChanged` statt
                //  `onAccepted` ist hier der ganze Punkt der Funktion.
                onTextChanged: galleryModel.searchText = text
                //  Escape leert das Feld und gibt den Fokus an die Galerie
                //  zurück (dort scrollen dann wieder die Pfeiltasten).
                Keys.onEscapePressed: function(event) {
                    searchInput.text = ""
                    searchInput.focus = false
                    event.accepted = true
                }
            }
            //  Leeren-Knopf; erscheint erst, wenn etwas drinsteht.
            Item {
                id: clearSearch
                anchors.right: parent.right; anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: searchInput.text.length > 0 ? 14 : 0
                height: 14
                visible: searchInput.text.length > 0
                DrawnIcon {
                    anchors.centerIn: parent
                    name: "close"
                    size: 12
                    color: clearHover.hovered ? App.themeAccent : App.themeTextMuted
                }
                HoverHandler { id: clearHover }
                TapHandler { onTapped: searchInput.text = "" }
            }
        }

        //  Trefferzahl neben dem Feld - sonst rät man bei 0 Treffern, ob der
        //  Ordner leer ist oder die Suche zu eng.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: searchInput.text.trim().length > 0
            text: galleryModel.count + " " + App.uiText(App.language, "FilterSearchHits")
            color: galleryModel.count === 0 ? App.themeTextMuted : App.themeAccent
            font.pixelSize: 12
        }

        ToolSeparator { anchors.verticalCenter: parent.verticalCenter }

        // ── „+": Anlegen-Knopf (Ordner ODER Datei) ────────────────────────────
        //  Beide Aktionen legen etwas im GEÖFFNETEN Ordner an und standen als
        //  zwei Knöpfe („+ Ordner", „+ Erstellen") nebeneinander in der Leiste.
        //  Jetzt EIN Knopf mit kleinem Menü: gleiche Aktionen, weniger Breite in
        //  einer Leiste, die mit Tag-Chips ohnehin um Platz ringt. Ohne offenen
        //  Ordner deaktiviert - beide Aktionen brauchen ein Ziel.
        Button {
            id: plusBtn
            anchors.verticalCenter: parent.verticalCenter
            height: 30
            padding: 0
            enabled: App.currentFolder.length > 0
            //  Zweiter Druck schließt wieder - wie beim Filter-Knopf. Das gilt
            //  auch für das bereits geöffnete Erstellen-Popup, sonst stünde der
            //  Knopf ohne Wirkung da.
            onClicked: {
                if (plusMenu.opened || createPopup.opened) {
                    plusMenu.close()
                    createPopup.close()
                } else {
                    bar.actionFolder = ""      // dieser Knopf meint den offenen Ordner
                    plusMenu.open()
                }
            }

            background: Rectangle {
                implicitWidth: 34
                color: plusBtn.down ? App.themeCard
                     : (plusBtn.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                                  App.themeTextPrimary.b, 0.08)
                                        : App.themeMenuBarBg)
                border.color: (plusMenu.opened || createPopup.opened) ? App.themeAccent : App.themeBorder
                border.width: 1
                radius: 4
            }
            //  Gezeichnetes Symbol statt eines „+"-Zeichens (Regel 28) - es
            //  folgt damit Theme und Strichstärke der übrigen Bedienelemente.
            contentItem: Item {
                DrawnIcon {
                    anchors.centerIn: parent
                    name: "plus"
                    size: 14
                    color: plusBtn.enabled ? App.themeTextPrimary : App.themeTextMuted
                }
            }
            ToolTip.visible: plusBtn.hovered && !plusMenu.opened && !createPopup.opened
            ToolTip.delay: 500
            ToolTip.text: App.uiText(App.language, "FolderNewTitle") + " · "
                          + App.uiText(App.language, "CreateFileTitle")

            // ── Das Menü hinter dem „+" ───────────────────────────────────────
            //  Zwei Zeilen im Stil der übrigen Popup-Zeilen (Hover-Fläche,
            //  Symbol + Text). „Ordner" ruft denselben Dialog wie zuvor der
            //  eigene Knopf, „Erstellen" öffnet das Popup darunter.
            Popup {
                id: plusMenu
                y: plusBtn.height + 4
                width: 210
                padding: 6

                background: Rectangle {
                    color: App.themeMenuBarBg
                    border.color: App.themeBorder; border.width: 1
                    radius: 6
                }

                contentItem: Column {
                    spacing: 2
                    Repeater {
                        model: [ { act: "folder", icon: "folder", key: "FolderNewTitle" },
                                 { act: "file",   icon: "file",   key: "CreateFileBtn"  } ]
                        delegate: Rectangle {
                            id: actRow
                            required property var modelData
                            width: parent.width; height: 30; radius: 5
                            color: acHover.hovered
                                   ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                             App.themeTextPrimary.b, 0.10)
                                   : "transparent"
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 10; anchors.rightMargin: 10
                                spacing: 8
                                DrawnIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: actRow.modelData.icon
                                    size: 14
                                    color: App.themeTextMuted
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: App.uiText(App.language, actRow.modelData.key)
                                    color: App.themeTextPrimary; font.pixelSize: 12
                                }
                            }
                            HoverHandler { id: acHover }
                            TapHandler {
                                onTapped: {
                                    plusMenu.close()
                                    if (actRow.modelData.act === "folder")
                                        bar.newFolderRequested("")
                                    else
                                        bar.openCreateFor(bar.actionFolder)
                                }
                            }
                        }
                    }
                }
            }

            // ── „Erstellen": leere PDF/HTML/TXT/DOCX im aktuellen Ordner ───────
            //  Kompaktes Popup: Typ wählen (4 Zeilen, radio-artig) + Dateiname ->
            //  App.createEmptyFile schreibt atomar, aktualisiert die Galerie
            //  (folderContentsChanged) und meldet den Erfolg über die Statuszeile.
            Popup {
                id: createPopup
                y: plusBtn.height + 4
                width: 260
                padding: 12
                property string kind: "pdf"    // "pdf" | "html" | "txt" | "docx"

                function doCreate() {
                    if (createNameField.text.trim().length === 0)
                        return
                    App.createEmptyFile(createPopup.kind, createNameField.text,
                                        bar.actionFolder)
                    createPopup.close()
                }

                background: Rectangle {
                    color: App.themeMenuBarBg
                    border.color: App.themeBorder; border.width: 1
                    radius: 6
                }

                contentItem: Column {
                    spacing: 8

                    Text {
                        text: App.uiText(App.language, "CreateFileTitle")
                        color: App.themeTextPrimary; font.pixelSize: 13; font.bold: true
                    }

                    // Typ-Zeilen (radio-artig, Muster wie die Filter-Popup-Zeilen).
                    Repeater {
                        model: [ { kind: "pdf",  key: "CreateFileTypePdf"  },
                                 { kind: "html", key: "CreateFileTypeHtml" },
                                 { kind: "txt",  key: "CreateFileTypeTxt"  },
                                 { kind: "docx", key: "CreateFileTypeDocx" } ]
                        delegate: Rectangle {
                            id: typeRow
                            required property var modelData
                            readonly property bool on: createPopup.kind === modelData.kind
                            //  DOCX braucht ZLIB (s. src/core/ZCodec.h). Fehlt sie,
                            //  bleibt die Zeile stehen, ist aber ausgegraut - der
                            //  Hover-Text nennt die fehlende Bibliothek.
                            readonly property bool avail: modelData.kind !== "docx"
                                                          || App.docxAvailable
                            opacity: avail ? 1.0 : 0.45
                            width: parent.width; height: 30; radius: 5
                            color: on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                 : (tyHover.hovered
                                    ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.10)
                                    : "transparent")
                            border.color: on ? App.themeAccent : "transparent"
                            border.width: 1
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 10; anchors.rightMargin: 10
                                spacing: 8
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 10; height: 10; radius: 5
                                    color: typeRow.on ? App.themeAccent : "transparent"
                                    border.color: typeRow.on ? App.themeAccent : App.themeTextMuted
                                    border.width: 1
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: App.uiText(App.language, typeRow.modelData.key)
                                    color: App.themeTextPrimary; font.pixelSize: 12
                                }
                            }
                            HoverHandler { id: tyHover }
                            TapHandler {
                                enabled: typeRow.avail
                                onTapped: createPopup.kind = typeRow.modelData.kind
                            }
                            ToolTip.visible: tyHover.hovered && !typeRow.avail
                            ToolTip.delay: 500
                            ToolTip.text: App.uiText(App.language, "LibMissingZlib")
                        }
                    }

                    Text {
                        text: App.uiText(App.language, "CreateFileNameLabel")
                        color: App.themeTextMuted; font.pixelSize: 11
                    }
                    TextField {
                        id: createNameField
                        width: parent.width
                        font.pixelSize: 12
                        onAccepted: createPopup.doCreate()
                    }

                    Row {
                        anchors.right: parent.right
                        spacing: 8
                        Button {
                            height: 28; font.pixelSize: 12
                            text: App.uiText(App.language, "SettingsCancel")
                            onClicked: createPopup.close()
                        }
                        Button {
                            height: 28; font.pixelSize: 12
                            enabled: createNameField.text.trim().length > 0
                            text: App.uiText(App.language, "CreateFileBtn")
                            palette.buttonText: enabled ? App.themeAccent : App.themeTextMuted
                            onClicked: createPopup.doCreate()
                        }
                    }
                }
            }
        }

        // ── „Extrahieren": globale Seiten-Extraktion über alle PDFs des ───────
        //    aktuellen Ordners. Der Button meldet nur die Absicht - Scan und
        //    Auswahldialog hostet die ApplicationShell (sie kennt Overlay und
        //    Statuszeile). Ohne offenen Ordner deaktiviert (wie „Erstellen").
        Button {
            id: extractBtn
            anchors.verticalCenter: parent.verticalCenter
            height: 30
            font.pixelSize: 13
            enabled: App.currentFolder.length > 0
            text: App.uiText(App.language, "FilterExtractBtn")
            onClicked: bar.extractPagesRequested("")   // der offene Ordner
        }

        ToolSeparator { anchors.verticalCenter: parent.verticalCenter }

        // ── Aktive Tag-Chips (INLINE) ─────────────────────────────────────────
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Repeater {
                model: bar.activeTags
                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    height: 24; radius: 12
                    width: chipRow.implicitWidth + 16
                    readonly property color chipCol: bar.tagColorOf(chip.modelData)
                    color: Qt.rgba(chipCol.r, chipCol.g, chipCol.b, 0.25)
                    border.color: chipCol; border.width: 1
                    Row {
                        id: chipRow
                        anchors.centerIn: parent; spacing: 5
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: chip.modelData; color: App.themeTextPrimary; font.pixelSize: 11
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "\u2715"; color: App.themeTextMuted; font.pixelSize: 10
                            TapHandler { onTapped: bar.toggleTag(chip.modelData) }
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: bar.enterAddToTagMode(chip.modelData)
                    }
                }
            }
            Text {
                visible: bar.activeTags.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: "Leeren"; color: App.themeAccent; font.pixelSize: 11
                TapHandler { onTapped: bar.clearTags() }
            }
        }
    }
}
