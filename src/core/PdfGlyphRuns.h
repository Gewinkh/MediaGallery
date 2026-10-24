#pragma once
// Qts PDF-Erzeuger schreibt ein Textobjekt je GLYPHE; PDFium hält eine so gesetzte Seite für senkrechten Text
// und trennt zwischen den Buchstaben (gemessen: "Hallo" 0 Treffer, "allo" 132). Hier wird die Kette zu einem TJ
// zusammengefasst. Was nicht sicher verstanden wird, bleibt unangetastet.

#include <QByteArray>
#include <QList>
#include <QRectF>

namespace mg::pdfglyphs {

struct Link {
    int        page = 0;
    QRectF     rect;
    QByteArray uri;              // leer: Sprung nach `targetPage`, Hoehe `targetY`
    int        targetPage = -1;
    qreal      targetY = 0;
};

//  Liefert die nachbearbeiteten Bytes - oder `pdf` unverändert, wenn die Datei
//  nicht der von `QPdfWriter` erzeugten Form entspricht.
QByteArray mergeGlyphRuns(const QByteArray& pdf);

//  Qt schreibt Verlauf, Muster und Kachelbild je Füllung neu: gleiche werden eines. Im Zweifel `pdf` unverändert.
QByteArray dropDuplicates(const QByteArray& pdf);

//  Qt schreibt Verweise nur aus Textformaten; Punkte von oben, Seiten 0-basiert. Im Zweifel `pdf` unverändert.
QByteArray addLinks(const QByteArray& pdf, const QList<Link>& links);

}  // namespace mg::pdfglyphs
