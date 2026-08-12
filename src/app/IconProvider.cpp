#include "app/IconProvider.h"

#include <QSvgRenderer>
#include <QPainter>
#include <QColor>
#include <QUrlQuery>
#include <QMutexLocker>

IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconProvider::requestImage(const QString& id, QSize* size,
                                  const QSize& requestedSize) {
    //  id zerlegen: "<pfad>?c=%23rrggbb" — der Farbteil ist optional.
    QString path = id;
    QColor tint;
    const int q = id.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        path = id.left(q);
        const QUrlQuery query(id.mid(q + 1));
        const QString c = query.queryItemValue(QStringLiteral("c"),
                                               QUrl::FullyDecoded);
        if (!c.isEmpty())
            tint = QColor(c);
    }

    const int px = requestedSize.width() > 0 ? requestedSize.width()
                 : (requestedSize.height() > 0 ? requestedSize.height() : kDefaultPx);
    const QString key = path + QLatin1Char('|')
                      + (tint.isValid() ? tint.name() : QStringLiteral("-"))
                      + QLatin1Char('|') + QString::number(px);

    {
        QMutexLocker lock(&m_lock);
        const auto it = m_cache.constFind(key);
        if (it != m_cache.cend()) {
            m_lru.removeOne(key);
            m_lru.prepend(key);
            if (size) *size = it->size();
            return *it;
        }
    }

    //  Zeichnen. Die Datei liegt in der Ressource; ein unbekannter Pfad ergibt
    //  ein leeres Bild — QML zeigt dann nichts, statt zu stürzen.
    QSvgRenderer renderer(QStringLiteral(":/") + path);
    if (!renderer.isValid()) {
        if (size) *size = QSize(px, px);
        return {};
    }

    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&p, QRectF(0, 0, px, px));
        //  Umfärben über die DECKUNG: SourceIn behält den Alphakanal der
        //  Zeichnung und ersetzt die Farbe. So bleibt die Kantenglättung heil,
        //  und die Datei selbst darf schwarz bleiben (Regel 29).
        if (tint.isValid()) {
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(img.rect(), tint);
        }
    }

    {
        QMutexLocker lock(&m_lock);
        m_cache.insert(key, img);
        m_lru.prepend(key);
        while (m_lru.size() > kMaxCached) {
            const QString old = m_lru.takeLast();
            m_cache.remove(old);
        }
    }
    if (size) *size = img.size();
    return img;
}
