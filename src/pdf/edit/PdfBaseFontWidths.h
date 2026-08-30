#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfBaseFontWidths.h - Zeichenbreiten der 14 PDF-Standardschriften
// ══════════════════════════════════════════════════════════════════════════════
//
//  WARUM ES DAS GIBT
//  ─────────────────
//  Ein PDF DARF die `/Widths` einer der 14 Standardschriften weglassen - der
//  Betrachter soll ihre Maße kennen (PDF-Spezifikation). `PdfTextLayout` lehnte
//  solche Seiten bisher ab („Glyphenbreite fehlt"), weil Raten schlimmer wäre
//  als Nichtstun: Eine falsche Breite verschiebt die Schreibmarke Zeichen für
//  Zeichen weiter aus der Zeile. Mit diesen Tabellen kennt sie die Maße
//  wirklich - und das Werkzeug „Text bearbeiten" funktioniert auch auf
//  Dokumenten, die ihre Standardschriften nicht selbst vermessen.
//
//  Die Zahlen sind maschinell erzeugt, nicht abgetippt; Stichproben prüft
//  `tests/pdf/tst_pdfbasewidths.cpp`.
//
//  NICHT ENTHALTEN: Courier (alle Zeichen 600 - das steht direkt in
//  `PdfTextLayout`), Symbol und ZapfDingbats (für Textbearbeitung ohne Belang).
// ══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>

namespace mg {

//  EIN Eintrag: Unicode des Zeichens -> Breite in 1/1000 em.
struct BaseWidth { unsigned short unicode; unsigned short width; };

//  Tabelle zu `baseFont` (z. B. "/Helvetica-Bold", auch mit Untermengen-Präfix
//  "ABCDEF+Helvetica"); `count` erhält die Länge. Kein Treffer -> nullptr, und
//  der Aufrufer lehnt wie bisher ab. Arial und Times New Roman gelten als
//  metrisch gleich zu Helvetica bzw. Times.
const BaseWidth* baseFontWidths(const QByteArray& baseFont, int* count);

} // namespace mg
