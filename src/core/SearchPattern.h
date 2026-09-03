#pragma once
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringView>

// ─────────────────────────────────────────────────────────────────────────────
//  SearchPattern.h - EIN Suchbegriff fuer die ganze App.
//
//  Der Begriff wird IMMER woertlich gesucht. Traegt er zusaetzlich Sonderzeichen
//  eines regulaeren Ausdrucks (`\d{4}`, `^#include`, `a|b`) und ist er als
//  Ausdruck gueltig, wird er ZUSAETZLICH als Muster gesucht und beide
//  Trefferlisten werden vereinigt.
//
//  Warum keine Umschaltung „Regex an/aus", wie sie jede IDE hat:
//   • Mit Umschalter sucht man frueher oder spaeter `\d{4}` woertlich und
//     findet nichts, oder man sucht `datei.txt` und bekommt `dateiXtxt` dazu.
//   • Eine HALBFERTIGE Eingabe hat keinen Fehlerzustand mehr: waehrend man
//     `(\w+)` tippt, ist `(` kein gueltiger Ausdruck - der Muster-Zweig faellt
//     dann einfach weg, der woertliche sucht `(` weiter. Kein rotes Feld.
//
//  ZWEI Regeln tragen das:
//   1. Der Muster-Zweig laeuft NUR, wenn der Begriff ein Sonderzeichen enthaelt
//      UND uebersetzbar ist. Ohne Sonderzeichen waeren beide Zweige identisch -
//      dann kostet die Vereinigung nichts, weil sie gar nicht stattfindet.
//   2. Die vereinigte Liste ist UEBERSCHNEIDUNGSFREI (`mergeRanges`). Zwei sich
//      ueberlappende Treffer nacheinander zu ersetzen zerlegt den Text - das
//      ist der einzige Weg, auf dem diese Bequemlichkeit gefaehrlich wuerde.
//
//  ERSETZT wird immer woertlich: `\1` hat fuer einen woertlichen Treffer keine
//  Bedeutung, und ein Treffer kann aus beiden Zweigen stammen. Wer `\d{4}` als
//  Ersatztext eingibt, bekommt genau diese sechs Zeichen.
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::search {

struct Range {
    int start  = 0;
    int length = 0;
    friend bool operator==(const Range& a, const Range& b) {
        return a.start == b.start && a.length == b.length;
    }
};

//  Traegt der Begriff ein Zeichen, das als Muster etwas anderes bedeutet als
//  als Buchstabe? Nur dann lohnt der zweite Zweig.
bool hasMetaCharacters(QStringView s);

//  Sortiert nach Startposition und wirft alles weg, was einen bereits
//  behaltenen Treffer ueberlappt. Bei gleichem Start gewinnt der laengere.
void mergeRanges(QList<Range>& bereiche);

class Pattern {
public:
    Pattern() = default;
    explicit Pattern(const QString& needle, bool caseSensitive = false,
                     bool wholeWords = false);

    bool isEmpty() const { return m_literal.isEmpty(); }
    //  Wurde der Begriff (auch) als Muster gelesen? Die Suchleiste zeigt das an.
    bool usesRegex() const { return m_reAktiv; }

    const QString&            literal() const { return m_literal; }
    const QRegularExpression& regex() const { return m_re; }
    bool  caseSensitive() const { return m_case; }
    bool  wholeWords() const { return m_whole; }
    Qt::CaseSensitivity caseSensitivity() const {
        return m_case ? Qt::CaseSensitive : Qt::CaseInsensitive;
    }

    //  Kommt der Begriff im Text vor? Fuer Filter (Galerie, Wiedergabeliste):
    //  der woertliche Zweig laeuft zuerst, weil er der billigere ist.
    bool contains(QStringView hay) const;

    //  Alle Fundstellen, sortiert und ueberschneidungsfrei. `cap` deckelt die
    //  Anzahl - ein Muster wie `.` trifft sonst jedes Zeichen der Datei.
    //  LEERE Treffer (`a*` auf einer Leerzeile) werden verworfen: sie waeren
    //  weder markierbar noch ersetzbar und wuerden endlos laufen.
    QList<Range> findAll(QStringView hay, int cap = 100000) const;

    //  Erster Treffer, der BEI ODER HINTER `from` beginnt - fuer Suchleisten,
    //  die je Absatz weitersuchen. `length == 0` heisst „keiner".
    Range firstFrom(QStringView hay, int from) const;
    //  Letzter Treffer, der bei oder VOR `end` endet (Rueckwaertssuche).
    Range lastEndingAtOrBefore(QStringView hay, int end) const;
    //  Ist `hay` GENAU ein Treffer? Antwort auf „steht der Cursor wirklich auf
    //  dem, was ersetzt werden soll?" - bei einem Muster kann man den Wortlaut
    //  nicht mehr vergleichen.
    bool matchesWhole(QStringView hay) const;

private:
    QString            m_literal;
    QRegularExpression m_re;
    QRegularExpression m_literalRe;   // nur bei „ganze Woerter" gebraucht
    bool m_reAktiv = false;
    bool m_case    = false;
    bool m_whole   = false;
};

}  // namespace mg::search
