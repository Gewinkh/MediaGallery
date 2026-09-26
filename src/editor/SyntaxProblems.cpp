#include "editor/SyntaxProblems.h"

#include "editor/SyntaxScanner.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QTextBlock>
#include <QTextDocument>
#include <QXmlStreamReader>

namespace mg::editor {
namespace {

//  Notbremse gegen eine entartete Datei.
constexpr int kMaxFundstellen = 200;

QStringView endungVon(const QString& pfad) {
    const qsizetype punkt = pfad.lastIndexOf(u'.');
    const qsizetype trenner = qMax(pfad.lastIndexOf(u'/'), pfad.lastIndexOf(u'\\'));
    if (punkt < 0 || punkt < trenner) return {};
    return QStringView(pfad).mid(punkt + 1);
}

bool endungIst(const QString& pfad, QLatin1StringView was) {
    return endungVon(pfad).compare(was, Qt::CaseInsensitive) == 0;
}

QChar partnerVon(QChar zu) {
    if (zu == u')') return u'(';
    if (zu == u']') return u'[';
    return u'{';
}

void stelleZu(const QTextDocument* doc, int position, int& block, int& spalte) {
    const QTextBlock b = doc->findBlock(qMax(0, position));
    block = b.isValid() ? b.blockNumber() : 0;
    spalte = b.isValid() ? qMax(0, position - b.position()) : 0;
}

//  Klammern. Gemeldet wird HOECHSTENS EINE Stelle: steht das Paar erst einmal
//  schief, ist alles danach Folgerauschen.
void pruefeKlammern(const QList<QStringView>& zeilen, const LanguageDef& def,
                    QList<Problem>& raus) {
    struct Offen { QChar zeichen; int block; int spalte; };
    QList<Offen> stapel;
    //  Je offener `#if`-Ebene der Stapel vor dem Zweig; ein `#else` setzt ihn zurueck.
    QList<QList<Offen>> praepStapel;
    SpanList spans;
    int zustand = 0;

    for (int i = 0; i < zeilen.size(); ++i) {
        const QStringView z = zeilen.at(i);
        zustand = scanLine(z, def, zustand, spans);
        if (def.preprocHash) {
            switch (praepVon(z, spans)) {
            case Praep::Wenn:
                if (praepStapel.size() < kMaxPraepTiefe) praepStapel.append(stapel);
                continue;
            case Praep::Sonst:
                if (!praepStapel.isEmpty()) stapel = praepStapel.last();
                continue;
            case Praep::Ende:
                if (!praepStapel.isEmpty()) praepStapel.removeLast();
                continue;
            case Praep::Andere:
                continue;
            case Praep::Keine:
                break;
            }
        }

        for (int k = 0; k < z.size(); ++k) {
            const QChar c = z.at(k);
            const bool auf  = (c == u'(' || c == u'[' || c == u'{');
            const bool zu   = (c == u')' || c == u']' || c == u'}');
            if (!auf && !zu) continue;
            if (inStringOrComment(spans, k)) continue;

            if (auf) {
                stapel.append({ c, i, k });
                continue;
            }
            if (stapel.isEmpty()) {
                raus.append({ i, k, 1, ProblemKind::UnmatchedClose, {} });
                return;
            }
            if (stapel.last().zeichen != partnerVon(c)) {
                raus.append({ i, k, 1, ProblemKind::MismatchedClose, {} });
                return;
            }
            stapel.removeLast();
        }
    }
    if (!stapel.isEmpty()) {
        const Offen& o = stapel.first();
        raus.append({ o.block, o.spalte, 1, ProblemKind::UnmatchedOpen, {} });
    }
}

//  Gemeldet wird die Zeile, in der sie BEGINNT - das Dateiende sagt niemandem,
//  wo der Fehler sitzt.
void pruefeOffeneBloecke(const QList<QStringView>& zeilen, const LanguageDef& def,
                         QList<Problem>& raus) {
    SpanList spans;
    int zustand = 0;
    int startBlock = -1;
    int startSpalte = 0;
    BlockState offen = BlockState::None;

    for (int i = 0; i < zeilen.size(); ++i) {
        const int vorher = zustand;
        zustand = scanLine(zeilen.at(i), def, zustand, spans);
        const BlockState davor = stateKind(vorher);
        const BlockState danach = stateKind(zustand);
        if (davor == danach) continue;
        if (danach == BlockState::BlockComment || danach == BlockState::MultiString) {
            startBlock = i;
            offen = danach;
            //  Dort beginnt die letzte gefaerbte Gruppe, also der Oeffner.
            startSpalte = spans.isEmpty() ? 0 : spans.back().start;
        } else if (danach == BlockState::None) {
            startBlock = -1;
        }
    }
    if (startBlock < 0) return;
    raus.append({ startBlock, startSpalte, 1,
                  offen == BlockState::BlockComment ? ProblemKind::UnterminatedComment
                                                    : ProblemKind::UnterminatedString,
                  {} });
}

//  JSON hat eine geschlossene Grammatik - hier ist „falsch" keine Vermutung.
void pruefeJson(const QTextDocument* doc, const QString& text, QList<Problem>& raus) {
    QJsonParseError fehler{};
    const QByteArray utf8 = text.toUtf8();
    QJsonDocument::fromJson(utf8, &fehler);
    if (fehler.error == QJsonParseError::NoError) return;
    //  `offset` zaehlt BYTES der UTF-8-Form, die Anzeige zaehlt Zeichen.
    const int stelle = int(QString::fromUtf8(utf8.left(fehler.offset)).size());
    int block = 0, spalte = 0;
    stelleZu(doc, stelle, block, spalte);
    raus.append({ block, spalte, 1, ProblemKind::Format, fehler.errorString() });
}

//  Nur die XML-Familie, NICHT HTML: HTML laesst `<br>` und `<li>` offen, und
//  ein Wohlgeformtheits-Leser meldete dort reihenweise Fehler, die keine sind.
void pruefeXml(const QString& text, QList<Problem>& raus) {
    QXmlStreamReader leser(text);
    while (!leser.atEnd()) leser.readNext();
    if (!leser.hasError()) return;
    raus.append({ int(qMax(qint64(1), leser.lineNumber())) - 1,
                  int(qMax(qint64(1), leser.columnNumber())) - 1, 1,
                  ProblemKind::Format, leser.errorString() });
}

//  Nur eine Abschnittsklammer ohne Ende: in einem Wert darf jedes Zeichen
//  stehen, und TOML schreibt ueber mehrere Zeilen.
void pruefeIni(const QList<QStringView>& zeilen, QList<Problem>& raus) {
    for (int i = 0; i < zeilen.size() && raus.size() < kMaxFundstellen; ++i) {
        const QStringView voll = zeilen.at(i);
        const QStringView z = voll.trimmed();
        if (!z.startsWith(u'[') || z.endsWith(u']')) continue;
        raus.append({ i, int(voll.size()) - int(z.size()), 1,
                      ProblemKind::UnmatchedOpen, {} });
    }
}

//  Tabulator NACH einem Leerzeichen in der Einrueckung: gemischt ergibt sie je
//  nach Tabulatorbreite eine andere Struktur, Python lehnt das selbst ab.
void pruefeEinrueckung(const QList<QStringView>& zeilen, QList<Problem>& raus) {
    for (int i = 0; i < zeilen.size() && raus.size() < kMaxFundstellen; ++i) {
        const QStringView z = zeilen.at(i);
        bool leerzeichen = false;
        for (int k = 0; k < z.size(); ++k) {
            const QChar c = z.at(k);
            if (c == u' ') { leerzeichen = true; continue; }
            if (c == u'\t') {
                if (leerzeichen) { raus.append({ i, k, 1, ProblemKind::MixedIndent, {} }); }
                break;
            }
            break;
        }
    }
}

}  // namespace

bool touchesProblems(QStringView eingefuegt) {
    for (const QChar c : eingefuegt) {
        switch (c.unicode()) {
        case u'(': case u')': case u'[': case u']': case u'{': case u'}':
        case u'"': case u'\'': case u'*': case u'/': case u'<': case u'>':
        case u',': case u':': case u'\t': case u'\n': case u'\\':
            return true;
        default:
            break;
        }
    }
    return false;
}

QList<Problem> scanProblems(const QTextDocument* doc, const LanguageDef& def,
                            const QString& pfad) {
    QList<Problem> raus;
    if (!doc || doc->characterCount() > kMaxZeichen) return raus;

    //  `QTextBlock::text()` gibt eine KOPIE zurueck - erst halten, dann die
    //  Sichten bauen, sonst zeigten sie ins Leere.
    QList<QString> halter;
    halter.reserve(doc->blockCount());
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) halter.append(b.text());
    QList<QStringView> zeilen;
    zeilen.reserve(halter.size());
    for (const QString& s : halter) zeilen.append(QStringView(s));

