#include "simd/FindChar.h"

#if MG_SIMD_ARM

#include <arm_neon.h>

namespace mg::simd {

//  NEON kennt kein `movemask`. Stattdessen schmaelert `vshrn` jede 16-Bit-Spur
//  auf 8 Bit, und die 64 Bit werden am Stueck gelesen: das niedrigste gesetzte
//  BYTE ist der erste Treffer, daher `>> 3`. Mit `>> 2` kam `i + 2k` heraus.
qsizetype findeErstesNeon(const char16_t* p, qsizetype n, char16_t a, char16_t b) {
    //  Wie bei x86: erst die Laenge, dann die Register (s. `FindChar_x86.cpp`).
    if (n < 8) {
        for (qsizetype i = 0; i < n; ++i)
            if (p[i] == a || p[i] == b) return i;
        return n;
    }
    const uint16x8_t va = vdupq_n_u16(a);
    const uint16x8_t vb = vdupq_n_u16(b);
    qsizetype i = 0;
    for (; i + 8 <= n; i += 8) {
        const uint16x8_t v = vld1q_u16(reinterpret_cast<const uint16_t*>(p + i));
        const uint16x8_t tr = vorrq_u16(vceqq_u16(v, va), vceqq_u16(v, vb));
        const uint64_t maske = vget_lane_u64(
            vreinterpret_u64_u8(vshrn_n_u16(tr, 4)), 0);
        if (maske) return i + (__builtin_ctzll(maske) >> 3);
    }
    for (; i < n; ++i)
        if (p[i] == a || p[i] == b) return i;
    return n;
}

//  Dieselbe Form mit drei gesuchten Zeichen.
qsizetype findeErstesDreiNeon(const char16_t* p, qsizetype n,
                              char16_t a, char16_t b, char16_t c) {
    if (n < 8) {
        for (qsizetype i = 0; i < n; ++i)
            if (p[i] == a || p[i] == b || p[i] == c) return i;
        return n;
    }
    const uint16x8_t va = vdupq_n_u16(a);
    const uint16x8_t vb = vdupq_n_u16(b);
    const uint16x8_t vc = vdupq_n_u16(c);
    qsizetype i = 0;
    for (; i + 8 <= n; i += 8) {
        const uint16x8_t v = vld1q_u16(reinterpret_cast<const uint16_t*>(p + i));
        const uint16x8_t tr = vorrq_u16(vorrq_u16(vceqq_u16(v, va), vceqq_u16(v, vb)),
                                        vceqq_u16(v, vc));
        const uint64_t maske = vget_lane_u64(
            vreinterpret_u64_u8(vshrn_n_u16(tr, 4)), 0);
        if (maske) return i + (__builtin_ctzll(maske) >> 3);
    }
    for (; i < n; ++i)
        if (p[i] == a || p[i] == b || p[i] == c) return i;
    return n;
}

}  // namespace mg::simd

#endif  // MG_SIMD_ARM
