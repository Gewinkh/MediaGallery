#include "pdf/edit/PdfTextLayout.h"
#include "pdf/edit/PdfObjects.h"
#include "pdf/edit/PdfEncodings.h"
#include "pdf/edit/PdfBaseFontWidths.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace mg::pdfobj;

namespace {

// ── 3×2-Matrix [a b c d e f] wie in PDF ─────────────────────────────────────
struct Mat {
    qreal a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    //  this ⋅ m  (erst this, dann m anwenden — PDF-Reihenfolge)
    Mat mul(const Mat& m) const {
        return { a*m.a + b*m.c,        a*m.b + b*m.d,
                 c*m.a + d*m.c,        c*m.b + d*m.d,
                 e*m.a + f*m.c + m.e,  e*m.b + f*m.d + m.f };
    }
    QPointF map(qreal x, qreal y) const { return QPointF(a*x + c*y + e, b*x + d*y + f); }
    //  Längenmaßstab in x- bzw. y-Richtung (für Breite/Höhe der Zeichen).
    qreal scaleX() const { return std::hypot(a, b); }
    qreal scaleY() const { return std::hypot(c, d); }
};

// ── Breitentabelle EINES Fonts ──────────────────────────────────────────────
struct FontMetrics {
    mg::pdfenc::Encoding enc;
    bool     cid      = false;      // 2-Byte-Codes
    qreal    defWidth = -1.0;       // /DW bzw. feste Dickte (Courier), sonst −1
    QHash<quint32, qreal> widths;   // Code → Breite in 1/1000 em
    bool valid = false;

    //  Breite eines Codes in Glyphenraum-Einheiten (1/1000 em).
    //  −1 = unbekannt → Aufrufer bricht ab (kein Raten).
    qreal widthOf(quint32 code) const {
        const auto it = widths.constFind(code);
        if (it != widths.constEnd()) return it.value();
        return defWidth;                    // −1, wenn es keinen Standard gibt
    }
};

//  Zahlen aus einem PDF-Array "[ 1 2 3 ]" (nur Zahlen, verschachtelt nicht).
QVector<qreal> numbersIn(const QByteArray& arr) {
    QVector<qreal> out;
    int i = 0;
    while (i < arr.size()) {
        while (i < arr.size() && !((arr[i] >= '0' && arr[i] <= '9')
                                   || arr[i] == '-' || arr[i] == '+' || arr[i] == '.')) ++i;
        if (i >= arr.size()) break;
        const int s = i;
        while (i < arr.size() && ((arr[i] >= '0' && arr[i] <= '9')
                                  || arr[i] == '-' || arr[i] == '+' || arr[i] == '.')) ++i;
        bool ok = false;
        const qreal v = arr.mid(s, i - s).toDouble(&ok);
        if (ok) out.push_back(v);
    }
    return out;
}

} // namespace

namespace mg {

bool PdfTextLayout::buildForPage(const QString& pdfPath, int pageIndex,
                                 QVector<PdfGlyph>* out, QString* err) {
    PdfPageText t;
    if (!buildForPage(pdfPath, pageIndex, &t, err)) return false;
    if (out) *out = t.glyphs;
    return true;
}

bool PdfTextLayout::buildForPage(const QString& pdfPath, int pageIndex,
                                 PdfPageText* page, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };
    if (!page) return fail("kein Ausgabeziel");
    *page = PdfPageText();
    QVector<PdfGlyph>* out = &page->glyphs;

    QFile in(pdfPath);
    if (!in.open(QIODevice::ReadOnly)) return fail("Quelle nicht lesbar");
    const QByteArray buf = in.readAll();
    in.close();
    if (buf.size() < 32 || !buf.startsWith("%PDF-")) return fail("kein PDF");
    if (buf.contains("/Encrypt")) return fail("verschlüsselt");

    const QHash<int, ObjLoc> objs = scanObjects(buf);
    if (objs.isEmpty()) return fail("keine Objekte");

