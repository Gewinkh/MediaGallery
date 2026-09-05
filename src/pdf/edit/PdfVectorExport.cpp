#include "pdf/edit/PdfVectorExport.h"

#include <array>
#include "pdf/edit/PdfObjects.h"
#include "pdf/edit/PdfEncodings.h"
#include "pdf/extract/PdfPageCopier.h"
#include "pdf/edit/PdfFontEmbed.h"
#include "pdf/edit/PdfImageEmbed.h"

#include <QFile>
#include <QSaveFile>
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <QRectF>
#include <QPointF>
#include <QColor>
#include <QTemporaryFile>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace mg::pdfobj;

namespace {

// PDF-Zahlen: höchstens drei Nachkommastellen, kein Exponent, Punkt als Dezimaltrenner (locale-unabhängig).
// Überflüssige Nullen werden entfernt, damit die Ströme klein bleiben.

QByteArray rgb(const QColor& c) {
    return num(c.redF()) + " " + num(c.greenF()) + " " + num(c.blueF());
}


// Nur die Standard-14 lassen sich OHNE Einbetten benutzen; jede andere Familie müsste als Font-Programm
// mitgeliefert werden. Die Zuordnung folgt dem, was der Editor anbietet.
QByteArray baseFontFor(const QString& family, bool bold, bool italic) {
    const QString f = family.trimmed().toLower();
    const bool times   = f.contains(QStringLiteral("times")) || f.contains(QStringLiteral("serif"))
                         || f.contains(QStringLiteral("roman")) || f.contains(QStringLiteral("georgia"));
    const bool courier = f.contains(QStringLiteral("courier")) || f.contains(QStringLiteral("mono"))
                         || f.contains(QStringLiteral("consol"));
    if (courier) {
        if (bold && italic) return "Courier-BoldOblique";
        if (bold)           return "Courier-Bold";
        if (italic)         return "Courier-Oblique";
        return "Courier";
    }
    if (times) {
        if (bold && italic) return "Times-BoldItalic";
        if (bold)           return "Times-Bold";
        if (italic)         return "Times-Italic";
        return "Times-Roman";
    }
    if (bold && italic) return "Helvetica-BoldOblique";
    if (bold)           return "Helvetica-Bold";
    if (italic)         return "Helvetica-Oblique";
    return "Helvetica";
}

qreal avgCharWidth(const QByteArray& baseFont, qreal sizePt) {
    if (baseFont.startsWith("Courier")) return 0.600 * sizePt;
    if (baseFont.startsWith("Times"))   return 0.500 * sizePt;
    return 0.540 * sizePt;                                   // Helvetica
}

QVector<QString> wrapText(const QString& text, qreal widthPt,
                          const QByteArray& baseFont, qreal sizePt) {
    QVector<QString> lines;
    const qreal cw = qMax(0.1, avgCharWidth(baseFont, sizePt));
    const int maxChars = qMax(1, int(widthPt / cw));
    const QStringList paras = text.split(QLatin1Char('\n'));
    for (const QString& para : paras) {
        if (para.isEmpty()) { lines.push_back(QString()); continue; }
        QString cur;
        for (const QString& word : para.split(QLatin1Char(' '))) {
            QString w = word;
            while (w.size() > maxChars) {                    // überlanges Wort
                if (!cur.isEmpty()) { lines.push_back(cur); cur.clear(); }
                lines.push_back(w.left(maxChars));
                w = w.mid(maxChars);
            }
            if (cur.isEmpty())                       cur = w;
            else if (cur.size() + 1 + w.size() <= maxChars) cur += QLatin1Char(' ') + w;
            else { lines.push_back(cur); cur = w; }
        }
        lines.push_back(cur);
    }
    return lines;
}

// Was der letzte Strom offen laesst, gilt fuer alles Angehaengte: offene q und ein cm
// AUSSERHALB jeder q-Klammer, das kein Q zuruecknimmt. Daran lag der schwarze Block -
// eine stehende Matrix [0.24 0 0 -0.24 0 842] verschob den Balken.
struct TrailingState {
    int   openSaves = 0;              // nicht geschlossene `q`
    qreal m[6] = { 1, 0, 0, 1, 0, 0 }; // CTM, NACHDEM diese `q` zurückgerollt sind
};

inline void matMul(const qreal a[6], const qreal b[6], qreal out[6]) {
    const qreal r0 = a[0]*b[0] + a[1]*b[2];
    const qreal r1 = a[0]*b[1] + a[1]*b[3];
    const qreal r2 = a[2]*b[0] + a[3]*b[2];
    const qreal r3 = a[2]*b[1] + a[3]*b[3];
    const qreal r4 = a[4]*b[0] + a[5]*b[2] + b[4];
    const qreal r5 = a[4]*b[1] + a[5]*b[3] + b[5];
    out[0]=r0; out[1]=r1; out[2]=r2; out[3]=r3; out[4]=r4; out[5]=r5;
}

TrailingState trailingState(const QByteArray& c) {
    TrailingState st;
    QVector<qreal> nums;
    QVector<std::array<qreal,6>> stack;
    const qint64 n = c.size();
    qint64 i = 0;
    while (i < n) {
        const char ch = c[i];
        if (isWs(ch)) { ++i; continue; }
        if (ch == '%') { while (i < n && c[i] != '\n' && c[i] != '\r') ++i; continue; }
        if (ch == '(' || ch == '<' || ch == '[') { i = skipValue(c, i); nums.clear(); continue; }
        if (isDelim(ch)) { ++i; nums.clear(); continue; }
        const qint64 s0 = i;
        while (i < n && !isWs(c[i]) && !isDelim(c[i])) ++i;
        const QByteArray tok = c.mid(s0, i - s0);
        bool isNum = false;
        const qreal v = tok.toDouble(&isNum);
        if (isNum) { nums.push_back(v); continue; }
        if (tok == "q") {
            std::array<qreal,6> a{};
            for (int k = 0; k < 6; ++k) a[k] = st.m[k];
            stack.push_back(a);
        } else if (tok == "Q") {
            if (!stack.isEmpty()) {
                const auto a = stack.takeLast();
                for (int k = 0; k < 6; ++k) st.m[k] = a[k];
            }
        } else if (tok == "cm" && nums.size() >= 6) {
            const qreal a[6] = { nums[nums.size()-6], nums[nums.size()-5], nums[nums.size()-4],
                                 nums[nums.size()-3], nums[nums.size()-2], nums[nums.size()-1] };
            qreal out[6];
            matMul(a, st.m, out);
            for (int k = 0; k < 6; ++k) st.m[k] = out[k];
        } else if (tok == "BI") {
            const qint64 ei = c.indexOf("EI", i);
            i = (ei < 0) ? n : ei + 2;
        }
        nums.clear();
    }
    //  Offene `q` zurückrollen: der Zustand VOR der äußersten offenen Klammer
    //  ist das, was ein vorangestelltes `Q` je Ebene wiederherstellt.
    st.openSaves = stack.size();
    if (!stack.isEmpty())
        for (int k = 0; k < 6; ++k) st.m[k] = stack.first()[k];
    return st;
}

struct PageJob {
    int        objNum = -1;        // Objektnummer der Seite
    int        gen    = 0;
    qreal      heightPt = 0.0;     // ANGEZEIGTE Höhe (für die Y-Spiegelung)
    qreal      widthPt  = 0.0;     // ANGEZEIGTE Breite
    int        rot      = 0;       // /Rotate der Seite (0/90/180/270)
    QByteArray cm;                 // Abbildung Anzeige -> Benutzerraum (leer = Identität)
    QByteArray ops;                // erzeugte Zeichenbefehle
    QSet<QByteArray> fonts;        // benötigte Standard-14-Namen
    QSet<QString>    embedFams;    // Familien, die eingebettet werden müssen
    bool       needsAlpha = false; // /ExtGState für Deckkraft nötig?
    QHash<QByteArray, QString> images;
};

} // namespace

