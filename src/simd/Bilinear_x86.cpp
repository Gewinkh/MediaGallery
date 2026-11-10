#include "simd/Bilinear.h"

#if MG_SIMD_X86

#include <immintrin.h>

namespace mg::simd {

//  Acht Pixel je Schritt: Bytes zu 16-Bit-Kanaelen entpackt (unpacklo nimmt je 128-Bit-Haelfte Pixel 0,1 bzw. 4,5),
//  gemischt wie plain, gepackt zurueck in derselben Reihenfolge. Der Rest hinter dem letzten vollen Block laeuft
//  schlicht, sonst laese der Kern ueber das Zeilenende.
__attribute__((target("avx2")))
static inline __m256i mische8(__m256i a, __m256i b, __m256i fLo, __m256i fHi) {
    const __m256i null = _mm256_setzero_si256();
    const __m256i k256 = _mm256_set1_epi16(256);
    const __m256i k128 = _mm256_set1_epi16(128);
    const __m256i lo = _mm256_srli_epi16(
        _mm256_add_epi16(_mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpacklo_epi8(a, null), _mm256_sub_epi16(k256, fLo)),
                                          _mm256_mullo_epi16(_mm256_unpacklo_epi8(b, null), fLo)),
                         k128), 8);
    const __m256i hi = _mm256_srli_epi16(
        _mm256_add_epi16(_mm256_add_epi16(_mm256_mullo_epi16(_mm256_unpackhi_epi8(a, null), _mm256_sub_epi16(k256, fHi)),
                                          _mm256_mullo_epi16(_mm256_unpackhi_epi8(b, null), fHi)),
                         k128), 8);
    return _mm256_packus_epi16(lo, hi);
}

static inline quint32 mische1(quint32 a, quint32 b, quint32 f) {
    const quint32 g = 256 - f;
    return ((((a & 0xff00ff) * g + (b & 0xff00ff) * f + 0x800080) >> 8) & 0xff00ff)
           | ((((a >> 8) & 0xff00ff) * g + ((b >> 8) & 0xff00ff) * f + 0x800080) & 0xff00ff00);
}

//  Ohne gather: das ist hier mit der Downfall-Abhilfe im Mikrocode langsam (gemessen kaum schneller als plain).
//  Stattdessen je Pixel ein 64-Bit-Laden des Nachbarpaars, Kanaele von a und b verzahnt, madd rechnet a * g + b * f.
//  Paare k und k + 4 teilen ein Register, damit das Packen am Ende die Reihenfolge von selbst herstellt.
__attribute__((target("avx2")))
void zeileWaagAvx2(const quint32* row, const qint32* x0, const qint32* x1, const quint16* fx, int n, quint32* out) {
    const __m128i verzahnt = _mm_setr_epi8(0, 4, 1, 5, 2, 6, 3, 7, 8, 12, 9, 13, 10, 14, 11, 15);
    const __m256i k128 = _mm256_set1_epi32(128);
    const __m256i k256 = _mm256_set1_epi32(256);
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256i f = _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(fx + i)));
        const __m256i gf = _mm256_or_si256(_mm256_sub_epi32(k256, f), _mm256_slli_epi32(f, 16));
        __m256i s[4];
        for (int k = 0; k < 4; ++k) {
            const __m128i paar = _mm_castpd_si128(
                _mm_loadh_pd(_mm_castsi128_pd(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(row + x0[i + k]))),
                             reinterpret_cast<const double*>(row + x0[i + k + 4])));
            const __m256i v = _mm256_cvtepu8_epi16(_mm_shuffle_epi8(paar, verzahnt));
            const __m256i w = _mm256_permutevar8x32_epi32(gf, _mm256_setr_epi32(k, k, k, k, k + 4, k + 4, k + 4, k + 4));
            s[k] = _mm256_srli_epi32(_mm256_add_epi32(_mm256_madd_epi16(v, w), k128), 8);
        }
        const __m256i p = _mm256_packus_epi16(_mm256_packus_epi32(s[0], s[1]), _mm256_packus_epi32(s[2], s[3]));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + i), p);
    }
    for (; i < n; ++i) out[i] = mische1(row[x0[i]], row[x1[i]], fx[i]);
}

__attribute__((target("avx2")))
void zeileSenkAvx2(const quint32* a, const quint32* b, int fy, int n, quint32* out) {
    const __m256i f = _mm256_set1_epi16(short(fy));
    int i = 0;
    for (; i + 8 <= n; i += 8)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + i),
                            mische8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i)),
                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + i)), f, f));
    for (; i < n; ++i) out[i] = mische1(a[i], b[i], quint32(fy));
}

}  // namespace mg::simd

#endif
