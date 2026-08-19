#pragma once
#include <QMutex>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  AudioRing - der kleine Vorrat zwischen Dekoder und Ausgabe.
//
//  EIN Schreiber (der Dekoder, im GUI-Thread) und EIN Leser (der Zuliefer-Ruf
//  der Ausgabe, in Qts Audio-Thread). Der Puffer ist bewusst KLEIN (~200 ms):
//  eine ganze Datei zu dekodieren kostete bei fünf Minuten Stereo/48 kHz rund
//  115 MB - RAM ist Priorität 1. 200 ms sind ~38 kB und überbrücken jede
//  Nachschub-Verzögerung, die eine Platte macht.
//
//  Geschützt wird mit einem MUTEX, nicht schlossfrei: die kritischen Abschnitte
//  sind zwei `memcpy` von wenigen kB, und ein selbst gebauter schlossfreier Ring
//  wäre die Art von Cleverness, die man ein Jahr später nicht mehr versteht
//  (§0-Priorität 5 vor 2). Ob das trägt, misst `bench_audio` (Unterläufe).
// ─────────────────────────────────────────────────────────────────────────────
class AudioRing {
public:
    //  Fassungsvermögen in EINZELWERTEN (Frames × Kanäle), nicht in Bytes.
    void resize(size_t samples);
    void clear();

    size_t capacity() const;
    size_t available() const;      // wie viel drin ist
    size_t space() const;          // wie viel noch hineinpasst

    //  Schreibt so viel wie möglich; liefert die Zahl der übernommenen Werte.
    size_t write(const float* src, size_t count);
    //  Liest bis zu `count` Werte; liefert die Zahl der gelieferten Werte.
    //  Der Rest des Zielpuffers bleibt unberührt (der Aufrufer füllt mit Stille).
    size_t read(float* dst, size_t count);

private:
    mutable QMutex     m_mutex;
    std::vector<float> m_buf;
    size_t             m_head = 0;   // nächste Schreibstelle
    size_t             m_tail = 0;   // nächste Lesestelle
    size_t             m_fill = 0;   // belegte Werte
};
