#include "pdf/edit/PdfTextReflow.h"
#include "pdf/edit/PdfObjects.h"
#include "pdf/edit/PdfEncodings.h"
#include "pdf/edit/PdfTextEditor.h"

#include <QFile>
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace mg::pdfobj;

namespace {

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

struct Line {
    int   first = 0, last = 0;      // Glyphen-Indizes, inklusive
    qreal x0 = 0, x1 = 0;           // linke/rechte Kante (PDF-Punkte)
    qreal top = 0;                  // Oberkante (für den Zeilenabstand)
    qreal fontSize = 0;             // größte Schriftgröße der Zeile
};

// Glyphen in Zeilen gruppieren: neue Zeile, sobald die Oberkante um mehr als eine halbe Schriftgröße springt
// oder der Text spürbar nach LINKS zurückgeht - unabhängig davon, ob der Erzeuger `Td`, `T*` oder `Tm` nutzt.
QVector<Line> groupLines(const QVector<mg::PdfGlyph>& g) {
    QVector<Line> lines;
    for (int i = 0; i < g.size(); ++i) {
        const mg::PdfGlyph& gl = g.at(i);
        const qreal fs = gl.fontSizePt > 0 ? gl.fontSizePt : gl.box.height();
        bool startNew = lines.isEmpty();
        if (!startNew) {
            Line& cur = lines.last();
            const qreal tol = 0.5 * qMax(fs, cur.fontSize);
            if (qAbs(gl.box.top() - cur.top) > tol)      startNew = true;
            else if (gl.box.left() < cur.x1 - 0.5 * fs)  startNew = true;
        }
        if (startNew) {
            Line l;
            l.first = l.last = i;
            l.x0 = gl.box.left();
            l.x1 = gl.box.right();
            l.top = gl.box.top();
            l.fontSize = fs;
            lines.push_back(l);
        } else {
            Line& cur = lines.last();
            cur.last = i;
            cur.x1 = qMax(cur.x1, gl.box.right());
            cur.x0 = qMin(cur.x0, gl.box.left());
            cur.fontSize = qMax(cur.fontSize, fs);
        }
    }
    return lines;
}

QString lineText(const QVector<mg::PdfGlyph>& g, const Line& l) {
    QString s;
    s.reserve(l.last - l.first + 1);
    for (int i = l.first; i <= l.last; ++i)
        s += g.at(i).ch;
    return s;
}

qreal avgAdvance(const QVector<mg::PdfGlyph>& g, int from, int to) {
    qreal sum = 0; int n = 0;
    for (int i = from; i <= to && i < g.size(); ++i) {
        if (g.at(i).box.width() > 0) { sum += g.at(i).box.width(); ++n; }
    }
    return n > 0 ? sum / n : 0.0;
}

struct Edit {
    qint64     start = 0;
    qint64     end   = 0;      // == start -> Einfügung
    QByteArray bytes;
};

QByteArray pnum(qreal v) {
    if (!std::isfinite(v)) v = 0.0;
    QByteArray b = QByteArray::number(v, 'f', 3);
    if (b.contains('.')) { while (b.endsWith('0')) b.chop(1); if (b.endsWith('.')) b.chop(1); }
    return (b == "-0") ? QByteArray("0") : b;
}

// `Tm` ist ABSOLUT - die y-Komponente wird verkleinert, nachfolgende relative Sprünge tragen die Verschiebung
// mit. `Td`/`TD` sind relativ und werden genau EINMAL je Textobjekt angefasst; `T*` und `'`/`"` gar nicht.
bool shiftStatement(const mg::PdfShowSpan& sp, qreal dy, bool alreadyShifted,
                    Edit* out, bool* nowShifted) {
    *nowShifted = alreadyShifted;
    //  ABSOLUTES Tm verankert die Zeile neu - es erbt NICHTS und muss deshalb
    //  immer angefasst werden. Relative Sprünge (Td/TD/T*) tragen eine bereits
    //  wirksame Verschiebung dagegen von selbst weiter.
    if (sp.posOp == "Tm" && sp.posArgs.size() >= 6) {
        QByteArray b;
        for (int k = 0; k < 5; ++k) { b += pnum(sp.posArgs.at(k)); b += ' '; }
        b += pnum(sp.posArgs.at(5) - dy);
        b += " Tm";
        *out = { sp.posStart, sp.posEnd, b };
        *nowShifted = true;
        return true;
    }
    if (alreadyShifted)
        return true;                                  // relativ -> erbt sie
    if ((sp.posOp == "Td" || sp.posOp == "TD") && sp.posArgs.size() >= 2) {
        const QByteArray b = pnum(sp.posArgs.at(0)) + " "
                           + pnum(sp.posArgs.at(1) - dy) + " " + sp.posOp;
        *out = { sp.posStart, sp.posEnd, b };
        *nowShifted = true;
        return true;
    }
    return false;                                     // nicht verschiebbar
}

// Darf der Absatz eine Zeile gewinnen? Geprüft werden Geometrie UND Schreibbarkeit: keine Grafik unterhalb,
// jede Zeile darunter verschiebbar, Zeilenabstand im Textraum bestimmbar, nichts fällt von der Seite.
bool canGrowParagraph(const mg::PdfPageText& page, const QVector<Line>& lines,
                      int start, int end, qreal leading, qreal* dyText) {
    const qreal boundary = lines.at(end).top + 0.5 * leading;   // Grenze „darunter"

    //  (1) Unterhalb darf NICHTS gemalt sein - Grafik wandert nicht mit.
    for (const QRectF& r : std::as_const(page.paints))
        if (r.bottom() > boundary)
            return false;

    //  (2) Zeilenabstand im TEXTRAUM aus den Anweisungen ablesen.
    //      Absolute Tm: Differenz zweier aufeinanderfolgender y-Werte.
    //      Relative Td/TD: der Sprung der letzten Absatzzeile selbst.
    auto spanOf = [&](int li) -> const mg::PdfShowSpan* {
        const int gi = lines.at(li).first;
        if (gi < 0 || gi >= page.glyphs.size()) return nullptr;
        const int si = page.glyphs.at(gi).showIndex;
        return (si >= 0 && si < page.spans.size()) ? &page.spans.at(si) : nullptr;
    };
    const mg::PdfShowSpan* last = spanOf(end);
    if (!last) return false;
    qreal dy = 0.0;
    if (last->posOp == "Tm" && last->posArgs.size() >= 6) {
        if (end <= start) return false;                       // nur EINE Zeile
        const mg::PdfShowSpan* prev = spanOf(end - 1);
        if (!prev || prev->posOp != "Tm" || prev->posArgs.size() < 6) return false;
        //  Gleiche Ausrichtung/Skalierung verlangt - sonst ist die Differenz
        //  der y-Werte kein Zeilenabstand.
        for (int k = 0; k < 4; ++k)
            if (qAbs(prev->posArgs.at(k) - last->posArgs.at(k)) > 0.0001) return false;
        dy = prev->posArgs.at(5) - last->posArgs.at(5);
    } else if ((last->posOp == "Td" || last->posOp == "TD")
               && last->posArgs.size() >= 2) {
        dy = -last->posArgs.at(1);
    } else {
        return false;                                          // T*, ', " -> nein
    }
    if (dy <= 0.0001) return false;

    // Die neue Zeile wird IM Textobjekt des Absatzes eingefügt und schiebt dessen Textmatrix schon eine Zeile
    // tiefer - relative Sprünge danach erben das, ein neues `BT` setzt es zurück.
    bool shifted = true;
    int  curObj  = last->objIndex;
    for (int li = end + 1; li < lines.size(); ++li) {
        if (lines.at(li).top <= boundary) continue;
        const mg::PdfShowSpan* sp = spanOf(li);
        if (!sp) return false;
        if (sp->objIndex != curObj) { curObj = sp->objIndex; shifted = false; }
        Edit e; bool now = false;
        if (!shiftStatement(*sp, dy, shifted, &e, &now)) return false;
        shifted = now;
    }

    //  (4) Nichts darf von der Seite fallen.
    qreal lowest = lines.at(end).top + lines.at(end).fontSize;
    for (int li = end + 1; li < lines.size(); ++li)
        lowest = qMax(lowest, lines.at(li).top + lines.at(li).fontSize);
    //  `leading` ist der Abstand in ANZEIGE-Punkten (die Zeilen der Seite),
    //  `dy` derselbe Schritt im Textraum - für die Seitenprüfung zählt der
    //  angezeigte.
    if (page.pageHeightPt > 0 && lowest + leading > page.pageHeightPt)
        return false;

    *dyText = dy;
    return true;
}

}   // namespace

