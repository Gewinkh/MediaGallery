#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxPdfExporter — schreibt ein bereits geladenes Docx::Document als
//  paginiertes A4-PDF (Aufgabe 2 „→ PDF").
//
//  VERFAHREN (Entscheidung nach §0): Es wird bewusst KEINE eigene Layout-Engine
//  gebaut, sondern die Qt-Text-Engine genutzt — dieselbe, die auch die
//  Editor-Anzeige (DocxTextArea) speist. Aus dem verlusterhaltenden Modell wird
//  ein QTextDocument mit den GLEICHEN aufgelösten Formaten (resolvePar/
//  resolveRun/numLevel) zusammengesetzt und via QTextDocument::print() auf einen
//  QPdfWriter paginiert. Ergebnis = „wie die Editor-Ansicht", nur auf echte
//  A4-Seiten umbrochen (fortlaufender Text, Tabellen als Platzhalter — wie im
//  Editor). Abhängigkeiten: NUR Qt6::Gui (QTextDocument + QPdfWriter), also KEINE
//  neue Bibliothek (§0-Priorität 3).
//
//  Threadsicher: die Funktion arbeitet ausschließlich auf der übergebenen
//  Document-Kopie und lokalen Qt-Objekten (QTextDocument/QPdfWriter/QPainter) —
//  sie läuft im Worker-Thread des DocxEditController (Muster PdfExportTask).
//  Schreiben atomar via QSaveFile.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>

namespace Docx { class Document; }

namespace DocxPdf {

//  Exportiert doc nach targetPath (.pdf). tableLabel/pageBreakLabel sind die
//  i18n-Texte aus QML (identisch zur Editor-Anzeige). Liefert false + *err bei
//  Fehler; die Zieldatei wird dann nicht angelegt (QSaveFile-Rollback).
bool exportToPdf(const Docx::Document& doc, const QString& targetPath,
                 const QString& tableLabel, const QString& pageBreakLabel,
                 QString* err = nullptr);

} // namespace DocxPdf
