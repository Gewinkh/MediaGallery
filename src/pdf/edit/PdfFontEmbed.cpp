#include "pdf/edit/PdfFontEmbed.h"
#include "pdf/edit/PdfEncodings.h"

#include <QFont>
#include <QRawFont>
#include <QtEndian>

#include <cmath>

namespace {

//  Die Tabellen, die ein PDF-Betrachter für eine eingebettete TrueType-Schrift
//  braucht. `post`/`name` sind für die Darstellung entbehrlich, werden aber
//  mitgenommen, wenn vorhanden - manche Werkzeuge erwarten sie.
const char* const kTables[] = {
    "head", "hhea", "maxp", "cmap", "glyf", "loca", "hmtx", "name", "OS/2", "post",
    "cvt ", "fpgm", "prep"
};

void put16(QByteArray& b, quint16 v) { char t[2]; qToBigEndian(v, t); b.append(t, 2); }
void put32(QByteArray& b, quint32 v) { char t[4]; qToBigEndian(v, t); b.append(t, 4); }

quint32 tableChecksum(const QByteArray& t) {
    //  Summe aller 32-Bit-Wörter, fehlende Bytes am Ende als 0 (sfnt-Regel).
    quint32 sum = 0;
    for (int i = 0; i < t.size(); i += 4) {
        quint32 w = 0;
        for (int k = 0; k < 4; ++k)
            w = (w << 8) | quint8((i + k < t.size()) ? t[i + k] : 0);
        sum += w;
    }
    return sum;
}

//  sfnt aus einzelnen Tabellen wieder zusammensetzen. Reihenfolge und
//  4-Byte-Ausrichtung folgen der Spezifikation; `checkSumAdjustment` in `head`
//  wird korrekt nachgetragen.
QByteArray buildSfnt(const QVector<QPair<QByteArray, QByteArray>>& tables) {
    const int n = tables.size();
    if (n == 0) return {};

    //  searchRange & Co. laut Spezifikation.
    int pow2 = 1, logv = 0;
    while (pow2 * 2 <= n) { pow2 *= 2; ++logv; }

    QByteArray out;
    put32(out, 0x00010000u);                  // sfnt-Version (TrueType)
    put16(out, quint16(n));
    put16(out, quint16(pow2 * 16));
    put16(out, quint16(logv));
    put16(out, quint16(n * 16 - pow2 * 16));

    //  Verzeichnis mit vorläufigen Offsets, danach die Daten.
    const int dirStart = out.size();
    for (int i = 0; i < n; ++i) { out.append(16, '\0'); }

    int headDirIdx = -1, headDataOff = -1;
    QByteArray data;
    const int dataStart = out.size();
    for (int i = 0; i < n; ++i) {
        const QByteArray& tag = tables[i].first;
        const QByteArray& t   = tables[i].second;
        const int off = dataStart + data.size();
        if (tag == "head") { headDirIdx = i; headDataOff = off; }

        QByteArray entry;
        entry.append(tag.leftJustified(4, ' '));
        put32(entry, tableChecksum(t));
        put32(entry, quint32(off));
        put32(entry, quint32(t.size()));
        out.replace(dirStart + i * 16, 16, entry);

        data.append(t);
        while (data.size() % 4) data.append('\0');            // 4-Byte-Ausrichtung
    }
    out.append(data);

    //  checkSumAdjustment: 0xB1B0AFBA − Prüfsumme der GESAMTEN Datei (mit dem
    //  Feld auf 0). Ohne diesen Nachtrag halten strenge Prüfer die Datei für
    //  beschädigt.
    if (headDirIdx >= 0 && headDataOff >= 0 && headDataOff + 12 <= out.size()) {
        qToBigEndian<quint32>(0, out.data() + headDataOff + 8);
        const quint32 total = tableChecksum(out);
        qToBigEndian<quint32>(quint32(0xB1B0AFBAu - total), out.data() + headDataOff + 8);
    }
    return out;
}

} // namespace

