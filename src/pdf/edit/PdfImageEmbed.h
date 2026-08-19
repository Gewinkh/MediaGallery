#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfImageEmbed.h - ein Bild als PDF-XObject vorbereiten
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Signatur- und Stempelbilder (README ▸ Planned ▸ PDF-Editor) müssen als
//  Bild-XObject IN die PDF. Diese Einheit macht aus einem `QImage` die beiden
//  Datenströme, die dafür nötig sind - die Objekte selbst schreibt der
//  Aufrufer, weil nur er seine Objektnummern kennt (`PdfVectorExport`,
//  `PdfAnnotations`).
//
//  WARUM ALS EIGENE EINHEIT: Dieselbe Aufbereitung brauchen der gezeichnete
//  Export und die Annotations-Ausgabe. Zwei Kopien wären die schlechtere Lösung
//  - dieselbe Begründung wie bei `PdfObjects`/`PdfFontEmbed`.
//
//  VERFAHREN
//  ─────────
//  Das Bild wird auf `/DeviceRGB` mit 8 Bit je Kanal gebracht und mit zlib
//  gepackt (`/FlateDecode`) - ohne Verlust und ohne Fremdbibliothek. Hat es
//  Transparenz, entsteht zusätzlich ein 8-Bit-Graustufenstrom als **`/SMask`**;
//  ohne ihn zeichnete PDF den transparenten Rand einer Signatur als weißen
//  Kasten. JPEG-Wiederverwendung (`/DCTDecode`) wäre kleiner, verlangt aber
//  Vertrauen in fremde Dateibytes - hier zählt Vorhersagbarkeit mehr.
//
//  RAM/GRÖSSE (§0-Priorität 4): Die längere Kante wird auf `maxEdgePx`
//  begrenzt. Eine Handy-Fotografie einer Unterschrift hat sonst 4000 px, von
//  denen im Dokument nichts ankommt außer Dateigröße.
//
//  ABHÄNGIGKEITEN: Qt6::Gui (QImage) + ZLIB. Kein Q_OBJECT/moc; isoliert
//  testbar.
// ══════════════════════════════════════════════════════════════════════════════

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
