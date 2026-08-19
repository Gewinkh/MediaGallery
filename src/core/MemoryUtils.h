#pragma once
#include <QtGlobal>

#ifdef Q_OS_LINUX
#include <malloc.h>   // malloc_trim
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  MemoryUtils - RSS nach großen Freigaben aktiv ans OS zurückgeben.
//
//  Hintergrund (Linux/glibc): free() gibt großen Heap oft NICHT sofort an das
//  Betriebssystem zurück (Arena-Retention) - der RSS bleibt nach Cache-
//  Freigaben unnötig hoch, obwohl der Speicher intern frei ist. malloc_trim(0)
//  stößt die Rückgabe explizit an.
//
//  Einsatzregel: NUR nach großen Freigaben aufrufen (Ordnerwechsel, LRU-
//  Verdrängungen im MB-Bereich) - der Aufruf hat Overhead und bringt bei
//  kleinen Freigaben nichts. Andere Plattformen: No-Op (Windows/macOS geben
//  über ihre Allokatoren selbstständig zurück).
// ─────────────────────────────────────────────────────────────────────────────
namespace mg {

inline void trimHeap() {
#ifdef Q_OS_LINUX
    malloc_trim(0);
#endif
}

} // namespace mg