namespace mg {

bool PdfFontEmbed::needsEmbedding(const QString& family) {
    const QString f = family.trimmed().toLower();
    if (f.isEmpty()) return false;
    //  Genau die Familien, für die eine Standard-14-Schrift eine ECHTE
    //  Entsprechung ist - alles andere würde sichtbar anders aussehen.
    static const char* const known[] = {
        "helvetica", "arial", "liberation sans", "nimbus sans", "sans", "sans-serif",
        "times", "times new roman", "liberation serif", "nimbus roman", "serif",
        "courier", "courier new", "liberation mono", "nimbus mono", "monospace", "mono"
    };
    for (const char* k : known)
        if (f == QString::fromLatin1(k)) return false;
    return true;
}

bool PdfFontEmbed::build(const QString& family, bool bold, bool italic,
                         EmbeddedFont* out, QString* err) {
    auto fail = [&](const char* m) { if (err) *err = QString::fromLatin1(m); return false; };
    if (!out) return fail("kein Ausgabeziel");
    *out = EmbeddedFont();

    QFont qf(family);
    qf.setBold(bold);
    qf.setItalic(italic);
    qf.setPointSizeF(1000.0 / 10.0);          // groß wählen -> genaue Vorschübe
    const QRawFont rf = QRawFont::fromFont(qf);
    if (!rf.isValid()) return fail("Schrift nicht verfügbar");

    //  Die geladene Familie kann von der angeforderten abweichen (Ersetzung
    //  durch das System). Dann bringt das Einbetten nichts Gutes - lieber
    //  ablehnen als eine FALSCHE Schrift einbetten und Treue vortäuschen.
    if (rf.familyName().compare(family, Qt::CaseInsensitive) != 0)
        return fail("Systemersetzung - angeforderte Schrift nicht vorhanden");

    QVector<QPair<QByteArray, QByteArray>> tables;
    for (const char* tag : kTables) {
        const QByteArray t = rf.fontTable(tag);
        if (!t.isEmpty()) tables.push_back({ QByteArray(tag), t });
    }
    //  Ohne diese vier ist es kein brauchbares TrueType-Programm.
    auto has = [&](const char* tag) {
        for (const auto& p : tables) if (p.first == tag) return true;
        return false;
    };
    if (!has("head") || !has("glyf") || !has("loca") || !has("cmap"))
        return fail("Schrifttabellen unvollständig (kein TrueType?)");

    out->sfnt = buildSfnt(tables);
    if (out->sfnt.isEmpty()) return fail("Schriftprogramm nicht zusammensetzbar");

    //  PostScript-Name: Leerzeichen und Sonderzeichen sind dort nicht erlaubt.
    QByteArray ps;
    for (QChar c : family)
        if (c.isLetterOrNumber()) ps += char(c.toLatin1() ? c.toLatin1() : '_');
    if (ps.isEmpty()) ps = "EmbeddedFont";
    if (bold)   ps += "-Bold";
    if (italic) ps += bold ? "Italic" : "-Italic";
    out->psName = ps;

    bool eok = false;
    const auto enc = mg::pdfenc::Encoding::fromEncodingValue("/WinAnsiEncoding", &eok);
    if (!eok) return fail("WinAnsi-Tabelle fehlt");

    const qreal upem = qMax<qreal>(1.0, rf.unitsPerEm());
    //  QRawFont liefert Vorschübe in Pixeln der eingestellten Größe; auf
    //  1/1000 em umrechnen.
    const qreal pixPerEm = qMax<qreal>(0.0001, rf.pixelSize());
    out->firstChar = 32;
    out->widths.reserve(224);
    for (int code = 32; code <= 255; ++code) {
        const QChar c = enc.toUnicode(quint8(code));
        int w = 0;
        if (!c.isNull()) {
            const QVector<quint32> gi = rf.glyphIndexesForString(QString(c));
            if (!gi.isEmpty()) {
                const QVector<QPointF> adv = rf.advancesForGlyphIndexes(gi);
                if (!adv.isEmpty())
                    w = int(std::lround(adv.first().x() / pixPerEm * 1000.0));
            }
        }
        out->widths.push_back(w);
    }

    //  Deskriptor-Werte. Ascent/Descent/CapHeight in 1/1000 em.
    out->ascent      = rf.ascent()  / pixPerEm * 1000.0;
    out->descent     = -rf.descent() / pixPerEm * 1000.0;
    out->capHeight   = out->ascent;                 // brauchbare Näherung
    out->italicAngle = italic ? -12.0 : 0.0;
    //  FontBBox aus der head-Tabelle (xMin,yMin,xMax,yMax ab Offset 36),
    //  in Einheiten von unitsPerEm -> auf 1/1000 em umrechnen.
    for (const auto& p : tables) {
        if (p.first != "head" || p.second.size() < 44) continue;
        const auto rd = [&](int off) {
            return qint16(qFromBigEndian<quint16>(p.second.constData() + off));
        };
        out->bbox[0] = rd(36) / upem * 1000.0;
        out->bbox[1] = rd(38) / upem * 1000.0;
        out->bbox[2] = rd(40) / upem * 1000.0;
        out->bbox[3] = rd(42) / upem * 1000.0;
        break;
    }
    out->flags = italic ? (32 | 64) : 32;           // nichtsymbolisch (+ kursiv)
    out->valid = true;
    return true;
}

} // namespace mg
