#include "pdf/PdfRawScan.h"

#include <QList>
#include <QPair>
#include <QStringDecoder>
#include <algorithm>
#include <cstring>

namespace mg::pdfraw {

bool isWs(char c)    { return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\0'; }
bool isDelim(char c) { return isWs(c)||c=='('||c==')'||c=='<'||c=='>'||c=='['||c==']'||c=='{'||c=='}'||c=='/'||c=='%'; }
void skipWs(const QByteArray& d, qsizetype& i) { while (i < d.size() && isWs(d[i])) ++i; }

long readUInt(const QByteArray& d, qsizetype& i) {
    const qsizetype s = i;
    while (i < d.size() && d[i] >= '0' && d[i] <= '9') ++i;
    if (i == s) return -1;
    bool ok = false; const long v = d.mid(s, i - s).toLong(&ok);
    return ok ? v : -1;
}

qsizetype keyPos(const QByteArray& d, const char* key, qsizetype from) {
    const QByteArray k(key);
    qsizetype p = from;
    while ((p = d.indexOf(k, p)) >= 0) {
        const qsizetype a = p + k.size();
        const char c = a < d.size() ? d[a] : ' ';
        if (isDelim(c)) return p;
        p = a;
    }
    return -1;
}

QByteArray readDictAt(const QByteArray& d, qsizetype from) {
    const qsizetype lt = d.indexOf("<<", from);
    if (lt < 0) return {};
    int depth = 0;
    for (qsizetype i = lt; i + 1 < d.size(); ++i) {
        if (d[i] == '<' && d[i+1] == '<')      { ++depth; ++i; }
        else if (d[i] == '>' && d[i+1] == '>') { --depth; ++i; if (depth == 0) return d.mid(lt, i - lt + 1); }
    }
    return {};
}

long intDirect(const QByteArray& dict, const char* key, long def) {
    const qsizetype kp = keyPos(dict, key); if (kp < 0) return def;
    qsizetype v = kp + qstrlen(key); skipWs(dict, v);
    const long a = readUInt(dict, v); return a < 0 ? def : a;
}

int firstRefForKey(const QByteArray& dict, const char* key) {
    const qsizetype klen = qstrlen(key);
    qsizetype from = 0;
    for (;;) {
        const qsizetype kp = keyPos(dict, key, from); if (kp < 0) return -1;
        qsizetype v = kp + klen; skipWs(dict, v);
        const long a = readUInt(dict, v);
        if (a >= 0) {
            qsizetype v2 = v; skipWs(dict, v2);
            const long b = readUInt(dict, v2); skipWs(dict, v2);
            if (b >= 0 && v2 < dict.size() && dict[v2] == 'R') return (int)a;
        }
        from = kp + klen;
    }
}

int firstAnyRef(const QByteArray& d) {
    qsizetype i = 0;
    while (i < d.size()) {
        if (d[i] >= '0' && d[i] <= '9') {
            qsizetype j = i; const long a = readUInt(d, j); skipWs(d, j);
            const long b = readUInt(d, j); skipWs(d, j);
            if (b >= 0 && j < d.size() && d[j] == 'R') return (int)a;
            i = (j > i) ? j : i + 1;
        } else ++i;
    }
    return -1;
}

QByteArray nestedDictForKey(const QByteArray& dict, const char* key) {
    const qsizetype kp = keyPos(dict, key); if (kp < 0) return {};
    qsizetype v = kp + qstrlen(key); skipWs(dict, v);
    if (v + 1 < dict.size() && dict[v] == '<' && dict[v+1] == '<') return readDictAt(dict, v);
    return {};
}

QByteArray bracketValue(const QByteArray& dict, const char* key) {
    const qsizetype kp = keyPos(dict, key); if (kp < 0) return {};
    qsizetype v = kp + qstrlen(key); skipWs(dict, v);
    if (v < dict.size() && dict[v] == '[') {
        const qsizetype e = dict.indexOf(']', v);
        if (e >= 0) return dict.mid(v, e - v + 1);
    }
    return {};
}

QString stringValue(const QByteArray& dict, const char* key) {
    const qsizetype kp = keyPos(dict, key); if (kp < 0) return {};
    qsizetype v = kp + qstrlen(key); skipWs(dict, v);
    if (v < dict.size() && dict[v] == '(') {
        int depth = 0; QByteArray out;
        for (qsizetype i = v; i < dict.size(); ++i) {
            const char c = dict[i];
            if (c == '(') { if (depth > 0) out += c; ++depth; }
            else if (c == ')') { --depth; if (depth == 0) return QString::fromLatin1(out); out += c; }
            else out += c;
        }
    }
    return {};
}

long lengthValue(const QByteArray& d, const QHash<int,qsizetype>& off, const QByteArray& dict) {
    const qsizetype kp = keyPos(dict, "/Length"); if (kp < 0) return -1;
    qsizetype v = kp + 7; skipWs(dict, v);
    const long a = readUInt(dict, v); if (a < 0) return -1;
    qsizetype v2 = v; skipWs(dict, v2);
    const long b = readUInt(dict, v2); skipWs(dict, v2);
    if (b >= 0 && v2 < dict.size() && dict[v2] == 'R') {           // „N G R" -> Objekt lesen
        if (off.contains((int)a)) { qsizetype o = off.value((int)a); skipWs(d, o); const long val = readUInt(d, o); if (val >= 0) return val; }
        return -1;
    }
    return a;                                                       // direkter Wert
}

bool isPageObject(const QByteArray& dict) {
    qsizetype t = keyPos(dict, "/Type");
    while (t >= 0) {
        qsizetype v = t + 5; skipWs(dict, v);
        if (v < dict.size() && dict[v] == '/') {
            qsizetype e = v + 1; while (e < dict.size() && !isDelim(dict[e])) ++e;
            if (dict.mid(v, e - v) == "/Page") return true;        // NICHT /Pages
        }
        t = keyPos(dict, "/Type", t + 5);
    }
    return false;
}

QSizeF mediaBoxSize(const QByteArray& pageDict) {
    QByteArray mb = bracketValue(pageDict, "/MediaBox");
    if (mb.size() < 2) mb = bracketValue(pageDict, "/CropBox");   // viele Seiten erben /MediaBox
    if (mb.size() < 2) return QSizeF(595, 842);
    const QByteArray inner = mb.mid(1, mb.size() - 2).trimmed();
    const QList<QByteArray> parts = inner.split(' ');
    QList<double> v; for (const auto& p : parts) { bool ok = false; const double d = p.trimmed().toDouble(&ok); if (ok) v << d; }
    if (v.size() < 4) return QSizeF(595, 842);
    return QSizeF(qAbs(v[2] - v[0]), qAbs(v[3] - v[1]));
}

QRectF parseNormalisedRect(const QByteArray& rectBytes, const QSizeF& ps) {
    if (rectBytes.size() < 2) return {};
    const QByteArray inner = rectBytes.mid(1, rectBytes.size() - 2).trimmed();
    const QList<QByteArray> parts = inner.split(' ');
    QList<double> v; for (const auto& p : parts) { bool ok = false; const double d = p.trimmed().toDouble(&ok); if (ok) v << d; }
    if (v.size() < 4) return {};
    double x1 = v[0], y1 = v[1], x2 = v[2], y2 = v[3];
    if (x2 < x1) std::swap(x1, x2);
    if (y2 < y1) std::swap(y1, y2);
    const double pw = ps.width()  > 0 ? ps.width()  : 595;
    const double ph = ps.height() > 0 ? ps.height() : 842;
    return QRectF(x1 / pw, 1.0 - y2 / ph, (x2 - x1) / pw, (y2 - y1) / ph);
}

QVector<qsizetype> findAll(const QByteArray& d, const char* pat) {
    QVector<qsizetype> r; const QByteArray p(pat); qsizetype i = 0;
    while ((i = d.indexOf(p, i)) >= 0) { r.append(i); i += p.size(); }
    return r;
}

QHash<int,qsizetype> buildObjectOffsets(const QByteArray& d, const std::atomic<bool>* cancel) {
    QHash<int,qsizetype> map; qsizetype p = 0;
    int tick = 0;
    while ((p = d.indexOf("obj", p)) >= 0) {
        //  Nicht bei jedem Treffer prüfen (atomarer Load in der heißen Schleife) -
        //  alle 4096 Objekte genügt für eine Reaktionszeit im Millisekundenbereich.
        if (((++tick) & 0xFFF) == 0 && cancel && cancel->load(std::memory_order_relaxed)) return {};
        const qsizetype after = p + 3;
        const char nc = after < d.size() ? d[after] : ' ';
        const char pc = p > 0 ? d[p-1] : ' ';
        if (isWs(pc) && (isWs(nc) || nc == '<' || nc == '[')) {
            qsizetype i = p - 1; while (i >= 0 && isWs(d[i])) --i;
            const qsizetype ge = i; while (i >= 0 && d[i] >= '0' && d[i] <= '9') --i;   // Generationsnummer
            if (i < ge) {
                while (i >= 0 && isWs(d[i])) --i;
                const qsizetype ne = i; while (i >= 0 && d[i] >= '0' && d[i] <= '9') --i; // Objektnummer
                if (i < ne) { bool ok = false; const long num = d.mid(i + 1, ne - i).toLong(&ok); if (ok && num > 0) map.insert((int)num, p + 3); }
            }
        }
        p = after;
    }
    return map;
}

QByteArray enclosingObjDict(const QByteArray& d, qsizetype pos) {
    const qsizetype k = d.lastIndexOf("obj", pos); if (k < 0) return {};
    const qsizetype lt = d.indexOf("<<", k); if (lt < 0 || lt > pos) return {};
    return readDictAt(d, lt);
}

QVector<int> kidsRefs(const QByteArray& dict) {
    QVector<int> r;
    const QByteArray arr = bracketValue(dict, "/Kids");
    qsizetype i = 0;
    while (i < arr.size()) {
        if (arr[i] >= '0' && arr[i] <= '9') {
            const long a = readUInt(arr, i); skipWs(arr, i);
            const long b = readUInt(arr, i); skipWs(arr, i);
            if (b >= 0 && i < arr.size() && arr[i] == 'R') { r.append((int)a); ++i; }
        } else ++i;
    }
    return r;
}

int findRootPagesObj(const QByteArray& d, const QHash<int,qsizetype>& off) {
    int catalog = -1;
    const qsizetype tr = d.lastIndexOf("trailer");
    if (tr >= 0) { const QByteArray td = readDictAt(d, tr); if (!td.isEmpty()) catalog = firstRefForKey(td, "/Root"); }
    QByteArray catDict;
    if (catalog > 0 && off.contains(catalog)) catDict = readDictAt(d, off.value(catalog));
    if (catDict.isEmpty()) {
        qsizetype c = d.lastIndexOf("/Type/Catalog"); if (c < 0) c = d.lastIndexOf("/Type /Catalog");
        if (c >= 0) catDict = enclosingObjDict(d, c);
    }
    if (catDict.isEmpty()) return -1;
    return firstRefForKey(catDict, "/Pages");
}

void flattenPages(const QByteArray& d, const QHash<int,qsizetype>& off, int num,
                  QVector<int>& out, QSet<int>& visited, int depth) {
    if (num <= 0 || depth > 50 || visited.contains(num) || !off.contains(num)) return;
    visited.insert(num);
    const qsizetype lt = d.indexOf("<<", off.value(num)); if (lt < 0) return;
    const QByteArray dict = readDictAt(d, lt); if (dict.isEmpty()) return;
    if (isPageObject(dict)) { out.append(num); return; }           // Blatt = Seite
    for (int k : kidsRefs(dict)) flattenPages(d, off, k, out, visited, depth + 1);
}
QByteArray objectDict(const QByteArray& d, const QHash<int,qsizetype>& off, int num) {
    const auto it = off.constFind(num);
    if (it == off.cend()) return {};
    qsizetype p = it.value();
    skipWs(d, p);
    if (p + 1 >= d.size() || d[p] != '<' || d[p+1] != '<') return {};
    return readDictAt(d, p);
}

QVector<int> pageObjects(const QByteArray& d, const QHash<int,qsizetype>& off) {
    QVector<int> pages;
    const int root = findRootPagesObj(d, off);
    if (root > 0) { QSet<int> vis; flattenPages(d, off, root, pages, vis, 0); }
    if (!pages.isEmpty()) return pages;
    QVector<QPair<qsizetype,int>> treffer;
    for (auto it = off.constBegin(); it != off.constEnd(); ++it)
        if (isPageObject(objectDict(d, off, it.key()))) treffer.append({ it.value(), it.key() });
    std::sort(treffer.begin(), treffer.end());
    for (const auto& t : treffer) pages.append(t.second);
    return pages;
}

namespace {

//  Literal-String ab der oeffnenden Klammer; `ok` false, wenn er nicht schliesst.
QByteArray literalAt(const QByteArray& d, qsizetype v, bool* ok) {
    QByteArray out;
    int tiefe = 0;
    for (qsizetype i = v; i < d.size(); ++i) {
        const char c = d[i];
        if (c == '\\') {
            if (++i >= d.size()) break;
            const char e = d[i];
            switch (e) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '\r': if (i + 1 < d.size() && d[i+1] == '\n') ++i; break;   // Zeilenfortsetzung
            case '\n': break;
            default:
                if (e >= '0' && e <= '7') {
                    int wert = e - '0';
                    for (int k = 0; k < 2 && i + 1 < d.size() && d[i+1] >= '0' && d[i+1] <= '7'; ++k)
                        wert = wert * 8 + (d[++i] - '0');
                    out += char(wert & 0xFF);
                } else {
                    out += e;                                                // \( \) \\ und Unbekanntes
                }
            }
            continue;
        }
        if (c == '(') { if (tiefe++ > 0) out += c; continue; }
        if (c == ')') { if (--tiefe == 0) { *ok = true; return out; } out += c; continue; }
        out += c;
    }
    *ok = false;
    return {};
}

QString alsText(const QByteArray& raw) {
    if (raw.size() >= 2 && uchar(raw[0]) == 0xFE && uchar(raw[1]) == 0xFF)
        return QStringDecoder(QStringDecoder::Utf16BE).decode(raw.mid(2));
    if (raw.startsWith("\xEF\xBB\xBF")) return QString::fromUtf8(raw.mid(3));
    return QString::fromLatin1(raw);
}

}  // namespace

QString stringForKey(const QByteArray& dict, const char* key) {
    const qsizetype klen = qsizetype(std::strlen(key));
    for (qsizetype from = 0;;) {
        const qsizetype kp = keyPos(dict, key, from);
        if (kp < 0) return {};
        qsizetype v = kp + klen;
        skipWs(dict, v);
        if (v < dict.size() && dict[v] == '(') {
            bool ok = false;
            const QByteArray raw = literalAt(dict, v, &ok);
            if (ok) return alsText(raw);
        } else if (v + 1 < dict.size() && dict[v] == '<' && dict[v+1] != '<') {
            const qsizetype e = dict.indexOf('>', v + 1);
            if (e > v) return alsText(QByteArray::fromHex(dict.mid(v + 1, e - v - 1)));
        }
        from = kp + klen;
    }
}

QByteArray streamData(const QByteArray& d, const QHash<int,qsizetype>& off, int num) {
    const auto it = off.constFind(num);
    if (it == off.cend()) return {};
    qsizetype p = it.value();
    skipWs(d, p);
    if (p + 1 >= d.size() || d[p] != '<' || d[p+1] != '<') return {};
    const QByteArray dict = readDictAt(d, p);
    if (dict.isEmpty()) return {};
    qsizetype s = p + dict.size();
    skipWs(d, s);
    if (d.mid(s, 6) != "stream") return {};
    s += 6;
    if (s < d.size() && d[s] == '\r') ++s;
    if (s < d.size() && d[s] == '\n') ++s;
    //  `/Length` gilt nur, wenn danach wirklich `endstream` steht - eine luegende Laenge faellt auf die Suche zurueck.
    const long len = lengthValue(d, off, dict);
    if (len >= 0 && qsizetype(len) <= d.size() - s) {
        qsizetype e = s + qsizetype(len);
        skipWs(d, e);
        if (d.mid(e, 9) == "endstream") return d.mid(s, qsizetype(len));
    }
    const qsizetype es = d.indexOf("endstream", s);
    if (es < 0) return {};
    qsizetype e = es;
    if (e > s && d[e-1] == '\n') --e;
    if (e > s && d[e-1] == '\r') --e;
    return d.mid(s, e - s);
}

}  // namespace mg::pdfraw
