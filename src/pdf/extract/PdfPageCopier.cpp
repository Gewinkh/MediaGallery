#include "pdf/extract/PdfPageCopier.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QHash>
#include <QSet>
#include <QIODevice>
#include <QtGlobal>

#include <zlib.h>
#include <cstring>

// ══════════════════════════════════════════════════════════════════════════════
//  Interne Helfer (Datei-lokal): Lexer, Objektmodell, Quelldokument, Plan.
//  Alles bewusst in einem anonymen Namespace — nach außen existiert nur der
//  PdfAssembler (Header). Kommentare erklären die PDF-Spezifika (ISO 32000).
// ══════════════════════════════════════════════════════════════════════════════
namespace {

// ── Zeichenklassen laut PDF-Spezifikation ────────────────────────────────────
inline bool isWs(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0';
}
inline bool isDelim(char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']'
        || c == '{' || c == '}' || c == '/' || c == '%';
}
inline bool isRegular(char c) { return !isWs(c) && !isDelim(c); }
inline bool isDigit(char c)   { return c >= '0' && c <= '9'; }

// ── zlib-Inflate (FlateDecode) ───────────────────────────────────────────────
//  Nur für XRef-/Objekt-Streams nötig (normale Streams werden verbatim
//  kopiert). Erst zlib-Header versuchen, bei kaputtem Header roh-Deflate
//  (windowBits −15) — manche Erzeuger schreiben fehlerhafte Header.
QByteArray zlibInflate(const char* src, qint64 len, bool* ok) {
    *ok = false;
    if (!src || len <= 0) return {};
    for (int attempt = 0; attempt < 2; ++attempt) {
        z_stream zs;
        std::memset(&zs, 0, sizeof(zs));
        if (inflateInit2(&zs, attempt == 0 ? 15 : -15) != Z_OK) continue;
        zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(src));
        zs.avail_in = static_cast<uInt>(qMin<qint64>(len, 0x7FFFFFFF));
        QByteArray out;
        char buf[1 << 15];
        int rc = Z_OK;
        while (rc == Z_OK) {
            zs.next_out  = reinterpret_cast<Bytef*>(buf);
            zs.avail_out = sizeof(buf);
            rc = inflate(&zs, Z_NO_FLUSH);
            const qint64 produced = sizeof(buf) - zs.avail_out;
            if (produced > 0) out.append(buf, produced);
            if (out.size() > (256LL << 20)) { rc = Z_MEM_ERROR; break; }  // Deckel 256 MB
        }
        inflateEnd(&zs);
        if (rc == Z_STREAM_END || (rc == Z_OK && !out.isEmpty())
            || (rc == Z_BUF_ERROR && !out.isEmpty())) {
            *ok = true;
            return out;
        }
    }
    return {};
}

// ── PNG-Prädiktor-Dekodierung (für XRef-Streams üblich: Predictor 12/„Up") ──
QByteArray applyPngPredictor(const QByteArray& in, int colors, int bpc, int columns,
                             bool* ok) {
    *ok = false;
    if (colors <= 0 || bpc <= 0 || columns <= 0) return {};
    const int rowLen = (colors * bpc * columns + 7) / 8;
    const int bpp    = qMax(1, (colors * bpc + 7) / 8);
    if (rowLen <= 0 || in.size() % (rowLen + 1) != 0) {
        // Zeilenraster passt nicht exakt → defensiv trotzdem zeilenweise lesen,
        // solange volle Zeilen vorhanden sind.
        if (in.size() < rowLen + 1) return {};
    }
    const int rows = in.size() / (rowLen + 1);
    QByteArray out(rows * rowLen, Qt::Uninitialized);
    QByteArray prior(rowLen, '\0');
    const uchar* p = reinterpret_cast<const uchar*>(in.constData());
    uchar* q = reinterpret_cast<uchar*>(out.data());
    for (int r = 0; r < rows; ++r) {
        const int ft = p[r * (rowLen + 1)];
        const uchar* row = p + r * (rowLen + 1) + 1;
        uchar* dst = q + r * rowLen;
        for (int i = 0; i < rowLen; ++i) {
            const int left  = (i >= bpp) ? dst[i - bpp] : 0;
            const int up    = static_cast<uchar>(prior[i]);
            const int ul    = (i >= bpp) ? static_cast<uchar>(prior[i - bpp]) : 0;
            int v = row[i];
            switch (ft) {
            case 0: break;                                   // None
            case 1: v += left; break;                        // Sub
            case 2: v += up; break;                          // Up
            case 3: v += (left + up) / 2; break;             // Average
            case 4: {                                        // Paeth
                const int pa = qAbs(up - ul), pb = qAbs(left - ul),
                          pc = qAbs(left + up - 2 * ul);
                v += (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : ul);
                break;
            }
            default: return {};                              // unbekannt → Fehler
            }
            dst[i] = static_cast<uchar>(v & 0xFF);
        }
        std::memcpy(prior.data(), dst, rowLen);
    }
    *ok = true;
    return out;
}

// ── Geparstes PDF-Objekt (Werte-Baum; Skalar-Rohtext bleibt verbatim) ────────
struct PObj {
    enum T { Null, Bool, Int, Real, Str, Name, Arr, Dict, Stream, Ref };
    T t = Null;

    qint64      i   = 0;   // Int-Wert bzw. Ref-Objektnummer
    QByteArray  raw;       // Skalar-Rohtext (Bool/Int/Real/Str/Name) — verbatim
    QList<PObj> arr;                              // Arr
    QList<QPair<QByteArray, PObj>> dict;          // Dict/Stream: (dekodierter Key, Wert)
    qint64      streamPos = -1;                   // Stream: Rohdaten-Offset in der Quelle
    qint64      streamLen = 0;                    //         und -Länge

