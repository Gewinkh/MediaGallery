#include "docx/DocxDocument.h"
#include "docx/DocxZip.h"

#include <QXmlStreamReader>
#include <QIODevice>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QFileInfo>
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

//  Element mit diesem LOKALEN Namen suchen (das Präfix ist nicht garantiert:
//  `wp:inline`, `w14:inline`, …). Liefert Beginn und Gesamtlänge des Elements.
bool findElement(const QString& xml, const QString& localName, int* start,
                 int* len, QString* fullTag) {
    int i = 0;
    while (i < xml.size()) {
        const int lt = xml.indexOf(QLatin1Char('<'), i);
        if (lt < 0) return false;
        const int gt = tagEnd(xml, lt);
        if (gt < 0) return false;
        const QString name = tagName(xml, lt, gt);
        if (name.section(QLatin1Char(':'), -1) == localName
            && xml.at(lt + 1) != QLatin1Char('/')) {
            //  Länge über dieselbe balancierte Suche wie childRanges.
            if (xml.at(gt - 1) == QLatin1Char('/')) {
                if (start) *start = lt;
                if (len)   *len   = gt - lt + 1;
            } else {
                int depth = 1, j = gt + 1;
                while (depth > 0 && j < xml.size()) {
                    const int nlt = xml.indexOf(QLatin1Char('<'), j);
                    if (nlt < 0) return false;
                    const int ngt = tagEnd(xml, nlt);
                    if (ngt < 0) return false;
                    const QString n2 = tagName(xml, nlt, ngt);
                    if (n2 == name) {
                        if (xml.at(nlt + 1) == QLatin1Char('/')) --depth;
                        else if (xml.at(ngt - 1) != QLatin1Char('/')) ++depth;
                    }
                    j = ngt + 1;
                }
                if (depth != 0) return false;
                if (start) *start = lt;
                if (len)   *len   = j - lt;
            }
            if (fullTag) *fullTag = name;
            return true;
        }
        i = gt + 1;
    }
    return false;
}

