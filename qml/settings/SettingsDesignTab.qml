pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MediaGallery 1.0
import "../common"

Item {
    id: root

    readonly property int customIndex: 7
    readonly property bool customActive: App.designProfile === root.customIndex

    property var profiles: []
    function refreshProfiles() { profiles = App.designProfiles() }

    //  `colorFor` ist ein Aufruf und bindet nicht; erst das Lesen von `profile` (NOTIFY paletteChanged) tut es.
    function edColor(key) { void Editor.profile; return Editor.colorFor(key) }

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
        var m = App.themeMap(App.designProfile)
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

    //  Erste Aenderung in einem mitgelieferten Profil: es wird samt Aenderung zu Custom, das Profil selbst bleibt.
    function applyLive(eigenerName) {
        if (!root.customActive) {
            if (!eigenerName)
                for (var i = 0; i < profiles.length; ++i)
                    if (profiles[i].index === root.customIndex) twName = profiles[i].name
            App.setCustomThemeFromMap(buildMap())
            App.setDesignProfile(root.customIndex)
            return
        }
        App.setCustomThemeFromMap(buildMap())
    }

    Component.onCompleted: { refreshProfiles(); loadTheme() }
    Connections {
        target: App
        function onThemeChanged() { root.refreshProfiles() }
    }

    //  Zwei Spalten ab dieser Breite; darunter steht alles untereinander.
    readonly property bool twoCols: designScroll.availableWidth >= 600
    readonly property real contentW: designScroll.availableWidth - 14
    readonly property real colW: twoCols ? (contentW - 38) / 2 : contentW

    function previewLines() {
        void Editor.profile
        function f(k, s) { return "<font color='" + Editor.colorFor(k) + "'>" + s + "</font>" }
        return [
            f("preproc", "#include") + " " + f("string", "&lt;QString&gt;"),
            f("type", "int") + " " + f("function", "berechne") + f("operator", "(") + f("type", "int")
                + " wert" + f("operator", ") {"),
            "&nbsp;&nbsp;" + f("keyword", "return") + " wert " + f("operator", "*") + " " + f("number", "42")
                + f("operator", ";") + "  " + f("comment", "// Kommentar"),
            f("heading", "<b>## Ueberschrift</b>") + "  " + f("emphasis", "<i>*kursiv*</i>"),
            f("link", "[Verweis](ziel)") + "  " + f("code", "`code`")
        ]
    }

    //  Zeile mit Linie darunter: Beschriftung links, Bedienelement rechts.
    component SettingRow: Item {
        id: sr
        property string label: ""
        //  Nur mit `stretch` reicht der Platz rechts bis an die Beschriftung (Schieber); sonst so schmal wie nötig.
        property bool stretch: false
        //  Rechtsbündig: das ERSTE Kind steht ganz rechts.
        default property alias control: slot.data
        Layout.fillWidth: true
        implicitHeight: 44
        Label {
            id: srLabel
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: Math.min(implicitWidth, sr.width * 0.5)
            text: sr.label
            color: App.themeTextPrimary
            elide: Text.ElideRight
        }
        RowLayout {
            id: slot
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            width: sr.stretch ? sr.width - srLabel.width - 12 : implicitWidth
            layoutDirection: Qt.RightToLeft
            spacing: 8
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: App.themeBorder
            opacity: 0.6
        }
    }

    component ColorRow: SettingRow {
        id: cr
        property color value: "#000000"
        signal picked(color c)
        ColorPicker {
            id: sw
            Layout.preferredWidth: 26; Layout.preferredHeight: 26
            showAlpha: false
            title: cr.label
            selectedColor: cr.value
            //  Der Wähler setzt `selectedColor` selbst und bricht damit die Bindung; ohne sie folgte das Feld keinem
            //  späteren Profilwechsel mehr.
            onColorPicked: (c) => {
                cr.picked(c)
                sw.selectedColor = Qt.binding(() => cr.value)
            }
        }
        Text {
            text: cr.value.toString().toUpperCase()
            color: App.themeTextMuted
            font: App.fallbackFont("monospace", 12)
        }
    }

    component EdColorRow: ColorRow {
        id: ecr
        property string colorKey: ""
        label: Editor.colorLabel(ecr.colorKey)
        value: root.edColor(ecr.colorKey)
        onPicked: (c) => Editor.setColorFor(ecr.colorKey, c)
    }

    //  Verbundene Wahlknöpfe für kurze, gleichrangige Arten (Einfarbig/Verlauf/...).
    component Segmented: Rectangle {
        id: seg
        property var labels: []
        property int current: 0
        signal picked(int index)
        implicitWidth: segRow.implicitWidth + 2
        implicitHeight: 32
        radius: 6
        color: "transparent"
        border.color: App.themeBorder
        Row {
            id: segRow
            anchors { fill: parent; margins: 1 }
            Repeater {
                model: seg.labels
                delegate: Rectangle {
                    id: segItem
                    required property int index
                    required property string modelData
                    readonly property bool on: seg.current === segItem.index
                    width: segText.implicitWidth + 28
                    height: segRow.height
                    radius: 5
                    color: segItem.on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.22)
                                      : (segHover.hovered ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                                                    App.themeTextPrimary.b, 0.05)
                                                          : "transparent")
                    Text {
                        id: segText
                        anchors.centerIn: parent
                        text: segItem.modelData
                        color: segItem.on ? App.themeTextPrimary : App.themeTextMuted
                    }
                    HoverHandler { id: segHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: seg.picked(segItem.index) }
                }
            }
        }
    }

    component SliderRow: SettingRow {
        id: sl
        property real from: 0
        property real to: 1
        property real value: 0
        property int decimals: 0
        signal moved(real v)
        stretch: true
        Label {
            Layout.preferredWidth: 34
            horizontalAlignment: Text.AlignRight
            text: sl.value.toFixed(sl.decimals)
            color: App.themeTextMuted
        }
        Slider {
            Layout.fillWidth: true
            from: sl.from; to: sl.to
            value: sl.value
            onMoved: sl.moved(value)
        }
    }

    component AngleRow: SettingRow {
        id: ar
        property int value: 0
        signal modified(int v)
        SpinBox {
            from: 0; to: 360; stepSize: 5
            value: ar.value
            onValueModified: ar.modified(value)
        }
    }

    component SwitchRow: SettingRow {
        id: swr
        property bool checked: false
        signal toggled(bool on)
        Switch {
            checked: swr.checked
            onToggled: swr.toggled(checked)
        }
    }

    //  Profilkarte mit drei schrägen Farbstreifen; gemeinsam für Oberfläche und Editor.
    component ProfileCard: Rectangle {
        id: card
        property string name: ""
        property var swatches: []
        property bool sel: false
        signal chosen()
        Layout.fillWidth: true
        Layout.preferredHeight: 54
        radius: 8
        //  Heller als der Grund: die dunklen Profilfarben (bis Schwarz) muessen sich abheben.
        color: card.sel ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.20)
                        : (cardHover.hovered ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.06))
        border.color: card.sel ? App.themeAccent : App.themeBorder
        border.width: 1

        HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: card.chosen() }

        Row {
            id: slashes
            //  Von Rahmen zu Rahmen: oben und unten nur die Rahmenbreite frei.
            anchors { left: parent.left; leftMargin: 14; top: parent.top; bottom: parent.bottom
                      topMargin: card.border.width; bottomMargin: card.border.width }
            spacing: 2
            Repeater {
                model: card.swatches
                delegate: Item {
                    id: slash
                    required property var modelData
                    //  Geschert statt Shape: der Software-Renderer beschneidet Shapes ganz ausserhalb der Liste nicht.
                    readonly property real neig: height * 0.3
                    height: slashes.height
                    width: 9 + neig
                    Rectangle {
                        width: 9
                        height: parent.height
                        color: slash.modelData
                        antialiasing: true
                        transform: Matrix4x4 { matrix: Qt.matrix4x4(1, -0.3, 0, slash.neig, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) }
                    }
                }
            }
        }
        Text {
            anchors { left: slashes.right; leftMargin: 12; right: parent.right; rightMargin: 10
                      verticalCenter: parent.verticalCenter }
            text: card.name
            color: App.themeTextPrimary
            font.pixelSize: 13
            wrapMode: Text.WordWrap; maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    ScrollView {
        id: designScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.contentW
            spacing: 30

            SettingsBlock {
                title: App.uiText(App.language, "SettingsDesignBlockUi")
                groupKey: "design.block.ui"

                SettingsGroup {
                    key: "design.profile"
                    title: App.uiText(App.language, "SettingsDesignProfileLabel")
                    Layout.fillWidth: true
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.twoCols ? 4 : 2
                        rowSpacing: 8; columnSpacing: 8

                        Repeater {
                            model: root.profiles
                            delegate: ProfileCard {
                                required property var modelData
                                name: modelData.name
                                swatches: [modelData.background, modelData.card, modelData.accent]
                                sel: App.designProfile === modelData.index
                                onChosen: {
                                    App.setDesignProfile(modelData.index)
                                    root.loadTheme()
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 22

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.twoCols ? 2 : 1
                        columnSpacing: 38
                        rowSpacing: 22

                        ColumnLayout {
                            Layout.preferredWidth: root.colW
                            Layout.alignment: Qt.AlignTop
                            spacing: 22

                            SettingsGroup {
                                key: "design.base-colors"
                                title: App.uiText(App.language, "SettingsDesignBaseColors")
                                Layout.fillWidth: true
                                contentSpacing: 0
                                ColorRow { label: App.uiText(App.language, "SettingsDesignBackground");  value: root.twBackground;  onPicked: (c) => { root.twBackground = c;  root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignCardTile");    value: root.twCard;        onPicked: (c) => { root.twCard = c;        root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignTextPrimary"); value: root.twTextPrimary; onPicked: (c) => { root.twTextPrimary = c; root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignTextMuted");   value: root.twTextMuted;   onPicked: (c) => { root.twTextMuted = c;   root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignFrame");       value: root.twBorder;      onPicked: (c) => { root.twBorder = c;      root.applyLive() } }
                            }

                            SettingsGroup {
                                key: "design.bars"
                                title: App.uiText(App.language, "SettingsDesignBars")
                                Layout.fillWidth: true
                                contentSpacing: 0
                                ColorRow { label: App.uiText(App.language, "SettingsDesignMenuBar");      value: root.twMenuBarBg;    onPicked: (c) => { root.twMenuBarBg = c;    root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignToolbar");      value: root.twToolbarBg;    onPicked: (c) => { root.twToolbarBg = c;    root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignFilterBar");    value: root.twFilterBarBg;  onPicked: (c) => { root.twFilterBarBg = c;  root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignStatusBar");    value: root.twStatusBarBg;  onPicked: (c) => { root.twStatusBarBg = c;  root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignSidebar");      value: root.twSidebarBg;    onPicked: (c) => { root.twSidebarBg = c;    root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignEditorBgText"); value: root.twEditorBgText; onPicked: (c) => { root.twEditorBgText = c; root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignEditorBgHtml"); value: root.twEditorBgHtml; onPicked: (c) => { root.twEditorBgHtml = c; root.applyLive() } }
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: root.colW
                            Layout.alignment: Qt.AlignTop
                            spacing: 22

                            SettingsGroup {
                                key: "design.accent"
                                title: App.uiText(App.language, "SettingsDesignAccent")
                                Layout.fillWidth: true
                                contentSpacing: 0
                                SettingRow {
                                    label: App.uiText(App.language, "SettingsDesignAccentType")
                                    Segmented {
                                        labels: [App.uiText(App.language, "SettingsDesignAccentSolid"),
                                                 App.uiText(App.language, "SettingsDesignAccentGradient"),
                                                 App.uiText(App.language, "SettingsDesignGlow")]
                                        current: root.twAccentType
                                        onPicked: (i) => { root.twAccentType = i; root.applyLive() }
                                    }
                                }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignAccentColorLabel"); value: root.twAccent; onPicked: (c) => { root.twAccent = c; root.applyLive() } }
                                ColorRow {
                                    label: App.uiText(App.language, "SettingsDesignAccentGradEnd"); value: root.twAccentGradEnd
                                    visible: root.twAccentType !== 0
                                    onPicked: (c) => { root.twAccentGradEnd = c; root.applyLive() }
                                }
                                SliderRow {
                                    label: App.uiText(App.language, "SettingsDesignGlowRadiusLabel")
                                    visible: root.twAccentType === 2
                                    from: 0; to: 40; value: root.twGlowRadius
                                    onMoved: (v) => { root.twGlowRadius = v; root.applyLive() }
                                }
                                SliderRow {
                                    label: App.uiText(App.language, "SettingsDesignGlowIntensityLabel")
                                    visible: root.twAccentType === 2
                                    from: 0; to: 1; decimals: 2; value: root.twGlowIntensity
                                    onMoved: (v) => { root.twGlowIntensity = v; root.applyLive() }
                                }
                            }

                            SettingsGroup {
                                key: "design.bg-gradient"
                                title: App.uiText(App.language, "SettingsDesignBgGradient")
                                Layout.fillWidth: true
                                contentSpacing: 0
                                SwitchRow {
                                    label: App.uiText(App.language, "SettingsDesignGradInsteadSolid")
                                    checked: root.twBgIsGradient
                                    onToggled: (on) => { root.twBgIsGradient = on; root.applyLive() }
                                }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignGradStartLabel"); value: root.twBgGradStart; visible: root.twBgIsGradient; onPicked: (c) => { root.twBgGradStart = c; root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignGradEndLabel");   value: root.twBgGradEnd;   visible: root.twBgIsGradient; onPicked: (c) => { root.twBgGradEnd = c;   root.applyLive() } }
                                AngleRow {
                                    label: App.uiText(App.language, "SettingsDesignAngleLabel")
                                    visible: root.twBgIsGradient
                                    value: root.twBgGradAngle
                                    onModified: (v) => { root.twBgGradAngle = v; root.applyLive() }
                                }
                            }

                            SettingsGroup {
                                key: "design.tile-bg"
                                title: App.uiText(App.language, "SettingsDesignTileBgLabel")
                                Layout.fillWidth: true
                                contentSpacing: 0
                                SettingRow {
                                    label: App.uiText(App.language, "SettingsDesignTypeLabel")
                                    Segmented {
                                        labels: [App.uiText(App.language, "SettingsDesignAccentSolid"),
                                                 App.uiText(App.language, "SettingsDesignAccentGradient"),
                                                 App.uiText(App.language, "SettingsDesignTileTransparent")]
                                        current: root.twTileBgType
                                        onPicked: (i) => { root.twTileBgType = i; root.applyLive() }
                                    }
                                }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignTileColorLabel");   value: root.twTileBgColor;   visible: root.twTileBgType !== 2; onPicked: (c) => { root.twTileBgColor = c;   root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignTileGradEndLabel"); value: root.twTileBgGradEnd; visible: root.twTileBgType === 1; onPicked: (c) => { root.twTileBgGradEnd = c; root.applyLive() } }
                                AngleRow {
                                    label: App.uiText(App.language, "SettingsDesignGradAngle")
                                    visible: root.twTileBgType === 1
                                    value: root.twTileBgGradAngle
                                    onModified: (v) => { root.twTileBgGradAngle = v; root.applyLive() }
                                }
                                SwitchRow {
                                    label: App.uiText(App.language, "SettingsDesignGlowHover")
                                    checked: root.twTileGlowOnHover
                                    onToggled: (on) => { root.twTileGlowOnHover = on; root.applyLive() }
                                }
                                SliderRow {
                                    label: App.uiText(App.language, "SettingsDesignGlowRadiusLabel")
                                    visible: root.twTileGlowOnHover
                                    from: 0; to: 40; value: root.twTileGlowRadius
                                    onMoved: (v) => { root.twTileGlowRadius = v; root.applyLive() }
                                }
                            }

                            SettingsGroup {
                                key: "design.pdf-viewer"
                                title: App.uiText(App.language, "SettingsDesignPdfViewer")
                                Layout.fillWidth: true
                                contentSpacing: 0
                                ColorRow { label: App.uiText(App.language, "SettingsDesignViewerBg");  value: root.twPdfViewerBg;    onPicked: (c) => { root.twPdfViewerBg = c;    root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignThumbBg");   value: root.twPdfThumbBg;     onPicked: (c) => { root.twPdfThumbBg = c;     root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignSidebar");   value: root.twPdfSidebarBg;   onPicked: (c) => { root.twPdfSidebarBg = c;   root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignToolbar");   value: root.twPdfToolbarBg;   onPicked: (c) => { root.twPdfToolbarBg = c;   root.applyLive() } }
                                ColorRow { label: App.uiText(App.language, "SettingsDesignScrollbar"); value: root.twPdfScrollbarBg; onPicked: (c) => { root.twPdfScrollbarBg = c; root.applyLive() } }
                            }
                        }
                    }

                    SettingsGroup {
                        key: "design.theme-name-export"
                        title: App.uiText(App.language, "SettingsDesignThemeNameExport")
                        Layout.fillWidth: true
                        contentSpacing: 12
                        RowLayout {
                            Layout.fillWidth: true; spacing: 12
                            Label { text: App.uiText(App.language, "FilterName"); color: App.themeTextPrimary; Layout.preferredWidth: 90 }
                            TextField {
                                Layout.fillWidth: true
                                text: root.twName
                                color: App.themeTextPrimary
                                onEditingFinished: if (text !== root.twName) { root.twName = text; root.applyLive(true) }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Button {
                                text: App.uiText(App.language, "SettingsDesignApplyBtn")
                                highlighted: true
                                onClicked: root.applyLive()
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: App.uiText(App.language, "SettingsDesignExportBtn")
                                onClicked: { if (root.customActive) App.setCustomThemeFromMap(root.buildMap()); exportDialog.open() }
                            }
                            Button {
                                text: App.uiText(App.language, "SettingsDesignImportBtn")
                                onClicked: importDialog.open()
                            }
                        }
                        Text {
                            id: themeImportHint
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: ""
                            color: App.themeTextMuted
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Timer { id: themeImportHintTimer; interval: 6000; onTriggered: themeImportHint.text = "" }
                        }
                    }
                }
            }

            SettingsBlock {
                title: App.uiText(App.language, "SettingsDesignBlockEditor")
                groupKey: "design.block.editor"

                SettingsGroup {
                    key: "design.editor.profile"
                    title: App.uiText(App.language, "SettingsEditorProfileLabel")
                    Layout.fillWidth: true

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.twoCols ? 4 : 2
                        rowSpacing: 8; columnSpacing: 8

                        Repeater {
                            model: 4
                            delegate: ProfileCard {
                                required property int index
                                name: Editor.profileLabel(index)
                                swatches: Editor.profileSwatches(index)
                                sel: Editor.profile === index
                                onChosen: Editor.profile = index
                            }
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.twoCols ? 2 : 1
                    columnSpacing: 38
                    rowSpacing: 22

                    ColumnLayout {
                        Layout.preferredWidth: root.colW
                        Layout.alignment: Qt.AlignTop
                        spacing: 22

                        SettingsGroup {
                            key: "design.editor.surface"
                            title: App.uiText(App.language, "SettingsEditorSurfaceGroup")
                            Layout.fillWidth: true
                            contentSpacing: 0
                            EdColorRow { colorKey: "background" }
                            EdColorRow { colorKey: "text" }
                        }
                        SettingsGroup {
                            key: "design.editor.gutter"
                            title: App.uiText(App.language, "SettingsEditorGutterGroup")
                            Layout.fillWidth: true
                            contentSpacing: 0
                            EdColorRow { colorKey: "gutterBackground" }
                            EdColorRow { colorKey: "gutterText" }
                            EdColorRow { colorKey: "gutterTextActive" }
                        }
                        SettingsGroup {
                            key: "design.editor.markdown"
                            title: App.uiText(App.language, "SettingsEditorMarkdownGroup")
                            Layout.fillWidth: true
                            contentSpacing: 0
                            EdColorRow { colorKey: "heading" }
                            EdColorRow { colorKey: "emphasis" }
                            EdColorRow { colorKey: "link" }
                            EdColorRow { colorKey: "code" }
                        }
                    }

                    ColumnLayout {
                        Layout.preferredWidth: root.colW
                        Layout.alignment: Qt.AlignTop
                        spacing: 22

                        SettingsGroup {
                            key: "design.editor.highlight"
                            title: App.uiText(App.language, "SettingsEditorHighlightGroup")
                            Layout.fillWidth: true
                            contentSpacing: 0
                            EdColorRow { colorKey: "currentLine" }
                            EdColorRow { colorKey: "selection" }
                        }
                        SettingsGroup {
                            key: "design.editor.syntax"
                            title: App.uiText(App.language, "SettingsEditorSyntaxGroup")
                            Layout.fillWidth: true
                            contentSpacing: 0
                            EdColorRow { colorKey: "keyword" }
                            EdColorRow { colorKey: "type" }
                            EdColorRow { colorKey: "string" }
                            EdColorRow { colorKey: "number" }
                            EdColorRow { colorKey: "comment" }
                            EdColorRow { colorKey: "preproc" }
                            EdColorRow { colorKey: "function" }
                            EdColorRow { colorKey: "operator" }
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
                    Button {
                        text: App.uiText(App.language, "SettingsDesignExportBtn")
                        onClicked: edExportDialog.open()
                    }
                    Button {
                        text: App.uiText(App.language, "SettingsDesignImportBtn")
                        onClicked: edImportDialog.open()
                    }
                }

                SettingsGroup {
                    key: "design.editor.preview"
                    title: App.uiText(App.language, "SettingsEditorPreviewGroup")
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8

                    //  Wie ein kleines Editorfenster: Randspalte, aktuelle Zeile (2) und Text je Zeile auf einer Höhe.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: vorschau.implicitHeight + 20
                        radius: 8
                        color: Editor.background
                        border.color: App.themeBorder

                        Rectangle {
                            x: 1; y: 1
                            width: 40; height: parent.height - 2
                            topLeftRadius: 7; bottomLeftRadius: 7
                            color: Editor.gutterBackground
                        }

                        Column {
                            id: vorschau
                            x: 1; y: 10
                            width: parent.width - 2
                            Repeater {
                                model: root.previewLines()
                                delegate: Item {
                                    id: zeile
                                    required property int index
                                    required property string modelData
                                    width: vorschau.width
                                    height: 21
                                    Rectangle {
                                        x: 40; width: parent.width - 40; height: parent.height
                                        visible: zeile.index === 1
                                        color: root.edColor("currentLine")
                                    }
                                    Text {
                                        anchors { right: parent.left; rightMargin: -32; verticalCenter: parent.verticalCenter }
                                        text: zeile.index + 1
                                        color: zeile.index === 1 ? Editor.gutterTextActive : Editor.gutterText
                                        font: App.fallbackFont("monospace", 12)
                                    }
                                    Text {
                                        x: 52
                                        anchors.verticalCenter: parent.verticalCenter
                                        textFormat: Text.StyledText
                                        text: zeile.modelData
                                        color: root.edColor("text")
                                        font: App.fallbackFont("monospace", 12)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

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
        onAccepted: {
            if (App.importCustomTheme(selectedFile)) {
                themeImportHint.text = ""
                root.loadTheme()
            } else {
                themeImportHint.text = App.uiText(App.language, "SettingsDesignImportInvalid")
                themeImportHintTimer.restart()
            }
        }
    }

    SmoothWheelArea { flickable: designScroll.contentItem; backdrop: App.themeBackground }
}
