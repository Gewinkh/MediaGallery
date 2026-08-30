#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <QObject>
#include <QVector>

// ─────────────────────────────────────────────────────────────────────────────
//  AudioEqualizer - 10 Bänder, Vorverstärkung, Bypass.
//
//  BÄNDER (ISO-Oktaven): 31 · 62 · 125 · 250 · 500 · 1k · 2k · 4k · 8k · 16k Hz,
//  je ±12 dB; dazu ein Preamp (−12…+12 dB).
//
//  FILTER: je Band ein **RBJ-Peaking-Biquad** in Direct Form II transposed.
//  Kosten je Sample und Band: 5 Multiplikationen + 4 Additionen -> Stereo,
//  48 kHz, 10 Bänder ≈ 4,3 Mio Rechenschritte/s, unter 1 % eines Kerns
//  (nachgemessen mit `bench_audio`).
//
//  ZWEI REGELN, die den Audio-Pfad billig halten:
//   • Koeffizienten werden **nur bei Änderung** gerechnet, nie je Sample.
//   • Der fertige Satz wird als Ganzes übergeben (`std::shared_ptr`, atomar
//     getauscht) - im Verarbeitungsschritt liegt **kein Schloss**, und ein
//     halb gesetzter Satz kann nicht vorkommen.
//
//  Der Zustand (je Kanal und Band zwei Speicher) gehört der Verarbeitung, nicht
//  den Koeffizienten: ein Reglerwechsel klingt damit weich statt zu knacken.
// ─────────────────────────────────────────────────────────────────────────────
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
    //  Mittenfrequenzen der Bänder (Hz).
    static const std::array<double, kBands>& frequencies();

    explicit AudioEqualizer(QObject* parent = nullptr);

    //  Abtastrate und Kanalzahl der laufenden Wiedergabe. Ändert sich etwas,
    //  werden Koeffizienten und Zustand neu aufgebaut.
    void configure(int sampleRate, int channels);

    void setBandGain(int band, double db);      // −12 … +12
    double bandGain(int band) const;
    void setGains(const QVector<double>& db);   // alle zehn auf einmal
    QVector<double> gains() const;

    void setPreamp(double db);
    double preamp() const { return m_preampDb; }

    void setEnabled(bool on);                   // Bypass
    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }

    //  Die groesste Verstaerkung, die die Filterkette IRGENDWO im Spektrum
    //  erzeugt (dB, nie negativ). Nicht das Maximum der Regler: mehrere
    //  benachbarte Baender addieren sich, ein einzelnes hebt nur um seine
    //  Mitte herum. Gerechnet aus den Koeffizienten ueber ein Frequenzraster -
    //  nur bei Reglerwechsel, nie je Sample.
    double peakGainDb() const;

    //  Vorverstaerkung, bei der die Kette gerade nicht mehr uebersteuert
    //  (negativer Wert, 0 wenn nichts angehoben ist). Frueher stand hier
    //  `-groesste Anhebung`, was ein einzelnes Band um volle 12 dB absenkte,
    //  obwohl es breitbandig nur ~4 dB lauter macht.
    double suggestedPreamp() const;

    //  Verarbeitet `frames` × `channels` Werte AN ORT UND STELLE (Interleaved).
    //  Läuft im Zuliefer-Thread der Wiedergabe; kein Schloss, keine Zuweisung.
    void process(float* samples, int frames);

    //  Zustand vergessen (Titelwechsel, Sprung) - sonst klingt der Rest des
    //  vorherigen Titels in die ersten Millisekunden des nächsten hinein.
    void resetState();

signals:
    void changed();

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    //  Ein vollständiger Satz: die AKTIVEN Bänder (dicht gepackt) + die
    //  Vorverstärkung als linearer Faktor. Bänder auf 0 dB stehen gar nicht
    //  darin - ein Durchlass-Filter kostet sonst je Sample fünf
    //  Multiplikationen für nichts, und die meisten Regler stehen meistens auf
    //  null (gemessen: alle zehn aktiv 1,8 % eines Kerns, zwei aktiv 0,4 %).
    struct CoeffSet {
        std::array<Biquad, kBands> band {};
        //  Zu welchem Band gehört Platz i? (für den Zustand je Kanal)
        std::array<int, kBands>    slot {};
        int   count = 0;
        float preamp = 1.0f;
    };

    void rebuild();
    //  Koeffizienten EINES Bandes. Von `rebuild` und `peakGainDb` benutzt,
    //  damit die Rechnung nur an einer Stelle steht.
    bool makeBiquad(int band, double gainDb, Biquad* out) const;

    std::array<double, kBands> m_gainDb {};
    double m_preampDb = 0.0;
    int    m_sampleRate = 48000;
    int    m_channels = 2;
    std::atomic<bool> m_enabled { false };

    //  Der aktive Satz. Der Audio-Pfad greift ihn EINMAL je Aufruf ab und
    //  arbeitet dann auf seiner Kopie des Zeigers weiter.
    std::shared_ptr<const CoeffSet> m_coeffs;
    //  Zustand je Kanal und Band (zwei Speicher, Direct Form II transposed).
    std::vector<std::array<double, 2>> m_state;   // [channel * kBands + band]
};
