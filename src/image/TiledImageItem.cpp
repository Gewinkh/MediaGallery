#include "image/TiledImageItem.h"

#include "core/PathUtils.h"
#include "simd/Bilinear.h"

#include <QCoreApplication>
#include <QImageReader>
#include <QPointer>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGImageNode>
#include <QSGOpacityNode>
#include <QSGRendererInterface>
#include <QSGTransformNode>
#include <QThread>
#include <QThreadPool>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <utility>

namespace mg::image {

std::vector<TileRect> tileGrid(QSize image, int tile, int gutter) {
    std::vector<TileRect> out;
    if (image.isEmpty() || tile <= 0) return out;
    const QRect all(QPoint(0, 0), image);
    for (int y = 0; y < image.height(); y += tile)
        for (int x = 0; x < image.width(); x += tile) {
            const QRect inner = QRect(x, y, tile, tile) & all;
            out.push_back({inner, inner.adjusted(-gutter, -gutter, gutter, gutter) & all});
        }
    return out;
}

QSize stageSize(QSize full, int longSide) {
    if (full.isEmpty() || longSide <= 0 || longSide >= std::max(full.width(), full.height())) return full;
    const double f = double(longSide) / std::max(full.width(), full.height());
    return QSize(std::max(1, qRound(full.width() * f)), std::max(1, qRound(full.height() * f)));
}

namespace {

QImage::Format displayFormat(const QImage& stage) {
    return stage.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
}

// Fuellt `out` (Mass und Format gelten, displayFormat) mit dem Ausschnitt ab `at`; `out` darf wiederverwendet sein.
void stretchInto(QImage& out, const QImage& stage, QSize device, QPoint at) {
    if (out.isNull() || stage.isNull() || device.isEmpty()) return;
    const QImage src = stage.format() == out.format() ? stage : stage.convertToFormat(out.format());
    mg::simd::skaliereBilinear(reinterpret_cast<const quint32*>(src.constBits()), src.width(), src.height(),
                               src.bytesPerLine(), reinterpret_cast<quint32*>(out.bits()), out.width(), out.height(),
                               out.bytesPerLine(), double(device.width()) / src.width(),
                               double(device.height()) / src.height(), at.x(), at.y());
}

} // namespace

QImage renderDisplayTile(const QImage& stage, QSize device, QRect area) {
    QImage out(area.size(), displayFormat(stage));
    stretchInto(out, stage, device, area.topLeft());
    return out;
}

namespace {

// Ein neuer Stufen-Satz wird unsichtbar hochgeladen und erst komplett getauscht; bei Deckkraft 0 liesse der
// Renderer den Teilbaum aus und luede nie hoch (Grenze 0,001). 0,002 aendert ein 8-Bit-Pixel hoechstens um 1.
constexpr qreal kHidden = 0.002;
// Je Bild hoechstens so viele Bytes hochladen: eine Kachel 1024 px = 4 MB, gemessen 42 ms fuer 96 MB auf OpenGL.
constexpr qsizetype kUploadBytes = 4 << 20;

QThreadPool* pool() {
    static QPointer<QThreadPool> p;
    if (!p) {
        p = new QThreadPool(QCoreApplication::instance());
        p->setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 2, 4));
    }
    return p;
}

quint64 tileKey(int tx, int ty) { return (quint64(quint32(ty)) << 32) | quint32(tx); }

QRect tileArea(quint64 key, QSize device) {
    const int tx = int(key & 0xffffffffu), ty = int(key >> 32);
    const int t = TiledImageItem::kCpuTile;
    return QRect(tx * t, ty * t, t, t) & QRect(QPoint(0, 0), device);
}

class Root : public QSGTransformNode {
public:
    struct Stage {
        QSGOpacityNode* fade = nullptr;
        QSGTransformNode* xf = nullptr;
        quint64 gen = 0;
        quint64 srcGen = 0;
        QSize size;
        size_t next = 0;
        bool done = false;
        bool shown = false;
        bool asked = false;
        bool reduced = false;
        bool wasExact = false;                      // einmal 1:1 gezeigt: beim Hineinzoomen als `rest` behalten
        int filter = -1;                            // 1 = Nearest (1:1), 0 = Linear, -1 = neu
    };
    // `rest`: die eingepasste, 1:1 gezeigte Stufe desselben Bildes, nachdem eine groessere sie abgeloest hat. Passt
    // die Anzeige wieder auf ihr Mass, steht sie statt der gefilterten grossen; ausgehaengt, solange nicht.
    Stage cur, old, rest;
    QSGImageNode* base = nullptr;
    quint64 baseGen = 0;
    // Software: die Sicht als ein fertig gestrecktes Bild, solange Kacheln fehlen (beim Zoomen jedes Bild neu).
    QSGImageNode* view = nullptr;
    quint64 viewGen = 0;
    QSize viewDevice;
    QRect viewArea;
    QImage viewImage;                               // von der Textur geteilt; nach deren Ende ungeteilt wiederbeschrieben
    std::unordered_map<quint64, QSGImageNode*> tiles;
    quint64 epoch = 0;

