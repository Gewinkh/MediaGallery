#pragma once
// Klartext als paginiertes A4-PDF; eigenständig statt am DOCX-Weg gebaut - gemeinsam wären nur Seiteneinrichtung
// und Zielnamensfindung. Monospace 10 pt, weicher Umbruch ohne Einzug, Fußzeile nur mit Zählung.
// Der Text kommt als Parameter, nicht von Platte: ein Neu-Einlesen druckte den alten Stand.

#include "editor/SyntaxPalette.h"

#include <QColor>
#include <QString>

namespace TextPdf {

//  Zwei Betriebsarten nebeneinander: EINE Farbe auf weissem Papier (Vorgabe,
//  fuer Ausdrucke) oder die Farben des Editor-Profils. Im zweiten Fall gehoert
//  die FLAECHE dazu und ist nicht abwaehlbar - die Zeichenfarben eines dunklen
//  Profils auf weissem Papier waeren stellenweise unlesbar.
struct Stil {
    QColor tinte = QColor(Qt::black);   // alles Ungefaerbte
    QColor papier;                      // ungueltig = weiss, dann keine Fuellung
    bool   syntax = false;
    //  `mg::editor::languageForPath(...).id`; unbekannt -> keine Faerbung, kein Fehler.
    QString sprache;
    mg::editor::SyntaxPalette palette;
};

// Liefert false + `*err` bei Fehler; die Zieldatei wird dann nicht angelegt (QSaveFile-Rollback). `tabWidth` in
// ZEICHEN aus der Editor-Einstellung - eine feste 8 verdoppelte die Einrückung einer mit vier eingerückten Datei.
bool exportToPdf(const QString& text, const QString& targetPath,
                 const Stil& stil = Stil(),
                 int tabWidth = 4,
                 QString* err = nullptr);

//  Freier Zielpfad NEBEN der Quelle: <Name>.pdf, bei Kollision <Name> (2).pdf …
//  (gleiche Namensregel wie DocxEditController::pdfExportTargetPath).
QString targetPathFor(const QString& sourcePath);

} // namespace TextPdf
