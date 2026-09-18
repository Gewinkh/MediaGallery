#include "editor/TextPdfExporter.h"

#include "core/PdfGlyphRuns.h"
#include "editor/CodeHighlighter.h"
#include "editor/LanguageTable.h"

#include <QBuffer>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QMarginsF>
#include <QPainter>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextLayout>
#include <QList>
#include <QTextOption>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QSaveFile>
#include <QFileInfo>
#include <QColor>
#include <memory>

namespace {

constexpr int   kResolution = 96;      // Layout-DPI
constexpr qreal kMarginMm   = 20.0;    // Rand rundum
constexpr qreal kFontPt     = 10.0;    // Textschrift (Monospace)
constexpr qreal kFooterPt   = 8.0;     // Fußzeile („1/3")

// `styleHint` + `fixedPitch` sorgen dafür, dass ein Rückfall wieder eine Monospace wählt - sonst verrutschen
// genau die Einrückungen, für die sie gewählt wurde. MEDIUM, weil Regular bei 10 pt grau wirkt (138/255 gegen 129).
QFont monoFont() {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPointSizeF(kFontPt);
    f.setWeight(QFont::Medium);
    return f;
}

} // namespace

namespace TextPdf {

QString targetPathFor(const QString& sourcePath) {
    if (sourcePath.isEmpty())
        return {};
    const QFileInfo fi(sourcePath);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName();
    QString candidate = dir + QLatin1Char('/') + base + QStringLiteral(".pdf");
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).pdf").arg(n);
        ++n;
    }
    return candidate;
}