namespace mg {

bool PdfTextReflow::planParagraph(const PdfPageText& page, int glyphIndex,
                                  PdfReflowPlan* out, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };
    if (!out) return fail("kein Ergebnis-Zeiger");
    *out = PdfReflowPlan();

    const QVector<PdfGlyph>& g = page.glyphs;
    if (g.isEmpty()) return fail("kein Text auf der Seite");
    if (glyphIndex < 0 || glyphIndex > g.size()) return fail("Zeichenindex außerhalb");
    const int gi = qMin(glyphIndex, g.size() - 1);

    const QVector<Line> lines = groupLines(g);
    if (lines.isEmpty()) return fail("keine Zeile erkannt");

    int hit = -1;
    for (int i = 0; i < lines.size(); ++i)
        if (gi >= lines.at(i).first && gi <= lines.at(i).last) { hit = i; break; }
    if (hit < 0) return fail("Zeile nicht gefunden");

    // Absatz eingrenzen
    //  Schritt 1: Block gleicher Bauart (Zeilenabstand, Schriftgröße, linke
    //  Kante). Der Zeilenabstand ergibt sich aus dem Nachbarn der Trefferzeile.
    const qreal fs = lines.at(hit).fontSize;
    qreal leading = 0;
    if (hit + 1 < lines.size())      leading = lines.at(hit + 1).top - lines.at(hit).top;
    else if (hit > 0)                leading = lines.at(hit).top - lines.at(hit - 1).top;
    if (leading <= 0) leading = 1.2 * fs;

