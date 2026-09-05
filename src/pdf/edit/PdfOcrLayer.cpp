#include "pdf/edit/PdfOcrLayer.h"
#include "pdf/edit/PdfObjects.h"
#include "pdf/edit/PdfBaseFontWidths.h"

#include <QFile>
#include <QHash>
#include <QPointF>
#include <QSizeF>

using namespace mg::pdfobj;

namespace mg {
namespace {

//  Kennung im Inhaltsstrom: Sie steht als PDF-Kommentar ganz vorn und macht
//  einen zweiten Lauf erkennbar (sonst läge der Text doppelt in der Datei).
const char* const kMarker = "% MediaGallery-OCR";

//  Name der Schrift in `/Resources /Font`. Kollidiert er, hängt der Aufrufer
//  eine Zahl an (s. uniqueFontName).
const char* const kFontBase = "/MGOCR";

// Bewusst NUR der Bereich, in dem WinAnsi und Latin-1 übereinstimmen: darüber müsste zeichenweise umgesetzt
// werden, und ein falsches Byte hieße "die Suche findet das falsche Wort". Sonst wird das Wort übersprungen.
bool toWinAnsi(const QString& s, QByteArray* out) {
    out->clear();
    out->reserve(s.size());
    for (const QChar& c : s) {
        const ushort u = c.unicode();
        //  Steuerzeichen und der in WinAnsi abweichende Block 0x7F–0x9F raus.
        if (u < 0x20 || (u >= 0x7F && u <= 0x9F) || u > 0xFF)
            return false;
        out->append(static_cast<char>(u & 0xFF));
    }
    return !out->isEmpty();
}

const QHash<ushort, ushort>& helveticaWidths() {
    static const QHash<ushort, ushort> map = [] {
        QHash<ushort, ushort> m;
        int n = 0;
        if (const BaseWidth* w = baseFontWidths(QByteArrayLiteral("/Helvetica"), &n)) {
            m.reserve(n);
            for (int i = 0; i < n; ++i)
                m.insert(w[i].unicode, w[i].width);
        }
        return m;
    }();
    return map;
}

//  Natürliche Breite von `s` bei Schriftgröße 1 (in em). 0, wenn keine
//  Breite bekannt ist - dann verzichtet der Aufrufer auf die Dehnung.
double naturalWidthEm(const QString& s) {
    const QHash<ushort, ushort>& w = helveticaWidths();
    if (w.isEmpty())
        return 0.0;
    double sum = 0.0;
    for (const QChar& c : s) {
        const auto it = w.constFind(c.unicode());
        //  Unbekanntes Zeichen: mittlere Breite, damit ein einzelnes Sonder-
        //  zeichen die Dehnung nicht verwirft.
        sum += (it != w.constEnd() ? double(*it) : 500.0) / 1000.0;
    }
    return sum;
}

//  Freier Ressourcenname im (ggf. leeren) `/Font`-Dict-Inhalt.
QByteArray uniqueFontName(const QByteArray& fontDictInner) {
    const QList<QByteArray> taken = dictKeys(fontDictInner);
    QByteArray name = QByteArray(kFontBase);
    for (int i = 0; taken.contains(name) && i < 1000; ++i)
        name = QByteArray(kFontBase) + QByteArray::number(i);
    return name;
}

//  `/Resources` der Seite - eigenes oder aus dem Seitenbaum geerbtes. Der
//  Aufrufer materialisiert es anschließend auf der Seite selbst.
QByteArray inheritedResources(const PdfDoc& doc, int pageObj) {
    for (int walk = pageObj, guard = 0; walk >= 0 && guard < 32; ++guard) {
        const QByteArray wd = doc.dictOf(walk);
        if (wd.isEmpty())
            break;
        const QByteArray res = doc.resolved(wd, "Resources");
        if (res.startsWith("<<"))
            return dictOfObject(res);
        walk = refValue(wd, "Parent");
    }
    return {};
}

}  // namespace

bool PdfOcrLayer::hasLayer(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    //  Die Kennung steht in einem UNKOMPRIMIERTEN Strom, den wir selbst
    //  geschrieben haben - ein Byte-Scan genügt und kostet kein Parsen.
    return f.readAll().contains(kMarker);
}

bool PdfOcrLayer::write(const QString& inputPath, const QString& outputPath,
                        const QVector<QVector<PdfOcrWord>>& words,
                        QString* err, int* skipped) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (err) err->clear();
    if (skipped) *skipped = 0;
    if (inputPath == outputPath)
        return fail(QStringLiteral("Ziel darf nicht die Quelle sein"));

    PdfDoc doc;
    QString le;
    if (!doc.load(inputPath, &le))
        return fail(le);

    IncrementalUpdate up(doc);

    //  EIN Schrift-Objekt für das ganze Dokument. Helvetica ist eine der 14
    //  Standardschriften - sie muss nicht eingebettet werden, und da der Text
    //  unsichtbar bleibt, spielt ihr Aussehen ohnehin keine Rolle.
    const int fontObj = up.reserveObjNum();
    up.addObject(fontObj, 0,
                 QByteArrayLiteral("<< /Type /Font /Subtype /Type1 "
                                   "/BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"));

