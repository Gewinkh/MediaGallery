#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfObjects.h — gemeinsame Bausteine für das Arbeiten AN einer PDF-Datei
// ══════════════════════════════════════════════════════════════════════════════
//
//  Byte-nahe Helfer, die sowohl das verlustfreie Text-Splicing
//  (`PdfContentEditor`) als auch der verlustfreie Vektor-Export
//  (`PdfVectorExport`) brauchen: Objekt-Tabelle, Mini-Dict-Parser,
//  Stream-Zugriff, zlib.
//
//  WARUM ALS EIGENE EINHEIT: beide Nutzer arbeiten auf demselben
//  Dateiformat und derselben Sicherheitszusage („bei jeder Unsicherheit
//  abbrechen"). Zwei Kopien dieser bewusst streng geprüften Byte-Arbeit
//  wären die schlechtere Lösung — ein Fehler müsste an zwei Stellen
//  gefunden und behoben werden.
//
//  GRUNDSATZ: Jede Funktion ist gutmütig gegenüber Müll-Eingaben und liefert
//  im Zweifel „nicht gefunden" (−1 / leer) statt zu raten oder zu lesen, was
//  ihr nicht gehört. Sie wirft nie und greift nie außerhalb der Puffer zu.
//
//  ABHÄNGIGKEITEN: Qt6::Core + ZLIB. Kein Q_OBJECT/moc; isoliert testbar.
// ══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace mg::pdfobj {

// ── zlib ────────────────────────────────────────────────────────────────────
//  Versucht zuerst den zlib-Header, dann „raw" (−15) — PDFs enthalten beides.
QByteArray zInflate(const QByteArray& src, bool* ok);
QByteArray zDeflate(const QByteArray& src);

// ── Zeichenklassen des PDF-Lexers ───────────────────────────────────────────
inline bool isWs(char c) {
    return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\0';
}
inline bool isDelim(char c) {
    return c=='('||c==')'||c=='<'||c=='>'||c=='['||c==']'||c=='{'||c=='}'||c=='/'||c=='%';
}

// ── Objekt-Tabelle ──────────────────────────────────────────────────────────
//  Brute-Scan „N G obj" → Byte-Offset + Generation. Das LETZTE Vorkommen
//  gewinnt, entspricht also dem jüngsten inkrementellen Save.
struct ObjLoc { qint64 offset; int gen; };
QHash<int, ObjLoc> scanObjects(const QByteArray& b);

//  Objektkörper ab `offset` (zwischen "obj" und "endobj").
QByteArray objectBody(const QByteArray& buf, qint64 offset);

// ── Mini-Parser ─────────────────────────────────────────────────────────────
//  Ende des Wertes ab Index i; überspringt Dict/Array/String/Hex/Name/Zahl/
//  Referenz/Keyword als GANZE Einheit.
qint64 skipValue(const QByteArray& b, qint64 i);

//  Werteanfang von `/key` auf Ebene 0 des Dict-Inhalts (−1 = nicht vorhanden).
//  Springt über die WERTE, damit ein Wert-Name nie fälschlich als Schlüssel gilt.
qint64 findKey(const QByteArray& dict, const char* key);

//  Der `<< … >>`-Inhalt eines Objektkörpers (ohne die äußeren Klammern).
QByteArray dictOfObject(const QByteArray& objBody);

int        refValue (const QByteArray& dict, const char* key);   // "/k N G R" → N, sonst −1
QByteArray nameValue(const QByteArray& dict, const char* key);   // "/k /Name" → "/Name"
qint64     intValue (const QByteArray& dict, const char* key);   // "/k 42"    → 42, sonst −1

//  Rohes Wert-Stück von `/key` (leer, wenn nicht vorhanden) — anders als die
//  drei Typ-Helfer oben ohne jede Deutung.
QByteArray rawValue(const QByteArray& dict, const char* key);
//  Setzt/ersetzt `/key` im Dict-INHALT (ohne die äußeren `<< >>`).
QByteArray setDictKey(QByteArray dict, const char* key, const QByteArray& value);
//  Alle Schlüsselnamen auf Ebene 0 eines Dict-Inhalts, in Dokumentreihenfolge.
QList<QByteArray> dictKeys(const QByteArray& dict);
//  Alle Zahlen eines `[ … ]`-Stücks (auch verschachtelte Klammern werden als
//  Zahlenfolge gelesen — der Aufrufer weiß, wie viele er erwartet).
QVector<double> numbersOfArray(const QByteArray& arr);

// ── Zahlen / Strings / Namen schreiben und lesen ────────────────────────────
//  PDF-Zahl: höchstens drei Nachkommastellen, kein Exponent, Punkt als
//  Dezimaltrenner (locale-unabhängig).
QByteArray num(qreal v);
//  Literal-String `( … )`; `(`, `)`, `\` escaped, alles außerhalb des
//  druckbaren ASCII oktal — 7-Bit-sicher.
QByteArray parenString(const QByteArray& bytes);
//  Liest den STRING-Wert ab `i` (Literal `(…)` oder Hex `<…>`) als ROHBYTES.
//  Liefert false, wenn dort kein String steht oder er unabgeschlossen ist.
bool readPdfStringBytes(const QByteArray& b, qint64 i, QByteArray* out);
//  PDF-Textstring-Rohbytes → Text (UTF-16BE am BOM erkannt, sonst PDFDoc-
//  Encoding, das für die belegten Codes mit Latin-1 übereinstimmt).
QString pdfTextToString(const QByteArray& raw);
//  Text → PDF-Textstring (reines ASCII bleibt lesbares Literal, alles andere
//  wird UTF-16BE mit BOM).
QByteArray toPdfTextString(const QString& s);
//  PDF-Name → Text ohne führenden Schrägstrich; `#xx` wird aufgelöst.
QString nameToString(const QByteArray& name);
//  Text → PDF-Name mit führendem Schrägstrich; Sonderzeichen als `#xx`.
QByteArray toPdfName(const QString& s);

