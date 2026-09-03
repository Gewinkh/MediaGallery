#include "editor/FoldScanner.h"

#include "editor/SyntaxScanner.h"

#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <iterator>

using namespace Qt::Literals::StringLiterals;

namespace mg::editor {
namespace {

//  Notbremse: mehr Bereiche als das kann niemand mehr bedienen, und die Liste
//  soll auch bei einer entarteten Datei nicht ins Uferlose wachsen.
constexpr int kMaxBereiche = 50000;


//  Einrueckung in Spalten; -1 fuer eine Zeile, die nur aus Leerraum besteht
//  (sie gehoert immer zum umgebenden Block, egal wie tief sie „steht").
int einzugVon(QStringView z, int tabWidth) {
    int spalte = 0;
    for (const QChar c : z) {
        if (c == u'\t')      spalte += tabWidth - (spalte % tabWidth);
        else if (c == u' ')  ++spalte;
        else                 return spalte;
    }
    return -1;
}

//  Rang einer Markdown-Ueberschrift (1..6), 0 wenn keine.
int ueberschriftsRang(QStringView z) {
    int i = 0;
    while (i < z.size() && (z.at(i) == u' ' || z.at(i) == u'\t')) ++i;
    int rang = 0;
    while (i + rang < z.size() && z.at(i + rang) == u'#') ++rang;
    if (rang < 1 || rang > 6) return 0;
    const int nach = i + rang;
    if (nach < z.size() && z.at(nach) != u' ') return 0;
    return rang;
}

bool istZaun(QStringView z) {
    QStringView t = z.trimmed();
    return t.startsWith(QLatin1String("```")) || t.startsWith(QLatin1String("~~~"));
}

bool istAbschnitt(QStringView z) {
    QStringView t = z.trimmed();
    return t.size() >= 2 && t.startsWith(u'[') && t.endsWith(u']');
}

//  Leerzeilen am Ende eines Bereichs gehoeren nicht dazu - sonst zoege ein
//  zugeklappter Abschnitt die Luft vor dem naechsten mit ein.
int ohneLeerzeilenAmEnde(const QList<QStringView>& zeilen, int start, int ende) {
    while (ende > start && zeilen.at(ende).trimmed().isEmpty()) --ende;
    return ende;
}

//  Leerraum-Sprung.
bool istWortzeichen(QChar c) { return c.isLetterOrNumber() || c == u'_'; }

}  // namespace

bool touchesFolding(QStringView eingefuegt, FoldKind kind) {
    switch (kind) {
    case FoldKind::None:     return false;
    case FoldKind::Braces:
        for (const QChar c : eingefuegt)
            if (c == u'{' || c == u'}' || c == u'\n' || c == u'"' || c == u'\''
                || c == u'/' || c == u'*' || c == u'#')
                return true;
        return false;
    case FoldKind::Headings:
        for (const QChar c : eingefuegt)
            if (c == u'#' || c == u'\n' || c == u'`' || c == u'~') return true;
        return false;
    case FoldKind::Sections:
        for (const QChar c : eingefuegt)
            if (c == u'[' || c == u']' || c == u'\n') return true;
        return false;
    case FoldKind::Tags:
        for (const QChar c : eingefuegt)
            if (c == u'<' || c == u'>' || c == u'/' || c == u'\n') return true;
        return false;
    case FoldKind::Keywords:
        //  Jedes Wortzeichen kann ein Schluesselwort vervollstaendigen - hier
        //  laesst sich nur der reine Satzzeichen-Fall sparen.
        for (const QChar c : eingefuegt)
            if (istWortzeichen(c) || c == u'\n') return true;
        return false;
    case FoldKind::Indent:
        //  Bei Einrueckung aendert JEDER Umbruch und jedes Leerzeichen am
        //  Zeilenanfang die Struktur - hier laesst sich nichts sparen.
        for (const QChar c : eingefuegt)
            if (c == u'\n' || c == u' ' || c == u'\t') return true;
        return false;
    }
    return false;
}

QList<FoldRegion> scanFolds(const QTextDocument* doc, const LanguageDef& def,
                            int tabWidth) {
    QList<FoldRegion> raus;
    if (!doc || def.fold == FoldKind::None) return raus;

    //  Der Text wird EINMAL als Sichten eingesammelt; die Bloecke selbst liefern
    //  ihre Zeichenketten ohnehin ohne Kopie.
    QList<QString> halten;
    QList<QStringView> zeilen;
    halten.reserve(doc->blockCount());
    zeilen.reserve(doc->blockCount());
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        halten.append(b.text());
        zeilen.append(halten.last());
    }
    const int n = int(zeilen.size());
    if (n < 2) return raus;

