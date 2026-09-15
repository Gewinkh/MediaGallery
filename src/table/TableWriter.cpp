#include "table/TableWriter.h"

#include "core/TextEncoding.h"
#include "table/TableSort.h"

namespace mg::table {
namespace {

const QByteArray kBom("\xEF\xBB\xBF");

//  So viele Datensaetze entscheiden, wie die Datei klammert.
constexpr int kStilProbe = 200;

//  Ein Datensatz der Grundlage als Bytebereich. `term` ist sein Zeilenende
//  (leer in der letzten Zeile einer Datei ohne abschliessenden Umbruch).
struct Satz {
    qsizetype  anfang = 0;
    qsizetype  inhaltEnde = 0;
    qsizetype  ende = 0;
    QByteArray term;
};

//  Wie die Datei ein NEUES Feld schreibt - abgelesen an ihren eigenen.
struct Stil {
    bool alle = false;          // jedes belegte Feld geklammert
    bool text = false;          // Text geklammert, Zahlen nicht
    bool leer = false;          // ein leeres Feld als ""
};

QString dekodiere(const QByteArray& b, qsizetype von, qsizetype bis, bool cp1252) {
    const QByteArray stueck = b.mid(von, bis - von);
    QString t = cp1252 ? mg::decodeCp1252(stueck) : QString::fromUtf8(stueck);
    //  Wie der Leser: Zeilen am LF getrennt, ein CR davor gehoert zum Ende.
    if (t.contains(u'\r')) t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return t;
}

Stil erkenneStil(const QByteArray& b, const QList<Satz>& saetze, QChar trenner, bool cp1252) {
    int belegt = 0, belegtGeklammert = 0, texte = 0, texteGeklammert = 0;
    int zahlen = 0, zahlenGeklammert = 0, leere = 0, leereGeklammert = 0;
    QList<std::pair<qsizetype, qsizetype>> grenzen;
    const int n = qMin(int(saetze.size()), kStilProbe);
    for (int k = 0; k < n; ++k) {
        const Satz& s = saetze.at(k);
        if (s.inhaltEnde <= s.anfang) continue;
        const QString text = dekodiere(b, s.anfang, s.inhaltEnde, cp1252);
        const QStringList felder = splitRecord(text, trenner, nullptr, nullptr, &grenzen);
        for (int i = 0; i < felder.size() && i < grenzen.size(); ++i) {
            const bool geklammert = grenzen.at(i).second > grenzen.at(i).first
                                    && text.at(grenzen.at(i).first) == u'"';
            if (felder.at(i).isEmpty()) {
                ++leere; if (geklammert) ++leereGeklammert;
                continue;
            }
            ++belegt; if (geklammert) ++belegtGeklammert;
            bool zahl = false;
            alsZahl(felder.at(i), &zahl);
            if (zahl) { ++zahlen; if (geklammert) ++zahlenGeklammert; }
            else      { ++texte;  if (geklammert) ++texteGeklammert; }
        }
    }
    Stil st;
    st.alle = belegt > 0 && belegtGeklammert * 10 >= belegt * 9;
    st.text = !st.alle && texte > 0 && texteGeklammert * 10 >= texte * 9
              && zahlenGeklammert * 10 <= zahlen;
    st.leer = leere > 0 && leereGeklammert * 10 >= leere * 9;
    return st;
}

bool neuKlammern(const Stil& st, const QString& wert) {
    if (wert.isEmpty()) return st.leer;
    if (st.alle) return true;
    if (!st.text) return false;
    bool zahl = false;
    alsZahl(wert, &zahl);
    return !zahl;
}

}  // namespace

QString feldText(const QString& wert, QChar trenner, bool klammern, const QString& umbruch) {
    const bool noetig = wert.contains(trenner) || wert.contains(u'"')
                        || wert.contains(u'\n') || wert.contains(u'\r');
    if (!noetig && !klammern) return wert;
    QString innen = wert;
    innen.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    if (umbruch != QStringLiteral("\n") && innen.contains(u'\n'))
        innen.replace(QStringLiteral("\n"), umbruch);
    return QLatin1Char('"') + innen + QLatin1Char('"');
}

SchreibErgebnis baueDatei(const QByteArray& b, const QList<int>& grundZeilenNr,
                          QChar trenner, bool cp1252, const QList<SchreibZeile>& zeilen) {
    SchreibErgebnis erg;
    const bool bom = !cp1252 && b.startsWith(kBom);

    //  Zeilenanfaenge: Eintrag j ist der Beginn der Zeile j+1.
    QList<qsizetype> anfang;
    anfang.append(0);
    for (qsizetype i = 0; i < b.size(); ++i)
        if (b.at(i) == '\n') anfang.append(i + 1);
    const int stuecke = int(anfang.size());
    auto stueckAnfang = [&](int j) { return (j == 0 && bom) ? qsizetype(3) : anfang.at(j); };
    auto stueckEnde   = [&](int j) { return j + 1 < stuecke ? anfang.at(j + 1) - 1 : b.size(); };
    auto stueckLeer   = [&](int j) {
        const qsizetype von = stueckAnfang(j), bis = stueckEnde(j);
        return bis <= von || (bis - von == 1 && b.at(von) == '\r');
    };
    //  Wie der Leser: Leerzeilen am Dateiende gehoeren zu keinem Datensatz.
    int genutzt = stuecke;
    while (genutzt > 0 && stueckLeer(genutzt - 1)) --genutzt;

    const int n = int(grundZeilenNr.size());
    QList<Satz> saetze;
    saetze.reserve(n);
    for (int k = 0; k < n; ++k) {
        const int erste = grundZeilenNr.at(k) - 1;
        const int letzte = (k + 1 < n) ? grundZeilenNr.at(k + 1) - 2 : genutzt - 1;
        //  Haertung: die Nummern kommen vom Aufrufer und muessen aufsteigend in
        //  der Datei liegen, sonst waere jede Bytegrenze geraten.
        if (erste < 0 || letzte < erste || letzte >= genutzt) {
            erg.fehler = QStringLiteral("Grundlage passt nicht zur Datei");
            return erg;
        }
        Satz s;
        s.anfang = stueckAnfang(erste);
        s.ende = (letzte + 1 < stuecke) ? anfang.at(letzte + 1) : b.size();
        s.inhaltEnde = s.ende;
        if (s.inhaltEnde > s.anfang && b.at(s.inhaltEnde - 1) == '\n') {
            --s.inhaltEnde;
            s.term = QByteArrayLiteral("\n");
            if (s.inhaltEnde > s.anfang && b.at(s.inhaltEnde - 1) == '\r') {
                --s.inhaltEnde;
                s.term = QByteArrayLiteral("\r\n");
            }
        }
        saetze.append(s);
    }
    for (const SchreibZeile& z : zeilen) {
        if (!z.zeile || z.herkunft >= n) {
            erg.fehler = QStringLiteral("Grundlage passt nicht zur Datei");
            return erg;
        }
    }

    QByteArray dominant = QByteArrayLiteral("\n");
    for (const Satz& s : std::as_const(saetze))
        if (!s.term.isEmpty()) { dominant = s.term; break; }
    const QByteArray letzterTerm = n > 0 ? saetze.last().term : dominant;
    const qsizetype rest = n > 0 ? saetze.last().ende : 0;

    const Stil stil = erkenneStil(b, saetze, trenner, cp1252);
    const QString umbruchText = QString::fromLatin1(dominant);

    QByteArray& aus = erg.bytes;
    aus.reserve(b.size() + 256);
    if (bom) aus += kBom;

    QList<std::pair<qsizetype, qsizetype>> grenzen;
    int zeile = 1;
    erg.zeilenNr.reserve(zeilen.size());
    for (int i = 0; i < zeilen.size(); ++i) {
        const SchreibZeile& sz = zeilen.at(i);
        const Zeile& z = *sz.zeile;
        QByteArray inhalt;
        if (sz.herkunft >= 0 && !sz.geaendert) {
            const Satz& s = saetze.at(sz.herkunft);
            inhalt = b.mid(s.anfang, s.inhaltEnde - s.anfang);
        } else if (!z.isEmpty() || sz.herkunft < 0) {
            QStringList alt;
            QString altText;
            QString innenUmbruch = umbruchText;
            if (sz.herkunft >= 0) {
                const Satz& s = saetze.at(sz.herkunft);
                altText = dekodiere(b, s.anfang, s.inhaltEnde, cp1252);
                alt = splitRecord(altText, trenner, nullptr, nullptr, &grenzen);
                if (!s.term.isEmpty()) innenUmbruch = QString::fromLatin1(s.term);
            }
            const int felder = z.felder();
            bool gleich = sz.herkunft >= 0 && alt.size() == felder;
            QString text;
            for (int j = 0; j < felder; ++j) {
                if (j > 0) text += trenner;
                const QString w = z.wert(j);
                if (sz.herkunft >= 0 && j < alt.size() && j < grenzen.size() && w == alt.at(j)) {
                    QString stueck = altText.mid(grenzen.at(j).first,
                                                 grenzen.at(j).second - grenzen.at(j).first);
                    if (innenUmbruch != QStringLiteral("\n") && stueck.contains(u'\n'))
                        stueck.replace(QStringLiteral("\n"), innenUmbruch);
                    text += stueck;
                } else {
                    gleich = false;
                    text += feldText(w, trenner, neuKlammern(stil, w), umbruchText);
                }
            }
            //  Eine Zeile aus EINEM leeren Feld waere eine Leerzeile - und die
            //  trennt beim naechsten Lesen zwei Tabellen.
            if (felder == 1 && text.isEmpty()) text = QStringLiteral("\"\"");
            if (gleich) {
                const Satz& s = saetze.at(sz.herkunft);
                inhalt = b.mid(s.anfang, s.inhaltEnde - s.anfang);
            } else if (cp1252) {
                qsizetype fehlerAn = -1;
                inhalt = mg::encodeCp1252(text, &fehlerAn);
                if (fehlerAn >= 0) {
                    erg.fehler = QStringLiteral("Zeichen in CP1252 nicht darstellbar");
                    erg.fehlerZeile = i;
                    erg.bytes.clear();
                    return erg;
                }
            } else {
                inhalt = text.toUtf8();
            }
        } else {
            //  Eine Leerzeile der Grundlage, als geaendert markiert: sie hat
            //  keinen Inhalt, der sich geaendert haben koennte.
            const Satz& s = saetze.at(sz.herkunft);
            inhalt = b.mid(s.anfang, s.inhaltEnde - s.anfang);
        }

        QByteArray term = sz.herkunft >= 0 ? saetze.at(sz.herkunft).term : dominant;
        if (i + 1 == zeilen.size()) term = letzterTerm;
        else if (term.isEmpty())    term = dominant;

        aus += inhalt;
        aus += term;
        erg.zeilenNr.append(zeile);
        zeile += 1 + int(inhalt.count('\n'));
    }
    //  Leerzeilen am Ende der Grundlage bleiben am Ende.
    if (!zeilen.isEmpty() && rest < b.size()) aus += b.mid(rest);

    erg.ok = true;
    return erg;
}

}  // namespace mg::table