namespace mg {

namespace {

//  Hängt die Anmerkungen an eine BESTEHENDE Datei an (append-only). `boxes`
//  müssen bereits auf die Seiten DIESER Datei bezogen sein.
bool appendAnnotations(const QString& inputPath, const QString& outputPath,
                       const QVector<PdfEditBox>& boxes, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };

    if (boxes.isEmpty())
        return fail("keine Anmerkungen");

    QFile in(inputPath);
    if (!in.open(QIODevice::ReadOnly)) return fail("Quelle nicht lesbar");
    const QByteArray buf = in.readAll();
    in.close();
    if (buf.size() < 32 || !buf.startsWith("%PDF-")) return fail("kein PDF");
    if (buf.contains("/Encrypt")) return fail("verschlüsselt");

    const int sxi = buf.lastIndexOf("startxref");
    if (sxi < 0) return fail("kein startxref");
    qint64 prevXrefOffset = -1;
    {
        qint64 p = sxi + 9; while (p < buf.size() && isWs(buf[p])) ++p;
        const qint64 s = p; while (p < buf.size() && buf[p] >= '0' && buf[p] <= '9') ++p;
        bool okv = false; prevXrefOffset = buf.mid(s, p - s).toLongLong(&okv);
        if (!okv || prevXrefOffset < 0 || prevXrefOffset >= buf.size())
            return fail("startxref ungültig");
        qint64 q = prevXrefOffset; while (q < buf.size() && isWs(buf[q])) ++q;
        if (buf.mid(q, 4) != "xref") return fail("kein klassisches xref (XRef-Stream)");
    }

    const QHash<int, ObjLoc> objs = scanObjects(buf);
    if (objs.isEmpty()) return fail("keine Objekte");

    auto bodyOf = [&](int n) -> QByteArray {
        const auto it = objs.constFind(n);
        return (it == objs.constEnd()) ? QByteArray() : objectBody(buf, it->offset);
    };
    auto dictOf = [&](int n) -> QByteArray { return dictOfObject(bodyOf(n)); };

    auto streamDataOf = [&](int n, bool* ok) -> QByteArray {
        *ok = false;
        const QByteArray body = bodyOf(n);
        if (body.isEmpty()) return {};
        const QByteArray d = dictOfObject(body);
        const QByteArray filt = nameValue(d, "Filter");
        const bool fl = (filt == "/FlateDecode");
        if (!filt.isEmpty() && !fl) return {};
        qint64 sp = body.indexOf("stream");
        if (sp < 0) return {};
        sp += 6;
        if (sp < body.size() && body[sp] == '\r') ++sp;
        if (sp < body.size() && body[sp] == '\n') ++sp;
        const qint64 ep = body.indexOf("endstream", sp);
        if (ep < 0) return {};
        qint64 len = ep - sp;
        const qint64 dl = streamLength(d, buf, objs);
        if (dl >= 0 && dl <= len) len = dl;
        const QByteArray raw = body.mid(sp, len);
        if (!fl) { *ok = true; return raw; }
        bool iok = false;
        const QByteArray inf = zInflate(raw, &iok);
        if (!iok) return {};
        *ok = true;
        return inf;
    };

