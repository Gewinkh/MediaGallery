#pragma once
//  TableFormula - Formeln in Tabellenzellen (=A1+B2). Gespeichert wird der
//  Formeltext, gezeigt sein Ergebnis; die Datei bleibt ihre eigene Wahrheit.
#include "table/DelimitedText.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <memory>

namespace mg::table {

//  Ein einzelnes '=' ist keine Formel - eine Zelle darf es tragen.
bool istFormel(const QString& text);

//  0 = A, 25 = Z, 26 = AA.
QString spaltenName(int spalte);

//  Die Spalte bleibt unter kMaxFelderZeile, also passt sie in 20 Bit.
inline qint64 zellSchluessel(int zeile, int spalte) {
    return (qint64(zeile) << 20) | qint64(spalte & 0xFFFFF);
}

//  Die Formelzellen eines Bereichs, aufsteigend. Voller Durchlauf ueber die
//  belegten Felder: 8,2 ms an 100.000 Zeilen mal 20 Spalten.
QList<qint64> sammleFormeln(const QList<Zeile>& zeilen, int von, int bis);

//  Die Ergebnisse eines Bereichs. Nur Formelzellen stehen darin; jede andere
//  Zelle kommt weiter aus ihrer Zeile.
class Werte {
public:
    bool leer() const { return m_werte.isEmpty(); }
    //  Vorpruefung je ZEILE: gefragt wird je Zelle, und fast keine Zeile traegt
    //  eine Formel.
    bool hatZeile(int zeile) const { return m_zeilen.contains(zeile); }
    const QString* wert(int zeile, int spalte) const {
        auto it = m_werte.constFind(zellSchluessel(zeile, spalte));
        return it == m_werte.cend() ? nullptr : &it.value();
    }
    void setze(int zeile, int spalte, QString text) {
        m_werte.insert(zellSchluessel(zeile, spalte), std::move(text));
        m_zeilen.insert(zeile);
    }

private:
    QHash<qint64, QString> m_werte;
    QSet<int> m_zeilen;
};

//  Der Wert, den eine Zelle ZEIGT. Suche, Filter und Sortierung fragen darueber,
//  damit sie dasselbe sehen wie der Leser; ohne Formeln ein Zeigervergleich.
inline QString gezeigterWert(const QList<Zeile>& zeilen, int zeile, int spalte,
                             const Werte* formeln) {
    if (formeln && formeln->hatZeile(zeile))
        if (const QString* w = formeln->wert(zeile, spalte)) return *w;
    return zeilen.at(zeile).wert(spalte);
}

//  Ein Block und seine Formelzellen. `daten` ist seine erste Datenzeile und
//  damit Zeile 1 im Bezug A1 - unabhaengig von der gezeigten Ansicht; ein Bezug
//  ueber die Blockgrenze ergibt #REF!.
struct FormelBereich {
    int daten = 0;
    int bis   = 0;
    QList<qint64> zellen;
};

//  `komma` waehlt das Dezimalzeichen der AUSGABE.
std::shared_ptr<const Werte> rechne(const QList<Zeile>& zeilen,
                                    const QList<FormelBereich>& bereiche, bool komma);

//  Entschieden an Zellen mit BEIDEN Zeichen (1.234,56) - dort ist die Lesart
//  eindeutig; ohne eine solche Zelle entscheidet das Trennzeichen.
bool dezimalKomma(const QList<Zeile>& zeilen, int von, int bis, QChar trenner);

}  // namespace mg::table