// ══════════════════════════════════════════════════════════════════════════════
//  PdfDoc — eine GEÖFFNETE Datei mit Objekt- und Seitentabelle
// ══════════════════════════════════════════════════════════════════════════════
//
//  Gemeinsame Grundlage aller Einheiten, die eine PDF an der Byte-Ebene lesen
//  oder inkrementell fortschreiben (`PdfFormFields`, `PdfAnnotations`, …).
//  `load()` prüft dabei EINMAL die Vorbedingungen, unter denen ein
//  inkrementelles Update sicher ist: lesbar, `%PDF-`, unverschlüsselt,
//  klassische xref-Tabelle (kein XRef-Stream), Seitenbaum lesbar.
struct PdfDoc {
    QByteArray         buf;                   // die vollständige Datei
    QHash<int, ObjLoc> objs;
    int                rootNum  = -1;
    qint64             prevXref = -1;         // Offset der bisherigen xref (für /Prev)
    QVector<int>       pageObjs;              // Objektnummer je Seitenindex
    //  Annotation → Seitenindex, aus den `/Annots` der Seiten gewonnen. Das ist
    //  die verlässliche Richtung; `/P` in der Annotation ist optional.
    QHash<int, int>    annotPage;

    QByteArray bodyOf(int n) const;
    QByteArray dictOf(int n) const;
    int        genOf (int n) const;
    //  Wert von `/key`; ist er eine Referenz, wird das referenzierte Objekt
    //  eingesetzt. So sieht der Aufrufer immer den EIGENTLICHEN Wert.
    QByteArray resolved(const QByteArray& dict, const char* key) const;
    //  Ungedrehte Seitengröße aus der (ggf. geerbten) /MediaBox.
    QSizeF pageBox(int pageObj) const;
    //  Eigenes oder geerbtes /Rotate, normalisiert auf 0/90/180/270.
    int pageRotate(int pageObj) const;
    //  Höchste vergebene Objektnummer (Basis für angehängte Objekte).
    int maxObjNum() const;

    //  Öffnet `path` und baut die Tabellen auf. `err` erhält bei false einen
    //  kurzen Grund.
    bool load(const QString& path, QString* err = nullptr);
};

// ══════════════════════════════════════════════════════════════════════════════
//  IncrementalUpdate — eine PDF FORTSCHREIBEN, ohne sie neu zu schreiben
// ══════════════════════════════════════════════════════════════════════════════
//
//  Verfahren aller schreibenden Einheiten des Projekts (`PdfContentEditor`,
//  `PdfVectorExport`, `PdfFormFields`, `PdfAnnotations`): Die Originalbytes
//  bleiben 1:1 erhalten, angehängt werden nur neue oder ERSETZTE Objekte, eine
//  kleine xref-Sektion und ein Trailer mit `/Prev`. Vorteile: nichts geht
//  verloren, was wir nicht verstanden haben, und ein Fehlschlag hinterlässt
//  keine halbe Datei (`commit` schreibt atomar über `QSaveFile`).
//
//  ERSETZEN heißt hier: dasselbe Objekt noch einmal anhängen. Der Brute-Scan
//  in `scanObjects` nimmt beim nächsten Lesen das LETZTE Vorkommen — genau die
//  Semantik eines inkrementellen Updates.
class IncrementalUpdate {
public:
    //  Übernimmt die Bytes von `doc` als Grundlage (der Aufrufer muss `doc`
    //  danach nicht am Leben halten).
    explicit IncrementalUpdate(const PdfDoc& doc);

    //  Nächste freie Objektnummer (fortlaufend ab der höchsten belegten).
    int reserveObjNum();
    //  Objekt anhängen — `body` ist der vollständige Objektinhalt (z. B.
    //  `<< … >>` oder `[ … ]`), OHNE „N G obj"/„endobj".
    void addObject(int num, int gen, const QByteArray& body);
    //  Strom anhängen: `dictExtra` sind die Schlüssel neben `/Length`.
    void addStream(int num, int gen, const QByteArray& dictExtra,
                   const QByteArray& data);
    //  Bestehendes Objekt durch ein Dict ersetzen (Generation bleibt erhalten).
    void replaceDict(int num, const QByteArray& dictInner);

    //  Wurde überhaupt etwas angehängt?
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

//  Anzeigekoordinaten: PDF-Benutzerraum (Ursprung unten-links, ungedreht) →
//  Editor-Raum (Ursprung OBEN-LINKS der ANGEZEIGTEN, also gedrehten Seite).
//  Umkehrung der `cm`-Abbildung aus PdfVectorExport — dieselbe Konvention im
//  ganzen Projekt.
QPointF toDisplay(double ux, double uy, const QSizeF& box, int rot);
//  Gegenrichtung von `toDisplay` (Editor-Raum → PDF-Benutzerraum).
QPointF toUser(double dx, double dy, const QSizeF& box, int rot);

} // namespace mg::pdfobj
