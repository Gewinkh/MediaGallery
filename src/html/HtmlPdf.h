#pragma once
//  HtmlPdf - das Dokument als A4-PDF, neu gelegt wie beim Drucken im Browser; im Arbeitsfaden.
#include "html/HtmlDom.h"
#include <QList>
#include <QString>
#include <QUrl>
#include <atomic>
#include <span>
#include <vector>

namespace mg::html {

struct Layout;
struct Kastenbaum;
struct Stile;

struct PdfOptionen {
    bool druck = false;         // @media print, weisses Papier, helle Schrift abgedunkelt
    bool quer = false;
    int von = 1;                // 1-basiert
    int bis = 0;                // 0 = bis zum Ende
    QList<int> seiten;          // einzelne Seiten, 1-basiert; sticht von/bis
    QString quelle;             // Pfad der Datei, fuer relative Verweise
};

//  Wie beim Klick: Web/Mail nach aussen, `#id` als Knoten (0: Seitenanfang), eine andere vorhandene Datei als Pfad.
struct VerweisZiel {
    QUrl aussen{};
    uint32_t knoten = kKein;
    QString datei{};
};
VerweisZiel verweisZiel(const Dokument& d, uint32_t verweis, const QString& quelle);

struct SeitenKopf {
    float oben = 0, unten = 0;          // thead, den die Seite oben wiederholt (sie ist um ihn kuerzer); gleich: keiner
};

std::vector<float> seitenSchnitte(const Dokument& d, const Layout& l, const Kastenbaum& k, const Stile& s,
                                  float seitenHoehe, float ersteMehr = 0, std::vector<SeitenKopf>* koepfe = nullptr);

//  Leeres `ziel` zaehlt nur. Seitenzahl des ganzen Dokuments, -1 bei Fehler; `zustand` wie bei berechne().
int schreibePdf(const Dokument& d, std::span<const uint8_t> zustand, const PdfOptionen& o, const QString& ziel,
                QString* fehler = nullptr, const std::atomic<bool>* abbruch = nullptr);

}   // namespace mg::html
