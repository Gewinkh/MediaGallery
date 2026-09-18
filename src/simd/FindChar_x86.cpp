#include "simd/FindChar.h"

#if MG_SIMD_X86

#include <immintrin.h>

namespace mg::simd {

//  Beide Fassungen arbeiten gleich: 8 bzw. 16 Zeichen auf einmal vergleichen,
//  die Treffer als Bitmaske einsammeln, das niedrigste gesetzte Bit zaehlen.
//  `movemask` liefert ZWEI Bits je 16-Bit-Zeichen, deshalb die Halbierung.
//  Der Rest hinter dem letzten vollen Block laeuft schlicht - ihn zu
//  vektorisieren hiesse, ueber das Ende zu lesen.

//  DIE LAENGENPRUEFUNG STEHT VOR DEM AUFBAU DER VEKTOREN, nicht danach.
//  Der Zerleger ruft die Suche je FELD, und ein DATEV-Stapel hat 125 meist sehr
//  kurze Felder: dort wurde zweimal ein 256-Bit-Register gefuellt, um danach
//  doch Zeichen fuer Zeichen zu laufen. Gemessen kostete das den ganzen Gewinn.
__attribute__((target("sse2")))
qsizetype findeErstesSse2(const char16_t* p, qsizetype n, char16_t a, char16_t b) {
    if (n < 8) {
        for (qsizetype i = 0; i < n; ++i)
            if (p[i] == a || p[i] == b) return i;
        return n;
    }
    const __m128i va = _mm_set1_epi16(static_cast<short>(a));
    const __m128i vb = _mm_set1_epi16(static_cast<short>(b));
    qsizetype i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
        const __m128i tr = _mm_or_si128(_mm_cmpeq_epi16(v, va), _mm_cmpeq_epi16(v, vb));
        const int maske = _mm_movemask_epi8(tr);
        if (maske) return i + __builtin_ctz(static_cast<unsigned>(maske)) / 2;
    }
    for (; i < n; ++i)
        if (p[i] == a || p[i] == b) return i;
    return n;
}

__attribute__((target("avx2")))
qsizetype findeErstesAvx2(const char16_t* p, qsizetype n, char16_t a, char16_t b) {
    if (n < 16) {
        for (qsizetype i = 0; i < n; ++i)
            if (p[i] == a || p[i] == b) return i;
        return n;
    }
    const __m256i va = _mm256_set1_epi16(static_cast<short>(a));
    const __m256i vb = _mm256_set1_epi16(static_cast<short>(b));
    qsizetype i = 0;
    for (; i + 16 <= n; i += 16) {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
        const __m256i tr = _mm256_or_si256(_mm256_cmpeq_epi16(v, va),
                                           _mm256_cmpeq_epi16(v, vb));
        const int maske = _mm256_movemask_epi8(tr);
        if (maske) return i + __builtin_ctz(static_cast<unsigned>(maske)) / 2;
    }
    for (; i < n; ++i)
        if (p[i] == a || p[i] == b) return i;
    return n;
}

//  Dieselbe Form mit drei gesuchten Zeichen - ein Vergleich und ein Oder mehr.
__attribute__((target("sse2")))
qsizetype findeErstesDreiSse2(const char16_t* p, qsizetype n,
                              char16_t a, char16_t b, char16_t c) {
    if (n < 8) {
        for (qsizetype i = 0; i < n; ++i)
            if (p[i] == a || p[i] == b || p[i] == c) return i;
        return n;
    }
    const __m128i va = _mm_set1_epi16(static_cast<short>(a));
    const __m128i vb = _mm_set1_epi16(static_cast<short>(b));
    const __m128i vc = _mm_set1_epi16(static_cast<short>(c));
    qsizetype i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
        const __m128i tr = _mm_or_si128(
            _mm_or_si128(_mm_cmpeq_epi16(v, va), _mm_cmpeq_epi16(v, vb)),
            _mm_cmpeq_epi16(v, vc));
        const int maske = _mm_movemask_epi8(tr);
        if (maske) return i + __builtin_ctz(static_cast<unsigned>(maske)) / 2;
    }
    for (; i < n; ++i)
        if (p[i] == a || p[i] == b || p[i] == c) return i;
    return n;
}

__attribute__((target("avx2")))
qsizetype findeErstesDreiAvx2(const char16_t* p, qsizetype n,
                              char16_t a, char16_t b, char16_t c) {
    if (n < 16) {
        for (qsizetype i = 0; i < n; ++i)
            if (p[i] == a || p[i] == b || p[i] == c) return i;
        return n;
    }
    const __m256i va = _mm256_set1_epi16(static_cast<short>(a));
    const __m256i vb = _mm256_set1_epi16(static_cast<short>(b));
    const __m256i vc = _mm256_set1_epi16(static_cast<short>(c));
    qsizetype i = 0;
    for (; i + 16 <= n; i += 16) {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
        const __m256i tr = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi16(v, va), _mm256_cmpeq_epi16(v, vb)),
            _mm256_cmpeq_epi16(v, vc));
        const int maske = _mm256_movemask_epi8(tr);
        if (maske) return i + __builtin_ctz(static_cast<unsigned>(maske)) / 2;
    }
    for (; i < n; ++i)
        if (p[i] == a || p[i] == b || p[i] == c) return i;
    return n;
}

}  // namespace mg::simd

#endif  // MG_SIMD_X86
