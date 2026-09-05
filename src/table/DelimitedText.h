#pragma once
//  DelimitedText - Leser fuer getrennte Textdateien (CSV/TSV und Verwandte).
//  Kennt nur Trennzeichen, Klammerung und Kodierung; was die Zeilen BEDEUTEN,
//  entscheidet der Aufrufer. Der DATEV-Leser sitzt darauf (s. datev/DatevCsv.h).
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <utility>

namespace mg::table {

//  Groessen-Deckel gegen praeparierte Dateien: eine echte Zeile bleibt weit
//  darunter, ein Ueberlauf traegt hier nur noch abgeschnittene Reste.
inline constexpr int kMaxFeldZeichen = 1 << 16;
inline constexpr int kMaxFelderZeile = 4096;
inline constexpr int kMaxZeilen      = 1'000'000;

//  Die Trennzeichen, die geraten werden - `;` steht vorn und gewinnt jeden
//  Gleichstand: es ist das im deutschsprachigen Raum uebliche.
inline constexpr char16_t kTrenner[] = { u';', u',', u'\t', u'|' };

struct Warnung {
    int     zeile = 0;          // 1-basiert, wie sie ein Mensch zaehlt
    QString text;
};

//  Eine Zeile haelt nur die BELEGTEN Felder (Spaltennummer + Wert, aufsteigend;
//  `wert()` sucht binaer). Dicht gespeichert kosten die leeren Plaetze einer
//  breiten Datei ein Vielfaches - gemessen an 50.000 DATEV-Buchungen mit
//  125 Spalten: 4178 gegen 835 Byte je Zeile.
class Zeile {
public:
    Zeile() = default;
    explicit Zeile(const QStringList& felder);

    QString wert(int spalte) const;
    int     felder() const { return m_felder; }
    bool    isEmpty() const { return m_felder == 0; }

    //  Nur die belegten Felder, aufsteigend. Wer ueber alle Zeilen laeuft,
    //  nimmt das hier - `wert()` je Spalte waere bei 125 Spalten ein Vielfaches.
    const QList<std::pair<quint16, QString>>& belegte() const { return m_gefuellt; }

    //  Alle Felder als Liste, leere eingeschlossen. Fuer die wenigen Zeilen
    //  gedacht, die als Ganzes gebraucht werden (etwa eine Kopfzeile).
    QStringList alle() const;

    //  Die Zeile auf mindestens `n` Felder erklaeren, ohne leere Plaetze
    //  anzulegen. Ein Aufrufer, der eine feste Spaltenzahl kennt, meldet sie so
    //  fuer die Anzeige, ohne dass Speicher dafuer draufgeht.
    void ensureFelder(int n) { if (n > m_felder) m_felder = n; }

private:
    QList<std::pair<quint16, QString>> m_gefuellt;
    int m_felder = 0;
};

struct Datei {
    bool          ok = false;
    QString       fehler;
    QList<Zeile>  zeilen;
    //  Quellzeilennummer je Datenzeile (1-basiert). Leerzeilen und
    //  zusammengezogene Datensaetze verschieben die Zaehlung sonst, und eine
    //  Meldung „Zeile 47" muesste dann in der Datei woanders stehen.
    QList<int>    zeilenNr;
    QList<Warnung> warnungen;
    QChar         trenner = u';';
    bool          cp1252 = false;
    bool          abgeschnitten = false;   // Deckel erreicht
};

//  Ein Abschnitt der Datei - ein Block zwischen zwei Leerzeilen. Ausgaben
//  stapeln mehrere Tabellen in EINE Datei und trennen sie genau so.
struct Bereich {
    int von   = 0;      // erste Zeile des Blocks (Index in `Datei::zeilen`)
    int bis   = 0;      // eine HINTER der letzten
    int titel = -1;     // Zeile mit dem Blocknamen, sonst -1
    int kopf  = -1;     // Zeile mit den Spaltennamen, sonst -1
    int daten = 0;      // erste Datenzeile
};

//  Die Bloecke einer Datei. Getrennt wird ausschliesslich an LEERZEILEN - das
//  ist keine Schaetzung, sondern steht so in der Datei. Innerhalb eines Blocks
//  gilt: eine erste Zeile mit genau EINEM Feld ueber einer breiteren ist der
//  Blockname, und dann traegt die naechste Zeile die Spaltennamen. Ohne
//  Blocknamen entscheidet dieselbe Vermutung wie sonst (Zeile ohne Zahlen ueber
//  einer mit). Eine Datei ohne Leerzeilen ergibt genau EINEN Bereich.
QList<Bereich> findBlocks(const Datei& d);

//  Eine Zeile in Felder zerlegen.
//  `""` ist ein Anfuehrungszeichen im Feld. Ein einzelnes `"`, auf das WEDER
//  Trenner noch Zeilenende folgt, gilt als Inhalt - eine DATEV-Vorlage enthaelt
//  `" "Normalabschr. immater. VermG" "`, und ein Zerleger, der dort schliesst,
//  verliert den halben Text.
QStringList splitRecord(QStringView zeile, QChar trenner, bool* unbalanciert = nullptr);

//  Welches Trennzeichen benutzt die Datei? Gewertet wird ueber die ersten
//  Zeilen: gesucht ist das Zeichen, das die gleichmaessigste Feldzahl (>= 2)
//  ergibt. Findet sich keines, bleibt es bei `;`.
QChar detectSeparator(const QString& text, int zeilenProbe = 20);

//  Rohe Bytes -> Zeilen. Kodierung wird erkannt (UTF-8, sonst CP1252).
//  Ein ungueltiger `trenner` (Null) laesst `detectSeparator` entscheiden.
Datei parse(const QByteArray& raw, QChar trenner = QChar());

}  // namespace mg::table
