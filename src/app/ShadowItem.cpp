#include "app/ShadowItem.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <QSGTransformNode>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

//  Haelt die gemeinsame Textur der Bildknoten; die Kinder besitzen sie nicht.
struct Wurzel : QSGTransformNode {
    ~Wurzel() override { delete tex; }
    QSGTexture* tex = nullptr;
    quint64 schluessel = 0;
    QSGImageNode* teile[9] = {};
};

quint64 schluesselFuer(int r, int s, int a) {
    return (quint64(quint16(r)) << 32) | (quint64(quint16(s)) << 16) | quint64(quint16(a));
}

}  // namespace

ShadowItem::ShadowItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
}

void ShadowItem::neu() {
    emit changed();
    update();
}

void ShadowItem::setRadius(qreal v) { if (v != m_radius) { m_radius = std::max<qreal>(0, v); neu(); } }
void ShadowItem::setSpread(qreal v) { if (v != m_spread) { m_spread = std::max<qreal>(0, v); neu(); } }
void ShadowItem::setStrength(qreal v) { if (v != m_strength) { m_strength = std::clamp<qreal>(v, 0, 1); neu(); } }
void ShadowItem::setOffsetY(qreal v) { if (v != m_offsetY) { m_offsetY = v; neu(); } }

QImage ShadowItem::shadowImage(int radiusPx, int spreadPx, int strength255) {
    static QMutex sperre;
    static QHash<quint64, QImage> vorrat;
    radiusPx = std::clamp(radiusPx, 0, 200);
    spreadPx = std::clamp(spreadPx, 0, 200);
    strength255 = std::clamp(strength255, 0, 255);
    const quint64 k = schluesselFuer(radiusPx, spreadPx, strength255);
    QMutexLocker halt(&sperre);
    if (const auto it = vorrat.constFind(k); it != vorrat.constEnd()) return *it;

    const int e = spreadPx + radiusPx;
    const int n = 2 * e + 1;
    //  Abgerundetes Rechteck (2r+1)^2 in der Mitte, Kantenglaettung ueber den Abstand zur Kontur.
    std::vector<float> a(size_t(n) * n, 0.f), b(size_t(n) * n, 0.f);
    const float c = n * 0.5f;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float dx = std::max(std::abs(x + 0.5f - c) - 0.5f, 0.f);
            const float dy = std::max(std::abs(y + 0.5f - c) - 0.5f, 0.f);
            const float d = std::sqrt(dx * dx + dy * dy) - float(radiusPx);
            a[size_t(y) * n + x] = std::clamp(0.5f - d, 0.f, 1.f);
        }
    //  Gauss getrennt in x und y; sigma = Weite/3, damit er an der Bildkante ausgelaufen ist.
    if (spreadPx > 0) {
        const float sigma = std::max(0.5f, spreadPx / 3.f);
        std::vector<float> kern(size_t(spreadPx) * 2 + 1);
        float summe = 0;
        for (int i = -spreadPx; i <= spreadPx; ++i)
            summe += kern[size_t(i + spreadPx)] = std::exp(-(i * i) / (2 * sigma * sigma));
        for (float& w : kern) w /= summe;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                float v = 0;
                for (int i = -spreadPx; i <= spreadPx; ++i)
                    if (const int xx = x + i; xx >= 0 && xx < n) v += a[size_t(y) * n + xx] * kern[size_t(i + spreadPx)];
                b[size_t(y) * n + x] = v;
            }
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                float v = 0;
                for (int i = -spreadPx; i <= spreadPx; ++i)
                    if (const int yy = y + i; yy >= 0 && yy < n) v += b[size_t(yy) * n + x] * kern[size_t(i + spreadPx)];
                a[size_t(y) * n + x] = v;
            }
    }
    QImage bild(n, n, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < n; ++y) {
        auto* zeile = reinterpret_cast<QRgb*>(bild.scanLine(y));
        for (int x = 0; x < n; ++x)
            zeile[x] = qRgba(0, 0, 0, int(std::lround(std::clamp(a[size_t(y) * n + x], 0.f, 1.f) * strength255)));
    }
    vorrat.insert(k, bild);
    return bild;
}

QSGNode* ShadowItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* w = static_cast<Wurzel*>(old);
    if (width() <= 0 || height() <= 0 || m_strength <= 0 || !window()) {
        delete w;
        return nullptr;
    }
    const qreal dpr = window()->effectiveDevicePixelRatio();
    const int rp = int(std::lround(m_radius * dpr));
    const int sp = int(std::lround(m_spread * dpr));
    const int ap = int(std::lround(m_strength * 255));
    if (!w) {
        w = new Wurzel;
        for (QSGImageNode*& t : w->teile) {
            t = window()->createImageNode();
            t->setFiltering(QSGTexture::Linear);
            t->setOwnsTexture(false);
            w->appendChildNode(t);
        }
    }
    if (const quint64 k = schluesselFuer(rp, sp, ap); !w->tex || w->schluessel != k) {
        delete w->tex;
        w->tex = window()->createTextureFromImage(shadowImage(rp, sp, ap));
        w->schluessel = k;
        for (QSGImageNode* t : w->teile) t->setTexture(w->tex);
    }

    //  Neun Felder: Ecken in voller Groesse, Kanten und Mitte aus der 1-px-Mitte des Bildes gestreckt.
    const qreal e = (sp + rp) / dpr;
    const int ep = sp + rp;
    const qreal x0 = -m_spread, y0 = -m_spread + m_offsetY;
    const qreal x1 = width() + m_spread, y1 = height() + m_spread + m_offsetY;
    const qreal mx = std::max<qreal>(0, (x1 - x0) - 2 * e), my = std::max<qreal>(0, (y1 - y0) - 2 * e);
    const qreal zx[4] = { x0, x0 + e, x0 + e + mx, x0 + 2 * e + mx };
    const qreal zy[4] = { y0, y0 + e, y0 + e + my, y0 + 2 * e + my };
    const int qx[4] = { 0, ep, ep + 1, 2 * ep + 1 };
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            QSGImageNode* t = w->teile[j * 3 + i];
            //  Die Mitte liegt unter der deckenden Tafel, solange der Versatz den Radius nicht uebersteigt.
            const bool verdeckt = i == 1 && j == 1 && std::abs(m_offsetY) <= m_radius;
            t->setRect(verdeckt ? QRectF() : QRectF(zx[i], zy[j], zx[i + 1] - zx[i], zy[j + 1] - zy[j]));
            t->setSourceRect(QRectF(qx[i], qx[j], qx[i + 1] - qx[i], qx[j + 1] - qx[j]));
        }
    return w;
}
