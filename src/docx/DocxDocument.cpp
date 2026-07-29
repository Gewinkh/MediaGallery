#include "docx/DocxDocument.h"
#include "docx/DocxZip.h"

#include <QXmlStreamReader>
#include <QIODevice>
#include <QBuffer>
#include <QFile>
#include <QRegularExpression>

namespace Docx {

// ─────────────────────────────────────────────────────────────────────────────
//  Kleine XML-Helfer
// ─────────────────────────────────────────────────────────────────────────────
QString Document::xmlEscape(const QString& s) {
    QString out;
    out.reserve(s.size() + 8);
    for (const QChar c : s) {
        if      (c == QLatin1Char('&'))  out += QLatin1String("&amp;");
        else if (c == QLatin1Char('<'))  out += QLatin1String("&lt;");
        else if (c == QLatin1Char('>'))  out += QLatin1String("&gt;");
        else if (c == QLatin1Char('"'))  out += QLatin1String("&quot;");
        else                             out += c;
    }
    return out;
}

namespace {

//  Direkte Kinder eines XML-Fragments "<wrap …>…</wrap>" als (Name,Start,Länge)
//  — quote-bewusster Balancierungs-Scanner (kein voller Parser nötig; die
//  Fragmente stammen aus bereits validiertem XML). Grundlage von upsertProp,
//  damit Ersetzen/Einfügen NIE in verschachtelte Kinder (z. B. w:rPr in w:pPr)
//  hineingreift.
struct ChildRange { QString name; int start = 0; int len = 0; };

int tagEnd(const QString& xml, int lt) {                // Index von '>' des Tags
    bool inQ = false; QChar q;
    for (int i = lt + 1; i < xml.size(); ++i) {
        const QChar c = xml.at(i);
        if (inQ) { if (c == q) inQ = false; continue; }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { inQ = true; q = c; continue; }
        if (c == QLatin1Char('>')) return i;
    }
    return -1;
}

QString tagName(const QString& xml, int lt, int gt) {
    int s = lt + 1;
    if (s < xml.size() && xml.at(s) == QLatin1Char('/')) ++s;
    int e = s;
    while (e < gt && !xml.at(e).isSpace() && xml.at(e) != QLatin1Char('/')
           && xml.at(e) != QLatin1Char('>'))
        ++e;
    return xml.mid(s, e - s);
}

QList<ChildRange> childRanges(const QString& wrapped) {
    QList<ChildRange> out;
    const int outerGt = tagEnd(wrapped, 0);
    if (outerGt < 0)
        return out;
    int i = outerGt + 1;
    const int end = wrapped.lastIndexOf(QLatin1Char('<'));   // "</wrap>"
    while (i < end && i >= 0) {
        const int lt = wrapped.indexOf(QLatin1Char('<'), i);
        if (lt < 0 || lt >= end) break;
        const int gt = tagEnd(wrapped, lt);
        if (gt < 0) break;
        const QString name = tagName(wrapped, lt, gt);
        if (wrapped.at(gt - 1) == QLatin1Char('/')) {         // selbstschließend
            out.append({name, lt, gt - lt + 1});
            i = gt + 1;
            continue;
        }
        // Balanciert bis zum passenden End-Tag desselben Namens.
        int depth = 1, j = gt + 1;
        while (depth > 0) {
            const int nlt = wrapped.indexOf(QLatin1Char('<'), j);
            if (nlt < 0) { j = end; break; }
            const int ngt = tagEnd(wrapped, nlt);
            if (ngt < 0) { j = end; break; }
            const QString n2 = tagName(wrapped, nlt, ngt);
            if (n2 == name) {
                if (wrapped.at(nlt + 1) == QLatin1Char('/')) --depth;
                else if (wrapped.at(ngt - 1) != QLatin1Char('/')) ++depth;
            }
            j = ngt + 1;
        }
        out.append({name, lt, j - lt});
        i = j;
    }
    return out;
}

} // namespace

QString Document::upsertProp(const QString& prXml, const QString& wrapTag,
                             const QString& propName, const QString& newXml,
                             const QStringList& order) {
    if (prXml.isEmpty()) {
        if (newXml.isEmpty()) return {};
        return QLatin1Char('<') + wrapTag + QLatin1Char('>') + newXml
               + QLatin1String("</") + wrapTag + QLatin1Char('>');
    }
    const QList<ChildRange> kids = childRanges(prXml);
    // Vorhandenes Property ersetzen/entfernen (nur DIREKTE Kinder).
    for (const ChildRange& k : kids) {
        if (k.name == propName) {
            QString out = prXml;
            out.replace(k.start, k.len, newXml);
            return out;
        }
    }
    if (newXml.isEmpty())
        return prXml;                                    // nichts zu entfernen
    // Kanonische Position: vor dem ersten Kind, das in der Reihenfolge NACH
    // dem Property kommt; sonst vor dem schließenden Tag.
    const int myIdx = order.indexOf(propName);
    int insertAt = prXml.lastIndexOf(QLatin1Char('<')); // "</wrap>"
    for (const ChildRange& k : kids) {
        const int ki = order.indexOf(k.name);
        if (ki >= 0 && myIdx >= 0 && ki > myIdx) { insertAt = k.start; break; }
    }
    QString out = prXml;
    out.insert(insertAt, newXml);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Property-Fragmente parsen (rPr/pPr) — über einen frischen Stream-Reader auf
//  dem Teilstring (Namespace-Verarbeitung aus: wir arbeiten mit "w:"-Präfixen,
//  document.xml deklariert sie ohnehin erst am Wurzelelement).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

QString attr(const QXmlStreamReader& r, const char* name) {
    const auto attrs = r.attributes();
    for (const QXmlStreamAttribute& a : attrs)
        if (a.qualifiedName() == QLatin1String(name))
            return a.value().toString();
    return {};
}

bool onOffAttr(const QXmlStreamReader& r) {              // w:b/w:i: val fehlt = an
    const QString v = attr(r, "w:val");
    return !(v == QLatin1String("0") || v == QLatin1String("false")
             || v == QLatin1String("none") || v == QLatin1String("off"));
}

} // namespace

void Document::parseRunProps(QStringView xml, RunFmt* out) {
    QXmlStreamReader r(xml.toString());
    r.setNamespaceProcessing(false);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        const auto n = r.qualifiedName();
        if (n == QLatin1String("w:rPr")) continue;       // Wrapper selbst
        if (n == QLatin1String("w:b")) {
            out->bold = onOffAttr(r); out->set |= RunFmt::FBold;
        } else if (n == QLatin1String("w:i")) {
            out->italic = onOffAttr(r); out->set |= RunFmt::FItalic;
        } else if (n == QLatin1String("w:u")) {
            const QString v = attr(r, "w:val");
            out->underline = (v != QLatin1String("none"));
            out->set |= RunFmt::FUnderline;
        } else if (n == QLatin1String("w:sz")) {
            const qreal hp = attr(r, "w:val").toDouble();
            if (hp > 0) { out->sizePt = hp / 2.0; out->set |= RunFmt::FSize; }
        } else if (n == QLatin1String("w:rFonts")) {
            QString f = attr(r, "w:ascii");
            if (f.isEmpty()) f = attr(r, "w:hAnsi");
            if (f.isEmpty()) f = attr(r, "w:cs");
            if (!f.isEmpty()) { out->font = f; out->set |= RunFmt::FFont; }
        } else if (n == QLatin1String("w:color")) {
            const QString v = attr(r, "w:val");
            if (!v.isEmpty() && v != QLatin1String("auto")) {
                QColor c(QLatin1Char('#') + v);
                if (c.isValid()) { out->color = c; out->set |= RunFmt::FColor; }
            }
        }
        r.skipCurrentElement();
    }
}

