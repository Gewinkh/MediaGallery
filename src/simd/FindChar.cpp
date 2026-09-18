#include "simd/FindChar.h"

#include "simd/Dispatch.h"

namespace mg::simd {
namespace {

using Fn = qsizetype (*)(const char16_t*, qsizetype, char16_t, char16_t);

//  Einmal gewaehlt, danach nur noch gerufen. Die Wahl haengt an nichts, was
//  sich waehrend des Laufs aendern koennte.
Fn waehle() {
    switch (gewaehlt()) {
#if MG_SIMD_HAVE_ASM
    case Weg::AsmAvx2: return &mg_finde_erstes_avx2_sysv;
#endif
#if MG_SIMD_X86
    case Weg::Avx2: return &findeErstesAvx2;
    case Weg::Sse2: return &findeErstesSse2;
#endif
#if MG_SIMD_ARM
    case Weg::Neon: return &findeErstesNeon;
#endif
    default: break;
    }
    return &findeErstesPlain;
}

}  // namespace

qsizetype findeErstes(const char16_t* p, qsizetype n, char16_t a, char16_t b) {
    static const Fn f = waehle();
    return f(p, n, a, b);
}

namespace {

using Fn3 = qsizetype (*)(const char16_t*, qsizetype, char16_t, char16_t, char16_t);

//  Dieselbe Weiche, nur ohne Assembly-Zweig - die gibt es fuer drei Zeichen
//  nicht. `MG_SIMD_WEG=asm` faellt deshalb auf AVX2 zurueck, damit ein
//  erzwungener Weg auch hier gilt und eine Messung beide Fassungen trifft.
Fn3 waehle3() {
    switch (gewaehlt()) {
#if MG_SIMD_X86
    case Weg::AsmAvx2:
    case Weg::Avx2: return &findeErstesDreiAvx2;
    case Weg::Sse2: return &findeErstesDreiSse2;
#endif
#if MG_SIMD_ARM
    case Weg::Neon: return &findeErstesDreiNeon;
#endif
    default: break;
    }
    return &findeErstesDreiPlain;
}

}  // namespace

qsizetype findeErstesDrei(const char16_t* p, qsizetype n,
                          char16_t a, char16_t b, char16_t c) {
    static const Fn3 f = waehle3();
    return f(p, n, a, b, c);
}

}  // namespace mg::simd
