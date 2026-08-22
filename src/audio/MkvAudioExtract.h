#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  MkvAudioExtract - holt die TONSPUR aus einer MKV/WEBM heraus, OHNE sie neu
//  zu kodieren, und verpackt sie als Ogg-Datei (`.opus` bzw. `.ogg`).
//
//  Das Gegenstück zu `Mp4AudioExtract` für die zweite große Hüllenfamilie. Der
//  Weg ist derselbe - Bytes umpacken statt neu rechnen -, die Arbeit dahinter
//  aber eine andere: Matroska ist ein EBML-Baum (Elemente mit variabel langer
//  Kennung und Länge), und Opus/Vorbis passen NICHT in eine MP4-Hülle. Es
//  braucht deshalb beides: einen Leser für Matroska und einen Schreiber für Ogg
//  (Seiten, Segmenttafel, eigene Prüfsumme).
//
//  WAS GEHT - zwei Wege, je nach Codec:
//   **Ogg-Hülle** (die Pakete tragen ihre Länge nicht selbst):
//      A_OPUS    -> `<Name>.opus`  (OpusHead aus `CodecPrivate`, OpusTags neu)
//      A_VORBIS  -> `<Name>.ogg`   (die drei Kopfpakete stecken in `CodecPrivate`)
//   **Roher Strom** (selbstrahmend: jeder Rahmen nennt seine Länge selbst, eine
//   Hülle wäre überflüssig - so liegt so eine Datei auch sonst auf der Platte):
//      A_EAC3     -> `<Name>.eac3`
//      A_AC3      -> `<Name>.ac3`   (auch A_AC3/BSID9, /BSID10)
//      A_MPEG/L3  -> `<Name>.mp3`
//      A_MPEG/L2  -> `<Name>.mp2`
//   Der rohe Weg braucht KEIN `CodecPrivate` - diese Ströme haben keins.
//
//  WAS NICHT GEHT (jeweils ein eigener Ergebniswert, keine Ausrede):
//   • AAC in Matroska: es müsste erst ADTS-Köpfe aus `CodecPrivate` bekommen,
//     sonst kann kein Abspieler den Strom deuten - eine eigene Baustelle;
//   • DTS, ALAC, PCM und der Rest: kein Ziel, das ohne Neukodieren passt;
//   • Blöcke mit „Lacing" über mehrere Rahmen sind unterstützt, aber ein
//     defektes Lacing führt zum Abbruch statt zu geratenen Daten.
//
//  GRANULE (die Zeitangabe jeder Ogg-Seite; beim rohen Weg gibt es keine):
//   • **Opus**: exakt, aus dem TOC-Byte jedes Pakets gerechnet (48 kHz).
//   • **Vorbis**: aus dem ZEITSTEMPEL des Matroska-Blocks abgeleitet. Die exakte
//     Rechnung bräuchte die Blockgrößen aus dem Setup-Kopf und den Modus jedes
//     Pakets - der Unterschied ist beim Abspielen nicht hörbar, kann die
//     angezeigte Dauer aber um Bruchteile einer Sekunde verschieben.
//
//  Muster `Mp4AudioExtract`: nur Qt6::Core, **kein `Q_OBJECT`**, kein moc -
//  isoliert testbar (`tests/audio/tst_mkvextract.cpp`). Härtung nach Regel 21:
//  jede Länge wird gegen die vorhandenen Bytes geprüft, bevor gelesen wird.
// ─────────────────────────────────────────────────────────────────────────────

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

//  Endungs-Vorprüfung (mkv/webm/mka). Sagt NICHT, ob es klappt.
bool isCandidate(const QString& path);

//  Liest nur den Kopf (EBML-Header, `Tracks`) - ohne die Blöcke anzufassen.
//  `Info::tracks` beschreibt dabei ALLE Tonspuren, `codec`/`rate`/`channels`
//  die gewählte; `probeTrack` wählt eine andere als die erste.
Info probe(const QString& path);
Info probeTrack(const QString& path, int trackIndex);

//  Schreibt die Tonspur nach `targetPath` (atomar über QSaveFile).
//  `cancel` wird während des Kopierens regelmäßig gelesen (Regel 8).
//  `trackIndex` zählt die TONSPUREN in der Reihenfolge der Datei; 0 ist die
//  erste. Ein Index außerhalb wird zu 0 - lieber die erste Spur als keine.
Result extract(const QString& srcPath, const QString& targetPath,
               Info* info = nullptr,
               const std::atomic<bool>* cancel = nullptr,
               int trackIndex = 0);

//  ── Sprung im Player: die Tonspur AB einer Zeitstelle als roher Strom ──────
//  Der Player kann in einer Hülle nicht springen: `QAudioDecoder` kennt kein
//  Springen, und ein Stück aus der Mitte einer Matroska ist für sich nicht
//  lesbar. Dieses Gerät liefert deshalb die Tonspur ab der Zielstelle als
//  SELBSTRAHMENDEN Strom (AC-3, E-AC-3, MP3/MP2, AAC mit ADTS-Köpfen) - genau
//  das, was ein Dekoder mitten im Strom aufnehmen kann.
//
//  Gefunden wird die Stelle über das Inhaltsverzeichnis (`Cues`), sonst durch
//  Abschreiten der Cluster-Köpfe (gemessen an einer 1,5-GB-Datei: 451 ms kalt
//  für 3001 Cluster; mit `Cues` ist es ein Sprung).
//
//  `actualMs` meldet, wo der Strom TATSÄCHLICH beginnt - das ist der Anfang des
//  Clusters, also etwas vor der gesuchten Stelle. Den Rest überspringt der
//  Aufrufer wie bisher beim Dekodieren.
//  Rückgabe `nullptr`, wenn es nicht geht: Opus und Vorbis brauchen eine
//  Ogg-Hülle mit Kopfpaketen, das kann dieser Weg (noch) nicht.
QIODevice* openRawStream(const QString& srcPath, int trackIndex, qint64 startMs,
                         qint64* actualMs, QObject* parent = nullptr);

//  Freier Zielpfad NEBEN der Quelle. Die ENDUNG hängt am Codec: `.opus` für
//  Opus, `.ogg` für Vorbis - deshalb wird die Datei dafür kurz angelesen.
QString targetPathFor(const QString& srcPath, int trackIndex = 0);

} // namespace MkvAudio
