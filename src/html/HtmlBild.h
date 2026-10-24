#pragma once
//  HtmlBild - Bilder fuer <img> und background-image aus data:-URLs und lokalen Dateien; nie aus dem Netz.
#include <QImage>

#include <memory>
#include <string_view>

class QSvgRenderer;

namespace mg::html {

struct Masse {
    float b = 0, h = 0, verhaeltnis = 0;    // eigene Groesse in CSS-px, 0 = keine
};

struct Geladen {
    std::shared_ptr<QSvgRenderer> svg;
    QImage raster;
    Masse masse;
};

//  `basis`: Ordner der Datei fuer relative Pfade (leer: nur data: und file:). `kante` > 0 begrenzt die laengere Seite
//  eines Rasterbilds schon beim Dekodieren: ein Foto in einem kleinen Kasten hielte sonst seine volle Groesse im RAM.
Geladen ladeBild(std::string_view url, std::string_view basis, int kante = 0);
//  Nur die Masse; eine Datei wird dafuer nur im Kopf gelesen (das Layout fragt bei jedem Legen).
Masse bildMasse(std::string_view url, std::string_view basis);
//  Wert eines Attributs am ersten <svg>-Tag; leer, wenn es fehlt.
std::string_view wurzelAttribut(std::string_view xml, std::string_view name);

}   // namespace mg::html
