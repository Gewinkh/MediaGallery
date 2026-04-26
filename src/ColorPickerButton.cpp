#include "ColorPickerButton.h"
#include <QColorDialog>

ColorPickerButton::ColorPickerButton(const QColor& color, QWidget* parent)
    : QPushButton(parent), m_color(color)
{
    setFixedSize(32, 22);
    updateButtonStyle();
    connect(this, &QPushButton::clicked, this, &ColorPickerButton::pickColor);
}

void ColorPickerButton::setColor(const QColor& c) {
    if (m_color == c) return;
    m_color = c;
    updateButtonStyle();
}

void ColorPickerButton::pickColor() {
    QColor c = QColorDialog::getColor(m_color, this, tr("Pick Color"),
                                      QColorDialog::ShowAlphaChannel);
    if (c.isValid()) {
        setColor(c);
        emit colorChanged(c);
    }
}

void ColorPickerButton::updateButtonStyle() {
    setStyleSheet(QString(
        "QPushButton { background: %1; border: 2px solid rgba(255,255,255,0.3);"
        "border-radius: 4px; }"
        "QPushButton:hover { border: 2px solid rgba(255,255,255,0.7); }"
    ).arg(m_color.name(QColor::HexArgb)));
}
