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
    signal categoryPanelToggled()

    property var activeTags: []

    readonly property var modeNames:  [App.uiText(App.language, "FilterTagModeOr"), App.uiText(App.language, "FilterTagModeAnd"), App.uiText(App.language, "FilterTagModeNur"), App.uiText(App.language, "FilterTagModeInklusiv")]
    readonly property var modeColors: ["#6ab0ff", "#00c8b4", "#ff9060", "#c090ff"]
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
            height: 28
            font.pixelSize: 11
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

                readonly property var cats: [
                    { label: App.uiText(App.language, "FilterMedia"),          hint: bar.mediaActiveCount + "/" + bar.mediaTypes.length },
                    { label: App.uiText(App.language, "FilterTagModeLabel"), hint: bar.modeNames[galleryModel.tagFilterMode] },
                    { label: "Tags",            hint: bar.activeTags.length > 0 ? bar.activeTags.length + App.uiText(App.language, "FilterActiveSuffix") : "—" },
                    { label: App.uiText(App.language, "SettingsTabCategories"),      hint: App.uiText(App.language, "FilterCatPanelHint") }
                ]

                background: Rectangle {
                    color: App.themeCard
                    border.color: App.themeBorder; border.width: 1
                    radius: 8
                }

                contentItem: Row {
                    spacing: 0

                    // ── Master: Kategorie-Buttons ─────────────────────────────
                    Item {
                        width: 156
                        height: 248
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
                                    height: 40; radius: 6
                                    color: sel ? Qt.rgba(0.16, 0.71, 0.31, 0.16)
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
                                                font.pixelSize: 12
                                                font.weight: catBtn.sel ? Font.DemiBold : Font.Normal
                                            }
                                            Text {
                                                text: catBtn.modelData.hint
                                                color: App.themeTextMuted; font.pixelSize: 10
                                                width: masterCol.width - 44; elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    Text {
                                        anchors.right: parent.right; anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "\u25B8"
                                        color: catBtn.sel ? App.themeAccent : App.themeTextMuted
                                        font.pixelSize: 11
                                    }
                                    HoverHandler { id: catHover }
                                    TapHandler { onTapped: filterPopup.selectedCat = catBtn.index }
                                }
                            }
                        }
                    }

                    Rectangle { width: 1; height: 248; color: App.themeBorder }

                    // ── Detail: Optionen der gewaehlten Kategorie ─────────────
                    Item {
                        width: 256
                        height: 248
                        Loader {
                            anchors.fill: parent
                            anchors.margins: 10
                            sourceComponent: filterPopup.selectedCat === 0 ? medienComp
                                           : filterPopup.selectedCat === 1 ? modeComp
                                           : filterPopup.selectedCat === 2 ? tagsComp
                                           : katComp
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
                            font.pixelSize: 11; font.bold: true; bottomPadding: 4
                        }
                        Repeater {
                            model: bar.mediaTypes
                            delegate: Rectangle {
                                id: mediaRow
                                required property var modelData
                                readonly property bool on: bar.mediaShown(modelData.key)
                                width: 236; height: 32; radius: 6
                                color: on ? Qt.rgba(0.16, 0.71, 0.31, 0.22)
                                          : (mHover.hovered ? Qt.rgba(1,1,1,0.06) : "transparent")
                                border.width: 1
                                border.color: on ? Qt.rgba(0.16, 0.71, 0.31, 0.55) : App.themeBorder
                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10; anchors.rightMargin: 10
                                    spacing: 8
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        text: mediaRow.on ? "\u2713" : "\u2715"
                                        color: mediaRow.on ? "#50e080" : App.themeTextMuted
                                        font.pixelSize: 12; font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: mediaRow.modelData.label
                                        color: mediaRow.on ? App.themeTextPrimary : App.themeTextMuted
                                        font.pixelSize: 12
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
                            font.pixelSize: 11; font.bold: true; bottomPadding: 4
                        }
                        Repeater {
                            model: bar.modeNames
                            delegate: Rectangle {
                                id: modeRow
                                required property int index
                                required property var modelData
                                readonly property bool selm: galleryModel.tagFilterMode === index
                                readonly property color accent: bar.modeColors[index]
                                width: 236; height: 44; radius: 6
                                color: selm ? Qt.rgba(accent.r, accent.g, accent.b, 0.18)
                                            : (mdHover.hovered ? Qt.rgba(1,1,1,0.06) : "transparent")
                                border.width: 1
                                border.color: selm ? accent : App.themeBorder
                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10; anchors.rightMargin: 10
                                    spacing: 9
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        text: modeRow.selm ? "\u25CF" : "\u25CB"
                                        color: modeRow.accent; font.pixelSize: 13
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 1
                                        Text {
                                            text: modeRow.modelData
                                            color: modeRow.accent; font.pixelSize: 12
                                            font.weight: modeRow.selm ? Font.Bold : Font.DemiBold
                                        }
                                        Text {
                                            text: bar.modeTips[modeRow.index]
                                            color: App.themeTextMuted; font.pixelSize: 10
                                            width: 188; wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                                HoverHandler { id: mdHover }
                                TapHandler { onTapped: galleryModel.tagFilterMode = modeRow.index }
                            }
                        }
                    }
                }

                Component {
                    id: tagsComp
                    Column {
                        spacing: 4
                        Item {
                            width: 236; height: tagsHdr.implicitHeight + 4
                            Text {
                                id: tagsHdr
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: App.uiText(App.language, "FilterTagsToFilter"); color: App.themeAccent
                                font.pixelSize: 11; font.bold: true
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                visible: bar.activeTags.length > 0
                                text: "Leeren"; color: App.themeAccent; font.pixelSize: 10
                                TapHandler { onTapped: bar.clearTags() }
                            }
                        }
                        Text {
                            visible: filterPopup.popupTags.length === 0
                            text: App.uiText(App.language, "FilterNoTagsShort")
                            color: App.themeTextMuted; font.pixelSize: 11
                        }
                        ScrollView {
                            width: 236
                            visible: filterPopup.popupTags.length > 0
                            height: 196
                            clip: true
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            Column {
                                width: 236
                                spacing: 4
                                Repeater {
                                    model: filterPopup.popupTags
                                    delegate: Rectangle {
                                        id: tagRow
                                        required property var modelData
                                        readonly property bool on: bar.activeTags.indexOf(modelData) >= 0
                                        readonly property color tagCol: App.tagColor(modelData)
                                        width: 220; height: 28; radius: 6
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
                                                font.pixelSize: 12; font.bold: true
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
                                                font.pixelSize: 12
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

                Component {
                    id: katComp
                    Column {
                        spacing: 8
                        Text {
                            text: App.uiText(App.language, "SettingsTabCategories"); color: App.themeAccent
                            font.pixelSize: 11; font.bold: true
                        }
                        Text {
                            width: 236
                            text: App.uiText(App.language, "FilterCatPanelTooltip")
                            color: App.themeTextMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
                        }
                        Rectangle {
                            width: 236; height: 34; radius: 6
                            color: katHover.hovered ? Qt.rgba(1,1,1,0.08) : Qt.rgba(1,1,1,0.04)
                            border.color: App.themeBorder; border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: App.uiText(App.language, "FilterCatPanelToggle")
                                color: App.themeTextPrimary; font.pixelSize: 12
                            }
                            HoverHandler { id: katHover }
                            TapHandler {
                                onTapped: { bar.categoryPanelToggled(); filterPopup.close() }
                            }
                        }
                    }
                }
            }
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
