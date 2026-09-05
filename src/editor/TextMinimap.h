#pragma once
#include <QColor>
#include <QQuickPaintedItem>
#include <QQuickTextDocument>

// Zeigt die ganze Datei als farbige Balken, nicht als Text - bei drei Pixel Zeilenhoehe
// waere Text unlesbar und teuer. Die Farben kommen aus den Bloecken des Faerbers
// (layout()->formats()), ein eigener Zerleger-Lauf waere ein zweiter voller Durchgang.
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
