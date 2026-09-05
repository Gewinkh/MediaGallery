#include "editor/TextGutter.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextDocument>

namespace mg::editor {

TextGutter::TextGutter(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::NoButton);
    setAntialiasing(true);
}

QTextDocument* TextGutter::doc() const {
    return m_quickDoc ? m_quickDoc->textDocument() : nullptr;
}

void TextGutter::setDocument(QQuickTextDocument* d) {
    if (d == m_quickDoc) return;
    if (QTextDocument* alt = doc())
        disconnect(alt, nullptr, this, nullptr);

    m_quickDoc = d;

    if (QTextDocument* neu = doc()) {
        connect(neu, &QTextDocument::contentsChanged, this, [this] {
            breiteNeuRechnen();
            update();
        });
        if (auto* lay = neu->documentLayout())
            connect(lay, &QAbstractTextDocumentLayout::documentSizeChanged,
                    this, [this] { update(); });
    }
    breiteNeuRechnen();
    emit documentChanged();
    update();
}

void TextGutter::setContentY(qreal y) {
    if (qFuzzyCompare(y, m_contentY)) return;
    m_contentY = y;
    emit contentYChanged();
    update();
}

void TextGutter::setTopPadding(qreal v) {
    if (qFuzzyCompare(v, m_topPadding)) return;
    m_topPadding = v;
    emit topPaddingChanged();
    update();
}

void TextGutter::setCursorPosition(int p) {
    if (p == m_cursorPosition) return;
    m_cursorPosition = p;
    emit cursorPositionChanged();
    update();
}

void TextGutter::setFont(const QFont& f) {
    if (f == m_font) return;
    m_font = f;
    breiteNeuRechnen();
    emit styleChanged();
    update();
}

void TextGutter::setBackgroundColor(const QColor& c) {
    if (c == m_background) return;
    m_background = c; emit styleChanged(); update();
}
void TextGutter::setTextColor(const QColor& c) {
    if (c == m_textColor) return;
    m_textColor = c; emit styleChanged(); update();
}
void TextGutter::setActiveColor(const QColor& c) {
    if (c == m_activeColor) return;
    m_activeColor = c; emit styleChanged(); update();
}
void TextGutter::setBorderColor(const QColor& c) {
    if (c == m_borderColor) return;
    m_borderColor = c; emit styleChanged(); update();
}

int TextGutter::lineCount() const {
    const QTextDocument* d = doc();
    return d ? d->blockCount() : 0;
}

int TextGutter::cursorLine() const {
    const QTextDocument* d = doc();
    if (!d) return 1;
    return d->findBlock(m_cursorPosition).blockNumber() + 1;
}

int TextGutter::cursorColumn() const {
    const QTextDocument* d = doc();
    if (!d) return 1;
    const QTextBlock b = d->findBlock(m_cursorPosition);
    if (!b.isValid()) return 1;
    return m_cursorPosition - b.position() + 1;
}

void TextGutter::breiteNeuRechnen() {
    const int zeilen = qMax(1, lineCount());
    if (zeilen == m_letzteZeilenzahl && m_requiredWidth > 0)
        return;
    m_letzteZeilenzahl = zeilen;

    //  Breite aus der HOECHSTEN Nummer, nicht aus der laengsten sichtbaren:
    //  sonst zuckte die Spalte beim Scrollen, sobald die Stellenzahl wechselt,
    //  und der Text daneben rutschte mit.
    const int stellen = QString::number(zeilen).size();
    const QFontMetricsF fm(m_font);
    const qreal neu = fm.horizontalAdvance(QString(qMax(2, stellen), u'9')) + 18.0;
    if (!qFuzzyCompare(neu, m_requiredWidth)) {
        m_requiredWidth = neu;
        emit requiredWidthChanged();
    }
}

