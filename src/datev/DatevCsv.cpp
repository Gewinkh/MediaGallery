#include "datev/DatevCsv.h"

namespace mg::datev {

namespace {

void bucheEin(const Zeile& z, Summenspalten ss, double* soll, double* haben) {
    if (ss.betrag < 0) return;
    bool gut = false;
    const double v = parseBetrag(z.wert(ss.betrag), &gut);
    if (!gut) return;
    if (ss.sollHaben >= 0 && z.wert(ss.sollHaben).trimmed().compare(QLatin1String("H"),
                                                                   Qt::CaseInsensitive) == 0)
        *haben += v;
    else
        *soll += v;
}

}  // namespace

Summenspalten summenSpalten(const QStringList& spalten) {
    auto spalteMit = [&spalten](QLatin1String praefix, int ersatz) {
        for (int i = 0; i < spalten.size(); ++i)
            if (spalten.at(i).startsWith(praefix, Qt::CaseInsensitive)) return i;
        return (ersatz < spalten.size()) ? ersatz : -1;
    };
    return { spalteMit(QLatin1String("Umsatz"), 0), spalteMit(QLatin1String("Soll/Haben"), 1) };
}

void summiere(const QList<Zeile>& buchungen, Summenspalten s, const QList<int>* auswahl,
              double* soll, double* haben) {
    *soll = *haben = 0.0;
    if (!auswahl) {
        for (const Zeile& z : buchungen) bucheEin(z, s, soll, haben);
        return;
    }
    for (int i : *auswahl)
        if (i >= 0 && i < buchungen.size()) bucheEin(buchungen.at(i), s, soll, haben);
}

bool looksLikeDatev(const QByteArray& anfang) {
    //  Nur die erste Zeile ansehen, und die nur so weit, wie die Kennung reicht.
    const QByteArray kopf = anfang.left(64);
    return kopf.startsWith("\"EXTF\";") || kopf.startsWith("\"DTVF\";")
        || kopf.startsWith("\xEF\xBB\xBF\"EXTF\";") || kopf.startsWith("\xEF\xBB\xBF\"DTVF\";");
}

Datei parse(const QByteArray& raw) {
    Datei d;
    const mg::table::Datei roh = mg::table::parse(raw, u';');
    d.cp1252 = roh.cp1252;
    d.abgeschnitten = roh.abgeschnitten;
    d.warnungen = roh.warnungen;
    if (!roh.ok) { d.fehler = roh.fehler; return d; }

    d.kopf = roh.zeilen.at(0).alle();
    if (d.kopf.isEmpty() || (d.kopf.at(0) != QLatin1String("EXTF")
                             && d.kopf.at(0) != QLatin1String("DTVF"))) {
        d.fehler = QStringLiteral("keine DATEV-Kennung");
        return d;
    }
    if (roh.zeilen.size() < 2) {
        d.fehler = QStringLiteral("keine Spaltenzeile");
        return d;
    }
    d.spalten = roh.zeilen.at(1).alle();

    const int erwartet = int(d.spalten.size());
    d.buchungen.reserve(roh.zeilen.size() - 2);
    for (qsizetype z = 2; z < roh.zeilen.size(); ++z) {
        Zeile b = roh.zeilen.at(z);
        if (b.isEmpty()) continue;
        //  Eine DATEV-Zeile endet mit dem Trenner; das eine leere Feld dahinter
        //  ist Normalfall und keine Meldung wert.
        int gezaehlt = b.felder();
        if (gezaehlt == erwartet + 1 && b.wert(erwartet).isEmpty()) gezaehlt = erwartet;
        if (gezaehlt != erwartet)
            d.warnungen.append({roh.zeilenNr.value(int(z), int(z) + 1),
                                QStringLiteral("%1 Felder statt %2")
                                    .arg(gezaehlt).arg(erwartet)});
        //  Kurze Zeilen zaehlen wie volle - die Anzeige griffe sonst je Zeile
        //  ins Leere. Abschneiden verloere Daten, lange bleiben also lang.
        b.ensureFelder(erwartet);
        d.buchungen.append(b);
    }

    const Summenspalten ss = summenSpalten(d.spalten);
    d.spalteGefuellt = QList<bool>(erwartet, false);
    for (const Zeile& z : std::as_const(d.buchungen)) {
        for (const auto& [spalte, wert] : z.belegte())
            if (spalte < erwartet && !d.spalteGefuellt.at(spalte)
                && !wert.trimmed().isEmpty())
                d.spalteGefuellt[spalte] = true;
        bucheEin(z, ss, &d.soll, &d.haben);
    }

    d.ok = true;
    return d;
}

double parseBetrag(QStringView s, bool* ok) {
    if (ok) *ok = false;
    QString rein;
    rein.reserve(s.size());
    for (QChar c : s) {
        if (c == u'.' || c == u' ') continue;        // Tausenderpunkt
        if (c == u',') { rein.append(u'.'); continue; }
        rein.append(c);
    }
    if (rein.isEmpty()) return 0.0;
    bool gut = false;
    const double v = rein.toDouble(&gut);
    if (ok) *ok = gut;
    return gut ? v : 0.0;
}

}  // namespace mg::datev
