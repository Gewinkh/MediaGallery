#include "pdf/edit/PdfEncodings.h"

#include <cstring>
#include <iterator>
#include <QVector>

// ══════════════════════════════════════════════════════════════════════════════
//  PdfEncodings.cpp - Tabellen der einfachen PDF-Textkodierungen.
//
//  DATENTABELLEN: WinAnsiEncoding und MacRomanEncoding legt die
//  PDF-Spezifikation fest (ISO 32000-1, Anhang D); die Glyphennamen folgen der
//  Adobe Glyph List. Ausgeschrieben ist nur die hier gebrauchte Teilmenge.
//  Sie stehen bewusst hier statt im Content-Editor: die
//  Zuordnung Byte ↔ Unicode wird auch beim direkten Bearbeiten der Textebene
//  gebraucht (README ▸ Planned) und hat mit dem Splicing nichts zu tun.
//
//  0x20–0x7E ist in allen unterstuetzten Kodierungen ASCII; abgebildet werden
//  daher nur die Bereiche, die abweichen.
// ══════════════════════════════════════════════════════════════════════════════

namespace mg::pdfenc {
namespace {
// WinAnsi weicht von Latin-1 NUR im Bereich 0x80–0x9F ab (CP1252-Block).
struct CodeUni { quint8 code; char16_t uni; };
const CodeUni kWinAnsiHigh[] = {
    { 0x80, 0x20AC },
    { 0x82, 0x201A },
    { 0x83, 0x0192 },
    { 0x84, 0x201E },
    { 0x85, 0x2026 },
    { 0x86, 0x2020 },
    { 0x87, 0x2021 },
    { 0x88, 0x02C6 },
    { 0x89, 0x2030 },
    { 0x8A, 0x0160 },
    { 0x8B, 0x2039 },
    { 0x8C, 0x0152 },
    { 0x8E, 0x017D },
    { 0x91, 0x2018 },
    { 0x92, 0x2019 },
    { 0x93, 0x201C },
    { 0x94, 0x201D },
    { 0x95, 0x2022 },
    { 0x96, 0x2013 },
    { 0x97, 0x2014 },
    { 0x98, 0x02DC },
    { 0x99, 0x2122 },
    { 0x9A, 0x0161 },
    { 0x9B, 0x203A },
    { 0x9C, 0x0153 },
    { 0x9E, 0x017E },
    { 0x9F, 0x0178 },
};

// MacRoman: der gesamte Bereich 0x80–0xFF weicht ab.
const char16_t kMacRomanHigh[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

// Adobe-Glyphennamen -> Unicode (Teilmenge: alles, was WinAnsi/MacRoman/
// Standard und Latin-1 brauchen). Unbekannte Namen -> Ersetzung wird
// abgelehnt (Aufrufer faellt auf Raster zurueck) statt zu raten.
struct GlyphUni { const char* name; char16_t uni; };
const GlyphUni kGlyphs[] = {
    { "A", 0x0041 },
    { "AE", 0x00C6 },
    { "Aacute", 0x00C1 },
    { "Acircumflex", 0x00C2 },
    { "Adieresis", 0x00C4 },
    { "Agrave", 0x00C0 },
    { "Aring", 0x00C5 },
    { "Atilde", 0x00C3 },
    { "B", 0x0042 },
    { "C", 0x0043 },
    { "Ccedilla", 0x00C7 },
    { "D", 0x0044 },
    { "E", 0x0045 },
    { "Eacute", 0x00C9 },
    { "Ecircumflex", 0x00CA },
    { "Edieresis", 0x00CB },
    { "Egrave", 0x00C8 },
    { "Eth", 0x00D0 },
    { "Euro", 0x20AC },
    { "F", 0x0046 },
    { "G", 0x0047 },
    { "H", 0x0048 },
    { "I", 0x0049 },
    { "Iacute", 0x00CD },
    { "Icircumflex", 0x00CE },
    { "Idieresis", 0x00CF },
    { "Igrave", 0x00CC },
    { "J", 0x004A },
    { "K", 0x004B },
    { "L", 0x004C },
    { "M", 0x004D },
    { "N", 0x004E },
    { "Ntilde", 0x00D1 },
    { "O", 0x004F },
    { "OE", 0x0152 },
    { "Oacute", 0x00D3 },
    { "Ocircumflex", 0x00D4 },
    { "Odieresis", 0x00D6 },
    { "Ograve", 0x00D2 },
    { "Oslash", 0x00D8 },
    { "Otilde", 0x00D5 },
    { "P", 0x0050 },
    { "Q", 0x0051 },
    { "R", 0x0052 },
    { "S", 0x0053 },
    { "Scaron", 0x0160 },
    { "T", 0x0054 },
    { "Thorn", 0x00DE },
    { "U", 0x0055 },
    { "Uacute", 0x00DA },
    { "Ucircumflex", 0x00DB },
    { "Udieresis", 0x00DC },
    { "Ugrave", 0x00D9 },
    { "V", 0x0056 },
    { "W", 0x0057 },
    { "X", 0x0058 },
    { "Y", 0x0059 },
    { "Yacute", 0x00DD },
    { "Ydieresis", 0x0178 },
    { "Z", 0x005A },
    { "Zcaron", 0x017D },
    { "a", 0x0061 },
    { "aacute", 0x00E1 },
    { "acircumflex", 0x00E2 },
    { "acute", 0x00B4 },
    { "adieresis", 0x00E4 },
    { "ae", 0x00E6 },
    { "agrave", 0x00E0 },
    { "ampersand", 0x0026 },
    { "aring", 0x00E5 },
    { "asciicircum", 0x005E },
    { "asciitilde", 0x007E },
    { "asterisk", 0x002A },
    { "at", 0x0040 },
    { "atilde", 0x00E3 },
    { "b", 0x0062 },
    { "backslash", 0x005C },
    { "bar", 0x007C },
    { "braceleft", 0x007B },
    { "braceright", 0x007D },
    { "bracketleft", 0x005B },
    { "bracketright", 0x005D },
    { "breve", 0x02D8 },
    { "brokenbar", 0x00A6 },
    { "bullet", 0x2022 },
    { "c", 0x0063 },
    { "caron", 0x02C7 },
    { "ccedilla", 0x00E7 },
    { "cedilla", 0x00B8 },
    { "cent", 0x00A2 },
    { "circumflex", 0x02C6 },
    { "colon", 0x003A },
    { "comma", 0x002C },
    { "copyright", 0x00A9 },
    { "currency", 0x00A4 },
    { "d", 0x0064 },
    { "dagger", 0x2020 },
    { "daggerdbl", 0x2021 },
    { "degree", 0x00B0 },
    { "dieresis", 0x00A8 },
    { "divide", 0x00F7 },
    { "dollar", 0x0024 },
    { "dotaccent", 0x02D9 },
    { "dotlessi", 0x0131 },
    { "e", 0x0065 },
    { "eacute", 0x00E9 },
    { "ecircumflex", 0x00EA },
    { "edieresis", 0x00EB },
    { "egrave", 0x00E8 },
    { "eight", 0x0038 },
    { "ellipsis", 0x2026 },
    { "emdash", 0x2014 },
    { "endash", 0x2013 },
    { "equal", 0x003D },
    { "eth", 0x00F0 },
    { "exclam", 0x0021 },
    { "exclamdown", 0x00A1 },
    { "f", 0x0066 },
    { "fi", 0xFB01 },
    { "five", 0x0035 },
    { "fl", 0xFB02 },
    { "florin", 0x0192 },
    { "four", 0x0034 },
    { "g", 0x0067 },
    { "germandbls", 0x00DF },
    { "grave", 0x0060 },
    { "greater", 0x003E },
    { "guillemotleft", 0x00AB },
    { "guillemotright", 0x00BB },
    { "guilsinglleft", 0x2039 },
    { "guilsinglright", 0x203A },
    { "h", 0x0068 },
    { "hungarumlaut", 0x02DD },
    { "hyphen", 0x002D },
    { "i", 0x0069 },
    { "iacute", 0x00ED },
    { "icircumflex", 0x00EE },
    { "idieresis", 0x00EF },
    { "igrave", 0x00EC },
    { "j", 0x006A },
    { "k", 0x006B },
    { "l", 0x006C },
    { "less", 0x003C },
    { "logicalnot", 0x00AC },
    { "m", 0x006D },
    { "macron", 0x00AF },
    { "mu", 0x00B5 },
    { "multiply", 0x00D7 },
    { "n", 0x006E },
    { "nbspace", 0x00A0 },
    { "nine", 0x0039 },
    { "ntilde", 0x00F1 },
    { "numbersign", 0x0023 },
    { "o", 0x006F },
    { "oacute", 0x00F3 },
    { "ocircumflex", 0x00F4 },
    { "odieresis", 0x00F6 },
    { "oe", 0x0153 },
    { "ogonek", 0x02DB },
    { "ograve", 0x00F2 },
    { "one", 0x0031 },
    { "onehalf", 0x00BD },
    { "onequarter", 0x00BC },
    { "onesuperior", 0x00B9 },
    { "ordfeminine", 0x00AA },
    { "ordmasculine", 0x00BA },
    { "oslash", 0x00F8 },
    { "otilde", 0x00F5 },
    { "p", 0x0070 },
    { "paragraph", 0x00B6 },
    { "parenleft", 0x0028 },
    { "parenright", 0x0029 },
    { "percent", 0x0025 },
    { "period", 0x002E },
    { "periodcentered", 0x00B7 },
    { "perthousand", 0x2030 },
    { "plus", 0x002B },
    { "plusminus", 0x00B1 },
    { "q", 0x0071 },
    { "question", 0x003F },
    { "questiondown", 0x00BF },
    { "quotedbl", 0x0022 },
    { "quotedblbase", 0x201E },
    { "quotedblleft", 0x201C },
    { "quotedblright", 0x201D },
    { "quoteleft", 0x2018 },
    { "quoteright", 0x2019 },
    { "quotesinglbase", 0x201A },
    { "quotesingle", 0x0027 },
    { "r", 0x0072 },
    { "registered", 0x00AE },
    { "ring", 0x02DA },
    { "s", 0x0073 },
    { "scaron", 0x0161 },
    { "section", 0x00A7 },
    { "semicolon", 0x003B },
    { "seven", 0x0037 },
    { "six", 0x0036 },
    { "slash", 0x002F },
    { "space", 0x0020 },
    { "sterling", 0x00A3 },
    { "t", 0x0074 },
    { "thorn", 0x00FE },
    { "three", 0x0033 },
    { "threequarters", 0x00BE },
    { "threesuperior", 0x00B3 },
    { "tilde", 0x02DC },
    { "trademark", 0x2122 },
    { "two", 0x0032 },
    { "twosuperior", 0x00B2 },
    { "u", 0x0075 },
    { "uacute", 0x00FA },
    { "ucircumflex", 0x00FB },
    { "udieresis", 0x00FC },
    { "ugrave", 0x00F9 },
    { "underscore", 0x005F },
    { "v", 0x0076 },
    { "w", 0x0077 },
    { "x", 0x0078 },
    { "y", 0x0079 },
    { "yacute", 0x00FD },
    { "ydieresis", 0x00FF },
    { "yen", 0x00A5 },
    { "z", 0x007A },
    { "zcaron", 0x017E },
    { "zero", 0x0030 },
};
} // namespace

// ── Glyphenname -> Unicode ────────────────────────────────────────────────────
QChar glyphToUnicode(const QByteArray& glyphName) {
    if (glyphName.isEmpty())
        return {};
    //  AGL-Konventionen `uniXXXX` und `uXXXX` (4–6 Hex-Ziffern) zuerst - sie
    //  sind eindeutig und decken alles ab, was in der Namensliste fehlt.
    auto hexToChar = [](const QByteArray& hex) -> QChar {
        bool ok = false;
        const uint v = hex.toUInt(&ok, 16);
        //  Nur die Basic Multilingual Plane: QChar fasst nicht mehr, und
        //  Zeichen darüber kämen in einfachen Fonts ohnehin nicht vor.
        return (ok && v > 0 && v <= 0xFFFF) ? QChar(char16_t(v)) : QChar();
    };
    if (glyphName.startsWith("uni") && glyphName.size() == 7)
        return hexToChar(glyphName.mid(3));
    if (glyphName.startsWith("u") && glyphName.size() >= 5 && glyphName.size() <= 7
        && !glyphName.startsWith("uni"))
        return hexToChar(glyphName.mid(1));

    //  Namensliste (sortiert erzeugt -> binäre Suche).
    int lo = 0, hi = int(std::size(kGlyphs)) - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int cmp = std::strcmp(kGlyphs[mid].name, glyphName.constData());
        if (cmp == 0) return QChar(kGlyphs[mid].uni);
        if (cmp < 0)  lo = mid + 1;
        else          hi = mid - 1;
    }
    return {};
}

// ── Encoding ────────────────────────────────────────────────────────────────
QChar Encoding::toUnicode(quint8 code) const {
    const auto d = m_diffToUni.constFind(code);
    if (d != m_diffToUni.constEnd())
        return d.value();
    if (code >= 0x20 && code <= 0x7E)
        return QChar(char16_t(code));           // in allen Tabellen ASCII
    switch (m_base) {
    case Base::WinAnsi:
        if (code >= 0x80 && code <= 0x9F) {
            for (const CodeUni& e : kWinAnsiHigh)
                if (e.code == code) return QChar(e.uni);
            return {};                          // im CP1252-Block unbelegt
        }
        if (code >= 0xA0) return QChar(char16_t(code));   // sonst Latin-1
        return {};
    case Base::MacRoman:
        if (code >= 0x80) return QChar(kMacRomanHigh[code - 0x80]);
        return {};
    case Base::AsciiOnly:
    default:
        return {};                              // alles außerhalb ASCII unsicher
    }
}

bool Encoding::fromUnicode(QChar c, quint8* code) const {
    const auto u = m_uniToCode.constFind(c);    // /Differences hat Vorrang
    if (u != m_uniToCode.constEnd()) { *code = u.value(); return true; }
    const char16_t v = c.unicode();
    if (v >= 0x20 && v <= 0x7E) { *code = quint8(v); return true; }
    switch (m_base) {
    case Base::WinAnsi:
        for (const CodeUni& e : kWinAnsiHigh)
            if (e.uni == v) { *code = e.code; return true; }
        if (v >= 0xA0 && v <= 0xFF) { *code = quint8(v); return true; }
        return false;
    case Base::MacRoman:
        for (int i = 0; i < 128; ++i)
            if (kMacRomanHigh[i] == v) { *code = quint8(0x80 + i); return true; }
        return false;
    case Base::AsciiOnly:
    default:
        return false;
    }
}

QString Encoding::decode(const QByteArray& bytes) const {
    if (m_base == Base::IdentityCid) {
        //  2-Byte-Codes, big-endian. Ein ungerades Restbyte bzw. ein unbekannter
        //  Code ergibt U+FFFD - der Vergleich mit dem Originaltext scheitert
        //  dann sicher, statt zufaellig zu passen.
        QString out;
        out.reserve(bytes.size() / 2);
        for (int i = 0; i + 1 < bytes.size(); i += 2) {
            const quint16 code = quint16((quint8(bytes[i]) << 8) | quint8(bytes[i+1]));
            const auto it = m_cidToUni.constFind(code);
            out += (it == m_cidToUni.constEnd()) ? QString(QChar(0xFFFD)) : it.value();
        }
        if (bytes.size() % 2) out += QChar(0xFFFD);
        return out;
    }
    QString out;
    out.reserve(bytes.size());
    for (char ch : bytes) {
        const QChar c = toUnicode(quint8(ch));
        //  Unbelegter Code -> U+FFFD. Der Vergleich mit dem gesuchten
        //  Originaltext scheitert dann sicher, statt zufällig zu passen.
        out += c.isNull() ? QChar(0xFFFD) : c;
    }
    return out;
}

bool Encoding::encode(const QString& text, QByteArray* out) const {
    out->clear();
    if (m_base == Base::IdentityCid) {
        out->reserve(text.size() * 2);
        for (QChar c : text) {
            const auto it = m_uniToCid.constFind(c);
            //  Nicht in der CMap = die (Teilmengen-)Schrift hat keine Glyphe
            //  dafuer -> ablehnen statt einen Leerkasten zu erzeugen.
            if (it == m_uniToCid.constEnd())
                return false;
            const quint16 code = it.value();
            out->append(char((code >> 8) & 0xFF));
            out->append(char(code & 0xFF));
        }
        return true;
    }
    out->reserve(text.size());
    for (QChar c : text) {
        quint8 code = 0;
        if (!fromUnicode(c, &code))
            return false;                       // nicht darstellbar -> ablehnen
        out->append(char(code));
    }
    return true;
}

Encoding Encoding::fromEncodingValue(const QByteArray& encValue, bool* ok) {
    if (ok) *ok = true;
    Encoding e;
    const QByteArray v = encValue.trimmed();
    if (v.isEmpty())
        return e;                               // AsciiOnly

    auto baseFromName = [](const QByteArray& n) {
        if (n == "/WinAnsiEncoding")  return Base::WinAnsi;
        if (n == "/MacRomanEncoding") return Base::MacRoman;
        return Base::AsciiOnly;                 // Standard/PDFDoc/unbekannt
    };

    if (v.startsWith('/')) {                    // schlichter Name
        e.m_base = baseFromName(v);
        return e;
    }
    if (!v.startsWith("<<"))
        return e;

    //  Dict: /BaseEncoding auslesen …
    const int bp = v.indexOf("/BaseEncoding");
    if (bp >= 0) {
        int i = bp + 13;
        while (i < v.size() && (v[i] == ' ' || v[i] == '\n' || v[i] == '\r' || v[i] == '\t')) ++i;
        int s = i;
        if (i < v.size() && v[i] == '/') {
            ++i;
            while (i < v.size() && v[i] != ' ' && v[i] != '/' && v[i] != '>'
                   && v[i] != '\n' && v[i] != '\r' && v[i] != '\t' && v[i] != '[') ++i;
            e.m_base = baseFromName(v.mid(s, i - s));
        }
    }

    //  … und /Differences [ code /name /name … code /name … ] anwenden.
    const int dp = v.indexOf("/Differences");
    if (dp >= 0) {
        const int as = v.indexOf('[', dp);
        const int ae = (as >= 0) ? v.indexOf(']', as) : -1;
        if (as >= 0 && ae > as) {
            const QByteArray arr = v.mid(as + 1, ae - as - 1);
            int cur = -1;
            int i = 0;
            while (i < arr.size()) {
                const char ch = arr[i];
                if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') { ++i; continue; }
                if (ch >= '0' && ch <= '9') {           // neuer Startcode
                    int s = i;
                    while (i < arr.size() && arr[i] >= '0' && arr[i] <= '9') ++i;
                    cur = arr.mid(s, i - s).toInt();
                    continue;
                }
                if (ch == '/') {                        // Glyphenname
                    int s = ++i;
                    while (i < arr.size() && arr[i] != ' ' && arr[i] != '/' && arr[i] != '\n'
                           && arr[i] != '\r' && arr[i] != '\t') ++i;
                    const QByteArray name = arr.mid(s, i - s);
                    const QChar c = glyphToUnicode(name);
                    if (c.isNull()) {
                        //  NICHT raten: ein unbekannter Glyphenname macht die
                        //  ganze Kodierung unsicher.
                        if (ok) *ok = false;
                        return e;
                    }
                    if (cur >= 0 && cur <= 255) {
                        e.m_diffToUni.insert(quint8(cur), c);
                        //  Erste Zuordnung gewinnt in der Rückrichtung.
                        if (!e.m_uniToCode.contains(c))
                            e.m_uniToCode.insert(c, quint8(cur));
                        ++cur;
                    }
                    continue;
                }
                ++i;                                    // Fremdzeichen überspringen
            }
        }
    }
    return e;
}


// ── Type0 / Identity-H: /ToUnicode-CMap ─────────────────────────────────────
namespace {

//  Ein Hex-String "<0041>" -> Bytes. Liefert false bei ungerader Ziffernzahl
//  oder Fremdzeichen (dann ist die CMap nicht vertrauenswürdig).
bool hexStringBytes(const QByteArray& tok, QByteArray* out) {
    if (tok.size() < 2 || tok.front() != '<' || tok.back() != '>')
        return false;
    QByteArray hex = tok.mid(1, tok.size() - 2);
    hex.replace(" ", "").replace("\n", "").replace("\r", "").replace("\t", "");
    if (hex.isEmpty() || (hex.size() % 2) != 0)
        return false;
    for (char c : hex)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    *out = QByteArray::fromHex(hex);
    return true;
}

//  UTF-16BE-Bytes -> QString (die CMap-Zielwerte sind immer UTF-16BE).
QString utf16beToString(const QByteArray& b) {
    if (b.isEmpty() || (b.size() % 2) != 0)
        return {};
    QString out;
    out.reserve(b.size() / 2);
    for (int i = 0; i + 1 < b.size(); i += 2)
        out += QChar(char16_t((quint8(b[i]) << 8) | quint8(b[i + 1])));
    return out;
}

//  Zerlegt eine CMap in Token: Hex-Strings <…>, Namen /…, Arrays [ ], Zahlen
//  und Schlüsselwörter. Kommentare (%) fallen weg.
QVector<QByteArray> cmapTokens(const QByteArray& c) {
    QVector<QByteArray> t;
    int i = 0;
    const int n = c.size();
    auto isSpace = [](char x) { return x==' '||x=='\t'||x=='\r'||x=='\n'||x=='\f'||x=='\0'; };
    while (i < n) {
        const char ch = c[i];
        if (isSpace(ch)) { ++i; continue; }
        if (ch == '%') { while (i < n && c[i] != '\n' && c[i] != '\r') ++i; continue; }
        if (ch == '<') {
            const int e = c.indexOf('>', i);
            if (e < 0) break;
            t.push_back(c.mid(i, e - i + 1));
            i = e + 1; continue;
        }
        if (ch == '[' || ch == ']') { t.push_back(QByteArray(1, ch)); ++i; continue; }
        const int s = i;
        while (i < n && !isSpace(c[i]) && c[i] != '<' && c[i] != '[' && c[i] != ']'
               && c[i] != '%') ++i;
        if (i == s) ++i;                       // Sicherheitsnetz gegen Endlosschleife
        else t.push_back(c.mid(s, i - s));
    }
    return t;
}

} // namespace

Encoding Encoding::fromCidToUnicode(const QByteArray& cmap, bool* ok) {
    if (ok) *ok = false;
    Encoding e;
    e.m_base = Base::IdentityCid;

    const QVector<QByteArray> t = cmapTokens(cmap);
    //  Eine Zuordnung eintragen. Mehrzeichen-Ziele (Ligaturen) gelten NUR in
    //  Leserichtung - rückwärts wären sie mehrdeutig.
    auto put = [&e](quint16 code, const QString& text) {
        if (text.isEmpty()) return;
        e.m_cidToUni.insert(code, text);
        if (text.size() == 1 && !e.m_uniToCid.contains(text.at(0)))
            e.m_uniToCid.insert(text.at(0), code);
    };
    auto codeOf = [](const QByteArray& bytes, quint16* out) {
        //  Identity-H: Codes sind 2 Byte. 1 Byte akzeptieren wir ebenfalls
        //  (manche Erzeuger kürzen führende Nullen), mehr nicht.
        if (bytes.size() == 2) { *out = quint16((quint8(bytes[0]) << 8) | quint8(bytes[1])); return true; }
        if (bytes.size() == 1) { *out = quint8(bytes[0]); return true; }
        return false;
    };

    for (int i = 0; i < t.size(); ++i) {
        if (t[i] == "beginbfchar") {
            //  Paare: <src> <dst>
            int j = i + 1;
            while (j + 1 < t.size() && t[j] != "endbfchar") {
                QByteArray sb, db;
                if (!hexStringBytes(t[j], &sb) || !hexStringBytes(t[j+1], &db))
                    return e;                  // ok bleibt false
                quint16 code = 0;
                if (!codeOf(sb, &code)) return e;
                put(code, utf16beToString(db));
                j += 2;
            }
            if (j >= t.size() || t[j] != "endbfchar") return e;
            i = j; continue;
        }
        if (t[i] == "beginbfrange") {
            //  Entweder <lo> <hi> <dstStart> oder <lo> <hi> [ <d1> <d2> … ]
            int j = i + 1;
            while (j < t.size() && t[j] != "endbfrange") {
                if (j + 2 >= t.size()) return e;
                QByteArray lob, hib;
                if (!hexStringBytes(t[j], &lob) || !hexStringBytes(t[j+1], &hib))
                    return e;
                quint16 lo = 0, hi = 0;
                if (!codeOf(lob, &lo) || !codeOf(hib, &hi) || hi < lo) return e;
                //  Bereichsgröße deckeln - eine absurd große Angabe wäre ein
                //  Zeichen für eine defekte/feindliche Datei.
                if (int(hi) - int(lo) > 65535) return e;
                if (t[j+2] == "[") {
                    int k = j + 3;
                    quint16 cur = lo;
                    while (k < t.size() && t[k] != "]") {
                        QByteArray db;
                        if (!hexStringBytes(t[k], &db)) return e;
                        if (cur <= hi) put(cur, utf16beToString(db));
                        ++cur; ++k;
                    }
                    if (k >= t.size()) return e;
                    j = k + 1;
                } else {
                    QByteArray db;
                    if (!hexStringBytes(t[j+2], &db)) return e;
                    const QString start = utf16beToString(db);
                    if (start.isEmpty()) return e;
                    for (quint16 cd = lo; ; ++cd) {
                        //  Nur das LETZTE Zeichen zählt hoch (so definiert die
                        //  Spezifikation den Bereich).
                        QString v = start;
                        v[v.size() - 1] = QChar(char16_t(start.at(start.size()-1).unicode()
                                                         + (cd - lo)));
                        put(cd, v);
                        if (cd == hi) break;
                    }
                    j += 3;
                }
            }
            if (j >= t.size() || t[j] != "endbfrange") return e;
            i = j; continue;
        }
    }

    //  Ohne jede Zuordnung ist die CMap wertlos -> ablehnen.
    if (e.m_cidToUni.isEmpty())
        return e;
    if (ok) *ok = true;
    return e;
}

} // namespace mg::pdfenc
