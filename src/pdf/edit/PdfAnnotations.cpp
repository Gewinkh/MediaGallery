#include "pdf/edit/PdfAnnotations.h"
#include "pdf/edit/PdfObjects.h"

#include <QRegularExpression>
#include <QSet>
#include <QSizeF>

#include <algorithm>

using namespace mg::pdfobj;

// Die Erläuterungen zum Gesamtverfahren stehen im Header.
namespace {

// ── Arten ───────────────────────────────────────────────────────────────────
//  /Subtype -> Art. Alles, was hier NICHT auftaucht, gehört entweder einer
//  anderen Einheit (Widget/Link/Medien) oder ist uns unbekannt - beides führt
//  zum Überspringen, nie zum Raten.
mg::PdfAnnotKind kindOf(const QByteArray& subtype) {
    if (subtype == "/Text")      return mg::PdfAnnotKind::Text;
    if (subtype == "/FreeText")  return mg::PdfAnnotKind::FreeText;
    if (subtype == "/Square")    return mg::PdfAnnotKind::Square;
    if (subtype == "/Circle")    return mg::PdfAnnotKind::Circle;
    if (subtype == "/Line")      return mg::PdfAnnotKind::Line;
    if (subtype == "/Ink")       return mg::PdfAnnotKind::Ink;
    if (subtype == "/Highlight") return mg::PdfAnnotKind::Highlight;
    if (subtype == "/Underline") return mg::PdfAnnotKind::Underline;
    if (subtype == "/StrikeOut") return mg::PdfAnnotKind::StrikeOut;
    return mg::PdfAnnotKind::Unknown;
}

// ── Farben ──────────────────────────────────────────────────────────────────
//  PDF-Farbarrays: 1 Zahl = Grau, 3 = RGB, 4 = CMYK, 0 Zahlen = „keine Farbe"
//  (das ist erlaubt und heißt transparent). Werte werden geklemmt, damit ein
//  defektes Dokument keine unsinnige Farbe erzeugt.
QColor colorOfArray(const QByteArray& arr) {
    if (!arr.startsWith('['))
        return {};
    const QVector<double> v = numbersOfArray(arr);
    auto cl = [](double d) { return std::clamp(d, 0.0, 1.0); };
    switch (v.size()) {
    case 1: return QColor::fromRgbF(cl(v[0]), cl(v[0]), cl(v[0]));
    case 3: return QColor::fromRgbF(cl(v[0]), cl(v[1]), cl(v[2]));
    case 4: return QColor::fromCmykF(cl(v[0]), cl(v[1]), cl(v[2]), cl(v[3]));
    default: return {};
    }
}

// ── Verschachtelte Zahlen-Arrays (/InkList) ─────────────────────────────────
//  Liefert je Unter-Array die Zahlenfolge. Steht dort ausnahmsweise eine flache
//  Liste (manche Erzeuger schreiben `[x y x y]` statt `[[x y x y]]`), wird sie
//  als EIN Strich gelesen, statt die Annotation zu verwerfen.
QVector<QVector<double>> groupsOfArray(const QByteArray& arr) {
    QVector<QVector<double>> out;
    if (!arr.startsWith('['))
        return out;
    const QByteArray inner = arr.mid(1, arr.size() - 2);
    qint64 i = 0;
    int guard = 0;
    while (i < inner.size() && ++guard < 100000) {
        while (i < inner.size() && isWs(inner[i])) ++i;
        if (i >= inner.size()) break;
        if (inner[i] != '[') { ++i; continue; }
        const qint64 e = skipValue(inner, i);
        if (e <= i) break;
        out.push_back(numbersOfArray(inner.mid(i, e - i)));
        i = e;
    }
    if (out.isEmpty()) {
        const QVector<double> flat = numbersOfArray(arr);
        if (!flat.isEmpty())
            out.push_back(flat);
    }
    return out;
}

// ── /DA (nur /FreeText) ─────────────────────────────────────────────────────
//  „/Helv 12 Tf 0 0 1 rg" -> Größe und Textfarbe. Fehlt etwas, bleibt es beim
//  Standardwert; geraten wird nicht.
void parseDa(const QByteArray& da, qreal* sizePt, QColor* color) {
    static const QRegularExpression tf(
        QStringLiteral("/[^\\s/\\[\\]<>(){}]+\\s+([0-9]*\\.?[0-9]+)\\s+Tf"));
    const auto m = tf.match(QString::fromLatin1(da));
    if (m.hasMatch())
        *sizePt = m.captured(1).toDouble();

    static const QRegularExpression rg(
        QStringLiteral("([0-9.]+)\\s+([0-9.]+)\\s+([0-9.]+)\\s+rg"));
    static const QRegularExpression gr(QStringLiteral("([0-9.]+)\\s+g(?![a-zA-Z])"));
    const QString s = QString::fromLatin1(da);
    const auto mc = rg.match(s);
    if (mc.hasMatch()) {
        *color = QColor::fromRgbF(std::clamp(mc.captured(1).toDouble(), 0.0, 1.0),
                                  std::clamp(mc.captured(2).toDouble(), 0.0, 1.0),
                                  std::clamp(mc.captured(3).toDouble(), 0.0, 1.0));
        return;
    }
    const auto mg_ = gr.match(s);
    if (mg_.hasMatch()) {
        const double v = std::clamp(mg_.captured(1).toDouble(), 0.0, 1.0);
        *color = QColor::fromRgbF(v, v, v);
    }
}

// ── Rechteck ────────────────────────────────────────────────────────────────
//  /Rect ist ein Paar GEGENÜBERLIEGENDER Ecken in beliebiger Reihenfolge -
//  erst normalisieren, dann beide Ecken in Anzeigekoordinaten abbilden und
//  daraus wieder ein normalisiertes Rechteck bauen (bei 90°/270° tauschen
//  Breite und Höhe, deshalb nicht einfach Punkt + Größe).
bool rectOf(const QByteArray& raw, const QSizeF& box, int rot, QRectF* out) {
    if (!raw.startsWith('['))
        return false;
    const QVector<double> v = numbersOfArray(raw);
    if (v.size() < 4)
        return false;
    const double x0 = qMin(v[0], v[2]), x1 = qMax(v[0], v[2]);
    const double y0 = qMin(v[1], v[3]), y1 = qMax(v[1], v[3]);
    const QPointF a = toDisplay(x0, y0, box, rot);
    const QPointF b = toDisplay(x1, y1, box, rot);
    *out = QRectF(QPointF(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
                  QPointF(qMax(a.x(), b.x()), qMax(a.y(), b.y())));
    return true;
}

// ── Strichbreite ────────────────────────────────────────────────────────────
//  /BS << /W n >> hat Vorrang vor dem alten /Border [h v w]; fehlt beides,
//  gilt die PDF-Vorgabe 1.
qreal borderWidthOf(const mg::pdfobj::PdfDoc& doc, const QByteArray& dict) {
    const QByteArray bs = doc.resolved(dict, "BS");
    if (bs.startsWith("<<")) {
        const QByteArray inner = dictOfObject(bs);
        const QByteArray w = rawValue(inner, "W");
        if (!w.isEmpty()) {
            bool ok = false;
            const double d = w.toDouble(&ok);
            if (ok && d >= 0.0) return d;
        }
    }
    const QByteArray border = rawValue(dict, "Border");
    if (border.startsWith('[')) {
        const QVector<double> v = numbersOfArray(border);
        if (v.size() >= 3 && v[2] >= 0.0) return v[2];
    }
    return 1.0;
}


// ── Schreiben: Bausteine ────────────────────────────────────────────────────
//  Farbe -> PDF-Array „[r g b]" (leer, wenn ungültig = „keine Farbe").
QByteArray colorArray(const QColor& c) {
    if (!c.isValid())
        return {};
    return "[" + num(c.redF()) + " " + num(c.greenF()) + " " + num(c.blueF()) + "]";
}

//  Farbe als Zeichenbefehl-Operanden („r g b").
QByteArray rgbOps(const QColor& c) {
    return num(c.redF()) + " " + num(c.greenF()) + " " + num(c.blueF());
}

QByteArray subtypeName(mg::PdfAnnotKind k) {
    switch (k) {
    case mg::PdfAnnotKind::Text:      return "/Text";
    case mg::PdfAnnotKind::FreeText:  return "/FreeText";
    case mg::PdfAnnotKind::Square:    return "/Square";
    case mg::PdfAnnotKind::Circle:    return "/Circle";
    case mg::PdfAnnotKind::Line:      return "/Line";
    case mg::PdfAnnotKind::Ink:       return "/Ink";
    case mg::PdfAnnotKind::Highlight: return "/Highlight";
    case mg::PdfAnnotKind::Underline: return "/Underline";
    case mg::PdfAnnotKind::StrikeOut: return "/StrikeOut";
    default:                          return {};
    }
}

//  Ellipse aus vier kubischen Bézier-Bögen (k = 0.5523 · r) - dieselbe
//  Mathematik wie im Vektor-Export, damit Anzeige und Datei übereinstimmen.
QByteArray ellipsePath(qreal x, qreal y, qreal w, qreal h) {
    const qreal cx = x + w/2, cy = y + h/2, rx = w/2, ry = h/2;
    const qreal kx = rx * 0.5523, ky = ry * 0.5523;
    QByteArray o;
    o += num(cx + rx) + " " + num(cy) + " m\n";
    o += num(cx + rx) + " " + num(cy + ky) + " " + num(cx + kx) + " " + num(cy + ry)
       + " " + num(cx) + " " + num(cy + ry) + " c\n";
    o += num(cx - kx) + " " + num(cy + ry) + " " + num(cx - rx) + " " + num(cy + ky)
       + " " + num(cx - rx) + " " + num(cy) + " c\n";
    o += num(cx - rx) + " " + num(cy - ky) + " " + num(cx - kx) + " " + num(cy - ry)
       + " " + num(cx) + " " + num(cy - ry) + " c\n";
    o += num(cx + kx) + " " + num(cy - ry) + " " + num(cx + rx) + " " + num(cy - ky)
       + " " + num(cx + rx) + " " + num(cy) + " c\n";
    return o;
}

//  Mittlere Zeichenbreite für den Umbruch im FreeText-Erscheinungsbild.
//  Bewusst leicht ZU GROSS geschätzt: lieber eine Zeile früher umbrechen als
//  über den Rand schreiben (dieselbe Haltung wie im Vektor-Export).
qreal avgCharWidth(qreal sizePt) { return 0.54 * sizePt; }

QVector<QString> wrapText(const QString& text, qreal widthPt, qreal sizePt) {
    QVector<QString> lines;
    const int maxChars = qMax(1, int(widthPt / qMax(0.1, avgCharWidth(sizePt))));
    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    for (const QString& para : paragraphs) {
        if (para.isEmpty()) { lines.push_back(QString()); continue; }
        QString cur;
        const QStringList words = para.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& w0 : words) {
            QString w = w0;
            while (w.size() > maxChars) {          // Wort länger als die Zeile
                if (!cur.isEmpty()) { lines.push_back(cur); cur.clear(); }
                lines.push_back(w.left(maxChars));
                w = w.mid(maxChars);
            }
            if (cur.isEmpty())                          cur = w;
            else if (cur.size() + 1 + w.size() <= maxChars) cur += QLatin1Char(' ') + w;
            else { lines.push_back(cur); cur = w; }
        }
        lines.push_back(cur);
    }
    return lines;
}

//  Der Inhalt EINES Erscheinungsbildes, gezeichnet im LOKALEN Raum der
//  Annotation: (0|0) ist die untere linke Ecke ihres Rechtecks. `w`/`h` sind
//  seine Maße in Punkten. `fontRes` ist true, wenn der Strom eine Schrift
//  braucht (nur FreeText/Text) - der Aufrufer hängt sie dann in /Resources.
QByteArray appearanceOps(const mg::PdfAnnotation& a, qreal w, qreal h,
                         const QVector<QPointF>& localLine,
                         const QVector<QVector<QPointF>>& localInk,
                         const QVector<QRectF>& localQuads,
                         bool* needsFont, bool* needsAlpha) {
    using K = mg::PdfAnnotKind;
    *needsFont  = false;
    *needsAlpha = a.opacity < 1.0;

    const QColor stroke = a.color.isValid() ? a.color : QColor(Qt::black);
    const qreal  lw     = qMax(0.0, a.borderWidth);
    QByteArray o;
    o += "q\n";
    if (*needsAlpha)
        o += "/GSa gs\n";

    switch (a.kind) {
    case K::Square: {
        const qreal inset = lw / 2.0;
        const qreal x = inset, y = inset;
        const qreal rw = qMax(0.0, w - lw), rh = qMax(0.0, h - lw);
        if (a.interiorColor.isValid()) o += rgbOps(a.interiorColor) + " rg\n";
        o += rgbOps(stroke) + " RG\n" + num(lw) + " w\n";
        o += num(x) + " " + num(y) + " " + num(rw) + " " + num(rh) + " re\n";
        o += a.interiorColor.isValid() ? (lw > 0 ? "B\n" : "f\n") : "S\n";
        break;
    }
    case K::Circle: {
        const qreal inset = lw / 2.0;
        if (a.interiorColor.isValid()) o += rgbOps(a.interiorColor) + " rg\n";
        o += rgbOps(stroke) + " RG\n" + num(lw) + " w\n";
        o += ellipsePath(inset, inset, qMax(0.0, w - lw), qMax(0.0, h - lw));
        o += a.interiorColor.isValid() ? (lw > 0 ? "B\n" : "f\n") : "S\n";
        break;
    }
    case K::Line: {
        if (localLine.size() < 2) break;
        o += rgbOps(stroke) + " RG\n" + num(qMax(0.1, lw)) + " w\n1 J\n1 j\n";
        o += num(localLine[0].x()) + " " + num(localLine[0].y()) + " m\n";
        o += num(localLine[1].x()) + " " + num(localLine[1].y()) + " l\nS\n";
        break;
    }
    case K::Ink: {
        o += rgbOps(stroke) + " RG\n" + num(qMax(0.1, lw)) + " w\n1 J\n1 j\n";
        for (const QVector<QPointF>& p : localInk) {
            if (p.size() < 2) continue;
            o += num(p[0].x()) + " " + num(p[0].y()) + " m\n";
            for (int i = 1; i < p.size(); ++i)
                o += num(p[i].x()) + " " + num(p[i].y()) + " l\n";
            o += "S\n";
        }
        break;
    }
    case K::Highlight: {
        //  Multiplizieren, damit der Text darunter lesbar bleibt (so machen es
        //  die verbreiteten Betrachter auch).
        *needsAlpha = true;
        o += rgbOps(a.color.isValid() ? a.color : QColor(255, 255, 0)) + " rg\n";
        for (const QRectF& r : localQuads)
            o += num(r.x()) + " " + num(r.y()) + " "
               + num(r.width()) + " " + num(r.height()) + " re\n";
        o += "f\n";
        break;
    }
    case K::Underline:
    case K::StrikeOut: {
        o += rgbOps(stroke) + " RG\n";
        for (const QRectF& r : localQuads) {
            const qreal t = qMax(0.5, r.height() / 14.0);
            const qreal y = (a.kind == K::Underline) ? r.y() + t
                                                     : r.y() + r.height() / 2.0;
            o += num(t) + " w\n";
            o += num(r.x()) + " " + num(y) + " m\n";
            o += num(r.x() + r.width()) + " " + num(y) + " l\nS\n";
        }
        break;
    }
    case K::FreeText: {
        const qreal size = a.fontSizePt > 0.0 ? a.fontSizePt : 11.0;
        *needsFont = true;
        if (a.interiorColor.isValid()) {
            o += rgbOps(a.interiorColor) + " rg\n";
            o += "0 0 " + num(w) + " " + num(h) + " re\nf\n";
        }
        if (lw > 0.0 && a.color.isValid()) {
            o += rgbOps(a.color) + " RG\n" + num(lw) + " w\n";
            o += num(lw/2) + " " + num(lw/2) + " " + num(qMax(0.0, w - lw))
               + " " + num(qMax(0.0, h - lw)) + " re\nS\n";
        }
        const qreal pad = 2.0 + lw;
        const QVector<QString> lines = wrapText(a.contents, qMax(1.0, w - 2*pad), size);
        o += "q\n" + num(pad) + " " + num(pad) + " " + num(qMax(0.0, w - 2*pad))
           + " " + num(qMax(0.0, h - 2*pad)) + " re\nW\nn\n";     // beschneiden
        o += "BT\n/MGF " + num(size) + " Tf\n";
        o += rgbOps(a.textColor.isValid() ? a.textColor : QColor(Qt::black)) + " rg\n";
        const qreal lead = size * 1.16;
        qreal y = h - pad - size * 0.85;
        for (const QString& l : lines) {
            if (y < -lead) break;
            o += "1 0 0 1 " + num(pad) + " " + num(y) + " Tm\n";
            o += parenString(l.toLatin1()) + " Tj\n";
            y -= lead;
        }
        o += "ET\nQ\n";
        break;
    }
    case K::Text: {
        //  Notizsymbol: gefüllte Fläche mit Rahmen und drei „Zeilen". Ein
        //  ausgefeiltes Icon wäre Geschmack; erkennbar sein muss es.
        const QColor fill = a.color.isValid() ? a.color : QColor(255, 220, 90);
        o += rgbOps(fill) + " rg\n0 0 0 RG\n0.7 w\n";
        o += "0.5 0.5 " + num(qMax(0.0, w - 1)) + " " + num(qMax(0.0, h - 1)) + " re\nB\n";
        for (int i = 1; i <= 3; ++i) {
            const qreal y = h * (0.25 * i);
            o += num(w * 0.2) + " " + num(y) + " m\n"
               + num(w * 0.8) + " " + num(y) + " l\nS\n";
        }
        break;
    }
    default:
        break;
    }
    o += "Q\n";
    return o;
}

} // namespace

