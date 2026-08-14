#include "pdf/edit/PdfObjects.h"

#include <QRegularExpression>
#include <QString>
#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <functional>
#include <cstring>

#include "core/ZCodec.h"

// Die Erläuterungen zu jeder Funktion stehen im Header.
namespace mg::pdfobj {

// ── zlib raw/zlib inflate + deflate ─────────────────────────────────────────
QByteArray zInflate(const QByteArray& src, bool* ok) {
    *ok = false;
    // Erst der zlib-Rahmen (Normalfall), dann roh — manche Erzeuger schreiben
    // fehlerhafte Köpfe. Ohne ZLIB gebaut trägt nur der erste Versuch
    // (s. ZCodec.h); solche Sonderlinge werden dann abgelehnt.
    QByteArray out = mg::zcodec::inflate(src, mg::zcodec::Wrap::Zlib, 0,
                                         /*tolerant*/ false, ok);
    if (*ok) return out;
    return mg::zcodec::inflate(src, mg::zcodec::Wrap::Raw, 0,
                               /*tolerant*/ false, ok);
}

QByteArray zDeflate(const QByteArray& src) {
    bool ok = false;
    return mg::zcodec::deflate(src, mg::zcodec::Wrap::Zlib, 9, &ok);
}


// Objekt-Tabelle per Brute-Scan: "N G obj" → Byte-Offset + Generation (letztes
// Vorkommen gewinnt = jüngster Inkrement-Save).
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

//  /Length — direkt oder als Referenz. Siehe Header: die Referenzform ist der
//  Normalfall bei Qt-erzeugten Dateien.
qint64 streamLength(const QByteArray& dict, const QByteArray& buf,
                    const QHash<int, ObjLoc>& objs) {
    const int ref = refValue(dict, "Length");
    if (ref >= 0) {
        const auto it = objs.constFind(ref);
        if (it == objs.constEnd()) return -1;
        const QByteArray body = objectBody(buf, it->offset).trimmed();
        qint64 e = 0; bool any = false;
        while (e < body.size() && body[e] >= '0' && body[e] <= '9') { ++e; any = true; }
        return any ? body.left(e).toLongLong() : -1;
    }
    return intValue(dict, "Length");
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


// ── Zahlen ──────────────────────────────────────────────────────────────────
//  PDF-Zahlen: höchstens drei Nachkommastellen, kein Exponent, Punkt als
//  Dezimaltrenner (locale-unabhängig).
QByteArray num(qreal v) {
    if (!std::isfinite(v)) v = 0.0;
    QByteArray s = QByteArray::number(v, 'f', 3);
    if (s.contains('.')) {
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
    }
    if (s == "-0") s = "0";
    return s;
}

// ── Strings ─────────────────────────────────────────────────────────────────
//  ( ) \ escapen, alles ausserhalb des druckbaren ASCII oktal — 7-Bit-sicher.
QByteArray parenString(const QByteArray& bytes) {
    QByteArray out = "(";
    for (char c : bytes) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c=='(' || c==')' || c=='\\') { out += '\\'; out += c; }
        else if (u < 0x20 || u > 0x7E) {
            out += '\\';
            out += QByteArray::number(u, 8).rightJustified(3, '0');
        }
        else out += c;
    }
    return out + ")";
}