    bool isNull() const { return t == Null; }
    const PObj* find(const char* key) const {
        for (const auto& kv : dict)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

// ── Lexer über einem Rohpuffer ───────────────────────────────────────────────
struct Lexer {
    const char* d = nullptr;
    qint64      n = 0;
    qint64      p = 0;

    bool eof() const { return p >= n; }
    char peek() const { return p < n ? d[p] : '\0'; }

    void skipWs() {
        while (p < n) {
            const char c = d[p];
            if (isWs(c)) { ++p; continue; }
            if (c == '%') {                       // Kommentar bis Zeilenende
                while (p < n && d[p] != '\n' && d[p] != '\r') ++p;
                continue;
            }
            break;
        }
    }
    // Liest ein reguläres Token (Keyword/Zahl) ab der aktuellen Position.
    QByteArray token() {
        skipWs();
        const qint64 s = p;
        while (p < n && isRegular(d[p])) ++p;
        return QByteArray(d + s, static_cast<int>(p - s));
    }
    bool expectKeyword(const char* kw) {
        const qint64 save = p;
        if (token() == kw) return true;
        p = save;
        return false;
    }
    bool readInt(qint64* out) {
        skipWs();
        const qint64 s = p;
        if (p < n && (d[p] == '+' || d[p] == '-')) ++p;
        while (p < n && isDigit(d[p])) ++p;
        if (p == s) return false;
        bool ok = false;
        *out = QByteArray(d + s, static_cast<int>(p - s)).toLongLong(&ok);
        return ok;
    }
};

// PDF-Name dekodieren (#xx-Escapes) — nur für den Key-VERGLEICH; der Rohtext
// bleibt für die Ausgabe erhalten.
QByteArray decodeName(const QByteArray& rawWithSlash) {
    QByteArray out;
    out.reserve(rawWithSlash.size());
    for (int i = 1; i < rawWithSlash.size(); ++i) {      // führenden '/' überspringen
        const char c = rawWithSlash.at(i);
        if (c == '#' && i + 2 < rawWithSlash.size()) {
            bool ok = false;
            const int v = rawWithSlash.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) { out.append(static_cast<char>(v)); i += 2; continue; }
        }
        out.append(c);
    }
    return out;
}

constexpr int kMaxParseDepth = 200;

// Rekursiver Objekt-Parser. Erkennt „N G R"-Referenzen per Lookahead.
bool parseValue(Lexer& lx, PObj* out, int depth) {
    if (depth > kMaxParseDepth) return false;
    lx.skipWs();
    if (lx.eof()) return false;
    const char c = lx.peek();

    if (c == '<') {
        if (lx.p + 1 < lx.n && lx.d[lx.p + 1] == '<') {          // Dictionary
            lx.p += 2;
            out->t = PObj::Dict;
            for (;;) {
                lx.skipWs();
                if (lx.p + 1 < lx.n && lx.d[lx.p] == '>' && lx.d[lx.p + 1] == '>') {
                    lx.p += 2;
                    return true;
                }
                if (lx.eof() || lx.peek() != '/') return false;
                PObj key;
                if (!parseValue(lx, &key, depth + 1) || key.t != PObj::Name)
                    return false;
                PObj val;
                if (!parseValue(lx, &val, depth + 1)) return false;
                out->dict.append({decodeName(key.raw), val});
            }
        }
        // Hex-String <...>
        const qint64 s = lx.p;
        ++lx.p;
        while (lx.p < lx.n && lx.d[lx.p] != '>') ++lx.p;
        if (lx.eof()) return false;
        ++lx.p;
        out->t = PObj::Str;
        out->raw = QByteArray(lx.d + s, static_cast<int>(lx.p - s));
        return true;
    }
    if (c == '(') {                                              // Literal-String
        const qint64 s = lx.p;
        int nest = 0;
        while (lx.p < lx.n) {
            const char ch = lx.d[lx.p];
            if (ch == '\\') { lx.p += 2; continue; }             // Escape überspringen
            if (ch == '(') ++nest;
            else if (ch == ')' && --nest == 0) { ++lx.p; break; }
            ++lx.p;
        }
        if (nest != 0) return false;
        out->t = PObj::Str;
        out->raw = QByteArray(lx.d + s, static_cast<int>(lx.p - s));
        return true;
    }
    if (c == '[') {                                              // Array
        ++lx.p;
        out->t = PObj::Arr;
        for (;;) {
            lx.skipWs();
            if (lx.eof()) return false;
            if (lx.peek() == ']') { ++lx.p; return true; }
            PObj v;
            if (!parseValue(lx, &v, depth + 1)) return false;
            out->arr.append(v);
        }
    }
    if (c == '/') {                                              // Name
        const qint64 s = lx.p;
        ++lx.p;
        while (lx.p < lx.n && isRegular(lx.d[lx.p])) ++lx.p;
        out->t = PObj::Name;
        out->raw = QByteArray(lx.d + s, static_cast<int>(lx.p - s));
        return true;
    }
    if (isDigit(c) || c == '+' || c == '-' || c == '.') {        // Zahl (evtl. Ref)
        const qint64 s = lx.p;
        bool real = false;
        while (lx.p < lx.n) {
            const char ch = lx.d[lx.p];
            if (isDigit(ch) || ch == '+' || ch == '-') { ++lx.p; continue; }
            if (ch == '.') { real = true; ++lx.p; continue; }
            break;
        }
        const QByteArray numRaw(lx.d + s, static_cast<int>(lx.p - s));
        if (!real && c != '+' && c != '-') {
            // Lookahead auf „G R" → indirekte Referenz (zerstörungsfrei).
            const qint64 save = lx.p;
            qint64 gen = 0;
            if (lx.readInt(&gen)) {
                lx.skipWs();
                if (lx.p < lx.n && lx.d[lx.p] == 'R'
                    && (lx.p + 1 >= lx.n || !isRegular(lx.d[lx.p + 1]))) {
                    ++lx.p;
                    out->t = PObj::Ref;
                    out->i = numRaw.toLongLong();
                    return true;
                }
            }
            lx.p = save;
        }
        out->t = real ? PObj::Real : PObj::Int;
        out->raw = numRaw;
        if (!real) out->i = numRaw.toLongLong();
        return true;
    }
    // Keywords
    {
        const QByteArray kw = lx.token();
        if (kw == "true" || kw == "false") { out->t = PObj::Bool; out->raw = kw; return true; }
        if (kw == "null") { out->t = PObj::Null; out->raw = kw; return true; }
        return false;   // unbekanntes Token auf Werteebene
    }
}

// ── XRef-Eintrag ─────────────────────────────────────────────────────────────
struct XEntry {
    int    type = 0;    // 1 = Offset in Datei · 2 = in Objekt-Stream
    qint64 a    = 0;    // type1: Byte-Offset · type2: Objektnummer des ObjStm
    int    b    = 0;    // type1: Generation  · type2: Index im ObjStm
};

// ─────────────────────────────────────────────────────────────────────────────
//  SourceDoc — EIN Quell-PDF: mmap, XRef-Kette, Objektauflösung, Seitenbaum.
// ─────────────────────────────────────────────────────────────────────────────
class SourceDoc {
public:
    ~SourceDoc() { close(); }

    bool open(const QString& path, QString* err);
    void close();

    bool encrypted() const { return m_trailer.find("Encrypt") != nullptr; }

    // Objekt auflösen (Cache; type1 direkt, type2 aus Objekt-Stream). Liefert
    // nullptr bei STRUKTURELLEM Fehler; ein fehlender XRef-Eintrag ist laut
    // Spezifikation dagegen ein legitimes `null` (→ *legalNull = true).
    const PObj* getObject(qint64 num, bool* legalNull);

    // Seitenbaum abflachen: Objektnummern + materialisierte Vererbung.
    struct PageInfo {
        qint64 objNum = 0;
        PObj   dict;                 // Original-Seiten-Dictionary
        PObj   inhResources, inhMediaBox, inhCropBox, inhRotate;   // ggf. Null
    };
    bool flattenPages(QVector<PageInfo>* out, QString* err);

    const char* data() const { return m_data; }
    qint64      size() const { return m_size; }
    int declaredPageCount();     // /Root→/Pages→/Count (−1 wenn nicht lesbar)

private:
    bool parseXrefChain(QString* err);
    bool parseXrefAt(qint64 offset, QSet<qint64>* visited, QString* err);
    void mergeTrailer(const PObj& dict);
    void insertEntry(qint64 num, const XEntry& e);
    bool parseIndirectAt(qint64 offset, qint64* numOut, PObj* out);
    bool decodeStream(const PObj& streamObj, QByteArray* out);
    bool ensureObjStm(qint64 stmNum);
    bool reconstructByScan(QString* err);
    void indexObjStmsByScan();
    const PObj* resolveInline(const PObj* v, bool* legalNull);

