#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <QObject>
#include <QVector>

// Zehn ISO-Oktaven je +-12 dB, RBJ-Peaking-Biquads in Direct Form II transposed;
// Stereo/48 kHz kostet unter 1 % eines Kerns. Koeffizienten werden nur bei Aenderung
// gerechnet und als Satz atomar getauscht - im Audio-Pfad liegt kein Schloss.
class AudioEqualizer : public QObject {
    Q_OBJECT
public:
    static constexpr int kBands = 10;
    static constexpr double kMaxGainDb = 12.0;
    //  Der Preamp reicht WEITER nach unten als nach oben: die Gegenrechnung
    //  gegen das Uebersteuern braucht bei zehn angehobenen Baendern gut 12,5 dB
    //  (gemessen), und bei -12 dB blieben 6 % der Werte am Anschlag haengen.
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

    // Vorverstärkung, bei der die Kette gerade nicht mehr übersteuert (negativ, 0 wenn nichts angehoben ist).
    // Früher stand hier `-größte Anhebung`, was ein einzelnes Band um volle 12 dB absenkte.
    double suggestedPreamp() const;

    void process(float* samples, int frames);

    void resetState();

signals:
    void changed();

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
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
