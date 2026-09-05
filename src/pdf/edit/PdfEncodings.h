#pragma once
// Byte <-> Unicode für einfache (nicht-CID) PDF-Fonts; ohne diese Tabelle ist nur reines ASCII
// verlustfrei ersetzbar. Unterstützt WinAnsi, MacRoman, /Differences, AGL-Namen und Identity-H über
// /ToUnicode. Sonst weicht der Aufrufer auf Raster aus - geraten wird nie.

#include <QByteArray>
#include <QHash>
#include <QString>

namespace mg::pdfenc {

enum class Base {
    AsciiOnly,      // unbekannt/fehlend -> nur 0x20–0x7E gelten als sicher
    WinAnsi,
    MacRoman,
    IdentityCid,    // Type0 /Identity-H: 2-Byte-Codes, Tabelle aus /ToUnicode
};

//  Eine aufgelöste Kodierung eines konkreten Fonts: Basistabelle plus die
//  Abweichungen aus /Differences.
class Encoding {
public:
    // `encValue` ist ein Name ("/WinAnsiEncoding") oder ein Dict mit /BaseEncoding und /Differences; leer = keine
    // Angabe. `ok` wird false bei einem nicht auflösbaren Glyphennamen - dann darf der Aufrufer nichts ersetzen.
    static Encoding fromEncodingValue(const QByteArray& encValue, bool* ok = nullptr);

    // Bei /Identity-H sind die Stringbytes Glyphennummern; welches Zeichen dahintersteht, sagt einzig
    // die /ToUnicode-CMap. Umgekehrt gelesen begrenzt sie Ersetzungen auf Zeichen, die im Dokument schon
    // vorkommen - eine Teilmengen-Schrift hat keine anderen Glyphen. `ok` false = nicht sicher auswertbar.
    static Encoding fromCidToUnicode(const QByteArray& cmap, bool* ok = nullptr);

    int codeBytes() const { return m_base == Base::IdentityCid ? 2 : 1; }

    //  Byte -> Zeichen. Liefert QChar() (null), wenn der Code in dieser
    //  Kodierung nicht belegt ist.
    QChar toUnicode(quint8 code) const;

    //  Zeichen -> Byte. Liefert false, wenn das Zeichen in dieser Kodierung
    //  nicht darstellbar ist (Aufrufer: Ersetzung ablehnen).
    bool fromUnicode(QChar c, quint8* code) const;

    QString    decode(const QByteArray& bytes) const;
    bool       encode(const QString& text, QByteArray* out) const;

    Base base() const { return m_base; }

private:
    Base                   m_base = Base::AsciiOnly;
    QHash<quint8, QChar>   m_diffToUni;    // aus /Differences
    QHash<QChar, quint8>   m_uniToCode;    // Rückrichtung (Differences gewinnen)
    //  Identity-H: 2-Byte-Code ↔ Text. Der Wert ist ein QString, weil ein Code
    //  laut CMap auf MEHRERE Zeichen zeigen darf (Ligaturen); solche Einträge
    //  kommen bewusst NICHT in die Rückrichtung.
    QHash<quint16, QString> m_cidToUni;
    QHash<QChar, quint16>   m_uniToCid;
};

//  Glyphenname -> Zeichen (Adobe Glyph List, Teilmenge + uniXXXX/uXXXX).
//  Liefert QChar() bei unbekanntem Namen.
QChar glyphToUnicode(const QByteArray& glyphName);

} // namespace mg::pdfenc
