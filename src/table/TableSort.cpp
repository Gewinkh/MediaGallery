#include "table/TableSort.h"

#include <QCollator>
#include <QDate>
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

bool spalteIstZahl(const QList<Zeile>& zeilen, int von, int bis, int spalte,
                   const Werte* formeln) {
    const int ende = qMin(bis, int(zeilen.size()));
    int gesehen = 0;
    for (int z = qMax(0, von); z < ende && gesehen < kProbeZeilen; ++z) {
        const QString w = gezeigterWert(zeilen, z, spalte, formeln);
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

bool datumsSpanne(QStringView text, bool monatZuerst, bool teil, int* von, int* bis) {
    const QStringView s = text.trimmed();
    //  Ohne Liste und ohne Kopie: der Filter fragt das je Zelle, bei 100.000
    //  Zeilen mal 20 Spalten zwei Millionen Mal.
    if (s.isEmpty() || s.size() > 10 || !s.front().isDigit()) return false;
    int wert[3] = {0, 0, 0};
    int stellen[3] = {0, 0, 0};
    int n = 1;
    QChar trenner;
    for (QChar c : s) {
        if (c.isDigit()) {
            if (++stellen[n - 1] > 4) return false;
            wert[n - 1] = wert[n - 1] * 10 + c.digitValue();
            continue;
        }
        if ((c != u'-' && c != u'.' && c != u'/') || (!trenner.isNull() && c != trenner)
            || n == 3 || stellen[n - 1] == 0)
            return false;
        trenner = c;
        ++n;
    }
    if (stellen[n - 1] == 0) return false;
    int j = 0, m = 0, t = 0;
    if (trenner.isNull() || trenner == u'-') {
        if (stellen[0] != 4 || (n >= 2 && stellen[1] > 2) || (n == 3 && stellen[2] > 2)) return false;
        j = wert[0];
        m = n >= 2 ? wert[1] : 0;
        t = n == 3 ? wert[2] : 0;
    } else {
        //  Jahr hinten: T.M.JJJJ bzw. die Schraegstrich-Form; zwei Teile sind M.JJJJ.
        if (n < 2 || stellen[n - 1] != 4 || stellen[0] > 2 || (n == 3 && stellen[1] > 2)) return false;
        j = wert[n - 1];
        if (n == 2)                             { m = wert[0]; }
        else if (trenner == u'/' && monatZuerst) { m = wert[0]; t = wert[1]; }
        else                                    { t = wert[0]; m = wert[1]; }
    }
    const bool mitTag = (n == 3);
    const bool mitMonat = (n >= 2);
    if (j < 1000 || (!mitTag && !teil)) return false;
    if (!mitMonat) {
        if (von) *von = j * 10000 + 101;
        if (bis) *bis = j * 10000 + 1231;
        return true;
    }
    if (m < 1 || m > 12) return false;
    if (!mitTag) {
        if (von) *von = j * 10000 + m * 100 + 1;
        if (bis) *bis = j * 10000 + m * 100 + QDate(j, m, 1).daysInMonth();
        return true;
    }
    if (!QDate(j, m, t).isValid()) return false;
    if (von) *von = j * 10000 + m * 100 + t;
    if (bis) *bis = j * 10000 + m * 100 + t;
    return true;
}

bool alsDatum(const QString& text, int* schluessel, bool monatZuerst) {
    return datumsSpanne(text, monatZuerst, false, schluessel, nullptr);
}

bool spalteIstDatum(const QList<Zeile>& zeilen, int von, int bis, int spalte, bool monatZuerst,
                    const Werte* formeln) {
    const int ende = qMin(bis, int(zeilen.size()));
    int gesehen = 0;
    for (int z = qMax(0, von); z < ende && gesehen < kProbeZeilen; ++z) {
        const QString w = gezeigterWert(zeilen, z, spalte, formeln);
        if (w.trimmed().isEmpty()) continue;
        ++gesehen;
        if (!alsDatum(w, nullptr, monatZuerst)) return false;
    }
    return gesehen > 0;
}

QList<int> sortiere(const QList<Zeile>& zeilen, int von, int bis, int spalte,
                    SortRichtung richtung, const std::atomic<bool>* abbruch,
                    const QList<int>* auswahl, bool monatZuerst,
                    const Werte* formeln) {
    if (richtung == SortRichtung::Keine || spalte < 0) return {};
    const int start = qMax(0, von);
    const int ende  = qMin(bis, int(zeilen.size()));
    if (ende <= start) return {};

    QList<int> ordnung;
    if (auswahl) {
        ordnung.reserve(auswahl->size());
        for (int z : *auswahl)
            if (z >= start && z < ende) ordnung.append(z);
    } else {
        ordnung.reserve(ende - start);
        for (int z = start; z < ende; ++z) ordnung.append(z);
    }

    const bool ab      = (richtung == SortRichtung::Ab);
    const bool zahlen  = spalteIstZahl(zeilen, start, ende, spalte, formeln);
    //  Ein Datum faellt durch die Zahlenpruefung (zwei Punkte); ohne eigene Art
    //  stuende der 02.01.2025 hinter dem 01.12.2025.
    const bool daten   = !zahlen && spalteIstDatum(zeilen, start, ende, spalte, monatZuerst, formeln);

    //  Natuerliche Ordnung: "Datei 10" gehoert hinter "Datei 9", nicht davor.
    QCollator koll;
    koll.setNumericMode(true);
    koll.setCaseSensitivity(Qt::CaseInsensitive);

    QList<double> zahl;
    //  Schluessel EINMAL gezogen und vorbereitet statt `compare` je Vergleich: rund
    //  1,7 Millionen Vergleiche je Lauf, gemessen an 100.000 Zeilen 195 -> 42 ms.
    QList<QCollatorSortKey> schluessel;
    const int m = int(ordnung.size());
    QList<bool> leer;
    leer.resize(m);
    const bool alsWert = zahlen || daten;
    if (alsWert) zahl.resize(m);
    else         schluessel.reserve(m);

    //  Schluessel je POSITION in `ordnung` - mit Filter nur ein Teil des Bereichs.
    for (int i = 0; i < m; ++i) {
        const QString w = gezeigterWert(zeilen, ordnung.at(i), spalte, formeln).trimmed();
        leer[i] = w.isEmpty();
        if (zahlen) {
            bool ok = false;
            zahl[i] = alsZahl(w, &ok);
        } else if (daten) {
            int k = 0;
            alsDatum(w, &k, monatZuerst);
            zahl[i] = k;
        } else {
            schluessel.append(koll.sortKey(w));
        }
    }
    if (abbruch && abbruch->load()) return {};

    QList<int> pos(m);
    for (int i = 0; i < m; ++i) pos[i] = i;
    std::stable_sort(pos.begin(), pos.end(), [&](int ia, int ib) {
        //  Leere Zellen IMMER ans Ende, in beiden Richtungen: eine Luecke ist
        //  kein kleiner Wert, und sie oben zu haben verdeckt genau das, wonach
        //  man sortiert hat.
        if (leer[ia] != leer[ib]) return !leer[ia];
        if (leer[ia]) return false;
        if (alsWert) {
            if (zahl[ia] == zahl[ib]) return false;
            return ab ? (zahl[ia] > zahl[ib]) : (zahl[ia] < zahl[ib]);
        }
        //  `QCollatorSortKey` kennt nur `<` - Gleichheit ist "keiner kleiner".
        if (schluessel[ia] < schluessel[ib]) return !ab;
        if (schluessel[ib] < schluessel[ia]) return ab;
        return false;
    });

    if (abbruch && abbruch->load()) return {};
    QList<int> aus(m);
    for (int i = 0; i < m; ++i) aus[i] = ordnung.at(pos.at(i));
    return aus;
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