    ~Root() override {
        if (base && !base->parent()) delete base;
        if (view && !view->parent()) delete view;
        for (Stage* s : {&cur, &old, &rest})
            if (s->fade && !s->fade->parent()) delete s->fade;
    }
    void dropStage(Stage& s) {
        delete s.fade;
        s = Stage{};
    }
    void hang(Stage& s, bool on) {
        if (!s.fade || bool(s.fade->parent()) == on) return;
        if (on) appendChildNode(s.fade);
        else removeChildNode(s.fade);
    }
    bool hung(const Stage& s) const { return s.fade && s.fade->parent(); }
};

// Eine fremde Stufe unverzerrt eingepasst; weicht das nur um Bruchteile eines Pixels ab (QML haelt beim Wechsel
// das Mass des alten Bildes), ganz gefuellt, sonst verschoebe das Neuabtasten das stehende Bild.
QRectF fitForeign(qreal w, qreal h, QSize src) {
    const qreal k = std::min(w / src.width(), h / src.height());
    const qreal fw = src.width() * k, fh = src.height() * k;
    if (w - fw < 1.0 && h - fh < 1.0) return QRectF(0, 0, w, h);
    return QRectF((w - fw) / 2, (h - fh) / 2, fw, fh);
}

// Eine Stufe in Bildschirmgroesse (bis auf Rundung) wird Pixel fuer Pixel gezeigt: bilinear neu abgetastet waere sie
// sichtbar weicher, und die Software-Zeichnung kopiert dann nur.
bool nearSize(QSize a, QSize b) {
    return std::abs(a.width() - b.width()) <= 1 && std::abs(a.height() - b.height()) <= 1;
}

// Nur der Filter wechselt, die Mipmap-Einstellung nie: eine schon ohne Mipmaps hochgeladene Textur bekaeme keine nach,
// verkleinert waere sie dann ungefiltert ("frittiert"). Bei Massstab 1 waehlt die GPU ohnehin die volle Ebene.
void setFilter(QSGNode* parent, bool nearest) {
    for (QSGNode* n = parent ? parent->firstChild() : nullptr; n; n = n->nextSibling())
        static_cast<QSGImageNode*>(n)->setFiltering(nearest ? QSGTexture::Nearest : QSGTexture::Linear);
}

} // namespace

struct TiledImageItem::Shared {
    std::mutex lock;
    TiledImageItem* owner = nullptr;
    std::atomic<quint64> decodeGen{0};
    std::atomic<quint64> cpuEpoch{0};
    std::atomic<quint64> levelGen{0};

    template <class F>
    void post(F f) {
        std::lock_guard<std::mutex> g(lock);
        if (!owner) return;
        TiledImageItem* o = owner;
        QMetaObject::invokeMethod(o, [o, f = std::move(f)]() mutable { f(o); }, Qt::QueuedConnection);
    }
};

struct TiledImageItem::Decoded {
    quint64 gen = 0;
    quint64 srcGen = 0;
    QSize size;
    QSize full;
    QImage stage;
    std::vector<TileRect> rects;
    std::vector<QImage> tiles;
    bool failed = false;
};

namespace {

// Kacheln als Sichten auf den Speicher der Stufe, ohne Kopie; die Stufe lebt, bis die letzte Sicht hochgeladen ist.
std::vector<QImage> tileViews(const QImage& stage, const std::vector<TileRect>& rects) {
    using Holder = std::shared_ptr<const QImage>;
    const Holder keep = std::make_shared<const QImage>(stage);
    const qsizetype bpl = keep->bytesPerLine();
    std::vector<QImage> out;
    out.reserve(rects.size());
    for (const TileRect& r : rects)
        out.emplace_back(keep->constBits() + r.outer.y() * bpl + r.outer.x() * 4, r.outer.width(), r.outer.height(),
                         bpl, keep->format(), [](void* h) { delete static_cast<Holder*>(h); }, new Holder(keep));
    return out;
}

} // namespace

class DecodeTask : public QRunnable {
public:
    DecodeTask(std::shared_ptr<TiledImageItem::Shared> sh, quint64 gen, quint64 srcGen, QString path, int longSide,
               bool software)
        : m_sh(std::move(sh)), m_gen(gen), m_srcGen(srcGen), m_path(std::move(path)), m_long(longSide),
          m_software(software) {
        setAutoDelete(true);
    }
    void run() override;

private:
    std::shared_ptr<TiledImageItem::Shared> m_sh;
    quint64 m_gen;
    quint64 m_srcGen;
    QString m_path;
    int m_long;
    bool m_software;
};

