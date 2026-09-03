#include "editor/TextMinimap.h"

#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace mg::editor {
namespace {

//  Hoehe einer Zeile in der Spalte. Drei Pixel sind der Punkt, an dem die FORM
//  des Dokuments noch zu erkennen ist (Einrueckung, Bloecke, Leerzeilen) und
//  gleichzeitig genug Zeilen hineinpassen, damit die Spalte etwas nuetzt.
constexpr qreal kZeilenhoehe = 3.0;
//  Wie viele Spalten Text die Breite darstellt. Laengere Zeilen laufen rechts
//  aus - genau wie in Kate: die Minimap zeigt die Form, nicht den Inhalt.
constexpr int kSpalten = 90;

}  // namespace

TextMinimap::TextMinimap(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAntialiasing(false);   // Balken von wenigen Pixeln - Glaettung macht Matsch
}

QTextDocument* TextMinimap::doc() const {
    return m_quickDoc ? m_quickDoc->textDocument() : nullptr;
}

void TextMinimap::setDocument(QQuickTextDocument* d) {
    if (d == m_quickDoc) return;
    if (QTextDocument* alt = doc())
        disconnect(alt, nullptr, this, nullptr);

    m_quickDoc = d;

    if (QTextDocument* neu = doc()) {
        connect(neu, &QTextDocument::contentsChanged, this,
                [this] { m_zaehlungAlt = true; update(); });
        //  Auch auf `update` des Layouts hoeren: der Faerber schreibt seine
        //  Formate DORT hinein, ohne den Inhalt zu aendern - ohne dieses Signal
        //  bliebe die Spalte nach dem ersten Faerben grau.
        if (auto* lay = neu->documentLayout())
            connect(lay, &QAbstractTextDocumentLayout::update, this,
                    [this] { m_zaehlungAlt = true; update(); });
    }
    emit documentChanged();
    update();
}

void TextMinimap::setContentY(qreal v) {
    if (qFuzzyCompare(v, m_contentY)) return;
    m_contentY = v; emit viewChanged(); update();
}
void TextMinimap::setViewportHeight(qreal v) {
    if (qFuzzyCompare(v, m_viewportH)) return;
    m_viewportH = v; emit viewChanged(); update();
}
void TextMinimap::setContentHeight(qreal v) {
    if (qFuzzyCompare(v, m_contentH)) return;
    m_contentH = v; emit viewChanged(); update();
}
void TextMinimap::setBackgroundColor(const QColor& c) {
    if (c == m_background) return;
    m_background = c; emit styleChanged(); update();
}
void TextMinimap::setTextColor(const QColor& c) {
    if (c == m_textColor) return;
    m_textColor = c; emit styleChanged(); update();
}
void TextMinimap::setViewportColor(const QColor& c) {
    if (c == m_viewportColor) return;
    m_viewportColor = c; emit styleChanged(); update();
}
void TextMinimap::setBorderColor(const QColor& c) {
    if (c == m_borderColor) return;
    m_borderColor = c; emit styleChanged(); update();
}

int TextMinimap::sichtbareAnzahl() const {
    const QTextDocument* d = doc();
    if (!d) return 0;
    if (m_zaehlungAlt) {
        int n = 0;
        for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
            if (b.isVisible()) ++n;
        m_sichtbar = n;
        m_zaehlungAlt = false;
    }
    return m_sichtbar;
}

qreal TextMinimap::spaltenVersatz() const {
    const QTextDocument* d = doc();
    if (!d) return 0;
    const qreal gesamt = sichtbareAnzahl() * kZeilenhoehe;
    if (gesamt <= height()) return 0;          // die ganze Datei passt hinein

    //  Passt sie nicht, wandert die Spalte MIT dem Dokument - im selben
    //  Verhaeltnis. Am Anfang der Datei steht sie oben, am Ende unten.
    const qreal scrollbar = qMax(1.0, m_contentH - m_viewportH);
    const qreal anteil = qBound(0.0, m_contentY / scrollbar, 1.0);
    return anteil * (gesamt - height());
}

