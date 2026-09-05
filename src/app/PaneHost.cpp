#include "app/PaneHost.h"

#include "app/PaneController.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>

PaneHost::PaneHost(QQuickItem* parent) : QQuickItem(parent) {}

PaneHost::~PaneHost() { clearItem(); }

void PaneHost::setPane(PaneController* p) {
    if (m_pane == p) return;
    m_pane = p;
    emit paneChanged();
    rebuild();
}

void PaneHost::setSource(const QUrl& url) {
    if (m_source == url) return;
    m_source = url;
    emit sourceChanged();
    rebuild();
}

void PaneHost::componentComplete() {
    QQuickItem::componentComplete();
    //  Erst jetzt stehen `pane` und `source` beide fest - davor käme je nach
    //  Reihenfolge der Zuweisungen ein halber Aufbau heraus.
    rebuild();
}

// Reihenfolge ist hier alles: der Teilbaum wird verzögert gelöscht, seine Bindungen laufen also noch einen
// Durchlauf weiter. Deshalb geht auch der Kontext über `deleteLater`, und zwar NACH dem Element.
void PaneHost::clearItem() {
    if (m_item) {
        m_item->setParentItem(nullptr);
        m_item->deleteLater();
        m_item = nullptr;
    }
    if (m_component) {
        m_component->deleteLater();
        m_component = nullptr;
    }
    if (m_context) {
        m_context->deleteLater();
        m_context = nullptr;
    }
}

void PaneHost::geometryChange(const QRectF& newGeom, const QRectF& oldGeom) {
    QQuickItem::geometryChange(newGeom, oldGeom);
    //  Der Teilbaum füllt den Host. Bewusst durch Setzen statt durch Anker: der
    //  Host gehört einem anderen Kontext, ein Anker über die Grenze hinweg wäre
    //  eine Bindung auf ein fremdes Element.
    if (m_item) {
        m_item->setWidth(width());
        m_item->setHeight(height());
    }
}

void PaneHost::rebuild() {
    if (!isComponentComplete()) return;      // kommt gleich noch
    clearItem();
    if (!m_pane || m_source.isEmpty()) return;

    QQmlEngine* engine = qmlEngine(this);
    if (!engine) return;

    //  Eigener Kontext UNTER dem der Anwendung: alles Appweite (App, Settings,
    //  Viewer …) bleibt sichtbar, die vier Namen unten zeigen auf DIESE Hälfte.
    m_context = new QQmlContext(qmlContext(this), this);
    m_context->setContextProperty(QStringLiteral("mediaModel"),   m_pane->mediaModelObject());
    m_context->setContextProperty(QStringLiteral("galleryModel"), m_pane->galleryModelObject());
    m_context->setContextProperty(QStringLiteral("Tags"),         m_pane->tagsObject());
    // NICHT "Pane": das ist ein Typ aus QtQuick.Controls, und Typnamen werden beim Übersetzen aufgelöst - sie
    // gewinnen gegen Kontext-Eigenschaften, der Name blieb dann still auf dem Typ stehen.
    m_context->setContextProperty(QStringLiteral("PaneCtl"),      m_pane);

    m_component = new QQmlComponent(engine, m_source, this);
    if (m_component->isError()) {
        emit loadFailed(m_component->errorString());
        return;
    }
    QObject* obj = m_component->create(m_context);
    if (!obj) {
        emit loadFailed(m_component->errorString());
        return;
    }
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        obj->deleteLater();
        emit loadFailed(QStringLiteral("Wurzel ist kein Item: %1").arg(m_source.toString()));
        return;
    }
    QQmlEngine::setObjectOwnership(item, QQmlEngine::CppOwnership);
    //  BEIDES setzen: `setParentItem` hängt es in den Szenengraph, `setParent`
    //  in den QObject-Baum. Ohne das zweite ist der Teilbaum für `findChild`
    //  unsichtbar (Prüfstand-Griffe) und hängt an keiner Lebensdauer.
    item->setParent(this);
    item->setParentItem(this);
    item->setWidth(width());
    item->setHeight(height());
    m_item = item;
    emit itemChanged();
}
