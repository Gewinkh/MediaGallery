#pragma once
#include <cstdint>

namespace mg {

// 16 Byte Zustand, ~5 Schritte je Zahl. rand() ist global und je Plattform anders,
// mt19937 schleppt 2,5 kB mit. Mit demselben Saatwert kommt dieselbe Folge - die
// Testtreiber pruefen die Mischung damit deterministisch.
class Xoshiro {
public:
    //  Saat: EIN 64-Bit-Wert wird über SplitMix64 auf vier 32-Bit-Wörter
    //  gestreut. Ohne dieses Streuen stünden bei kleinen Saatwerten fast nur
    //  Nullen im Zustand, und die ersten Zahlen wären erkennbar schlecht.
    explicit Xoshiro(uint64_t seed = 0x9E3779B97F4A7C15ULL) {
        uint64_t z = seed + 0x9E3779B97F4A7C15ULL;
        for (uint32_t& s : m_s) {
            uint64_t x = (z += 0x9E3779B97F4A7C15ULL);
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
            s = uint32_t((x ^ (x >> 31)) >> 16);
        }
        //  Ein Zustand aus lauter Nullen kann sich nie wieder verlassen.
        if (!(m_s[0] | m_s[1] | m_s[2] | m_s[3])) m_s[0] = 0x9E3779B9u;
    }

    uint32_t next() {
        const uint32_t result = rotl(m_s[0] + m_s[3], 7) + m_s[0];
        const uint32_t t = m_s[1] << 9;
        m_s[2] ^= m_s[0];
        m_s[3] ^= m_s[1];
        m_s[1] ^= m_s[2];
        m_s[0] ^= m_s[3];
        m_s[2] ^= t;
        m_s[3] = rotl(m_s[3], 11);
        return result;
    }

    // Gleichverteilt in [0, bound): der Rest-Trick braucht EINE Multiplikation statt einer Division, die Nachprüfung
    // schlägt selten zu. Ein blankes `% bound` bevorzugt die kleinen Werte.
    uint32_t below(uint32_t bound) {
        if (bound <= 1) return 0;
        uint64_t m = uint64_t(next()) * uint64_t(bound);
        uint32_t l = uint32_t(m);
        if (l < bound) {
            const uint32_t t = uint32_t(-int32_t(bound)) % bound;
            while (l < t) {
                m = uint64_t(next()) * uint64_t(bound);
                l = uint32_t(m);
            }
        }
        return uint32_t(m >> 32);
    }

private:
    static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
    uint32_t m_s[4] {};
};

} // namespace mg
