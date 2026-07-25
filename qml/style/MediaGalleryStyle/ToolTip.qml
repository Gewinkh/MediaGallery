import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  ToolTip.qml — gethemte Kurzinfo (Stil "MediaGalleryStyle").
//
//  Dieselbe Karten-/Rahmenoptik wie Menüs und Popups der App, damit die vielen
//  ToolTips der Editor-Leisten nicht als systemgelbe Fremdkörper erscheinen.
// ─────────────────────────────────────────────────────────────────────────────
T.ToolTip {
    id: control

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - 6

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: 6
    padding: 6
    leftPadding: 9
    rightPadding: 9

    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
                 | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        font: control.font
        color: App.themeTextPrimary
        wrapMode: Text.WordWrap
    }

    background: Rectangle {
        color: App.themeMenuBarBg
        border.color: App.themeBorder
        border.width: 1
        radius: 6
    }
}
