#include "editor/TextFoldBar.h"

#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

namespace mg::editor {
namespace {

//  Wie lange nach der letzten Eingabe gewartet wird, bevor neu erfasst wird.
//  Kurz genug, dass die Pfeile „sofort" stimmen, lang genug, dass beim Tippen
//  kein Durchgang laeuft.
constexpr int kSammelMs = 300;

//  Groesse des Dreiecks in Pixeln (Zielpixel, nicht ueber `scale` - ein
//  vergroessertes Rechteck bekommt seinen Glaettungssaum mitvergroessert).
constexpr qreal kPfeil = 5.0;

}  // namespace

TextFoldBar::TextFoldBar(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAntialiasing(true);
    m_timer.setSingleShot(true);
    m_timer.setInterval(kSammelMs);
    connect(&m_timer, &QTimer::timeout, this, &TextFoldBar::neuErfassen);
}

TextFoldBar::~TextFoldBar() {
    //  Die Datei wird geschlossen, das Dokument lebt aber weiter (die
    //  `TextArea` besitzt es): verborgene Bloecke wieder sichtbar machen,
    //  sonst bliebe der naechste Inhalt teilweise unsichtbar.
    if (QTextDocument* d = doc()) {
        if (!m_gefaltet.isEmpty()) {
            m_gefaltet.clear();
            for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
                b.setVisible(true);
        }
    }
}

QTextDocument* TextFoldBar::doc() const {
    return m_quickDoc ? m_quickDoc->textDocument() : nullptr;
}

qreal TextFoldBar::requiredWidth() const {
    //  Etwas breiter als der Pfeil, damit er sich anklicken laesst.
    return qMax(14.0, QFontMetricsF(m_font).height() * 0.9);
}

void TextFoldBar::setDocument(QQuickTextDocument* d) {
    if (d == m_quickDoc) return;
    if (QTextDocument* alt = doc()) disconnect(alt, nullptr, this, nullptr);
    m_quickDoc = d;
    m_gefaltet.clear();
    m_hatBereiche = false;

    if (QTextDocument* neu = doc()) {
        connect(neu, &QTextDocument::contentsChange, this,
                [this](int pos, int removed, int added) {
                    const LanguageDef& def = languageForPath(m_path);
                    if (def.fold == FoldKind::None) return;
                    //  Geloescht wird seltener als getippt - und WAS geloescht
                    //  wurde, weiss hier niemand mehr. Also immer erfassen.
                    if (removed > 0) { m_timer.start(); return; }
                    if (added <= 0) return;
                    QTextDocument* d = doc();
                    if (!d) return;
                    QTextCursor c(d);
                    c.setPosition(pos);
                    c.setPosition(qMin(pos + added, d->characterCount() - 1),
                                  QTextCursor::KeepAnchor);
                    if (touchesFolding(c.selectedText(), def.fold))
                        m_timer.start();
                });
        connect(neu, &QTextDocument::contentsChanged, this,
                [this] { emit documentHeightChanged(); update(); });
    }
    emit documentChanged();
    neuErfassen();
}

void TextFoldBar::setPath(const QString& p) {
    if (p == m_path) return;
    m_path = p;
    //  Neue Datei = neuer Anfang, auch fuer die Klebrigkeit der Leiste.
    m_gefaltet.clear();
    m_hatBereiche = false;
    emit pathChanged();
    neuErfassen();
}

void TextFoldBar::setTextArea(QQuickItem* i) {
    if (i == m_textArea) return;
    m_textArea = i;
    emit textAreaChanged();
}

void TextFoldBar::setFlickable(QQuickItem* i) {
    if (i == m_flick) return;
    m_flick = i;
    emit textAreaChanged();
}

qreal TextFoldBar::documentHeight() const {
    const QTextDocument* d = doc();
    return d ? d->documentLayout()->documentSize().height() : 0.0;
}

void TextFoldBar::setContentY(qreal v) {
    if (qFuzzyCompare(v, m_contentY)) return;
    m_contentY = v; emit viewChanged(); update();
}
void TextFoldBar::setTopPadding(qreal v) {
    if (qFuzzyCompare(v, m_topPadding)) return;
    m_topPadding = v; emit viewChanged(); update();
}
void TextFoldBar::setFont(const QFont& f) {
    if (f == m_font) return;
    m_font = f; emit styleChanged(); update();
}
void TextFoldBar::setTabWidth(int v) {
    const int w = qBound(2, v, 8);
    if (w == m_tabWidth) return;
    m_tabWidth = w; emit styleChanged(); neuErfassen();
}
void TextFoldBar::setBackgroundColor(const QColor& c) {
    if (c == m_background) return;
    m_background = c; emit styleChanged(); update();
}
void TextFoldBar::setMarkerColor(const QColor& c) {
    if (c == m_marker) return;
    m_marker = c; emit styleChanged(); update();
}

void TextFoldBar::neuErfassen() {
    m_timer.stop();
    const QList<FoldRegion> alt = m_bereiche;
    m_bereiche = scanFolds(doc(), languageForPath(m_path), m_tabWidth);

    //  Bereiche, die es nicht mehr gibt, koennen auch nicht mehr zugeklappt
    //  sein - sonst blieben Zeilen verborgen, zu denen nichts mehr gehoert.
    if (!m_gefaltet.isEmpty()) {
        QSet<int> gueltig;
        for (const FoldRegion& r : std::as_const(m_bereiche))
            if (m_gefaltet.contains(r.start)) gueltig.insert(r.start);
        if (gueltig.size() != m_gefaltet.size()) {
            m_gefaltet = gueltig;
            sichtbarkeitNeuSetzen();
        }
    }

    //  KLEBRIG: einmal sichtbar, bleibt sichtbar (s. Kopf).
    const bool vorher = m_hatBereiche;
    if (!m_bereiche.isEmpty()) m_hatBereiche = true;
    if (m_hatBereiche != vorher) emit regionsChanged();
    if (alt != m_bereiche) update();
}

const FoldRegion* TextFoldBar::bereichAb(int block) const {
    //  Der WEITESTE Bereich, der bei diesem Block beginnt (die Liste ist nach
    //  Start sortiert, bei gleichem Start der weitere zuerst). Binaer gesucht:
    //  gefragt wird je gemaltem Block, und eine grosse Quelldatei hat leicht
    //  fuenfhundert Bereiche.
    const auto it = std::lower_bound(m_bereiche.cbegin(), m_bereiche.cend(), block,
                                     [](const FoldRegion& r, int b) { return r.start < b; });
    if (it == m_bereiche.cend() || it->start != block) return nullptr;
    return &*it;
}

int TextFoldBar::foldedStartFor(int position) const {
    QTextDocument* d = doc();
    if (!d || m_gefaltet.isEmpty()) return -1;
    const int block = d->findBlock(position).blockNumber();
    int aeusserster = -1;
    for (const FoldRegion& r : m_bereiche) {
        if (!m_gefaltet.contains(r.start)) continue;
        if (block > r.start && block <= r.end)
            if (aeusserster < 0 || r.start < aeusserster) aeusserster = r.start;
    }
    return aeusserster;
}

bool TextFoldBar::ensureVisible(int position) {
    QTextDocument* d = doc();
    if (!d || m_gefaltet.isEmpty()) return false;
    const int block = d->findBlock(position).blockNumber();
    //  Es koennen mehrere ineinanderliegende Bereiche zugeklappt sein - alle,
    //  die `block` enthalten, muessen auf. Von aussen nach innen, damit das
    //  Aufklappen des aeusseren die inneren nicht wieder verbirgt.
    QList<FoldRegion> auf;
    for (const FoldRegion& r : std::as_const(m_bereiche))
        if (m_gefaltet.contains(r.start) && block > r.start && block <= r.end)
            auf.append(r);
    if (auf.isEmpty()) return false;

    const qreal vorher = documentHeight();
    const int oberster = auf.first().start;
    for (const FoldRegion& r : std::as_const(auf)) m_gefaltet.remove(r.start);
    for (const FoldRegion& r : std::as_const(auf)) bereichAnwenden(r, false);
    nachDemFalten(oberster, vorher, false);
    emit foldingChanged();
    emit documentHeightChanged();
    update();
    return true;
}

void TextFoldBar::unfoldAll() {
    if (m_gefaltet.isEmpty()) return;
    const qreal vorher = documentHeight();
    m_gefaltet.clear();
    sichtbarkeitNeuSetzen();
    nachDemFalten(0, vorher);
    emit foldingChanged();
    emit documentHeightChanged();
    update();
}

void TextFoldBar::bereichAnwenden(const FoldRegion& r, bool verbergen) {
    QTextDocument* d = doc();
    if (!d) return;

    QTextBlock b = d->findBlockByNumber(r.start + 1);
    const int erster = r.start + 1;
    for (int i = erster; i <= r.end && b.isValid(); ++i, b = b.next())
        b.setVisible(!verbergen);

    //  Beim AUFklappen bleiben innen liegende, weiterhin zugeklappte Bereiche
    //  zu - sonst verloere man beim Oeffnen des aeusseren jede innere
    //  Einstellung.
    if (!verbergen) {
        for (const FoldRegion& inner : std::as_const(m_bereiche)) {
            if (!m_gefaltet.contains(inner.start)) continue;
            if (inner.start <= r.start || inner.end > r.end) continue;
            QTextBlock ib = d->findBlockByNumber(inner.start + 1);
            for (int i = inner.start + 1; i <= inner.end && ib.isValid(); ++i, ib = ib.next())
                ib.setVisible(false);
        }
    }

    //  NUR den betroffenen Bereich neu umbrechen lassen. Frueher stand hier
    //  `markContentsDirty(0, characterCount())` plus ein Umlegen von
    //  `wrapMode`; das kostete gemessen 66 ms statt 1 ms auf 5 000 Zeilen -
    //  und `TextEdit` rollte dabei den Cursor ins Bild, sprang also an den
    //  Dateianfang, sobald man irgendwo unten zuklappte.
    const QTextBlock von = d->findBlockByNumber(r.start);
    const QTextBlock bis = d->findBlockByNumber(qMin(r.end + 1, d->blockCount() - 1));
    if (von.isValid() && bis.isValid())
        d->markContentsDirty(von.position(),
                             qMax(1, bis.position() + bis.length() - von.position()));
}

void TextFoldBar::sichtbarkeitNeuSetzen() {
    QTextDocument* d = doc();
    if (!d) return;
    //  Erst alles zeigen, dann jeden zugeklappten Bereich verbergen. Teuer
    //  (ein Durchgang ueber das ganze Dokument), deshalb nur nach einer
    //  Neuerfassung, in der Bereiche verschwunden sind.
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        b.setVisible(true);
    for (const FoldRegion& r : std::as_const(m_bereiche)) {
        if (!m_gefaltet.contains(r.start)) continue;
        QTextBlock b = d->findBlockByNumber(r.start + 1);
        for (int i = r.start + 1; i <= r.end && b.isValid(); ++i, b = b.next())
            b.setVisible(false);
    }
    d->markContentsDirty(0, d->characterCount());
}

void TextFoldBar::nachDemFalten(int startBlock, qreal hoeheVorher,
                                bool standHalten) {
    QTextDocument* d = doc();
    if (!d) return;

    //  Der Cursor darf nie im Verborgenen stehen - dort blinkte er unsichtbar,
    //  und getippt wuerde in eine Zeile, die niemand sieht.
    if (m_textArea) {
        const int pos = m_textArea->property("cursorPosition").toInt();
        const QTextBlock cb = d->findBlock(pos);
        if (cb.isValid() && !cb.isVisible()) {
            const int start = foldedStartFor(pos);
            const QTextBlock sb = d->findBlockByNumber(qMax(0, start));
            if (start >= 0 && sb.isValid())
                m_textArea->setProperty("cursorPosition",
                                        sb.position() + sb.length() - 1);
        }
    }

    //  ── Der Scrollstand ────────────────────────────────────────────────
    //  Nur EINE Sache, seit die `TextArea` kein `TextArea.flickable` mehr ist
    //  (s. `TextSurface.qml`): liegt die geaenderte Stelle KOMPLETT ueber der
    //  Sicht, verschiebt sich alles Darunterliegende - dann muss der Stand um
    //  dieselbe Hoehe mitwandern, sonst stuende ploetzlich anderer Text im
    //  Fenster. Klickt man auf einen sichtbaren Pfeil, bleibt alles stehen,
    //  ohne dass hier etwas zu tun waere.
    if (!m_flick || !standHalten) return;
    const QTextBlock sb = d->findBlockByNumber(startBlock);
    if (!sb.isValid()) return;
    const qreal ySt = d->documentLayout()->blockBoundingRect(sb).bottom();
    if (ySt > m_contentY) return;                 // die Stelle ist sichtbar
    const qreal delta = documentHeight() - hoeheVorher;
    if (qFuzzyIsNull(delta)) return;
    m_flick->setProperty("contentY", qMax(0.0, m_contentY + delta));
}


int TextFoldBar::blockBeiY(qreal y) const {
    QTextDocument* d = doc();
    if (!d) return -1;
    const qreal ziel = y + m_contentY - m_topPadding;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
        if (!b.isVisible()) continue;
        const QRectF r = d->documentLayout()->blockBoundingRect(b);
        if (r.bottom() < ziel) continue;
        if (r.top() > ziel) return -1;
        return b.blockNumber();
    }
    return -1;
}

