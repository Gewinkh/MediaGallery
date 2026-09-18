#include "simd/Dispatch.h"

#include "simd/FindChar.h"

#include <QByteArray>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace mg::simd {
namespace {

Maschine ermittle() {
    Maschine m;
#if MG_SIMD_X86
#  if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    m.sse2 = __builtin_cpu_supports("sse2");
    m.avx2 = __builtin_cpu_supports("avx2");
#  elif defined(_MSC_VER)
    //  Nicht auf diesem Rechner geprueft - hier gibt es keine MSVC-Umgebung.
    int info[4] = { 0, 0, 0, 0 };
    __cpuid(info, 1);
    m.sse2 = (info[3] & (1 << 26)) != 0;
    int sieben[4] = { 0, 0, 0, 0 };
    __cpuidex(sieben, 7, 0);
    m.avx2 = (sieben[1] & (1 << 5)) != 0;
#  endif
#elif MG_SIMD_ARM
    //  NEON gehoert bei ARMv8 zur Grundausstattung - nichts zu erkennen.
    m.neon = true;
#endif
    return m;
}

}  // namespace

const Maschine& maschine() {
    static const Maschine m = ermittle();
    return m;
}

Weg gewaehlt() {
    //  Nur, was diese Maschine wirklich kann - ein erzwungener Weg, den sie
    //  nicht beherrscht, waere ein Absturz statt einer Messung.
    const QByteArray wunsch = qgetenv("MG_SIMD_WEG");
    if (!wunsch.isEmpty()) {
        if (wunsch == "plain") return Weg::Plain;
#if MG_SIMD_X86
        if (wunsch == "sse2" && maschine().sse2) return Weg::Sse2;
        if (wunsch == "avx2" && maschine().avx2) return Weg::Avx2;
#endif
#if MG_SIMD_ARM
        if (wunsch == "neon" && maschine().neon) return Weg::Neon;
#endif
#if MG_SIMD_HAVE_ASM
        if (wunsch == "asm" && maschine().avx2) return Weg::AsmAvx2;
#endif
    }
    //  PRODUKTIV IST SIMD, NICHT DIE ASSEMBLY. Gemessen liegen beide gleichauf
    //  (avx2 0,110 us je Zeile, asm-avx2 0,112) - dann gewinnt der Weg, der auf
    //  jeder Plattform gleich aussieht und den der Compiler pflegt. Die
    //  Assembly bleibt als gepruefte Zweitfassung und wird nur ueber
    //  `MG_SIMD_WEG=asm` gerufen.
#if MG_SIMD_X86
    if (maschine().avx2) return Weg::Avx2;
    if (maschine().sse2) return Weg::Sse2;
#elif MG_SIMD_ARM
    if (maschine().neon) return Weg::Neon;
#endif
    return Weg::Plain;
}

const char* wegName(Weg w) {
    switch (w) {
    case Weg::Plain:   return "plain";
    case Weg::Sse2:    return "sse2";
    case Weg::Avx2:    return "avx2";
    case Weg::Neon:    return "neon";
    case Weg::AsmAvx2: return "asm-avx2";
    }
    return "?";
}

QString bericht() {
    const char* os =
#if defined(Q_OS_LINUX)
        "linux";
#elif defined(Q_OS_WIN)
        "windows";
#elif defined(Q_OS_MACOS)
        "macos";
#else
        "sonstiges";
#endif
    const char* arch =
#if MG_SIMD_X86
        "x86-64";
#elif MG_SIMD_ARM
        "aarch64";
#else
        "unbekannt";
#endif
    QString merkmale;
    if (maschine().sse2) merkmale += QStringLiteral("sse2 ");
    if (maschine().avx2) merkmale += QStringLiteral("avx2 ");
    if (maschine().neon) merkmale += QStringLiteral("neon ");
    if (merkmale.isEmpty()) merkmale = QStringLiteral("keine ");
    return QStringLiteral("%1 / %2 / %3-> %4")
        .arg(QLatin1String(os), QLatin1String(arch), merkmale,
             QLatin1String(wegName(gewaehlt())));
}

}  // namespace mg::simd