    auto sameKind = [&](const Line& a, const Line& b) {
        const qreal gap = qAbs(b.top - a.top);
        if (gap > leading * 1.25 || gap < leading * 0.75) return false;
        if (qAbs(a.fontSize - b.fontSize) > 0.05 * qMax(a.fontSize, b.fontSize)) return false;
        return true;
    };

    int from = hit, to = hit;
    while (from > 0 && sameKind(lines.at(from - 1), lines.at(from))) --from;
    while (to + 1 < lines.size() && sameKind(lines.at(to), lines.at(to + 1))) ++to;

    // Der rechte Rand darf NICHT einfach die größte Kante sein: genau die Zeile, in die getippt wurde, steht über
    // den Rand hinaus und definierte ihn sonst selbst. Ragt die breiteste deutlich hinaus, gilt die zweitgrößte.
    const qreal adv = avgAdvance(g, lines.at(from).first, lines.at(to).last);
    const qreal fullTol = qMax(2.5 * adv, 0.5 * fs);
    // Woran erkennt man, dass die breiteste Zeile über den Rand hinausragt und nicht selbst der Rand IST? Daran,
    // dass ihr LETZTES WORT den Überstand erklärt - ein sauberer Umbruch hätte genau dieses Wort geschoben.
    int widest = from;
    for (int i = from; i <= to; ++i)
        if (lines.at(i).x1 > lines.at(widest).x1) widest = i;
    qreal restMax = 0;
    for (int i = from; i <= to; ++i)
        if (i != widest) restMax = qMax(restMax, lines.at(i).x1);
    const qreal maxR = lines.at(widest).x1;
    qreal lastWordW = 0;                       // letztes Wort der breitesten Zeile
    for (int i = lines.at(widest).last; i >= lines.at(widest).first; --i) {
        if (g.at(i).ch.isSpace()) break;
        lastWordW += g.at(i).box.width();
    }
    const bool overhang = (restMax > 0)
                          && (maxR - restMax <= lastWordW + adv + fullTol);
    const qreal rightMax = overhang ? restMax : maxR;
    auto isFull = [&](int i) { return lines.at(i).x1 >= rightMax - fullTol; };

    //  Schritt 3: Eine Zeile, die den Rand NICHT ausfüllt, BEENDET den Absatz.
    //  Nach oben: nur weitergehen, solange die Zeile darüber voll ist.
    int start = hit;
    while (start > from && isFull(start - 1)) --start;
    int end = hit;
    while (end < to && isFull(end)) ++end;

