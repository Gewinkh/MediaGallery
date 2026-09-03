#include "core/TextPdfExporter.h"

#include "core/PdfGlyphRuns.h"

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

namespace {

constexpr int   kResolution = 96;      // Layout-DPI
constexpr qreal kMarginMm   = 20.0;    // Rand rundum
constexpr qreal kFontPt     = 10.0;    // Textschrift (Monospace)
constexpr qreal kFooterPt   = 8.0;     // Fußzeile („1/3")
//  Die Tabulatorweite kommt jetzt als Argument (aus der Editor-Einstellung).
//  Geklemmt wie dort, damit ein verfaelschter Wert nichts zerlegt.

//  Dicktengleiche Schrift des Systems. styleHint + fixedPitch sorgen dafür,
//  dass ein Rückfall (Familie nicht vorhanden) wieder eine Monospace wählt -
//  sonst verrutschen genau die Einrückungen, für die sie gewählt wurde.
//
//  GEWICHT MEDIUM, nicht Regular: die Tinte ist reines Schwarz (s. unten), der
//  Regular-Schnitt einer Monospace ist bei 10 pt aber so dünn, dass die Seite in
//  voller Größe am Bildschirm GRAU wirkt - gemessen 138/255 mittlere Tinte gegen
//  129 bei Medium; Bold (111) liest sich als Auszeichnung und kommt für Fließtext
//  nicht in Frage. Fehlt der Medium-Schnitt, wählt Qt den nächstliegenden.
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
                 const QColor& textColor, int tabWidth,
                 QString* err) {
    //  Unbrauchbare Farbe ⇒ Schwarz. Nie ungeprüft übernehmen: eine
    //  ungültige QColor malte sonst schwarz-transparent bis unsichtbar.
    const QColor ink = textColor.isValid() ? textColor : QColor(Qt::black);
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
        writer.setPageMargins(QMarginsF(kMarginMm, kMarginMm, kMarginMm, kMarginMm),
                              QPageLayout::Millimeter);
        writer.setResolution(kResolution);
        writer.setTitle(QFileInfo(targetPath).completeBaseName());

        //  Bedruckbare Fläche (Seite abzüglich Ränder) - der QPainter auf einem
        //  QPdfWriter hat dort seinen Ursprung.
        const QRectF paintRect =
            writer.pageLayout().paintRectPixels(writer.resolution());

        const QFont font = monoFont();
        QFont footFont = font;
        footFont.setPointSizeF(kFooterPt);

        //  Die Fußzeile bekommt einen eigenen Streifen am Fuß der bedruckbaren
        //  Fläche; der Text wird nur bis darüber umbrochen, sonst liefe die
        //  letzte Zeile in die Zählung.
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
        //  An echten LEERZEICHEN gemessen und mit derselben Zahl wie im Editor -
        //  sonst ist derselbe Text gedruckt anders eingerueckt als am Bildschirm.
        to.setTabStopDistance(
            QFontMetricsF(font, &writer).horizontalAdvance(
                QString(qBound(2, tabWidth, 8), u' ')));
        td.setDefaultTextOption(to);
        //  Metriken am Writer messen (nicht am Bildschirm) - sonst hinge das
        //  Ergebnis an der Bildschirm-DPI des Rechners.
        td.documentLayout()->setPaintDevice(&writer);

        //  Zeilenenden vereinheitlichen: setPlainText trennt an '\n'; ein
        //  stehengebliebenes '\r' aus CRLF- oder alten Mac-Dateien würde als
        //  Ersatzkästchen mitgedruckt.
        QString body = text;
        body.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        body.replace(QLatin1Char('\r'),     QLatin1Char('\n'));
        td.setPlainText(body);

        //  DER TEXT FLIESST DURCH (eine „unendlich" hohe Seite); umbrochen wird
        //  unten selbst.
        //
        //  Warum nicht td.setPageSize(…, bodyH) und QTextDocument paginieren
        //  lassen: gemessen legt es die Zeile an der Kante 0,009 px ÜBER die
        //  Grenze und lässt sie 18 px hinunterragen. Sichtbar ist das nichts,
        //  aber gezeichnet wird sie damit auf BEIDEN Seiten (Klippen versteckt
        //  sie nur optisch) - im Textlayer steht die Zeile doppelt, Suche und
        //  Kopieren finden sie zweimal. Eigene Zeilengrenzen kennen diesen
        //  Rundungsfall nicht.
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

            const Span& sp = pageSpans.at(pg);
            p.save();
            p.translate(0, -sp.top);

            //  WINZIGER EINZUG (kEps) an Ober- und Unterkante: die Auswahl der zu
            //  zeichnenden Blöcke arbeitet mit BERÜHRUNG - die Nachbarzeile der
            //  vorigen/nächsten Seite endet exakt auf der Kante und käme sonst
            //  mit in den Textlayer (nur optisch weggeklippt, für Suche und
            //  Kopieren aber vorhanden). Sichtbar kostet der Einzug nichts.
            constexpr qreal kEps = 0.05;
            const QRectF band(0, sp.top + kEps, paintRect.width(),
                              sp.bottom - sp.top - 2 * kEps);

            //  DIE SCHRIFTFARBE WIRD IMMER GESETZT - über die PALETTE des
            //  PaintContext, NICHT über die Feder des Malers allein.
            //  `drawContents` wäre kürzer, nimmt aber die Anwendungspalette: im
            //  dunklen Theme stand deshalb `0.902 0.902 0.902` (= #E6E6E6,
            //  `themeTextPrimary`) als Füllfarbe im PDF - das Papier war weiß,
            //  die Schrift fast auch. Am Prüfstand fiel es nicht auf, weil dort
            //  die Standardpalette ohnehin schwarz ist. Die Farbe kommt deshalb
            //  vom AUFRUFER (Datei- bzw. globale Einstellung), nie aus der
            //  laufenden Anwendung.
            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.clip = band;
            ctx.palette.setColor(QPalette::Text, ink);
            p.setClipRect(band);
            p.setPen(ink);
            td.documentLayout()->draw(&p, ctx);
            p.restore();

            //  Fußzeile: ausschließlich die Zählung, mittig und gedämpft.
            p.save();
            p.setFont(footFont);
            p.setPen(QColor(120, 120, 120));
            p.drawText(QRectF(0, paintRect.height() - footerH,
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