    switch (def.fold) {
    case FoldKind::None:
        break;

    case FoldKind::Braces: {
        QList<int> stapel;
        SpanList spans;
        int zustand = 0;
        for (int i = 0; i < n; ++i) {
            zustand = scanLine(zeilen.at(i), def, zustand, spans);
            const QStringView z = zeilen.at(i);
            for (int k = 0; k < z.size(); ++k) {
                const QChar c = z.at(k);
                if (c != u'{' && c != u'}') continue;
                if (inStringOrComment(spans, k)) continue;
                if (c == u'{') {
                    stapel.append(i);
                } else if (!stapel.isEmpty()) {
                    const int start = stapel.takeLast();
                    if (i > start) raus.append({ start, i });
                    if (raus.size() >= kMaxBereiche) return raus;
                }
            }
        }
        break;
    }

    case FoldKind::Indent: {
        QList<int> einzug;
        einzug.reserve(n);
        for (const QStringView& z : zeilen) einzug.append(einzugVon(z, tabWidth));

        for (int i = 0; i < n; ++i) {
            if (einzug.at(i) < 0) continue;
            int j = i + 1;
            while (j < n && einzug.at(j) < 0) ++j;
            if (j >= n || einzug.at(j) <= einzug.at(i)) continue;

            int letzte = j;
            for (int k = j; k < n; ++k) {
                if (einzug.at(k) < 0) continue;
                if (einzug.at(k) <= einzug.at(i)) break;
                letzte = k;
            }
            if (letzte > i) raus.append({ i, letzte });
            if (raus.size() >= kMaxBereiche) return raus;
        }
        break;
    }

    case FoldKind::Headings: {
        //  Codezaeune zuerst: eine `#`-Zeile IN einem Zaun ist keine Ueberschrift.
        QList<bool> imZaun(n, false);
        int zaunAuf = -1;
        for (int i = 0; i < n; ++i) {
            if (!istZaun(zeilen.at(i))) continue;
            if (zaunAuf < 0) {
                zaunAuf = i;
            } else {
                for (int k = zaunAuf; k <= i; ++k) imZaun[k] = true;
                if (i > zaunAuf) raus.append({ zaunAuf, i });
                zaunAuf = -1;
            }
        }

        QList<int> rang;
        rang.reserve(n);
        for (int i = 0; i < n; ++i)
            rang.append(imZaun.at(i) ? 0 : ueberschriftsRang(zeilen.at(i)));

        for (int i = 0; i < n; ++i) {
            if (rang.at(i) == 0) continue;
            int ende = n - 1;
            for (int k = i + 1; k < n; ++k)
                if (rang.at(k) > 0 && rang.at(k) <= rang.at(i)) { ende = k - 1; break; }
            ende = ohneLeerzeilenAmEnde(zeilen, i, ende);
            if (ende > i) raus.append({ i, ende });
            if (raus.size() >= kMaxBereiche) return raus;
        }
        break;
    }

    case FoldKind::Tags: {
        //  Ein Stapel offener Tags. Leere Elemente (`<br>`, `<img>`) und
        //  selbstschliessende (`… />`) eroeffnen nichts; ein `</x>` ohne
        //  Partner wird verworfen, statt den Stapel zu zerlegen - echtes HTML
        //  ist selten sauber geschachtelt.
        static constexpr QLatin1StringView kLeer[] = {
            "area"_L1, "base"_L1, "br"_L1, "col"_L1, "embed"_L1, "hr"_L1, "img"_L1,
            "input"_L1, "link"_L1, "meta"_L1, "source"_L1, "track"_L1, "wbr"_L1
        };
        const WordList leere{ kLeer, int(std::size(kLeer)) };

        QList<QPair<QString, int>> stapel;      // Tagname, Startzeile
        SpanList spans;
        int zustand = 0;
        for (int i = 0; i < n; ++i) {
            zustand = scanLine(zeilen.at(i), def, zustand, spans);
            const QStringView z = zeilen.at(i);
            for (int k = 0; k < z.size(); ++k) {
                if (z.at(k) != u'<') continue;
                if (inStringOrComment(spans, k)) continue;
                int p = k + 1;
                const bool schliessend = (p < z.size() && z.at(p) == u'/');
                if (schliessend) ++p;
                //  Verarbeitungsanweisungen und Kommentare gehen uns nichts an.
                if (p < z.size() && (z.at(p) == u'!' || z.at(p) == u'?')) continue;
                const int namensAnfang = p;
                while (p < z.size() && (istWortzeichen(z.at(p)) || z.at(p) == u'-'
                                        || z.at(p) == u':'))
                    ++p;
                if (p == namensAnfang) continue;
                const QString name = z.mid(namensAnfang, p - namensAnfang).toString().toLower();

                //  Bis zum Ende des Tags schauen: endet es auf `/>`, ist es
                //  selbstschliessend.
                int ende = p;
                while (ende < z.size() && z.at(ende) != u'>') ++ende;
                const bool selbst = ende > 0 && ende <= z.size() - 1
                                    && z.at(ende - 1) == u'/';

                if (schliessend) {
                    for (int t = int(stapel.size()) - 1; t >= 0; --t) {
                        if (stapel.at(t).first != name) continue;
                        const int start = stapel.at(t).second;
                        if (i > start) raus.append({ start, i });
                        stapel.remove(t, int(stapel.size()) - t);
                        break;
                    }
                } else if (!selbst && !containsWord(leere, name)) {
                    stapel.append({ name, i });
                }
                if (raus.size() >= kMaxBereiche) return raus;
                k = qMax(k, ende);
            }
        }
        break;
    }

    case FoldKind::Keywords: {
        //  Wie die Klammern, nur mit WOERTERN: je Zeile zaehlen, was oeffnet
        //  und was schliesst. Ein einzeiliges `if x then y end` hebt sich damit
        //  von selbst auf und ergibt keinen Bereich.
        QList<int> stapel;
        SpanList spans;
        int zustand = 0;
        for (int i = 0; i < n; ++i) {
            zustand = scanLine(zeilen.at(i), def, zustand, spans);
            const QStringView z = zeilen.at(i);
            int k = 0;
            while (k < z.size()) {
                if (!istWortzeichen(z.at(k))) { ++k; continue; }
                const int von = k;
                while (k < z.size() && istWortzeichen(z.at(k))) ++k;
                if (inStringOrComment(spans, von)) continue;
                const QStringView wort = z.mid(von, k - von);
                if (containsWord(def.foldOpen, wort)) {
                    stapel.append(i);
                } else if (containsWord(def.foldClose, wort) && !stapel.isEmpty()) {
                    const int start = stapel.takeLast();
                    if (i > start) raus.append({ start, i });
                    if (raus.size() >= kMaxBereiche) return raus;
                }
            }
        }
        break;
    }

    case FoldKind::Sections: {
        for (int i = 0; i < n; ++i) {
            if (!istAbschnitt(zeilen.at(i))) continue;
            int ende = n - 1;
            for (int k = i + 1; k < n; ++k)
                if (istAbschnitt(zeilen.at(k))) { ende = k - 1; break; }
            ende = ohneLeerzeilenAmEnde(zeilen, i, ende);
            if (ende > i) raus.append({ i, ende });
            if (raus.size() >= kMaxBereiche) return raus;
        }
        break;
    }
    }

    //  Nach Startzeile sortiert - die Leiste sucht darin mit Binaersuche.
    std::sort(raus.begin(), raus.end(), [](const FoldRegion& a, const FoldRegion& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.end > b.end;                 // der weitere zuerst
    });
    return raus;
}

}  // namespace mg::editor
