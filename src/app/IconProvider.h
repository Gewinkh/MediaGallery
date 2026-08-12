#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  IconProvider — liefert die Bedien-Symbole (`qml/icons/*.svg`) FERTIG
//  EINGEFÄRBT an QML: `image://mgicon/<qrc-pfad>?c=%23rrggbb`.
//
//  WARUM ÜBERHAUPT: Regel 29 verlangt SVGs statt Glyphen, und die Zeichnung muss
//  der Theme-Textfarbe folgen — `currentColor` kennt Qts SVG-Renderer aber nicht.
//  Der bisherige Weg (unsichtbares `Image` + `MultiEffect` mit `colorization`)
//  ließ sich in KEINEM Prüflauf belegen: gemessen zeichnete `MultiEffect` weder
//  offscreen noch in einer echten Grafiksitzung ein einziges Pixel, während
//  dieselbe SVG als schlichtes `Image` sauber ankam (16 080 Pixel Tinte). Ein
//  Effekt, dessen Wirkung sich nicht nachweisen lässt, taugt nicht als Träger
//  der ganzen Symbolleiste — hier wird deshalb in C++ eingefärbt (Regel 7).
//
//  VERFAHREN: `QSvgRenderer` zeichnet die (schwarze) Datei in ein transparentes
//  `QImage`, danach färbt ein `CompositionMode_SourceIn`-Durchgang die Deckung
//  in die Zielfarbe um. Kein Shader, keine Grafik-Pipeline — funktioniert damit
//  auch unter Software-Rendering und in Testtreibern ohne Fenster.
//
//  RAM: kleiner LRU-Cache über (Pfad, Farbe, Größe); Symbole sind wenige KB, der
//  Deckel `kMaxCached` hält das zuverlässig klein.
//
//  Registrierung: `engine.addImageProvider("mgicon", new IconProvider)` in
//  main.cpp; Nutzung in QML ausschließlich über `qml/common/ThemedIcon.qml`.
// ─────────────────────────────────────────────────────────────────────────────

#include <QQuickImageProvider>
#include <QHash>
#include <QList>
#include <QImage>
#include <QMutex>

class IconProvider : public QQuickImageProvider {
public:
    IconProvider();

    //  id = "<qrc-pfad ohne führenden Doppelpunkt>?c=%23rrggbb", z. B.
    //  "qml/icons/pen.svg?c=%23e6e6e6". Ohne `c` bleibt die Datei, wie sie ist.
    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

private:
    static constexpr int kMaxCached  = 96;   // Symbole × Farben × Größen
    static constexpr int kDefaultPx  = 24;

    QMutex              m_lock;              // QML darf aus mehreren Threads fragen
    QHash<QString, QImage> m_cache;
    QList<QString>      m_lru;               // vorne = zuletzt benutzt
};