    int rootNum = -1;
    {
        static const QRegularExpression re(QStringLiteral("/Root\\s+(\\d+)\\s+(\\d+)\\s+R"));
        auto it = re.globalMatch(QString::fromLatin1(buf));
        while (it.hasNext()) rootNum = it.next().captured(1).toInt();
    }
    if (rootNum < 0 || !objs.contains(rootNum)) return fail("kein /Root");

    QVector<int> pageObjs;
    {
        int guard = 0;
        std::function<bool(int,int)> walk = [&](int num, int depth) -> bool {
            if (++guard > 100000 || depth > 50) return false;
            const QByteArray d = dictOf(num);
            if (d.isEmpty()) return false;
            if (nameValue(d, "Type") == "/Page") { pageObjs.push_back(num); return true; }
            const qint64 kp = findKey(d, "Kids");
            if (kp < 0 || d[kp] != '[') return false;
            const qint64 ke = skipValue(d, kp);
            const QByteArray kids = d.mid(kp, ke - kp);
            static const QRegularExpression kre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
            auto it = kre.globalMatch(QString::fromLatin1(kids));
            while (it.hasNext())
                if (!walk(it.next().captured(1).toInt(), depth + 1)) return false;
            return true;
        };
        const int pagesRoot = refValue(dictOf(rootNum), "Pages");
        if (pagesRoot < 0 || !walk(pagesRoot, 0)) return fail("Seitenbaum nicht lesbar");
    }
    if (pageObjs.isEmpty()) return fail("keine Seiten");

    auto pageBox = [&](int pageNum) -> QSizeF {
        int cur = pageNum;
        for (int hop = 0; hop < 50 && cur >= 0; ++hop) {
            const QByteArray d = dictOf(cur);
            const qint64 mp = findKey(d, "MediaBox");
            if (mp >= 0 && d[mp] == '[') {
                const qint64 me = skipValue(d, mp);
                const QByteArray arr = d.mid(mp + 1, (me - 1) - (mp + 1));
                const QList<QByteArray> parts = arr.simplified().split(' ');
                if (parts.size() >= 4) {
                    bool a=false,b=false,c=false,e=false;
                    const double x0 = parts[0].toDouble(&a), y0 = parts[1].toDouble(&b);
                    const double x1 = parts[2].toDouble(&c), y1 = parts[3].toDouble(&e);
                    if (a && b && c && e) return QSizeF(qAbs(x1 - x0), qAbs(y1 - y0));
                }
            }
            cur = refValue(d, "Parent");
        }
        return QSizeF();
    };

    // Eigenes oder vom Seitenbaum GEERBTES /Rotate, normalisiert auf 0/90/180/270. Es entscheidet, wie die
    // Anmerkungen in den Benutzerraum abgebildet werden - ohne das landeten Notizen auf gedrehten Seiten quer.
    auto pageRotate = [&](int pageNum) -> int {
        int cur = pageNum;
        for (int hop = 0; hop < 50 && cur >= 0; ++hop) {
            const QByteArray d = dictOf(cur);
            const qint64 rp = findKey(d, "Rotate");
            if (rp >= 0) {
                const qint64 re = skipValue(d, rp);
                bool okv = false;
                const int r = d.mid(rp, re - rp).simplified().toInt(&okv);
                if (okv) {
                    const int n = ((r % 360) + 360) % 360;
                    return (n / 90) * 90;
                }
            }
            cur = refValue(d, "Parent");
        }
        return 0;
    };

    QHash<int, PageJob> jobs;      // Seitenindex -> Auftrag

