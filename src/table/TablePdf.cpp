#include "table/TablePdf.h"

#include "core/PdfGlyphRuns.h"
#include "table/TableSort.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <cmath>

namespace mg::table {
namespace {

constexpr int   kResolution = 96;
constexpr qreal kMarginMm   = 15.0;
constexpr qreal kFooterPt   = 8.0;
constexpr qreal kMinMass    = 0.7;     // 12 px Zellschrift -> nicht unter rund 6,3 pt
constexpr int   kProbe      = 500;     // Breite aus den ersten Zeilen, wie in der Ansicht
constexpr qreal kPad        = 6;
constexpr qreal kMaxSpalte  = 320;
constexpr qreal kMinSpalte  = 36;

// Spalten, die zusammen auf eine Seitenbreite passen; mindestens eine je Gruppe.
QList<QList<int>> gruppen(const QList<qreal>& breiten, qreal platz) {
    QList<QList<int>> out;
    qreal summe = 0;
    for (int i = 0; i < breiten.size(); ++i) {
        if (out.isEmpty() || (summe + breiten[i] > platz && !out.last().isEmpty())) {
            out.append(QList<int>());
            summe = 0;
        }
        out.last().append(i);
        summe += breiten[i];
    }
    if (out.isEmpty()) out.append(QList<int>());
    return out;
}

// Im Druck bleibt nur die Helligkeit, und auch die nur zu einem Drittel: eine dunkle Spalte kostete sonst Toner.
QColor druckGrund(const QString& farbe) {
    const QColor c(farbe);
    if (!c.isValid()) return {};
    const int g = 255 - int((255 - qGray(c.rgb())) * 0.3);
    return QColor(g, g, g);
}

struct Seite {
    int teil;
    int gruppe;
    int ersteZeile;
    int zeilen;
};

}  // namespace

int schreibePdf(const QList<PdfTeil>& teile, const PdfOptionen& opt, const QString& ziel, QString* err,
                const std::atomic<bool>* abbruch) {
    auto fehler = [err](const QString& t) { if (err) *err = t; return -1; };
    auto abgebrochen = [abbruch] { return abbruch && abbruch->load(std::memory_order_relaxed); };

    QByteArray bytes;
    QBuffer sink(&bytes);
    sink.open(QIODevice::WriteOnly);
    //  Zeiger, damit er vor dem Zusammenfassen stirbt: erst sein Ende schreibt den Abschluss der Datei.
    auto schreiber = std::make_unique<QPdfWriter>(&sink);
    QPdfWriter& writer = *schreiber;
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(opt.quer ? QPageLayout::Landscape : QPageLayout::Portrait);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    writer.setResolution(kResolution);
    if (!ziel.isEmpty()) writer.setTitle(QFileInfo(ziel).completeBaseName());

    const QRectF blatt = writer.pageLayout().paintRectPixels(writer.resolution());
    const qreal rand = kMarginMm / 25.4 * kResolution;
    QFont fussSchrift = opt.schrift;
    fussSchrift.setPointSizeF(kFooterPt);
    fussSchrift.setBold(false);
    const qreal fussH = QFontMetricsF(fussSchrift, &writer).height() * 1.8;
    const qreal platzB = qMax(1.0, blatt.width() - 2 * rand);
    const qreal platzH = qMax(1.0, blatt.height() - 2 * rand - fussH);

    QFont zell = opt.schrift;
    QFont fett = zell;
    fett.setBold(true);
    QFont titelSchrift = zell;
    titelSchrift.setBold(true);
    titelSchrift.setPixelSize(qMax(1, int(zell.pixelSize() > 0 ? zell.pixelSize() * 1.3 : 16)));
    const QFontMetricsF fm(zell, &writer);
    const QFontMetricsF fmFett(fett, &writer);
    const qreal zeileH = std::ceil(fm.height() + 6);
    const qreal kopfH = std::ceil(fmFett.height() + 8);
    const qreal titelH = std::ceil(QFontMetricsF(titelSchrift, &writer).height() + 10);

    //  Je Teil: Breiten, Masstab, Spaltengruppen - und daraus die Seiten.
    struct Plan {
        QList<qreal> breiten;
        qreal mass = 1;
        QList<QList<int>> gruppen;
    };
    QList<Plan> plaene;
    QList<Seite> seiten;
    for (int t = 0; t < teile.size(); ++t) {
        const PdfTeil& teil = teile[t];
        Plan p;
        const int probe = qMin(teil.zeilen, kProbe);
        for (int s = 0; s < teil.spalten.size(); ++s) {
            const PdfSpalte& sp = teil.spalten[s];
            qreal b = sp.px;
            if (b <= 0) {
                b = fmFett.horizontalAdvance(sp.titel);
                const QFontMetricsF& m = sp.format.fett ? fmFett : fm;
                for (int z = 0; z < probe; ++z) b = qMax(b, m.horizontalAdvance(teil.zelle(z, s)));
                b = qBound(kMinSpalte, std::ceil(b + 2 * kPad + 1), kMaxSpalte);
            }
            p.breiten.append(b);
        }
        qreal gesamt = 0;
        for (qreal b : std::as_const(p.breiten)) gesamt += b;
        if (gesamt > platzB) p.mass = qMax(kMinMass, platzB / gesamt);
        p.gruppen = gruppen(p.breiten, platzB / p.mass);

        const qreal hoehe = platzH / p.mass;
        const bool mitTitel = !teil.titel.isEmpty();
        for (int g = 0; g < p.gruppen.size(); ++g) {
            int ab = 0;
            bool erste = true;
            do {
                const qreal frei = hoehe - kopfH - (erste && mitTitel ? titelH : 0);
                const int n = qMax(1, int(frei / zeileH));
                const int hier = qMin(n, teil.zeilen - ab);
                seiten.append({t, g, ab, qMax(0, hier)});
                ab += qMax(0, hier);
                erste = false;
            } while (ab < teil.zeilen);
        }
        plaene.append(std::move(p));
    }
    if (seiten.isEmpty()) seiten.append({-1, 0, 0, 0});
    const int gesamtSeiten = int(seiten.size());
    if (ziel.isEmpty()) return gesamtSeiten;

    const int von = qBound(1, opt.von, gesamtSeiten);
    const int bis = opt.bis <= 0 ? gesamtSeiten : qBound(von, opt.bis, gesamtSeiten);

    const QColor papier = opt.druck ? QColor(Qt::white) : opt.grund;
    const QColor tinte = opt.druck ? QColor(Qt::black) : opt.text;
    const QColor kopfGrund = opt.druck ? QColor(232, 232, 232) : opt.kopfGrund;
    const QColor kopfTinte = opt.druck ? QColor(Qt::black) : opt.kopfText;
    const QColor linie = opt.druck ? QColor(160, 160, 160)
                                   : QColor::fromRgbF(opt.text.redF(), opt.text.greenF(), opt.text.blueF(), 0.2f);
    const QColor streifen = QColor::fromRgbF(opt.text.redF(), opt.text.greenF(), opt.text.blueF(), 0.05f);
    const bool gitter = opt.druck || opt.gitter;

    QPainter p;
    if (!p.begin(&writer)) return fehler(QStringLiteral("PDF nicht anlegbar."));
    for (int nr = von; nr <= bis; ++nr) {
        if (abgebrochen()) return fehler(QStringLiteral("Abgebrochen."));
        if (nr > von) writer.newPage();
        if (!opt.druck && papier.isValid()) p.fillRect(blatt.adjusted(-2, -2, 2, 2), papier);

        const Seite& se = seiten[nr - 1];
        if (se.teil >= 0) {
            const PdfTeil& teil = teile[se.teil];
            const Plan& plan = plaene[se.teil];
            const QList<int>& spalten = plan.gruppen[se.gruppe];
            p.save();
            p.translate(rand, rand);
            p.scale(plan.mass, plan.mass);
            qreal y = 0;
            if (se.ersteZeile == 0 && !teil.titel.isEmpty()) {
                p.setFont(titelSchrift);
                p.setPen(tinte);
                p.drawText(QRectF(0, 0, platzB / plan.mass, titelH - 4), Qt::AlignLeft | Qt::AlignVCenter,
                           teil.titel);
                y = titelH;
            }
            qreal breite = 0;
            for (int s : spalten) breite += plan.breiten[s];

            //  Kopf
            p.fillRect(QRectF(0, y, breite, kopfH), kopfGrund);
            p.setFont(fett);
            p.setPen(kopfTinte);
            qreal x = 0;
            for (int s : spalten) {
                const qreal b = plan.breiten[s];
                p.drawText(QRectF(x + kPad, y, b - 2 * kPad, kopfH), Qt::AlignLeft | Qt::AlignVCenter,
                           fmFett.elidedText(teil.spalten[s].titel, Qt::ElideRight, b - 2 * kPad));
                x += b;
            }
            const qreal kopfUnten = y + kopfH;
            y = kopfUnten;

            //  Zeilen
            for (int z = 0; z < se.zeilen; ++z) {
                const int zeile = se.ersteZeile + z;
                if (!gitter && zeile % 2 == 1) p.fillRect(QRectF(0, y, breite, zeileH), streifen);
                x = 0;
                for (int s : spalten) {
                    const qreal b = plan.breiten[s];
                    const PdfSpalte& sp = teil.spalten[s];
                    const QColor grund = opt.druck ? druckGrund(sp.format.hintergrund)
                                                   : QColor(sp.format.hintergrund);
                    if (grund.isValid()) p.fillRect(QRectF(x, y, b, zeileH), grund);
                    QColor farbe = tinte;
                    if (!opt.druck) {
                        if (!sp.format.farbe.isEmpty()) farbe = QColor(sp.format.farbe);
                        else if (!sp.format.hintergrund.isEmpty()) farbe = QColor(lesbarAuf(sp.format.hintergrund));
                    }
                    p.setPen(farbe);
                    p.setFont(sp.format.fett ? fett : zell);
                    const QFontMetricsF& m = sp.format.fett ? fmFett : fm;
                    const Qt::Alignment ausrichtung =
                        (opt.druck && sp.zahl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
                    p.drawText(QRectF(x + kPad, y, b - 2 * kPad, zeileH), ausrichtung,
                               m.elidedText(teil.zelle(zeile, s), Qt::ElideRight, b - 2 * kPad));
                    x += b;
                }
                y += zeileH;
            }

            //  Linien zuletzt, damit keine Flaeche sie verdeckt.
            p.setPen(QPen(linie, gitter ? 0.75 : 1));
            p.setBrush(Qt::NoBrush);
            if (gitter) {
                for (qreal ly = kopfUnten; ly <= y + 0.01; ly += zeileH) p.drawLine(QPointF(0, ly), QPointF(breite, ly));
                p.drawRect(QRectF(0, kopfUnten - kopfH, breite, y - kopfUnten + kopfH));
            } else {
                p.drawLine(QPointF(0, kopfUnten), QPointF(breite, kopfUnten));
            }
            x = 0;
            for (int i = 0; i + 1 < spalten.size(); ++i) {
                x += plan.breiten[spalten[i]];
                p.drawLine(QPointF(x, kopfUnten - kopfH), QPointF(x, gitter ? y : kopfUnten));
            }
            p.restore();
        }

        p.setFont(fussSchrift);
        QColor fuss = tinte;
        fuss.setAlphaF(0.55f);
        p.setPen(fuss);
        p.drawText(QRectF(rand, blatt.height() - rand - fussH, platzB, fussH), Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("%1/%2").arg(nr).arg(gesamtSeiten));
    }
    p.end();
    schreiber.reset();
    sink.close();

    const QByteArray fertig = mg::pdfglyphs::mergeGlyphRuns(bytes);
    QSaveFile out(ziel);
    if (!out.open(QIODevice::WriteOnly) || out.write(fertig) != fertig.size() || !out.commit())
        return fehler(QStringLiteral("Schreiben fehlgeschlagen."));
    return gesamtSeiten;
}

QList<PdfTeil> teileAusDatei(std::shared_ptr<const Datei> datei, const QList<Bereich>& bloecke,
                             std::shared_ptr<const Werte> werte, const QHash<int, int>& breiten,
                             const QHash<int, SpaltenFormat>& formate, bool gruppiert, bool komma) {
    QList<PdfTeil> out;
    if (!datei) return out;
    const QList<Zeile>& zeilen = datei->zeilen;
    QList<Bereich> liste = bloecke;
    if (liste.isEmpty()) liste.append({0, int(zeilen.size()), -1, -1, 0});
    for (const Bereich& b : std::as_const(liste)) {
        PdfTeil t;
        if (liste.size() > 1) {
            if (b.titel >= 0) t.titel = zeilen.at(b.titel).wert(0);
            if (t.titel.isEmpty() && b.kopf >= 0) t.titel = zeilen.at(b.kopf).wert(0);
            if (t.titel.isEmpty()) t.titel = QStringLiteral("%1").arg(out.size() + 1);
        }
        int spaltenZahl = b.kopf >= 0 ? zeilen.at(b.kopf).felder() : 0;
        for (int z = b.daten; z < b.bis; ++z) spaltenZahl = qMax(spaltenZahl, zeilen.at(z).felder());
        for (int s = 0; s < spaltenZahl; ++s) {
            PdfSpalte sp;
            sp.titel = b.kopf >= 0 ? zeilen.at(b.kopf).wert(s) : QString();
            sp.px = breiten.value(s, 0);
            sp.zahl = spalteIstZahl(zeilen, b.daten, b.bis, s, werte.get());
            sp.format = formate.value(s);
            t.spalten.append(sp);
        }
        t.zeilen = b.bis - b.daten;
        const int daten = b.daten;
        t.zelle = [datei, werte, daten, gruppiert, komma](int z, int s) {
            return zahlAnzeigen(gezeigterWert(datei->zeilen, daten + z, s, werte.get()), gruppiert, komma);
        };
        out.append(std::move(t));
    }
    return out;
}

bool dateiAlsPdf(const QString& quelle, const PdfOptionen& opt, bool gruppiert, const QString& ziel,
                 QString* err, const std::atomic<bool>* abbruch) {
    constexpr qint64 kMaxBytes = 32LL * 1024 * 1024;   // derselbe Deckel wie die Ansicht
    QFile f(quelle);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("Datei nicht lesbar.");
        return false;
    }
    auto d = std::make_shared<Datei>(parse(f.read(kMaxBytes)));
    if (!d->ok) {
        if (err) *err = d->fehler;
        return false;
    }
    const QList<Bereich> bloecke = findBlocks(*d);
    QList<FormelBereich> auftrag;
    for (const Bereich& b : bloecke) {
        FormelBereich fb{b.daten, b.bis, sammleFormeln(d->zeilen, b.daten, b.bis)};
        if (!fb.zellen.isEmpty()) auftrag.append(std::move(fb));
    }
    const bool komma = dezimalKomma(d->zeilen, 0, int(d->zeilen.size()), d->trenner);
    std::shared_ptr<const Werte> werte = auftrag.isEmpty() ? nullptr : rechne(d->zeilen, auftrag, komma);
    QHash<int, int> breiten;
    QHash<int, SpaltenFormat> formate;
    liesSpalten(quelle, breiten, formate);
    const QList<PdfTeil> teile = teileAusDatei(d, bloecke, werte, breiten, formate, gruppiert, komma);
    return schreibePdf(teile, opt, ziel, err, abbruch) > 0;
}

}  // namespace mg::table
