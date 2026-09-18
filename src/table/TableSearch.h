#pragma once
//  TableSearch - Suche ueber die Zeilen einer getrennten Textdatei. Kennt weder
//  Anzeige noch DATEV: beide Tabellen-Controller benutzen dieselbe Maschine,
//  damit eine CSV und ein Buchungsstapel auf dieselbe Frage gleich antworten.
#include "simd/FindChar.h"
#include "table/DelimitedText.h"
#include "table/TableFormula.h"

#include <QList>
#include <QObject>
#include <QRunnable>
#include <QString>
#include <QStringMatcher>
#include <atomic>
#include <functional>
#include <memory>

namespace mg::table {

//  Deckel gegen die Suche nach einem einzelnen Buchstaben: bei 300.000 Zeilen
//  mal 20 Spalten waeren es Millionen Treffer zu je 8 Byte, und ansteuern kann
//  ein Mensch sie ohnehin nicht.
inline constexpr int kMaxTreffer = 200'000;

struct Treffer {
    int zeile = 0;      // Anzeigezeile, gezaehlt ab der ersten Datenzeile
    int spalte = 0;
};

struct SuchOptionen {
    bool gross      = false;    // Gross-/Kleinschreibung beachten
    bool ganzeZelle = false;    // die Zelle muss dem Text ENTSPRECHEN, nicht ihn enthalten
};

//  Welche Schreibweisen muss ein Vorfilter suchen, damit er nichts uebersieht,
//  das Qt als gleich ansaehe? Bis zu drei nach `aus`, Rueckgabe ihre Zahl;
//  0 heisst: kein Vorfilter. Die Klassen stammen aus Qts eigenen Tabellen
//  (`QChar::toCaseFolded`) - eine Liste im Quelltext altert gegen die naechste
//  Unicode-Fassung.
int vorfilterAnker(QChar c, char16_t* aus);

//  Trifft eine Zelle? Suche und Filter stellen dieselbe Frage und sollen sie
//  gleich beantworten.
class ZellVergleich {
public:
    ZellVergleich(const QString& text, SuchOptionen o);
    //  Im Kopf, damit der Laengen-Ausschluss vor jedem Aufruf steht: ausgelagert
    //  kostete die Suche nach einem seltenen Begriff 25 % mehr (8,9 -> 11,2 ms),
    //  die Laenge ueber den Textzeiger je Zelle noch einmal 7 %.
    bool trifft(const QString& zelle) const {
        //  Kuerzer als der Begriff kann nie treffen.
        if (zelle.size() < m_laenge) return false;
        if (m_ganzeZelle) return zelle.compare(m_text, m_gross) == 0;
        //  Bei EINEM Zeichen ist Qts eigener Weg schneller als eine Sprungtabelle,
        //  die nie springt (gemessen 5 gegen 13 ms ueber 100.000 Zeilen).
        if (m_laenge == 1) return zelle.contains(m_text, m_gross);
        //  Vorfilter: erst die Stelle suchen, an der der Begriff ueberhaupt
        //  anfangen kann, dann Qt ab dort fragen - ohne Gross-/Kleinschreibung
        //  kostet dessen Vergleich je Zelle 31,9 statt 10,4 ns. Geurteilt wird
        //  weiter von Qt: gefunden wird eine Stelle, entschieden mit `indexIn`.
        if (m_vorfilter) {
            const qsizetype rest = zelle.size() - m_laenge + 1;
            const auto* p = reinterpret_cast<const char16_t*>(zelle.constData());
            const qsizetype k = (m_anker == 3)
                ? mg::simd::findeErstesDrei(p, rest, m_ersteKl, m_ersteGr, m_ersteDr)
                : mg::simd::findeErstes(p, rest, m_ersteKl, m_ersteGr);
            if (k >= rest) return false;
            return m_sucher.indexIn(zelle, k) >= 0;
        }
        return m_sucher.indexIn(zelle) >= 0;
    }

private:
    QString             m_text;
    qsizetype           m_laenge;
    Qt::CaseSensitivity m_gross;
    //  EIN vorbereiteter Sucher statt `QString::contains` je Zelle: der baut
    //  seine Sprungtabelle einmal, `contains` faltet die Schreibweise bei jedem
    //  Aufruf neu.
    QStringMatcher      m_sucher;
    bool                m_ganzeZelle;
    //  Die Schreibweisen des ERSTEN Zeichens - zwei, manchmal drei, 0 = kein
    //  Vorfilter (s. `vorfilterAnker`).
    char16_t            m_ersteKl = 0;
    char16_t            m_ersteGr = 0;
    char16_t            m_ersteDr = 0;
    int                 m_anker = 0;
    bool                m_vorfilter = false;
};

//  Alle Treffer in [von, bis), aufsteigend nach Zeile und Spalte; `zeile` zaehlt
//  ab `von`. `spalten` laesst nur die angezeigten Spalten zu - ein Treffer in
//  einer ausgeblendeten Spalte waere ein Sprung ins Nichts. `abbruch` wird je
//  Zeile geprueft, `mehr` meldet den erreichten Deckel.
//  `ordnung` (s. `TableSort.h`) laesst den Lauf in ANZEIGEreihenfolge gehen;
//  ohne sie waere in einer sortierten Tabelle jeder Schritt zum naechsten
//  Treffer ein Sprung quer durch das Bild.
QList<Treffer> suche(const QList<Zeile>& zeilen, int von, int bis,
                     const QString& text, SuchOptionen o,
                     const QList<bool>* spalten = nullptr,
                     const std::atomic<bool>* abbruch = nullptr,
                     bool* mehr = nullptr,
                     const QList<int>* ordnung = nullptr,
                     const Werte* formeln = nullptr);

//  Der Zustand einer stehenden Suche: die Trefferliste, der laufende Treffer und
//  die Frage, die die Anzeige je sichtbarer Zelle stellt.
class Suchzustand {
public:
    void setzeTreffer(QList<Treffer> treffer, bool mehr);
    void leeren();

