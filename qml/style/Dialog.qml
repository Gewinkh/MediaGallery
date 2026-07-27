import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  Dialog.qml — gethemter Dialograhmen (Stil "style").
//
//  Fusion zeichnete die Titelzeile mit eigenem Hintergrund in `palette.window`
//  und Radius 2 — in den App-Dialogen (Karten-Hintergrund, Radius 8) ergab das
//  ein andersfarbiges Band mit eckigen Ecken über der abgerundeten Kante. Hier
//  ist die Titelzeile transparent (der Dialoghintergrund trägt sie) und der
//  Fuß-Bereich existiert nur, wenn er wirklich Schaltflächen enthält.
//  Aufrufstellen, die `background`/`contentItem` selbst setzen, überschreiben
//  die Vorgaben wie gehabt.
// ─────────────────────────────────────────────────────────────────────────────
T.Dialog {
    id: control

    implicitWidth:  Math.max(implicitBackgroundWidth + leftInset + rightInset,
                             implicitContentWidth + leftPadding + rightPadding,
                             implicitHeaderWidth,
                             implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))

    padding: 12

    header: Text {
        text: control.title
        visible: control.title.length > 0
        color: App.themeTextPrimary
        font.pixelSize: 13
        font.bold: true
        elide: Text.ElideRight
        leftPadding: 12
        rightPadding: 12
        topPadding: 10
        bottomPadding: 4
    }

    //  Nur anlegen, wenn der Dialog Standard-Schaltflächen nutzt — sonst bliebe
    //  ein leerer, ungenutzter Streifen am Fuß des Dialogs stehen.
    footer: DialogButtonBox {
        visible: count > 0
    }

    background: Rectangle {
        color: App.themeCard
        border.color: App.themeBorder
        border.width: 1
        radius: 8
    }

    T.Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.55)
    }
    T.Overlay.modeless: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.30)
    }
}
