pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

// ── Design: Profile + Custom-Theme-Editor ────────────────────────────────────
Item {
    id: root

    //  DesignProfile::Custom - der LETZTE Eintrag. Er ist am 2026-09-02 von 8
    //  auf 7 gerueckt, weil „Neon Purple" entfallen ist (s. `ISettings.h`).
    readonly property int customIndex: 7
    readonly property bool customActive: App.designProfile === root.customIndex

    property var profiles: []
    function refreshProfiles() { profiles = App.designProfiles() }

    // ── Arbeitskopie des Custom-Themes (reaktive Einzel-Properties) ──────────
    property string twName: "Custom"
    property color twBackground: "#0a1216"
    property color twCard: "#121c22"
    property color twTextPrimary: "#dcebd8"
    property color twTextMuted: "#789891"
    property color twBorder: "#28303c"
    property int   twAccentType: 0
    property color twAccent: "#00b4a0"
    property color twAccentGradEnd: "#0078c8"
    property real  twGlowRadius: 8
    property real  twGlowIntensity: 0.6
    property bool  twBgIsGradient: false
    property color twBgGradStart: "#0a1216"
    property color twBgGradEnd: "#0a1216"
    property int   twBgGradAngle: 180
    property int   twTileBgType: 0
    property color twTileBgColor: "#121c22"
    property color twTileBgGradEnd: "#121c22"
    property int   twTileBgGradAngle: 180
    property bool  twTileGlowOnHover: false
    property real  twTileGlowRadius: 6
    property color twPdfViewerBg: "#0d1518"
    property color twPdfThumbBg: "#ffffff"
    property color twPdfSidebarBg: "#0a1216"
    property color twPdfToolbarBg: "#121c22"
    property color twPdfScrollbarBg: "#121c22"
    property color twSidebarBg: "#121c22"
    property color twEditorBgText: "#121c22"
    property color twEditorBgHtml: "#121c22"
    property color twMenuBarBg: "#0c141a"
    property color twToolbarBg: "#0c141a"
    property color twFilterBarBg: "#0c141a"
    property color twStatusBarBg: "#080e12"

    function loadTheme() {
        var m = App.customThemeMap()
        twName = m.name;                 twBackground = m.background;     twCard = m.card
        twTextPrimary = m.textPrimary;   twTextMuted = m.textMuted;       twBorder = m.border
        twAccentType = m.accentType;     twAccent = m.accent;             twAccentGradEnd = m.accentGradEnd
        twGlowRadius = m.glowRadius;      twGlowIntensity = m.glowIntensity
        twBgIsGradient = m.bgIsGradient; twBgGradStart = m.bgGradStart;   twBgGradEnd = m.bgGradEnd
        twBgGradAngle = m.bgGradAngle
        twTileBgType = m.tileBgType;     twTileBgColor = m.tileBgColor;   twTileBgGradEnd = m.tileBgGradEnd
        twTileBgGradAngle = m.tileBgGradAngle
        twTileGlowOnHover = m.tileGlowOnHover; twTileGlowRadius = m.tileGlowRadius
        twPdfViewerBg = m.pdfViewerBg;   twPdfThumbBg = m.pdfThumbBg;     twPdfSidebarBg = m.pdfSidebarBg
        twPdfToolbarBg = m.pdfToolbarBg; twPdfScrollbarBg = m.pdfScrollbarBg
        twSidebarBg = m.sidebarBg;       twMenuBarBg = m.menuBarBg;       twToolbarBg = m.toolbarBg
        twEditorBgText = m.editorBgText; twEditorBgHtml = m.editorBgHtml
        twFilterBarBg = m.filterBarBg;   twStatusBarBg = m.statusBarBg
    }

    function buildMap() {
        return {
            name: twName, background: twBackground, card: twCard,
            textPrimary: twTextPrimary, textMuted: twTextMuted, border: twBorder,
            accentType: twAccentType, accent: twAccent, accentGradEnd: twAccentGradEnd,
            glowRadius: twGlowRadius, glowIntensity: twGlowIntensity,
            bgIsGradient: twBgIsGradient, bgGradStart: twBgGradStart, bgGradEnd: twBgGradEnd,
            bgGradAngle: twBgGradAngle,
            tileBgType: twTileBgType, tileBgColor: twTileBgColor, tileBgGradEnd: twTileBgGradEnd,
            tileBgGradAngle: twTileBgGradAngle,
            tileGlowOnHover: twTileGlowOnHover, tileGlowRadius: twTileGlowRadius,
            pdfViewerBg: twPdfViewerBg, pdfThumbBg: twPdfThumbBg, pdfSidebarBg: twPdfSidebarBg,
            pdfToolbarBg: twPdfToolbarBg, pdfScrollbarBg: twPdfScrollbarBg,
            sidebarBg: twSidebarBg, menuBarBg: twMenuBarBg, toolbarBg: twToolbarBg,
            editorBgText: twEditorBgText, editorBgHtml: twEditorBgHtml,
            filterBarBg: twFilterBarBg, statusBarBg: twStatusBarBg
        }
    }

    // Live-Vorschau: wirkt sichtbar nur, wenn das Custom-Profil aktiv ist.
    function applyLive() {
        if (root.customActive) App.setCustomThemeFromMap(buildMap())
    }

    Component.onCompleted: { refreshProfiles(); loadTheme() }
    Connections {
        target: App
        function onThemeChanged() { root.refreshProfiles() }
    }

    // ── Der GROSSE Block (zweite Ebene) ──────────────────────────────────────
    //  Er muss ANDERS aussehen als die `SettingsGroup`s darin, sonst liest sich
    //  die Verschachtelung als „Fenster im Fenster" - dieselbe Lehre, an der die
    //  zweite Reiterleiste im Allgemein-Reiter gescheitert ist. Deshalb:
    //  DUNKLERE Fläche als die Gruppen (Festlegung des Nutzers), abgesetzter
    //  Rahmen, größerer und fetter Kopf mit Trennlinie darunter.
    component BigGroup: Rectangle {
        id: big
        default property alias content: innerCol.data
        property string title: ""
        property string groupKey: ""
        property bool collapsed: false

        Layout.fillWidth: true
        //  Dunkler als `themeCard`: der Block liegt UNTER den Gruppen, nicht
        //  auf derselben Ebene.
        color: Qt.rgba(0, 0, 0, 0.22)
        border.color: App.themeBorder
        border.width: 1
        radius: 10
        implicitHeight: big.collapsed ? (head.height + 16)
                                      : (head.height + innerCol.implicitHeight + 26)

        Component.onCompleted:
            if (big.groupKey.length > 0)
                big.collapsed = App.settingsGroupCollapsed(big.groupKey)
        onCollapsedChanged:
            if (big.groupKey.length > 0)
                App.setSettingsGroupCollapsed(big.groupKey, big.collapsed)

        Item {
            id: head
            anchors { left: parent.left; right: parent.right; top: parent.top
                      margins: 12 }
            height: 30

            DrawnIcon {
                id: chev
                anchors { left: parent.left; verticalCenter: titleText.verticalCenter }
                name: big.collapsed ? "chevron-right" : "chevron-down"
                size: 14
                color: App.themeTextMuted
            }
            Text {
                id: titleText
                anchors { left: chev.right; leftMargin: 8; top: parent.top }
                text: big.title
                color: App.themeTextPrimary
                font.pixelSize: 15
                font.bold: true
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: App.themeBorder
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: big.collapsed = !big.collapsed
            }
        }

        ColumnLayout {
            id: innerCol
            anchors { left: parent.left; right: parent.right; top: head.bottom
                      leftMargin: 12; rightMargin: 12; topMargin: 10 }
            spacing: 14
            visible: !big.collapsed
        }
    }

    // ── Wiederverwendbare Farbzeile ──────────────────────────────────────────
    component ColorRow: RowLayout {
        id: cr
        property string label: ""
        property color value: "#000000"
        signal picked(color c)
        spacing: 8
        Layout.fillWidth: true
        Label {
            text: cr.label; color: App.themeTextPrimary
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            elide: Text.ElideRight
            maximumLineCount: 1
        }
        ColorPicker {
            width: 36; height: 22; showAlpha: false
            Layout.preferredWidth: 36; Layout.preferredHeight: 22
            title: cr.label
            selectedColor: cr.value
            onColorPicked: (c) => cr.picked(c)
        }
    }

    ScrollView {
        id: designScroll
        objectName: "designScroll"
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 14

            // ══ Block 1: OBERFLAECHE ═════════════════════════════════════
            BigGroup {
                title: App.uiText(App.language, "SettingsDesignBlockUi")
                groupKey: "design.block.ui"
                // ── Profil-Auswahl ────────────────────────────────────────────────
                SettingsGroup {
                    key: "design.profile"
                    title: App.uiText(App.language, "SettingsDesignProfileLabel")
                    Layout.fillWidth: true

                    GridLayout {
                        Layout.fillWidth: true
                        //  VIER je Reihe: mit acht Profilen sind das genau zwei
                        //  volle Reihen (Festlegung des Nutzers 2026-09-02, dafuer
                        //  ist „Neon Purple" entfallen).
                        columns: 4
                        rowSpacing: 8; columnSpacing: 8

                        Repeater {
                            model: root.profiles
                            delegate: Rectangle {
                                id: card
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 78
                                radius: 8
                                readonly property bool sel: App.designProfile === card.modelData.index
                                color: sel ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.12)
                                           : Qt.rgba(1, 1, 1, 0.03)
                                border.color: sel ? App.themeAccent : App.themeBorder
                                border.width: sel ? 2 : 1

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        App.setDesignProfile(card.modelData.index)
                                        if (card.modelData.index === root.customIndex) root.loadTheme()
                                    }
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 4

                                    RowLayout {
                                        spacing: 6
                                        Text { text: card.modelData.icon; font.pixelSize: 16 }
                                        Text {
                                            text: card.modelData.name
                                            color: App.themeTextPrimary
                                            font.pixelSize: 13; font.bold: true
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        // Mini-Swatches
                                        Row {
                                            spacing: 3
                                            Repeater {
                                                model: [card.modelData.background, card.modelData.card, card.modelData.accent]
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    width: 12; height: 12; radius: 3
                                                    color: modelData
                                                    border.color: Qt.rgba(1, 1, 1, 0.25)
                                                }
                                            }
                                        }
                                    }
                                    Text {
                                        text: card.modelData.description
                                        color: App.themeTextMuted
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Custom-Editor ─────────────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    enabled: root.customActive
                    opacity: root.customActive ? 1.0 : 0.45

                    Text {
                        visible: !root.customActive
                        Layout.fillWidth: true
                        text: App.uiText(App.language, "SettingsDesignCustomHint")
                        color: App.themeTextMuted; font.pixelSize: 11
                    }

                    SettingsGroup {
                        key: "design.base-colors"
                        title: App.uiText(App.language, "SettingsDesignBaseColors")
                        Layout.fillWidth: true
                        ColorRow { label: App.uiText(App.language, "SettingsDesignBackground");      value: root.twBackground;  onPicked: (c) => { root.twBackground = c;  root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignCardTile");   value: root.twCard;        onPicked: (c) => { root.twCard = c;        root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignTextPrimary");      value: root.twTextPrimary; onPicked: (c) => { root.twTextPrimary = c; root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignTextMuted");    value: root.twTextMuted;   onPicked: (c) => { root.twTextMuted = c;   root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignFrame");           value: root.twBorder;      onPicked: (c) => { root.twBorder = c;      root.applyLive() } }
                    }

                    SettingsGroup {
                        key: "design.accent"
                        title: App.uiText(App.language, "SettingsDesignAccent")
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            // Rechtsbündig wie die Farbfelder der ColorRow: das Label
                            // füllt die Zeile, die Box rückt an den rechten Rand.
                            Label { text: App.uiText(App.language, "SettingsDesignAccentType"); color: App.themeTextPrimary
                                    Layout.fillWidth: true; elide: Text.ElideRight }
                            ComboBox {
                                Layout.preferredWidth: 200
                                model: [App.uiText(App.language, "SettingsDesignAccentSolid"), App.uiText(App.language, "SettingsDesignAccentGradient"), App.uiText(App.language, "SettingsDesignGlow")]
                                currentIndex: root.twAccentType
                                onActivated: { root.twAccentType = currentIndex; root.applyLive() }
                            }
                        }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignAccentColorLabel"); value: root.twAccent; onPicked: (c) => { root.twAccent = c; root.applyLive() } }
                        ColorRow {
                            label: App.uiText(App.language, "SettingsDesignAccentGradEnd"); value: root.twAccentGradEnd
                            visible: root.twAccentType !== 0
                            onPicked: (c) => { root.twAccentGradEnd = c; root.applyLive() }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: root.twAccentType === 2
                            Label { text: App.uiText(App.language, "SettingsDesignGlowRadiusLabel"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                            Slider {
                                Layout.fillWidth: true; from: 0; to: 40
                                value: root.twGlowRadius
                                onMoved: { root.twGlowRadius = value; root.applyLive() }
                            }
                            Label { text: Math.round(root.twGlowRadius); color: App.themeTextMuted }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: root.twAccentType === 2
                            Label { text: App.uiText(App.language, "SettingsDesignGlowIntensityLabel"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                            Slider {
                                Layout.fillWidth: true; from: 0; to: 1
                                value: root.twGlowIntensity
                                onMoved: { root.twGlowIntensity = value; root.applyLive() }
                            }
                            Label { text: root.twGlowIntensity.toFixed(2); color: App.themeTextMuted }
                        }
                    }

                    SettingsGroup {
                        key: "design.bg-gradient"
                        title: App.uiText(App.language, "SettingsDesignBgGradient")
                        Layout.fillWidth: true
                        CheckBox {
                            text: App.uiText(App.language, "SettingsDesignGradInsteadSolid")
                            checked: root.twBgIsGradient
                            onToggled: { root.twBgIsGradient = checked; root.applyLive() }
                            contentItem: Text {
                                text: parent.text; color: App.themeTextPrimary
                                leftPadding: parent.indicator.width + 6; verticalAlignment: Text.AlignVCenter
                            }
                        }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignGradStartLabel"); value: root.twBgGradStart; visible: root.twBgIsGradient; onPicked: (c) => { root.twBgGradStart = c; root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignGradEndLabel");  value: root.twBgGradEnd;   visible: root.twBgIsGradient; onPicked: (c) => { root.twBgGradEnd = c;   root.applyLive() } }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: root.twBgIsGradient
                            Label { text: App.uiText(App.language, "SettingsDesignAngleLabel"); color: App.themeTextPrimary
                                    Layout.fillWidth: true; elide: Text.ElideRight }
                            SpinBox {
                                from: 0; to: 360; stepSize: 5
                                value: root.twBgGradAngle
                                onValueModified: { root.twBgGradAngle = value; root.applyLive() }
                            }
                        }
                    }

                    SettingsGroup {
                        key: "design.tile-bg"
                        title: App.uiText(App.language, "SettingsDesignTileBgLabel")
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Label { text: App.uiText(App.language, "SettingsDesignTypeLabel"); color: App.themeTextPrimary
                                    Layout.fillWidth: true; elide: Text.ElideRight }
                            ComboBox {
                                Layout.preferredWidth: 200
                                model: [App.uiText(App.language, "SettingsDesignAccentSolid"), App.uiText(App.language, "SettingsDesignAccentGradient"), App.uiText(App.language, "SettingsDesignTileTransparent")]
                                currentIndex: root.twTileBgType
                                onActivated: { root.twTileBgType = currentIndex; root.applyLive() }
                            }
                        }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignTileColorLabel");  value: root.twTileBgColor;  visible: root.twTileBgType !== 2; onPicked: (c) => { root.twTileBgColor = c;  root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignTileGradEndLabel"); value: root.twTileBgGradEnd; visible: root.twTileBgType === 1; onPicked: (c) => { root.twTileBgGradEnd = c; root.applyLive() } }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: root.twTileBgType === 1
                            Label { text: App.uiText(App.language, "SettingsDesignGradAngle"); color: App.themeTextPrimary
                                    Layout.fillWidth: true; elide: Text.ElideRight }
                            SpinBox {
                                from: 0; to: 360; stepSize: 5
                                value: root.twTileBgGradAngle
                                onValueModified: { root.twTileBgGradAngle = value; root.applyLive() }
                            }
                        }
                        CheckBox {
                            text: App.uiText(App.language, "SettingsDesignGlowHover")
                            checked: root.twTileGlowOnHover
                            onToggled: { root.twTileGlowOnHover = checked; root.applyLive() }
                            contentItem: Text {
                                text: parent.text; color: App.themeTextPrimary
                                leftPadding: parent.indicator.width + 6; verticalAlignment: Text.AlignVCenter
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: root.twTileGlowOnHover
                            Label { text: App.uiText(App.language, "SettingsDesignGlowRadiusLabel"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                            Slider {
                                Layout.fillWidth: true; from: 0; to: 40
                                value: root.twTileGlowRadius
                                onMoved: { root.twTileGlowRadius = value; root.applyLive() }
                            }
                            Label { text: Math.round(root.twTileGlowRadius); color: App.themeTextMuted }
                        }
                    }

                    SettingsGroup {
                        key: "design.bars"
                        title: App.uiText(App.language, "SettingsDesignBars")
                        Layout.fillWidth: true
                        ColorRow { label: App.uiText(App.language, "SettingsDesignMenuBar");   value: root.twMenuBarBg;   onPicked: (c) => { root.twMenuBarBg = c;   root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignToolbar"); value: root.twToolbarBg; onPicked: (c) => { root.twToolbarBg = c;   root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignFilterBar");  value: root.twFilterBarBg; onPicked: (c) => { root.twFilterBarBg = c; root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignStatusBar");  value: root.twStatusBarBg; onPicked: (c) => { root.twStatusBarBg = c; root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignSidebar");  value: root.twSidebarBg;   onPicked: (c) => { root.twSidebarBg = c;   root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignEditorBgText"); value: root.twEditorBgText; onPicked: (c) => { root.twEditorBgText = c; root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignEditorBgHtml"); value: root.twEditorBgHtml; onPicked: (c) => { root.twEditorBgHtml = c; root.applyLive() } }
                    }

                    SettingsGroup {
                        key: "design.pdf-viewer"
                        title: App.uiText(App.language, "SettingsDesignPdfViewer")
                        Layout.fillWidth: true
                        ColorRow { label: App.uiText(App.language, "SettingsDesignViewerBg"); value: root.twPdfViewerBg;    onPicked: (c) => { root.twPdfViewerBg = c;    root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignThumbBg");  value: root.twPdfThumbBg;     onPicked: (c) => { root.twPdfThumbBg = c;     root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignSidebar");           value: root.twPdfSidebarBg;   onPicked: (c) => { root.twPdfSidebarBg = c;   root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignToolbar");         value: root.twPdfToolbarBg;   onPicked: (c) => { root.twPdfToolbarBg = c;   root.applyLive() } }
                        ColorRow { label: App.uiText(App.language, "SettingsDesignScrollbar");           value: root.twPdfScrollbarBg; onPicked: (c) => { root.twPdfScrollbarBg = c; root.applyLive() } }
                    }

                    SettingsGroup {
                        key: "design.theme-name-export"
                        title: App.uiText(App.language, "SettingsDesignThemeNameExport")
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Label { text: App.uiText(App.language, "FilterName"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                            TextField {
                                Layout.fillWidth: true
                                text: root.twName
                                color: App.themeTextPrimary
                                onEditingFinished: { root.twName = text; root.applyLive() }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Button {
                                text: App.uiText(App.language, "SettingsDesignApplyBtn")
                                highlighted: true
                                onClicked: { App.setDesignProfile(root.customIndex); App.setCustomThemeFromMap(root.buildMap()) }
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: App.uiText(App.language, "SettingsDesignExportBtn")
                                onClicked: { App.setCustomThemeFromMap(root.buildMap()); exportDialog.open() }
                            }
                            Button {
                                text: App.uiText(App.language, "SettingsDesignImportBtn")
                                onClicked: importDialog.open()
                            }
                        }
                    }
                }

            }

            // ══ Block 2: TEXT-EDITOR ═════════════════════════════════════
            //  Voellig eigen: eigene Profile, eigene Palette, eigener Konfigurator.
            //  Er teilt sich mit der Oberflaeche KEINE Farbe - ein Wechsel des
            //  App-Themes laesst den Editor unberuehrt und umgekehrt
            //  (Festlegung des Nutzers 2026-09-02).
            BigGroup {
                title: App.uiText(App.language, "SettingsDesignBlockEditor")
                groupKey: "design.block.editor"

                SettingsGroup {
                    key: "design.editor.profile"
                    title: App.uiText(App.language, "SettingsEditorProfileLabel")
                    Layout.fillWidth: true

                    GridLayout {
                        Layout.fillWidth: true
                        //  VIER in EINER Reihe - genau die vier Profile
                        //  (drei mitgelieferte + Eigenes).
                        columns: 4
                        rowSpacing: 8; columnSpacing: 8

                        Repeater {
                            model: 4
                            delegate: Rectangle {
                                id: eCard
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredHeight: 62
                                radius: 8
                                readonly property bool sel: Editor.profile === eCard.index
                                color: sel ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                     App.themeAccent.b, 0.12)
                                           : Qt.rgba(1, 1, 1, 0.03)
                                border.color: sel ? App.themeAccent : App.themeBorder
                                border.width: sel ? 2 : 1

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Editor.profile = eCard.index
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 6
                                    Text {
                                        text: Editor.profileLabel(eCard.index)
                                        color: App.themeTextPrimary
                                        font.pixelSize: 13; font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    //  Drei Proben wie bei den Oberflaechen-Karten:
                                    //  Flaeche, Schluesselwort, Zeichenkette.
                                    Row {
                                        spacing: 4
                                        Repeater {
                                            model: Editor.profileSwatches(eCard.index)
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: 22; height: 14; radius: 3
                                                color: modelData
                                                border.color: Qt.rgba(1, 1, 1, 0.25)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            Layout.fillWidth: true
                            visible: !Editor.customActive
                            text: App.uiText(App.language, "SettingsEditorCustomHint")
                            color: App.themeTextMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                        Item { Layout.fillWidth: true; visible: Editor.customActive }
                        Button {
                            text: App.uiText(App.language, "SettingsEditorSeedCustom")
                            visible: !Editor.customActive
                            onClicked: Editor.seedCustomFrom(Editor.profile)
                        }
                        Button {
                            text: App.uiText(App.language, "SettingsEditorResetColors")
                            visible: Editor.customActive
                            onClicked: Editor.resetCustom()
                        }
                        //  Sichern und Laden wie beim Oberflächen-Thema - nur
                        //  dass hier die EDITOR-Palette in die Datei geht
                        //  (dasselbe JSON, das in den Einstellungen steht).
                        Button {
                            text: App.uiText(App.language, "SettingsDesignExportBtn")
                            onClicked: edExportDialog.open()
                        }
                        Button {
                            text: App.uiText(App.language, "SettingsDesignImportBtn")
                            onClicked: edImportDialog.open()
                        }
                    }
                }

                //  Die Farbzeilen. Der Schluessel steht EINMAL da; den Namen
                //  liefert `Editor.colorLabel` aus C++ - ein in QML
                //  zusammengesetzter String-Key waere fuer den Katalog-Treiber
                //  (`core.strings`) ein Torso.
                component EdColorRow: RowLayout {
                    id: ecr
                    property string colorKey: ""
                    Layout.fillWidth: true
                    spacing: 10
                    Label {
                        text: Editor.colorLabel(ecr.colorKey)
                        color: App.themeTextPrimary
                        Layout.preferredWidth: 180
                    }
                    Item { Layout.fillWidth: true }
                    ColorPicker {
                        implicitWidth: 44; implicitHeight: 24
                        showAlpha: false
                        title: Editor.colorLabel(ecr.colorKey)
                        selectedColor: Editor.colorFor(ecr.colorKey)
                        onColorPicked: function (c) {
                            Editor.setColorFor(ecr.colorKey, c)
                            selectedColor = Qt.binding(function () {
                                return Editor.colorFor(ecr.colorKey)
                            })
                        }
                    }
                }

                //  Nur im Profil „Custom" bedienbar - anderswo waere die
                //  Aenderung beim naechsten Profilwechsel still weg.
                SettingsGroup {
                    key: "design.editor.surface"
                    title: App.uiText(App.language, "SettingsEditorSurfaceGroup")
                    Layout.fillWidth: true
                    enabled: Editor.customActive
                    opacity: Editor.customActive ? 1.0 : 0.45

                    EdColorRow { colorKey: "background" }
                    EdColorRow { colorKey: "text" }
                    EdColorRow { colorKey: "currentLine" }
                    EdColorRow { colorKey: "selection" }
                    EdColorRow { colorKey: "gutterBackground" }
                    EdColorRow { colorKey: "gutterText" }
                    EdColorRow { colorKey: "gutterTextActive" }
                }

                SettingsGroup {
                    key: "design.editor.syntax"
                    title: App.uiText(App.language, "SettingsEditorSyntaxGroup")
                    Layout.fillWidth: true
                    enabled: Editor.customActive
                    opacity: Editor.customActive ? 1.0 : 0.45

                    EdColorRow { colorKey: "keyword" }
                    EdColorRow { colorKey: "type" }
                    EdColorRow { colorKey: "string" }
                    EdColorRow { colorKey: "number" }
                    EdColorRow { colorKey: "comment" }
                    EdColorRow { colorKey: "preproc" }
                    EdColorRow { colorKey: "function" }
                    EdColorRow { colorKey: "operator" }
                    EdColorRow { colorKey: "heading" }
                    EdColorRow { colorKey: "emphasis" }
                    EdColorRow { colorKey: "link" }
                    EdColorRow { colorKey: "code" }
                }

                //  Vorschau: dieselbe Palette, ohne dass man eine Datei oeffnen
                //  muss. Bewusst gestellter Text statt eines echten Editors -
                //  ein zweiter `TextArea` mit eigenem Faerber kostete Speicher
                //  fuer nichts.
                SettingsGroup {
                    key: "design.editor.preview"
                    title: App.uiText(App.language, "SettingsEditorPreviewGroup")
                    Layout.fillWidth: true

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: vorschau.implicitHeight + 20
                        radius: 6
                        color: Editor.background
                        border.color: App.themeBorder

                        Row {
                            anchors.fill: parent
                            anchors.margins: 1

                            //  Nummernspalte der Vorschau
                            Rectangle {
                                width: 34; height: parent.height
                                color: Editor.gutterBackground
                                Column {
                                    anchors { top: parent.top; topMargin: 10
                                              right: parent.right; rightMargin: 8 }
                                    spacing: 2
                                    Repeater {
                                        model: 5
                                        delegate: Text {
                                            required property int index
                                            text: (index + 1)
                                            color: index === 1 ? Editor.gutterTextActive
                                                               : Editor.gutterText
                                            font: App.fallbackFont("monospace", 12)
                                        }
                                    }
                                }
                            }

                            Column {
                                id: vorschau
                                padding: 10
                                spacing: 2
                                //  Jede Zeile zeigt andere Klassen - so sieht man
                                //  beim Einstellen sofort, was sich aendert.
                                Text {
                                    font: App.fallbackFont("monospace", 12)
                                    textFormat: Text.StyledText
                                    text: "<font color='" + Editor.colorFor("preproc") + "'>#include</font>"
                                        + " <font color='" + Editor.colorFor("string") + "'>&lt;QString&gt;</font>"
                                }
                                Text {
                                    font: App.fallbackFont("monospace", 12)
                                    textFormat: Text.StyledText
                                    text: "<font color='" + Editor.colorFor("type") + "'>int</font>"
                                        + " <font color='" + Editor.colorFor("function") + "'>berechne</font>"
                                        + "<font color='" + Editor.colorFor("operator") + "'>(</font>"
                                        + "<font color='" + Editor.colorFor("type") + "'>int</font> wert"
                                        + "<font color='" + Editor.colorFor("operator") + "'>) {</font>"
                                }
                                Text {
                                    font: App.fallbackFont("monospace", 12)
                                    textFormat: Text.StyledText
                                    text: "&nbsp;&nbsp;<font color='" + Editor.colorFor("keyword") + "'>return</font>"
                                        + " wert <font color='" + Editor.colorFor("operator") + "'>*</font> "
                                        + "<font color='" + Editor.colorFor("number") + "'>42</font>"
                                        + "<font color='" + Editor.colorFor("operator") + "'>;</font>"
                                        + "  <font color='" + Editor.colorFor("comment") + "'>// Kommentar</font>"
                                }
                                Text {
                                    font: App.fallbackFont("monospace", 12)
                                    textFormat: Text.StyledText
                                    text: "<font color='" + Editor.colorFor("heading") + "'><b>## Ueberschrift</b></font>"
                                        + "  <font color='" + Editor.colorFor("emphasis") + "'><i>*kursiv*</i></font>"
                                }
                                Text {
                                    font: App.fallbackFont("monospace", 12)
                                    textFormat: Text.StyledText
                                    text: "<font color='" + Editor.colorFor("link") + "'>[Verweis](ziel)</font>"
                                        + "  <font color='" + Editor.colorFor("code") + "'>`code`</font>"
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    //  Ex- und Import der EDITOR-Palette (die beiden darunter gehören dem
    //  Oberflächen-Thema).
    FileChooser {
        id: edExportDialog
        title: App.uiText(App.language, "SettingsEditorExportTitle")
        fileMode: FileChooser.SaveFile
        nameFilters: [App.uiText(App.language, "SettingsDesignThemeFileFilter"),
                      App.uiText(App.language, "SettingsDesignAllFilesFilter")]
        defaultSuffix: "json"
        onAccepted: Editor.exportPalette(selectedFile)
    }

    FileChooser {
        id: edImportDialog
        title: App.uiText(App.language, "SettingsEditorImportTitle")
        fileMode: FileChooser.OpenFile
        nameFilters: [App.uiText(App.language, "SettingsDesignThemeFileFilter"),
                      App.uiText(App.language, "SettingsDesignAllFilesFilter")]
        onAccepted: Editor.importPalette(selectedFile)
    }

    FileChooser {
        id: exportDialog
        title: App.uiText(App.language, "SettingsDesignExportTitle")
        fileMode: FileChooser.SaveFile
        nameFilters: [App.uiText(App.language, "SettingsDesignThemeFileFilter"), App.uiText(App.language, "SettingsDesignAllFilesFilter")]
        defaultSuffix: "json"
        onAccepted: App.exportCustomTheme(selectedFile)
    }

    FileChooser {
        id: importDialog
        title: App.uiText(App.language, "SettingsDesignImportTitle")
        fileMode: FileChooser.OpenFile
        nameFilters: [App.uiText(App.language, "SettingsDesignThemeFileFilter"), App.uiText(App.language, "SettingsDesignAllFilesFilter")]
        onAccepted: { if (App.importCustomTheme(selectedFile)) root.loadTheme() }
    }

    //  Weiches, schnelles Mausrad-Scrollen (Galerie-Muster) statt der festen
    //  60 px je Rastung von `Flickable`.
    SmoothWheelArea { flickable: designScroll.contentItem }
}
