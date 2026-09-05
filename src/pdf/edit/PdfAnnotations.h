#pragma once
// Liest Standard-Annotationen (/Text, /FreeText, /Square, /Ink, /Highlight ...) als OBJEKTE.
// RenderFlag::Annotations zeigt sie zwar, aber ein gerendertes Bild ist nicht auswählbar oder löschbar.
// /Widget -> PdfFormFields, /Link und Medien -> PdfMediaHandler, /Popup übersprungen (Text steht im Elter).

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace mg {

//  Art der Annotation. Die Reihenfolge ist Teil der QML-Schnittstelle (als int
//  weitergereicht) - neue Arten hinten anhängen.
enum class PdfAnnotKind {
    Unknown = 0,
    Text,          // „Sticky Note" - kleines Symbol, Text im Popup
    FreeText,      // sichtbarer Textkasten auf der Seite
    Square,
    Circle,
    Line,
    Ink,           // Freihand (eine oder mehrere Striche)
    Highlight,
    Underline,
    StrikeOut
};

//  EINE gelesene Annotation. Felder, die die jeweilige Art nicht kennt, bleiben
//  auf ihrem Standardwert (ungültige Farbe, leere Liste, 0).
struct PdfAnnotation {
    int          page = -1;               // Seitenindex (0-basiert)
    PdfAnnotKind kind = PdfAnnotKind::Unknown;
    QRectF       rect;                    // /Rect in Anzeigekoordinaten

    QString contents;                     // /Contents (Notiztext)
    QString author;                       // /T (Verfasser)
    QString subject;                      // /Subj

    QColor color;                         // /C  - Strich/Symbolfarbe (ungültig = keine)
    QColor interiorColor;                 // /IC - Füllung von Square/Circle/Line-Enden
    qreal  borderWidth = 1.0;             // /BS /W bzw. /Border[2]
    qreal  opacity     = 1.0;             // /CA (0…1)

    qreal  fontSizePt = 0.0;              // 0 = „automatisch" bzw. nicht angegeben
    QColor textColor;

    QVector<QVector<QPointF>> inkPaths;
    QVector<QPointF> line;
    QVector<QRectF> quads;

    bool hidden = false;                  // /F Bit 2 (Hidden) oder Bit 6 (NoView)
    int  objNum = -1;                     // Objektnummer (Identität in der Datei)
};

class PdfAnnotations {
public:
    // Liest alle Standard-Annotationen. false bei nicht lesbarer, verschlüsselter oder XRef-Stream-Datei; ein
    // Dokument OHNE Annotationen ist KEIN Fehler - true mit leerer Liste.
    static bool read(const QString& path, QVector<PdfAnnotation>* out,
                     QString* err = nullptr);

    // Hängt `annots` per inkrementellem Update an; `removeObjNums` streicht Vorgänger aus den /Annots,
    // sonst stünde die alte Fassung weiter unter der neuen. /AP /N wird IMMER selbst erzeugt - für
    // Markup gibt es kein /NeedAppearances, ohne Erscheinungsbild zeigen viele Betrachter nichts.
    static bool write(const QString& inputPath, const QString& outputPath,
                      const QVector<PdfAnnotation>& annots,
                      const QVector<int>& removeObjNums = {},
                      QString* err = nullptr);
};

} // namespace mg
