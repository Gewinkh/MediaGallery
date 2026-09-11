#include "audio/AudioDenoise.h"

#include <algorithm>
#include <cmath>

namespace {
//  Die Absenkung wird alle 64 Frames neu bestimmt (1,3 ms bei 48 kHz) und
//  dazwischen je Frame linear nachgezogen - eine sprungweise Verstaerkung
//  knackt hoerbar.
constexpr int kControl = 64;

//  Halbes Fenster des gleitenden Minimums: das Grundrauschen ist der leiseste
//  Stand der letzten gut anderthalb Sekunden.
constexpr int kWinBlocks = 512;

//  Das Minimum einer schwankenden Groesse liegt systematisch UNTER ihrem
//  Mittel; ohne diesen Ausgleich schaetzt die Stufe das Grundrauschen zu
//  niedrig und senkt dann fast nichts ab (gemessen: 3,3 statt 16 dB).
constexpr double kBias = 1.3;

//  Ab wie weit UEBER dem Grundrauschen ein Band als Nutzsignal gilt (+9,5 dB).
constexpr double kMargin = 3.0;

//  Steigung der Abwaertsdehnung: 3 ergibt 4:1 - 6 dB unter der Schwelle
//  werden zu 24 dB. Mit 2 (3:1) blieben von breitbandigem Rauschen 12,6 dB
//  uebrig, und das war an Musik kaum herauszuhoeren.
constexpr double kSlope = 3.0;

//  Rauschen ist LEISE. Ohne diesen Deckel zoege ein stehender Ton sein eigenes
//  „Grundrauschen" hoch und schnitte sich danach selbst weg. -48 dBFS je Band
//  liegt ueber jedem Zischen und unter allem, was noch als Musik durchgeht.
constexpr double kFloorMax = 0.00398;
constexpr double kFloorMin = 1e-6;      // nie durch null teilen
constexpr double kHuge     = 1e30;

double coefFor(double blockMs, double tauMs) {
    return 1.0 - std::exp(-blockMs / tauMs);
}
}  // namespace

AudioDenoise::AudioDenoise(QObject* parent) : QObject(parent) {
    configure(m_sampleRate, m_channels);
}

void AudioDenoise::configure(int sampleRate, int channels) {
    m_sampleRate = sampleRate > 0 ? sampleRate : 48000;
    m_channels   = std::clamp(channels, 1, 8);
    buildFilters();
    m_state.assign(size_t(m_channels) * size_t(std::max(1, m_count - 1)), 0.0);

    const double blockMs = 1000.0 * double(kControl) / double(m_sampleRate);
    m_atk  = coefFor(blockMs,   5.0);
    m_rel  = coefFor(blockMs,  80.0);
    m_slow = coefFor(blockMs, 250.0);
    m_open  = coefFor(blockMs,   4.0);
    m_close = coefFor(blockMs, 200.0);
    resetState();
}

//  Getrennt wird durch ABZIEHEN: was der Tiefpass durchlaesst, ist das Band,
//  der Rest geht weiter. Die Summe ergibt damit wieder genau das Eingangssignal
//  - stehen alle Regler gleich, faerbt die Stufe nichts ein. Eine Bank aus
//  Bandpaessen tut das nicht: dort schwankte dieselbe Absenkung ueber die
//  Frequenz um 17 dB (gemessen).
void AudioDenoise::buildFilters() {
    m_count = 0;
    const auto& freq = AudioEqualizer::frequencies();
    const double nyq = 0.45 * double(m_sampleRate);
    for (int i = 0; i + 1 < kBands; ++i) {
        const double f = std::sqrt(freq[size_t(i)] * freq[size_t(i + 1)]);
        if (f >= nyq) break;                 // darueber bleibt ein Band uebrig
        m_coef[size_t(m_count++)] =
            1.0 - std::exp(-2.0 * M_PI * f / double(m_sampleRate));
    }
    ++m_count;                               // das oberste Band ist der Rest
}

void AudioDenoise::resetState() {
    std::fill(m_state.begin(), m_state.end(), 0.0);
    m_gain.fill(1.0);
    m_gainStep.fill(0.0);
    m_envFast.fill(0.0);
    m_envSlow.fill(0.0);
    m_min.fill(kHuge);
    m_minCand.fill(kHuge);
    m_floor.fill(0.0);
    m_acc.fill(0.0);
    m_accFrames = 0;
    m_winBlocks = 0;
}

void AudioDenoise::setEnabled(bool on) {
    if (m_enabled.load(std::memory_order_relaxed) == on) return;
    m_enabled.store(on, std::memory_order_relaxed);
    emit changed();
}

