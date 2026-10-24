#pragma once
//  HtmlAuswahl - Markieren, Kopieren und Suchen ueber dem Layout: Stellen im Absatztext, Flaechen je Zeile, Klartext.
#include "html/HtmlPaint.h"

#include <QRectF>
#include <QTransform>

#include <atomic>

namespace mg::html {

struct Stelle {
    uint32_t absatz = kKein;
    int32_t zeichen = 0;                // UTF-16 im Absatztext, die Einfuegemarke steht davor
    bool gueltig() const { return absatz != kKein; }
    bool operator==(const Stelle&) const = default;
};

struct Bereich {
    Stelle von, bis;                    // [von, bis), von liegt vorn
};

struct Abbildung {
    QTransform t;
    QRectF clip;                        // nach t, Dokumentkoordinaten
};

struct Abschnitt {
    uint32_t absatz = kKein;
    int32_t von = 0, bis = 0;
    bool anschluss = false;             // ohne Zeilenumbruch an den vorigen
};

struct Zelle {
    uint32_t zeile = kKein, tabelle = kKein, spalte = 0;
};

//  Lesereihenfolge nach Kasten, ein Inline-Block an seiner Kante im Wirt; je Lauf/Zeile die Abbildung wie gemalt.
struct Ordnung {
    std::vector<Abschnitt> folge;
    std::vector<uint32_t> abschnitte, erster;   // Absatz a: folge[abschnitte[erster[a] .. erster[a + 1])]
    std::vector<uint32_t> lauf, zeile;
    std::vector<Abbildung> abbildungen;
    std::vector<Zelle> zellen;          // je Absatz seine innerste Tabellenzelle; leer ohne Tabellen oder Kastenbaum
};
Ordnung ordne(const Layout& l, const Anzeigeliste* a = nullptr, const Kastenbaum* k = nullptr);

bool vor(const Ordnung& o, const Stelle& a, const Stelle& b);
Bereich bereich(const Ordnung& o, const Stelle& a, const Stelle& b);

//  Naechste gemalte Zeile; `glyphe`: die Stelle der Glyphe statt der Einfuegemarke an ihrer naeheren Kante.
Stelle treffe(const Layout& l, const Ordnung& o, float x, float y, bool glyphe = false);
Bereich wortUm(const Layout& l, const Stelle& s);
Bereich absatzUm(const Layout& l, const Stelle& s);
Bereich alles(const Layout& l, const Ordnung& o);

void flaechen(const Layout& l, const Ordnung& o, const Bereich& b, float y0, float y1, std::vector<Rechteck>& aus);

QString klartext(const Layout& l, const Ordnung& o, const Bereich& b);

std::vector<Bereich> suche(const Layout& l, const Ordnung& o, const QString& muster, bool gross, bool woerter,
                           size_t grenze, bool* ueberlauf, const std::atomic<bool>* abbruch);

//  Oberkante der Zeile, in der `s` steht, wie gemalt; NaN ohne Zeile oder ungemalt (transform darf ueber 0 hinaus).
float zeilenOben(const Layout& l, const Ordnung& o, const Stelle& s);

Stelle obersteZeile(const Layout& l, const Ordnung& o, float y0, float y1);

}   // namespace mg::html