    QFile        m_file;
    const char*  m_data = nullptr;
    qint64       m_size = 0;
    QByteArray   m_heapCopy;                       // Fallback, wenn map() scheitert

    QHash<qint64, XEntry> m_xref;
    PObj                  m_trailer;               // zusammengeführte Trailer-Keys
    QHash<qint64, PObj>   m_cache;                 // Objekt-Cache
    QSet<qint64>          m_resolving;             // Zyklus-Schutz
    QHash<qint64, QPair<QByteArray, QVector<QPair<qint64, qint64>>>> m_objStms;
    bool m_scannedObjStms = false;
    bool m_recovered      = false;
};

bool SourceDoc::open(const QString& path, QString* err) {
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("open");
        return false;
    }
    m_size = m_file.size();
    if (m_size < 32) {
        if (err) *err = QStringLiteral("size");
        return false;
    }
    // RAM-Priorität: Quelldatei mappen statt laden (OS-gepagt, portabel).
    uchar* mapped = m_file.map(0, m_size);
    if (mapped) {
        m_data = reinterpret_cast<const char*>(mapped);
    } else {
        m_heapCopy = m_file.readAll();             // seltener Fallback (FS ohne mmap)
        if (m_heapCopy.size() != m_size) {
            if (err) *err = QStringLiteral("read");
            return false;
        }
        m_data = m_heapCopy.constData();
    }
    return parseXrefChain(err);
}

void SourceDoc::close() {
    m_data = nullptr;
    m_size = 0;
    m_heapCopy.clear();
    if (m_file.isOpen()) m_file.close();           // unmapped implizit beim Schließen
    m_xref.clear();
    m_cache.clear();
    m_objStms.clear();
    m_trailer = PObj();
}

void SourceDoc::insertEntry(qint64 num, const XEntry& e) {
    // Erste Sichtung gewinnt (jüngste XRef zuerst gelesen). Freie Einträge
    // werden gar nicht erst eingetragen — so füllt bei Hybrid-Dateien der
    // /XRefStm die im klassischen Teil als „frei" markierten ObjStm-Objekte.
    if (num <= 0 || m_xref.contains(num)) return;
    m_xref.insert(num, e);
}

void SourceDoc::mergeTrailer(const PObj& dict) {
    static const char* keep[] = {"Root", "Info", "Encrypt", "Size"};
    for (const char* k : keep) {
        if (m_trailer.find(k)) continue;
        if (const PObj* v = dict.find(k))
            m_trailer.dict.append({QByteArray(k), *v});
    }
}

bool SourceDoc::parseXrefChain(QString* err) {
    // `startxref` steht am Dateiende; defensiv in den letzten 64 KB suchen.
    const qint64 tail = qMin<qint64>(m_size, 64 * 1024);
    const QByteArray tailBytes = QByteArray::fromRawData(m_data + m_size - tail,
                                                         static_cast<int>(tail));
    const int sx = tailBytes.lastIndexOf("startxref");
    bool chainOk = false;
    if (sx >= 0) {
        Lexer lx{m_data, m_size, m_size - tail + sx + 9};
        qint64 off = -1;
        if (lx.readInt(&off) && off >= 0 && off < m_size) {
            QSet<qint64> visited;
            QString localErr;
            chainOk = parseXrefAt(off, &visited, &localErr);
        }
    }
    if (!chainOk || !m_trailer.find("Root")) {
        // Recovery: Objekttabelle per Brute-Scan rekonstruieren (Muster wie
        // der Roh-Scan des PdfAudioControllers, hier zusätzlich mit
        // /Type/Catalog-Suche und nachträglicher ObjStm-Indizierung).
        if (!reconstructByScan(err)) return false;
    }
    if (encrypted()) {
        if (err) *err = QStringLiteral("encrypted");
        return false;
    }
    if (!m_trailer.find("Root")) {
        if (err) *err = QStringLiteral("root");
        return false;
    }
    return true;
}

bool SourceDoc::parseXrefAt(qint64 offset, QSet<qint64>* visited, QString* err) {
    if (visited->contains(offset)) return true;    // Zyklus → still beenden
    visited->insert(offset);
    if (visited->size() > 64) return false;        // Ketten-Deckel

    Lexer lx{m_data, m_size, offset};
    lx.skipWs();
    if (lx.expectKeyword("xref")) {
        // ── Klassische Tabelle ────────────────────────────────────────────
        for (;;) {
            lx.skipWs();
            if (lx.expectKeyword("trailer")) break;
            qint64 start = 0, count = 0;
            if (!lx.readInt(&start) || !lx.readInt(&count) || count < 0
                || count > 5'000'000)
                return false;
            for (qint64 k = 0; k < count; ++k) {
                qint64 a = 0, b = 0;
                if (!lx.readInt(&a) || !lx.readInt(&b)) return false;
                lx.skipWs();
                if (lx.eof()) return false;
                const char kind = lx.d[lx.p++];
                if (kind == 'n')
                    insertEntry(start + k, {1, a, static_cast<int>(b)});
                else if (kind != 'f')
                    return false;
            }
        }
        PObj tdict;
        if (!parseValue(lx, &tdict, 0) || tdict.t != PObj::Dict) return false;
        // Hybrid-Datei: /XRefStm VOR /Prev verarbeiten (Spez. 7.5.8.4).
        if (const PObj* xs = tdict.find("XRefStm"); xs && xs->t == PObj::Int)
            parseXrefAt(xs->i, visited, err);      // best effort
        mergeTrailer(tdict);
        if (const PObj* prev = tdict.find("Prev"); prev && prev->t == PObj::Int)
            return parseXrefAt(prev->i, visited, err);
        return true;
    }

    // ── XRef-STREAM (PDF 1.5+) ────────────────────────────────────────────
    qint64 num = 0;
    PObj obj;
    if (!parseIndirectAt(offset, &num, &obj) || obj.t != PObj::Stream)
        return false;
    const PObj* w = obj.find("W");
    if (!w || w->t != PObj::Arr || w->arr.size() < 3) return false;
    const qint64 w1 = w->arr[0].i, w2 = w->arr[1].i, w3 = w->arr[2].i;
    if (w1 < 0 || w2 <= 0 || w3 < 0 || w1 + w2 + w3 <= 0 || w1 + w2 + w3 > 32)
        return false;

    QByteArray data;
    if (!decodeStream(obj, &data)) return false;

    // /Index (Standard: [0 /Size]) → (start,count)-Paare.
    QVector<QPair<qint64, qint64>> ranges;
    if (const PObj* idx = obj.find("Index"); idx && idx->t == PObj::Arr) {
        for (int k = 0; k + 1 < idx->arr.size(); k += 2)
            ranges.append({idx->arr[k].i, idx->arr[k + 1].i});
    } else {
        const PObj* sz = obj.find("Size");
        if (!sz || sz->t != PObj::Int) return false;
        ranges.append({0, sz->i});
    }

    const int rec = static_cast<int>(w1 + w2 + w3);
    qint64 pos = 0;
    auto beField = [&](qint64 width) -> qint64 {
        qint64 v = 0;
        for (qint64 k = 0; k < width; ++k)
            v = (v << 8) | static_cast<uchar>(data.at(static_cast<int>(pos + k)));
        pos += width;
        return v;
    };
    for (const auto& r : ranges) {
        for (qint64 k = 0; k < r.second; ++k) {
            if (pos + rec > data.size()) break;                // defensiv
            const qint64 type = (w1 == 0) ? 1 : beField(w1);   // w1=0 → Typ 1
            const qint64 f2   = beField(w2);
            const qint64 f3   = (w3 > 0) ? beField(w3) : 0;
            const qint64 onum = r.first + k;
            if (type == 1)      insertEntry(onum, {1, f2, static_cast<int>(f3)});
            else if (type == 2) insertEntry(onum, {2, f2, static_cast<int>(f3)});
            // Typ 0 (frei) und unbekannte Typen: überspringen.
        }
    }
    mergeTrailer(obj);
    if (const PObj* prev = obj.find("Prev"); prev && prev->t == PObj::Int)
        return parseXrefAt(prev->i, visited, err);
    return true;
}

