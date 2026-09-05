#pragma once
// Zeichenweises Einfügen/Löschen in der eingebetteten Textebene - anders als
// PdfContentEditor::editText zählt hier die POSITION, genau das, was ein Caret braucht. Geändert
// werden die Rohbytes genau EINES Zeigeoperators; TJ-Kerning dieses Operanden geht dabei verloren.

#include <QByteArray>
#include <QString>

#include "pdf/edit/PdfEncodings.h"

namespace mg {

class PdfTextEditor {
public:
    // Kodierung EINER Seiten-Schrift auflösen: /Resources -> /Font -> Font-Objekt -> /Encoding bzw. /ToUnicode.
    // Öffentlich, weil `PdfTextReflow` dieselbe Auflösung braucht - eine zweite Kopie wäre eine Fehlerquelle.
    static bool encodingForPageFont(const QByteArray& pdfBytes, int pageIndex,
                                    const QByteArray& fontRes,
                                    pdfenc::Encoding* out);

    //  Fügt `text` VOR dem Zeichen mit dem Index `glyphIndex` ein
    //  (`glyphIndex == Anzahl` -> ans Ende der letzten Zeige-Anweisung).
    static bool insertText(const QString& inputPath, const QString& outputPath,
                           int pageIndex, int glyphIndex, const QString& text,
                           QString* err = nullptr);

    static bool deleteText(const QString& inputPath, const QString& outputPath,
                           int pageIndex, int glyphIndex, int count,
                           QString* err = nullptr);
};

} // namespace mg