// Der Strich beginnt an der Oberkante der ersten Fortsetzungszeile und endet auf
// halber Hoehe der letzten; je Fortsetzungszeile geht ein kurzer Pfeil nach rechts ab.
void TextGutter::zeichneAst(QPainter* p, const QTextLayout& tl,
                            qreal blockOben, const QColor& farbe) {
    const int zeilen = tl.lineCount();
    if (zeilen < 2) return;

    constexpr qreal kStaerke = 1.6;    // Strichstaerke
    constexpr qreal kStummel = 9.0;    // Laenge des Abzweigs inkl. Spitze
    constexpr qreal kSpitze  = 3.5;    // Laenge der beiden Spitzenstriche
    constexpr qreal kRand    = 6.0;    // Abstand der Pfeilspitze zum Textrand

    const qreal xEnde   = width() - kRand;
    const qreal xStrich = qRound(xEnde - kStummel) + 0.5;   // scharf, nicht grau

    QPen stift(farbe);
    stift.setWidthF(kStaerke);
    stift.setCapStyle(Qt::RoundCap);
    stift.setJoinStyle(Qt::RoundJoin);
    p->save();
    p->setPen(stift);

    // Anfang: Oberkante der ersten FORTSETZUNGSzeile. Soll der Strich später bis kurz unter die Nummer
    // hochgezogen werden, ist das genau diese eine Zeile - dann die Mitte der ERSTEN Zeile nehmen.
    const qreal oben = blockOben + tl.lineAt(1).y();

    const QTextLine letzte = tl.lineAt(zeilen - 1);
    const qreal unten = blockOben + letzte.y() + letzte.height() / 2.0;

    p->drawLine(QPointF(xStrich, oben), QPointF(xStrich, unten));

    for (int i = 1; i < zeilen; ++i) {
        const QTextLine zeile = tl.lineAt(i);
        const qreal mitte = qRound(blockOben + zeile.y() + zeile.height() / 2.0) + 0.5;
        p->drawLine(QPointF(xStrich, mitte), QPointF(xEnde, mitte));
        p->drawLine(QPointF(xEnde, mitte), QPointF(xEnde - kSpitze, mitte - kSpitze));
        p->drawLine(QPointF(xEnde, mitte), QPointF(xEnde - kSpitze, mitte + kSpitze));
    }
    p->restore();
}

void TextGutter::paint(QPainter* p) {
    p->fillRect(QRectF(0, 0, width(), height()), m_background);
    if (m_borderColor.alpha() > 0)
        p->fillRect(QRectF(width() - 1, 0, 1, height()), m_borderColor);

    QTextDocument* d = doc();
    if (!d) return;
    QAbstractTextDocumentLayout* lay = d->documentLayout();
    if (!lay) return;

    p->setFont(m_font);

    const int aktiveZeile = d->findBlock(m_cursorPosition).blockNumber();

    //  Beim ERSTEN sichtbaren Block anfangen statt vorne im Dokument: bei
    //  240 000 Zeilen waere das Durchlaufen von Block 0 an bei JEDEM Malen
    //  spuerbar. `hitTest` findet die Stelle direkt.
    const int trefferPos = lay->hitTest(QPointF(0, m_contentY), Qt::FuzzyHit);
    QTextBlock block = d->findBlock(qMax(0, trefferPos));
    if (!block.isValid())
        block = d->firstBlock();
    if (block.previous().isValid())
        block = block.previous();

    const qreal unten = m_contentY + height();
    const QFontMetricsF fm(m_font);

    while (block.isValid()) {
        //  Zugeklappte Zeilen bekommen KEINE Nummer. Ihr Rechteck ist null hoch
        //  und laege sonst genau auf der naechsten sichtbaren Zeile - die
        //  Nummern druckten uebereinander (s. `TextFoldBar`).
        if (!block.isVisible()) { block = block.next(); continue; }
        const QRectF r = lay->blockBoundingRect(block);
        if (r.top() > unten) break;
        if (r.bottom() >= m_contentY) {
            const qreal y = r.top() - m_contentY + m_topPadding;
            QTextLayout* tl = block.layout();
            const qreal zeilenhoehe = (tl && tl->lineCount() > 0) ? tl->lineAt(0).height()
                                                                  : fm.height();
            const bool aktiv = (block.blockNumber() == aktiveZeile);
            const QColor farbe = aktiv ? m_activeColor : m_textColor;
            p->setPen(farbe);
            p->drawText(QRectF(0, y, width() - 10, zeilenhoehe),
                        Qt::AlignRight | Qt::AlignVCenter,
                        QString::number(block.blockNumber() + 1));

            // In den Fortsetzungszeilen steht ein dünner senkrechter Strich mit kurzem Pfeil statt Leere: er zeigt, wie weit
            // eine logische Zeile reicht, ohne eine Nummer vorzutäuschen, die es dort nicht gibt.
            if (tl && tl->lineCount() > 1)
                zeichneAst(p, *tl, y, farbe);
        }
        block = block.next();
    }
}

}  // namespace mg::editor