    int wrote = 0;
    int dropped = 0;

    for (int pi = 0; pi < doc.pageObjs.size() && pi < words.size(); ++pi) {
        const QVector<PdfOcrWord>& pw = words.at(pi);
        if (pw.isEmpty())
            continue;

        const int        pageObj  = doc.pageObjs.at(pi);
        const QByteArray pageDict = doc.dictOf(pageObj);
        if (pageDict.isEmpty())
            continue;

        const QSizeF box = doc.pageBox(pageObj);
        const int    rot = doc.pageRotate(pageObj);
        if (box.isEmpty())
            continue;

        //  Ressourcen materialisieren und den Schriftnamen bestimmen.
        const QByteArray resInner  = inheritedResources(doc, pageObj);
        const QByteArray fontVal   = rawValue(resInner, "Font");
        const QByteArray fontInner = fontVal.startsWith("<<") ? dictOfObject(fontVal)
                                                              : QByteArray();
        const QByteArray fontName  = uniqueFontName(fontInner);

        QByteArray ops;
        ops += kMarker;
        ops += "\nq\nBT\n3 Tr\n";                 // 3 Tr = unsichtbar
        int onPage = 0;
        for (const PdfOcrWord& word : pw) {
            const QString t = word.text.trimmed();
            const QRectF  r = word.rectPts;
            if (t.isEmpty() || r.width() <= 0.0 || r.height() <= 0.0)
                continue;
            QByteArray bytes;
            if (!toWinAnsi(t, &bytes)) { ++dropped; continue; }

            //  Grundlinie: knapp unter der Unterkante des Kästchens, damit
            //  Unterlängen mit hineinpassen. Die Größe folgt der Kästchenhöhe.
            const double size = r.height();
            const double baseY = r.bottom() - r.height() * 0.18;

            // Anzeige-Raum -> Benutzerraum: die Drehung der Seite steckt in der Abbildung. Statt sie noch einmal von Hand
            // herzuleiten, wird die Textmatrix aus drei abgebildeten Punkten gewonnen.
            const QPointF p0 = toUser(r.left(),       baseY,     box, rot);
            const QPointF px = toUser(r.left() + 1.0, baseY,     box, rot);
            const QPointF py = toUser(r.left(),       baseY - 1.0, box, rot);
            const double a = px.x() - p0.x(), b = px.y() - p0.y();
            const double c = py.x() - p0.x(), d = py.y() - p0.y();

            //  Waagerecht so dehnen, dass das Wort genau sein Kästchen füllt -
            //  nur dann deckt sich die Auswahl im fertigen PDF mit dem Bild.
            const double nat = naturalWidthEm(t) * size;
            const double tz  = (nat > 0.01) ? qBound(10.0, r.width() / nat * 100.0, 1000.0)
                                            : 100.0;

            ops += '/' ; ops += fontName.mid(1); ops += ' ';
            ops += num(size);  ops += " Tf\n";
            ops += num(tz);    ops += " Tz\n";
            ops += num(a); ops += ' '; ops += num(b); ops += ' ';
            ops += num(c); ops += ' '; ops += num(d); ops += ' ';
            ops += num(p0.x()); ops += ' '; ops += num(p0.y()); ops += " Tm\n";
            ops += parenString(bytes); ops += " Tj\n";
            ++onPage;
        }
        ops += "ET\nQ\n";
        if (onPage == 0)
            continue;                              // nichts Schreibbares auf der Seite

        const int streamObj = up.reserveObjNum();
        up.addStream(streamObj, 0, QByteArray(), ops);

        //  Seite fortschreiben: /Contents ergänzen, /Resources sichern
        const QByteArray oldContents = rawValue(pageDict, "Contents");
        QByteArray newContents;
        if (oldContents.startsWith('[')) {
            //  Vorhandenes Array um unsere Referenz ERWEITERN (ganz hinten,
            //  damit der unsichtbare Text über allem liegt).
            const int close = oldContents.lastIndexOf(']');
            newContents = oldContents.left(close) + ' '
                        + QByteArray::number(streamObj) + " 0 R ]";
        } else if (!oldContents.isEmpty()) {
            newContents = "[ " + oldContents + ' '
                        + QByteArray::number(streamObj) + " 0 R ]";
        } else {
            newContents = QByteArray::number(streamObj) + " 0 R";
        }

        QByteArray newFontInner = fontInner;
        newFontInner += (newFontInner.isEmpty() ? "" : " ");
        newFontInner += fontName + ' ' + QByteArray::number(fontObj) + " 0 R";
        QByteArray newRes = setDictKey(resInner, "Font", "<< " + newFontInner + " >>");

        QByteArray newPage = pageDict;
        newPage = setDictKey(newPage, "Contents",  newContents);
        newPage = setDictKey(newPage, "Resources", "<< " + newRes + " >>");
        up.replaceDict(pageObj, newPage);
        ++wrote;
    }

    if (skipped) *skipped = dropped;
    if (wrote == 0)
        return fail(QStringLiteral("kein schreibbares Wort"));
    return up.commit(outputPath, err);
}

}  // namespace mg