namespace mg {

bool PdfAnnotations::read(const QString& path, QVector<PdfAnnotation>* out,
                          QString* err) {
    if (!out)
        return false;
    out->clear();

    pdfobj::PdfDoc doc;
    if (!doc.load(path, err))
        return false;

    static const QRegularExpression refRe(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));

    for (int pi = 0; pi < doc.pageObjs.size(); ++pi) {
        const int pageObj = doc.pageObjs.at(pi);
        const QByteArray annots = doc.resolved(doc.dictOf(pageObj), "Annots");
        if (!annots.startsWith('['))
            continue;

        const QSizeF box = doc.pageBox(pageObj);
        if (box.isEmpty())
            continue;                       // ohne Seitenmaß keine Koordinaten
        const int rot = doc.pageRotate(pageObj);

        auto it = refRe.globalMatch(QString::fromLatin1(annots));
        while (it.hasNext()) {
            const int objNum = it.next().captured(1).toInt();
            const QByteArray dict = doc.dictOf(objNum);
            if (dict.isEmpty())
                continue;                   // tote Referenz - überspringen

            const PdfAnnotKind kind = kindOf(nameValue(dict, "Subtype"));
            if (kind == PdfAnnotKind::Unknown)
                continue;                   // Widget/Link/Medien/Popup/unbekannt

            PdfAnnotation a;
            a.page   = pi;
            a.kind   = kind;
            a.objNum = objNum;
            if (!rectOf(rawValue(dict, "Rect"), box, rot, &a.rect))
                continue;                   // ohne /Rect ist nichts platzierbar

            //  Texte: /Contents und /T können Referenzen sein.
            auto textOf = [&](const char* key) -> QString {
                const QByteArray raw = doc.resolved(dict, key);
                QByteArray bytes;
                if (!raw.isEmpty() && readPdfStringBytes(raw, 0, &bytes))
                    return pdfTextToString(bytes);
                return {};
            };
            a.contents = textOf("Contents");
            a.author   = textOf("T");
            a.subject  = textOf("Subj");

            a.color         = colorOfArray(doc.resolved(dict, "C"));
            a.interiorColor = colorOfArray(doc.resolved(dict, "IC"));
            a.borderWidth   = borderWidthOf(doc, dict);

            {   const QByteArray ca = rawValue(dict, "CA");
                bool ok = false;
                const double d = ca.toDouble(&ok);
                if (ok) a.opacity = std::clamp(d, 0.0, 1.0);
            }
            {   const qint64 f = intValue(dict, "F");
                if (f > 0) a.hidden = (f & 2) || (f & 32);   // Hidden | NoView
            }

            if (kind == PdfAnnotKind::FreeText) {
                const QByteArray raw = doc.resolved(dict, "DA");
                QByteArray da;
                if (!raw.isEmpty() && readPdfStringBytes(raw, 0, &da))
                    parseDa(da, &a.fontSizePt, &a.textColor);
            }

            if (kind == PdfAnnotKind::Ink) {
                const QVector<QVector<double>> groups =
                    groupsOfArray(doc.resolved(dict, "InkList"));
                for (const QVector<double>& g : groups) {
                    QVector<QPointF> stroke;
                    stroke.reserve(g.size() / 2);
                    for (int k = 0; k + 1 < g.size(); k += 2)
                        stroke.push_back(toDisplay(g[k], g[k+1], box, rot));
                    if (!stroke.isEmpty())
                        a.inkPaths.push_back(stroke);
                }
            }

            if (kind == PdfAnnotKind::Line) {
                const QVector<double> v = numbersOfArray(doc.resolved(dict, "L"));
                if (v.size() >= 4) {
                    a.line.push_back(toDisplay(v[0], v[1], box, rot));
                    a.line.push_back(toDisplay(v[2], v[3], box, rot));
                }
            }

            if (kind == PdfAnnotKind::Highlight || kind == PdfAnnotKind::Underline
                || kind == PdfAnnotKind::StrikeOut) {
                //  /QuadPoints: je markiertem Bereich VIER Eckpunkte (Reihenfolge
                //  ist in der Praxis uneinheitlich) - deshalb Hülle statt Ecken.
                const QVector<double> v = numbersOfArray(doc.resolved(dict, "QuadPoints"));
                for (int k = 0; k + 7 < v.size(); k += 8) {
                    double lo_x = v[k], hi_x = v[k], lo_y = v[k+1], hi_y = v[k+1];
                    for (int c = 0; c < 4; ++c) {
                        lo_x = qMin(lo_x, v[k + c*2]);     hi_x = qMax(hi_x, v[k + c*2]);
                        lo_y = qMin(lo_y, v[k + c*2 + 1]); hi_y = qMax(hi_y, v[k + c*2 + 1]);
                    }
                    const QPointF p1 = toDisplay(lo_x, lo_y, box, rot);
                    const QPointF p2 = toDisplay(hi_x, hi_y, box, rot);
                    a.quads.push_back(QRectF(
                        QPointF(qMin(p1.x(), p2.x()), qMin(p1.y(), p2.y())),
                        QPointF(qMax(p1.x(), p2.x()), qMax(p1.y(), p2.y()))));
                }
            }

            out->push_back(a);
        }
    }
    return true;
}

