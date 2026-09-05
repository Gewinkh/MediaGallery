#include "pdf/edit/PdfTextEditor.h"
#include "pdf/edit/PdfTextLayout.h"
#include "pdf/edit/PdfObjects.h"
#include "pdf/edit/PdfEncodings.h"

#include <QFile>
#include <QSaveFile>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <functional>

using namespace mg::pdfobj;

namespace {

//  Bytes -> PDF-Paren-String; ( ) \ escapt, alles ausserhalb des druckbaren
//  ASCII oktal (7-Bit-sicher, wie im übrigen PDF-Teil des Projekts).
QByteArray parenBytes(const QByteArray& b) {
    QByteArray out = "(";
    for (char c : b) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '(' || c == ')' || c == '\\') { out += '\\'; out += c; }
        else if (u < 0x20 || u > 0x7E) {
            out += '\\';
            out += QByteArray::number(u, 8).rightJustified(3, '0');
        }
        else out += c;
    }
    return out + ")";
}

//  Kodierung der Schrift `fontRes` auf der Seite ermitteln. Dieselbe Auflösung
//  wie in PdfContentEditor/PdfTextLayout - hier nur für EINEN Namen gebraucht.
bool encodingForFontImpl(const QByteArray& buf, const QHash<int, ObjLoc>& objs,
                         int pageIndex, const QByteArray& fontRes,
                         mg::pdfenc::Encoding* out) {
    auto bodyOf = [&](int n) -> QByteArray {
        const auto it = objs.constFind(n);
        return (it == objs.constEnd()) ? QByteArray() : objectBody(buf, it->offset);
    };
    auto dictOf = [&](int n) { return dictOfObject(bodyOf(n)); };
    auto streamOf = [&](int n, bool* ok) -> QByteArray {
        *ok = false;
        const QByteArray body = bodyOf(n);
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
        //  /Length darf eine REFERENZ sein (s. pdfobj::streamLength).
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
    if (rootNum < 0) return false;

    QByteArray pageRes;
    {
        int seen = 0, guard = 0;
        bool found = false;
        std::function<void(int, const QByteArray&, int)> walk =
            [&](int num, const QByteArray& inherited, int depth) {
                if (++guard > 100000 || depth > 50 || found) return;
                const QByteArray d = dictOf(num);
                if (d.isEmpty()) return;
                QByteArray res = inherited;
                const qint64 rp = findKey(d, "Resources");
                if (rp >= 0) {
                    if (d[rp] == '<') { const qint64 e = skipValue(d, rp); res = d.mid(rp, e - rp); }
                    else { const int rn = refValue(d, "Resources");
                           if (rn >= 0) res = QByteArray("<<") + dictOf(rn) + ">>"; }
                }
                if (nameValue(d, "Type") == "/Page") {
                    if (seen++ == pageIndex) { pageRes = res; found = true; }
                    return;
                }
                const qint64 kp = findKey(d, "Kids");
                if (kp < 0 || d[kp] != '[') return;
                const qint64 ke = skipValue(d, kp);
                static const QRegularExpression kre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
                auto it = kre.globalMatch(QString::fromLatin1(d.mid(kp, ke - kp)));
                while (it.hasNext() && !found)
                    walk(it.next().captured(1).toInt(), res, depth + 1);
            };
        walk(refValue(dictOf(rootNum), "Pages"), QByteArray(), 0);
        if (!found) return false;
    }

    const qint64 fp = findKey(pageRes, "Font");
    if (fp < 0) return false;
    QByteArray fontDict;
    if (pageRes[fp] == '<') { const qint64 e = skipValue(pageRes, fp);
                              fontDict = pageRes.mid(fp + 2, (e - 2) - (fp + 2)); }
    else {
        const int fn = refValue(pageRes, "Font");
        if (fn < 0) return false;
        fontDict = dictOf(fn);
    }

    qint64 i = 0;
    while (i < fontDict.size()) {
        while (i < fontDict.size() && isWs(fontDict[i])) ++i;
        if (i >= fontDict.size() || fontDict[i] != '/') { ++i; continue; }
        const qint64 ns = i;
        i = skipValue(fontDict, i);
        const QByteArray name = fontDict.mid(ns, i - ns);
        const qint64 vs = i;
        i = skipValue(fontDict, vs);
        if (name != fontRes) continue;
        static const QRegularExpression rre(QStringLiteral("^\\s*(\\d+)\\s+(\\d+)\\s+R"));
        const auto m = rre.match(QString::fromLatin1(fontDict.mid(vs, i - vs)));
        if (!m.hasMatch()) return false;
        const QByteArray fd = dictOf(m.captured(1).toInt());

        if (nameValue(fd, "Subtype") == "/Type0") {
            if (nameValue(fd, "Encoding") != "/Identity-H") return false;
            const int tu = refValue(fd, "ToUnicode");
            if (tu < 0) return false;
            bool sok = false;
            const QByteArray cm = streamOf(tu, &sok);
            if (!sok) return false;
            bool cok = false;
            *out = mg::pdfenc::Encoding::fromCidToUnicode(cm, &cok);
            return cok;
        }
        QByteArray encVal;
        const qint64 ep = findKey(fd, "Encoding");
        if (ep >= 0) {
            if (fd[ep] == '/' || fd[ep] == '<') { const qint64 e = skipValue(fd, ep);
                                                  encVal = fd.mid(ep, e - ep); }
            else { const int en = refValue(fd, "Encoding");
                   if (en >= 0) encVal = QByteArray("<<") + dictOf(en) + ">>"; }
        }
        bool eok = false;
        *out = mg::pdfenc::Encoding::fromEncodingValue(encVal, &eok);
        return eok;
    }
    return false;
}

//  Gemeinsamer Kern von Einfügen und Löschen: berechnet die neuen Rohbytes
//  EINES Zeigeoperanden und schreibt das inkrementelle Update.
bool spliceAt(const QString& inputPath, const QString& outputPath,
              int pageIndex, int glyphIndex, const QString& insert, int deleteCount,
              QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };

    QFile in(inputPath);
    if (!in.open(QIODevice::ReadOnly)) return fail("Quelle nicht lesbar");
    const QByteArray buf = in.readAll();
    in.close();
    if (buf.size() < 32 || !buf.startsWith("%PDF-")) return fail("kein PDF");
    if (buf.contains("/Encrypt")) return fail("verschlüsselt");

    //  Klassische xref verlangt (wie im übrigen PDF-Teil).
    const int sxi = buf.lastIndexOf("startxref");
    if (sxi < 0) return fail("kein startxref");
    qint64 prevXref = -1;
    {
        qint64 p = sxi + 9; while (p < buf.size() && isWs(buf[p])) ++p;
        const qint64 s = p; while (p < buf.size() && buf[p] >= '0' && buf[p] <= '9') ++p;
        bool ok = false; prevXref = buf.mid(s, p - s).toLongLong(&ok);
        if (!ok || prevXref < 0 || prevXref >= buf.size()) return fail("startxref ungültig");
        qint64 q = prevXref; while (q < buf.size() && isWs(buf[q])) ++q;
        if (buf.mid(q, 4) != "xref") return fail("kein klassisches xref");
    }

    mg::PdfPageText page;
    if (!mg::PdfTextLayout::buildForPage(inputPath, pageIndex, &page, err))
        return false;
    if (page.contentObj < 0) return fail("mehrteiliger /Contents -> nicht bearbeitbar");
    if (page.glyphs.isEmpty()) return fail("kein Text auf der Seite");
    if (glyphIndex < 0 || glyphIndex > page.glyphs.size())
        return fail("Zeichenindex außerhalb");
    if (deleteCount < 0) return fail("negative Anzahl");

    //  Betroffenen Zeigeoperator bestimmen. Einfügen am Ende hängt an das
    //  LETZTE Zeichen an.
    const int refIdx = (glyphIndex < page.glyphs.size()) ? glyphIndex
                                                         : page.glyphs.size() - 1;
    const mg::PdfGlyph& refGlyph = page.glyphs.at(refIdx);
    const int spanIdx = refGlyph.showIndex;
    if (spanIdx < 0 || spanIdx >= page.spans.size()) return fail("Zeigeoperator nicht gefunden");
    const mg::PdfShowSpan& span = page.spans.at(spanIdx);

    //  Alle betroffenen Zeichen müssen im SELBEN Operanden liegen.
    for (int k = glyphIndex; k < glyphIndex + deleteCount; ++k) {
        if (k >= page.glyphs.size()) return fail("Löschbereich außerhalb");
        if (page.glyphs.at(k).showIndex != spanIdx)
            return fail("Änderung über mehrere Zeige-Anweisungen -> nicht bearbeitbar");
    }

    //  Kodierung der dort aktiven Schrift.
    const QHash<int, ObjLoc> objs = scanObjects(buf);
    mg::pdfenc::Encoding enc;
    if (!encodingForFontImpl(buf, objs, pageIndex, span.fontRes, &enc))
        return fail("Schriftkodierung nicht bestimmbar");
    const int step = (enc.codeBytes() == 2) ? 2 : 1;

    //  Byte-Position innerhalb des Operanden.
    int cutStart = (glyphIndex < page.glyphs.size())
                       ? refGlyph.byteOffset
                       : refGlyph.byteOffset + step;      // hinter das letzte Zeichen
    if (cutStart < 0 || cutStart > span.bytes.size()) return fail("Byte-Position außerhalb");
    const int cutLen = deleteCount * step;
    if (cutStart + cutLen > span.bytes.size()) return fail("Löschbereich außerhalb");

    QByteArray insBytes;
    if (!insert.isEmpty() && !enc.encode(insert, &insBytes))
        return fail("Text in der Schriftkodierung nicht darstellbar");

    QByteArray newBytes = span.bytes;
    newBytes.remove(cutStart, cutLen);
    newBytes.insert(cutStart, insBytes);

    //  Operanden ersetzen. Bei TJ wird das ganze Array zu EINEM String - die
    //  Kerning-Abstände DIESES Operanden entfallen dabei (s. Header).
    QByteArray repl = parenBytes(newBytes);
    if (span.isArray) repl = "[" + repl + "]";

    QByteArray newContent = page.content;
    if (span.operandStart < 0 || span.operandEnd > newContent.size()
        || span.operandEnd <= span.operandStart)
        return fail("Operanden-Bereich ungültig");
    newContent.replace(span.operandStart, span.operandEnd - span.operandStart, repl);

    const auto cit = objs.constFind(page.contentObj);
    if (cit == objs.constEnd()) return fail("Content-Objekt nicht gefunden");

    const QByteArray def = zDeflate(newContent);
    if (def.isEmpty() && !newContent.isEmpty()) return fail("Deflate fehlgeschlagen");

    QByteArray out = buf;
    if (!out.endsWith('\n')) out += '\n';
    const qint64 objOff = out.size();
    out += QByteArray::number(page.contentObj) + " " + QByteArray::number(cit->gen) + " obj\n";
    out += "<< /Length " + QByteArray::number(def.size()) + " /Filter /FlateDecode >>\n";
    out += "stream\n";
    out += def;
    out += "\nendstream\nendobj\n";

    int rootNum = -1;
    {
        static const QRegularExpression re(QStringLiteral("/Root\\s+(\\d+)\\s+(\\d+)\\s+R"));
        auto it = re.globalMatch(QString::fromLatin1(buf));
        while (it.hasNext()) rootNum = it.next().captured(1).toInt();
    }
    if (rootNum < 0) return fail("kein /Root");

    int maxObj = 0;
    for (auto it = objs.constBegin(); it != objs.constEnd(); ++it)
        maxObj = qMax(maxObj, it.key());

    const qint64 xrefOff = out.size();
    out += "xref\n";
    out += QByteArray::number(page.contentObj) + " 1\n";
    out += QByteArray::number(objOff).rightJustified(10, '0') + " "
         + QByteArray::number(cit->gen).rightJustified(5, '0') + " n \n";
    out += "trailer\n<< /Size " + QByteArray::number(maxObj + 1)
         + " /Root " + QByteArray::number(rootNum) + " 0 R"
         + " /Prev " + QByteArray::number(prevXref) + " >>\n";
    out += "startxref\n" + QByteArray::number(xrefOff) + "\n%%EOF\n";

    QSaveFile sf(outputPath);
    if (!sf.open(QIODevice::WriteOnly)) return fail("Ziel nicht schreibbar");
    if (sf.write(out) != out.size()) { sf.cancelWriting(); return fail("Schreibfehler"); }
    if (!sf.commit()) return fail("Commit fehlgeschlagen");
    return true;
}

} // namespace

namespace mg {

bool PdfTextEditor::encodingForPageFont(const QByteArray& pdfBytes, int pageIndex,
                                        const QByteArray& fontRes,
                                        pdfenc::Encoding* out) {
    if (!out) return false;
    return encodingForFontImpl(pdfBytes, scanObjects(pdfBytes), pageIndex, fontRes, out);
}

bool PdfTextEditor::insertText(const QString& in, const QString& out,
                               int pageIndex, int glyphIndex, const QString& text,
                               QString* err) {
    if (text.isEmpty()) { if (err) *err = QStringLiteral("leerer Text"); return false; }
    return spliceAt(in, out, pageIndex, glyphIndex, text, 0, err);
}

bool PdfTextEditor::deleteText(const QString& in, const QString& out,
                               int pageIndex, int glyphIndex, int count, QString* err) {
    if (count <= 0) { if (err) *err = QStringLiteral("nichts zu löschen"); return false; }
    return spliceAt(in, out, pageIndex, glyphIndex, QString(), count, err);
}

} // namespace mg
