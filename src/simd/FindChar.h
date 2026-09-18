#pragma once
//  FindChar - die erste Stelle, an der eines von zwei bzw. drei Zeichen steht,
//  in UTF-16. Jede Fassung liefert bitgleich dasselbe: es wird gesucht, nicht
//  gerechnet, deshalb ist die Gleichheit hart pruefbar.
#include <QtGlobal>

//  Bau-Weichen, HIER und nirgends sonst - sonst wird eine Fassung auf einer
//  Plattform gebaut, die sie nicht kennt.
#if defined(__x86_64__) || defined(_M_X64)
#  define MG_SIMD_X86 1
#else
#  define MG_SIMD_X86 0
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
#  define MG_SIMD_ARM 1
#else
#  define MG_SIMD_ARM 0
#endif

//  Assembly nur fuer System V auf x86-64: Windows uebergibt in anderen
//  Registern und verlangt xmm6-xmm15 zurueck, aarch64 ist ein anderer
//  Befehlssatz. Dort bleibt es beim C++-Weg.
#if defined(MG_ASM_ENABLED) && MG_SIMD_X86 && (defined(__linux__) || defined(__APPLE__)) \
    && !defined(_WIN32)
#  define MG_SIMD_HAVE_ASM 1
#else
#  define MG_SIMD_HAVE_ASM 0
#endif

namespace mg::simd {

//  Erste Stelle in [0, n), an der `a` ODER `b` steht; sonst `n`.
//  Die Weiche waehlt beim ersten Aufruf und behaelt die Wahl.
qsizetype findeErstes(const char16_t* p, qsizetype n, char16_t a, char16_t b);

//  Dasselbe mit DREI Zeichen: Qt sieht `s`, `S` und das lange `s` als gleich an,
//  ebenso `k`, `K` und das Kelvin-Zeichen. Beide Anfangsbuchstaben sind zu
//  haeufig, um sie vom Vorfilter auszunehmen - das kostete 62,5 statt 15,4 ms.
//  Ohne Assembly-Fassung, produktiv ist SIMD.
qsizetype findeErstesDrei(const char16_t* p, qsizetype n,
                          char16_t a, char16_t b, char16_t c);

//  Offen, damit der Treiber sie gegeneinander haelt und der Pruefstand jede
//  einzeln misst.
qsizetype findeErstesPlain(const char16_t* p, qsizetype n, char16_t a, char16_t b);
qsizetype findeErstesDreiPlain(const char16_t* p, qsizetype n,
                               char16_t a, char16_t b, char16_t c);
#if MG_SIMD_X86
qsizetype findeErstesSse2(const char16_t* p, qsizetype n, char16_t a, char16_t b);
qsizetype findeErstesAvx2(const char16_t* p, qsizetype n, char16_t a, char16_t b);
qsizetype findeErstesDreiSse2(const char16_t* p, qsizetype n,
                              char16_t a, char16_t b, char16_t c);
qsizetype findeErstesDreiAvx2(const char16_t* p, qsizetype n,
                              char16_t a, char16_t b, char16_t c);
#endif
#if MG_SIMD_ARM
qsizetype findeErstesNeon(const char16_t* p, qsizetype n, char16_t a, char16_t b);
qsizetype findeErstesDreiNeon(const char16_t* p, qsizetype n,
                              char16_t a, char16_t b, char16_t c);
#endif
#if MG_SIMD_HAVE_ASM
//  Handgeschrieben, verlangt AVX2. Praefix im Namen, weil das C-Symbol im
//  ganzen Programm sichtbar ist.
extern "C" qsizetype mg_finde_erstes_avx2_sysv(const char16_t* p, qsizetype n,
                                               char16_t a, char16_t b);
#endif

}  // namespace mg::simd