bool PdfAnnotations::write(const QString& inputPath, const QString& outputPath,
                           const QVector<PdfAnnotation>& annots,
                           const QVector<int>& removeObjNums, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (err) err->clear();

    if (inputPath == outputPath)
        return fail(QStringLiteral("Ziel darf nicht die Quelle sein"));
    if (annots.isEmpty() && removeObjNums.isEmpty())
        return fail(QStringLiteral("nichts zu tun"));

    pdfobj::PdfDoc doc;
    QString le;
    if (!doc.load(inputPath, &le))
        return fail(le);

    for (const PdfAnnotation& a : annots) {
        if (a.page < 0 || a.page >= doc.pageObjs.size())
            return fail(QStringLiteral("Seite %1 gibt es nicht").arg(a.page));
        if (subtypeName(a.kind).isEmpty())
            return fail(QStringLiteral("unbekannte Annotationsart"));
    }

    pdfobj::IncrementalUpdate up(doc);

    //  Genau EINE Schrift und EIN Transparenz-Zustand für alle neuen
    //  Erscheinungsbilder - lazy, damit ein Dokument ohne Text/Deckkraft auch
    //  keine ungenutzten Objekte bekommt.
    int fontObj = -1;
    auto ensureFont = [&]() {
        if (fontObj < 0) {
            fontObj = up.reserveObjNum();
            up.addObject(fontObj, 0,
                         "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica"
                         " /Encoding /WinAnsiEncoding >>");
        }
        return fontObj;
    };

    //  Neue Annotationsobjekte je Seite (Objektnummern).
    QHash<int, QVector<int>> perPage;

    for (const PdfAnnotation& a : annots) {
        const int    pageObj = doc.pageObjs.at(a.page);
        const QSizeF box     = doc.pageBox(pageObj);
        if (box.isEmpty())
            return fail(QStringLiteral("Seitenmaß nicht lesbar"));
        const int rot = doc.pageRotate(pageObj);

        //  Rechteck: beide Ecken in den Benutzerraum, dann normalisieren.
        const QPointF u1 = toUser(a.rect.left(),  a.rect.top(),    box, rot);
        const QPointF u2 = toUser(a.rect.right(), a.rect.bottom(), box, rot);
        const qreal ux0 = qMin(u1.x(), u2.x()), ux1 = qMax(u1.x(), u2.x());
        const qreal uy0 = qMin(u1.y(), u2.y()), uy1 = qMax(u1.y(), u2.y());
        const qreal w = ux1 - ux0, h = uy1 - uy0;
        if (w <= 0.0 || h <= 0.0)
            return fail(QStringLiteral("leeres Rechteck"));

        //  Geometrie in den LOKALEN Raum des Erscheinungsbildes (Ursprung =
        //  untere linke Ecke des Rechtecks).
        auto localOf = [&](const QPointF& display) {
            const QPointF u = toUser(display.x(), display.y(), box, rot);
            return QPointF(u.x() - ux0, u.y() - uy0);
        };
        QVector<QPointF> localLine;
        for (const QPointF& p : a.line) localLine.push_back(localOf(p));
        QVector<QVector<QPointF>> localInk;
        for (const QVector<QPointF>& stroke : a.inkPaths) {
            QVector<QPointF> s;
            for (const QPointF& p : stroke) s.push_back(localOf(p));
            localInk.push_back(s);
        }
        QVector<QRectF> localQuads;
        for (const QRectF& r : a.quads) {
            const QPointF p1 = localOf(r.topLeft());
            const QPointF p2 = localOf(r.bottomRight());
            localQuads.push_back(QRectF(QPointF(qMin(p1.x(), p2.x()), qMin(p1.y(), p2.y())),
                                        QPointF(qMax(p1.x(), p2.x()), qMax(p1.y(), p2.y()))));
        }

        bool needsFont = false, needsAlpha = false;
        const QByteArray ops = appearanceOps(a, w, h, localLine, localInk, localQuads,
                                             &needsFont, &needsAlpha);

        QByteArray res = "<<";
        if (needsFont)
            res += " /Font << /MGF " + QByteArray::number(ensureFont()) + " 0 R >>";
        if (needsAlpha) {
            //  Deckkraft (und beim Markieren zusätzlich Multiplizieren) als
            //  Grafikzustand - inline im /Resources, das spart ein Objekt.
            res += " /ExtGState << /GSa << /Type /ExtGState /ca "
                 + num(qBound(0.0, a.opacity, 1.0)) + " /CA "
                 + num(qBound(0.0, a.opacity, 1.0));
            if (a.kind == PdfAnnotKind::Highlight)
                res += " /BM /Multiply";
            res += " >> >>";
        }
        res += " >>";

        const int apObj = up.reserveObjNum();
        up.addStream(apObj, 0,
                     "/Type /XObject /Subtype /Form /FormType 1"
                     " /BBox [0 0 " + num(w) + " " + num(h) + "]"
                     " /Resources " + res, ops);

        //  Das Annotationsobjekt.
        QByteArray d = "<< /Type /Annot /Subtype " + subtypeName(a.kind);
        d += " /Rect [" + num(ux0) + " " + num(uy0) + " " + num(ux1) + " " + num(uy1) + "]";
        d += " /P " + QByteArray::number(pageObj) + " 0 R";
        //  Bit 3 = Print (soll mitgedruckt werden), Bit 2 = Hidden.
        d += " /F " + QByteArray::number(a.hidden ? 6 : 4);
        if (!a.contents.isEmpty()) d += " /Contents " + toPdfTextString(a.contents);
        if (!a.author.isEmpty())   d += " /T "        + toPdfTextString(a.author);
        if (!a.subject.isEmpty())  d += " /Subj "     + toPdfTextString(a.subject);
        const QByteArray c = colorArray(a.color);
        if (!c.isEmpty())          d += " /C " + c;
        const QByteArray ic = colorArray(a.interiorColor);
        if (!ic.isEmpty())         d += " /IC " + ic;
        d += " /BS << /Type /Border /W " + num(qMax(0.0, a.borderWidth)) + " /S /S >>";
        if (a.opacity < 1.0)       d += " /CA " + num(qBound(0.0, a.opacity, 1.0));

        if (a.kind == PdfAnnotKind::Ink) {
            QByteArray ink = " /InkList [";
            for (const QVector<QPointF>& stroke : a.inkPaths) {
                ink += "[";
                for (const QPointF& p : stroke) {
                    const QPointF u = toUser(p.x(), p.y(), box, rot);
                    ink += num(u.x()) + " " + num(u.y()) + " ";
                }
                ink += "]";
            }
            d += ink + "]";
        }
        if (a.kind == PdfAnnotKind::Line && a.line.size() >= 2) {
            const QPointF p0 = toUser(a.line[0].x(), a.line[0].y(), box, rot);
            const QPointF p1 = toUser(a.line[1].x(), a.line[1].y(), box, rot);
            d += " /L [" + num(p0.x()) + " " + num(p0.y()) + " "
               + num(p1.x()) + " " + num(p1.y()) + "]";
        }
        if (a.kind == PdfAnnotKind::Highlight || a.kind == PdfAnnotKind::Underline
            || a.kind == PdfAnnotKind::StrikeOut) {
            //  /QuadPoints in der Reihenfolge des Standards: oben-links,
            //  oben-rechts, unten-links, unten-rechts (in Benutzerkoordinaten).
            QByteArray q = " /QuadPoints [";
            for (const QRectF& r : a.quads) {
                const QPointF a1 = toUser(r.left(),  r.top(),    box, rot);
                const QPointF a2 = toUser(r.right(), r.bottom(), box, rot);
                const qreal qx0 = qMin(a1.x(), a2.x()), qx1 = qMax(a1.x(), a2.x());
                const qreal qy0 = qMin(a1.y(), a2.y()), qy1 = qMax(a1.y(), a2.y());
                q += num(qx0) + " " + num(qy1) + " " + num(qx1) + " " + num(qy1) + " "
                   + num(qx0) + " " + num(qy0) + " " + num(qx1) + " " + num(qy0) + " ";
            }
            d += q + "]";
        }
        if (a.kind == PdfAnnotKind::FreeText) {
            //  /DA ist für FreeText Pflicht - auch wenn wir das Aussehen selbst
            //  liefern, richten sich Betrachter beim NACHbearbeiten danach.
            const qreal size = a.fontSizePt > 0.0 ? a.fontSizePt : 11.0;
            const QColor tc  = a.textColor.isValid() ? a.textColor : QColor(Qt::black);
            d += " /DA " + parenString("/MGF " + num(size) + " Tf "
                                       + rgbOps(tc) + " rg");
        }
        d += " /AP << /N " + QByteArray::number(apObj) + " 0 R >> >>";

        const int annObj = up.reserveObjNum();
        up.addObject(annObj, 0, d);
        perPage[a.page].push_back(annObj);
    }

    //  Zu streichende Objekte: die Seiten, auf denen sie liegen, müssen ihr
    //  /Annots ebenfalls neu bekommen.
    QSet<int> drop;
    for (int n : removeObjNums) {
        if (n <= 0)
            continue;
        const auto it = doc.annotPage.constFind(n);
        if (it == doc.annotPage.constEnd())
            continue;                       // steht auf keiner Seite -> nichts zu tun
        drop.insert(n);
        if (!perPage.contains(it.value()))
            perPage.insert(it.value(), {});
    }

    //  Je betroffener Seite das /Annots-Array fortschreiben. Ist es eine
    //  REFERENZ, wird das Array-Objekt ersetzt - sonst die Seite selbst; so
    //  bleibt eine von mehreren Seiten geteilte Liste unangetastet.
    for (auto it = perPage.constBegin(); it != perPage.constEnd(); ++it) {
        const int pageObj = doc.pageObjs.at(it.key());
        const QByteArray pageDict = doc.dictOf(pageObj);

        QByteArray refs;
        for (int n : it.value())
            refs += QByteArray::number(n) + " 0 R ";

        //  Bestehende Einträge übernehmen - ohne die gestrichenen.
        auto keptOf = [&](const QByteArray& arr) {
            QByteArray kept;
            static const QRegularExpression re(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
            auto m = re.globalMatch(QString::fromLatin1(arr));
            while (m.hasNext()) {
                const auto one = m.next();
                const int n = one.captured(1).toInt();
                if (!drop.contains(n))
                    kept += one.captured(0).toLatin1() + " ";
            }
            return kept;
        };

        const int arrayObj = refValue(pageDict, "Annots");
        if (arrayObj >= 0) {
            const QByteArray arr = doc.bodyOf(arrayObj).trimmed();
            if (!arr.startsWith('[') || !arr.endsWith(']'))
                return fail(QStringLiteral("/Annots-Array nicht lesbar"));
            up.addObject(arrayObj, doc.genOf(arrayObj), "[" + keptOf(arr) + refs + "]");
        } else {
            const QByteArray arr = rawValue(pageDict, "Annots");
            QByteArray next;
            if (arr.isEmpty()) {
                next = "[" + refs + "]";
            } else if (arr.startsWith('[') && arr.endsWith(']')) {
                next = "[" + keptOf(arr) + refs + "]";
            } else {
                return fail(QStringLiteral("/Annots weder Array noch Referenz"));
            }
            up.replaceDict(pageObj, setDictKey(pageDict, "Annots", next));
        }
    }

    QString ce;
    if (!up.commit(outputPath, &ce))
        return fail(ce);
    return true;
}

} // namespace mg
