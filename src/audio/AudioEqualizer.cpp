#include "audio/AudioEqualizer.h"

#include <algorithm>
#include <cmath>

namespace {
//  Güte der Bänder: bei Oktavabstand ergibt Q ≈ 1,41 einen glatten Verlauf -
//  benachbarte Bänder überlappen sich, ohne einander auszulöschen.
constexpr double kQ = 1.41;
}  // namespace

const std::array<double, AudioEqualizer::kBands>& AudioEqualizer::frequencies() {
    static const std::array<double, kBands> f {
        31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
    };
    return f;
}

AudioEqualizer::AudioEqualizer(QObject* parent) : QObject(parent) {
    m_gainDb.fill(0.0);
    rebuild();
    configure(m_sampleRate, m_channels);
}

void AudioEqualizer::configure(int sampleRate, int channels) {
    const int sr = sampleRate > 0 ? sampleRate : 48000;
    const int ch = std::clamp(channels, 1, 8);
    const bool rateChanged = (sr != m_sampleRate);
    m_sampleRate = sr;
    m_channels   = ch;
    m_state.assign(size_t(ch) * kBands, { 0.0, 0.0 });
    if (rateChanged) rebuild();     // die Koeffizienten hängen an der Rate
}

void AudioEqualizer::setBandGain(int band, double db) {
    if (band < 0 || band >= kBands) return;
    const double v = std::clamp(db, -kMaxGainDb, kMaxGainDb);
    if (qFuzzyCompare(m_gainDb[size_t(band)] + 1.0, v + 1.0)) return;
    m_gainDb[size_t(band)] = v;
    rebuild();
    emit changed();
}

double AudioEqualizer::bandGain(int band) const {
    return (band >= 0 && band < kBands) ? m_gainDb[size_t(band)] : 0.0;
}

void AudioEqualizer::setGains(const QVector<double>& db) {
    for (int i = 0; i < kBands; ++i)
        m_gainDb[size_t(i)] = (i < db.size()) ? std::clamp(db.at(i), -kMaxGainDb, kMaxGainDb)
                                              : 0.0;
    rebuild();
    emit changed();
}

QVector<double> AudioEqualizer::gains() const {
    QVector<double> out;
    out.reserve(kBands);
    for (double g : m_gainDb) out.append(g);
    return out;
}

void AudioEqualizer::setPreamp(double db) {
    const double v = std::clamp(db, -kMaxGainDb, kMaxGainDb);
    if (qFuzzyCompare(m_preampDb + 1.0, v + 1.0)) return;
    m_preampDb = v;
    rebuild();
    emit changed();
}

void AudioEqualizer::setEnabled(bool on) {
    if (m_enabled.load(std::memory_order_relaxed) == on) return;
    m_enabled.store(on, std::memory_order_relaxed);
    emit changed();
}

//  Anhebungen addieren sich nicht einfach, überlappen sich aber: als
//  Faustregel reicht die stärkste Anhebung als Gegenwert - mehr wegzunehmen
//  kostete nur Lautstärke.
double AudioEqualizer::suggestedPreamp() const {
    const double maxGain = *std::max_element(m_gainDb.cbegin(), m_gainDb.cend());
    return maxGain > 0.0 ? -maxGain : 0.0;
}

//  RBJ-Peaking-Biquad („Cookbook"-Formeln, allgemein bekannt und hier
//  eigenständig ausgeschrieben). Bei 0 dB ergibt sich exakt der Durchlass
//  (b0=1, alles andere 0) - ein Band ohne Anhebung kostet dann zwar noch
//  Rechenschritte, ändert aber bitgenau nichts.
void AudioEqualizer::rebuild() {
    auto set = std::make_shared<CoeffSet>();
    set->preamp = float(std::pow(10.0, m_preampDb / 20.0));

    for (int i = 0; i < kBands; ++i) {
        const double gainDb = m_gainDb[size_t(i)];
        //  Ein Band ohne Anhebung wird ÜBERSPRUNGEN, nicht als Durchlass
        //  gerechnet.
        if (std::abs(gainDb) < 1e-9) continue;
        Biquad& q = set->band[size_t(set->count)];
        set->slot[size_t(set->count)] = i;
        ++set->count;

        const double A     = std::pow(10.0, gainDb / 40.0);
        const double w0    = 2.0 * M_PI * frequencies()[size_t(i)] / double(m_sampleRate);
        //  Über der halben Abtastrate gibt es nichts mehr zu heben.
        if (w0 >= M_PI) { --set->count; continue; }   // über der halben Rate
        const double alpha = std::sin(w0) / (2.0 * kQ);
        const double cosw0 = std::cos(w0);

        const double b0 =  1.0 + alpha * A;
        const double b1 = -2.0 * cosw0;
        const double b2 =  1.0 - alpha * A;
        const double a0 =  1.0 + alpha / A;
        const double a1 = -2.0 * cosw0;
        const double a2 =  1.0 - alpha / A;

        q.b0 = b0 / a0;
        q.b1 = b1 / a0;
        q.b2 = b2 / a0;
        q.a1 = a1 / a0;
        q.a2 = a2 / a0;
    }
    //  Als GANZES tauschen: der Audio-Pfad sieht entweder den alten oder den
    //  neuen Satz, nie eine Mischung.
    std::atomic_store(&m_coeffs, std::shared_ptr<const CoeffSet>(std::move(set)));
}

void AudioEqualizer::resetState() {
    for (auto& s : m_state) s = { 0.0, 0.0 };
}

void AudioEqualizer::process(float* samples, int frames) {
    if (!samples || frames <= 0) return;
    if (!m_enabled.load(std::memory_order_relaxed)) return;

    //  EINMAL abgreifen, dann auf der eigenen Kopie arbeiten.
    const std::shared_ptr<const CoeffSet> set = std::atomic_load(&m_coeffs);
    if (!set) return;

    const int ch = m_channels;
    if (int(m_state.size()) < ch * kBands) return;   // configure() fehlt

    const int active = set->count;
    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < ch; ++c) {
            double x = double(samples[f * ch + c]) * double(set->preamp);
            for (int b = 0; b < active; ++b) {
                const Biquad& q = set->band[size_t(b)];
                //  Der Zustand gehört dem BAND, nicht dem Platz - sonst
                //  sprängen die Speicher, sobald ein Regler auf null geht.
                auto& st = m_state[size_t(c) * kBands + size_t(set->slot[size_t(b)])];
                //  Direct Form II transposed: ein Speicherpaar je Filter.
                const double y = q.b0 * x + st[0];
                st[0] = q.b1 * x - q.a1 * y + st[1];
                st[1] = q.b2 * x - q.a2 * y;
                x = y;
            }
            //  Erst am Ausgang klemmen - dazwischen darf es über 1 gehen.
            samples[f * ch + c] = float(std::clamp(x, -1.0, 1.0));
        }
    }
}
