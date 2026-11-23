import QtQuick
import QtQuick.Templates as T
import MediaGallery 1.0

// Eingerueckte Trennlinie wie in den Entwuerfen; die Einrueckung kommt aus dem Menue-Rand plus `horizontalPadding`.
T.MenuSeparator {
    id: control
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    // Versteckt ohne Hoehe: sonst bleiben 11 px Luft im Menue stehen.
    implicitHeight: visible ? implicitContentHeight + topPadding + bottomPadding : 0
    horizontalPadding: 6
    verticalPadding: 5
    contentItem: Rectangle {
        implicitWidth: 140
        implicitHeight: 1
        color: App.themeBorder
    }
}
