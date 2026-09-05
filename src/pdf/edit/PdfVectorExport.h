#pragma once
// Verlustfreier Export: die Overlay-Anmerkungen werden als echte VEKTOR-Inhalte in die Content-Streams
// geschrieben. Der frühere Weg (QPdfWriter + QPainter) rasterte ausnahmslos jede Seite, auch unberührte - dabei
// gingen Textebene, eingebettete Schriften und Vektorgrafik verloren. Umgerechnet wird nur in `toPdfY`.

#include <QString>
#include <QVector>

#include "pdf/edit/PdfEditTypes.h"

namespace mg {

class PdfVectorExport {
public:
    // `boxes` in PDF-Punkten mit Ursprung oben-links der ANGEZEIGTEN Seite; der Seiten-Plan steckt bereits in
    // `inputPath`. Gedrehte Seiten (auch geerbtes /Rotate) werden berücksichtigt; false lässt `outputPath` ungeschrieben.
    static bool exportAnnotations(const QString& inputPath, const QString& outputPath,
                                  const QVector<PdfEditBox>& boxes,
                                  QString* err = nullptr);
};

} // namespace mg
