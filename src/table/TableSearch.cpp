#include "table/TableSearch.h"

#include <QStringMatcher>

#include <algorithm>
#include <utility>

namespace mg::table {
namespace {

bool vorher(const Treffer& a, const Treffer& b) {
    return a.zeile != b.zeile ? a.zeile < b.zeile : a.spalte < b.spalte;
}

}  // namespace

QList<Treffer> suche(const QList<Zeile>& zeilen, int von, int bis,
                     const QString& text, SuchOptionen o,
                     const QList<bool>* spalten,
                     const std::atomic<bool>* abbruch,
                     bool* mehr,
                     const QList<int>* ordnung) {
    QList<Treffer> out;
    if (mehr) *mehr = false;
    if (text.isEmpty()) return out;

    const Qt::CaseSensitivity gross = o.gross ? Qt::CaseSensitive : Qt::CaseInsensitive;
    //  EIN vorbereiteter Sucher statt `QString::contains` je Zelle: der baut
    //  seine Sprungtabelle einmal, `contains` faltet die Schreibweise bei jedem
    //  Aufruf neu.
    const QStringMatcher sucher(text, gross);
    const int nadel = int(text.size());
    //  Bei EINEM Zeichen ist Qts eigener Weg schneller als eine Sprungtabelle,
    //  die nie springt (gemessen 5 gegen 13 ms ueber 100.000 Zeilen).
    const bool einZeichen = (nadel == 1);
    const int ende = qMin(bis, int(zeilen.size()));
    //  Mit Reihenfolge laeuft die Schleife ueber die ANZEIGE, ohne ueber die
    //  Datei. `anzeige` ist in beiden Faellen die Zeile, die gemeldet wird.
    const int anzahl = ordnung ? int(ordnung->size()) : qMax(0, ende - qMax(0, von));
    for (int anzeige = 0; anzeige < anzahl; ++anzeige) {
        const int z = ordnung ? ordnung->at(anzeige) : (qMax(0, von) + anzeige);
        if (z < 0 || z >= ende) continue;
        //  Je Zeile, nicht je Zelle: der Abbruch soll schnell greifen, aber die
        //  atomare Last nicht die Suche selbst bestimmen.
        if (abbruch && abbruch->load()) return {};
        //  Ueber die BELEGTEN Felder, nicht ueber `wert()` je Spalte - bei 125
        //  Spalten waere das je Zeile ein Vielfaches an binaeren Suchen.
        for (const std::pair<quint16, QString>& feld : zeilen.at(z).belegte()) {
            const int spalte = int(feld.first);
            if (spalten && (spalte >= spalten->size() || !spalten->at(spalte))) continue;
            //  Kuerzer als der Begriff kann nie treffen - der Ausschluss ueber
            //  die Laenge ist ein Vergleich statt eines Suchlaufs.
            if (int(feld.second.size()) < nadel) continue;
            const bool treffer =
                o.ganzeZelle ? (feld.second.compare(text, gross) == 0)
              : einZeichen   ? feld.second.contains(text, gross)
                             : (sucher.indexIn(feld.second) >= 0);
            if (!treffer) continue;
            out.append(Treffer{ anzeige, spalte });
            if (out.size() >= kMaxTreffer) {
                if (mehr) *mehr = true;
                return out;
            }
        }
    }
    return out;
}

SuchTask::SuchTask(QObject* owner, std::shared_ptr<const void> anker,
                   const QList<Zeile>* zeilen, int von, int bis,
                   QString text, SuchOptionen opt, QList<bool> spalten,
                   QList<BlockBereich> bloecke, std::shared_ptr<std::atomic<bool>> abbruch,
                   std::function<void(QList<Treffer>, bool, QList<int>)> zurueck,
                   QList<int> ordnung)
    : m_owner(owner), m_anker(std::move(anker)), m_zeilen(zeilen), m_von(von), m_bis(bis),
      m_text(std::move(text)), m_opt(opt), m_spalten(std::move(spalten)),
      m_bloecke(std::move(bloecke)), m_abbruch(std::move(abbruch)),
      m_zurueck(std::move(zurueck)), m_ordnung(std::move(ordnung)) {
    setAutoDelete(true);
}

void SuchTask::run() {
    const QList<bool>* maske = m_spalten.isEmpty() ? nullptr : &m_spalten;
    bool mehr = false;
    const QList<int>* ordnung = m_ordnung.isEmpty() ? nullptr : &m_ordnung;
    QList<Treffer> treffer = suche(*m_zeilen, m_von, m_bis, m_text, m_opt,
                                   maske, m_abbruch.get(), &mehr, ordnung);
    //  Der gezeigte Block wird mitgezaehlt - eine Zeilenspanne mehr, dafuer
    //  ist die Liste vollstaendig.
    QList<int> proBlock;
    if (!m_bloecke.isEmpty() && !m_abbruch->load()) {
        proBlock.reserve(m_bloecke.size());
        for (const BlockBereich& b : m_bloecke) {
            if (m_abbruch->load()) return;
            bool egal = false;
            //  Ohne Reihenfolge: hier zaehlt nur, WIE VIELE Treffer der Block
            //  hat - die Reihenfolge einer fremden Tabelle sieht niemand.
            proBlock.append(int(suche(*m_zeilen, b.von, b.bis, m_text, m_opt,
                                      maske, m_abbruch.get(), &egal).size()));
        }
    }
    if (m_abbruch->load()) return;
    auto zurueck = m_zurueck;
    auto flag = m_abbruch;
    QMetaObject::invokeMethod(m_owner, [zurueck, treffer, mehr, proBlock, flag] {
        //  Eine neuere Suche hat diese ueberholt, waehrend das Ergebnis in der
        //  Warteschlange lag.
        if (flag->load()) return;
        zurueck(treffer, mehr, proBlock);
    }, Qt::QueuedConnection);
}

void Suchzustand::setzeTreffer(QList<Treffer> treffer, bool mehr) {
    m_treffer = std::move(treffer);
    m_mehr    = mehr;
    m_index   = m_treffer.isEmpty() ? -1 : 0;
    ++m_revision;
}

void Suchzustand::leeren() {
    m_treffer.clear();
    m_mehr  = false;
    m_index = -1;
    ++m_revision;
}

Treffer Suchzustand::aktuell() const {
    if (m_index < 0 || m_index >= m_treffer.size()) return {};
    return m_treffer.at(m_index);
}

void Suchzustand::gehZuAb(int zeile) {
    if (m_treffer.isEmpty()) { m_index = -1; return; }
    const auto it = std::lower_bound(m_treffer.cbegin(), m_treffer.cend(),
                                     Treffer{ zeile, 0 }, vorher);
    m_index = it == m_treffer.cend() ? 0 : int(it - m_treffer.cbegin());
    ++m_revision;
}

void Suchzustand::schritt(int delta) {
    if (m_treffer.isEmpty()) { m_index = -1; return; }
    const int n = int(m_treffer.size());
    m_index = ((m_index + delta) % n + n) % n;
    ++m_revision;
}

QList<int> Suchzustand::spaltenIn(int zeile) const {
    QList<int> out;
    if (m_treffer.isEmpty()) return out;
    auto it = std::lower_bound(m_treffer.cbegin(), m_treffer.cend(),
                               Treffer{ zeile, 0 }, vorher);
    for (; it != m_treffer.cend() && it->zeile == zeile; ++it) out.append(it->spalte);
    return out;
}

bool Suchzustand::trifft(int zeile, int spalte) const {
    if (m_treffer.isEmpty()) return false;
    const auto it = std::lower_bound(m_treffer.cbegin(), m_treffer.cend(),
                                     Treffer{ zeile, spalte }, vorher);
    return it != m_treffer.cend() && it->zeile == zeile && it->spalte == spalte;
}

}  // namespace mg::table
