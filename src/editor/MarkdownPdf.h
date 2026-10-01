#pragma once
#include "editor/MarkdownRender.h"

#include <QString>
#include <atomic>

// MarkdownPdf - die gerenderte Markdown-Ansicht als A4-PDF; laeuft im Arbeitsfaden.
// Code-Kaesten und Frontmatter-Karte zeichnet hier der Exporter, in der Ansicht tut es die Oberflaeche.
namespace mg::editor::md {

struct PdfOptions {
    bool print = false;        // Schwarz/Weiss auf weissem Papier statt der Farben der Ansicht
    bool landscape = false;
    int  firstPage = 1;        // 1-basiert
    int  lastPage = 0;         // 0 = bis zum Ende
};

// Graustufen fuer den Druck: Schrift schwarz, Kommentare und Nebensachen grau, keine Farbflaechen.
SyntaxPalette printPalette();

// Leeres `target` zaehlt nur. Liefert die Seitenzahl des GANZEN Dokuments, -1 bei Fehler (`*err`);
// die Zieldatei entsteht nur bei Erfolg.
int writePdf(const Document& doc, RenderStyle style, const PdfOptions& opt, const QString& target,
             QString* err = nullptr, const std::atomic<bool>* abort = nullptr);

}  // namespace mg::editor::md
