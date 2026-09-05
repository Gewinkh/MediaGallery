#pragma once
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringView>

// Der Begriff wird immer woertlich gesucht; traegt er gueltige Regex-Sonderzeichen,
// zusaetzlich als Muster, beide Trefferlisten vereinigt. Kein Umschalter, weil eine
// halbfertige Eingabe dann keinen Fehlerzustand mehr hat - "(" sucht einfach weiter.
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

    // Alle Fundstellen, sortiert und überschneidungsfrei; `cap` deckelt die Anzahl - ein Muster wie `.` trifft sonst
    // jedes Zeichen. LEERE Treffer werden verworfen: sie wären weder markierbar noch ersetzbar.
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