void Document::parseParProps(QStringView xml, ParFmt* out) {
    QXmlStreamReader r(xml.toString());
    r.setNamespaceProcessing(false);
    int depth = 0;
    while (!r.atEnd()) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::EndElement) { --depth; continue; }
        if (tok != QXmlStreamReader::StartElement) continue;
        ++depth;
        const auto n = r.qualifiedName();
        if (n == QLatin1String("w:pPr")) continue;
        if (n == QLatin1String("w:rPr")) { r.skipCurrentElement(); --depth; continue; }
        if (n == QLatin1String("w:jc")) {
            const QString v = attr(r, "w:val");
            if      (v == QLatin1String("center"))                            out->align = 1;
            else if (v == QLatin1String("right") || v == QLatin1String("end")) out->align = 2;
            else if (v == QLatin1String("both") || v == QLatin1String("distribute")
                     || v == QLatin1String("justify"))                         out->align = 3;
            else                                                               out->align = 0;
            out->set |= ParFmt::FAlign;
        } else if (n == QLatin1String("w:spacing")) {
            const QString before = attr(r, "w:before");
            const QString after  = attr(r, "w:after");
            const QString line   = attr(r, "w:line");
            const QString rule   = attr(r, "w:lineRule");
            if (!before.isEmpty()) { out->beforePt = before.toDouble() / 20.0; out->set |= ParFmt::FBefore; }
            if (!after.isEmpty())  { out->afterPt  = after.toDouble()  / 20.0; out->set |= ParFmt::FAfter; }
            if (!line.isEmpty() && (rule.isEmpty() || rule == QLatin1String("auto"))) {
                const qreal m = line.toDouble() / 240.0;
                if (m > 0.1) { out->lineSpacing = m; out->set |= ParFmt::FLine; }
            }
        } else if (n == QLatin1String("w:numPr")) {
            // Kinder: w:ilvl, w:numId
            continue;                                     // Kinder unten lesen
        } else if (n == QLatin1String("w:ilvl")) {
            out->ilvl = attr(r, "w:val").toInt();
            out->set |= ParFmt::FNum;
            continue;
        } else if (n == QLatin1String("w:numId")) {
            out->numId = attr(r, "w:val").toInt();
            out->set |= ParFmt::FNum;
            continue;
        } else if (n == QLatin1String("w:pStyle")) {
            out->styleId = attr(r, "w:val");
        }
        if (n != QLatin1String("w:numPr")) { r.skipCurrentElement(); --depth; }
    }
    if ((out->set & ParFmt::FNum) && out->numId <= 0) {   // numId 0 = keine Liste
        out->set &= ~ParFmt::FNum;
        out->numId = -1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Laden
// ─────────────────────────────────────────────────────────────────────────────
bool Document::load(const QString& path, QString* err) {
    m_path = path;
    blocks.clear();
    m_styles.clear();
    m_numLevels.clear();
    m_numToAbstract.clear();
    m_pendingNums.clear();
    m_numberingXml.clear();
    m_hadNumberingPart = false;

    DocxZip::Reader zip;
    if (!zip.open(path, err))
        return false;

    bool ok = false;
    const QByteArray docBytes = zip.fileData(QStringLiteral("word/document.xml"), &ok);
    if (!ok) {
        if (err) *err = QStringLiteral("word/document.xml fehlt oder ist defekt.");
        return false;
    }
    //  UTF-8-Roundtrip-Garantie: nur wenn Dekodieren+Re-Enkodieren byteidentisch
    //  ist, dürfen wir in QChar-Spans arbeiten (Verlusterhaltung!). UTF-16-
    //  Exoten fallen hier bewusst mit Fehler heraus statt still zu mutieren.
    m_docXml = QString::fromUtf8(docBytes);
    if (m_docXml.toUtf8() != docBytes) {
        if (err) *err = QStringLiteral("document.xml ist nicht UTF-8 — Bearbeitung nicht möglich.");
        return false;
    }

    // Standard-Formate + Formatvorlagen (nur Anzeige-Auflösung).
    m_defRun = RunFmt();
    m_defRun.set = RunFmt::FBold | RunFmt::FItalic | RunFmt::FUnderline
                   | RunFmt::FSize | RunFmt::FFont | RunFmt::FColor;
    m_defRun.sizePt = 11.0;
    m_defRun.font   = QStringLiteral("Calibri");
    m_defRun.color  = QColor(0, 0, 0);
    m_defPar = ParFmt();
    {
        bool sOk = false;
        const QByteArray styles = zip.fileData(QStringLiteral("word/styles.xml"), &sOk);
        if (sOk) parseStylesXml(styles);
    }
    {
        bool nOk = false;
        const QByteArray num = zip.fileData(QStringLiteral("word/numbering.xml"), &nOk);
        if (nOk) {
            m_hadNumberingPart = true;
            m_numberingXml = QString::fromUtf8(num);
            parseNumberingXml(num);
        }
    }
    zip.close();

    return parseDocumentXml(err);
}

// Absoluter Span-Rekorder über QXmlStreamReader-Token: jedes Token belegt
// [Offset nach dem Vorgänger-Token, characterOffset() nach readNext()).
bool Document::parseDocumentXml(QString* err) {
    QXmlStreamReader r(m_docXml);
    r.setNamespaceProcessing(false);

    qint64 tokStart = 0, tokEnd = 0;
    auto next = [&]() {
        tokStart = tokEnd;
        const auto t = r.readNext();
        tokEnd = r.characterOffset();
        return t;
    };

    // Bis in den <w:body> laufen.
    int bodyContentStart = -1;
    while (!r.atEnd()) {
        const auto t = next();
        if (t == QXmlStreamReader::StartElement
            && r.qualifiedName() == QLatin1String("w:body")) {
            bodyContentStart = int(tokEnd);
            break;
        }
    }
    if (bodyContentStart < 0) {
        if (err) *err = QStringLiteral("Kein <w:body> gefunden.");
        return false;
    }
    m_bodyPrefix = { 0, bodyContentStart };

    // Sub-Parser für einen Absatz: arbeitet auf dem TEILSTRING des Absatzes
    // (frischer Reader), rechnet alle Spans auf absolute Offsets um.
    auto parseParagraph = [&](int absStart, int absLen) -> Block {
        Block b;
        b.kind    = Block::Paragraph;
        b.rawSpan = { absStart, absLen };
        const QString frag = m_docXml.mid(absStart, absLen);
        QXmlStreamReader pr(frag);
        pr.setNamespaceProcessing(false);

        qint64 pStart = 0, pEnd = 0;
        auto pnext = [&]() {
            pStart = pEnd;
            const auto t = pr.readNext();
            pEnd = pr.characterOffset();
            return t;
        };

        // Erstes Token = StartElement <w:p …>. WICHTIG: Sub-Reader auf
        // Fragmenten (ohne XML-Deklaration) melden für das StartDocument-
        // Token verschobene Offsets — der Start-Tag beginnt aber per
        // Konstruktion bei Fragment-Offset 0; nur das Token-ENDE ist
        // verlässlich (die Folge-Token sind wieder exakt).
        while (!pr.atEnd()) {
            if (pnext() == QXmlStreamReader::StartElement) break;
        }
        b.startTagSpan = { absStart, int(pEnd) };

        // Sub-Sub-Parser für einen Run (analoge Technik).
        auto parseRun = [&](int rAbs, int rLen) -> Run {
            Run run;
            run.rawSpan = { rAbs, rLen };
            const QString rf = m_docXml.mid(rAbs, rLen);
            QXmlStreamReader rr(rf);
            rr.setNamespaceProcessing(false);
            qint64 s = 0, e = 0;
            auto rnext = [&]() {
                s = e;
                const auto t = rr.readNext();
                e = rr.characterOffset();
                return t;
            };
            while (!rr.atEnd()) {
                if (rnext() == QXmlStreamReader::StartElement) break;
            }
            run.startTagSpan = { rAbs, int(e) };     // s. Absatz-Kommentar (Skew)
            while (!rr.atEnd()) {
                const auto t = rnext();
                if (t == QXmlStreamReader::EndElement
                    && rr.qualifiedName() == QLatin1String("w:r"))
                    break;
                if (t != QXmlStreamReader::StartElement)
                    continue;
                const auto n = rr.qualifiedName();
                if (n == QLatin1String("w:rPr")) {
                    const int ps = int(s);
                    rr.skipCurrentElement();
                    e = rr.characterOffset();
                    run.rprSpan = { rAbs + ps, int(e - ps) };
                    parseRunProps(QStringView(m_docXml).mid(run.rprSpan.start,
                                                            run.rprSpan.len),
                                  &run.fmt);
                } else if (n == QLatin1String("w:t")) {
                    run.text += rr.readElementText();
                    e = rr.characterOffset();
                } else if (n == QLatin1String("w:tab")) {
                    run.text += QLatin1Char('\t');
                    rr.skipCurrentElement(); e = rr.characterOffset();
                } else if (n == QLatin1String("w:br")) {
                    const bool page = attr(rr, "w:type") == QLatin1String("page");
                    run.text += page ? kPageBreak : kLineBreak;
                    rr.skipCurrentElement(); e = rr.characterOffset();
                } else if (n == QLatin1String("w:cr")) {
                    run.text += kLineBreak;
                    rr.skipCurrentElement(); e = rr.characterOffset();
                } else if (n == QLatin1String("w:noBreakHyphen")) {
                    run.text += QChar(0x2011);
                    rr.skipCurrentElement(); e = rr.characterOffset();
                } else if (n == QLatin1String("w:softHyphen")) {
                    run.text += QChar(0x00AD);
                    rr.skipCurrentElement(); e = rr.characterOffset();
                } else {
                    // Unverstandener Run-Inhalt (Zeichnung, Feld, Objekt …):
                    // der GANZE Run bleibt verbatim erhalten; Anzeige = ein
                    // atomares Platzhalter-Zeichen.
                    run.opaque = true;
                    run.text   = QString(kObjectChar);
                    break;
                }
            }
            return run;
        };

        // Kinder des Absatzes.
        while (!pr.atEnd()) {
            const auto t = pnext();
            if (t == QXmlStreamReader::EndElement
                && pr.qualifiedName() == QLatin1String("w:p"))
                break;
            if (t == QXmlStreamReader::Comment
                || t == QXmlStreamReader::ProcessingInstruction
                || (t == QXmlStreamReader::Characters && pr.isWhitespace())) {
                Run filler;                               // Bytes erhalten, unsichtbar
                filler.opaque  = true;
                filler.rawSpan = { absStart + int(pStart), int(pEnd - pStart) };
                b.runs.append(filler);
                continue;
            }
            if (t != QXmlStreamReader::StartElement)
                continue;
            const auto n  = pr.qualifiedName();
            const int  cs = int(pStart);
            if (n == QLatin1String("w:pPr")) {
                pr.skipCurrentElement();
                pEnd = pr.characterOffset();
                b.pprSpan = { absStart + cs, int(pEnd - cs) };
                parseParProps(QStringView(m_docXml).mid(b.pprSpan.start, b.pprSpan.len),
                              &b.pfmt);
            } else if (n == QLatin1String("w:r")) {
                pr.skipCurrentElement();
                pEnd = pr.characterOffset();
                b.runs.append(parseRun(absStart + cs, int(pEnd - cs)));
            } else {
                //  Hyperlink/Feld/Bookmark/ins/del/… — als opaker atomarer Run
                //  erhalten; sichtbaren Text (innere <w:t>) für die Anzeige
                //  extrahieren (z. B. Hyperlink-Beschriftung).
                Run op;
                op.opaque = true;
                const int fs = cs;
                //  Text einsammeln, bis das Element geschlossen ist.
                int depth = 1;
                while (!pr.atEnd() && depth > 0) {
                    const auto t2 = pnext();
                    if (t2 == QXmlStreamReader::StartElement) {
                        ++depth;
                        if (pr.qualifiedName() == QLatin1String("w:t")) {
                            op.text += pr.readElementText();
                            pEnd = pr.characterOffset();
                            --depth;
                        }
                    } else if (t2 == QXmlStreamReader::EndElement) {
                        --depth;
                    }
                }
                op.rawSpan = { absStart + fs, int(pEnd - fs) };
                if (op.text.isEmpty()
                    && (n == QLatin1String("w:fldSimple")
                        || n == QLatin1String("w:object")
                        || n == QLatin1String("w:drawing")))
                    op.text = QString(kObjectChar);
                b.runs.append(op);
            }
        }
        return b;
    };

    // Body-Kinder.
    int bodyContentEnd = -1;
    while (!r.atEnd()) {
        const auto t = next();
        if (t == QXmlStreamReader::EndElement
            && r.qualifiedName() == QLatin1String("w:body")) {
            bodyContentEnd = int(tokStart);
            break;
        }
        if (t == QXmlStreamReader::Comment
            || t == QXmlStreamReader::ProcessingInstruction
            || (t == QXmlStreamReader::Characters && r.isWhitespace())) {
            Block filler;
            filler.kind    = Block::OpaqueHidden;
            filler.rawSpan = { int(tokStart), int(tokEnd - tokStart) };
            blocks.append(filler);
            continue;
        }
        if (t != QXmlStreamReader::StartElement)
            continue;
        const QString name = r.qualifiedName().toString();
        const int bs = int(tokStart);
        r.skipCurrentElement();
        tokEnd = r.characterOffset();
        const int blen = int(tokEnd) - bs;
        if (name == QLatin1String("w:p")) {
            blocks.append(parseParagraph(bs, blen));
        } else {
            Block ob;
            ob.kind = (name == QLatin1String("w:tbl")
                       || name == QLatin1String("w:sdt")
                       || name == QLatin1String("w:altChunk"))
                          ? Block::OpaqueVisible : Block::OpaqueHidden;
            ob.rawSpan    = { bs, blen };
            ob.opaqueName = name;
            blocks.append(ob);
        }
    }
    if (bodyContentEnd < 0 || r.hasError()) {
        if (err) *err = QStringLiteral("document.xml ist beschädigt (%1).")
                            .arg(r.errorString());
        return false;
    }
    m_bodySuffix = { bodyContentEnd, int(m_docXml.size()) - bodyContentEnd };

    //  SELBSTPRÜFUNG (Verlusterhaltungs-Garantie): Prefix + Block-Spans +
    //  Suffix müssen das Original EXAKT rekonstruieren. Bei Abweichung wird
    //  das Laden verweigert (lieber gar nicht editieren als still verlieren).
    {
        QString rebuilt;
        rebuilt.reserve(m_docXml.size());
        rebuilt += QStringView(m_docXml).mid(m_bodyPrefix.start, m_bodyPrefix.len);
        for (const Block& b : std::as_const(blocks))
            rebuilt += QStringView(m_docXml).mid(b.rawSpan.start, b.rawSpan.len);
        rebuilt += QStringView(m_docXml).mid(m_bodySuffix.start, m_bodySuffix.len);
        if (rebuilt != m_docXml) {
            if (err) *err = QStringLiteral("Struktur-Selbstprüfung fehlgeschlagen — "
                                           "Datei wird zum Schutz nicht bearbeitet.");
            return false;
        }
    }
    //  SELBSTPRÜFUNG, Stufe 2 (Absatz-Innenstruktur): StartTag + pPr + Runs
    //  (+ „</w:p>") müssen JEDEN Absatz exakt rekonstruieren — exakt die
    //  Fragmente, die ein Dirty-Neuaufbau verbatim wiederverwendet. Damit ist
    //  garantiert, dass buildParagraphXml nie Bytes erfindet oder verliert.
    for (const Block& b : std::as_const(blocks)) {
        if (b.kind != Block::Paragraph)
            continue;
        const QString raw = m_docXml.mid(b.rawSpan.start, b.rawSpan.len);
        QString inner = m_docXml.mid(b.startTagSpan.start, b.startTagSpan.len);
        if (b.pprSpan.valid())
            inner += QStringView(m_docXml).mid(b.pprSpan.start, b.pprSpan.len);
        for (const Run& rn : b.runs)
            inner += QStringView(m_docXml).mid(rn.rawSpan.start, rn.rawSpan.len);
        const bool selfClosing = inner.endsWith(QLatin1String("/>"))
                                 && b.runs.isEmpty() && !b.pprSpan.valid()
                                 && b.startTagSpan.len == b.rawSpan.len;
        const bool ok = selfClosing
                            ? (inner == raw)
                            : (raw.startsWith(inner)
                               && raw.mid(inner.size()) == QLatin1String("</w:p>"));
        if (!ok) {
            if (err) *err = QStringLiteral("Absatz-Selbstprüfung fehlgeschlagen — "
                                           "Datei wird zum Schutz nicht bearbeitet.");
            return false;
        }
    }

    // Nummerierungs-IDs für neue Listen hinter dem Bestand beginnen.
    int maxNum = 0, maxAbs = -1;
    for (auto it = m_numToAbstract.constBegin(); it != m_numToAbstract.constEnd(); ++it) {
        maxNum = qMax(maxNum, it.key());
        maxAbs = qMax(maxAbs, it.value());
    }
    m_nextNumId      = maxNum + 1;
    m_nextAbstractId = maxAbs + 1;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  styles.xml (nur Anzeige): docDefaults + Vorlagen-Kette (basedOn)
// ─────────────────────────────────────────────────────────────────────────────
bool Document::parseStylesXml(const QByteArray& xml) {
    QXmlStreamReader r(QString::fromUtf8(xml));
    r.setNamespaceProcessing(false);
    QString curStyle;
    while (!r.atEnd()) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::EndElement
            && r.qualifiedName() == QLatin1String("w:style"))
            curStyle.clear();
        if (t != QXmlStreamReader::StartElement)
            continue;
        const auto n = r.qualifiedName();
        auto readFragment = [&]() -> QString {            // Element roh mitschneiden
            QString frag = QLatin1Char('<') + r.qualifiedName().toString() + QLatin1Char('>');
            int depth = 1;
            while (!r.atEnd() && depth > 0) {
                const auto t2 = r.readNext();
                if (t2 == QXmlStreamReader::StartElement) {
                    ++depth;
                    frag += QLatin1Char('<') + r.qualifiedName().toString();
                    const auto attrs = r.attributes();
                    for (const QXmlStreamAttribute& a : attrs)
                        frag += QLatin1Char(' ') + a.qualifiedName().toString()
                                + QLatin1String("=\"") + xmlEscape(a.value().toString())
                                + QLatin1Char('"');
                    frag += QLatin1Char('>');
                } else if (t2 == QXmlStreamReader::EndElement) {
                    --depth;
                    frag += QLatin1String("</") + r.qualifiedName().toString() + QLatin1Char('>');
                }
            }
            return frag;
        };
        if (n == QLatin1String("w:docDefaults")) {
            continue;
        } else if (n == QLatin1String("w:rPrDefault") || n == QLatin1String("w:pPrDefault")) {
            const bool isRun = (n == QLatin1String("w:rPrDefault"));
            const QString frag = readFragment();
            if (isRun) {
                RunFmt f;
                parseRunProps(frag, &f);
                if (f.set & RunFmt::FSize)  m_defRun.sizePt = f.sizePt;
                if (f.set & RunFmt::FFont)  m_defRun.font   = f.font;
                if (f.set & RunFmt::FColor) m_defRun.color  = f.color;
                if (f.set & RunFmt::FBold)  m_defRun.bold   = f.bold;
            } else {
                ParFmt p;
                parseParProps(frag, &p);
                if (p.set & ParFmt::FAlign)  m_defPar.align       = p.align;
                if (p.set & ParFmt::FLine)   { m_defPar.lineSpacing = p.lineSpacing; m_defPar.set |= ParFmt::FLine; }
                if (p.set & ParFmt::FBefore) { m_defPar.beforePt  = p.beforePt; m_defPar.set |= ParFmt::FBefore; }
                if (p.set & ParFmt::FAfter)  { m_defPar.afterPt   = p.afterPt;  m_defPar.set |= ParFmt::FAfter; }
            }
        } else if (n == QLatin1String("w:style")) {
            curStyle = attr(r, "w:styleId");
            if (!curStyle.isEmpty())
                m_styles.insert(curStyle, StyleDef());
        } else if (!curStyle.isEmpty() && n == QLatin1String("w:basedOn")) {
            m_styles[curStyle].basedOn = attr(r, "w:val");
        } else if (!curStyle.isEmpty() && n == QLatin1String("w:rPr")) {
            parseRunProps(readFragment(), &m_styles[curStyle].rf);
        } else if (!curStyle.isEmpty() && n == QLatin1String("w:pPr")) {
            parseParProps(readFragment(), &m_styles[curStyle].pf);
        }
    }

    //  Merken, ob ueberhaupt EINE Vorlage eine Nummerierung mitbringt. Ist das
    //  nicht der Fall (der Normalfall), kann resolvePar(b).numId nur aus dem
    //  Absatz selbst stammen — die Listenmarker-Berechnung darf dann jeden
    //  Absatz ohne eigenes w:numPr per Bit-Test ueberspringen, statt fuer ihn
    //  die komplette Vorlagenkette abzulaufen (s. stylesMayNumber()).
    m_stylesMayNumber = (m_defPar.set & ParFmt::FNum) != 0;
    for (auto it = m_styles.cbegin(); !m_stylesMayNumber && it != m_styles.cend(); ++it)
        m_stylesMayNumber = (it.value().pf.set & ParFmt::FNum) != 0;

    return !r.hasError();
}

