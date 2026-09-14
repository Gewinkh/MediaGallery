#include "audio/AudioLimiter.h"

#include <algorithm>
#include <cmath>

namespace {
//  Rücklaufzeit. Kurz genug, um leise Stellen schnell wieder freizugeben, lang
//  genug, damit die Absenkung nicht im Takt der Musik pumpt.
constexpr double kReleaseMs = 80.0;
}  // namespace

AudioLimiter::AudioLimiter(QObject* parent) : QObject(parent) {
    configure(m_sampleRate, m_channels);
}

void AudioLimiter::configure(int sampleRate, int channels) {
    m_sampleRate = sampleRate > 0 ? sampleRate : 48000;
    m_channels   = std::clamp(channels, 1, 8);

    //  Die Grenzen liegen im geometrischen Mittel benachbarter Bandmitten -
    //  dieselbe Einteilung wie beim Equalizer.
    const auto& f = AudioEqualizer::frequencies();
    for (int i = 0; i + 1 < kBands; ++i) {
        const double fc = std::sqrt(f[size_t(i)] * f[size_t(i + 1)]);
        m_coef[size_t(i)] = 1.0 - std::exp(-2.0 * M_PI * fc / double(m_sampleRate));
    }
    m_state.assign(size_t(m_channels) * size_t(kBands - 1), 0.0);

    m_look = int(double(m_sampleRate) * kLookaheadMs * 0.001);
    if (m_look < 1) m_look = 1;
    m_ring.assign(size_t(m_look) * size_t(kBands) * size_t(m_channels), 0.0f);

    //  Die Ansprechzeit folgt dem Fenster: die Absenkung soll genau dann stehen,
    //  wenn die laute Stelle herauskommt.
    m_atk = 1.0 - std::exp(-1.0 / (double(m_sampleRate) * kLookaheadMs * 0.001));
    m_rel = 1.0 - std::exp(-1.0 / (double(m_sampleRate) * kReleaseMs * 0.001));
    rebuild();
    resetState();
}

void AudioLimiter::resetState() {
    std::fill(m_state.begin(), m_state.end(), 0.0);
    std::fill(m_ring.begin(), m_ring.end(), 0.0f);
    m_gain.fill(1.0);
    m_wpos = 0;
    m_held = 0.0;
    m_holdLeft = 0;
    m_minGain = 1.0;
}

void AudioLimiter::setEnabled(bool on) {
    if (m_enabled.load(std::memory_order_relaxed) == on) return;
    m_enabled.store(on, std::memory_order_relaxed);
    emit changed();
}

void AudioLimiter::setStrength(double v) {
    const double clamped = std::clamp(v, 0.0, 1.0);
    if (qFuzzyCompare(m_strength + 1.0, clamped + 1.0)) return;
    m_strength = clamped;
    m_strengthPm.store(int(std::lround(clamped * 1000.0)), std::memory_order_relaxed);
    rebuild();
    emit changed();
}

//  Die Stärke zieht die Decke herunter: bei 0 liegt sie auf 1 und die Stufe
//  greift nie, bei voller Stärke auf `kMinCeiling`.
void AudioLimiter::rebuild() {
    m_ceiling = 1.0 - (1.0 - kMinCeiling) * m_strength;
}

double AudioLimiter::lastReductionDb() const {
    return 20.0 * std::log10(std::max(1e-9, m_minGain));
}

void AudioLimiter::process(float* samples, int frames) {
    if (!samples || frames <= 0) return;
    if (!m_enabled.load(std::memory_order_relaxed)) return;
    //  Leere Stärke heisst BITGENAU nichts - kein Filter, keine Rundung, und
    //  vor allem keine Verzögerung.
    const int pm = m_strengthPm.load(std::memory_order_relaxed);
    if (pm <= 0) return;

    const int ch = m_channels;
    const int splits = kBands - 1;
    if (int(m_state.size()) < ch * splits) return;          // configure() fehlt
    if (int(m_ring.size()) < m_look * kBands * ch) return;

    //  Zehn Bandwerte je Kanal - sie leben nur hier, es gibt keinen
    //  Zwischenpuffer über die Schleife hinaus.
    double band[kBands][8];

    for (int f = 0; f < frames; ++f) {
        //  Zerlegen. Was der Tiefpass durchlässt, ist das Band; der Rest geht
        //  weiter. Die Summe ergibt wieder genau das Eingangssignal.
        for (int c = 0; c < ch; ++c) {
            double rest = double(samples[f * ch + c]);
            double* st = &m_state[size_t(c) * size_t(splits)];
            for (int k = 0; k < splits; ++k) {
                st[k] += m_coef[size_t(k)] * (rest - st[k]);
                band[k][c] = st[k];
                rest -= st[k];
            }
            band[kBands - 1][c] = rest;
        }

        //  Detektor auf dem UNVERZÖGERTEN Signal: was ginge jetzt hinaus?
        double peak = 0.0, total = 0.0;
        for (int c = 0; c < ch; ++c) {
            double y = 0.0;
            for (int k = 0; k < kBands; ++k) {
                const double v = m_gain[size_t(k)] * band[k][c];
                y     += v;
                total += std::abs(v);
            }
            peak = std::max(peak, std::abs(y));
        }
        //  Spitzenhalt: der Ausschlag bleibt stehen, bis er heraus ist.
        if (peak >= m_held) { m_held = peak; m_holdLeft = m_look; }
        else if (--m_holdLeft <= 0) { m_held = peak; m_holdLeft = m_look; }

        if (m_held > m_ceiling && total > 1e-12) {
            const double r = m_ceiling / m_held;
            for (int k = 0; k < kBands; ++k) {
                double contrib = 0.0;
                for (int c = 0; c < ch; ++c) contrib += std::abs(m_gain[size_t(k)] * band[k][c]);
                //  Gewichtet nach Beitrag: das lauteste Band gibt am meisten
                //  nach. Der Exponent liegt im Mittel bei 1, die Summe der
                //  Anteile also bei der nötigen Gesamtabsenkung.
                const double want = std::pow(r, double(kBands) * (contrib / total));
                double& g = m_gain[size_t(k)];
                g += (want < g ? m_atk : m_rel) * (want - g);
                if (g < m_minGain) m_minGain = g;
            }
        } else {
            for (int k = 0; k < kBands; ++k)
                m_gain[size_t(k)] += m_rel * (1.0 - m_gain[size_t(k)]);
        }

        //  Ausgeben: die Absenkung trifft die VERZÖGERTEN Bänder, während an
        //  derselben Stelle die neuen einziehen.
        for (int c = 0; c < ch; ++c) {
            double out = 0.0;
            for (int k = 0; k < kBands; ++k) {
                float& slot = m_ring[(size_t(m_wpos) * size_t(kBands) + size_t(k))
                                     * size_t(ch) + size_t(c)];
                out += m_gain[size_t(k)] * double(slot);
                slot = float(band[k][c]);
            }
            samples[f * ch + c] = float(out);
        }
        if (++m_wpos >= m_look) m_wpos = 0;
    }
}
