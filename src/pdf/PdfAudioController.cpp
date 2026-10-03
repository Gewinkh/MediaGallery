#include "pdf/PdfAudioController.h"
#include "core/PathUtils.h"
#include "core/MemoryUtils.h"   // mg::trimHeap - RSS-Rückgabe nach WAV-Cache-Eviction

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>
#include <QRunnable>
#include <QSizeF>
#include <QPair>
#include <algorithm>
#include <utility>
#include <cstring>
#include "core/ZCodec.h"
#include "pdf/PdfRawScan.h"

// Bewusst KEIN QPdfDocument: Seitengrößen kommen aus /MediaBox, alles andere aus dem rohen Bytestrom.
// Voraussetzung (von den Ziel-PDFs erfüllt): klassische Objekte, also per "N G obj" scanbar.
namespace {

using namespace mg::pdfraw;
using CancelFlag = std::shared_ptr<std::atomic<bool>>;
inline bool aborted(const CancelFlag& c) {
    return c && c->load(std::memory_order_relaxed);
}

//  Datei ABSCHNITTSWEISE lesen und zwischen den Abschnitten den Abbruch prüfen.
//  QFile::readAll() auf eine 363-MB-PDF dauerte gemessen 225 ms - in dieser Zeit
//  war der Scan nicht unterbrechbar und der Destruktor blockierte den GUI-Thread.
QByteArray readAllCancellable(QFile& f, const CancelFlag& cancel) {
    constexpr qint64 kChunk = 4 * 1024 * 1024;
    QByteArray out;
    const qint64 total = f.size();
    if (total > 0) out.reserve(int(qMin<qint64>(total, INT_MAX)));
    while (!f.atEnd()) {
        if (aborted(cancel)) return {};
        const QByteArray part = f.read(kChunk);
        if (part.isEmpty()) break;
        out.append(part);
    }
    return out;
}


int resolveSoundObj(const QByteArray& d, const QHash<int,qsizetype>& off, const QByteArray& annotDict) {
    const int direct = firstRefForKey(annotDict, "/Sound");
    if (direct > 0) return direct;
    int actionObj = firstRefForKey(annotDict, "/A");
    if (actionObj <= 0) { const QByteArray aa = nestedDictForKey(annotDict, "/AA"); if (!aa.isEmpty()) actionObj = firstAnyRef(aa); }
    QByteArray actionDict;
    if (actionObj > 0 && off.contains(actionObj)) actionDict = readDictAt(d, off.value(actionObj));
    if (actionDict.isEmpty()) actionDict = nestedDictForKey(annotDict, "/A");   // Inline-Aktion (selten)
    if (actionDict.isEmpty()) return -1;
    if (!actionDict.contains("/Sound")) return -1;                              // muss /S /Sound sein
    const int m = firstRefForKey(actionDict, "/Sound");
    return m > 0 ? m : -1;
}

bool soundStreamInfo(const QByteArray& d, const QHash<int,qsizetype>& off, int soundObj,
                     int& bits, int& ch, int& rate, bool& flate,
                     qsizetype& streamStart, qsizetype& streamLen) {
    if (!off.contains(soundObj)) return false;
    const qsizetype lt = d.indexOf("<<", off.value(soundObj)); if (lt < 0) return false;
    const QByteArray dict = readDictAt(d, lt); if (dict.isEmpty()) return false;
    const qsizetype dictEnd = lt + dict.size();

    bits  = qMax(1, (int)intDirect(dict, "/B", 8));      // /B Bits/Sample (Default 8)
    ch    = qMax(1, (int)intDirect(dict, "/C", 1));      // /C Kanaele      (Default 1)
    rate  = qMax(1, (int)intDirect(dict, "/R", 8000));   // /R Samplerate   (Default 8000)
    flate = dict.contains("/FlateDecode");

    const qsizetype st = d.indexOf("stream", dictEnd); if (st < 0) return false;
    qsizetype p = st + 6;
    if (p < d.size() && d[p] == '\r') ++p;
    if (p < d.size() && d[p] == '\n') ++p;
    streamStart = p;

    const long len = lengthValue(d, off, dict);
    if (len > 0) streamLen = len;
    else {
        const qsizetype es = d.indexOf("endstream", p); if (es < 0) return false;
        qsizetype e = es; if (e > p && d[e-1] == '\n') --e; if (e > p && d[e-1] == '\r') --e;
        streamLen = e - p;
    }
    return streamLen > 0;
}


QVector<PdfAudioClip> scanClips(const QByteArray& d, const CancelFlag& cancel) {
    QVector<PdfAudioClip> out;
    const QHash<int,qsizetype> off = buildObjectOffsets(d, cancel.get());
    if (aborted(cancel)) return {};

    // Seitenobjekte in AUTORITATIVER Lesereihenfolge über den Seitenbaum: reines Byte-Offset-Scannen wäre falsch -
    // Inkrement-Saves hinterlassen verwaiste /Type/Page-Objekte, und die Dateireihenfolge ist nicht die Seitenfolge.
    QVector<int> pageObjs; QHash<int,QSizeF> pageSize;
    {
        const int rootPages = findRootPagesObj(d, off);
        if (rootPages > 0) { QSet<int> vis; flattenPages(d, off, rootPages, pageObjs, vis, 0); }
        if (pageObjs.isEmpty()) {                       // Fallback: Datei-Reihenfolge
            QVector<QPair<qsizetype,int>> hits;
            for (auto it = off.constBegin(); it != off.constEnd(); ++it) {
                if (aborted(cancel)) return {};
                const qsizetype lt = d.indexOf("<<", it.value()); if (lt < 0) continue;
                const QByteArray dict = readDictAt(d, lt);
                if (!dict.isEmpty() && isPageObject(dict)) hits.append({ lt, it.key() });
            }
            std::sort(hits.begin(), hits.end());
            for (const auto& h : hits) pageObjs.append(h.second);
        }
        for (int pn : pageObjs) {
            if (aborted(cancel)) return {};
            QSizeF sz(595, 842);
            const qsizetype lt = off.contains(pn) ? d.indexOf("<<", off.value(pn)) : -1;
            if (lt >= 0) sz = mediaBoxSize(readDictAt(d, lt));
            pageSize.insert(pn, sz);
        }
    }

    QSet<int> seen;   // Dedup ueber die Sound-Stream-Objektnummer
    const auto addAnnot = [&](const QByteArray& ad) {
        const int sObj = resolveSoundObj(d, off, ad); if (sObj <= 0 || seen.contains(sObj)) return;
        int bits, ch, rate; bool flate; qsizetype ss = 0, sl = 0;
        if (!soundStreamInfo(d, off, sObj, bits, ch, rate, flate, ss, sl)) return;

        int pidx = 0; QSizeF psz(595, 842);
        const int pRef = firstRefForKey(ad, "/P");
        if (pRef > 0) { const int ix = pageObjs.indexOf(pRef); if (ix >= 0) { pidx = ix; psz = pageSize.value(pRef, psz); } }

        PdfAudioClip c;
        c.page = pidx;
        c.rect = parseNormalisedRect(bracketValue(ad, "/Rect"), psz);
        c.bits = bits; c.channels = ch; c.rate = rate; c.flate = flate;
        c.streamStart = ss; c.streamLen = sl;
        QString lab = stringValue(ad, "/T"); if (lab.isEmpty()) lab = stringValue(ad, "/Contents");
        c.label = lab;
        out.append(c); seen.insert(sObj);
    };

    if (aborted(cancel)) return {};
    { QVector<qsizetype> hits = findAll(d, "/Subtype/Widget"); hits += findAll(d, "/Subtype /Widget");
      for (qsizetype p : hits) { if (aborted(cancel)) return {};
                                 const QByteArray ad = enclosingObjDict(d, p); if (!ad.isEmpty()) addAnnot(ad); } }
    if (aborted(cancel)) return {};
    { QVector<qsizetype> hits = findAll(d, "/Subtype/Sound"); hits += findAll(d, "/Subtype /Sound");
      for (qsizetype p : hits) { if (aborted(cancel)) return {};
                                 const QByteArray ad = enclosingObjDict(d, p); if (!ad.isEmpty()) addAnnot(ad); } }

    std::sort(out.begin(), out.end(), [](const PdfAudioClip& a, const PdfAudioClip& b) {
        if (a.page != b.page) return a.page < b.page;
        if (a.rect.y() != b.rect.y()) return a.rect.y() < b.rect.y();
        return a.rect.x() < b.rect.x();
    });
    for (int i = 0; i < out.size(); ++i) out[i].id = i;
    return out;
}

QByteArray zlibInflate(const QByteArray& in) {
    bool ok = false;
    return mg::zcodec::inflate(in, mg::zcodec::Wrap::Auto, 0,
                               /*tolerant*/ true, &ok);
}

void byteswap16(QByteArray& b) {
    char* p = b.data(); const qsizetype n = b.size() - (b.size() & 1);
    for (qsizetype i = 0; i + 1 < n; i += 2) std::swap(p[i], p[i+1]);
}

QByteArray wavHeader(qint64 dataLen, int channels, int rate, int bits) {
    QByteArray h;
    const auto u32 = [&](quint32 v){ h.append(char(v & 0xff)); h.append(char((v>>8) & 0xff)); h.append(char((v>>16) & 0xff)); h.append(char((v>>24) & 0xff)); };
    const auto u16 = [&](quint16 v){ h.append(char(v & 0xff)); h.append(char((v>>8) & 0xff)); };
    const quint32 byteRate   = quint32(rate) * quint32(channels) * quint32(bits / 8);
    const quint16 blockAlign = quint16(channels * (bits / 8));
    h.append("RIFF"); u32(quint32(36 + dataLen)); h.append("WAVE");
    h.append("fmt "); u32(16); u16(1); u16(quint16(channels)); u32(quint32(rate)); u32(byteRate); u16(blockAlign); u16(quint16(bits));
    h.append("data"); u32(quint32(dataLen));
    return h;
}

QByteArray readSlice(const QString& path, qsizetype start, qsizetype len) {
    if (len <= 0) return {};
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {};
    if (!f.seek(start)) { f.close(); return {}; }
    const QByteArray b = f.read(len); f.close(); return b;
}

QString writeTempWav(const QString& pdfPath, int id, int gen, const QByteArray& bytes) {
    const QString dir  = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString base = QFileInfo(pdfPath).completeBaseName();
    const QString tag  = QString::number(qHash(pdfPath) & 0xffff, 16);
    // Generationszahl im Namen: JEDE Dokument-Session schreibt in frische Dateien. Sonst kollidierte die Extraktion
    // beim erneuten Öffnen mit einer noch gesperrten WAV der vorigen Session - leere URL, jede zweite Datei stumm.
    const QString path = dir + QString("/mgaudio_%1_%2_g%3_%4.wav")
                                   .arg(base, tag, QString::number(gen), QString::number(id));
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(bytes); f.close(); return path;
}

class PdfAudioScanTask : public QRunnable {
public:
    PdfAudioScanTask(PdfAudioController* o, QString path, int gen, CancelFlag cancel)
        : m_owner(o), m_path(std::move(path)), m_gen(gen), m_cancel(std::move(cancel)) { setAutoDelete(true); }
    void run() override {
        QVector<PdfAudioClip> clips;
        QFile f(m_path);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray d = readAllCancellable(f, m_cancel);
            f.close();
            if (!d.isEmpty()) clips = scanClips(d, m_cancel);
        }
        //  Abgebrochen ⇒ Ergebnis verwerfen UND den Owner nicht mehr anfassen:
        //  Er wird gerade zerstört (Kachel geschlossen) oder hat auf ein anderes
        //  Dokument umgeschaltet.
        if (aborted(m_cancel)) return;
        PdfAudioController* owner = m_owner; const QString path = m_path; const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, path, clips, gen]() { owner->applyScan(path, clips, gen); }, Qt::QueuedConnection);
    }
