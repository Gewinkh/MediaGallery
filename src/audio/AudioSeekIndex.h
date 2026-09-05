#pragma once
// Springen ohne Dekodieren: MP3, MP2, AC-3, E-AC-3 und AAC-ADTS sind selbstrahmend, ein Lauf über die
// Rahmenköpfe findet das Zielbyte (11 ms für 307.471 Rahmen gegen 2813 ms Vorspulen). `setSourceDevice`
// ignoriert die Geräteposition - daher `TailDevice`, dessen Position 0 der Zielrahmen IST.

#include <QFile>
#include <QIODevice>
#include <QString>

#include <atomic>

namespace AudioSeek {

//  Lohnt sich der Weg für diese Datei? Reine ENDUNGS-Prüfung - Hüllen (m4a,
//  ogg, flac, wav) tragen ihren Index an anderer Stelle und kommen hier nicht
//  durch.
bool isSelfFraming(const QString& path);

struct Position {
    bool    ok         = false;
    qint64  byteOffset = 0;   // erster Rahmen AB der gesuchten Stelle
    qint64  sampleAt   = 0;   // wie viele Abtastwerte davor liegen
    int     sampleRate = 0;   // aus dem Rahmenkopf
    qint64  ms() const { return sampleRate > 0 ? sampleAt * 1000 / sampleRate : 0; }
};

// Den Rahmen suchen, der die Zeitstelle ENTHÄLT; der Aufrufer überspringt danach höchstens eine Rahmenlänge.
// Gelesen werden nur Kopfbytes, in Stücken - die Datei wird NIE am Stück geladen.
Position findFrame(const QString& path, qint64 targetMs,
                   const std::atomic<bool>* cancel = nullptr);

//  Ein Gerät, das nur den REST einer Datei zeigt: seine Position 0 ist
//  `from`. Notwendig, weil der Dekoder auf 0 zurückspult (s. o.).
class TailDevice : public QIODevice {
public:
    TailDevice(const QString& path, qint64 from, QObject* parent = nullptr);

    bool   open(OpenMode mode) override;
    void   close() override;
    qint64 size() const override;
    bool   isSequential() const override { return false; }
    bool   seek(qint64 pos) override;

protected:
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    QFile  m_file;
    qint64 m_from = 0;
};

} // namespace AudioSeek
