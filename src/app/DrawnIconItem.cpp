#include "app/DrawnIconItem.h"

#include "app/DrawnIconData.h"

#include <QFont>
#include <QHash>
#include <QPointer>
#include <QPolygonF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>

using namespace mg::icons;

namespace {

//  Je Engine einmal uebersetzt: ohne Bindungen im QML bleibt je Instanz nur das reine Erzeugen.
class IconTemplates : public QObject {
public:
    explicit IconTemplates(QQmlEngine* engine) : QObject(engine), m_engine(engine) { setObjectName(kName); }

    QQmlEngine* engine() const { return m_engine; }

    static IconTemplates* of(QQmlEngine* engine) {
        static QPointer<IconTemplates> zuletzt;
        if (zuletzt && zuletzt->engine() == engine) return zuletzt;
        auto* t = static_cast<IconTemplates*>(engine->findChild<QObject*>(kName, Qt::FindDirectChildrenOnly));
        zuletzt = t ? t : new IconTemplates(engine);
        return zuletzt;
    }

    QObject* rectangle() { return make(m_rectangle, "import QtQuick\nRectangle {}\n"); }
    QObject* text() { return make(m_text, "import QtQuick\nText {}\n"); }
    QObject* stroke() {
        return make(m_stroke, "import QtQuick\nimport QtQuick.Shapes\nShape {\n"
                              "    preferredRendererType: Shape.CurveRenderer\n"
                              "    ShapePath { fillColor: \"transparent\"; capStyle: ShapePath.RoundCap;"
                              " joinStyle: ShapePath.RoundJoin; PathMultiline {} }\n}\n");
    }
    QObject* curve(const Symbol& symbol) {
        QQmlComponent*& c = m_curves[symbol.curve];
        return make(c, curveSource(symbol));
    }

private:
    static inline const QString kName = QStringLiteral("mgDrawnIconTemplates");

    QObject* make(QQmlComponent*& slot, const QByteArray& source) {
        if (!slot) {
            slot = new QQmlComponent(m_engine, this);
            slot->setData(source, QUrl());
        }
        QObject* o = slot->create(m_engine->rootContext());
        if (!o) qWarning("DrawnIconItem: %s", qPrintable(slot->errorString()));
        return o;
    }

    QQmlEngine* m_engine;
    QQmlComponent* m_rectangle = nullptr;
    QQmlComponent* m_text = nullptr;
    QQmlComponent* m_stroke = nullptr;
    QHash<const char*, QQmlComponent*> m_curves;
};

bool isDiagonal(const Stroke& s) { return s.x0 != s.x1 && s.y0 != s.y1; }

//  Wie JavaScripts `Math.round`: `std::round` rundet -2,5 anders und verschoebe Formen um ein Geraetepixel.
double roundHalfUp(double v) { return std::floor(v + 0.5); }

QObject* firstChildWith(const QObject* parent, const char* property) {
    const QObjectList& kinder = parent->children();
    const auto it = std::find_if(kinder.cbegin(), kinder.cend(),
                                 [&](const QObject* k) { return k->metaObject()->indexOfProperty(property) >= 0; });
    return it == kinder.cend() ? nullptr : *it;
}

}  // namespace

DrawnIconItem::DrawnIconItem(QQuickItem* parent) : QQuickItem(parent) {
    setImplicitSize(m_size, m_size);
}

void DrawnIconItem::setName(const QString& name) {
    if (name == m_name) return;
    m_name = name;
    m_symbol = findSymbol(name);
    clearParts();
    buildIfShown();
    emit nameChanged();
}

void DrawnIconItem::setIconSize(qreal size) {
    if (qFuzzyCompare(size, m_size)) return;
    m_size = size;
    setImplicitSize(size, size);
    if (m_built) arrange();
    emit iconSizeChanged();
}

void DrawnIconItem::setColor(const QColor& color) {
    if (color == m_color) return;
    m_color = color;
    for (const Part& part : std::as_const(m_parts)) applyColor(part);
    emit colorChanged();
}

void DrawnIconItem::componentComplete() {
    QQuickItem::componentComplete();
    buildIfShown();
}

void DrawnIconItem::itemChange(ItemChange change, const ItemChangeData& data) {
    QQuickItem::itemChange(change, data);
    switch (change) {
    case ItemVisibleHasChanged:
    case ItemSceneChange:
        buildIfShown();
        break;
    case ItemDevicePixelRatioHasChanged:
        if (m_built) arrange();
        break;
    default:
        break;
    }
}

void DrawnIconItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (m_built && newGeometry.size() != oldGeometry.size()) arrange();
}

