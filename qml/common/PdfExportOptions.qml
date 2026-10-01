import QtQuick
import MediaGallery 1.0

// PdfExportOptions - Stil und Ausrichtung eines PDF-Exports; beides gilt appweit und wird gemerkt.
Column {
    id: root

    property real tipWidth: 280
    spacing: 8

    ChoiceChips {
        labels: [App.uiText(App.language, "PdfStylePrint"), App.uiText(App.language, "PdfStyleOriginal")]
        current: App.textPdfNative ? 1 : 0
        onPicked: function (i) { App.textPdfNative = i === 1 }
    }
    Text {
        width: root.tipWidth
        wrapMode: Text.WordWrap
        text: App.uiText(App.language, "PdfStyleTip")
        color: App.themeTextMuted
        font.pixelSize: 10
    }
    ChoiceChips {
        labels: [App.uiText(App.language, "PdfPortrait"), App.uiText(App.language, "PdfLandscape")]
        current: App.pdfLandscape ? 1 : 0
        onPicked: function (i) { App.pdfLandscape = i === 1 }
    }
}
