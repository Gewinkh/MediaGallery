#include "pdf/edit/PdfContentEditor.h"
#include "pdf/edit/PdfEncodings.h"
#include "pdf/edit/PdfObjects.h"
#include "pdf/edit/PdfTextLayout.h"

#include <QFile>
#include <QSaveFile>
#include <QHash>
#include <QRegularExpression>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <cstring>

// ══════════════════════════════════════════════════════════════════════════════
//  Alle Helfer liegen in einem anonymen Namespace - reine Byte-Arbeit, kein Qt-
//  Objektzustand. Bei JEDER Unsicherheit geben die Funktionen einen Fehler
//  zurück; PdfContentEditor::editText bricht dann ab (Aufrufer -> Raster-Export).
// ══════════════════════════════════════════════════════════════════════════════
namespace {

//  Die generischen PDF-Objekt-Helfer liegen jetzt in
//  PdfObjects.h - sie werden auch vom Vektor-Export gebraucht und sind
//  bewusst nur EINMAL vorhanden (dieselbe streng geprüfte Byte-Arbeit
//  zweimal zu pflegen wäre die schlechtere Lösung).
using namespace mg::pdfobj;

// PDF-Paren-String -> Bytes (Escapes auflösen). in zeigt HINTER '('.
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

// Bytes -> PDF-Paren-String "(...)".
//  ( ) \ werden escapt; Bytes ausserhalb des druckbaren ASCII zusaetzlich
//  OKTAL (\ddd). Das ist nötig, seit auch Nicht-ASCII-Kodierungen (WinAnsi/
//  MacRoman) unterstützt werden: rohe Bytes ≥ 0x80 im String wären zwar nach
//  Spezifikation erlaubt, aber \ddd ist die robuste, 7-Bit-sichere Form und
//  vermeidet Ärger mit Werkzeugen, die den Strom als Text behandeln.
QByteArray encodeParenBytes(const QByteArray& bytes) {
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
    out += ")";
    return out;
}

// Ein Textzeige-Treffer im Content-Stream: Byte-Bereich des zu ersetzenden
// Operanden (String bei Tj bzw. Array bei TJ), der dekodierte ASCII-Text und ob
// es ein TJ-Array ist (dann muss der Ersatz ein Array bleiben).
//  `opEnd` = Byte-Position HINTER dem Operator-Schlüsselwort (Tj/TJ). Damit
//  lässt sich prüfen, ob zwei Treffer UNMITTELBAR aufeinander folgen: liegt
//  zwischen dem Operator des einen und dem Operanden des nächsten nur
//  Leerraum, zeichnen beide ohne dazwischenliegende Positionierung (Td/TD/Tm/
//  T*/Tf …) weiter - nur dann darf ihr Text als EINE zusammenhängende Stelle
//  behandelt werden (s. `runsAreAdjacent`).
//  `bytes` sind die ROHEN Stringbytes (bei TJ: alle String-Elemente
//  aneinandergehängt). Sie werden ERST in editText entschlüsselt, wo die
//  Kodierung der aktiven Schrift bekannt ist - vorher stünde nur eine
//  Latin-1-Vermutung zur Verfügung, die bei WinAnsi/MacRoman falsch wäre.
//  `fontRes` ist der Ressourcenname der zuletzt per `Tf` gesetzten Schrift.
//  `size`/`charSp`/`wordSp` sind der Textzustand AN DIESER STELLE (Tf/Tc/Tw).
//  Die Teil-Redaktion (s. unten) braucht sie, um die entstehende Lücke exakt
//  auszugleichen - und lehnt ab, sobald sie ihn nicht sicher kennt.
struct ShowHit { qint64 start; qint64 end; qint64 opEnd;
                 QByteArray bytes; bool isArray; QByteArray fontRes;
                 double size = 0.0; double charSp = 0.0; double wordSp = 0.0; };

// Findet alle Tj/TJ-Treffer im (inflateten) Content-Stream. Überspringt
// Inline-Bilder (BI…EI), damit deren Binärdaten nicht als Tokens missraten.
QVector<ShowHit> scanTextShows(const QByteArray& c) {
    QVector<ShowHit> hits;
    const qint64 n = c.size();
    qint64 i = 0;
    // „Letzter Operand": Bereich + Typ (0 = keiner, 1 = String, 2 = Array).
    qint64 lastStart = -1, lastEnd = -1; int lastType = 0; QByteArray lastBytes;
    QByteArray lastName;      // zuletzt gesehener /Name-Operand (für Tf)
    QByteArray curFont;       // aktive Schrift laut letztem "…Tf"
    //  Textzustand: Schriftgröße (Tf), Zeichen- und Wortabstand (Tc/Tw).
    double curSize = 0.0, curCharSp = 0.0, curWordSp = 0.0;
    //  Der zuletzt gesehene Zahlen-Operand (Tf/Tc/Tw brauchen ihn - jeweils
    //  den LETZTEN vor dem Operator).
    double num2 = 0.0;
    while (i < n) {
        char ch = c[i];
        if (isWs(ch)) { ++i; continue; }
        if (ch == '%') { while (i<n && c[i]!='\n' && c[i]!='\r') ++i; continue; }
        if (ch == '(') {
            qint64 e = skipValue(c, i);
            lastStart=i; lastEnd=e; lastType=1;
            lastBytes = decodeParenString(c, i, e);
            i = e; continue;
        }
        if (ch == '<' && !(i+1<n && c[i+1]=='<')) {          // Hex-String
            qint64 e = skipValue(c, i);
            lastStart=i; lastEnd=e; lastType=1;
            QByteArray hex = c.mid(i+1, e-1-(i+1)); hex.replace(" ","").replace("\n","").replace("\r","");
            lastBytes = QByteArray::fromHex(hex);
            i = e; continue;
        }
        if (ch == '[') {
            qint64 e = skipValue(c, i);
            // Array-Text = Verkettung seiner String-Elemente (Zahlen = Spacing).
            QByteArray inner = c.mid(i, e-i);
            QByteArray t; qint64 j = 0;
            while (j < inner.size()) {
                if (inner[j]=='(') { qint64 je = skipValue(inner, j);
                    t += decodeParenString(inner, j, je); j = je; }
                else if (inner[j]=='<') { qint64 je = skipValue(inner, j);
                    QByteArray hex = inner.mid(j+1, je-1-(j+1)); hex.replace(" ","").replace("\n","").replace("\r","");
                    t += QByteArray::fromHex(hex); j = je; }
                else ++j;
            }
            lastStart=i; lastEnd=e; lastType=2; lastBytes=t;
            i = e; continue;
        }
        if (ch == '<' && i+1<n && c[i+1]=='<') { i = skipValue(c, i); lastType=0; continue; }
        if (ch == '/') { const qint64 ns = i; i = skipValue(c, i);
                         lastName = c.mid(ns, i-ns); lastType=0; continue; }
        if (ch=='-'||ch=='+'||ch=='.'||(ch>='0'&&ch<='9')) {  // Zahl
            const qint64 ns2 = i;
            ++i; while (i<n && !isWs(c[i]) && !isDelim(c[i])) ++i;
            num2 = c.mid(ns2, i - ns2).toDouble();
            continue;
        }
        // Keyword/Operator
        qint64 s = i; while (i<n && !isWs(c[i]) && !isDelim(c[i])) ++i;
        const QByteArray op = c.mid(s, i-s);
        if (op == "BI") {                                    // Inline-Bild -> bis EI
            qint64 ei = c.indexOf("EI", i);
            i = (ei < 0) ? n : ei + 2; lastType = 0; continue;
        }
        if (op == "Tf") { curFont = lastName; curSize = num2; }   // "/F1 12 Tf"
        else if (op == "Tc") curCharSp = num2;
        else if (op == "Tw") curWordSp = num2;
        if (op == "Tj" && lastType == 1)
            hits.push_back({ lastStart, lastEnd, i, lastBytes, false, curFont,
                             curSize, curCharSp, curWordSp });
        else if (op == "TJ" && lastType == 2)
            hits.push_back({ lastStart, lastEnd, i, lastBytes, true, curFont,
                             curSize, curCharSp, curWordSp });
        lastType = 0;
    }
    return hits;
}

//  Folgen zwei Treffer UNMITTELBAR aufeinander? Nur wenn zwischen dem Operator
//  des ersten und dem Operanden des zweiten ausschließlich Leerraum (oder ein
//  Kommentar) steht. Jede andere Anweisung dazwischen - insbesondere
//  Positionierung (Td/TD/Tm/T*) oder ein Schriftwechsel (Tf) - bedeutet, dass
//  die Stücke an verschiedenen Stellen bzw. in verschiedenen Schriften
//  gezeichnet werden; sie dürfen dann NICHT zu einer Fundstelle zusammengefasst
//  werden, weil der Ersatz sonst an die Position des ERSTEN Stücks rutschen und
//  der Zeilenumbruch bzw. die Schriftzuordnung verloren gehen würde.
bool showsAreAdjacent(const QByteArray& c, const ShowHit& a, const ShowHit& b) {
    qint64 i = a.opEnd;
    while (i < b.start) {
        const char ch = c[i];
        if (isWs(ch)) { ++i; continue; }
        if (ch == '%') {                              // Kommentar bis Zeilenende
            while (i < b.start && c[i] != '\n' && c[i] != '\r') ++i;
            continue;
        }
        return false;                                 // echte Anweisung dazwischen
    }
    return true;
}


// ══════════════════════════════════════════════════════════════════════════════
//  GEMEINSAME GRUNDLAGE der beiden Einstiege (`editText` und `redactAreas`)
//
//  Beide brauchen exakt dasselbe Gerüst: Datei prüfen, klassische xref, Objekte,
//  Seitenbaum, Content-Stream einer Seite, inkrementelles Update schreiben. Das
//  steht deshalb EINMAL hier - dieselbe streng geprüfte Byte-Arbeit zweimal zu
//  pflegen wäre die schlechtere Lösung (gleiche Begründung wie bei PdfObjects.h).
// ══════════════════════════════════════════════════════════════════════════════

using FailFn = std::function<bool(const char*)>;

struct DocCtx {
    QByteArray          buf;
    QHash<int, ObjLoc>  objs;
    int                 rootNum = -1;
    int                 rootGen = 0;
    qint64              prevXref = 0;
    QVector<int>        pageObjs;    // Objektnummer je Seite (Dokumentreihenfolge)
    QVector<QByteArray> pageRes;     // vererbtes /Resources-Dict je Seite

