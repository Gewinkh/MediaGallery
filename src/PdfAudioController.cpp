#include "PdfAudioController.h"
#include "PathUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>
#include <QRunnable>
#include <QSizeF>
#include <QPair>
#include <algorithm>
#include <utility>
#include <cstring>
#include <zlib.h>

// ══════════════════════════════════════════════════════════════════════════════
//  Roh-PDF-Parser-Helfer (frei, im anonymen Namespace) + Worker-Tasks.
//  Bewusst KEIN QPdfDocument: Seitengroessen kommen aus /MediaBox, alles andere
//  aus dem rohen Bytestrom. Voraussetzung (von den Ziel-PDFs erfuellt): KLASSISCHE
//  Objekte (kein /ObjStm-komprimiertes Objekt-/XRef-Stream-Layout) → per „N G obj"
//  scanbar.
// ══════════════════════════════════════════════════════════════════════════════
namespace {

inline bool isWs(char c)    { return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\0'; }
inline bool isDelim(char c) { return isWs(c)||c=='('||c==')'||c=='<'||c=='>'||c=='['||c==']'||c=='{'||c=='}'||c=='/'||c=='%'; }
inline void skipWs(const QByteArray& d, qsizetype& i) { while (i < d.size() && isWs(d[i])) ++i; }

// Liest eine vorzeichenlose Ganzzahl ab i (i wird hinter die Ziffern gesetzt). -1 = keine.
long readUInt(const QByteArray& d, qsizetype& i) {
    const qsizetype s = i;
    while (i < d.size() && d[i] >= '0' && d[i] <= '9') ++i;
    if (i == s) return -1;
    bool ok = false; const long v = d.mid(s, i - s).toLong(&ok);
    return ok ? v : -1;
}

// Position eines Namens-SCHLUESSELS (Token-Grenze: Folgezeichen ist Delimiter →
// „/A" matcht NICHT „/AA"/„/Annot").
qsizetype keyPos(const QByteArray& d, const char* key, qsizetype from = 0) {
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

// Liest ein (ggf. verschachteltes) Dictionary ab der naechsten „<<" hinter `from`.
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

// Direkter Ganzzahlwert eines Schluessels (kein Ref-Aufloesen). def bei Fehlen.
long intDirect(const QByteArray& dict, const char* key, long def) {
    const qsizetype kp = keyPos(dict, key); if (kp < 0) return def;
    qsizetype v = kp + qstrlen(key); skipWs(dict, v);
    const long a = readUInt(dict, v); return a < 0 ? def : a;
}

// Erste Objektnummer eines Schluessels, dessen Wert eine indirekte Referenz „N G R"
// ist. Ueberspringt gleichnamige NAMENS-Werte (z. B. „/S /Sound" vor „/Sound N R").
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

// Erste indirekte Referenz „N G R" irgendwo in den Bytes.
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

// Verschachteltes Dictionary als Wert eines Schluessels („/AA<<…>>").
QByteArray nestedDictForKey(const QByteArray& dict, const char* key) {
    const qsizetype kp = keyPos(dict, key); if (kp < 0) return {};
    qsizetype v = kp + qstrlen(key); skipWs(dict, v);
    if (v + 1 < dict.size() && dict[v] == '<' && dict[v+1] == '<') return readDictAt(dict, v);
    return {};
}

// Array-Wert „[ … ]" eines Schluessels (z. B. /Rect, /MediaBox).
QByteArray bracketValue(const QByteArray& dict, const char* key) {
    const qsizetype kp = keyPos(dict, key); if (kp < 0) return {};
    qsizetype v = kp + qstrlen(key); skipWs(dict, v);
    if (v < dict.size() && dict[v] == '[') {
        const qsizetype e = dict.indexOf(']', v);
        if (e >= 0) return dict.mid(v, e - v + 1);
    }
    return {};
}

// String-Wert „( … )" eines Schluessels (z. B. /T, /Contents). Einfache Klammer-
// balance (Labels enthalten praktisch nie maskierte Klammern).
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

// /Length: direkter Wert ODER indirekte Referenz (dann Zielobjekt lesen).
long lengthValue(const QByteArray& d, const QHash<int,qsizetype>& off, const QByteArray& dict) {
    const qsizetype kp = keyPos(dict, "/Length"); if (kp < 0) return -1;
    qsizetype v = kp + 7; skipWs(dict, v);
    const long a = readUInt(dict, v); if (a < 0) return -1;
    qsizetype v2 = v; skipWs(dict, v2);
    const long b = readUInt(dict, v2); skipWs(dict, v2);
    if (b >= 0 && v2 < dict.size() && dict[v2] == 'R') {           // „N G R" → Objekt lesen
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

// Normalisiertes Rechteck [0..1], y=0 oben (PDF-Ursprung unten links → gespiegelt).
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

// Alle Vorkommen eines Musters.
QVector<qsizetype> findAll(const QByteArray& d, const char* pat) {
    QVector<qsizetype> r; const QByteArray p(pat); qsizetype i = 0;
    while ((i = d.indexOf(p, i)) >= 0) { r.append(i); i += p.size(); }
    return r;
}

// Objektnummer → Byte-Offset HINTER „obj" (Start des Objektkoerpers). „endobj"
// wird nicht als Deklaration gewertet (Zeichen vor „obj" muss Whitespace sein).
QHash<int,qsizetype> buildObjectOffsets(const QByteArray& d) {
    QHash<int,qsizetype> map; qsizetype p = 0;
    while ((p = d.indexOf("obj", p)) >= 0) {
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

// Dictionary des Objekts, das `pos` enthaelt (naechstes „obj" rueckwaerts).
QByteArray enclosingObjDict(const QByteArray& d, qsizetype pos) {
    const qsizetype k = d.lastIndexOf("obj", pos); if (k < 0) return {};
    const qsizetype lt = d.indexOf("<<", k); if (lt < 0 || lt > pos) return {};
    return readDictAt(d, lt);
}

// Aufloesung Annotation/Widget → Objektnummer des Sound-Streams (-1 wenn keiner).
int resolveSoundObj(const QByteArray& d, const QHash<int,qsizetype>& off, const QByteArray& annotDict) {
    // (a) Klassische /Subtype /Sound-Annotation: /Sound zeigt direkt auf den Stream.
    const int direct = firstRefForKey(annotDict, "/Sound");
    if (direct > 0) return direct;
    // (b) Widget-Button: Aktion ueber /A (Ref) oder /AA<</D N 0 R …>>.
    int actionObj = firstRefForKey(annotDict, "/A");
    if (actionObj <= 0) { const QByteArray aa = nestedDictForKey(annotDict, "/AA"); if (!aa.isEmpty()) actionObj = firstAnyRef(aa); }
    QByteArray actionDict;
    if (actionObj > 0 && off.contains(actionObj)) actionDict = readDictAt(d, off.value(actionObj));
    if (actionDict.isEmpty()) actionDict = nestedDictForKey(annotDict, "/A");   // Inline-Aktion (selten)
    if (actionDict.isEmpty()) return -1;
    if (!actionDict.contains("/Sound")) return -1;                              // muss /S /Sound sein
    const int m = firstRefForKey(actionDict, "/Sound");
    return m > 0 ? m : -1;
}

// Sound-Stream-Parameter + Byte-Offsets der Stream-Daten ermitteln.
bool soundStreamInfo(const QByteArray& d, const QHash<int,qsizetype>& off, int soundObj,
                     int& bits, int& ch, int& rate, bool& flate,
                     qsizetype& streamStart, qsizetype& streamLen) {
    if (!off.contains(soundObj)) return false;
    const qsizetype lt = d.indexOf("<<", off.value(soundObj)); if (lt < 0) return false;
    const QByteArray dict = readDictAt(d, lt); if (dict.isEmpty()) return false;
    const qsizetype dictEnd = lt + dict.size();

    bits  = qMax(1, (int)intDirect(dict, "/B", 8));      // /B Bits/Sample (Default 8)
    ch    = qMax(1, (int)intDirect(dict, "/C", 1));      // /C Kanaele      (Default 1)
    rate  = qMax(1, (int)intDirect(dict, "/R", 8000));   // /R Samplerate   (Default 8000)
    flate = dict.contains("/FlateDecode");

    const qsizetype st = d.indexOf("stream", dictEnd); if (st < 0) return false;
    qsizetype p = st + 6;
    if (p < d.size() && d[p] == '\r') ++p;
    if (p < d.size() && d[p] == '\n') ++p;
    streamStart = p;

    const long len = lengthValue(d, off, dict);
    if (len > 0) streamLen = len;
    else {
        const qsizetype es = d.indexOf("endstream", p); if (es < 0) return false;
        qsizetype e = es; if (e > p && d[e-1] == '\n') --e; if (e > p && d[e-1] == '\r') --e;
        streamLen = e - p;
    }
    return streamLen > 0;
}

// Objektnummern aus einem /Kids-Array „[16 0 R 17 0 R …]".
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

// Wurzel-/Pages-Objekt ermitteln: Trailer /Root → Catalog → /Pages (autoritativ),
// Fallback letztes /Type /Catalog-Objekt.
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

// Seitenbaum in Lesereihenfolge abflachen (echte /Page-Objekte in /Kids-Folge).
void flattenPages(const QByteArray& d, const QHash<int,qsizetype>& off, int num,
                  QVector<int>& out, QSet<int>& visited, int depth) {
    if (num <= 0 || depth > 50 || visited.contains(num) || !off.contains(num)) return;
    visited.insert(num);
    const qsizetype lt = d.indexOf("<<", off.value(num)); if (lt < 0) return;
    const QByteArray dict = readDictAt(d, lt); if (dict.isEmpty()) return;
    if (isPageObject(dict)) { out.append(num); return; }           // Blatt = Seite
    for (int k : kidsRefs(dict)) flattenPages(d, off, k, out, visited, depth + 1);
}

// ── Haupt-Scan: alle eingebetteten Audio-Clips eines PDFs (nur Metadaten) ──────
QVector<PdfAudioClip> scanClips(const QByteArray& d) {
    QVector<PdfAudioClip> out;
    const QHash<int,qsizetype> off = buildObjectOffsets(d);

    // Seitenobjekte in AUTORITATIVER Lesereihenfolge (Seitenbaum /Pages→/Kids).
    // Reines Byte-Offset-Scannen waere falsch: Illustrator-Inkrement-Saves
    // hinterlassen verwaiste /Type/Page-Objekte und die Datei-Reihenfolge ist
    // nicht die Seitenfolge.
    QVector<int> pageObjs; QHash<int,QSizeF> pageSize;
    {
        const int rootPages = findRootPagesObj(d, off);
        if (rootPages > 0) { QSet<int> vis; flattenPages(d, off, rootPages, pageObjs, vis, 0); }
        if (pageObjs.isEmpty()) {                       // Fallback: Datei-Reihenfolge
            QVector<QPair<qsizetype,int>> hits;
            for (auto it = off.constBegin(); it != off.constEnd(); ++it) {
                const qsizetype lt = d.indexOf("<<", it.value()); if (lt < 0) continue;
                const QByteArray dict = readDictAt(d, lt);
                if (!dict.isEmpty() && isPageObject(dict)) hits.append({ lt, it.key() });
            }
            std::sort(hits.begin(), hits.end());
            for (const auto& h : hits) pageObjs.append(h.second);
        }
        for (int pn : pageObjs) {
            QSizeF sz(595, 842);
            const qsizetype lt = off.contains(pn) ? d.indexOf("<<", off.value(pn)) : -1;
            if (lt >= 0) sz = mediaBoxSize(readDictAt(d, lt));
            pageSize.insert(pn, sz);
        }
    }

    QSet<int> seen;   // Dedup ueber die Sound-Stream-Objektnummer
    const auto addAnnot = [&](const QByteArray& ad) {
        const int sObj = resolveSoundObj(d, off, ad); if (sObj <= 0 || seen.contains(sObj)) return;
        int bits, ch, rate; bool flate; qsizetype ss = 0, sl = 0;
        if (!soundStreamInfo(d, off, sObj, bits, ch, rate, flate, ss, sl)) return;

        int pidx = 0; QSizeF psz(595, 842);
        const int pRef = firstRefForKey(ad, "/P");
        if (pRef > 0) { const int ix = pageObjs.indexOf(pRef); if (ix >= 0) { pidx = ix; psz = pageSize.value(pRef, psz); } }

        PdfAudioClip c;
        c.page = pidx;
        c.rect = parseNormalisedRect(bracketValue(ad, "/Rect"), psz);
        c.bits = bits; c.channels = ch; c.rate = rate; c.flate = flate;
        c.streamStart = ss; c.streamLen = sl;
        QString lab = stringValue(ad, "/T"); if (lab.isEmpty()) lab = stringValue(ad, "/Contents");
        c.label = lab;
        out.append(c); seen.insert(sObj);
    };

    // Pass A: Widget-Buttons mit Sound-Aktion.
    { QVector<qsizetype> hits = findAll(d, "/Subtype/Widget"); hits += findAll(d, "/Subtype /Widget");
      for (qsizetype p : hits) { const QByteArray ad = enclosingObjDict(d, p); if (!ad.isEmpty()) addAnnot(ad); } }
    // Pass B: klassische /Subtype /Sound-Annotationen.
    { QVector<qsizetype> hits = findAll(d, "/Subtype/Sound"); hits += findAll(d, "/Subtype /Sound");
      for (qsizetype p : hits) { const QByteArray ad = enclosingObjDict(d, p); if (!ad.isEmpty()) addAnnot(ad); } }

    // Stabile Reihenfolge: Seite, dann von oben nach unten, dann links nach rechts.
    std::sort(out.begin(), out.end(), [](const PdfAudioClip& a, const PdfAudioClip& b) {
        if (a.page != b.page) return a.page < b.page;
        if (a.rect.y() != b.rect.y()) return a.rect.y() < b.rect.y();
        return a.rect.x() < b.rect.x();
    });
    for (int i = 0; i < out.size(); ++i) out[i].id = i;
    return out;
}

// ── zlib-Inflate (FlateDecode). 15+32 = Fenster 15, Header-Autoerkennung. ──────
QByteArray zlibInflate(const QByteArray& in) {
    if (in.isEmpty()) return {};
    z_stream s; std::memset(&s, 0, sizeof(s));
    if (inflateInit2(&s, 15 + 32) != Z_OK) return {};
    s.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
    s.avail_in = static_cast<uInt>(in.size());
    QByteArray out, buf; buf.resize(256 * 1024);
    int ret = Z_OK;
    do {
        s.next_out  = reinterpret_cast<Bytef*>(buf.data());
        s.avail_out = static_cast<uInt>(buf.size());
        ret = inflate(&s, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) { inflateEnd(&s); return {}; }
        out.append(buf.constData(), buf.size() - s.avail_out);
        if (ret == Z_BUF_ERROR && s.avail_in == 0) break;     // kein weiterer Input
    } while (ret != Z_STREAM_END);
    inflateEnd(&s);
    return out;
}

// 16-bit-Samples Big-Endian → Little-Endian (PDF-/Sound-Reihenfolge → WAV).
void byteswap16(QByteArray& b) {
    char* p = b.data(); const qsizetype n = b.size() - (b.size() & 1);
    for (qsizetype i = 0; i + 1 < n; i += 2) std::swap(p[i], p[i+1]);
}

// 44-Byte-RIFF/WAVE-PCM-Header (Little-Endian).
QByteArray wavHeader(qint64 dataLen, int channels, int rate, int bits) {
    QByteArray h;
    const auto u32 = [&](quint32 v){ h.append(char(v & 0xff)); h.append(char((v>>8) & 0xff)); h.append(char((v>>16) & 0xff)); h.append(char((v>>24) & 0xff)); };
    const auto u16 = [&](quint16 v){ h.append(char(v & 0xff)); h.append(char((v>>8) & 0xff)); };
    const quint32 byteRate   = quint32(rate) * quint32(channels) * quint32(bits / 8);
    const quint16 blockAlign = quint16(channels * (bits / 8));
    h.append("RIFF"); u32(quint32(36 + dataLen)); h.append("WAVE");
    h.append("fmt "); u32(16); u16(1); u16(quint16(channels)); u32(quint32(rate)); u32(byteRate); u16(blockAlign); u16(quint16(bits));
    h.append("data"); u32(quint32(dataLen));
    return h;
}

QByteArray readSlice(const QString& path, qsizetype start, qsizetype len) {
    if (len <= 0) return {};
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {};
    if (!f.seek(start)) { f.close(); return {}; }
    const QByteArray b = f.read(len); f.close(); return b;
}

QString writeTempWav(const QString& pdfPath, int id, const QByteArray& bytes) {
    const QString dir  = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString base = QFileInfo(pdfPath).completeBaseName();
    // Pfad-Hash gegen Kollisionen zwischen gleichnamigen PDFs verschiedener Ordner.
    const QString tag  = QString::number(qHash(pdfPath) & 0xffff, 16);
    const QString path = dir + QString("/mgaudio_%1_%2_%3.wav").arg(base, tag).arg(id);
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(bytes); f.close(); return path;
}

// ── Worker: Metadaten-Scan ────────────────────────────────────────────────────
class PdfAudioScanTask : public QRunnable {
public:
    PdfAudioScanTask(PdfAudioController* o, QString path, int gen)
        : m_owner(o), m_path(std::move(path)), m_gen(gen) { setAutoDelete(true); }
    void run() override {
        QVector<PdfAudioClip> clips;
        QFile f(m_path);
        if (f.open(QIODevice::ReadOnly)) { const QByteArray d = f.readAll(); f.close(); clips = scanClips(d); }
        PdfAudioController* owner = m_owner; const QString path = m_path; const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, path, clips, gen]() { owner->applyScan(path, clips, gen); }, Qt::QueuedConnection);
    }
private:
    PdfAudioController* m_owner; QString m_path; int m_gen;
};

// ── Worker: Einzel-Clip-Extraktion (inflate → swap → WAV) ─────────────────────
class PdfAudioExtractTask : public QRunnable {
public:
    PdfAudioExtractTask(PdfAudioController* o, QString path, PdfAudioClip c, int gen)
        : m_owner(o), m_path(std::move(path)), m_clip(c), m_gen(gen) { setAutoDelete(true); }
    void run() override {
        QString wav; int durMs = 0; qint64 bytes = 0;
        const QByteArray comp = readSlice(m_path, m_clip.streamStart, m_clip.streamLen);
        if (!comp.isEmpty()) {
            QByteArray pcm = m_clip.flate ? zlibInflate(comp) : comp;
            if (!pcm.isEmpty()) {
                if (m_clip.bits == 16) byteswap16(pcm);   // PDF /Sound = Big-Endian
                const int bps = m_clip.channels * (m_clip.bits / 8);
                if (bps > 0) durMs = int(double(pcm.size()) / double(qint64(m_clip.rate) * bps) * 1000.0 + 0.5);
                QByteArray file = wavHeader(pcm.size(), m_clip.channels, m_clip.rate, m_clip.bits);
                file.append(pcm);
                wav = writeTempWav(m_path, m_clip.id, file);
                bytes = file.size();
            }
        }
        PdfAudioController* owner = m_owner; const int id = m_clip.id; const int gen = m_gen;
        const QString w = wav; const int dm = durMs; const qint64 by = bytes;
        QMetaObject::invokeMethod(owner, [owner, id, w, dm, by, gen]() { owner->applyClip(id, w, dm, by, gen); }, Qt::QueuedConnection);
    }
private:
    PdfAudioController* m_owner; QString m_path; PdfAudioClip m_clip; int m_gen;
};

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
//  PdfAudioController — alle Member laufen auf dem GUI-Thread (keine Sync noetig).
// ══════════════════════════════════════════════════════════════════════════════
PdfAudioController::PdfAudioController(QObject* parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);   // Scan + Extraktion serialisieren (RAM/Disk-schonend)
}

PdfAudioController::~PdfAudioController() {
    m_pool.clear();
    m_pool.waitForDone();
    for (const WavEntry& e : std::as_const(m_wavCache)) QFile::remove(e.path);
}

void PdfAudioController::prepare(const QString& filePathOrUrl) {
    const QString path = mg::toLocalPath(filePathOrUrl);
    if (path == m_path && (m_scanInFlight || m_ready)) return;   // idempotent

    releaseDocument();                                           // gen++, leert Zustand, ready=false
    m_path = path;
    if (path.isEmpty() || !QFileInfo::exists(path)) { m_ready = true; emit readyChanged(); return; }

    m_scanInFlight = true;
    m_pool.start(new PdfAudioScanTask(this, path, m_gen));
}

QVariantList PdfAudioController::clips() const {
    QVariantList out; out.reserve(m_clips.size());
    for (const PdfAudioClip& c : m_clips) {
        QVariantMap m;
        m.insert("id",    c.id);
        m.insert("page",  c.page);
        m.insert("x",     c.rect.x());
        m.insert("y",     c.rect.y());
        m.insert("w",     c.rect.width());
        m.insert("h",     c.rect.height());
        m.insert("label", c.label);
        out.append(m);
    }
    return out;
}

void PdfAudioController::requestClip(int id) {
    if (id < 0 || id >= m_clips.size()) return;

    // Cache-Treffer → sofort (queued, damit QML einheitlich asynchron reagiert).
    if (m_wavCache.contains(id)) {
        m_wavOrder.removeAll(id); m_wavOrder.append(id);
        const WavEntry e = m_wavCache.value(id); const int gen = m_gen;
        QMetaObject::invokeMethod(this, [this, id, e, gen]() {
            if (gen != m_gen) return;
            emit clipReady(id, QUrl::fromLocalFile(e.path).toString(), e.durationMs);
        }, Qt::QueuedConnection);
        return;
    }
    if (m_clipInFlight.contains(id)) return;
    m_clipInFlight.insert(id);
    m_pool.start(new PdfAudioExtractTask(this, m_path, m_clips.at(id), m_gen));
}

void PdfAudioController::releaseDocument() {
    ++m_gen;                                  // laufende Tasks werden verworfen
    m_path.clear();
    m_clips.clear();
    for (const WavEntry& e : std::as_const(m_wavCache)) QFile::remove(e.path);
    m_wavCache.clear(); m_wavOrder.clear(); m_wavBytes = 0; m_clipInFlight.clear();
    const bool was = m_ready;
    m_ready = false; m_scanInFlight = false;
    if (was) emit readyChanged();
}

// ── Vom Worker via QueuedConnection (GUI-Thread) ──────────────────────────────
void PdfAudioController::applyScan(const QString& path, const QVector<PdfAudioClip>& clips, int gen) {
    if (gen != m_gen || path != m_path) return;   // veraltet / anderes Dokument
    m_clips = clips;
    m_scanInFlight = false;
    m_ready = true;
    emit readyChanged();
}

void PdfAudioController::applyClip(int id, const QString& wavPath, int durationMs, qint64 bytes, int gen) {
    if (gen != m_gen) { if (!wavPath.isEmpty()) QFile::remove(wavPath); return; }
    m_clipInFlight.remove(id);
    if (wavPath.isEmpty()) { emit clipReady(id, QString(), 0); return; }   // Extraktion fehlgeschlagen

    m_wavCache.insert(id, WavEntry{ wavPath, durationMs, bytes });
    m_wavOrder.removeAll(id); m_wavOrder.append(id);
    m_wavBytes += bytes;
    evictCache();
    emit clipReady(id, QUrl::fromLocalFile(wavPath).toString(), durationMs);
}

void PdfAudioController::evictCache() {
    while (m_wavBytes > kMaxWavBytes && m_wavOrder.size() > 1) {
        const int victim = m_wavOrder.takeFirst();
        auto it = m_wavCache.find(victim);
        if (it != m_wavCache.end()) { m_wavBytes -= it->bytes; QFile::remove(it->path); m_wavCache.erase(it); }
    }
}

QString PdfAudioController::tempPathFor(int id) const {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QString("/mgaudio_%1.wav").arg(id);
}
