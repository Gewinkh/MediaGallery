#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <QObject>
#include <QVector>

// Zehn ISO-Oktaven je +-12 dB, ADDITIV überlagert: Durchgriff plus RBJ-Bandpässe
// in Direct Form II transposed (Q folgt dem Gain, s. `makeBiquad`);
// Stereo/48 kHz kostet unter 1 % eines Kerns. Koeffizienten werden nur bei Aenderung
// gerechnet und als Satz atomar getauscht - im Audio-Pfad liegt kein Schloss.
class AudioEqualizer : public QObject {
    Q_OBJECT
public:
    static constexpr int kBands = 10;
    static constexpr double kMaxGainDb = 12.0;
    //  Der Preamp reicht WEITER nach unten als nach oben - Platz zum Leisermachen
    //  braucht man oefter als zum Lautermachen.
    static constexpr double kMinPreampDb = -24.0;
    static constexpr double kMaxPreampDb =  12.0;
    static const std::array<double, kBands>& frequencies();

    explicit AudioEqualizer(QObject* parent = nullptr);

    void configure(int sampleRate, int channels);

    void setBandGain(int band, double db);      // −12 … +12
    double bandGain(int band) const;
    void setGains(const QVector<double>& db);   // alle zehn auf einmal
    QVector<double> gains() const;

    void setPreamp(double db);
    double preamp() const { return m_preampDb; }

    void setEnabled(bool on);                   // Bypass
    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }

    // Die größte Verstärkung, die die Kette irgendwo im Spektrum erzeugt (dB, nie negativ) - nicht das Maximum der
    // Regler. Aus den Koeffizienten über ein Frequenzraster, nur bei Reglerwechsel, nie je Sample.
    double peakGainDb() const;

    void process(float* samples, int frames);

    void resetState();

signals:
    void changed();

private:
    //  `lin` ist das Gewicht des Bandanteils (A^2-1); die Sektion selbst ist
    //  ein Bandpass, kein Peaking-Filter (s. `makeBiquad`).
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, lin = 0; };
    // Bänder auf 0 dB stehen gar nicht darin: ein Durchlass-Filter kostet sonst je Sample fünf Multiplikationen für
    // nichts (gemessen: zehn aktive Bänder 1,8 % eines Kerns, zwei 0,4 %).
    struct CoeffSet {
        std::array<Biquad, kBands> band {};
        std::array<int, kBands>    slot {};
        int   count = 0;
        float preamp = 1.0f;
    };

    void rebuild();
    bool makeBiquad(int band, double gainDb, Biquad* out) const;

    std::array<double, kBands> m_gainDb {};
    double m_preampDb = 0.0;
    int    m_sampleRate = 48000;
    int    m_channels = 2;
    std::atomic<bool> m_enabled { false };

    std::shared_ptr<const CoeffSet> m_coeffs;
    std::vector<std::array<double, 2>> m_state;   // [channel * kBands + band]
};