    auto bodyOf = [&](int n) -> QByteArray {
        const auto it = objs.constFind(n);
        return (it == objs.constEnd()) ? QByteArray() : objectBody(buf, it->offset);
    };
    auto dictOf = [&](int n) -> QByteArray { return dictOfObject(bodyOf(n)); };
    auto streamOf = [&](int n, bool* ok) -> QByteArray {
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
        const qint64 dl = intValue(d, "Length");
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
    if (rootNum < 0) return fail("kein /Root");

    //  Seitenbaum → gesuchte Seite samt geerbtem /Resources.
    int pageNum = -1;
    QByteArray pageRes;
    {
        int seen = 0, guard = 0;
        std::function<bool(int, const QByteArray&, int)> walk =
            [&](int num, const QByteArray& inherited, int depth) -> bool {
                if (++guard > 100000 || depth > 50 || pageNum >= 0) return pageNum >= 0;
                const QByteArray d = dictOf(num);
                if (d.isEmpty()) return false;
                QByteArray res = inherited;
                const qint64 rp = findKey(d, "Resources");
                if (rp >= 0) {
                    if (d[rp] == '<') { const qint64 e = skipValue(d, rp); res = d.mid(rp, e - rp); }
                    else { const int rn = refValue(d, "Resources");
                           if (rn >= 0) res = QByteArray("<<") + dictOf(rn) + ">>"; }
                }
                if (nameValue(d, "Type") == "/Page") {
                    if (seen++ == pageIndex) { pageNum = num; pageRes = res; }
                    return true;
                }
                const qint64 kp = findKey(d, "Kids");
                if (kp < 0 || d[kp] != '[') return false;
                const qint64 ke = skipValue(d, kp);
                const QByteArray kids = d.mid(kp, ke - kp);
                static const QRegularExpression kre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
                auto it = kre.globalMatch(QString::fromLatin1(kids));
                while (it.hasNext() && pageNum < 0)
                    walk(it.next().captured(1).toInt(), res, depth + 1);
                return true;
            };
        const int pagesRoot = refValue(dictOf(rootNum), "Pages");
        if (pagesRoot < 0) return fail("kein /Pages");
        walk(pagesRoot, QByteArray(), 0);
    }
    if (pageNum < 0) return fail("Seite nicht gefunden");

    //  Seitenhöhe (für die Spiegelung nach oben-links).
    qreal pageH = 0.0;
    {
        int cur = pageNum;
        for (int hop = 0; hop < 50 && cur >= 0 && pageH <= 0.0; ++hop) {
            const QByteArray d = dictOf(cur);
            const qint64 mp = findKey(d, "MediaBox");
            if (mp >= 0 && d[mp] == '[') {
                const qint64 me = skipValue(d, mp);
                const QVector<qreal> v = numbersIn(d.mid(mp, me - mp));
                if (v.size() >= 4) pageH = qAbs(v[3] - v[1]);
            }
            cur = refValue(d, "Parent");
        }
    }
    if (pageH <= 0.0) return fail("Seitenhöhe unbekannt");

    // ── Fonts der Seite samt Breiten ────────────────────────────────────────
    QHash<QByteArray, FontMetrics> fonts;
    {
        const qint64 fp = findKey(pageRes, "Font");
        if (fp < 0) return fail("keine Fonts");
        QByteArray fontDict;
        if (pageRes[fp] == '<') { const qint64 e = skipValue(pageRes, fp);
                                  fontDict = pageRes.mid(fp + 2, (e - 2) - (fp + 2)); }
        else { const int fn = refValue(pageRes, "Font");
               if (fn < 0) return fail("Font-Ref"); fontDict = dictOf(fn); }

        qint64 i = 0;
        while (i < fontDict.size()) {
            while (i < fontDict.size() && isWs(fontDict[i])) ++i;
            if (i >= fontDict.size() || fontDict[i] != '/') { ++i; continue; }
            const qint64 ns = i;
            i = skipValue(fontDict, i);
            const QByteArray resName = fontDict.mid(ns, i - ns);
            const qint64 vs = i;
            i = skipValue(fontDict, vs);
            static const QRegularExpression rre(QStringLiteral("^\\s*(\\d+)\\s+(\\d+)\\s+R"));
            const auto m = rre.match(QString::fromLatin1(fontDict.mid(vs, i - vs)));
            if (!m.hasMatch()) continue;
            const QByteArray fd = dictOf(m.captured(1).toInt());

            FontMetrics fm;
            const QByteArray baseFont = nameValue(fd, "BaseFont");
            //  Courier ist per Definition dicktengleich (600/1000) — das ist
            //  keine Schätzung, sondern Teil der Schriftdefinition.
            if (baseFont.contains("Courier")) fm.defWidth = 600.0;

            if (nameValue(fd, "Subtype") == "/Type0") {
                fm.cid = true;
                if (nameValue(fd, "Encoding") != "/Identity-H")
                    return fail("Type0 ohne /Identity-H → nicht auswertbar");
                const int tu = refValue(fd, "ToUnicode");
                if (tu < 0) return fail("Type0 ohne /ToUnicode");
                bool sok = false;
                const QByteArray cm = streamOf(tu, &sok);
                if (!sok) return fail("/ToUnicode nicht lesbar");
                bool cok = false;
                fm.enc = mg::pdfenc::Encoding::fromCidToUnicode(cm, &cok);
                if (!cok) return fail("/ToUnicode nicht auswertbar");

                //  Breiten des Nachfahren-Fonts: /DW (Standard 1000) + /W.
                int desc = -1;
                const qint64 dp = findKey(fd, "DescendantFonts");
                if (dp >= 0 && fd[dp] == '[') {
                    const qint64 de = skipValue(fd, dp);
                    static const QRegularExpression dre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
                    const auto dm = dre.match(QString::fromLatin1(fd.mid(dp, de - dp)));
                    if (dm.hasMatch()) desc = dm.captured(1).toInt();
                }
                if (desc < 0) return fail("kein /DescendantFonts");
                const QByteArray dd = dictOf(desc);
                const qint64 dw = intValue(dd, "DW");
                fm.defWidth = (dw >= 0) ? qreal(dw) : 1000.0;   // Spezifikation: 1000
                const qint64 wp = findKey(dd, "W");
                if (wp >= 0 && dd[wp] == '[') {
                    //  /W kennt zwei Formen:  c [w1 w2 …]   und   cFirst cLast w
                    const qint64 we = skipValue(dd, wp);
                    const QByteArray warr = dd.mid(wp + 1, (we - 1) - (wp + 1));
                    int k = 0;
                    while (k < warr.size()) {
                        while (k < warr.size() && isWs(warr[k])) ++k;
                        if (k >= warr.size()) break;
                        const int s0 = k;
                        while (k < warr.size() && !isWs(warr[k]) && warr[k] != '[') ++k;
                        bool ok0 = false;
                        const int c0 = warr.mid(s0, k - s0).toInt(&ok0);
                        if (!ok0) { ++k; continue; }
                        while (k < warr.size() && isWs(warr[k])) ++k;
                        if (k < warr.size() && warr[k] == '[') {
                            const qint64 ae = skipValue(warr, k);
                            const QVector<qreal> ws = numbersIn(warr.mid(k, ae - k));
                            for (int j = 0; j < ws.size(); ++j)
                                fm.widths.insert(quint32(c0 + j), ws[j]);
                            k = ae;
                        } else {
                            const int s1 = k;
                            while (k < warr.size() && !isWs(warr[k])) ++k;
                            const int c1 = warr.mid(s1, k - s1).toInt();
                            while (k < warr.size() && isWs(warr[k])) ++k;
                            const int s2 = k;
                            while (k < warr.size() && !isWs(warr[k])) ++k;
                            bool ok2 = false;
                            const qreal w = warr.mid(s2, k - s2).toDouble(&ok2);
                            if (ok2 && c1 >= c0 && c1 - c0 < 65536)
                                for (int c = c0; c <= c1; ++c)
                                    fm.widths.insert(quint32(c), w);
                        }
                    }
                }
            } else {
                //  Einfacher Font: /Encoding + /FirstChar + /Widths.
                QByteArray encVal;
                const qint64 ep = findKey(fd, "Encoding");
                if (ep >= 0) {
                    if (fd[ep] == '/' || fd[ep] == '<') {
                        const qint64 e = skipValue(fd, ep); encVal = fd.mid(ep, e - ep);
                    } else { const int en = refValue(fd, "Encoding");
                             if (en >= 0) encVal = QByteArray("<<") + dictOf(en) + ">>"; }
                }
                bool eok = false;
                fm.enc = mg::pdfenc::Encoding::fromEncodingValue(encVal, &eok);
                if (!eok) return fail("unbekannter Glyphenname in /Differences");

                const qint64 first = intValue(fd, "FirstChar");
                const qint64 wp = findKey(fd, "Widths");
                if (wp >= 0 && first >= 0) {
                    QByteArray warr;
                    if (fd[wp] == '[') { const qint64 we = skipValue(fd, wp);
                                         warr = fd.mid(wp, we - wp); }
                    else { const int wn = refValue(fd, "Widths");
                           if (wn >= 0) warr = bodyOf(wn); }
                    const QVector<qreal> ws = numbersIn(warr);
                    for (int j = 0; j < ws.size(); ++j)
                        fm.widths.insert(quint32(first + j), ws[j]);
                }

                //  KEINE (oder unvollständige) /Widths? Bei einer der 14
                //  Standardschriften ist das ERLAUBT — die Spezifikation
                //  erwartet, dass der Betrachter ihre Maße kennt. Genau daran
                //  scheiterte „Text bearbeiten" auf solchen Dokumenten.
                //  Die Tabellen stammen aus den Adobe-AFM-Metriken; gefüllt
                //  wird über die Kodierung der Schrift (Code → Zeichen →
                //  Breite), damit WinAnsi, MacRoman und /Differences
                //  gleichermaßen stimmen. Bereits vorhandene Werte aus /Widths
                //  bleiben unangetastet — die Datei weiß es besser.
                int baseCount = 0;
                if (const mg::BaseWidth* table = mg::baseFontWidths(baseFont, &baseCount)) {
                    for (quint32 code = 0; code < 256; ++code) {
                        if (fm.widths.contains(code))
                            continue;
                        const QString dec = fm.enc.decode(QByteArray(1, char(code)));
                        if (dec.isEmpty())
                            continue;
                        const ushort uni = dec.at(0).unicode();
                        for (int k = 0; k < baseCount; ++k)
                            if (table[k].unicode == uni) {
                                fm.widths.insert(code, table[k].width);
                                break;
                            }
                    }
                }
            }
            fm.valid = true;
            fonts.insert(resName, fm);
        }
    }
    if (fonts.isEmpty()) return fail("keine auswertbaren Fonts");

    // ── Content-Stream(s) der Seite ─────────────────────────────────────────
    QByteArray content;
    {
        const QByteArray pd = dictOf(pageNum);
        const qint64 cp = findKey(pd, "Contents");
        if (cp < 0) return fail("kein /Contents");
        QVector<int> nums;
        if (pd[cp] == '[') {
            const qint64 ce = skipValue(pd, cp);
            static const QRegularExpression cre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
            auto it = cre.globalMatch(QString::fromLatin1(pd.mid(cp, ce - cp)));
            while (it.hasNext()) nums.push_back(it.next().captured(1).toInt());
        } else {
            const int n = refValue(pd, "Contents");
            if (n >= 0) nums.push_back(n);
        }
        if (nums.isEmpty()) return fail("/Contents leer");
        //  Zeichenweises Bearbeiten setzt EINEN Strom voraus (sonst ist unklar,
        //  in welchem Objekt eine Byte-Position liegt). Anzeigen geht trotzdem.
        page->contentObj = (nums.size() == 1) ? nums.first() : -1;
        for (int n : nums) {
            bool ok = false;
            const QByteArray part = streamOf(n, &ok);
            if (!ok) return fail("Content-Stream nicht lesbar");
            content += part;
            content += '\n';
        }
        page->content = content;
    }

    // ── Textzustand simulieren ──────────────────────────────────────────────
    Mat ctm;                       // Grafikmatrix
    QVector<Mat> ctmStack;
    Mat tm, tlm;                   // Text- und Textzeilen-Matrix
    const FontMetrics* fm = nullptr;
    QByteArray curFontRes;
    qreal fsize = 0, charSp = 0, wordSp = 0, hscale = 1.0, leading = 0, rise = 0;
    int showIndex = 0;
    //  Beginn des zuletzt gelesenen Operanden (fuer die Span-Aufzeichnung).
    qint64 lastOperandStart = -1, lastOperandEnd = -1;

    //  Operanden-Puffer (PDF ist postfix: erst Operanden, dann Operator).
    QVector<QByteArray> ops;
    auto numAt = [&](int fromEnd) -> qreal {
        const int i = ops.size() - fromEnd;
        return (i >= 0 && i < ops.size()) ? ops[i].toDouble() : 0.0;
    };

    //  Eine Zeichenkette zeigen und die Glyphen eintragen.
    auto showBytes = [&](const QByteArray& bytes) {
        if (!fm || !fm->valid) return true;                   // ohne Font nichts zu tun
        const int step = fm->cid ? 2 : 1;
        for (int i = 0; i + step <= bytes.size(); i += step) {
            const quint32 code = (step == 2)
                ? quint32((quint8(bytes[i]) << 8) | quint8(bytes[i+1]))
                : quint32(quint8(bytes[i]));
            const qreal w0 = fm->widthOf(code);
            if (w0 < 0) return false;                          // Breite unbekannt

            const QString dec = fm->enc.decode(bytes.mid(i, step));
            const QChar ch = dec.isEmpty() ? QChar(0xFFFD) : dec.at(0);

            //  Vorschub laut Spezifikation. Tw gilt NUR für Byte 32 in
            //  Ein-Byte-Kodierungen (bei CID wäre das ein gültiger Code).
            const bool isSpaceByte = (step == 1 && quint8(bytes[i]) == 32);
            const qreal adv = ((w0 / 1000.0) * fsize + charSp + (isSpaceByte ? wordSp : 0.0))
                              * hscale;

            const Mat trm = Mat{ fsize * hscale, 0, 0, fsize, 0, rise }.mul(tm).mul(ctm);
            const QPointF origin = trm.map(0, 0);
            const qreal hPt = fsize * trm.scaleY() / qMax(0.0001, fsize);  // Zeilenhöhe ≈ Größe
            const qreal wPt = adv * ctm.scaleX() * tm.scaleX() / qMax(0.0001, 1.0);

            PdfGlyph g;
            g.ch = ch;
            //  Nach oben-links spiegeln: PDF-Ursprung ist unten-links, und der
            //  Glyphen-Ursprung sitzt auf der GRUNDLINIE.
            g.box = QRectF(origin.x(), pageH - origin.y() - hPt,
                           qMax(0.0, wPt), qMax(0.0, hPt));
            g.fontSizePt = fsize * trm.scaleY() / qMax(0.0001, fsize);
            g.showIndex  = showIndex;
            g.byteOffset = i;
            out->push_back(g);

            tm = Mat{ 1, 0, 0, 1, adv, 0 }.mul(tm);            // Textmatrix vorrücken
        }
        return true;
    };

    //  Anfang der laufenden ANWEISUNG (erster Operand) — nötig, um eine
    //  Positionierung später vollständig ersetzen zu können.
    qint64 stmtStart = -1;
    //  Zuletzt gesetzte Positionierung (gilt für die folgenden Zeigeoperatoren).
    qint64     posStart = -1, posEnd = -1;
    QByteArray posOp;
    QVector<qreal> posArgs;
    int        objIndex = 0;              // laufende Nummer des Textobjekts (BT)
    //  Hülle des gerade aufgebauten Pfades (Benutzerraum) + Gültigkeit.
    QRectF pathBox;
    bool   pathValid = false;
    auto addPoint = [&](qreal x, qreal y) {
        const QPointF p = ctm.map(x, y);
        if (!pathValid) { pathBox = QRectF(p, QSizeF(0, 0)); pathValid = true; }
        else {
            pathBox.setLeft(qMin(pathBox.left(), p.x()));
            pathBox.setRight(qMax(pathBox.right(), p.x()));
            pathBox.setTop(qMin(pathBox.top(), p.y()));
            pathBox.setBottom(qMax(pathBox.bottom(), p.y()));
        }
    };
    //  Gemalte Fläche merken (Umrechnung unten-links → oben-links wie bei den
    //  Glyphen). `page` ist optional — nur der ausführliche Aufruf sammelt sie.
    auto notePaint = [&](const QRectF& userBox) {
        if (!page) return;
        page->paints.push_back(QRectF(userBox.left(),
                                      pageH - userBox.bottom(),
                                      qMax(0.0, userBox.width()),
                                      qMax(0.0, userBox.height())));
    };
    auto flushPath = [&](bool painted) {
        if (painted && pathValid) notePaint(pathBox.normalized());
        pathValid = false;
    };
    //  Ein XObject/Inline-Bild füllt das Einheitsquadrat der aktuellen Matrix.
    auto noteUnitSquare = [&]() {
        const QPointF a = ctm.map(0, 0), b = ctm.map(1, 0);
        const QPointF c = ctm.map(1, 1), d = ctm.map(0, 1);
        QRectF r(a, QSizeF(0, 0));
        for (const QPointF& p : { b, c, d }) {
            r.setLeft(qMin(r.left(), p.x()));   r.setRight(qMax(r.right(), p.x()));
            r.setTop(qMin(r.top(), p.y()));     r.setBottom(qMax(r.bottom(), p.y()));
        }
        notePaint(r.normalized());
    };

    if (page) page->pageHeightPt = pageH;

    const qint64 n = content.size();
    qint64 i = 0;
    while (i < n) {
        const char ch = content[i];
        if (isWs(ch)) { ++i; continue; }
        if (ch == '%') { while (i < n && content[i] != '\n' && content[i] != '\r') ++i; continue; }
        if (ch == '(' || ch == '<' || ch == '[' || ch == '/') {
            if (ch == '<' && i + 1 < n && content[i+1] == '<') {   // Dict → überspringen
                i = skipValue(content, i); ops.clear(); stmtStart = -1; continue;
            }
            const qint64 e = skipValue(content, i);
            lastOperandStart = i; lastOperandEnd = e;
            if (ops.isEmpty()) stmtStart = i;
            ops.push_back(content.mid(i, e - i));
            i = e; continue;
        }
        if (ch == '-' || ch == '+' || ch == '.' || (ch >= '0' && ch <= '9')) {
            const qint64 s = i; ++i;
            while (i < n && !isWs(content[i]) && !isDelim(content[i])) ++i;
            if (ops.isEmpty()) stmtStart = s;
            ops.push_back(content.mid(s, i - s));
            continue;
        }
        const qint64 s = i;
        while (i < n && !isWs(content[i]) && !isDelim(content[i])) ++i;
        const QByteArray op = content.mid(s, i - s);

        if      (op == "q")  { ctmStack.push_back(ctm); }
        else if (op == "Q")  { if (!ctmStack.isEmpty()) ctm = ctmStack.takeLast(); }
        else if (op == "cm") { ctm = Mat{ numAt(6), numAt(5), numAt(4),
                                          numAt(3), numAt(2), numAt(1) }.mul(ctm); }
        else if (op == "BT") { tm = Mat(); tlm = Mat(); ++objIndex;
                               posStart = posEnd = -1; posOp.clear(); posArgs.clear(); }
        else if (op == "ET") { /* nichts */ }
        else if (op == "Tf") {
            if (ops.size() >= 2) {
                curFontRes = ops[ops.size()-2];
                const auto it = fonts.constFind(curFontRes);
                fm = (it == fonts.constEnd()) ? nullptr : &it.value();
                fsize = numAt(1);
            }
        }
        else if (op == "Td" || op == "TD" || op == "Tm" || op == "T*") {
            //  Anweisung merken, BEVOR die Weiche sie auswertet — nur so lässt
            //  sich die Zeile später verschieben (s. PdfShowSpan::posStart).
            posStart = (stmtStart >= 0) ? stmtStart : s;
            posEnd   = i;
            posOp    = op;
            posArgs.clear();
            for (const QByteArray& t : std::as_const(ops)) {
                bool okNum = false;
                const qreal v = t.toDouble(&okNum);
                if (okNum) posArgs.push_back(v);
            }
            if      (op == "Td") { tlm = Mat{1,0,0,1,numAt(2),numAt(1)}.mul(tlm); tm = tlm; }
            else if (op == "TD") { leading = -numAt(1);
                                   tlm = Mat{1,0,0,1,numAt(2),numAt(1)}.mul(tlm); tm = tlm; }
            else if (op == "Tm") { tlm = Mat{ numAt(6), numAt(5), numAt(4),
                                              numAt(3), numAt(2), numAt(1) }; tm = tlm; }
            else                 { tlm = Mat{1,0,0,1,0,-leading}.mul(tlm); tm = tlm; }
        }
        else if (op == "TL") { leading = numAt(1); }
        else if (op == "Tc") { charSp  = numAt(1); }
        else if (op == "Tw") { wordSp  = numAt(1); }
        else if (op == "Tz") { hscale  = numAt(1) / 100.0; }
        else if (op == "Ts") { rise    = numAt(1); }
        else if (op == "BI") { noteUnitSquare();
                               const qint64 ei = content.indexOf("EI", i);
                               i = (ei < 0) ? n : ei + 2; }
        else if (op == "Do") { noteUnitSquare(); }
        else if (op == "m" || op == "l") { addPoint(numAt(2), numAt(1)); }
        else if (op == "c") { addPoint(numAt(6), numAt(5));
                              addPoint(numAt(4), numAt(3));
                              addPoint(numAt(2), numAt(1)); }
        else if (op == "v" || op == "y") { addPoint(numAt(4), numAt(3));
                                           addPoint(numAt(2), numAt(1)); }
        else if (op == "re") { const qreal x = numAt(4), y = numAt(3),
                                           w = numAt(2), h = numAt(1);
                               addPoint(x, y); addPoint(x + w, y + h); }
        else if (op == "S" || op == "s" || op == "f" || op == "F" || op == "f*"
                 || op == "B" || op == "B*" || op == "b" || op == "b*") { flushPath(true); }
        else if (op == "n") { flushPath(false); }
        else if (op == "sh") { noteUnitSquare(); }
        else if (op == "Tj" || op == "'" || op == "\"") {
            if (op != "Tj") { tlm = Mat{1,0,0,1,0,-leading}.mul(tlm); tm = tlm; }
            if (op == "\"" && ops.size() >= 3) { wordSp = numAt(3); charSp = numAt(2); }
            if (!ops.isEmpty()) {
                const QByteArray& tok = ops.last();
                QByteArray bytes;
                if (tok.startsWith('(')) {
                    //  Escapes auflösen (gleiche Regeln wie PdfContentEditor).
                    for (int k = 1; k + 1 < tok.size(); ++k) {
                        if (tok[k] != '\\') { bytes += tok[k]; continue; }
                        if (++k + 1 > tok.size()) break;
                        const char e2 = tok[k];
                        if (e2 >= '0' && e2 <= '7') {
                            int v = 0, cnt = 0;
                            while (cnt < 3 && k < tok.size() && tok[k] >= '0' && tok[k] <= '7')
                                { v = v*8 + (tok[k]-'0'); ++k; ++cnt; }
                            --k; bytes += char(v & 0xFF);
                        } else if (e2=='n') bytes += '\n';
                        else if (e2=='r') bytes += '\r';
                        else if (e2=='t') bytes += '\t';
                        else bytes += e2;
                    }
                } else if (tok.startsWith('<')) {
                    QByteArray hex = tok.mid(1, tok.size() - 2);
                    hex.replace(" ", "").replace("\n", "").replace("\r", "");
                    bytes = QByteArray::fromHex(hex);
                }
                PdfShowSpan sp;
                sp.operandStart = lastOperandStart; sp.operandEnd = lastOperandEnd;
                sp.isArray = false; sp.bytes = bytes; sp.fontRes = curFontRes;
                //  Bei ' und " setzt der Zeigeoperator SELBST die neue Zeile —
                //  eine eigene Positionierung gibt es dafür nicht.
                if (op == "Tj") { sp.posStart = posStart; sp.posEnd = posEnd;
                                  sp.posOp = posOp; sp.posArgs = posArgs; }
                sp.objIndex = objIndex;
                page->spans.push_back(sp);
                if (!showBytes(bytes)) return fail("Glyphenbreite fehlt (kein /Widths)");
                ++showIndex;
            }
        }
        else if (op == "TJ") {
            if (!ops.isEmpty() && ops.last().startsWith('[')) {
                const QByteArray arr = ops.last();
                PdfShowSpan sp;
                sp.operandStart = lastOperandStart; sp.operandEnd = lastOperandEnd;
                sp.isArray = true; sp.fontRes = curFontRes;
                sp.posStart = posStart; sp.posEnd = posEnd;
                sp.posOp = posOp; sp.posArgs = posArgs;
                sp.objIndex = objIndex;
                int k = 1;
                while (k < arr.size() - 1) {
                    if (isWs(arr[k])) { ++k; continue; }
                    if (arr[k] == '(' || arr[k] == '<') {
                        const qint64 e = skipValue(arr, k);
                        const QByteArray tok = arr.mid(k, e - k);
                        QByteArray bytes;
                        if (tok.startsWith('(')) {
                            for (int q = 1; q + 1 < tok.size(); ++q) {
                                if (tok[q] != '\\') { bytes += tok[q]; continue; }
                                if (++q + 1 > tok.size()) break;
                                const char e2 = tok[q];
                                if (e2 >= '0' && e2 <= '7') {
                                    int v = 0, cnt = 0;
                                    while (cnt < 3 && q < tok.size() && tok[q] >= '0' && tok[q] <= '7')
                                        { v = v*8 + (tok[q]-'0'); ++q; ++cnt; }
                                    --q; bytes += char(v & 0xFF);
                                } else bytes += e2;
                            }
                        } else {
                            QByteArray hex = tok.mid(1, tok.size() - 2);
                            hex.replace(" ", "").replace("\n", "").replace("\r", "");
                            bytes = QByteArray::fromHex(hex);
                        }
                        sp.bytes += bytes;
                        if (!showBytes(bytes)) return fail("Glyphenbreite fehlt (kein /Widths)");
                        k = e;
                    } else {
                        const int s2 = k;
                        while (k < arr.size() - 1 && !isWs(arr[k]) && arr[k] != '('
                               && arr[k] != '<') ++k;
                        bool ok = false;
                        const qreal adj = arr.mid(s2, k - s2).toDouble(&ok);
                        if (ok)   // Kerning: verschiebt die Textmatrix zurück
                            tm = Mat{1,0,0,1, -adj/1000.0 * fsize * hscale, 0}.mul(tm);
                    }
                }
                page->spans.push_back(sp);
                ++showIndex;
            }
        }
        ops.clear();
        stmtStart = -1;
    }

    return true;
}

int PdfTextLayout::hitTest(const QVector<PdfGlyph>& glyphs, const QPointF& p) {
    if (glyphs.isEmpty()) return -1;
    //  Erst ein echter Treffer, sonst das Zeichen mit dem kleinsten Abstand
    //  zum Mittelpunkt — so landet der Caret auch neben der Zeile sinnvoll.
    for (int i = 0; i < glyphs.size(); ++i)
        if (glyphs[i].box.contains(p)) return i;
    int best = 0;
    qreal bestD = std::numeric_limits<qreal>::max();
    for (int i = 0; i < glyphs.size(); ++i) {
        const QPointF c = glyphs[i].box.center();
        const qreal d = std::hypot(c.x() - p.x(), c.y() - p.y());
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

QRectF PdfTextLayout::caretRect(const QVector<PdfGlyph>& glyphs, int index) {
    if (glyphs.isEmpty()) return {};
    if (index >= glyphs.size()) {                       // hinter dem letzten
        const QRectF b = glyphs.last().box;
        return QRectF(b.right(), b.top(), 1.0, b.height());
    }
    if (index < 0) return {};
    const QRectF b = glyphs.at(index).box;
    return QRectF(b.left(), b.top(), 1.0, b.height());
}

} // namespace mg
