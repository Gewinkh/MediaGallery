#include "DocxPdfExporter.h"
#include "DocxDocument.h"

#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextOption>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QMarginsF>
#include <QSaveFile>
#include <QFileInfo>
#include <QFont>
#include <QColor>
#include <QHash>
#include <QRegularExpression>

using namespace Docx;

namespace {

//  Punkt → Layout-Pixel: identisch zur Editor-Anzeige (DocxTextArea kPtToPx),
//  hier relativ zur tatsächlichen Writer-Auflösung berechnet, damit Abstände
//  auflösungsunabhängig korrekt sind. Schriftgrößen bleiben in Punkt (physisch
//  korrekt, unabhängig von der Auflösung).
constexpr qreal kListIndentPt = 21.0;   // ≈ 28 px @96 dpi (Editor: kListIndent)
constexpr int   kResolution   = 96;     // Layout-DPI (wie die Editor-Anzeige)

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
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(25, 25, 25, 25), QPageLayout::Millimeter);
        writer.setResolution(kResolution);
        writer.setTitle(QFileInfo(targetPath).completeBaseName());

        const qreal ptToPx = writer.resolution() / 72.0;

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

        QTextCursor cur(&td);
        bool first = true;   // erster sichtbarer Block nutzt den Initial-Block
        auto beginBlock = [&](const QTextBlockFormat& bf) {
            if (first) { cur.setBlockFormat(bf); first = false; }
            else       { cur.insertBlock(bf); }
        };

        QHash<int, int> counters;   // numId → laufende Nummer (Listen)
        const QTextCharFormat baseCf = charFormatFor(def, def);

        for (const Block& b : doc.blocks) {
            if (b.kind == Block::OpaqueHidden)
                continue;   // sectPr/bookmarks u. Ä. — im Dokument intakt, unsichtbar

            if (b.kind == Block::OpaqueVisible) {
                //  Platzhalter (Tabelle & Co.) — wie im Editor eine dezente,
                //  kursive Hinweiszeile; der Inhalt bleibt in der .docx erhalten.
                QTextBlockFormat pbf;
                pbf.setAlignment(Qt::AlignHCenter);
                pbf.setTopMargin(6 * ptToPx);
                pbf.setBottomMargin(6 * ptToPx);
                beginBlock(pbf);
                QTextCharFormat pcf;
                QFont pf; pf.setFamily(def.font); pf.setPointSizeF(9.5); pf.setItalic(true);
                pcf.setFont(pf);
                pcf.setForeground(QColor(110, 110, 110));
                cur.insertText(tableLabel, pcf);
                continue;
            }

            //  Absatz.
            const ParFmt pf = doc.resolvePar(b);
            const QTextBlockFormat bf = blockFormatFor(pf, ptToPx);
            beginBlock(bf);

            const QString marker = markerFor(doc, pf, counters);
            if (!marker.isEmpty())
                cur.insertText(marker, baseCf);

            for (const Run& r : b.runs) {
                if (r.text.isEmpty()) continue;
                const QTextCharFormat cf = charFormatFor(doc.resolveRun(b, r), def);
                const QString t = r.text;
                int start = 0;
                for (int k = 0; k < t.size(); ++k) {
                    if (t.at(k) == kPageBreak) {
                        if (k > start) {
                            QString seg = t.mid(start, k - start);
                            seg.remove(kObjectChar);            // opake Objekte: kein Kästchen
                            if (!seg.isEmpty()) cur.insertText(seg, cf);
                        }
                        //  Echter Seitenumbruch: neuer Block, Umbruch DAVOR.
                        QTextBlockFormat bf2 = bf;
                        bf2.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
                        cur.insertBlock(bf2);
                        start = k + 1;
                    }
                }
                QString tail = t.mid(start);
                tail.remove(kObjectChar);
                //  U+2028 (kLineBreak) bleibt erhalten → QTextDocument rendert es
                //  als weichen Zeilenumbruch im Absatz; '\t' als Tabulator.
                if (!tail.isEmpty()) cur.insertText(tail, cf);
            }
        }

        //  Paginiert nach A4 schreiben (Qt-Text-Engine übernimmt Umbruch/Ränder).
        td.print(&writer);
    }   // QPdfWriter zerstört → PDF finalisiert (Trailer) in den QSaveFile-Puffer

    if (!out.commit()) {
        if (err) *err = QStringLiteral("Schreiben fehlgeschlagen.");
        return false;
    }
    return true;
}

} // namespace DocxPdf