QQuickItem* DrawnIconItem::adopt(QObject* created) {
    auto* item = qobject_cast<QQuickItem*>(created);
    if (!item) {
        delete created;
        return nullptr;
    }
    item->setParent(this);
    item->setParentItem(this);
    return item;
}

//  Der groesste Teil der Symbole liegt in zugeklappten Kacheln und Leisten: gebaut wird beim ersten Zeigen und dann behalten.
void DrawnIconItem::buildIfShown() {
    if (m_built || !isComponentComplete() || !window() || !isVisible()) return;
    QQmlEngine* engine = qmlEngine(this);
    if (!engine) return;
    m_built = true;
    if (!m_symbol) return;
    IconTemplates* templates = IconTemplates::of(engine);

    const auto addSolid = [&](bool smooth, double opacity) {
        QQuickItem* r = adopt(templates->rectangle());
        if (!r) return;
        if (smooth) r->setAntialiasing(true);
        if (opacity < 1) r->setOpacity(opacity);
        m_parts.append({r, Kind::Solid});
    };
    const auto addFrame = [&] {
        QQuickItem* r = adopt(templates->rectangle());
        if (!r) return;
        r->setProperty("color", QColor(Qt::transparent));
        r->setAntialiasing(true);
        m_parts.append({r, Kind::Frame, {r->property("border").value<QObject*>(), false, false}});
    };

    //  Reihenfolge = Stapel: achsenparallele Striche, schraege, Rahmen, Ringe, Flaechen, Buchstabe, Kurve.
    for (const Stroke& s : m_symbol->strokes)
        if (!isDiagonal(s)) addSolid(false, 1);
    //  Ein `Shape` fuer alle schraegen Striche halbiert die Ruhekosten je Bild und bleibt bildgleich: Jeder Strich ist ein
    //  eigener Teilpfad, also ohne Verbindungsecke.
    if (std::any_of(m_symbol->strokes.begin(), m_symbol->strokes.end(), isDiagonal)) {
        if (QQuickItem* shape = adopt(templates->stroke())) {
            QObject* path = firstChildWith(shape, "strokeColor");
            m_parts.append({shape, Kind::Stroke, {path, true, false}, {firstChildWith(path, "paths"), false, false}});
        }
    }
    for (size_t i = 0; i < m_symbol->boxes.size() + m_symbol->rings.size(); ++i) addFrame();
    for (const Fill& f : m_symbol->fills) addSolid(true, f.opacity);
    if (m_symbol->letter) {
        if (QQuickItem* t = adopt(templates->text())) {
            t->setProperty("text", QString::fromLatin1(m_symbol->letter));
            m_parts.append({t, Kind::Letter});
            //  Waehrend des Aufbaus der Eltern steht die Textbreite noch nicht fest, sie kommt spaeter als Signal.
            connect(t, &QQuickItem::widthChanged, this, &DrawnIconItem::placeLetter);
            connect(t, &QQuickItem::heightChanged, this, &DrawnIconItem::placeLetter);
        }
    }
    if (m_symbol->curve) {
        if (QQuickItem* shape = adopt(templates->curve(*m_symbol))) {
            shape->setTransformOrigin(QQuickItem::TopLeft);
            Part part{shape, Kind::Curve};
            Path* frei = &part.first;
            for (QObject* k : shape->children()) {
                if (k->metaObject()->indexOfProperty("strokeColor") < 0) continue;
                const bool stroke = k->property("strokeColor").value<QColor>().alpha() > 0;
                const bool fill = k->property("fillColor").value<QColor>().alpha() > 0;
                *frei = {k, stroke, fill};
                frei = &part.second;
            }
            m_parts.append(part);
        }
    }
    for (const Part& part : std::as_const(m_parts)) applyColor(part);
    arrange();
}

void DrawnIconItem::clearParts() {
    for (const Part& part : std::as_const(m_parts)) delete part.item;
    m_parts.clear();
    m_built = false;
}

void DrawnIconItem::applyColor(const Part& part) {
    const auto onPath = [&](const Path& p) {
        if (!p.object) return;
        if (p.stroke) p.object->setProperty("strokeColor", m_color);
        if (p.fill) p.object->setProperty("fillColor", m_color);
    };
    switch (part.kind) {
    case Kind::Solid:
    case Kind::Letter:
        part.item->setProperty("color", m_color);
        break;
    case Kind::Frame:
        if (part.first.object) part.first.object->setProperty("color", m_color);
        break;
    case Kind::Stroke:
        onPath(part.first);
        break;
    case Kind::Curve:
        onPath(part.first);
        onPath(part.second);
        break;
    }
}

