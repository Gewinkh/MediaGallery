#include "table/DelimitedText.h"

#include "core/TextEncoding.h"

#include <QMap>

#include <algorithm>

namespace mg::table {
namespace {

constexpr QChar kKlammer = u'"';

//  Zeilen aufteilen; CRLF und LF, die leere Restzeile am Dateiende faellt weg.
QList<QStringView> inZeilen(const QString& text) {
    const QList<QStringView> roh = QStringView(text).split(u'\n');
    QList<QStringView> aus;
    aus.reserve(roh.size());
    for (QStringView z : roh) {
        if (z.endsWith(u'\r')) z.chop(1);
        aus.append(z);
    }
    while (!aus.isEmpty() && aus.last().isEmpty()) aus.removeLast();
    return aus;
}

}  // namespace

Zeile::Zeile(const QStringList& felder) : m_felder(int(felder.size())) {
    int belegt = 0;
    for (const QString& f : felder) if (!f.isEmpty()) ++belegt;
    m_gefuellt.reserve(belegt);
    for (int i = 0; i < felder.size(); ++i)
        if (!felder.at(i).isEmpty())
            m_gefuellt.append({quint16(qMin(i, 0xFFFF)), felder.at(i)});
}

QString Zeile::wert(int spalte) const {
    if (spalte < 0 || spalte >= m_felder) return {};
    auto it = std::lower_bound(m_gefuellt.cbegin(), m_gefuellt.cend(), quint16(spalte),
                               [](const std::pair<quint16, QString>& e, quint16 s) {
                                   return e.first < s;
                               });
    return (it != m_gefuellt.cend() && it->first == spalte) ? it->second : QString();
}

namespace {

//  Wie viele Felder der Zeile lassen sich als Zahl lesen? Ueberschriften sind
//  Text, Daten enthalten meist Zahlen - das ist der einzige Anhaltspunkt, wenn
//  ein Block keinen Namen traegt.
int zahlenFelder(const Zeile& z) {
    int n = 0;
    for (const auto& [spalte, wert] : z.belegte()) {
        Q_UNUSED(spalte);
        bool ok = false;
        QString rein = wert;
        rein.replace(QLatin1Char(','), QLatin1Char('.'));
        rein.toDouble(&ok);
        if (ok) ++n;
    }
    return n;
}

}  // namespace

QList<Bereich> findBlocks(const Datei& d) {
    QList<Bereich> aus;
    int i = 0;
    const int n = int(d.zeilen.size());
    while (i < n) {
        while (i < n && d.zeilen.at(i).isEmpty()) ++i;
        if (i >= n) break;
        int j = i;
        while (j < n && !d.zeilen.at(j).isEmpty()) ++j;

        Bereich b;
        b.von = i;
        b.bis = j;
        int s = i;
        //  Ein Blockname steht allein in seiner Zeile, und darunter folgt etwas
        //  Breiteres - sonst waere es eine Tabelle mit einer Spalte.
        if (j - i >= 2 && d.zeilen.at(i).felder() == 1 && d.zeilen.at(i + 1).felder() > 1) {
            b.titel = i;
            s = i + 1;
        }
        if (b.titel >= 0) {
            //  Nach einem Blocknamen traegt die naechste Zeile die Spaltennamen.
            //  Das ist der verlaessliche Fall: eine reine Text-Tabelle (Namen,
            //  Raeume, Sprechstunden) hat gar keine Zahl, an der sich eine
            //  Kopfzeile sonst erkennen liesse.
            if (j - s >= 2) { b.kopf = s; b.daten = s + 1; }
            else            { b.daten = s; }
        } else if (j - s >= 2 && zahlenFelder(d.zeilen.at(s)) == 0
                              && zahlenFelder(d.zeilen.at(s + 1)) > 0) {
            b.kopf  = s;
            b.daten = s + 1;
        } else {
            b.daten = s;
        }
        aus.append(b);
        i = j;
    }
    return aus;
}

QStringList Zeile::alle() const {
    QStringList aus;
    aus.reserve(m_felder);
    for (int i = 0; i < m_felder; ++i) aus.append(wert(i));
    return aus;
}

QStringList splitRecord(QStringView zeile, QChar trenner, bool* unbalanciert) {
    if (unbalanciert) *unbalanciert = false;
    QStringList felder;
    QString feld;
    feld.reserve(64);

    qsizetype i = 0;
    const qsizetype n = zeile.size();
    while (true) {
        feld.clear();
        bool inKlammer = false;
        bool offen     = false;
        if (i < n && zeile[i] == kKlammer) { inKlammer = offen = true; ++i; }

        while (i < n) {
            const QChar c = zeile[i];
            if (!inKlammer) {
                if (c == trenner) break;
                feld.append(c); ++i;
                continue;
            }
            if (c != kKlammer) { feld.append(c); ++i; continue; }
            //  `""` - ein Anfuehrungszeichen im Feld
            if (i + 1 < n && zeile[i + 1] == kKlammer) {
                feld.append(kKlammer); i += 2; continue;
            }
            //  Nur wenn danach der Trenner oder das Zeilenende steht, schliesst
            //  dieses Anfuehrungszeichen das Feld. Sonst gehoert es zum Inhalt.
            if (i + 1 >= n || zeile[i + 1] == trenner) { offen = false; ++i; break; }
            feld.append(kKlammer); ++i;
        }
        if (offen && unbalanciert) *unbalanciert = true;
        if (feld.size() > kMaxFeldZeichen) feld.truncate(kMaxFeldZeichen);
        felder.append(feld);

        if (felder.size() >= kMaxFelderZeile) break;
        if (i >= n) break;
        ++i;                                   // ueber den Trenner
    }
    return felder;
}

QChar detectSeparator(const QString& text, int zeilenProbe) {
    const QList<QStringView> zeilen = inZeilen(text);
    if (zeilen.isEmpty()) return u';';
    const int bis = int(qMin<qsizetype>(zeilen.size(), qMax(1, zeilenProbe)));

    QChar bester = u';';
    double bestesMass = 0.0;
    for (char16_t k : kTrenner) {
        const QChar t(k);
        //  Wie oft kommt welche Feldzahl vor? Gesucht ist der Trenner, der ueber
        //  die Probe die GLEICHMAESSIGSTE Zerlegung ergibt - ein Zeichen, das
        //  nur zufaellig im Text steht, liefert je Zeile eine andere Feldzahl.
        QMap<int, int> haeufig;
        for (int z = 0; z < bis; ++z) {
            if (zeilen.at(z).isEmpty()) continue;
            haeufig[int(splitRecord(zeilen.at(z), t).size())]++;
        }
        int felder = 0, treffer = 0, gezaehlt = 0;
        for (auto it = haeufig.cbegin(); it != haeufig.cend(); ++it) {
            gezaehlt += it.value();
            if (it.value() > treffer) { treffer = it.value(); felder = it.key(); }
        }
        if (felder < 2 || gezaehlt == 0) continue;
        //  Mass: Anteil der uebereinstimmenden Zeilen, gewichtet mit der
        //  Feldzahl - zwei Spalten sind ein schwaecherer Beleg als zwoelf.
        const double mass = (double(treffer) / gezaehlt) * qMin(felder, 32);
        if (mass > bestesMass) { bestesMass = mass; bester = t; }
    }
    return bester;
}

Datei parse(const QByteArray& raw, QChar trenner) {
    Datei d;
    if (raw.isEmpty()) { d.fehler = QStringLiteral("leer"); return d; }

    mg::TextEncodingUsed kod = mg::TextEncodingUsed::Utf8;
    const QString text = mg::decodeUnknownText(raw, &kod);
    d.cp1252 = (kod == mg::TextEncodingUsed::Cp1252);
    d.trenner = trenner.isNull() ? detectSeparator(text) : trenner;

    const QList<QStringView> zeilen = inZeilen(text);
    if (zeilen.isEmpty()) { d.fehler = QStringLiteral("leer"); return d; }

    //  Ein geklammertes Feld darf einen Zeilenumbruch enthalten - dann gehoert
    //  die naechste Zeile noch zum selben Datensatz. Ohne dieses Zusammenziehen
    //  zerfiele eine solche Zeile in zwei kaputte.
    bool unbal = false;
    for (qsizetype z = 0; z < zeilen.size(); ++z) {
        if (d.zeilen.size() >= kMaxZeilen) { d.abgeschnitten = true; break; }
        const int nr = int(z) + 1;
        //  Eine Leerzeile bleibt eine LEERE Zeile (`felder() == 0`), kein Feld.
        //  Sie traegt keine Daten, trennt aber sichtbar - Ausgaben stapeln
        //  mehrere Tabellen in eine Datei und setzen genau dorthin eine
        //  Leerzeile. Wer sie nicht will, fragt `Zeile::isEmpty()`.
        if (zeilen.at(z).isEmpty()) {
            d.zeilen.append(Zeile());
            d.zeilenNr.append(nr);
            continue;
        }
        QString roh = zeilen.at(z).toString();
        QStringList f = splitRecord(roh, d.trenner, &unbal);
        while (unbal && z + 1 < zeilen.size()) {
            ++z;
            roh += QLatin1Char('\n');
            roh += zeilen.at(z);
            f = splitRecord(roh, d.trenner, &unbal);
        }
        if (unbal)
            d.warnungen.append({nr, QStringLiteral("Anfuehrungszeichen nicht geschlossen")});
        d.zeilen.append(Zeile(f));
        d.zeilenNr.append(nr);
    }

    //  Eine Datei aus lauter Leerzeilen ist keine Tabelle.
    d.ok = std::any_of(d.zeilen.cbegin(), d.zeilen.cend(),
                       [](const Zeile& z) { return !z.isEmpty(); });
    if (!d.ok) d.fehler = QStringLiteral("leer");
    return d;
}

}  // namespace mg::table
