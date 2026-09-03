#include "media/ContentSniff.h"

#include <QFile>

namespace mg {
namespace {

//  Synchronbyte jedes TS-Pakets.
constexpr char kSync = '\x47';
//  Die drei ueblichen Paketlaengen: klassisch, M2TS (vier Byte Zeitstempel
//  davor) und mit Fehlerkorrektur.
constexpr int kPaketLaengen[] = { 188, 192, 204 };
//  So viele Pakete muessen im Raster liegen. Vier hintereinander sind kein
//  Zufall mehr; mehr zu verlangen hiesse, kurze Dateien zu verwerfen.
constexpr int kPakete = 4;
//  Mehr als das wird nie gelesen - der Anfang genuegt.
constexpr qint64 kLeseGrenze = 1024;

//  Liegt ab `start` in jedem `laenge`-Schritt ein Synchronbyte?
bool rasterPasst(const QByteArray& kopf, int start, int laenge) {
    if (kopf.size() < start + laenge * (kPakete - 1) + 1) return false;
    for (int k = 0; k < kPakete; ++k)
        if (kopf.at(start + k * laenge) != kSync) return false;
    return true;
}

}  // namespace

bool looksLikeTransportStream(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray kopf = f.read(kLeseGrenze);
    f.close();
    if (kopf.isEmpty()) return false;          // eine leere Datei ist kein Video

    for (const int laenge : kPaketLaengen) {
        //  Start 0 fuer 188/204, Start 4 fuer M2TS - dort stehen vier Byte
        //  Zeitstempel vor dem Synchronbyte.
        if (rasterPasst(kopf, 0, laenge)) return true;
        if (laenge == 192 && rasterPasst(kopf, 4, laenge)) return true;
    }
    return false;
}

MediaType refineType(const QString& path, MediaType lexical) {
    //  NUR die eine mehrdeutige Endung - jede andere Datei wird nicht angefasst.
    if (mg::suffixView(path).compare(QLatin1String("ts"), Qt::CaseInsensitive) != 0)
        return lexical;
    return looksLikeTransportStream(path) ? MediaType::Video : MediaType::Text;
}

}  // namespace mg