//  Liest den STRING-Wert ab `i` (Literal `(…)` oder Hex `<…>`) als ROHBYTES.
//  Liefert false, wenn dort kein String steht oder er unabgeschlossen ist.
bool readPdfStringBytes(const QByteArray& b, qint64 i, QByteArray* out) {
    if (i < 0 || i >= b.size()) return false;
    if (b[i] == '(') {
        QByteArray r; int depth = 0; qint64 p = i + 1;
        while (p < b.size()) {
            const char c = b[p];
            if (c == '\\') {
                if (p + 1 >= b.size()) return false;
                const char e = b[p+1];
                switch (e) {
                case 'n':  r += '\n'; p += 2; break;
                case 'r':  r += '\r'; p += 2; break;
                case 't':  r += '\t'; p += 2; break;
                case 'b':  r += '\b'; p += 2; break;
                case 'f':  r += '\f'; p += 2; break;
                case '(':  r += '(';  p += 2; break;
                case ')':  r += ')';  p += 2; break;
                case '\\': r += '\\'; p += 2; break;
                case '\r': p += 2; if (p < b.size() && b[p] == '\n') ++p; break;  // Zeilenfortsetzung
                case '\n': p += 2; break;
                default:
                    if (e >= '0' && e <= '7') {                  // Oktal \ddd
                        int v = 0, k = 0; ++p;
                        while (k < 3 && p < b.size() && b[p] >= '0' && b[p] <= '7') {
                            v = v*8 + (b[p]-'0'); ++p; ++k;
                        }
                        r += char(v & 0xFF);
                    } else { r += e; p += 2; }
                }
                continue;
            }
            if (c == '(') { ++depth; r += c; ++p; continue; }
            if (c == ')') {
                if (depth == 0) { *out = r; return true; }
                --depth; r += c; ++p; continue;
            }
            r += c; ++p;
        }
        return false;                                            // nie geschlossen
    }
    if (b[i] == '<' && !(i+1 < b.size() && b[i+1] == '<')) {
        QByteArray hex; qint64 p = i + 1;
        while (p < b.size() && b[p] != '>') { if (!isWs(b[p])) hex += b[p]; ++p; }
        if (p >= b.size()) return false;
        if (hex.size() % 2) hex += '0';                          // ungerade → mit 0 auffüllen
        *out = QByteArray::fromHex(hex);
        return true;
    }
    return false;
}

//  PDF-Textstring-Rohbytes → Text. UTF-16BE erkennt man am BOM; alles andere
//  ist PDFDocEncoding, das für die belegten Codes mit Latin-1 übereinstimmt.
QString pdfTextToString(const QByteArray& raw) {
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE
                        && static_cast<unsigned char>(raw[1]) == 0xFF) {
        QString s;
        for (qint64 p = 2; p + 1 < raw.size(); p += 2)
            s += QChar(static_cast<ushort>((static_cast<unsigned char>(raw[p]) << 8)
                                           | static_cast<unsigned char>(raw[p+1])));
        return s;
    }
    return QString::fromLatin1(raw);
}

//  Text → PDF-Textstring. Reines ASCII bleibt lesbar als Literal; alles andere
//  wird UTF-16BE mit BOM (universell verstanden, im Gegensatz zu Latin-1).
QByteArray toPdfTextString(const QString& s) {
    bool ascii = true;
    for (const QChar c : s)
        if (c.unicode() < 0x20 || c.unicode() > 0x7E) { ascii = false; break; }
    if (ascii) return parenString(s.toLatin1());
    QByteArray hex = "FEFF";
    for (const QChar c : s)
        hex += QByteArray::number(c.unicode(), 16).rightJustified(4, '0').toUpper();
    return "<" + hex + ">";
}

//  PDF-Name → Text ohne führenden Schrägstrich; `#xx` wird aufgelöst.
QString nameToString(const QByteArray& name) {
    QByteArray n = name.startsWith('/') ? name.mid(1) : name;
    QByteArray out;
    for (int i = 0; i < n.size(); ++i) {
        if (n[i] == '#' && i + 2 < n.size()) {
            bool ok = false;
            const int v = n.mid(i+1, 2).toInt(&ok, 16);
            if (ok) { out += char(v); i += 2; continue; }
        }
        out += n[i];
    }
    return QString::fromLatin1(out);
}

//  Text → PDF-Name mit führendem Schrägstrich; Sonderzeichen als `#xx`.
QByteArray toPdfName(const QString& s) {
    QByteArray out = "/";
    for (const char c : s.toLatin1()) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7F || isDelim(c) || c == '#')
            out += "#" + QByteArray::number(u, 16).rightJustified(2, '0').toUpper();
        else out += c;
    }
    return out;
}

