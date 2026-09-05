#pragma once
// QPdfDocument hat keine API für Annotationen oder eingebettete Ströme - deshalb Roh-Parsen. Audio hängt meist
// an einem Widget-Button über /A auf ein Sound-Objekt; der Strom ist rohes PCM, wird lazy inflated, gedreht
// und als WAV geschrieben. `prepare()` scannt nur Metadaten, nichts wird dabei inflated.

#include <QObject>
#include <QString>
#include <QRectF>
#include <QVector>
#include <QHash>
#include <QList>
#include <QSet>
#include <QVariantList>
#include <QThreadPool>
#include <atomic>
#include <memory>

// PdfAudioClip - ein extrahierbarer Audio-Treffer (Metadaten, kein PCM)
//   Auf Namespace-Ebene, damit die Worker-Tasks (in der .cpp) ihn bilden koennen.
struct PdfAudioClip {
    int     id    = 0;            // stabile ID innerhalb des aktuellen Dokuments
    int     page  = 0;           // 0-basiert
    QRectF  rect;                // normalisiert [0..1], y=0 oben
    QString label;               // /T (Buttonname) oder /Contents

    int       bits     = 16;     // /B  Bits pro Sample
    int       channels = 1;      // /C  Kanaele
    int       rate     = 8000;   // /R  Samplerate in Hz
    bool      flate    = true;   // /Filter /FlateDecode?
    qsizetype streamStart = 0;   // Byte-Offset der Stream-Daten in der PDF-Datei
    qsizetype streamLen   = 0;   // Laenge der (komprimierten) Stream-Daten
};

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

    Q_INVOKABLE QVariantList clips() const;

    // Lazy-Extraktion EINES Clips (inflate -> byteswap -> WAV-Temp). Idempotent:
    // bei Cache-Treffer kommt clipReady sofort (queued). Sonst async im Pool.
    Q_INVOKABLE void requestClip(int id);

    Q_INVOKABLE void releaseDocument();

    void applyScan(const QString& path, const QVector<PdfAudioClip>& clips, int gen);
    void applyClip(int id, const QString& wavPath, int durationMs, qint64 bytes, int gen);

signals:
    void readyChanged();
    void clipReady(int id, const QString& url, int durationMs);

private:
    // Kooperativer Abbruch: ohne das Flag wartete der Destruktor im GUI-Thread, bis ein laufender Scan fertig war -
    // bei einer 363-MB-PDF gemessen 745 ms Freeze allein fürs Schließen der Kachel.
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;
    void   cancelRunningTasks();          // Flag setzen + frisches Flag anlegen

    void   evictCache();
    QString tempPathFor(int id) const;

    QString               m_path;       // lokaler Pfad des aktiven PDFs
    QVector<PdfAudioClip> m_clips;      // Metadaten aller Audio-Clips
    bool                  m_ready = false;
    int                   m_gen   = 0;  // Generationszahl (verwirft veraltete Tasks)
    bool                  m_scanInFlight = false;

    struct WavEntry { QString path; int durationMs = 0; qint64 bytes = 0; };
    QHash<int, WavEntry>  m_wavCache;   // id -> extrahierte WAV
    QList<int>            m_wavOrder;    // LRU-Reihenfolge (alt -> neu)
    qint64                m_wavBytes = 0;
    QSet<int>             m_clipInFlight;

    // Eigener Pool (nicht der globale): serialisiert Scan + Extraktion (RAM-schonend
    // und vermeidet Disk-/CPU-Stoss beim Seitenwechsel).
    QThreadPool           m_pool;
    //  Wird von JEDEM laufenden Task geteilt; cancelRunningTasks() setzt es und
    //  legt ein frisches an, damit neu gestartete Tasks nicht mit abgebrochen
    //  werden. Der Destruktor setzt es VOR waitForDone().
    CancelFlag            m_cancel;

    static constexpr qint64 kMaxWavBytes = 96LL * 1024 * 1024;   // Cache-Deckel
};