bool exportToPdf(const QString& text, const QString& targetPath,
                 const Stil& stil, int tabWidth,
                 QString* err) {
    const QColor ink = stil.tinte.isValid() ? stil.tinte : QColor(Qt::black);
    //  Gefaerbt wird nur, wenn der Zerleger die Sprache auch kennt - sonst
    //  haenge ein leeres Blatt am Wunsch statt am Koennen.
    const bool faerben =
        stil.syntax
        && mg::editor::languageForId(stil.sprache).kind != mg::editor::ScannerKind::PlainText;
    if (targetPath.isEmpty()) {
        if (err) *err = QStringLiteral("Kein Zielpfad.");
        return false;
    }

    QSaveFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (err) *err = QStringLiteral("Ziel nicht beschreibbar.");
        return false;
    }

    //  Erst in den Speicher, dann durch `mergeGlyphRuns` - Qt schreibt sonst
    //  ein Textobjekt je Glyphe, und PDFium liest daraus zerrissene Wörter
    //  (s. core/PdfGlyphRuns.h).
    QByteArray pdfBytes;
    {
        QBuffer sink(&pdfBytes);
        sink.open(QIODevice::WriteOnly);
        QPdfWriter writer(&sink);
        writer.setPageSize(QPageSize(QPageSize::A4));
        //  Der Schreiber bekommt KEINE Raender - den Abstand setzt der Maler
        //  selbst. Mit Raendern begrenzt Qt die Malflaeche auf das Innere, und
        //  die Papierfarbe endete an der Randkante statt am Blattrand.
        writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
        writer.setResolution(kResolution);
        writer.setTitle(QFileInfo(targetPath).completeBaseName());

        //  Das GANZE Blatt, und darin der Satzspiegel.
        const QRectF blatt =
            writer.pageLayout().paintRectPixels(writer.resolution());
        const qreal  rand  = kMarginMm / 25.4 * kResolution;
        const QRectF paintRect(0, 0,
                               qMax(1.0, blatt.width()  - 2 * rand),
                               qMax(1.0, blatt.height() - 2 * rand));

        const QFont font = monoFont();
        QFont footFont = font;
        footFont.setPointSizeF(kFooterPt);

        const qreal footerH = QFontMetricsF(footFont, &writer).height() * 1.8;
        const qreal bodyH   = qMax(1.0, paintRect.height() - footerH);

        QTextDocument td;
        td.setDocumentMargin(0);
        td.setDefaultFont(font);
        QTextOption to;
        //  Weicher Umbruch OHNE Einzug: eine überlange Zeile geht weiter, statt
        //  abgeschnitten zu werden. AnywhereIfNecessary greift bei Zeilen ohne
        //  Leerzeichen (lange Pfade, base64).
        to.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        to.setTabStopDistance(
            QFontMetricsF(font, &writer).horizontalAdvance(
                QString(qBound(2, tabWidth, 8), u' ')));
        td.setDefaultTextOption(to);

        //  Zeilenenden vereinheitlichen: setPlainText trennt an '\n'; ein
        //  stehengebliebenes '\r' aus CRLF- oder alten Mac-Dateien würde als
        //  Ersatzkästchen mitgedruckt.
        QString body = text;
        body.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        body.replace(QLatin1Char('\r'),     QLatin1Char('\n'));
        td.setPlainText(body);

        //  Gefaerbt wird ueber DENSELBEN Faerber wie im Editor - eine zweite
        //  Umsetzung liefe gegen die erste, sobald eine Sprache dazukommt.
        //  Er haengt sich an das Dokument und faerbt beim ersten Auslegen.
        std::unique_ptr<mg::editor::Highlighter> faerber;
        if (faerben) {
            faerber = std::make_unique<mg::editor::Highlighter>(&td);
            faerber->setPalette(stil.palette);
            faerber->setLanguageId(stil.sprache);
        }

        //  Metriken am Writer messen (nicht am Bildschirm) - sonst hinge das
        //  Ergebnis an der Bildschirm-DPI des Rechners. ERST NACH dem Faerber:
        //  solange der Schreiber das Geraet ist, legt jede Formatmarke ihren
        //  Block mit dessen Metriken neu aus - 11,4 statt 0,02 s bei 8.200
        //  Zeilen. Die Farben haengen nicht an den Metriken, die endgueltige
        //  Auslegung unten schon.
        td.documentLayout()->setPaintDevice(&writer);

        // Der Text fließt durch eine "unendlich" hohe Seite, umbrochen wird unten selbst. Ließe man QTextDocument
        // paginieren, legt es die Zeile 0,009 px über die Grenze und zeichnet sie auf BEIDEN Seiten - im Textlayer
        // stünde sie doppelt, Suche und Kopieren fänden sie zweimal.
        td.setPageSize(QSizeF(paintRect.width(), 1e7));
        //  Auslegen ERZWINGEN: QTextDocument legt faul aus, ohne diesen Aufruf
        //  meldet jeder Block 0 Zeilen und die Seiteneinteilung unten liefe leer.
        (void)td.documentLayout()->documentSize();

        //  Zeilenweise in Seiten schneiden: eine Zeile gehört GANZ auf eine
        //  Seite. Ergebnis je Seite: [oben, unten) in Dokument-Koordinaten.
        struct Span { qreal top; qreal bottom; };
        QList<Span> pageSpans;
        {
            qreal top = 0.0;          // Oberkante der laufenden Seite
            qreal bottom = 0.0;       // Unterkante der letzten aufgenommenen Zeile
            for (QTextBlock b = td.begin(); b.isValid(); b = b.next()) {
                const QTextLayout* lay = b.layout();
                if (!lay) continue;
                const qreal blockY = lay->position().y();
                for (int i = 0; i < lay->lineCount(); ++i) {
                    const QTextLine ln = lay->lineAt(i);
                    const qreal lTop = blockY + ln.y();
                    const qreal lBot = lTop + ln.height();
                    //  Passt die Zeile nicht mehr ganz? -> Seite hier schließen.
                    //  Eine Zeile, die für sich schon höher ist als die Seite,
                    //  bekommt trotzdem ihre eigene (sonst Endlosschleife).
                    if (lBot - top > bodyH && bottom > top) {
                        pageSpans.append({top, bottom});
                        top = lTop;
                    }
                    bottom = lBot;
                }
            }
            pageSpans.append({top, qMax(bottom, top + 1.0)});
        }

        const int pages = pageSpans.size();

        QPainter p(&writer);
        for (int pg = 0; pg < pages; ++pg) {
            if (pg > 0) writer.newPage();

            //  Die Flaeche zuerst und ueber das GANZE Blatt, Rand eingeschlossen.
            //  Die zwei Pixel darueber hinaus fangen die Rundung von
            //  `paintRectPixels` ab (gemessen: 1588 weisse Pixel, eine Zeile).
            if (stil.papier.isValid())
                p.fillRect(blatt.adjusted(-2, -2, 2, 2), stil.papier);

            const Span& sp = pageSpans.at(pg);
            p.save();
            //  Der Rand steckt jetzt in der Verschiebung, nicht mehr im Schreiber.
            p.translate(rand, rand - sp.top);

            // Winziger Einzug an Ober- und Unterkante: die Blockauswahl arbeitet mit BERÜHRUNG, die Nachbarzeile der
            // vorigen Seite endet exakt auf der Kante und käme sonst in den Textlayer (optisch geklippt, für Suche da).
            constexpr qreal kEps = 0.05;
            const QRectF band(0, sp.top + kEps, paintRect.width(),
                              sp.bottom - sp.top - 2 * kEps);

            // Die Schriftfarbe kommt über die PALETTE des PaintContext, nicht über die Feder allein: `drawContents` nähme
            // die Anwendungspalette - im dunklen Theme stand deshalb #E6E6E6 auf weißem Papier. Sie kommt vom AUFRUFER.
            //  Bei Syntaxfaerbung traegt jede Marke ihre eigene Farbe; die
            //  Palette gilt dann nur noch fuer alles Ungefaerbte.
            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.clip = band;
            ctx.palette.setColor(QPalette::Text, ink);
            p.setClipRect(band);
            p.setPen(ink);
            td.documentLayout()->draw(&p, ctx);
            p.restore();

            p.save();
            p.setFont(footFont);
            //  Die Fusszeile muss auf dem Papier lesbar bleiben - auf einem
            //  dunklen Blatt waere ein festes Grau kaum zu sehen. Genommen wird
            //  deshalb die Grundfarbe, halb durchscheinend.
            QColor fuss(120, 120, 120);
            if (stil.papier.isValid()) { fuss = ink; fuss.setAlphaF(0.55f); }
            p.setPen(fuss);
            p.drawText(QRectF(rand, rand + paintRect.height() - footerH,
                              paintRect.width(), footerH),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       QStringLiteral("%1/%2").arg(pg + 1).arg(pages));
            p.restore();
        }
        p.end();
    }   // QPdfWriter zerstört -> PDF finalisiert (Trailer)

    const QByteArray fixed = mg::pdfglyphs::mergeGlyphRuns(pdfBytes);
    pdfBytes.clear();
    if (out.write(fixed) != fixed.size()) {
        if (err) *err = QStringLiteral("Schreiben fehlgeschlagen.");
        return false;
    }
    if (!out.commit()) {
        if (err) *err = QStringLiteral("Schreiben fehlgeschlagen.");
        return false;
    }
    return true;
}

} // namespace TextPdf
