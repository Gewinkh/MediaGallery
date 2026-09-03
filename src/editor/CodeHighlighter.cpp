#include "editor/CodeHighlighter.h"

#include "core/SearchPattern.h"

#include "editor/EditorController.h"
#include "editor/LanguageTable.h"
#include "editor/SyntaxScanner.h"


#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextOption>

namespace mg::editor {

// ── Der Faerber ──────────────────────────────────────────────────────────────
Highlighter::Highlighter(QTextDocument* doc)
    : QSyntaxHighlighter(doc) {
    m_palette = activeController() ? activeController()->palette()
                                   : paletteForProfile(EditorProfile::Nightfall);
    rebuildFormats();
}

void Highlighter::rebuildFormats() {
    for (int i = 0; i < int(Tok::Count); ++i) {
        QTextCharFormat f;
        f.setForeground(m_palette.colorFor(Tok(i)));
        //  Zwei Klassen tragen zusaetzlich eine Schriftauszeichnung - sie
        //  bedeuten in Markdown genau das, und ohne sie saehe eine Ueberschrift
        //  aus wie gewoehnlicher Text in einer anderen Farbe.
        if (Tok(i) == Tok::Heading)  f.setFontWeight(QFont::Bold);
        if (Tok(i) == Tok::Emphasis) f.setFontItalic(true);
        m_formats[i] = f;
    }
}

void Highlighter::setLanguageId(const QString& id) {
    if (id == m_languageId) return;
    m_languageId = id;
    rehighlight();
}

void Highlighter::setSearchTerm(const QString& term, bool caseSensitive) {
    if (term == m_searchPattern.literal()
        && caseSensitive == m_searchPattern.caseSensitive()) return;
    m_searchPattern = mg::search::Pattern(term, caseSensitive, false);
    rehighlight();
}

void Highlighter::setPalette(const SyntaxPalette& p) {
    m_palette = p;
    rebuildFormats();
    rehighlight();
}

void Highlighter::highlightBlock(const QString& text) {
    const LanguageDef& def = languageForId(m_languageId);
    if (def.kind == ScannerKind::PlainText) {
        //  Auch OHNE Syntax werden Fundstellen hinterlegt - eine `.txt` ist
        //  genau die Datei, in der man sucht.
        markiereFundstellen(text);
        setCurrentBlockState(0);
        return;
    }
    //  `previousBlockState` ist -1 fuer den ersten Block bzw. fuer einen, der
    //  noch nie gefaerbt wurde - der Zerleger versteht das als „nichts offen".
    const int zustandRein = qMax(0, previousBlockState());

    SpanList spans;
    const int zustandRaus = scanLine(text, def, zustandRein, spans);
    for (const Span& s : spans)
        setFormat(int(s.start), int(s.length), m_formats[int(s.tok)]);

    markiereFundstellen(text);
    setCurrentBlockState(zustandRaus);
}

//  Hinterlegt jede Fundstelle im Block. Die Farbe ist die AUSWAHLfarbe der
//  Palette, abgeschwaecht: der gerade angesprungene Treffer ist die echte
//  Auswahl des Editors und hebt sich dadurch von selbst ab - so braucht es
//  keine zwanzigste Farbe im Einstellungsdialog.
void Highlighter::markiereFundstellen(const QString& text) {
    if (m_searchPattern.isEmpty()) return;
    QTextCharFormat f;
    QColor c = m_palette.selection;
    c.setAlpha(120);
    f.setBackground(c);
    //  Derselbe Begriff wie in der Leiste - woertlich UND als Muster, damit
    //  markiert ist, was der Zaehler zaehlt.
    for (const mg::search::Range& r : m_searchPattern.findAll(text, 2000))
        setFormat(r.start, r.length, f);
}

// ── Die QML-Fassade ──────────────────────────────────────────────────────────
CodeHighlighter::CodeHighlighter(QObject* parent) : QObject(parent) {
    //  Farbwechsel in den Einstellungen faerbt JEDE offene Kachel sofort um.
    if (EditorController* c = activeController()) {
        connect(c, &EditorController::paletteChanged, this, [this] {
            if (m_highlighter && activeController())
                m_highlighter->setPalette(activeController()->palette());
        });
    }
}

CodeHighlighter::~CodeHighlighter() = default;

void CodeHighlighter::setDocument(QQuickTextDocument* d) {
    if (d == m_quickDoc) return;
    m_quickDoc = d;
    neuAufbauen();
    emit documentChanged();
}

void CodeHighlighter::setPath(const QString& p) {
    if (p == m_path) return;
    m_path = p;
    const QString neueSprache = languageForPath(p).id;
    const bool gewechselt = neueSprache != m_languageId;
    m_languageId = neueSprache;
    if (m_highlighter && gewechselt)
        m_highlighter->setLanguageId(m_languageId);
    emit languageChanged();
}

void CodeHighlighter::neuAufbauen() {
    //  Der alte Faerber haengt am ALTEN Dokument - er muss weg, sonst faerbte
    //  er weiter mit und der neue kaeme obendrauf.
    delete m_highlighter;
    m_highlighter = nullptr;
    if (!m_quickDoc) return;

    QTextDocument* doc = m_quickDoc->textDocument();
    if (!doc) return;

    m_highlighter = new Highlighter(doc);
    m_highlighter->setLanguageId(m_languageId);
    tabBreiteAnwenden();

    //  `QQuickTextEdit` setzt die Textoptionen des Dokuments neu, sobald sich
    //  Umbruch oder Ausrichtung aendern - dabei faellt die Tabulatorbreite
    //  wieder auf Qts 80 px zurueck. Deshalb nach JEDER Layout-Aenderung erneut
    //  setzen; die Pruefung in `tabBreiteAnwenden` haelt das billig.
    if (auto* lay = doc->documentLayout())
        connect(lay, &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this] { tabBreiteAnwenden(); });
}

