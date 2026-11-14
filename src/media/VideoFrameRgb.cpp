#include "media/VideoFrameRgb.h"

#include "simd/Bilinear.h"
#include "simd/FindChar.h"

#include <QScopeGuard>
#include <QTransform>
#include <QVideoFrame>
#include <QVideoFrameFormat>

#if MG_SIMD_X86
#include <immintrin.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace mg::media {
namespace {

//  Drei Durchgaenge je Zielzeile, jeder ohne Sprung und Division im Innern, damit der Uebersetzer sie vektorisiert:
//  senkrecht die k Quellzeilen je Spalte summieren, waagrecht je k Spalten, dann wandeln.
template <typename T, int Schritt>
Q_ALWAYS_INLINE void verkleinere(const YuvEbenen& e, int k, QImage& out) {
    const int ow = out.width();
    const int oh = out.height();
    const int cw = (e.w + (1 << e.cShiftX) - 1) >> e.cShiftX;
    std::vector<quint32> zy(static_cast<size_t>(e.w)), zu(static_cast<size_t>(cw)), zv(static_cast<size_t>(cw));
    std::vector<float> sy(static_cast<size_t>(ow)), su(sy.size()), sv(sy.size()), kehr(sy.size());
    int kehrZeilen = 0;

    const float bereichY = e.vollerBereich ? 1.0f : 255.0f / 219.0f;
    const float bereichC = e.vollerBereich ? 1.0f : 255.0f / 224.0f;
    const float versatzY = e.vollerBereich ? 0.0f : 16.0f * bereichY;
    const float kg = 1.0f - e.kr - e.kb;
    const float crv = 2.0f * (1.0f - e.kr) * bereichC;
    const float cbu = 2.0f * (1.0f - e.kb) * bereichC;
    const float cgu = 2.0f * e.kb * (1.0f - e.kb) / kg * bereichC;
    const float cgv = 2.0f * e.kr * (1.0f - e.kr) / kg * bereichC;

    for (int oy = 0; oy < oh; ++oy) {
        const int y0 = oy * k;
        const int y1 = std::min(e.h, y0 + k);
        const int zeilen = y1 - y0;
        if (zeilen != kehrZeilen) {
            for (int ox = 0; ox < ow; ++ox)
                kehr[size_t(ox)] = 1.0f / float(std::min(k, e.w - ox * k) * zeilen * e.teiler);
            kehrZeilen = zeilen;
        }
        //  Ohne Mitteln (Abspielen in Fenstergroesse) direkt aus den Ebenen: die Summenzeilen kosteten dort die Haelfte.
        if (k == 1) {
            const T* yr = reinterpret_cast<const T*>(e.y + oy * e.yAbstand);
            const qsizetype cz = qsizetype(oy >> e.cShiftY) * e.cAbstand;
            const T* ur = reinterpret_cast<const T*>(e.u + cz);
            const T* vr = reinterpret_cast<const T*>(e.v + cz);
            for (int x = 0; x < ow; ++x) sy[size_t(x)] = float(yr[x]);
            for (int x = 0; x < ow; ++x) {
                su[size_t(x)] = float(ur[(x >> e.cShiftX) * Schritt]);
                sv[size_t(x)] = float(vr[(x >> e.cShiftX) * Schritt]);
            }
        } else {
            std::fill(zy.begin(), zy.end(), 0u);
            std::fill(zu.begin(), zu.end(), 0u);
            std::fill(zv.begin(), zv.end(), 0u);
            for (int y = y0; y < y1; ++y) {
                const T* yr = reinterpret_cast<const T*>(e.y + y * e.yAbstand);
                const qsizetype cz = qsizetype(y >> e.cShiftY) * e.cAbstand;
                const T* ur = reinterpret_cast<const T*>(e.u + cz);
                const T* vr = reinterpret_cast<const T*>(e.v + cz);
                for (int x = 0; x < e.w; ++x) zy[size_t(x)] += yr[x];
                for (int cx = 0; cx < cw; ++cx) {
                    zu[size_t(cx)] += ur[cx * Schritt];
                    zv[size_t(cx)] += vr[cx * Schritt];
                }
            }
            for (int ox = 0, x = 0; ox < ow; ++ox) {
                quint32 a = 0, b = 0, c = 0;
                for (const int xe = std::min(e.w, x + k); x < xe; ++x) {
                    a += zy[size_t(x)];
                    b += zu[size_t(x >> e.cShiftX)];
                    c += zv[size_t(x >> e.cShiftX)];
                }
                sy[size_t(ox)] = float(a);
                su[size_t(ox)] = float(b);
                sv[size_t(ox)] = float(c);
            }
        }
        auto* ziel = reinterpret_cast<QRgb*>(out.scanLine(oy));
        for (int ox = 0; ox < ow; ++ox) {
            const float inv = kehr[size_t(ox)];
            const float yy = sy[size_t(ox)] * inv * bereichY - versatzY;
            const float uu = su[size_t(ox)] * inv - 128.0f;
            const float vv = sv[size_t(ox)] * inv - 128.0f;
            const auto r = quint32(std::min(std::max(yy + crv * vv + 0.5f, 0.0f), 255.0f));
            const auto g = quint32(std::min(std::max(yy - cgu * uu - cgv * vv + 0.5f, 0.0f), 255.0f));
            const auto b = quint32(std::min(std::max(yy + cbu * uu + 0.5f, 0.0f), 255.0f));
            ziel[ox] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

using Kern = void (*)(const YuvEbenen&, int, QImage&);

template <typename T, int Schritt>
void kernPlain(const YuvEbenen& e, int k, QImage& out) { verkleinere<T, Schritt>(e, k, out); }

//  Derselbe Quelltext, fuer AVX2 uebersetzt: erst damit vektorisiert die Wandlung (float -> int mit Klemmen).
//  Ohne FMA, damit jede Rundung wie in plain faellt und beide bitgleich bleiben.
#if MG_SIMD_X86
template <typename T, int Schritt>
__attribute__((target("avx2"))) void kernAvx2(const YuvEbenen& e, int k, QImage& out) {
    verkleinere<T, Schritt>(e, k, out);
}
#endif

template <typename T, int Schritt>
Kern kern(mg::simd::Weg weg) {
#if MG_SIMD_X86
    if ((weg == mg::simd::Weg::Avx2 || weg == mg::simd::Weg::AsmAvx2) && mg::simd::maschine().avx2)
        return &kernAvx2<T, Schritt>;
#endif
    Q_UNUSED(weg);
    return &kernPlain<T, Schritt>;
}

//  Nachbarn und Gewicht (1/256) eines Zielpunkts in der Quelle, Lage wie skaliereBilinear, am Rand geklemmt.
void stelle(double u, int n, qint32& i0, qint32& i1, quint32& f) {
    u = std::clamp(u, 0.0, double(n - 1));
    i0 = std::min(int(u), std::max(0, n - 2));
    i1 = std::min(i0 + 1, n - 1);
    f = quint32(std::clamp(int((u - i0) * 256.0 + 0.5), 0, 256));
}

//  16-Bit-Werte gleich auf 1/1 zurueck: so bleibt jede spaetere Summe unter 2^24 und passt verlustfrei in float.
template <typename T>
Q_ALWAYS_INLINE void waagrecht(const T* row, const qint32* x0, const qint32* x1, const quint32* f, int n,
                               quint32* out) {
    for (int i = 0; i < n; ++i) {
        const quint32 s = quint32(row[x0[i]]) * (256 - f[i]) + quint32(row[x1[i]]) * f[i];
        out[i] = sizeof(T) == 1 ? s : (s + 128) >> 8;
    }
}

//  Je acht Zielpunkte ein Block: ab `basis` liegen beide Nachbarn aller acht in 16 Bytes. Die Lagen gelten fuer jede
//  Zeile, Masken und Gewichte entstehen also einmal je Bild; basis < 0 = Block schlicht rechnen.
struct Mischtafel {
    std::vector<qint32> basis;
    std::vector<quint8> maske;
    std::vector<qint16> gewicht;

    void baue(const qint32* x0, const qint32* x1, const quint32* f, int n, int grenze) {
        const size_t bloecke = size_t(n / 8);
        basis.assign(bloecke, -1);
        maske.assign(bloecke * 16, 0);
        gewicht.assign(bloecke * 16, 0);
        for (size_t j = 0; j < bloecke; ++j) {
            const int b = x0[j * 8];
            bool passt = b + 16 <= grenze;
            for (size_t k = 0; k < 8 && passt; ++k) {
                const int a = x0[j * 8 + k] - b;
                const int c = x1[j * 8 + k] - b;
                passt = a >= 0 && c >= 0 && a < 16 && c < 16;
                maske[j * 16 + k * 2] = quint8(a);
                maske[j * 16 + k * 2 + 1] = quint8(c);
                gewicht[j * 16 + k * 2] = qint16(256 - f[j * 8 + k]);
                gewicht[j * 16 + k * 2 + 1] = qint16(f[j * 8 + k]);
            }
            if (passt) basis[j] = b;
        }
    }
};

#if MG_SIMD_X86
//  Rechnet exakt wie waagrecht<uchar>: Paare per Maske nebeneinander, madd bildet a * (256 - f) + b * f in 32 Bit.
__attribute__((target("avx2"))) void waagTafel(const uchar* row, const qint32* x0, const qint32* x1,
                                               const quint32* f, const Mischtafel& t, int n, quint32* out) {
    const int bloecke = int(t.basis.size());
    for (int j = 0; j < bloecke; ++j) {
        const int b = t.basis[size_t(j)];
        if (b < 0) {
            waagrecht(row, x0 + j * 8, x1 + j * 8, f + j * 8, 8, out + j * 8);
            continue;
        }
        const __m128i paare = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(row + b)),
                                               _mm_loadu_si128(reinterpret_cast<const __m128i*>(t.maske.data() + j * 16)));
        const __m256i w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(t.gewicht.data() + j * 16));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + j * 8), _mm256_madd_epi16(_mm256_cvtepu8_epi16(paare), w));
    }
    const int rest = bloecke * 8;
    waagrecht(row, x0 + rest, x1 + rest, f + rest, n - rest, out + rest);
}
#endif