// ── Dict-Werkzeug ───────────────────────────────────────────────────────────
//  Rohes Wert-Stück von `/key` (leer, wenn nicht vorhanden).
QByteArray rawValue(const QByteArray& dict, const char* key) {
    const qint64 p = findKey(dict, key);
    if (p < 0) return {};
    const qint64 e = skipValue(dict, p);
    return (e > p) ? dict.mid(p, e - p).trimmed() : QByteArray();
}

//  Setzt/ersetzt `/key` im Dict-INHALT (ohne die äußeren `<< >>`).
QByteArray setDictKey(QByteArray dict, const char* key, const QByteArray& value) {
    const qint64 p = findKey(dict, key);
    if (p < 0) return dict + " /" + key + " " + value + " ";
    const qint64 e = skipValue(dict, p);
    dict.replace(p, e - p, value);
    return dict;
}

//  Alle Schlüsselnamen auf Ebene 0 eines Dict-Inhalts, in Dokumentreihenfolge.
QList<QByteArray> dictKeys(const QByteArray& dict) {
    QList<QByteArray> keys;
    const qint64 n = dict.size();
    qint64 i = 0;
    while (i < n) {
        while (i < n && isWs(dict[i])) ++i;
        if (i >= n) break;
        if (dict[i] != '/') { ++i; continue; }
        const qint64 s = i; ++i;
        while (i < n && !isWs(dict[i]) && !isDelim(dict[i])) ++i;
        keys.push_back(dict.mid(s, i - s));
        while (i < n && isWs(dict[i])) ++i;
        i = skipValue(dict, i);
    }
    return keys;
}

//  Bis zu vier Zahlen aus einem `[ … ]`-Stück.
QVector<double> numbersOfArray(const QByteArray& arr) {
    QVector<double> v;
    const QByteArray inner = arr.startsWith('[') ? arr.mid(1, arr.size() - 2) : arr;
    const QList<QByteArray> parts = inner.simplified().split(' ');
    for (const QByteArray& p : parts) {
        if (p.isEmpty()) continue;
        bool ok = false;
        const double d = p.toDouble(&ok);
        if (ok) v.push_back(d);
    }
    return v;
}

// ── PdfDoc ──────────────────────────────────────────────────────────────────
QByteArray PdfDoc::bodyOf(int n) const {
    const auto it = objs.constFind(n);
    return (it == objs.constEnd()) ? QByteArray() : objectBody(buf, it->offset);
}

QByteArray PdfDoc::dictOf(int n) const { return dictOfObject(bodyOf(n)); }

int PdfDoc::genOf(int n) const {
    const auto it = objs.constFind(n);
    return (it == objs.constEnd()) ? 0 : it->gen;
}

QByteArray PdfDoc::resolved(const QByteArray& dict, const char* key) const {
    const QByteArray raw = rawValue(dict, key);
    if (raw.isEmpty()) return {};
    static const QRegularExpression re(QStringLiteral("^(\\d+)\\s+(\\d+)\\s+R$"));
    const auto m = re.match(QString::fromLatin1(raw));
    if (!m.hasMatch()) return raw;
    return bodyOf(m.captured(1).toInt()).trimmed();
}

QSizeF PdfDoc::pageBox(int pageObj) const {
    int cur = pageObj;
    for (int hop = 0; hop < 50 && cur >= 0; ++hop) {
        const QByteArray d = dictOf(cur);
        const QByteArray mb = rawValue(d, "MediaBox");
        if (mb.startsWith('[')) {
            const QVector<double> v = numbersOfArray(mb);
            if (v.size() >= 4)
                return QSizeF(qAbs(v[2] - v[0]), qAbs(v[3] - v[1]));
        }
        cur = refValue(d, "Parent");
    }
    return QSizeF();
}

int PdfDoc::pageRotate(int pageObj) const {
    int cur = pageObj;
    for (int hop = 0; hop < 50 && cur >= 0; ++hop) {
        const QByteArray d = dictOf(cur);
        const QByteArray r = rawValue(d, "Rotate");
        if (!r.isEmpty()) {
            bool ok = false;
            const int v = r.simplified().toInt(&ok);
            if (ok) return ((v % 360) + 360) % 360 / 90 * 90;
        }
        cur = refValue(d, "Parent");
    }
    return 0;
}

