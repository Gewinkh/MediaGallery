#pragma once
// image://audiocover/<pfad>?rev=<n>. Ein Anbieter statt eines Zwischenspeichers, weil
// er im Bild-Faden laeuft und das Bild auf die angefragte Groesse verkleinert, bevor
// es an die Szene geht - ein Titelbild ist gern 1500x1500 (~9 MB als QImage).

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
