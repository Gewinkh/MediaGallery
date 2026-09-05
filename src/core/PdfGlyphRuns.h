#pragma once
// Qts PDF-Erzeuger schreibt ein Textobjekt je GLYPHE; PDFium hält eine so gesetzte Seite für senkrechten Text
// und trennt zwischen den Buchstaben (gemessen: "Hallo" 0 Treffer, "allo" 132). Hier wird die Kette zu einem TJ
// zusammengefasst. Was nicht sicher verstanden wird, bleibt unangetastet.

#include <QByteArray>

namespace mg::pdfglyphs {

//  Liefert die nachbearbeiteten Bytes - oder `pdf` unverändert, wenn die Datei
//  nicht der von `QPdfWriter` erzeugten Form entspricht.
QByteArray mergeGlyphRuns(const QByteArray& pdf);

}  // namespace mg::pdfglyphs
