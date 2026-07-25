import QtQuick
import QtQuick.Layouts
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  SettingsGroup.qml — einheitlicher Titel-Container (ersetzt QGroupBox-Stil).
//  Kinder werden in die innere ColumnLayout aufgenommen (default property alias).
//  implicitHeight/Width leiten sich aus dem Inhalt ab → passt in ColumnLayouts.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: group
    property string title: ""
    property int contentSpacing: 10
    default property alias content: inner.data

    readonly property int padH:      14
    readonly property int padTop:    title.length > 0 ? 28 : 14
    readonly property int padBottom: 14

    color: Qt.rgba(1, 1, 1, 0.02)
    border.color: App.themeBorder
    radius: 8

    implicitWidth:  inner.implicitWidth + padH * 2
    implicitHeight: inner.implicitHeight + padTop + padBottom

    Text {
        visible: group.title.length > 0
        text: group.title
        color: App.themeTextMuted
        font.pixelSize: 11
        font.bold: true
        x: 12; y: 9
    }

    ColumnLayout {
        id: inner
        x: group.padH
        y: group.padTop
        width: group.width - group.padH * 2
        spacing: group.contentSpacing
    }
}