//  Zwei waagrecht gestreckte Quellzeilen; folgt die naechste Zielzeile, wird nur die neue Quellzeile gerechnet.
struct Zeilenpaar {
    quint32* r0;
    quint32* r1;
    int j0 = -1;
    int j1 = -1;

    template <typename Holen>
    Q_ALWAYS_INLINE void auf(int a, int b, int n, Holen&& holen) {
        if (a == j0 && b == j1) return;
        if (a == j1) std::swap(r0, r1);
        else holen(a, r0);
        if (b != a) holen(b, r1);
        else std::memcpy(r1, r0, size_t(n) * sizeof(quint32));
        j0 = a;
        j1 = b;
    }
};

//  Abspielen: bilinear gleich aus den Ebenen auf die Zielgroesse, erst dann gewandelt. So rechnet die Wandlung nur
//  die Zielpunkte und kein volles RGB-Zwischenbild entsteht. Farbe mittig in ihrer Gruppe gedacht.
template <typename T, int Schritt, bool MitTafel>
Q_ALWAYS_INLINE void strecke(const YuvEbenen& e, QImage& out) {
    const int ow = out.width();
    const int oh = out.height();
    const int cw = (e.w + (1 << e.cShiftX) - 1) >> e.cShiftX;
    const int ch = (e.h + (1 << e.cShiftY) - 1) >> e.cShiftY;
    const double sx = double(ow) / e.w;
    const double sy = double(oh) / e.h;
    const size_t n = size_t(ow);
    std::vector<qint32> stellen(n * 4);
    std::vector<quint32> gewichte(n * 2), puffer(n * 6);
    qint32* lx0 = stellen.data();
    qint32* lx1 = lx0 + n;
    qint32* cx0 = lx1 + n;
    qint32* cx1 = cx0 + n;
    quint32* lf = gewichte.data();
    quint32* cf = lf + n;
    for (int x = 0; x < ow; ++x) {
        const double u = (x + 0.5) / sx;
        stelle(u - 0.5, e.w, lx0[x], lx1[x], lf[x]);
        stelle(u / (1 << e.cShiftX) - 0.5, cw, cx0[x], cx1[x], cf[x]);
        cx0[x] *= Schritt;
        cx1[x] *= Schritt;
    }
    //  Verzahnt beginnt u oder v ein Byte hinter dem Zeilenanfang: die Grenze gilt fuer beide.
    Mischtafel tl, tc;
    if constexpr (MitTafel) {
        tl.baue(lx0, lx1, lf, ow, e.w);
        tc.baue(cx0, cx1, cf, ow, Schritt == 2 ? 2 * cw - 1 : cw);
    }
    const auto waag = [&](const T* row, const qint32* x0, const qint32* x1, const quint32* f, const Mischtafel& t,
                          quint32* r) {
#if MG_SIMD_X86
        if constexpr (MitTafel) {
            waagTafel(row, x0, x1, f, t, ow, r);
            return;
        }
#endif
        Q_UNUSED(t);
        waagrecht(row, x0, x1, f, ow, r);
    };
    Zeilenpaar py{puffer.data(), puffer.data() + n};
    Zeilenpaar pu{puffer.data() + 2 * n, puffer.data() + 3 * n};
    Zeilenpaar pv{puffer.data() + 4 * n, puffer.data() + 5 * n};

    const float skala = (sizeof(T) == 1 ? 65536.0f : 256.0f) * float(e.teiler);
    const float bereichY = e.vollerBereich ? 1.0f : 255.0f / 219.0f;
    const float bereichC = e.vollerBereich ? 1.0f : 255.0f / 224.0f;
    const float versatzY = e.vollerBereich ? 0.0f : 16.0f * bereichY;
    const float invY = bereichY / skala;
    const float invC = 1.0f / skala;
    const float kg = 1.0f - e.kr - e.kb;
    const float crv = 2.0f * (1.0f - e.kr) * bereichC;
    const float cbu = 2.0f * (1.0f - e.kb) * bereichC;
    const float cgu = 2.0f * e.kb * (1.0f - e.kb) / kg * bereichC;
    const float cgv = 2.0f * e.kr * (1.0f - e.kr) / kg * bereichC;

    for (int oy = 0; oy < oh; ++oy) {
        const double v = (oy + 0.5) / sy;
        qint32 ya, yb, ca, cb;
        quint32 fy, fc;
        stelle(v - 0.5, e.h, ya, yb, fy);
        stelle(v / (1 << e.cShiftY) - 0.5, ch, ca, cb, fc);
        //  Gleiche Breite (Vollbild in Videogroesse): jedes Gewicht 0, die Zeile nur aufgeweitet - vektorisiert ueberall.
        py.auf(ya, yb, ow, [&](int j, quint32* r) {
            const T* row = reinterpret_cast<const T*>(e.y + j * e.yAbstand);
            if (ow == e.w) {
                for (int i = 0; i < ow; ++i) r[i] = sizeof(T) == 1 ? quint32(row[i]) << 8 : quint32(row[i]);
                return;
            }
            waag(row, lx0, lx1, lf, tl, r);
        });
        pu.auf(ca, cb, ow, [&](int j, quint32* r) {
            waag(reinterpret_cast<const T*>(e.u + j * e.cAbstand), cx0, cx1, cf, tc, r);
        });
        pv.auf(ca, cb, ow, [&](int j, quint32* r) {
            waag(reinterpret_cast<const T*>(e.v + j * e.cAbstand), cx0, cx1, cf, tc, r);
        });
        const quint32 gy = 256 - fy;
        const quint32 gc = 256 - fc;
        const quint32* y0 = py.r0;
        const quint32* y1 = py.r1;
        const quint32* u0 = pu.r0;
        const quint32* u1 = pu.r1;
        const quint32* v0 = pv.r0;
        const quint32* v1 = pv.r1;
        auto* ziel = reinterpret_cast<QRgb*>(out.scanLine(oy));
        for (int ox = 0; ox < ow; ++ox) {
            const float yy = float(qint32(y0[ox] * gy + y1[ox] * fy)) * invY - versatzY;
            const float uu = float(qint32(u0[ox] * gc + u1[ox] * fc)) * invC - 128.0f;
            const float vv = float(qint32(v0[ox] * gc + v1[ox] * fc)) * invC - 128.0f;
            //  Ueber int: geklemmt passt jeder Wert, und float -> unsigned kostete je Kanal fuenf Befehle mehr.
            const auto r = quint32(qint32(std::min(std::max(yy + crv * vv + 0.5f, 0.0f), 255.0f)));
            const auto g = quint32(qint32(std::min(std::max(yy - cgu * uu - cgv * vv + 0.5f, 0.0f), 255.0f)));
            const auto b = quint32(qint32(std::min(std::max(yy + cbu * uu + 0.5f, 0.0f), 255.0f)));
            ziel[ox] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

using Strecker = void (*)(const YuvEbenen&, QImage&);

template <typename T, int Schritt>
void streckerPlain(const YuvEbenen& e, QImage& out) { strecke<T, Schritt, false>(e, out); }

#if MG_SIMD_X86
template <typename T, int Schritt>
__attribute__((target("avx2"))) void streckerAvx2(const YuvEbenen& e, QImage& out) {
    strecke<T, Schritt, sizeof(T) == 1>(e, out);
}
#endif

template <typename T, int Schritt>
Strecker strecker(mg::simd::Weg weg) {
#if MG_SIMD_X86
    if ((weg == mg::simd::Weg::Avx2 || weg == mg::simd::Weg::AsmAvx2) && mg::simd::maschine().avx2)
        return &streckerAvx2<T, Schritt>;
#endif
    Q_UNUSED(weg);
    return &streckerPlain<T, Schritt>;
}

//  Bilinear statt Qts glatter Verkleinerung: je Bild 3,4 statt 5,5 ms bei 1080p auf 1200 px, sichtbar gleich.
QImage skaliert(QImage roh, const QSize& passt) {
    if (roh.isNull() || passt == roh.size() || passt.isEmpty()) return roh;
    QImage out(passt, QImage::Format_RGB32);
    if (out.isNull()) return {};
    mg::simd::skaliereBilinear(reinterpret_cast<const quint32*>(roh.constBits()), roh.width(), roh.height(),
                               roh.bytesPerLine(), reinterpret_cast<quint32*>(out.bits()), out.width(), out.height(),
                               out.bytesPerLine(), double(out.width()) / roh.width(),
                               double(out.height()) / roh.height(), 0, 0);
    return out;
}

//  Ebene p traegt `zeilen` Zeilen zu je `bytes` Bytes: Zeiger, Abstand und abgebildete Laenge reichen dafuer.
bool ebeneReicht(const QVideoFrame& f, int p, int zeilen, qsizetype bytes, int ausrichtung) {
    const uchar* b = f.bits(p);
    const qsizetype abstand = f.bytesPerLine(p);
    if (!b || abstand < bytes || abstand % ausrichtung || quintptr(b) % quintptr(ausrichtung)) return false;
    return qsizetype(f.mappedBytes(p)) >= abstand * (zeilen - 1) + bytes;
}

}  // namespace

QImage yuvVerkleinert(const YuvEbenen& e, int k, mg::simd::Weg weg) {
    if (!e.y || !e.u || !e.v || e.w <= 0 || e.h <= 0 || k < 1 || k > 255 || e.teiler < 1) return {};
    QImage out((e.w + k - 1) / k, (e.h + k - 1) / k, QImage::Format_RGB32);
    if (out.isNull()) return {};
    const Kern f = e.zweiByte ? (e.verzahnt ? kern<quint16, 2>(weg) : kern<quint16, 1>(weg))
                              : (e.verzahnt ? kern<uchar, 2>(weg) : kern<uchar, 1>(weg));
    f(e, k, out);
    return out;
}

QImage yuvGestreckt(const YuvEbenen& e, const QSize& ziel, QImage ablage, mg::simd::Weg weg) {
    if (!e.y || !e.u || !e.v || e.w <= 0 || e.h <= 0 || ziel.isEmpty() || e.teiler < 1) return {};
    //  Ein frischer Puffer kostet je Bild ~800 Seitenfehler (1080p auf 1200 px): das war fast die Haelfte der Zeit.
    QImage out = ablage.size() == ziel && ablage.format() == QImage::Format_RGB32 && ablage.isDetached()
                     ? std::move(ablage) : QImage(ziel, QImage::Format_RGB32);
    if (out.isNull()) return {};
    const Strecker f = e.zweiByte ? (e.verzahnt ? strecker<quint16, 2>(weg) : strecker<quint16, 1>(weg))
                                  : (e.verzahnt ? strecker<uchar, 2>(weg) : strecker<uchar, 1>(weg));
    f(e, out);
    return out;
}

namespace {

//  `genau`: Abspielen, das Ergebnis hat exakt die eingepasste Groesse; sonst nur k x k gemittelt (Vorschau).
QImage videoBild(const QVideoFrame& frame, const QSize& ziel, bool genau, QImage ablage) {
    const QVideoFrameFormat fmt = frame.surfaceFormat();
    if (fmt.colorTransfer() == QVideoFrameFormat::ColorTransfer_ST2084
        || fmt.colorTransfer() == QVideoFrameFormat::ColorTransfer_STD_B67 || frame.mirrored() || fmt.isMirrored()
        || fmt.scanLineDirection() != QVideoFrameFormat::TopToBottom)
        return {};
    const int w = frame.width();
    const int h = frame.height();
    if (w <= 0 || h <= 0 || ziel.isEmpty() || (fmt.viewport().isValid() && fmt.viewport() != QRect(0, 0, w, h)))
        return {};
    const int drehung = (int(fmt.rotation()) + int(frame.rotation())) % 360;

    QVideoFrame f = frame;
    if (!f.map(QVideoFrame::ReadOnly)) return {};
    const auto freigeben = qScopeGuard([&f] { f.unmap(); });

    const QSize gedreht = drehung % 180 ? QSize(h, w) : QSize(w, h);
    const QSize passt = gedreht.scaled(ziel, Qt::KeepAspectRatio);
    const int k = passt.isEmpty() ? 1
                                  : std::clamp(std::min(gedreht.width() / passt.width(),
                                                        gedreht.height() / passt.height()), 1, 255);
    const QSize flach = drehung % 180 ? passt.transposed() : passt;

    QImage bild;
    const QImage::Format rgb = QVideoFrameFormat::imageFormatFromPixelFormat(f.pixelFormat());
    if (rgb != QImage::Format_Invalid) {
        const int tiefe = QImage::toPixelFormat(rgb).bitsPerPixel() / 8;
        if (tiefe < 1 || f.planeCount() != 1 || !ebeneReicht(f, 0, h, qsizetype(w) * tiefe, 1)) return {};
        //  copy() vor dem Wandeln: ist das Format schon RGB32, teilte das Ergebnis sonst den gleich freigegebenen Puffer.
        bild = QImage(f.bits(0), w, h, f.bytesPerLine(0), rgb).copy();
        bild.convertTo(QImage::Format_RGB32);
    } else {
        YuvEbenen e;
        e.w = w;
        e.h = h;
        int ebenen = 3;
        bool tauschen = false;
        switch (f.pixelFormat()) {
        case QVideoFrameFormat::Format_YUV420P: break;
        case QVideoFrameFormat::Format_YV12: tauschen = true; break;
        case QVideoFrameFormat::Format_YUV422P: e.cShiftY = 0; break;
        case QVideoFrameFormat::Format_YUV420P10: e.zweiByte = true; e.teiler = 4; break;
        case QVideoFrameFormat::Format_NV21: tauschen = true; [[fallthrough]];
        case QVideoFrameFormat::Format_NV12: ebenen = 2; e.verzahnt = true; break;
        case QVideoFrameFormat::Format_P010:
        case QVideoFrameFormat::Format_P016: ebenen = 2; e.verzahnt = true; e.zweiByte = true; e.teiler = 256; break;
        default: return {};
        }
        if (f.planeCount() != ebenen) return {};
        const int wert = e.zweiByte ? 2 : 1;
        const int cw = (w + (1 << e.cShiftX) - 1) >> e.cShiftX;
        const int ch = (h + (1 << e.cShiftY) - 1) >> e.cShiftY;
        const qsizetype cBytes = qsizetype(cw) * wert * (e.verzahnt ? 2 : 1);
        if (!ebeneReicht(f, 0, h, qsizetype(w) * wert, wert)) return {};
        for (int p = 1; p < ebenen; ++p)
            if (!ebeneReicht(f, p, ch, cBytes, wert) || f.bytesPerLine(p) != f.bytesPerLine(1)) return {};
        e.y = f.bits(0);
        e.yAbstand = f.bytesPerLine(0);
        e.cAbstand = f.bytesPerLine(1);
        if (e.verzahnt) {
            e.u = f.bits(1) + (tauschen ? wert : 0);
            e.v = f.bits(1) + (tauschen ? 0 : wert);
        } else {
            e.u = f.bits(tauschen ? 2 : 1);
            e.v = f.bits(tauschen ? 1 : 2);
        }
        //  Unbenannte Matrix wie Qt: ueber 576 Zeilen HD (BT.709), sonst SD (BT.601).
        QVideoFrameFormat::ColorSpace raum = fmt.colorSpace();
        if (raum == QVideoFrameFormat::ColorSpace_Undefined)
            raum = h > 576 ? QVideoFrameFormat::ColorSpace_BT709 : QVideoFrameFormat::ColorSpace_BT601;
        switch (raum) {
        case QVideoFrameFormat::ColorSpace_BT601:
        case QVideoFrameFormat::ColorSpace_AdobeRgb: e.kr = 0.299f; e.kb = 0.114f; break;
        case QVideoFrameFormat::ColorSpace_BT2020: e.kr = 0.2627f; e.kb = 0.0593f; break;
        default: break;
        }
        e.vollerBereich = fmt.colorRange() == QVideoFrameFormat::ColorRange_Full
                          || raum == QVideoFrameFormat::ColorSpace_AdobeRgb;
        //  Ab halber Groesse liest bilinear nicht mehr jeden Quellpunkt: dort erst k x k mitteln, dann strecken.
        //  Ohne AVX2 ist bei gleicher Groesse der alte Kern schneller (1080p 14,2 gegen 16,7 ms, Farbe schlicht).
        const bool avx2 = MG_SIMD_X86 && mg::simd::maschine().avx2
                          && (mg::simd::gewaehlt() == mg::simd::Weg::Avx2
                              || mg::simd::gewaehlt() == mg::simd::Weg::AsmAvx2);
        const bool strecken = genau && k == 1 && (avx2 || flach != QSize(w, h));
        bild = strecken ? yuvGestreckt(e, flach, std::move(ablage)) : yuvVerkleinert(e, k);
    }
    if (genau) bild = skaliert(std::move(bild), flach);
    if (bild.isNull()) return {};
    if (drehung) bild = bild.transformed(QTransform().rotate(drehung));
    return bild;
}

}  // namespace

QImage videoBildFuerFlaeche(const QVideoFrame& frame, const QSize& ziel, QImage ablage) {
    return videoBild(frame, ziel, true, std::move(ablage));
}

QImage videoBildVerkleinert(const QVideoFrame& frame, const QSize& ziel) {
    const QImage bild = videoBild(frame, ziel, false, {});
    return bild.isNull() ? bild : bild.scaled(ziel, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

}  // namespace mg::media