    for (const PdfEditBox& b : boxes) {
        if (b.page < 0 || b.page >= pageObjs.size())
            return fail("Seitenindex außerhalb");

        PageJob& job = jobs[b.page];
        if (job.objNum < 0) {
            job.objNum   = pageObjs.at(b.page);
            const auto it = objs.constFind(job.objNum);
            job.gen      = (it == objs.constEnd()) ? 0 : it->gen;
            const QSizeF box = pageBox(job.objNum);
            if (box.isEmpty()) return fail("Seitengröße unbekannt");
            job.rot = pageRotate(job.objNum);
            //  ANGEZEIGTE Maße: bei 90°/270° tauschen Breite und Höhe (genau
            //  das liefert auch QPdfDocument::pagePointSize, auf dem die
            //  Anzeige und damit die Notiz-Koordinaten beruhen).
            const bool quarter = (job.rot == 90 || job.rot == 270);
            job.widthPt  = quarter ? box.height() : box.width();
            job.heightPt = quarter ? box.width()  : box.height();
            // Abbildung angezeigter Raum -> Benutzerraum als cm-Matrix: im angezeigten Raum liegt der Ursprung unten links,
            // der Benutzerraum ist die ungedrehte Seite. Ohne Drehung ist das die Identität.
            switch (job.rot) {
            case 90:                                   // Anzeige 90° im UZS
                job.cm = "0 1 -1 0 " + num(box.width()) + " 0 cm\n";
                break;
            case 180:
                job.cm = "-1 0 0 -1 " + num(box.width()) + " "
                       + num(box.height()) + " cm\n";
                break;
            case 270:                                  // Anzeige 90° gegen UZS
                job.cm = "0 -1 1 0 0 " + num(box.height()) + " cm\n";
                break;
            default:
                break;
            }
        }
        const qreal H = job.heightPt;
        auto Y = [H](qreal yTop) { return H - yTop; };

        QByteArray& o = job.ops;
        o += "q\n";                                   // Zustand sichern
        o += job.cm;                                  // Drehung der Seite (falls vorhanden)

        const bool hasFill   = b.fill.alpha()   > 0;
        const bool hasStroke = b.stroke.alpha() > 0;

        auto pushAlpha = [&](int fillA, int strokeA) {
            if (fillA >= 255 && strokeA >= 255) return;
            job.needsAlpha = true;
            o += "/GS" + QByteArray::number(fillA) + "_"
               + QByteArray::number(strokeA) + " gs\n";
        };

        switch (b.kind) {
        case PdfAnnKind::Rect:
        case PdfAnnKind::Ellipse: {
            pushAlpha(hasFill ? b.fill.alpha() : 255, hasStroke ? b.stroke.alpha() : 255);
            if (hasFill)   o += rgb(b.fill)   + " rg\n";
            if (hasStroke) o += rgb(b.stroke) + " RG\n";
            o += num(qMax(0.0, b.lineWidth)) + " w\n";
            const qreal x = b.rect.x(), w = b.rect.width(), h = b.rect.height();
            const qreal yb = Y(b.rect.y() + h);          // untere Kante
            if (b.kind == PdfAnnKind::Rect) {
                o += num(x) + " " + num(yb) + " " + num(w) + " " + num(h) + " re\n";
            } else {
                const qreal cx = x + w/2, cy = yb + h/2, rx = w/2, ry = h/2;
                const qreal kx = rx * 0.5523, ky = ry * 0.5523;
                o += num(cx + rx) + " " + num(cy) + " m\n";
                o += num(cx + rx) + " " + num(cy + ky) + " " + num(cx + kx) + " " + num(cy + ry)
                   + " " + num(cx) + " " + num(cy + ry) + " c\n";
                o += num(cx - kx) + " " + num(cy + ry) + " " + num(cx - rx) + " " + num(cy + ky)
                   + " " + num(cx - rx) + " " + num(cy) + " c\n";
                o += num(cx - rx) + " " + num(cy - ky) + " " + num(cx - kx) + " " + num(cy - ry)
                   + " " + num(cx) + " " + num(cy - ry) + " c\n";
                o += num(cx + kx) + " " + num(cy - ry) + " " + num(cx + rx) + " " + num(cy - ky)
                   + " " + num(cx + rx) + " " + num(cy) + " c\n";
            }
            if (hasFill && hasStroke && b.lineWidth > 0) o += "B\n";
            else if (hasFill)                            o += "f\n";
            else                                         o += "S\n";
            break;
        }
        case PdfAnnKind::Freehand: {
            if (b.points.size() < 2) { o += "Q\n"; continue; }
            pushAlpha(255, hasStroke ? b.stroke.alpha() : 255);
            o += rgb(b.stroke) + " RG\n";
            o += num(qMax(0.1, b.lineWidth)) + " w\n1 J\n1 j\n";   // runde Enden/Ecken
            o += num(b.points.at(0).x()) + " " + num(Y(b.points.at(0).y())) + " m\n";
            for (int i = 1; i < b.points.size(); ++i)
                o += num(b.points.at(i).x()) + " " + num(Y(b.points.at(i).y())) + " l\n";
            o += "S\n";
            break;
        }
        case PdfAnnKind::Arrow: {
            if (b.points.size() < 2) { o += "Q\n"; continue; }
            const QPointF p0 = b.points.first(), p1 = b.points.last();
            pushAlpha(255, hasStroke ? b.stroke.alpha() : 255);
            o += rgb(b.stroke) + " RG\n" + rgb(b.stroke) + " rg\n";
            o += num(qMax(0.1, b.lineWidth)) + " w\n1 J\n1 j\n";
            o += num(p0.x()) + " " + num(Y(p0.y())) + " m\n";
            o += num(p1.x()) + " " + num(Y(p1.y())) + " l\nS\n";
            const qreal dx = p1.x() - p0.x(), dy = Y(p1.y()) - Y(p0.y());
            const qreal len = std::hypot(dx, dy);
            if (len > 0.001) {
                const qreal ux = dx/len, uy = dy/len;
                const qreal head = qMax(4.0, b.lineWidth * 4.0);
                const qreal hw   = head * 0.45;
                const qreal bx = p1.x() - ux*head,  by = Y(p1.y()) - uy*head;
                o += num(p1.x()) + " " + num(Y(p1.y())) + " m\n";
                o += num(bx - uy*hw) + " " + num(by + ux*hw) + " l\n";
                o += num(bx + uy*hw) + " " + num(by - ux*hw) + " l\nf\n";
            }
            break;
        }
        case PdfAnnKind::Stamp: {
            // Das Bild wird EINMAL je Datei eingebettet (gleicher Pfad, gleiches XObject) und hier nur platziert. Die
            // `cm`-Matrix bildet das Einheitsquadrat auf das Box-Rechteck ab, Ursprung unten-links.
            if (b.imagePath.isEmpty()) { o += "Q\n"; continue; }
            const QByteArray name = "MGI" + QByteArray::number(
                                        qHash(b.imagePath) & 0xffffff, 16).toUpper();
            job.images.insert(name, b.imagePath);
            if (b.fill.alpha() > 0 && b.fill.alpha() < 255) {
                job.needsAlpha = true;
                o += "/GS" + QByteArray::number(b.fill.alpha()) + "_"
                   + QByteArray::number(b.fill.alpha()) + " gs\n";
            }
            o += num(b.rect.width()) + " 0 0 " + num(b.rect.height()) + " "
               + num(b.rect.x()) + " " + num(Y(b.rect.y() + b.rect.height())) + " cm\n";
            o += "/" + name + " Do\n";
            break;
        }
        case PdfAnnKind::Redact: {
            //  Schwärzung: eine deckende Fläche. Der Text darunter ist bereits
            //  aus dem Strom entfernt (PdfContentEditor) - die Fläche macht
            //  sichtbar, DASS hier etwas entfernt wurde.
            const QColor cover = b.highlight.alpha() > 0 ? b.highlight : QColor(0, 0, 0);
            o += rgb(cover) + " rg\n";
            o += num(b.rect.x()) + " " + num(Y(b.rect.y() + b.rect.height())) + " "
               + num(b.rect.width()) + " " + num(b.rect.height()) + " re\nf\n";
            break;
        }
        case PdfAnnKind::Markup: {
            // Textmarkierung: mehrere Bereiche in einem Objekt (je zwei Ecken in `points`). Markieren füllt die Fläche und
            // MULTIPLIZIERT, damit der Text darunter lesbar bleibt - so machen es die verbreiteten Betrachter auch.
            if (b.points.size() < 2) { o += "Q\n"; continue; }
            const int alpha = hasStroke ? b.stroke.alpha() : 255;
            if (b.markupStyle == 0) {
                job.needsAlpha = true;
                //  Eigener Zustand mit Multiplizieren (s. PageJob::gsNames).
                o += "/GSM" + QByteArray::number(alpha) + "_" + QByteArray::number(alpha) + " gs\n";
                o += rgb(b.stroke) + " rg\n";
                for (int i = 0; i + 1 < b.points.size(); i += 2) {
                    const QRectF q = QRectF(b.points.at(i), b.points.at(i+1)).normalized();
                    o += num(q.x()) + " " + num(Y(q.y() + q.height())) + " "
                       + num(q.width()) + " " + num(q.height()) + " re\n";
                }
                o += "f\n";
            } else {
                pushAlpha(255, alpha);
                o += rgb(b.stroke) + " RG\n";
                for (int i = 0; i + 1 < b.points.size(); i += 2) {
                    const QRectF q = QRectF(b.points.at(i), b.points.at(i+1)).normalized();
                    const qreal t = qMax(0.5, q.height() / 14.0);
                    //  Unterstreichen knapp über der Unterkante, Durchstreichen
                    //  auf halber Höhe des Bereichs.
                    const qreal yTop = (b.markupStyle == 1) ? q.y() + q.height() - t
                                                            : q.y() + q.height() / 2.0;
                    o += num(t) + " w\n";
                    o += num(q.x()) + " " + num(Y(yTop)) + " m\n";
                    o += num(q.x() + q.width()) + " " + num(Y(yTop)) + " l\nS\n";
                }
            }
            break;
        }
        case PdfAnnKind::Text:
        case PdfAnnKind::Replace: {
            //  Hat die Familie KEINE echte Standard-14-Entsprechung, wird die
            //  Schrift eingebettet - sonst sähe die Ausgabe anders aus als der
            //  Bildschirm (vorher wurde still durch Helvetica ersetzt).
            const bool embed = mg::PdfFontEmbed::needsEmbedding(b.fontFamily);
            const QByteArray baseFont = embed
                ? ("EMB_" + b.fontFamily.toUtf8().toBase64(
                       QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
                   + (b.bold ? "_B" : "") + (b.italic ? "_I" : ""))
                : baseFontFor(b.fontFamily, b.bold, b.italic);
            const bool paper = b.highlight.alpha() > 0;
            if (paper) {
                pushAlpha(b.highlight.alpha(), 255);
                o += rgb(b.highlight) + " rg\n";
                o += num(b.rect.x()) + " " + num(Y(b.rect.y() + b.rect.height())) + " "
                   + num(b.rect.width()) + " " + num(b.rect.height()) + " re\nf\n";
                //  Deckkraft für den Text wieder zurücksetzen.
                if (b.highlight.alpha() < 255) { o += "/GS255_255 gs\n"; job.needsAlpha = true; }
            }
            if (b.text.isEmpty()) break;

            //  Text muss in WinAnsi darstellbar sein - die Standard-14 nutzen
            //  genau diese Kodierung. Sonst: Fallback (Raster).
            bool encOk = false;
            const auto enc = mg::pdfenc::Encoding::fromEncodingValue("/WinAnsiEncoding", &encOk);
            const qreal pad  = 2.0;
            const qreal size = qMax(1.0, b.fontSizePt);
            const QVector<QString> lines =
                wrapText(b.text, qMax(1.0, b.rect.width() - 2*pad), baseFont, size);

            job.fonts.insert(baseFont);
            if (embed) job.embedFams.insert(b.fontFamily
                                            + (b.bold ? QStringLiteral("\x01B") : QString())
                                            + (b.italic ? QStringLiteral("\x01I") : QString()));
            o += "BT\n/F_" + baseFont + " " + num(size) + " Tf\n";
            o += rgb(b.color) + " rg\n";
            const qreal lead = size * 1.2;
            qreal ty = b.rect.y() + pad + size;             // Grundlinie der 1. Zeile
            for (const QString& line : lines) {
                if (ty > b.rect.y() + b.rect.height() + lead) break;   // unten raus
                QByteArray bytes;
                if (!enc.encode(line, &bytes))
                    return fail("Notiztext in WinAnsi nicht darstellbar -> Fallback");
                qreal tx = b.rect.x() + pad;
                if (b.alignment != 0) {
                    const qreal wpt = line.size() * avgCharWidth(baseFont, size);
                    const qreal freeW = qMax(0.0, b.rect.width() - 2*pad - wpt);
                    tx += (b.alignment == 1) ? freeW/2 : freeW;
                }
                o += "1 0 0 1 " + num(tx) + " " + num(Y(ty)) + " Tm\n";
                o += parenString(bytes) + " Tj\n";
                ty += lead;
            }
            o += "ET\n";
            break;
        }
        default:
            break;
        }
        o += "Q\n";                                    // Zustand zurück
    }

    if (jobs.isEmpty()) return fail("nichts zu zeichnen");

    QByteArray out = buf;
    if (!out.endsWith('\n')) out += '\n';
    int nextObj = 0;
    for (auto it = objs.constBegin(); it != objs.constEnd(); ++it)
        nextObj = qMax(nextObj, it.key());
    ++nextObj;

    struct XEntry { int num; qint64 off; int gen; };
    QVector<XEntry> xentries;
    auto addObject = [&](int num, int gen, const QByteArray& body) {
        xentries.push_back({ num, out.size(), gen });
        out += QByteArray::number(num) + " " + QByteArray::number(gen) + " obj\n";
        out += body;
        out += "\nendobj\n";
    };
    //  Strom-Objekt (Bilder): `/Length` setzt der Schreiber, nicht der Aufrufer.
    auto addStream = [&](int num, int gen, const QByteArray& dictExtra,
                         const QByteArray& data) {
        xentries.push_back({ num, out.size(), gen });
        out += QByteArray::number(num) + " " + QByteArray::number(gen) + " obj\n";
        out += "<< " + dictExtra + " /Length " + QByteArray::number(data.size())
             + " >>\nstream\n";
        out += data;
        out += "\nendstream\nendobj\n";
    };

    //  Schriften und Transparenz-Zustände einmal je Dokument anlegen.
    QHash<QByteArray, int> fontObjNum;
    QHash<QByteArray, int> gsObjNum;
    //  Familien, die eingebettet werden sollen, zuerst bauen - schlägt das
    //  fehl, ist die ganze Ausgabe zu verwerfen (der Raster-Weg zeigt die
    //  Schrift dann korrekt, s. Kommentar in PdfEditController).
    QHash<QString, mg::EmbeddedFont> embedded;
    for (auto it = jobs.cbegin(); it != jobs.cend(); ++it) {
        for (const QString& key : it.value().embedFams) {
            if (embedded.contains(key)) continue;
            const QString fam = key.section(QChar(0x01), 0, 0);
            const bool bold   = key.contains(QStringLiteral("\x01B"));
            const bool italic = key.contains(QStringLiteral("\x01I"));
            mg::EmbeddedFont ef;
            QString ferr;
            if (!mg::PdfFontEmbed::build(fam, bold, italic, &ef, &ferr))
                return fail("Schrift nicht einbettbar -> Fallback");
            embedded.insert(key, ef);
        }
    }
    for (auto it = jobs.cbegin(); it != jobs.cend(); ++it) {
        for (const QByteArray& f : it.value().fonts) {
            if (fontObjNum.contains(f)) continue;
            const int n = nextObj++;
            fontObjNum.insert(f, n);
            if (!f.startsWith("EMB_")) {
                addObject(n, 0, "<< /Type /Font /Subtype /Type1 /BaseFont /" + f
                                + " /Encoding /WinAnsiEncoding >>");
                continue;
            }
            //  Eingebettet: Schriftprogramm + Deskriptor + Font-Dict.
            QByteArray core = f.mid(4);
            const bool bold   = core.endsWith("_B") || core.contains("_B_I") || core.endsWith("_B_I");
            const bool italic = core.endsWith("_I");
            core.replace("_B", "").replace("_I", "");
            const QString fam = QString::fromUtf8(
                QByteArray::fromBase64(core, QByteArray::Base64UrlEncoding));
            const QString key = fam + (bold ? QStringLiteral("\x01B") : QString())
                                    + (italic ? QStringLiteral("\x01I") : QString());
            const auto eit = embedded.constFind(key);
            if (eit == embedded.constEnd()) return fail("eingebettete Schrift fehlt");
            const mg::EmbeddedFont& ef = eit.value();

            const int ffNum = nextObj++;
            {
                const QByteArray def = zDeflate(ef.sfnt);
                if (def.isEmpty()) return fail("Schriftprogramm nicht komprimierbar");
                xentries.push_back({ ffNum, out.size(), 0 });
                out += QByteArray::number(ffNum) + " 0 obj\n";
                out += "<< /Length " + QByteArray::number(def.size())
                     + " /Length1 " + QByteArray::number(ef.sfnt.size())
                     + " /Filter /FlateDecode >>\nstream\n";
                out += def;
                out += "\nendstream\nendobj\n";
            }
            const int fdNum = nextObj++;
            addObject(fdNum, 0,
                "<< /Type /FontDescriptor /FontName /" + ef.psName
                + " /Flags " + QByteArray::number(ef.flags)
                + " /FontBBox [" + num(ef.bbox[0]) + " " + num(ef.bbox[1]) + " "
                                 + num(ef.bbox[2]) + " " + num(ef.bbox[3]) + "]"
                + " /ItalicAngle " + num(ef.italicAngle)
                + " /Ascent " + num(ef.ascent)
                + " /Descent " + num(ef.descent)
                + " /CapHeight " + num(ef.capHeight)
                + " /StemV 80"
                + " /FontFile2 " + QByteArray::number(ffNum) + " 0 R >>");
            QByteArray ws;
            for (int w : ef.widths) ws += QByteArray::number(w) + " ";
            addObject(n, 0,
                "<< /Type /Font /Subtype /TrueType /BaseFont /" + ef.psName
                + " /FirstChar " + QByteArray::number(ef.firstChar)
                + " /LastChar "  + QByteArray::number(ef.firstChar + ef.widths.size() - 1)
                + " /Widths [" + ws.trimmed() + "]"
                + " /Encoding /WinAnsiEncoding"
                + " /FontDescriptor " + QByteArray::number(fdNum) + " 0 R >>");
        }
    }
    // Benötigte Alpha-Kombinationen aus den erzeugten Strömen einsammeln; ein "M" nach "GS" verlangt zusätzlich
    // MULTIPLIZIEREN - das braucht die Textmarkierung, damit der Text darunter lesbar bleibt.
    QSet<QByteArray> gsNames;
    {
        static const QRegularExpression gre(QStringLiteral("/(GSM?\\d+_\\d+) gs"));
        for (auto it = jobs.cbegin(); it != jobs.cend(); ++it) {
            auto m = gre.globalMatch(QString::fromLatin1(it.value().ops));
            while (m.hasNext())
                gsNames.insert(m.next().captured(1).toLatin1());
        }
    }
    for (const QByteArray& g : gsNames) {
        const bool multiply = g.startsWith("GSM");
        const int  skip = multiply ? 3 : 2;
        const int us = g.indexOf('_');
        const int fa = g.mid(skip, us - skip).toInt();
        const int sa = g.mid(us + 1).toInt();
        const int n  = nextObj++;
        gsObjNum.insert(g, n);
        addObject(n, 0, "<< /Type /ExtGState /ca " + num(fa/255.0)
                        + " /CA " + num(sa/255.0)
                        + (multiply ? " /BM /Multiply" : "") + " >>");
    }

    // Bilder EINMAL je Dateipfad einbetten, auch wenn dasselbe Bild mehrfach platziert ist. Ein nicht lesbares
    // Bild lässt die Anmerkung ausfallen, statt den ganzen Export scheitern zu lassen.
    QHash<QString, int> imgObjNum;                 // Pfad -> Objektnummer
    {
        for (auto it = jobs.cbegin(); it != jobs.cend(); ++it) {
            for (auto im = it.value().images.cbegin(); im != it.value().images.cend(); ++im) {
                const QString& path = im.value();
                if (imgObjNum.contains(path))
                    continue;
                mg::PdfImageData data;
                QString ierr;
                if (!mg::PdfImageEmbed::encodeFile(path, &data, 1600, &ierr)) {
                    qInfo("PdfVectorExport: Bild uebersprungen (%s)", qPrintable(ierr));
                    continue;
                }
                int smask = -1;
                if (data.hasAlpha()) {
                    smask = nextObj++;
                    addStream(smask, 0, mg::PdfImageEmbed::smaskDict(data), data.alpha);
                }
                const int n = nextObj++;
                addStream(n, 0, mg::PdfImageEmbed::imageDict(data, smask), data.rgb);
                imgObjNum.insert(path, n);
            }
        }
    }

    //  Je betroffener Seite: neuer Content-Stream + aktualisiertes Seiten-Objekt.
    for (auto it = jobs.cbegin(); it != jobs.cend(); ++it) {
        const PageJob& job = it.value();
        // Erst die offen gelassenen `q` der Seite schließen, dann zeichnen: sonst erbt jeder Strich die letzte `cm` der
        // Seite. Der Deckel ist eine Sicherung gegen einen verkorksten Strom.
        QByteArray ops;
        {
            TrailingState st;
            bool known = false;
            const QByteArray pd = dictOf(job.objNum);
            const qint64 cp = findKey(pd, "Contents");
            if (cp >= 0) {
                QVector<int> nums;
                if (pd[cp] == '[') {
                    const qint64 ce = skipValue(pd, cp);
                    static const QRegularExpression cre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
                    auto mit = cre.globalMatch(QString::fromLatin1(pd.mid(cp, ce - cp)));
                    while (mit.hasNext()) nums.push_back(mit.next().captured(1).toInt());
                } else {
                    const int n2 = refValue(pd, "Contents");
                    if (n2 >= 0) nums.push_back(n2);
                }
                QByteArray whole;
                known = !nums.isEmpty();
                for (int n2 : nums) {
                    bool sok = false;
                    const QByteArray part = streamDataOf(n2, &sok);
                    if (!sok) { known = false; break; }      // nicht lesbar -> nichts raten
                    whole += part;
                    whole += '\n';
                }
                if (known) st = trailingState(whole);
            }
            if (known) {
                //  1) offene `q` schließen (Klemme/Farben/Linienbreite),
                for (int k = 0; k < qMin(st.openSaves, 32); ++k) ops += "Q\n";
                //  2) eine stehen gebliebene Matrix INVERTIEREN - sie lässt sich
                //     durch kein `Q` zurücknehmen (s. trailingState).
                const qreal det = st.m[0]*st.m[3] - st.m[1]*st.m[2];
                const bool identity = qFuzzyCompare(st.m[0], qreal(1)) && qFuzzyIsNull(st.m[1])
                                   && qFuzzyIsNull(st.m[2]) && qFuzzyCompare(st.m[3], qreal(1))
                                   && qFuzzyIsNull(st.m[4]) && qFuzzyIsNull(st.m[5]);
                if (!identity) {
                    if (qAbs(det) < 1e-9)
                        return fail("Seitenmatrix nicht umkehrbar -> Fallback");
                    const qreal inv[6] = {
                         st.m[3]/det, -st.m[1]/det,
                        -st.m[2]/det,  st.m[0]/det,
                        (st.m[2]*st.m[5] - st.m[3]*st.m[4])/det,
                        (st.m[1]*st.m[4] - st.m[0]*st.m[5])/det
                    };
                    for (int k = 0; k < 6; ++k) ops += num(inv[k]) + " ";
                    ops += "cm\n";
                }
            }
        }
        ops += job.ops;
        const QByteArray def = zDeflate(ops);
        if (def.isEmpty() && !ops.isEmpty()) return fail("Deflate fehlgeschlagen");
        const int csNum = nextObj++;
        {
            xentries.push_back({ csNum, out.size(), 0 });
            out += QByteArray::number(csNum) + " 0 obj\n";
            out += "<< /Length " + QByteArray::number(def.size()) + " /Filter /FlateDecode >>\n";
            out += "stream\n";
            out += def;
            out += "\nendstream\nendobj\n";
        }

        //  Seiten-Dict neu schreiben: /Contents -> Array, /Resources ergänzen.
        QByteArray pd = dictOf(job.objNum);
        if (pd.isEmpty()) return fail("Seiten-Dict leer");

        {
            const qint64 cp = findKey(pd, "Contents");
            if (cp < 0) return fail("kein /Contents");
            const qint64 ce = skipValue(pd, cp);
            const QByteArray old = pd.mid(cp, ce - cp).trimmed();
            QByteArray repl;
            if (old.startsWith('['))
                repl = old.left(old.size() - 1) + " " + QByteArray::number(csNum) + " 0 R]";
            else
                repl = "[" + old + " " + QByteArray::number(csNum) + " 0 R]";
            pd.replace(cp, ce - cp, repl);
        }

        // Ein bestehendes /Resources-Dict wird um die fehlenden Einträge erweitert; steht dort eine Referenz oder fehlt
        // der Eintrag, wird ein eigenes gesetzt - die geerbten Ressourcen bleiben über den Seitenbaum erhalten.
        {
            QByteArray addFonts;
            for (const QByteArray& f : job.fonts)
                addFonts += "/F_" + f + " " + QByteArray::number(fontObjNum.value(f)) + " 0 R ";
            QByteArray addGs;
            for (const QByteArray& g : gsNames)
                addGs += "/" + g + " " + QByteArray::number(gsObjNum.value(g)) + " 0 R ";

            const qint64 rp = findKey(pd, "Resources");
            QByteArray inner;                       // Inhalt des Resources-Dicts
            qint64 rs = -1, re_ = -1;
            if (rp >= 0 && pd[rp] == '<') {
                re_ = skipValue(pd, rp);
                rs  = rp;
                inner = pd.mid(rp + 2, (re_ - 2) - (rp + 2));
            } else if (rp >= 0) {
                // Bei `/Resources 4 0 R` wurde früher ein ZWEITES `/Resources` angehängt; ein Dict mit doppeltem Schlüssel ist
                // ungültig, und Leser nahmen unser fast leeres - die Seite verlor Schriften und Text. Jetzt wird der
                // referenzierte Inhalt in die Seite geholt und dort ergänzt; das gemeinsame Objekt bleibt unangetastet.
                const int rn = refValue(pd, "Resources");
                const QByteArray refBody = (rn >= 0) ? bodyOf(rn).trimmed() : QByteArray();
                if (refBody.startsWith("<<") && refBody.endsWith(">>")) {
                    inner = refBody.mid(2, refBody.size() - 4);
                    re_ = skipValue(pd, rp);
                    rs  = rp;
                }
            }
            auto mergeSub = [](QByteArray dict, const char* key, const QByteArray& add) {
                if (add.isEmpty()) return dict;
                const qint64 kp = findKey(dict, key);
                if (kp >= 0 && dict[kp] == '<') {
                    const qint64 ke = skipValue(dict, kp);
                    dict.insert(ke - 2, add);       // vor das schliessende >>
                    return dict;
                }
                return dict + " /" + key + " << " + add + ">>";
            };
            QByteArray addXObj;
            for (auto im = job.images.cbegin(); im != job.images.cend(); ++im) {
                const int n = imgObjNum.value(im.value(), -1);
                if (n >= 0)
                    addXObj += "/" + im.key() + " " + QByteArray::number(n) + " 0 R ";
            }
            inner = mergeSub(inner, "Font",      addFonts);
            inner = mergeSub(inner, "ExtGState", addGs);
            inner = mergeSub(inner, "XObject",   addXObj);
            const QByteArray newRes = "<< " + inner + " >>";
            if (rs >= 0) pd.replace(rs, re_ - rs, newRes);
            else         pd += " /Resources " + newRes;
        }

        addObject(job.objNum, job.gen, "<<" + pd + ">>");
    }

    //  Neue klassische XRef-Sektion (je Objekt eine Subsektion) + Trailer.
    std::sort(xentries.begin(), xentries.end(),
              [](const XEntry& a, const XEntry& b){ return a.num < b.num; });
    const qint64 xrefOff = out.size();
    out += "xref\n";
    for (const XEntry& e : xentries) {
        out += QByteArray::number(e.num) + " 1\n";
        out += QByteArray::number(e.off).rightJustified(10, '0') + " "
             + QByteArray::number(e.gen).rightJustified(5, '0') + " n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(nextObj)
         + " /Root " + QByteArray::number(rootNum) + " 0 R"
         + " /Prev " + QByteArray::number(prevXrefOffset) + " >>\n";
    out += "startxref\n" + QByteArray::number(xrefOff) + "\n%%EOF\n";

    QSaveFile sf(outputPath);
    if (!sf.open(QIODevice::WriteOnly)) return fail("Ziel nicht schreibbar");
    if (sf.write(out) != out.size()) { sf.cancelWriting(); return fail("Schreibfehler"); }
    if (!sf.commit()) return fail("Commit fehlgeschlagen");
    return true;
}

} // namespace

bool PdfVectorExport::exportAnnotations(const QString& inputPath, const QString& outputPath,
                                       const QVector<PdfEditBox>& boxes,
                                       QString* err) {
    // Der Seiten-Plan ist hier BEREITS angewendet: der Aufrufer übergibt die gebackene Arbeitsdatei und die auf
    // Ansichts-Seiten abgebildeten Notizen. Bleibt genau eine Aufgabe - anhängen, ohne Vorhandenes anzutasten.
    return appendAnnotations(inputPath, outputPath, boxes, err);
}

} // namespace mg