void CodeHighlighter::setTabWidth(int zeichen) {
    if (zeichen == m_tabWidth || zeichen < 0) return;
    m_tabWidth = zeichen;
    tabBreiteAnwenden();
    emit tabWidthChanged();
}

int CodeHighlighter::columnAt(int position) const {
    if (!m_quickDoc) return 0;
    const QTextDocument* doc = m_quickDoc->textDocument();
    if (!doc) return 0;
    const QTextBlock b = doc->findBlock(position);
    if (!b.isValid()) return 0;
    return qMax(0, position - b.position());
}

void CodeHighlighter::tabBreiteAnwenden() {
    if (!m_quickDoc || m_tabWidth <= 0) return;
    QTextDocument* doc = m_quickDoc->textDocument();
    if (!doc) return;

    //  An der Schrift des DOKUMENTS messen - mit ihr rechnet das Layout auch.
    //  Und an echten Leerzeichen, damit ein Tabulator genau dort endet, wo
    //  `m_tabWidth` Leerzeichen enden.
    const QFontMetricsF fm(doc->defaultFont());
    const qreal px = fm.horizontalAdvance(QString(m_tabWidth, u' '));
    if (px <= 0) return;

    QTextOption opt = doc->defaultTextOption();
    if (qFuzzyCompare(opt.tabStopDistance(), px))
        return;
    opt.setTabStopDistance(px);
    doc->setDefaultTextOption(opt);
}

// ── Suchen und Ersetzen ─────────────────────────────────────────────────────
namespace {

//  Deckel fuer die Trefferliste. Die ANZEIGE „999+" sagt genug, und die Leiste
//  soll beim Tippen nicht stehenbleiben.
constexpr int kMaxTreffer = 10000;

//  Alle Fundstellen des Dokuments, in Dokumentkoordinaten, sortiert und
//  ueberschneidungsfrei. Gesammelt wird BLOCKWEISE - nicht ueber
//  `QTextDocument::find`: nur so laufen der woertliche und der Muster-Zweig
//  ueber denselben Text (s. `core/SearchPattern.h`), und der Volltext des
//  Dokuments muss nicht kopiert werden (am Lesedeckel 8 MB).
//  Ein Treffer ueber einen Zeilenumbruch hinweg ist damit nicht moeglich -
//  `QTextDocument::find` konnte das aber ebenfalls nie.
QList<mg::search::Range> alleTreffer(const QTextDocument* doc,
                                     const mg::search::Pattern& p) {
    QList<mg::search::Range> raus;
    if (!doc || p.isEmpty()) return raus;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const int basis = b.position();
        for (const mg::search::Range& r : p.findAll(b.text(), kMaxTreffer)) {
            raus.append({ basis + r.start, r.length });
            if (raus.size() >= kMaxTreffer) return raus;
        }
    }
    return raus;
}

//  Der wievielte Treffer beginnt bei `pos`? 1-basiert, 0 = keiner.
int indexBei(const QList<mg::search::Range>& treffer, int pos) {
    for (qsizetype i = 0; i < treffer.size(); ++i)
        if (treffer.at(i).start == pos) return int(i) + 1;
    return 0;
}

QVariantMap ergebnis(bool gefunden, int start, int len, int index, int total) {
    QVariantMap m;
    m.insert(QStringLiteral("found"),  gefunden);
    m.insert(QStringLiteral("start"),  start);
    m.insert(QStringLiteral("length"), len);
    m.insert(QStringLiteral("index"),  index);
    m.insert(QStringLiteral("total"),  total);
    return m;
}

}  // namespace

int CodeHighlighter::countMatches(const QString& needle, bool caseSensitive,
                                  bool wholeWords) const {
    if (!m_quickDoc || needle.isEmpty()) return 0;
    QTextDocument* doc = m_quickDoc->textDocument();
    if (!doc) return 0;
    return int(alleTreffer(doc, mg::search::Pattern(needle, caseSensitive,
                                                    wholeWords)).size());
}

