#pragma once
#include "media/MediaItem.h"

#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  ContentSniff.h - der Blick IN die Datei, wenn die Endung nicht reicht.
//
//  `MediaItem::detectType` entscheidet rein lexikalisch und fasst das
//  Dateisystem NICHT an - das ist Absicht: es laeuft je Datei beim Aufbauen der
//  Galerie, und ein Oeffnen je Eintrag waere dort spuerbar. Genau eine Endung
//  ist aber doppelt belegt:
//
//      `.ts`  =  TypeScript-Quelltext   ODER   MPEG-Transportstrom (Video)
//
//  Deshalb dieser eine, sehr kleine Zusatzschritt: er liest den ANFANG der
//  Datei (ein Kilobyte) und entscheidet an der Signatur. Er wird NUR fuer die
//  mehrdeutige Endung gerufen - jede andere Datei kostet ihn nichts.
//
//  Woran ein Transportstrom zu erkennen ist: er besteht aus Paketen fester
//  Laenge, und jedes beginnt mit dem Synchronbyte **0x47**. Ueblich sind 188
//  Byte (klassisch), 192 (M2TS/Blu-ray, mit vier Byte Zeitstempel davor) und
//  204 (mit Fehlerkorrektur). Vier Pakete in Folge im selben Raster sind ein
//  Zufall von rund 1 zu 4 Milliarden - das genuegt.
//
//  Alles andere - auch die LEERE Datei - gilt als Text. Ein leeres Video gibt
//  es nicht, eine leere Quelldatei sehr wohl (Nutzerbefund 2026-09-03).
// ─────────────────────────────────────────────────────────────────────────────
namespace mg {

//  Sieht der Dateianfang nach einem MPEG-Transportstrom aus?
bool looksLikeTransportStream(const QString& path);

//  Den lexikalisch bestimmten Typ nachbessern, wo die Endung mehrdeutig ist.
//  Fuer alles andere gibt sie `lexical` unveraendert zurueck - der Aufrufer
//  braucht also keine Fallunterscheidung.
MediaType refineType(const QString& path, MediaType lexical);

}  // namespace mg
