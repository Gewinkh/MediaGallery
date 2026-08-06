pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  FilterBar.qml — Filter-/Sortierleiste (ersetzt FilterBar(QWidget)).
//
//  Bindet ausschließlich serverseitig an den Proxy (galleryModel). Keine
//  Filterlogik in QML.
//
//  Layout:
//    • Sortierung (Feld + Richtung) bleibt INLINE links.
//    • Ein "Filter"-Button oeffnet ein MASTER-DETAIL-Popup: links eine Spalte mit
//      Kategorie-Buttons (Medien, Tag-Filtermodus, Tags, Kategorien), rechts
//      daneben die Optionen der jeweils gewaehlten Kategorie. Das haelt jede
//      Kategorie uebersichtlich getrennt.
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
    // gespiegelt → die Toggle-Zeilen im Filter-Popup zeigen den Aktiv-Zustand.
    signal tagPanelToggled()
    signal categoryPanelToggled()
    // „Extrahieren": globale Seiten-Extraktion anstoßen (Dialog hostet die Shell).
    signal extractPagesRequested()
    property bool tagPanelVisible: false
    property bool categoryPanelVisible: false

    property var activeTags: []

    // ── Referenzbasierte Konsistenz ──────────────────────────────────────────
    //  Referenzquelle des Tag-Filters ist der Proxy (galleryModel.tagFilter).
    //  Externe Änderungen — Tag-Toggle im Kategorie-Panel, Kaskaden-
    //  Deaktivierung beim Abwählen von Kategorien — spiegeln sich hier, damit
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

    readonly property var mediaTypes: [
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
    readonly property int mediaActiveCount:
        (galleryModel.showImages ? 1 : 0) + (galleryModel.showVideos ? 1 : 0)
        + (galleryModel.showAudio ? 1 : 0) + (galleryModel.showPdfs ? 1 : 0)
        + (galleryModel.showTexts ? 1 : 0)

    readonly property bool anyFilterActive:
        mediaActiveCount < mediaTypes.length || activeTags.length > 0
    readonly property int filterBadge:
        (mediaActiveCount < mediaTypes.length ? 1 : 0) + activeTags.length

    function toggleTag(tag) {
        var a = activeTags.slice()
        var i = a.indexOf(tag)
        if (i >= 0) a.splice(i, 1); else a.push(tag)
        activeTags = a
        galleryModel.tagFilter = a
    }
    function clearTags() { activeTags = []; galleryModel.tagFilter = [] }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: 1
        color: App.themeBorder
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 8

        // ── Sortierfeld (INLINE) ──────────────────────────────────────────────
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: App.uiText(App.language, "FilterSortLabel"); color: App.themeTextMuted; font.pixelSize: 12
        }
        ComboBox {
            id: sortField
            anchors.verticalCenter: parent.verticalCenter
            width: 130
            model: [App.uiText(App.language, "FilterDate"), App.uiText(App.language, "FilterName"), App.uiText(App.language, "FilterTags"), App.uiText(App.language, "FilterFileSize")]
            currentIndex: galleryModel.sortRole
            onActivated: galleryModel.sortRole = currentIndex

            // Dropdown folgt der Menüleisten-Farbe (App.themeMenuBarBg) statt der
            // ungefärbten Fusion-Standardvorgabe — Struktur analog Qt-Doku
            // "Customizing ComboBox": contentItem/ListView bleibt Standardverhalten
            // (Delegates/Highlight unverändert), nur der Popup-Hintergrund wird ersetzt.
            popup: Popup {
                y: sortField.height
                width: sortField.width
                implicitHeight: contentItem.implicitHeight
                padding: 1

                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: sortField.popup.visible ? sortField.delegateModel : null
                    currentIndex: sortField.highlightedIndex
                    ScrollIndicator.vertical: ScrollIndicator {}
                }

                background: Rectangle {
                    color: App.themeMenuBarBg
                    border.color: App.themeBorder; border.width: 1
                    radius: 4
                }
            }
        }
        ToolButton {
            anchors.verticalCenter: parent.verticalCenter
            text: galleryModel.sortDescending ? "\u2193" : "\u2191"
            ToolTip.text: galleryModel.sortDescending ? App.uiText(App.language, "FilterSortDesc") : App.uiText(App.language, "FilterSortAsc")
            ToolTip.visible: hovered
            onClicked: galleryModel.sortDescending = !galleryModel.sortDescending
        }

        ToolSeparator { anchors.verticalCenter: parent.verticalCenter }

        // ── Sammel-Button "Filter" (Master-Detail-Popup) ─────────────────────
        Button {
            id: filterBtn
            anchors.verticalCenter: parent.verticalCenter
            height: 30
            font.pixelSize: 13
            text: bar.filterBadge > 0 ? "Filter (" + bar.filterBadge + ") \u25BE"
                                      : "Filter \u25BE"
            palette.buttonText: bar.anyFilterActive ? App.themeAccent : App.themeTextPrimary
            onClicked: filterPopup.opened ? filterPopup.close() : filterPopup.open()

            Popup {
                id: filterPopup
                y: filterBtn.height + 4
                padding: 0
                modal: false
                focus: true

                // Aktuell gewaehlte Kategorie (Master-Spalte): 0..3
                property int selectedCat: 0
                // Tag-Liste beim Oeffnen auffrischen (App.allTags() ist eine Funktion).
                property var popupTags: []
                onAboutToShow: popupTags = App.allTags()

                // "Tags" und "Kategorien" sind zu EINEM Eintrag zusammengelegt:
                // Panel-Steuerung (Tag-/Kategorie-Panel individuell) + Tag-Schnellfilter.
                readonly property var cats: [
                    { label: App.uiText(App.language, "FilterMedia"), hint: bar.mediaActiveCount + "/" + bar.mediaTypes.length },
                    { label: App.uiText(App.language, "FilterTagModeLabel"), hint: bar.modeNames[galleryModel.tagFilterMode] },
                    { label: App.uiText(App.language, "FilterTagsCatsLabel"),
                      hint: bar.activeTags.length > 0 ? bar.activeTags.length + App.uiText(App.language, "FilterActiveSuffix") : "—" }
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
                    // Höhe = größerer der beiden Inhalte, gedeckelt → kein Leerraum,
                    // aber scrollbar sobald es zu groß wird. (Als Property statt
                    // height der Row, damit das Popup-Sizing nicht kollidiert.)
                    readonly property real bodyH: Math.min(maxBodyH, Math.max(navH, detailH))

                    // ── Master: Kategorie-Buttons ─────────────────────────────
                    Item {
                        width: 184
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
                                                width: masterCol.width - 44; elide: Text.ElideRight
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
                                sourceComponent: filterPopup.selectedCat === 0 ? medienComp
                                               : filterPopup.selectedCat === 1 ? modeComp
                                               : tagsCatsComp
                            }
                        }
                    }
                }

                // ── Detail-Komponenten ────────────────────────────────────────

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
                        //  bestehenden Filterliste (bar.activeTags → Proxy) hinzu.
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
                                readonly property color tagCol: App.tagColor(modelData)
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

        ToolSeparator { anchors.verticalCenter: parent.verticalCenter }

        // ── „Erstellen": leere PDF/HTML/TXT im aktuellen Ordner ───────────────
        //  Öffnet ein kompaktes Popup: Typ wählen (3 Zeilen, radio-artig) +
        //  Dateiname → App.createEmptyFile schreibt atomar, aktualisiert die
        //  Galerie (folderContentsChanged) und meldet den Erfolg über die
        //  Statuszeile. Ohne offenen Ordner deaktiviert.
        Button {
            id: createBtn
            anchors.verticalCenter: parent.verticalCenter
            height: 30
            font.pixelSize: 13
            enabled: App.currentFolder.length > 0
            text: "+ " + App.uiText(App.language, "CreateFileBtn")
            onClicked: {
                if (createPopup.opened) {
                    createPopup.close()
                } else {
                    createNameField.text = ""
                    createPopup.kind = "pdf"
                    createPopup.open()
                    createNameField.forceActiveFocus()
                }
            }

            Popup {
                id: createPopup
                y: createBtn.height + 4
                width: 260
                padding: 12
                property string kind: "pdf"    // "pdf" | "html" | "txt" | "docx"

                function doCreate() {
                    if (createNameField.text.trim().length === 0)
                        return
                    App.createEmptyFile(createPopup.kind, createNameField.text)
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
                            //  bleibt die Zeile stehen, ist aber ausgegraut — der
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
        //    aktuellen Ordners. Der Button meldet nur die Absicht — Scan und
        //    Auswahldialog hostet die ApplicationShell (sie kennt Overlay und
        //    Statuszeile). Ohne offenen Ordner deaktiviert (wie „Erstellen").
        Button {
            id: extractBtn
            anchors.verticalCenter: parent.verticalCenter
            height: 30
            font.pixelSize: 13
            enabled: App.currentFolder.length > 0
            text: App.uiText(App.language, "FilterExtractBtn")
            onClicked: bar.extractPagesRequested()
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
                    color: Qt.rgba(App.tagColor(chip.modelData).r, App.tagColor(chip.modelData).g,
                                   App.tagColor(chip.modelData).b, 0.25)
                    border.color: App.tagColor(chip.modelData); border.width: 1
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
