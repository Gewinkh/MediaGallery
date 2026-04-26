#pragma once
#include <QColor>
#include <QString>
#include <QPainter>
#include <QRect>
#include <QLinearGradient>
#include <cmath>
#include "AppSettings.h"

class Style {
public:
    // Main application stylesheet (reads current theme from AppSettings)
    static QString mainStyleSheet(const QColor& bg = QColor(), const QColor& accent = QColor());

    // Scrollbar stylesheet
    static QString scrollBarStyle();

    // PDF viewer stylesheet (sidebar, scrollbars, toolbar)
    static QString pdfViewerStyle();

    // Sidebar/tag-panel background
    static QString sidebarStyle();

    // Thumbnail card stylesheet
    static QString thumbnailStyle();

    // Generic accent button style
    static QString buttonStyle(const QColor& accent);

    // Paint tile background respecting theme (gradient / solid / transparent)
    static void paintTileBackground(QPainter& p, const QRect& rect);

    // Resolve accent color (first stop) from current theme
    static QColor resolveAccent();

    // Accent gradient across a rectangle
    static QLinearGradient accentGradient(const QRectF& rect);
};
