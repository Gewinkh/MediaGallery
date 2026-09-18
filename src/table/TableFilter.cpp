#include "table/TableFilter.h"

#include <QMetaObject>

namespace mg::table {

bool filterZahl(QStringView text, double* wert) {
    QString rein;
    rein.reserve(text.size());
    for (QChar c : text) {
        if (c.isSpace() || c == u'€' || c == u'$' || c == u'£' || c == u'¥' || c == u'%') continue;
        rein.append(c);
    }
    bool ok = false;
    const double w = alsZahl(rein, &ok);
    if (ok && wert) *wert = w;
    return ok;
}

FilterAusdruck deuteFilter(QStringView text, bool monatZuerst) {
    FilterAusdruck a;
    const QStringView t = text.trimmed();
    if (t.size() < 2) return a;
    int von = 0, bis = 0;

    const qsizetype pfeil = t.indexOf(u"->");
    if (pfeil > 0) {
        const QStringView links = t.left(pfeil).trimmed();
        const QStringView rechts = t.mid(pfeil + 2).trimmed();
        int von2 = 0, bis2 = 0;
        if (datumsSpanne(links, monatZuerst, true, &von, &bis)
            && datumsSpanne(rechts, monatZuerst, true, &von2, &bis2)) {
            a.art = FilterAusdruck::Art::Datum;
            a.datumVon = qMin(von, von2);
            a.datumBis = qMax(bis, bis2);
            return a;
        }
        double z1 = 0.0, z2 = 0.0;
        if (filterZahl(links, &z1) && filterZahl(rechts, &z2)) {
            a.art = FilterAusdruck::Art::Zahl;
            a.zahlVon = qMin(z1, z2);
            a.zahlBis = qMax(z1, z2);
            a.hatVon = a.hatBis = true;
        }
        return a;
    }

    const QChar erstes = t.front();
    if (erstes == u'>' || erstes == u'<') {
        const bool gleich = t.size() > 1 && t.at(1) == u'=';
        const QStringView rest = t.mid(gleich ? 2 : 1).trimmed();
        const bool groesser = (erstes == u'>');
        if (datumsSpanne(rest, monatZuerst, true, &von, &bis)) {
            a.art = FilterAusdruck::Art::Datum;
            //  Mit Jahr oder Monat allein: >2025 heisst nach dem ganzen Jahr.
            if (groesser) a.datumVon = gleich ? von : bis + 1;
            else          a.datumBis = gleich ? bis : von - 1;
            return a;
        }
        double z = 0.0;
        if (filterZahl(rest, &z)) {
            a.art = FilterAusdruck::Art::Zahl;
            if (groesser) { a.zahlVon = z; a.hatVon = true; a.vonOffen = !gleich; }
            else          { a.zahlBis = z; a.hatBis = true; a.bisOffen = !gleich; }
        }
        return a;
    }

    const QChar letztes = t.back();
    if ((letztes == u'+' || letztes == u'-')
        && datumsSpanne(t.chopped(1), monatZuerst, true, &von, &bis)) {
        a.art = FilterAusdruck::Art::Datum;
        if (letztes == u'+') a.datumVon = von;
        else                 a.datumBis = bis;
    }
    return a;
}

bool trifftAusdruck(const FilterAusdruck& a, const QString& zelle, bool monatZuerst) {
    if (a.art == FilterAusdruck::Art::Datum) {
        int k = 0;
        return alsDatum(zelle, &k, monatZuerst) && k >= a.datumVon && k <= a.datumBis;
    }
    double z = 0.0;
    if (!filterZahl(zelle, &z)) return false;
    if (a.hatVon && (a.vonOffen ? z <= a.zahlVon : z < a.zahlVon)) return false;
    if (a.hatBis && (a.bisOffen ? z >= a.zahlBis : z > a.zahlBis)) return false;
    return true;
}

QList<int> filtere(const QList<Zeile>& zeilen, int von, int bis, const FilterRegel& regel,
                   const QList<bool>* spalten, const std::atomic<bool>* abbruch,
                   const Werte* formeln) {
    QList<int> aus;
    const int start = qMax(0, von);
    const int ende  = qMin(bis, int(zeilen.size()));
    if (!regel.aktiv() || ende <= start) return aus;

    const Werte* erg = (formeln && !formeln->leer()) ? formeln : nullptr;
    const FilterAusdruck ausdruck = deuteFilter(regel.text, regel.monatZuerst);
    const bool text = ausdruck.art == FilterAusdruck::Art::Text;
    const ZellVergleich vergleich(regel.text, regel.opt);
    auto trifft = [&](const QString& zelle) {
        return text ? vergleich.trifft(zelle) : trifftAusdruck(ausdruck, zelle, regel.monatZuerst);
    };
    for (int z = start; z < ende; ++z) {
        if (abbruch && abbruch->load()) return {};
        const Zeile& zeile = zeilen.at(z);
        const Werte* zerg = (erg && erg->hatZeile(z)) ? erg : nullptr;
        if (regel.spalte >= 0) {
            if (trifft(gezeigterWert(zeilen, z, regel.spalte, zerg))) aus.append(z);
            continue;
        }
        for (const auto& feld : zeile.belegte()) {
            const int s = int(feld.first);
            if (spalten && (s >= spalten->size() || !spalten->at(s))) continue;
            const QString* gezeigt = zerg ? zerg->wert(z, s) : nullptr;
            if (trifft(gezeigt ? *gezeigt : feld.second)) { aus.append(z); break; }
        }
    }
    return aus;
}

QList<int> ordne(const QList<Zeile>& zeilen, const Ordnungsauftrag& a,
                 const std::atomic<bool>* abbruch, bool* aktiv,
                 const Werte* formeln) {
    const bool gefiltert = a.filter.aktiv();
    const bool sortiert  = a.richtung != SortRichtung::Keine && a.sortSpalte >= 0;
    if (aktiv) *aktiv = gefiltert || sortiert;
    if (!gefiltert && !sortiert) return {};

    const QList<bool>* maske = a.spalten.isEmpty() ? nullptr : &a.spalten;
    QList<int> auswahl;
    if (gefiltert) auswahl = filtere(zeilen, a.von, a.bis, a.filter, maske, abbruch, formeln);
    if (abbruch && abbruch->load()) return {};
    if (!sortiert) return auswahl;
    //  Ein Filter ohne Treffer bleibt ohne Treffer - `sortiere` laese eine leere
    //  Auswahl sonst als „alles".
    if (gefiltert && auswahl.isEmpty()) return auswahl;
    return sortiere(zeilen, a.von, a.bis, a.sortSpalte, a.richtung, abbruch,
                    gefiltert ? &auswahl : nullptr, a.monatZuerst, formeln);
}

OrdnungTask::OrdnungTask(QObject* owner, std::shared_ptr<const void> anker,
                         const QList<Zeile>* zeilen, Ordnungsauftrag auftrag,
                         std::shared_ptr<std::atomic<bool>> abbruch, Zurueck zurueck,
                         Zusatz zusatz, std::shared_ptr<const Werte> formeln)
    : m_owner(owner), m_anker(std::move(anker)), m_zeilen(zeilen),
      m_auftrag(std::move(auftrag)), m_abbruch(std::move(abbruch)),
      m_zurueck(std::move(zurueck)), m_zusatz(std::move(zusatz)),
      m_formeln(std::move(formeln)) {
    setAutoDelete(true);
}

void OrdnungTask::run() {
    bool aktiv = false;
    QList<int> ordnung = ordne(*m_zeilen, m_auftrag, m_abbruch.get(), &aktiv, m_formeln.get());
    if (m_abbruch->load()) return;
    QVariant extra;
    //  Die Summen gelten fuer die GEZEIGTEN Zeilen; ohne Filter fuer alle.
    if (m_zusatz)
        extra = m_zusatz(*m_zeilen, m_auftrag.filter.aktiv() ? &ordnung : nullptr);
    if (m_abbruch->load()) return;
    auto zurueck = m_zurueck;
    auto flag = m_abbruch;
    QMetaObject::invokeMethod(m_owner,
                              [zurueck, flag, aktiv, extra, ordnung = std::move(ordnung)]() mutable {
                                  if (flag->load()) return;
                                  zurueck(std::move(ordnung), aktiv, extra);
                              },
                              Qt::QueuedConnection);
}

}  // namespace mg::table