    //  Hat die Datei einen EIGENEN Leser, entscheidet der allein: er nennt die
    //  Stelle genauer als die Klammernpruefung.
    const bool istJson = endungIst(pfad, QLatin1StringView("json"));
    const bool istXml  = endungIst(pfad, QLatin1StringView("xml"))
                      || endungIst(pfad, QLatin1StringView("qrc"))
                      || endungIst(pfad, QLatin1StringView("svg"));
    if (istJson) {
        pruefeJson(doc, doc->toPlainText(), raus);
    } else if (istXml) {
        pruefeXml(doc->toPlainText(), raus);
    } else if (def.id == QLatin1StringView("ini")) {
        pruefeIni(zeilen, raus);
    } else if (def.kind == ScannerKind::CLike || def.kind == ScannerKind::Script) {
        //  Sonst die allgemeine Pruefung - aber nur dort, wo Klammern Struktur
        //  tragen. In Klartext und Markdown ist eine Klammer ein Satzzeichen.
        pruefeKlammern(zeilen, def, raus);
        pruefeOffeneBloecke(zeilen, def, raus);
    }

    if (def.id == QLatin1StringView("python") || def.id == QLatin1StringView("yaml"))
        pruefeEinrueckung(zeilen, raus);

    if (raus.size() > kMaxFundstellen) raus.resize(kMaxFundstellen);
    return raus;
}

}  // namespace mg::editor