// „N G obj … endobj" an einem Datei-Offset parsen (inkl. Stream-Erkennung).
bool SourceDoc::parseIndirectAt(qint64 offset, qint64* numOut, PObj* out) {
    if (offset < 0 || offset >= m_size) return false;
    Lexer lx{m_data, m_size, offset};
    qint64 num = 0, gen = 0;
    if (!lx.readInt(&num) || !lx.readInt(&gen)) return false;
    if (!lx.expectKeyword("obj")) return false;
    if (!parseValue(lx, out, 0)) return false;
    *numOut = num;

    lx.skipWs();
    if (out->t == PObj::Dict && lx.expectKeyword("stream")) {
        // Stream-Daten beginnen nach CRLF oder LF direkt hinter dem Keyword.
        if (lx.p < lx.n && lx.d[lx.p] == '\r') ++lx.p;
        if (lx.p < lx.n && lx.d[lx.p] == '\n') ++lx.p;
        out->t = PObj::Stream;
        out->streamPos = lx.p;

        // /Length auflösen (darf indirekt sein).
        qint64 len = -1;
        if (const PObj* L = out->find("Length")) {
            if (L->t == PObj::Int) {
                len = L->i;
            } else if (L->t == PObj::Ref) {
                bool legalNull = false;
                if (const PObj* lo = getObject(L->i, &legalNull))
                    if (lo->t == PObj::Int) len = lo->i;
            }
        }
        // Validierung: hinter den Daten muss `endstream` folgen — sonst
        // Recovery über die Suche nach dem Keyword (defekte /Length-Angaben).
        auto endstreamAt = [&](qint64 dataEnd) -> bool {
            Lexer probe{m_data, m_size, dataEnd};
            probe.skipWs();
            return probe.expectKeyword("endstream");
        };
        if (len >= 0 && out->streamPos + len <= m_size
            && endstreamAt(out->streamPos + len)) {
            out->streamLen = len;
        } else {
            const QByteArray hay = QByteArray::fromRawData(
                m_data + out->streamPos,
                static_cast<int>(qMin<qint64>(m_size - out->streamPos,
                                              1'500'000'000)));
            const int idx = hay.indexOf("endstream");
            if (idx < 0) return false;
            qint64 realLen = idx;
            // EOL unmittelbar vor `endstream` zählt nicht zu den Daten.
            while (realLen > 0 && (m_data[out->streamPos + realLen - 1] == '\n'
                                   || m_data[out->streamPos + realLen - 1] == '\r'))
                --realLen;
            out->streamLen = realLen;
        }
    }
    return true;
}

bool SourceDoc::decodeStream(const PObj& streamObj, QByteArray* out) {
    // Nur für XRef-/Objekt-Streams: FlateDecode (+ optionale PNG-Prädiktoren)
    // oder ungefiltert. Alles andere → Fehler (Aufrufer fällt zurück).
    if (streamObj.t != PObj::Stream || streamObj.streamPos < 0) return false;
    const char*  src = m_data + streamObj.streamPos;
    const qint64 len = streamObj.streamLen;

    const PObj* filter = streamObj.find("Filter");
    if (!filter || filter->t == PObj::Null) {
        *out = QByteArray(src, static_cast<int>(len));
        return true;
    }
    QByteArray fname;
    if (filter->t == PObj::Name) fname = decodeName(filter->raw);
    else if (filter->t == PObj::Arr && filter->arr.size() == 1
             && filter->arr[0].t == PObj::Name)
        fname = decodeName(filter->arr[0].raw);
    if (fname != "FlateDecode" && fname != "Fl") return false;

    bool ok = false;
    QByteArray inflated = zlibInflate(src, len, &ok);
    if (!ok) return false;

    const PObj* parms = streamObj.find("DecodeParms");
    if (!parms) parms = streamObj.find("DP");
    if (parms && parms->t == PObj::Arr && parms->arr.size() == 1)
        parms = &parms->arr[0];
    if (parms && parms->t == PObj::Dict) {
        auto intOf = [&](const char* k, qint64 def) {
            const PObj* v = parms->find(k);
            return (v && v->t == PObj::Int) ? v->i : def;
        };
        const qint64 pred = intOf("Predictor", 1);
        if (pred >= 10) {
            inflated = applyPngPredictor(inflated,
                                         static_cast<int>(intOf("Colors", 1)),
                                         static_cast<int>(intOf("BitsPerComponent", 8)),
                                         static_cast<int>(intOf("Columns", 1)), &ok);
            if (!ok) return false;
        } else if (pred != 1) {
            return false;                                   // TIFF-Prädiktor: unüblich
        }
    }
    *out = inflated;
    return true;
}

bool SourceDoc::ensureObjStm(qint64 stmNum) {
    if (m_objStms.contains(stmNum)) return true;
    const auto it = m_xref.constFind(stmNum);
    if (it == m_xref.constEnd() || it->type != 1) return false;
    qint64 n = 0;
    PObj stm;
    if (!parseIndirectAt(it->a, &n, &stm) || stm.t != PObj::Stream) return false;
    const PObj* N = stm.find("N");
    const PObj* First = stm.find("First");
    if (!N || N->t != PObj::Int || !First || First->t != PObj::Int) return false;

    QByteArray data;
    if (!decodeStream(stm, &data)) return false;

    // Kopf: N Paare „objnum offset" (Offsets relativ zu /First).
    QVector<QPair<qint64, qint64>> pairs;
    Lexer hl{data.constData(), data.size(), 0};
    for (qint64 k = 0; k < N->i && k < 100'000; ++k) {
        qint64 on = 0, off = 0;
        if (!hl.readInt(&on) || !hl.readInt(&off)) return false;
        pairs.append({on, First->i + off});
    }
    m_objStms.insert(stmNum, {data, pairs});
    return true;
}

const PObj* SourceDoc::getObject(qint64 num, bool* legalNull) {
    if (legalNull) *legalNull = false;
    const auto cit = m_cache.constFind(num);
    if (cit != m_cache.constEnd()) return &cit.value();

    const auto it = m_xref.constFind(num);
    if (it == m_xref.constEnd()) {
        // Recovery-Nachschlag: Objekt könnte in einem noch nicht indizierten
        // Objekt-Stream liegen (nur relevant nach Brute-Scan-Rekonstruktion).
        if (m_recovered && !m_scannedObjStms) {
            indexObjStmsByScan();
            return getObject(num, legalNull);
        }
        // Referenz auf nicht existierendes Objekt ist laut Spez. `null`.
        if (legalNull) *legalNull = true;
        return nullptr;
    }
    if (m_resolving.contains(num)) return nullptr;   // Zyklus (defekt)
    m_resolving.insert(num);
    const auto guard = qScopeGuard([&] { m_resolving.remove(num); });

    PObj obj;
    if (it->type == 1) {
        qint64 parsedNum = 0;
        if (!parseIndirectAt(it->a, &parsedNum, &obj) || parsedNum != num)
            return nullptr;
    } else {                                          // type 2: Objekt-Stream
        if (!ensureObjStm(it->a)) return nullptr;
        const auto& stm = m_objStms[it->a];
        qint64 off = -1;
        if (it->b >= 0 && it->b < stm.second.size()
            && stm.second[it->b].first == num) {
            off = stm.second[it->b].second;
        } else {
            for (const auto& p : stm.second)          // Index defekt → Suche
                if (p.first == num) { off = p.second; break; }
        }
        if (off < 0 || off >= stm.first.size()) return nullptr;
        Lexer lx{stm.first.constData(), stm.first.size(), off};
        if (!parseValue(lx, &obj, 0)) return nullptr;
    }
    return &m_cache.insert(num, obj).value();
}

bool SourceDoc::reconstructByScan(QString* err) {
    // Brute-Scan: alle „N G obj"-Vorkommen; das LETZTE gewinnt (jüngster
    // Inkrement-Save). Danach Trailer-/Katalog-Suche.
    m_xref.clear();
    m_recovered = true;
    const QByteArray hay = QByteArray::fromRawData(m_data,
        static_cast<int>(qMin<qint64>(m_size, 2'000'000'000)));
    int at = 0;
    QHash<qint64, XEntry> found;
    for (;;) {
        at = hay.indexOf("obj", at);
        if (at < 0) break;
        const int kwPos = at;
        at += 3;
        if (kwPos + 3 < hay.size() && isRegular(hay.at(kwPos + 3))) continue;
        // Rückwärts: WS, Generationszahl, WS, Objektnummer.
        int p = kwPos - 1;
        while (p >= 0 && isWs(hay.at(p))) --p;
        const int genEnd = p;
        while (p >= 0 && isDigit(hay.at(p))) --p;
        if (p == genEnd) continue;
        int q = p;
        while (q >= 0 && isWs(hay.at(q))) --q;
        const int numEnd = q;
        while (q >= 0 && isDigit(hay.at(q))) --q;
        if (q == numEnd) continue;
        if (q >= 0 && isRegular(hay.at(q))) continue;      // kein Tokenanfang
        bool ok = false;
        const qint64 num = hay.mid(q + 1, numEnd - q).toLongLong(&ok);
        if (!ok || num <= 0) continue;
        found.insert(num, {1, static_cast<qint64>(q + 1), 0});   // letztes gewinnt
    }
    if (found.isEmpty()) {
        if (err) *err = QStringLiteral("scan");
        return false;
    }
    m_xref = found;

    // /Root aus dem letzten Trailer …
    if (!m_trailer.find("Root")) {
        int tpos = hay.lastIndexOf("trailer");
        while (tpos >= 0 && !m_trailer.find("Root")) {
            Lexer lx{m_data, m_size, tpos + 7};
            PObj tdict;
            if (parseValue(lx, &tdict, 0) && tdict.t == PObj::Dict)
                mergeTrailer(tdict);
            tpos = (tpos > 0) ? hay.lastIndexOf("trailer", tpos - 1) : -1;
        }
    }
    // … oder notfalls über die /Type/Catalog-Suche.
    if (!m_trailer.find("Root")) {
        for (auto it = m_xref.constBegin(); it != m_xref.constEnd(); ++it) {
            bool legalNull = false;
            const PObj* o = getObject(it.key(), &legalNull);
            if (!o || o->t != PObj::Dict) continue;
            const PObj* ty = o->find("Type");
            if (ty && ty->t == PObj::Name && decodeName(ty->raw) == "Catalog") {
                PObj ref;
                ref.t = PObj::Ref;
                ref.i = it.key();
                m_trailer.dict.append({QByteArray("Root"), ref});
                break;
            }
        }
    }
    return true;
}

void SourceDoc::indexObjStmsByScan() {
    m_scannedObjStms = true;
    const auto keys = m_xref.keys();
    for (qint64 num : keys) {
        const XEntry e = m_xref.value(num);
        if (e.type != 1) continue;
        qint64 n = 0;
        PObj o;
        if (!parseIndirectAt(e.a, &n, &o) || o.t != PObj::Stream) continue;
        const PObj* ty = o.find("Type");
        if (!ty || ty->t != PObj::Name || decodeName(ty->raw) != "ObjStm") continue;
        if (!ensureObjStm(num)) continue;
        const auto& stm = m_objStms[num];
        for (int k = 0; k < stm.second.size(); ++k)
            insertEntry(stm.second[k].first, {2, num, k});
    }
}

const PObj* SourceDoc::resolveInline(const PObj* v, bool* legalNull) {
    if (!v) return nullptr;
    if (v->t != PObj::Ref) return v;
    return getObject(v->i, legalNull);
}

int SourceDoc::declaredPageCount() {
    bool ln = false;
    const PObj* root = resolveInline(m_trailer.find("Root"), &ln);
    if (!root || root->t != PObj::Dict) return -1;
    const PObj* pages = resolveInline(root->find("Pages"), &ln);
    if (!pages || (pages->t != PObj::Dict && pages->t != PObj::Stream)) return -1;
    const PObj* cnt = resolveInline(pages->find("Count"), &ln);
    if (!cnt || cnt->t != PObj::Int || cnt->i < 0 || cnt->i > 1'000'000) return -1;
    return static_cast<int>(cnt->i);
}

bool SourceDoc::flattenPages(QVector<PageInfo>* out, QString* err) {
    bool ln = false;
    const PObj* root = resolveInline(m_trailer.find("Root"), &ln);
    if (!root || root->t != PObj::Dict) {
        if (err) *err = QStringLiteral("catalog");
        return false;
    }
    const PObj* pagesRef = root->find("Pages");
    if (!pagesRef || pagesRef->t != PObj::Ref) {
        if (err) *err = QStringLiteral("pages");
        return false;
    }

    // Iterativ (expliziter Stapel) statt rekursiv — tiefe/kaputte Bäume sicher.
    struct Frame {
        qint64 objNum;
        PObj inhRes, inhMedia, inhCrop, inhRot;
    };
    QVector<Frame> stack;
    stack.append({pagesRef->i, PObj(), PObj(), PObj(), PObj()});
    QSet<qint64> visited;

    while (!stack.isEmpty()) {
        const Frame fr = stack.takeLast();
        if (visited.contains(fr.objNum)) continue;         // Zyklus-Schutz
        visited.insert(fr.objNum);
        if (out->size() > 100'000 || visited.size() > 500'000) {
            if (err) *err = QStringLiteral("treesize");
            return false;
        }
        const PObj* node = getObject(fr.objNum, &ln);
        if (!node || node->t != PObj::Dict) {
            if (err) *err = QStringLiteral("node");
            return false;
        }
        // Vererbbare Attribute dieses Knotens übernehmen (Kind schlägt Eltern).
        Frame nx = fr;
        if (const PObj* v = node->find("Resources")) nx.inhRes   = *v;
        if (const PObj* v = node->find("MediaBox"))  nx.inhMedia = *v;
        if (const PObj* v = node->find("CropBox"))   nx.inhCrop  = *v;
        if (const PObj* v = node->find("Rotate"))    nx.inhRot   = *v;

        const PObj* kids = node->find("Kids");
        const PObj* type = node->find("Type");
        const bool isPage = (type && type->t == PObj::Name
                             && decodeName(type->raw) == "Page")
                            || (!kids && (!type || type->t != PObj::Name
                                          || decodeName(type->raw) != "Pages"));
        if (kids && kids->t == PObj::Arr && !isPage) {
            // Kinder in UMGEKEHRTER Reihenfolge stapeln → Dokumentreihenfolge.
            for (int k = kids->arr.size() - 1; k >= 0; --k)
                if (kids->arr[k].t == PObj::Ref)
                    stack.append({kids->arr[k].i, nx.inhRes, nx.inhMedia,
                                  nx.inhCrop, nx.inhRot});
            continue;
        }
        PageInfo pi;
        pi.objNum       = fr.objNum;
        pi.dict         = *node;
        pi.inhResources = nx.inhRes;
        pi.inhMediaBox  = nx.inhMedia;
        pi.inhCropBox   = nx.inhCrop;
        pi.inhRotate    = nx.inhRot;
        out->append(pi);
    }
    if (out->isEmpty()) {
        if (err) *err = QStringLiteral("nopages");
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CopyPlan — plant die verlustfreie Übernahme EINER Quelle vollständig im
//  Speicher (Segmente), damit ein Fehlschlag die Ausgabe nicht fragmentiert.
//  Stream-ROHDATEN bleiben als (Offset,Länge)-Spans auf dem Quell-Mapping —
//  kein Byte-Kopieren großer Inhalte vor dem eigentlichen Schreiben.
// ─────────────────────────────────────────────────────────────────────────────
struct Segment {
    QByteArray bytes;          // ODER …
    qint64     srcOff = -1;    // … Span in der Quelldatei
    qint64     srcLen = 0;
};

struct PlannedObject {
    int               newNum = 0;
    QVector<Segment>  segs;
    bool              isPage = false;
};

class CopyPlan {
public:
    CopyPlan(SourceDoc* doc, int firstNewNum) : m_doc(doc), m_next(firstNewNum) {}

    //  `rotations` ist entweder leer (keine zusätzliche Drehung) oder parallel
    //  zu `pageIndices`: Gradzahl, die ZUSÄTZLICH zur Eigendrehung der Quellseite
    //  wirkt (Vielfaches von 90).
    bool plan(const QVector<int>& pageIndices, const QVector<int>& rotations,
              QString* err);

    const QVector<PlannedObject>& objects() const { return m_objects; }
    int nextNum() const { return m_next; }
    const QVector<int>& pageNewNums() const { return m_pageNewNums; }

private:
    int  mapNum(qint64 srcNum);
    bool serializeValue(const PObj& v, QByteArray* out, int depth);
    bool planObject(qint64 srcNum, int newNum);
    bool planPage(const SourceDoc::PageInfo& pi, int newNum, int rotDelta);

    SourceDoc*             m_doc;
    int                    m_next;
    QHash<qint64, int>     m_map;          // Quell-Objnr. → neue Objnr.
    QSet<qint64>           m_allPageNums;  // ALLE Seiten der Quelle (Kappen)
    QVector<qint64>        m_queue;        // noch zu planende Quell-Objekte
    QVector<PlannedObject> m_objects;
    QVector<int>           m_pageNewNums;
};

int CopyPlan::mapNum(qint64 srcNum) {
    const auto it = m_map.constFind(srcNum);
    if (it != m_map.constEnd()) return it.value();
    const int nn = m_next++;
    m_map.insert(srcNum, nn);
    m_queue.append(srcNum);
    return nn;
}

bool CopyPlan::serializeValue(const PObj& v, QByteArray* out, int depth) {
    if (depth > kMaxParseDepth) return false;
    switch (v.t) {
    case PObj::Null:  out->append("null");  return true;
    case PObj::Bool:
    case PObj::Int:
    case PObj::Real:
    case PObj::Str:
    case PObj::Name:  out->append(v.raw);   return true;
    case PObj::Ref: {
        if (m_allPageNums.contains(v.i)) {
            // Referenz auf eine Seite: gewählte Seiten werden umgemappt (z. B.
            // /P einer Annotation, Link-Ziel innerhalb der Auswahl), NICHT
            // gewählte werden zu `null` gekappt (verhindert, dass der
            // Graph-Abschluss das restliche Dokument mitzieht).
            const auto it = m_map.constFind(v.i);
            if (it == m_map.constEnd()) { out->append("null"); return true; }
            out->append(QByteArray::number(it.value()));
            out->append(" 0 R");
            return true;
        }
        out->append(QByteArray::number(mapNum(v.i)));
        out->append(" 0 R");
        return true;
    }
    case PObj::Arr: {
        out->append('[');
        for (int k = 0; k < v.arr.size(); ++k) {
            if (k) out->append(' ');
            if (!serializeValue(v.arr[k], out, depth + 1)) return false;
        }
        out->append(']');
        return true;
    }
    case PObj::Dict:
    case PObj::Stream: {
        out->append("<< ");
        for (const auto& kv : v.dict) {
            if (v.t == PObj::Stream && kv.first == "Length")
                continue;                        // wird als Literal neu geschrieben
            out->append('/');
            // Key-Rohtext ist dekodiert gespeichert → re-escapen, falls nötig.
            for (const char c : kv.first) {
                if (isRegular(c) && c != '#' && static_cast<uchar>(c) > 0x20) {
                    out->append(c);
                } else {
                    out->append('#');
                    const char hex[] = "0123456789ABCDEF";
                    out->append(hex[(static_cast<uchar>(c) >> 4) & 0xF]);
                    out->append(hex[static_cast<uchar>(c) & 0xF]);
                }
            }
            out->append(' ');
            if (!serializeValue(kv.second, out, depth + 1)) return false;
            out->append(' ');
        }
        if (v.t == PObj::Stream) {
            out->append("/Length ");
            out->append(QByteArray::number(v.streamLen));
            out->append(' ');
        }
        out->append(">>");
        return true;
    }
    }
    return false;
}

bool CopyPlan::planObject(qint64 srcNum, int newNum) {
    bool legalNull = false;
    const PObj* obj = m_doc->getObject(srcNum, &legalNull);
    if (!obj) {
        if (!legalNull) return false;            // struktureller Fehler → Abbruch
        // Referenz auf nicht existierendes Objekt = laut Spezifikation `null`.
        PlannedObject po;
        po.newNum = newNum;
        po.segs.append({QByteArray("null"), -1, 0});
        m_objects.append(po);
        return true;
    }
    PlannedObject po;
    po.newNum = newNum;
    QByteArray head;
    if (!serializeValue(*obj, &head, 0)) return false;
    if (obj->t == PObj::Stream) {
        head.append("\nstream\n");
        po.segs.append({head, -1, 0});
        po.segs.append({QByteArray(), obj->streamPos, obj->streamLen});
        po.segs.append({QByteArray("\nendstream"), -1, 0});
    } else {
        po.segs.append({head, -1, 0});
    }
    m_objects.append(po);
    return true;
}

bool CopyPlan::planPage(const SourceDoc::PageInfo& pi, int newNum, int rotDelta) {
    // Zusätzliche Drehung (Seite drehen im Editor): Die Eigendrehung der
    // Quellseite — eigenes /Rotate oder ein vom Seitenbaum geerbtes — wird um
    // `rotDelta` weitergedreht und als EIN materialisierter Wert geschrieben.
    // Das Original bleibt dabei unangetastet (es wird nur kopiert), und der
    // Seiteninhalt selbst bleibt byteweise erhalten.
    rotDelta = ((rotDelta % 360) + 360) % 360;
    int rotOut = -1;                                 // −1 = keine Drehung schreiben
    if (rotDelta != 0) {
        int base = 0;
        const PObj* own = pi.dict.find("Rotate");
        if (own && own->t == PObj::Int)              base = static_cast<int>(own->i);
        else if (pi.inhRotate.t == PObj::Int)        base = static_cast<int>(pi.inhRotate.i);
        rotOut = (((base + rotDelta) % 360) + 360) % 360;
        rotOut = (rotOut / 90) * 90;                 // Vielfaches von 90 sicherstellen
    }

    // Seiten-Dictionary neu zusammensetzen: /Parent zeigt auf den NEUEN
    // Seitenbaum (Objekt 2); vererbte Attribute werden materialisiert;
    // /B (Artikel-Fäden) entfällt (zöge fremde Seitenketten in den Abschluss).
    QByteArray head("<< /Parent 2 0 R ");
    QSet<QByteArray> present;
    if (rotOut >= 0) {
        head.append("/Rotate ");
        head.append(QByteArray::number(rotOut));
        head.append(' ');
        present.insert("Rotate");                    // eigenes /Rotate ersetzt
    }
    for (const auto& kv : pi.dict.dict) {
        if (kv.first == "Parent" || kv.first == "B") continue;
        if (rotOut >= 0 && kv.first == "Rotate") continue;   // s. o.
        present.insert(kv.first);
        head.append('/');
        head.append(kv.first);
        head.append(' ');
        if (!serializeValue(kv.second, &head, 0)) return false;
        head.append(' ');
    }
    auto addInherited = [&](const char* key, const PObj& v) -> bool {
        if (v.isNull() || present.contains(key)) return true;
        head.append('/');
        head.append(key);
        head.append(' ');
        if (!serializeValue(v, &head, 0)) return false;
        head.append(' ');
        return true;
    };
    if (!addInherited("Resources", pi.inhResources)) return false;
    if (!addInherited("MediaBox",  pi.inhMediaBox))  return false;
    if (!addInherited("CropBox",   pi.inhCropBox))   return false;
    if (!addInherited("Rotate",    pi.inhRotate))    return false;
    if (!present.contains("MediaBox") && pi.inhMediaBox.isNull())
        head.append("/MediaBox [0 0 612 792] ");     // Pflichtattribut absichern
    if (!present.contains("Type"))
        head.append("/Type /Page ");
    head.append(">>");

    PlannedObject po;
    po.newNum = newNum;
    po.isPage = true;
    po.segs.append({head, -1, 0});
    m_objects.append(po);
    return true;
}

bool CopyPlan::plan(const QVector<int>& pageIndices, const QVector<int>& rotations,
                    QString* err) {
    QVector<SourceDoc::PageInfo> pages;
    if (!m_doc->flattenPages(&pages, err)) return false;
    for (const auto& p : pages)
        m_allPageNums.insert(p.objNum);

    // Gewählten Seiten ZUERST neue Nummern geben → Querverweise zwischen
    // gewählten Seiten (Links) bleiben funktionsfähig.
    QVector<QPair<const SourceDoc::PageInfo*, int>> chosen;
    QVector<int> chosenRot;
    for (int i = 0; i < pageIndices.size(); ++i) {
        const int idx = pageIndices.at(i);
        if (idx < 0 || idx >= pages.size()) {
            if (err) *err = QStringLiteral("pageindex");
            return false;
        }
        const int nn = m_next++;
        m_map.insert(pages[idx].objNum, nn);
        chosen.append({&pages[idx], nn});
        chosenRot.append(i < rotations.size() ? rotations.at(i) : 0);
    }
    for (int i = 0; i < chosen.size(); ++i) {
        const auto& c = chosen.at(i);
        if (!planPage(*c.first, c.second, chosenRot.at(i))) {
            if (err) *err = QStringLiteral("page");
            return false;
        }
        m_pageNewNums.append(c.second);
    }
    // Transitiven Abschluss abarbeiten (Deckel gegen entartete Dateien).
    while (!m_queue.isEmpty()) {
        if (m_objects.size() > 250'000) {
            if (err) *err = QStringLiteral("closure");
            return false;
        }
        const qint64 srcNum = m_queue.takeLast();
        if (!planObject(srcNum, m_map.value(srcNum))) {
            if (err) *err = QStringLiteral("object");
            return false;
        }
    }
    return true;
}

}   // namespace

// ══════════════════════════════════════════════════════════════════════════════
//  PdfAssembler
// ══════════════════════════════════════════════════════════════════════════════
PdfAssembler::PdfAssembler(QIODevice* out) : m_out(out) {}

bool PdfAssembler::writeRaw(const QByteArray& bytes, QString* err) {
    if (m_failed) return false;
    if (m_out->write(bytes) != bytes.size()) {
        m_failed = true;
        if (err) *err = QStringLiteral("write");
        return false;
    }
    m_pos += bytes.size();
    return true;
}

bool PdfAssembler::beginObject(int num, QString* err) {
    if (m_offsets.size() <= num) m_offsets.resize(num + 1);
    m_offsets[num] = m_pos;
    return writeRaw(QByteArray::number(num) + " 0 obj\n", err);
}

bool PdfAssembler::begin(QString* err) {
    if (m_begun) return true;
    m_begun = true;
    m_offsets.resize(3);                          // 0 (frei) + 1 Katalog + 2 Baum
    // Binärmarker-Kommentar signalisiert 8-Bit-Inhalt (Spez.-Empfehlung).
    return writeRaw(QByteArrayLiteral("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n"), err);
}

bool PdfAssembler::addSourcePages(const QString& sourcePath,
                                  const QVector<int>& pages, QString* err) {
    return addSourcePages(sourcePath, pages, {}, err);
}

bool PdfAssembler::addSourcePages(const QString& sourcePath,
                                  const QVector<int>& pages,
                                  const QVector<int>& rotations, QString* err) {
    if (m_failed || !m_begun) {
        if (err) *err = QStringLiteral("state");
        return false;
    }
    SourceDoc doc;
    if (!doc.open(sourcePath, err)) return false;

    // Erst VOLLSTÄNDIG planen — schlägt hier etwas fehl, wurde noch kein Byte
    // geschrieben und der Aufrufer kann für DIESE Quelle rastern.
    CopyPlan plan(&doc, m_nextObj);
    if (!plan.plan(pages, rotations, err)) return false;

    // Dann in einem Rutsch schreiben (Stream-Spans direkt vom Quell-Mapping).
    for (const PlannedObject& po : plan.objects()) {
        if (!beginObject(po.newNum, err)) return false;
        for (const Segment& s : po.segs) {
            if (s.srcOff >= 0) {
                if (!writeRaw(QByteArray::fromRawData(doc.data() + s.srcOff,
                                                      static_cast<int>(s.srcLen)),
                              err))
                    return false;
            } else if (!writeRaw(s.bytes, err)) {
                return false;
            }
        }
        if (!writeRaw(QByteArrayLiteral("\nendobj\n"), err)) return false;
    }
    m_nextObj = plan.nextNum();
    for (int pn : plan.pageNewNums())
        m_pageObjs.append(pn);
    return true;
}

bool PdfAssembler::addRasterPage(const QByteArray& jpeg, int pxW, int pxH,
                                 const QSizeF& pagePt, QString* err) {
    if (m_failed || !m_begun || jpeg.isEmpty() || pxW <= 0 || pxH <= 0) {
        if (err) *err = QStringLiteral("state");
        return false;
    }
    const double wPt = pagePt.width()  > 1.0 ? pagePt.width()  : 612.0;
    const double hPt = pagePt.height() > 1.0 ? pagePt.height() : 792.0;
    const int imgObj  = m_nextObj++;
    const int cntObj  = m_nextObj++;
    const int pageObj = m_nextObj++;

    // Bild-XObject (JPEG bleibt JPEG: DCTDecode, kein Re-Encoding).
    if (!beginObject(imgObj, err)) return false;
    QByteArray d = "<< /Type /XObject /Subtype /Image /Width "
                 + QByteArray::number(pxW) + " /Height " + QByteArray::number(pxH)
                 + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode"
                   " /Length " + QByteArray::number(jpeg.size())
                 + " >>\nstream\n";
    if (!writeRaw(d, err) || !writeRaw(jpeg, err)
        || !writeRaw(QByteArrayLiteral("\nendstream\nendobj\n"), err))
        return false;

    // Inhaltsstrom: Bild vollflächig auf die Seite skalieren.
    const QByteArray content = "q " + QByteArray::number(wPt, 'f', 4) + " 0 0 "
                             + QByteArray::number(hPt, 'f', 4)
                             + " 0 0 cm /Im0 Do Q";
    if (!beginObject(cntObj, err)) return false;
    if (!writeRaw("<< /Length " + QByteArray::number(content.size())
                      + " >>\nstream\n" + content + "\nendstream\nendobj\n", err))
        return false;

    // Seite
    if (!beginObject(pageObj, err)) return false;
    if (!writeRaw("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
                      + QByteArray::number(wPt, 'f', 4) + ' '
                      + QByteArray::number(hPt, 'f', 4)
                      + "] /Resources << /XObject << /Im0 "
                      + QByteArray::number(imgObj)
                      + " 0 R >> >> /Contents " + QByteArray::number(cntObj)
                      + " 0 R >>\nendobj\n", err))
        return false;
    m_pageObjs.append(pageObj);
    return true;
}

bool PdfAssembler::addBlankPage(const QSizeF& pagePt, QString* err) {
    if (m_failed || !m_begun) {
        if (err) *err = QStringLiteral("state");
        return false;
    }
    // A4-Fallback, falls eine unbrauchbare Größe hereingereicht wird.
    const double wPt = pagePt.width()  > 1.0 ? pagePt.width()  : 595.276;
    const double hPt = pagePt.height() > 1.0 ? pagePt.height() : 841.890;
    const int cntObj  = m_nextObj++;
    const int pageObj = m_nextObj++;

    // Leerer Inhaltsstrom — nichts zu zeichnen; der Seitengrund ist weiß.
    if (!beginObject(cntObj, err)) return false;
    if (!writeRaw(QByteArrayLiteral("<< /Length 0 >>\nstream\n\nendstream\nendobj\n"), err))
        return false;

    // Seite (keine Ressourcen, eigene MediaBox).
    if (!beginObject(pageObj, err)) return false;
    if (!writeRaw("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
                      + QByteArray::number(wPt, 'f', 4) + ' '
                      + QByteArray::number(hPt, 'f', 4)
                      + "] /Resources << >> /Contents " + QByteArray::number(cntObj)
                      + " 0 R >>\nendobj\n", err))
        return false;
    m_pageObjs.append(pageObj);
    return true;
}

bool PdfAssembler::finish(QString* err) {
    if (m_failed || !m_begun) {
        if (err) *err = QStringLiteral("state");
        return false;
    }
    if (m_pageObjs.isEmpty()) {
        if (err) *err = QStringLiteral("nopages");
        return false;
    }
    // Seitenbaum (Objekt 2)
    if (!beginObject(2, err)) return false;
    QByteArray kids;
    for (int pn : std::as_const(m_pageObjs))
        kids += QByteArray::number(pn) + " 0 R ";
    if (!writeRaw("<< /Type /Pages /Count " + QByteArray::number(m_pageObjs.size())
                      + " /Kids [ " + kids + "] >>\nendobj\n", err))
        return false;
    // Katalog (Objekt 1)
    if (!beginObject(1, err)) return false;
    if (!writeRaw(QByteArrayLiteral("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"),
                  err))
        return false;

    // Klassische XRef-Tabelle (überall akzeptiert) + Trailer.
    const qint64 xrefPos = m_pos;
    const int count = m_offsets.size();
    QByteArray x = "xref\n0 " + QByteArray::number(count) + "\n"
                   "0000000000 65535 f \n";
    for (int i = 1; i < count; ++i) {
        QByteArray off = QByteArray::number(m_offsets.at(i));
        x += QByteArray(10 - off.size(), '0') + off + " 00000 n \n";
    }
    x += "trailer\n<< /Size " + QByteArray::number(count)
       + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefPos)
       + "\n%%EOF\n";
    return writeRaw(x, err);
}

bool PdfAssembler::rebuild(const QString& sourcePath, const QString& outputPath,
                           QString* err) {
    auto fail = [&](const char* m) {
        if (err) *err = QString::fromLatin1(m);
        return false;
    };
    if (sourcePath == outputPath)
        return fail("Ziel darf nicht die Quelle sein");

    const int n = probePageCount(sourcePath);
    if (n <= 0)
        return fail("Seitenzahl nicht lesbar");
    QVector<int> pages;
    pages.reserve(n);
    for (int i = 0; i < n; ++i)
        pages.append(i);

    //  QSaveFile: entweder es steht am Ende die vollständige neue Datei da
    //  oder gar keine — eine halb geschriebene Ausgabe wäre hier besonders
    //  bösartig, weil der Aufrufer sie für die geschwärzte Fassung hielte.
    QSaveFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly))
        return fail("Ziel nicht schreibbar");

    PdfAssembler a(&out);
    if (!a.begin(err) || !a.addSourcePages(sourcePath, pages, err)
        || !a.finish(err)) {
        out.cancelWriting();
        return false;
    }
    if (!out.commit())
        return fail("Ziel nicht abschliessbar");
    return true;
}

int PdfAssembler::probePageCount(const QString& sourcePath) {
    SourceDoc doc;
    QString err;
    if (!doc.open(sourcePath, &err)) return -1;
    const int declared = doc.declaredPageCount();
    if (declared >= 0) return declared;
    QVector<SourceDoc::PageInfo> pages;
    if (!doc.flattenPages(&pages, &err)) return -1;
    return pages.size();
}
