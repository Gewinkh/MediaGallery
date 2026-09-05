#pragma once
// Byte-nahe Bausteine für das Arbeiten AN einer PDF-Datei (Objekt-Tabelle, Dict-Parser, Streams,
// zlib), geteilt von Splicing und Vektor-Export - ein Fehler in dieser Byte-Arbeit müsste sonst
// zweimal gefunden werden. Im Zweifel "nicht gefunden" (-1 / leer) statt raten; wirft nie.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace mg::pdfobj {

// zlib
//  Versucht zuerst den zlib-Header, dann „raw" (−15) - PDFs enthalten beides.
//  Der zweite Versuch trägt nur mit einkompiliertem ZLIB (s. core/ZCodec.h).
QByteArray zInflate(const QByteArray& src, bool* ok);
QByteArray zDeflate(const QByteArray& src);

inline bool isWs(char c) {
    return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\0';
}
inline bool isDelim(char c) {
    return c=='('||c==')'||c=='<'||c=='>'||c=='['||c==']'||c=='{'||c=='}'||c=='/'||c=='%';
}

// Objekt-Tabelle
//  Brute-Scan „N G obj" -> Byte-Offset + Generation. Das LETZTE Vorkommen
//  gewinnt, entspricht also dem jüngsten inkrementellen Save.
struct ObjLoc { qint64 offset; int gen; };
QHash<int, ObjLoc> scanObjects(const QByteArray& b);

QByteArray objectBody(const QByteArray& buf, qint64 offset);

// Mini-Parser
//  Ende des Wertes ab Index i; überspringt Dict/Array/String/Hex/Name/Zahl/
//  Referenz/Keyword als GANZE Einheit.
qint64 skipValue(const QByteArray& b, qint64 i);

//  Werteanfang von `/key` auf Ebene 0 des Dict-Inhalts (−1 = nicht vorhanden).
//  Springt über die WERTE, damit ein Wert-Name nie fälschlich als Schlüssel gilt.
qint64 findKey(const QByteArray& dict, const char* key);

QByteArray dictOfObject(const QByteArray& objBody);

int        refValue (const QByteArray& dict, const char* key);   // "/k N G R" -> N, sonst −1
QByteArray nameValue(const QByteArray& dict, const char* key);   // "/k /Name" -> "/Name"

// `/Length` auch als indirekte Referenz ("/Length 13 0 R") - genau so schreibt Qt seine PDFs, und `intValue`
// las daraus die 13 und schnitt den Strom nach 13 Bytes ab. -1 = keine Angabe.
qint64 streamLength(const QByteArray& dict, const QByteArray& buf,
                    const QHash<int, ObjLoc>& objs);
qint64     intValue (const QByteArray& dict, const char* key);   // "/k 42"    -> 42, sonst −1

//  Rohes Wert-Stück von `/key` (leer, wenn nicht vorhanden) - anders als die
//  drei Typ-Helfer oben ohne jede Deutung.
QByteArray rawValue(const QByteArray& dict, const char* key);
QByteArray setDictKey(QByteArray dict, const char* key, const QByteArray& value);
QList<QByteArray> dictKeys(const QByteArray& dict);
QVector<double> numbersOfArray(const QByteArray& arr);

// Zahlen / Strings / Namen schreiben und lesen
//  PDF-Zahl: höchstens drei Nachkommastellen, kein Exponent, Punkt als
//  Dezimaltrenner (locale-unabhängig).
QByteArray num(qreal v);
//  Literal-String `( … )`; `(`, `)`, `\` escaped, alles außerhalb des
//  druckbaren ASCII oktal - 7-Bit-sicher.
QByteArray parenString(const QByteArray& bytes);
//  Liest den STRING-Wert ab `i` (Literal `(…)` oder Hex `<…>`) als ROHBYTES.
//  Liefert false, wenn dort kein String steht oder er unabgeschlossen ist.
bool readPdfStringBytes(const QByteArray& b, qint64 i, QByteArray* out);
//  PDF-Textstring-Rohbytes -> Text (UTF-16BE am BOM erkannt, sonst PDFDoc-
//  Encoding, das für die belegten Codes mit Latin-1 übereinstimmt).
QString pdfTextToString(const QByteArray& raw);
//  Text -> PDF-Textstring (reines ASCII bleibt lesbares Literal, alles andere
//  wird UTF-16BE mit BOM).
QByteArray toPdfTextString(const QString& s);
QString nameToString(const QByteArray& name);
QByteArray toPdfName(const QString& s);

