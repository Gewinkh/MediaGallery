#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  Mp4AudioExtract - holt die TONSPUR aus einem MP4/MOV heraus, OHNE sie neu
//  zu kodieren, und verpackt sie als eigenständige `.m4a`.
//
//  VERFAHREN (Entscheidung des Nutzers, §0-Priorität 1 vor 4):
//  Die AAC-Blöcke werden byteweise aus `mdat` übernommen und in eine neue
//  ISO-BMFF-Datei einsortiert; neu geschrieben wird nur die BESCHREIBUNG
//  (moov/trak/stbl). Der Ton ist damit bitgleich zum Original - kein
//  Qualitätsverlust, keine Kodierzeit, kein Codec und keine Fremdbibliothek.
//  Die Alternative (dekodieren + als WAV schreiben) hätte je Minute rund 10 MB
//  gekostet; das Herauskopieren behält die Größe der Tonspur bei.
//
//  Muster `PdfPageCopier`: Abhängigkeit ist NUR Qt6::Core, **kein `Q_OBJECT`**,
//  kein moc - dadurch isoliert testbar (`tests/audio/tst_mp4extract.cpp`).
//
//  HÄRTUNG (Regel 21): Fremddaten werden nie geglaubt. Jede Box wird gegen die
//  Grenzen ihres Elternteils geprüft, jede Tabelle gegen ihre Einträge, jeder
//  Sample-Bereich gegen die Dateigröße. Nicht Erkanntes führt zu einem
//  Ergebniswert, nie zu einem Zugriff daneben.
//
//  BEWUSST NICHT ABGEDECKT (jeweils ein eigener Ergebniswert, keine Ausrede):
//   • fragmentierte Dateien (`moof`/`mvex`) - dort stehen die Sample-Tabellen
//     nicht in `stbl`, das ist ein eigener Leser;
//   • Spuren, deren Daten in einer FREMDEN Datei liegen (`dref` nicht
//     selbsttragend);
//   • andere Hüllen (mkv/webm/avi/wmv) - sie kommen hier gar nicht erst an.
//
//  Das Ergebnis ist ein ENUM, kein Text: die Übersetzung in die Sprache der
//  Oberfläche gehört nach `Strings.cpp`, nicht in den Leser.
// ─────────────────────────────────────────────────────────────────────────────

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
//  Wie `probe`, aber für eine BESTIMMTE Tonspur (0 = die erste).
Info probeTrack(const QString& path, int trackIndex);

//  Schreibt die Tonspur nach `targetPath` (atomar über QSaveFile: schlägt es
//  fehl, entsteht KEINE halbe Datei). `info` optional für die Meldung.
//  `cancel` (optional) wird während des Kopierens regelmäßig gelesen; steht es,
//  bricht der Lauf ab und die Zieldatei entsteht NICHT (QSaveFile-Rollback) -
//  das kooperative Abbrechen der Projektkonvention (CLAUDE.md Regel 8).
//  `trackIndex` zählt die TONSPUREN in der Reihenfolge der Datei; ein Index
//  außerhalb wird zu 0 - lieber die erste Spur als keine.
Result extract(const QString& srcPath, const QString& targetPath,
               Info* info = nullptr,
               const std::atomic<bool>* cancel = nullptr,
               int trackIndex = 0);

//  Freier Zielpfad NEBEN der Quelle: <Name>.m4a, bei Kollision <Name> (2).m4a …
//  (gleiche Namensregel wie `DocxEditController::pdfExportTargetPath`).
QString targetPathFor(const QString& srcPath);

} // namespace Mp4Audio