QVariantMap CodeHighlighter::findNext(const QString& needle, int from,
                                      bool caseSensitive, bool wholeWords,
                                      bool backward) const {
    if (!m_quickDoc || needle.isEmpty()) return ergebnis(false, 0, 0, 0, 0);
    QTextDocument* doc = m_quickDoc->textDocument();
    if (!doc) return ergebnis(false, 0, 0, 0, 0);

    const mg::search::Pattern p(needle, caseSensitive, wholeWords);
    const QList<mg::search::Range> treffer = alleTreffer(doc, p);
    if (treffer.isEmpty()) return ergebnis(false, 0, 0, 0, 0);

    //  Vorwaerts: der erste, der bei oder hinter `from` beginnt. Rueckwaerts:
    //  der letzte, der davor endet. Findet sich keiner, wird UMGELAUFEN -
    //  hinter dem letzten Treffer geht es beim ersten weiter.
    qsizetype ziel = -1;
    if (backward) {
        for (qsizetype i = treffer.size() - 1; i >= 0; --i)
            if (treffer.at(i).start + treffer.at(i).length <= from) { ziel = i; break; }
        if (ziel < 0) ziel = treffer.size() - 1;
    } else {
        for (qsizetype i = 0; i < treffer.size(); ++i)
            if (treffer.at(i).start >= from) { ziel = i; break; }
        if (ziel < 0) ziel = 0;
    }

    const mg::search::Range& r = treffer.at(ziel);
    return ergebnis(true, r.start, r.length, int(ziel) + 1, int(treffer.size()));
}

QVariantMap CodeHighlighter::replaceAndFind(const QString& needle,
                                            const QString& replacement, int from,
                                            bool caseSensitive, bool wholeWords) {
    if (!m_quickDoc || needle.isEmpty()) return ergebnis(false, 0, 0, 0, 0);
    QTextDocument* doc = m_quickDoc->textDocument();
    if (!doc) return ergebnis(false, 0, 0, 0, 0);

    //  Nur ersetzen, wenn bei `from` WIRKLICH ein Treffer beginnt. Sonst wuerde
    //  „Ersetzen" etwas anfassen, das der Nutzer gar nicht ausgewaehlt sieht.
    const mg::search::Pattern p(needle, caseSensitive, wholeWords);
    const QList<mg::search::Range> treffer = alleTreffer(doc, p);
    const int idx = indexBei(treffer, from);
    if (idx > 0) {
        const mg::search::Range& r = treffer.at(idx - 1);
        QTextCursor c(doc);
        c.setPosition(r.start);
        c.setPosition(r.start + r.length, QTextCursor::KeepAnchor);
        c.beginEditBlock();
        //  WOERTLICH einsetzen: `\1` waere fuer einen woertlichen Treffer
        //  bedeutungslos, und ein Treffer kann aus beiden Zweigen stammen.
        c.insertText(replacement);
        c.endEditBlock();
        return findNext(needle, from + int(replacement.size()),
                        caseSensitive, wholeWords, false);
    }
    return findNext(needle, from, caseSensitive, wholeWords, false);
}

int CodeHighlighter::replaceAll(const QString& needle, const QString& replacement,
                                bool caseSensitive, bool wholeWords) {
    if (!m_quickDoc || needle.isEmpty()) return 0;
    QTextDocument* doc = m_quickDoc->textDocument();
    if (!doc) return 0;

    const mg::search::Pattern p(needle, caseSensitive, wholeWords);
    const QList<mg::search::Range> treffer = alleTreffer(doc, p);
    if (treffer.isEmpty()) return 0;

    QTextCursor klammer(doc);
    //  EIN Undo-Schritt fuer alles: sonst muesste der Nutzer so oft Strg+Z
    //  druecken, wie ersetzt wurde.
    klammer.beginEditBlock();
    //  VON HINTEN nach vorn, damit die Positionen der noch offenen Treffer
    //  gueltig bleiben - ein laengerer oder kuerzerer Ersatz verschiebt sonst
    //  alles Nachfolgende. (Die Liste ist ueberschneidungsfrei, s. mergeRanges.)
    int n = 0;
    for (qsizetype i = treffer.size() - 1; i >= 0; --i) {
        const mg::search::Range& r = treffer.at(i);
        QTextCursor c(doc);
        c.setPosition(r.start);
        c.setPosition(r.start + r.length, QTextCursor::KeepAnchor);
        c.insertText(replacement);
        ++n;
    }
    klammer.endEditBlock();
    return n;
}

bool CodeHighlighter::usesRegex(const QString& needle) const {
    return mg::search::Pattern(needle, false, false).usesRegex();
}

void CodeHighlighter::highlightMatches(const QString& needle, bool caseSensitive) {
    if (m_highlighter)
        m_highlighter->setSearchTerm(needle, caseSensitive);
}

QString CodeHighlighter::languageLabel() const {
    return languageForId(m_languageId).label;
}

bool CodeHighlighter::active() const {
    return languageForId(m_languageId).kind != ScannerKind::PlainText;
}

}  // namespace mg::editor
