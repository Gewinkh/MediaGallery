#pragma once

#include <QByteArray>
#include <QStringView>

#include <span>

//  Alle Bedien-Symbole als Daten im 24x24-Raster, ohne JavaScript und ohne Heap.
namespace mg::icons {

//  Alle schraegen Striche eines Symbols teilen sich einen `Shape` und damit eine Strichbreite.
struct Stroke { double x0, y0, x1, y1, width; };
struct Box    { double x, y, w, h, radius; };
struct Ring   { double x, y, r; };
struct Fill   { double x, y, w, h, radius, opacity; };

struct Symbol {
    const char* name;
    std::span<const Stroke> strokes;
    std::span<const Box> boxes;
    std::span<const Ring> rings;
    std::span<const Fill> fills;
    const char* letter;
    double letterSize;
    double letterY;
    const char* curve;
};

const Symbol* findSymbol(QStringView name);
std::span<const Symbol> allSymbols();

//  QML ohne Bindungen; das Item uebersetzt es einmal je Engine und setzt nur noch Farben.
QByteArray curveSource(const Symbol& symbol);

}  // namespace mg::icons
