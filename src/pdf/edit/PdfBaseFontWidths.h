#pragma once
// Zeichenbreiten der 14 PDF-Standardschriften: ein PDF darf deren /Widths weglassen. PdfTextLayout
// lehnte solche Seiten bisher ab, weil eine geratene Breite die Schreibmarke Zeichen für Zeichen aus
// der Zeile schiebt. Ohne Courier (fest 600), Symbol und ZapfDingbats.

#include <QByteArray>

namespace mg {

//  EIN Eintrag: Unicode des Zeichens -> Breite in 1/1000 em.
struct BaseWidth { unsigned short unicode; unsigned short width; };

// Tabelle zu `baseFont`, auch mit Untermengen-Präfix ("ABCDEF+Helvetica"); kein Treffer -> nullptr, und der
// Aufrufer lehnt ab. Arial und Times New Roman gelten als metrisch gleich zu Helvetica bzw. Times.
const BaseWidth* baseFontWidths(const QByteArray& baseFont, int* count);

} // namespace mg