bool TextFoldBar::toggleFold(int blockNumber) {
    const FoldRegion* r = bereichAb(blockNumber);
    if (!r) return false;
    const FoldRegion bereich = *r;                // Kopie: die Liste kann wandern
    const qreal vorher = documentHeight();

    const bool verbergen = !m_gefaltet.contains(blockNumber);
    if (verbergen) m_gefaltet.insert(blockNumber);
    else           m_gefaltet.remove(blockNumber);

    bereichAnwenden(bereich, verbergen);
    nachDemFalten(bereich.start, vorher);
    emit foldingChanged();
    emit documentHeightChanged();
    update();
    return true;
}

void TextFoldBar::mousePressEvent(QMouseEvent* e) {
    e->accept();
    const int block = blockBeiY(e->position().y());
    if (block >= 0) toggleFold(block);
}

void TextFoldBar::paint(QPainter* p) {
    p->fillRect(QRectF(0, 0, width(), height()), m_background);
    QTextDocument* d = doc();
    if (!d || m_bereiche.isEmpty()) return;

    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(m_marker);

    //  Beim ERSTEN SICHTBAREN Block anfangen, nicht vorne im Dokument - sonst
    //  liefe dieser Durchgang je Bild ueber die ganze Datei (dasselbe Muster
    //  wie in der Nummernspalte).
    QAbstractTextDocumentLayout* lay = d->documentLayout();
    QTextBlock start = d->findBlock(qMax(0, lay->hitTest(QPointF(0, m_contentY),
                                                          Qt::FuzzyHit)));
    if (!start.isValid()) start = d->firstBlock();
    if (start.previous().isValid()) start = start.previous();

    const qreal mitteX = width() / 2.0;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        if (!b.isVisible()) continue;
        const QRectF r = lay->blockBoundingRect(b);
        const qreal y = r.top() - m_contentY + m_topPadding;
        if (y > height()) break;
        if (y + r.height() < 0) continue;
        if (!bereichAb(b.blockNumber())) continue;

        //  Nur die ERSTE Bildschirmzeile des Absatzes traegt den Pfeil - bei
        //  weichem Umbruch belegt ein Absatz mehrere Zeilen.
        const QRectF ersteZeile = b.layout() && b.layout()->lineCount() > 0
                                      ? b.layout()->lineAt(0).rect()
                                      : QRectF(0, 0, 0, r.height());
        const qreal mitteY = y + ersteZeile.height() / 2.0;

        QPolygonF dreieck;
        if (m_gefaltet.contains(b.blockNumber())) {
            //  Zugeklappt: Spitze nach rechts („da steckt noch was").
            dreieck << QPointF(mitteX - kPfeil * 0.5, mitteY - kPfeil)
                    << QPointF(mitteX + kPfeil * 0.7, mitteY)
                    << QPointF(mitteX - kPfeil * 0.5, mitteY + kPfeil);
        } else {
            //  Offen: Spitze nach unten.
            dreieck << QPointF(mitteX - kPfeil, mitteY - kPfeil * 0.5)
                    << QPointF(mitteX + kPfeil, mitteY - kPfeil * 0.5)
                    << QPointF(mitteX, mitteY + kPfeil * 0.7);
        }
        p->drawPolygon(dreieck);
    }
}

}  // namespace mg::editor
