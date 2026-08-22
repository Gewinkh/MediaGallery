#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  AudioSeekIndex - SPRINGEN in selbstrahmenden Tonströmen, ohne zu dekodieren.
//
//  Das Problem: `QAudioDecoder` kann in Qt 6 nicht springen. `AudioEngine::seek`
//  musste deshalb von vorn dekodieren und alles davor verwerfen - gemessen
//  **2813 ms** für einen Sprung auf 45 min in einem Film-Ton.
//
//  Die Lösung braucht keinen eigenen Dekoder, nur einen Blick in die
//  Rahmenköpfe: MP3, MP2, AC-3, E-AC-3 und AAC-ADTS sind **selbstrahmend** -
//  jeder Rahmen beginnt mit einem Synchronwort und nennt seine Länge und die
//  Zahl seiner Abtastwerte. Wer die Köpfe entlangläuft, weiß, an welchem BYTE
//  eine Zeitstelle beginnt (gemessen: 11 ms für alle 307.471 Rahmen einer
//  225-MB-Datei; bis zu einer Stelle bei 45 min nur 3 ms).
//
//  ZWEI FALLEN, beide gemessen, beide hier gelöst:
//   • `QAudioDecoder::setSourceDevice` IGNORIERT die Position des Geräts - der
//     Dekoder spult selbst auf 0 zurück. Das Ergebnis war bitgenau der
//     Dateianfang. Deshalb `TailDevice`: ein Gerät, dessen Position 0 der
//     Zielrahmen IST.
//   • Ein Strom, der mitten im Rahmen beginnt, ist Müll. Deshalb wird immer auf
//     einen echten Rahmenkopf aufgesetzt, nie auf einen gerechneten Versatz.
//
//  Muster `AudioTags`/`MkvAudioExtract`: nur Qt6::Core, **kein `Q_OBJECT`**,
//  isoliert testbar (`tests/audio/tst_audioseek.cpp`). Härtung nach Regel 21:
//  jede Länge wird gegen die vorhandenen Bytes geprüft.
// ─────────────────────────────────────────────────────────────────────────────

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

//  Den Rahmen suchen, der die Zeitstelle ENTHÄLT - also den letzten, der nicht
//  hinter ihr beginnt. Der Aufrufer setzt dort auf und überspringt den Rest
//  (`targetMs - Position::ms()`, höchstens eine Rahmenlänge).
//  Gelesen werden nur Kopfbytes, in Stücken - die Datei wird NIE am Stück
//  geladen. `cancel` wird regelmäßig geprüft (Regel 8).
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
