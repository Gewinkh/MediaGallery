#include "simd/Bilinear.h"

namespace mg::simd {

//  Zwei Kanaele je Wort (Rot/Blau bzw. Alpha/Gruen in je 16 Bit): Produkt plus Rundung bleiben unter 65536, kein
//  Uebertrag in den Nachbarkanal.
static inline quint32 mische(quint32 a, quint32 b, quint32 f) {
    const quint32 g = 256 - f;
    const quint32 rb = (((a & 0xff00ff) * g + (b & 0xff00ff) * f + 0x800080) >> 8) & 0xff00ff;
    const quint32 ag = (((a >> 8) & 0xff00ff) * g + ((b >> 8) & 0xff00ff) * f + 0x800080) & 0xff00ff00;
    return rb | ag;
}

void zeileWaagPlain(const quint32* row, const qint32* x0, const qint32* x1, const quint16* fx, int n, quint32* out) {
    for (int i = 0; i < n; ++i) out[i] = mische(row[x0[i]], row[x1[i]], fx[i]);
}

void zeileSenkPlain(const quint32* a, const quint32* b, int fy, int n, quint32* out) {
    for (int i = 0; i < n; ++i) out[i] = mische(a[i], b[i], quint32(fy));
}

}  // namespace mg::simd