private:
    PdfAudioController* m_owner; QString m_path; int m_gen; CancelFlag m_cancel;
};

class PdfAudioExtractTask : public QRunnable {
public:
    PdfAudioExtractTask(PdfAudioController* o, QString path, PdfAudioClip c, int gen,
                        CancelFlag cancel)
        : m_owner(o), m_path(std::move(path)), m_clip(c), m_gen(gen),
          m_cancel(std::move(cancel)) { setAutoDelete(true); }
    void run() override {
        QString wav; int durMs = 0; qint64 bytes = 0;
        if (aborted(m_cancel)) return;
        const QByteArray comp = readSlice(m_path, m_clip.streamStart, m_clip.streamLen);
        if (!comp.isEmpty() && !aborted(m_cancel)) {
            QByteArray pcm = m_clip.flate ? zlibInflate(comp) : comp;
            if (!pcm.isEmpty()) {
                if (m_clip.bits == 16) byteswap16(pcm);   // PDF /Sound = Big-Endian
                const int bps = m_clip.channels * (m_clip.bits / 8);
                if (bps > 0) durMs = int(double(pcm.size()) / double(qint64(m_clip.rate) * bps) * 1000.0 + 0.5);
                QByteArray file = wavHeader(pcm.size(), m_clip.channels, m_clip.rate, m_clip.bits);
                file.append(pcm);
                wav = writeTempWav(m_path, m_clip.id, m_gen, file);
                bytes = file.size();
            }
        }
        if (aborted(m_cancel)) {
            //  Abgebrochen NACH dem Schreiben: die WAV landet nie im Cache und
            //  wuerde von dessen Aufraeumen (Destruktor/evictCache) nie erfasst
            //  - sie bliebe bis zum naechsten Neustart im Temp-Verzeichnis liegen.
            if (!wav.isEmpty()) QFile::remove(wav);
            return;
        }
        PdfAudioController* owner = m_owner; const int id = m_clip.id; const int gen = m_gen;
        const QString w = wav; const int dm = durMs; const qint64 by = bytes;
        QMetaObject::invokeMethod(owner, [owner, id, w, dm, by, gen]() { owner->applyClip(id, w, dm, by, gen); }, Qt::QueuedConnection);
    }
private:
    PdfAudioController* m_owner; QString m_path; PdfAudioClip m_clip; int m_gen;
    CancelFlag m_cancel;
};

} // namespace

