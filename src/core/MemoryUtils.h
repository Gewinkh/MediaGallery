#pragma once
#include <QtGlobal>

#ifdef Q_OS_LINUX
#include <malloc.h>   // malloc_trim
#endif

// RSS nach großen Freigaben ans OS zurückgeben: glibcs free() behält großen Heap in seinen Arenen, der RSS
// bleibt hoch, obwohl er intern frei ist. NUR nach großen Freigaben rufen; andere Plattformen: No-Op.
namespace mg {

inline void trimHeap() {
#ifdef Q_OS_LINUX
    malloc_trim(0);
#endif
}

} // namespace mg
