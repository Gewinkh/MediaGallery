#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  TextPdfExporter - schreibt einen Klartext (TXT/Quelltext) als paginiertes
//  A4-PDF. Bewusst EIGENSTÄNDIG und nicht am DOCX-Weg gebaut: der
//  DOCX-Exporter setzt sein QTextDocument aus dem OOXML-Modell zusammen
//  (aufgelöste Formate, Tabellen, verankerte Bilder) - davon gibt es bei einer
//  Textdatei nichts. Gemeinsam wären nur Seiteneinrichtung und Zielnamensfindung,
//  zu wenig für einen Umbau des bestehenden Exporters (CLAUDE.md Regeln 13/19).
//
//  FESTLEGUNGEN (Nutzerentscheidung 2026-08-11) - sie bestimmen das Ergebnis
//  vollständig und stehen deshalb hier, nicht in der Oberfläche:
//    • Monospace, 10 pt - Einrückungen, ASCII-Tabellen und Spalten bleiben stehen.
//    • Lange Zeilen brechen WEICH um (ohne Einzug); nichts wird abgeschnitten.
//    • Keine Zeilennummern.
//    • A4 hoch, 20 mm Rand rundum.
//    • Fußzeile: ausschließlich die Zählung „1/3", mittig - kein Dateiname.
//    • SCHRIFTFARBE wählbar (Nutzerentscheidung 2026-08-15): der Aufrufer gibt
//      sie mit; ohne Angabe bleibt es Schwarz. Sie darf NIE aus der
//      Anwendungspalette kommen - s. Kommentar an der Malstelle im .cpp.
//
//  Der Text kommt als Parameter herein und wird NICHT selbst von Platte gelesen:
//  der Texteditor ist editierbar, ein erneutes Einlesen würde bei ungespeicherten
//  Änderungen den alten Stand drucken.
//
//  Threadsicher: arbeitet nur auf lokalen Qt-Objekten (QTextDocument/QPdfWriter/
//  QPainter) und der übergebenen Kopie - läuft im Worker des ViewerController
//  (Muster wie DocxEditController::exportToPdf). Abhängigkeiten: NUR Qt6::Gui.
//  Schreiben atomar via QSaveFile.
// ─────────────────────────────────────────────────────────────────────────────

#include <QColor>
#include <QString>

namespace TextPdf {

//  Exportiert text nach targetPath (.pdf). Liefert false + *err bei Fehler; die
//  Zieldatei wird dann nicht angelegt (QSaveFile-Rollback).
//  textColor: Schriftfarbe des Fließtextes. Ungültige Farbe ⇒ Schwarz (die
//  Fußzeile bleibt immer gedämpftes Grau, sie gehört nicht zum Inhalt).
bool exportToPdf(const QString& text, const QString& targetPath,
                 const QColor& textColor = QColor(Qt::black),
                 QString* err = nullptr);

//  Freier Zielpfad NEBEN der Quelle: <Name>.pdf, bei Kollision <Name> (2).pdf …
//  (gleiche Namensregel wie DocxEditController::pdfExportTargetPath).
QString targetPathFor(const QString& sourcePath);

} // namespace TextPdf