//  Die Rasterrechnung bleibt je Form Mitte minus halbe Breite: von der Kante gerundet laegen Formen mit gleicher
//  Mittellinie einen halben Pixel auseinander. Eingerastet wird auf Geraetepixel, nicht auf logische.
void DrawnIconItem::arrange() {
    if (!m_symbol) return;
    const qreal windowRatio = window() ? window()->effectiveDevicePixelRatio() : 1;
    const qreal dpr = windowRatio > 0 ? windowRatio : 1;
    const qreal u = m_size / 24;
    const auto q = [dpr](double v) { return roundHalfUp(v * dpr) / dpr; };
    const auto qw = [&](double v) { return std::max(1 / dpr, q(v)); };
    const auto place = [](QQuickItem* i, double x, double y, double w, double h, double radius) {
        i->setPosition(QPointF(x, y));
        i->setSize(QSizeF(w, h));
        i->setProperty("radius", radius);
    };

    qsizetype next = 0;
    const auto take = [&](Kind kind) -> const Part* {
        return next < m_parts.size() && m_parts[next].kind == kind ? &m_parts[next++] : nullptr;
    };

    for (const Stroke& s : m_symbol->strokes) {
        if (isDiagonal(s)) continue;
        const Part* p = take(Kind::Solid);
        if (!p) continue;
        const bool horiz = s.y0 == s.y1;
        const double sw = qw(s.width * u);
        const double len = std::hypot(s.x1 - s.x0, s.y1 - s.y0) * u;
        const double w = horiz ? q(len) + sw : sw, h = horiz ? sw : q(len) + sw;
        place(p->item, q((s.x0 + s.x1) / 2 * u - w / 2), q((s.y0 + s.y1) / 2 * u - h / 2), w, h, std::min(w, h) / 2);
    }
    if (const Part* p = take(Kind::Stroke)) {
        p->item->setSize(size());
        QList<QPolygonF> linien;
        for (const Stroke& s : m_symbol->strokes) {
            if (!isDiagonal(s)) continue;
            if (linien.isEmpty()) p->first.object->setProperty("strokeWidth", s.width * u);
            linien.append(QPolygonF{QPointF(s.x0 * u, s.y0 * u), QPointF(s.x1 * u, s.y1 * u)});
        }
        p->second.object->setProperty("paths", QVariant::fromValue(linien));
    }
    const double bw = qw(u * 2);
    const auto frame = [&](const Part* p, double x, double y, double w, double h, double radius) {
        place(p->item, x, y, w, h, radius);
        if (p->first.object) p->first.object->setProperty("width", bw);
    };
    for (const Box& b : m_symbol->boxes) {
        const Part* p = take(Kind::Frame);
        if (!p) continue;
        const double w = q(b.w * u) + bw, h = q(b.h * u) + bw;
        frame(p, q((b.x + b.w / 2) * u - w / 2), q((b.y + b.h / 2) * u - h / 2), w, h, (b.radius + 1) * u);
    }
    for (const Ring& r : m_symbol->rings) {
        const Part* p = take(Kind::Frame);
        if (!p) continue;
        const double w = q(2 * r.r * u + bw);
        frame(p, q(r.x * u - w / 2), q(r.y * u - w / 2), w, w, w / 2);
    }
    for (const Fill& f : m_symbol->fills) {
        const Part* p = take(Kind::Solid);
        if (!p) continue;
        const double w = q(f.w * u), h = qw(f.h * u);
        place(p->item, q((f.x + f.w / 2) * u - w / 2), q((f.y + f.h / 2) * u - h / 2), w, h, f.radius * u);
    }
    if (m_symbol->letter) {
        if (const Part* p = take(Kind::Letter)) {
            QFont font = p->item->property("font").value<QFont>();
            font.setPixelSize(std::max(6, int(roundHalfUp(m_size * m_symbol->letterSize))));
            font.setBold(true);
            p->item->setProperty("font", font);
            placeLetter();
        }
    }
    if (m_symbol->curve)
        if (const Part* p = take(Kind::Curve)) p->item->setScale(u);
}

void DrawnIconItem::placeLetter() {
    if (!m_symbol || !m_symbol->letter) return;
    for (const Part& part : std::as_const(m_parts)) {
        if (part.kind != Kind::Letter) continue;
        part.item->setPosition(QPointF((width() - part.item->width()) / 2,
                                       (height() - part.item->height()) / 2 + m_symbol->letterY * m_size / 24));
    }
}
