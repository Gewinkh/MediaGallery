#pragma once
// Wo steht welches Zeichen: Ursprung, Vorschubbreite, Höhe und Größe je Glyphe in PDF-Punkten,
// Ursprung oben-links - Grundlage für hitTest und caretRect. Breiten kommen ausschliesslich aus dem
// Dokument; fehlen sie, wird ABGELEHNT statt geschätzt: eine falsche Breite verschiebt jedes Folgezeichen.

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace mg {

struct PdfGlyph {
    QChar   ch;             // dekodiertes Zeichen (U+FFFD, wenn nicht abbildbar)
    QRectF  box;            // Ursprung oben-links, PDF-Punkte (Vorschub × Zeilenhöhe)
    qreal   fontSizePt = 0; // wirksame Schriftgröße (inkl. Matrix-Skalierung)
    int     showIndex  = 0; // Nummer des Zeigeoperators, in dem das Zeichen steht
    int     byteOffset = 0; // Byte-Position in dessen ZUSAMMENGEFÜGTEN Bytes
};

// Ein Zeigeoperator mit seiner Lage IM ENTPACKTEN Content-Stream. Damit lässt sich ein Zeichen nicht nur
// anzeigen, sondern gezielt ersetzen: `bytesStart/bytesEnd` umschließen den Operanden, `enc` sagt, wie zu kodieren ist.
struct PdfShowSpan {
    qint64     operandStart = 0;   // '(' bzw. '[' des Operanden
    qint64     operandEnd   = 0;   // hinter dem schliessenden Zeichen
    bool       isArray      = false;
    QByteArray bytes;              // entschlüsselte Rohbytes (TJ: zusammengefügt)
    QByteArray fontRes;            // Ressourcenname der aktiven Schrift
    // Ohne die Positionierung lässt sich Text nur ÄNDERN, nicht VERSCHIEBEN - und genau das braucht ein Absatz, der
    // um eine Zeile wächst. `posStart/posEnd` umschließen die vollständige Anweisung und sind direkt ersetzbar.
    qint64         posStart = -1;
    qint64         posEnd   = -1;
    QByteArray     posOp;          // "Tm" | "Td" | "TD" | "T*" | leer
    QVector<qreal> posArgs;        // Operanden in Schreibreihenfolge
    //  Laufende Nummer des Textobjekts (`BT`): Ein neues Textobjekt setzt die
    //  Textmatrix zurück - eine relative Verschiebung wirkt nicht darüber
    //  hinaus.
    int            objIndex = 0;
    // Seiten-Punkte, um die ein TJ-Versatz von -1000 hier nach rechts schiebt. Wer Zeichen herausschneidet, muss die
    // Lücke damit ausgleichen, sonst rutscht der Rest der Zeile nach links. Nur hier ist `Tz` bekannt.
    qreal          tjUnitPt = 0;
};

struct PdfPageText {
    QVector<PdfGlyph>    glyphs;
    QVector<PdfShowSpan> spans;
    QByteArray           content;      // entpackter Content-Stream
    int                  contentObj = -1;   // Objektnummer (−1 = mehrteilig)
    qreal                pageHeightPt = 0;  // /MediaBox-Höhe (Umrechnung oben/unten)
    // Flächen, die NICHT Text sind (Pfade, XObjects, Inline-Bilder), in denselben Koordinaten wie `PdfGlyph::box`.
    // Wer Text verschieben will, muss wissen, was stehen bliebe: Grafik wandert nicht mit.
    QVector<QRectF>      paints;
    // Nur die BILDartigen davon. Wer schwärzt, muss sie kennen: Text lässt sich aus dem Strom entfernen,
    // Bildpunkte NICHT - unter einem Balken über einem Bild bliebe das Original erhalten.
    QVector<QRectF>      imagePaints;
};

class PdfTextLayout {
public:
    //  Baut das Layout EINER Seite (0-basiert). Liefert false, wenn die Seite
    //  nicht sicher auswertbar ist (fehlende Breiten, fremder Stream-Filter,
    //  verschlüsselt …); `err` erhält dann einen kurzen Grund.
    static bool buildForPage(const QString& pdfPath, int pageIndex,
                             QVector<PdfGlyph>* out, QString* err = nullptr);

    //  Wie oben, liefert zusätzlich die Zeigeoperator-Bereiche und den
    //  entpackten Stream - die Grundlage fürs zeichenweise Bearbeiten.
    static bool buildForPage(const QString& pdfPath, int pageIndex,
                             PdfPageText* out, QString* err = nullptr);

    static int hitTest(const QVector<PdfGlyph>& glyphs, const QPointF& ptTopLeft);

    static QRectF caretRect(const QVector<PdfGlyph>& glyphs, int index);
};

} // namespace mg
