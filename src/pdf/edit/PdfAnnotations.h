#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfAnnotations.h — STANDARD-Annotationen einer PDF lesen
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK (README ▸ Planned ▸ PDF-Editor: „Annotation Interchange")
//  ──────────────────────────────────────────────────────────────
//  Notizen, die in einem ANDEREN Betrachter (Acrobat, Okular, Xournal++ …)
//  entstanden sind, liegen als Annotationsobjekte im Dokument. Diese Einheit
//  liest sie heraus, damit die App sie zeigen und bearbeiten kann, statt sie
//  zu ignorieren.
//
//  WARUM ÜBERHAUPT SELBST LESEN — Qt PDF zeigt sie doch?
//  ────────────────────────────────────────────────────
//  Gemessen (Qt 6.11.1): `QPdfDocument::render` zeichnet Markup-Annotationen
//  NUR mit `RenderFlag::Annotations` (ohne das Flag: 0 Pixel, mit Flag:
//  vollständig — anders als bei Widget-/Formularfeldern, die PDFium ohne
//  `FPDF_FFLDraw` nie malt). Das Flag reicht aber nicht: Ein GERENDERTES Bild
//  ist unveränderlich — die Annotation wäre sichtbar, aber nicht auswählbar,
//  verschiebbar oder löschbar. Für „Interchange" muss die App die Objekte
//  selbst kennen; erst dann kann sie sie in ihr eigenes Overlay-Modell
//  übernehmen.
//
//  KOORDINATEN
//  ───────────
//  Wie im ganzen Editor: PDF-Punkte mit Ursprung OBEN-LINKS der ANGEZEIGTEN
//  (also ggf. gedrehten) Seite — `mg::pdfobj::toDisplay`. Der Aufrufer muss
//  nichts umrechnen.
//
//  ABGRENZUNG
//  ──────────
//   • `/Widget` (Formularfelder) → `PdfFormFields`,
//   • `/Link`, `/Screen`, `/Movie`, `/RichMedia`, `/Sound`, `/FileAttachment`
//     (eingebettete Medien und Verweise) → `PdfMediaHandler`,
//   • `/Popup` ist nur das Notizfenster einer anderen Annotation und wird
//     bewusst übersprungen (der Inhalt steht in der Eltern-Annotation).
//  Alles Übrige — `/Text`, `/FreeText`, `/Square`, `/Circle`, `/Line`, `/Ink`,
//  `/Highlight`, `/Underline`, `/StrikeOut` — liefert diese Einheit.
//
//  ABHÄNGIGKEITEN: Qt6::Gui (QColor) + `PdfObjects`. Kein Q_OBJECT/moc;
//  isoliert testbar.
// ══════════════════════════════════════════════════════════════════════════════

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace mg {

//  Art der Annotation. Die Reihenfolge ist Teil der QML-Schnittstelle (als int
//  weitergereicht) — neue Arten hinten anhängen.
enum class PdfAnnotKind {
    Unknown = 0,
    Text,          // „Sticky Note" — kleines Symbol, Text im Popup
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

    QColor color;                         // /C  — Strich/Symbolfarbe (ungültig = keine)
    QColor interiorColor;                 // /IC — Füllung von Square/Circle/Line-Enden
    qreal  borderWidth = 1.0;             // /BS /W bzw. /Border[2]
    qreal  opacity     = 1.0;             // /CA (0…1)

    //  Nur /FreeText: aus dem /DA-String gelesen.
    qreal  fontSizePt = 0.0;              // 0 = „automatisch" bzw. nicht angegeben
    QColor textColor;

    //  Nur /Ink: Striche, je Strich die Punktfolge (Anzeigekoordinaten).
    QVector<QVector<QPointF>> inkPaths;
    //  Nur /Line: Anfangs- und Endpunkt (Anzeigekoordinaten).
    QVector<QPointF> line;
    //  Nur /Highlight, /Underline, /StrikeOut: die markierten Bereiche.
    QVector<QRectF> quads;

    bool hidden = false;                  // /F Bit 2 (Hidden) oder Bit 6 (NoView)
    int  objNum = -1;                     // Objektnummer (Identität in der Datei)
};

class PdfAnnotations {
public:
    //  Liest alle Standard-Annotationen von `path`. Liefert false, wenn die
    //  Datei nicht lesbar/kein PDF/verschlüsselt ist oder eine XRef-Stream-
    //  Struktur trägt (dieselbe Zusage wie die Schwester-Einheiten). Ein
    //  Dokument OHNE Annotationen ist KEIN Fehler: true mit leerer Liste.
    static bool read(const QString& path, QVector<PdfAnnotation>* out,
                     QString* err = nullptr);

    //  Schreibt `outputPath` (atomar) mit den ZUSÄTZLICHEN Annotationen aus
    //  `annots`; `inputPath` bleibt byteweise unangetastet, vorhandene
    //  Annotationen bleiben erhalten. Beide Pfade dürfen NICHT gleich sein.
    //
    //  `removeObjNums` streicht Annotationen aus den `/Annots` der Seiten —
    //  gebraucht, sobald eine ÜBERNOMMENE Annotation im Editor bearbeitet oder
    //  gelöscht wurde: sonst stünde die alte Fassung weiter unter der neuen.
    //  Das Objekt selbst bleibt in der Datei liegen (inkrementelles Update
    //  entfernt keine Bytes); es ist danach nur von keiner Seite mehr
    //  referenziert und damit wirkungslos.
    //
    //  Verfahren: inkrementelles Update (s. `mg::pdfobj::IncrementalUpdate`) —
    //  je Annotation ein neues Objekt, je betroffener Seite ein fortgeschriebenes
    //  `/Annots`-Array.
    //
    //  ERSCHEINUNGSBILD: `/AP /N` wird IMMER selbst erzeugt. Für Markup-
    //  Annotationen gibt es kein Gegenstück zu `/NeedAppearances`; ohne
    //  Erscheinungsbild zeigen viele Betrachter (und praktisch jeder Druckweg)
    //  nichts — die Annotation stünde in der Datei und wäre unsichtbar.
    //
    //  BEWUSST BEGRENZT (Rückgabe false):
    //   • unverschlüsselt, klassische xref (Zusage der Schwester-Einheiten),
    //   • Seitenindex außerhalb des Dokuments, leere Liste, Ziel == Quelle,
    //   • `/Text` (Sticky Note) wird als kleines Notizsymbol gezeichnet — das
    //     Popup-Fenster mit dem Text erzeugt der Betrachter selbst.
    static bool write(const QString& inputPath, const QString& outputPath,
                      const QVector<PdfAnnotation>& annots,
                      const QVector<int>& removeObjNums = {},
                      QString* err = nullptr);
};

} // namespace mg
