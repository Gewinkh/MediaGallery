#pragma once
#include <QColor>
#include <QQuickPaintedItem>
#include <QQuickTextDocument>

// ─────────────────────────────────────────────────────────────────────────────
//  TextMinimap.h - die Uebersichtsspalte des Texteditors (Kates „Minimap").
//
//  Zeigt die GANZE Datei stark verkleinert: je Zeile ein paar Pixel hoch, die
//  Zeichen als farbige Balken in den Farben der Syntaxfaerbung. Man sieht damit
//  die FORM des Dokuments - Einrueckungen, Bloecke, Leerzeilen - und kann darin
//  scrollen, ohne sie zu lesen.
//
//  ZWEI Entscheidungen tragen das Ganze:
//
//  1. **Es wird kein Text gezeichnet, sondern Balken.** Bei zwei bis drei Pixel
//     Zeilenhoehe waere Text ohnehin nicht lesbar, und ihn zu rastern kostete
//     je Zeile ein Vielfaches. Balken sagen dasselbe: wo Code steht, wo
//     eingerueckt ist, wo eine Zeichenkette beginnt.
//
//  2. **Die Farben werden NICHT neu berechnet.** Der Faerber hat sie laengst in
//     die Bloecke geschrieben (`QTextBlock::layout()->formats()`); die Minimap
//     liest sie nur ab. Ein eigener Durchlauf durch den Zerleger waere ein
//     zweiter voller Durchgang durch die Datei - fuer dieselbe Information.
//
//  Gemalt werden nur die Zeilen, die gerade in der Spalte stehen (rund 250 bei
//  3 px Zeilenhoehe), nie das ganze Dokument. Passt die Datei nicht in die
//  Spalte, scrollt die Spalte SELBST mit - im Verhaeltnis zum Dokument.
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::editor {

class TextMinimap : public QQuickPaintedItem {
    Q_OBJECT

    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    //  Scrollstand und Sichthoehe der `Flickable` daneben, dazu die Gesamthoehe
    //  des Inhalts - daraus entsteht der Sichtfenster-Rahmen.
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY viewChanged)
    Q_PROPERTY(qreal viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY viewChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight WRITE setContentHeight NOTIFY viewChanged)

    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY styleChanged)
    Q_PROPERTY(QColor viewportColor READ viewportColor WRITE setViewportColor NOTIFY styleChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY styleChanged)

public:
    explicit TextMinimap(QQuickItem* parent = nullptr);

    void paint(QPainter* p) override;

    QQuickTextDocument* document() const { return m_quickDoc; }
    void setDocument(QQuickTextDocument* d);

    qreal contentY() const { return m_contentY; }
    void  setContentY(qreal v);
    qreal viewportHeight() const { return m_viewportH; }
    void  setViewportHeight(qreal v);
    qreal contentHeight() const { return m_contentH; }
    void  setContentHeight(qreal v);

    QColor backgroundColor() const { return m_background; }
    void   setBackgroundColor(const QColor& c);
    QColor textColor() const { return m_textColor; }
    void   setTextColor(const QColor& c);
    QColor viewportColor() const { return m_viewportColor; }
    void   setViewportColor(const QColor& c);
    QColor borderColor() const { return m_borderColor; }
    void   setBorderColor(const QColor& c);

signals:
    void documentChanged();
    void viewChanged();
    void styleChanged();
    //  Der Nutzer hat in der Spalte geklickt oder gezogen. Die Oberflaeche
    //  setzt daraufhin `contentY` der `Flickable` - die Spalte scrollt NICHT
    //  selbst, sie bittet nur darum. So bleibt EINE Stelle fuer den Scrollstand.
    void scrollRequested(qreal contentY);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    QTextDocument* doc() const;
    //  Wie weit die Spalte selbst verschoben ist, wenn die Datei nicht hineinpasst.
    qreal spaltenVersatz() const;
    //  Sichtbare Bloecke, aus dem Zwischenspeicher.
    int   sichtbareAnzahl() const;
    void  anKlickScrollen(qreal y);

    QQuickTextDocument* m_quickDoc = nullptr;
    qreal  m_contentY = 0;
    qreal  m_viewportH = 0;
    qreal  m_contentH = 0;
    QColor m_background    = QColor(20, 24, 29);
    QColor m_textColor     = QColor(216, 222, 233);
    QColor m_viewportColor = QColor(255, 255, 255, 28);
    QColor m_borderColor   = QColor(0, 0, 0, 0);
    //  Anzahl der SICHTBAREN Bloecke (zugeklappte zaehlen nicht mit). Wird nur
    //  neu gezaehlt, wenn sich am Dokument etwas geaendert hat - beim blossen
    //  Scrollen waere ein Durchgang ueber 200 000 Bloecke je Bild zu teuer.
    mutable int  m_sichtbar = 0;
    mutable bool m_zaehlungAlt = true;
};

}  // namespace mg::editor