    //  Schritt 4: linke Kante - die erste Zeile darf eingezogen sein, alle
    //  weiteren müssen bündig stehen (sonst ist es kein Fließabsatz).
    if (end > start) {
        const qreal baseX = lines.at(start + 1).x0;
        for (int i = start + 1; i <= end; ++i)
            if (qAbs(lines.at(i).x0 - baseX) > 0.75 * fs)
                return fail("Zeilen nicht bündig -> kein Fließabsatz");
    }

    out->firstLine  = start;
    out->firstGlyph = lines.at(start).first;
    out->lineCount  = end - start + 1;

    struct Word { QString text; qreal width = 0; };
    QVector<Word> words;
    qreal spaceW = 0;
    {
        Word cur;
        for (int li = start; li <= end; ++li) {
            const Line& l = lines.at(li);
            out->oldLines << lineText(g, l);
            for (int i = l.first; i <= l.last; ++i) {
                const PdfGlyph& gl = g.at(i);
                if (gl.ch.isSpace()) {
                    if (spaceW <= 0 && gl.box.width() > 0) spaceW = gl.box.width();
                    if (!cur.text.isEmpty()) { words.push_back(cur); cur = Word(); }
                    continue;
                }
                cur.text += gl.ch;
                cur.width += gl.box.width();
            }
            //  Zeilenwechsel trennt Wörter (der Umbruch selbst trägt kein
            //  Leerzeichen im Strom).
            if (!cur.text.isEmpty()) { words.push_back(cur); cur = Word(); }
        }
        if (!cur.text.isEmpty()) words.push_back(cur);
    }
    if (spaceW <= 0) spaceW = qMax(0.25 * fs, 0.5 * adv);
    if (words.isEmpty()) return fail("keine Wörter im Absatz");

    QVector<QString> fresh;
    int w = 0;
    auto fillLine = [&](qreal capacity) {
        QString text;
        qreal used = 0;
        while (w < words.size()) {
            const qreal need = (text.isEmpty() ? 0.0 : spaceW) + words.at(w).width;
            if (!text.isEmpty() && used + need > capacity)
                break;
            if (!text.isEmpty()) text += QLatin1Char(' ');
            text += words.at(w).text;
            used += need;
            ++w;
        }
        return text;
    };
    for (int li = start; li <= end; ++li)
        fresh << fillLine(rightMax - lines.at(li).x0);

    // Passt der Rest nicht mehr: darf der Absatz eine ZEILE gewinnen?
    if (w < words.size()) {
        qreal dyText = 0.0;
        if (canGrowParagraph(page, lines, start, end, leading, &dyText)) {
            fresh << fillLine(rightMax - lines.at(end).x0);
            out->grew = true;
            out->growDyText = dyText;
        }
    }
    //  Was jetzt noch übrig ist, trägt die letzte Zeile - verloren gehen darf
    //  nichts, aber der Aufrufer erfährt es.
    if (w < words.size()) {
        QString& lastLine = fresh.last();
        while (w < words.size()) {
            if (!lastLine.isEmpty()) lastLine += QLatin1Char(' ');
            lastLine += words.at(w).text;
            ++w;
        }
        out->overflow = true;
    }

    out->newLines = fresh;
    // Vergleich auf WORTEBENE: der Umbruch schreibt einfache Leerzeichen, im Strom kann dieselbe Zeile doppelte
    // Abstände tragen. Ohne diese Normalisierung meldete jeder Aufruf eine Änderung und schriebe die Datei neu.
    auto norm = [](const QStringList& in) {
        QStringList out;
        for (const QString& s : in)
            out << s.split(QLatin1Char(' '), Qt::SkipEmptyParts).join(QLatin1Char(' '));
        return out;
    };
    out->changed = out->grew || (norm(out->newLines) != norm(out->oldLines));
    return true;
}

int PdfTextReflow::mapCaretIndex(const PdfReflowPlan& plan, int glyphIndex) {
    if (plan.firstGlyph < 0 || glyphIndex <= plan.firstGlyph || !plan.changed)
        return glyphIndex;                       // vor dem Absatz -> unberührt
    const QString oldText = plan.oldLines.join(QString());
    const QString newText = plan.newLines.join(QString());
    const int off = glyphIndex - plan.firstGlyph;
    if (off >= oldText.size())
        return plan.firstGlyph + newText.size(); // hinter dem Absatz -> ans Ende

    int want = 0;
    for (int i = 0; i < off; ++i)
        if (!oldText.at(i).isSpace()) ++want;
    int seen = 0;
    for (int i = 0; i < newText.size(); ++i) {
        if (seen == want && !newText.at(i).isSpace())
            return plan.firstGlyph + i;
        if (!newText.at(i).isSpace()) ++seen;
    }
    return plan.firstGlyph + newText.size();
}

