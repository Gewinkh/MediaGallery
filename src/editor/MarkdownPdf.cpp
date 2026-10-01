#include "editor/MarkdownPdf.h"

#include "core/PdfGlyphRuns.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextFrame>
#include <memory>

namespace mg::editor::md {
namespace {

constexpr int   kResolution = 96;      // 1 px der Ansicht = 1 px im PDF
constexpr qreal kMarginMm   = 20.0;
constexpr qreal kFooterPt   = 8.0;

QColor tint(const QColor& base, const QColor& t, qreal a) {
    return QColor::fromRgbF(float(base.redF() + (t.redF() - base.redF()) * a),
                            float(base.greenF() + (t.greenF() - base.greenF()) * a),
                            float(base.blueF() + (t.blueF() - base.blueF()) * a));
}

struct Deko {
    enum Art { Code, Meta } art;
    QRectF rect;
    qreal header = 0;
    QString language;
};

QList<Deko> dekorationen(QTextDocument& doc) {
    QList<Deko> out;
    QAbstractTextDocumentLayout* lay = doc.documentLayout();
    QList<QTextFrame*> offen{doc.rootFrame()};
    while (!offen.isEmpty()) {
        QTextFrame* f = offen.takeFirst();
        const QList<QTextFrame*> kinder = f->childFrames();
        for (int i = int(kinder.size()) - 1; i >= 0; --i) offen.prepend(kinder[i]);
        const QTextFrameFormat ff = f->frameFormat();
        const QRectF r = lay->frameBoundingRect(f);
        //  Dieselbe Geometrie wie `MarkdownView::codeBlocks`: oberer Aussenabstand = Kopfleiste, linker = Einzug.
        if (ff.hasProperty(kCodeTextProperty))
            out.append({Deko::Code,
                        QRectF(r.x() + ff.leftMargin(), r.y(), r.width() - ff.leftMargin() - ff.rightMargin(),
                               r.height() - ff.bottomMargin()),
                        ff.topMargin(), ff.stringProperty(kCodeLangProperty)});
        else if (ff.hasProperty(kMetaProperty))
            out.append({Deko::Meta, r, 0, {}});
    }
    return out;
}

// Gezeichnet wie in `MarkdownSurface.qml`; im Druckstil nur Linien, keine Flaeche.
void maleDeko(QPainter& p, const Deko& d, const RenderStyle& st, bool druck) {
    const SyntaxPalette& pal = st.palette;
    const QColor linie = druck ? QColor(150, 150, 150) : QColor::fromRgbF(pal.text.redF(), pal.text.greenF(),
                                                                          pal.text.blueF(), 0.13f);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = d.rect.adjusted(0.5, 0.5, -0.5, -0.5);
    if (d.art == Deko::Meta) {
        p.setPen(QPen(druck ? QColor(170, 170, 170) : linie, 1));
        p.setBrush(druck ? QBrush(Qt::NoBrush) : QBrush(tint(pal.background, pal.text, 0.035)));
        p.drawRoundedRect(r, 8, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(druck ? QColor(130, 130, 130) : pal.colorFor(Tok::Link));
        p.drawRoundedRect(QRectF(d.rect.x() + 12, d.rect.y() + 12, 3, d.rect.height() - 24), 1.5, 1.5);
        p.restore();
        return;
    }

    const bool hell = pal.background.lightnessF() > 0.5;
    const QColor grund = hell ? pal.background.darker(105) : pal.background.darker(150);
    const QColor kopf = tint(grund, pal.text, 0.07);
    p.setPen(QPen(linie, druck ? 0.75 : 1));
    p.setBrush(druck ? QBrush(Qt::NoBrush) : QBrush(grund));
    p.drawRoundedRect(r, 8, 8);
    if (!druck) {
        //  Oben rund, unten gerade - wie die beiden Rechtecke der Oberflaeche.
        QPainterPath pfad;
        pfad.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, 0), 7.5, 7.5);
        p.save();
        p.setClipRect(QRectF(d.rect.x(), d.rect.y(), d.rect.width(), d.header));
        p.fillPath(pfad, kopf);
        p.restore();
        const QRgb punkte[] = {0xff5f57, 0xfebc2e, 0x28c840};
        p.setPen(Qt::NoPen);
        p.setOpacity(0.85);
        for (int i = 0; i < 3; ++i) {
            p.setBrush(QColor(punkte[i]));
            p.drawEllipse(QRectF(d.rect.x() + 12 + i * 16, d.rect.y() + qRound((d.header - 10) / 2), 10, 10));
        }
        p.setOpacity(1);
    }
    p.setPen(QPen(druck ? QColor(200, 200, 200) : linie, druck ? 0.75 : 1));
    p.drawLine(QPointF(d.rect.x() + 1, d.rect.y() + d.header), QPointF(d.rect.right() - 1, d.rect.y() + d.header));
    if (!d.language.isEmpty()) {
        QFont f = st.mono;
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(druck ? QColor(90, 90, 90) : pal.gutterText);
        p.drawText(QRectF(d.rect.x() + (druck ? 12 : 66), d.rect.y(), d.rect.width(), d.header),
                   Qt::AlignLeft | Qt::AlignVCenter, d.language);
    }
    p.restore();
}

}  // namespace

