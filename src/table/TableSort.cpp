#include "table/TableSort.h"

#include <QCollator>
#include <QMetaObject>
#include <algorithm>

namespace mg::table {
namespace {

//  So viele Zellen entscheiden, ob eine Spalte als Zahl gilt.
constexpr int kProbeZeilen = 200;

}  // namespace

double alsZahl(const QString& text, bool* ok) {
    if (ok) *ok = false;
    QString s = text.trimmed();
    if (s.isEmpty()) return 0.0;
    const int komma = int(s.lastIndexOf(QLatin1Char(',')));
    const int punkt = int(s.lastIndexOf(QLatin1Char('.')));
    //  Kommen BEIDE vor, ist das hintere das Dezimalzeichen und das vordere der
    //  Tausendertrenner - das entscheidet 1.234,56 und 1,234.56 richtig. Kommt
    //  nur eines vor, gilt es als Dezimalzeichen; "1.234" ist damit 1,234 und
    //  nicht Tausendzweihundertvierunddreissig. Diese eine Stelle bleibt
    //  mehrdeutig, und zwar in jeder Leserichtung.
    if (komma >= 0 && punkt >= 0) {
        if (komma > punkt) { s.remove(QLatin1Char('.')); s.replace(QLatin1Char(','), QLatin1Char('.')); }
        else               { s.remove(QLatin1Char(',')); }
    } else if (komma >= 0) {
        s.replace(QLatin1Char(','), QLatin1Char('.'));
    }
    bool gut = false;
    const double w = s.toDouble(&gut);
    if (ok) *ok = gut;
    return gut ? w : 0.0;
}

bool spalteIstZahl(const QList<Zeile>& zeilen, int von, int bis, int spalte) {
    const int ende = qMin(bis, int(zeilen.size()));
    int gesehen = 0;
    for (int z = qMax(0, von); z < ende && gesehen < kProbeZeilen; ++z) {
        const QString w = zeilen.at(z).wert(spalte);
        if (w.trimmed().isEmpty()) continue;
        ++gesehen;
        bool ok = false;
        alsZahl(w, &ok);
        if (!ok) return false;
    }
    //  Eine Spalte, in der nur Leerzellen standen, ist keine Zahlenspalte -
    //  sonst entschiede die Probe ueber eine Frage, die sie nie gesehen hat.
    return gesehen > 0;
}

QList<int> sortiere(const QList<Zeile>& zeilen, int von, int bis, int spalte,
                    SortRichtung richtung, const std::atomic<bool>* abbruch) {
    if (richtung == SortRichtung::Keine || spalte < 0) return {};
    const int start = qMax(0, von);
    const int ende  = qMin(bis, int(zeilen.size()));
    if (ende <= start) return {};

    QList<int> ordnung;
    ordnung.reserve(ende - start);
    for (int z = start; z < ende; ++z) ordnung.append(z);

    const bool ab      = (richtung == SortRichtung::Ab);
    const bool zahlen  = spalteIstZahl(zeilen, start, ende, spalte);

    //  Die Schluessel werden EINMAL gezogen, nicht bei jedem Vergleich: ein
    //  Sortierlauf ueber 100.000 Zeilen stellt rund 1,7 Millionen Vergleiche an,
    //  und `wert()` sucht je Aufruf binaer in den belegten Feldern.
    //  Natuerliche Ordnung: "Datei 10" gehoert hinter "Datei 9", nicht davor.
    QCollator koll;
    koll.setNumericMode(true);
    koll.setCaseSensitivity(Qt::CaseInsensitive);

    QList<double> zahl;
    //  Der VORBEREITETE Sortierschluessel statt `QCollator::compare` je
    //  Vergleich: gemessen an 100.000 Zeilen 195 -> 42 ms. Ein Sortierlauf
    //  stellt rund 1,7 Millionen Vergleiche an, und jeder faltete den Text
    //  sonst neu.
    QList<QCollatorSortKey> schluessel;
    QList<bool> leer;
    leer.resize(ende - start);
    if (zahlen) zahl.resize(ende - start);
    else        schluessel.reserve(ende - start);

    for (int i = 0; i < ende - start; ++i) {
        const QString w = zeilen.at(start + i).wert(spalte).trimmed();
        leer[i] = w.isEmpty();
        if (zahlen) {
            bool ok = false;
            zahl[i] = alsZahl(w, &ok);
        } else {
            schluessel.append(koll.sortKey(w));
        }
    }
    if (abbruch && abbruch->load()) return {};

    std::stable_sort(ordnung.begin(), ordnung.end(),
                     [&](int a, int b) {
        const int ia = a - start, ib = b - start;
        //  Leere Zellen IMMER ans Ende, in beiden Richtungen: eine Luecke ist
        //  kein kleiner Wert, und sie oben zu haben verdeckt genau das, wonach
        //  man sortiert hat.
        if (leer[ia] != leer[ib]) return !leer[ia];
        if (leer[ia]) return false;
        if (zahlen) {
            if (zahl[ia] == zahl[ib]) return false;
            return ab ? (zahl[ia] > zahl[ib]) : (zahl[ia] < zahl[ib]);
        }
        //  `QCollatorSortKey` kennt nur `<` - Gleichheit ist "keiner kleiner".
        if (schluessel[ia] < schluessel[ib]) return !ab;
        if (schluessel[ib] < schluessel[ia]) return ab;
        return false;
    });

    if (abbruch && abbruch->load()) return {};
    return ordnung;
}

SortTask::SortTask(QObject* owner, std::shared_ptr<const void> anker,
                   const QList<Zeile>* zeilen, int von, int bis, int spalte,
                   SortRichtung richtung, std::shared_ptr<std::atomic<bool>> abbruch,
                   std::function<void(QList<int>)> zurueck)
    : m_owner(owner), m_anker(std::move(anker)), m_zeilen(zeilen), m_von(von),
      m_bis(bis), m_spalte(spalte), m_richtung(richtung),
      m_abbruch(std::move(abbruch)), m_zurueck(std::move(zurueck)) {
    setAutoDelete(true);
}

void SortTask::run() {
    QList<int> ordnung = sortiere(*m_zeilen, m_von, m_bis, m_spalte, m_richtung,
                                  m_abbruch.get());
    if (m_abbruch->load()) return;
    auto zurueck = m_zurueck;
    QMetaObject::invokeMethod(m_owner,
                              [zurueck, ordnung = std::move(ordnung)]() mutable {
                                  zurueck(std::move(ordnung));
                              },
                              Qt::QueuedConnection);
}

}  // namespace mg::table