bool PdfTextReflow::reflowParagraph(const QString& inputPath, const QString& outputPath,
                                    int pageIndex, int glyphIndex,
                                    PdfReflowPlan* planOut, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };

    QFile in(inputPath);
    if (!in.open(QIODevice::ReadOnly)) return fail("Quelle nicht lesbar");
    const QByteArray buf = in.readAll();
    in.close();
    if (buf.size() < 32 || !buf.startsWith("%PDF-")) return fail("kein PDF");
    if (buf.contains("/Encrypt")) return fail("verschlüsselt");

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

    PdfPageText page;
    if (!PdfTextLayout::buildForPage(inputPath, pageIndex, &page, err))
        return false;
    if (page.contentObj < 0) return fail("mehrteiliger /Contents -> nicht bearbeitbar");

    PdfReflowPlan plan;
    if (!planParagraph(page, glyphIndex, &plan, err))
        return false;
    if (planOut) *planOut = plan;
    if (!plan.changed) return fail("unveraendert");

    const QVector<Line> lines = groupLines(page.glyphs);
    if (plan.firstLine < 0 || plan.firstLine + plan.lineCount > lines.size())
        return fail("Zeilen passen nicht zum Plan");

    //  Je Zeile: die beteiligten Operatoren in Reihenfolge, EINE Schrift.
    //  (Eine durch Wachstum HINZUGEKOMMENE Zeile hat noch keine - sie wird
    //  weiter unten erzeugt.)
    struct LineSpans { QVector<int> spans; QByteArray fontRes; };
    QVector<LineSpans> perLine;
    for (int li = 0; li < plan.lineCount; ++li) {
        const Line& l = lines.at(plan.firstLine + li);
        LineSpans ls;
        for (int i = l.first; i <= l.last; ++i) {
            const int sp = page.glyphs.at(i).showIndex;
            if (sp < 0 || sp >= page.spans.size()) return fail("Zeigeoperator fehlt");
            if (ls.spans.isEmpty() || ls.spans.last() != sp) {
                if (!ls.spans.contains(sp))
                    ls.spans.push_back(sp);
            }
            const QByteArray& fr = page.spans.at(sp).fontRes;
            if (ls.fontRes.isEmpty()) ls.fontRes = fr;
            else if (ls.fontRes != fr)
                return fail("mehrere Schriften im Absatz -> Auszeichnung bliebe nicht erhalten");
        }
        if (ls.spans.isEmpty()) return fail("Zeile ohne Zeigeoperator");
        perLine.push_back(ls);
    }
    //  Auch über die Zeilen hinweg muss es EINE Schrift sein: Wörter wandern
    //  zwischen den Zeilen, eine abweichende Schrift ginge dabei verloren.
    for (int li = 1; li < perLine.size(); ++li)
        if (perLine.at(li).fontRes != perLine.at(0).fontRes)
            return fail("mehrere Schriften im Absatz -> Auszeichnung bliebe nicht erhalten");
    //  Ein Operator darf nicht zu ZWEI Zeilen gehören (sonst würde die zweite
    //  Zuweisung die erste überschreiben).
    {
        QSet<int> seen;
        for (const LineSpans& ls : std::as_const(perLine))
            for (int sp : ls.spans) {
                if (seen.contains(sp))
                    return fail("Zeigeoperator über zwei Zeilen -> nicht umbrechbar");
                seen.insert(sp);
            }
    }

    const QHash<int, ObjLoc> objs = scanObjects(buf);
    pdfenc::Encoding enc;
    if (!PdfTextEditor::encodingForPageFont(buf, pageIndex, perLine.at(0).fontRes, &enc))
        return fail("Schriftkodierung nicht bestimmbar");

    QVector<Edit> repls;
    for (int li = 0; li < plan.lineCount; ++li) {
        const LineSpans& ls = perLine.at(li);
        QByteArray encoded;
        if (!enc.encode(plan.newLines.at(li), &encoded))
            return fail("Zeile in der Schriftkodierung nicht darstellbar");
        for (int k = 0; k < ls.spans.size(); ++k) {
            const PdfShowSpan& sp = page.spans.at(ls.spans.at(k));
            QByteArray repl = parenBytes(k == 0 ? encoded : QByteArray());
            if (sp.isArray) repl = "[" + repl + "]";
            if (sp.operandStart < 0 || sp.operandEnd <= sp.operandStart
                || sp.operandEnd > page.content.size())
                return fail("Operanden-Bereich ungültig");
            repls.push_back({ sp.operandStart, sp.operandEnd, repl });
        }
    }

    if (plan.grew) {
        if (plan.newLines.size() != plan.lineCount + 1)
            return fail("Plan und Zeilenzahl passen nicht zusammen");
        const int lastLine = plan.firstLine + plan.lineCount - 1;
        const PdfShowSpan& lastSp = page.spans.at(perLine.last().spans.first());

        //  Einfügepunkt: HINTER dem Zeigeoperator der letzten Absatzzeile
        //  (zwischen Operand und Operator darf nichts stehen).
        qint64 ip = lastSp.operandEnd;
        while (ip < page.content.size() && isWs(page.content[ip])) ++ip;
        const qint64 opStart = ip;
        while (ip < page.content.size() && !isWs(page.content[ip])
               && !isDelim(page.content[ip])) ++ip;
        const QByteArray showOp = page.content.mid(opStart, ip - opStart);
        if (showOp != "Tj" && showOp != "TJ")
            return fail("Zeigeoperator der letzten Zeile nicht gefunden");

        //  Positionierung der neuen Zeile: absolut wie die letzte, nur eine
        //  Zeilenhöhe tiefer - bzw. relativ derselbe Sprung noch einmal.
        QByteArray posStmt;
        if (lastSp.posOp == "Tm" && lastSp.posArgs.size() >= 6) {
            for (int k = 0; k < 5; ++k) { posStmt += pnum(lastSp.posArgs.at(k)); posStmt += ' '; }
            posStmt += pnum(lastSp.posArgs.at(5) - plan.growDyText);
            posStmt += " Tm";
        } else if ((lastSp.posOp == "Td" || lastSp.posOp == "TD")
                   && lastSp.posArgs.size() >= 2) {
            posStmt = "0 " + pnum(-plan.growDyText) + " Td";
        } else {
            return fail("neue Zeile nicht positionierbar");
        }

        QByteArray encoded;
        if (!enc.encode(plan.newLines.last(), &encoded))
            return fail("neue Zeile in der Schriftkodierung nicht darstellbar");
        const QByteArray run = "\n" + posStmt + " " + parenBytes(encoded) + " Tj";
        repls.push_back({ ip, ip, run });          // Einfügung (start == end)

        const QVector<Line> allLines = lines;
        const qreal boundary = allLines.at(lastLine).top
                             + 0.5 * qMax(1.0, allLines.at(lastLine).fontSize);
        bool shifted = true;                       // s. canGrowParagraph
        int  curObj  = lastSp.objIndex;
        for (int li = lastLine + 1; li < allLines.size(); ++li) {
            if (allLines.at(li).top <= boundary) continue;
            const int gi = allLines.at(li).first;
            const int si = page.glyphs.at(gi).showIndex;
            if (si < 0 || si >= page.spans.size()) return fail("Zeigeoperator fehlt");
            const PdfShowSpan& sp = page.spans.at(si);
            if (sp.objIndex != curObj) { curObj = sp.objIndex; shifted = false; }
            Edit e; bool now = false;
            if (!shiftStatement(sp, plan.growDyText, shifted, &e, &now))
                return fail("Zeile unterhalb nicht verschiebbar");
            if (e.end > e.start)
                repls.push_back(e);
            shifted = now;
        }
    }

    std::sort(repls.begin(), repls.end(),
              [](const Edit& a, const Edit& b) { return a.start > b.start; });

    QByteArray newContent = page.content;
    for (const Edit& r : std::as_const(repls))
        newContent.replace(r.start, r.end - r.start, r.bytes);

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

} // namespace mg