    int  anzahl() const   { return int(m_treffer.size()); }
    bool mehr() const     { return m_mehr; }
    int  index() const    { return m_index; }
    //  Steigt bei jeder Aenderung. Die Anzeige haengt ihre Zell-Bindungen daran:
    //  ohne einen gelesenen Wert wertet QML sie nie neu aus.
    int  revision() const { return m_revision; }
    Treffer aktuell() const;

    //  Auf den ersten Treffer ab dieser Anzeigezeile, sonst auf den ersten.
    void gehZuAb(int zeile);
    //  Einen Treffer weiter (+1) oder zurueck (-1), umlaufend.
    void schritt(int delta);

    bool trifft(int zeile, int spalte) const;
    //  Die Trefferspalten EINER Zeile - die Anzeige baut daraus je Zeile nur so
    //  viele Marken, wie sie braucht (meistens keine).
    QList<int> spaltenIn(int zeile) const;

private:
    QList<Treffer> m_treffer;
    int  m_index    = -1;
    bool m_mehr     = false;
    int  m_revision = 0;
};

//  Der Suchlauf im Arbeitsfaden - beide Tabellen-Controller starten ihn in
//  ihrem eigenen Ein-Faden-Pool. `anker` haelt die Datei am Leben, aus der
//  `zeilen` stammt; sie ist unveraenderlich, ein neues Lesen erzeugt eine neue.
//  Der Sucher zaehlt je Bereich, damit die Leiste nicht nur „woanders steht
//  noch etwas" sagen, sondern auch hinfuehren kann.
struct BlockBereich {
    int von = 0;
    int bis = 0;
};

class SuchTask : public QRunnable {
public:
    SuchTask(QObject* owner, std::shared_ptr<const void> anker,
             const QList<Zeile>* zeilen, int von, int bis,
             QString text, SuchOptionen opt, QList<bool> spalten,
             QList<BlockBereich> bloecke, std::shared_ptr<std::atomic<bool>> abbruch,
             std::function<void(QList<Treffer>, bool, QList<int>)> zurueck,
             QList<int> ordnung = {}, std::shared_ptr<const Werte> formeln = {});

    void run() override;

private:
    QObject* m_owner;
    std::shared_ptr<const void> m_anker;
    const QList<Zeile>* m_zeilen;
    int m_von;
    int m_bis;
    QString m_text;
    SuchOptionen m_opt;
    //  Leer = alle Spalten.
    QList<bool> m_spalten;
    //  Leer = nicht je Block zaehlen. Sonst eine Zahl je Bereich, den
    //  gezeigten eingeschlossen - was davon zu sehen ist, entscheidet der Aufrufer.
    QList<BlockBereich> m_bloecke;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    std::function<void(QList<Treffer>, bool, QList<int>)> m_zurueck;
    //  Leer = Dateireihenfolge. Die Zaehlung je Block laeuft immer in
    //  Dateireihenfolge - dort zaehlt nur, WIE VIELE es sind.
    QList<int> m_ordnung;
    //  Gesucht wird, was die Zelle ZEIGT - eine Formel ueber ihr Ergebnis.
    std::shared_ptr<const Werte> m_formeln;
};

}  // namespace mg::table
