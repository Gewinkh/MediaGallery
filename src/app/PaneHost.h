#pragma once
#include <QPointer>
#include <QQuickItem>
#include <QUrl>

class PaneController;
class QQmlContext;
class QQmlComponent;

// Erzeugt den QML-Teilbaum einer Haelfte mit EIGENEM Kontext: dieselben Namen
// (mediaModel, galleryModel, Tags, PaneCtl), andere Objekte - sonst muesste jede der
// ~200 Fundstellen ein pane-Argument durchreichen. Nicht "Pane": der Typ gehoert Qt.
class PaneHost : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(PaneController* pane READ pane WRITE setPane NOTIFY paneChanged)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    //  Das erzeugte Element - QML ruft darauf Funktionen der Hälfte auf.
    Q_PROPERTY(QQuickItem* item READ item NOTIFY itemChanged)

public:
    explicit PaneHost(QQuickItem* parent = nullptr);
    ~PaneHost() override;

    PaneController* pane() const { return m_pane; }
    void setPane(PaneController* p);

    QUrl source() const { return m_source; }
    void setSource(const QUrl& url);

    QQuickItem* item() const { return m_item; }

signals:
    void paneChanged();
    void sourceChanged();
    void itemChanged();
    //  Der Teilbaum ließ sich nicht erzeugen (Tippfehler im Pfad, QML-Fehler).
    void loadFailed(const QString& error);

protected:
    void componentComplete() override;
    void geometryChange(const QRectF& newGeom, const QRectF& oldGeom) override;

private:
    void rebuild();
    void clearItem();

    PaneController*        m_pane = nullptr;
    QUrl                   m_source;
    QPointer<QQuickItem>   m_item;
    QQmlContext*           m_context = nullptr;
    QQmlComponent*         m_component = nullptr;
};
