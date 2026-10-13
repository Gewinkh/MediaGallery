#pragma once
#include <QtGlobal>

#ifdef Q_OS_LINUX
#include <malloc.h>   // malloc_trim, mallopt
#endif

// RSS nach großen Freigaben ans OS zurückgeben: glibcs free() behält großen Heap in seinen Arenen, der RSS
// bleibt hoch, obwohl er intern frei ist. NUR nach großen Freigaben rufen; andere Plattformen: No-Op.
namespace mg {

//  Feste Schwellen statt glibcs dynamischer: nach einer grossen Freigabe (dekodierte CSV, 23 MB) stieg die
//  Trim-Schwelle auf ~46 MB, und das freie Ende der Arena eines Arbeitsfadens blieb im RSS - malloc_trim
//  erreicht es nicht. Alle Ansichten nacheinander: 406 -> 347 MB, Video-CPU gleich. Vor dem ersten Faden rufen.
inline void festeHeapSchwellen() {
#ifdef Q_OS_LINUX
    mallopt(M_MMAP_THRESHOLD, 1 << 20);
    mallopt(M_TRIM_THRESHOLD, 128 << 10);
#endif
}

inline void trimHeap() {
#ifdef Q_OS_LINUX
    malloc_trim(0);
#endif
}

} // namespace mg