//  Namensraum-Deklarationen des Start-Tags erhalten — unsere eigene Zeichnung
//  trägt `xmlns:wp` AM `wp:inline`; ginge das beim Umschreiben verloren, wäre
//  die Datei kaputt.
QString keepXmlnsAttrs(const QString& xml, int lt, int gt) {
    QString out;
    const QString attrs = xml.mid(lt, gt - lt);
    int i = 0;
    while (i < attrs.size()) {
        const int x = attrs.indexOf(QLatin1String("xmlns"), i);
        if (x < 0) break;
        const int eq = attrs.indexOf(QLatin1Char('='), x);
        if (eq < 0) break;
        int q = eq + 1;
        while (q < attrs.size() && attrs.at(q).isSpace()) ++q;
        if (q >= attrs.size()) break;
        const QChar quote = attrs.at(q);
        const int close = attrs.indexOf(quote, q + 1);
        if (close < 0) break;
        out += QLatin1Char(' ') + attrs.mid(x, close - x + 1);
        i = close + 1;
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
    return loadPart(path, QStringLiteral("word/document.xml"), err);
}

//  Beziehungs-Datei EINES Teils: "word/header1.xml" → "word/_rels/header1.xml.rels".
static QString relsPathOf(const QString& partPath) {
    const int slash = partPath.lastIndexOf(QLatin1Char('/'));
    const QString dir  = (slash > 0) ? partPath.left(slash + 1) : QString();
    const QString name = partPath.mid(slash + 1);
    return dir + QStringLiteral("_rels/") + name + QStringLiteral(".rels");
}

bool Document::loadPart(const QString& path, const QString& partPath, QString* err) {
    m_path = path;
    m_partPath = partPath;
    blocks.clear();
    m_styles.clear();
    m_numLevels.clear();
    m_numToAbstract.clear();
    m_pendingNums.clear();
    m_numberingXml.clear();
    m_hadNumberingPart = false;

    m_tables.clear();
    m_rels.clear();
    m_hdrRefs.clear();
    m_ftrRefs.clear();

    DocxZip::Reader zip;
    if (!zip.open(path, err))
        return false;

    //  Beziehungen (Bilder, Kopf-/Fußzeilen-Teile). Fehlt die Datei, bleibt die
    //  Tabelle leer — dann gibt es eben keine Bilder/Kopfzeilen anzuzeigen.
    {
        bool rOk = false;
        const QByteArray relBytes = zip.fileData(relsPathOf(m_partPath), &rOk);
        if (rOk) {
            QXmlStreamReader rr(QString::fromUtf8(relBytes));
            rr.setNamespaceProcessing(false);
            while (!rr.atEnd()) {
                if (rr.readNext() != QXmlStreamReader::StartElement) continue;
                if (rr.qualifiedName() != QLatin1String("Relationship")) continue;
                const QString id = attr(rr, "Id");
                const QString target = attr(rr, "Target");
                //  Externe Ziele (TargetMode="External") sind keine ZIP-Einträge.
                if (!id.isEmpty() && !target.isEmpty()
                    && attr(rr, "TargetMode") != QLatin1String("External"))
                    m_rels.insert(id, target);
            }
        }
    }

    bool ok = false;
    const QByteArray docBytes = zip.fileData(m_partPath, &ok);
    if (!ok) {
        if (err) *err = QStringLiteral("%1 fehlt oder ist defekt.").arg(m_partPath);
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
        if (sOk) {
            m_hadStylesPart = true;
            m_stylesXml = QString::fromUtf8(styles);
            parseStylesXml(styles);
        }
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

    //  Bis in den Rumpf laufen. Der heißt im Hauptdokument <w:body>, in einem
    //  Kopf-/Fußzeilen-Teil <w:hdr>/<w:ftr> — sonst ist alles identisch.
    QString rootTag;
    int bodyContentStart = -1;
    while (!r.atEnd()) {
        const auto t = next();
        if (t != QXmlStreamReader::StartElement) continue;
        const QString qn = r.qualifiedName().toString();
        if (qn == QLatin1String("w:body") || qn == QLatin1String("w:hdr")
            || qn == QLatin1String("w:ftr")) {
            rootTag = qn;
            bodyContentStart = int(tokEnd);
            break;
        }
    }
    if (bodyContentStart < 0) {
        if (err) *err = QStringLiteral("Kein <w:body>/<w:hdr>/<w:ftr> gefunden.");
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

    //  ── w:tbl FLACH zerlegen (Option A) ──────────────────────────────────────
    //  Zell-Absätze werden reguläre Blöcke in `blocks`, das XML-Gerüst landet als
    //  Spans in `m_tables`. Rückgabe false = nicht vollständig verstanden; dann
    //  bleibt die Tabelle EIN opaker Block (Verhalten wie bisher). Nichts wird
    //  angefasst, solange nicht der ganze Baum sauber zerlegt ist — deshalb wird
    //  erst in Zwischenspeicher gesammelt und am Ende gemeinsam übernommen.
    auto parseTable = [&](int absStart, int absLen) -> bool {
        const QString frag = m_docXml.mid(absStart, absLen);
        QXmlStreamReader tr(frag);
        tr.setNamespaceProcessing(false);

        qint64 ts = 0, te = 0;
        auto tnext = [&]() {
            ts = te;
            const auto t = tr.readNext();
            te = tr.characterOffset();
            return t;
        };

        TableDef def;
        QList<Block> cellBlocks;                       // in Reihenfolge (row-major)
        int firstRowStart = -1;                        // Fragment-Offset "<w:tr"

        //  Erstes Token = StartElement <w:tbl …> (Offsets im Fragment ab hier exakt).
        while (!tr.atEnd()) {
            if (tnext() == QXmlStreamReader::StartElement) break;
        }
        if (tr.qualifiedName() != QLatin1String("w:tbl")) return false;

        while (!tr.atEnd()) {
            const auto t = tnext();
            if (t == QXmlStreamReader::EndElement
                && tr.qualifiedName() == QLatin1String("w:tbl")) {
                //  Footer = "</w:tbl>" (Rest des Fragments).
                def.footerSpan = { absStart + int(ts), int(te - ts) };
                break;
            }
            if (t != QXmlStreamReader::StartElement) continue;
            if (tr.qualifiedName() != QLatin1String("w:tr")) {
                //  tblPr/tblGrid gehören in den Header; alles ANDERE (z. B.
                //  w:sdt um Zeilen herum, w:customXml) wird nicht gedeutet.
                if (tr.qualifiedName() != QLatin1String("w:tblPr")
                    && tr.qualifiedName() != QLatin1String("w:tblGrid")
                    && tr.qualifiedName() != QLatin1String("w:tblPrEx"))
                    return false;
                const bool isGrid = (tr.qualifiedName() == QLatin1String("w:tblGrid"));
                const int gs = int(ts);
                tr.skipCurrentElement();
                te = tr.characterOffset();
                if (isGrid) {
                    //  Spaltenbreiten fürs Layout mitnehmen.
                    QXmlStreamReader gr(frag.mid(gs, int(te) - gs));
                    gr.setNamespaceProcessing(false);
                    while (!gr.atEnd()) {
                        if (gr.readNext() != QXmlStreamReader::StartElement) continue;
                        if (gr.qualifiedName() != QLatin1String("w:gridCol")) continue;
                        bool okw = false;
                        const int w = attr(gr, "w:w").toInt(&okw);
                        def.gridTw.append(okw ? qBound(0, w, 100000) : 0);
                    }
                }
                continue;
            }

            //  ── Zeile ────────────────────────────────────────────────────────
            const int rowStart = int(ts);
            if (firstRowStart < 0) firstRowStart = rowStart;
            tr.skipCurrentElement();
            te = tr.characterOffset();
            const int rowEnd = int(te);

            const QString rowFrag = frag.mid(rowStart, rowEnd - rowStart);
            QXmlStreamReader rr(rowFrag);
            rr.setNamespaceProcessing(false);
            qint64 rs = 0, re = 0;
            auto rnext = [&]() {
                rs = re;
                const auto t2 = rr.readNext();
                re = rr.characterOffset();
                return t2;
            };
            while (!rr.atEnd()) {
                if (rnext() == QXmlStreamReader::StartElement) break;
            }

            const int rowIdx = def.rowSpans.size();
            int firstCellStart = -1, lastCellEnd = -1;
            QVector<QPair<int, int>> cellRanges;        // (start,end) im rowFrag
            bool rowOk = true;
            while (!rr.atEnd()) {
                const auto t2 = rnext();
                if (t2 == QXmlStreamReader::EndElement
                    && rr.qualifiedName() == QLatin1String("w:tr"))
                    break;
                if (t2 != QXmlStreamReader::StartElement) continue;
                if (rr.qualifiedName() != QLatin1String("w:tc")) {
                    if (rr.qualifiedName() != QLatin1String("w:trPr")) { rowOk = false; break; }
                    rr.skipCurrentElement();
                    re = rr.characterOffset();
                    continue;
                }
                const int cs = int(rs);
                rr.skipCurrentElement();
                re = rr.characterOffset();
                const int ce = int(re);
                if (firstCellStart < 0) firstCellStart = cs;
                lastCellEnd = ce;
                cellRanges.append({ cs, ce });
            }
            if (!rowOk || cellRanges.isEmpty()) return false;

            def.rowSpans.append({ absStart + rowStart, firstCellStart });
            def.rowFirstCell.append(def.cellSpans.size());

            //  ── Zellen ───────────────────────────────────────────────────────
            for (int ci = 0; ci < cellRanges.size(); ++ci) {
                const int cs = cellRanges.at(ci).first;
                const int ce = cellRanges.at(ci).second;
                const QString cellFrag = rowFrag.mid(cs, ce - cs);
                QXmlStreamReader cr(cellFrag);
                cr.setNamespaceProcessing(false);
                qint64 ps = 0, pe = 0;
                auto pnext2 = [&]() {
                    ps = pe;
                    const auto t3 = cr.readNext();
                    pe = cr.characterOffset();
                    return t3;
                };
                while (!cr.atEnd()) {
                    if (pnext2() == QXmlStreamReader::StartElement) break;
                }

                int firstParStart = -1, lastParEnd = -1;
                QVector<QPair<int, int>> parRanges;
                bool cellOk = true;
                int cellSpanVal = 1, cellWidthVal = 0;
                while (!cr.atEnd()) {
                    const auto t3 = pnext2();
                    if (t3 == QXmlStreamReader::EndElement
                        && cr.qualifiedName() == QLatin1String("w:tc"))
                        break;
                    if (t3 != QXmlStreamReader::StartElement) continue;
                    const auto cn = cr.qualifiedName();
                    if (cn == QLatin1String("w:p")) {
                        const int pStart = int(ps);
                        cr.skipCurrentElement();
                        pe = cr.characterOffset();
                        if (firstParStart < 0) firstParStart = pStart;
                        lastParEnd = int(pe);
                        parRanges.append({ pStart, int(pe) });
                    } else if (cn == QLatin1String("w:tcPr")) {
                        const int ps2 = int(ps);
                        cr.skipCurrentElement();
                        pe = cr.characterOffset();
                        //  gridSpan/tcW fürs Layout mitnehmen.
                        QXmlStreamReader pr3(cellFrag.mid(ps2, int(pe) - ps2));
                        pr3.setNamespaceProcessing(false);
                        while (!pr3.atEnd()) {
                            if (pr3.readNext() != QXmlStreamReader::StartElement) continue;
                            const auto pn = pr3.qualifiedName();
                            if (pn == QLatin1String("w:gridSpan")) {
                                bool okg = false;
                                const int g = attr(pr3, "w:val").toInt(&okg);
                                if (okg) cellSpanVal = qBound(1, g, 64);
                            } else if (pn == QLatin1String("w:tcW")) {
                                bool okw = false;
                                const int w = attr(pr3, "w:w").toInt(&okw);
                                if (okw && attr(pr3, "w:type") != QLatin1String("pct"))
                                    cellWidthVal = qBound(0, w, 100000);
                            }
                        }
                    } else {
                        //  Verschachtelte Tabelle, sdt, altChunk … → nicht deuten.
                        cellOk = false;
                        break;
                    }
                }
                //  Eine Zelle OHNE Absatz ist laut Schema unzulässig; hier wäre
                //  sie ein Loch in der Rekonstruktion.
                if (!cellOk || parRanges.isEmpty()) return false;

                const int cellAbs = absStart + rowStart + cs;
                def.cellSpans.append({ cellAbs, firstParStart });
                def.cellEndSpans.append({ absStart + rowStart + cs + lastParEnd,
                                          (ce - cs) - lastParEnd });
                def.cellRow.append(rowIdx);
                def.cellGridSpan.append(cellSpanVal);
                def.cellWidthTw.append(cellWidthVal);

                for (const auto& pr2 : parRanges) {
                    Block cb = parseParagraph(cellAbs + pr2.first,
                                              pr2.second - pr2.first);
                    cb.row = rowIdx;
                    cb.col = ci;
                    cellBlocks.append(cb);
                }
            }

            def.rowEndSpans.append({ absStart + rowStart + lastCellEnd,
                                     (rowEnd - rowStart) - lastCellEnd });
        }

        if (def.rowSpans.isEmpty() || firstRowStart < 0 || !def.footerSpan.valid())
            return false;
        def.headerSpan = { absStart, firstRowStart };

        //  Übernehmen — ab hier ist die Zerlegung vollständig.
        const int tableId = m_tables.size();
        for (Block& cb : cellBlocks) {
            cb.tableId = tableId;
            blocks.append(cb);
        }
        def.blockCount = int(cellBlocks.size());
        m_tables.append(def);
        return true;
    };

    // Body-Kinder.
    int bodyContentEnd = -1;
    while (!r.atEnd()) {
        const auto t = next();
        if (t == QXmlStreamReader::EndElement && r.qualifiedName() == rootTag) {
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
        } else if (name == QLatin1String("w:tbl") && parseTable(bs, blen)) {
            //  Zellinhalt liegt jetzt FLACH in `blocks`, das Gerüst in m_tables.
            //  Schlägt das Zerlegen fehl (verschachtelte Tabelle, sdt, fremde
            //  Kinder), fällt es unten auf den bisherigen opaken Block zurück —
            //  dann bleibt alles wie vorher, statt Inhalt zu riskieren.
        } else {
            Block ob;
            ob.kind = (name == QLatin1String("w:tbl")
                       || name == QLatin1String("w:sdt")
                       || name == QLatin1String("w:altChunk"))
                          ? Block::OpaqueVisible : Block::OpaqueHidden;
            ob.rawSpan    = { bs, blen };
            ob.opaqueName = name;
            blocks.append(ob);
            //  Seiteneinrichtung des Hauptteils: das LETZTE w:sectPr im Körper
            //  gewinnt (frühere gehören zu abgeschlossenen Abschnitten).
            if (name == QLatin1String("w:sectPr"))
                parseSectPr(QStringView(m_docXml).mid(bs, blen));
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
    //  Seit Tabellen flach zerlegt werden, reicht ein Aneinanderhängen der
    //  Block-Spans nicht mehr — das Gerüst (w:tbl/w:tr/w:tc) liegt in m_tables.
    //  Geprüft wird deshalb über denselben Gruppen-Lauf, den auch das Speichern
    //  nimmt (emitBlocks, nur-Roh-Variante).
    {
        QString rebuilt;
        rebuilt.reserve(m_docXml.size());
        rebuilt += QStringView(m_docXml).mid(m_bodyPrefix.start, m_bodyPrefix.len);
        rebuilt += emitBlocks(true);
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
// ─────────────────────────────────────────────────────────────────────────────
//  Tabellen-ANZEIGE: w:tbl → Zeilen/Zellen/Absätze.
//
//  Bewusst ein EIGENER, schlanker Parser statt einer Erweiterung von
//  parseDocumentXml: die Tabelle bleibt im Blockmodell ein unangetasteter
//  OpaqueVisible-Block (beim Speichern byteidentisch), diese Sicht dient
//  ausschließlich dem Auslegen und Zeichnen. Sie füllt daher nur `pfmt` und
//  `runs` der Zell-Absätze — genau das, was resolvePar/resolveRun brauchen.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

//  Sichtbaren Text eines w:p-Fragments samt Zeichenformaten einsammeln.
//  Dieselben Sentinels wie im Hauptparser (Tab/Umbruch/atomares Objekt).
//  Text eines Runs (samt Sentinels) aus seinem Roh-Fragment lesen.
void collectRunText(QStringView runFrag, Run* run) {
    QXmlStreamReader tr(runFrag.toString());
    tr.setNamespaceProcessing(false);
    while (!tr.atEnd()) {
        if (tr.readNext() != QXmlStreamReader::StartElement) continue;
        const auto tn = tr.qualifiedName();
        if (tn == QLatin1String("w:t")) {
            run->text += tr.readElementText();
        } else if (tn == QLatin1String("w:tab")) {
            run->text += QLatin1Char('\t');
        } else if (tn == QLatin1String("w:br")) {
            run->text += (attr(tr, "w:type") == QLatin1String("page")) ? kPageBreak
                                                                      : kLineBreak;
        } else if (tn == QLatin1String("w:cr")) {
            run->text += kLineBreak;
        } else if (tn == QLatin1String("w:noBreakHyphen")) {
            run->text += QChar(0x2011);
        } else if (tn == QLatin1String("w:drawing") || tn == QLatin1String("w:object")
                   || tn == QLatin1String("w:pict")) {
            run->text  += kObjectChar;
            run->opaque = true;
        }
    }
}

Block parseCellParagraph(QStringView frag) {
    Block b;
    b.kind = Block::Paragraph;
    QXmlStreamReader r(frag.toString());
    r.setNamespaceProcessing(false);

    //  Token-Grenzen mitführen (Muster parseDocumentXml): ein Element beginnt
    //  bei `tokStart`, characterOffset() zeigt hinter das gerade gelesene Token.
    qint64 tokStart = 0, tokEnd = 0;
    auto next = [&]() {
        tokStart = tokEnd;
        const auto t = r.readNext();
        tokEnd = r.characterOffset();
        return t;
    };
    auto fragmentOf = [&](qint64 startAt) -> QStringView {
        r.skipCurrentElement();
        tokEnd = r.characterOffset();
        if (startAt < 0 || tokEnd <= startAt || tokEnd > frag.size()) return {};
        return frag.mid(int(startAt), int(tokEnd - startAt));
    };

    while (!r.atEnd()) {
        if (next() != QXmlStreamReader::StartElement) continue;
        const auto n = r.qualifiedName();
        const qint64 elemStart = tokStart;
        if (n == QLatin1String("w:pPr")) {
            const QStringView f = fragmentOf(elemStart);
            if (!f.isEmpty()) Document::parseParProps(f, &b.pfmt);
        } else if (n == QLatin1String("w:r")) {
            const QStringView f = fragmentOf(elemStart);
            if (f.isEmpty()) continue;
            Run run;
            Document::parseRunProps(f, &run.fmt);
            collectRunText(f, &run);
            if (!run.text.isEmpty()) b.runs.append(run);
        } else if (n == QLatin1String("w:hyperlink") || n == QLatin1String("w:ins")
                   || n == QLatin1String("w:smartTag") || n == QLatin1String("w:sdt")) {
            //  Beschriftung anzeigen, Struktur nicht deuten.
            const QStringView f = fragmentOf(elemStart);
            if (f.isEmpty()) continue;
            Run run;
            collectRunText(f, &run);
            if (!run.text.isEmpty()) b.runs.append(run);
        }
    }
    return b;
}

} // namespace

TableView Document::parseTableForDisplay(const Block& b) const {
    TableView tv;
    if (b.kind != Block::OpaqueVisible
        || b.opaqueName != QLatin1String("w:tbl")
        || !b.rawSpan.valid()
        || b.rawSpan.start < 0
        || b.rawSpan.start + b.rawSpan.len > m_docXml.size())
        return tv;                                   // ok bleibt false

    const QString all = QStringView(m_docXml).mid(b.rawSpan.start, b.rawSpan.len).toString();

    //  Ein Fragment-Leser mit mitgeführten Token-Grenzen — dreimal gebraucht
    //  (Tabelle → Zeile → Zelle), deshalb als Lambda-Fabrik.
    struct Scan {
        QXmlStreamReader r;
        qint64 tokStart = 0, tokEnd = 0;
        explicit Scan(const QString& s) : r(s) { r.setNamespaceProcessing(false); }
        QXmlStreamReader::TokenType next() {
            tokStart = tokEnd;
            const auto t = r.readNext();
            tokEnd = r.characterOffset();
            return t;
        }
        //  Roh-Fragment des gerade begonnenen Elements (ab seinem '<').
        QString take(const QString& src) {
            const qint64 s = tokStart;
            r.skipCurrentElement();
            tokEnd = r.characterOffset();
            if (s < 0 || tokEnd <= s || tokEnd > src.size()) return {};
            return src.mid(int(s), int(tokEnd - s));
        }
    };

    Scan t1(all);
    int depth = 0;                                   // Verschachtelungstiefe w:tbl
    while (!t1.r.atEnd()) {
        const auto t = t1.next();
        if (t == QXmlStreamReader::EndElement) {
            if (t1.r.qualifiedName() == QLatin1String("w:tbl")) --depth;
            continue;
        }
        if (t != QXmlStreamReader::StartElement) continue;
        const auto n = t1.r.qualifiedName();
        if (n == QLatin1String("w:tbl")) { ++depth; continue; }
        if (depth != 1) continue;                    // nur die ÄUSSERE Tabelle

        if (n == QLatin1String("w:gridCol")) {
            bool ok = false;
            const int w = attr(t1.r, "w:w").toInt(&ok);
            tv.gridTw.append(ok ? qBound(0, w, 100000) : 0);
        } else if (n == QLatin1String("w:tr")) {
            const QString rowFrag = t1.take(all);
            if (rowFrag.isEmpty()) continue;

            TableRow row;
            Scan t2(rowFrag);
            int cellDepth = 0;
            while (!t2.r.atEnd()) {
                const auto t2t = t2.next();
                if (t2t == QXmlStreamReader::EndElement) {
                    if (t2.r.qualifiedName() == QLatin1String("w:tbl")) --cellDepth;
                    continue;
                }
                if (t2t != QXmlStreamReader::StartElement) continue;
                if (t2.r.qualifiedName() == QLatin1String("w:tbl")) { ++cellDepth; continue; }
                if (cellDepth != 0) continue;        // Inhalt verschachtelter Tabellen
                if (t2.r.qualifiedName() != QLatin1String("w:tc")) continue;

                const QString cellFrag = t2.take(rowFrag);
                if (cellFrag.isEmpty()) continue;

                TableCell cell;
                Scan t3(cellFrag);
                while (!t3.r.atEnd()) {
                    if (t3.next() != QXmlStreamReader::StartElement) continue;
                    const auto cn = t3.r.qualifiedName();
                    if (cn == QLatin1String("w:tbl")) {
                        //  VERSCHACHTELTE Tabelle → Platzhalter-Absatz in dieser
                        //  Zelle (Inhalt bleibt in der Datei, nur nicht gedeutet).
                        Block nested;
                        nested.kind = Block::OpaqueVisible;
                        nested.opaqueName = QStringLiteral("w:tbl");
                        cell.paragraphs.append(nested);
                        t3.take(cellFrag);
                    } else if (cn == QLatin1String("w:gridSpan")) {
                        bool ok = false;
                        const int gs = attr(t3.r, "w:val").toInt(&ok);
                        if (ok) cell.gridSpan = qBound(1, gs, 64);
                    } else if (cn == QLatin1String("w:tcW")) {
                        bool ok = false;
                        const int w = attr(t3.r, "w:w").toInt(&ok);
                        if (ok && attr(t3.r, "w:type") != QLatin1String("pct"))
                            cell.widthTw = qBound(0, w, 100000);
                    } else if (cn == QLatin1String("w:p")) {
                        const QString pFrag = t3.take(cellFrag);
                        if (!pFrag.isEmpty())
                            cell.paragraphs.append(parseCellParagraph(pFrag));
                    }
                }
                row.cells.append(cell);
            }
            if (!row.cells.isEmpty()) tv.rows.append(row);
        }
    }
    QXmlStreamReader& r = t1.r;

    //  Brauchbar nur mit mindestens einer Zeile und einer Zelle. Fehlt das
    //  Gitter, werden die Spalten später gleichmäßig verteilt.
    tv.ok = !r.hasError() && !tv.rows.isEmpty();
    return tv;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bilder, weitere ZIP-Teile, Kopf-/Fußzeilen (alles ANZEIGE)
// ─────────────────────────────────────────────────────────────────────────────
QString Document::relTarget(const QString& relId) const {
    return m_rels.value(relId);
}

QByteArray Document::partBytes(const QString& zipPath) const {
    if (zipPath.isEmpty() || m_path.isEmpty()) return {};
    //  Das ZIP wird für diesen einen Eintrag erneut geöffnet und NICHT
    //  offengehalten: Bilddaten dauerhaft im Speicher zu halten wäre bei einem
    //  bildlastigen Dokument der größte Einzelposten (RAM = Priorität 1). Die
    //  Anzeige hält stattdessen nur das eingepasste QImage im Layout-Fenster.
    DocxZip::Reader zip;
    if (!zip.open(m_path, nullptr)) return {};
    bool ok = false;
    const QByteArray data = zip.fileData(zipPath, &ok);
    zip.close();
    return ok ? data : QByteArray();
}

QByteArray Document::imageBytes(const QString& relId) const {
    //  Frisch eingefügte Bilder liegen noch NICHT im Container — sonst zeigte
    //  die Anzeige bis zum Speichern nur einen leeren Rahmen.
    for (const PendingMedia& m : m_pendingMedia)
        if (m.relId == relId) return m.bytes;
    QString target = relTarget(relId);
    if (target.isEmpty()) return {};
    //  Ziele stehen relativ zu word/ ("media/bild1.png"); absolute Angaben
    //  ("/word/media/…") und Rückwärtsschritte kommen vor.
    while (target.startsWith(QLatin1Char('/')))
        target.remove(0, 1);
    if (target.startsWith(QLatin1String("word/")))
        return partBytes(target);
    if (target.contains(QLatin1String("..")))
        return {};                                   // keine Pfad-Ausbrüche
    return partBytes(QStringLiteral("word/") + target);
}

//  Ein `w:drawing`-Run → Bild (Beziehung + Sollmaß). Der Run bleibt opak, hier
//  wird nur GELESEN, was die Anzeige braucht.
static bool parseDrawingRun(QStringView frag, InlineImage* img) {
    QXmlStreamReader r(frag.toString());
    r.setNamespaceProcessing(false);
    //  `wp:positionH`/`wp:positionV` tragen ihren Wert im KIND `wp:posOffset`;
    //  beim Lesen des Offsets muss also bekannt sein, welche Achse gerade läuft.
    int axis = 0;                                    // 1 = waagerecht, 2 = senkrecht
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) {
            if (r.tokenType() == QXmlStreamReader::EndElement) {
                const QString e = r.qualifiedName().toString()
                                      .section(QLatin1Char(':'), -1);
                if (e == QLatin1String("positionH") || e == QLatin1String("positionV"))
                    axis = 0;
            }
            continue;
        }
        //  Präfixe sind nicht garantiert (a:blip, wp:extent) → lokaler Name.
        const QString qn = r.qualifiedName().toString();
        const QString local = qn.section(QLatin1Char(':'), -1);
        if (local == QLatin1String("anchor")) {
            img->anchored = true;
            //  Abstände zum Text (EMU); fehlen sie, bleibt es bei 0.
            const int dl = attr(r, "distL").toInt();
            const int dr = attr(r, "distR").toInt();
            img->distLEmu = qBound(0, dl, 914400);
            img->distREmu = qBound(0, dr, 914400);
        } else if (local == QLatin1String("positionH")) {
            axis = 1;
        } else if (local == QLatin1String("positionV")) {
            axis = 2;
        } else if (local == QLatin1String("posOffset")) {
            const QString v = r.readElementText();
            bool ok = false;
            const int off = v.trimmed().toInt(&ok);
            if (ok && axis == 1) img->posXEmu = qBound(-182880000, off, 182880000);
            if (ok && axis == 2) img->posYEmu = qBound(-182880000, off, 182880000);
            axis = 0;
        } else if (local == QLatin1String("wrapSquare")
                   || local == QLatin1String("wrapTight")
                   || local == QLatin1String("wrapThrough")) {
            //  Tight/Through werden wie Square ausgelegt: die Kontur eines
            //  Bildes ist sein Rechteck, ein Unterschied entstünde nur bei
            //  freigestellten Formen.
            img->wrap = InlineImage::WrapSquare;
            const QString ws = attr(r, "wrapText");
            if (ws == QLatin1String("left"))          img->wrapSide = InlineImage::SideLeft;
            else if (ws == QLatin1String("right"))    img->wrapSide = InlineImage::SideRight;
            else if (ws == QLatin1String("largest"))  img->wrapSide = InlineImage::SideLargest;
            else                                      img->wrapSide = InlineImage::SideBoth;
        } else if (local == QLatin1String("blip")) {
            const QString id = attr(r, "r:embed");
            if (!id.isEmpty()) img->relId = id;
        } else if (local == QLatin1String("extent")) {
            bool okx = false, oky = false;
            const int cx = attr(r, "cx").toInt(&okx);
            const int cy = attr(r, "cy").toInt(&oky);
            //  Grenzen: 0 … 200 Zoll. Unsinnige Werte → Vorgabe über das Bild.
            if (okx && cx > 0) img->cxEmu = qMin(cx, 182880000);
            if (oky && cy > 0) img->cyEmu = qMin(cy, 182880000);
        }
    }
    return !img->relId.isEmpty();
}

//  ALLE Bilder eines Absatzes in Textreihenfolge. `pos` ist die Stelle des
//  Objekt-Zeichens im Absatztext — daran hängt die Anzeige das Bild auf.
QVector<InlineImage> Document::paragraphImages(const Block& b) const {
    QVector<InlineImage> out;
    if (b.kind != Block::Paragraph) return out;
    int pos = 0;
    for (int i = 0; i < b.runs.size(); ++i) {
        const Run& r = b.runs.at(i);
        const int len = int(r.text.size());
        if (r.opaque && r.text == QString(kObjectChar) && r.rawSpan.valid()
            && r.rawSpan.start >= 0
            && r.rawSpan.start + r.rawSpan.len <= m_docXml.size()) {
            InlineImage img;
            img.run = i;
            img.pos = pos;
            if (parseDrawingRun(QStringView(m_docXml).mid(r.rawSpan.start,
                                                          r.rawSpan.len), &img))
                out.append(img);
        }
        pos += len;
    }
    return out;
}

//  Sonderfall „der Absatz IST das Bild" — daran hängen Bildgröße, Kopieren und
//  der Bild-Absatz in einer Tabellenzelle.
bool Document::paragraphImage(const Block& b, InlineImage* out) const {
    if (b.kind != Block::Paragraph || b.textLength() != 1) return false;
    const QVector<InlineImage> imgs = paragraphImages(b);
    if (imgs.size() != 1) return false;
    if (out) *out = imgs.first();
    return true;
}

//  ZIP-Pfad des Kopf-/Fußzeilen-Teils; leer, wenn es keinen gibt. Öffentlich,
//  weil der Editor diesen Teil als EIGENE Document-Instanz lädt (Region).
QString Document::headerFooterPart(bool footer, bool first) const {
    const QHash<QString, QString>& refs = footer ? m_ftrRefs : m_hdrRefs;
    QString id = first ? refs.value(QStringLiteral("first")) : QString();
    if (id.isEmpty()) id = refs.value(QStringLiteral("default"));
    if (id.isEmpty()) return {};

    QString target = relTarget(id);
    if (target.isEmpty() || target.contains(QLatin1String(".."))) return {};
    while (target.startsWith(QLatin1Char('/'))) target.remove(0, 1);
    if (!target.startsWith(QLatin1String("word/")))
        target = QStringLiteral("word/") + target;
    return target;
}

HeaderFooter Document::headerFooter(bool footer, bool first) const {
    HeaderFooter hf;
    const QString target = headerFooterPart(footer, first);
    if (target.isEmpty()) return hf;
    const QByteArray xml = partBytes(target);
    if (xml.isEmpty()) return hf;

    //  Absätze des Teils einsammeln — gleiche Technik wie bei Tabellenzellen.
    const QString text = QString::fromUtf8(xml);
    QXmlStreamReader r(text);
    r.setNamespaceProcessing(false);
    qint64 tokStart = 0, tokEnd = 0;
    int tblDepth = 0;
    while (!r.atEnd()) {
        tokStart = tokEnd;
        const auto t = r.readNext();
        tokEnd = r.characterOffset();
        if (t == QXmlStreamReader::EndElement
            && r.qualifiedName() == QLatin1String("w:tbl")) { --tblDepth; continue; }
        if (t != QXmlStreamReader::StartElement) continue;
        if (r.qualifiedName() == QLatin1String("w:tbl")) { ++tblDepth; continue; }
        if (tblDepth > 0) continue;                  // Tabellen in Kopfzeilen: nein
        if (r.qualifiedName() != QLatin1String("w:p")) continue;
        const qint64 s = tokStart;
        r.skipCurrentElement();
        tokEnd = r.characterOffset();
        if (s < 0 || tokEnd <= s || tokEnd > text.size()) continue;
        hf.paragraphs.append(parseCellParagraph(QStringView(text).mid(int(s),
                                                                     int(tokEnd - s))));
    }
    hf.ok = !hf.paragraphs.isEmpty();
    return hf;
}

//  w:sectPr → SectionProps. Bewusst tolerant: fehlende Attribute behalten den
//  A4-Vorgabewert, unsinnige Werte werden geklemmt (ein Dokument mit
//  pgSz w="0" würde sonst eine Seite ohne Textbreite ergeben).
void Document::parseSectPr(QStringView xml) {
    QXmlStreamReader r(xml.toString());
    r.setNamespaceProcessing(false);
    //  Twips: 1/1440 Zoll. Grenzen = 0,5 cm … 2 m Seitenmaß bzw. 0 … halbe
    //  Seite Rand — großzügig, aber nicht mehr zerstörerisch.
    auto num = [&r](const char* name, int fallback, int lo, int hi) {
        const QString v = attr(r, name);
        if (v.isEmpty()) return fallback;
        bool ok = false;
        const int n = v.toInt(&ok);
        return ok ? qBound(lo, n, hi) : fallback;
    };
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        const auto n = r.qualifiedName();
        if (n == QLatin1String("w:pgSz")) {
            m_section.pageW = num("w:w", m_section.pageW, 284, 113386);
            m_section.pageH = num("w:h", m_section.pageH, 284, 113386);
            m_section.landscape = (attr(r, "w:orient") == QLatin1String("landscape"));
        } else if (n == QLatin1String("w:pgMar")) {
            m_section.marTop    = num("w:top",    m_section.marTop,    0, 56693);
            m_section.marRight  = num("w:right",  m_section.marRight,  0, 56693);
            m_section.marBottom = num("w:bottom", m_section.marBottom, 0, 56693);
            m_section.marLeft   = num("w:left",   m_section.marLeft,   0, 56693);
        } else if (n == QLatin1String("w:cols")) {
            m_section.cols     = num("w:num",   1,   1, 12);
            m_section.colSpace = num("w:space", 708, 0, 14400);
        } else if (n == QLatin1String("w:headerReference")
                   || n == QLatin1String("w:footerReference")) {
            //  w:type = default | first | even; das Ziel steckt in r:id.
            const QString id = attr(r, "r:id");
            QString type = attr(r, "w:type");
            if (type.isEmpty()) type = QStringLiteral("default");
            if (id.isEmpty()) continue;
            if (n == QLatin1String("w:headerReference")) m_hdrRefs.insert(type, id);
            else                                        m_ftrRefs.insert(type, id);
        }
    }
    //  Ränder dürfen die Seite nicht auffressen: mindestens 1 cm Textbreite
    //  bzw. -höhe bleiben stehen, sonst gilt die Angabe als unbrauchbar.
    if (m_section.marLeft + m_section.marRight > m_section.pageW - 567)
        m_section.marLeft = m_section.marRight = qMax(0, (m_section.pageW - 567) / 2);
    if (m_section.marTop + m_section.marBottom > m_section.pageH - 567)
        m_section.marTop = m_section.marBottom = qMax(0, (m_section.pageH - 567) / 2);
    //  Spalten müssen zusammen mit ihren Abständen in die Textbreite passen.
    const int textW = m_section.pageW - m_section.marLeft - m_section.marRight;
    while (m_section.cols > 1
           && (m_section.cols - 1) * m_section.colSpace + m_section.cols * 284 > textW)
        --m_section.cols;
}

bool Document::parseStylesXml(const QByteArray& xml) {
    QXmlStreamReader r(QString::fromUtf8(xml));
    r.setNamespaceProcessing(false);

    //  Anzeige-Daten der ABSATZ-Vorlagen, in Reihenfolge der styles.xml
    //  gesammelt und erst am Ende gefiltert (semiHidden fällt weg, sofern die
    //  Vorlage nicht per qFormat ausdrücklich in die Auswahl gehört — genau
    //  die Menge, die auch Word im Formatvorlagen-Katalog zeigt).
    struct UiStyle { StyleInfo info; bool semiHidden = false; bool quick = false; };
    QList<UiStyle> uiStyles;
    m_parStyles.clear();
    m_defaultParStyle.clear();

    //  w:default/w:semiHidden/w:qFormat sind ST_OnOff: fehlender Wert = an.
    auto isOn = [](const QString& v) {
        return !(v == QLatin1String("0") || v == QLatin1String("false")
                 || v == QLatin1String("off"));
    };

    QString curStyle;
    int curUi = -1;                                   // Index in uiStyles, −1 = keiner
    while (!r.atEnd()) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::EndElement
            && r.qualifiedName() == QLatin1String("w:style")) {
            curStyle.clear();
            curUi = -1;
        }
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
            curUi = -1;
            if (!curStyle.isEmpty()) {
                m_styles.insert(curStyle, StyleDef());
                //  Fehlendes w:type bedeutet laut Schema "paragraph"; nur
                //  Absatzvorlagen sind über w:pStyle anwendbar.
                const QString type = attr(r, "w:type");
                if (type.isEmpty() || type == QLatin1String("paragraph")) {
                    UiStyle u;
                    u.info.id   = curStyle;
                    u.info.name = curStyle;          // bis ein w:name kommt
                    const QString def = attr(r, "w:default");
                    u.info.isDefault = !def.isNull() && isOn(def);
                    if (u.info.isDefault && m_defaultParStyle.isEmpty())
                        m_defaultParStyle = curStyle;
                    uiStyles.append(u);
                    curUi = uiStyles.size() - 1;
                }
            }
        } else if (curUi >= 0 && n == QLatin1String("w:name")) {
            const QString nm = attr(r, "w:val");
            if (!nm.isEmpty()) uiStyles[curUi].info.name = nm;
        } else if (curUi >= 0 && n == QLatin1String("w:semiHidden")) {
            uiStyles[curUi].semiHidden = isOn(attr(r, "w:val"));
        } else if (curUi >= 0 && n == QLatin1String("w:qFormat")) {
            uiStyles[curUi].quick = isOn(attr(r, "w:val"));
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

    //  Auswahlliste: semiHidden bleibt draußen, es sei denn qFormat holt die
    //  Vorlage ausdrücklich in den Katalog. Die Standardvorlage steht vorn —
    //  sie ist der Weg ZURÜCK (Anwenden entfernt das w:pStyle wieder).
    m_parStyles.reserve(uiStyles.size());
    for (const UiStyle& u : uiStyles)
        if (u.info.isDefault)
            m_parStyles.append(u.info);
    for (const UiStyle& u : uiStyles)
        if (!u.info.isDefault && (u.quick || !u.semiHidden))
            m_parStyles.append(u.info);

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

RunFmt Document::paragraphMarkFormat(const Block& b) const {
    RunFmt out = resolveRun(b, Run());
    const QString ppr = b.currentPpr(m_docXml);
    if (ppr.isEmpty()) return out;
    const int at = ppr.indexOf(QLatin1String("<w:rPr"));
    if (at < 0) return out;
    //  Sowohl <w:rPr>…</w:rPr> als auch das leere <w:rPr/> abdecken.
    int end = ppr.indexOf(QLatin1String("</w:rPr>"), at);
    end = (end >= 0) ? end + int(QLatin1String("</w:rPr>").size())
                     : ppr.indexOf(QLatin1Char('>'), at) + 1;
    if (end <= at) return out;
    RunFmt f;
    parseRunProps(QStringView(ppr).mid(at, end - at), &f);
    if (f.set & RunFmt::FFont)      out.font      = f.font;
    if (f.set & RunFmt::FSize)      out.sizePt    = f.sizePt;
    if (f.set & RunFmt::FBold)      out.bold      = f.bold;
    if (f.set & RunFmt::FItalic)    out.italic    = f.italic;
    if (f.set & RunFmt::FUnderline) out.underline = f.underline;
    if (f.set & RunFmt::FColor)     out.color     = f.color;
    out.set |= f.set;
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

// ─────────────────────────────────────────────────────────────────────────────
//  Einfügen neuer Knoten
// ─────────────────────────────────────────────────────────────────────────────
int Document::appendPool(const QString& xml) {
    const int at = m_docXml.size();
    m_docXml += xml;
    return at;
}

int Document::insertTable(int beforeBlock, int rows, int cols) {
    rows = qBound(1, rows, 100);
    cols = qBound(1, cols, 32);
    beforeBlock = qBound(0, beforeBlock, int(blocks.size()));

    //  Gleichmäßiges Gitter über die Textbreite des Abschnitts.
    const int textW = qMax(1000, m_section.pageW - m_section.marLeft - m_section.marRight);
    const int colW  = qMax(200, textW / cols);

    //  XML in EINEM Stück bauen und dabei die lokalen Offsets mitschreiben —
    //  wir erzeugen es selbst, es muss also nicht zurückgeparst werden.
    QString xml;
    xml.reserve(256 + rows * cols * 64);

    QString header = QStringLiteral(
        "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders>");
    for (const char* side : { "top", "left", "bottom", "right",
                              "insideH", "insideV" })
        header += QStringLiteral("<w:%1 w:val=\"single\" w:sz=\"4\" w:space=\"0\" "
                                 "w:color=\"auto\"/>").arg(QLatin1String(side));
    header += QStringLiteral("</w:tblBorders></w:tblPr><w:tblGrid>");
    for (int c = 0; c < cols; ++c)
        header += QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(colW);
    header += QStringLiteral("</w:tblGrid>");
    xml += header;

    struct LocalSpan { int start = 0; int len = 0; };
    QVector<LocalSpan> rowPre, rowEnd, cellPre, cellEnd, parAll, parTag;
    TableDef def;

    for (int r = 0; r < rows; ++r) {
        const int rp = xml.size();
        xml += QStringLiteral("<w:tr>");
        rowPre.append({ rp, xml.size() - rp });
        def.rowFirstCell.append(cellPre.size());

        for (int c = 0; c < cols; ++c) {
            const int cp = xml.size();
            xml += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/>"
                                  "</w:tcPr>").arg(colW);
            cellPre.append({ cp, xml.size() - cp });

            //  Leerer Absatz: NICHT selbstschließend, damit der Dirty-Neuaufbau
            //  (StartTag + Inhalt + "</w:p>") ohne Sonderfall greift.
            const int pp = xml.size();
            xml += QStringLiteral("<w:p>");
            const int tagLen = xml.size() - pp;
            xml += QStringLiteral("</w:p>");
            parAll.append({ pp, xml.size() - pp });
            parTag.append({ pp, tagLen });

            const int ce = xml.size();
            xml += QStringLiteral("</w:tc>");
            cellEnd.append({ ce, xml.size() - ce });

            def.cellRow.append(r);
            def.cellGridSpan.append(1);
            def.cellWidthTw.append(colW);
        }
        const int re = xml.size();
        xml += QStringLiteral("</w:tr>");
        rowEnd.append({ re, xml.size() - re });
    }
    const int fs = xml.size();
    xml += QStringLiteral("</w:tbl>");

    //  In den Pool legen und alle Offsets absolut machen.
    const int base = appendPool(xml);
    auto abs = [base](const LocalSpan& s) { return Span{ base + s.start, s.len }; };

    def.headerSpan = { base, header.size() };
    def.footerSpan = { base + fs, xml.size() - fs };
    for (const LocalSpan& s : rowPre)  def.rowSpans.append(abs(s));
    for (const LocalSpan& s : rowEnd)  def.rowEndSpans.append(abs(s));
    for (const LocalSpan& s : cellPre) def.cellSpans.append(abs(s));
    for (const LocalSpan& s : cellEnd) def.cellEndSpans.append(abs(s));
    for (int c = 0; c < cols; ++c) def.gridTw.append(colW);

    def.blockCount = rows * cols;
    const int tableId = m_tables.size();
    m_tables.append(def);

    //  Zell-Blöcke in Lesereihenfolge einsetzen.
    QList<Block> newBlocks;
    newBlocks.reserve(rows * cols);
    for (int i = 0; i < rows * cols; ++i) {
        Block b;
        b.kind         = Block::Paragraph;
        b.rawSpan      = abs(parAll.at(i));
        b.startTagSpan = abs(parTag.at(i));
        b.tableId      = tableId;
        b.row          = i / cols;
        b.col          = i % cols;
        newBlocks.append(b);
    }
    for (int i = 0; i < newBlocks.size(); ++i)
        blocks.insert(beforeBlock + i, newBlocks.at(i));
    return beforeBlock;
}

//  Eingefügte Bilder: Medien-Teil, Content-Type (Default je Endung) und
//  Beziehung. Baut auf dem Bestand auf und ergänzt nur Fehlendes.
QHash<QString, QByteArray> Document::mediaParts() const {
    QHash<QString, QByteArray> out;
    if (m_pendingMedia.isEmpty())
        return out;

    static const QHash<QString, QString> kTypes = {
        { QStringLiteral("png"),  QStringLiteral("image/png") },
        { QStringLiteral("jpg"),  QStringLiteral("image/jpeg") },
        { QStringLiteral("jpeg"), QStringLiteral("image/jpeg") },
        { QStringLiteral("gif"),  QStringLiteral("image/gif") },
        { QStringLiteral("bmp"),  QStringLiteral("image/bmp") },
        { QStringLiteral("tif"),  QStringLiteral("image/tiff") },
        { QStringLiteral("tiff"), QStringLiteral("image/tiff") },
        { QStringLiteral("webp"), QStringLiteral("image/webp") },
    };

    for (const PendingMedia& m : m_pendingMedia)
        out.insert(m.zipName, m.bytes);

    DocxZip::Reader zip;
    if (!zip.open(m_path))
        return out;

    //  [Content_Types].xml: je Endung ein <Default>, falls noch keiner da ist.
    bool ok = false;
    QByteArray ct = zip.fileData(QStringLiteral("[Content_Types].xml"), &ok);
    if (ok) {
        QString s = QString::fromUtf8(ct);
        bool changed = false;
        for (const PendingMedia& m : m_pendingMedia) {
            const QString type = kTypes.value(m.ext);
            if (type.isEmpty()) continue;
            const QString needle = QStringLiteral("Extension=\"%1\"").arg(m.ext);
            if (s.contains(needle)) continue;
            const int p = s.lastIndexOf(QLatin1String("</Types>"));
            if (p < 0) break;
            s.insert(p, QStringLiteral("<Default Extension=\"%1\" ContentType=\"%2\"/>")
                            .arg(m.ext, type));
            changed = true;
        }
        if (changed) out.insert(QStringLiteral("[Content_Types].xml"), s.toUtf8());
    }

    //  Beziehungen ergänzen (auf einer evtl. schon von numberingParts()
    //  geänderten Fassung würde hier der Bestand gewinnen — deshalb liest diese
    //  Funktion IMMER den Originalstand und fügt beide Ergänzungen zusammen).
    const QString relsName = relsPathOf(m_partPath);
    QByteArray rels = zip.fileData(relsName, &ok);
    QString relsXml = ok ? QString::fromUtf8(rels)
                         : QStringLiteral(
                               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
                               "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                               "package/2006/relationships\"></Relationships>");
    //  Listen-Beziehung ggf. mitnehmen (sonst ginge sie hier verloren).
    if (!m_pendingNums.isEmpty() && !relsXml.contains(QLatin1String("relationships/numbering"))) {
        const int p = relsXml.lastIndexOf(QLatin1String("</Relationships>"));
        if (p >= 0)
            relsXml.insert(p, QStringLiteral(
                "<Relationship Id=\"rIdMGnum\" Type=\"http://schemas.openxmlformats.org/"
                "officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>"));
    }
    static const char* kImageRelType =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
    for (const PendingMedia& m : m_pendingMedia) {
        if (relsXml.contains(QStringLiteral("Id=\"%1\"").arg(m.relId))) continue;
        const int p = relsXml.lastIndexOf(QLatin1String("</Relationships>"));
        if (p < 0) break;
        QString target = m.zipName;
        target.remove(0, QStringLiteral("word/").size());   // relativ zu word/
        relsXml.insert(p, QStringLiteral("<Relationship Id=\"%1\" Type=\"%2\" "
                                         "Target=\"%3\"/>")
                              .arg(m.relId, QLatin1String(kImageRelType), target));
    }
    out.insert(relsName, relsXml.toUtf8());
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inhaltsverzeichnis
//
//  Das Feld bleibt DEKLARATIV: `w:fldSimple` mit der TOC-Anweisung, ohne
//  eingebackene Seitenzahlen. Damit rechnet Word die Zahlen selbst (und aktua-
//  lisiert sie beim Drucken), und wir müssen beim Speichern keine Zahlen
//  pflegen, die schon beim nächsten Tippen falsch wären. Unsere ANZEIGE füllt
//  sie aus der eigenen Paginierung — s. DocxTextArea.
// ─────────────────────────────────────────────────────────────────────────────
bool Document::isTocParagraph(const Block& b) const {
    if (b.kind != Block::Paragraph || !b.rawSpan.valid()) return false;
    if (b.rawSpan.start < 0 || b.rawSpan.start + b.rawSpan.len > m_docXml.size())
        return false;
    const QStringView frag = QStringView(m_docXml).mid(b.rawSpan.start, b.rawSpan.len);
    const int fld = frag.indexOf(QLatin1String("<w:fldSimple"));
    if (fld < 0) return false;
    //  Die Anweisung steht im Attribut w:instr; " TOC \o ... " ist der Normalfall.
    const int gt = frag.indexOf(QLatin1Char('>'), fld);
    if (gt < 0) return false;
    if (!frag.mid(fld, gt - fld).contains(QLatin1String("TOC")))
        return false;
    //  Der Absatz muss AUS dem Feld bestehen — Felder tragen ihren Inhalt im
    //  Roh-Bereich, im Absatztext steht dafür nur das Objekt-Zeichen. Steht
    //  daneben ECHTER Text, ist es kein Verzeichnis(-Absatz) mehr, sondern ein
    //  Absatz, in den ein Feld geraten ist: dann muss sein Text wieder sichtbar
    //  und bearbeitbar sein, statt hinter der Verzeichnis-Anzeige zu
    //  verschwinden (Nutzerbefund an tests/ER.docx).
    for (const QChar& c : b.plainText())
        if (c != kObjectChar && !c.isSpace())
            return false;
    return true;
}

QList<TocEntry> Document::tocEntries(int maxLevel) const {
    maxLevel = qBound(1, maxLevel, 9);
    QList<TocEntry> out;
    for (int i = 0; i < blocks.size(); ++i) {
        const Block& b = blocks.at(i);
        if (b.kind != Block::Paragraph || b.tableId >= 0) continue;
        //  Die styleId ist sprachunabhängig ("Heading1"), der Anzeigename nicht.
        const QString id = resolvePar(b).styleId;
        if (!id.startsWith(QLatin1String("Heading"), Qt::CaseInsensitive)) continue;
        bool ok = false;
        const int lvl = QStringView(id).mid(7).toInt(&ok);
        if (!ok || lvl < 1 || lvl > maxLevel) continue;
        //  Ein Überschrift-Absatz kann MEHRERE Überschriften tragen: viele
        //  Dokumente trennen sie nur durch Zeilenumbrüche (`w:br`) statt durch
        //  eigene Absätze. Jede Zeile ist dann eine eigene Überschrift und
        //  bekommt ihre eigene Zeile im Verzeichnis — alles in EINEM Eintrag
        //  zusammenzuziehen ergibt kein Inhaltsverzeichnis, sondern eine
        //  Textwurst (Nutzerbefund an `tests/ER.docx`).
        //  Selbst zerlegt statt `QStringView::split`: der Eintrag braucht die
        //  Position seines Zeilenanfangs (s. `TocEntry::pos`), und split()
        //  wirft sie weg.
        const QString raw = b.plainText();
        for (int start = 0; start <= int(raw.size()); ) {
            int brk = int(raw.indexOf(kLineBreak, start));
            if (brk < 0) brk = int(raw.size());
            //  Seitenumbruch-Sentinel und Objekt-Zeichen gehören nicht in den
            //  Eintragstext.
            QString text = raw.mid(start, brk - start);
            text.remove(kPageBreak);
            text.remove(kObjectChar);
            text = text.trimmed();
            if (!text.isEmpty())
                out.append({ text, lvl, i, start });
            start = brk + 1;
        }
    }
    return out;
}

int Document::insertToc(int beforeBlock, int maxLevel) {
    maxLevel = qBound(1, maxLevel, 9);
    beforeBlock = qBound(0, beforeBlock, int(blocks.size()));

    //  Ein leerer Run als Feldergebnis: Word ersetzt ihn beim Aktualisieren,
    //  unsere Anzeige baut die Einträge ohnehin selbst. Ohne IRGENDein Kind
    //  wäre das fldSimple laut Schema unzulässig.
    //  `w:pageBreakBefore` gibt dem Verzeichnis auch in Word eine eigene Seite;
    //  die Anzeige erzwingt sie zusätzlich selbst (paginateBlock kennt isToc).
    //  Der Absatz DANACH bekommt sie ebenfalls — sonst liefe der Text in Word
    //  direkt hinter dem Verzeichnis weiter.
    const QString xml =
        QStringLiteral("<w:p><w:pPr><w:pageBreakBefore/></w:pPr>"
                       "<w:fldSimple w:instr=\" TOC \\o &quot;1-%1&quot; "
                       "\\h \\z \\u \"><w:r><w:t xml:space=\"preserve\"> "
                       "</w:t></w:r></w:fldSimple></w:p>")
            .arg(maxLevel);

    const int base = appendPool(xml);
    const QString openTag = QStringLiteral("<w:p>");
    const QString pPr     = QStringLiteral("<w:pPr><w:pageBreakBefore/></w:pPr>");
    const QString closeTag = QStringLiteral("</w:p>");
    Block b;
    b.kind         = Block::Paragraph;
    b.rawSpan      = { base, xml.size() };
    b.startTagSpan = { base, int(openTag.size()) };
    //  Der pPr-Span MUSS eigenständig sein: sobald das Zeichenformat des
    //  Verzeichnisses gesetzt wird, materialisiert der Controller ihn, und
    //  buildParagraphXml setzt Start-Tag + pPr + Runs neu zusammen. Läge das
    //  pPr im Roh-Span des Runs, stünde es danach doppelt im Absatz.
    b.pprSpan      = { base + int(openTag.size()), int(pPr.size()) };
    //  EIN atomarer opaker Run: der Absatz ist damit nicht versehentlich
    //  bearbeitbar, und beim Speichern geht sein Roh-Span verbatim heraus.
    Run r;
    const int runStart = base + int(openTag.size()) + int(pPr.size());
    r.rawSpan = { runStart, base + xml.size() - int(closeTag.size()) - runStart };
    r.opaque  = true;
    r.text    = QString(kObjectChar);
    b.runs.append(r);
    blocks.insert(beforeBlock, b);
    return beforeBlock;
}

//  Bild IM Fließtext: der `w:drawing`-Run wird an einer bestehenden Run-Grenze
//  eingesetzt. Der Absatz wird dadurch dirty und beim Speichern aus seinen
//  Teilen serialisiert — der neue Run trägt einen echten Span in den
//  Anhang-Pool, alle übrigen bleiben ihre Original-Spans.
int Document::insertImageRunAt(int blockIdx, int runIdx, const QByteArray& bytes,
                               const QString& ext, QString* err,
                               qint64 cxEmu, qint64 cyEmu) {
    if (blockIdx < 0 || blockIdx >= blocks.size()) return -1;
    Block& b = blocks[blockIdx];
    if (b.kind != Block::Paragraph) {
        if (err) *err = QStringLiteral("Hier kann kein Bild stehen.");
        return -1;
    }
    const QString runXml = buildImageRunXml(bytes, ext, err, cxEmu, cyEmu);
    if (runXml.isEmpty()) return -1;

    const int base = appendPool(runXml);
    Run r;
    r.rawSpan = { base, int(runXml.size()) };
    r.opaque  = true;
    r.text    = QString(kObjectChar);
    const int at = qBound(0, runIdx, int(b.runs.size()));
    b.runs.insert(at, r);
    b.dirty = true;
    return at;
}

int Document::insertImage(int beforeBlock, const QString& localPath, QString* err) {
    QFile f(localPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("Bilddatei nicht lesbar.");
        return -1;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    return insertImageData(beforeBlock, bytes, QFileInfo(localPath).suffix(), err);
}

QString Document::buildImageRunXml(const QByteArray& bytes, const QString& extIn,
                                   QString* err, qint64 cxEmu, qint64 cyEmu) {
    QImage probe;
    if (bytes.isEmpty() || !probe.loadFromData(bytes) || probe.isNull()) {
        if (err) *err = QStringLiteral("Datei ist kein lesbares Bild.");
        return {};
    }

    QString ext = extIn.toLower();
    if (ext == QLatin1String("jpe")) ext = QStringLiteral("jpg");
    //  Nur Endungen, für die mediaParts() einen Content-Type kennt — sonst
    //  landete ein Teil ohne <Default> im Container und Word verweigerte ihn.
    static const QStringList kKnown = {
        QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"),  QStringLiteral("bmp"), QStringLiteral("tif"),
        QStringLiteral("tiff"), QStringLiteral("webp")
    };
    if (!kKnown.contains(ext)) ext = QStringLiteral("png");

    PendingMedia pm;
    pm.ext     = ext;
    pm.relId   = QStringLiteral("rIdMGimg%1").arg(m_nextMediaId);
    pm.zipName = QStringLiteral("word/media/mgimg%1.%2").arg(m_nextMediaId).arg(ext);
    pm.bytes   = bytes;
    ++m_nextMediaId;
    m_pendingMedia.append(pm);
    m_rels.insert(pm.relId, pm.zipName.mid(QStringLiteral("word/").size()));

    //  Größe: vorgegeben (Kopieren im Editor — sonst käme das Bild in voller
    //  Auflösung zurück statt in der Größe, die es im Dokument hatte) oder aus
    //  den nativen Pixeln bei 96 dpi. Gedeckelt wird auf die Textbreite.
    constexpr qint64 kEmuPerPx = 914400 / 96;
    const bool given = cxEmu > 0 && cyEmu > 0;
    qint64 cx = given ? cxEmu : qint64(probe.width())  * kEmuPerPx;
    qint64 cy = given ? cyEmu : qint64(probe.height()) * kEmuPerPx;
    const qint64 maxCx = qint64(qMax(1000, m_section.pageW - m_section.marLeft
                                           - m_section.marRight)) * 635;  // Twip→EMU
    if (cx > maxCx && cx > 0) {
        cy = cy * maxCx / cx;
        cx = maxCx;
    }
    cx = qBound<qint64>(1LL, cx, 182880000LL);
    cy = qBound<qint64>(1LL, cy, 182880000LL);

    //  Namensräume BEWUSST inline: das Wirtsdokument deklariert wp:/a:/pic:
    //  (und teils nicht einmal r:) nicht zwingend am Wurzelelement.
    const QString runXml = QStringLiteral(
        "<w:r><w:drawing>"
        "<wp:inline xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/"
        "wordprocessingDrawing\">"
        "<wp:extent cx=\"%1\" cy=\"%2\"/>"
        "<wp:docPr id=\"%3\" name=\"Bild %3\"/>"
        "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
        "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
        "<pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"Bild %3\"/><pic:cNvPicPr/></pic:nvPicPr>"
        "<pic:blipFill>"
        "<a:blip xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships\" r:embed=\"%4\"/>"
        "<a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
        "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
        "</pic:pic></a:graphicData></a:graphic></wp:inline>"
        "</w:drawing></w:r>")
        .arg(cx).arg(cy).arg(m_nextMediaId - 1).arg(pm.relId);
    return runXml;
}

int Document::insertImageData(int beforeBlock, const QByteArray& bytes,
                              const QString& extIn, QString* err,
                              qint64 cxEmu, qint64 cyEmu) {
    beforeBlock = qBound(0, beforeBlock, int(blocks.size()));
    const QString runXml = buildImageRunXml(bytes, extIn, err, cxEmu, cyEmu);
    if (runXml.isEmpty()) return -1;

    QString xml = QStringLiteral("<w:p>");
    const int tagLen = xml.size();
    const int runStart = xml.size();
    xml += runXml;
    const int runLen = xml.size() - runStart;
    xml += QStringLiteral("</w:p>");

    const int base = appendPool(xml);
    Block b;
    b.kind         = Block::Paragraph;
    b.rawSpan      = { base, xml.size() };
    b.startTagSpan = { base, tagLen };
    Run r;
    r.rawSpan = { base + runStart, runLen };
    r.opaque  = true;
    r.text    = QString(kObjectChar);
    b.runs.append(r);
    blocks.insert(beforeBlock, b);
    return beforeBlock;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tabellen-STRUKTUR bearbeiten (Zeilen/Spalten/Breiten)
//
//  Das Gerüst besteht aus Spans ins Original und ist damit unveränderlich.
//  Geändert wird deshalb nach dem bewährten Muster Block::pprXml: die betroffene
//  Stelle wird EINMAL materialisiert (headerXml/cellXml) und ab dann statt des
//  Spans emittiert. Neu hinzukommende Zeilen/Zellen bekommen echte Spans in den
//  Anhang-Pool — sie brauchen keine Sonderbehandlung.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

//  Kanonische Kindreihenfolge von w:tcPr (ECMA-376, CT_TcPr).
const QStringList& tcPrOrder() {
    static const QStringList o = {
        QStringLiteral("w:cnfStyle"),   QStringLiteral("w:tcW"),
        QStringLiteral("w:gridSpan"),   QStringLiteral("w:hMerge"),
        QStringLiteral("w:vMerge"),     QStringLiteral("w:tcBorders"),
        QStringLiteral("w:shd"),        QStringLiteral("w:noWrap"),
        QStringLiteral("w:tcMar"),      QStringLiteral("w:textDirection"),
        QStringLiteral("w:tcFitText"),  QStringLiteral("w:vAlign"),
        QStringLiteral("w:hideMark")
    };
    return o;
}

//  Index hinter dem '>' des ersten Start-Tags (quote-bewusst); −1 = keins.
int afterStartTag(const QString& xml) {
    const int lt = xml.indexOf(QLatin1Char('<'));
    if (lt < 0) return -1;
    bool inQ = false; QChar q;
    for (int i = lt + 1; i < xml.size(); ++i) {
        const QChar c = xml.at(i);
        if (inQ) { if (c == q) inQ = false; continue; }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { inQ = true; q = c; continue; }
        if (c == QLatin1Char('>')) return i + 1;
    }
    return -1;
}

//  Balancierter Bereich eines Kind-Elements `name` in `xml` (auch
//  selbstschließend); liefert false, wenn es nicht vorkommt.
bool findElement(const QString& xml, const QString& name, int* start, int* len) {
    int i = 0;
    while (i < xml.size()) {
        const int lt = xml.indexOf(QLatin1Char('<'), i);
        if (lt < 0) return false;
        bool inQ = false; QChar q; int gt = -1;
        for (int k = lt + 1; k < xml.size(); ++k) {
            const QChar c = xml.at(k);
            if (inQ) { if (c == q) inQ = false; continue; }
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { inQ = true; q = c; continue; }
            if (c == QLatin1Char('>')) { gt = k; break; }
        }
        if (gt < 0) return false;
        int s = lt + 1;
        if (s < xml.size() && xml.at(s) == QLatin1Char('/')) { i = gt + 1; continue; }
        int e = s;
        while (e < gt && !xml.at(e).isSpace() && xml.at(e) != QLatin1Char('/')
               && xml.at(e) != QLatin1Char('>'))
            ++e;
        if (xml.mid(s, e - s) != name) { i = gt + 1; continue; }
        if (xml.at(gt - 1) == QLatin1Char('/')) {           // selbstschließend
            *start = lt; *len = gt - lt + 1;
            return true;
        }
        const QString close = QLatin1String("</") + name + QLatin1Char('>');
        const int ce = xml.indexOf(close, gt + 1);
        if (ce < 0) return false;
        *start = lt; *len = ce + close.size() - lt;
        return true;
    }
    return false;
}

//  w:tcW eines Zell-Präfixes ("<w:tc …>" + tcPr) auf `widthTw` setzen.
QString withCellWidth(const QString& cellPrefix, int widthTw) {
    const QString prop = QStringLiteral("<w:tcW w:w=\"%1\" w:type=\"dxa\"/>").arg(widthTw);
    int s = 0, l = 0;
    if (findElement(cellPrefix, QStringLiteral("w:tcPr"), &s, &l)) {
        QString frag = cellPrefix.mid(s, l);
        if (frag.endsWith(QLatin1String("/>")))              // <w:tcPr/> öffnen
            frag = QStringLiteral("<w:tcPr></w:tcPr>");
        frag = Document::upsertProp(frag, QStringLiteral("w:tcPr"),
                                    QStringLiteral("w:tcW"), prop, tcPrOrder());
        QString out = cellPrefix;
        out.replace(s, l, frag);
        return out;
    }
    const int at = afterStartTag(cellPrefix);
    if (at < 0) return cellPrefix;
    QString out = cellPrefix;
    out.insert(at, QStringLiteral("<w:tcPr>") + prop + QStringLiteral("</w:tcPr>"));
    return out;
}

//  w:tblGrid eines Tabellen-Headers durch das neue Gitter ersetzen.
QString withGrid(const QString& header, const QVector<int>& widthsTw) {
    QString grid = QStringLiteral("<w:tblGrid>");
    for (int w : widthsTw)
        grid += QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(qMax(1, w));
    grid += QStringLiteral("</w:tblGrid>");

    int s = 0, l = 0;
    QString out = header;
    if (findElement(header, QStringLiteral("w:tblGrid"), &s, &l)) {
        out.replace(s, l, grid);
        return out;
    }
    //  Kein Gitter vorhanden: hinter tblPr, sonst hinter "<w:tbl …>".
    int at = afterStartTag(header);
    if (findElement(header, QStringLiteral("w:tblPr"), &s, &l))
        at = s + l;
    if (at < 0) return header;
    out.insert(at, grid);
    return out;
}

} // namespace

void Document::rowCellRange(const TableDef& d, int row, int* first, int* last) {
    *first = -1; *last = -2;
    if (row < 0 || row >= d.rowFirstCell.size()) return;
    *first = d.rowFirstCell.at(row);
    *last = (row + 1 < d.rowFirstCell.size()) ? d.rowFirstCell.at(row + 1) - 1
                                              : d.cellSpans.size() - 1;
}

QString Document::tableHeaderText(const TableDef& d) const {
    if (!d.headerXml.isEmpty()) return d.headerXml;
    return m_docXml.mid(d.headerSpan.start, d.headerSpan.len);
}

QString Document::tableCellText(const TableDef& d, int cellIdx) const {
    if (cellIdx >= 0 && cellIdx < d.cellXml.size()
        && !d.cellXml.at(cellIdx).isEmpty())
        return d.cellXml.at(cellIdx);
    if (cellIdx < 0 || cellIdx >= d.cellSpans.size()) return {};
    const Span& s = d.cellSpans.at(cellIdx);
    return m_docXml.mid(s.start, s.len);
}

void Document::materializeGrid(TableDef& d, const QVector<int>& widthsTw) {
    d.headerXml = withGrid(tableHeaderText(d), widthsTw);
    d.gridTw.clear();
    for (int w : widthsTw) d.gridTw.append(qMax(1, w));

    if (d.cellXml.size() != d.cellSpans.size())
        d.cellXml.resize(d.cellSpans.size());
    for (int r = 0; r < d.rowFirstCell.size(); ++r) {
        int first = 0, last = -1;
        rowCellRange(d, r, &first, &last);
        for (int c = first; c <= last; ++c) {
            const int col = c - first;
            const int w = qMax(1, widthsTw.value(col, widthsTw.isEmpty()
                                                          ? 1000
                                                          : widthsTw.last()));
            d.cellXml[c] = withCellWidth(tableCellText(d, c), w);
            if (c < d.cellWidthTw.size()) d.cellWidthTw[c] = w;
        }
    }
    d.structDirty = true;
}

int Document::tableRowCount(int tableId) const {
    if (tableId < 0 || tableId >= m_tables.size()) return 0;
    return m_tables.at(tableId).rowSpans.size();
}

int Document::tableColumnCount(int tableId) const {
    if (tableId < 0 || tableId >= m_tables.size()) return 0;
    int first = 0, last = -1;
    rowCellRange(m_tables.at(tableId), 0, &first, &last);
    return qMax(0, last - first + 1);
}

bool Document::tableStructEditable(int tableId) const {
    if (tableId < 0 || tableId >= m_tables.size()) return false;
    const TableDef& d = m_tables.at(tableId);
    if (d.rowSpans.isEmpty() || d.cellSpans.isEmpty()) return false;
    if (tableFirstBlock(tableId) < 0) return false;         // gelöschte Tabelle

    const int cols = tableColumnCount(tableId);
    if (cols <= 0) return false;
    for (int r = 0; r < d.rowSpans.size(); ++r) {
        int first = 0, last = -1;
        rowCellRange(d, r, &first, &last);
        if (last - first + 1 != cols) return false;         // ungleiche Zeilen
    }
    for (int c = 0; c < d.cellSpans.size(); ++c) {
        if (d.cellGridSpan.value(c, 1) != 1) return false;   // verbundene Zellen
        const QString pre = tableCellText(d, c);
        if (pre.contains(QLatin1String("w:vMerge"))
            || pre.contains(QLatin1String("w:hMerge")))
            return false;
    }
    return true;
}

QVector<int> Document::tableColumnWidths(int tableId) const {
    QVector<int> out;
    if (tableId < 0 || tableId >= m_tables.size()) return out;
    const TableDef& d = m_tables.at(tableId);
    const int cols = tableColumnCount(tableId);
    if (cols <= 0) return out;

    for (int c = 0; c < cols; ++c) {
        int w = d.gridTw.value(c, 0);
        if (w <= 0) w = d.cellWidthTw.value(d.rowFirstCell.value(0, 0) + c, 0);
        out.append(w);
    }
    //  Kein brauchbares Gitter → gleichmäßig über die Textbreite.
    bool anyZero = false;
    for (int w : out) if (w <= 0) anyZero = true;
    if (anyZero) {
        const int textW = qMax(1000, m_section.pageW - m_section.marLeft
                                         - m_section.marRight);
        for (int& w : out) w = textW / cols;
    }
    return out;
}

TableDef Document::tableDef(int tableId) const {
    if (tableId < 0 || tableId >= m_tables.size()) return {};
    return m_tables.at(tableId);
}

void Document::setTableDef(int tableId, const TableDef& def) {
    if (tableId < 0 || tableId >= m_tables.size()) return;
    m_tables[tableId] = def;
}

bool Document::tableInsertRow(int tableId, int atRow) {
    if (!tableStructEditable(tableId)) return false;
    TableDef& d = m_tables[tableId];
    const int rows = d.rowSpans.size();
    const int cols = tableColumnCount(tableId);
    atRow = qBound(0, atRow, rows);
    if (rows >= 200) return false;

    const QVector<int> widths = tableColumnWidths(tableId);

    //  XML der neuen Zeile in EINEM Stück bauen (Offsets beim Bauen mitschreiben).
    struct LocalSpan { int start = 0; int len = 0; };
    QString xml;
    QVector<LocalSpan> cellPre, cellEnd, parAll, parTag;
    const int rowPreStart = 0;
    xml += QStringLiteral("<w:tr>");
    const int rowPreLen = xml.size() - rowPreStart;
    for (int c = 0; c < cols; ++c) {
        const int cp = xml.size();
        xml += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/>"
                              "</w:tcPr>").arg(qMax(1, widths.value(c, 1000)));
        cellPre.append({ cp, xml.size() - cp });
        const int pp = xml.size();
        xml += QStringLiteral("<w:p>");
        const int tagLen = xml.size() - pp;
        xml += QStringLiteral("</w:p>");
        parAll.append({ pp, xml.size() - pp });
        parTag.append({ pp, tagLen });
        const int ce = xml.size();
        xml += QStringLiteral("</w:tc>");
        cellEnd.append({ ce, xml.size() - ce });
    }
    const int rowEndStart = xml.size();
    xml += QStringLiteral("</w:tr>");
    const int rowEndLen = xml.size() - rowEndStart;

    const int base = appendPool(xml);
    auto abs = [base](const LocalSpan& s) { return Span{ base + s.start, s.len }; };

    //  Gerüst einsetzen.
    const int cellBase = (atRow < rows) ? d.rowFirstCell.at(atRow) : d.cellSpans.size();
    d.rowSpans.insert(atRow, Span{ base + rowPreStart, rowPreLen });
    d.rowEndSpans.insert(atRow, Span{ base + rowEndStart, rowEndLen });
    if (d.cellXml.size() != d.cellSpans.size()) d.cellXml.resize(d.cellSpans.size());
    for (int c = cols - 1; c >= 0; --c) {
        d.cellSpans.insert(cellBase, abs(cellPre.at(c)));
        d.cellEndSpans.insert(cellBase, abs(cellEnd.at(c)));
        d.cellRow.insert(cellBase, atRow);
        d.cellGridSpan.insert(cellBase, 1);
        d.cellWidthTw.insert(cellBase, qMax(1, widths.value(c, 1000)));
        d.cellXml.insert(cellBase, QString());
    }
    for (int c = cellBase + cols; c < d.cellRow.size(); ++c)
        d.cellRow[c] += 1;
    d.rowFirstCell.insert(atRow, cellBase);
    for (int r = atRow + 1; r < d.rowFirstCell.size(); ++r)
        d.rowFirstCell[r] += cols;
    d.structDirty = true;

    //  Blöcke einsetzen: vor den ersten Block der bisherigen Zeile `atRow`,
    //  bzw. hinter den letzten Block der Tabelle.
    int at = -1;
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks.at(i).tableId == tableId && blocks.at(i).row >= atRow) { at = i; break; }
    }
    if (at < 0) at = tableLastBlock(tableId) + 1;
    for (int i = 0; i < blocks.size(); ++i)
        if (blocks.at(i).tableId == tableId && blocks.at(i).row >= atRow)
            blocks[i].row += 1;
    for (int c = 0; c < cols; ++c) {
        Block b;
        b.kind         = Block::Paragraph;
        b.rawSpan      = abs(parAll.at(c));
        b.startTagSpan = abs(parTag.at(c));
        b.tableId      = tableId;
        b.row          = atRow;
        b.col          = c;
        blocks.insert(at + c, b);
    }
    return true;
}

bool Document::tableDeleteRow(int tableId, int row) {
    if (!tableStructEditable(tableId)) return false;
    TableDef& d = m_tables[tableId];
    const int rows = d.rowSpans.size();
    const int cols = tableColumnCount(tableId);
    if (rows <= 1 || row < 0 || row >= rows) return false;

    const int cellBase = d.rowFirstCell.at(row);
    if (d.cellXml.size() != d.cellSpans.size()) d.cellXml.resize(d.cellSpans.size());
    for (int c = 0; c < cols; ++c) {
        d.cellSpans.removeAt(cellBase);
        d.cellEndSpans.removeAt(cellBase);
        d.cellRow.removeAt(cellBase);
        d.cellGridSpan.removeAt(cellBase);
        d.cellWidthTw.removeAt(cellBase);
        d.cellXml.removeAt(cellBase);
    }
    for (int c = cellBase; c < d.cellRow.size(); ++c)
        d.cellRow[c] -= 1;
    d.rowSpans.removeAt(row);
    d.rowEndSpans.removeAt(row);
    d.rowFirstCell.removeAt(row);
    for (int r = row; r < d.rowFirstCell.size(); ++r)
        d.rowFirstCell[r] -= cols;
    d.structDirty = true;

    for (int i = blocks.size() - 1; i >= 0; --i) {
        Block& b = blocks[i];
        if (b.tableId != tableId) continue;
        if (b.row == row) blocks.removeAt(i);
        else if (b.row > row) b.row -= 1;
    }
    return true;
}

bool Document::tableInsertColumn(int tableId, int atCol) {
    if (!tableStructEditable(tableId)) return false;
    TableDef& d = m_tables[tableId];
    const int rows = d.rowSpans.size();
    const int cols = tableColumnCount(tableId);
    atCol = qBound(0, atCol, cols);
    if (cols >= 64) return false;

    //  Gesamtbreite konstant halten (wie Word): die neue Spalte bekommt den
    //  gleichen Anteil, die übrigen schrumpfen proportional.
    QVector<int> widths = tableColumnWidths(tableId);
    int total = 0;
    for (int w : widths) total += w;
    if (total <= 0) total = qMax(1000, m_section.pageW - m_section.marLeft
                                           - m_section.marRight);
    const int newW = qMax(200, total / (cols + 1));
    int rest = qMax(cols * 100, total - newW);
    int sum = 0;
    for (int w : widths) sum += w;
    QVector<int> out;
    int acc = 0;
    for (int i = 0; i < widths.size(); ++i) {
        const int w = (sum > 0) ? qMax(100, widths.at(i) * rest / sum) : rest / cols;
        out.append(w);
        acc += w;
    }
    Q_UNUSED(acc)
    out.insert(atCol, newW);

    //  Je Zeile eine Zelle + Absatzblock (alles in EINEM Pool-Stück).
    struct LocalSpan { int start = 0; int len = 0; };
    QString xml;
    QVector<LocalSpan> cellPre, cellEnd, parAll, parTag;
    for (int r = 0; r < rows; ++r) {
        const int cp = xml.size();
        xml += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/>"
                              "</w:tcPr>").arg(newW);
        cellPre.append({ cp, xml.size() - cp });
        const int pp = xml.size();
        xml += QStringLiteral("<w:p>");
        const int tagLen = xml.size() - pp;
        xml += QStringLiteral("</w:p>");
        parAll.append({ pp, xml.size() - pp });
        parTag.append({ pp, tagLen });
        const int ce = xml.size();
        xml += QStringLiteral("</w:tc>");
        cellEnd.append({ ce, xml.size() - ce });
    }
    const int base = appendPool(xml);
    auto abs = [base](const LocalSpan& s) { return Span{ base + s.start, s.len }; };

    if (d.cellXml.size() != d.cellSpans.size()) d.cellXml.resize(d.cellSpans.size());
    //  Von hinten nach vorn, damit die Indizes der noch offenen Zeilen stimmen.
    for (int r = rows - 1; r >= 0; --r) {
        const int at = d.rowFirstCell.at(r) + atCol;
        d.cellSpans.insert(at, abs(cellPre.at(r)));
        d.cellEndSpans.insert(at, abs(cellEnd.at(r)));
        d.cellRow.insert(at, r);
        d.cellGridSpan.insert(at, 1);
        d.cellWidthTw.insert(at, newW);
        d.cellXml.insert(at, QString());
        for (int r2 = r + 1; r2 < d.rowFirstCell.size(); ++r2)
            d.rowFirstCell[r2] += 1;
    }

    //  Blöcke: je Zeile einen neuen Zell-Absatz an der richtigen Stelle.
    for (int r = rows - 1; r >= 0; --r) {
        int at = -1;
        for (int i = 0; i < blocks.size(); ++i) {
            const Block& b = blocks.at(i);
            if (b.tableId == tableId && b.row == r && b.col >= atCol) { at = i; break; }
        }
        if (at < 0) {
            for (int i = blocks.size() - 1; i >= 0; --i) {
                const Block& b = blocks.at(i);
                if (b.tableId == tableId && b.row == r) { at = i + 1; break; }
            }
        }
        if (at < 0) continue;
        for (int i = 0; i < blocks.size(); ++i) {
            Block& b = blocks[i];
            if (b.tableId == tableId && b.row == r && b.col >= atCol) b.col += 1;
        }
        Block b;
        b.kind         = Block::Paragraph;
        b.rawSpan      = abs(parAll.at(r));
        b.startTagSpan = abs(parTag.at(r));
        b.tableId      = tableId;
        b.row          = r;
        b.col          = atCol;
        blocks.insert(at, b);
    }

    materializeGrid(d, out);
    return true;
}

bool Document::tableDeleteColumn(int tableId, int col) {
    if (!tableStructEditable(tableId)) return false;
    TableDef& d = m_tables[tableId];
    const int rows = d.rowSpans.size();
    const int cols = tableColumnCount(tableId);
    if (cols <= 1 || col < 0 || col >= cols) return false;

    //  Breite der entfallenden Spalte proportional auf die übrigen verteilen.
    QVector<int> widths = tableColumnWidths(tableId);
    int total = 0;
    for (int w : widths) total += w;
    const int gone = widths.value(col, 0);
    widths.removeAt(col);
    int rest = total - gone;
    if (rest <= 0) rest = total;
    int sum = 0;
    for (int w : widths) sum += w;
    QVector<int> out;
    for (int w : widths)
        out.append(sum > 0 ? qMax(100, w * total / sum) : total / qMax(1, cols - 1));

    if (d.cellXml.size() != d.cellSpans.size()) d.cellXml.resize(d.cellSpans.size());
    for (int r = rows - 1; r >= 0; --r) {
        const int at = d.rowFirstCell.at(r) + col;
        d.cellSpans.removeAt(at);
        d.cellEndSpans.removeAt(at);
        d.cellRow.removeAt(at);
        d.cellGridSpan.removeAt(at);
        d.cellWidthTw.removeAt(at);
        d.cellXml.removeAt(at);
        for (int r2 = r + 1; r2 < d.rowFirstCell.size(); ++r2)
            d.rowFirstCell[r2] -= 1;
    }
    for (int i = blocks.size() - 1; i >= 0; --i) {
        Block& b = blocks[i];
        if (b.tableId != tableId) continue;
        if (b.col == col) blocks.removeAt(i);
        else if (b.col > col) b.col -= 1;
    }

    materializeGrid(d, out);
    return true;
}

bool Document::tableSetColumnWidths(int tableId, const QVector<int>& widthsTw) {
    if (!tableStructEditable(tableId)) return false;
    const int cols = tableColumnCount(tableId);
    if (widthsTw.size() != cols) return false;
    QVector<int> out;
    for (int w : widthsTw) out.append(qBound(200, w, 100000));
    materializeGrid(m_tables[tableId], out);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bildgröße ändern (A2)
//
//  Der Bild-Run bleibt OPAK: statt die Zeichnung neu zu bauen (und dabei
//  Zuschnitt, Effekte, Alternativtext und Umbruchangaben eines von Word
//  erzeugten Bildes zu verlieren) wird der bestehende Roh-Text kopiert, darin
//  werden NUR wp:extent und a:ext umgeschrieben, und das Ergebnis kommt in den
//  Anhang-Pool. Der Run zeigt danach dorthin — das Original bleibt unberührt.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

//  cx/cy im Start-Tag des ersten Elements `elem` setzen (Attribut anlegen,
//  falls es fehlt). false = Element nicht gefunden.
bool setExtentAttrs(QString& xml, const QString& elem, qint64 cx, qint64 cy) {
    int s = 0, l = 0;
    if (!findElement(xml, elem, &s, &l)) return false;
    QString tag = xml.mid(s, l);
    auto put = [&](QLatin1String name, qint64 v) {
        const QString key = QLatin1Char(' ') + name + QLatin1String("=\"");
        const int p = tag.indexOf(key);
        if (p < 0) {
            tag.insert(1 + elem.size(),
                       QStringLiteral(" %1=\"%2\"").arg(name).arg(v));
            return;
        }
        const int vs = p + key.size();
        const int ve = tag.indexOf(QLatin1Char('"'), vs);
        if (ve < 0) return;
        tag.replace(vs, ve - vs, QString::number(v));
    };
    put(QLatin1String("cx"), cx);
    put(QLatin1String("cy"), cy);
    xml.replace(s, l, tag);
    return true;
}

} // namespace

bool Document::setImageSizeEmu(int blockIdx, qint64 cxEmu, qint64 cyEmu) {
    if (blockIdx < 0 || blockIdx >= blocks.size()) return false;
    InlineImage info;
    if (!paragraphImage(blocks.at(blockIdx), &info)) return false;
    return setImageSizeEmu(blockIdx, info.run, cxEmu, cyEmu);
}

bool Document::setImageSizeEmu(int blockIdx, int runIdx, qint64 cxEmu,
                               qint64 cyEmu) {
    if (blockIdx < 0 || blockIdx >= blocks.size()) return false;
    Block& b = blocks[blockIdx];
    if (runIdx < 0 || runIdx >= b.runs.size()) return false;

    cxEmu = qBound<qint64>(9525LL, cxEmu, 182880000LL);  // 1 px … 200 Zoll
    cyEmu = qBound<qint64>(9525LL, cyEmu, 182880000LL);

    Run* run = &b.runs[runIdx];
    if (!run->opaque || run->text != QString(kObjectChar)
        || !run->rawSpan.valid())
        return false;

    QString xml = m_docXml.mid(run->rawSpan.start, run->rawSpan.len);
    const bool a = setExtentAttrs(xml, QStringLiteral("wp:extent"), cxEmu, cyEmu);
    //  a:ext steckt in pic:spPr/a:xfrm; fehlt es (manche Erzeuger lassen es
    //  weg), reicht wp:extent — Word skaliert dann darüber.
    setExtentAttrs(xml, QStringLiteral("a:ext"), cxEmu, cyEmu);
    if (!a) return false;

    const int base = appendPool(xml);
    run->rawSpan = { base, xml.size() };
    b.dirty = true;                    // Absatz aus seinen Teilen serialisieren
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Umbruchart: `wp:inline` ⇄ `wp:anchor` + `w:wrapSquare`
//
//  Beide Elemente tragen DIESELBEN Kinder — der Anker hat nur zusätzlich seine
//  Lage (`wp:simplePos`/`positionH`/`positionV`) und die Umbruchart davor bzw.
//  dazwischen. Umgeschrieben wird deshalb nicht die Zeichnung, sondern nur ihr
//  Rahmen: Kinder werden ÜBERNOMMEN, nicht neu gebaut — Zuschnitt, Effekte und
//  Alternativtext eines von Word erzeugten Bildes überleben das unverändert.
//  Kindreihenfolge nach ECMA-376 (CT_Anchor): simplePos · positionH ·
//  positionV · extent · effectExtent · wrap* · docPr · cNvGraphicFramePr ·
//  graphic.
//
//  DIESELBE Funktion trägt auch Lage und Umbruchseite eines bereits
//  verankerten Bildes ein (Ziehen mit der Maus, Menüeintrag „links/rechts
//  umfließen") — dafür wird der Rahmen genauso neu gebaut, nur mit anderen
//  Werten. Ein zweiter Schreibweg hätte dieselben Fallen (xmlns am Start-Tag,
//  Kindreihenfolge) ein zweites Mal lösen müssen.
// ─────────────────────────────────────────────────────────────────────────────
//  `w:wrapSquare` ▸ `wrapText` — die vier Werte, die ECMA-376 kennt.
static QLatin1String wrapTextValue(int side) {
    switch (side) {
    case InlineImage::SideLeft:    return QLatin1String("left");
    case InlineImage::SideRight:   return QLatin1String("right");
    case InlineImage::SideLargest: return QLatin1String("largest");
    default:                       return QLatin1String("bothSides");
    }
}

bool Document::setImageWrap(int blockIdx, int runIdx, bool floating) {
    //  Vorgabe `bothSides` — wie in Word: der Text läuft links UND rechts am
    //  Bild vorbei. Die Anzeige teilt ein Band dafür in zwei Stücke
    //  (`DocxTextArea`, `usableSpan`/`pendingRightX`); Datei und Anzeige sagen
    //  damit dasselbe. Ist eine Seite zu schmal, weicht die Anzeige von selbst
    //  auf die breitere aus.
    return rewriteDrawingFrame(blockIdx, runIdx, floating, 0, 0,
                               InlineImage::SideBoth, /*requireModeChange=*/true);
}

int Document::moveImageRun(int srcBlock, int runIdx, int dstBlock) {
    if (srcBlock < 0 || srcBlock >= blocks.size()) return -1;
    if (dstBlock < 0 || dstBlock >= blocks.size()) return -1;
    if (srcBlock == dstBlock) return -1;
    Block& src = blocks[srcBlock];
    Block& dst = blocks[dstBlock];
    if (src.kind != Block::Paragraph || dst.kind != Block::Paragraph) return -1;
    if (runIdx < 0 || runIdx >= src.runs.size()) return -1;
    //  Nur ein VERANKERTES Bild darf umziehen: ein Bild im Zeilenfluss gehört
    //  an seine Textstelle, ein Umhängen würde den Satz auseinanderreißen.
    InlineImage info;
    if (!imageOfRun(srcBlock, runIdx, &info) || !info.anchored) return -1;

    //  Der Run wandert unverändert — sein Roh-Span zeigt weiter auf dieselbe
    //  Zeichnung (Original-XML oder Anhang-Pool), beide sind absolute Stellen.
    const Run r = src.runs.at(runIdx);
    src.runs.removeAt(runIdx);
    src.dirty = true;
    //  Ans ENDE des Zielabsatzes: ein verankertes Bild belegt keine Zeilenbreite,
    //  seine Stelle im Text ist nur der Anker.
    dst.runs.append(r);
    dst.dirty = true;
    return int(dst.runs.size()) - 1;
}

bool Document::setImageAnchorEmu(int blockIdx, int runIdx, int posXEmu, int posYEmu) {
    InlineImage cur;
    if (!imageOfRun(blockIdx, runIdx, &cur) || !cur.anchored) return false;
    //  Weit außerhalb der Seite hätte niemand etwas davon — 200 Zoll ist
    //  dieselbe Grenze, die auch das Lesen klemmt.
    posXEmu = qBound(-182880000, posXEmu, 182880000);
    posYEmu = qBound(-182880000, posYEmu, 182880000);
    if (posXEmu == cur.posXEmu && posYEmu == cur.posYEmu) return false;
    return rewriteDrawingFrame(blockIdx, runIdx, true, posXEmu, posYEmu,
                               cur.wrapSide, /*requireModeChange=*/false);
}

bool Document::setImageWrapSide(int blockIdx, int runIdx, int side) {
    if (side < InlineImage::SideBoth || side > InlineImage::SideLargest) return false;
    InlineImage cur;
    if (!imageOfRun(blockIdx, runIdx, &cur) || !cur.anchored) return false;
    if (side == cur.wrapSide) return false;
    return rewriteDrawingFrame(blockIdx, runIdx, true, cur.posXEmu, cur.posYEmu,
                               side, /*requireModeChange=*/false);
}

//  Das Bild EINES Runs — die Auskunft, aus der Lage und Umbruchseite kommen.
bool Document::imageOfRun(int blockIdx, int runIdx, InlineImage* out) const {
    if (blockIdx < 0 || blockIdx >= blocks.size()) return false;
    for (const InlineImage& ii : paragraphImages(blocks.at(blockIdx)))
        if (ii.run == runIdx) { *out = ii; return true; }
    return false;
}

bool Document::rewriteDrawingFrame(int blockIdx, int runIdx, bool floating,
                                   int posXEmu, int posYEmu, int wrapSide,
                                   bool requireModeChange) {
    if (blockIdx < 0 || blockIdx >= blocks.size()) return false;
    Block& b = blocks[blockIdx];
    if (runIdx < 0 || runIdx >= b.runs.size()) return false;
    Run* run = &b.runs[runIdx];
    if (!run->opaque || run->text != QString(kObjectChar) || !run->rawSpan.valid())
        return false;

    const QString xml = m_docXml.mid(run->rawSpan.start, run->rawSpan.len);
    int elStart = -1, elLen = 0;
    QString tag;
    const bool wasFloating = findElement(xml, QStringLiteral("anchor"),
                                         &elStart, &elLen, &tag);
    if (!wasFloating
        && !findElement(xml, QStringLiteral("inline"), &elStart, &elLen, &tag))
        return false;
    //  Nur der Wechsel der Umbruchart darf „schon so" ablehnen; Lage und
    //  Umbruchseite prüfen ihre Gleichheit selbst (sie kennen die alten Werte).
    if (requireModeChange && wasFloating == floating) return false;

    const QString el = xml.mid(elStart, elLen);
    const int gt = tagEnd(el, 0);
    if (gt < 0) return false;
    const QString prefix = tag.contains(QLatin1Char(':'))
                               ? tag.section(QLatin1Char(':'), 0, -2)
                                     + QLatin1Char(':')
                               : QString();
    const QString ns = keepXmlnsAttrs(el, 0, gt);

    //  Kinder einsammeln (die Lage-/Umbruch-Kinder eines Ankers fallen weg).
    QString extent, effect, docPr, frame, graphic, rest;
    for (const ChildRange& c : childRanges(el)) {
        const QString local = c.name.section(QLatin1Char(':'), -1);
        const QString frag = el.mid(c.start, c.len);
        if (local == QLatin1String("extent"))            extent  = frag;
        else if (local == QLatin1String("effectExtent")) effect  = frag;
        else if (local == QLatin1String("docPr"))        docPr   = frag;
        else if (local == QLatin1String("cNvGraphicFramePr")) frame = frag;
        else if (local == QLatin1String("graphic"))      graphic = frag;
        else if (local == QLatin1String("simplePos") || local == QLatin1String("positionH")
                 || local == QLatin1String("positionV")
                 || local.startsWith(QLatin1String("wrap")))
            continue;                                   // Lage/Umbruch neu
        else rest += frag;                              // Unbekanntes behalten
    }
    if (extent.isEmpty() || graphic.isEmpty())
        return false;                                   // nicht deutbar

    QString out;
    if (floating) {
        //  distL/distR halten den Text vom Bild ab (0,3 cm wie Word).
        out = QLatin1Char('<') + prefix + QLatin1String("anchor") + ns
            + QLatin1String(" distT=\"0\" distB=\"0\" distL=\"114300\""
                            " distR=\"114300\" simplePos=\"0\""
                            " relativeHeight=\"2\" behindDoc=\"0\" locked=\"0\""
                            " layoutInCell=\"1\" allowOverlap=\"1\">")
            + QLatin1Char('<') + prefix + QLatin1String("simplePos x=\"0\" y=\"0\"/>")
            + QLatin1Char('<') + prefix + QLatin1String("positionH relativeFrom=\"column\"><")
            + prefix + QLatin1String("posOffset>") + QString::number(posXEmu)
            + QLatin1String("</") + prefix
            + QLatin1String("posOffset></") + prefix + QLatin1String("positionH>")
            + QLatin1Char('<') + prefix + QLatin1String("positionV relativeFrom=\"paragraph\"><")
            + prefix + QLatin1String("posOffset>") + QString::number(posYEmu)
            + QLatin1String("</") + prefix
            + QLatin1String("posOffset></") + prefix + QLatin1String("positionV>")
            + extent + effect
            + QLatin1Char('<') + prefix + QLatin1String("wrapSquare wrapText=\"")
            + wrapTextValue(wrapSide) + QLatin1String("\"/>")
            + docPr + frame + rest + graphic
            + QLatin1String("</") + prefix + QLatin1String("anchor>");
    } else {
        out = QLatin1Char('<') + prefix + QLatin1String("inline") + ns
            + QLatin1String(" distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">")
            + extent + effect + docPr + frame + rest + graphic
            + QLatin1String("</") + prefix + QLatin1String("inline>");
    }

    QString newXml = xml;
    newXml.replace(elStart, elLen, out);
    const int base = appendPool(newXml);
    run->rawSpan = { base, newXml.size() };
    b.dirty = true;
    return true;
}

int Document::tableFirstBlock(int tableId) const {
    if (tableId < 0) return -1;
    for (int i = 0; i < blocks.size(); ++i)
        if (blocks.at(i).tableId == tableId) return i;
    return -1;
}
int Document::tableLastBlock(int tableId) const {
    if (tableId < 0) return -1;
    for (int i = blocks.size() - 1; i >= 0; --i)
        if (blocks.at(i).tableId == tableId) return i;
    return -1;
}

//  Ein Lauf für BEIDES: Speichern (rawOnly=false) und Selbstprüfung
//  (rawOnly=true). Dass die Prüfung denselben Code nimmt, ist der Punkt — sie
//  prüft damit genau den Weg, den unangetastete Teile beim Speichern gehen.
QString Document::emitBlocks(bool rawOnly) const {
    const QStringView doc(m_docXml);
    QString out;
    out.reserve(m_docXml.size() + 1024);

    auto emitOne = [&](const Block& b) {
        if (rawOnly || b.kind != Block::Paragraph
            || (!b.dirty && !b.pprMaterialized && b.rawSpan.valid()))
            out += doc.mid(b.rawSpan.start, b.rawSpan.len);
        else
            out += buildParagraphXml(b);
    };

    for (int i = 0; i < blocks.size(); ) {
        const Block& b = blocks.at(i);
        if (b.tableId < 0) { emitOne(b); ++i; continue; }

        //  ── Tabelle als GRUPPE ───────────────────────────────────────────────
        const int tid = b.tableId;
        const TableDef& def = m_tables.at(tid);
        const Span tblRaw = def.rawSpan();
        int j = i;
        int groupSize = 0;
        bool anyDirty = false;
        while (j < blocks.size() && blocks.at(j).tableId == tid) {
            const Block& cb = blocks.at(j);
            if (cb.dirty || cb.pprMaterialized || !cb.rawSpan.valid()) anyDirty = true;
            //  Ein Block, der NICHT im Originalbereich der Tabelle liegt, ist neu
            //  (Bild/PDF-Seite/Verzeichnis in eine Zelle eingefügt — sein Span
            //  zeigt in den Anhang-Pool). Er ist weder dirty noch span-los, würde
            //  den Schnellpfad also nicht auslösen — und ginge beim Speichern
            //  spurlos verloren, weil der Schnellpfad den ALTEN Original-
            //  Teilstring der Tabelle ausgibt.
            else if (cb.rawSpan.start < tblRaw.start
                     || cb.rawSpan.start + cb.rawSpan.len > tblRaw.start + tblRaw.len)
                anyDirty = true;
            ++groupSize;
            ++j;
        }
        //  Struktur geändert (Zeile/Spalte/Breite) ⇒ NIE der Schnellpfad, auch
        //  wenn keine einzige Zelle berührt wurde.
        if (def.structDirty) anyDirty = true;
        //  Blockzahl verändert ⇒ ebenfalls nicht. Fängt das ENTFERNEN eines
        //  Blocks aus einer Zelle (Bild löschen): die übrigen liegen weiter im
        //  Originalbereich, der Schnellpfad brächte den gelöschten zurück.
        if (def.blockCount > 0 && groupSize != def.blockCount) anyDirty = true;
        if (rawOnly || !anyDirty) {
            //  SCHNELLPFAD: nichts berührt → der ganze <w:tbl>-Bereich als
            //  Original-Teilstring, also byteidentisch und ohne Gerüst-Aufbau.
            const Span raw = def.rawSpan();
            out += doc.mid(raw.start, raw.len);
            i = j;
            continue;
        }

        //  Gerüst wieder darumlegen: Header, je Zeile ihr Prefix, je Zelle ihr
        //  Prefix + die Absatz-Blöcke + Zell-Ende, Zeilen-Ende, Footer.
        out += tableHeaderText(def);
        int blockAt = i;
        for (int rowIdx = 0; rowIdx < def.rowSpans.size(); ++rowIdx) {
            out += doc.mid(def.rowSpans.at(rowIdx).start, def.rowSpans.at(rowIdx).len);
            const int firstCell = def.rowFirstCell.at(rowIdx);
            const int lastCell = (rowIdx + 1 < def.rowFirstCell.size())
                                     ? def.rowFirstCell.at(rowIdx + 1) - 1
                                     : def.cellSpans.size() - 1;
            for (int cellIdx = firstCell; cellIdx <= lastCell; ++cellIdx) {
                out += tableCellText(def, cellIdx);
                const int colOfCell = cellIdx - firstCell;
                while (blockAt < j && blocks.at(blockAt).tableId == tid
                       && blocks.at(blockAt).row == rowIdx
                       && blocks.at(blockAt).col == colOfCell) {
                    emitOne(blocks.at(blockAt));
                    ++blockAt;
                }
                out += doc.mid(def.cellEndSpans.at(cellIdx).start,
                               def.cellEndSpans.at(cellIdx).len);
            }
            out += doc.mid(def.rowEndSpans.at(rowIdx).start,
                           def.rowEndSpans.at(rowIdx).len);
        }
        out += doc.mid(def.footerSpan.start, def.footerSpan.len);
        i = j;
    }
    return out;
}

QString Document::newDocumentXml() const {
    QString out;
    out.reserve(m_docXml.size() + 1024);
    out += QStringView(m_docXml).mid(m_bodyPrefix.start, m_bodyPrefix.len);
    out += emitBlocks(false);
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
const char* kStylesContentType =
    "application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml";
const char* kStylesRelType =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles";

//  Maße der erzeugten Überschriftvorlagen — bewusst die Word-Vorgaben, damit
//  ein so ausgezeichnetes Dokument in Word genauso aussieht wie hier.
struct HeadingSpec { int halfPt; const char* color; int beforeTw; int afterTw; };
constexpr HeadingSpec kHeadingSpecs[Docx::Document::kMaxHeadingLevel] = {
    { 32, "2F5496", 240, 120 },   // Heading1 — 16 pt
    { 26, "2F5496", 200, 100 },   // Heading2 — 13 pt
    { 24, "1F3763", 200,  80 },   // Heading3 — 12 pt
};

} // namespace

//  Überschriftvorlage bei Bedarf ANLEGEN. Die meisten .docx bringen keine mit
//  (auch die eigene Fabrik emptyDocxBytes nicht) — die Auswahlliste hätte dann
//  nur die Standardvorlage und es ließe sich gar keine Überschrift schreiben.
QString Document::ensureHeadingStyle(int level) {
    if (level < 1 || level > kMaxHeadingLevel)
        return QString();
    const QString id = QStringLiteral("Heading%1").arg(level);
    if (m_styles.contains(id))
        return id;

    const HeadingSpec& s = kHeadingSpecs[level - 1];
    //  `w:name` ist der EINGEBAUTE englische Name — nur daran erkennt Word die
    //  Vorlage als Überschrift (Navigationsbereich, eigenes Verzeichnis). Die
    //  Anzeige übersetzt ihn, s. DocxSurface.
    const QString basedOn = m_defaultParStyle.isEmpty()
                                ? QString()
                                : QStringLiteral("<w:basedOn w:val=\"%1\"/>"
                                                 "<w:next w:val=\"%1\"/>")
                                      .arg(xmlEscape(m_defaultParStyle));
    //  pPr/rPr als eigene Fragmente: genau die reicht die Auflösung unten an
    //  parseParProps/parseRunProps weiter — die erwarten den Wrapper, nicht das
    //  ganze w:style (dessen erstes Element würden sie samt Inhalt überspringen).
    const QString pPr = QStringLiteral("<w:pPr><w:keepNext/><w:keepLines/>"
                                       "<w:spacing w:before=\"%1\" w:after=\"%2\"/>"
                                       "<w:outlineLvl w:val=\"%3\"/></w:pPr>")
                            .arg(s.beforeTw).arg(s.afterTw).arg(level - 1);
    const QString rPr = QStringLiteral("<w:rPr><w:b/><w:color w:val=\"%1\"/>"
                                       "<w:sz w:val=\"%2\"/><w:szCs w:val=\"%2\"/>"
                                       "</w:rPr>")
                            .arg(QLatin1String(s.color)).arg(s.halfPt);
    m_pendingStyles.append(
        QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"%1\">"
                       "<w:name w:val=\"heading %2\"/>%3<w:qFormat/>%4%5</w:style>")
            .arg(id).arg(level).arg(basedOn, pPr, rPr));

    //  Sofort in die Auflösung übernehmen — die Anzeige soll nicht erst nach
    //  dem Speichern folgen. Geparst wird dasselbe XML, das gespeichert wird.
    StyleDef def;
    def.basedOn = m_defaultParStyle;
    parseRunProps(rPr, &def.rf);
    parseParProps(pPr, &def.pf);
    m_styles.insert(id, def);

    StyleInfo info;
    info.id   = id;
    info.name = QStringLiteral("heading %1").arg(level);
    m_parStyles.append(info);
    return id;
}

//  Neu angelegte Vorlagen in word/styles.xml splicen. Wie bei numberingParts()
//  wird der Bestand VERBATIM übernommen und nur ergänzt. `base` sind die
//  bereits von den anderen Ketten geänderten Teile — gemeinsame Dateien
//  ([Content_Types].xml, die .rels) werden von dort statt aus dem Container
//  gelesen, damit sich die Ketten nicht gegenseitig überschreiben.
QHash<QString, QByteArray> Document::stylesParts(
    const QHash<QString, QByteArray>& base) const {
    QHash<QString, QByteArray> out;
    if (m_pendingStyles.isEmpty())
        return out;
    const QString defs = m_pendingStyles.join(QString());

    if (m_hadStylesPart) {
        QString xml = m_stylesXml;
        const int end = xml.lastIndexOf(QLatin1String("</w:styles>"));
        if (end < 0)                       // kein verwertbarer Bestand
            return out;
        xml.insert(end, defs);
        out.insert(QStringLiteral("word/styles.xml"), xml.toUtf8());
        return out;
    }

    //  styles.xml fehlt → komplette Teile-Kette anlegen (Muster numberingParts).
    out.insert(QStringLiteral("word/styles.xml"),
               QStringLiteral(
                   "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
                   "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/"
                   "wordprocessingml/2006/main\">%1</w:styles>").arg(defs).toUtf8());

    DocxZip::Reader zip;
    if (zip.open(m_path)) {
        //  Bereits geänderte Fassung bevorzugen (s. Kommentar oben).
        auto part = [&](const QString& name, bool* ok) -> QByteArray {
            const auto it = base.constFind(name);
            if (it != base.constEnd()) { *ok = true; return it.value(); }
            return zip.fileData(name, ok);
        };
        bool ok = false;
        QByteArray ct = part(QStringLiteral("[Content_Types].xml"), &ok);
        if (ok && !ct.contains("/word/styles.xml")) {
            QString s = QString::fromUtf8(ct);
            const int p = s.lastIndexOf(QLatin1String("</Types>"));
            if (p >= 0) {
                s.insert(p, QStringLiteral("<Override PartName=\"/word/styles.xml\" "
                                           "ContentType=\"%1\"/>")
                                .arg(QLatin1String(kStylesContentType)));
                out.insert(QStringLiteral("[Content_Types].xml"), s.toUtf8());
            }
        }
        const QString relsName = relsPathOf(m_partPath);
        QByteArray rels = part(relsName, &ok);
        if (ok) {
            if (!rels.contains("relationships/styles")) {
                QString s = QString::fromUtf8(rels);
                const int p = s.lastIndexOf(QLatin1String("</Relationships>"));
                if (p >= 0) {
                    s.insert(p, QStringLiteral("<Relationship Id=\"rIdMGsty\" "
                                               "Type=\"%1\" Target=\"styles.xml\"/>")
                                    .arg(QLatin1String(kStylesRelType)));
                    out.insert(relsName, s.toUtf8());
                }
            }
        } else {
            out.insert(relsName, QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
                "relationships\">"
                "<Relationship Id=\"rIdMGsty\" Type=\"%1\" Target=\"styles.xml\"/>"
                "</Relationships>").arg(QLatin1String(kStylesRelType)).toUtf8());
        }
    }
    return out;
}

//  Ersatz-/Zusatzteile beim Speichern = Listen-Infrastruktur + eingefügte
//  Medien. Bewusst zwei getrennte Funktionen: die Nummerierungs-Kette ist
//  erprobt und wird nicht angefasst.
QHash<QString, QByteArray> Document::replacementParts() const {
    QHash<QString, QByteArray> out = numberingParts();
    const QHash<QString, QByteArray> media = mediaParts();
    for (auto it = media.cbegin(); it != media.cend(); ++it) {
        //  [Content_Types].xml und die .rels können von BEIDEN stammen —
        //  dann gewinnt die Medien-Fassung, weil sie auf der anderen aufbaut.
        out.insert(it.key(), it.value());
    }
    //  Vorlagen ZULETZT und auf dem bisherigen Stand aufbauend: fehlt sowohl
    //  numbering.xml als auch styles.xml, ändern beide Ketten dieselbe
    //  [Content_Types].xml — die zweite muss die Fassung der ersten fortführen,
    //  sonst geht deren Override verloren.
    const QHash<QString, QByteArray> styles = stylesParts(out);
    for (auto it = styles.cbegin(); it != styles.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QHash<QString, QByteArray> Document::numberingParts() const {
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
        const QString relsName = relsPathOf(m_partPath);
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
    return writeTo(target, {}, err);
}

bool Document::writeTo(QIODevice* target,
                       const QHash<QString, QByteArray>& extraParts,
                       QString* err) const {
    DocxZip::Reader zip;
    if (!zip.open(m_path, err))
        return false;

    QHash<QString, QByteArray> repl = replacementParts();
    //  Fremde Teile ZUERST, damit die eigenen (numbering/media/rels) gewinnen,
    //  falls beide dieselbe Datei anfassen wollten.
    for (auto it = extraParts.constBegin(); it != extraParts.constEnd(); ++it)
        if (!repl.contains(it.key()))
            repl.insert(it.key(), it.value());
    repl.insert(m_partPath, newDocumentXml().toUtf8());

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
    //  Leerzeilen überspringen: eine Vorschau mit sechs Zeilen soll INHALT
    //  zeigen, nicht die Absatzabstände des Dokuments.
    auto push = [&lines, maxLines](const QString& line) {
        if (lines.size() < maxLines && !line.trimmed().isEmpty())
            lines << line;
    };
    while (!r.atEnd() && lines.size() < maxLines) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            if (r.qualifiedName() == QLatin1String("w:t")) {
                cur += r.readElementText();
            } else if (r.qualifiedName() == QLatin1String("w:tab")) {
                cur += QLatin1Char('\t');
            } else if (r.qualifiedName() == QLatin1String("w:br")
                       || r.qualifiedName() == QLatin1String("w:cr")) {
                //  Zeilenumbruch IM Absatz beendet auch die Vorschauzeile.
                //  Ohne das lief ein ganzes Dokument, das mit Umschalt+Enter
                //  statt Enter gesetzt ist, in EINER Zeile zusammen
                //  („1 NFJedes Attribut nur ein…" — Nutzerbefund).
                push(cur);
                cur.clear();
            }
        } else if (t == QXmlStreamReader::EndElement
                   && r.qualifiedName() == QLatin1String("w:p")) {
            push(cur);
            cur.clear();
        }
    }
    push(cur);
    return lines.join(QLatin1Char('\n'));
}

} // namespace Docx
