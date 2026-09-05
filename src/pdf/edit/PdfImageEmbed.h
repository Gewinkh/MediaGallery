#pragma once
// Macht aus einem QImage die Datenströme eines Bild-XObjects; die Objekte schreibt der Aufrufer, nur
// er kennt seine Objektnummern. /FlateDecode statt /DCTDecode-Wiederverwendung: Vorhersagbarkeit vor
// Größe. Ohne /SMask stünde der transparente Rand einer Signatur als weißer Kasten.

#include <QByteArray>
#include <QString>

class QImage;

namespace mg {

//  Die fertig gepackten Ströme eines Bildes.
struct PdfImageData {
    int        width  = 0;
    int        height = 0;
    QByteArray rgb;        // /DeviceRGB, 8 bpc, FlateDecode
    QByteArray alpha;      // /DeviceGray, 8 bpc, FlateDecode (leer = deckend)

    bool hasAlpha() const { return !alpha.isEmpty(); }
    bool isValid()  const { return width > 0 && height > 0 && !rgb.isEmpty(); }
};

class PdfImageEmbed {
public:
    //  Bereitet `img` auf. Liefert false bei leerem/unbrauchbarem Bild.
    static bool encode(const QImage& img, PdfImageData* out,
                       int maxEdgePx = 1600, QString* err = nullptr);
    //  Wie oben, lädt das Bild zuvor von `path` (nicht lesbar -> false).
    static bool encodeFile(const QString& path, PdfImageData* out,
                           int maxEdgePx = 1600, QString* err = nullptr);

    //  Das Dict des Bild-XObjects (ohne `/Length`, das setzt der Schreiber).
    //  `smaskObj` ≥ 0 hängt die Transparenzmaske an.
    static QByteArray imageDict(const PdfImageData& d, int smaskObj = -1);
    //  Das Dict der Transparenzmaske.
    static QByteArray smaskDict(const PdfImageData& d);
};

} // namespace mg