class TileTask : public QRunnable {
public:
    TileTask(std::shared_ptr<TiledImageItem::Shared> sh, quint64 epoch, quint64 key, QImage stage, QSize device)
        : m_sh(std::move(sh)), m_epoch(epoch), m_key(key), m_stage(std::move(stage)), m_device(device) {
        setAutoDelete(true);
    }
    void run() override {
        if (m_sh->cpuEpoch.load() != m_epoch) return;
        QImage t = renderDisplayTile(m_stage, m_device, tileArea(m_key, m_device));
        m_stage = QImage();
        m_sh->post([e = m_epoch, k = m_key, t = std::move(t)](TiledImageItem* o) mutable {
            o->adoptTile(e, k, std::move(t));
        });
    }

private:
    std::shared_ptr<TiledImageItem::Shared> m_sh;
    quint64 m_epoch;
    quint64 m_key;
    QImage m_stage;
    QSize m_device;
};

class LevelTask : public QRunnable {
public:
    // `from` ist die Stufe oder eine schon verkleinerte Ebene; `steps` Halbierungen davon ergeben Ebene `shift`.
    LevelTask(std::shared_ptr<TiledImageItem::Shared> sh, quint64 gen, int shift, int steps, QImage from)
        : m_sh(std::move(sh)), m_gen(gen), m_shift(shift), m_steps(steps), m_stage(std::move(from)) {
        setAutoDelete(true);
    }
    void run() override {
        if (m_sh->levelGen.load() != m_gen) return;
        const QSize size(std::max(1, m_stage.width() >> m_steps), std::max(1, m_stage.height() >> m_steps));
        QImage level = m_stage.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_stage = QImage();
        m_sh->post([g = m_gen, n = m_shift, level = std::move(level)](TiledImageItem* o) mutable {
            o->adoptLevel(g, n, std::move(level));
        });
    }

private:
    std::shared_ptr<TiledImageItem::Shared> m_sh;
    quint64 m_gen;
    int m_shift;
    int m_steps;
    QImage m_stage;
};

