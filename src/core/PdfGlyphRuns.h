#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  PdfGlyphRuns - Nachbearbeitung einer von `QPdfWriter` geschriebenen PDF.
//
//  WARUM ES DAS GIBT
//  Qts PDF-Erzeuger schreibt **ein Textobjekt je GLYPHE**:
//      BT /F11 15 Tf 1 0 0 -1 0 0 Tm
//      94.45 -138.48 Td <0001> Tj      <- H
//      11.109 0     Td <0002> Tj      <- a
//      …
//  Sichtbar ist das einwandfrei. Aber PDFium - die Maschine hinter der Suche
//  DIESER App und hinter Chromes PDF-Anzeige - entscheidet je Seite, ob die
//  Schrift waagerecht oder senkrecht läuft, und zwar daraus, wie dicht die
//  Textobjekte die Seite in beiden Richtungen füllen. Eine Seite aus kurzen,
//  eng gesetzten Zeilen kippt diese Abstimmung mit lauter Einzelglyphen auf
//  „senkrecht"; danach setzt PDFium zwischen die Buchstaben einen Umbruch, und
//  aus „Hallo" wird beim Auslesen „H" + „allo".
//  Gemessen an 44 Zeilen „Hallo wie geht es": „Hallo" 0 Treffer, „allo" 132.
//
//  WAS DIESE DATEI TUT - zwei Eingriffe, beide verlustfrei:
//   (1) Die Glyphenkette wird zu EINEM `TJ` zusammengefasst:
//          94.45 -138.48 Td [<0001> 0.625 <0002> … ] TJ
//       Die Zwischenwerte sind die Versätze in Tausendstel Textraum
//       (`a = Breite(g) - dx*1000/Schriftgrad`) - dieselben Pixel, ein
//       Textobjekt je gezeichnetem Stück statt eines je Zeichen.
//   (2) Die `ToUnicode`-Tabelle wird korrigiert: Qt bildet die Leerzeichen-
//       Glyphe auf **U+0009** ab (die Rückwärtssuche im Zeichensatz findet den
//       Tabulator zuerst). Vorher fiel das nicht auf, weil PDFium das
//       Leerzeichen selbst erzeugte; mit (1) wird die Glyphe gelesen, und ein
//       Satz käme mit Tabulatoren statt Leerzeichen heraus.
//
//  GRUNDSATZ: Was nicht sicher verstanden wird, bleibt unangetastet. Jede
//  Unstimmigkeit (fremdes xref-Format, unbekannter Filter, Glyphe ohne
//  Breitenangabe, Kette ohne `ET` dahinter) führt dazu, dass die EINGABE
//  unverändert zurückkommt - eine ausgegebene Datei darf nie schlechter werden
//  als die, die Qt geschrieben hat.
//
//  Kein Q_OBJECT/moc; freie Funktion im Namespace mg::pdfglyphs.
//  Test: tests/core/tst_pdfglyphruns.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>

namespace mg::pdfglyphs {

//  Liefert die nachbearbeiteten Bytes - oder `pdf` unverändert, wenn die Datei
//  nicht der von `QPdfWriter` erzeugten Form entspricht.
QByteArray mergeGlyphRuns(const QByteArray& pdf);

}  // namespace mg::pdfglyphs
