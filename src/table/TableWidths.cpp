#include "table/TableWidths.h"

#include "core/EditSidecar.h"
#include "core/MGEditBin.h"

#include <QJsonObject>

namespace mg::table {
namespace {

const QLatin1String kAbschnitt("spaltenbreiten");

//  Eine Spalte zaehlt nur, wenn Nummer und Breite im erlaubten Bereich liegen -
//  die Beidatei ist eine fremde Datei, auch wenn die App sie geschrieben hat.
void nimmAuf(QHash<int, int>& aus, const QString& schluessel, int px) {
    bool ok = false;
    const int spalte = schluessel.toInt(&ok);
    if (ok && spalte >= 0 && px >= kMinBreite && px <= kMaxBreite)
        aus.insert(spalte, px);
}

}  // namespace

QHash<int, int> liesBreiten(const QString& datei) {
    QHash<int, int> aus;
    const mg::editsidecar::Inhalt in = mg::editsidecar::liesInhalt(datei);
    if (in.istBin) {
        const mg::mgeb::Objekt o = in.bin.wurzel().value(mg::mgeb::k_spaltenbreiten).toObject();
        for (int i = 0; i < o.count(); ++i)
            nimmAuf(aus, o.schluesselBei(i), o.wertBei(i).toInt());
        return aus;
    }
    const QJsonObject o = in.json.value(kAbschnitt).toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        nimmAuf(aus, it.key(), it.value().toInt());
    return aus;
}

bool schreibeBreiten(const QString& datei, const QHash<int, int>& breiten) {
    const mg::editsidecar::Inhalt in = mg::editsidecar::liesInhalt(datei, false);

    mg::mgeb::Schreiber s;
    s.beginneObjekt();
    const int fremde = mg::editsidecar::uebernimmFremde(s, in, kAbschnitt);
    if (breiten.isEmpty()) {
        s.beendeObjekt();
        //  Nichts mehr drin: dann soll auch keine Beidatei liegenbleiben.
        if (fremde == 0) return mg::editsidecar::entferne(datei);
        return mg::editsidecar::schreibeBin(datei, s.fertig());
    }
    s.schluessel(mg::mgeb::k_spaltenbreiten);
    s.beginneObjekt();
    for (auto it = breiten.constBegin(); it != breiten.constEnd(); ++it)
        s.feld(QString::number(it.key()), it.value());
    s.beendeObjekt();
    s.beendeObjekt();
    return mg::editsidecar::schreibeBin(datei, s.fertig());
}

}  // namespace mg::table
