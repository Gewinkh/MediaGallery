#pragma once
//  HtmlPaint - Anzeigeliste in Zeichenreihenfolge und ihr Zeichnen ueber QPainter: eine Liste fuer Bildschirm und PDF.
#include "html/HtmlLayout.h"

#include <QImage>
#include <QTransform>

#include <memory>

class QPainter;
class QSvgRenderer;

namespace mg::html {

enum class BArt : uint8_t { Flaeche, Rahmen, Text, Linie, Verlauf, Bild, ClipAn, ClipText, ClipAus, TransformAn, TransformAus,
                            Verweis };  // Verweis: Klickflaeche eines Links (daten = Kasten), malt nichts, nur fuers PDF

struct Befehl {
    Rechteck r;
    uint32_t farbe = 0;
    uint32_t daten = kKein;             // Index in laeufe, rahmen, verlaeufe, bilder, transformationen, textMasken
    BArt art = BArt::Flaeche;
    bool hinten = false;                // background-color/-image: der Druckstil laesst ihn weg wie ein Browser
};

//  Seiten oben, rechts, unten, links; Ecken oben links, oben rechts, unten rechts, unten links, schon gekuerzt.
struct Rahmen {
    float breite[4] = {};
    uint32_t farbe[4] = {};
    RahmenStil stil[4] = {};
    float rx[4] = {}, ry[4] = {};
};

//  Schicht von background-image; linear (x1,y1) -> (x2,y2), radial um (x1,y1) mit Halbachsen x2, y2, relativ zur Kachel.
struct Verlauf {
    Rechteck kachel;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    uint32_t haltAnfang = 0, haltAnzahl = 0;
    uint32_t rahmen = kKein;            // Radien der Flaeche
    bool radial = false, wiederholenX = true, wiederholenY = true;
    bool fest = false;                  // background-attachment fixed: das PDF setzt die Kachel je Seite neu an
};

//  Inline-SVG, <img> oder background-image; Kachel wie bei Verlauf, ohne Wiederholung genau eine.
struct Bild {
    std::shared_ptr<QSvgRenderer> svg;  // gleiche URLs teilen ihn; gezeichnet nur im Faden, der die Liste baute
    QImage raster;
    Rechteck kachel;
    uint32_t rahmen = kKein;
    uint8_t deckkraft = 255;
    bool wiederholenX = false, wiederholenY = false;
    bool fest = false;
};

struct Halt {
    float lage = 0;                     // 0..1 auf der Verlaufslinie
    uint32_t farbe = 0;
};

//  fixed oder sticky: die Ansicht malt sie als eigene Textur und verschiebt sie beim Rollen um `versatz`.
inline constexpr float kSchichtMax = 2048;
struct Schicht {
    uint32_t anfang = 0, ende = 0;      // Befehle [anfang, ende), TransformAn bis TransformAus
    uint32_t knoten = kKein;
    Rechteck r;
    Rechteck kasten;
    float oben = NAN, unten = NAN;      // sticky: Versaetze; beide NAN bei fixed
    float grenzeOben = 0, grenzeUnten = 0;
    bool fest = false;

    float versatz(float y0, float h) const;
};

struct Anzeigeliste {
    std::vector<Befehl> befehle;
    std::vector<Rahmen> rahmen;
    std::vector<Verlauf> verlaeufe;
    std::vector<Halt> halte;
    std::vector<Bild> bilder;
    std::vector<QTransform> transformationen;   // CSS-transform samt transform-origin in Dokumentkoordinaten
    //  Schichten mit position fixed (Indizes in transformationen): das PDF verschiebt sie je Seite wie ein Browser.
    std::vector<uint32_t> fest;
    std::vector<Schicht> schichten;
    std::vector<uint32_t> textMasken;   // background-clip: text, je Maske die Anzahl, dann die Laeufe
    uint32_t leinwand = 0xFFFFFFFFu;    // Hintergrund von html bzw. body, deckt das ganze Dokument
    float breite = 0, hoehe = 0;
    size_t bytes() const;
};

//  `druck`: helle Farben in Inline-SVG abgedunkelt wie die Schrift (Druckstil des PDF).
Anzeigeliste baueAnzeige(const Dokument& d, const Stile& s, const Kastenbaum& k, const Layout& l, bool druck = false);
uint32_t dunkler(uint32_t argb);        // Helligkeit hoechstens 0,4, der Farbton bleibt

//  Nur beim PDF: Text nur mit der Grundlinie im Fenster; Verlaufsbilder und Kacheln ueber alle Seiten geteilt.
struct PdfMalen {
    struct Verweis {
        uint32_t kasten;
        QRectF r;                       // Geraetekoordinaten, beschnitten wie der Inhalt
    };
    std::vector<QImage> verlaufBilder;
    std::vector<QImage> bildKacheln;
    std::vector<Verweis> verweise;      // der Aufrufer leert sie je Seite
    Rechteck blatt;                     // ganze Seite in Dokumentkoordinaten: Fenster und Clip der fixed-Schichten
};
//  `kacheln`: gerasterte Musterkacheln bleiben (ein Faden); `schicht`: alles, ohne Schichten oder nur diese ohne Leinwand.
inline constexpr int32_t kAlleBefehle = -1, kOhneSchichten = -2;
void male(QPainter& p, const Anzeigeliste& a, const Layout& l, const Rechteck& fenster, PdfMalen* pdf = nullptr,
          std::vector<QImage>* kacheln = nullptr, int32_t schicht = kAlleBefehle);

}   // namespace mg::html
