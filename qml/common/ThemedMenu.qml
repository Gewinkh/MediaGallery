import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// Eigener Hintergrund, weil Fusions weder Themenfarbe noch Rundung folgt; ohne `implicitWidth` kollabiert er zum
// 2-px-Strich (contentWidth liefert hier 0). Der Rand von 6 px haelt die Markierung von den runden Ecken fern.
Menu {
    id: control

    padding: 6

    // Einmal messen statt Bindung ueber alle Eintraege: die lief bei jedem eingefuegten Eintrag erneut ueber alle,
    // 403 Tags kosteten so 1,4 s. Echte Zeilenhoehen statt der beim Rollen nachgeschaetzten contentHeight.
    property real _maxW: 0
    property real _sumH: 0
    function _measure() {
        var w = 0, h = 0
        for (var i = 0; i < count; i++) {
            var it = itemAt(i)
            if (!it || !it.visible) continue
            if (it.implicitWidth > w) w = it.implicitWidth
            h += it.height
        }
        _maxW = w
        _sumH = h
    }
    onCountChanged: Qt.callLater(_measure)
    onAboutToShow: _measure()
    Component.onCompleted: _measure()

    implicitWidth: Math.max(_maxW, 160) + leftPadding + rightPadding
    implicitHeight: _sumH + topPadding + bottomPadding

    background: PopupPanel { }
}