PdfAudioController::PdfAudioController(QObject* parent)
    : QObject(parent), m_cancel(std::make_shared<std::atomic<bool>>(false)) {
    m_pool.setMaxThreadCount(1);   // Scan + Extraktion serialisieren (RAM/Disk-schonend)
}

//  Laufende Tasks abbrechen und ein FRISCHES Flag anlegen: danach gestartete
//  Tasks dürfen nicht mit dem alten Abbruch mitgerissen werden.
void PdfAudioController::cancelRunningTasks() {
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
    m_cancel = std::make_shared<std::atomic<bool>>(false);
}

PdfAudioController::~PdfAudioController() {
    //  ERST das Abbruch-Flag setzen, DANN warten: sonst blockiert der
    //  GUI-Thread hier, bis ein laufender Scan die komplette PDF gelesen und
    //  mehrfach durchsucht hat (gemessen 745 ms bei 363 MB).
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
    m_pool.clear();
    m_pool.waitForDone();
    for (const WavEntry& e : std::as_const(m_wavCache)) QFile::remove(e.path);
}

void PdfAudioController::prepare(const QString& filePathOrUrl) {
    const QString path = mg::toLocalPath(filePathOrUrl);
    if (path == m_path && (m_scanInFlight || m_ready)) return;   // idempotent

    releaseDocument();                                           // gen++, leert Zustand, ready=false
    m_path = path;
    if (path.isEmpty() || !QFileInfo::exists(path)) { m_ready = true; emit readyChanged(); return; }

    m_scanInFlight = true;
    m_pool.start(new PdfAudioScanTask(this, path, m_gen, m_cancel));
}