void DecodeTask::run() {
    if (m_sh->decodeGen.load() != m_gen) return;
    auto d = std::make_shared<TiledImageItem::Decoded>();
    d->gen = m_gen;
    d->srcGen = m_srcGen;
    QImageReader reader(m_path);
    reader.setAutoTransform(false);
    const QSize full = reader.size();
    d->full = full;
    const QSize want = stageSize(full, m_long);
    bool shrink = false;
    if (want.isValid() && want != full) {
        // JPEG verkleinert schon beim Dekodieren um 2, 4 oder 8, hier nur bis zur doppelten Zielgroesse; den Rest
        // ein Flaechenmittel. Qt dekodierte knapp ueber das Ziel und skalierte dann ein zweites Mal: sichtbar weicher.
        // Abgerundet, weil Qt den Teiler per Ganzzahldivision waehlt; aufgerundet nahm es den naechstkleineren.
        if (reader.format() == "jpeg") {
            int k = 1;
            while (k < 8 && full.width() / (2 * k) >= 2 * want.width() && full.height() / (2 * k) >= 2 * want.height())
                k *= 2;
            if (k > 1) reader.setScaledSize(QSize(full.width() / k, full.height() / k));
            shrink = true;
        } else {
            reader.setScaledSize(want);
        }
    }
    QImage img = reader.read();
    // Beim schnellen Zoom ist die Stufe hier oft schon ueberholt: Skalieren und Umwandeln waeren verworfen worden.
    if (m_sh->decodeGen.load() != m_gen) return;
    if (!img.isNull() && shrink && img.size() != want)
        img = img.scaled(want, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (img.isNull()) {
        d->failed = true;
    } else {
        // Das Format, das Textur und QPixmap ohne weitere Umwandlung nehmen.
        const QImage::Format f = img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
        if (img.format() != f) img.convertTo(f);
        d->size = img.size();
        if (m_software) {
            d->stage = std::move(img);
        } else {
            d->rects = tileGrid(img.size(), TiledImageItem::kGpuTile, 1);
            d->tiles = tileViews(img, d->rects);
        }
    }
    if (m_sh->decodeGen.load() != m_gen) return;
    m_sh->post([d](TiledImageItem* o) { o->adoptDecoded(d); });
}

TiledImageItem::TiledImageItem(QQuickItem* parent) : QQuickItem(parent), m_shared(std::make_shared<Shared>()) {
    setFlag(ItemHasContents);
    m_shared->owner = this;
    m_settle.setSingleShot(true);
    // Die Stufe darunter zeigt in Ruhe dasselbe bilineare Bild; Kacheln sparen erst beim naechsten Schwenk. Beim
    // Rastung-fuer-Rastung-Zoom (250 ms) rechnete ein kuerzerer Wert je Rastung einen ganzen Satz umsonst.
    m_settle.setInterval(400);
    connect(&m_settle, &QTimer::timeout, this, &TiledImageItem::planCpuTiles);
    connect(this, &QQuickItem::smoothChanged, this, &QQuickItem::update);
}

TiledImageItem::~TiledImageItem() {
    std::lock_guard<std::mutex> g(m_shared->lock);
    m_shared->owner = nullptr;
    ++m_shared->decodeGen;
    ++m_shared->cpuEpoch;
    ++m_shared->levelGen;
}

bool TiledImageItem::software() const {
    return window() && window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
}

void TiledImageItem::setSource(const QString& s) {
    const QString local = mg::toLocalPath(s);
    if (local == m_source) return;
    m_source = local;
    ++m_srcGen;
    emit sourceChanged();
    setCurrent(false);
    // Die Kacheln des alten Bildes bleiben, bis das neue kommt: QML haelt die Flaeche solange auf ihrem Mass.
    ++m_shared->levelGen;
    m_levelWant = 0;
    update();
    requestDecode();
}

void TiledImageItem::setDecodeSize(qreal d) {
    if (qFuzzyCompare(d + 1, m_decodeSize + 1)) return;
    m_decodeSize = d;
    emit decodeSizeChanged();
    if (!m_source.isEmpty()) requestDecode();
}

void TiledImageItem::setMipmap(bool m) {
    if (m == m_mipmap) return;
    m_mipmap = m;
    emit mipmapChanged();
}

void TiledImageItem::setViewRect(const QRectF& r) {
    if (r == m_viewRect) return;
    m_viewRect = r;
    emit viewRectChanged();
    if (!m_isSoftware) return;
    if (!m_settle.isActive()) planCpuTiles();
    update();                                       // ob die Stufe unter der neuen Sicht wieder gebraucht wird
}

void TiledImageItem::setStatus(Status s) {
    if (s == m_status) return;
    m_status = s;
    emit statusChanged();
}

void TiledImageItem::setCurrent(bool c) {
    if (c == m_current) return;
    m_current = c;
    emit currentChanged();
}

void TiledImageItem::requestDecode() {
    if (m_decodeQueued) return;
    m_decodeQueued = true;
    QMetaObject::invokeMethod(this, &TiledImageItem::startDecode, Qt::QueuedConnection);
}

void TiledImageItem::startDecode() {
    m_decodeQueued = false;
    const quint64 gen = ++m_shared->decodeGen;
    if (m_source.isEmpty()) {
        m_stageSrcGen = m_srcGen;
        dropLevel();
        m_cpuStage = QImage();
        m_rest = QImage();
        m_gpuRects.clear();
        m_gpuTiles.clear();
        m_stage = QSize();
        ++m_stageGen;
        resetCpuTiles();
        setImplicitSize(0, 0);
        setStatus(Null);
        setCurrent(true);
        update();
        return;
    }
    if (!window()) return;                         // itemChange holt es nach
    m_isSoftware = software();
    // Gerundet wie deviceSize(): eine eingepasste Stufe trifft die Flaeche dann aufs Pixel.
    const int longSide = m_decodeSize > 0 ? qCeil(m_decodeSize * window()->effectiveDevicePixelRatio() - 0.01) : 0;
    if (!std::exchange(m_noReuse, false) && reuseRest(gen, longSide)) return;
    setStatus(Loading);
    pool()->start(new DecodeTask(m_shared, gen, m_srcGen, m_source, longSide, m_isSoftware));
}

// Zurueck auf Einpassen: dieselbe Groesse aus derselben Datei liefert dieselben Pixel, die behaltene Stufe genuegt.
bool TiledImageItem::reuseRest(quint64 gen, int longSide) {
    if (m_fullSrcGen != m_srcGen || m_stageSrcGen != m_srcGen) return false;
    const QSize want = stageSize(m_full, longSide);
    if (want == m_full) return false;
    if (m_isSoftware) {
        if (m_rest.isNull() || m_restSrcGen != m_srcGen || m_rest.size() != want) return false;
        auto d = std::make_shared<Decoded>();
        d->gen = gen;
        d->srcGen = m_srcGen;
        d->size = want;
        d->full = m_full;
        d->stage = m_rest;
        adoptDecoded(d);
        return true;
    }
    if (m_gpuRestSrcGen != m_srcGen || m_gpuRest != want) return false;
    // Die Texturen liegen im Szenengraph; updatePaintNode haengt sie um oder dekodiert doch, wenn sie fehlen.
    m_promoteGen = ++m_stageGen;
    m_stage = want;
    m_stageReduced = true;
    m_gpuRects.clear();
    m_gpuTiles.clear();
    setImplicitSize(want.width(), want.height());
    setStatus(Ready);
    update();
    return true;
}

void TiledImageItem::adoptDecoded(const std::shared_ptr<Decoded>& d) {
    if (d->gen != m_shared->decodeGen.load()) return;
    ++m_stageGen;
    ++m_baseGen;
    m_stageSrcGen = d->srcGen;
    m_full = d->full;
    m_fullSrcGen = d->srcGen;
    dropLevel();
    if (d->failed) {
        m_cpuStage = QImage();
        m_rest = QImage();
        m_gpuRects.clear();
        m_gpuTiles.clear();
        m_stage = QSize();
        resetCpuTiles();
        setStatus(Error);
        setCurrent(true);
        update();
        return;
    }
    m_stage = d->size;
    m_stageReduced = d->size != d->full;
    if (m_isSoftware) {
        m_cpuStage = std::move(d->stage);
        setImplicitSize(m_stage.width(), m_stage.height());
        // Software zeigt die Stufe im naechsten Bild; QML stellt die Flaeche jetzt um, geometryChange rechnet schon
        // mit der neuen Stufe.
        if (d->srcGen == m_srcGen) setCurrent(true);
        m_cpuDevice = deviceSize();
        if (m_stageReduced && nearSize(m_cpuDevice, m_cpuStage.size())) {
            m_rest = m_cpuStage;                    // teilt den Speicher, solange sie die Stufe ist
            m_restSrcGen = d->srcGen;
        } else if (m_restSrcGen != d->srcGen) {
            m_rest = QImage();
        }
        resetCpuTiles();
        syncRest();
        updateLevel();
    } else {
        m_gpuRects = std::move(d->rects);
        m_gpuTiles = std::move(d->tiles);
        setImplicitSize(m_stage.width(), m_stage.height());
    }
    setStatus(Ready);
    if (m_isSoftware) m_settle.start();
    update();
}

QSize TiledImageItem::deviceSize() const {
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    return QSize(qCeil(width() * dpr - 0.01), qCeil(height() * dpr - 0.01));
}

QRect TiledImageItem::deviceView() const {
    const QRect all(QPoint(0, 0), m_cpuDevice);
    if (m_viewRect.isEmpty()) return all;
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    return QRectF(m_viewRect.x() * dpr, m_viewRect.y() * dpr, m_viewRect.width() * dpr, m_viewRect.height() * dpr)
               .toAlignedRect() & all;
}

void TiledImageItem::resetCpuTiles() {
    m_cpuEpoch = ++m_shared->cpuEpoch;
    m_cache.clear();
    m_pending.clear();
    m_dropped.clear();
    emit tilesReadyChanged();
    update();
}

int TiledImageItem::wantShift() const {
    if (m_cpuStage.isNull() || m_cpuDevice.isEmpty()) return 0;
    const double s = double(m_cpuDevice.width()) / m_cpuStage.width();
    int n = 0;
    while (n < 16 && s * double(2 << n) <= 1.0) ++n;
    return n;
}

bool TiledImageItem::restActive() const {
    return !m_rest.isNull() && m_restSrcGen == m_stageSrcGen && nearSize(m_cpuDevice, m_rest.size());
}

void TiledImageItem::syncRest() {
    if (restActive() == m_restShown) return;
    m_restShown = !m_restShown;
    ++m_baseGen;
    update();
}

void TiledImageItem::dropLevel() {
    ++m_shared->levelGen;
    m_levelWant = 0;
    if (m_cpuLevel.isNull()) return;
    m_cpuLevel = QImage();
    m_levelShift = 0;
    ++m_baseGen;
    update();
}

void TiledImageItem::updateLevel() {
    if (!m_isSoftware || foreign()) return;
    if (restActive()) {
        if (m_levelWant) {
            ++m_shared->levelGen;
            m_levelWant = 0;
        }
        return;
    }
    const int n = wantShift();
    if (n == (m_cpuLevel.isNull() ? 0 : m_levelShift)) {
        if (m_levelWant) {
            ++m_shared->levelGen;
            m_levelWant = 0;
        }
        return;
    }
    if (n == 0) {
        dropLevel();
        return;
    }
    if (n == m_levelWant) return;
    m_levelWant = n;
    // Beim Herauszoomen aus der vorhandenen Ebene: ein Viertel der Pixel je Schritt statt jedes Mal die volle Stufe.
    const bool chain = !m_cpuLevel.isNull() && n > m_levelShift;
    pool()->start(new LevelTask(m_shared, ++m_shared->levelGen, n, chain ? n - m_levelShift : n,
                                chain ? m_cpuLevel : m_cpuStage));
}

void TiledImageItem::adoptLevel(quint64 gen, int shift, QImage img) {
    if (gen != m_shared->levelGen.load()) return;
    m_levelWant = 0;
    m_cpuLevel = std::move(img);
    m_levelShift = shift;
    ++m_baseGen;
    resetCpuTiles();
    if (!m_settle.isActive()) planCpuTiles();
}

void TiledImageItem::planCpuTiles() {
    if (!m_isSoftware || m_cpuStage.isNull() || m_cpuDevice.isEmpty() || foreign()) return;
    if (wantShift() != (m_cpuLevel.isNull() ? 0 : m_levelShift)) return;   // adoptLevel plant neu
    const QImage& source = cpuSource();
    if (nearSize(m_cpuDevice, source.size())) return;
    const QRect view = deviceView();
    if (view.isEmpty()) return;
    const int t = kCpuTile;
    const int cols = (m_cpuDevice.width() + t - 1) / t, rows = (m_cpuDevice.height() + t - 1) / t;
    const int c0 = std::max(0, view.left() / t - 1), c1 = std::min(cols - 1, view.right() / t + 1);
    const int r0 = std::max(0, view.top() / t - 1), r1 = std::min(rows - 1, view.bottom() / t + 1);
    std::unordered_set<quint64> needed;
    std::vector<quint64> ring;
    bool visibleMissing = false;
    for (int ty = r0; ty <= r1; ++ty)
        for (int tx = c0; tx <= c1; ++tx) {
            const quint64 key = tileKey(tx, ty);
            needed.insert(key);
            if (auto it = m_cache.find(key); it != m_cache.end()) {
                it->second.lastUse = ++m_useClock;
                continue;
            }
            if (!tileArea(key, m_cpuDevice).intersects(view)) {
                ring.push_back(key);
                continue;
            }
            visibleMissing = true;
            if (m_pending.insert(key).second)
                pool()->start(new TileTask(m_shared, m_cpuEpoch, key, source, m_cpuDevice));
        }
    // Der Ring erst, wenn die Sicht steht: er ist doppelt so gross und wird beim naechsten Zoom ohnehin verworfen.
    if (!visibleMissing)
        for (quint64 key : ring)
            if (m_pending.insert(key).second)
                pool()->start(new TileTask(m_shared, m_cpuEpoch, key, source, m_cpuDevice));
    // Drei Sichtflaechen, mindestens 32 MB: der Ring um die Sicht passt, Verlassenes faellt nach Alter heraus.
    const qsizetype budget = std::max<qsizetype>(32 << 20, qsizetype(3) * view.width() * view.height() * 4);
    qsizetype total = 0;
    for (const auto& [k, c] : m_cache) total += c.image.sizeInBytes();
    while (total > budget) {
        auto lru = m_cache.end();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (!needed.count(it->first) && (lru == m_cache.end() || it->second.lastUse < lru->second.lastUse)) lru = it;
        if (lru == m_cache.end()) break;
        total -= lru->second.image.sizeInBytes();
        m_dropped.push_back(lru->first);
        m_cache.erase(lru);
    }
    if (!m_dropped.empty()) update();
}

void TiledImageItem::adoptTile(quint64 epoch, quint64 key, QImage img) {
    if (epoch != m_cpuEpoch) return;
    m_pending.erase(key);
    m_cache[key] = CpuTile{std::move(img), ++m_useClock};
    emit tilesReadyChanged();
    // Ein Bild erst, wenn die Sicht vollstaendig ist: je Kachel eines zeichnete die glatte Stufe darunter jedes Mal mit.
    const QRect view = deviceView();
    if (!tileArea(key, m_cpuDevice).intersects(view)) return;
    for (quint64 k : m_pending)
        if (tileArea(k, m_cpuDevice).intersects(view)) return;
    update();
    planCpuTiles();
}

void TiledImageItem::geometryChange(const QRectF& now, const QRectF& before) {
    QQuickItem::geometryChange(now, before);
    if (now.size() == before.size()) return;
    update();
    if (!m_isSoftware || m_cpuStage.isNull()) return;
    const QSize dev = deviceSize();
    if (dev == m_cpuDevice) return;
    m_cpuDevice = dev;
    resetCpuTiles();
    syncRest();
    updateLevel();
    m_settle.start();                               // erst nach dem Zoom rechnen, nicht je Rastung
}

void TiledImageItem::itemChange(ItemChange change, const ItemChangeData& data) {
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange && data.window && !m_source.isEmpty() && m_status != Ready) requestDecode();
    if (change == ItemDevicePixelRatioHasChanged && m_isSoftware && !m_cpuStage.isNull()) {
        m_cpuDevice = deviceSize();
        resetCpuTiles();
        syncRest();
        updateLevel();
        planCpuTiles();
    }
}