void AudioDenoise::setLevel(double v) {
    const double clamped = std::clamp(v, 0.0, 1.0);
    if (qFuzzyCompare(m_level + 1.0, clamped + 1.0)) return;
    m_level = clamped;
    m_levelPm.store(int(std::lround(clamped * 1000.0)), std::memory_order_relaxed);
    emit changed();
}

std::array<double, AudioDenoise::kBands> AudioDenoise::reductionDb() const {
    std::array<double, kBands> out {};
    for (int i = 0; i < kBands; ++i)
        out[size_t(i)] = 20.0 * std::log10(std::max(1e-9, m_gain[size_t(i)]));
    return out;
}

//  Ein Regelschritt: Huellkurven fortschreiben, Grundrauschen nachfuehren,
//  daraus die Zielabsenkung als Rampe auf den naechsten Block.
void AudioDenoise::updateGains(double level) {
    const double inv = 1.0 / double(std::max(1, m_accFrames * m_channels));
    const double minGain = std::pow(10.0, -kMaxCutDb / 20.0);

    for (int b = 0; b < m_count; ++b) {
        const double rms = std::sqrt(m_acc[size_t(b)] * inv);
        m_acc[size_t(b)] = 0.0;
        //  Schnell: ein Einsatz darf nicht abgeschnitten beginnen.
        const double a = rms > m_envFast[size_t(b)] ? m_atk : m_rel;
        m_envFast[size_t(b)] += (rms - m_envFast[size_t(b)]) * a;
        //  Langsam: ueber eine Viertelsekunde gemittelt schwankt sie kaum
        //  noch - erst dadurch trifft das Minimum den Rauschteppich.
        m_envSlow[size_t(b)] += (rms - m_envSlow[size_t(b)]) * m_slow;
        m_min[size_t(b)]     = std::min(m_min[size_t(b)],     m_envSlow[size_t(b)]);
        m_minCand[size_t(b)] = std::min(m_minCand[size_t(b)], m_envSlow[size_t(b)]);
    }

    //  Zwei Fenster im Wechsel: das aeltere liefert den Wert, das juengere
    //  sammelt schon.
    if (++m_winBlocks >= kWinBlocks) {
        for (int b = 0; b < m_count; ++b) {
            m_floor[size_t(b)]   = m_min[size_t(b)] * kBias;
            m_min[size_t(b)]     = m_minCand[size_t(b)];
            m_minCand[size_t(b)] = kHuge;
        }
        m_winBlocks = 0;
    }

    for (int b = 0; b < m_count; ++b) {
        const double grund = std::clamp(m_floor[size_t(b)], kFloorMin, kFloorMax);
        const double over  = m_envFast[size_t(b)] / (grund * kMargin);
        double g = 1.0;
        if (over < 1.0) g = std::max(minGain, std::pow(over, kSlope));
        //  Der Pegel skaliert die Tiefe in dB: bei 0 ist es genau 1, bei 0,5
        //  die halbe Absenkung, bei 1 die volle.
        const double ziel = (level >= 0.999) ? g : std::pow(g, level);
        const double coef = ziel > m_gain[size_t(b)] ? m_open : m_close;
        const double neu  = m_gain[size_t(b)] + (ziel - m_gain[size_t(b)]) * coef;
        m_gainStep[size_t(b)] = (neu - m_gain[size_t(b)]) / double(kControl);
    }
    m_accFrames = 0;
}

void AudioDenoise::process(float* samples, int frames) {
    if (!samples || frames <= 0) return;
    if (!m_enabled.load(std::memory_order_relaxed)) return;
    //  Leerer Pegel heisst BITGENAU nichts - kein Filter, keine Rundung.
    const int pm = m_levelPm.load(std::memory_order_relaxed);
    if (pm <= 0) return;
    const double level = double(pm) / 1000.0;

    const int ch = m_channels;
    const int splits = m_count - 1;
    if (int(m_state.size()) < ch * std::max(1, splits)) return;   // configure() fehlt

    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < ch; ++c) {
            double rest = double(samples[f * ch + c]);
            double summe = 0.0;
            for (int b = 0; b < splits; ++b) {
                double& s = m_state[size_t(c) * size_t(splits) + size_t(b)];
                s += m_coef[size_t(b)] * (rest - s);
                const double band = s;
                rest  -= band;
                summe += m_gain[size_t(b)] * band;
                m_acc[size_t(b)] += band * band;
            }
            summe += m_gain[size_t(splits)] * rest;
            m_acc[size_t(splits)] += rest * rest;
            samples[f * ch + c] = float(std::clamp(summe, -1.0, 1.0));
        }
        for (int b = 0; b < m_count; ++b) m_gain[size_t(b)] += m_gainStep[size_t(b)];
        if (++m_accFrames >= kControl) updateGains(level);
    }
}
