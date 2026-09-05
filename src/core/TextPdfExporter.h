#pragma once
// Klartext als paginiertes A4-PDF; eigenständig statt am DOCX-Weg gebaut - gemeinsam wären nur Seiteneinrichtung
// und Zielnamensfindung. Monospace 10 pt, weicher Umbruch ohne Einzug, Fußzeile nur mit Zählung.
// Der Text kommt als Parameter, nicht von Platte: ein Neu-Einlesen druckte den alten Stand.

#include <QColor>
#include <QString>

namespace TextPdf {

// Liefert false + `*err` bei Fehler; die Zieldatei wird dann nicht angelegt (QSaveFile-Rollback). `tabWidth` in
// ZEICHEN aus der Editor-Einstellung - eine feste 8 verdoppelte die Einrückung einer mit vier eingerückten Datei.
bool exportToPdf(const QString& text, const QString& targetPath,
                 const QColor& textColor = QColor(Qt::black),
                 int tabWidth = 4,
                 QString* err = nullptr);

//  Freier Zielpfad NEBEN der Quelle: <Name>.pdf, bei Kollision <Name> (2).pdf …
//  (gleiche Namensregel wie DocxEditController::pdfExportTargetPath).
QString targetPathFor(const QString& sourcePath);

} // namespace TextPdf