int PdfDoc::maxObjNum() const {
    int mx = 0;
    for (auto it = objs.constBegin(); it != objs.constEnd(); ++it)
        mx = qMax(mx, it.key());
    return mx;
}

bool PdfDoc::load(const QString& path, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return fail("Quelle nicht lesbar");
    this->buf = in.readAll();
    in.close();
    if (this->buf.size() < 32 || !this->buf.startsWith("%PDF-")) return fail("kein PDF");
    if (this->buf.contains("/Encrypt")) return fail("verschlüsselt");

    const int sxi = this->buf.lastIndexOf("startxref");
    if (sxi < 0) return fail("kein startxref");
    {
        qint64 p = sxi + 9; while (p < this->buf.size() && isWs(this->buf[p])) ++p;
        const qint64 s = p;
        while (p < this->buf.size() && this->buf[p] >= '0' && this->buf[p] <= '9') ++p;
        bool ok = false;
        this->prevXref = this->buf.mid(s, p - s).toLongLong(&ok);
        if (!ok || this->prevXref < 0 || this->prevXref >= this->buf.size())
            return fail("startxref ungültig");
        qint64 q = this->prevXref; while (q < this->buf.size() && isWs(this->buf[q])) ++q;
        if (this->buf.mid(q, 4) != "xref") return fail("kein klassisches xref (XRef-Stream)");
    }

    this->objs = scanObjects(this->buf);
    if (this->objs.isEmpty()) return fail("keine Objekte");

    {
        static const QRegularExpression re(QStringLiteral("/Root\\s+(\\d+)\\s+(\\d+)\\s+R"));
        auto it = re.globalMatch(QString::fromLatin1(this->buf));
        while (it.hasNext()) this->rootNum = it.next().captured(1).toInt();
    }
    if (this->rootNum < 0 || !this->objs.contains(this->rootNum)) return fail("kein /Root");

    //  Seitenbaum in Dokumentreihenfolge (gleiches Muster wie PdfVectorExport).
    {
        int guard = 0;
        std::function<bool(int,int)> walk = [&](int n, int depth) -> bool {
            if (++guard > 100000 || depth > 50) return false;
            const QByteArray d = this->dictOf(n);
            if (d.isEmpty()) return false;
            if (nameValue(d, "Type") == "/Page") { this->pageObjs.push_back(n); return true; }
            const QByteArray kids = rawValue(d, "Kids");
            if (!kids.startsWith('[')) return false;
            static const QRegularExpression kre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
            auto it = kre.globalMatch(QString::fromLatin1(kids));
            while (it.hasNext())
                if (!walk(it.next().captured(1).toInt(), depth + 1)) return false;
            return true;
        };
        const int pagesRoot = refValue(this->dictOf(this->rootNum), "Pages");
        if (pagesRoot < 0 || !walk(pagesRoot, 0)) return fail("Seitenbaum nicht lesbar");
    }
    if (this->pageObjs.isEmpty()) return fail("keine Seiten");

    //  Annotation → Seite: je Seite die /Annots-Referenzen einsammeln. Das ist
    //  die verlässliche Richtung; /P in der Annotation ist optional.
    {
        static const QRegularExpression are(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
        for (int pi = 0; pi < this->pageObjs.size(); ++pi) {
            const QByteArray annots = this->resolved(this->dictOf(this->pageObjs.at(pi)), "Annots");
            if (!annots.startsWith('[')) continue;
            auto it = are.globalMatch(QString::fromLatin1(annots));
            while (it.hasNext())
                this->annotPage.insert(it.next().captured(1).toInt(), pi);
        }
    }
    return true;
}

// ── IncrementalUpdate ───────────────────────────────────────────────────────
IncrementalUpdate::IncrementalUpdate(const PdfDoc& doc)
    : m_out(doc.buf), m_rootNum(doc.rootNum), m_prevXref(doc.prevXref) {
    if (!m_out.endsWith('\n')) m_out += '\n';
    m_nextObj = doc.maxObjNum() + 1;
    for (auto it = doc.objs.constBegin(); it != doc.objs.constEnd(); ++it)
        m_gens.insert(it.key(), it->gen);
}

int IncrementalUpdate::reserveObjNum() { return m_nextObj++; }

void IncrementalUpdate::addObject(int num, int gen, const QByteArray& body) {
    m_entries.push_back({ num, m_out.size(), gen });
    m_out += QByteArray::number(num) + " " + QByteArray::number(gen) + " obj\n";
    m_out += body;
    m_out += "\nendobj\n";
    if (num >= m_nextObj) m_nextObj = num + 1;
}

void IncrementalUpdate::addStream(int num, int gen, const QByteArray& dictExtra,
                                  const QByteArray& data) {
    m_entries.push_back({ num, m_out.size(), gen });
    m_out += QByteArray::number(num) + " " + QByteArray::number(gen) + " obj\n";
    m_out += "<< " + dictExtra + " /Length " + QByteArray::number(data.size())
           + " >>\nstream\n";
    m_out += data;
    m_out += "\nendstream\nendobj\n";
    if (num >= m_nextObj) m_nextObj = num + 1;
}

void IncrementalUpdate::replaceDict(int num, const QByteArray& dictInner) {
    addObject(num, m_gens.value(num, 0), "<<" + dictInner + ">>");
}

bool IncrementalUpdate::isEmpty() const { return m_entries.isEmpty(); }

bool IncrementalUpdate::commit(const QString& outputPath, QString* err) {
    auto fail = [&](const char* m) {
        if (err) *err = QString::fromLatin1(m);
        return false;
    };
    if (m_entries.isEmpty())            return fail("nichts zu schreiben");
    if (m_rootNum < 0 || m_prevXref < 0) return fail("Quelle unvollstaendig");

    //  Je Objekt EIN xref-Abschnitt: die Nummern sind nicht zwingend
    //  zusammenhaengend, und Abschnitte der Laenge 1 sind zulaessig.
    QVector<Entry> sorted = m_entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const Entry& a, const Entry& b) { return a.num < b.num; });

    QByteArray out = m_out;
    const qint64 xrefOff = out.size();
    out += "xref\n";
    for (const Entry& e : sorted) {
        out += QByteArray::number(e.num) + " 1\n";
        out += QByteArray::number(e.off).rightJustified(10, '0') + " "
             + QByteArray::number(e.gen).rightJustified(5, '0') + " n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(m_nextObj)
         + " /Root " + QByteArray::number(m_rootNum) + " 0 R"
         + " /Prev " + QByteArray::number(m_prevXref) + " >>\n";
    out += "startxref\n" + QByteArray::number(xrefOff) + "\n%%EOF\n";

    QSaveFile sf(outputPath);
    if (!sf.open(QIODevice::WriteOnly))  return fail("Ziel nicht schreibbar");
    if (sf.write(out) != out.size()) { sf.cancelWriting(); return fail("Schreibfehler"); }
    if (!sf.commit())                    return fail("Commit fehlgeschlagen");
    return true;
}

// ── Koordinaten ─────────────────────────────────────────────────────────────
QPointF toDisplay(double ux, double uy, const QSizeF& box, int rot) {
    switch (rot) {
    case 90:  return QPointF(uy, ux);
    case 180: return QPointF(box.width() - ux, uy);            // yTop = bh - dy, dy = bh - uy
    case 270: return QPointF(box.height() - uy, box.width() - ux);
    default:  return QPointF(ux, box.height() - uy);
    }
}

QPointF toUser(double dx, double dy, const QSizeF& box, int rot) {
    switch (rot) {
    case 90:  return QPointF(dy, dx);
    case 180: return QPointF(box.width() - dx, dy);
    case 270: return QPointF(box.width() - dy, box.height() - dx);
    default:  return QPointF(dx, box.height() - dy);
    }
}

} // namespace mg::pdfobj
