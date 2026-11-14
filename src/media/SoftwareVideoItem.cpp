#include "media/SoftwareVideoItem.h"

#include "media/VideoFrameRgb.h"

#include <QCoreApplication>
#include <QPointer>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGImageNode>
#include <QThreadPool>
#include <QVideoFrame>
#include <QVideoSink>

#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace mg::media {

namespace {

QThreadPool* pool() {
    static QPointer<QThreadPool> p;
    if (!p) {
        p = new QThreadPool(QCoreApplication::instance());
        p->setObjectName(QStringLiteral("mgVideoRgb"));     // Qt benennt die Faeden danach: im Profil vom Rest getrennt
        p->setMaxThreadCount(2);
    }
    return p;
}

}  // namespace

struct SoftwareVideoItem::Stand {
    std::mutex lock;
    SoftwareVideoItem* owner = nullptr;
    QThreadPool* pool = nullptr;      // im GUI-Faden geholt: der Dekoderfaden darf dem App-Objekt kein Kind anlegen
    QVideoFrame wartet;
    std::vector<QImage> frei;     // gezeigte Bilder zurueck: die Textur teilt das juengste noch, das aeltere ist frei
    quint64 gen = 0;          // ein leeres Bild (Stop, neue Quelle) macht laufende Wandlungen ungueltig
    bool laeuft = false;
    std::atomic<bool> abbruch{false};
    std::atomic<int> zielB{0};
    std::atomic<int> zielH{0};

    void abgeben(QImage bild, quint64 vonGen) {
        std::lock_guard<std::mutex> g(lock);
        if (!owner || vonGen != gen) return;
        SoftwareVideoItem* o = owner;
        QMetaObject::invokeMethod(o, [o, bild = std::move(bild)]() mutable {
            o->m_groesse = bild.size();
            o->m_bild = std::move(bild);
            o->m_neu = true;
            o->update();
        }, Qt::QueuedConnection);
    }
};

namespace {

class Wandlung : public QRunnable {
public:
    explicit Wandlung(std::shared_ptr<SoftwareVideoItem::Stand> s) : m_s(std::move(s)) { setAutoDelete(true); }
    void run() override {
        for (;;) {
            QVideoFrame frame;
            QImage ablage;
            quint64 vonGen = 0;
            {
                std::lock_guard<std::mutex> g(m_s->lock);
                if (m_s->abbruch.load() || !m_s->wartet.isValid()) {
                    m_s->laeuft = false;
                    return;
                }
                frame = std::move(m_s->wartet);
                m_s->wartet = QVideoFrame();
                vonGen = m_s->gen;
                for (auto it = m_s->frei.begin(); it != m_s->frei.end(); ++it)
                    if (it->isDetached()) {
                        ablage = std::move(*it);
                        m_s->frei.erase(it);
                        break;
                    }
            }
            const QSize ziel(m_s->zielB.load(), m_s->zielH.load());
            if (ziel.isEmpty()) continue;
            QImage bild = videoBildFuerFlaeche(frame, ziel, std::move(ablage));
            if (bild.isNull()) {
                const QImage roh = frame.toImage();
                if (!roh.isNull()) bild = roh.scaled(ziel, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            if (!bild.isNull() && !m_s->abbruch.load()) m_s->abgeben(std::move(bild), vonGen);
        }
    }

private:
    std::shared_ptr<SoftwareVideoItem::Stand> m_s;
};

}  // namespace

SoftwareVideoItem::SoftwareVideoItem(QQuickItem* parent)
    : QQuickItem(parent), m_sink(new QVideoSink(this)), m_stand(std::make_shared<Stand>()) {
    setFlag(ItemHasContents, true);
    m_stand->owner = this;
    m_stand->pool = pool();
    //  Der Dekoder meldet aus seinem eigenen Faden; hier wird nur gemerkt und hoechstens ein Auftrag gestartet.
    connect(m_sink, &QVideoSink::videoFrameChanged, m_sink, [s = m_stand](const QVideoFrame& frame) {
        if (!frame.isValid()) {
            quint64 neu = 0;
            {
                std::lock_guard<std::mutex> g(s->lock);
                s->wartet = QVideoFrame();
                s->frei.clear();
                neu = ++s->gen;
            }
            s->abgeben(QImage(), neu);
            return;
        }
        std::lock_guard<std::mutex> g(s->lock);
        s->wartet = frame;
        if (s->laeuft || s->abbruch.load()) return;
        s->laeuft = true;
        s->pool->start(new Wandlung(s));
    }, Qt::DirectConnection);
}

SoftwareVideoItem::~SoftwareVideoItem() {
    m_stand->abbruch.store(true);
    std::lock_guard<std::mutex> g(m_stand->lock);
    m_stand->owner = nullptr;
    m_stand->wartet = QVideoFrame();
    m_stand->frei.clear();
}

void SoftwareVideoItem::merkeZiel() {
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    m_stand->zielB.store(qRound(width() * dpr));
    m_stand->zielH.store(qRound(height() * dpr));
}

void SoftwareVideoItem::geometryChange(const QRectF& neu, const QRectF& alt) {
    QQuickItem::geometryChange(neu, alt);
    merkeZiel();
    update();
}

void SoftwareVideoItem::itemChange(ItemChange change, const ItemChangeData& data) {
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) merkeZiel();
}

QSGNode* SoftwareVideoItem::updatePaintNode(QSGNode* alt, UpdatePaintNodeData*) {
    auto* n = static_cast<QSGImageNode*>(alt);
    if (m_groesse.isEmpty() || width() <= 0 || height() <= 0) {
        delete n;
        m_neu = false;
        return nullptr;
    }
    if (!n) {
        n = window()->createImageNode();
        n->setOwnsTexture(true);
    }
    //  Ein neuer Knoten ohne neues Bild (Fenster neu aufgebaut) kann nichts zeigen: die Kopie ist schon frei.
    if (!m_neu && !n->texture()) {
        delete n;
        return nullptr;
    }
    if (m_neu) {
        n->setTexture(window()->createTextureFromImage(m_bild));
        std::lock_guard<std::mutex> g(m_stand->lock);
        if (m_stand->frei.size() >= 2) m_stand->frei.erase(m_stand->frei.begin());
        m_stand->frei.push_back(std::move(m_bild));
        m_bild = QImage();
        m_neu = false;
    }
    const QSizeF passt = QSizeF(m_groesse).scaled(size(), Qt::KeepAspectRatio);
    //  Bei Faktor 1 und Bild in Geraetepixeln ungefiltert auf ganzer Lage: sonst rechnete der Software-Zeichner jedes
    //  Bild bilinear nach. Bei 1,25 wird es dadurch teurer: dann skaliert der GUI-Faden allein statt Qts Hilfsfaeden.
    const bool deckt = window()->effectiveDevicePixelRatio() == 1.0 && QSizeF(m_groesse) == passt;
    if (deckt) {
        const QPointF szene = mapToScene(QPointF(0, 0));
        const QPointF lage = szene + QPointF((width() - passt.width()) / 2, (height() - passt.height()) / 2);
        n->setRect(QRectF(QPointF(std::round(lage.x()), std::round(lage.y())) - szene, passt));
    } else {
        n->setRect(QRectF(QPointF((width() - passt.width()) / 2, (height() - passt.height()) / 2), passt));
    }
    n->setFiltering(deckt ? QSGTexture::Nearest : QSGTexture::Linear);
    return n;
}

}  // namespace mg::media
