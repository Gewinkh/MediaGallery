#pragma once
#include "media/MediaItem.h"

#include <QString>

// Der Blick in die Datei fuer die eine doppelt belegte Endung: .ts ist TypeScript ODER
// MPEG-Transportstrom. Erkannt am Synchronbyte 0x47 in festem Raster (188/192/204).
// Alles andere - auch die LEERE Datei - gilt als Text: ein leeres Video gibt es nicht.
namespace mg {

//  Sieht der Dateianfang nach einem MPEG-Transportstrom aus?
bool looksLikeTransportStream(const QString& path);

//  Den lexikalisch bestimmten Typ nachbessern, wo die Endung mehrdeutig ist.
//  Fuer alles andere gibt sie `lexical` unveraendert zurueck - der Aufrufer
//  braucht also keine Fallunterscheidung.
MediaType refineType(const QString& path, MediaType lexical);

}  // namespace mg
