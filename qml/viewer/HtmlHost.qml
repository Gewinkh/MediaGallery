import QtQuick
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  HtmlHost.qml - Zwischenschicht vor `HtmlSurface`.
//
//  Zweck unverändert (früher eine Inline-Komponente im `FullscreenViewer`):
//  `HtmlSurface.qml` wird per URL erst zur LAUFZEIT übersetzt - der
//  QtWebEngine-Import (und damit die WebEngineView) wird also nie angefasst,
//  solange WebEngine nicht bereit ist. Der innere Loader ist hart auf
//  `WebEngine.ready` gegated.
//
//  Eigene Datei geworden, weil der `surface`-Loader seine Fläche jetzt über
//  eine URL wählt (s. `ViewerNote.qml`).
// ─────────────────────────────────────────────────────────────────────────────
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