SyntaxPalette printPalette() {
    SyntaxPalette p;
    p.name = QStringLiteral("Druck");
    p.background = Qt::white;
    p.text = Qt::black;
    p.gutterText = QColor(90, 90, 90);
    for (QColor& c : p.tok) c = Qt::black;
    p.tok[int(Tok::Comment)] = QColor(110, 110, 110);
    p.tok[int(Tok::String)] = QColor(70, 70, 70);
    p.tok[int(Tok::Number)] = QColor(70, 70, 70);
    p.tok[int(Tok::Operator)] = QColor(60, 60, 60);
    p.tok[int(Tok::CodeSpan)] = QColor(40, 40, 40);
    return p;
}

int writePdf(const Document& doc, RenderStyle style, const PdfOptions& opt, const QString& target,
             QString* err, const std::atomic<bool>* abort) {
    auto fehler = [err](const QString& t) { if (err) *err = t; return -1; };
    auto abgebrochen = [abort] { return abort && abort->load(std::memory_order_relaxed); };
    if (opt.print) style.palette = printPalette();

    QByteArray bytes;
    QBuffer sink(&bytes);
    sink.open(QIODevice::WriteOnly);
    //  Zeiger, damit er vor dem Zusammenfassen stirbt: erst sein Ende schreibt den Abschluss der Datei.
    auto schreiber = std::make_unique<QPdfWriter>(&sink);
    QPdfWriter& writer = *schreiber;
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(opt.landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    //  Keine Raender im Schreiber: sonst endete die Papierfarbe an der Randkante statt am Blattrand.
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    writer.setResolution(kResolution);
    if (!target.isEmpty()) writer.setTitle(QFileInfo(target).completeBaseName());

    const QRectF blatt = writer.pageLayout().paintRectPixels(writer.resolution());
    const qreal rand = kMarginMm / 25.4 * kResolution;
    const qreal breite = qMax(1.0, blatt.width() - 2 * rand);
    QFont fussSchrift = style.body;
    fussSchrift.setPointSizeF(kFooterPt);
    const qreal fussH = QFontMetricsF(fussSchrift, &writer).height() * 1.8;
    const qreal hoehe = qMax(1.0, blatt.height() - 2 * rand - fussH);

    std::unique_ptr<QTextDocument> td(buildDocument(doc, style, abort));
    if (!td || abgebrochen()) return fehler(QStringLiteral("Abgebrochen."));
    td->setDocumentMargin(0);
    td->documentLayout()->setPaintDevice(&writer);
    //  Qt umbricht selbst und schiebt Zeilen, Tabellenzeilen und Bilder, die nicht mehr passen, auf die naechste Seite.
    td->setPageSize(QSizeF(breite, hoehe));
    const int seiten = qMax(1, td->pageCount());
    if (target.isEmpty()) return seiten;

    const int von = qBound(1, opt.firstPage, seiten);
    const int bis = opt.lastPage <= 0 ? seiten : qBound(von, opt.lastPage, seiten);
    const QList<Deko> deko = dekorationen(*td);
    const QColor tinte = style.palette.text;

    QPainter p;
    if (!p.begin(&writer)) return fehler(QStringLiteral("PDF nicht anlegbar."));
    for (int s = von; s <= bis; ++s) {
        if (abgebrochen()) return fehler(QStringLiteral("Abgebrochen."));
        if (s > von) writer.newPage();
        //  Zwei Pixel ueber das Blatt hinaus: `paintRectPixels` rundet.
        if (!opt.print) p.fillRect(blatt.adjusted(-2, -2, 2, 2), style.palette.background);

        const qreal oben = (s - 1) * hoehe;
        p.save();
        p.translate(rand, rand - oben);
        //  Winziger Einzug: Qt legt eine Zeile bis 0,009 px ueber die Seitengrenze, sie stuende sonst auf
        //  beiden Seiten in der Textschicht.
        constexpr qreal kEps = 0.05;
        const QRectF band(0, oben + kEps, breite, hoehe - 2 * kEps);
        p.setClipRect(band);
        for (const Deko& d : deko)
            if (d.rect.intersects(band)) maleDeko(p, d, style, opt.print);
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.clip = band;
        ctx.palette.setColor(QPalette::Text, tinte);
        td->documentLayout()->draw(&p, ctx);
        p.restore();

        p.setFont(fussSchrift);
        QColor fuss = tinte;
        fuss.setAlphaF(0.55f);
        p.setPen(fuss);
        p.drawText(QRectF(rand, blatt.height() - rand - fussH, breite, fussH), Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("%1/%2").arg(s).arg(seiten));
    }
    p.end();
    td.reset();
    schreiber.reset();
    sink.close();

    //  Qt schreibt ein Textobjekt je Glyphe; zusammengefasst findet die Suche im PDF ganze Woerter.
    const QByteArray fertig = mg::pdfglyphs::mergeGlyphRuns(bytes);
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(fertig) != fertig.size() || !out.commit())
        return fehler(QStringLiteral("Schreiben fehlgeschlagen."));
    return seiten;
}

}  // namespace mg::editor::md
