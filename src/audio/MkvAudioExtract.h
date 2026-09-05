#pragma once
// Holt die Tonspur aus MKV/WEBM ohne Neukodierung; Ogg-Hülle für Opus/Vorbis (deren Pakete tragen ihre Länge
// nicht selbst), roher Strom für AC-3, E-AC-3 und MPEG-Audio. AAC bräuchte erst ADTS-Köpfe, DTS/ALAC/PCM
// haben kein Ziel. Vorbis-Granule kommt aus dem Blockzeitstempel - das kann die Dauer minimal verschieben.

#include <QIODevice>
#include <QString>
#include <QVector>

#include <atomic>

namespace MkvAudio {

enum class Result {
    Ok = 0,
    NotOpenable,        // Datei nicht lesbar
    NotMatroska,        // keine EBML-Kennung
    NoAudioTrack,       // kein Ton in der Datei
    UnsupportedCodec,   // Ton vorhanden, aber nicht Opus/Vorbis
    Damaged,            // Elemente/Blöcke widersprechen sich
    TooLarge,           // Schutzgrenzen überschritten
    WriteFailed
};

//  Eine Tonspur, wie sie in der Datei beschrieben ist - genug, damit die
//  Oberfläche sie benennen kann, wenn es mehr als eine gibt.
struct TrackDesc {
    QString codec;              // "A_OPUS", "A_EAC3", …
    QString language;           // ISO-639-2 wie in der Datei ("ger"), sonst leer
    QString name;               // Name der Spur, falls die Datei einen führt
    int     channels = 0;
    int     rate     = 0;
    bool    supported = false;  // lässt sie sich überhaupt herauskopieren?
};

struct Info {
    Result  result      = Result::NotOpenable;
    QString codec;              // "A_OPUS", "A_VORBIS", … (wie in der Datei)
    int     audioTracks = 0;
    QVector<TrackDesc> tracks;  // ALLE Tonspuren, in der Reihenfolge der Datei
    int     sampleRate  = 0;
    int     channels    = 0;
    qint64  durationMs  = 0;
    qint64  audioBytes  = 0;    // Nutzlast der Tonspur
    int     packets     = 0;
    bool ok() const { return result == Result::Ok; }
};

bool isCandidate(const QString& path);

//  Liest nur den Kopf (EBML-Header, `Tracks`) - ohne die Blöcke anzufassen.
//  `Info::tracks` beschreibt dabei ALLE Tonspuren, `codec`/`rate`/`channels`
//  die gewählte; `probeTrack` wählt eine andere als die erste.
Info probe(const QString& path);
Info probeTrack(const QString& path, int trackIndex);

// Schreibt atomar über QSaveFile; `cancel` wird während des Kopierens regelmäßig gelesen. `trackIndex` zählt
// die Tonspuren in Dateireihenfolge - ein Index außerhalb wird zu 0.
Result extract(const QString& srcPath, const QString& targetPath,
               Info* info = nullptr,
               const std::atomic<bool>* cancel = nullptr,
               int trackIndex = 0);

// Tonspur ab einer Zeitstelle als selbstrahmender Strom - genau das, was ein Dekoder
// mitten im Strom aufnehmen kann. Gefunden ueber Cues, sonst durch Abschreiten der
// Cluster (1,5 GB: 451 ms kalt). actualMs meldet den Clusteranfang, also etwas davor.
QIODevice* openRawStream(const QString& srcPath, int trackIndex, qint64 startMs,
                         qint64* actualMs, QObject* parent = nullptr);

//  Freier Zielpfad NEBEN der Quelle. Die ENDUNG hängt am Codec: `.opus` für
//  Opus, `.ogg` für Vorbis - deshalb wird die Datei dafür kurz angelesen.
QString targetPathFor(const QString& srcPath, int trackIndex = 0);

} // namespace MkvAudio
