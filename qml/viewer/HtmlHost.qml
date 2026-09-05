import QtQuick
import MediaGallery 1.0

// Zwischenschicht vor `HtmlSurface`: die wird per URL erst zur LAUFZEIT übersetzt, der QtWebEngine-Import also
// nie angefasst, solange WebEngine nicht bereit ist. Der innere Loader ist hart auf `WebEngine.ready` gegated.
Item {
    id: htmlHost
    property string source: ""
    property real   topInset: 0
    property real   bottomInset: 0
    function release() {
        if (htmlInner.item && htmlInner.item.release)
            htmlInner.item.release()
    }
    Loader {
        id: htmlInner
        anchors.fill: parent
        //  Harte Garantie: ohne Ready wird HtmlSurface (und damit die
        //  WebEngineView) nie erzeugt.
        source: WebEngine.ready ? "qrc:/qml/viewer/HtmlSurface.qml" : ""
    }
    //  Properties an die geladene HtmlSurface durchreichen (reaktiv).
    Binding { target: htmlInner.item; property: "source";      value: htmlHost.source
              when: htmlInner.item !== null; restoreMode: Binding.RestoreNone }
    Binding { target: htmlInner.item; property: "topInset";    value: htmlHost.topInset
              when: htmlInner.item !== null; restoreMode: Binding.RestoreNone }
    Binding { target: htmlInner.item; property: "bottomInset"; value: htmlHost.bottomInset
              when: htmlInner.item !== null; restoreMode: Binding.RestoreNone }
}
