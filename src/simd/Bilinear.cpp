#include "simd/Bilinear.h"

#include "simd/Dispatch.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace mg::simd {
namespace {

struct Kerne {
    ZeileWaag waag;
    ZeileSenk senk;
};

//  NEON und SSE2 fallen auf plain: gemessen wurde nur AVX2 gegen plain, ein ungemessener Weg waere geraten.
Kerne waehle() {
    switch (gewaehlt()) {
#if MG_SIMD_X86
    case Weg::AsmAvx2:
    case Weg::Avx2: return {&zeileWaagAvx2, &zeileSenkAvx2};
#endif
    default: break;
    }
    return {&zeileWaagPlain, &zeileSenkPlain};
}

//  Quellkoordinate eines Zielpixels, geklemmt; i0/i1 die beiden Nachbarn, f das Gewicht von i1 in 1/256.
void stelle(double u, int n, qint32& i0, qint32& i1, int& f) {
    u = std::clamp(u, 0.0, double(n - 1));
    i0 = std::min(int(u), std::max(0, n - 2));
    i1 = std::min(i0 + 1, n - 1);
    f = std::clamp(int((u - i0) * 256.0 + 0.5), 0, 256);
}

}  // namespace

void skaliereBilinearMit(ZeileWaag waag, ZeileSenk senk, const quint32* src, int sw, int sh, qsizetype srcStride,
                         quint32* dst, int w, int h, qsizetype dstStride, double sx, double sy, int ox, int oy) {
    if (!src || !dst || sw <= 0 || sh <= 0 || w <= 0 || h <= 0 || !(sx > 0) || !(sy > 0)) return;
    if (sw < 2) waag = &zeileWaagPlain;
    const size_t n = static_cast<size_t>(w);
    std::vector<qint32> x0(n), x1(n);
    std::vector<quint16> fx(n);
    for (int x = 0; x < w; ++x) {
        int f;
        stelle((ox + x + 0.5) / sx - 0.5, sw, x0[size_t(x)], x1[size_t(x)], f);
        fx[size_t(x)] = quint16(f);
    }
    //  Beim Vergroessern teilen sich mehrere Zielzeilen dieselben zwei Quellzeilen: waagrecht nur einmal je Quellzeile.
    std::vector<quint32> puffer(n * 2);
    quint32* r0 = puffer.data();
    quint32* r1 = r0 + w;
    int j0 = -1, j1 = -1;
    const auto zeile = [&](int j) {
        return reinterpret_cast<const quint32*>(reinterpret_cast<const uchar*>(src) + j * srcStride);
    };
    for (int y = 0; y < h; ++y) {
        qint32 a, b;
        int f;
        stelle((oy + y + 0.5) / sy - 0.5, sh, a, b, f);
        if (a != j0 || b != j1) {
            if (a == j1) {
                std::swap(r0, r1);
            } else {
                waag(zeile(a), x0.data(), x1.data(), fx.data(), w, r0);
            }
            if (b != a) waag(zeile(b), x0.data(), x1.data(), fx.data(), w, r1);
            else std::memcpy(r1, r0, n * 4);
            j0 = a;
            j1 = b;
        }
        senk(r0, r1, f, w, reinterpret_cast<quint32*>(reinterpret_cast<uchar*>(dst) + y * dstStride));
    }
}

void skaliereBilinear(const quint32* src, int sw, int sh, qsizetype srcStride, quint32* dst, int w, int h,
                      qsizetype dstStride, double sx, double sy, int ox, int oy) {
    static const Kerne k = waehle();
    skaliereBilinearMit(k.waag, k.senk, src, sw, sh, srcStride, dst, w, h, dstStride, sx, sy, ox, oy);
}

}  // namespace mg::simd
