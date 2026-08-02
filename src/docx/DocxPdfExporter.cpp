#include "docx/DocxPdfExporter.h"
#include "docx/DocxDocument.h"

#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextFrame>
#include <QTextFrameFormat>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableFormat>
#include <QTextLength>
#include <QTextOption>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QMarginsF>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPalette>
#include <QTextImageFormat>
#include <QUrl>
#include <QImage>
#include <QSaveFile>
#include <QFileInfo>
#include <QFont>
#include <QColor>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextLine>

using namespace Docx;

namespace {

//  Punkt → Layout-Pixel: identisch zur Editor-Anzeige (DocxTextArea kPtToPx),
//  hier relativ zur tatsächlichen Writer-Auflösung berechnet, damit Abstände
//  auflösungsunabhängig korrekt sind. Schriftgrößen bleiben in Punkt (physisch
//  korrekt, unabhängig von der Auflösung).
constexpr qreal kListIndentPt = 21.0;   // ≈ 28 px @96 dpi (Editor: kListIndent)
constexpr int   kResolution   = 96;     // Layout-DPI (wie die Editor-Anzeige)
constexpr qreal kCellPadTw    = 108.0;  // Zellrand, Word-Vorgabe (wie DocxTextArea)

//  Zeichenformat aus einem aufgelösten Run bauen (identisch zu DocxTextArea).
QTextCharFormat charFormatFor(const RunFmt& rf, const RunFmt& def) {
    QTextCharFormat cf;
    QFont f;
    f.setFamily(rf.font.isEmpty() ? def.font : rf.font);
    f.setPointSizeF(rf.sizePt > 0 ? rf.sizePt : def.sizePt);
    f.setBold(rf.bold);
    f.setItalic(rf.italic);
    f.setUnderline(rf.underline);
    cf.setFont(f);
    cf.setForeground(rf.color.isValid() ? rf.color : QColor(0, 0, 0));
    return cf;
}

//  Absatzformat → QTextBlockFormat (Ausrichtung, Zeilenabstand, Abstand
//  davor/danach, Listen-Einzug) — 1:1 zur Editor-Logik.
QTextBlockFormat blockFormatFor(const ParFmt& pf, qreal ptToPx) {
    QTextBlockFormat bf;
    switch (pf.align) {
    case 1:  bf.setAlignment(Qt::AlignHCenter); break;
    case 2:  bf.setAlignment(Qt::AlignRight);   break;
    case 3:  bf.setAlignment(Qt::AlignJustify); break;
    default: bf.setAlignment(Qt::AlignLeft);    break;
    }
    const qreal spacing = qMax(0.5, pf.lineSpacing);
    bf.setLineHeight(spacing * 100.0, QTextBlockFormat::ProportionalHeight);
    bf.setTopMargin(pf.beforePt * ptToPx);
    bf.setBottomMargin(pf.afterPt * ptToPx);
    if (pf.numId > 0)
        bf.setLeftMargin(kListIndentPt * ptToPx * (pf.ilvl + 1));
    return bf;
}

//  Listenmarker eines Absatzes (identisch zu DocxTextArea::rebuildMarkers).
QString markerFor(const Document& doc, const ParFmt& pf, QHash<int, int>& counters) {
    if (pf.numId <= 0) return {};
    const NumLevel lv = doc.numLevel(pf.numId, pf.ilvl);
    QString marker;
    if (lv.numFmt == QLatin1String("bullet")) {
        marker = lv.lvlText.isEmpty() ? QStringLiteral("\u2022") : lv.lvlText;
        if (marker.contains(QLatin1Char('%'))) marker = QStringLiteral("\u2022");
    } else {
        const int n = ++counters[pf.numId];
        marker = lv.lvlText;
        if (marker.isEmpty()) marker = QStringLiteral("%1.");
        marker.replace(QStringLiteral("%1"), QString::number(n));
        marker.remove(QRegularExpression(QStringLiteral("%\\d")));
    }
    marker += QLatin1Char(' ');
    return marker;
}

} // namespace

