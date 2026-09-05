#pragma once
// Dünner, OPTIONALER Wrapper um Tesseract; ohne die Bibliothek liefern `available == false` und `recognize == {}`,
// der Aufrufer fällt auf die eingebettete Textebene zurück. Die Erkennung läuft SYNCHRON - Threading macht der Aufrufer.
#include <QImage>
#include <QRectF>
#include <QString>
#include <QList>

namespace mg {

//  Eine erkannte Textzeile: Bounding-Box in PDF-PUNKTEN (Ursprung oben-links,
//  konsistent zu QPdfSelection::bounds) + der erkannte Text der Zeile.
struct OcrLine {
    QRectF  rectPts;
    QString text;
};

// ZEILE reicht, solange die Treffer nur im Speicher liegen (Auswahl, Zeilenfang); für eine unsichtbare
// Textebene IN der Datei braucht es WÖRTER - nur so deckt sich die Auswahl im fertigen PDF mit dem Bild.
enum class OcrLevel { Line, Word };

namespace ocr {

//  true, wenn Tesseract einkompiliert UND mindestens eine Sprachdatei
//  (traineddata) verfügbar ist.
bool available();

//  Tatsächlich verwendete Sprache (erste verfügbare aus der Präferenzliste),
//  leer wenn nicht verfügbar.
QString language();

//  Erkennt `img` (in `dpi` gerendert) und liefert Zeilen bzw. Wörter in
//  PDF-Punkten. Leer bei fehlendem Tesseract, ungültigem Bild oder dpi ≤ 0.
QList<OcrLine> recognize(const QImage& img, double dpi,
                         OcrLevel level = OcrLevel::Line);

}  // namespace ocr
}  // namespace mg
