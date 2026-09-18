#include "table/TableFormula.h"

#include "table/TableSort.h"

#include <QHash>
#include <cmath>

namespace mg::table {
namespace {

//  Deckel gegen praeparierte Dateien. Ohne die Tiefendeckel legt eine Zelle aus
//  lauter Klammern den Stapel um; ohne den Zugriffsdeckel haelt eine Handvoll
//  Formeln ueber grosse Spannen den GUI-Faden.
constexpr int    kMaxTiefe  = 64;          // Klammern und Aufrufe ineinander
constexpr int    kMaxKette  = 64;          // Formel zeigt auf Formel
constexpr qint64 kMaxZugriffe = 2'000'000; // Zellzugriffe je Neuberechnung
constexpr int    kMaxProbe  = 400;         // Zellen fuer die Dezimalzeichen-Probe

//  Die Fehlertexte der Tabellenkalkulationen - sie stehen IN der Zelle und
//  gehoeren deshalb nicht nach Strings.cpp: eine Datei liest sich sonst je nach
//  eingestellter Sprache anders.
const QLatin1String kSyntax("#SYNTAX!");
const QLatin1String kRef("#REF!");
const QLatin1String kName("#NAME?");
const QLatin1String kWert("#VALUE!");
const QLatin1String kDiv("#DIV/0!");
const QLatin1String kZyklus("#CYCLE!");
const QLatin1String kGrenze("#LIMIT!");

enum class Art { Leer, Zahl, Text, Fehler };

struct Wert {
    Art     art = Art::Leer;
    double  zahl = 0.0;
    QString text;
};

Wert zahlWert(double d) {
    if (!std::isfinite(d)) return { Art::Fehler, 0.0, kWert };
    return { Art::Zahl, d, {} };
}
Wert textWert(QString s) { return { Art::Text, 0.0, std::move(s) }; }
Wert fehlerWert(QLatin1String s) { return { Art::Fehler, 0.0, QString(s) }; }

QString zahlText(double d, bool komma) {
    QString s;
    //  Ausserhalb dieses Bandes wird die feste Schreibweise unlesbar lang.
    if (d != 0.0 && (std::abs(d) >= 1e15 || std::abs(d) < 1e-10)) {
        s = QString::number(d, 'g', 12);
    } else {
        s = QString::number(d, 'f', 10);
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
        if (s == QLatin1String("-0")) s = QStringLiteral("0");
    }
    if (komma) s.replace(QLatin1Char('.'), QLatin1Char(','));
    return s;
}

//  Ein Argument ist entweder ein einzelner Wert oder eine SPANNE. Die Spanne
//  bleibt als Koordinatenpaar stehen und wird nie in eine Liste ausgerollt -
//  A1:A100000 kostete sonst hunderttausend Werte im Speicher.
struct Arg {
    bool spanne = false;
    int  z1 = 0, s1 = 0, z2 = 0, s2 = 0;
    Wert wert;
};

struct Lauf {
    QStringView s;
    qsizetype   i = 0;
    int         kette = 0;
    int         tiefe = 0;
};

enum class Fn {
    Summe, Produkt, Mittel, Min, Max, Anzahl, Anzahl2, Runden, Betrag, Ganzzahl,
    Wurzel, Potenz, Rest, Wenn, Und, Oder, Nicht, Verketten, Laenge, Gross, Klein,
    Glaetten, Links, Rechts, Teil, Zahl
};

//  Die beiden Namen mit Umlaut gibt es zusaetzlich in ASCII-Schreibung: eine
//  Datei, die ohne Umlaute geschrieben wurde, soll nicht an einem Buchstaben
//  scheitern.
const QHash<QString, Fn>& funktionen() {
    static const QHash<QString, Fn> t = {
        { QStringLiteral("SUM"), Fn::Summe },        { QStringLiteral("SUMME"), Fn::Summe },
        { QStringLiteral("PRODUCT"), Fn::Produkt },  { QStringLiteral("PRODUKT"), Fn::Produkt },
        { QStringLiteral("AVERAGE"), Fn::Mittel },   { QStringLiteral("MITTELWERT"), Fn::Mittel },
        { QStringLiteral("MIN"), Fn::Min },          { QStringLiteral("MAX"), Fn::Max },
        { QStringLiteral("COUNT"), Fn::Anzahl },     { QStringLiteral("ANZAHL"), Fn::Anzahl },
        { QStringLiteral("COUNTA"), Fn::Anzahl2 },   { QStringLiteral("ANZAHL2"), Fn::Anzahl2 },
        { QStringLiteral("ROUND"), Fn::Runden },     { QStringLiteral("RUNDEN"), Fn::Runden },
        { QStringLiteral("ABS"), Fn::Betrag },
        { QStringLiteral("INT"), Fn::Ganzzahl },     { QStringLiteral("GANZZAHL"), Fn::Ganzzahl },
        { QStringLiteral("SQRT"), Fn::Wurzel },      { QStringLiteral("WURZEL"), Fn::Wurzel },
        { QStringLiteral("POWER"), Fn::Potenz },     { QStringLiteral("POTENZ"), Fn::Potenz },
        { QStringLiteral("MOD"), Fn::Rest },         { QStringLiteral("REST"), Fn::Rest },
        { QStringLiteral("IF"), Fn::Wenn },          { QStringLiteral("WENN"), Fn::Wenn },
        { QStringLiteral("AND"), Fn::Und },          { QStringLiteral("UND"), Fn::Und },
        { QStringLiteral("OR"), Fn::Oder },          { QStringLiteral("ODER"), Fn::Oder },
        { QStringLiteral("NOT"), Fn::Nicht },        { QStringLiteral("NICHT"), Fn::Nicht },
        { QStringLiteral("CONCAT"), Fn::Verketten }, { QStringLiteral("VERKETTEN"), Fn::Verketten },
        { QStringLiteral("LEN"), Fn::Laenge },       { QStringLiteral("LÄNGE"), Fn::Laenge },
        { QStringLiteral("LAENGE"), Fn::Laenge },
        { QStringLiteral("UPPER"), Fn::Gross },      { QStringLiteral("GROSS"), Fn::Gross },
        { QStringLiteral("LOWER"), Fn::Klein },      { QStringLiteral("KLEIN"), Fn::Klein },
        { QStringLiteral("TRIM"), Fn::Glaetten },    { QStringLiteral("GLÄTTEN"), Fn::Glaetten },
        { QStringLiteral("GLAETTEN"), Fn::Glaetten },
        { QStringLiteral("LEFT"), Fn::Links },       { QStringLiteral("LINKS"), Fn::Links },
        { QStringLiteral("RIGHT"), Fn::Rechts },     { QStringLiteral("RECHTS"), Fn::Rechts },
        { QStringLiteral("MID"), Fn::Teil },         { QStringLiteral("TEIL"), Fn::Teil },
        { QStringLiteral("VALUE"), Fn::Zahl },       { QStringLiteral("WERT"), Fn::Zahl },
    };
    return t;
}

class Rechner {
public:
    Rechner(const QList<Zeile>& zeilen, int daten, int bis, bool komma)
        : m_zeilen(zeilen), m_daten(qMax(0, daten)),
          m_bis(qMin(bis, int(zeilen.size()))), m_komma(komma) {}

