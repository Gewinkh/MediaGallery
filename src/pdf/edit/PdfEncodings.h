#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfEncodings.h - Byte ↔ Unicode für EINFACHE PDF-Textkodierungen
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Ein einfacher (nicht-CID) PDF-Font bildet jedes Byte eines Textstrings über
//  eine Kodierungstabelle auf ein Zeichen ab. Ohne diese Tabelle lässt sich
//  eingebetteter Text nur dann verlustfrei ersetzen, wenn er reines ASCII ist
//  (dort stimmen alle Tabellen überein) - Umlaute, Akzente und typografische
//  Zeichen fielen bislang auf den Raster-Export zurück.
//
//  UNTERSTÜTZT
//  ───────────
//   • /WinAnsiEncoding   (CP1252; in westlichen PDFs die Regel)
//   • /MacRomanEncoding
//   • /Encoding-Dict mit /BaseEncoding + /Differences (Glyphennamen)
//   • Glyphennamen der Adobe Glyph List, soweit die obigen Kodierungen und
//     Latin-1 sie brauchen, zusätzlich die Formen `uniXXXX` und `uXXXX…`.
//   • **Type0/CID mit /Encoding /Identity-H** über die /ToUnicode-CMap des
//     Fonts (2-Byte-Codes, s. `fromCidToUnicode`).
//
//  BEWUSST NICHT UNTERSTÜTZT (Aufrufer weicht dann auf Raster aus)
//  ──────────────────────────────────────────────────────────────
//   • Type0 OHNE /ToUnicode oder mit anderer /Encoding-CMap als /Identity-H -
//     dann ist nicht bestimmbar, welches Zeichen ein Code meint,
//   • unbekannte Glyphennamen in /Differences - es wird NICHT geraten,
//   • fehlendes/unbekanntes /Encoding: dann gilt nur ASCII als sicher, denn
//     die eingebaute Kodierung eines Fonts ist von außen nicht zuverlässig
//     bestimmbar (bei StandardEncoding weichen z. B. 0x27 und 0x60 ab).
//
//  ABHÄNGIGKEITEN: nur Qt6::Core. Kein Q_OBJECT/moc; isoliert testbar.
// ══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <QHash>
#include <QString>

namespace mg::pdfenc {

//  Welche Basistabelle gilt.
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
    //  Baut die Kodierung aus dem Wert des /Encoding-Eintrags eines Fonts.
    //  `encValue` ist entweder ein Name ("/WinAnsiEncoding") oder ein Dict
    //  ("<< /BaseEncoding … /Differences [ … ] >>"); leer = keine Angabe.
    //  `ok` (optional) wird false, wenn ein /Differences-Eintrag einen
    //  Glyphennamen nennt, der nicht auflösbar ist - dann darf der Aufrufer
    //  KEINE Ersetzung wagen (Zeichen wären nicht eindeutig).
    static Encoding fromEncodingValue(const QByteArray& encValue, bool* ok = nullptr);

    //  Baut die Kodierung eines Type0-Fonts mit /Encoding /Identity-H aus dem
    //  ENTPACKTEN Inhalt seines /ToUnicode-CMap-Streams.
    //
    //  Warum ausgerechnet /ToUnicode: bei /Identity-H sind die Stringbytes
    //  2-Byte-CIDs, also Glyphennummern der (meist als Teilmenge eingebetteten)
    //  Schrift. Welches Zeichen dahintersteht, sagt EINZIG diese CMap - dieselbe
    //  Tabelle, die auch Betrachter fürs Kopieren benutzen.
    //
    //  Für das ZURÜCKSCHREIBEN wird sie umgekehrt. Das begrenzt Ersetzungen
    //  zwangsläufig auf Zeichen, die im Dokument bereits vorkommen: eine
    //  Teilmengen-Schrift enthält keine anderen Glyphen, ein fremdes Zeichen
    //  käme als Leerkasten heraus. `encode` lehnt solche Zeichen daher ab.
    //  Mehrdeutige Einträge (ein Code -> mehrere Zeichen, z. B. Ligaturen)
    //  gelten nur in Leserichtung.
    //
    //  `ok` wird false, wenn die CMap nicht sicher auswertbar ist.
    static Encoding fromCidToUnicode(const QByteArray& cmap, bool* ok = nullptr);

    //  Bytes je Code: 1 (einfache Fonts) bzw. 2 (Identity-H).
    int codeBytes() const { return m_base == Base::IdentityCid ? 2 : 1; }

    //  Byte -> Zeichen. Liefert QChar() (null), wenn der Code in dieser
    //  Kodierung nicht belegt ist.
    QChar toUnicode(quint8 code) const;

    //  Zeichen -> Byte. Liefert false, wenn das Zeichen in dieser Kodierung
    //  nicht darstellbar ist (Aufrufer: Ersetzung ablehnen).
    bool fromUnicode(QChar c, quint8* code) const;

    //  Bequemlichkeit: ganze Bytefolge -> Text bzw. Text -> Bytefolge.
    //  `decode` ersetzt unbelegte Codes durch U+FFFD, damit ein Vergleich
    //  gegen den gesuchten Originaltext sicher scheitert statt zufällig zu
    //  passen. `encode` liefert false, sobald EIN Zeichen fehlt.
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