bool Document::parseNumberingXml(const QByteArray& xml) {
    QXmlStreamReader r(QString::fromUtf8(xml));
    r.setNamespaceProcessing(false);
    QHash<int, QHash<int, NumLevel>> abstractLevels;
    int curAbstract = -1, curLvl = -1;
    while (!r.atEnd()) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::EndElement) {
            if (r.qualifiedName() == QLatin1String("w:abstractNum")) curAbstract = -1;
            else if (r.qualifiedName() == QLatin1String("w:lvl"))    curLvl = -1;
            continue;
        }
        if (t != QXmlStreamReader::StartElement)
            continue;
        const auto n = r.qualifiedName();
        if (n == QLatin1String("w:abstractNum")) {
            curAbstract = attr(r, "w:abstractNumId").toInt();
        } else if (n == QLatin1String("w:lvl") && curAbstract >= 0) {
            curLvl = attr(r, "w:ilvl").toInt();
        } else if (n == QLatin1String("w:numFmt") && curAbstract >= 0 && curLvl >= 0) {
            abstractLevels[curAbstract][curLvl].numFmt = attr(r, "w:val");
        } else if (n == QLatin1String("w:lvlText") && curAbstract >= 0 && curLvl >= 0) {
            abstractLevels[curAbstract][curLvl].lvlText = attr(r, "w:val");
        } else if (n == QLatin1String("w:num")) {
            const int numId = attr(r, "w:numId").toInt();
            // Kind w:abstractNumId folgt.
            while (!r.atEnd()) {
                const auto t2 = r.readNext();
                if (t2 == QXmlStreamReader::StartElement
                    && r.qualifiedName() == QLatin1String("w:abstractNumId")) {
                    m_numToAbstract.insert(numId, attr(r, "w:val").toInt());
                } else if (t2 == QXmlStreamReader::EndElement
                           && r.qualifiedName() == QLatin1String("w:num")) {
                    break;
                }
            }
        }
    }
    for (auto it = m_numToAbstract.constBegin(); it != m_numToAbstract.constEnd(); ++it)
        m_numLevels.insert(it.key(), abstractLevels.value(it.value()));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Anzeige-Auflösung
