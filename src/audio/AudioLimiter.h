#pragma once
#include <array>
#include <atomic>
#include <vector>

#include <QObject>

#include "audio/AudioEqualizer.h"

// Begrenzer am Ende des Abspielwegs: das Signal wird in dieselben zehn Oktavbänder
// zerlegt wie beim Equalizer, und reißt die Summe die Decke, gibt jedes Band nach
// dem Maß seines BEITRAGS nach - das lauteste am meisten. Ohne Eingriff setzt sich
// die Zerlegung bitgenau wieder zusammen.
// Die Vorausschau verzögert die Bänder, während der Detektor schon das ungefilterte
// Signal sieht: die Absenkung steht damit, wenn die laute Stelle herauskommt. Das
// erlaubt die Decke bei 0,60 statt 0,25 und hält leise Passagen 8,7 dB lauter als
// eine feste Gegenrechnung; ein längeres Fenster hält sie nur länger stehen.
class AudioLimiter : public QObject {
    Q_OBJECT
public:
    static constexpr int    kBands       = AudioEqualizer::kBands;
    static constexpr double kLookaheadMs = 1.0;
    //  Darüber bleibt Material am Anschlag, darunter kostet es nur Lautstärke.
    static constexpr double kMinCeiling  = 0.60;

    explicit AudioLimiter(QObject* parent = nullptr);

    void configure(int sampleRate, int channels);
    // Verwirft Filterzustände UND die Vorausschau. Pflicht bei jedem Sprung,
    // sonst reicht der alte Abschnitt in den neuen hinein.
    void resetState();

    void setEnabled(bool on);
    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }

    void   setStrength(double v);                  // 0 … 1
    double strength() const { return m_strength; }

    // Greift die Stufe gerade ein? Stärke 0 ist genau so gut wie „aus".
    bool active() const {
        return m_enabled.load(std::memory_order_relaxed)
               && m_strengthPm.load(std::memory_order_relaxed) > 0;
    }

    // Verzögerung, die die Stufe dem Weg hinzufügt (Frames) - 0, solange sie ruht.
    int latencyFrames() const { return active() ? m_look : 0; }

    void process(float* samples, int frames);

    // Kleinste Absenkung seit dem letzten Abruf (dB, nie positiv) - Anzeige.
    double lastReductionDb() const;

signals:
    void changed();

private:
    void rebuild();

    //  Ein Einpol-Tiefpass je Grenze, getrennt durch ABZIEHEN - die Summe ergibt
    //  damit wieder genau das Eingangssignal.
    std::array<double, kBands - 1> m_coef {};
    std::vector<double> m_state;                   // [kanal * (kBands-1) + grenze]

    std::array<double, kBands> m_gain {};          // laufende Absenkung je Band
    double m_ceiling = 1.0;

    //  Ringpuffer der Bänder, in float - er trägt nur Nutzsignal.
    std::vector<float> m_ring;                     // [pos][band][kanal]
    int m_look = 0;
    int m_wpos = 0;
    //  Spitzenhalt: ein Ausschlag bleibt stehen, bis er den Ausgang passiert hat.
    double m_held = 0.0;
    int    m_holdLeft = 0;

    double m_atk = 0.0, m_rel = 0.0;
    int m_sampleRate = 48000;
    int m_channels   = 2;
    double m_strength = 0.0;
    double m_minGain  = 1.0;

    std::atomic<bool> m_enabled { false };
    //  Die Stärke wandert vom Bedienfaden in den Tonfaden. Als Promille-Ganzzahl,
    //  weil `std::atomic<double>` nicht auf jeder Plattform schlossfrei ist.
    std::atomic<int> m_strengthPm { 0 };
};
