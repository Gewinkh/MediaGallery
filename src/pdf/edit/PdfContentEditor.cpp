#include "pdf/edit/PdfContentEditor.h"

#include <QFile>
#include <QSaveFile>
#include <QHash>
#include <QRegularExpression>
#include <zlib.h>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <cstring>

// ══════════════════════════════════════════════════════════════════════════════
//  Alle Helfer liegen in einem anonymen Namespace — reine Byte-Arbeit, kein Qt-
//  Objektzustand. Bei JEDER Unsicherheit geben die Funktionen einen Fehler
//  zurück; PdfContentEditor::editText bricht dann ab (Aufrufer → Raster-Export).
// ══════════════════════════════════════════════════════════════════════════════
namespace {

// ── zlib raw/zlib inflate + deflate ─────────────────────────────────────────
QByteArray zInflate(const QByteArray& src, bool* ok) {
    *ok = false;
    for (int attempt = 0; attempt < 2; ++attempt) {         // 0 = zlib-Header, 1 = raw
        z_stream zs; std::memset(&zs, 0, sizeof(zs));
        if (inflateInit2(&zs, attempt == 0 ? 15 : -15) != Z_OK) continue;
        zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(src.constData()));
        zs.avail_in = static_cast<uInt>(src.size());
        QByteArray out; char buf[16384]; int rc;
        do {
            zs.next_out  = reinterpret_cast<Bytef*>(buf);
            zs.avail_out = sizeof(buf);
            rc = inflate(&zs, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) break;
            out.append(buf, sizeof(buf) - zs.avail_out);
        } while (rc != Z_STREAM_END);
        inflateEnd(&zs);
        if (rc == Z_STREAM_END) { *ok = true; return out; }
    }
    return {};
}

QByteArray zDeflate(const QByteArray& src) {
    z_stream zs; std::memset(&zs, 0, sizeof(zs));
    if (deflateInit(&zs, Z_BEST_COMPRESSION) != Z_OK) return {};
    zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(src.constData()));
    zs.avail_in = static_cast<uInt>(src.size());
    QByteArray out; char buf[16384]; int rc;
    do {
        zs.next_out  = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        rc = deflate(&zs, Z_FINISH);
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (rc == Z_OK);
    deflateEnd(&zs);
    return (rc == Z_STREAM_END) ? out : QByteArray();
}