    QString anzeige(int zeile, int spalte) {
        const Wert w = zelle(zeile, spalte, 0);
        switch (w.art) {
        case Art::Leer:   return {};
        case Art::Zahl:   return zahlText(w.zahl, m_komma);
        case Art::Fehler: return w.text;
        case Art::Text:   break;
        }
        return w.text;
    }

private:
    Wert zelle(int zeile, int spalte, int kette);
    Wert rohWert(const QString& text) const;

    Wert ganzerAusdruck(Lauf& l);
    Wert vergleich(Lauf& l);
    Wert verkettung(Lauf& l);
    Wert summe(Lauf& l);
    Wert produkt(Lauf& l);
    Wert potenz(Lauf& l);
    Wert unaer(Lauf& l);
    Wert primaer(Lauf& l);
    Wert aufruf(Lauf& l, Fn fn);
    bool bezug(Lauf& l, int* zeile, int* spalte);

    double alsZahlWert(const Wert& w, bool* ok) const;
    QString alsTextWert(const Wert& w) const;

    //  Ueber die Zellen eines Arguments laufen - eine Spanne Zelle fuer Zelle,
    //  ein einzelner Wert genau einmal. `false` bricht den Lauf ab.
    template <class F>
    bool jedeZelle(const Arg& a, int kette, F f);

