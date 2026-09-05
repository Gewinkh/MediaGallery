#pragma once
// Schreibt erkannte Wörter als unsichtbaren Text (Textmodus 3) über das Scan-Bild - kein Pixel ändert
// sich, aber Suche, Auswahl und Kopieren finden ihn in JEDEM Leser. Erkennt selbst nichts: die Kästen
// kommen fertig herein, damit die Einheit nicht an Tesseract hängt. Nicht-WinAnsi wird übersprungen.

#include <QRectF>
#include <QString>
#include <QVector>

namespace mg {

//  EIN erkanntes Wort. `rectPts` im Anzeige-Raum der Seite (PDF-Punkte,
//  Ursprung oben-links), `text` der erkannte Wortlaut ohne Leerraum.
struct PdfOcrWord {
    QRectF  rectPts;
    QString text;
};

class PdfOcrLayer {
public:
    // `words` hat EINEN Eintrag je Seite; eine leere Liste lässt die Seite unangetastet. `skipped` erhält die Zahl
    // der an WinAnsi gescheiterten Wörter. Ziel darf nicht die Quelle sein.
    static bool write(const QString& inputPath, const QString& outputPath,
                      const QVector<QVector<PdfOcrWord>>& words,
                      QString* err, int* skipped = nullptr);

    static bool hasLayer(const QString& path);
};

}  // namespace mg
