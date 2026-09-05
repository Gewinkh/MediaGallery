#pragma once
// Ersetzt eingebetteten Text DIREKT im Content-Stream statt ihn zu überdecken - die Seite bleibt
// vektoriell. Inkrementelles Update (append-only). Bewusst begrenzt, sonst false -> Raster-Export:
// unverschlüsselt, EIN /Contents, höchstens /FlateDecode, einfache Fonts, ASCII, Fundstelle eindeutig.

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector>

namespace mg {

//  Eine gewünschte Textersetzung auf einer Seite. `original` muss der exakte,
//  zusammenhängende eingebettete Text sein (wie ihn `PdfTextController::
//  replaceProbe` liefert); `replacement` leer = löschen.
struct PdfTextEdit {
    int     page = 0;      // 0-basiert
    QString original;
    QString replacement;
};

//  Eine zu SCHWÄRZENDE Fläche. Anders als `PdfTextEdit` braucht sie den Text
//  NICHT zu kennen: entfernt wird, was geometrisch darunter liegt.
struct PdfRedactArea {
    int    page = 0;       // 0-basiert
    QRectF rect;           // PDF-Punkte, Ursprung OBEN-LINKS (wie PdfEditBox::rect)
};

class PdfContentEditor {
public:
    // Liefert false, wenn eine Vorbedingung nicht sicher erfüllt ist ODER nichts ersetzt wurde - dann bleibt
    // `outputPath` ungeschrieben und der Aufrufer weicht auf den Raster-Export aus.
    static bool editText(const QString& inputPath, const QString& outputPath,
                         const QVector<PdfTextEdit>& edits, QString* err = nullptr);

    // Schwärzt GEOMETRISCH: entfernt jedes Zeichen, dessen Kasten eine Fläche berührt - anders als
    // `editText`, dessen Scheitern beim Wiederfinden bisher die ganze Textebene kostete. Die Lücke
    // gleicht ein TJ-Versatz aus; steht danach noch eine Glyphe drin, gilt der Lauf als gescheitert.
    static bool redactAreas(const QString& inputPath, const QString& outputPath,
                            const QVector<PdfRedactArea>& areas, QString* err = nullptr);
};

}  // namespace mg
