#pragma once

#include <QColor>
#include <QList>
#include <QQuickItem>
#include <QString>

namespace mg::icons { struct Symbol; }

//  Ein Bedien-Symbol. Die Formen sind echte `Rectangle`/`Shape`/`Text`, also dieselben Pixel und dieselbe
//  Stapelbildung wie aus QML; es fallen nur Delegate-Modelle, Bindungen und JavaScript je Instanz weg.
class DrawnIconItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(qreal size READ iconSize WRITE setIconSize NOTIFY iconSizeChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit DrawnIconItem(QQuickItem* parent = nullptr);

    QString name() const { return m_name; }
    void setName(const QString& name);
    qreal iconSize() const { return m_size; }
    void setIconSize(qreal size);
    QColor color() const { return m_color; }
    void setColor(const QColor& color);

signals:
    void nameChanged();
    void iconSizeChanged();
    void colorChanged();

protected:
    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    enum class Kind : quint8 { Solid, Frame, Letter, Stroke, Curve };
    struct Path { QObject* object; bool stroke; bool fill; };
    struct Part {
        QQuickItem* item;
        Kind kind;
        //  Solid: leer; Frame: Rahmen; Stroke: ShapePath, PathMultiline; Curve: bis zu zwei ShapePath.
        Path first{nullptr, false, false};
        Path second{nullptr, false, false};
    };

    void buildIfShown();
    void clearParts();
    void arrange();
    void placeLetter();
    void applyColor(const Part& part);
    QQuickItem* adopt(QObject* created);

    QString m_name;
    const mg::icons::Symbol* m_symbol = nullptr;
    qreal m_size = 16;
    QColor m_color = Qt::white;
    QList<Part> m_parts;
    bool m_built = false;
};
