#include "simd/FindChar.h"

namespace mg::simd {

//  Das MASS. Sie wird auf jeder Plattform gebaut, ist der Rueckfall und der
//  Vergleichswert jedes Treibers. Bewusst die einfachste denkbare Fassung -
//  eine, bei der man ohne Nachdenken sieht, dass sie stimmt.
qsizetype findeErstesPlain(const char16_t* p, qsizetype n, char16_t a, char16_t b) {
    for (qsizetype i = 0; i < n; ++i)
        if (p[i] == a || p[i] == b) return i;
    return n;
}

qsizetype findeErstesDreiPlain(const char16_t* p, qsizetype n,
                               char16_t a, char16_t b, char16_t c) {
    for (qsizetype i = 0; i < n; ++i)
        if (p[i] == a || p[i] == b || p[i] == c) return i;
    return n;
}

}  // namespace mg::simd
