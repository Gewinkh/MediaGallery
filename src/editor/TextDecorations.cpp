#include "editor/TextDecorations.h"

#include "editor/SyntaxScanner.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace mg::editor {
namespace {

//  Wie weit die Partnersuche laeuft, bevor sie aufgibt. Eine Klammer, deren
//  Partnerin zehntausend Zeilen entfernt steht, hilft niemandem mehr - und ein
//  unbegrenzter Lauf haenge bei jedem Cursorschritt an der Datei.
constexpr int kMaxBloecke = 5000;

bool istAuf(QChar c)  { return c == u'(' || c == u'[' || c == u'{'; }
bool istZu(QChar c)   { return c == u')' || c == u']' || c == u'}'; }
QChar partnerVon(QChar c) {
    switch (c.unicode()) {
    case u'(': return u')';   case u')': return u'(';
    case u'[': return u']';   case u']': return u'[';
    case u'{': return u'}';   case u'}': return u'{';
    }
    return QChar();
}

}  // namespace

TextDecorations::TextDecorations(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

QTextDocument* TextDecorations::doc() const {
    return m_quickDoc ? m_quickDoc->textDocument() : nullptr;
}

void TextDecorations::setDocument(QQuickTextDocument* d) {
    if (d == m_quickDoc) return;
    if (QTextDocument* alt = doc()) disconnect(alt, nullptr, this, nullptr);
    m_quickDoc = d;
    if (QTextDocument* neu = doc()) {
        connect(neu, &QTextDocument::contentsChanged, this, [this] {
            //  `trefferBereicheNeu` laeuft hier NICHT mit: es durchsucht die
            //  zugeklappten Bereiche, und in denen kann man gar nicht tippen.
            //  Es genuegt, wenn es sich beim Suchbegriff und beim Falten meldet.
            klammernSuchen();
            update();
        });
    }
    emit documentChanged();
    klammernSuchen();
    update();
}

void TextDecorations::setPath(const QString& p) {
    if (p == m_path) return;
    m_path = p;
    emit pathChanged();
    klammernSuchen();
    update();
}

void TextDecorations::setFoldBar(TextFoldBar* b) {
    if (b == m_foldBar) return;
    if (m_foldBar) disconnect(m_foldBar, nullptr, this, nullptr);
    m_foldBar = b;
    if (m_foldBar)
        connect(m_foldBar, &TextFoldBar::foldingChanged, this, [this] {
            trefferBereicheNeu();
            update();
        });
    emit foldBarChanged();
    update();
}

void TextDecorations::setContentY(qreal v) {
    if (qFuzzyCompare(v, m_contentY)) return;
    m_contentY = v; emit viewChanged(); update();
}
void TextDecorations::setViewportHeight(qreal v) {
    if (qFuzzyCompare(v, m_viewportH)) return;
    m_viewportH = v; emit viewChanged(); update();
}
void TextDecorations::setCursorPosition(int p) {
    if (p == m_cursor) return;
    m_cursor = p;
    emit viewChanged();
    klammernSuchen();
    update();
}
void TextDecorations::setLeftPadding(qreal v) {
    if (qFuzzyCompare(v + 1, m_leftPad + 1)) return;
    m_leftPad = v; emit viewChanged(); update();
}
void TextDecorations::setTopPadding(qreal v) {
    if (qFuzzyCompare(v + 1, m_topPad + 1)) return;
    m_topPad = v; emit viewChanged(); update();
}
void TextDecorations::setFont(const QFont& f) {
    if (f == m_font) return;
    m_font = f; emit styleChanged(); update();
}
void TextDecorations::setTabWidth(int v) {
    const int w = qBound(2, v, 8);
    if (w == m_tabWidth) return;
    m_tabWidth = w; emit styleChanged(); update();
}
void TextDecorations::setShowGuides(bool v) {
    if (v == m_guides) return;
    m_guides = v; emit styleChanged(); update();
}
void TextDecorations::setShowBrackets(bool v) {
    if (v == m_brackets) return;
    m_brackets = v;
    emit styleChanged();
    klammernSuchen();
    update();
}
void TextDecorations::setGuideColor(const QColor& c) {
    if (c == m_guideColor) return;
    m_guideColor = c; emit styleChanged(); update();
}
void TextDecorations::setMarkerColor(const QColor& c) {
    if (c == m_markerColor) return;
    m_markerColor = c; emit styleChanged(); update();
}
void TextDecorations::setBracketColor(const QColor& c) {
    if (c == m_bracketColor) return;
    m_bracketColor = c; emit styleChanged(); update();
}
void TextDecorations::setErrorColor(const QColor& c) {
    if (c == m_errorColor) return;
    m_errorColor = c; emit styleChanged(); update();
}
void TextDecorations::setMatchColor(const QColor& c) {
    if (c == m_matchColor) return;
    m_matchColor = c; emit styleChanged(); update();
}

void TextDecorations::setSearchTerm(const QString& term, bool caseSensitive) {
    m_suche = mg::search::Pattern(term, caseSensitive, false);
    trefferBereicheNeu();
    update();
}

void TextDecorations::trefferBereicheNeu() {
    m_trefferBereiche.clear();
    QTextDocument* d = doc();
    if (!d || !m_foldBar || m_suche.isEmpty()) return;

    //  Nur die ZUGEKLAPPTEN Bereiche werden durchsucht, und nur solange ein
    //  Suchbegriff steht - so kostet das nichts, wenn niemand sucht.
    for (const FoldRegion& r : m_foldBar->regions()) {
        if (!m_foldBar->foldedStarts().contains(r.start)) continue;
        QTextBlock b = d->findBlockByNumber(r.start + 1);
        for (int i = r.start + 1; i <= r.end && b.isValid(); ++i, b = b.next()) {
            if (m_suche.contains(b.text())) { m_trefferBereiche.insert(r.start); break; }
        }
    }
}

void TextDecorations::klammernSuchen() {
    QTextDocument* d = doc();
    if (!d || !m_brackets) { m_klammerA = m_klammerB = -1; return; }

    //  Nichts zu tun, wenn Cursor UND Dokumentstand dieselben sind wie beim
    //  letzten Mal - das Signal kommt oefter, als sich etwas aendert.
    const int rev = int(d->revision());
    if (m_cursor == m_letzterCursor && rev == m_letzteRevision) return;
    m_letzterCursor = m_cursor;
    m_letzteRevision = rev;
    m_klammerA = m_klammerB = -1;

    //  Die Klammer AM Cursor - erst das Zeichen davor (wie in Qt Creator, nach
    //  dem Tippen von `)` steht der Cursor dahinter), sonst das darunter.
    QTextBlock block = d->findBlock(m_cursor);
    if (!block.isValid()) return;
    const QString text = block.text();
    const int rel = m_cursor - block.position();

    int relKlammer = -1;
    if (rel > 0 && rel - 1 < text.size()
        && (istAuf(text.at(rel - 1)) || istZu(text.at(rel - 1))))
        relKlammer = rel - 1;
    else if (rel >= 0 && rel < text.size()
             && (istAuf(text.at(rel)) || istZu(text.at(rel))))
        relKlammer = rel;
    if (relKlammer < 0) return;

    const LanguageDef& def = languageForPath(m_path);
    SpanList spans;
    //  Der Zustand am Anfang DIESES Blocks steht im Block davor - der Faerber
    //  legt ihn dort ab (`QSyntaxHighlighter` fuehrt je Block ein int).
    int zustand = block.previous().isValid()
                      ? qMax(0, block.previous().userState()) : 0;
    scanLine(text, def, zustand, spans);
    if (inStringOrComment(spans, relKlammer)) return;   // Klammer in Text, nicht im Code

    const QChar hier = text.at(relKlammer);
    const QChar suche = partnerVon(hier);
    const bool vorwaerts = istAuf(hier);
    m_klammerA = block.position() + relKlammer;

    int tiefe = 0;
    int bloecke = 0;
    if (vorwaerts) {
        QTextBlock b = block;
        int start = relKlammer;
        int zst = zustand;
        while (b.isValid() && bloecke < kMaxBloecke) {
            const QString t = b.text();
            if (b != block) { zst = qMax(0, b.previous().userState()); }
            scanLine(t, def, zst, spans);
            for (int i = start; i < t.size(); ++i) {
                const QChar c = t.at(i);
                if (c != hier && c != suche) continue;
                if (inStringOrComment(spans, i)) continue;
                if (c == hier) ++tiefe;
                else if (--tiefe == 0) { m_klammerB = b.position() + i; return; }
            }
            b = b.next(); start = 0; ++bloecke;
        }
    } else {
        QTextBlock b = block;
        int start = relKlammer;
        while (b.isValid() && bloecke < kMaxBloecke) {
            const QString t = b.text();
            const int zst = b.previous().isValid()
                                ? qMax(0, b.previous().userState()) : 0;
            scanLine(t, def, zst, spans);
            for (int i = start; i >= 0; --i) {
                if (i >= t.size()) continue;
                const QChar c = t.at(i);
                if (c != hier && c != suche) continue;
                if (inStringOrComment(spans, i)) continue;
                if (c == hier) ++tiefe;
                else if (--tiefe == 0) { m_klammerB = b.position() + i; return; }
            }
            b = b.previous();
            start = b.isValid() ? int(b.text().size()) - 1 : -1;
            ++bloecke;
        }
    }
}

QRectF TextDecorations::zeichenRect(int position) const {
    QTextDocument* d = doc();
    if (!d || position < 0) return {};
    const QTextBlock b = d->findBlock(position);
    if (!b.isValid() || !b.isVisible() || !b.layout()) return {};
    const int rel = position - b.position();
    const QTextLine line = b.layout()->lineForTextPosition(rel);
    if (!line.isValid()) return {};

    const QRectF bb = d->documentLayout()->blockBoundingRect(b);
    const qreal x0 = line.cursorToX(rel);
    const qreal x1 = line.cursorToX(rel + 1);
    return QRectF(m_leftPad + bb.left() + qMin(x0, x1),
                  bb.top() + line.y() - m_contentY + m_topPad,
                  qAbs(x1 - x0), line.height());
}

void TextDecorations::zeichneHilfen(QPainter* p) {
    QTextDocument* d = doc();
    if (!d) return;
    //  Gemessen wird an der SCHRIFT DES DOKUMENTS, nicht an der aus QML: die
    //  QML-Schrift traegt eine Ersatzfamilien-Liste (`fallbackFont`), und
    //  `QFontMetricsF` darauf lieferte gemessen 2,86 px je Leerzeichen statt
    //  der tatsaechlichen ~7,8 - die Linien saessen dann am linken Rand statt
    //  an der Einrueckung. Dieselbe Falle wie beim Tabulator-Abstand.
    const QFontMetricsF fm(d->defaultFont());
    const qreal zeichenB = fm.horizontalAdvance(QLatin1Char(' '));
    const qreal stufe = zeichenB * m_tabWidth;
    if (stufe <= 0.5) return;

    p->setPen(Qt::NoPen);
    p->setBrush(m_guideColor);

    //  Beim ERSTEN SICHTBAREN Block anfangen, nicht vorne im Dokument: der
    //  Durchgang lief sonst je Bild ueber alle Bloecke der Datei - bei 20 000
    //  Zeilen ist das bei jedem Scrollschritt spuerbar. `hitTest` findet die
    //  Stelle direkt (dasselbe Muster wie in der Nummernspalte).
    QAbstractTextDocumentLayout* lay = d->documentLayout();
    QTextBlock start = d->findBlock(qMax(0, lay->hitTest(QPointF(0, m_contentY),
                                                         Qt::FuzzyHit)));
    if (!start.isValid()) start = d->firstBlock();
    if (start.previous().isValid()) start = start.previous();

    //  Einrueckung der vorigen sichtbaren Zeile: fuer eine Leerzeile wird sie
    //  uebernommen. Beim Einstieg mittendrin ist sie unbekannt - dann traegt
    //  die erste sichtbare Zeile ihre eigene.
    int letzterEinzug = 0;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        if (!b.isVisible()) continue;
        const QRectF bb = d->documentLayout()->blockBoundingRect(b);
        const qreal y = bb.top() - m_contentY + m_topPad;
        if (y > height()) break;

        const QString t = b.text();
        int spalte = 0;
        bool leer = true;
        for (const QChar c : t) {
            if (c == u'\t')     spalte += m_tabWidth - (spalte % m_tabWidth);
            else if (c == u' ') ++spalte;
            else                { leer = false; break; }
        }
        if (leer) spalte = letzterEinzug;
        else      letzterEinzug = spalte;
        if (y + bb.height() < 0) continue;

        //  Je Stufe eine Linie - die Stufe 0 (linker Rand) bekommt keine, dort
        //  steht schon die Nummernspalte.
        //  Nur bis EINE Stufe VOR der eigenen Einrueckung - eine Linie auf
        //  dem ersten Zeichen der Zeile stuende im Text statt daneben.
        for (int s = 1; s * m_tabWidth < spalte; ++s) {
            const qreal x = m_leftPad + s * stufe;
            if (x > width()) break;
            p->drawRect(QRectF(x, y, 1.0, bb.height()));
        }
    }
}

