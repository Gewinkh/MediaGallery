#pragma once
#include <QMutex>
#include <vector>

// Ein Schreiber (Dekoder, GUI-Thread), ein Leser (Zieh-Ruf, Audio-Thread). Bewusst
// klein (~200 ms = 38 kB): eine ganze Datei zu dekodieren kostete bei fuenf Minuten
// Stereo/48 kHz rund 115 MB. Mutex statt schlossfrei - zwei memcpy von wenigen kB.
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
