#pragma once
#include <QPushButton>
#include <QColor>

class ColorPickerButton : public QPushButton {
    Q_OBJECT
public:
    explicit ColorPickerButton(const QColor& color = Qt::white, QWidget* parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor& c);

signals:
    void colorChanged(const QColor& color);

private slots:
    void pickColor();

private:
    QColor m_color;
    void updateButtonStyle();
};
