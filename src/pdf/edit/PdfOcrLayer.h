#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfOcrLayer.h - eine UNSICHTBARE Textebene in eine gescannte PDF schreiben
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Eine gescannte Seite ist ein Bild: es gibt keinen Text, also findet die
//  Suche nichts, das Markieren greift ins Leere und die Werkzeuge des Editors
//  haben keine Zeilen, an denen sie einschnappen könnten. Diese Einheit legt
//  die erkannten Wörter als Text IN die Datei - im PDF-Textmodus 3 („neither
//  fill nor stroke"), also unsichtbar über dem Bild. Das Aussehen der Seite
//  ändert sich um kein Pixel; ab dann findet, markiert und kopiert JEDER Leser
//  den Text, nicht nur diese App.
//
//  Das ist derselbe Kunstgriff, mit dem jede OCR-PDF der Welt arbeitet.
//
//  BEWUSST OHNE TESSERACT
//  ──────────────────────
//  Diese Einheit ERKENNT nichts - sie bekommt fertige Wortkästchen und schreibt
//  sie. Dadurch hängt sie nicht an der optionalen Abhängigkeit, und ihr
//  Regressionstreiber läuft auf jedem Rechner, auch ohne installiertes
//  Tesseract (`tests/pdf/tst_pdfocrlayer.cpp` gibt die Kästchen von Hand vor).
//  Die Erkennung selbst treibt `PdfEditController` über `mg::ocr`.
//
//  VERFAHREN
//  ─────────
//  Inkrementelles Update (Muster des Projekts, s. `PdfObjects.h`): Die
//  Originalbytes bleiben 1:1 stehen, angehängt werden nur ein Schrift-Objekt,
//  je Seite ein Inhaltsstrom und die fortgeschriebenen Seiten-Dictionaries
//  (`/Contents` wird zum Array, `/Resources` bekommt die Schrift). Geschrieben
//  wird atomar über `QSaveFile`.
//
//  KOORDINATEN: `rectPts` liegt im ANZEIGE-Raum (Ursprung oben-links der
//  GEDREHTEN Seite, PDF-Punkte) - genau so, wie die Seite gerendert wurde und
//  wie `mg::OcrLine::rectPts` es liefert. Die Abbildung in den Benutzerraum
//  übernimmt `mg::pdfobj::toUser`, samt Seitendrehung.
//
//  GRENZEN
//  ───────
//   • **WinAnsi (Latin-1)**: Wörter mit Zeichen außerhalb dieser Kodierung
//     werden ÜBERSPRUNGEN, nicht falsch geschrieben. Arabisch, Japanisch,
//     Kyrillisch brauchen eine CID-Schrift mit `/ToUnicode` - eigenes Vorhaben.
//     `skipped` meldet, wie viele Wörter deshalb fehlen.
//   • Verschlüsselte Dateien und XRef-Stream-PDFs lehnt `PdfDoc::load` ab
//     (Vorbedingung jedes inkrementellen Updates in diesem Projekt).
//
//  ABHÄNGIGKEITEN: Qt6::Core + `PdfObjects` + `PdfBaseFontWidths`.
//  Kein Q_OBJECT/moc.
// ══════════════════════════════════════════════════════════════════════════════

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
    //  `words` hat EINEN Eintrag je Seite (Index = Seitenindex); eine leere
    //  Liste lässt die Seite unangetastet. `skipped` (optional) erhält die
    //  Zahl der Wörter, die an WinAnsi scheiterten.
    //  Ziel darf nicht die Quelle sein. Liefert false ohne Ausgabedatei, wenn
    //  nichts zu schreiben war oder die Datei sich nicht fortschreiben lässt.
    static bool write(const QString& inputPath, const QString& outputPath,
                      const QVector<QVector<PdfOcrWord>>& words,
                      QString* err, int* skipped = nullptr);

    //  Trägt `path` bereits eine von uns geschriebene Textebene? (Marker im
    //  Inhaltsstrom - verhindert, dass ein zweiter Lauf alles verdoppelt.)
    static bool hasLayer(const QString& path);
};

}  // namespace mg