QRectF TextDecorations::markenRect(int startBlock) const {
    QTextDocument* d = doc();
    if (!d) return {};
    const QTextBlock b = d->findBlockByNumber(startBlock);
    if (!b.isValid() || !b.isVisible() || !b.layout()) return {};
    const QRectF bb = d->documentLayout()->blockBoundingRect(b);
    const qreal y = bb.top() - m_contentY + m_topPad;
    if (y > height() || y + bb.height() < 0) return {};

    //  Hinter dem letzten Zeichen der Zeile - `naturalTextWidth` ist die
    //  Breite OHNE den nachlaufenden Leerraum.
    const int letzte = b.layout()->lineCount() - 1;
    if (letzte < 0) return {};
    const QTextLine line = b.layout()->lineAt(letzte);
    const QFontMetricsF fm(d->defaultFont());
    const qreal punkt = qMax(1.5, fm.height() * 0.09);
    const qreal x = m_leftPad + bb.left() + line.naturalTextWidth()
                    + fm.horizontalAdvance(QLatin1Char(' '));
    const qreal mitteY = y + line.y() + line.height() / 2.0;
    return QRectF(x - punkt, mitteY - fm.height() * 0.36,
                  punkt * 8.0, fm.height() * 0.72);
}

int TextDecorations::foldMarkerAt(qreal x, qreal y) const {
    if (!m_foldBar) return -1;
    for (const int start : m_foldBar->foldedStarts()) {
        //  Etwas Luft rundherum: die Marke ist nur ein paar Pixel hoch, und
        //  getroffen werden soll sie mit der Maus, nicht mit der Pinzette.
        const QRectF r = markenRect(start).adjusted(-3, -3, 3, 3);
        if (!r.isNull() && r.contains(x, y)) return start;
    }
    return -1;
}

