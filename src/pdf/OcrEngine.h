#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  OcrEngine - dünner, OPTIONALER Wrapper um Tesseract (Texterkennung für
//  gescannte PDFs). Zur Compile-Zeit über MG_HAVE_TESSERACT aktiviert
//  (CMake findet Tesseract via pkg-config). Ist Tesseract nicht vorhanden,
//  liefern available()==false und recognize()=={} - der Aufrufer fällt dann
//  auf den bestehenden Weg (eingebettete Textebene, sonst leere Box) zurück
//  (Degradationskette, §0-Priorität 3 Portabilität).
//
//  Kein Q_OBJECT/moc; reine freie Funktionen im Namespace mg::ocr. Die
//  Erkennung läuft SYNCHRON - das Threading stellt der Aufrufer sicher
//  (im Projekt der 1-Thread-Pool des PdfTextController, Regel 8/17).
// ─────────────────────────────────────────────────────────────────────────────
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

namespace ocr {

//  true, wenn Tesseract einkompiliert UND mindestens eine Sprachdatei
//  (traineddata) verfügbar ist.
bool available();

//  Tatsächlich verwendete Sprache (erste verfügbare aus der Präferenzliste),
//  leer wenn nicht verfügbar.
QString language();

//  Erkennt `img` (in `dpi` gerendert) und liefert die Textzeilen in
//  PDF-Punkten. Leer bei fehlendem Tesseract, ungültigem Bild oder dpi ≤ 0.
QList<OcrLine> recognize(const QImage& img, double dpi);

}  // namespace ocr
}  // namespace mg
