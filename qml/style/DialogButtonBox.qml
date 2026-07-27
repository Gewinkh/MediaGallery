import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  DialogButtonBox.qml — Schaltflächenleiste der Standard-Dialoge (Stil
//  "style"). `Button` löst hier auf die Datei desselben Moduls auf,
//  die Ok/Abbrechen/Ja/Nein-Knöpfe bekommen also automatisch die gethemte Optik.
// ─────────────────────────────────────────────────────────────────────────────
T.DialogButtonBox {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    spacing: 8
    padding: 12
    topPadding: 4
    alignment: Qt.AlignRight

    delegate: Button { }

    contentItem: ListView {
        implicitWidth: contentWidth
        model: control.contentModel
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        snapMode: ListView.SnapToItem
    }

    background: Item { }
}