// ─────────────────────────────────────────────────────────────────────────────
RunFmt Document::resolveRun(const Block& b, const Run& r) const {
    RunFmt out = m_defRun;
    //  Vorlagen-Kette des Absatzstils (max. 8 Stufen, Zyklus-Schutz).
    //  Feste Stack-Reihung statt QList: resolveRun/resolvePar laufen im
    //  heissesten Pfad des Editors (je Run beim Layout, je Absatz beim
    //  Markeraufbau) — die QList kostete dort eine Heap-Allokation pro Aufruf.
    const StyleDef* chain[8];
    int depth = 0;
    QString id = b.pfmt.styleId;      // implizit geteilt → keine Kopie
    while (depth < 8 && !id.isEmpty()) {
        auto it = m_styles.constFind(id);
        if (it == m_styles.constEnd()) break;
        chain[depth++] = &it.value();
        id = it->basedOn;
    }
    auto apply = [&out](const RunFmt& f) {
        if (f.set & RunFmt::FBold)      out.bold      = f.bold;
        if (f.set & RunFmt::FItalic)    out.italic    = f.italic;
        if (f.set & RunFmt::FUnderline) out.underline = f.underline;
        if (f.set & RunFmt::FSize)      out.sizePt    = f.sizePt;
        if (f.set & RunFmt::FFont)      out.font      = f.font;
        if (f.set & RunFmt::FColor)     out.color     = f.color;
    };
    //  Basis zuerst, abgeleitete Vorlage zuletzt (frueher: prepend + Vorwaertslauf).
    for (int i = depth - 1; i >= 0; --i) apply(chain[i]->rf);
    apply(r.fmt);
    return out;
}

