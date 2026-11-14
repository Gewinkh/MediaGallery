#pragma once
//  Videobild -> verkleinertes RGB auf der CPU. QVideoFrame::toImage() wandelt in Qt 6.12 ueber ein fadeneigenes RHI
//  und laedt dafuer den GL-Stapel samt NVIDIA-Treiber, auch beim Software-Backend.
#include "simd/Dispatch.h"

#include <QImage>
#include <QSize>
#include <QtGlobal>

class QVideoFrame;

namespace mg::media {

struct YuvEbenen {
    const uchar* y = nullptr;
    const uchar* u = nullptr;      // verzahnt (NV12): die UV-Ebene, v = u + ein Wert
    const uchar* v = nullptr;
    qsizetype yAbstand = 0;
    qsizetype cAbstand = 0;
    int w = 0;
    int h = 0;
    int cShiftX = 1;
    int cShiftY = 1;
    bool zweiByte = false;
    bool verzahnt = false;
    int teiler = 1;                // Wert / teiler = 8-Bit-Skala: 4 bei 10 Bit unten, 256 bei P010/P016
    float kr = 0.2126f;
    float kb = 0.0722f;
    bool vollerBereich = false;
};

//  Mittelt je k x k Bildpunkte (am Rand, was da ist) nach RGB32, ceil(w/k) x ceil(h/k); jede `weg`-Fassung bitgleich.
QImage yuvVerkleinert(const YuvEbenen& e, int k, mg::simd::Weg weg = mg::simd::gewaehlt());

//  Bilinear aus den Ebenen auf genau `ziel`, dann gewandelt; eine ungeteilte `ablage` passender Groesse wird beschrieben.
QImage yuvGestreckt(const YuvEbenen& e, const QSize& ziel, QImage ablage = {},
                    mg::simd::Weg weg = mg::simd::gewaehlt());

//  Gedreht wie toImage(), leer = toImage(): Vorschau k x k gemittelt und glatt verkleinert, Abspielen genau eingepasst.
QImage videoBildVerkleinert(const QVideoFrame& frame, const QSize& ziel);
QImage videoBildFuerFlaeche(const QVideoFrame& frame, const QSize& ziel, QImage ablage = {});

}  // namespace mg::media
