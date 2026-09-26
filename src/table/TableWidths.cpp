#include "table/TableWidths.h"

#include "core/EditSidecar.h"
#include "core/MGEditBin.h"

#include <QJsonObject>
#include <QStringList>

namespace mg::table {
namespace {

const QLatin1String kBreiten("spaltenbreiten");
const QLatin1String kFormate("spaltenformate");

//  Die Beidatei ist eine fremde Datei, auch wenn die App sie geschrieben hat -
//  eine Spalte zaehlt nur mit gueltiger Nummer und Breite im erlaubten Bereich.
int spalteVon(const QString& schluessel) {
    bool ok = false;
    const int s = schluessel.toInt(&ok);
    return (ok && s >= 0) ? s : -1;
}

void nimmBreite(QHash<int, int>& aus, const QString& schluessel, int px) {
    const int spalte = spalteVon(schluessel);
    if (spalte >= 0 && px >= kMinBreite && px <= kMaxBreite) aus.insert(spalte, px);
}

//  Nur "#rrggbb" - alles andere landete ungeprueft in einer QML-Farbbindung.
QString farbeVon(const QString& t) {
    if (t.size() != 7 || t.at(0) != QLatin1Char('#')) return {};
    for (int i = 1; i < 7; ++i) {
        const char16_t c = t.at(i).unicode();
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')
              || (c >= u'A' && c <= u'F')))
            return {};
    }
    return t;
}

template <class Obj>
void nimmFormat(QHash<int, SpaltenFormat>& aus, const QString& schluessel, const Obj& o) {
    const int spalte = spalteVon(schluessel);
    if (spalte < 0) return;
    SpaltenFormat f;
    f.fett        = o.value(mg::mgeb::k_bold).toBool(false);
    f.farbe       = farbeVon(o.value(mg::mgeb::k_color).toString(QString()));
    f.hintergrund = farbeVon(o.value(mg::mgeb::k_fill).toString(QString()));
    if (!f.leer()) aus.insert(spalte, f);
}

}  // namespace

QString lesbarAuf(const QString& hintergrund) {
    if (hintergrund.size() != 7) return QStringLiteral("#1a1a1a");
    const int r = hintergrund.mid(1, 2).toInt(nullptr, 16);
    const int g = hintergrund.mid(3, 2).toInt(nullptr, 16);
    const int b = hintergrund.mid(5, 2).toInt(nullptr, 16);
    return (299 * r + 587 * g + 114 * b) / 1000 >= 140 ? QStringLiteral("#1a1a1a")
                                                       : QStringLiteral("#f5f5f5");
}

void liesSpalten(const QString& datei, QHash<int, int>& breiten,
                 QHash<int, SpaltenFormat>& formate) {
    breiten.clear();
    formate.clear();
    const mg::editsidecar::Inhalt in = mg::editsidecar::liesInhalt(datei);
    if (in.istBin) {
        const mg::mgeb::Objekt b = in.bin.wurzel().value(mg::mgeb::k_spaltenbreiten).toObject();
        for (int i = 0; i < b.count(); ++i)
            nimmBreite(breiten, b.schluesselBei(i), b.wertBei(i).toInt());
        const mg::mgeb::Objekt f = in.bin.wurzel().value(mg::mgeb::k_spaltenformate).toObject();
        for (int i = 0; i < f.count(); ++i)
            nimmFormat(formate, f.schluesselBei(i), f.wertBei(i).toObject());
        return;
    }
    const QJsonObject b = in.json.value(kBreiten).toObject();
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        nimmBreite(breiten, it.key(), it.value().toInt());
    const QJsonObject f = in.json.value(kFormate).toObject();
    for (auto it = f.constBegin(); it != f.constEnd(); ++it)
        nimmFormat(formate, it.key(), mg::mgeb::alsObjekt(it.value()));
}

bool schreibeSpalten(const QString& datei, const QHash<int, int>& breiten,
                     const QHash<int, SpaltenFormat>& formate) {
    const mg::editsidecar::Inhalt in = mg::editsidecar::liesInhalt(datei, false);

    mg::mgeb::Schreiber s;
    s.beginneObjekt();
    //  BEIDE eigenen Abschnitte nennen - ein hier vergessener kaeme als
    //  fremder mit und staende danach doppelt in der Datei.
    const int fremde = mg::editsidecar::uebernimmFremde(
        s, in, QStringList{ kBreiten, kFormate });

    if (!breiten.isEmpty()) {
        s.schluessel(mg::mgeb::k_spaltenbreiten);
        s.beginneObjekt();
        for (auto it = breiten.constBegin(); it != breiten.constEnd(); ++it)
            s.feld(QString::number(it.key()), it.value());
        s.beendeObjekt();
    }
    if (!formate.isEmpty()) {
        s.schluessel(mg::mgeb::k_spaltenformate);
        s.beginneObjekt();
        for (auto it = formate.constBegin(); it != formate.constEnd(); ++it) {
            s.schluessel(QString::number(it.key()));
            s.beginneObjekt();
            if (it->fett) s.feld(mg::mgeb::k_bold, true);
            if (!it->farbe.isEmpty()) s.feld(mg::mgeb::k_color, it->farbe);
            if (!it->hintergrund.isEmpty()) s.feld(mg::mgeb::k_fill, it->hintergrund);
            s.beendeObjekt();
        }
        s.beendeObjekt();
    }
    s.beendeObjekt();

    //  Nichts mehr drin: dann soll auch keine Beidatei liegenbleiben.
    if (breiten.isEmpty() && formate.isEmpty() && fremde == 0)
        return mg::editsidecar::entferne(datei);
    return mg::editsidecar::schreibeBin(datei, s.fertig());
}

}  // namespace mg::table
