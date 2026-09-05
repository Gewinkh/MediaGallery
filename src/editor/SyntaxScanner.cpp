#include "editor/SyntaxScanner.h"

#include <QChar>

// EINE Routine (`scanCode`) bedient CLike UND Script - sie unterscheiden sich nur in Feldern der Tabelle, nicht
// im Ablauf. Nur Markdown, Markup und INI brauchen einen eigenen Weg; eine neue Klammer-Sprache ändert hier nichts.
namespace mg::editor {
namespace {

inline bool istWortAnfang(QChar c) { return c.isLetter() || c == u'_'; }
inline bool istWortZeichen(QChar c) { return c.isLetterOrNumber() || c == u'_'; }
inline bool istHexZiffer(QChar c) {
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')
        || (c >= u'A' && c <= u'F');
}

//  Satzzeichen, die als Operator gefaerbt werden. Bewusst eine Aufzaehlung und
//  kein `!isLetterOrNumber()`: sonst faerbte jedes Leerzeichen mit.
inline bool istOperator(QChar c) {
    switch (c.unicode()) {
    case '+': case '-': case '*': case '/': case '%': case '=': case '<':
    case '>': case '!': case '&': case '|': case '^': case '~': case '?':
    case ':': case ';': case ',': case '.': case '(': case ')': case '[':
    case ']': case '{': case '}': case '@': case '#': case '\\':
        return true;
    default:
        return false;
    }
}

inline void schiebe(SpanList& out, qsizetype start, qsizetype len, Tok t) {
    if (len > 0) out.push_back(Span{ start, len, t });
}

//  Passt `was` an Position `i`? Leeres `was` passt nie (sonst traefe jede
//  Sprache ohne Blockkommentar an jeder Stelle).
inline bool passt(QStringView zeile, qsizetype i, QLatin1StringView was) {
    if (was.isEmpty() || i + was.size() > zeile.size()) return false;
    return zeile.sliced(i, was.size()).compare(was) == 0;
}

// Liefert die Position NACH der Zeichenkette; Rückschrägstrich hebt das nächste Zeichen auf. Endet die Zeile
// vorher, hört sie am Zeilenende auf - einen Tippfehler über das halbe Dokument einzufärben hilft niemandem.
qsizetype ueberspringeZeichenkette(QStringView zeile, qsizetype i) {
    const QChar begrenzer = zeile[i];
    qsizetype j = i + 1;
    while (j < zeile.size()) {
        if (zeile[j] == u'\\') { j += 2; continue; }
        if (zeile[j] == begrenzer) return j + 1;
        ++j;
    }
    return zeile.size();
}

//  Zahl ab `i`. Deckt 42, 0x2A, 0b1011, 0o17, 3.14, 1e-9, 1'000'000, 1_000,
//  sowie angehaengte Endungen (f, u, ull, L) ab.
qsizetype ueberspringeZahl(QStringView zeile, qsizetype i) {
    qsizetype j = i;
    if (zeile[j] == u'0' && j + 1 < zeile.size()) {
        const QChar art = zeile[j + 1].toLower();
        if (art == u'x' || art == u'b' || art == u'o') {
            j += 2;
            while (j < zeile.size() && (zeile[j].isLetterOrNumber()
                                        || zeile[j] == u'\'' || zeile[j] == u'_'))
                ++j;
            return j;
        }
    }
    bool punktGesehen = false;
    while (j < zeile.size()) {
        const QChar c = zeile[j];
        if (c.isDigit() || c == u'\'' || c == u'_') { ++j; continue; }
        if (c == u'.' && !punktGesehen && j + 1 < zeile.size() && zeile[j + 1].isDigit()) {
            punktGesehen = true; ++j; continue;
        }
        if ((c == u'e' || c == u'E') && j + 1 < zeile.size()
            && (zeile[j + 1].isDigit() || zeile[j + 1] == u'+' || zeile[j + 1] == u'-')) {
            j += 2; continue;
        }
        if (c.isLetter()) { ++j; continue; }   // Endung (f, u, L, px …)
        break;
    }
    return j;
}

// Steht vor Position `i` nichts außer Leerraum, Bezeichnern und Punkten? Damit trennt sich eine
// QML-Eigenschaftsbindung von einer Bedingung (`a ? b : c`) - ohne die Zeile zu parsen.
bool nurBezeichnerDavor(QStringView zeile, qsizetype i) {
    for (qsizetype k = 0; k < i; ++k) {
        const QChar c = zeile[k];
        if (c.isSpace() || c == u'.' || istWortZeichen(c)) continue;
        return false;
    }
    return true;
}

//  Endet die Punkt-Kette ab `i` auf einem Doppelpunkt? Fuer QML: bei
//  `anchors.fill: parent` soll die GANZE Kette die Farbe der Eigenschaft
//  tragen, nicht nur ihr letztes Glied.
bool kettenEndeIstDoppelpunkt(QStringView zeile, qsizetype i) {
    while (i < zeile.size() && (istWortZeichen(zeile[i]) || zeile[i] == u'.')) ++i;
    while (i < zeile.size() && zeile[i].isSpace()) ++i;
    return i < zeile.size() && zeile[i] == u':';
}

//  Naechstes sichtbares Zeichen ab `i` - fuer „Bezeichner vor einer Klammer ist
//  ein Funktionsname".
QChar naechstesSichtbares(QStringView zeile, qsizetype i) {
    while (i < zeile.size() && zeile[i].isSpace()) ++i;
    return i < zeile.size() ? zeile[i] : QChar(u'\0');
}

int scanCode(QStringView zeile, const LanguageDef& def, int stateIn, SpanList& out) {
    qsizetype i = 0;

    switch (stateKind(stateIn)) {
    case BlockState::BlockComment: {
        const qsizetype ende = def.blockClose.isEmpty()
                                   ? -1 : zeile.indexOf(def.blockClose);
        if (ende < 0) {
            schiebe(out, 0, zeile.size(), Tok::Comment);
            return stateIn;                          // laeuft weiter
        }
        const qsizetype nach = ende + def.blockClose.size();
        schiebe(out, 0, nach, Tok::Comment);
        i = nach;
        break;
    }
    case BlockState::MultiString: {
        // Nutzlast: 0 = R"( ... )", sonst der Code des Begrenzerzeichens einer dreifachen Anführung. Die 1 steht für die
        // Vorlagenzeichenkette mit Gravis - als Zeichencode nie gültig, also passt der dritte Fall in dieselbe Nutzlast.
        const int nutz = statePayload(stateIn);
        const QString schluss = nutz == 0 ? QStringLiteral(")\"")
                              : nutz == 1 ? QStringLiteral("`")
                                          : QString(3, QChar(char16_t(nutz)));
        const qsizetype ende = zeile.indexOf(schluss);
        if (ende < 0) {
            schiebe(out, 0, zeile.size(), Tok::String);
            return stateIn;
        }
        const qsizetype nach = ende + schluss.size();
        schiebe(out, 0, nach, Tok::String);
        i = nach;
        break;
    }
    default:
        break;
    }

    if (i == 0) {
        qsizetype k = 0;
        while (k < zeile.size() && zeile[k].isSpace()) ++k;
        if (k < zeile.size() && zeile[k] == u'#') {
            const bool shebang = (k == 0 && k + 1 < zeile.size() && zeile[1] == u'!');
            //  In Skriptsprachen ist '#' ein KOMMENTAR - nur wo die Tabelle
            //  `preprocHash` setzt (C/C++), ist es eine Direktive.
            if (shebang) {
                schiebe(out, 0, zeile.size(), Tok::Preproc);
                return makeState(BlockState::None);
            }
            if (def.preprocHash) {
                qsizetype j = k + 1;
                while (j < zeile.size() && zeile[j].isSpace()) ++j;
                while (j < zeile.size() && istWortZeichen(zeile[j])) ++j;
                schiebe(out, k, j - k, Tok::Preproc);
                i = j;
            }
        }
    }

    while (i < zeile.size()) {
        const QChar c = zeile[i];

        if (c.isSpace()) { ++i; continue; }

        if (passt(zeile, i, def.lineComment) || passt(zeile, i, def.lineComment2)) {
            schiebe(out, i, zeile.size() - i, Tok::Comment);
            return makeState(BlockState::None);
        }

        if (passt(zeile, i, def.blockOpen)) {
            const qsizetype ende = zeile.indexOf(def.blockClose, i + def.blockOpen.size());
            if (ende < 0) {
                schiebe(out, i, zeile.size() - i, Tok::Comment);
                return makeState(BlockState::BlockComment);
            }
            const qsizetype nach = ende + def.blockClose.size();
            schiebe(out, i, nach - i, Tok::Comment);
            i = nach;
            continue;
        }

        if (def.rawStrings && c == u'R' && i + 2 < zeile.size()
            && zeile[i + 1] == u'"' && zeile[i + 2] == u'(') {
            const qsizetype ende = zeile.indexOf(QStringLiteral(")\""), i + 3);
            if (ende < 0) {
                schiebe(out, i, zeile.size() - i, Tok::String);
                return makeState(BlockState::MultiString, 0);
            }
            schiebe(out, i, ende + 2 - i, Tok::String);
            i = ende + 2;
            continue;
        }

        if (def.templateStrings && c == u'`') {
            qsizetype j = i + 1;
            while (j < zeile.size() && zeile[j] != u'`') {
                if (zeile[j] == u'\\') ++j;       // Fluchtzeichen ueberspringen
                ++j;
            }
            if (j >= zeile.size()) {
                schiebe(out, i, zeile.size() - i, Tok::String);
                return makeState(BlockState::MultiString, 1);
            }
            schiebe(out, i, j + 1 - i, Tok::String);
            i = j + 1;
            continue;
        }

        if (def.tripleQuotes && (c == u'"' || c == u'\'')
            && i + 2 < zeile.size() && zeile[i + 1] == c && zeile[i + 2] == c) {
            const QString schluss(3, c);
            const qsizetype ende = zeile.indexOf(schluss, i + 3);
            if (ende < 0) {
                schiebe(out, i, zeile.size() - i, Tok::String);
                return makeState(BlockState::MultiString, int(c.unicode()));
            }
            schiebe(out, i, ende + 3 - i, Tok::String);
            i = ende + 3;
            continue;
        }

        if (c == u'"' || c == u'\'' || c == u'`') {
            const qsizetype nach = ueberspringeZeichenkette(zeile, i);
            schiebe(out, i, nach - i, Tok::String);
            i = nach;
            continue;
        }

        if (def.hashColors && c == u'#') {
            qsizetype j = i + 1;
            while (j < zeile.size() && istHexZiffer(zeile[j])) ++j;
            if (j > i + 1) {
                schiebe(out, i, j - i, Tok::Number);
                i = j;
                continue;
            }
        }

        if (c.isDigit() && (i == 0 || !istWortZeichen(zeile[i - 1]))) {
            const qsizetype nach = ueberspringeZahl(zeile, i);
            schiebe(out, i, nach - i, Tok::Number);
            i = nach;
            continue;
        }

        if (istWortAnfang(c)) {
            qsizetype j = i;
            while (j < zeile.size() && istWortZeichen(zeile[j])) ++j;
            QStringView wort = zeile.sliced(i, j - i);

            const QString klein = def.caseSensitive ? QString() : wort.toString().toLower();
            const QStringView pruef = def.caseSensitive ? wort : QStringView(klein);

            // TYPEN ZUERST, dann Schlüsselwörter: `int`, `bool` und `void` stehen in beiden Listen. Wer färbt, erwartet sie
            // in der Farbe von `QString`, nicht in der von `if` - reine Schlüsselwörter stehen deshalb nicht in der Typliste.
            if (containsWord(def.types, pruef))
                schiebe(out, i, j - i, Tok::Type);
            else if (containsWord(def.keywords, pruef))
                schiebe(out, i, j - i, Tok::Keyword);
            else if (naechstesSichtbares(zeile, j) == u'(')
                schiebe(out, i, j - i, Tok::Function);
            else if (def.typeBeforeBrace && wort.front().isUpper()
                     && naechstesSichtbares(zeile, j) == u'{')
                schiebe(out, i, j - i, Tok::Type);
            else if (naechstesSichtbares(zeile, j) == u':'
                     && (def.propertyColon == ColonStyle::Anywhere
                         || (def.propertyColon == ColonStyle::LineStart
                             && nurBezeichnerDavor(zeile, i))))
                schiebe(out, i, j - i, Tok::Type);
            else if (def.propertyColon == ColonStyle::LineStart
                     && naechstesSichtbares(zeile, j) == u'.'
                     && nurBezeichnerDavor(zeile, i)
                     && kettenEndeIstDoppelpunkt(zeile, j))
                schiebe(out, i, j - i, Tok::Type);
            i = j;
            continue;
        }

        if (istOperator(c)) {
            qsizetype j = i;
            while (j < zeile.size() && istOperator(zeile[j])
                   && !passt(zeile, j, def.lineComment)
                   && !passt(zeile, j, def.lineComment2)
                   && !passt(zeile, j, def.blockOpen)
                   && !(def.hashColors && j > i && zeile[j] == u'#'))
                ++j;
            if (j == i) ++j;                       // Kommentaranfang direkt hier
            schiebe(out, i, j - i, Tok::Operator);
            i = j;
            continue;
        }

        ++i;
    }
    return makeState(BlockState::None);
}

// Zerlegt den Abschnitt in der EINGEBETTETEN Sprache (CSS bzw. JavaScript) und hängt die Abschnitte - auf die
// Zeile umgerechnet - an `out`. Rückgabe ist der Zustand für die nächste Zeile.
int scanEingebettet(QStringView zeile, qsizetype von, qsizetype bis,
                    BlockState art, int subZustand, SpanList& out) {
    if (bis <= von) return subZustand;
    const LanguageDef& sub = languageForId(art == BlockState::EmbeddedCss
                                               ? QStringView(u"css")
                                               : QStringView(u"js"));
    SpanList lokal;
    const int raus = scanCode(zeile.sliced(von, bis - von), sub,
                              makeState(BlockState(subZustand)), lokal);
    for (const Span& sp : lokal)
        out.push_back(Span{ sp.start + von, sp.length, sp.tok });
    return int(stateKind(raus));
}

BlockState eingebettetFuer(QStringView name) {
    if (name.compare(QLatin1StringView("style"), Qt::CaseInsensitive) == 0)
        return BlockState::EmbeddedCss;
    if (name.compare(QLatin1StringView("script"), Qt::CaseInsensitive) == 0)
        return BlockState::EmbeddedJs;
    return BlockState::None;
}

QLatin1StringView schlussTag(BlockState art) {
    return art == BlockState::EmbeddedCss ? QLatin1StringView("</style>")
                                          : QLatin1StringView("</script>");
}

int scanMarkup(QStringView zeile, const LanguageDef& def, int stateIn, SpanList& out) {
    qsizetype i = 0;

    if (stateKind(stateIn) == BlockState::XmlComment) {
        const qsizetype ende = zeile.indexOf(def.blockClose);
        if (ende < 0) { schiebe(out, 0, zeile.size(), Tok::Comment); return stateIn; }
        const qsizetype nach = ende + def.blockClose.size();
        schiebe(out, 0, nach, Tok::Comment);
        i = nach;
    }

    // Läuft ein <style>- oder <script>-Block aus der Vorzeile weiter, wird bis zum schließenden Tag in der
    // EINGEBETTETEN Sprache zerlegt. Ohne das bliebe der größte Teil einer HTML-Seite ungefärbt (176 von 800 Zeilen).
    const BlockState offen = stateKind(stateIn);
    if (offen == BlockState::EmbeddedCss || offen == BlockState::EmbeddedJs) {
        const QLatin1StringView schluss = schlussTag(offen);
        const qsizetype ende = zeile.indexOf(schluss, 0, Qt::CaseInsensitive);
        const qsizetype grenze = (ende < 0) ? zeile.size() : ende;
        const int subRaus = scanEingebettet(zeile, 0, grenze, offen,
                                            statePayload(stateIn), out);
        if (ende < 0)
            return makeState(offen, subRaus);
        i = ende;                       // ab dem </style> wieder Markup
    }

    BlockState startetGleich = BlockState::None;
    bool naechsterWertIstCss = false;
    bool naechsterWertIstJs = false;

    while (i < zeile.size()) {
        const QChar c = zeile[i];

        if (c == u'<') {
            if (passt(zeile, i, def.blockOpen)) {          // <!--
                const qsizetype ende = zeile.indexOf(def.blockClose, i + def.blockOpen.size());
                if (ende < 0) {
                    schiebe(out, i, zeile.size() - i, Tok::Comment);
                    return makeState(BlockState::XmlComment);
                }
                const qsizetype nach = ende + def.blockClose.size();
                schiebe(out, i, nach - i, Tok::Comment);
                i = nach;
                continue;
            }
            if (i + 1 < zeile.size() && (zeile[i + 1] == u'?' || zeile[i + 1] == u'!')) {
                qsizetype ende = zeile.indexOf(u'>', i);
                if (ende < 0) ende = zeile.size() - 1;
                schiebe(out, i, ende + 1 - i, Tok::Preproc);
                i = ende + 1;
                continue;
            }
            qsizetype j = i + 1;
            if (j < zeile.size() && zeile[j] == u'/') ++j;
            schiebe(out, i, j - i, Tok::Operator);
            const bool schliessend = (j > i + 1);
            const qsizetype nameStart = j;
            while (j < zeile.size() && (istWortZeichen(zeile[j]) || zeile[j] == u'-'
                                        || zeile[j] == u':'))
                ++j;
            schiebe(out, nameStart, j - nameStart, Tok::Keyword);
            if (!schliessend)
                startetGleich = eingebettetFuer(zeile.sliced(nameStart, j - nameStart));
            i = j;
            continue;
        }

        if (c == u'>' || (c == u'/' && i + 1 < zeile.size() && zeile[i + 1] == u'>')) {
            const qsizetype len = (c == u'/') ? 2 : 1;
            schiebe(out, i, len, Tok::Operator);
            i += len;
            if (startetGleich != BlockState::None && len == 1) {
                const QLatin1StringView schluss = schlussTag(startetGleich);
                const qsizetype ende = zeile.indexOf(schluss, i, Qt::CaseInsensitive);
                const qsizetype grenze = (ende < 0) ? zeile.size() : ende;
                const int subRaus = scanEingebettet(zeile, i, grenze, startetGleich, 0, out);
                if (ende < 0)
                    return makeState(startetGleich, subRaus);
                i = ende;
                startetGleich = BlockState::None;
            }
            continue;
        }

        if (c == u'"' || c == u'\'') {
            const qsizetype nach = ueberspringeZeichenkette(zeile, i);
            if ((naechsterWertIstCss || naechsterWertIstJs) && nach - i >= 2) {
                schiebe(out, i, 1, Tok::String);
                scanEingebettet(zeile, i + 1, nach - 1,
                                naechsterWertIstJs ? BlockState::EmbeddedJs
                                                   : BlockState::EmbeddedCss, 0, out);
                schiebe(out, nach - 1, 1, Tok::String);
            } else {
                schiebe(out, i, nach - i, Tok::String);
            }
            naechsterWertIstCss = false;
            naechsterWertIstJs = false;
            i = nach;
            continue;
        }

        if (c == u'&') {
            qsizetype j = i + 1;
            const qsizetype grenze = qMin(zeile.size(), i + 12);
            while (j < grenze && (istWortZeichen(zeile[j]) || zeile[j] == u'#')) ++j;
            if (j < zeile.size() && zeile[j] == u';' && j > i + 1) {
                schiebe(out, i, j + 1 - i, Tok::Number);
                i = j + 1;
                continue;
            }
        }

        if (c == u'=') { schiebe(out, i, 1, Tok::Operator); ++i; continue; }

        if (istWortAnfang(c)) {
            qsizetype j = i;
            while (j < zeile.size() && (istWortZeichen(zeile[j]) || zeile[j] == u'-'
                                        || zeile[j] == u':'))
                ++j;
            if (naechstesSichtbares(zeile, j) == u'=') {
                schiebe(out, i, j - i, Tok::Type);
                const QStringView attribut = zeile.sliced(i, j - i);
                naechsterWertIstCss =
                    attribut.compare(QLatin1StringView("style"),
                                     Qt::CaseInsensitive) == 0;
                naechsterWertIstJs =
                    attribut.size() > 2
                    && attribut.first(2).compare(QLatin1StringView("on"),
                                                 Qt::CaseInsensitive) == 0;
            }
            i = j;
            continue;
        }

        ++i;
    }
    return makeState(BlockState::None);
}

int scanIni(QStringView zeile, const LanguageDef& def, int /*stateIn*/, SpanList& out) {
    qsizetype i = 0;
    while (i < zeile.size() && zeile[i].isSpace()) ++i;
    if (i >= zeile.size()) return makeState(BlockState::None);

    if (passt(zeile, i, def.lineComment) || passt(zeile, i, def.lineComment2)) {
        schiebe(out, i, zeile.size() - i, Tok::Comment);
        return makeState(BlockState::None);
    }

    if (zeile[i] == u'[') {
        qsizetype ende = zeile.indexOf(u']', i);
        if (ende < 0) ende = zeile.size() - 1;
        schiebe(out, i, ende + 1 - i, Tok::Heading);
        return makeState(BlockState::None);
    }

    const qsizetype gleich = zeile.indexOf(u'=');
    if (gleich > i) {
        qsizetype ende = gleich;
        while (ende > i && zeile[ende - 1].isSpace()) --ende;
        schiebe(out, i, ende - i, Tok::Keyword);
        schiebe(out, gleich, 1, Tok::Operator);
        qsizetype w = gleich + 1;
        while (w < zeile.size() && zeile[w].isSpace()) ++w;
        if (w < zeile.size()) {
            if (zeile[w] == u'"' || zeile[w] == u'\'') {
                const qsizetype nach = ueberspringeZeichenkette(zeile, w);
                schiebe(out, w, nach - w, Tok::String);
            } else if (zeile[w].isDigit() || zeile[w] == u'-' || zeile[w] == u'+') {
                schiebe(out, w, zeile.size() - w, Tok::Number);
            }
        }
    }
    return makeState(BlockState::None);
}

// Der Sonderweg, den die Tabelle nicht abbilden kann: Betonung, Listen, Codezäune und Verweise greifen
// ineinander und hängen an der POSITION in der Zeile, nicht an Wörtern.
int scanMarkdown(QStringView zeile, const LanguageDef&, int stateIn, SpanList& out) {
    if (stateKind(stateIn) == BlockState::CodeFence) {
        const int zeichen = statePayload(stateIn);
        qsizetype k = 0;
        while (k < zeile.size() && zeile[k].isSpace()) ++k;
        int zaehler = 0;
        while (k + zaehler < zeile.size() && zeile[k + zaehler].unicode() == char16_t(zeichen))
            ++zaehler;
        schiebe(out, 0, zeile.size(), Tok::CodeSpan);
        return zaehler >= 3 ? makeState(BlockState::None) : stateIn;
    }

    qsizetype start = 0;
    while (start < zeile.size() && zeile[start] == u' ') ++start;

    if (start < zeile.size() && (zeile[start] == u'`' || zeile[start] == u'~')) {
        const QChar z = zeile[start];
        int zaehler = 0;
        while (start + zaehler < zeile.size() && zeile[start + zaehler] == z) ++zaehler;
        if (zaehler >= 3) {
            schiebe(out, 0, zeile.size(), Tok::CodeSpan);
            return makeState(BlockState::CodeFence, int(z.unicode()));
        }
    }

    if (start >= 4 || (!zeile.isEmpty() && zeile[0] == u'\t')) {
        schiebe(out, 0, zeile.size(), Tok::CodeSpan);
        return makeState(BlockState::None);
    }

    if (start < zeile.size() && zeile[start] == u'#') {
        int grad = 0;
        while (start + grad < zeile.size() && zeile[start + grad] == u'#') ++grad;
        if (grad <= 6 && (start + grad >= zeile.size() || zeile[start + grad].isSpace())) {
            schiebe(out, 0, zeile.size(), Tok::Heading);
            return makeState(BlockState::None);
        }
    }

    if (start < zeile.size()) {
        const QChar z = zeile[start];
        if (z == u'=' || z == u'-' || z == u'*' || z == u'_') {
            qsizetype j = start;
            while (j < zeile.size() && (zeile[j] == z || zeile[j].isSpace())) ++j;
            int zaehler = 0;
            for (qsizetype k = start; k < zeile.size(); ++k) if (zeile[k] == z) ++zaehler;
            if (j >= zeile.size() && zaehler >= 3) {
                schiebe(out, 0, zeile.size(), z == u'=' ? Tok::Heading : Tok::Operator);
                return makeState(BlockState::None);
            }
        }
    }

    qsizetype i = start;

    if (i < zeile.size() && zeile[i] == u'>') {
        qsizetype j = i;
        while (j < zeile.size() && (zeile[j] == u'>' || zeile[j] == u' ')) ++j;
        schiebe(out, i, j - i, Tok::Comment);
        i = j;
    } else if (i < zeile.size() && (zeile[i] == u'-' || zeile[i] == u'*' || zeile[i] == u'+')
               && i + 1 < zeile.size() && zeile[i + 1] == u' ') {
        schiebe(out, i, 1, Tok::Operator);
        i += 2;
    } else if (i < zeile.size() && zeile[i].isDigit()) {
        qsizetype j = i;
        while (j < zeile.size() && zeile[j].isDigit()) ++j;
        if (j < zeile.size() && (zeile[j] == u'.' || zeile[j] == u')')
            && j + 1 < zeile.size() && zeile[j + 1] == u' ') {
            schiebe(out, i, j + 1 - i, Tok::Operator);
            i = j + 2;
        }
    }

    while (i < zeile.size()) {
        const QChar c = zeile[i];

        if (c == u'`') {
            const qsizetype ende = zeile.indexOf(u'`', i + 1);
            if (ende > 0) {
                schiebe(out, i, ende + 1 - i, Tok::CodeSpan);
                i = ende + 1;
                continue;
            }
        }

        if (c == u'[' || (c == u'!' && i + 1 < zeile.size() && zeile[i + 1] == u'[')) {
            const qsizetype klammerAuf = (c == u'!') ? i + 1 : i;
            const qsizetype klammerZu = zeile.indexOf(u']', klammerAuf + 1);
            if (klammerZu > 0 && klammerZu + 1 < zeile.size() && zeile[klammerZu + 1] == u'(') {
                const qsizetype zielEnde = zeile.indexOf(u')', klammerZu + 2);
                if (zielEnde > 0) {
                    schiebe(out, i, zielEnde + 1 - i, Tok::Link);
                    i = zielEnde + 1;
                    continue;
                }
            }
        }

        if (c == u'*' || c == u'_') {
            const int laenge = (i + 1 < zeile.size() && zeile[i + 1] == c) ? 2 : 1;
            //  Ein Unterstrich MITTEN in einem Wort ist kein Auszeichner
            //  (`variablen_name` bliebe sonst halb kursiv).
            const bool imWort = c == u'_' && i > 0 && istWortZeichen(zeile[i - 1]);
            if (!imWort) {
                const QString marke(laenge, c);
                const qsizetype ende = zeile.indexOf(marke, i + laenge);
                if (ende > i) {
                    schiebe(out, i, ende + laenge - i, Tok::Emphasis);
                    i = ende + laenge;
                    continue;
                }
            }
        }

        ++i;
    }
    return makeState(BlockState::None);
}

}  // namespace

int scanLine(QStringView zeile, const LanguageDef& def, int stateIn, SpanList& out) {
    out.clear();
    switch (def.kind) {
    case ScannerKind::PlainText: return makeState(BlockState::None);
    case ScannerKind::CLike:
    case ScannerKind::Script:    return scanCode(zeile, def, stateIn, out);
    case ScannerKind::Markup:    return scanMarkup(zeile, def, stateIn, out);
    case ScannerKind::Ini:       return scanIni(zeile, def, stateIn, out);
    case ScannerKind::Markdown:  return scanMarkdown(zeile, def, stateIn, out);
    }
    return makeState(BlockState::None);
}

}  // namespace mg::editor
