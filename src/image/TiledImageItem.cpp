#include "image/TiledImageItem.h"

#include "core/PathUtils.h"

#include <QCoreApplication>
#include <QImageReader>
#include <QPainter>
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

QImage renderDisplayTile(const QImage& stage, QSize device, QRect area) {
    QImage out(area.size(), stage.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(QTransform(double(device.width()) / stage.width(), 0, 0,
                              double(device.height()) / stage.height(), -area.x(), -area.y()));
    p.drawImage(QPointF(0, 0), stage);
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
        QSize size;
        size_t next = 0;
        bool done = false;
    };
    Stage cur, old;
    QSGImageNode* base = nullptr;
    quint64 baseGen = 0;
    std::unordered_map<quint64, QSGImageNode*> tiles;
    quint64 epoch = 0;
    bool smooth = true;

    ~Root() override {
        if (base && !base->parent()) delete base;
    }
    void dropStage(Stage& s) {
        delete s.fade;
        s = Stage{};
    }
};

void setFilter(QSGNode* parent, bool smooth) {
    for (QSGNode* n = parent ? parent->firstChild() : nullptr; n; n = n->nextSibling())
        static_cast<QSGImageNode*>(n)->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
}

} // namespace

struct TiledImageItem::Shared {
    std::mutex lock;
    TiledImageItem* owner = nullptr;
    std::atomic<quint64> decodeGen{0};
    std::atomic<quint64> cpuEpoch{0};

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
    QSize size;
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
    DecodeTask(std::shared_ptr<TiledImageItem::Shared> sh, quint64 gen, QString path, int longSide, bool software)
        : m_sh(std::move(sh)), m_gen(gen), m_path(std::move(path)), m_long(longSide), m_software(software) {
        setAutoDelete(true);
    }
    void run() override;

private:
    std::shared_ptr<TiledImageItem::Shared> m_sh;
    quint64 m_gen;
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

void DecodeTask::run() {
    if (m_sh->decodeGen.load() != m_gen) return;
    auto d = std::make_shared<TiledImageItem::Decoded>();
    d->gen = m_gen;
    QImageReader reader(m_path);
    reader.setAutoTransform(false);
    const QSize full = reader.size();
    const QSize want = stageSize(full, m_long);
    if (want.isValid() && want != full) reader.setScaledSize(want);
    QImage img = reader.read();
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
}

bool TiledImageItem::software() const {
    return window() && window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
}

void TiledImageItem::setSource(const QString& s) {
    const QString local = mg::toLocalPath(s);
    if (local == m_source) return;
    m_source = local;
    emit sourceChanged();
    requestDecode();
}

void TiledImageItem::setDecodeSize(int d) {
    if (d == m_decodeSize) return;
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

void TiledImageItem::requestDecode() {
    if (m_decodeQueued) return;
    m_decodeQueued = true;
    QMetaObject::invokeMethod(this, &TiledImageItem::startDecode, Qt::QueuedConnection);
}

void TiledImageItem::startDecode() {
    m_decodeQueued = false;
    const quint64 gen = ++m_shared->decodeGen;
    if (m_source.isEmpty()) {
        m_cpuStage = QImage();
        m_gpuRects.clear();
        m_gpuTiles.clear();
        m_stage = QSize();
        ++m_stageGen;
        resetCpuTiles();
        setImplicitSize(0, 0);
        setStatus(Null);
        update();
        return;
    }
    if (!window()) return;                         // itemChange holt es nach
    m_isSoftware = software();
    setStatus(Loading);
    pool()->start(new DecodeTask(m_shared, gen, m_source, m_decodeSize, m_isSoftware));
}

void TiledImageItem::adoptDecoded(const std::shared_ptr<Decoded>& d) {
    if (d->gen != m_shared->decodeGen.load()) return;
    ++m_stageGen;
    if (d->failed) {
        m_cpuStage = QImage();
        m_gpuRects.clear();
        m_gpuTiles.clear();
        m_stage = QSize();
        resetCpuTiles();
        setStatus(Error);
        update();
        return;
    }
    m_stage = d->size;
    m_gpuDropped = false;
    if (m_isSoftware) {
        m_cpuStage = std::move(d->stage);
        m_cpuDevice = deviceSize();
        resetCpuTiles();
    } else {
        m_gpuRects = std::move(d->rects);
        m_gpuTiles = std::move(d->tiles);
    }
    setImplicitSize(m_stage.width(), m_stage.height());
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

void TiledImageItem::planCpuTiles() {
    if (!m_isSoftware || m_cpuStage.isNull() || m_cpuDevice.isEmpty() || m_cpuDevice == m_cpuStage.size()) return;
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
                pool()->start(new TileTask(m_shared, m_cpuEpoch, key, m_cpuStage, m_cpuDevice));
        }
    // Der Ring erst, wenn die Sicht steht: er ist doppelt so gross und wird beim naechsten Zoom ohnehin verworfen.
    if (!visibleMissing)
        for (quint64 key : ring)
            if (m_pending.insert(key).second)
                pool()->start(new TileTask(m_shared, m_cpuEpoch, key, m_cpuStage, m_cpuDevice));
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
    m_settle.start();                               // erst nach dem Zoom rechnen, nicht je Rastung
}

void TiledImageItem::itemChange(ItemChange change, const ItemChangeData& data) {
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange && data.window && !m_source.isEmpty() && m_status != Ready) requestDecode();
    if (change == ItemDevicePixelRatioHasChanged && m_isSoftware && !m_cpuStage.isNull()) {
        m_cpuDevice = deviceSize();
        resetCpuTiles();
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
            return root;
        }
        if (root->cur.gen != m_stageGen && !m_gpuTiles.empty()) {
            if (root->cur.done) {
                root->dropStage(root->old);
                root->old = root->cur;
            } else {
                root->dropStage(root->cur);
            }
            Root::Stage& c = root->cur = Root::Stage{};
            c.fade = new QSGOpacityNode;
            c.fade->setOpacity(kHidden);
            c.xf = new QSGTransformNode;
            c.fade->appendChildNode(c.xf);
            root->appendChildNode(c.fade);
            c.gen = m_stageGen;
            c.size = m_stage;
        }
        Root::Stage& c = root->cur;
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
                n->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
                n->setMipmapFiltering(m_mipmap ? QSGTexture::Linear : QSGTexture::None);
                c.xf->appendChildNode(n);
                img = QImage();
            }
            if (c.next >= m_gpuTiles.size()) {
                c.done = true;
                c.fade->setOpacity(1.0);
                root->dropStage(root->old);
                m_gpuTiles.clear();
                m_gpuRects.clear();
            } else {
                QMetaObject::invokeMethod(this, &QQuickItem::update, Qt::QueuedConnection);
            }
        }
        for (Root::Stage* s : {&root->cur, &root->old}) {
            if (!s->xf) continue;
            QMatrix4x4 m;
            m.scale(float(width() / s->size.width()), float(height() / s->size.height()));
            if (s->xf->matrix() != m) s->xf->setMatrix(m);
        }
        if (root->smooth != smooth) {
            setFilter(root->cur.xf, smooth);
            setFilter(root->old.xf, smooth);
            root->smooth = smooth;
        }
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
    if (root->base && (root->baseGen != m_stageGen || m_cpuStage.isNull())) {
        delete root->base;
        root->base = nullptr;
    }
    if (m_cpuStage.isNull()) return root;
    if (!root->base) {
        root->base = win->createImageNode();
        root->base->setTexture(win->createTextureFromImage(m_cpuStage));
        root->base->setOwnsTexture(true);
        root->baseGen = m_stageGen;
    }
    const bool direct = m_cpuDevice == m_cpuStage.size();
    root->base->setRect(QRectF(0, 0, width(), height()));
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
    if (covered && root->base->parent()) root->removeChildNode(root->base);
    else if (!covered && !root->base->parent()) root->prependChildNode(root->base);
    return root;
}

} // namespace mg::image
