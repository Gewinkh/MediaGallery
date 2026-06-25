#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// PdfAudioController.h   (QML-Singleton „PdfAudio")
// ══════════════════════════════════════════════════════════════════════════════
//
// Extrahiert in PDFs EINGEBETTETE Audios und stellt sie der QML-Seite (PdfSurface)
// fuer eine seitenbezogene Audio-Leiste mit Mini-Player bereit.
//
// Qt6's QPdfDocument hat KEINE oeffentliche API fuer Annotationen, Aktionen oder
// eingebettete Streams. Wie der vorhandene PdfMediaHandler umgehen wir das durch
// Roh-Parsen des PDF-Bytestroms. Audio-Buttons sind in der Praxis (z. B. den
// IRODORI-Lehrbuechern) so verdrahtet:
//
//   Widget-Button (/Subtype /Widget, /FT /Btn, /P=Seite, /Rect)
//        └─ /A  oder /AA<</D N 0 R …>>            (Maus-Down-Aktion)
//             └─ Aktionsobjekt <</S /Sound /Sound M 0 R>>
//                  └─ Sound-Stream M: <</Type /Sound /B 16 /C 2 /R 44100
//                                       /E /Signed /Filter /FlateDecode>>stream …
//
// Der Sound-Stream ist ROHES PCM ohne Container. Zum Abspielen ueber QMediaPlayer
// wird er erst beim Bedarf (Lazy):
//   1. per zlib inflated (FlateDecode),
//   2. von BIG-ENDIAN auf Little-Endian umgestellt (PDF-/Sound-Samplereihenfolge),
//   3. mit einem RIFF/WAVE-Header (aus /B /C /R) versehen und als Temp-WAV
//      geschrieben.
//
// Zusaetzlich werden klassische /Subtype /Sound-Annotationen unterstuetzt.
//
// RAM/Disk-Strategie (Prio 1):
//   • prepare()  scannt nur METADATEN (Seite/Rechteck/Stream-Offset/Format) — es
//                wird NICHTS inflated → Oeffnen bleibt billig (auch bei 60+ Clips).
//   • requestClip() extrahiert genau EINEN Clip async (eigener QThreadPool) und
//                cached das Ergebnis in einem LRU mit Byte-Deckel.
//   • Generationszahl verwirft Ergebnisse veralteter Dokumente (schnelles
//                Vor/Zurueck ist sicher).
// ══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QRectF>
#include <QVector>
#include <QHash>
#include <QList>
#include <QSet>
#include <QVariantList>
#include <QThreadPool>

// ─────────────────────────────────────────────────────
// PdfAudioClip — ein extrahierbarer Audio-Treffer (Metadaten, kein PCM)
//   Auf Namespace-Ebene, damit die Worker-Tasks (in der .cpp) ihn bilden koennen.
// ─────────────────────────────────────────────────────
struct PdfAudioClip {
    int     id    = 0;            // stabile ID innerhalb des aktuellen Dokuments
    int     page  = 0;           // 0-basiert
    QRectF  rect;                // normalisiert [0..1], y=0 oben
    QString label;               // /T (Buttonname) oder /Contents

    // Sound-Stream-Parameter (PDF /Sound-Dictionary)
    int       bits     = 16;     // /B  Bits pro Sample
    int       channels = 1;      // /C  Kanaele
    int       rate     = 8000;   // /R  Samplerate in Hz
    bool      flate    = true;   // /Filter /FlateDecode?
    qsizetype streamStart = 0;   // Byte-Offset der Stream-Daten in der PDF-Datei
    qsizetype streamLen   = 0;   // Laenge der (komprimierten) Stream-Daten
};

// ─────────────────────────────────────────────────────
// PdfAudioController
// ─────────────────────────────────────────────────────
class PdfAudioController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready            READ ready            NOTIFY readyChanged)
    Q_PROPERTY(bool documentHasAudio READ documentHasAudio NOTIFY readyChanged)

public:
    explicit PdfAudioController(QObject* parent = nullptr);
    ~PdfAudioController() override;

    bool ready()            const { return m_ready; }
    bool documentHasAudio() const { return !m_clips.isEmpty(); }

    // Async-Metadaten-Scan des PDFs. Idempotent fuer denselben Pfad (laufender oder
    // bereits abgeschlossener Scan wird nicht wiederholt). Lazy: kein Inflate.
    Q_INVOKABLE void prepare(const QString& filePathOrUrl);

    // Alle Clips des aktuellen Dokuments fuer QML:
    //   [{ id, page, x, y, w, h, label }]  — sortiert nach (Seite, y, x).
    Q_INVOKABLE QVariantList clips() const;

    // Lazy-Extraktion EINES Clips (inflate → byteswap → WAV-Temp). Idempotent:
    // bei Cache-Treffer kommt clipReady sofort (queued). Sonst async im Pool.
    Q_INVOKABLE void requestClip(int id);

    // Dokument + alle extrahierten Temp-WAVs freigeben (RAM/Disk-Prio 1).
    Q_INVOKABLE void releaseDocument();

    // ── Vom Worker-Thread via QueuedConnection aufgerufen (GUI-Thread) ──────────
    void applyScan(const QString& path, const QVector<PdfAudioClip>& clips, int gen);
    void applyClip(int id, const QString& wavPath, int durationMs, qint64 bytes, int gen);

signals:
    void readyChanged();
    // url = abspielbare file://-URL der extrahierten WAV; durationMs = Laenge.
    void clipReady(int id, const QString& url, int durationMs);

private:
    void   evictCache();
    QString tempPathFor(int id) const;

    // ── Aktueller Dokumentzustand ─────────────────────────────────────────────
    QString               m_path;       // lokaler Pfad des aktiven PDFs
    QVector<PdfAudioClip> m_clips;      // Metadaten aller Audio-Clips
    bool                  m_ready = false;
    int                   m_gen   = 0;  // Generationszahl (verwirft veraltete Tasks)
    bool                  m_scanInFlight = false;

    // ── Extraktions-Cache (LRU, GUI-Thread → keine Synchronisation noetig) ─────
    struct WavEntry { QString path; int durationMs = 0; qint64 bytes = 0; };
    QHash<int, WavEntry>  m_wavCache;   // id -> extrahierte WAV
    QList<int>            m_wavOrder;    // LRU-Reihenfolge (alt -> neu)
    qint64                m_wavBytes = 0;
    QSet<int>             m_clipInFlight;

    // Eigener Pool (nicht der globale): serialisiert Scan + Extraktion (RAM-schonend
    // und vermeidet Disk-/CPU-Stoss beim Seitenwechsel).
    QThreadPool           m_pool;

    static constexpr qint64 kMaxWavBytes = 96LL * 1024 * 1024;   // Cache-Deckel
};