void TextDecorations::zeichneFaltmarken(QPainter* p) {
    QTextDocument* d = doc();
    if (!d || !m_foldBar) return;
    const QFontMetricsF fm(d->defaultFont());
    const qreal punkt = qMax(1.5, fm.height() * 0.09);

    for (const int start : m_foldBar->foldedStarts()) {
        const QRectF r = markenRect(start);
        if (r.isNull()) continue;

        //  Steckt in dem verborgenen Teil ein Suchtreffer, wird die Marke
        //  hervorgehoben - der Zaehler der Leiste zaehlt ihn ja mit.
        const bool treffer = m_trefferBereiche.contains(start);
        p->setPen(Qt::NoPen);
        p->setBrush(treffer ? m_matchColor : m_bracketColor);
        p->drawRoundedRect(r, punkt, punkt);

        p->setBrush(m_markerColor);
        const qreal mitteY = r.center().y();
        for (int i = 0; i < 3; ++i)
            p->drawEllipse(QPointF(r.left() + punkt * (1.8 + i * 2.4), mitteY),
                           punkt, punkt);
    }
}

void TextDecorations::zeichneKlammern(QPainter* p) {
    if (!m_brackets || m_klammerA < 0) return;
    p->setPen(Qt::NoPen);

    const QRectF a = zeichenRect(m_klammerA);
    if (a.isNull()) return;
    //  Ohne Partnerin: rot. Das ist die eigentliche Hilfe - eine fehlende
    //  Klammer sieht man sonst erst beim Uebersetzen.
    p->setBrush(m_klammerB >= 0 ? m_bracketColor : m_errorColor);
    p->drawRoundedRect(a.adjusted(-1, 0, 1, 0), 2, 2);

    if (m_klammerB >= 0) {
        const QRectF b = zeichenRect(m_klammerB);
        if (!b.isNull()) p->drawRoundedRect(b.adjusted(-1, 0, 1, 0), 2, 2);
    }
}

void TextDecorations::paint(QPainter* p) {
    if (!doc()) return;
    p->setRenderHint(QPainter::Antialiasing, true);
    if (m_guides) zeichneHilfen(p);
    zeichneFaltmarken(p);
    zeichneKlammern(p);
}

}  // namespace mg::editor
