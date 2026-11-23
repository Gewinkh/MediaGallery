pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MediaGallery 1.0
import "../common"

// Filter-/Sortierleiste; bindet ausschliesslich serverseitig an den Proxy - keine Filterlogik in QML.
// Links nur der Filter-Knopf, der ein Master-Detail-Popup öffnet (Rubriken links, Optionen rechts);
// die Sortierung ist eine Rubrik darin, aktive Tag-Chips bleiben inline rechts.
Rectangle {
    id: bar
    implicitHeight: 46
    color: App.themeFilterBarBg

    signal enterAddToTagMode(string tag)
    signal tagPanelToggled()
    signal categoryPanelToggled()
    property var tagsCtl: Tags
    // Zähler, der bei jeder Tag-Änderung hochgeht - die Abhängigkeit, die den Bindungen fehlte: `tagColorOf` ruft
    // nur Funktionen, und darauf erzeugt QML keine Bindung. Innerhalb der Funktion gelesen, hängt jede daran.
    property int tagRev: 0
    Connections {
        target: bar.tagsCtl
        function onTagsChanged()       { bar.tagRev++ }
        function onCategoriesChanged() { bar.tagRev++ }
    }

    function tagColorOf(tag) {
        void bar.tagRev                 // s. oben - die Bindung braucht sie
        var c = mediaModel.visibleTagColor(tag)
        return (c && c.a > 0) ? c : bar.tagsCtl.tagColor(tag)
    }

    property string actionFolder: ""
    signal extractPagesRequested(string folder)
    signal newFolderRequested(string folder)

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

    // Referenzquelle des Tag-Filters ist der Proxy (`galleryModel.tagFilter`); externe Änderungen spiegeln sich
    // hier, damit Inline-Chips und Filter-Badge nie veralten.
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

    //  Zeile der Filteroptionen wie im Entwurf: Kreis (eine Wahl) oder Kaestchen (an/aus) statt umrahmter Kachel;
    //  `tint` faerbt Kreis und Kaestchen, `dot` setzt den Farbpunkt eines Tags davor.
    component OptionRow: Item {
        id: opt
        property string label: ""
        property string sub: ""
        property bool on: false
        property bool radio: true
        property color tint: App.themeAccent
        property color dot: "transparent"
        signal picked()

        implicitHeight: Math.max(30, optText.implicitHeight + 12)
        Rectangle {
            anchors.fill: parent
            radius: 5
            color: optHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.05)
                                    : "transparent"
        }
        Rectangle {
            id: optMark
            x: 6
            y: opt.sub.length > 0 ? 8 : (opt.height - height) / 2
            width: 14; height: 14
            radius: opt.radio ? 7 : 3.5
            color: !opt.radio && opt.on ? opt.tint : "transparent"
            border.width: opt.radio || !opt.on ? 1.5 : 0
            border.color: opt.on ? opt.tint
                                 : Qt.rgba(App.themeTextMuted.r, App.themeTextMuted.g, App.themeTextMuted.b, 0.8)
            Rectangle {
                visible: opt.radio && opt.on
                anchors.centerIn: parent
                width: 6; height: 6; radius: 3
                color: opt.tint
            }
            DrawnIcon {
                visible: !opt.radio && opt.on
                anchors.centerIn: parent
                name: "check"; size: 10
                color: App.themeBackground
            }
        }
        Rectangle {
            id: optDot
            visible: opt.dot.a > 0
            x: optMark.x + optMark.width + 9
            anchors.verticalCenter: optMark.verticalCenter
            width: visible ? 8 : 0; height: 8; radius: 4
            color: opt.dot
        }
        Column {
            id: optText
            x: optMark.x + optMark.width + 9 + (optDot.visible ? optDot.width + 7 : 0)
            width: opt.width - x - 6
            y: opt.sub.length > 0 ? 6 : (opt.height - height) / 2
            spacing: 1
            Text {
                width: parent.width
                text: opt.label
                color: App.themeTextPrimary
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Text {
                visible: opt.sub.length > 0
                width: parent.width
                text: opt.sub
                color: App.themeTextMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
        HoverHandler { id: optHover }
        TapHandler { onTapped: opt.picked() }
    }

    component Heading: Text {
        color: App.themeTextMuted
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
        font.capitalization: Font.AllUppercase
        leftPadding: 6
        topPadding: 10
        bottomPadding: 4
    }

    function toggleTag(tag) {
        var a = activeTags.slice()
        var i = a.indexOf(tag)
        if (i >= 0) a.splice(i, 1); else a.push(tag)
        activeTags = a
        galleryModel.tagFilter = a
    }
    function clearTags() { activeTags = []; galleryModel.tagFilter = [] }
    function focusSearch() { searchInput.forceActiveFocus(); searchInput.selectAll() }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: 1
        color: App.themeBorder
    }

    ScrollableBar {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 8

        // EINZIGER Knopf links: Sortierung UND Filter sitzen darin - vorher standen Sortierfeld und Richtungspfeil
        // daneben in der Leiste.
        Button {
            id: filterBtn
            objectName: "filterBtn"
            anchors.verticalCenter: parent.verticalCenter
            height: 30
            padding: 0
            onClicked: filterPopup.opened ? filterPopup.close() : filterPopup.open()

            //  Akzentrahmen, solange der Aufklapper offen oder ein Filter aktiv ist; offen zusaetzlich leicht getoent.
            background: Rectangle {
                implicitWidth: filterRow.implicitWidth + 20
                color: filterPopup.opened ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.10)
                     : (filterBtn.hovered || filterBtn.down
                        ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.08)
                        : App.themeCard)
                border.color: bar.anyFilterActive || filterPopup.opened ? App.themeAccent : App.themeBorder
                border.width: 1
                radius: 5
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
                    color: bar.anyFilterActive || filterPopup.opened ? App.themeAccent : App.themeTextMuted
                }
            }

            Popup {
                id: filterPopup
                objectName: "filterPopup"
                y: filterBtn.height + 6
                padding: 0
                modal: false
                focus: true

                property int selectedCat: 0
                readonly property real detailW: 210
                property var popupTags: []
                onAboutToShow: popupTags = mediaModel.visibleTags()

                readonly property var cats: [
                    { label: App.uiText(App.language, "FilterSortLabel"),
                      hint: bar.sortFields[galleryModel.sortRole] + " · "
                            + App.uiText(App.language, galleryModel.sortDescending ? "FilterSortDesc" : "FilterSortAsc") },
                    { label: App.uiText(App.language, "FilterMedia"), hint: bar.mediaActiveCount + "/" + bar.mediaTypes.length },
                    { label: App.uiText(App.language, "FilterTagModeLabel"), hint: bar.modeNames[galleryModel.tagFilterMode] },
                    { label: App.uiText(App.language, "FilterTagsCatsLabel"),
                      hint: bar.activeTags.length > 0 ? bar.activeTags.length + App.uiText(App.language, "FilterActiveSuffix") : "-" }
                ]

                background: PopupPanel { }

                contentItem: Row {
                    id: popupRow
                    spacing: 0

                    readonly property int maxBodyH: 400
                    readonly property real navH:
                        filterPopup.cats.length * 44 + (filterPopup.cats.length - 1) * 4 + 12
                    readonly property real detailH:
                        detailLoader.item ? detailLoader.item.implicitHeight + 20 : 0
                    readonly property real bodyH: Math.min(maxBodyH, Math.max(navH, detailH))

                    // 184 statt 180 des Entwurfs: der Hinweis der Sortier-Rubrik trägt Feld UND Richtung
                    // ("Dateigröße · Absteigend") - schmaler schnitt er ihn ab.
                    Item {
                        width: 184
                        height: popupRow.bodyH
                        Column {
                            id: masterCol
                            anchors.fill: parent
                            anchors.margins: 6
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
                                    color: Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b,
                                                   sel ? 0.07 : (catHover.hovered ? 0.04 : 0))
                                    Column {
                                        anchors.left: parent.left; anchors.leftMargin: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 1
                                        Text {
                                            text: catBtn.modelData.label
                                            color: App.themeTextPrimary
                                            font.pixelSize: 13
                                        }
                                        Text {
                                            text: catBtn.modelData.hint
                                            color: App.themeTextMuted; font.pixelSize: 11
                                            width: masterCol.width - 40; elide: Text.ElideRight
                                        }
                                    }
                                    DrawnIcon {
                                        anchors.right: parent.right; anchors.rightMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "chevron-right"
                                        size: 12
                                        color: App.themeTextMuted
                                    }
                                    HoverHandler { id: catHover }
                                    TapHandler { onTapped: filterPopup.selectedCat = catBtn.index }
                                }
                            }
                        }
                    }

                    Rectangle { width: 1; height: popupRow.bodyH; color: App.themeBorder }

                    Item {
                        width: filterPopup.detailW + 20
                        height: popupRow.bodyH
                        ScrollView {
                            anchors.fill: parent
                            clip: true
                            leftPadding: 10; rightPadding: 10
                            topPadding: 4; bottomPadding: 10
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            Loader {
                                id: detailLoader
                                width: filterPopup.detailW
                                sourceComponent: filterPopup.selectedCat === 0 ? sortComp
                                               : filterPopup.selectedCat === 1 ? medienComp
                                               : filterPopup.selectedCat === 2 ? modeComp
                                               : tagsCatsComp
                            }
                        }
                    }
                }


                Component {
                    id: sortComp
                    Column {
                        Heading { text: App.uiText(App.language, "FilterSortField") }
                        Repeater {
                            model: bar.sortFields
                            delegate: OptionRow {
                                required property int index
                                required property var modelData
                                width: filterPopup.detailW
                                label: modelData
                                on: galleryModel.sortRole === index
                                onPicked: galleryModel.sortRole = index
                            }
                        }
                        Heading { text: App.uiText(App.language, "FilterSortDirection") }
                        Repeater {
                            model: [
                                { key: "FilterSortAsc",  desc: false },
                                { key: "FilterSortDesc", desc: true }
                            ]
                            delegate: OptionRow {
                                required property var modelData
                                width: filterPopup.detailW
                                label: App.uiText(App.language, modelData.key)
                                on: galleryModel.sortDescending === modelData.desc
                                onPicked: galleryModel.sortDescending = modelData.desc
                            }
                        }
                    }
                }

                Component {
                    id: medienComp
                    Column {
                        Heading { text: App.uiText(App.language, "FilterShowMediaTypes") }
                        Repeater {
                            model: bar.mediaTypes
                            delegate: OptionRow {
                                required property var modelData
                                width: filterPopup.detailW
                                radio: false
                                label: modelData.label
                                on: bar.mediaShown(modelData.key)
                                onPicked: bar.setMediaShown(modelData.key, !on)
                            }
                        }
                    }
                }

                Component {
                    id: modeComp
                    Column {
                        Heading { text: App.uiText(App.language, "FilterTagModeLabel") }
                        Repeater {
                            model: bar.modeNames
                            delegate: OptionRow {
                                required property int index
                                required property var modelData
                                width: filterPopup.detailW
                                label: modelData
                                sub: bar.modeTips[index]
                                on: galleryModel.tagFilterMode === index
                                onPicked: galleryModel.tagFilterMode = index
                            }
                        }
                    }
                }

                Component {
                    id: tagsCatsComp
                    Column {
                        spacing: 0

                        Heading { text: App.uiText(App.language, "FilterPanelHeader") }
                        Repeater {
                            model: [
                                { label: App.uiText(App.language, "FilterTagPanelRow"), tag: true },
                                { label: App.uiText(App.language, "FilterCatPanelRow"), tag: false }
                            ]
                            delegate: OptionRow {
                                required property var modelData
                                width: filterPopup.detailW
                                radio: false
                                label: modelData.label
                                on: modelData.tag ? bar.tagPanelVisible : bar.categoryPanelVisible
                                onPicked: modelData.tag ? bar.tagPanelToggled() : bar.categoryPanelToggled()
                            }
                        }

                        Item {
                            width: filterPopup.detailW; height: tagsHdr.implicitHeight
                            Heading {
                                id: tagsHdr
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                text: App.uiText(App.language, "FilterTagsToFilter")
                            }
                            Text {
                                anchors.right: parent.right; anchors.rightMargin: 6
                                anchors.baseline: tagsHdr.baseline
                                visible: bar.activeTags.length > 0
                                text: App.uiText(App.language, "TableFilterClear")
                                color: App.themeAccent; font.pixelSize: 11
                                TapHandler { onTapped: bar.clearTags() }
                            }
                        }
                        Row {
                            spacing: 6
                            bottomPadding: 4
                            function addTypedTag() {
                                var t = addTagInput.text.trim()
                                if (t.length === 0) return
                                if (bar.activeTags.indexOf(t) < 0)
                                    bar.toggleTag(t)          // fügt hinzu (nicht vorhanden)
                                addTagInput.text = ""
                            }
                            TextField {
                                id: addTagInput
                                width: filterPopup.detailW - addTagBtn.width - 6
                                height: 28
                                font.pixelSize: 12
                                color: App.themeTextPrimary
                                placeholderText: App.uiText(App.language, "FilterAddTagPlaceholder")
                                background: Rectangle {
                                    color: App.themeBackground; radius: 5
                                    border.color: addTagInput.activeFocus ? App.themeAccent : App.themeBorder
                                    border.width: 1
                                }
                                onAccepted: parent.addTypedTag()
                            }
                            Rectangle {
                                id: addTagBtn
                                // Ganze Pixel: der Zeichner rastet die Lage ein, ein Bruchteil schob den rechten
                                // Rand hinter die Kante des Bildlaufbereichs.
                                width: Math.ceil(addTagLbl.implicitWidth) + 18; height: 28; radius: 5
                                color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b,
                                               addHover.hovered ? 0.30 : 0.16)
                                border.color: App.themeAccent; border.width: 1
                                Text {
                                    id: addTagLbl
                                    anchors.centerIn: parent
                                    text: App.uiText(App.language, "FilterAddTagBtn")
                                    color: App.themeAccent; font.pixelSize: 12
                                }
                                HoverHandler { id: addHover }
                                TapHandler { onTapped: addTagBtn.parent.addTypedTag() }
                            }
                        }
                        Text {
                            visible: filterPopup.popupTags.length === 0
                            leftPadding: 6
                            text: App.uiText(App.language, "FilterNoTagsShort")
                            color: App.themeTextMuted; font.pixelSize: 12
                        }
                        Repeater {
                            model: filterPopup.popupTags
                            delegate: OptionRow {
                                required property var modelData
                                width: filterPopup.detailW
                                radio: false
                                label: modelData
                                tint: bar.tagColorOf(modelData)
                                dot: bar.tagColorOf(modelData)
                                on: bar.activeTags.indexOf(modelData) >= 0
                                onPicked: bar.toggleTag(modelData)
                            }
                        }
                    }
                }

            }
        }

        // Sucht LIVE bei jeder Änderung; gefiltert wird im Proxy (`galleryModel.searchText`), nicht in QML, und nur in
        // Anzeigename, Dateiname und Tags des offenen Ordners - nicht im Dateiinhalt. UND-verknüpft mit allem anderen.
        Rectangle {
            id: searchBox
            anchors.verticalCenter: parent.verticalCenter
            width: 250; height: 28; radius: 5
            color: App.themeCard
            border.color: searchInput.activeFocus ? App.themeAccent : App.themeBorder
            border.width: 1

            DrawnIcon {
                id: searchIcon
                anchors.left: parent.left; anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 14
                color: searchInput.activeFocus ? App.themeAccent : App.themeTextMuted
            }
            TextField {
                id: searchInput
                objectName: "gallerySearch"
                anchors.left: searchIcon.right; anchors.leftMargin: 8
                anchors.right: clearSearch.left; anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height - 2
                padding: 0
                font.pixelSize: 13
                color: App.themeTextPrimary
                placeholderText: App.uiText(App.language, "FilterSearchPlaceholder")
                background: null
                verticalAlignment: TextInput.AlignVCenter
                // Erst nach 150 ms Ruhe filtern: jede Taste filterte alle Zeilen neu. Auch nicht die erste Taste sofort - ihr
                // Lauf liess die Unterordner-Suche mitten im Tippen für einen Buchstaben anspringen. Leeren gilt sofort.
                onTextChanged: {
                    translitHaken.pruefe()
                    if (text.length > 0) { suchRuhe.restart(); return }
                    suchRuhe.stop()
                    galleryModel.searchText = text
                }
                Timer { id: suchRuhe; interval: 150; onTriggered: galleryModel.searchText = searchInput.text }
                TranslitInput { id: translitHaken; feld: searchInput }
                Keys.onEscapePressed: function(event) {
                    searchInput.text = ""
                    searchInput.focus = false
                    event.accepted = true
                }
            }
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

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: searchInput.text.trim().length > 0
            text: galleryModel.count + " " + App.uiText(App.language, "FilterSearchHits")
            color: galleryModel.count === 0 ? App.themeTextMuted : App.themeAccent
            font.pixelSize: 12
        }

        //  Im Player-Modus faellt beides weg: eine neue Datei anzulegen und
        //  PDF-Seiten zu extrahieren hat dort keinen Sinn.
        ToolSeparator {
            anchors.verticalCenter: parent.verticalCenter
            visible: !bar.audioOnly
            width: visible ? implicitWidth : 0
        }

        Button {
            id: plusBtn
            objectName: "plusBtn"
            anchors.verticalCenter: parent.verticalCenter
            visible: !bar.audioOnly
            width: visible ? implicitWidth : 0
            height: 28
            padding: 0
            enabled: App.currentFolder.length > 0
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
                implicitWidth: 28
                color: plusBtn.hovered || plusBtn.down
                       ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.08)
                       : App.themeCard
                border.color: (plusMenu.opened || createPopup.opened) ? App.themeAccent : App.themeBorder
                border.width: 1
                radius: 5
            }
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

            Popup {
                id: plusMenu
                objectName: "plusMenu"
                y: plusBtn.height + 6
                width: 170
                padding: 6

                background: PopupPanel { }

                contentItem: Column {
                    spacing: 2
                    Repeater {
                        model: [ { act: "folder", icon: "folder", key: "FolderNewTitle" },
                                 { act: "file",   icon: "file",   key: "CreateFileBtn"  } ]
                        delegate: Rectangle {
                            id: actRow
                            required property var modelData
                            width: parent.width; height: 28; radius: 5
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
                                    color: App.themeTextPrimary; font.pixelSize: 13
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

            Popup {
                id: createPopup
                objectName: "createPopup"   // fuer `bench_qmlscene`
                y: plusBtn.height + 4
                width: 260
                padding: 12
                property string kind: "pdf"    // "pdf" | "html" | "txt" | "docx" | "free"

                function doCreate() {
                    if (createNameField.text.trim().length === 0)
                        return
                    App.createEmptyFile(createPopup.kind, createNameField.text,
                                        bar.actionFolder)
                    createPopup.close()
                }

                background: PopupPanel { }

                contentItem: Column {
                    spacing: 8

                    Text {
                        text: App.uiText(App.language, "CreateFileTitle")
                        color: App.themeTextPrimary; font.pixelSize: 13; font.bold: true
                    }

                    Repeater {
                        model: [ { kind: "pdf",  key: "CreateFileTypePdf"  },
                                 { kind: "html", key: "CreateFileTypeHtml" },
                                 { kind: "txt",  key: "CreateFileTypeTxt"  },
                                 { kind: "docx", key: "CreateFileTypeDocx" },
                                 { kind: "free", key: "CreateFileTypeFree" } ]
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
                        width: parent.width
                        text: createPopup.kind === "free"
                              ? App.uiText(App.language, "CreateFileNameFreeLabel")
                              : App.uiText(App.language, "CreateFileNameLabel")
                        color: App.themeTextMuted; font.pixelSize: 11
                        wrapMode: Text.WordWrap
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

        Button {
            id: extractBtn
            anchors.verticalCenter: parent.verticalCenter
            visible: !bar.audioOnly
            width: visible ? implicitWidth : 0
            height: 28
            font.pixelSize: 13
            enabled: App.currentFolder.length > 0
            text: App.uiText(App.language, "FilterExtractBtn")
            onClicked: bar.extractPagesRequested("")   // der offene Ordner
        }

        ToolSeparator {
            anchors.verticalCenter: parent.verticalCenter
            visible: !bar.audioOnly
            width: visible ? implicitWidth : 0
        }

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
