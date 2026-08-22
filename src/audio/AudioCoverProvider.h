#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  AudioCoverProvider - liefert das im Titel eingebettete Bild an QML.
//
//      image://audiocover/<prozentkodierter Pfad>?rev=<n>
//
//  WARUM EIN ANBIETER UND KEIN ZWISCHENSPEICHER IM CONTROLLER:
//   • Er läuft im BILD-Faden von Qt Quick (`ForceAsynchronousImageLoading`) -
//     das Lesen der Datei und das Dekodieren des JPEGs kosten damit nichts im
//     GUI-Faden (Regel 16). Der Controller sagt nur, DASS es ein Bild gibt.
//   • Das Bild wird auf die ANGEFRAGTE Größe verkleinert, bevor es an die Szene
//     geht: ein Titelbild ist gern 1500×1500 (~9 MB als `QImage`), die Fläche im
//     Player ist ein Bruchteil davon (§0-Priorität 1).
//
//  `?rev=` ist nur ein Cache-Brecher: derselbe Pfad mit anderer Nummer zwingt
//  Qt, neu zu laden (Muster wie `image://pdfthumb/...?r=`).
//
//  Kein eigener Zustand, kein Schloss: `requestImage` liest die Datei bei jedem
//  Ruf frisch. Qt Quick hält das Ergebnis in seinem eigenen Bild-Cache.
// ─────────────────────────────────────────────────────────────────────────────

#include <QQuickImageProvider>

class AudioCoverProvider : public QQuickImageProvider {
public:
    AudioCoverProvider();

    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

    //  Die QML-Quelle für einen Pfad - EINE Stelle, an der die Adresse
    //  zusammengesetzt wird (der Controller benutzt sie).
    static QString sourceFor(const QString& path, int rev);
};