namespace DocxPdf {

bool exportToPdf(const Document& doc, const QString& targetPath,
                 const QString& tableLabel, const QString& pageBreakLabel,
                 QString* err) {
    Q_UNUSED(pageBreakLabel);   // in der Editor-Ansicht ein Marker; im PDF sorgt
                                // der Seitenumbruch selbst für die Trennung.

    QSaveFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (err) *err = QStringLiteral("Ziel nicht beschreibbar.");
        return false;
    }

    {
        QPdfWriter writer(&out);
        //  SEITENEINRICHTUNG AUS DEM DOKUMENT (w:sectPr) statt fest A4/25 mm —
        //  sonst bekommt jedes Dokument dieselben Ränder, egal wie es gesetzt
        //  ist. Die Werte liegen in Twips vor (1440 = 1 Zoll).
        constexpr qreal kTwipToMm = 25.4 / 1440.0;
        const SectionProps& sp = doc.section();
        writer.setPageSize(QPageSize(QSizeF(sp.pageW * kTwipToMm, sp.pageH * kTwipToMm),
                                     QPageSize::Millimeter, QString(),
                                     QPageSize::FuzzyMatch));
        writer.setPageMargins(QMarginsF(sp.marLeft * kTwipToMm, sp.marTop * kTwipToMm,
                                        sp.marRight * kTwipToMm, sp.marBottom * kTwipToMm),
                              QPageLayout::Millimeter);
        writer.setResolution(kResolution);
        writer.setTitle(QFileInfo(targetPath).completeBaseName());

        const qreal ptToPx = writer.resolution() / 72.0;
        //  Bedruckbare Breite — Bilder werden darauf eingepasst.
        const qreal printW =
            writer.pageLayout().paintRectPixels(writer.resolution()).width();

        //  QTextDocument aus dem Modell aufbauen (gleiche aufgelösten Formate
        //  wie die Editor-Anzeige). Seitenränder liefert der QPdfWriter.
        QTextDocument td;
        td.setDocumentMargin(0);
        const RunFmt def = doc.defaultRun();
        QFont base;
        base.setFamily(def.font);
        base.setPointSizeF(def.sizePt > 0 ? def.sizePt : 11.0);
        td.setDefaultFont(base);
        QTextOption to;
        to.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        to.setTextDirection(Qt::LayoutDirectionAuto);   // Arabisch: RTL automatisch
        td.setDefaultTextOption(to);

        //  Bedruckbare HÖHE — ein Bild, das höher ist, passt auf keine Seite:
        //  QTextDocument kann es nicht trennen, es liefe unten aus der Seite
        //  heraus und schöbe alles davor auf eine fast leere Seite.
        const QRectF paintRect =
            writer.pageLayout().paintRectPixels(writer.resolution());
        const qreal printH = paintRect.height();
        const qreal twToPx = writer.resolution() / 1440.0;

        //  Seitengröße und Metriken VOR dem Aufbau setzen: die Seitenzahlen des
        //  Inhaltsverzeichnisses werden am fertigen Layout abgelesen.
        td.documentLayout()->setPaintDevice(&writer);   // Metriken = Writer-DPI
        td.setPageSize(paintRect.size());

        //  ── Inhaltsverzeichnis ──────────────────────────────────────────────
        //  Die Einträge stehen NICHT in der Datei (das Feld bleibt deklarativ),
        //  die Seitenzahlen ergeben sich erst aus dem fertigen Layout. Deshalb
        //  wird das Dokument bei einem Verzeichnis ZWEIMAL gebaut: einmal, um
        //  die Seite jeder Überschrift zu messen, und einmal mit den Zahlen.
        const QList<TocEntry> tocEntries = doc.tocEntries();
        bool hasToc = false;
        for (const Block& b : doc.blocks)
            if (doc.isTocParagraph(b)) { hasToc = true; break; }
        QHash<int, int> headingPos;    // Docx-Block → Position in td
        QHash<int, int> headingPage;   // Docx-Block → 1-basierte Seite
        QSet<int> tocTargets;
        for (const TocEntry& e : tocEntries) tocTargets.insert(e.block);

        QTextCursor cur(&td);
        bool first = true;   // erster sichtbarer Block nutzt den Initial-Block
        //  Nach dem Verzeichnis fängt der Text auf der nächsten Seite an; und
        //  eine Seite wird nur umgebrochen, wenn davor überhaupt etwas steht.
        bool breakBeforeNext  = false;
        bool sawVisibleContent = false;
        auto beginBlock = [&](QTextCursor& c, bool& firstFlag,
                              const QTextBlockFormat& bf) {
            if (firstFlag) { c.setBlockFormat(bf); firstFlag = false; }
            else           { c.insertBlock(bf); }
        };

        QHash<int, int> counters;   // numId → laufende Nummer (Listen)
        const QTextCharFormat baseCf = charFormatFor(def, def);

        //  Ein Bild als QTextImageFormat, eingepasst auf den verfügbaren Platz:
        //  im Fließtext die Textbreite der Seite, in einer Tabelle die Breite
        //  IHRER Zelle. Ohne den Zell-Bezug bekäme ein Bild in einer schmalen
        //  Zelle die volle Seitenbreite und sprengte die Tabelle.
        auto imageFormatFor = [&](const InlineImage& info, qreal availW,
                                  QTextImageFormat* out) -> bool {
            const QByteArray bytes = doc.imageBytes(info.relId);
            QImage im;
            if (bytes.isEmpty() || !im.loadFromData(bytes) || im.isNull())
                return false;
            const QString name = QStringLiteral("mgdocx:/%1").arg(info.relId);
            td.addResource(QTextDocument::ImageResource, QUrl(name), QVariant(im));
            //  Sollmaß aus wp:extent (EMU), sonst native Pixel.
            constexpr qreal kEmuPerPx = 914400.0 / 96.0;
            qreal w = info.cxEmu > 0 ? info.cxEmu / kEmuPerPx : im.width();
            qreal h = info.cyEmu > 0 ? info.cyEmu / kEmuPerPx : im.height();
            w *= writer.resolution() / 96.0;
            h *= writer.resolution() / 96.0;
            if (w > availW && w > 0.0) { h *= availW / w; w = availW; }
            if (h > printH && h > 0.0) { w *= printH / h; h = printH; }
            out->setName(name);
            out->setWidth(qMax(1.0, w));
            out->setHeight(qMax(1.0, h));
            return true;
        };

        //  EINEN Absatz ausgeben — Fließtext wie Zellinhalt laufen hier durch.
        auto emitParagraph = [&](QTextCursor& c, bool& firstFlag, const Block& b,
                                 qreal availW, bool inCell, int blockIdx = -1) {
            const ParFmt pf = doc.resolvePar(b);
            QTextBlockFormat bf = blockFormatFor(pf, ptToPx);
            if (inCell) {   // Zellabsätze tragen keinen Abstand (wie die Anzeige)
                bf.setTopMargin(0);
                bf.setBottomMargin(0);
            }
            if (breakBeforeNext && !inCell) {
                bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
                breakBeforeNext = false;
            }
            if (b.textLength() > 0) sawVisibleContent = true;

            //  VERANKERTE Bilder (wp:anchor + w:wrapSquare) belegen keinen Platz
            //  in der Zeile — der Text fließt um sie herum. In Qt kann das nur
            //  ein FRAME (FloatLeft/FloatRight); ein QTextImageFormat wäre ein
            //  großes Zeichen IN der Zeile, der Text säße an seiner Unterkante
            //  und liefe darunter weiter — der Platz daneben bliebe leer
            //  (Nutzerbefund). Der Rahmen steht VOR dem Absatz; Qt lässt diesen
            //  und die folgenden daneben fließen, bis seine Unterkante erreicht
            //  ist. Die genaue Lage im Absatz (wp:positionV) bildet Qt nicht ab
            //  — nur die SEITE, wie in der Editor-Anzeige.
            QHash<int, InlineImage> imgRuns;   // Run-Index → Bild IN der Zeile
            for (const InlineImage& ii : doc.paragraphImages(b)) {
                if (!(ii.anchored && ii.wrap == InlineImage::WrapSquare)) {
                    imgRuns.insert(ii.run, ii);
                    continue;
                }
                QTextImageFormat ifmt;
                if (!imageFormatFor(ii, availW, &ifmt)) continue;
                constexpr qreal kEmuPerPx = 914400.0 / 96.0;
                const qreal padL = qBound(0.0, ii.distLEmu / kEmuPerPx, 40.0);
                const qreal padR = qBound(0.0, ii.distREmu / kEmuPerPx, 40.0);
                const qreal x = qBound(0.0, ii.posXEmu / kEmuPerPx,
                                       qMax(0.0, availW - ifmt.width()));
                //  Seite wie in der Anzeige: dorthin, wo weniger Rest bleibt.
                const bool leftSide = (x <= availW - (x + ifmt.width()));
                QTextFrameFormat ff;
                ff.setPosition(leftSide ? QTextFrameFormat::FloatLeft
                                        : QTextFrameFormat::FloatRight);
                ff.setWidth(QTextLength(QTextLength::FixedLength, ifmt.width()));
                ff.setBorder(0);
                ff.setPadding(0);
                ff.setTopMargin(0);
                ff.setBottomMargin(0);
                ff.setLeftMargin(leftSide ? 0.0 : padL);
                ff.setRightMargin(leftSide ? padR : 0.0);
                QTextFrame* fr = c.insertFrame(ff);
                QTextCursor fc = fr->firstCursorPosition();
                fc.insertImage(ifmt);
                //  Hinter dem Rahmen weiterschreiben: Qt legt dort einen leeren
                //  Absatz an — den nimmt dieser Absatz selbst.
                c = fr->lastCursorPosition();
                c.movePosition(QTextCursor::NextBlock);
                firstFlag = true;
            }

            beginBlock(c, firstFlag, bf);
            //  Lage merken, wenn ein Verzeichnis-Eintrag auf diesen Block zeigt
            //  — daraus wird im zweiten Durchgang seine Seitenzahl.
            if (blockIdx >= 0 && tocTargets.contains(blockIdx))
                headingPos.insert(blockIdx, c.position());

            const QString marker = markerFor(doc, pf, counters);
            if (!marker.isEmpty())
                c.insertText(marker, baseCf);

            //  Bilder dieses Absatzes an IHRER Stelle im Text ausgeben. Ohne
            //  das fiele die Zeichnung ersatzlos weg: der Text-Zweig entfernt
            //  das Objekt-Zeichen (sonst stünde dort ein Ersatzkästchen).
            //  QTextDocument kann Inline-Objekte selbst — zwei Bilder
            //  hintereinander landen also auch im PDF nebeneinander.
            for (int ri = 0; ri < b.runs.size(); ++ri) {
                const Run& r = b.runs.at(ri);
                if (r.text.isEmpty()) continue;
                const auto imgIt = imgRuns.constFind(ri);
                if (imgIt != imgRuns.constEnd()) {
                    QTextImageFormat ifmt;
                    if (imageFormatFor(imgIt.value(), availW, &ifmt)) {
                        c.insertImage(ifmt);
                        continue;
                    }
                }
                const QTextCharFormat cf = charFormatFor(doc.resolveRun(b, r), def);
                const QString t = r.text;
                int start = 0;
                for (int k = 0; k < t.size(); ++k) {
                    if (t.at(k) == kPageBreak) {
                        if (k > start) {
                            QString seg = t.mid(start, k - start);
                            seg.remove(kObjectChar);            // opake Objekte: kein Kästchen
                            if (!seg.isEmpty()) c.insertText(seg, cf);
                        }
                        //  Echter Seitenumbruch: neuer Block, Umbruch DAVOR.
                        //  In einer Zelle gibt es keine Seiten — dort bleibt es
                        //  bei einem gewöhnlichen Absatzwechsel.
                        QTextBlockFormat bf2 = bf;
                        if (!inCell)
                            bf2.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
                        c.insertBlock(bf2);
                        start = k + 1;
                    }
                }
                QString tail = t.mid(start);
                tail.remove(kObjectChar);
                //  U+2028 (kLineBreak) bleibt erhalten → QTextDocument rendert es
                //  als weichen Zeilenumbruch im Absatz; '\t' als Tabulator.
                if (!tail.isEmpty()) c.insertText(tail, cf);
            }
        };

        //  Das INHALTSVERZEICHNIS: je Eintrag eine Zeile aus Text · Punktreihe ·
        //  Seitenzahl, wie die Anzeige sie zeichnet. Ohne diesen Zweig fiel es
        //  ersatzlos weg — sein Feld-Run ist ein atomarer opaker Run, dessen
        //  Objekt-Zeichen der Text-Pfad entfernt (Nutzerbefund).
        auto emitToc = [&](const Block& b) {
            if (tocEntries.isEmpty()) return;   // leeres Verzeichnis: nichts zu drucken
            //  Schrift wie in der Anzeige: aufgelöstes Format der Absatzmarke.
            const RunFmt tfm = doc.paragraphMarkFormat(b);
            QFont f;
            f.setFamily(tfm.font.isEmpty() ? def.font : tfm.font);
            f.setPointSizeF(tfm.sizePt > 0 ? tfm.sizePt : 11.0);
            f.setBold(tfm.bold);
            f.setItalic(tfm.italic);
            QTextCharFormat cf;
            cf.setFont(f);
            cf.setForeground(QColor(30, 30, 30));
            const QFontMetricsF fm(f, &writer);
            const qreal dotW = qMax(1.0, fm.horizontalAdvance(QLatin1Char('.')));

            for (int ei = 0; ei < tocEntries.size(); ++ei) {
                const TocEntry& e = tocEntries.at(ei);
                const qreal indent = (e.level - 1) * 18.0 * (writer.resolution() / 96.0);
                QTextBlockFormat bf;
                bf.setAlignment(Qt::AlignLeft);
                bf.setLeftMargin(indent);
                //  Rechter Tabulator hält die Seitenzahl am Rand — ohne ihn
                //  müsste die Punktzahl auf den Pixel stimmen.
                bf.setTabPositions({ QTextOption::Tab(printW - indent,
                                                      QTextOption::RightTab) });
                //  Das Verzeichnis beginnt auf einer eigenen Seite — aber nur,
                //  wenn davor schon etwas steht (sonst bliebe Seite 1 leer,
                //  derselbe Fall wie in der Anzeige).
                if (ei == 0 && sawVisibleContent)
                    bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
                breakBeforeNext = false;   // gilt für DIESEN Block, s. oben
                beginBlock(cur, first, bf);

                const QString num =
                    QString::number(headingPage.value(e.block, 1));
                //  Punkte konservativ füllen: lieber eine Lücke als ein Umbruch.
                const qreal gap = printW - indent - fm.horizontalAdvance(e.text)
                                  - fm.horizontalAdvance(num) - 4.0 * dotW;
                const int dots = qBound(0, int(gap / dotW), 400);
                cur.insertText(e.text + QLatin1Char(' ')
                                   + QString(dots, QLatin1Char('.'))
                                   + QLatin1Char('\t') + num, cf);
            }
            sawVisibleContent = true;
            //  … und der Text danach fängt auf der nächsten Seite an.
            breakBeforeNext = true;
        };

        //  Eine FLACH zerlegte Tabelle als echte Tabelle ausgeben (ihre Zellen
        //  liegen als gewöhnliche Blöcke in `doc.blocks`, s. Docx-Modell). Ohne
        //  das erschienen sie im PDF als lose Absätze hintereinander — die
        //  Tabelle war schlicht weg (Nutzerbefund). Rückgabe: Index des letzten
        //  verbrauchten Blocks.
        auto emitTable = [&](int firstBlock) -> int {
            const int tid = doc.blocks.at(firstBlock).tableId;
            const TableDef& tdef = doc.tables().at(tid);
            const int rows = int(tdef.rowSpans.size());
            if (rows <= 0 || tdef.rowFirstCell.isEmpty()) return firstBlock;

            //  Zelle → Gitterspalte: w:gridSpan wird erst hier aufgelöst (im
            //  Modell ist `col` der laufende Index der Zelle in ihrer Zeile).
            auto lastCellOfRow = [&](int r) {
                return (r + 1 < tdef.rowFirstCell.size())
                           ? tdef.rowFirstCell.at(r + 1) - 1
                           : int(tdef.cellSpans.size()) - 1;
            };
            int gridCols = 1;
            for (int r = 0; r < rows; ++r) {
                int n = 0;
                for (int c2 = tdef.rowFirstCell.at(r); c2 <= lastCellOfRow(r); ++c2)
                    n += qMax(1, tdef.cellGridSpan.value(c2, 1));
                gridCols = qMax(gridCols, n);
            }

            //  Spaltenbreiten wie in der Anzeige: aus w:tblGrid, zu breite
            //  Tabellen proportional in die Textspalte eingepasst.
            //  **Der RAHMEN zählt mit.** Füllt das Gitter die Textspalte exakt
            //  aus (`w:tblGrid` = Textbreite, der Normalfall in Word), läge die
            //  rechte Linie GENAU auf der Kante der bedruckbaren Fläche — der
            //  Maler-Clip der Seitenschleife schnitte sie weg (Nutzerbefund:
            //  „ganz rechts fehlt die Linie"). Die Spalten bekommen deshalb nur
            //  den Platz zwischen den Linien.
            constexpr qreal kTableBorder = 1.0;
            const qreal usableW =
                qMax(20.0, printW - kTableBorder * (gridCols + 1));
            QVector<qreal> colPx;
            for (int w : tdef.gridTw) colPx.append(qMax(1.0, w * twToPx));
            while (colPx.size() < gridCols) colPx.append(usableW / gridCols);
            if (colPx.size() > gridCols) colPx.resize(gridCols);
            qreal sum = 0.0;
            for (qreal w : colPx) sum += w;
            if (sum > usableW && sum > 0.0) {
                const qreal f = usableW / sum;
                for (qreal& w : colPx) w *= f;
            }

            QTextTableFormat tf;
            QList<QTextLength> constraints;
            for (qreal w : colPx)
                constraints.append(QTextLength(QTextLength::FixedLength, w));
            tf.setColumnWidthConstraints(constraints);
            tf.setCellSpacing(0);
            tf.setCellPadding(kCellPadTw * twToPx);
            //  Gitterlinien wie in der Editor-Anzeige (w:tblBorders wird dort
            //  ebenso wenig gedeutet) — eine randlose Tabelle wäre im PDF von
            //  losen Absätzen nicht zu unterscheiden.
            tf.setBorder(kTableBorder);
            tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            tf.setBorderBrush(QColor(150, 150, 150));
            tf.setBorderCollapse(true);
            tf.setTopMargin(4 * ptToPx);
            tf.setBottomMargin(4 * ptToPx);
            if (breakBeforeNext) {
                tf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
                breakBeforeNext = false;
            }
            sawVisibleContent = true;
            QTextTable* t = cur.insertTable(rows, gridCols, tf);
            if (!t) return firstBlock;

            //  Erst verbinden, dann füllen — cellAt() liefert danach für jede
            //  überdeckte Spalte dieselbe (verbundene) Zelle.
            for (int r = 0; r < rows; ++r) {
                int gc = 0;
                for (int c2 = tdef.rowFirstCell.at(r);
                     c2 <= lastCellOfRow(r) && gc < gridCols; ++c2) {
                    const int span = qBound(1, tdef.cellGridSpan.value(c2, 1),
                                            gridCols - gc);
                    if (span > 1) t->mergeCells(r, gc, 1, span);
                    gc += span;
                }
            }

            const qreal padPx = kCellPadTw * twToPx;
            int bi = firstBlock;
            for (int r = 0; r < rows; ++r) {
                int gc = 0;
                for (int c2 = tdef.rowFirstCell.at(r);
                     c2 <= lastCellOfRow(r) && gc < gridCols; ++c2) {
                    const int span = qBound(1, tdef.cellGridSpan.value(c2, 1),
                                            gridCols - gc);
                    qreal cw = 0.0;
                    for (int k = 0; k < span && gc + k < colPx.size(); ++k)
                        cw += colPx.at(gc + k);
                    cw = qMax(20.0, cw - 2.0 * padPx);

                    const int colOfCell = c2 - tdef.rowFirstCell.at(r);
                    QTextTableCell cell = t->cellAt(r, gc);
                    if (cell.isValid()) {
                        QTextCursor cc = cell.firstCursorPosition();
                        bool firstInCell = true;
                        while (bi < int(doc.blocks.size())
                               && doc.blocks.at(bi).tableId == tid
                               && doc.blocks.at(bi).row == r
                               && doc.blocks.at(bi).col == colOfCell) {
                            emitParagraph(cc, firstInCell, doc.blocks.at(bi), cw, true);
                            ++bi;
                        }
                    }
                    gc += span;
                }
            }

            //  Hinter der Tabelle weiterschreiben — Qt legt dort einen leeren
            //  Absatz an, den nimmt der nächste Block.
            cur = t->lastCursorPosition();
            cur.movePosition(QTextCursor::NextBlock);
            first = true;
            //  Nie hinter den Startblock zurückfallen: sonst liefe die Schleife
            //  des Aufrufers auf derselben Tabelle endlos.
            return qMax(bi - 1, firstBlock);
        };

        //  EIN Aufbau-Durchgang. Bei einem Inhaltsverzeichnis läuft er zweimal:
        //  der erste liefert die Seiten der Überschriften, der zweite schreibt
        //  sie in die Einträge.
        auto buildPass = [&]() {
        td.clear();
        td.setDocumentMargin(0);
        td.setDefaultFont(base);
        td.setDefaultTextOption(to);
        cur = QTextCursor(&td);
        first = true;
        breakBeforeNext = false;
        sawVisibleContent = false;
        counters.clear();
        headingPos.clear();

        for (int bi = 0; bi < int(doc.blocks.size()); ++bi) {
            const Block& b = doc.blocks.at(bi);
            if (b.kind == Block::OpaqueHidden)
                continue;   // sectPr/bookmarks u. Ä. — im Dokument intakt, unsichtbar

            //  Flach zerlegte Tabelle: ihre Zellblöcke auf einmal.
            if (b.kind == Block::Paragraph && b.tableId >= 0
                && b.tableId < int(doc.tables().size())) {
                bi = emitTable(bi);
                continue;
            }

            if (b.kind == Block::Paragraph && doc.isTocParagraph(b)) {
                emitToc(b);
                continue;
            }

            if (b.kind == Block::OpaqueVisible) {
                //  Platzhalter — wie im Editor eine dezente, kursive
                //  Hinweiszeile. Betrifft nur, was NICHT zerlegt werden konnte
                //  (verschachtelte Tabelle, w:sdt, w:altChunk); der Inhalt
                //  bleibt in der .docx erhalten.
                QTextBlockFormat pbf;
                pbf.setAlignment(Qt::AlignHCenter);
                pbf.setTopMargin(6 * ptToPx);
                pbf.setBottomMargin(6 * ptToPx);
                beginBlock(cur, first, pbf);
                QTextCharFormat pcf;
                QFont pf; pf.setFamily(def.font); pf.setPointSizeF(9.5); pf.setItalic(true);
                pcf.setFont(pf);
                pcf.setForeground(QColor(110, 110, 110));
                cur.insertText(tableLabel, pcf);
                continue;
            }

            emitParagraph(cur, first, b, printW, false, bi);
        }
        };   // buildPass

        buildPass();
        if (hasToc && !tocEntries.isEmpty()) {
            //  Seite jeder Überschrift am fertigen Layout ablesen und noch
            //  einmal bauen. Die Zeilenzahl des Verzeichnisses ändert sich
            //  dabei nicht (nur die Zahl am Zeilenende), der zweite Durchgang
            //  verschiebt also nichts mehr.
            for (auto it = headingPos.cbegin(); it != headingPos.cend(); ++it) {
                const QTextBlock tb = td.findBlock(it.value());
                if (!tb.isValid()) continue;
                qreal top = td.documentLayout()->blockBoundingRect(tb).top();
                //  Die ERSTE ZEILE zählt, nicht der Blockanfang: passt die Zeile
                //  nicht mehr auf die Seite (hohes Bild im selben Absatz),
                //  schiebt Qt sie per Zwischenraum auf die nächste, während der
                //  Block noch auf der alten beginnt. Ohne das war die Seitenzahl
                //  im Verzeichnis um eins zu klein.
                if (const QTextLayout* lay = tb.layout())
                    if (lay->lineCount() > 0) top += lay->lineAt(0).y();
                headingPage.insert(it.key(),
                                   int(top / qMax(1.0, paintRect.height())) + 1);
            }
            buildPass();
        }

        //  SELBST paginieren statt QTextDocument::print(): print() skaliert den
        //  Inhalt mit dem Verhältnis Geräte-DPI zu Dokument-DPI — und die
        //  Dokument-DPI kommt mangels Zeichengerät vom BILDSCHIRM. Auf einem
        //  HiDPI-Schirm schrumpfte der Text dadurch auf ~75 % und die Ränder
        //  wuchsen entsprechend (gemessen: 45 mm statt 25 mm, Textbreite 119
        //  statt 160 mm). Mit eigenem Paginieren hängt die Ausgabe an nichts
        //  außer dem Dokument. Seitengröße und Metriken stehen schon (s. oben).
        QPainter p(&writer);
        const int pages = qMax(1, td.pageCount());
        for (int pg = 0; pg < pages; ++pg) {
            if (pg > 0) writer.newPage();
            p.save();
            p.translate(0, -pg * paintRect.height());
            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.clip = QRectF(0, pg * paintRect.height(),
                              paintRect.width(), paintRect.height());
            //  `ctx.clip` WÄHLT nur aus, was gezeichnet wird — geklippt wird
            //  nichts. Ein Block, der die Seitengrenze schneidet (ein hohes
            //  Bild), wurde deshalb ganz gemalt und ragte als Streifen in die
            //  Nachbarseite. Der Maler-Clip schneidet ihn wirklich ab.
            p.setClipRect(ctx.clip);
            ctx.palette.setColor(QPalette::Text, Qt::black);
            td.documentLayout()->draw(&p, ctx);
            p.restore();
        }
        p.end();
    }   // QPdfWriter zerstört → PDF finalisiert (Trailer) in den QSaveFile-Puffer

    if (!out.commit()) {
        if (err) *err = QStringLiteral("Schreiben fehlgeschlagen.");
        return false;
    }
    return true;
}

} // namespace DocxPdf