    const QList<Zeile>& m_zeilen;
    int  m_daten;
    int  m_bis;
    bool m_komma;
    qint64 m_zugriffe = 0;
    QHash<qint64, Wert> m_memo;
    QSet<qint64> m_laeuft;
};

void ueberspringe(Lauf& l) {
    while (l.i < l.s.size() && l.s[l.i].isSpace()) ++l.i;
}
QChar jetzt(Lauf& l) {
    ueberspringe(l);
    return l.i < l.s.size() ? l.s[l.i] : QChar();
}

Wert Rechner::rohWert(const QString& text) const {
    if (text.isEmpty()) return {};
    bool ok = false;
    const double d = alsZahl(text, &ok);
    return ok ? zahlWert(d) : textWert(text);
}

Wert Rechner::zelle(int zeile, int spalte, int kette) {
    if (zeile < m_daten || zeile >= m_bis || spalte < 0) return fehlerWert(kRef);
    if (++m_zugriffe > kMaxZugriffe) return fehlerWert(kGrenze);
    const QString roh = m_zeilen.at(zeile).wert(spalte);
    if (!istFormel(roh)) return rohWert(roh);

    const qint64 k = zellSchluessel(zeile, spalte);
    const auto it = m_memo.constFind(k);
    if (it != m_memo.cend()) return it.value();
    if (m_laeuft.contains(k) || kette >= kMaxKette) return fehlerWert(kZyklus);

    m_laeuft.insert(k);
    Lauf l{ QStringView(roh).mid(1), 0, kette + 1, 0 };
    const Wert w = ganzerAusdruck(l);
    m_laeuft.remove(k);
    m_memo.insert(k, w);
    return w;
}

Wert Rechner::ganzerAusdruck(Lauf& l) {
    const Wert w = vergleich(l);
    if (w.art == Art::Fehler) return w;
    ueberspringe(l);
    return l.i < l.s.size() ? fehlerWert(kSyntax) : w;
}

double Rechner::alsZahlWert(const Wert& w, bool* ok) const {
    *ok = true;
    switch (w.art) {
    case Art::Leer: return 0.0;
    case Art::Zahl: return w.zahl;
    case Art::Fehler: *ok = false; return 0.0;
    case Art::Text: break;
    }
    bool gut = false;
    const double d = alsZahl(w.text, &gut);
    *ok = gut;
    return d;
}

QString Rechner::alsTextWert(const Wert& w) const {
    switch (w.art) {
    case Art::Leer: return {};
    case Art::Zahl: return zahlText(w.zahl, m_komma);
    default: return w.text;
    }
}

Wert Rechner::vergleich(Lauf& l) {
    Wert a = verkettung(l);
    if (a.art == Art::Fehler) return a;
    ueberspringe(l);
    if (l.i >= l.s.size()) return a;

    const QChar c = l.s[l.i];
    const QChar n = (l.i + 1 < l.s.size()) ? l.s[l.i + 1] : QChar();
    int op = 0;                                    // 1 = , 2 <>, 3 <, 4 >, 5 <=, 6 >=
    if (c == u'=')                     { op = 1; l.i += 1; }
    else if (c == u'<' && n == u'>')   { op = 2; l.i += 2; }
    else if (c == u'<' && n == u'=')   { op = 5; l.i += 2; }
    else if (c == u'>' && n == u'=')   { op = 6; l.i += 2; }
    else if (c == u'<')                { op = 3; l.i += 1; }
    else if (c == u'>')                { op = 4; l.i += 1; }
    else return a;

    const Wert b = verkettung(l);
    if (b.art == Art::Fehler) return b;

    //  Zwei Zahlen werden als Zahlen verglichen, sonst als Text - so wie jede
    //  Tabellenkalkulation. Das Ergebnis ist 1 oder 0.
    int cmp = 0;
    bool okA = false, okB = false;
    const double x = alsZahlWert(a, &okA);
    const double y = alsZahlWert(b, &okB);
    if (okA && okB && a.art != Art::Text && b.art != Art::Text) {
        cmp = (x < y) ? -1 : (x > y) ? 1 : 0;
    } else {
        const QString sa = alsTextWert(a);
        const QString sb = alsTextWert(b);
        cmp = QString::compare(sa, sb, Qt::CaseInsensitive);
    }
    bool r = false;
    switch (op) {
    case 1: r = (cmp == 0); break;
    case 2: r = (cmp != 0); break;
    case 3: r = (cmp <  0); break;
    case 4: r = (cmp >  0); break;
    case 5: r = (cmp <= 0); break;
    case 6: r = (cmp >= 0); break;
    }
    return zahlWert(r ? 1.0 : 0.0);
}

Wert Rechner::verkettung(Lauf& l) {
    Wert a = summe(l);
    while (a.art != Art::Fehler && jetzt(l) == u'&') {
        ++l.i;
        const Wert b = summe(l);
        if (b.art == Art::Fehler) return b;
        a = textWert(alsTextWert(a) + alsTextWert(b));
    }
    return a;
}

Wert Rechner::summe(Lauf& l) {
    Wert a = produkt(l);
    if (a.art == Art::Fehler) return a;
    for (;;) {
        const QChar c = jetzt(l);
        if (c != u'+' && c != u'-') return a;
        ++l.i;
        const Wert b = produkt(l);
        if (b.art == Art::Fehler) return b;
        bool okA = false, okB = false;
        const double x = alsZahlWert(a, &okA);
        const double y = alsZahlWert(b, &okB);
        if (!okA || !okB) return fehlerWert(kWert);
        a = zahlWert(c == u'+' ? x + y : x - y);
        if (a.art == Art::Fehler) return a;
    }
}

Wert Rechner::produkt(Lauf& l) {
    Wert a = potenz(l);
    if (a.art == Art::Fehler) return a;
    for (;;) {
        const QChar c = jetzt(l);
        if (c != u'*' && c != u'/') return a;
        ++l.i;
        const Wert b = potenz(l);
        if (b.art == Art::Fehler) return b;
        bool okA = false, okB = false;
        const double x = alsZahlWert(a, &okA);
        const double y = alsZahlWert(b, &okB);
        if (!okA || !okB) return fehlerWert(kWert);
        if (c == u'/' && y == 0.0) return fehlerWert(kDiv);
        a = zahlWert(c == u'*' ? x * y : x / y);
        if (a.art == Art::Fehler) return a;
    }
}

Wert Rechner::potenz(Lauf& l) {
    const Wert a = unaer(l);
    if (a.art == Art::Fehler || jetzt(l) != u'^') return a;
    ++l.i;
    const Wert b = potenz(l);                      // rechtsassoziativ
    if (b.art == Art::Fehler) return b;
    bool okA = false, okB = false;
    const double x = alsZahlWert(a, &okA);
    const double y = alsZahlWert(b, &okB);
    if (!okA || !okB) return fehlerWert(kWert);
    return zahlWert(std::pow(x, y));
}

Wert Rechner::unaer(Lauf& l) {
    const QChar c = jetzt(l);
    if (c == u'+' || c == u'-') {
        ++l.i;
        const Wert a = unaer(l);
        if (a.art == Art::Fehler) return a;
        bool ok = false;
        const double x = alsZahlWert(a, &ok);
        if (!ok) return fehlerWert(kWert);
        return zahlWert(c == u'-' ? -x : x);
    }
    return primaer(l);
}

//  Ein Bezug: [$]Buchstaben[$]Ziffern. Der Dollar wird gelesen und verworfen -
//  eine CSV kennt kein Herunterziehen, gegen das er schuetzen muesste, aber die
//  Formel kommt oft genug aus einer Tabellenkalkulation.
bool Rechner::bezug(Lauf& l, int* zeile, int* spalte) {
    qsizetype i = l.i;
    if (i < l.s.size() && l.s[i] == u'$') ++i;
    int sp = 0, buchstaben = 0;
    while (i < l.s.size() && l.s[i].isLetter() && l.s[i].unicode() < 128) {
        sp = sp * 26 + (l.s[i].toUpper().unicode() - u'A' + 1);
        ++i; ++buchstaben;
        if (buchstaben > 3) return false;          // ueber ZZZ gibt es keine Spalte
    }
    if (buchstaben == 0) return false;
    if (i < l.s.size() && l.s[i] == u'$') ++i;
    qint64 nr = 0;
    int ziffern = 0;
    while (i < l.s.size() && l.s[i].isDigit()) {
        nr = nr * 10 + l.s[i].digitValue();
        ++i; ++ziffern;
        if (ziffern > 7) return false;             // ueber kMaxZeilen hinaus
    }
    //  `A0` ist eine gueltige SCHREIBWEISE, die nirgendwohin zeigt - sie ergibt
    //  #REF! und keinen Syntaxfehler.
    if (ziffern == 0) return false;
    l.i = i;
    *spalte = sp - 1;
    *zeile  = m_daten + int(nr) - 1;
    return true;
}

template <class F>
bool Rechner::jedeZelle(const Arg& a, int kette, F f) {
    if (!a.spanne) return f(a.wert);
    for (int z = a.z1; z <= a.z2; ++z)
        for (int s = a.s1; s <= a.s2; ++s)
            if (!f(zelle(z, s, kette))) return false;
    return true;
}

Wert Rechner::aufruf(Lauf& l, Fn fn) {
    QList<Arg> args;
    ueberspringe(l);
    if (l.i >= l.s.size() || l.s[l.i] != u'(') return fehlerWert(kSyntax);
    ++l.i;
    if (jetzt(l) == u')') { ++l.i; }
    else {
        for (;;) {
            //  Erst als Spanne versuchen - `A1:B7` ist kein Ausdruck, und ein
            //  Rueckschritt auf die alte Stelle ist billiger als ein Vorgriff.
            Arg a;
            const qsizetype merk = l.i;
            ueberspringe(l);
            int z1 = 0, s1 = 0, z2 = 0, s2 = 0;
            if (bezug(l, &z1, &s1) && jetzt(l) == u':') {
                ++l.i;
                ueberspringe(l);
                if (bezug(l, &z2, &s2)) {
                    a.spanne = true;
                    a.z1 = qMin(z1, z2); a.z2 = qMax(z1, z2);
                    a.s1 = qMin(s1, s2); a.s2 = qMax(s1, s2);
                }
            }
            if (!a.spanne) {
                l.i = merk;
                a.wert = vergleich(l);
                if (a.wert.art == Art::Fehler) return a.wert;
            }
            args.append(a);
            if (args.size() > kMaxFelderZeile) return fehlerWert(kGrenze);
            const QChar c = jetzt(l);
            if (c == u';' || c == u',') { ++l.i; continue; }
            if (c == u')') { ++l.i; break; }
            return fehlerWert(kSyntax);
        }
    }

    const int kette = l.kette;
    //  Die Sammler laufen ueber alle Zellen, die Uebrigen ueber genau ein
    //  Argument; eine Spanne an einer Stelle, die einen Wert erwartet, ist ein
    //  Fehler und kein stiller Griff auf die erste Zelle.
    auto einzel = [&](int i) -> Wert {
        if (i >= args.size() || args.at(i).spanne) return fehlerWert(kWert);
        return args.at(i).wert;
    };
    auto einzelZahl = [&](int i, bool* ok) -> double {
        const Wert w = einzel(i);
        if (w.art == Art::Fehler) { *ok = false; return 0.0; }
        return alsZahlWert(w, ok);
    };

    switch (fn) {
    case Fn::Summe: case Fn::Produkt: case Fn::Mittel:
    case Fn::Min: case Fn::Max: case Fn::Anzahl: case Fn::Anzahl2: {
        double summe = 0.0, produkt = 1.0, kleinst = 0.0, groesst = 0.0;
        int zahlen = 0, belegt = 0;
        Wert fehler;
        for (const Arg& a : std::as_const(args)) {
            if (!jedeZelle(a, kette, [&](const Wert& w) {
                    if (w.art == Art::Fehler) { fehler = w; return false; }
                    if (w.art == Art::Leer) return true;
                    ++belegt;
                    bool ok = false;
                    const double d = alsZahlWert(w, &ok);
                    //  Text in einer Spanne ist kein Fehler, sondern zaehlt
                    //  nicht mit - sonst faellt eine Summe ueber eine Spalte mit
                    //  Kopfzeile immer aus.
                    if (!ok) return true;
                    if (zahlen == 0) { kleinst = groesst = d; }
                    else { kleinst = qMin(kleinst, d); groesst = qMax(groesst, d); }
                    ++zahlen;
                    summe += d;
                    produkt *= d;
                    return true;
                }))
                return fehler;
        }
        switch (fn) {
        case Fn::Summe:   return zahlWert(summe);
        case Fn::Produkt: return zahlWert(zahlen == 0 ? 0.0 : produkt);
        case Fn::Mittel:  return zahlen == 0 ? fehlerWert(kDiv) : zahlWert(summe / zahlen);
        case Fn::Min:     return zahlen == 0 ? zahlWert(0.0) : zahlWert(kleinst);
        case Fn::Max:     return zahlen == 0 ? zahlWert(0.0) : zahlWert(groesst);
        case Fn::Anzahl:  return zahlWert(zahlen);
        default:          return zahlWert(belegt);
        }
    }
    case Fn::Runden: {
        bool okA = false, okB = true;
        const double x = einzelZahl(0, &okA);
        const double n = args.size() > 1 ? einzelZahl(1, &okB) : 0.0;
        if (!okA || !okB) return fehlerWert(kWert);
        const double f = std::pow(10.0, qBound(-15.0, std::trunc(n), 15.0));
        return zahlWert(std::round(x * f) / f);
    }
    case Fn::Betrag: case Fn::Ganzzahl: case Fn::Wurzel: case Fn::Nicht: case Fn::Zahl: {
        bool ok = false;
        const double x = einzelZahl(0, &ok);
        if (!ok) return fehlerWert(kWert);
        if (fn == Fn::Betrag)   return zahlWert(std::abs(x));
        if (fn == Fn::Ganzzahl) return zahlWert(std::floor(x));
        if (fn == Fn::Nicht)    return zahlWert(x == 0.0 ? 1.0 : 0.0);
        if (fn == Fn::Zahl)     return zahlWert(x);
        return x < 0.0 ? fehlerWert(kWert) : zahlWert(std::sqrt(x));
    }
    case Fn::Potenz: case Fn::Rest: {
        bool okA = false, okB = false;
        const double x = einzelZahl(0, &okA);
        const double y = einzelZahl(1, &okB);
        if (!okA || !okB) return fehlerWert(kWert);
        if (fn == Fn::Potenz) return zahlWert(std::pow(x, y));
        if (y == 0.0) return fehlerWert(kDiv);
        return zahlWert(std::fmod(x, y));
    }
    case Fn::Wenn: {
        if (args.size() < 2) return fehlerWert(kWert);
        bool ok = false;
        const double c = einzelZahl(0, &ok);
        if (!ok) return fehlerWert(kWert);
        if (c != 0.0) return einzel(1);
        return args.size() > 2 ? einzel(2) : zahlWert(0.0);
    }
    case Fn::Und: case Fn::Oder: {
        bool wahr = (fn == Fn::Und);
        Wert fehler;
        for (const Arg& a : std::as_const(args)) {
            if (!jedeZelle(a, kette, [&](const Wert& w) {
                    if (w.art == Art::Fehler) { fehler = w; return false; }
                    bool ok = false;
                    const double d = alsZahlWert(w, &ok);
                    if (!ok) { fehler = fehlerWert(kWert); return false; }
                    if (fn == Fn::Und) wahr = wahr && (d != 0.0);
                    else               wahr = wahr || (d != 0.0);
                    return true;
                }))
                return fehler;
        }
        return zahlWert(wahr ? 1.0 : 0.0);
    }
    case Fn::Verketten: {
        QString out;
        Wert fehler;
        for (const Arg& a : std::as_const(args)) {
            if (!jedeZelle(a, kette, [&](const Wert& w) {
                    if (w.art == Art::Fehler) { fehler = w; return false; }
                    out += alsTextWert(w);
                    return out.size() <= kMaxFeldZeichen;
                })) {
                return fehler.art == Art::Fehler ? fehler : fehlerWert(kGrenze);
            }
        }
        return textWert(out);
    }
    case Fn::Laenge: case Fn::Gross: case Fn::Klein: case Fn::Glaetten: {
        const Wert w = einzel(0);
        if (w.art == Art::Fehler) return w;
        const QString s = alsTextWert(w);
        if (fn == Fn::Laenge)   return zahlWert(double(s.size()));
        if (fn == Fn::Gross)    return textWert(s.toUpper());
        if (fn == Fn::Klein)    return textWert(s.toLower());
        return textWert(s.trimmed());
    }
    case Fn::Links: case Fn::Rechts: case Fn::Teil: {
        const Wert w = einzel(0);
        if (w.art == Art::Fehler) return w;
        const QString s = alsTextWert(w);
        bool ok = true;
        if (fn == Fn::Teil) {
            bool okB = false;
            const double von = einzelZahl(1, &ok);
            const double len = einzelZahl(2, &okB);
            if (!ok || !okB) return fehlerWert(kWert);
            const qsizetype a = qBound<qsizetype>(0, qsizetype(von) - 1, s.size());
            return textWert(s.mid(a, qMax<qsizetype>(0, qsizetype(len))));
        }
        const double n = args.size() > 1 ? einzelZahl(1, &ok) : 1.0;
        if (!ok) return fehlerWert(kWert);
        const qsizetype k = qBound<qsizetype>(0, qsizetype(n), s.size());
        return textWert(fn == Fn::Links ? s.left(k) : s.right(k));
    }
    }
    return fehlerWert(kName);
}

Wert Rechner::primaer(Lauf& l) {
    if (l.tiefe >= kMaxTiefe) return fehlerWert(kSyntax);
    ueberspringe(l);
    if (l.i >= l.s.size()) return fehlerWert(kSyntax);

    const QChar c = l.s[l.i];

    if (c == u'(') {
        ++l.i;
        ++l.tiefe;
        const Wert w = vergleich(l);
        --l.tiefe;
        if (w.art == Art::Fehler) return w;
        if (jetzt(l) != u')') return fehlerWert(kSyntax);
        ++l.i;
        return w;
    }

    if (c == u'"') {
        ++l.i;
        QString out;
        while (l.i < l.s.size()) {
            const QChar q = l.s[l.i];
            if (q == u'"') {
                if (l.i + 1 < l.s.size() && l.s[l.i + 1] == u'"') { out += q; l.i += 2; continue; }
                ++l.i;
                return textWert(out);
            }
            out += q;
            ++l.i;
        }
        return fehlerWert(kSyntax);
    }

    if (c.isDigit() || c == u'.') {
        QString roh;
        while (l.i < l.s.size()) {
            const QChar d = l.s[l.i];
            if (d.isDigit() || d == u'.') { roh += d; ++l.i; continue; }
            //  Ein Komma ZWISCHEN Ziffern ist ein Dezimalzeichen, sonst trennt
            //  es Argumente. Anders liesse sich weder `=1,5*2` noch `SUM(A1,A2)`
            //  schreiben, und beide Schreibweisen kommen im Bestand vor.
            if (d == u',' && l.i + 1 < l.s.size() && l.s[l.i + 1].isDigit()
                && !roh.isEmpty() && roh.back().isDigit()) {
                roh += QLatin1Char('.');
                ++l.i;
                continue;
            }
            break;
        }
        bool ok = false;
        const double d = roh.toDouble(&ok);
        return ok ? zahlWert(d) : fehlerWert(kSyntax);
    }

    if (c.isLetter() || c == u'$') {
        //  Ein Name vor einer Klammer ist ein Aufruf, alles andere ein Bezug.
        const qsizetype merk = l.i;
        QString name;
        qsizetype i = l.i;
        while (i < l.s.size() && (l.s[i].isLetter() || l.s[i] == u'_')) { name += l.s[i]; ++i; }
        if (!name.isEmpty()) {
            qsizetype j = i;
            while (j < l.s.size() && l.s[j].isSpace()) ++j;
            if (j < l.s.size() && l.s[j] == u'(') {
                const auto it = funktionen().constFind(name.toUpper());
                if (it == funktionen().cend()) return fehlerWert(kName);
                l.i = j;
                ++l.tiefe;
                const Wert w = aufruf(l, it.value());
                --l.tiefe;
                return w;
            }
        }
        int z = 0, s = 0;
        l.i = merk;
        if (!bezug(l, &z, &s)) return fehlerWert(kSyntax);
        return zelle(z, s, l.kette);
    }

    return fehlerWert(kSyntax);
}

}  // namespace

bool istFormel(const QString& text) {
    return text.size() > 1 && text.at(0) == u'=';
}

QString spaltenName(int spalte) {
    if (spalte < 0) return {};
    QString out;
    int n = spalte + 1;
    while (n > 0) {
        const int rest = (n - 1) % 26;
        out.prepend(QChar(u'A' + rest));
        n = (n - 1) / 26;
    }
    return out;
}

QList<qint64> sammleFormeln(const QList<Zeile>& zeilen, int von, int bis) {
    QList<qint64> out;
    const int ende = qMin(bis, int(zeilen.size()));
    for (int z = qMax(0, von); z < ende; ++z)
        for (const auto& f : zeilen.at(z).belegte())
            if (istFormel(f.second)) out.append(zellSchluessel(z, f.first));
    return out;
}

std::shared_ptr<const Werte> rechne(const QList<Zeile>& zeilen,
                                    const QList<FormelBereich>& bereiche, bool komma) {
    auto w = std::make_shared<Werte>();
    for (const FormelBereich& b : bereiche) {
        if (b.zellen.isEmpty()) continue;
        //  Je Block ein eigener Rechner: er traegt den Bezugspunkt und den
        //  Zugriffsdeckel, und beides gilt nur innerhalb seines Blocks.
        Rechner r(zeilen, b.daten, b.bis, komma);
        for (qint64 k : b.zellen) {
            const int z = int(k >> 20);
            const int s = int(k & 0xFFFFF);
            w->setze(z, s, r.anzeige(z, s));
        }
    }
    return w;
}

bool dezimalKomma(const QList<Zeile>& zeilen, int von, int bis, QChar trenner) {
    const int ende = qMin(bis, int(zeilen.size()));
    int komma = 0, punkt = 0, gesehen = 0;
    for (int z = qMax(0, von); z < ende && gesehen < kMaxProbe; ++z) {
        for (const auto& f : zeilen.at(z).belegte()) {
            const QString& t = f.second;
            const qsizetype k = t.lastIndexOf(QLatin1Char(','));
            const qsizetype p = t.lastIndexOf(QLatin1Char('.'));
            if (k < 0 || p < 0) continue;
            bool ok = false;
            alsZahl(t, &ok);
            if (!ok) continue;
            ++gesehen;
            if (k > p) ++komma; else ++punkt;
        }
    }
    if (komma != punkt) return komma > punkt;
    return trenner == u';';
}

}  // namespace mg::table