// Eine GEÖFFNETE Datei mit Objekt- und Seitentabelle, Grundlage aller byte-nahen Einheiten. `load()` prüft
// EINMAL die Vorbedingungen: lesbar, `%PDF-`, unverschlüsselt, klassische xref-Tabelle, Seitenbaum lesbar.
struct PdfDoc {
    QByteArray         buf;                   // die vollständige Datei
    QHash<int, ObjLoc> objs;
    int                rootNum  = -1;
    qint64             prevXref = -1;         // Offset der bisherigen xref (für /Prev)
    QVector<int>       pageObjs;              // Objektnummer je Seitenindex
    //  Annotation -> Seitenindex, aus den `/Annots` der Seiten gewonnen. Das ist
    //  die verlässliche Richtung; `/P` in der Annotation ist optional.
    QHash<int, int>    annotPage;

    QByteArray bodyOf(int n) const;
    QByteArray dictOf(int n) const;
    int        genOf (int n) const;
    //  Wert von `/key`; ist er eine Referenz, wird das referenzierte Objekt
    //  eingesetzt. So sieht der Aufrufer immer den EIGENTLICHEN Wert.
    QByteArray resolved(const QByteArray& dict, const char* key) const;
    QSizeF pageBox(int pageObj) const;
    int pageRotate(int pageObj) const;
    int maxObjNum() const;

    //  Öffnet `path` und baut die Tabellen auf. `err` erhält bei false einen
    //  kurzen Grund.
    bool load(const QString& path, QString* err = nullptr);
};

// Eine PDF FORTSCHREIBEN statt neu schreiben: Originalbytes 1:1, angehängt werden nur neue oder
// ersetzte Objekte, xref und Trailer mit /Prev. Nichts geht verloren, was wir nicht verstanden haben.
// Ersetzen = dasselbe Objekt noch einmal anhängen; `scanObjects` nimmt das LETZTE Vorkommen.
class IncrementalUpdate {
public:
    //  Übernimmt die Bytes von `doc` als Grundlage (der Aufrufer muss `doc`
    //  danach nicht am Leben halten).
    explicit IncrementalUpdate(const PdfDoc& doc);

    int reserveObjNum();
    void addObject(int num, int gen, const QByteArray& body);
    void addStream(int num, int gen, const QByteArray& dictExtra,
                   const QByteArray& data);
    void replaceDict(int num, const QByteArray& dictInner);

    bool isEmpty() const;
    //  xref + Trailer anhängen und atomar nach `outputPath` schreiben.
    //  `outputPath` darf NICHT die Quelle sein (das prüft der Aufrufer).
    bool commit(const QString& outputPath, QString* err = nullptr);

private:
    struct Entry { int num; qint64 off; int gen; };
    QByteArray     m_out;
    QVector<Entry> m_entries;
    int            m_nextObj = 1;
    int            m_rootNum = -1;
    qint64         m_prevXref = -1;
    QHash<int,int> m_gens;
};

// PDF-Benutzerraum (Ursprung unten-links, ungedreht) -> Editor-Raum (Ursprung oben-links der ANGEZEIGTEN
// Seite). Umkehrung der `cm`-Abbildung aus PdfVectorExport - dieselbe Konvention im ganzen Projekt.
QPointF toDisplay(double ux, double uy, const QSizeF& box, int rot);
QPointF toUser(double dx, double dy, const QSizeF& box, int rot);

} // namespace mg::pdfobj
