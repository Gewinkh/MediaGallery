#pragma once
//  Dispatch - welcher Rechenweg wird benutzt, und warum.
//  Die Kette ist immer dieselbe: Betriebssystem -> Architektur -> CPU-Merkmale.
//  Das Betriebssystem entscheidet nur ueber die Aufrufkonvention (und damit
//  darueber, ob eine handgeschriebene Assembly-Fassung ueberhaupt passt), die
//  Architektur ueber den Befehlssatz, die Merkmale ueber die Fassung.
//  ES GIBT IMMER EINEN RUECKFALL: `Plain` ist schlichtes C++, wird auf jeder
//  Plattform gebaut und ist zugleich das Mass, gegen das die Treiber pruefen.
#include <QString>

namespace mg::simd {

enum class Weg { Plain, Sse2, Avx2, Neon, AsmAvx2 };

//  Was diese Maschine kann - einmal ermittelt, danach nur noch gelesen.
struct Maschine {
    bool sse2 = false;
    bool avx2 = false;
    bool neon = false;
};
const Maschine& maschine();

//  Welcher Weg wird wirklich gerufen? Der beste SIMD-Weg, den Bau und Maschine
//  hergeben. Die handgeschriebene Assembly ist NICHT der Standardweg - sie
//  liegt gemessen gleichauf mit AVX2 und bleibt eine gepruefte Zweitfassung.
//  `MG_SIMD_WEG` (plain|sse2|avx2|neon|asm) erzwingt einen Weg; der Schalter ist
//  zum MESSEN da, denn nur so faehrt derselbe echte Code-Weg mit jeder Fassung.
//  Ein unbekannter oder auf dieser Maschine unmoeglicher Name wird ueberlesen.
Weg gewaehlt();
const char* wegName(Weg w);

//  „linux / x86-64 / sse2+avx2 -> asm-avx2" - fuer Pruefstand, Treiber und Log.
QString bericht();

}  // namespace mg::simd
