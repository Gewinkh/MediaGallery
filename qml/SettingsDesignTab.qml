pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import MediaGallery 1.0

// ── Design: Profile + Custom-Theme-Editor ────────────────────────────────────
Item {
    id: root

    readonly property int customIndex: 8     // DesignProfile::Custom
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

    // ── Wiederverwendbare Farbzeile ──────────────────────────────────────────
    component ColorRow: RowLayout {
        id: cr
        property string label: ""
        property color value: "#000000"
        signal picked(color c)
        spacing: 8
        Layout.fillWidth: true
        Label { text: cr.label; color: App.themeTextPrimary; Layout.preferredWidth: 160 }
        ColorPicker {
            width: 36; height: 22; showAlpha: false
            title: cr.label
            selectedColor: cr.value
            onColorPicked: (c) => cr.picked(c)
        }
        Item { Layout.fillWidth: true }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 14

            // ── Profil-Auswahl ────────────────────────────────────────────────
            SettingsGroup {
                title: qsTr("Design-Profil")
                Layout.fillWidth: true

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
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
                    text: qsTr("Wähle das Profil „Custom“, um eigene Farben zu bearbeiten.")
                    color: App.themeTextMuted; font.pixelSize: 11
                }

                SettingsGroup {
                    title: qsTr("Grundfarben")
                    Layout.fillWidth: true
                    ColorRow { label: qsTr("Hintergrund");      value: root.twBackground;  onPicked: (c) => { root.twBackground = c;  root.applyLive() } }
                    ColorRow { label: qsTr("Karte / Kachel");   value: root.twCard;        onPicked: (c) => { root.twCard = c;        root.applyLive() } }
                    ColorRow { label: qsTr("Text primär");      value: root.twTextPrimary; onPicked: (c) => { root.twTextPrimary = c; root.applyLive() } }
                    ColorRow { label: qsTr("Text gedämpft");    value: root.twTextMuted;   onPicked: (c) => { root.twTextMuted = c;   root.applyLive() } }
                    ColorRow { label: qsTr("Rahmen");           value: root.twBorder;      onPicked: (c) => { root.twBorder = c;      root.applyLive() } }
                }

                SettingsGroup {
                    title: qsTr("Akzent")
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Label { text: qsTr("Akzent-Typ"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                        ComboBox {
                            Layout.preferredWidth: 200
                            model: [qsTr("Einfarbig"), qsTr("Verlauf"), qsTr("Glow")]
                            currentIndex: root.twAccentType
                            onActivated: { root.twAccentType = currentIndex; root.applyLive() }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    ColorRow { label: qsTr("Akzentfarbe"); value: root.twAccent; onPicked: (c) => { root.twAccent = c; root.applyLive() } }
                    ColorRow {
                        label: qsTr("Akzent Verlauf-Ende"); value: root.twAccentGradEnd
                        visible: root.twAccentType !== 0
                        onPicked: (c) => { root.twAccentGradEnd = c; root.applyLive() }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        visible: root.twAccentType === 2
                        Label { text: qsTr("Glow-Radius"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
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
                        Label { text: qsTr("Glow-Intensität"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                        Slider {
                            Layout.fillWidth: true; from: 0; to: 1
                            value: root.twGlowIntensity
                            onMoved: { root.twGlowIntensity = value; root.applyLive() }
                        }
                        Label { text: root.twGlowIntensity.toFixed(2); color: App.themeTextMuted }
                    }
                }

                SettingsGroup {
                    title: qsTr("Hintergrund-Verlauf")
                    Layout.fillWidth: true
                    CheckBox {
                        text: qsTr("Verlauf statt Einfarbig")
                        checked: root.twBgIsGradient
                        onToggled: { root.twBgIsGradient = checked; root.applyLive() }
                        contentItem: Text {
                            text: parent.text; color: App.themeTextPrimary
                            leftPadding: parent.indicator.width + 6; verticalAlignment: Text.AlignVCenter
                        }
                    }
                    ColorRow { label: qsTr("Verlauf Start"); value: root.twBgGradStart; visible: root.twBgIsGradient; onPicked: (c) => { root.twBgGradStart = c; root.applyLive() } }
                    ColorRow { label: qsTr("Verlauf Ende");  value: root.twBgGradEnd;   visible: root.twBgIsGradient; onPicked: (c) => { root.twBgGradEnd = c;   root.applyLive() } }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        visible: root.twBgIsGradient
                        Label { text: qsTr("Winkel"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                        SpinBox {
                            from: 0; to: 360; stepSize: 5
                            value: root.twBgGradAngle
                            onValueModified: { root.twBgGradAngle = value; root.applyLive() }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                SettingsGroup {
                    title: qsTr("Kachel-Hintergrund")
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Label { text: qsTr("Typ"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                        ComboBox {
                            Layout.preferredWidth: 200
                            model: [qsTr("Einfarbig"), qsTr("Verlauf"), qsTr("Transparent")]
                            currentIndex: root.twTileBgType
                            onActivated: { root.twTileBgType = currentIndex; root.applyLive() }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    ColorRow { label: qsTr("Kachel-Farbe");  value: root.twTileBgColor;  visible: root.twTileBgType !== 2; onPicked: (c) => { root.twTileBgColor = c;  root.applyLive() } }
                    ColorRow { label: qsTr("Kachel Verlauf-Ende"); value: root.twTileBgGradEnd; visible: root.twTileBgType === 1; onPicked: (c) => { root.twTileBgGradEnd = c; root.applyLive() } }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        visible: root.twTileBgType === 1
                        Label { text: qsTr("Verlauf-Winkel"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                        SpinBox {
                            from: 0; to: 360; stepSize: 5
                            value: root.twTileBgGradAngle
                            onValueModified: { root.twTileBgGradAngle = value; root.applyLive() }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    CheckBox {
                        text: qsTr("Glow beim Überfahren")
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
                        Label { text: qsTr("Glow-Radius"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
                        Slider {
                            Layout.fillWidth: true; from: 0; to: 40
                            value: root.twTileGlowRadius
                            onMoved: { root.twTileGlowRadius = value; root.applyLive() }
                        }
                        Label { text: Math.round(root.twTileGlowRadius); color: App.themeTextMuted }
                    }
                }

                SettingsGroup {
                    title: qsTr("Leisten")
                    Layout.fillWidth: true
                    ColorRow { label: qsTr("Menüleiste");   value: root.twMenuBarBg;   onPicked: (c) => { root.twMenuBarBg = c;   root.applyLive() } }
                    ColorRow { label: qsTr("Werkzeugleiste"); value: root.twToolbarBg; onPicked: (c) => { root.twToolbarBg = c;   root.applyLive() } }
                    ColorRow { label: qsTr("Filterleiste");  value: root.twFilterBarBg; onPicked: (c) => { root.twFilterBarBg = c; root.applyLive() } }
                    ColorRow { label: qsTr("Statusleiste");  value: root.twStatusBarBg; onPicked: (c) => { root.twStatusBarBg = c; root.applyLive() } }
                    ColorRow { label: qsTr("Seitenleiste");  value: root.twSidebarBg;   onPicked: (c) => { root.twSidebarBg = c;   root.applyLive() } }
                }

                SettingsGroup {
                    title: qsTr("PDF-Betrachter")
                    Layout.fillWidth: true
                    ColorRow { label: qsTr("Betrachter-Hintergrund"); value: root.twPdfViewerBg;    onPicked: (c) => { root.twPdfViewerBg = c;    root.applyLive() } }
                    ColorRow { label: qsTr("Thumbnail-Hintergrund");  value: root.twPdfThumbBg;     onPicked: (c) => { root.twPdfThumbBg = c;     root.applyLive() } }
                    ColorRow { label: qsTr("Seitenleiste");           value: root.twPdfSidebarBg;   onPicked: (c) => { root.twPdfSidebarBg = c;   root.applyLive() } }
                    ColorRow { label: qsTr("Werkzeugleiste");         value: root.twPdfToolbarBg;   onPicked: (c) => { root.twPdfToolbarBg = c;   root.applyLive() } }
                    ColorRow { label: qsTr("Scrollleiste");           value: root.twPdfScrollbarBg; onPicked: (c) => { root.twPdfScrollbarBg = c; root.applyLive() } }
                }

                SettingsGroup {
                    title: qsTr("Theme-Name & Export")
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Label { text: qsTr("Name"); color: App.themeTextPrimary; Layout.preferredWidth: 160 }
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
                            text: qsTr("Übernehmen")
                            highlighted: true
                            onClicked: { App.setDesignProfile(root.customIndex); App.setCustomThemeFromMap(root.buildMap()) }
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: qsTr("Exportieren…")
                            onClicked: { App.setCustomThemeFromMap(root.buildMap()); exportDialog.open() }
                        }
                        Button {
                            text: qsTr("Importieren…")
                            onClicked: importDialog.open()
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Theme exportieren")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Theme-Dateien (*.json)"), qsTr("Alle Dateien (*)")]
        defaultSuffix: "json"
        onAccepted: App.exportCustomTheme(selectedFile)
    }

    FileDialog {
        id: importDialog
        title: qsTr("Theme importieren")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Theme-Dateien (*.json)"), qsTr("Alle Dateien (*)")]
        onAccepted: { if (App.importCustomTheme(selectedFile)) root.loadTheme() }
    }
}