ParFmt Document::resolvePar(const Block& b) const {
    ParFmt out = m_defPar;
    out.styleId = b.pfmt.styleId;
    const StyleDef* chain[8];
    int depth = 0;
    QString id = b.pfmt.styleId;      // implizit geteilt → keine Kopie
    while (depth < 8 && !id.isEmpty()) {
        auto it = m_styles.constFind(id);
        if (it == m_styles.constEnd()) break;
        chain[depth++] = &it.value();
        id = it->basedOn;
    }
    auto apply = [&out](const ParFmt& p) {
        if (p.set & ParFmt::FAlign)  { out.align       = p.align;       out.set |= ParFmt::FAlign; }
        if (p.set & ParFmt::FLine)   { out.lineSpacing = p.lineSpacing; out.set |= ParFmt::FLine; }
        if (p.set & ParFmt::FBefore) { out.beforePt    = p.beforePt;    out.set |= ParFmt::FBefore; }
        if (p.set & ParFmt::FAfter)  { out.afterPt     = p.afterPt;     out.set |= ParFmt::FAfter; }
        if (p.set & ParFmt::FNum)    { out.numId       = p.numId;
                                       out.ilvl        = p.ilvl;        out.set |= ParFmt::FNum; }
    };
    //  Basis zuerst, abgeleitete Vorlage zuletzt (frueher: prepend + Vorwaertslauf).
    for (int i = depth - 1; i >= 0; --i) apply(chain[i]->pf);
    apply(b.pfmt);
    return out;
}

