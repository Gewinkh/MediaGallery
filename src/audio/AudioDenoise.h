#pragma once
#include <array>
#include <atomic>
#include <vector>

#include <QObject>

#include "audio/AudioEqualizer.h"

// Rauschunterdrueckung im Abspielweg: dieselben zehn ISO-Oktaven wie der
// Equalizer, je Band wird der laufende Pegel mit dem eigenen Grundrauschen
// verglichen und abgesenkt, was nicht deutlich darueber liegt. Die Tiefe haengt
// an einem Pegel (0 = gar nichts, 1 = volle Tiefe).
// Die Stufe liegt VOR dem Equalizer und fasst dessen Regler nicht an: der
// ungefilterte Zustand geht durch das Auf und Ab des Pegels nie verloren.
class AudioDenoise : public QObject {
    Q_OBJECT
public:
    static constexpr int    kBands    = AudioEqualizer::kBands;
    static constexpr double kMaxCutDb = 36.0;      // Tiefe bei vollem Pegel

    explicit AudioDenoise(QObject* parent = nullptr);

    void configure(int sampleRate, int channels);
    void resetState();

    void setEnabled(bool on);
    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }

    void   setLevel(double v);                     // 0 … 1
    double level() const { return m_level; }

    // Greift die Stufe gerade ein? Ein Pegel von 0 ist genau so gut wie „aus".
    bool active() const {
        return m_enabled.load(std::memory_order_relaxed)
               && m_levelPm.load(std::memory_order_relaxed) > 0;
    }

    void process(float* samples, int frames);

    // Die zuletzt anliegende Absenkung je Band (dB, nie positiv) - fuer Test
    // und Pruefstand; im Abspielweg wird sie nicht gebraucht.
    std::array<double, kBands> reductionDb() const;

signals:
    void changed();

private:
    void buildFilters();
    void updateGains(double level);

    // Ein Koeffizient je Trennstelle (Einpol-Tiefpass); Baender = Trennstellen + 1.
    std::array<double, kBands> m_coef {};
    int m_count = 1;

    std::vector<double> m_state;                   // [channel * splits + split]

    std::array<double, kBands> m_gain     {};      // laufende Absenkung, linear
    std::array<double, kBands> m_gainStep {};      // Rampe je Frame
    std::array<double, kBands> m_envFast  {};
    std::array<double, kBands> m_envSlow  {};
    std::array<double, kBands> m_floor    {};
    std::array<double, kBands> m_min      {};
    std::array<double, kBands> m_minCand  {};
    std::array<double, kBands> m_acc      {};      // Quadratsumme im Block

    int m_accFrames = 0;
    int m_winBlocks = 0;
    // Zeitkonstanten haengen an der Abtastrate - gesetzt in configure().
    double m_atk = 0, m_rel = 0, m_slow = 0, m_open = 0, m_close = 0;

    int m_sampleRate = 48000;
    int m_channels   = 2;
    double m_level   = 0.0;

    std::atomic<bool> m_enabled { false };
    // Der Pegel wandert vom Bedienfaden in den Tonfaden. Als Promille-Ganzzahl,
    // weil `std::atomic<double>` nicht auf jeder Plattform schlossfrei ist.
    std::atomic<int> m_levelPm { 0 };
};
