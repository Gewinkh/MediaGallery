#pragma once
// Weicher Schatten unter Aufklappern ohne Live-Weichzeichner: EIN vorab weichgezeichnetes Neun-Teile-Bild je
// (Radius, Weite, Staerke), aus Bildknoten zusammengesetzt; der Software-Zeichner kopiert dabei nur Bildstuecke.

#include <QColor>
#include <QQuickItem>

class ShadowItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY changed)
    Q_PROPERTY(qreal spread READ spread WRITE setSpread NOTIFY changed)
    Q_PROPERTY(qreal strength READ strength WRITE setStrength NOTIFY changed)
    Q_PROPERTY(qreal offsetY READ offsetY WRITE setOffsetY NOTIFY changed)

public:
    explicit ShadowItem(QQuickItem* parent = nullptr);

    qreal radius() const { return m_radius; }
    void setRadius(qreal v);
    qreal spread() const { return m_spread; }
    void setSpread(qreal v);
    qreal strength() const { return m_strength; }
    void setStrength(qreal v);
    qreal offsetY() const { return m_offsetY; }
    void setOffsetY(qreal v);

    //  Das Neun-Teile-Bild in Geraetepixeln; Kante = Weite + Radius, Mitte 1 px. Fuer den Test oeffentlich.
    static QImage shadowImage(int radiusPx, int spreadPx, int strength255);

signals:
    void changed();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    void neu();

    qreal m_radius = 8;
    qreal m_spread = 18;
    qreal m_strength = 0.45;
    qreal m_offsetY = 6;
    bool m_bildNeu = true;
};
