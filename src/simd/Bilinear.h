#pragma once
//  Bilinear - achsparalleles Strecken von 32-Bit-Pixeln (premultipliziert, je Kanal 8 Bit) fuer den Software-Weg des
//  Bildbetrachters. Getrennt in zwei Durchgaenge: erst waagrecht je benoetigter Quellzeile, dann senkrecht zwischen
//  zwei solchen Zeilen. Gewichte 0..256, jede Mischung (a * (256 - f) + b * f + 128) >> 8 je Kanal: in 16 Bit ohne
//  Ueberlauf, deshalb liefern alle Fassungen bitgleich dasselbe.
#include "simd/FindChar.h"

#include <QtGlobal>

namespace mg::simd {

//  `dst` (w x h, Zeilenabstand in Bytes) ist der Ausschnitt ab (ox, oy) der auf sx, sy gestreckten Quelle: Zielpixel
//  (x, y) liest den Quellpunkt ((ox + x + 0.5) / sx - 0.5, (oy + y + 0.5) / sy - 0.5), am Rand geklemmt.
void skaliereBilinear(const quint32* src, int sw, int sh, qsizetype srcStride, quint32* dst, int w, int h,
                      qsizetype dstStride, double sx, double sy, int ox, int oy);

//  Die Zeilen-Kerne, offen fuer Treiber und Pruefstand. Waagrecht: out[i] mischt row[x0[i]] und row[x1[i]] mit fx[i];
//  AVX2 verlangt x1[i] == x0[i] + 1 (liest das Paar am Stueck), skaliereBilinear nimmt bei einspaltiger Quelle plain.
using ZeileWaag = void (*)(const quint32* row, const qint32* x0, const qint32* x1, const quint16* fx, int n,
                           quint32* out);
//  Senkrecht: out[i] mischt a[i] und b[i] mit fy.
using ZeileSenk = void (*)(const quint32* a, const quint32* b, int fy, int n, quint32* out);

void zeileWaagPlain(const quint32* row, const qint32* x0, const qint32* x1, const quint16* fx, int n, quint32* out);
void zeileSenkPlain(const quint32* a, const quint32* b, int fy, int n, quint32* out);
#if MG_SIMD_X86
void zeileWaagAvx2(const quint32* row, const qint32* x0, const qint32* x1, const quint16* fx, int n, quint32* out);
void zeileSenkAvx2(const quint32* a, const quint32* b, int fy, int n, quint32* out);
#endif

//  Dasselbe wie skaliereBilinear mit ausdruecklich gewaehlten Kernen (Treiber: jede Fassung gegen plain).
void skaliereBilinearMit(ZeileWaag waag, ZeileSenk senk, const quint32* src, int sw, int sh, qsizetype srcStride,
                         quint32* dst, int w, int h, qsizetype dstStride, double sx, double sy, int ox, int oy);

}  // namespace mg::simd