void TextMinimap::paint(QPainter* p) {
    p->fillRect(QRectF(0, 0, width(), height()), m_background);
    if (m_borderColor.alpha() > 0)
        p->fillRect(QRectF(0, 0, 1, height()), m_borderColor);

    QTextDocument* d = doc();
    if (!d || width() <= 0) return;

    const qreal versatz = spaltenVersatz();
    const qreal zeichenB = width() / qreal(kSpalten);

    //  Gezaehlt wird ueber die SICHTBAREN Bloecke - eine zugeklappte Funktion
    //  ist auch in der Uebersicht weg, sonst zeigte die Spalte etwas anderes
    //  als der Text daneben (s. `TextFoldBar`).
    const int ersterSichtbar = qMax(0, int(versatz / kZeilenhoehe));
    QTextBlock block;
    int lfd = 0;
    if (sichtbareAnzahl() == d->blockCount()) {
        //  Nichts zugeklappt - dann ist die laufende Nummer die Blocknummer,
        //  und der Einstieg kostet nichts. Der Normalfall.
        block = d->findBlockByNumber(ersterSichtbar);
        lfd = ersterSichtbar;
    } else {
        block = d->begin();
        while (block.isValid() && lfd < ersterSichtbar) {
            if (block.isVisible()) ++lfd;
            block = block.next();
        }
    }

    //  Grundfarbe fuer Text ohne eigene Faerbung, bewusst abgeschwaecht: die
    //  Syntaxfarben sollen die Form tragen, nicht ein Grauschleier.
    QColor grund = m_textColor;
    grund.setAlpha(110);

    while (block.isValid()) {
        if (!block.isVisible()) { block = block.next(); continue; }
        const qreal y = lfd * kZeilenhoehe - versatz;
        ++lfd;
        if (y > height()) break;
        if (y + kZeilenhoehe >= 0) {
            const QString text = block.text();
            //  Vom ersten bis zum letzten sichtbaren Zeichen - die Einrueckung
            //  bleibt dadurch leer, und genau daran erkennt man die Form.
            qsizetype von = 0;
            while (von < text.size() && text.at(von).isSpace()) ++von;
            qsizetype bis = text.size();
            while (bis > von && text.at(bis - 1).isSpace()) --bis;

            if (bis > von) {
                const qreal x0 = von * zeichenB;
                const qreal x1 = qMin(width(), bis * zeichenB);
                if (x1 > x0)
                    p->fillRect(QRectF(x0, y, x1 - x0, kZeilenhoehe - 1.0), grund);

                //  Die Farben des Faerbers daruebersetzen - sie stehen bereits
                //  im Block, es wird nichts neu gerechnet.
                if (const QTextLayout* lay = block.layout()) {
                    const auto bereiche = lay->formats();
                    for (const QTextLayout::FormatRange& fr : bereiche) {
                        if (fr.length <= 0) continue;
                        const QBrush b = fr.format.foreground();
                        if (b.style() == Qt::NoBrush) continue;
                        const qreal fx0 = fr.start * zeichenB;
                        const qreal fx1 = qMin(width(), (fr.start + fr.length) * zeichenB);
                        if (fx1 <= fx0 || fx0 >= width()) continue;
                        p->fillRect(QRectF(fx0, y, fx1 - fx0, kZeilenhoehe - 1.0),
                                    b.color());
                    }
                }
            }
        }
        block = block.next();
    }

    //  Sichtfenster-Rahmen: wo man gerade steht.
    if (m_contentH > 0 && m_viewportH > 0) {
        const qreal gesamt = sichtbareAnzahl() * kZeilenhoehe;
        const qreal oben = (m_contentY / m_contentH) * gesamt - versatz;
        const qreal hoch = qMax(6.0, (m_viewportH / m_contentH) * gesamt);
        p->fillRect(QRectF(0, oben, width(), hoch), m_viewportColor);
    }
}

void TextMinimap::anKlickScrollen(qreal y) {
    QTextDocument* d = doc();
    if (!d || m_contentH <= 0) return;
    const qreal gesamt = sichtbareAnzahl() * kZeilenhoehe;
    if (gesamt <= 0) return;

    //  Der Klickpunkt wird zur MITTE des Sichtfensters - so springt die Stelle
    //  unter dem Finger in die Bildmitte und nicht an den oberen Rand.
    const qreal anteil = qBound(0.0, (y + spaltenVersatz()) / gesamt, 1.0);
    const qreal ziel = anteil * m_contentH - m_viewportH / 2.0;
    emit scrollRequested(qBound(0.0, ziel, qMax(0.0, m_contentH - m_viewportH)));
}

void TextMinimap::mousePressEvent(QMouseEvent* e) {
    anKlickScrollen(e->position().y());
    e->accept();
}

void TextMinimap::mouseMoveEvent(QMouseEvent* e) {
    anKlickScrollen(e->position().y());
    e->accept();
}

}  // namespace mg::editor
