#include "table/TableSearch.h"

#include <QHash>

#include <QStringMatcher>

#include <algorithm>
#include <utility>

namespace mg::table {
namespace {

bool vorher(const Treffer& a, const Treffer& b) {
    return a.zeile != b.zeile ? a.zeile < b.zeile : a.spalte < b.spalte;
}

}  // namespace

namespace {

//  Nur die Zeichen, deren Faltungsklasse Gross- und Kleinform NICHT abdecken;
//  fuer alle uebrigen genuegen `toLower`/`toUpper`. Ueber die BMP sind es 118,
//  darunter `s`/`S`/langes s und `k`/`K`/Kelvin - kaselose Schriften wie
//  Arabisch, Kana und CJK gar keines. Nicht nur grosse Klassen sind betroffen:
//  bei griechischen Formen mit untergesetztem Iota hat die Klasse zwei
//  Mitglieder, und `toUpper` fuehrt trotzdem nicht auf das andere.
//  Abgeleitet aus Qts eigenem `toCaseFolded`, einmal beim ersten Bedarf - eine
//  Liste im Quelltext altert gegen die naechste Unicode-Fassung.
const QHash<char16_t, QList<char16_t>>& sonderKlassen() {
    static const QHash<char16_t, QList<char16_t>> tafel = [] {
        constexpr int kBmp = 0x10000;
        QHash<char16_t, QList<char16_t>> nachSchluessel;
        for (char32_t c = 1; c < kBmp; ++c) {
            if (QChar::isSurrogate(c)) continue;
            const QChar q{char16_t(c)};
            nachSchluessel[q.toCaseFolded().unicode()].append(char16_t(c));
        }
        QHash<char16_t, QList<char16_t>> aus;
        for (auto it = nachSchluessel.cbegin(); it != nachSchluessel.cend(); ++it) {
            const QList<char16_t>& klasse = it.value();
            for (char16_t m : klasse) {
                const QChar q{m};
                const char16_t kl = q.toLower().unicode();
                const char16_t gr = q.toUpper().unicode();
                bool gedeckt = true;
                for (char16_t a : klasse) gedeckt = gedeckt && (a == kl || a == gr);
                if (!gedeckt) aus.insert(m, klasse);
            }
        }
        return aus;
    }();
    return tafel;
}

}  // namespace

int vorfilterAnker(QChar c, char16_t* aus) {
    //  Ausserhalb der BMP wird nicht geraten: ein halbes Ersatzzeichen ist kein
    //  Anker, und Faltungspaare gibt es dort praktisch nicht.
    if (c.isSurrogate() || c.isNull()) return 0;
    const auto it = sonderKlassen().constFind(c.unicode());
    if (it == sonderKlassen().cend()) {
        aus[0] = c.toLower().unicode();
        aus[1] = c.toUpper().unicode();
        return 2;
    }
    //  Mehr als drei laesst die Primitive nicht zu - dann lieber gar kein
    //  Vorfilter als ein uebersehener Treffer.
    if (it.value().size() > 3) return 0;
    for (int i = 0; i < it.value().size(); ++i) aus[i] = it.value().at(i);
    return int(it.value().size());
}

ZellVergleich::ZellVergleich(const QString& text, SuchOptionen o)
    : m_text(text), m_laenge(text.size()), m_gross(o.gross ? Qt::CaseSensitive : Qt::CaseInsensitive),
      m_sucher(text, m_gross), m_ganzeZelle(o.ganzeZelle) {
    //  Der Vorfilter braucht einen Anker, an dem der Begriff anfangen MUSS.
    //  Bei „ganze Zelle" gibt es keinen Suchlauf, bei einem Zeichen ist Qts
    //  eigener Weg schon schneller.
    if (m_ganzeZelle || m_laenge < 2 || text.isEmpty()) return;
    const QChar e = text.at(0);
    if (o.gross) {
        //  Gross-/Kleinschreibung zaehlt: nur genau dieses Zeichen, keine
        //  Faltung, also auch keine Frage nach Faltungsklassen.
        m_ersteKl = m_ersteGr = m_ersteDr = e.unicode();
        m_anker = 2;
        m_vorfilter = true;
        return;
    }
    char16_t anker[3] = { 0, 0, 0 };
    m_anker = vorfilterAnker(e, anker);
    if (m_anker == 0) return;
    m_ersteKl = anker[0];
    m_ersteGr = anker[1];
    m_ersteDr = anker[2];
    m_vorfilter = true;
}

QList<Treffer> suche(const QList<Zeile>& zeilen, int von, int bis,
                     const QString& text, SuchOptionen o,
                     const QList<bool>* spalten,
                     const std::atomic<bool>* abbruch,
                     bool* mehr,
                     const QList<int>* ordnung,
                     const Werte* formeln) {
    QList<Treffer> out;
    if (mehr) *mehr = false;
    if (text.isEmpty()) return out;
    //  Ohne eine einzige Formel faellt die ganze Zusatzfrage weg - das ist der
    //  Normalfall, und je Zelle bliebe sonst eine Suche im Streuspeicher stehen.
    const Werte* erg = (formeln && !formeln->leer()) ? formeln : nullptr;

    const ZellVergleich vergleich(text, o);
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
        const Werte* zerg = (erg && erg->hatZeile(z)) ? erg : nullptr;
        for (const std::pair<quint16, QString>& feld : zeilen.at(z).belegte()) {
            const int spalte = int(feld.first);
            if (spalten && (spalte >= spalten->size() || !spalten->at(spalte))) continue;
            const QString* gezeigt = zerg ? zerg->wert(z, spalte) : nullptr;
            if (!vergleich.trifft(gezeigt ? *gezeigt : feld.second)) continue;
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
                   QList<int> ordnung, std::shared_ptr<const Werte> formeln)
    : m_owner(owner), m_anker(std::move(anker)), m_zeilen(zeilen), m_von(von), m_bis(bis),
      m_text(std::move(text)), m_opt(opt), m_spalten(std::move(spalten)),
      m_bloecke(std::move(bloecke)), m_abbruch(std::move(abbruch)),
      m_zurueck(std::move(zurueck)), m_ordnung(std::move(ordnung)),
      m_formeln(std::move(formeln)) {
    setAutoDelete(true);
}

void SuchTask::run() {
    const QList<bool>* maske = m_spalten.isEmpty() ? nullptr : &m_spalten;
    bool mehr = false;
    const QList<int>* ordnung = m_ordnung.isEmpty() ? nullptr : &m_ordnung;
    QList<Treffer> treffer = suche(*m_zeilen, m_von, m_bis, m_text, m_opt,
                                   maske, m_abbruch.get(), &mehr, ordnung, m_formeln.get());
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
                                      maske, m_abbruch.get(), &egal, nullptr,
                                      m_formeln.get()).size()));
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
