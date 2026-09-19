#include "table/TableWidths.h"

#include "core/EditSidecar.h"

#include <QJsonObject>

namespace mg::table {
namespace {

const QLatin1String kAbschnitt("spaltenbreiten");

}  // namespace

QHash<int, int> liesBreiten(const QString& datei) {
    QHash<int, int> aus;
    const QJsonObject wurzel = mg::editsidecar::lies(datei);
    const QJsonObject o = wurzel.value(kAbschnitt).toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        bool ok = false;
        const int spalte = it.key().toInt(&ok);
        const int px = it.value().toInt();
        if (ok && spalte >= 0 && px >= kMinBreite && px <= kMaxBreite)
            aus.insert(spalte, px);
    }
    return aus;
}

bool schreibeBreiten(const QString& datei, const QHash<int, int>& breiten) {
    QJsonObject wurzel = mg::editsidecar::lies(datei);
    if (breiten.isEmpty()) {
        if (!wurzel.contains(kAbschnitt)) return true;
        wurzel.remove(kAbschnitt);
        //  Nichts mehr drin: dann soll auch keine Beidatei liegenbleiben.
        if (wurzel.isEmpty()) return mg::editsidecar::entferne(datei);
        return mg::editsidecar::schreibe(datei, wurzel);
    }
    QJsonObject o;
    for (auto it = breiten.constBegin(); it != breiten.constEnd(); ++it)
        o.insert(QString::number(it.key()), it.value());
    wurzel.insert(kAbschnitt, o);
    return mg::editsidecar::schreibe(datei, wurzel);
}

}  // namespace mg::table