QVariantList PdfAudioController::clips() const {
    QVariantList out; out.reserve(m_clips.size());
    for (const PdfAudioClip& c : m_clips) {
        QVariantMap m;
        m.insert("id",    c.id);
        m.insert("page",  c.page);
        m.insert("x",     c.rect.x());
        m.insert("y",     c.rect.y());
        m.insert("w",     c.rect.width());
        m.insert("h",     c.rect.height());
        m.insert("label", c.label);
        out.append(m);
    }
    return out;
}

void PdfAudioController::requestClip(int id) {
    if (id < 0 || id >= m_clips.size()) return;

    if (m_wavCache.contains(id)) {
        m_wavOrder.removeAll(id); m_wavOrder.append(id);
        const WavEntry e = m_wavCache.value(id); const int gen = m_gen;
        QMetaObject::invokeMethod(this, [this, id, e, gen]() {
            if (gen != m_gen) return;
            emit clipReady(id, QUrl::fromLocalFile(e.path).toString(), e.durationMs);
        }, Qt::QueuedConnection);
        return;
    }
    if (m_clipInFlight.contains(id)) return;
    m_clipInFlight.insert(id);
    m_pool.start(new PdfAudioExtractTask(this, m_path, m_clips.at(id), m_gen, m_cancel));
}