    QByteArray bodyOf(int num) const {
        const auto it = objs.constFind(num);
        return (it == objs.constEnd()) ? QByteArray() : objectBody(buf, it->offset);
    }
    QByteArray dictOf(int num) const { return dictOfObject(bodyOf(num)); }

    //  Entpackte Nutzdaten eines Stream-Objekts (roh oder /FlateDecode).
    //  Wird für /Contents UND für /ToUnicode-CMaps gebraucht; `ok` bleibt
    //  false bei fremdem Filter oder defektem Strom.
    QByteArray streamDataOf(int num, bool* ok) const {
        *ok = false;
        const QByteArray body = bodyOf(num);
        if (body.isEmpty()) return {};
        const QByteArray d = dictOfObject(body);
        const QByteArray filt = nameValue(d, "Filter");
        const bool fl = (filt == "/FlateDecode");
        if (!filt.isEmpty() && !fl) return {};
        qint64 sp = body.indexOf("stream");
        if (sp < 0) return {};
        sp += 6;
        if (sp < body.size() && body[sp]=='\r') ++sp;
        if (sp < body.size() && body[sp]=='\n') ++sp;
        const qint64 ep = body.indexOf("endstream", sp);
        if (ep < 0) return {};
        qint64 len = ep - sp;
        //  /Length darf eine REFERENZ sein (s. pdfobj::streamLength).
        const qint64 dl = streamLength(d, buf, objs);
        if (dl >= 0 && dl <= len) len = dl;
        const QByteArray raw = body.mid(sp, len);
        if (!fl) { *ok = true; return raw; }
        bool infOk = false;
        const QByteArray inf = zInflate(raw, &infOk);
        if (!infOk) return {};
        *ok = true;
        return inf;
    }
};

//  Datei einlesen und das Gerüst prüfen. Bei JEDER Unsicherheit false - der
//  Aufrufer weicht dann auf den Raster-Weg aus.
bool loadDoc(const QString& path, DocCtx* d, const FailFn& fail) {
    QFile in(path);
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

    // /Root ermitteln (Trailer bzw. beliebiges Vorkommen - letztes gewinnt).
    int rootNum = -1, rootGen = 0;
    {
        static const QRegularExpression re(QStringLiteral("/Root\\s+(\\d+)\\s+(\\d+)\\s+R"));
        auto it = re.globalMatch(QString::fromLatin1(buf));
        while (it.hasNext()) { const auto m = it.next(); rootNum = m.captured(1).toInt(); rootGen = m.captured(2).toInt(); }
    }
    if (rootNum < 0 || !objs.contains(rootNum)) return fail("kein /Root");

    // Objektkörper-Zugriff (im Kontext später als DocCtx::bodyOf/dictOf).
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
                // /Pages-Knoten -> /Kids [ ... ]
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
    d->buf      = buf;
    d->objs     = objs;
    d->rootNum  = rootNum;
    d->rootGen  = rootGen;
    d->prevXref = prevXrefOffset;
    d->pageObjs = pageObjs;
    d->pageRes  = pageRes;
    return true;
}

//  Content-Stream EINER Seite: Objektnummer, Generation und die ENTPACKTEN
//  Bytes. Verlangt genau EINEN Stream (kein /Contents-Array) mit bekannten
//  Schlüsseln und höchstens /FlateDecode.
struct PageContent { int num = 0; int gen = 0; QByteArray data; };

bool loadPageContent(const DocCtx& doc, int pageIdx, PageContent* out, const FailFn& fail) {
    if (pageIdx < 0 || pageIdx >= doc.pageObjs.size()) return fail("Seitenindex außerhalb");
    const QByteArray pdict = doc.dictOf(doc.pageObjs[pageIdx]);
    const QByteArray& buf  = doc.buf;
    const auto& objs       = doc.objs;
        // /Contents muss EIN Stream sein (kein Array).
        const qint64 cp = findKey(pdict, "Contents");
        if (cp < 0) return fail("kein /Contents");
        if (pdict[cp] == '[') return fail("/Contents-Array -> Fallback");
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
                    return fail("unbekannte Stream-Schlüssel -> Fallback"); }
        }
        // Filter
        const QByteArray filt = nameValue(cdict, "Filter");
        const bool flate = (filt == "/FlateDecode");
        if (!filt.isEmpty() && !flate) return fail("fremder Stream-Filter -> Fallback");

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
        const qint64 declLen = streamLength(cdict, buf, objs);
        if (declLen >= 0 && declLen <= rawLen) rawLen = declLen;
        QByteArray raw = cbody.mid(sPos, rawLen);

        QByteArray content;
        if (flate) { bool ok=false; content = zInflate(raw, &ok); if (!ok) return fail("Inflate fehlgeschlagen"); }
        else content = raw;
    out->num  = cnum;
    out->gen  = cit->gen;
    out->data = content;
    return true;
}

