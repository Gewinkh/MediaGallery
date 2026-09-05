#pragma once
// Holt die Tonspur aus MP4/MOV ohne Neukodierung: die AAC-Bloecke werden byteweise
// uebernommen, neu geschrieben wird nur die Beschreibung (moov/trak/stbl).
// Nicht abgedeckt: fragmentierte Dateien, Spuren mit Daten in fremder Datei.

#include <QString>
#include <QVector>

#include <atomic>

namespace Mp4Audio {

enum class Result {
    Ok = 0,
    NotOpenable,     // Datei nicht lesbar
    NotMp4,          // keine ISO-BMFF-Hülle (kein ftyp/moov)
    Fragmented,      // moof/mvex - Sample-Tabellen liegen nicht in stbl
    NoAudioTrack,    // kein `soun`-Track (z. B. stummes Video)
    ExternalMedia,   // dref verweist auf eine andere Datei
    Damaged,         // Boxen/Tabellen widersprechen sich
    TooLarge,        // Schutzgrenzen überschritten (moov, Sample-Zahl)
    WriteFailed      // Ziel nicht schreibbar
};

//  Eine Tonspur, wie sie in der Datei beschrieben ist - genug, damit die
//  Oberfläche sie benennen kann, wenn es mehr als eine gibt.
struct TrackDesc {
    QString codec;              // 4CC: "mp4a", "ac-3", …
    QString language;           // ISO-639-2 wie in `mdhd` ("ger"), sonst leer
    QString name;               // MP4 führt keinen Spurnamen - bleibt leer
    int     channels = 0;
    int     rate     = 0;
    bool    supported = true;   // jede Tonspur lässt sich in ein m4a kopieren
};

//  Was in der Datei steckt - für die Rückmeldung an den Nutzer und für die
//  Prüfstände. Bei `result != Ok` sind die Zahlen bedeutungslos.
struct Info {
    Result  result      = Result::NotOpenable;
    QString codec;              // 4CC des Sample-Eintrags: "mp4a", "alac", …
    int     audioTracks = 0;    // wie viele Tonspuren die Datei hat
    int     sampleRate  = 0;
    int     channels    = 0;
    qint64  durationMs  = 0;
    qint64  audioBytes  = 0;    // Nutzlast der Tonspur ≈ Größe der Zieldatei
    int     sampleCount = 0;
    QVector<TrackDesc> tracks;  // ALLE Tonspuren, in der Reihenfolge der Datei
    bool ok() const { return result == Result::Ok; }
};

//  Endungs-Vorprüfung (mp4/m4v/mov/qt). Sagt NICHT, ob es klappt - nur, ob es
//  sich lohnt, die Datei überhaupt anzufassen (Kontextmenü, Player-Modus).
bool isCandidate(const QString& path);

//  Liest ausschließlich die Beschreibung (moov) und rührt `mdat` nicht an -
//  billig genug, um beim Öffnen eines Menüs zu laufen.
Info probe(const QString& path);
Info probeTrack(const QString& path, int trackIndex);

// Schreibt atomar über QSaveFile - ein Fehlschlag oder ein gesetztes `cancel` hinterlässt KEINE halbe Datei.
// `trackIndex` zählt die Tonspuren in Dateireihenfolge; ein Index außerhalb wird zu 0, lieber die erste als keine.
Result extract(const QString& srcPath, const QString& targetPath,
               Info* info = nullptr,
               const std::atomic<bool>* cancel = nullptr,
               int trackIndex = 0);

//  Freier Zielpfad NEBEN der Quelle: <Name>.m4a, bei Kollision <Name> (2).m4a …
//  (gleiche Namensregel wie `DocxEditController::pdfExportTargetPath`).
QString targetPathFor(const QString& srcPath);

} // namespace Mp4Audio