void PdfAudioController::releaseDocument() {
    ++m_gen;                                  // laufende Tasks werden verworfen
    // Die Generationszahl verwarf bisher nur das ERGEBNIS - der Task lief weiter und belegte den 1-Thread-Pool,
    // beim schnellen Blättern stauten sich komplette Datei-Scans. Jetzt bricht er auch wirklich ab.
    cancelRunningTasks();
    m_path.clear();
    m_clips.clear();
    const bool hadWavs = !m_wavCache.isEmpty();
    for (const WavEntry& e : std::as_const(m_wavCache)) QFile::remove(e.path);
    m_wavCache.clear(); m_wavOrder.clear(); m_wavBytes = 0; m_clipInFlight.clear();
    // Kompletter WAV-Cache + Clip-Metadaten freigegeben (große Freigabe beim
    // Dokumentwechsel) -> Heap aktiv ans OS zurückgeben.
    if (hadWavs)
        mg::trimHeap();
    const bool was = m_ready;
    m_ready = false; m_scanInFlight = false;
    if (was) emit readyChanged();
}

void PdfAudioController::applyScan(const QString& path, const QVector<PdfAudioClip>& clips, int gen) {
    if (gen != m_gen || path != m_path) return;   // veraltet / anderes Dokument
    m_clips = clips;
    m_scanInFlight = false;
    m_ready = true;
    emit readyChanged();
}

void PdfAudioController::applyClip(int id, const QString& wavPath, int durationMs, qint64 bytes, int gen) {
    if (gen != m_gen) { if (!wavPath.isEmpty()) QFile::remove(wavPath); return; }
    m_clipInFlight.remove(id);
    if (wavPath.isEmpty()) { emit clipReady(id, QString(), 0); return; }   // Extraktion fehlgeschlagen

    m_wavCache.insert(id, WavEntry{ wavPath, durationMs, bytes });
    m_wavOrder.removeAll(id); m_wavOrder.append(id);
    m_wavBytes += bytes;
    evictCache();
    emit clipReady(id, QUrl::fromLocalFile(wavPath).toString(), durationMs);
}

void PdfAudioController::evictCache() {
    bool evicted = false;
    while (m_wavBytes > kMaxWavBytes && m_wavOrder.size() > 1) {
        const int victim = m_wavOrder.takeFirst();
        auto it = m_wavCache.find(victim);
        if (it != m_wavCache.end()) { m_wavBytes -= it->bytes; QFile::remove(it->path); m_wavCache.erase(it); evicted = true; }
    }
    // Nur bei TATSÄCHLICHER Eviction: die Inflate-/WAV-Puffer der Extraktion
    // liegen im MB-Bereich - freigegebenen Heap aktiv ans OS zurückgeben.
    if (evicted)
        mg::trimHeap();
}

QString PdfAudioController::tempPathFor(int id) const {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QString("/mgaudio_%1.wav").arg(id);
}