inline bool isWs(char c) { return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\0'; }
inline bool isDelim(char c) { return c=='('||c==')'||c=='<'||c=='>'||c=='['||c==']'
                                     ||c=='{'||c=='}'||c=='/'||c=='%'; }

// Objekt-Tabelle per Brute-Scan: "N G obj" → Byte-Offset + Generation (letztes
// Vorkommen gewinnt = jüngster Inkrement-Save).
struct ObjLoc { qint64 offset; int gen; };
QHash<int, ObjLoc> scanObjects(const QByteArray& b) {
    QHash<int, ObjLoc> map;
    static const QRegularExpression re(QStringLiteral("(\\d+)\\s+(\\d+)\\s+obj"));
    auto it = re.globalMatch(QString::fromLatin1(b));
    while (it.hasNext()) {
        const auto m = it.next();
        const int num = m.captured(1).toInt();
        const int gen = m.captured(2).toInt();
        map.insert(num, { m.capturedStart(0), gen });        // letztes gewinnt
    }
    return map;
}

// Wert-Ende ab Index i (i zeigt auf das erste Nicht-WS des Wertes). Überspringt
// Dict/Array/String/Hex/Name/Zahl/Ref/Keyword als GANZE Einheit.
qint64 skipValue(const QByteArray& b, qint64 i) {
    const qint64 n = b.size();
    while (i < n && isWs(b[i])) ++i;
    if (i >= n) return i;
    const char c = b[i];
    if (c == '<' && i+1 < n && b[i+1] == '<') {              // Dict
        int depth = 0;
        while (i < n) {
            if (b[i]=='<' && i+1<n && b[i+1]=='<') { depth++; i+=2; }
            else if (b[i]=='>' && i+1<n && b[i+1]=='>') { depth--; i+=2; if(depth==0) return i; }
            else if (b[i]=='(') { i = skipValue(b, i); }
            else ++i;
        }
        return i;
    }
    if (c == '[') {                                          // Array
        int depth = 0;
        while (i < n) {
            if (b[i]=='[') { depth++; ++i; }
            else if (b[i]==']') { depth--; ++i; if(depth==0) return i; }
            else if (b[i]=='(') { i = skipValue(b, i); }
            else ++i;
        }
        return i;
    }
    if (c == '(') {                                          // String (Paren, mit Escapes)
        int depth = 0; ++i;
        while (i < n) {
            if (b[i]=='\\') { i += 2; continue; }
            if (b[i]=='(') { depth++; ++i; continue; }
            if (b[i]==')') { if (depth==0) return i+1; depth--; ++i; continue; }
            ++i;
        }
        return i;
    }
    if (c == '<') {                                          // Hex-String
        ++i; while (i < n && b[i] != '>') ++i; return (i<n)?i+1:i;
    }
    if (c == '/') {                                          // Name
        ++i; while (i < n && !isWs(b[i]) && !isDelim(b[i])) ++i; return i;
    }
    // Zahl/Keyword/Bool — evtl. Referenz "N G R" (drei Tokens).
    auto readToken = [&](qint64 p, QByteArray* tok) -> qint64 {
        while (p < n && isWs(b[p])) ++p;
        const qint64 s = p;
        while (p < n && !isWs(b[p]) && !isDelim(b[p])) ++p;
        *tok = b.mid(s, p - s); return p;
    };
    QByteArray t1; qint64 p1 = readToken(i, &t1);
    bool num1 = !t1.isEmpty() && (t1.at(0)=='-' || t1.at(0)=='+' || t1.at(0)=='.'
                                  || (t1.at(0)>='0' && t1.at(0)<='9'));
    if (num1) {
        QByteArray t2; qint64 p2 = readToken(p1, &t2);
        bool num2 = !t2.isEmpty() && t2.at(0)>='0' && t2.at(0)<='9';
        if (num2) {
            QByteArray t3; qint64 p3 = readToken(p2, &t3);
            if (t3 == "R") return p3;                        // Referenz
        }
    }
    return p1;                                               // einfacher Token
}

// Sucht Schlüssel `/key` auf DEPTH 0 im Dict-Inhalt und liefert den Werteanfang
// (oder -1). Dicts sind /Key Value /Key Value … → nach jedem Schlüssel wird der
// WERT via skipValue übersprungen (so kann kein Wert-Name fälschlich matchen).
qint64 findKey(const QByteArray& dict, const char* key) {
    const QByteArray k = QByteArray("/") + key;
    const qint64 n = dict.size();
    qint64 i = 0;
    while (i < n) {
        while (i < n && isWs(dict[i])) ++i;
        if (i >= n) break;
        if (dict[i] != '/') { ++i; continue; }               // Robustheit: Streubytes
        qint64 s = i; ++i;
        while (i < n && !isWs(dict[i]) && !isDelim(dict[i])) ++i;
        const QByteArray name = dict.mid(s, i - s);
        while (i < n && isWs(dict[i])) ++i;
        const qint64 valStart = i;
        if (name == k) return valStart;
        i = skipValue(dict, valStart);                       // Wert überspringen
    }
    return -1;
}

// Der `<< ... >>`-Inhalt eines Objektkörpers (ohne die äußeren <<>>), oder leer.
QByteArray dictOfObject(const QByteArray& objBody) {
    const qint64 s = objBody.indexOf("<<");
    if (s < 0) return {};
    qint64 e = skipValue(objBody, s);                        // bis inkl. schließendes >>
    if (e <= s) return {};
    return objBody.mid(s + 2, (e - 2) - (s + 2));
}

// Referenz "/key N G R" → Objektnummer (-1 falls keine).
int refValue(const QByteArray& dict, const char* key) {
    const qint64 i = findKey(dict, key);
    if (i < 0) return -1;
    const QByteArray tail = dict.mid(i, 40);
    static const QRegularExpression re(QStringLiteral("^(\\d+)\\s+(\\d+)\\s+R"));
    const auto m = re.match(QString::fromLatin1(tail));
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

// Name-Wert "/key /Name" → "/Name" (oder leer).
QByteArray nameValue(const QByteArray& dict, const char* key) {
    const qint64 i = findKey(dict, key);
    if (i < 0 || i >= dict.size() || dict[i] != '/') return {};
    qint64 e = i + 1;
    while (e < dict.size() && !isWs(dict[e]) && !isDelim(dict[e])) ++e;
    return dict.mid(i, e - i);
}

// Ganzzahl-Wert "/key 123" (nur direkte Zahl; -1 sonst).
qint64 intValue(const QByteArray& dict, const char* key) {
    const qint64 i = findKey(dict, key);
    if (i < 0) return -1;
    qint64 e = i; bool any=false;
    if (e < dict.size() && (dict[e]=='+'||dict[e]=='-')) ++e;
    while (e < dict.size() && dict[e]>='0' && dict[e]<='9') { ++e; any=true; }
    return any ? dict.mid(i, e-i).toLongLong() : -1;
}

// Objektkörper "N G obj … endobj" ab Offset.
QByteArray objectBody(const QByteArray& buf, qint64 offset) {
    const qint64 s = buf.indexOf("obj", offset);
    if (s < 0) return {};
    qint64 e = buf.indexOf("endobj", s);
    if (e < 0) return {};
    return buf.mid(s + 3, e - (s + 3));
}

bool asciiOnly(const QString& s) {
    for (QChar c : s) if (c.unicode() < 0x20 || c.unicode() > 0x7E) return false;
    return true;
}

// PDF-Paren-String → Bytes (Escapes auflösen). in zeigt HINTER '('.
QByteArray decodeParenString(const QByteArray& b, qint64 open, qint64 closeExcl) {
    QByteArray out; qint64 i = open + 1;
    while (i < closeExcl - 1) {
        char c = b[i];
        if (c == '\\') {
            if (i+1 >= closeExcl-1) break;
            char e = b[i+1];
            switch (e) {
                case 'n': out += '\n'; i+=2; break;
                case 'r': out += '\r'; i+=2; break;
                case 't': out += '\t'; i+=2; break;
                case 'b': out += '\b'; i+=2; break;
                case 'f': out += '\f'; i+=2; break;
                case '(': out += '('; i+=2; break;
                case ')': out += ')'; i+=2; break;
                case '\\': out += '\\'; i+=2; break;
                default:
                    if (e >= '0' && e <= '7') {               // Oktal \ddd
                        int v=0, k=0; i++;
                        while (k<3 && i<closeExcl-1 && b[i]>='0' && b[i]<='7') { v=v*8+(b[i]-'0'); ++i; ++k; }
                        out += char(v & 0xFF);
                    } else { out += e; i+=2; }
            }
        } else { out += c; ++i; }
    }
    return out;
}

// ASCII-Text → PDF-Paren-String "(...)" (nur ( ) \ escapen — ASCII sonst 1:1).
QByteArray encodeParenString(const QString& s) {
    QByteArray out = "(";
    for (QChar c : s) {
        char ch = char(c.unicode());
        if (ch=='(' || ch==')' || ch=='\\') out += '\\';
        out += ch;
    }
    out += ")";
    return out;
}

// Ein Textzeige-Treffer im Content-Stream: Byte-Bereich des zu ersetzenden
// Operanden (String bei Tj bzw. Array bei TJ), der dekodierte ASCII-Text und ob
// es ein TJ-Array ist (dann muss der Ersatz ein Array bleiben).
struct ShowHit { qint64 start; qint64 end; QString text; bool isArray; };

// Findet alle Tj/TJ-Treffer im (inflateten) Content-Stream. Überspringt
// Inline-Bilder (BI…EI), damit deren Binärdaten nicht als Tokens missraten.
QVector<ShowHit> scanTextShows(const QByteArray& c) {
    QVector<ShowHit> hits;
    const qint64 n = c.size();
    qint64 i = 0;
    // „Letzter Operand": Bereich + Typ (0 = keiner, 1 = String, 2 = Array).
    qint64 lastStart = -1, lastEnd = -1; int lastType = 0; QString lastText;
    while (i < n) {
        char ch = c[i];
        if (isWs(ch)) { ++i; continue; }
        if (ch == '%') { while (i<n && c[i]!='\n' && c[i]!='\r') ++i; continue; }
        if (ch == '(') {
            qint64 e = skipValue(c, i);
            lastStart=i; lastEnd=e; lastType=1;
            lastText = QString::fromLatin1(decodeParenString(c, i, e));
            i = e; continue;
        }
        if (ch == '<' && !(i+1<n && c[i+1]=='<')) {          // Hex-String
            qint64 e = skipValue(c, i);
            lastStart=i; lastEnd=e; lastType=1;
            QByteArray hex = c.mid(i+1, e-1-(i+1)); hex.replace(" ","").replace("\n","").replace("\r","");
            lastText = QString::fromLatin1(QByteArray::fromHex(hex));
            i = e; continue;
        }
        if (ch == '[') {
            qint64 e = skipValue(c, i);
            // Array-Text = Verkettung seiner String-Elemente (Zahlen = Spacing).
            QByteArray inner = c.mid(i, e-i);
            QString t; qint64 j = 0;
            while (j < inner.size()) {
                if (inner[j]=='(') { qint64 je = skipValue(inner, j);
                    t += QString::fromLatin1(decodeParenString(inner, j, je)); j = je; }
                else if (inner[j]=='<') { qint64 je = skipValue(inner, j);
                    QByteArray hex = inner.mid(j+1, je-1-(j+1)); hex.replace(" ","").replace("\n","").replace("\r","");
                    t += QString::fromLatin1(QByteArray::fromHex(hex)); j = je; }
                else ++j;
            }
            lastStart=i; lastEnd=e; lastType=2; lastText=t;
            i = e; continue;
        }
        if (ch == '<' && i+1<n && c[i+1]=='<') { i = skipValue(c, i); lastType=0; continue; }
        if (ch == '/') { i = skipValue(c, i); lastType=0; continue; }
        if (ch=='-'||ch=='+'||ch=='.'||(ch>='0'&&ch<='9')) {  // Zahl
            ++i; while (i<n && !isWs(c[i]) && !isDelim(c[i])) ++i; continue;
        }
        // Keyword/Operator
        qint64 s = i; while (i<n && !isWs(c[i]) && !isDelim(c[i])) ++i;
        const QByteArray op = c.mid(s, i-s);
        if (op == "BI") {                                    // Inline-Bild → bis EI
            qint64 ei = c.indexOf("EI", i);
            i = (ei < 0) ? n : ei + 2; lastType = 0; continue;
        }
        if (op == "Tj" && lastType == 1)
            hits.push_back({ lastStart, lastEnd, lastText, false });
        else if (op == "TJ" && lastType == 2)
            hits.push_back({ lastStart, lastEnd, lastText, true });
        lastType = 0;
    }
    return hits;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
namespace mg {

bool PdfContentEditor::editText(const QString& inputPath, const QString& outputPath,
                                const QVector<PdfTextEdit>& edits, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };
    if (edits.isEmpty()) return fail("keine Ersetzungen");

    QFile in(inputPath);
    if (!in.open(QIODevice::ReadOnly)) return fail("Quelle nicht lesbar");
    const QByteArray buf = in.readAll();
    in.close();
    if (buf.size() < 32 || !buf.startsWith("%PDF-")) return fail("kein PDF");
    if (buf.contains("/Encrypt")) return fail("verschlüsselt");

    // Letztes startxref lesen + prüfen, dass es eine KLASSISCHE xref-Tabelle ist.
    const int sxi = buf.lastIndexOf("startxref");
    if (sxi < 0) return fail("kein startxref");
    qint64 prevXrefOffset;
    {
        qint64 p = sxi + 9; while (p < buf.size() && isWs(buf[p])) ++p;
        qint64 s = p; while (p < buf.size() && buf[p]>='0' && buf[p]<='9') ++p;
        bool okv=false; prevXrefOffset = buf.mid(s, p-s).toLongLong(&okv);
        if (!okv || prevXrefOffset < 0 || prevXrefOffset >= buf.size())
            return fail("startxref ungültig");
        qint64 q = prevXrefOffset; while (q < buf.size() && isWs(buf[q])) ++q;
        if (buf.mid(q, 4) != "xref") return fail("kein klassisches xref (XRef-Stream)");
    }

    const QHash<int, ObjLoc> objs = scanObjects(buf);
    if (objs.isEmpty()) return fail("keine Objekte");

    // /Root ermitteln (Trailer bzw. beliebiges Vorkommen — letztes gewinnt).
    int rootNum = -1, rootGen = 0;
    {
        static const QRegularExpression re(QStringLiteral("/Root\\s+(\\d+)\\s+(\\d+)\\s+R"));
        auto it = re.globalMatch(QString::fromLatin1(buf));
        while (it.hasNext()) { const auto m = it.next(); rootNum = m.captured(1).toInt(); rootGen = m.captured(2).toInt(); }
    }
    if (rootNum < 0 || !objs.contains(rootNum)) return fail("kein /Root");

    // Objektkörper-Zugriff.
    auto bodyOf = [&](int num) -> QByteArray {
        const auto it = objs.constFind(num);
        return (it == objs.constEnd()) ? QByteArray() : objectBody(buf, it->offset);
    };
    auto dictOf = [&](int num) -> QByteArray { return dictOfObject(bodyOf(num)); };

    // Seitenbaum einsammeln (mit vererbtem /Resources), Reihenfolge = Dokument.
    const int pagesRoot = refValue(dictOf(rootNum), "Pages");
    if (pagesRoot < 0) return fail("kein /Pages");
    QVector<int> pageObjs;                 // Objektnummer je Seite
    QVector<QByteArray> pageRes;           // vererbtes /Resources-Dict je Seite
    {
        int guard = 0;
        // DFS über den Seitenbaum in Dokumentreihenfolge (Kids können Pages-
        // Knoten oder Page-Blätter sein); vererbtes /Resources wird durchgereicht.
        std::function<bool(int, const QByteArray&, int)> walk =
            [&](int num, const QByteArray& inheritedRes, int depth) -> bool {
                if (++guard > 100000 || depth > 50) return false;
                const QByteArray d = dictOf(num);
                if (d.isEmpty()) return false;
                const QByteArray type = nameValue(d, "Type");
                QByteArray res = inheritedRes;
                const qint64 rp = findKey(d, "Resources");
                if (rp >= 0) {
                    if (d[rp] == '<') { qint64 e = skipValue(d, rp); res = d.mid(rp, e-rp); }
                    else { const int rn = refValue(d, "Resources"); if (rn>=0) res = QByteArray("<<")+dictOf(rn)+">>"; }
                }
                if (type == "/Page") { pageObjs.push_back(num); pageRes.push_back(res); return true; }
                // /Pages-Knoten → /Kids [ ... ]
                const qint64 kp = findKey(d, "Kids");
                if (kp < 0 || d[kp] != '[') return false;
                const qint64 ke = skipValue(d, kp);
                const QByteArray kids = d.mid(kp, ke-kp);
                static const QRegularExpression kre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
                auto it = kre.globalMatch(QString::fromLatin1(kids));
                while (it.hasNext()) { const int kn = it.next().captured(1).toInt();
                    if (!walk(kn, res, depth+1)) return false; }
                return true;
            };
        if (!walk(pagesRoot, QByteArray(), 0)) return fail("Seitenbaum nicht lesbar");
    }
    if (pageObjs.isEmpty()) return fail("keine Seiten");

    // Ersetzungen nach Seite gruppieren; Vorbedingungen je betroffener Seite prüfen
    // und die neuen (inflateten) Content-Bytes bilden.
    struct NewObj { int num; int gen; QByteArray data; };  // data = inflatierter Content
    QHash<int, NewObj> edited;             // Content-ObjNum → neuer Inhalt

    for (const PdfTextEdit& ed : edits) {
        if (ed.page < 0 || ed.page >= pageObjs.size()) return fail("Seitenindex außerhalb");
        if (!asciiOnly(ed.original) || !asciiOnly(ed.replacement)) return fail("Nicht-ASCII-Text");
        if (ed.original.isEmpty()) return fail("leerer Originaltext");

        const int pnum = pageObjs[ed.page];
        const QByteArray pdict = dictOf(pnum);
        const QByteArray res   = pageRes[ed.page];

        // Font-Sicherheit: KEIN /Type0 unter /Resources/Font.
        {
            const qint64 fp = findKey(res, "Font");
            if (fp < 0) return fail("keine Fonts (Sicherheitscheck)");
            QByteArray fontDict;
            if (res[fp] == '<') { qint64 e = skipValue(res, fp); fontDict = res.mid(fp+2, (e-2)-(fp+2)); }
            else { const int fn = refValue(res, "Font"); if (fn<0) return fail("Font-Ref"); fontDict = dictOf(fn); }
            static const QRegularExpression fre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
            auto it = fre.globalMatch(QString::fromLatin1(fontDict));
            while (it.hasNext()) {
                const int fn = it.next().captured(1).toInt();
                if (nameValue(dictOf(fn), "Subtype") == "/Type0")
                    return fail("Type0/CID-Font → Fallback");
            }
        }

        // /Contents muss EIN Stream sein (kein Array).
        const qint64 cp = findKey(pdict, "Contents");
        if (cp < 0) return fail("kein /Contents");
        if (pdict[cp] == '[') return fail("/Contents-Array → Fallback");
        const int cnum = refValue(pdict, "Contents");
        if (cnum < 0 || !objs.contains(cnum)) return fail("/Contents-Ref");

        // Content-Objekt: Dict (nur /Length,/Filter erlaubt) + Stream-Rohdaten.
        const auto cit = objs.constFind(cnum);
        const QByteArray cbody = objectBody(buf, cit->offset);
        const QByteArray cdict = dictOfObject(cbody);
        // Nur bekannte Schlüssel? (sonst gingen sie beim Neuschreiben verloren)
        {
            static const QRegularExpression kre(QStringLiteral("/([A-Za-z0-9]+)"));
            auto it = kre.globalMatch(QString::fromLatin1(cdict));
            while (it.hasNext()) { const QString k = it.next().captured(1);
                if (k!="Length" && k!="Filter" && k!="DecodeParms" && k!="DL" && k!="FlateDecode")
                    return fail("unbekannte Stream-Schlüssel → Fallback"); }
        }
        // Filter
        const QByteArray filt = nameValue(cdict, "Filter");
        const bool flate = (filt == "/FlateDecode");
        if (!filt.isEmpty() && !flate) return fail("fremder Stream-Filter → Fallback");

        // Rohdaten zwischen stream…endstream.
        qint64 sPos = cbody.indexOf("stream");
        if (sPos < 0) return fail("kein stream");
        sPos += 6;
        if (sPos < cbody.size() && cbody[sPos]=='\r') ++sPos;
        if (sPos < cbody.size() && cbody[sPos]=='\n') ++sPos;
        qint64 ePos = cbody.indexOf("endstream", sPos);
        if (ePos < 0) return fail("kein endstream");
        // /Length bevorzugen (falls direkte Zahl), sonst bis endstream.
        qint64 rawLen = ePos - sPos;
        const qint64 declLen = intValue(cdict, "Length");
        if (declLen >= 0 && declLen <= rawLen) rawLen = declLen;
        QByteArray raw = cbody.mid(sPos, rawLen);

        QByteArray content;
        if (flate) { bool ok=false; content = zInflate(raw, &ok); if (!ok) return fail("Inflate fehlgeschlagen"); }
        else content = raw;

        // Auf bereits (für diese Seite) editierten Inhalt weiterarbeiten, falls
        // mehrere Ersetzungen dieselbe Seite betreffen.
        if (edited.contains(cnum)) content = edited.value(cnum).data;

        // Treffer suchen: der Originaltext GENAU EINMAL als Tj-String bzw.
        // TJ-Array.
        const QVector<ShowHit> hits = scanTextShows(content);
        int matchIdx = -1, matchCount = 0;
        for (int k = 0; k < hits.size(); ++k)
            if (hits[k].text == ed.original) { matchIdx = k; ++matchCount; }
        if (matchCount == 0) return fail("Originaltext nicht gefunden → Fallback");
        if (matchCount > 1) return fail("Originaltext mehrdeutig → Fallback");

        const ShowHit& h = hits[matchIdx];
        QByteArray repl;
        if (h.isArray) repl = QByteArray("[") + (ed.replacement.isEmpty() ? QByteArray()
                                                 : encodeParenString(ed.replacement)) + "]";
        else           repl = ed.replacement.isEmpty() ? QByteArray("()")
                                                        : encodeParenString(ed.replacement);
        QByteArray nc = content;
        nc.replace(h.start, h.end - h.start, repl);
        edited.insert(cnum, { cnum, cit->gen, nc });
    }
    if (edited.isEmpty()) return fail("nichts ersetzt");

    // ── Inkrementelles Update anhängen ──────────────────────────────────────
    QByteArray out = buf;
    if (!out.endsWith('\n')) out += '\n';
    int maxObj = 0; for (int num : objs.keys()) maxObj = qMax(maxObj, num);

    struct XEntry { int num; qint64 off; int gen; };
    QVector<XEntry> xentries;
    for (auto it = edited.constBegin(); it != edited.constEnd(); ++it) {
        const NewObj& no = it.value();
        const QByteArray def = zDeflate(no.data);
        if (def.isEmpty() && !no.data.isEmpty()) return fail("Deflate fehlgeschlagen");
        const qint64 off = out.size();
        xentries.push_back({ no.num, off, no.gen });
        out += QByteArray::number(no.num) + " " + QByteArray::number(no.gen) + " obj\n";
        out += "<< /Length " + QByteArray::number(def.size()) + " /Filter /FlateDecode >>\n";
        out += "stream\n";
        out += def;
        out += "\nendstream\nendobj\n";
    }

    // Neue klassische XRef-Sektion (je Objekt eine Subsektion) + Trailer.
    std::sort(xentries.begin(), xentries.end(), [](const XEntry&a,const XEntry&b){ return a.num<b.num; });
    const qint64 xrefOff = out.size();
    out += "xref\n";
    for (const XEntry& x : xentries) {
        out += QByteArray::number(x.num) + " 1\n";
        char line[24];
        std::snprintf(line, sizeof(line), "%010lld %05d n \n",
                      static_cast<long long>(x.off), x.gen);
        out += line;
    }
    out += "trailer\n<< /Size " + QByteArray::number(maxObj + 1)
         + " /Root " + QByteArray::number(rootNum) + " " + QByteArray::number(rootGen) + " R"
         + " /Prev " + QByteArray::number(prevXrefOffset) + " >>\n";
    out += "startxref\n" + QByteArray::number(xrefOff) + "\n%%EOF\n";

    QSaveFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("Ziel nicht schreibbar");
    if (f.write(out) != out.size()) { f.cancelWriting(); return fail("Schreibfehler"); }
    if (!f.commit()) return fail("Commit fehlgeschlagen");
    return true;
}

}  // namespace mg