NumLevel Document::numLevel(int numId, int ilvl) const {
    NumLevel lv = m_numLevels.value(numId).value(ilvl);
    if (lv.numFmt.isEmpty()) {
        // Eigene, noch ungespeicherte Listen + unbekannte Bestände.
        for (const auto& p : m_pendingNums) {
            if (p.first == numId) {
                lv.numFmt  = p.second ? QStringLiteral("bullet") : QStringLiteral("decimal");
                lv.lvlText = p.second ? QStringLiteral("\u2022") : QStringLiteral("%1.");
                return lv;
            }
        }
        lv.numFmt  = QStringLiteral("decimal");
        lv.lvlText = QStringLiteral("%1.");
    }
    return lv;
}

int Document::newListNum(bool bullet) {
    const int id = m_nextNumId++;
    m_pendingNums.append({ id, bullet });
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Serialisierung — nur GEÄNDERTE Knoten entstehen neu; alles Unberührte
//  stammt verbatim aus dem Original (Spans) bzw. materialisierten Fragmenten.
// ─────────────────────────────────────────────────────────────────────────────
QString Document::serializeRunsText(const QString& text) {
    QString out;
    QString seg;
    auto flush = [&]() {
        if (seg.isEmpty()) return;
        // xml:space="preserve" immer setzen — führende/mehrfache Leerzeichen
        // überleben so jeden Konsumenten (Word-üblich).
        out += QLatin1String("<w:t xml:space=\"preserve\">")
               + xmlEscape(seg) + QLatin1String("</w:t>");
        seg.clear();
    };
    for (const QChar c : text) {
        if (c == QLatin1Char('\t'))      { flush(); out += QLatin1String("<w:tab/>"); }
        else if (c == kLineBreak)        { flush(); out += QLatin1String("<w:br/>"); }
        else if (c == kPageBreak)        { flush(); out += QLatin1String("<w:br w:type=\"page\"/>"); }
        else if (c == kObjectChar)       { /* atomare Platzhalter nie serialisieren */ }
        else                             seg += c;
    }
    flush();
    return out;
}

QString Document::buildRPrXml(const RunFmt& f) const {
    QString props;
    if (f.set & RunFmt::FFont)
        props += QStringLiteral("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\" w:cs=\"%1\"/>")
                     .arg(xmlEscape(f.font));
    if ((f.set & RunFmt::FBold) && f.bold)           props += QLatin1String("<w:b/>");
    if ((f.set & RunFmt::FItalic) && f.italic)       props += QLatin1String("<w:i/>");
    if (f.set & RunFmt::FColor)
        props += QStringLiteral("<w:color w:val=\"%1\"/>")
                     .arg(f.color.name(QColor::HexRgb).mid(1).toUpper());
    if (f.set & RunFmt::FSize) {
        const int hp = qRound(f.sizePt * 2.0);
        props += QStringLiteral("<w:sz w:val=\"%1\"/><w:szCs w:val=\"%1\"/>").arg(hp);
    }
    if ((f.set & RunFmt::FUnderline) && f.underline)
        props += QLatin1String("<w:u w:val=\"single\"/>");
    if (props.isEmpty())
        return {};
    return QLatin1String("<w:rPr>") + props + QLatin1String("</w:rPr>");
}

// Selbstschließenden Start-Tag ("<w:p/>", "<w:r a=\"b\"/>") in einen offenen
// Tag normalisieren — nötig, sobald ein leerer Absatz/Run Inhalt bekommt und
// mit explizitem End-Tag neu aufgebaut wird.
static QString openedStartTag(QString tag) {
    if (tag.endsWith(QLatin1String("/>")))
        tag.replace(tag.size() - 2, 2, QStringLiteral(">"));
    return tag;
}

QString Document::buildRunXml(const Run& r) const {
    if (r.opaque)                                     // nie neu erzeugen
        return QStringView(m_docXml).mid(r.rawSpan.start, r.rawSpan.len).toString();
    QString out;
    if (r.startTagSpan.valid())
        out += openedStartTag(
            m_docXml.mid(r.startTagSpan.start, r.startTagSpan.len));
    else
        out += QLatin1String("<w:r>");
    out += r.currentRpr(m_docXml);
    out += serializeRunsText(r.text);
    out += QLatin1String("</w:r>");
    return out;
}

QString Document::buildParagraphXml(const Block& b) const {
    QString out;
    if (b.startTagSpan.valid())
        out += openedStartTag(
            m_docXml.mid(b.startTagSpan.start, b.startTagSpan.len));
    else
        out += QLatin1String("<w:p>");
    out += b.currentPpr(m_docXml);
    for (const Run& r : b.runs) {
        if (r.opaque || (!r.dirty && r.rawSpan.valid()))
            out += QStringView(m_docXml).mid(r.rawSpan.start, r.rawSpan.len);
        else
            out += buildRunXml(r);
    }
    out += QLatin1String("</w:p>");
    return out;
}

QString Document::newDocumentXml() const {
    QString out;
    out.reserve(m_docXml.size() + 1024);
    out += QStringView(m_docXml).mid(m_bodyPrefix.start, m_bodyPrefix.len);
    for (const Block& b : blocks) {
        if (b.kind != Block::Paragraph || (!b.dirty && !b.pprMaterialized
                                           && b.rawSpan.valid()))
            out += QStringView(m_docXml).mid(b.rawSpan.start, b.rawSpan.len);
        else
            out += buildParagraphXml(b);
    }
    out += QStringView(m_docXml).mid(m_bodySuffix.start, m_bodySuffix.len);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Listen-Infrastruktur beim Speichern (numbering.xml + ContentType + Rel)
// ─────────────────────────────────────────────────────────────────────────────
namespace {

QString abstractNumXml(int absId, bool bullet) {
    return QStringLiteral(
        "<w:abstractNum w:abstractNumId=\"%1\">"
        "<w:multiLevelType w:val=\"singleLevel\"/>"
        "<w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/>"
        "<w:numFmt w:val=\"%2\"/><w:lvlText w:val=\"%3\"/>"
        "<w:lvlJc w:val=\"left\"/>"
        "<w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr>"
        "</w:lvl></w:abstractNum>")
        .arg(absId)
        .arg(bullet ? QStringLiteral("bullet") : QStringLiteral("decimal"))
        .arg(bullet ? QStringLiteral("\u2022") : QStringLiteral("%1."));
}

const char* kNumberingContentType =
    "application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml";
const char* kNumberingRelType =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering";

} // namespace

QHash<QString, QByteArray> Document::replacementParts() const {
    QHash<QString, QByteArray> out;
    if (m_pendingNums.isEmpty())
        return out;

    // Eigene abstractNum-Definitionen (je Art höchstens EINE, geteilt).
    QString absDefs, numDefs;
    int absBullet = m_ownAbstractBullet, absDecimal = m_ownAbstractDecimal;
    int nextAbs = m_nextAbstractId;
    for (const auto& p : m_pendingNums) {
        int& absRef = p.second ? absBullet : absDecimal;
        if (absRef < 0) {
            absRef = nextAbs++;
            absDefs += abstractNumXml(absRef, p.second);
        }
        numDefs += QStringLiteral("<w:num w:numId=\"%1\">"
                                  "<w:abstractNumId w:val=\"%2\"/></w:num>")
                       .arg(p.first).arg(absRef);
    }
    m_ownAbstractBullet  = absBullet;
    m_ownAbstractDecimal = absDecimal;

    if (m_hadNumberingPart) {
        //  Gezieltes Splicing in den Bestand: abstractNum VOR das erste
        //  <w:num> (Schema-Reihenfolge!), num vor </w:numbering> — alles
        //  Übrige bleibt verbatim.
        QString xml = m_numberingXml;
        int numPos = xml.indexOf(QLatin1String("<w:num "));
        const int endPos = xml.lastIndexOf(QLatin1String("</w:numbering>"));
        if (numPos < 0) numPos = endPos;
        if (numPos >= 0)
            xml.insert(numPos, absDefs);
        const int endPos2 = xml.lastIndexOf(QLatin1String("</w:numbering>"));
        if (endPos2 >= 0)
            xml.insert(endPos2, numDefs);
        out.insert(QStringLiteral("word/numbering.xml"), xml.toUtf8());
        return out;
    }

    //  numbering.xml existiert noch nicht → komplette Teile-Kette anlegen.
    const QString numberingXml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "%1%2</w:numbering>").arg(absDefs, numDefs);
    out.insert(QStringLiteral("word/numbering.xml"), numberingXml.toUtf8());

    //  [Content_Types].xml: Override ergänzen (vor </Types>), falls fehlend.
    //  word/_rels/document.xml.rels: Relationship ergänzen (vor </Relationships>).
    DocxZip::Reader zip;
    if (zip.open(m_path)) {
        bool ok = false;
        QByteArray ct = zip.fileData(QStringLiteral("[Content_Types].xml"), &ok);
        if (ok && !ct.contains("/word/numbering.xml")) {
            QString s = QString::fromUtf8(ct);
            const int p = s.lastIndexOf(QLatin1String("</Types>"));
            if (p >= 0) {
                s.insert(p, QStringLiteral("<Override PartName=\"/word/numbering.xml\" "
                                           "ContentType=\"%1\"/>")
                                .arg(QLatin1String(kNumberingContentType)));
                out.insert(QStringLiteral("[Content_Types].xml"), s.toUtf8());
            }
        }
        const QString relsName = QStringLiteral("word/_rels/document.xml.rels");
        QByteArray rels = zip.fileData(relsName, &ok);
        if (ok) {
            if (!rels.contains("relationships/numbering")) {
                QString s = QString::fromUtf8(rels);
                const int p = s.lastIndexOf(QLatin1String("</Relationships>"));
                if (p >= 0) {
                    s.insert(p, QStringLiteral("<Relationship Id=\"rIdMGnum\" "
                                               "Type=\"%1\" Target=\"numbering.xml\"/>")
                                    .arg(QLatin1String(kNumberingRelType)));
                    out.insert(relsName, s.toUtf8());
                }
            }
        } else {
            out.insert(relsName, QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rIdMGnum\" Type=\"%1\" Target=\"numbering.xml\"/>"
                "</Relationships>").arg(QLatin1String(kNumberingRelType)).toUtf8());
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Container schreiben: Quelle Eintrag für Eintrag durchgehen — ersetzte Teile
//  neu deflatieren, ALLES andere byteidentisch roh kopieren; neue Teile hinten
//  anfügen. Ziel-Device liefert der Aufrufer (QSaveFile → atomar).
// ─────────────────────────────────────────────────────────────────────────────
bool Document::writeTo(QIODevice* target, QString* err) const {
    DocxZip::Reader zip;
    if (!zip.open(m_path, err))
        return false;

    QHash<QString, QByteArray> repl = replacementParts();
    repl.insert(QStringLiteral("word/document.xml"), newDocumentXml().toUtf8());

    DocxZip::Writer w(target);
    for (int i = 0; i < zip.entries().size(); ++i) {
        const DocxZip::Entry& e = zip.entries().at(i);
        auto it = repl.find(e.name);
        if (it != repl.end()) {
            if (!w.addFile(e.name, it.value(), &e, err))
                return false;
            repl.erase(it);
        } else {
            bool ok = false;
            const QByteArray raw = zip.rawData(i, &ok);
            if (!ok) {
                if (err) *err = QStringLiteral("Eintrag nicht lesbar: %1").arg(e.name);
                return false;
            }
            if (!w.addRaw(e, raw, err))
                return false;
        }
    }
    for (auto it = repl.constBegin(); it != repl.constEnd(); ++it) {
        if (!w.addFile(it.key(), it.value(), nullptr, err))
            return false;
    }
    return w.finish(err);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fabrik: leeres A4-Dokument (FilterBar „+ Erstellen" → DOCX)
// ─────────────────────────────────────────────────────────────────────────────
QByteArray Document::emptyDocxBytes(const QString& title) {
    Q_UNUSED(title)
    const QByteArray contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "</Types>";
    const QByteArray rootRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>";
    const QByteArray docRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "</Relationships>";
    //  A4 (11906×16838 Twips), Standardränder 2,5 cm ≈ 1417 Twips (deutsches
    //  Word-Standardlayout); ein leerer Absatz als Einstiegspunkt.
    const QByteArray documentXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:body><w:p/>"
        "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
        "<w:pgMar w:top=\"1417\" w:right=\"1417\" w:bottom=\"1417\" w:left=\"1417\" "
        "w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr>"
        "</w:body></w:document>";
    const QByteArray stylesXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:docDefaults><w:rPrDefault><w:rPr>"
        "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:cs=\"Calibri\"/>"
        "<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/></w:rPr></w:rPrDefault>"
        "<w:pPrDefault/></w:docDefaults>"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
        "<w:name w:val=\"Normal\"/></w:style>"
        "</w:styles>";

    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    DocxZip::Writer w(&buf);
    bool ok = w.addFile(QStringLiteral("[Content_Types].xml"), contentTypes)
              && w.addFile(QStringLiteral("_rels/.rels"), rootRels)
              && w.addFile(QStringLiteral("word/document.xml"), documentXml)
              && w.addFile(QStringLiteral("word/_rels/document.xml.rels"), docRels)
              && w.addFile(QStringLiteral("word/styles.xml"), stylesXml)
              && w.finish();
    buf.close();
    return ok ? buf.data() : QByteArray();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Thumbnail-Vorschau: erste Zeilen als Klartext (läuft im Thumbnail-Worker;
//  eigener Reader je Aufruf → threadsicher, kein geteilter Zustand).
// ─────────────────────────────────────────────────────────────────────────────
QString Document::plainTextPreview(const QString& path, int maxLines) {
    DocxZip::Reader zip;
    if (!zip.open(path))
        return {};
    bool ok = false;
    const QByteArray bytes = zip.fileData(QStringLiteral("word/document.xml"), &ok);
    if (!ok)
        return {};
    QXmlStreamReader r(QString::fromUtf8(bytes));
    r.setNamespaceProcessing(false);
    QStringList lines;
    QString cur;
    while (!r.atEnd() && lines.size() < maxLines) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            if (r.qualifiedName() == QLatin1String("w:t"))
                cur += r.readElementText();
            else if (r.qualifiedName() == QLatin1String("w:tab"))
                cur += QLatin1Char('\t');
        } else if (t == QXmlStreamReader::EndElement
                   && r.qualifiedName() == QLatin1String("w:p")) {
            lines << cur;
            cur.clear();
        }
    }
    if (!cur.isEmpty() && lines.size() < maxLines)
        lines << cur;
    while (!lines.isEmpty() && lines.constLast().trimmed().isEmpty())
        lines.removeLast();
    return lines.join(QLatin1Char('\n'));
}

} // namespace Docx