QSGNode* TiledImageItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* root = static_cast<Root*>(old);
    QQuickWindow* win = window();
    if (!root) {
        root = new Root;
        // Nach verworfenem Szenengraph sind hochgeladene Kacheln weg und ihre CPU-Kopie schon frei: neu dekodieren.
        const bool lost = !m_isSoftware && !m_stage.isEmpty()
                          && (m_gpuTiles.empty() || std::any_of(m_gpuTiles.begin(), m_gpuTiles.end(),
                                                                [](const QImage& i) { return i.isNull(); }));
        if (lost) QMetaObject::invokeMethod(this, &TiledImageItem::requestDecode, Qt::QueuedConnection);
    }
    const bool smooth = this->smooth();

    if (!m_isSoftware) {
        if (m_stage.isEmpty()) {
            root->dropStage(root->old);
            root->dropStage(root->cur);
            root->dropStage(root->rest);
            m_gpuRest = QSize();
            return root;
        }
        if (root->cur.gen != m_stageGen && m_promoteGen == m_stageGen) {
            Root::Stage& rest = root->rest;
            if (rest.xf && rest.srcGen == m_stageSrcGen && rest.size == m_stage) {
                root->dropStage(root->old);
                root->dropStage(root->cur);
                root->cur = std::exchange(rest, Root::Stage{});
                root->cur.gen = m_stageGen;
                root->hang(root->cur, true);
            } else {
                m_promoteGen = 0;
                m_noReuse = true;
                QMetaObject::invokeMethod(this, &TiledImageItem::requestDecode, Qt::QueuedConnection);
            }
        }
        if (root->cur.gen != m_stageGen && !m_gpuTiles.empty()) {
            Root::Stage& was = root->cur;
            if (!was.shown || !root->hung(was)) {
                root->dropStage(was);
            } else if (was.wasExact && was.reduced && was.srcGen == m_stageSrcGen) {
                root->dropStage(root->rest);        // sie steht weiter, bis die neue gezeigt wird
                root->rest = was;
            } else {
                root->dropStage(root->old);
                root->old = was;
            }
            Root::Stage& c = root->cur = Root::Stage{};
            c.fade = new QSGOpacityNode;
            c.fade->setOpacity(kHidden);
            c.xf = new QSGTransformNode;
            c.fade->appendChildNode(c.xf);
            root->appendChildNode(c.fade);
            c.gen = m_stageGen;
            c.srcGen = m_stageSrcGen;
            c.size = m_stage;
            c.reduced = m_stageReduced;
        }
        Root::Stage& c = root->cur;
        Root::Stage& rest = root->rest;
        if (c.xf && !c.done && c.gen == m_stageGen) {
            qsizetype budget = kUploadBytes;
            while (c.next < m_gpuTiles.size() && budget > 0) {
                const size_t i = c.next++;
                QImage& img = m_gpuTiles[i];
                budget -= img.sizeInBytes();
                QSGImageNode* n = win->createImageNode();
                n->setTexture(win->createTextureFromImage(img));
                n->setOwnsTexture(true);
                const TileRect& r = m_gpuRects[i];
                n->setRect(QRectF(r.inner));
                n->setSourceRect(QRectF(r.inner.topLeft() - r.outer.topLeft(), r.inner.size()));
                n->setMipmapFiltering(m_mipmap ? QSGTexture::Linear : QSGTexture::None);
                c.xf->appendChildNode(n);
                img = QImage();
            }
            c.filter = -1;                          // die neuen Kacheln bekommen den Filter unten
            if (c.next >= m_gpuTiles.size()) {
                c.done = true;
                m_gpuTiles.clear();
                m_gpuRects.clear();
            } else {
                QMetaObject::invokeMethod(this, &QQuickItem::update, Qt::QueuedConnection);
            }
        }
        // Ein anderes Bild erst, wenn QML die Flaeche auf sein Mass gestellt hat (`current`): bis dahin steht das
        // alte unbewegt. Ein liegengebliebenes Zwischenbild wird nie gezeigt.
        if (c.xf && c.done && !c.shown) {
            const bool other = (root->old.xf && root->old.srcGen != c.srcGen)
                               || (root->hung(rest) && rest.srcGen != c.srcGen);
            if (!other || (c.srcGen == m_srcGen && m_current)) {
                c.fade->setOpacity(1.0);
                c.shown = true;
                root->dropStage(root->old);
                if (rest.xf && (rest.srcGen != c.srcGen || nearSize(rest.size, c.size))) root->dropStage(rest);
            }
            if (c.srcGen == m_srcGen && !m_current && !c.asked) {
                c.asked = true;
                QMetaObject::invokeMethod(
                    this, [this, g = c.srcGen] {
                        if (g == m_srcGen) setCurrent(true);
                        update();
                    },
                    Qt::QueuedConnection);
            }
        }
        const qreal dpr = win->effectiveDevicePixelRatio();
        const auto shownSize = [&](const Root::Stage& s) {
            const QRectF r = s.srcGen != m_srcGen ? fitForeign(width(), height(), s.size) : QRectF(0, 0, width(), height());
            return std::pair{r, QSize(qCeil(r.width() * dpr - 0.01), qCeil(r.height() * dpr - 0.01))};
        };
        // Solange die neue Stufe hochlaedt, bleibt die gezeigte; danach entscheidet das Mass.
        if (rest.xf && c.shown) {
            const bool on = rest.srcGen == c.srcGen && nearSize(shownSize(rest).second, rest.size);
            root->hang(rest, on);
            root->hang(c, !on);
        }
        for (Root::Stage* s : {&root->cur, &root->old, &root->rest}) {
            if (!s->xf) continue;
            const auto [r, shown] = shownSize(*s);
            QMatrix4x4 m;
            m.translate(float(r.x()), float(r.y()));
            m.scale(float(r.width() / s->size.width()), float(r.height() / s->size.height()));
            if (s->xf->matrix() != m) s->xf->setMatrix(m);
            const int f = !smooth || nearSize(shown, s->size) ? 1 : 0;
            if (s->shown && nearSize(shown, s->size)) s->wasExact = true;
            if (f != s->filter) {
                setFilter(s->xf, f);
                s->filter = f;
            }
        }
        m_gpuRest = rest.xf ? rest.size : QSize();
        m_gpuRestSrcGen = rest.srcGen;
        return root;
    }

    if (root->epoch != m_cpuEpoch) {
        for (auto& [k, n] : root->tiles) delete n;
        root->tiles.clear();
        root->epoch = m_cpuEpoch;
    }
    for (quint64 k : m_dropped)
        if (auto it = root->tiles.find(k); it != root->tiles.end()) {
            delete it->second;
            root->tiles.erase(it);
        }
    m_dropped.clear();
    if (root->base && (root->baseGen != m_baseGen || m_cpuStage.isNull())) {
        delete root->base;
        root->base = nullptr;
    }
    if (m_cpuStage.isNull()) return root;
    const QImage& source = cpuSource();
    if (!root->base) {
        root->base = win->createImageNode();
        root->base->setTexture(win->createTextureFromImage(source));
        root->base->setOwnsTexture(true);
        root->baseGen = m_baseGen;
    }
    // Auch fremd (beim Wechsel gehalten): sonst taste das stehende Bild ploetzlich bilinear neu ab und wirkte weicher.
    const bool direct = nearSize(m_cpuDevice, source.size());
    root->base->setRect(foreign() ? fitForeign(width(), height(), source.size()) : QRectF(0, 0, width(), height()));
    root->base->setFiltering(direct || !smooth ? QSGTexture::Nearest : QSGTexture::Linear);
    const qreal dpr = win->effectiveDevicePixelRatio();
    for (const auto& [k, c] : m_cache) {
        if (root->tiles.count(k)) continue;
        QSGImageNode* n = win->createImageNode();
        n->setTexture(win->createTextureFromImage(c.image));
        n->setOwnsTexture(true);
        const QRect a = tileArea(k, m_cpuDevice);
        n->setRect(QRectF(a.x() / dpr, a.y() / dpr, a.width() / dpr, a.height() / dpr));
        n->setFiltering(QSGTexture::Nearest);
        root->appendChildNode(n);
        root->tiles.emplace(k, n);
    }
    // Die Stufe darunter nur, solange eine sichtbare Kachel fehlt: sonst zeichnete sie jedes Bild mit.
    bool covered = !direct && !root->tiles.empty();
    if (covered) {
        const QRect view = deviceView();
        const int t = kCpuTile;
        for (int ty = view.top() / t; covered && ty <= view.bottom() / t; ++ty)
            for (int tx = view.left() / t; covered && tx <= view.right() / t; ++tx)
                covered = root->tiles.count(tileKey(tx, ty)) > 0;
    }
    // Gestreckt rechnet der Rasterer je Bild bilinear ueber die ganze Sicht (gemessen 9-10 ms CPU auf vier Faeden);
    // dieselbe Rechnung wie die Kacheln kostet einen Bruchteil, und der Wechsel auf die Kacheln bleibt pixelgleich.
    // Erst mit der passenden Ebene: darunter laese sie fast die ganze grosse Stufe kalt aus dem Speicher (gemessen bis
    // 9 ms auf einem Faden); die wenigen Bilder bis dahin zeichnet der Rasterer wie bisher auf mehreren.
    const bool levelReady = wantShift() == (m_cpuLevel.isNull() ? 0 : m_levelShift);
    const QRect area = !covered && !direct && smooth && levelReady && !foreign() && !m_viewRect.isEmpty()
                           ? deviceView() : QRect();
    if (area.isEmpty()) {
        delete root->view;
        root->view = nullptr;
        root->viewImage = QImage();
    } else if (!root->view || root->viewGen != m_baseGen || root->viewDevice != m_cpuDevice || root->viewArea != area) {
        // Derselbe Knoten bleibt (ein neuer baut die Zeichenliste des ganzen Fensters neu); seine alte Textur geht
        // zuerst, dann ist das Bild ungeteilt und wird ohne Kopie und ohne frische Seiten beschrieben.
        const bool fresh = !root->view;
        if (fresh) {
            root->view = win->createImageNode();
            root->view->setOwnsTexture(true);
            root->view->setFiltering(QSGTexture::Nearest);
        } else {
            root->view->setTexture(win->createTextureFromImage(QImage(1, 1, QImage::Format_RGB32)));
        }
        if (root->viewImage.size() != area.size() || root->viewImage.format() != displayFormat(source))
            root->viewImage = QImage(area.size(), displayFormat(source));
        stretchInto(root->viewImage, source, m_cpuDevice, area.topLeft());
        root->view->setTexture(win->createTextureFromImage(root->viewImage));
        root->view->setRect(QRectF(area.x() / dpr, area.y() / dpr, area.width() / dpr, area.height() / dpr));
        if (fresh) root->prependChildNode(root->view);   // erst mit Textur: der Rasterer liest sie beim Einhaengen
        root->viewGen = m_baseGen;
        root->viewDevice = m_cpuDevice;
        root->viewArea = area;
    }
    const bool hideBase = covered || root->view;
    if (hideBase && root->base->parent()) root->removeChildNode(root->base);
    else if (!hideBase && !root->base->parent()) root->prependChildNode(root->base);
    return root;
}

} // namespace mg::image
