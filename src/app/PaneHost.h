#pragma once
#include <QPointer>
#include <QQuickItem>
#include <QUrl>

class PaneController;
class QQmlContext;
class QQmlComponent;

// ─────────────────────────────────────────────────────────────────────────────
//  PaneHost - erzeugt den QML-Teilbaum EINER Galerie-Hälfte mit einem EIGENEN
//  QML-Kontext.
//
//  WARUM: `mediaModel`, `galleryModel` und `Tags` stehen in rund 200 Zeilen
//  quer durch `qml/gallery`, `qml/tags` und `qml/viewer`. Mit zwei Hälften
//  müssen sie je Hälfte auf ANDERE Objekte zeigen. Statt jede dieser Zeilen
//  umzuschreiben (und jedem Blatt ein `pane`-Argument durchzureichen), bekommt
//  jede Hälfte ihren eigenen Kontext: dieselben Namen, andere Objekte. Für die
//  QML-Dateien ändert sich damit NICHTS - sie sahen diese Namen schon vorher
//  als Kontext-Eigenschaften (aus `main.cpp`).
//
//  Gesetzt werden: `mediaModel`, `galleryModel`, `Tags` und `PaneCtl` (der
//  `PaneController` selbst, für alles Ordnerbezogene). NICHT „Pane" - so heißt
//  ein Steuerelement in QtQuick.Controls, und der Typname gewinnt.
//
//  Nutzung in QML:
//      PaneHost { pane: einePaneController; source: "qrc:/qml/gallery/GalleryPane.qml" }
//  Das erzeugte Element füllt den Host; `item` gibt es für Aufrufe von außen.
// ─────────────────────────────────────────────────────────────────────────────
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