struct NewObj { int num; int gen; QByteArray data; };  // data = inflatierter Content

//  Die geänderten Content-Objekte als inkrementelles Update ans Original hängen
//  (append-only): neue Objektfassungen + kleine xref-Sektion + Trailer mit /Prev.
bool writeIncremental(const DocCtx& doc, const QHash<int, NewObj>& edited,
                      const QString& outputPath, const FailFn& fail) {
    const QByteArray& buf = doc.buf;
    const auto& objs      = doc.objs;
    const int rootNum     = doc.rootNum;
    const int rootGen     = doc.rootGen;
    const qint64 prevXrefOffset = doc.prevXref;
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


// ══════════════════════════════════════════════════════════════════════════════
//  SCHWÄRZEN: Zeichen aus einem Zeigeoperator herausschneiden
// ══════════════════════════════════════════════════════════════════════════════

//  Ein herausgeschnittener Lauf, gemessen in den ZUSAMMENGEFÜGTEN Bytes eines
//  Zeigeoperators (so zählt auch `PdfGlyph::byteOffset`).
struct ByteCut {
    qint64 from = 0;
    qint64 to   = 0;        // exklusiv
    qreal  advancePt = 0.0; // Vorschub der entfernten Zeichen in Seiten-Punkten
};

//  Den Zeigeoperator neu schreiben. Der Ersatz ist IMMER ein `[…] TJ`-Array -
//  auch für ein `Tj`: nur ein Array kann den Ausgleichsversatz aufnehmen, der
//  die entstehende Lücke schließt. Zahlen (Kerning) zwischen den Gliedern
//  bleiben unangetastet; sie verschieben unabhängig von den entfernten Zeichen.
//  `ok` bleibt false, wenn der Operand nicht sicher zerlegbar ist.
QByteArray rebuildShowWithoutCuts(const QByteArray& content, const ShowHit& h,
                                  const QVector<ByteCut>& cuts, qreal tjUnitPt,
                                  bool* ok) {
    *ok = false;
    if (tjUnitPt <= 0.0) return {};

    //  Ausgleich für EINEN Schnitt: ein TJ-Versatz von −1000 schiebt um
    //  `tjUnitPt` nach rechts, also gilt n = −1000 · Lücke / tjUnitPt.
    auto shiftNumber = [&](qreal advPt) {
        return QByteArray::number(-1000.0 * advPt / tjUnitPt, 'f', 2);
    };

    //  Ein Element beisteuern: die behaltenen Stücke + Ausgleich je Schnitt.
    QByteArray out = "[";
    qint64 base = 0;                       // Offset dieses Elements im Gesamttext
    auto emitString = [&](const QByteArray& bytes) {
        const qint64 len = bytes.size();
        qint64 pos = 0;
        for (const ByteCut& c : cuts) {
            if (c.to <= base || c.from >= base + len) continue;      // trifft nicht
            const qint64 lf = qBound<qint64>(0, c.from - base, len);
            const qint64 lt = qBound<qint64>(0, c.to   - base, len);
            if (lf > pos) out += encodeParenBytes(bytes.mid(pos, lf - pos));
            //  Der Ausgleich gehört genau EINMAL je Schnitt an dessen Anfang -
            //  auch wenn der Schnitt über mehrere Array-Glieder läuft.
            if (c.from >= base && c.from < base + len)
                out += " " + shiftNumber(c.advancePt) + " ";
            pos = qMax(pos, lt);
        }
        if (pos < len) out += encodeParenBytes(bytes.mid(pos, len - pos));
        base += len;
    };

    if (!h.isArray) {
        emitString(h.bytes);
    } else {
        //  Array zerlegen wie `scanTextShows`: Strings beisteuern, Zahlen
        //  wörtlich übernehmen.
        const QByteArray arr = content.mid(h.start, h.end - h.start);
        if (!arr.startsWith('[')) return {};
        qint64 k = 1;
        while (k < arr.size() - 1) {
            if (isWs(arr[k])) { ++k; continue; }
            if (arr[k] == '(') {
                const qint64 e = skipValue(arr, k);
                emitString(decodeParenString(arr, k, e));
                k = e;
            } else if (arr[k] == '<') {
                const qint64 e = skipValue(arr, k);
                QByteArray hex = arr.mid(k + 1, e - 1 - (k + 1));
                hex.replace(" ", "").replace("\n", "").replace("\r", "");
                emitString(QByteArray::fromHex(hex));
                k = e;
            } else {
                const qint64 s2 = k;
                while (k < arr.size() - 1 && !isWs(arr[k]) && arr[k] != '('
                       && arr[k] != '<') ++k;
                if (k == s2) return {};                     // kein Fortschritt
                out += " " + arr.mid(s2, k - s2) + " ";
            }
        }
    }
    out += "] TJ";
    *ok = true;
    return out;
}

//  Zählt eine Glyphe als „unter dem Balken"? Jede Berührung zählt - eine
//  Glyphe, die nur halb verdeckt ist, stünde sonst VOLLSTÄNDIG weiter in der
//  Textebene und wäre auslesbar. Nur ein Hauch von Überlappung (unter 15 % der
//  Glyphenfläche) zählt nicht: sonst risse ein etwas zu hoch gezogener Balken
//  die Nachbarzeile mit, deren Zeichen sichtbar NEBEN dem Balken stehen.
bool glyphIsCovered(const QRectF& glyph, const QRectF& area) {
    const QRectF sect = glyph.intersected(area);
    if (sect.isEmpty()) return false;
    const qreal ga = glyph.width() * glyph.height();
    if (ga <= 0.0) return area.contains(glyph.center());
    return (sect.width() * sect.height()) >= 0.15 * ga;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════════
namespace mg {

bool PdfContentEditor::editText(const QString& inputPath, const QString& outputPath,
                                const QVector<PdfTextEdit>& edits, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };
    if (edits.isEmpty()) return fail("keine Ersetzungen");

    DocCtx doc;
    if (!loadDoc(inputPath, &doc, fail)) return false;
    //  Kurznamen, damit die geprüfte Logik darunter unverändert bleibt.
    const QVector<int>& pageObjs = doc.pageObjs;
    const QVector<QByteArray>& pageRes = doc.pageRes;
    auto bodyOf = [&](int num) { return doc.bodyOf(num); };
    auto dictOf = [&](int num) { return doc.dictOf(num); };
    auto streamDataOf = [&](int num, bool* ok) { return doc.streamDataOf(num, ok); };

    // Ersetzungen nach Seite gruppieren; Vorbedingungen je betroffener Seite prüfen
    // und die neuen (inflateten) Content-Bytes bilden.
    QHash<int, NewObj> edited;             // Content-ObjNum -> neuer Inhalt

    for (const PdfTextEdit& ed : edits) {
        if (ed.page < 0 || ed.page >= pageObjs.size()) return fail("Seitenindex außerhalb");
        if (ed.original.isEmpty()) return fail("leerer Originaltext");
        //  KEINE ASCII-Vorabprüfung mehr: ob ein Zeichen darstellbar ist, hängt
        //  an der Kodierung der SCHRIFT an der Fundstelle und wird dort geprüft
        //  (s. unten, `enc.encode`).

        const int pnum = pageObjs[ed.page];
        const QByteArray pdict = dictOf(pnum);
        const QByteArray res   = pageRes[ed.page];

        // Font-Sicherheit + Kodierung je Ressourcenname (/F1 -> Encoding).
        //  KEIN /Type0 (CID) - dafür fehlt das CMap-Round-Tripping.
        QHash<QByteArray, mg::pdfenc::Encoding> fontEnc;
        //  Glyphenbreiten je Schrift-Ressource: /FirstChar + /Widths (in
        //  1/1000 em). NUR einfache Schriften; fehlt eines von beidem, bleibt
        //  die Ressource hier ungenannt - die Teil-Redaktion lehnt dann ab,
        //  statt mit geschätzten Breiten zu rechnen.
        struct FontWidths { int firstChar = 0; QVector<double> widths; };
        QHash<QByteArray, FontWidths> fontW;
        {
            const qint64 fp = findKey(res, "Font");
            if (fp < 0) return fail("keine Fonts (Sicherheitscheck)");
            QByteArray fontDict;
            if (res[fp] == '<') { qint64 e = skipValue(res, fp); fontDict = res.mid(fp+2, (e-2)-(fp+2)); }
            else { const int fn = refValue(res, "Font"); if (fn<0) return fail("Font-Ref"); fontDict = dictOf(fn); }

            //  Das Font-Dict ist "/F1 5 0 R /F2 6 0 R …" - Name und Objekt
            //  paarweise auslesen, damit die Kodierung dem im Content-Stream
            //  benutzten Ressourcennamen zugeordnet werden kann.
            qint64 i = 0;
            while (i < fontDict.size()) {
                while (i < fontDict.size() && isWs(fontDict[i])) ++i;
                if (i >= fontDict.size() || fontDict[i] != '/') { ++i; continue; }
                const qint64 ns = i;
                i = skipValue(fontDict, i);
                const QByteArray resName = fontDict.mid(ns, i - ns);
                const qint64 vs = i;
                i = skipValue(fontDict, vs);
                const QByteArray refTail = fontDict.mid(vs, i - vs);
                static const QRegularExpression rre(QStringLiteral("^\\s*(\\d+)\\s+(\\d+)\\s+R"));
                const auto m = rre.match(QString::fromLatin1(refTail));
                if (!m.hasMatch()) continue;
                const int fn = m.captured(1).toInt();
                const QByteArray fd = dictOf(fn);

                //  ── Type0 (CID) ────────────────────────────────────────────
                //  Die Stringbytes sind hier 2-Byte-CIDs, also Glyphennummern
                //  der (meist als Teilmenge eingebetteten) Schrift. Welches
                //  Zeichen dahintersteht, sagt einzig die /ToUnicode-CMap.
                //  Unterstützt wird NUR /Identity-H: bei einer anderen
                //  /Encoding-CMap wäre schon die Zerlegung in Codes unklar.
                if (nameValue(fd, "Subtype") == "/Type0") {
                    const QByteArray encName = nameValue(fd, "Encoding");
                    if (encName != "/Identity-H")
                        return fail("Type0 ohne /Identity-H -> Fallback");
                    const int tuNum = refValue(fd, "ToUnicode");
                    if (tuNum < 0)
                        return fail("Type0 ohne /ToUnicode -> Fallback");
                    bool sOk = false;
                    const QByteArray cmapData = streamDataOf(tuNum, &sOk);
                    if (!sOk || cmapData.isEmpty())
                        return fail("/ToUnicode nicht lesbar -> Fallback");
                    bool cOk = false;
                    const mg::pdfenc::Encoding cidEnc =
                        mg::pdfenc::Encoding::fromCidToUnicode(cmapData, &cOk);
                    if (!cOk)
                        return fail("/ToUnicode-CMap nicht auswertbar -> Fallback");
                    fontEnc.insert(resName, cidEnc);
                    continue;
                }

                //  /Encoding: Name ODER Dict (letzteres ggf. als Referenz).
                QByteArray encVal;
                const qint64 ep = findKey(fd, "Encoding");
                if (ep >= 0) {
                    if (fd[ep] == '/') { qint64 e = skipValue(fd, ep); encVal = fd.mid(ep, e-ep); }
                    else if (fd[ep] == '<') { qint64 e = skipValue(fd, ep); encVal = fd.mid(ep, e-ep); }
                    else { const int en = refValue(fd, "Encoding");
                           if (en >= 0) encVal = QByteArray("<<") + dictOf(en) + ">>"; }
                }
                bool encOk = false;
                const mg::pdfenc::Encoding enc =
                    mg::pdfenc::Encoding::fromEncodingValue(encVal, &encOk);
                if (!encOk) return fail("unbekannter Glyphenname in /Differences -> Fallback");
                fontEnc.insert(resName, enc);
                {   //  Breiten mitnehmen, soweit die Schrift sie mitbringt.
                    const QByteArray wRaw = [&]() -> QByteArray {
                        const qint64 wp = findKey(fd, "Widths");
                        if (wp < 0) return {};
                        if (fd[wp] == '[') { const qint64 e = skipValue(fd, wp);
                                             return fd.mid(wp, e - wp); }
                        const int wn = refValue(fd, "Widths");
                        return (wn >= 0) ? bodyOf(wn).trimmed() : QByteArray();
                    }();
                    const qint64 fcp = findKey(fd, "FirstChar");
                    if (wRaw.startsWith('[') && fcp >= 0) {
                        FontWidths fw;
                        fw.firstChar = fd.mid(fcp, 12).simplified().split(' ').value(0).toInt();
                        const QByteArray inner = wRaw.mid(1, wRaw.size() - 2).simplified();
                        for (const QByteArray& part : inner.split(' ')) {
                            if (part.isEmpty()) continue;
                            bool okw = false;
                            const double d = part.toDouble(&okw);
                            if (okw) fw.widths.push_back(d);
                        }
                        if (!fw.widths.isEmpty())
                            fontW.insert(resName, fw);
                    }
                }
            }
        }

        PageContent pc;
        if (!loadPageContent(doc, ed.page, &pc, fail)) return false;
        const int cnum = pc.num;
        QByteArray content = pc.data;

        // Auf bereits (für diese Seite) editierten Inhalt weiterarbeiten, falls
        // mehrere Ersetzungen dieselbe Seite betreffen.
        if (edited.contains(cnum)) content = edited.value(cnum).data;

        // ── Treffer suchen ──────────────────────────────────────────────────
        //  Der Originaltext muss GENAU EINMAL vorkommen - entweder als EIN
        //  Tj-String/TJ-Array oder, seit der Erweiterung, verteilt über eine
        //  FOLGE unmittelbar aufeinanderfolgender Zeige-Operatoren.
        //
        //  Warum das nötig ist: Erzeuger zerlegen eine Zeile regelmäßig in
        //  mehrere Operatoren - etwa `(Hal) Tj (lo) Tj` nach einem Kerning-Paar
        //  oder abwechselnde Tj/TJ-Stücke. Der Text steht dann VISUELL
        //  zusammenhängend auf einer Zeile, war für die Suche aber nie
        //  auffindbar, und jede solche Ersetzung fiel auf den Raster-Export
        //  zurück.
        //
        //  Sicherheit: eine Folge zählt nur, wenn zwischen ihren Gliedern
        //  ausschließlich Leerraum steht (`showsAreAdjacent`) - sobald
        //  Positionierung oder ein Schriftwechsel dazwischenliegt, bricht die
        //  Folge ab. Damit kann der Ersatz nie über einen Zeilenumbruch oder
        //  eine Schriftgrenze hinweg zusammengezogen werden.
        const QVector<ShowHit> hits = scanTextShows(content);

        //  Die Rohbytes jedes Treffers werden mit der Kodierung SEINER Schrift
        //  entschlüsselt. Ist die Schrift unbekannt (kein passendes /Tf oder
        //  fehlender Ressourceneintrag), gilt die konservative ASCII-Tabelle -
        //  Nicht-ASCII-Bytes werden dann zu U+FFFD und passen sicher auf keinen
        //  Originaltext, statt zufällig zu treffen.
        const mg::pdfenc::Encoding asciiFallback;
        auto encOfHit = [&](const ShowHit& h) -> const mg::pdfenc::Encoding& {
            const auto it = fontEnc.constFind(h.fontRes);
            return (it == fontEnc.constEnd()) ? asciiFallback : it.value();
        };
        QVector<QString> hitText;
        hitText.reserve(hits.size());
        for (const ShowHit& h : hits)
            hitText.push_back(encOfHit(h).decode(h.bytes));

        int matchStart = -1, matchLen = 0, matchCount = 0;
        for (int k = 0; k < hits.size(); ++k) {
            QString acc;
            for (int j = k; j < hits.size(); ++j) {
                if (j > k && !showsAreAdjacent(content, hits[j-1], hits[j]))
                    break;                        // Folge endet hier
                acc += hitText[j];
                if (acc.size() > ed.original.size())
                    break;                        // schon zu lang -> abbrechen
                if (acc == ed.original) {
                    //  Kürzeste Folge ab k gewinnt; über alle k zählen wir die
                    //  Fundstellen, um Mehrdeutigkeit zu erkennen.
                    if (matchCount == 0) { matchStart = k; matchLen = j - k + 1; }
                    ++matchCount;
                    break;
                }
            }
        }
        // ── TEIL-REDAKTION: der Text steht MITTEN in EINEM Zeige-String ─────
        //  Der Weg oben ersetzt nur GANZE Zeige-Strings (bzw. Folgen davon).
        //  Für das Schwärzen ist das zu grob: „Name Muster Geheim" steht oft in
        //  EINEM Tj, und nur „Geheim" soll verschwinden.
        //
        //  Das Herauslösen verschiebt den Rest der Zeile nach links - es sei
        //  denn, die entstehende Lücke wird ausgeglichen. Genau das tut der
        //  TJ-Versatz: `(Name Muster ) -2778 (…)` schiebt um 2.778 · Schriftgröße
        //  nach rechts, also exakt um die Breite der entfernten Zeichen.
        //  Gerechnet wird NUR mit den Breiten, die die Schrift selbst mitbringt
        //  (`/Widths`) - geschätzt wird nichts.
        //
        //  BEWUSST ENG (sonst Ablehnung, nie Raten):
        //   • nur ENTFERNEN (leerer Ersatz) - ein anderer Text hätte eine
        //     eigene Breite, die zusätzlich zu verrechnen wäre,
        //   • nur ein einfacher `Tj`-String (ein TJ-Array bringt eigene
        //     Versätze mit, die mitgerechnet werden müssten),
        //   • Zeichen- und Wortabstand müssen 0 sein (Tc/Tw gehen sonst in die
        //     Vorschubrechnung ein),
        //   • Schriftgröße bekannt, Breiten für JEDES entfernte Zeichen da,
        //   • der Text kommt im Dokument genau EINMAL vor.
        bool partial = false;
        int  partialHit = -1, partialAt = -1;
        if (matchCount == 0 && ed.replacement.isEmpty()) {
            int found = 0;
            for (int k = 0; k < hits.size(); ++k) {
                const int at = hitText[k].indexOf(ed.original);
                if (at < 0) continue;
                //  Mehrfach in DERSELBEN Zeichenkette? Dann ist es mehrdeutig.
                if (hitText[k].indexOf(ed.original, at + 1) >= 0) { found += 2; break; }
                ++found;
                partialHit = k;
                partialAt  = at;
            }
            if (found == 1) partial = true;
            else if (found > 1) return fail("Originaltext mehrdeutig -> Fallback");
        }

        if (!partial && matchCount == 0) return fail("Originaltext nicht gefunden -> Fallback");
        if (matchCount > 1) return fail("Originaltext mehrdeutig -> Fallback");

        if (partial) {
            const ShowHit& h = hits[partialHit];
            if (h.isArray)        return fail("Teiltext in einem TJ-Array -> Fallback");
            if (h.size <= 0.0)    return fail("Schriftgröße unbekannt -> Fallback");
            if (h.charSp != 0.0 || h.wordSp != 0.0)
                return fail("Zeichen-/Wortabstand gesetzt -> Fallback");

            const auto wit = fontW.constFind(h.fontRes);
            if (wit == fontW.constEnd()) return fail("keine /Widths -> Fallback");

            //  Byte-Index == Zeichen-Index gilt nur, solange die Zeichenkette
            //  rein ASCII ist - das prüft diese Einheit ohnehin für Original
            //  und Ersatz, hier zusätzlich für den GANZEN String.
            const QString whole = hitText[partialHit];
            for (const QChar c2 : whole)
                if (c2.unicode() < 0x20 || c2.unicode() > 0x7E)
                    return fail("Teiltext nur in reinem ASCII -> Fallback");
            if (whole.size() != h.bytes.size())
                return fail("Byte-/Zeichenlänge weichen ab -> Fallback");

            //  Breite der entfernten Zeichen (1/1000 em) aufsummieren.
            double removed = 0.0;
            for (int k = 0; k < ed.original.size(); ++k) {
                const int code = static_cast<unsigned char>(h.bytes.at(partialAt + k));
                const int idx  = code - wit->firstChar;
                if (idx < 0 || idx >= wit->widths.size())
                    return fail("Breite eines Zeichens fehlt -> Fallback");
                removed += wit->widths.at(idx);
            }

            const QByteArray prefix = h.bytes.left(partialAt);
            const QByteArray suffix = h.bytes.mid(partialAt + ed.original.size());

            //  Neues TJ-Array: Vorderteil, Versatz (negativ = nach rechts),
            //  Hinterteil. Leere Teile werden weggelassen.
            QByteArray arr = "[";
            if (!prefix.isEmpty()) arr += encodeParenBytes(prefix);
            //  Steht nichts mehr dahinter, braucht es auch keinen Ausgleich -
            //  dann verschiebt sich nichts.
            if (!suffix.isEmpty()) {
                arr += " " + QByteArray::number(-removed, 'f', 0) + " ";
                arr += encodeParenBytes(suffix);
            }
            arr += "] TJ";

            QByteArray nc = content;
            nc.replace(h.start, h.opEnd - h.start, arr);
            edited.insert(cnum, { cnum, pc.gen, nc });
            continue;
        }

        //  Der GESAMTE Ersatz wandert in das ERSTE Glied der Folge; alle
        //  weiteren Glieder werden geleert (ihre Operatoren bleiben stehen, sie
        //  zeichnen nur nichts mehr). Da die Folge nachweislich ohne
        //  Positionierung dazwischen auskommt, beginnt der Ersatz exakt an der
        //  Stelle, an der der Originaltext begann.
        //  Von HINTEN nach vorn ersetzen, damit die vorderen Byte-Offsets
        //  gültig bleiben.
        //  Der Ersatz muss in der Kodierung der Schrift AN DER FUNDSTELLE
        //  darstellbar sein - sonst stünden im PDF Bytes, die diese Schrift gar
        //  nicht kennt. Nicht darstellbar -> sauber ablehnen (Raster-Fallback).
        QByteArray replBytes;
        if (!encOfHit(hits[matchStart]).encode(ed.replacement, &replBytes))
            return fail("Ersatztext in der Schriftkodierung nicht darstellbar -> Fallback");

        QByteArray nc = content;
        for (int j = matchStart + matchLen - 1; j >= matchStart; --j) {
            const ShowHit& h = hits[j];
            const QByteArray piece = (j == matchStart) ? replBytes : QByteArray();
            QByteArray repl;
            if (h.isArray) repl = QByteArray("[") + (piece.isEmpty() ? QByteArray()
                                                     : encodeParenBytes(piece)) + "]";
            else           repl = piece.isEmpty() ? QByteArray("()")
                                                  : encodeParenBytes(piece);
            nc.replace(h.start, h.end - h.start, repl);
        }
        edited.insert(cnum, { cnum, pc.gen, nc });
    }
    if (edited.isEmpty()) return fail("nichts ersetzt");

    return writeIncremental(doc, edited, outputPath, fail);
}


// ══════════════════════════════════════════════════════════════════════════════
//  redactAreas - GEOMETRISCHES Schwärzen (ohne Kenntnis des Textes)
// ══════════════════════════════════════════════════════════════════════════════
bool PdfContentEditor::redactAreas(const QString& inputPath, const QString& outputPath,
                                   const QVector<PdfRedactArea>& areas, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };
    if (areas.isEmpty()) return fail("keine Flächen");

    DocCtx doc;
    if (!loadDoc(inputPath, &doc, fail)) return false;

    //  Flächen nach Seite bündeln.
    QHash<int, QVector<QRectF>> byPage;
    for (const PdfRedactArea& a : areas) {
        if (a.page < 0 || a.page >= doc.pageObjs.size()) return fail("Seitenindex außerhalb");
        const QRectF r = a.rect.normalized();
        if (r.width() <= 0.0 || r.height() <= 0.0) return fail("leere Fläche");
        byPage[a.page].push_back(r);
    }

    QHash<int, NewObj> edited;
    QVector<int> touchedPages;

    for (auto pit = byPage.constBegin(); pit != byPage.constEnd(); ++pit) {
        const int page = pit.key();
        const QVector<QRectF>& rects = pit.value();

        //  ── Gedrehte Seiten ablehnen ────────────────────────────────────────
        //  Die Flächen kommen in ANZEIGE-Koordinaten; `PdfTextLayout` misst in
        //  ungedrehtem PDF-Raum. Bei /Rotate ≠ 0 passt beides nicht zusammen -
        //  lieber der Raster-Weg als ein Balken über der falschen Stelle.
        {
            int num = doc.pageObjs[page];
            for (int depth = 0; depth < 8 && num > 0; ++depth) {
                const QByteArray d = doc.dictOf(num);
                const qint64 rp = findKey(d, "Rotate");
                if (rp >= 0) {
                    const qint64 rot = intValue(d, "Rotate");
                    if (rot != 0) return fail("gedrehte Seite -> Fallback");
                    break;
                }
                num = refValue(d, "Parent");
            }
        }

        PageContent pc;
        if (!loadPageContent(doc, page, &pc, fail)) return false;

        //  ── Text der Seite vermessen ────────────────────────────────────────
        mg::PdfPageText pt;
        QString lerr;
        if (!mg::PdfTextLayout::buildForPage(inputPath, page, &pt, &lerr))
            return fail("Textebene nicht vermessbar -> Fallback");
        //  Beide Wege müssen DENSELBEN Strom sehen - sonst zeigen die
        //  Byte-Offsets der Glyphen woanders hin als der Strom, den wir neu
        //  schreiben. `contentObj` ist die harte Zusage „genau EIN Strom"
        //  (−1 = mehrteilig); die Vermessung hängt an jeden Teil noch ein
        //  Zeilenende an, deshalb wird auf ANFANG verglichen, nicht auf
        //  Gleichheit.
        if (pt.contentObj != pc.num || !pt.content.startsWith(pc.data))
            return fail("Textebene und Content-Strom weichen ab -> Fallback");

        //  ── Form-XObjects ablehnen ──────────────────────────────────────────
        //  `PdfTextLayout` steigt NICHT in XObjects hinab. Zeichnet die Seite
        //  ein Formular-XObject, könnte darin Text unter dem Balken stehen, den
        //  wir weder sehen noch entfernen - genau die Lücke, die das Werkzeug
        //  nicht haben darf.
        {
            const QByteArray res = doc.pageRes.value(page);
            const qint64 xp = findKey(res, "XObject");
            if (xp >= 0) {
                QByteArray xdict;
                if (res[xp] == '<') { const qint64 e = skipValue(res, xp);
                                      xdict = res.mid(xp + 2, (e - 2) - (xp + 2)); }
                else { const int xn = refValue(res, "XObject");
                       if (xn >= 0) xdict = doc.dictOf(xn); }
                qint64 i = 0;
                while (i < xdict.size()) {
                    while (i < xdict.size() && isWs(xdict[i])) ++i;
                    if (i >= xdict.size() || xdict[i] != '/') { ++i; continue; }
                    i = skipValue(xdict, i);                 // Name
                    const qint64 vs = i;
                    i = skipValue(xdict, vs);
                    static const QRegularExpression rre(QStringLiteral("^\\s*(\\d+)\\s+(\\d+)\\s+R"));
                    const auto m = rre.match(QString::fromLatin1(xdict.mid(vs, i - vs)));
                    if (!m.hasMatch()) continue;
                    if (nameValue(doc.dictOf(m.captured(1).toInt()), "Subtype") == "/Form")
                        return fail("Form-XObject auf der Seite -> Fallback");
                }
            }
        }

        //  ── Bild unter dem Balken -> ABLEHNEN ────────────────────────────────
        //  Text lässt sich aus dem Strom entfernen, Bildpunkte nicht: Über einem
        //  Bild wäre der Balken nur eine Decke, und das Original bliebe in der
        //  Datei. Genau der Fall einer gescannten Seite - dafür ist der
        //  Raster-Weg da, der die Punkte selbst überschreibt.
        for (const QRectF& img : pt.imagePaints)
            for (const QRectF& r : rects)
                if (!img.intersected(r).isEmpty())
                    return fail("Bild unter dem Balken -> Fallback");

        if (pt.glyphs.isEmpty())
            continue;                       // nichts Auslesbares unter dem Balken

        //  ── Betroffene Glyphen je Zeigeoperator sammeln ─────────────────────
        //  `showIndex` zeigt auf `pt.spans`; die Byte-Grenzen einer Glyphe
        //  ergeben sich aus dem Offset der NÄCHSTEN Glyphe desselben Operators
        //  (bzw. dem Ende seiner Bytes) - so stimmt es auch bei Zwei-Byte-Codes.
        QHash<int, QVector<int>> hitGlyphs;         // spanIndex -> Glyphen-Indizes
        for (int gi = 0; gi < pt.glyphs.size(); ++gi) {
            const mg::PdfGlyph& g = pt.glyphs.at(gi);
            bool covered = false;
            for (const QRectF& r : rects)
                if (glyphIsCovered(g.box, r)) { covered = true; break; }
            if (covered) hitGlyphs[g.showIndex].push_back(gi);
        }
        if (hitGlyphs.isEmpty())
            continue;

        //  Byte-Ende einer Glyphe innerhalb ihres Operators.
        auto glyphByteEnd = [&](int gi) -> qint64 {
            const mg::PdfGlyph& g = pt.glyphs.at(gi);
            if (gi + 1 < pt.glyphs.size() && pt.glyphs.at(gi + 1).showIndex == g.showIndex)
                return pt.glyphs.at(gi + 1).byteOffset;
            const mg::PdfShowSpan& sp = pt.spans.at(g.showIndex);
            return sp.bytes.size();
        };

        //  Zeigeoperatoren im ENTPACKTEN Strom (dieselben Bytes wie oben).
        const QVector<ShowHit> hits = scanTextShows(pc.data);
        QHash<qint64, int> hitByStart;
        for (int k = 0; k < hits.size(); ++k) hitByStart.insert(hits[k].start, k);

        //  Ersetzungen sammeln, danach von HINTEN einsetzen (Offsets bleiben gültig).
        struct Splice { qint64 from; qint64 to; QByteArray text; };
        QVector<Splice> splices;

        for (auto hit = hitGlyphs.constBegin(); hit != hitGlyphs.constEnd(); ++hit) {
            const int spanIdx = hit.key();
            if (spanIdx < 0 || spanIdx >= pt.spans.size()) return fail("Span-Index -> Fallback");
            const mg::PdfShowSpan& sp = pt.spans.at(spanIdx);
            const auto hi = hitByStart.constFind(sp.operandStart);
            if (hi == hitByStart.constEnd())
                return fail("Zeigeoperator nicht wiedergefunden -> Fallback");
            const ShowHit& h = hits.at(hi.value());
            if (h.bytes != sp.bytes)
                return fail("Zeigeoperator weicht ab -> Fallback");
            if (sp.tjUnitPt <= 0.0)
                return fail("Textzustand unbekannt -> Fallback");

            //  Zusammenhängende Läufe bilden (Glyphen kommen sortiert an).
            QVector<int> gidx = hit.value();
            std::sort(gidx.begin(), gidx.end());
            QVector<ByteCut> cuts;
            for (int gi : gidx) {
                const qint64 from = pt.glyphs.at(gi).byteOffset;
                const qint64 to   = glyphByteEnd(gi);
                const qreal  adv  = pt.glyphs.at(gi).box.width();
                if (to <= from || to > sp.bytes.size()) return fail("Byte-Bereich -> Fallback");
                if (!cuts.isEmpty() && cuts.last().to == from) {
                    cuts.last().to = to;
                    cuts.last().advancePt += adv;
                } else {
                    cuts.push_back({ from, to, adv });
                }
            }

            bool ok = false;
            const QByteArray repl = rebuildShowWithoutCuts(pc.data, h, cuts, sp.tjUnitPt, &ok);
            if (!ok) return fail("Zeigeoperator nicht neu schreibbar -> Fallback");
            splices.push_back({ h.start, h.opEnd, repl });
        }

        std::sort(splices.begin(), splices.end(),
                  [](const Splice& a, const Splice& b) { return a.from > b.from; });
        QByteArray nc = pc.data;
        for (const Splice& sp2 : splices)
            nc.replace(sp2.from, sp2.to - sp2.from, sp2.text);

        edited.insert(pc.num, { pc.num, pc.gen, nc });
        touchedPages.push_back(page);
    }

    if (edited.isEmpty()) {
        //  Unter den Balken stand nichts Entfernbares - das ist KEIN Fehler,
        //  aber es gibt auch nichts zu schreiben. Der Aufrufer bekommt trotzdem
        //  eine Ausgabe, damit der weitere Weg (Balken zeichnen) gleich bleibt.
        QFile::remove(outputPath);
        if (!QFile::copy(inputPath, outputPath)) return fail("Kopie fehlgeschlagen");
        return true;
    }

    if (!writeIncremental(doc, edited, outputPath, fail))
        return false;

    //  ── SELBSTPRÜFUNG an der ERGEBNISDATEI ──────────────────────────────────
    //  Die Zusage lautet „der Text ist weg". Sie wird gemessen, nicht
    //  angenommen: steht nach dem Schreiben noch eine Glyphe unter einem
    //  Balken, gilt der Lauf als gescheitert und die Ausgabe wird verworfen.
    for (int page : touchedPages) {
        QVector<mg::PdfGlyph> glyphs;
        if (!mg::PdfTextLayout::buildForPage(outputPath, page, &glyphs, nullptr)) {
            QFile::remove(outputPath);
            return fail("Ergebnis nicht nachmessbar -> Fallback");
        }
        for (const mg::PdfGlyph& g : glyphs)
            for (const QRectF& r : byPage.value(page))
                if (glyphIsCovered(g.box, r)) {
                    QFile::remove(outputPath);
                    return fail("Text steht nach dem Schwärzen noch da -> Fallback");
                }
    }
    return true;
}

}  // namespace mg
